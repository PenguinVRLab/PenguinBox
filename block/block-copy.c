/*
 * block_copy API
 *
 * Copyright (C) 2013 Proxmox Server Solutions
 * Copyright (c) 2019 Virtuozzo International GmbH.
 *
 * Authors:
 *  Dietmar Maurer (dietmar@proxmox.com)
 *  Vladimir Sementsov-Ogievskiy <vsementsov@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "trace.h"
#include "qapi/error.h"
#include "block/block-copy.h"
#include "block/block_int-io.h"
#include "block/dirty-bitmap.h"
#include "block/reqlist.h"
#include "system/block-backend.h"
#include "qemu/units.h"
#include "qemu/co-shared-resource.h"
#include "qemu/coroutine.h"
#include "qemu/ratelimit.h"
#include "block/aio_task.h"
#include "qemu/error-report.h"
#include "qemu/memalign.h"

#define BLOCK_COPY_MAX_COPY_RANGE (16 * MiB)
#define BLOCK_COPY_MAX_BUFFER (1 * MiB)
#define BLOCK_COPY_MAX_MEM (128 * MiB)
#define BLOCK_COPY_MAX_WORKERS 64
#define BLOCK_COPY_SLICE_TIME 100000000ULL
#define BLOCK_COPY_CLUSTER_SIZE_DEFAULT (1 << 16)

typedef enum {
    COPY_READ_WRITE_CLUSTER,
    COPY_READ_WRITE,
    COPY_WRITE_ZEROES,
    COPY_RANGE_SMALL,
    COPY_RANGE_FULL
} BlockCopyMethod;

static coroutine_fn int block_copy_task_entry(AioTask *task);

typedef struct BlockCopyCallState {
    BlockCopyState *s;
    int64_t offset;
    int64_t bytes;
    int max_workers;
    int64_t max_chunk;
    bool ignore_ratelimit;
    BlockCopyAsyncCallbackFunc cb;
    void *cb_opaque;
    Coroutine *co;

    bool finished;
    QemuCoSleep sleep;
    bool cancelled;
    QLIST_ENTRY(BlockCopyCallState) list;

    bool error_is_read;
    int ret;
} BlockCopyCallState;

typedef struct BlockCopyTask {
    AioTask task;

    BlockCopyState *s;
    BlockCopyCallState *call_state;
    BlockCopyMethod method;

    BlockReq req;
} BlockCopyTask;

static int64_t task_end(BlockCopyTask *task)
{
    return task->req.offset + task->req.bytes;
}

typedef struct BlockCopyState {
    BdrvChild *source;
    BdrvChild *target;

    int64_t cluster_size;
    int64_t max_transfer;
    uint64_t len;
    BdrvRequestFlags write_flags;

    CoMutex lock;
    int64_t in_flight_bytes;
    BlockCopyMethod method;
    bool discard_source;
    BlockReqList reqs;
    QLIST_HEAD(, BlockCopyCallState) calls;
    bool skip_unallocated;
    BdrvDirtyBitmap *copy_bitmap;
    ProgressMeter *progress;
    SharedResource *mem;
    RateLimit rate_limit;
} BlockCopyState;

static int64_t block_copy_chunk_size(BlockCopyState *s)
{
    switch (s->method) {
    case COPY_READ_WRITE_CLUSTER:
        return s->cluster_size;
    case COPY_READ_WRITE:
    case COPY_RANGE_SMALL:
        return MIN(MAX(s->cluster_size, BLOCK_COPY_MAX_BUFFER),
                   s->max_transfer);
    case COPY_RANGE_FULL:
        return MIN(MAX(s->cluster_size, BLOCK_COPY_MAX_COPY_RANGE),
                   s->max_transfer);
    default:
        abort();
    }
}

static coroutine_fn BlockCopyTask *
block_copy_task_create(BlockCopyState *s, BlockCopyCallState *call_state,
                       int64_t offset, int64_t bytes)
{
    BlockCopyTask *task;
    int64_t max_chunk;

    QEMU_LOCK_GUARD(&s->lock);
    max_chunk = MIN_NON_ZERO(block_copy_chunk_size(s), call_state->max_chunk);
    if (!bdrv_dirty_bitmap_next_dirty_area(s->copy_bitmap,
                                           offset, offset + bytes,
                                           max_chunk, &offset, &bytes))
    {
        return NULL;
    }

    assert(QEMU_IS_ALIGNED(offset, s->cluster_size));
    bytes = QEMU_ALIGN_UP(bytes, s->cluster_size);

    assert(!reqlist_find_conflict(&s->reqs, offset, bytes));

    bdrv_reset_dirty_bitmap(s->copy_bitmap, offset, bytes);
    s->in_flight_bytes += bytes;

    task = g_new(BlockCopyTask, 1);
    *task = (BlockCopyTask) {
        .task.func = block_copy_task_entry,
        .s = s,
        .call_state = call_state,
        .method = s->method,
    };
    reqlist_init_req(&s->reqs, &task->req, offset, bytes);

    return task;
}

static void coroutine_fn block_copy_task_shrink(BlockCopyTask *task,
                                                int64_t new_bytes)
{
    QEMU_LOCK_GUARD(&task->s->lock);
    if (new_bytes == task->req.bytes) {
        return;
    }

    assert(new_bytes > 0 && new_bytes < task->req.bytes);

    task->s->in_flight_bytes -= task->req.bytes - new_bytes;
    bdrv_set_dirty_bitmap(task->s->copy_bitmap,
                          task->req.offset + new_bytes,
                          task->req.bytes - new_bytes);

    reqlist_shrink_req(&task->req, new_bytes);
}

static void coroutine_fn block_copy_task_end(BlockCopyTask *task, int ret)
{
    QEMU_LOCK_GUARD(&task->s->lock);
    task->s->in_flight_bytes -= task->req.bytes;
    if (ret < 0) {
        bdrv_set_dirty_bitmap(task->s->copy_bitmap, task->req.offset,
                              task->req.bytes);
    }
    if (task->s->progress) {
        progress_set_remaining(task->s->progress,
                               bdrv_get_dirty_count(task->s->copy_bitmap) +
                               task->s->in_flight_bytes);
    }
    reqlist_remove_req(&task->req);
}

void block_copy_state_free(BlockCopyState *s)
{
    if (!s) {
        return;
    }

    ratelimit_destroy(&s->rate_limit);
    bdrv_release_dirty_bitmap(s->copy_bitmap);
    shres_destroy(s->mem);
    g_free(s);
}

static uint32_t block_copy_max_transfer(BdrvChild *source, BdrvChild *target)
{
    return MIN_NON_ZERO(INT_MAX,
                        MIN_NON_ZERO(source->bs->bl.max_transfer,
                                     target->bs->bl.max_transfer));
}

void block_copy_set_copy_opts(BlockCopyState *s, bool use_copy_range,
                              bool compress)
{
    s->write_flags = (s->write_flags & BDRV_REQ_SERIALISING) |
        (compress ? BDRV_REQ_WRITE_COMPRESSED : 0);

    if (s->max_transfer < s->cluster_size) {
        s->method = COPY_READ_WRITE_CLUSTER;
    } else if (compress) {
        s->method = COPY_READ_WRITE_CLUSTER;
    } else {
        s->method = use_copy_range ? COPY_RANGE_SMALL : COPY_READ_WRITE;
    }
}

static int64_t block_copy_calculate_cluster_size(BlockDriverState *target,
                                                 int64_t min_cluster_size,
                                                 Error **errp)
{
    int ret;
    BlockDriverInfo bdi;
    bool target_does_cow;

    GLOBAL_STATE_CODE();
    GRAPH_RDLOCK_GUARD_MAINLOOP();

    min_cluster_size = MAX(min_cluster_size,
                           (int64_t)BLOCK_COPY_CLUSTER_SIZE_DEFAULT);

    target_does_cow = bdrv_backing_chain_next(target);

    ret = bdrv_get_info(target, &bdi);
    if (ret == -ENOTSUP && !target_does_cow) {
        warn_report("The target block device doesn't provide information about "
                    "the block size and it doesn't have a backing file. The "
                    "(default) block size of %" PRIi64 " bytes is used. If the "
                    "actual block size of the target exceeds this value, the "
                    "backup may be unusable",
                    min_cluster_size);
        return min_cluster_size;
    } else if (ret < 0 && !target_does_cow) {
        error_setg_errno(errp, -ret,
            "Couldn't determine the cluster size of the target image, "
            "which has no backing file");
        error_append_hint(errp,
            "Aborting, since this may create an unusable destination image\n");
        return ret;
    } else if (ret < 0 && target_does_cow) {
        return min_cluster_size;
    }

    return MAX(min_cluster_size, bdi.cluster_size);
}

BlockCopyState *block_copy_state_new(BdrvChild *source, BdrvChild *target,
                                     BlockDriverState *copy_bitmap_bs,
                                     const BdrvDirtyBitmap *bitmap,
                                     bool discard_source,
                                     uint64_t min_cluster_size,
                                     Error **errp)
{
    ERRP_GUARD();
    BlockCopyState *s;
    int64_t cluster_size;
    BdrvDirtyBitmap *copy_bitmap;
    bool is_fleecing;

    GLOBAL_STATE_CODE();

    if (min_cluster_size > INT64_MAX) {
        error_setg(errp, "min-cluster-size too large: %" PRIu64 " > %" PRIi64,
                   min_cluster_size, INT64_MAX);
        return NULL;
    } else if (min_cluster_size && !is_power_of_2(min_cluster_size)) {
        error_setg(errp, "min-cluster-size needs to be a power of 2");
        return NULL;
    }

    cluster_size = block_copy_calculate_cluster_size(target->bs,
                                                     (int64_t)min_cluster_size,
                                                     errp);
    if (cluster_size < 0) {
        return NULL;
    }

    copy_bitmap = bdrv_create_dirty_bitmap(copy_bitmap_bs, cluster_size, NULL,
                                           errp);
    if (!copy_bitmap) {
        return NULL;
    }
    bdrv_disable_dirty_bitmap(copy_bitmap);
    if (bitmap) {
        if (!bdrv_merge_dirty_bitmap(copy_bitmap, bitmap, NULL, errp)) {
            error_prepend(errp, "Failed to merge bitmap '%s' to internal "
                          "copy-bitmap: ", bdrv_dirty_bitmap_name(bitmap));
            bdrv_release_dirty_bitmap(copy_bitmap);
            return NULL;
        }
    } else {
        bdrv_set_dirty_bitmap(copy_bitmap, 0,
                              bdrv_dirty_bitmap_size(copy_bitmap));
    }

    bdrv_graph_rdlock_main_loop();
    is_fleecing = bdrv_chain_contains(target->bs, source->bs);
    bdrv_graph_rdunlock_main_loop();

    s = g_new(BlockCopyState, 1);
    *s = (BlockCopyState) {
        .source = source,
        .target = target,
        .copy_bitmap = copy_bitmap,
        .cluster_size = cluster_size,
        .len = bdrv_dirty_bitmap_size(copy_bitmap),
        .write_flags = (is_fleecing ? BDRV_REQ_SERIALISING : 0),
        .mem = shres_create(BLOCK_COPY_MAX_MEM),
        .max_transfer = QEMU_ALIGN_DOWN(
                                    block_copy_max_transfer(source, target),
                                    cluster_size),
    };

    s->discard_source = discard_source;
    block_copy_set_copy_opts(s, false, false);

    ratelimit_init(&s->rate_limit);
    qemu_co_mutex_init(&s->lock);
    QLIST_INIT(&s->reqs);
    QLIST_INIT(&s->calls);

    return s;
}

void block_copy_set_progress_meter(BlockCopyState *s, ProgressMeter *pm)
{
    s->progress = pm;
}

static coroutine_fn int block_copy_task_run(AioTaskPool *pool,
                                            BlockCopyTask *task)
{
    if (!pool) {
        int ret = task->task.func(&task->task);

        g_free(task);
        return ret;
    }

    aio_task_pool_wait_slot(pool);
    if (aio_task_pool_status(pool) < 0) {
        co_put_to_shres(task->s->mem, task->req.bytes);
        block_copy_task_end(task, -ECANCELED);
        g_free(task);
        return -ECANCELED;
    }

    aio_task_pool_start_task(pool, &task->task);

    return 0;
}

static int coroutine_fn GRAPH_RDLOCK
block_copy_do_copy(BlockCopyState *s, int64_t offset, int64_t bytes,
                   BlockCopyMethod *method, bool *error_is_read)
{
    int ret;
    int64_t nbytes = MIN(offset + bytes, s->len) - offset;
    void *bounce_buffer = NULL;

    assert(offset >= 0 && bytes > 0 && INT64_MAX - offset >= bytes);
    assert(QEMU_IS_ALIGNED(offset, s->cluster_size));
    assert(QEMU_IS_ALIGNED(bytes, s->cluster_size));
    assert(offset < s->len);
    assert(offset + bytes <= s->len ||
           offset + bytes == QEMU_ALIGN_UP(s->len, s->cluster_size));
    assert(nbytes < INT_MAX);

    switch (*method) {
    case COPY_WRITE_ZEROES:
        ret = bdrv_co_pwrite_zeroes(s->target, offset, nbytes, s->write_flags &
                                    ~BDRV_REQ_WRITE_COMPRESSED);
        if (ret < 0) {
            trace_block_copy_write_zeroes_fail(s, offset, ret);
            *error_is_read = false;
        }
        return ret;

    case COPY_RANGE_SMALL:
    case COPY_RANGE_FULL:
        ret = bdrv_co_copy_range(s->source, offset, s->target, offset, nbytes,
                                 0, s->write_flags);
        if (ret >= 0) {
            *method = COPY_RANGE_FULL;
            return 0;
        }

        trace_block_copy_copy_range_fail(s, offset, ret);
        *method = COPY_READ_WRITE;
        /* Fall through to read+write with allocated buffer */

    case COPY_READ_WRITE_CLUSTER:
    case COPY_READ_WRITE:

        bounce_buffer = qemu_blockalign(s->source->bs, nbytes);

        ret = bdrv_co_pread(s->source, offset, nbytes, bounce_buffer, 0);
        if (ret < 0) {
            trace_block_copy_read_fail(s, offset, ret);
            *error_is_read = true;
            goto out;
        }

        ret = bdrv_co_pwrite(s->target, offset, nbytes, bounce_buffer,
                             s->write_flags);
        if (ret < 0) {
            trace_block_copy_write_fail(s, offset, ret);
            *error_is_read = false;
            goto out;
        }

    out:
        qemu_vfree(bounce_buffer);
        break;

    default:
        abort();
    }

    return ret;
}

