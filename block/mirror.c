/*
 * Image mirroring
 *
 * Copyright Red Hat, Inc. 2012
 *
 * Authors:
 *  Paolo Bonzini  <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/coroutine.h"
#include "qemu/range.h"
#include "trace.h"
#include "block/blockjob_int.h"
#include "block/block_int.h"
#include "block/dirty-bitmap.h"
#include "system/block-backend.h"
#include "qapi/error.h"
#include "qemu/ratelimit.h"
#include "qemu/bitmap.h"
#include "qemu/memalign.h"

#define MAX_IN_FLIGHT 16
#define MAX_IO_BYTES (1 << 20)
#define DEFAULT_MIRROR_BUF_SIZE (MAX_IN_FLIGHT * MAX_IO_BYTES)

typedef struct MirrorBuffer {
    QSIMPLEQ_ENTRY(MirrorBuffer) next;
} MirrorBuffer;

typedef struct MirrorOp MirrorOp;

typedef struct MirrorBlockJob {
    BlockJob common;
    BlockBackend *target;
    BlockDriverState *mirror_top_bs;
    BlockDriverState *base;
    BlockDriverState *base_overlay;

    char *replaces;
    BlockDriverState *to_replace;
    Error *replace_blocker;
    MirrorSyncMode sync_mode;
    BlockMirrorBackingMode backing_mode;
    bool target_is_zero;
    MirrorCopyMode copy_mode;
    BlockdevOnError on_source_error, on_target_error;
    bool actively_synced;
    bool should_complete;
    int64_t granularity;
    size_t buf_size;
    int64_t bdev_length;
    unsigned long *cow_bitmap;
    unsigned long *zero_bitmap;
    BdrvDirtyBitmap *dirty_bitmap;
    BdrvDirtyBitmapIter *dbi;
    uint8_t *buf;
    QSIMPLEQ_HEAD(, MirrorBuffer) buf_free;
    int buf_free_count;

    uint64_t last_pause_ns;
    unsigned long *in_flight_bitmap;
    unsigned in_flight;
    int64_t bytes_in_flight;
    QTAILQ_HEAD(, MirrorOp) ops_in_flight;
    int ret;
    bool unmap;
    int target_cluster_size;
    int max_iov;
    bool initial_zeroing_ongoing;
    int in_active_write_counter;
    int64_t active_write_bytes_in_flight;
    bool prepared;
    bool in_drain;
    bool base_ro;
} MirrorBlockJob;

typedef struct MirrorBDSOpaque {
    MirrorBlockJob *job;
    bool stop;
    bool is_commit;
} MirrorBDSOpaque;

struct MirrorOp {
    MirrorBlockJob *s;
    QEMUIOVector qiov;
    int64_t offset;
    uint64_t bytes;

    int64_t *bytes_handled;
    bool *io_skipped;

    bool is_pseudo_op;
    bool is_active_write;
    bool is_in_flight;
    CoQueue waiting_requests;
    Coroutine *co;
    MirrorOp *waiting_for_op;

    QTAILQ_ENTRY(MirrorOp) next;
};

typedef enum MirrorMethod {
    MIRROR_METHOD_COPY,
    MIRROR_METHOD_ZERO,
    MIRROR_METHOD_DISCARD,
} MirrorMethod;

static BlockErrorAction mirror_error_action(MirrorBlockJob *s, bool read,
                                            int error)
{
    qatomic_set(&s->actively_synced, false);
    if (read) {
        return block_job_error_action(&s->common, s->on_source_error,
                                      true, error);
    } else {
        return block_job_error_action(&s->common, s->on_target_error,
                                      false, error);
    }
}

static void coroutine_fn mirror_wait_on_conflicts(MirrorOp *self,
                                                  MirrorBlockJob *s,
                                                  uint64_t offset,
                                                  uint64_t bytes)
{
    uint64_t self_start_chunk = offset / s->granularity;
    uint64_t self_end_chunk = DIV_ROUND_UP(offset + bytes, s->granularity);
    uint64_t self_nb_chunks = self_end_chunk - self_start_chunk;

    while (find_next_bit(s->in_flight_bitmap, self_end_chunk,
                         self_start_chunk) < self_end_chunk &&
           s->ret >= 0)
    {
        MirrorOp *op;

        QTAILQ_FOREACH(op, &s->ops_in_flight, next) {
            uint64_t op_start_chunk = op->offset / s->granularity;
            uint64_t op_nb_chunks = DIV_ROUND_UP(op->offset + op->bytes,
                                                 s->granularity) -
                                    op_start_chunk;

            if (op == self) {
                continue;
            }

            if (ranges_overlap(self_start_chunk, self_nb_chunks,
                               op_start_chunk, op_nb_chunks))
            {
                if (self) {
                    if (op->waiting_for_op) {
                        continue;
                    }

                    self->waiting_for_op = op;
                }

                qemu_co_queue_wait(&op->waiting_requests, NULL);

                if (self) {
                    self->waiting_for_op = NULL;
                }

                break;
            }
        }
    }
}

static void coroutine_fn mirror_iteration_done(MirrorOp *op, int ret)
{
    MirrorBlockJob *s = op->s;
    struct iovec *iov;
    int64_t chunk_num;
    int i, nb_chunks;

    trace_mirror_iteration_done(s, op->offset, op->bytes, ret);

    s->in_flight--;
    s->bytes_in_flight -= op->bytes;
    iov = op->qiov.iov;
    for (i = 0; i < op->qiov.niov; i++) {
        MirrorBuffer *buf = (MirrorBuffer *) iov[i].iov_base;
        QSIMPLEQ_INSERT_TAIL(&s->buf_free, buf, next);
        s->buf_free_count++;
    }

    chunk_num = op->offset / s->granularity;
    nb_chunks = DIV_ROUND_UP(op->bytes, s->granularity);

    bitmap_clear(s->in_flight_bitmap, chunk_num, nb_chunks);
    QTAILQ_REMOVE(&s->ops_in_flight, op, next);
    if (ret >= 0) {
        if (s->cow_bitmap) {
            bitmap_set(s->cow_bitmap, chunk_num, nb_chunks);
        }
        if (!s->initial_zeroing_ongoing) {
            job_progress_update(&s->common.job, op->bytes);
        }
    }
    qemu_iovec_destroy(&op->qiov);

    qemu_co_queue_restart_all(&op->waiting_requests);
    g_free(op);
}

static void coroutine_fn mirror_write_complete(MirrorOp *op, int ret)
{
    MirrorBlockJob *s = op->s;

    if (ret < 0) {
        BlockErrorAction action;

        bdrv_set_dirty_bitmap(s->dirty_bitmap, op->offset, op->bytes);
        action = mirror_error_action(s, false, -ret);
        if (action == BLOCK_ERROR_ACTION_REPORT && s->ret >= 0) {
            s->ret = ret;
        }
    }

    mirror_iteration_done(op, ret);
}

static void coroutine_fn mirror_read_complete(MirrorOp *op, int ret)
{
    MirrorBlockJob *s = op->s;

    if (ret < 0) {
        BlockErrorAction action;

        bdrv_set_dirty_bitmap(s->dirty_bitmap, op->offset, op->bytes);
        action = mirror_error_action(s, true, -ret);
        if (action == BLOCK_ERROR_ACTION_REPORT && s->ret >= 0) {
            s->ret = ret;
        }

        mirror_iteration_done(op, ret);
        return;
    }

    ret = blk_co_pwritev(s->target, op->offset, op->qiov.size, &op->qiov, 0);
    mirror_write_complete(op, ret);
}

static inline int64_t mirror_clip_bytes(MirrorBlockJob *s,
                                        int64_t offset,
                                        int64_t bytes)
{
    return MIN(bytes, s->bdev_length - offset);
}

static int coroutine_fn mirror_cow_align(MirrorBlockJob *s, int64_t *offset,
                                         uint64_t *bytes)
{
    bool need_cow;
    int ret = 0;
    int64_t align_offset = *offset;
    int64_t align_bytes = *bytes;
    int max_bytes = s->granularity * s->max_iov;

    need_cow = !test_bit(*offset / s->granularity, s->cow_bitmap);
    need_cow |= !test_bit((*offset + *bytes - 1) / s->granularity,
                          s->cow_bitmap);
    if (need_cow) {
        bdrv_round_to_subclusters(blk_bs(s->target), *offset, *bytes,
                                  &align_offset, &align_bytes);
    }

    if (align_bytes > max_bytes) {
        align_bytes = max_bytes;
        if (need_cow) {
            align_bytes = QEMU_ALIGN_DOWN(align_bytes, s->target_cluster_size);
        }
    }
    align_bytes = mirror_clip_bytes(s, align_offset, align_bytes);

    ret = align_offset + align_bytes - (*offset + *bytes);
    *offset = align_offset;
    *bytes = align_bytes;
    assert(ret >= 0);
    return ret;
}

static inline void coroutine_fn
mirror_wait_for_free_in_flight_slot(MirrorBlockJob *s)
{
    MirrorOp *op;

    QTAILQ_FOREACH(op, &s->ops_in_flight, next) {
        if (!op->is_pseudo_op && op->is_in_flight && !op->is_active_write) {
            qemu_co_queue_wait(&op->waiting_requests, NULL);
            return;
        }
    }
    abort();
}

static void coroutine_fn mirror_co_read(void *opaque)
{
    MirrorOp *op = opaque;
    MirrorBlockJob *s = op->s;
    int nb_chunks;
    int ret = -1;
    uint64_t max_bytes;

    max_bytes = s->granularity * s->max_iov;

    op->bytes = MIN(s->buf_size, MIN(max_bytes, op->bytes));
    assert(op->bytes);
    assert(op->bytes < BDRV_REQUEST_MAX_BYTES);
    *op->bytes_handled = op->bytes;

    if (s->cow_bitmap) {
        *op->bytes_handled += mirror_cow_align(s, &op->offset, &op->bytes);
    }
    assert(*op->bytes_handled <= UINT_MAX);
    assert(op->bytes <= s->buf_size);
    assert(QEMU_IS_ALIGNED(op->offset, s->granularity));
    assert(QEMU_IS_ALIGNED(op->bytes, BDRV_SECTOR_SIZE));
    nb_chunks = DIV_ROUND_UP(op->bytes, s->granularity);

    while (s->buf_free_count < nb_chunks) {
        trace_mirror_yield_in_flight(s, op->offset, s->in_flight);
        mirror_wait_for_free_in_flight_slot(s);
    }

    qemu_iovec_init(&op->qiov, nb_chunks);
    while (nb_chunks-- > 0) {
        MirrorBuffer *buf = QSIMPLEQ_FIRST(&s->buf_free);
        size_t remaining = op->bytes - op->qiov.size;

        QSIMPLEQ_REMOVE_HEAD(&s->buf_free, next);
        s->buf_free_count--;
        qemu_iovec_add(&op->qiov, buf, MIN(s->granularity, remaining));
    }

    s->in_flight++;
    s->bytes_in_flight += op->bytes;
    op->is_in_flight = true;
    trace_mirror_one_iteration(s, op->offset, op->bytes);

    WITH_GRAPH_RDLOCK_GUARD() {
        ret = bdrv_co_preadv(s->mirror_top_bs->backing, op->offset, op->bytes,
                             &op->qiov, 0);
    }
    mirror_read_complete(op, ret);
}

static void coroutine_fn mirror_co_zero(void *opaque)
{
    MirrorOp *op = opaque;
    bool write_needed = true;
    int ret = 0;

    op->s->in_flight++;
    op->s->bytes_in_flight += op->bytes;
    *op->bytes_handled = op->bytes;
    op->is_in_flight = true;

    if (op->s->zero_bitmap) {
        unsigned long end = DIV_ROUND_UP(op->offset + op->bytes,
                                         op->s->granularity);
        assert(QEMU_IS_ALIGNED(op->offset, op->s->granularity));
        assert(QEMU_IS_ALIGNED(op->bytes, op->s->granularity) ||
               op->offset + op->bytes == op->s->bdev_length);
        if (find_next_zero_bit(op->s->zero_bitmap, end,
                               op->offset / op->s->granularity) == end) {
            write_needed = false;
            *op->io_skipped = true;
        }
    }
    if (write_needed) {
        ret = blk_co_pwrite_zeroes(op->s->target, op->offset, op->bytes,
                                   op->s->unmap ? BDRV_REQ_MAY_UNMAP : 0);
    }
    if (ret >= 0 && op->s->zero_bitmap) {
        bitmap_set(op->s->zero_bitmap, op->offset / op->s->granularity,
                   DIV_ROUND_UP(op->bytes, op->s->granularity));
    }
    mirror_write_complete(op, ret);
}

static void coroutine_fn mirror_co_discard(void *opaque)
{
    MirrorOp *op = opaque;
    int ret;

    op->s->in_flight++;
    op->s->bytes_in_flight += op->bytes;
    *op->bytes_handled = op->bytes;
    op->is_in_flight = true;

    ret = blk_co_pdiscard(op->s->target, op->offset, op->bytes);
    mirror_write_complete(op, ret);
}

static unsigned mirror_perform(MirrorBlockJob *s, int64_t offset,
                               unsigned bytes, MirrorMethod mirror_method,
                               bool *io_skipped)
{
    MirrorOp *op;
    Coroutine *co;
    int64_t bytes_handled = -1;

    assert(QEMU_IS_ALIGNED(offset, s->granularity));
    assert(QEMU_IS_ALIGNED(bytes, s->granularity) ||
           offset + bytes == s->bdev_length);
    op = g_new(MirrorOp, 1);
    *op = (MirrorOp){
        .s              = s,
        .offset         = offset,
        .bytes          = bytes,
        .bytes_handled  = &bytes_handled,
        .io_skipped     = io_skipped,
    };
    qemu_co_queue_init(&op->waiting_requests);

    switch (mirror_method) {
    case MIRROR_METHOD_COPY:
        if (s->zero_bitmap) {
            bitmap_clear(s->zero_bitmap, offset / s->granularity,
                         DIV_ROUND_UP(bytes, s->granularity));
        }
        co = qemu_coroutine_create(mirror_co_read, op);
        break;
    case MIRROR_METHOD_ZERO:
        co = qemu_coroutine_create(mirror_co_zero, op);
        break;
    case MIRROR_METHOD_DISCARD:
        if (s->zero_bitmap) {
            bitmap_clear(s->zero_bitmap, offset / s->granularity,
                         DIV_ROUND_UP(bytes, s->granularity));
        }
        co = qemu_coroutine_create(mirror_co_discard, op);
        break;
    default:
        abort();
    }
    op->co = co;

    QTAILQ_INSERT_TAIL(&s->ops_in_flight, op, next);
    qemu_coroutine_enter(co);

    assert(bytes_handled >= 0);

    assert(bytes_handled <= UINT_MAX);
    return bytes_handled;
}

static void coroutine_fn GRAPH_UNLOCKED mirror_iteration(MirrorBlockJob *s)
{
    BlockDriverState *source;
    MirrorOp *pseudo_op;
    int64_t offset;
    int nb_chunks = 1;
    bool write_zeroes_ok = bdrv_can_write_zeroes_with_unmap(blk_bs(s->target));
    int max_io_bytes = MAX(s->buf_size / MAX_IN_FLIGHT, MAX_IO_BYTES);

    bdrv_graph_co_rdlock();
    source = s->mirror_top_bs->backing->bs;
    bdrv_graph_co_rdunlock();

    bdrv_dirty_bitmap_lock(s->dirty_bitmap);
    offset = bdrv_dirty_iter_next(s->dbi);
    if (offset < 0) {
        bdrv_set_dirty_iter(s->dbi, 0);
        offset = bdrv_dirty_iter_next(s->dbi);
        trace_mirror_restart_iter(s, bdrv_get_dirty_count(s->dirty_bitmap));
        assert(offset >= 0);
    }
    bdrv_dirty_bitmap_unlock(s->dirty_bitmap);

    mirror_wait_on_conflicts(NULL, s, offset, 1);

    job_pause_point(&s->common.job);

    bdrv_dirty_bitmap_lock(s->dirty_bitmap);
    while (nb_chunks * s->granularity < s->buf_size) {
        int64_t next_dirty;
        int64_t next_offset = offset + nb_chunks * s->granularity;
        int64_t next_chunk = next_offset / s->granularity;
        if (next_offset >= s->bdev_length ||
            !bdrv_dirty_bitmap_get_locked(s->dirty_bitmap, next_offset)) {
            break;
        }
        if (test_bit(next_chunk, s->in_flight_bitmap)) {
            break;
        }

        next_dirty = bdrv_dirty_iter_next(s->dbi);
        if (next_dirty > next_offset || next_dirty < 0) {
            bdrv_set_dirty_iter(s->dbi, next_offset);
            next_dirty = bdrv_dirty_iter_next(s->dbi);
        }
        assert(next_dirty == next_offset);
        nb_chunks++;
    }

    bdrv_reset_dirty_bitmap_locked(s->dirty_bitmap, offset,
                                   nb_chunks * s->granularity);
    bdrv_dirty_bitmap_unlock(s->dirty_bitmap);

    pseudo_op = g_new(MirrorOp, 1);
    *pseudo_op = (MirrorOp){
        .offset         = offset,
        .bytes          = nb_chunks * s->granularity,
        .is_pseudo_op   = true,
    };
    qemu_co_queue_init(&pseudo_op->waiting_requests);
    QTAILQ_INSERT_TAIL(&s->ops_in_flight, pseudo_op, next);

    bitmap_set(s->in_flight_bitmap, offset / s->granularity, nb_chunks);
    while (nb_chunks > 0 && offset < s->bdev_length) {
        int ret = -1;
        int64_t io_bytes;
        int64_t io_bytes_acct;
        bool io_skipped = false;
        MirrorMethod mirror_method = MIRROR_METHOD_COPY;

        assert(!(offset % s->granularity));
        WITH_GRAPH_RDLOCK_GUARD() {
            ret = bdrv_co_block_status_above(source, NULL, offset,
                                             nb_chunks * s->granularity,
                                             &io_bytes, NULL, NULL);
        }
        if (ret < 0) {
            io_bytes = MIN(nb_chunks * s->granularity, max_io_bytes);
        } else if (ret & BDRV_BLOCK_DATA) {
            io_bytes = MIN(io_bytes, max_io_bytes);
        }

        io_bytes -= io_bytes % s->granularity;
        if (io_bytes < s->granularity) {
            io_bytes = s->granularity;
        } else if (ret >= 0 && !(ret & BDRV_BLOCK_DATA)) {
            int64_t target_offset;
            int64_t target_bytes;
            WITH_GRAPH_RDLOCK_GUARD() {
                bdrv_round_to_subclusters(blk_bs(s->target), offset, io_bytes,
                                          &target_offset, &target_bytes);
            }
            if (target_offset == offset &&
                target_bytes == io_bytes) {
                mirror_method = ret & BDRV_BLOCK_ZERO ?
                                    MIRROR_METHOD_ZERO :
                                    MIRROR_METHOD_DISCARD;
            }
        }

        while (s->in_flight >= MAX_IN_FLIGHT) {
            trace_mirror_yield_in_flight(s, offset, s->in_flight);
            mirror_wait_for_free_in_flight_slot(s);
        }

        if (s->ret < 0) {
            ret = 0;
            goto fail;
        }

        io_bytes = mirror_clip_bytes(s, offset, io_bytes);
        io_bytes = mirror_perform(s, offset, io_bytes, mirror_method,
                                  &io_skipped);
        if (io_skipped ||
            (mirror_method != MIRROR_METHOD_COPY && write_zeroes_ok)) {
            io_bytes_acct = 0;
        } else {
            io_bytes_acct = io_bytes;
        }
        assert(io_bytes);
        offset += io_bytes;
        nb_chunks -= DIV_ROUND_UP(io_bytes, s->granularity);
        block_job_ratelimit_processed_bytes(&s->common, io_bytes_acct);
    }

fail:
    QTAILQ_REMOVE(&s->ops_in_flight, pseudo_op, next);
    qemu_co_queue_restart_all(&pseudo_op->waiting_requests);
    g_free(pseudo_op);
}

static void mirror_free_init(MirrorBlockJob *s)
{
    int granularity = s->granularity;
    size_t buf_size = s->buf_size;
    uint8_t *buf = s->buf;

    assert(s->buf_free_count == 0);
    QSIMPLEQ_INIT(&s->buf_free);
    while (buf_size != 0) {
        MirrorBuffer *cur = (MirrorBuffer *)buf;
        QSIMPLEQ_INSERT_TAIL(&s->buf_free, cur, next);
        s->buf_free_count++;
        buf_size -= granularity;
        buf += granularity;
    }
}

static void coroutine_fn mirror_wait_for_all_io(MirrorBlockJob *s)
{
    while (s->in_flight > 0) {
        mirror_wait_for_free_in_flight_slot(s);
    }
}

static int mirror_exit_common(Job *job)
{
    MirrorBlockJob *s = container_of(job, MirrorBlockJob, common.job);
    BlockJob *bjob = &s->common;
    MirrorBDSOpaque *bs_opaque;
    BlockDriverState *src;
    BlockDriverState *target_bs;
    BlockDriverState *mirror_top_bs;
    Error *local_err = NULL;
    bool abort = job->ret < 0;
    int ret = 0;

    GLOBAL_STATE_CODE();

    if (s->prepared) {
        return 0;
    }
    s->prepared = true;

    bdrv_graph_rdlock_main_loop();

    mirror_top_bs = s->mirror_top_bs;
    bs_opaque = mirror_top_bs->opaque;
    src = mirror_top_bs->backing->bs;
    target_bs = blk_bs(s->target);

    if (bdrv_chain_contains(src, target_bs)) {
        bdrv_unfreeze_backing_chain(mirror_top_bs, target_bs);
    }

    bdrv_release_dirty_bitmap(s->dirty_bitmap);

    bdrv_ref(src);
    bdrv_ref(mirror_top_bs);
    bdrv_ref(target_bs);

    bdrv_graph_rdunlock_main_loop();

    blk_unref(s->target);
    s->target = NULL;

    bdrv_drained_begin(mirror_top_bs);
    bdrv_drained_begin(target_bs);
    bs_opaque->stop = true;

    bdrv_graph_rdlock_main_loop();
    bdrv_child_refresh_perms(mirror_top_bs, mirror_top_bs->backing,
                             &error_abort);
    bdrv_graph_rdunlock_main_loop();

    if (!abort && s->backing_mode == MIRROR_SOURCE_BACKING_CHAIN) {
        BlockDriverState *backing;
        BlockDriverState *unfiltered_target;

        bdrv_graph_wrlock_drained();
        unfiltered_target = bdrv_skip_filters(target_bs);

        backing = s->sync_mode == MIRROR_SYNC_MODE_NONE ? src : s->base;
        if (bdrv_cow_bs(unfiltered_target) != backing) {
            bdrv_set_backing_hd(unfiltered_target, backing, &local_err);
            if (local_err) {
                error_report_err(local_err);
                local_err = NULL;
                ret = -EPERM;
            }
        }
        bdrv_graph_wrunlock();
    } else if (!abort && s->backing_mode == MIRROR_OPEN_BACKING_CHAIN) {
        bdrv_graph_rdlock_main_loop();
        assert(!bdrv_backing_chain_next(target_bs));
        ret = bdrv_open_backing_file(bdrv_skip_filters(target_bs), NULL,
                                     "backing", &local_err);
        bdrv_graph_rdunlock_main_loop();
        if (ret < 0) {
            error_report_err(local_err);
            local_err = NULL;
        }
    }

    if (s->should_complete && !abort) {
        BlockDriverState *to_replace = s->to_replace ?: src;
        bool ro = bdrv_is_read_only(to_replace);

        if (ro != bdrv_is_read_only(target_bs)) {
            bdrv_reopen_set_read_only(target_bs, ro, NULL);
        }

        assert(s->in_drain);
        bdrv_drained_begin(to_replace);
        bdrv_graph_wrlock();
        if (bdrv_recurse_can_replace(src, to_replace)) {
            bdrv_replace_node(to_replace, target_bs, &local_err);
        } else {
            error_setg(&local_err, "Can no longer replace '%s' by '%s', "
                       "because it can no longer be guaranteed that doing so "
                       "would not lead to an abrupt change of visible data",
                       to_replace->node_name, target_bs->node_name);
        }
        bdrv_graph_wrunlock();
        bdrv_drained_end(to_replace);
        if (local_err) {
            error_report_err(local_err);
            ret = -EPERM;
        }
    }
    if (s->to_replace) {
        bdrv_op_unblock_all(s->to_replace, s->replace_blocker);
        error_free(s->replace_blocker);
        bdrv_unref(s->to_replace);
    }
    g_free(s->replaces);

    block_job_remove_all_bdrv(bjob);
    bdrv_graph_wrlock();
    bdrv_replace_node(mirror_top_bs, mirror_top_bs->backing->bs, &error_abort);
    bdrv_graph_wrunlock();

    if (abort && s->base_ro && !bdrv_is_read_only(target_bs)) {
        bdrv_reopen_set_read_only(target_bs, true, NULL);
    }

    bdrv_drained_end(target_bs);
    bdrv_unref(target_bs);

    bs_opaque->job = NULL;

    bdrv_drained_end(src);
    bdrv_drained_end(mirror_top_bs);
    s->in_drain = false;
    bdrv_unref(mirror_top_bs);
    bdrv_unref(src);

    return ret;
}

static int mirror_prepare(Job *job)
{
    return mirror_exit_common(job);
}

static void mirror_abort(Job *job)
{
    int ret = mirror_exit_common(job);
    assert(ret == 0);
}

static void coroutine_fn mirror_throttle(MirrorBlockJob *s)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

    if (now - s->last_pause_ns > BLOCK_JOB_SLICE_TIME) {
        s->last_pause_ns = now;
        job_sleep_ns(&s->common.job, 0);
    } else {
        job_pause_point(&s->common.job);
    }
}

static int coroutine_fn GRAPH_UNLOCKED mirror_dirty_init(MirrorBlockJob *s)
{
    int64_t offset;
    BlockDriverState *bs;
    BlockDriverState *target_bs = blk_bs(s->target);
    int ret = -EIO;
    int64_t count;
    bool punch_holes =
        target_bs->detect_zeroes == BLOCKDEV_DETECT_ZEROES_OPTIONS_UNMAP &&
        bdrv_can_write_zeroes_with_unmap(target_bs);
    int64_t bitmap_length = DIV_ROUND_UP(s->bdev_length, s->granularity);

    s->zero_bitmap = bitmap_new(bitmap_length);
    bdrv_graph_co_rdlock();
    bs = s->mirror_top_bs->backing->bs;
    if (s->target_is_zero) {
        ret = 1;
    } else {
        ret = bdrv_co_is_all_zeroes(target_bs);
    }
    bdrv_graph_co_rdunlock();

    if (ret < 0) {
        return ret;
    } else if (s->sync_mode == MIRROR_SYNC_MODE_TOP) {
        if (ret > 0 && !punch_holes) {
            bitmap_set(s->zero_bitmap, 0, bitmap_length);
        }
    } else if (ret == 0 || punch_holes) {
        if (!bdrv_can_write_zeroes_with_unmap(target_bs) || punch_holes) {
            bdrv_set_dirty_bitmap(s->dirty_bitmap, 0, s->bdev_length);
            return 0;
        }

        s->initial_zeroing_ongoing = true;
        for (offset = 0; offset < s->bdev_length; ) {
            int bytes = MIN(s->bdev_length - offset,
                            QEMU_ALIGN_DOWN(INT_MAX, s->granularity));
            bool ignored;

            mirror_throttle(s);

            if (job_is_cancelled(&s->common.job)) {
                s->initial_zeroing_ongoing = false;
                return 0;
            }

            if (s->in_flight >= MAX_IN_FLIGHT) {
                trace_mirror_yield(s, UINT64_MAX, s->buf_free_count,
                                   s->in_flight);
                mirror_wait_for_free_in_flight_slot(s);
                continue;
            }

            mirror_perform(s, offset, bytes, MIRROR_METHOD_ZERO, &ignored);
            offset += bytes;
        }

        mirror_wait_for_all_io(s);
        s->initial_zeroing_ongoing = false;
    } else {
        bitmap_set(s->zero_bitmap, 0, bitmap_length);
    }

    for (offset = 0; offset < s->bdev_length; ) {
        int bytes = MIN(s->bdev_length - offset,
                        QEMU_ALIGN_DOWN(INT_MAX, s->granularity));

        mirror_throttle(s);

        if (job_is_cancelled(&s->common.job)) {
            return 0;
        }

        WITH_GRAPH_RDLOCK_GUARD() {
            ret = bdrv_co_is_allocated_above(bs, s->base_overlay, true, offset,
                                             bytes, &count);
        }
        if (ret < 0) {
            return ret;
        }

        assert(count);
        if (ret > 0) {
            bdrv_set_dirty_bitmap(s->dirty_bitmap, offset, count);
        }
        offset += count;
    }
    return 0;
}

static int coroutine_fn mirror_flush(MirrorBlockJob *s)
{
    int ret = blk_co_flush(s->target);
    if (ret < 0) {
        if (mirror_error_action(s, false, -ret) == BLOCK_ERROR_ACTION_REPORT) {
            s->ret = ret;
        }
    }
    return ret;
}

static int coroutine_fn mirror_run(Job *job, Error **errp)
{
    MirrorBlockJob *s = container_of(job, MirrorBlockJob, common.job);
    BlockDriverState *bs;
    MirrorBDSOpaque *mirror_top_opaque = s->mirror_top_bs->opaque;
    BlockDriverState *target_bs = blk_bs(s->target);
    bool need_drain = true;
    BlockDeviceIoStatus iostatus = BLOCK_DEVICE_IO_STATUS__MAX;
    int64_t length;
    int64_t target_length;
    BlockDriverInfo bdi;
    char backing_filename[2];
    int ret = 0;

    bdrv_graph_co_rdlock();
    bs = bdrv_filter_bs(s->mirror_top_bs);
    bdrv_graph_co_rdunlock();

    if (job_is_cancelled(&s->common.job)) {
        goto immediate_exit;
    }

    bdrv_graph_co_rdlock();
    s->bdev_length = bdrv_co_getlength(bs);
    bdrv_graph_co_rdunlock();

    if (s->bdev_length < 0) {
        ret = s->bdev_length;
        goto immediate_exit;
    }

    target_length = blk_co_getlength(s->target);
    if (target_length < 0) {
        ret = target_length;
        goto immediate_exit;
    }

    if (s->base == blk_bs(s->target)) {
        if (s->bdev_length > target_length) {
            ret = blk_co_truncate(s->target, s->bdev_length, false,
                                  PREALLOC_MODE_OFF, 0, NULL);
            if (ret < 0) {
                goto immediate_exit;
            }
        }
    } else if (s->bdev_length != target_length) {
        error_setg(errp, "Source and target image have different sizes");
        ret = -EINVAL;
        goto immediate_exit;
    }

    if (s->bdev_length == 0) {
        job_transition_to_ready(&s->common.job);
        qatomic_set(&s->actively_synced, true);
        while (!job_cancel_requested(&s->common.job) && !s->should_complete) {
            job_yield(&s->common.job);
        }
        goto immediate_exit;
    }

    length = DIV_ROUND_UP(s->bdev_length, s->granularity);
    s->in_flight_bitmap = bitmap_new(length);

    bdrv_get_backing_filename(target_bs, backing_filename,
                              sizeof(backing_filename));
    bdrv_graph_co_rdlock();
    if (!bdrv_co_get_info(target_bs, &bdi) && bdi.cluster_size) {
        s->target_cluster_size = bdi.cluster_size;
    } else {
        s->target_cluster_size = BDRV_SECTOR_SIZE;
    }
    if (backing_filename[0] && !bdrv_backing_chain_next(target_bs) &&
        s->granularity < s->target_cluster_size) {
        s->buf_size = MAX(s->buf_size, s->target_cluster_size);
        s->cow_bitmap = bitmap_new(length);
    }
    s->max_iov = MIN(bs->bl.max_iov, target_bs->bl.max_iov);
    bdrv_graph_co_rdunlock();

    s->buf = qemu_try_blockalign(bs, s->buf_size);
    if (s->buf == NULL) {
        ret = -ENOMEM;
        goto immediate_exit;
    }

    mirror_free_init(s);

    s->last_pause_ns = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    if (s->sync_mode != MIRROR_SYNC_MODE_NONE) {
        ret = mirror_dirty_init(s);
        if (ret < 0 || job_is_cancelled(&s->common.job)) {
            goto immediate_exit;
        }
    }

    mirror_top_opaque->job = s;

    assert(!s->dbi);
    s->dbi = bdrv_dirty_iter_new(s->dirty_bitmap);
    for (;;) {
        int64_t cnt, delta;
        bool should_complete;

        if (s->ret < 0) {
            ret = s->ret;
            goto immediate_exit;
        }

        job_pause_point(&s->common.job);

        if (job_is_cancelled(&s->common.job)) {
            ret = 0;
            goto immediate_exit;
        }

        cnt = bdrv_get_dirty_count(s->dirty_bitmap);
        job_progress_set_remaining(&s->common.job,
                                   s->bytes_in_flight + cnt +
                                   s->active_write_bytes_in_flight);

        delta = qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - s->last_pause_ns;
        WITH_JOB_LOCK_GUARD() {
            iostatus = s->common.iostatus;
        }
        if (delta < BLOCK_JOB_SLICE_TIME &&
            iostatus == BLOCK_DEVICE_IO_STATUS_OK) {
            if (s->in_flight >= MAX_IN_FLIGHT || s->buf_free_count == 0 ||
                (cnt == 0 && s->in_flight > 0)) {
                trace_mirror_yield(s, cnt, s->buf_free_count, s->in_flight);
                mirror_wait_for_free_in_flight_slot(s);
                continue;
            } else if (cnt != 0) {
                mirror_iteration(s);
            }
        }

        should_complete = false;
        if (s->in_flight == 0 && cnt == 0) {
            trace_mirror_before_flush(s);
            if (!job_is_ready(&s->common.job)) {
                if (mirror_flush(s) < 0) {
                    continue;
                }
                job_transition_to_ready(&s->common.job);
            }
            if (qatomic_read(&s->copy_mode) != MIRROR_COPY_MODE_BACKGROUND) {
                qatomic_set(&s->actively_synced, true);
            }

            should_complete = s->should_complete ||
                job_cancel_requested(&s->common.job);
            cnt = bdrv_get_dirty_count(s->dirty_bitmap);
        }

        if (cnt == 0 && should_complete) {
            trace_mirror_before_drain(s, cnt);

            s->in_drain = true;
            bdrv_drained_begin(bs);

            assert(s->in_active_write_counter == 0);

            cnt = bdrv_get_dirty_count(s->dirty_bitmap);
            if (cnt > 0 || mirror_flush(s) < 0) {
                bdrv_drained_end(bs);
                s->in_drain = false;
                continue;
            }

            assert(QLIST_EMPTY(&bs->tracked_requests));
            need_drain = false;
            break;
        }

        if (job_is_ready(&s->common.job) && !should_complete) {
            if (s->in_flight == 0 && cnt == 0) {
                trace_mirror_before_sleep(s, cnt, job_is_ready(&s->common.job),
                                          BLOCK_JOB_SLICE_TIME);
                job_sleep_ns(&s->common.job, BLOCK_JOB_SLICE_TIME);
            }
        } else {
            block_job_ratelimit_sleep(&s->common);
        }
        s->last_pause_ns = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    }

immediate_exit:
    if (s->in_flight > 0) {
        assert(ret < 0 || job_is_cancelled(&s->common.job));
        assert(need_drain);
        mirror_wait_for_all_io(s);
    }

    assert(s->in_flight == 0);
    qemu_vfree(s->buf);
    g_free(s->cow_bitmap);
    g_free(s->zero_bitmap);
    g_free(s->in_flight_bitmap);
    bdrv_dirty_iter_free(s->dbi);

    if (need_drain) {
        s->in_drain = true;
        bdrv_drained_begin(bs);
    }

    return ret;
}

static void mirror_complete(Job *job, Error **errp)
{
    MirrorBlockJob *s = container_of(job, MirrorBlockJob, common.job);

    if (!job_is_ready(job)) {
        error_setg(errp, "The active block job '%s' cannot be completed",
                   job->id);
        return;
    }

    if (s->replaces) {
        s->to_replace = bdrv_find_node(s->replaces);
        if (!s->to_replace) {
            error_setg(errp, "Node name '%s' not found", s->replaces);
            return;
        }

        error_setg(&s->replace_blocker,
                   "block device is in use by block-job-complete");
        bdrv_op_block_all(s->to_replace, s->replace_blocker);
        bdrv_ref(s->to_replace);
    }

    s->should_complete = true;

    WITH_JOB_LOCK_GUARD() {
        if (!job->paused) {
            job_enter_cond_locked(job, NULL);
        }
    }
}

static void coroutine_fn mirror_pause(Job *job)
{
    MirrorBlockJob *s = container_of(job, MirrorBlockJob, common.job);

    mirror_wait_for_all_io(s);
}

static bool mirror_drained_poll(BlockJob *job)
{
    MirrorBlockJob *s = container_of(job, MirrorBlockJob, common);

    WITH_JOB_LOCK_GUARD() {
        if (!s->common.job.paused && !job_is_cancelled_locked(&job->job)
            && !s->in_drain) {
            return true;
        }
    }

    return !!s->in_flight;
}

static bool mirror_cancel(Job *job, bool force)
{
    MirrorBlockJob *s = container_of(job, MirrorBlockJob, common.job);
    BlockDriverState *target = blk_bs(s->target);

    force = force || !job_is_ready(job);

    if (force) {
        bdrv_cancel_in_flight(target);
    }
    return force;
}

static bool commit_active_cancel(Job *job, bool force)
{
    return force || !job_is_ready(job);
}

static void mirror_change(BlockJob *job, BlockJobChangeOptions *opts,
                          Error **errp)
{
    MirrorBlockJob *s = container_of(job, MirrorBlockJob, common);
    BlockJobChangeOptionsMirror *change_opts = &opts->u.mirror;
    MirrorCopyMode current;

    GLOBAL_STATE_CODE();

    if (qatomic_read(&s->copy_mode) == change_opts->copy_mode) {
        return;
    }

    if (change_opts->copy_mode != MIRROR_COPY_MODE_WRITE_BLOCKING) {
        error_setg(errp, "Change to copy mode '%s' is not implemented",
                   MirrorCopyMode_str(change_opts->copy_mode));
        return;
    }

    current = qatomic_cmpxchg(&s->copy_mode, MIRROR_COPY_MODE_BACKGROUND,
                              change_opts->copy_mode);
    if (current != MIRROR_COPY_MODE_BACKGROUND) {
        error_setg(errp, "Expected current copy mode '%s', got '%s'",
                   MirrorCopyMode_str(MIRROR_COPY_MODE_BACKGROUND),
                   MirrorCopyMode_str(current));
    }
}

static void mirror_query(BlockJob *job, BlockJobInfo *info)
{
    MirrorBlockJob *s = container_of(job, MirrorBlockJob, common);

    info->u.mirror = (BlockJobInfoMirror) {
        .actively_synced = qatomic_read(&s->actively_synced),
    };
}

static const BlockJobDriver mirror_job_driver = {
    .job_driver = {
        .instance_size          = sizeof(MirrorBlockJob),
        .job_type               = JOB_TYPE_MIRROR,
        .free                   = block_job_free,
        .user_resume            = block_job_user_resume,
        .run                    = mirror_run,
        .prepare                = mirror_prepare,
        .abort                  = mirror_abort,
        .pause                  = mirror_pause,
        .complete               = mirror_complete,
        .cancel                 = mirror_cancel,
    },
    .drained_poll           = mirror_drained_poll,
    .change                 = mirror_change,
    .query                  = mirror_query,
};

static const BlockJobDriver commit_active_job_driver = {
    .job_driver = {
        .instance_size          = sizeof(MirrorBlockJob),
        .job_type               = JOB_TYPE_COMMIT,
        .free                   = block_job_free,
        .user_resume            = block_job_user_resume,
        .run                    = mirror_run,
        .prepare                = mirror_prepare,
        .abort                  = mirror_abort,
        .pause                  = mirror_pause,
        .complete               = mirror_complete,
        .cancel                 = commit_active_cancel,
    },
    .drained_poll           = mirror_drained_poll,
};

static void coroutine_fn
do_sync_target_write(MirrorBlockJob *job, MirrorMethod method,
                     uint64_t offset, uint64_t bytes,
                     QEMUIOVector *qiov, int flags)
{
    int ret;
    size_t qiov_offset = 0;
    int64_t dirty_bitmap_offset, dirty_bitmap_end;
    int64_t zero_bitmap_offset, zero_bitmap_end;

    if (!QEMU_IS_ALIGNED(offset, job->granularity) &&
        bdrv_dirty_bitmap_get(job->dirty_bitmap, offset))
    {
            qiov_offset = QEMU_ALIGN_UP(offset, job->granularity) - offset;
            if (bytes <= qiov_offset) {
                return;
            }
            offset += qiov_offset;
            bytes -= qiov_offset;
    }

    if (!QEMU_IS_ALIGNED(offset + bytes, job->granularity) &&
        bdrv_dirty_bitmap_get(job->dirty_bitmap, offset + bytes - 1))
    {
        uint64_t tail = (offset + bytes) % job->granularity;

        if (bytes <= tail) {
            return;
        }
        bytes -= tail;
    }

    dirty_bitmap_offset = QEMU_ALIGN_UP(offset, job->granularity);
    dirty_bitmap_end = QEMU_ALIGN_DOWN(offset + bytes, job->granularity);
    if (dirty_bitmap_offset < dirty_bitmap_end) {
        bdrv_reset_dirty_bitmap(job->dirty_bitmap, dirty_bitmap_offset,
                                dirty_bitmap_end - dirty_bitmap_offset);
    }
    zero_bitmap_offset = offset / job->granularity;
    zero_bitmap_end = DIV_ROUND_UP(offset + bytes, job->granularity);

    job_progress_increase_remaining(&job->common.job, bytes);
    job->active_write_bytes_in_flight += bytes;

    switch (method) {
    case MIRROR_METHOD_COPY:
        if (job->zero_bitmap) {
            bitmap_clear(job->zero_bitmap, zero_bitmap_offset,
                         zero_bitmap_end - zero_bitmap_offset);
        }
        ret = blk_co_pwritev_part(job->target, offset, bytes,
                                  qiov, qiov_offset, flags);
        break;

    case MIRROR_METHOD_ZERO:
        if (job->zero_bitmap) {
            if (find_next_zero_bit(job->zero_bitmap, zero_bitmap_end,
                                   zero_bitmap_offset) == zero_bitmap_end) {
                ret = 0;
                break;
            }
        }
        assert(!qiov);
        ret = blk_co_pwrite_zeroes(job->target, offset, bytes, flags);
        if (job->zero_bitmap && ret >= 0) {
            bitmap_set(job->zero_bitmap, dirty_bitmap_offset / job->granularity,
                       (dirty_bitmap_end - dirty_bitmap_offset) /
                       job->granularity);
        }
        break;

    case MIRROR_METHOD_DISCARD:
        if (job->zero_bitmap) {
            bitmap_clear(job->zero_bitmap, zero_bitmap_offset,
                         zero_bitmap_end - zero_bitmap_offset);
        }
        assert(!qiov);
        ret = blk_co_pdiscard(job->target, offset, bytes);
        break;

    default:
        abort();
    }

    job->active_write_bytes_in_flight -= bytes;
    if (ret >= 0) {
        job_progress_update(&job->common.job, bytes);
    } else {
        BlockErrorAction action;

        dirty_bitmap_offset = QEMU_ALIGN_DOWN(offset, job->granularity);
        dirty_bitmap_end = QEMU_ALIGN_UP(offset + bytes, job->granularity);
        bdrv_set_dirty_bitmap(job->dirty_bitmap, dirty_bitmap_offset,
                              dirty_bitmap_end - dirty_bitmap_offset);
        qatomic_set(&job->actively_synced, false);

        action = mirror_error_action(job, false, -ret);
        if (action == BLOCK_ERROR_ACTION_REPORT) {
            if (!job->ret) {
                job->ret = ret;
            }
        }
    }
}

static MirrorOp *coroutine_fn active_write_prepare(MirrorBlockJob *s,
                                                   uint64_t offset,
                                                   uint64_t bytes)
{
    MirrorOp *op;
    uint64_t start_chunk = offset / s->granularity;
    uint64_t end_chunk = DIV_ROUND_UP(offset + bytes, s->granularity);

    op = g_new(MirrorOp, 1);
    *op = (MirrorOp){
        .s                  = s,
        .offset             = offset,
        .bytes              = bytes,
        .is_active_write    = true,
        .is_in_flight       = true,
        .co                 = qemu_coroutine_self(),
    };
    qemu_co_queue_init(&op->waiting_requests);
    QTAILQ_INSERT_TAIL(&s->ops_in_flight, op, next);

    s->in_active_write_counter++;

    mirror_wait_on_conflicts(op, s, offset, bytes);

    bitmap_set(s->in_flight_bitmap, start_chunk, end_chunk - start_chunk);

    return op;
}

static void coroutine_fn GRAPH_RDLOCK active_write_settle(MirrorOp *op)
{
    uint64_t start_chunk = op->offset / op->s->granularity;
    uint64_t end_chunk = DIV_ROUND_UP(op->offset + op->bytes,
                                      op->s->granularity);

    if (!--op->s->in_active_write_counter &&
        qatomic_read(&op->s->actively_synced)) {
        BdrvChild *source = op->s->mirror_top_bs->backing;

        if (QLIST_FIRST(&source->bs->parents) == source &&
            QLIST_NEXT(source, next_parent) == NULL)
        {
            assert(!bdrv_get_dirty_count(op->s->dirty_bitmap));
        }
    }
    bitmap_clear(op->s->in_flight_bitmap, start_chunk, end_chunk - start_chunk);
    QTAILQ_REMOVE(&op->s->ops_in_flight, op, next);
    qemu_co_queue_restart_all(&op->waiting_requests);
    g_free(op);
}

static int coroutine_fn GRAPH_RDLOCK
bdrv_mirror_top_preadv(BlockDriverState *bs, int64_t offset, int64_t bytes,
                       QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    return bdrv_co_preadv(bs->backing, offset, bytes, qiov, flags);
}

static bool should_copy_to_target(MirrorBDSOpaque *s)
{
    return s->job && s->job->ret >= 0 &&
        !job_is_cancelled(&s->job->common.job) &&
        qatomic_read(&s->job->copy_mode) == MIRROR_COPY_MODE_WRITE_BLOCKING;
}

static int coroutine_fn GRAPH_RDLOCK
bdrv_mirror_top_do_write(BlockDriverState *bs, MirrorMethod method,
                         bool copy_to_target, uint64_t offset, uint64_t bytes,
                         QEMUIOVector *qiov, int flags)
{
    MirrorOp *op = NULL;
    MirrorBDSOpaque *s = bs->opaque;
    int ret = 0;

    if (copy_to_target) {
        op = active_write_prepare(s->job, offset, bytes);
    }

    switch (method) {
    case MIRROR_METHOD_COPY:
        ret = bdrv_co_pwritev(bs->backing, offset, bytes, qiov, flags);
        break;

    case MIRROR_METHOD_ZERO:
        ret = bdrv_co_pwrite_zeroes(bs->backing, offset, bytes, flags);
        break;

    case MIRROR_METHOD_DISCARD:
        ret = bdrv_co_pdiscard(bs->backing, offset, bytes);
        break;

    default:
        abort();
    }

    if (!copy_to_target && s->job && s->job->dirty_bitmap) {
        qatomic_set(&s->job->actively_synced, false);
        bdrv_set_dirty_bitmap(s->job->dirty_bitmap, offset, bytes);
    }

    if (ret < 0) {
        goto out;
    }

    if (copy_to_target) {
        do_sync_target_write(s->job, method, offset, bytes, qiov, flags);
    }

out:
    if (copy_to_target) {
        active_write_settle(op);
    }
    return ret;
}

static int coroutine_fn GRAPH_RDLOCK
bdrv_mirror_top_pwritev(BlockDriverState *bs, int64_t offset, int64_t bytes,
                        QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    QEMUIOVector bounce_qiov;
    void *bounce_buf;
    int ret = 0;
    bool copy_to_target = should_copy_to_target(bs->opaque);

    if (copy_to_target) {
        bounce_buf = qemu_blockalign(bs, bytes);
        iov_to_buf_full(qiov->iov, qiov->niov, 0, bounce_buf, bytes);

        qemu_iovec_init(&bounce_qiov, 1);
        qemu_iovec_add(&bounce_qiov, bounce_buf, bytes);
        qiov = &bounce_qiov;

        flags &= ~BDRV_REQ_REGISTERED_BUF;
    }

    ret = bdrv_mirror_top_do_write(bs, MIRROR_METHOD_COPY, copy_to_target,
                                   offset, bytes, qiov, flags);

    if (copy_to_target) {
        qemu_iovec_destroy(&bounce_qiov);
        qemu_vfree(bounce_buf);
    }

    return ret;
}

static int coroutine_fn GRAPH_RDLOCK bdrv_mirror_top_flush(BlockDriverState *bs)
{
    if (bs->backing == NULL) {
        return 0;
    }
    return bdrv_co_flush(bs->backing->bs);
}

static int coroutine_fn GRAPH_RDLOCK
bdrv_mirror_top_pwrite_zeroes(BlockDriverState *bs, int64_t offset,
                              int64_t bytes, BdrvRequestFlags flags)
{
    bool copy_to_target = should_copy_to_target(bs->opaque);
    return bdrv_mirror_top_do_write(bs, MIRROR_METHOD_ZERO, copy_to_target,
                                    offset, bytes, NULL, flags);
}

static int coroutine_fn GRAPH_RDLOCK
bdrv_mirror_top_pdiscard(BlockDriverState *bs, int64_t offset, int64_t bytes)
{
    bool copy_to_target = should_copy_to_target(bs->opaque);
    return bdrv_mirror_top_do_write(bs, MIRROR_METHOD_DISCARD, copy_to_target,
                                    offset, bytes, NULL, 0);
}

static void GRAPH_RDLOCK bdrv_mirror_top_refresh_filename(BlockDriverState *bs)
{
    if (bs->backing == NULL) {
        return;
    }
    pstrcpy(bs->exact_filename, sizeof(bs->exact_filename),
            bs->backing->bs->filename);
}

static void bdrv_mirror_top_child_perm(BlockDriverState *bs, BdrvChild *c,
                                       BdrvChildRole role,
                                       BlockReopenQueue *reopen_queue,
                                       uint64_t perm, uint64_t shared,
                                       uint64_t *nperm, uint64_t *nshared)
{
    MirrorBDSOpaque *s = bs->opaque;

    if (s->stop) {
        *nperm = 0;
        *nshared = BLK_PERM_ALL;
        return;
    }

    bdrv_default_perms(bs, c, role, reopen_queue,
                       perm, shared, nperm, nshared);

    if (s->is_commit) {
        *nperm &= ~BLK_PERM_CONSISTENT_READ;
        *nshared |= BLK_PERM_WRITE;
    }
}

static BlockDriver bdrv_mirror_top = {
    .format_name                = "mirror_top",
    .bdrv_co_preadv             = bdrv_mirror_top_preadv,
    .bdrv_co_pwritev            = bdrv_mirror_top_pwritev,
    .bdrv_co_pwrite_zeroes      = bdrv_mirror_top_pwrite_zeroes,
    .bdrv_co_pdiscard           = bdrv_mirror_top_pdiscard,
    .bdrv_co_flush              = bdrv_mirror_top_flush,
    .bdrv_refresh_filename      = bdrv_mirror_top_refresh_filename,
    .bdrv_child_perm            = bdrv_mirror_top_child_perm,

    .is_filter                  = true,
    .filtered_child_is_backing  = true,
};

static BlockJob *mirror_start_job(
                             const char *job_id, BlockDriverState *bs,
                             int creation_flags, BlockDriverState *target,
                             const char *replaces, int64_t speed,
                             uint32_t granularity, int64_t buf_size,
                             MirrorSyncMode sync_mode,
                             BlockMirrorBackingMode backing_mode,
                             bool target_is_zero,
                             BlockdevOnError on_source_error,
                             BlockdevOnError on_target_error,
                             bool unmap,
                             BlockCompletionFunc *cb,
                             void *opaque,
                             const BlockJobDriver *driver,
                             BlockDriverState *base,
                             bool auto_complete, const char *filter_node_name,
                             bool is_mirror, MirrorCopyMode copy_mode,
                             bool base_ro,
                             Error **errp)
{
    MirrorBlockJob *s;
    MirrorBDSOpaque *bs_opaque;
    BlockDriverState *mirror_top_bs;
    bool target_is_backing;
    uint64_t target_perms, target_shared_perms;
    int ret;

    GLOBAL_STATE_CODE();

    if (granularity == 0) {
        granularity = bdrv_get_default_bitmap_granularity(target);
    }

    assert(is_power_of_2(granularity));

    if (buf_size < 0) {
        error_setg(errp, "Invalid parameter 'buf-size'");
        return NULL;
    }

    if (buf_size == 0) {
        buf_size = DEFAULT_MIRROR_BUF_SIZE;
    }

    bdrv_graph_rdlock_main_loop();
    if (bdrv_skip_filters(bs) == bdrv_skip_filters(target)) {
        error_setg(errp, "Can't mirror node into itself");
        bdrv_graph_rdunlock_main_loop();
        return NULL;
    }

    target_is_backing = bdrv_chain_contains(bs, target);
    bdrv_graph_rdunlock_main_loop();

    mirror_top_bs = bdrv_new_open_driver(&bdrv_mirror_top, filter_node_name,
                                         BDRV_O_RDWR, errp);
    if (mirror_top_bs == NULL) {
        return NULL;
    }
    if (!filter_node_name) {
        mirror_top_bs->implicit = true;
    }

    mirror_top_bs->never_freeze = true;

    mirror_top_bs->total_sectors = bs->total_sectors;
    mirror_top_bs->supported_write_flags = BDRV_REQ_WRITE_UNCHANGED;
    mirror_top_bs->supported_zero_flags = BDRV_REQ_WRITE_UNCHANGED |
                                          BDRV_REQ_NO_FALLBACK;
    bs_opaque = g_new0(MirrorBDSOpaque, 1);
    mirror_top_bs->opaque = bs_opaque;

    bs_opaque->is_commit = target_is_backing;

    bdrv_drained_begin(bs);
    ret = bdrv_append(mirror_top_bs, bs, errp);
    bdrv_drained_end(bs);

    if (ret < 0) {
        bdrv_unref(mirror_top_bs);
        return NULL;
    }

    s = block_job_create(job_id, driver, NULL, mirror_top_bs,
                         BLK_PERM_CONSISTENT_READ,
                         BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE_UNCHANGED |
                         BLK_PERM_WRITE, speed,
                         creation_flags, cb, opaque, errp);
    if (!s) {
        goto fail;
    }

    bdrv_unref(mirror_top_bs);

    s->mirror_top_bs = mirror_top_bs;
    s->base_ro = base_ro;

    target_perms = BLK_PERM_WRITE;
    target_shared_perms = BLK_PERM_WRITE_UNCHANGED;

    if (target_is_backing) {
        int64_t bs_size, target_size;
        bs_size = bdrv_getlength(bs);
        if (bs_size < 0) {
            error_setg_errno(errp, -bs_size,
                             "Could not inquire top image size");
            goto fail;
        }

        target_size = bdrv_getlength(target);
        if (target_size < 0) {
            error_setg_errno(errp, -target_size,
                             "Could not inquire base image size");
            goto fail;
        }

        if (target_size < bs_size) {
            target_perms |= BLK_PERM_RESIZE;
        }

        target_shared_perms |= BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE;
    } else {
        bdrv_graph_rdlock_main_loop();
        if (bdrv_chain_contains(bs, bdrv_skip_filters(target))) {
            error_setg(errp, "Cannot mirror to a filter on top of a node in "
                       "the source's backing chain");
            bdrv_graph_rdunlock_main_loop();
            goto fail;
        }
        bdrv_graph_rdunlock_main_loop();
    }

    s->target = blk_new(s->common.job.aio_context,
                        target_perms, target_shared_perms);
    ret = blk_insert_bs(s->target, target, errp);
    if (ret < 0) {
        goto fail;
    }
    if (is_mirror) {
        blk_set_force_allow_inactivate(s->target);
    }
    blk_set_allow_aio_context_change(s->target, true);
    blk_set_disable_request_queuing(s->target, true);

    bdrv_graph_rdlock_main_loop();
    s->replaces = g_strdup(replaces);
    s->on_source_error = on_source_error;
    s->on_target_error = on_target_error;
    s->sync_mode = sync_mode;
    s->backing_mode = backing_mode;
    s->target_is_zero = target_is_zero;
    qatomic_set(&s->copy_mode, copy_mode);
    s->base = base;
    s->base_overlay = bdrv_find_overlay(bs, base);
    s->granularity = granularity;
    s->buf_size = ROUND_UP(buf_size, granularity);
    s->unmap = unmap;
    if (auto_complete) {
        s->should_complete = true;
    }
    bdrv_graph_rdunlock_main_loop();

    s->dirty_bitmap = bdrv_create_dirty_bitmap(s->mirror_top_bs, granularity,
                                               NULL, errp);
    if (!s->dirty_bitmap) {
        goto fail;
    }

    bdrv_disable_dirty_bitmap(s->dirty_bitmap);

    bdrv_graph_wrlock_drained();
    ret = block_job_add_bdrv(&s->common, "source", bs, 0,
                             BLK_PERM_WRITE_UNCHANGED | BLK_PERM_WRITE |
                             BLK_PERM_CONSISTENT_READ,
                             errp);
    if (ret < 0) {
        bdrv_graph_wrunlock();
        goto fail;
    }

    block_job_add_bdrv(&s->common, "target", target, 0, BLK_PERM_ALL,
                       &error_abort);

    if (target_is_backing) {
        BlockDriverState *iter, *filtered_target;
        uint64_t iter_shared_perms;

        filtered_target = bdrv_cow_bs(bdrv_find_overlay(bs, target));

        assert(bdrv_skip_filters(filtered_target) ==
               bdrv_skip_filters(target));

        iter_shared_perms = BLK_PERM_WRITE_UNCHANGED | BLK_PERM_WRITE;

        for (iter = bdrv_filter_or_cow_bs(bs); iter != target;
             iter = bdrv_filter_or_cow_bs(iter))
        {
            if (iter == filtered_target) {
                iter_shared_perms |= BLK_PERM_CONSISTENT_READ;
            }

            ret = block_job_add_bdrv(&s->common, "intermediate node", iter, 0,
                                     iter_shared_perms, errp);
            if (ret < 0) {
                bdrv_graph_wrunlock();
                goto fail;
            }
        }

        if (bdrv_freeze_backing_chain(mirror_top_bs, target, errp) < 0) {
            bdrv_graph_wrunlock();
            goto fail;
        }
    }
    bdrv_graph_wrunlock();

    QTAILQ_INIT(&s->ops_in_flight);

    trace_mirror_start(bs, s, opaque);
    job_start(&s->common.job);

    return &s->common;

fail:
    if (s) {
        bdrv_ref(mirror_top_bs);

        g_free(s->replaces);
        blk_unref(s->target);
        bs_opaque->job = NULL;
        if (s->dirty_bitmap) {
            bdrv_release_dirty_bitmap(s->dirty_bitmap);
        }
        job_early_fail(&s->common.job);
    }

    bs_opaque->stop = true;
    bdrv_drained_begin(bs);
    bdrv_graph_wrlock();
    assert(mirror_top_bs->backing->bs == bs);
    bdrv_child_refresh_perms(mirror_top_bs, mirror_top_bs->backing,
                             &error_abort);
    bdrv_replace_node(mirror_top_bs, bs, &error_abort);
    bdrv_graph_wrunlock();
    bdrv_drained_end(bs);

    bdrv_unref(mirror_top_bs);

    return NULL;
}

void mirror_start(const char *job_id, BlockDriverState *bs,
                  BlockDriverState *target, const char *replaces,
                  int creation_flags, int64_t speed,
                  uint32_t granularity, int64_t buf_size,
                  MirrorSyncMode mode, BlockMirrorBackingMode backing_mode,
                  bool target_is_zero,
                  BlockdevOnError on_source_error,
                  BlockdevOnError on_target_error,
                  bool unmap, const char *filter_node_name,
                  MirrorCopyMode copy_mode, Error **errp)
{
    BlockDriverState *base;

    GLOBAL_STATE_CODE();

    if ((mode == MIRROR_SYNC_MODE_INCREMENTAL) ||
        (mode == MIRROR_SYNC_MODE_BITMAP)) {
        error_setg(errp, "Sync mode '%s' not supported",
                   MirrorSyncMode_str(mode));
        return;
    }

    bdrv_graph_rdlock_main_loop();
    base = mode == MIRROR_SYNC_MODE_TOP ? bdrv_backing_chain_next(bs) : NULL;
    bdrv_graph_rdunlock_main_loop();

    mirror_start_job(job_id, bs, creation_flags, target, replaces,
                     speed, granularity, buf_size, mode, backing_mode,
                     target_is_zero, on_source_error, on_target_error, unmap,
                     NULL, NULL, &mirror_job_driver, base, false,
                     filter_node_name, true, copy_mode, false, errp);
}

BlockJob *commit_active_start(const char *job_id, BlockDriverState *bs,
                              BlockDriverState *base, int creation_flags,
                              int64_t speed, BlockdevOnError on_error,
                              const char *filter_node_name,
                              BlockCompletionFunc *cb, void *opaque,
                              bool auto_complete, Error **errp)
{
    bool base_read_only;
    BlockJob *job;

    GLOBAL_STATE_CODE();

    base_read_only = bdrv_is_read_only(base);

    if (base_read_only) {
        if (bdrv_reopen_set_read_only(base, false, errp) < 0) {
            return NULL;
        }
    }

    job = mirror_start_job(
                     job_id, bs, creation_flags, base, NULL, speed, 0, 0,
                     MIRROR_SYNC_MODE_TOP, MIRROR_LEAVE_BACKING_CHAIN, false,
                     on_error, on_error, true, cb, opaque,
                     &commit_active_job_driver, base, auto_complete,
                     filter_node_name, false, MIRROR_COPY_MODE_BACKGROUND,
                     base_read_only, errp);
    if (!job) {
        goto error_restore_flags;
    }

    return job;

error_restore_flags:
    if (base_read_only) {
        bdrv_reopen_set_read_only(base, true, NULL);
    }
    return NULL;
}