static coroutine_fn int block_copy_task_entry(AioTask *task)
{
    BlockCopyTask *t = container_of(task, BlockCopyTask, task);
    BlockCopyState *s = t->s;
    bool error_is_read = false;
    BlockCopyMethod method = t->method;
    int ret = -1;

    WITH_GRAPH_RDLOCK_GUARD() {
        ret = block_copy_do_copy(s, t->req.offset, t->req.bytes, &method,
                                 &error_is_read);
    }

    WITH_QEMU_LOCK_GUARD(&s->lock) {
        if (s->method == t->method) {
            s->method = method;
        }

        if (ret < 0) {
            if (!t->call_state->ret) {
                t->call_state->ret = ret;
                t->call_state->error_is_read = error_is_read;
            }
        } else if (s->progress) {
            progress_work_done(s->progress, t->req.bytes);
        }
    }
    co_put_to_shres(s->mem, t->req.bytes);
    block_copy_task_end(t, ret);

    if (s->discard_source && ret == 0) {
        int64_t nbytes =
            MIN(t->req.offset + t->req.bytes, s->len) - t->req.offset;
        WITH_GRAPH_RDLOCK_GUARD() {
            bdrv_co_pdiscard(s->source, t->req.offset, nbytes);
        }
    }

    return ret;
}

static coroutine_fn GRAPH_RDLOCK
int block_copy_block_status(BlockCopyState *s, int64_t offset, int64_t bytes,
                            int64_t *pnum)
{
    int64_t num;
    BlockDriverState *base;
    int ret;

    if (qatomic_read(&s->skip_unallocated)) {
        base = bdrv_backing_chain_next(s->source->bs);
    } else {
        base = NULL;
    }

    ret = bdrv_co_block_status_above(s->source->bs, base, offset, bytes, &num,
                                     NULL, NULL);
    if (ret < 0 || num < s->cluster_size) {
        num = s->cluster_size;
        ret = BDRV_BLOCK_ALLOCATED | BDRV_BLOCK_DATA;
    } else if (offset + num == s->len) {
        num = QEMU_ALIGN_UP(num, s->cluster_size);
    } else {
        num = QEMU_ALIGN_DOWN(num, s->cluster_size);
    }

    *pnum = num;
    return ret;
}

static int coroutine_fn GRAPH_RDLOCK
block_copy_is_cluster_allocated(BlockCopyState *s, int64_t offset,
                                int64_t *pnum)
{
    BlockDriverState *bs = s->source->bs;
    int64_t count, total_count = 0;
    int64_t bytes = s->len - offset;
    int ret;

    assert(QEMU_IS_ALIGNED(offset, s->cluster_size));

    while (true) {
        ret = bdrv_co_is_allocated(bs, offset, bytes, &count);
        if (ret < 0) {
            return ret;
        }

        total_count += count;

        if (ret || count == 0) {
            *pnum = DIV_ROUND_UP(total_count, s->cluster_size);
            return ret;
        }

        if (total_count >= s->cluster_size) {
            *pnum = total_count / s->cluster_size;
            return 0;
        }

        offset += count;
        bytes -= count;
    }
}

void block_copy_reset(BlockCopyState *s, int64_t offset, int64_t bytes)
{
    QEMU_LOCK_GUARD(&s->lock);

    bdrv_reset_dirty_bitmap(s->copy_bitmap, offset, bytes);
    if (s->progress) {
        progress_set_remaining(s->progress,
                               bdrv_get_dirty_count(s->copy_bitmap) +
                               s->in_flight_bytes);
    }
}

int64_t coroutine_fn block_copy_reset_unallocated(BlockCopyState *s,
                                                  int64_t offset,
                                                  int64_t *count)
{
    int ret;
    int64_t clusters, bytes;

    ret = block_copy_is_cluster_allocated(s, offset, &clusters);
    if (ret < 0) {
        return ret;
    }

    bytes = clusters * s->cluster_size;

    if (!ret) {
        block_copy_reset(s, offset, bytes);
    }

    *count = bytes;
    return ret;
}

static int coroutine_fn GRAPH_RDLOCK
block_copy_dirty_clusters(BlockCopyCallState *call_state)
{
    BlockCopyState *s = call_state->s;
    int64_t offset = call_state->offset;
    int64_t bytes = call_state->bytes;

    int ret = 0;
    bool found_dirty = false;
    int64_t end = offset + bytes;
    AioTaskPool *aio = NULL;

    assert(bdrv_get_aio_context(s->source->bs) ==
           bdrv_get_aio_context(s->target->bs));

    assert(QEMU_IS_ALIGNED(offset, s->cluster_size));
    assert(QEMU_IS_ALIGNED(bytes, s->cluster_size));

    while (bytes && aio_task_pool_status(aio) == 0 &&
           !qatomic_read(&call_state->cancelled)) {
        BlockCopyTask *task;
        int64_t status_bytes;

        task = block_copy_task_create(s, call_state, offset, bytes);
        if (!task) {
            trace_block_copy_skip_range(s, offset, bytes);
            break;
        }
        if (task->req.offset > offset) {
            trace_block_copy_skip_range(s, offset, task->req.offset - offset);
        }

        found_dirty = true;

        ret = block_copy_block_status(s, task->req.offset, task->req.bytes,
                                      &status_bytes);
        assert(ret >= 0);
        if (status_bytes < task->req.bytes) {
            block_copy_task_shrink(task, status_bytes);
        }
        if (qatomic_read(&s->skip_unallocated) &&
            !(ret & BDRV_BLOCK_ALLOCATED)) {
            block_copy_task_end(task, 0);
            trace_block_copy_skip_range(s, task->req.offset, task->req.bytes);
            offset = task_end(task);
            bytes = end - offset;
            g_free(task);
            continue;
        }
        if (ret & BDRV_BLOCK_ZERO) {
            task->method = COPY_WRITE_ZEROES;
        }

        if (!call_state->ignore_ratelimit) {
            uint64_t ns = ratelimit_calculate_delay(&s->rate_limit, 0);
            if (ns > 0) {
                block_copy_task_end(task, -EAGAIN);
                g_free(task);
                qemu_co_sleep_ns_wakeable(&call_state->sleep,
                                          QEMU_CLOCK_REALTIME, ns);
                continue;
            }
        }

        ratelimit_calculate_delay(&s->rate_limit, task->req.bytes);

        trace_block_copy_process(s, task->req.offset);

        co_get_from_shres(s->mem, task->req.bytes);

        offset = task_end(task);
        bytes = end - offset;

        if (!aio && bytes) {
            aio = aio_task_pool_new(call_state->max_workers);
        }

        ret = block_copy_task_run(aio, task);
        if (ret < 0) {
            goto out;
        }
    }

out:
    if (aio) {
        aio_task_pool_wait_all(aio);

        assert(ret >= 0 || aio_task_pool_status(aio) < 0);
        ret = aio_task_pool_status(aio);

        aio_task_pool_free(aio);
    }

    return ret < 0 ? ret : found_dirty;
}

void block_copy_kick(BlockCopyCallState *call_state)
{
    qemu_co_sleep_wake(&call_state->sleep);
}

static int coroutine_fn GRAPH_RDLOCK
block_copy_common(BlockCopyCallState *call_state)
{
    int ret;
    BlockCopyState *s = call_state->s;

    qemu_co_mutex_lock(&s->lock);
    QLIST_INSERT_HEAD(&s->calls, call_state, list);
    qemu_co_mutex_unlock(&s->lock);

    do {
        ret = block_copy_dirty_clusters(call_state);

        if (ret == 0 && !qatomic_read(&call_state->cancelled)) {
            WITH_QEMU_LOCK_GUARD(&s->lock) {
                ret = reqlist_wait_one(&s->reqs, call_state->offset,
                                       call_state->bytes, &s->lock);
                if (ret == 0) {
                    ret = bdrv_dirty_bitmap_next_dirty(s->copy_bitmap,
                                                       call_state->offset,
                                                       call_state->bytes) >= 0;
                }
            }
        }

    } while (ret > 0 && !qatomic_read(&call_state->cancelled));

    qatomic_store_release(&call_state->finished, true);

    if (call_state->cb) {
        call_state->cb(call_state->cb_opaque);
    }

    qemu_co_mutex_lock(&s->lock);
    QLIST_REMOVE(call_state, list);
    qemu_co_mutex_unlock(&s->lock);

    return ret;
}

static void coroutine_fn block_copy_async_co_entry(void *opaque)
{
    GRAPH_RDLOCK_GUARD();
    block_copy_common(opaque);
}

int coroutine_fn block_copy(BlockCopyState *s, int64_t start, int64_t bytes,
                            bool ignore_ratelimit, uint64_t timeout_ns,
                            BlockCopyAsyncCallbackFunc cb,
                            void *cb_opaque)
{
    int ret;
    BlockCopyCallState *call_state = g_new(BlockCopyCallState, 1);

    *call_state = (BlockCopyCallState) {
        .s = s,
        .offset = start,
        .bytes = bytes,
        .ignore_ratelimit = ignore_ratelimit,
        .max_workers = BLOCK_COPY_MAX_WORKERS,
        .cb = cb,
        .cb_opaque = cb_opaque,
    };

    ret = qemu_co_timeout(block_copy_async_co_entry, call_state, timeout_ns,
                          g_free);
    if (ret < 0) {
        assert(ret == -ETIMEDOUT);
        block_copy_call_cancel(call_state);
        return ret;
    }

    ret = call_state->ret;
    g_free(call_state);

    return ret;
}

BlockCopyCallState *block_copy_async(BlockCopyState *s,
                                     int64_t offset, int64_t bytes,
                                     int max_workers, int64_t max_chunk,
                                     BlockCopyAsyncCallbackFunc cb,
                                     void *cb_opaque)
{
    BlockCopyCallState *call_state = g_new(BlockCopyCallState, 1);

    *call_state = (BlockCopyCallState) {
        .s = s,
        .offset = offset,
        .bytes = bytes,
        .max_workers = max_workers,
        .max_chunk = max_chunk,
        .cb = cb,
        .cb_opaque = cb_opaque,

        .co = qemu_coroutine_create(block_copy_async_co_entry, call_state),
    };

    qemu_coroutine_enter(call_state->co);

    return call_state;
}

void block_copy_call_free(BlockCopyCallState *call_state)
{
    if (!call_state) {
        return;
    }

    assert(qatomic_read(&call_state->finished));
    g_free(call_state);
}

bool block_copy_call_finished(BlockCopyCallState *call_state)
{
    return qatomic_read(&call_state->finished);
}

bool block_copy_call_succeeded(BlockCopyCallState *call_state)
{
    return qatomic_load_acquire(&call_state->finished) &&
           !qatomic_read(&call_state->cancelled) &&
           call_state->ret == 0;
}

bool block_copy_call_failed(BlockCopyCallState *call_state)
{
    return qatomic_load_acquire(&call_state->finished) &&
           !qatomic_read(&call_state->cancelled) &&
           call_state->ret < 0;
}

bool block_copy_call_cancelled(BlockCopyCallState *call_state)
{
    return qatomic_read(&call_state->cancelled);
}

int block_copy_call_status(BlockCopyCallState *call_state, bool *error_is_read)
{
    assert(qatomic_load_acquire(&call_state->finished));
    if (error_is_read) {
        *error_is_read = call_state->error_is_read;
    }
    return call_state->ret;
}

void block_copy_call_cancel(BlockCopyCallState *call_state)
{
    qatomic_set(&call_state->cancelled, true);
    block_copy_kick(call_state);
}

BdrvDirtyBitmap *block_copy_dirty_bitmap(BlockCopyState *s)
{
    return s->copy_bitmap;
}

int64_t block_copy_cluster_size(BlockCopyState *s)
{
    return s->cluster_size;
}

void block_copy_set_skip_unallocated(BlockCopyState *s, bool skip)
{
    qatomic_set(&s->skip_unallocated, skip);
}

void block_copy_set_speed(BlockCopyState *s, uint64_t speed)
{
    ratelimit_set_speed(&s->rate_limit, speed, BLOCK_COPY_SLICE_TIME);

}
