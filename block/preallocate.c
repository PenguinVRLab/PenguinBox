/*
 * preallocate filter driver
 *
 * The driver performs preallocate operation: it is injected above
 * some node, and before each write over EOF it does additional preallocating
 * write-zeroes request.
 *
 * Copyright (c) 2020 Virtuozzo International GmbH.
 *
 * Author:
 *  Sementsov-Ogievskiy Vladimir <vsementsov@virtuozzo.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/units.h"
#include "block/block-io.h"
#include "block/block_int.h"


typedef struct PreallocateOpts {
    int64_t prealloc_size;
    int64_t prealloc_align;
} PreallocateOpts;

typedef struct BDRVPreallocateState {
    PreallocateOpts opts;

    int64_t data_end;

    int64_t zero_start;

    int64_t file_end;

    QEMUBH *drop_resize_bh;
} BDRVPreallocateState;

static int preallocate_drop_resize(BlockDriverState *bs, Error **errp);
static void preallocate_drop_resize_bh(void *opaque);

#define PREALLOCATE_OPT_PREALLOC_ALIGN "prealloc-align"
#define PREALLOCATE_OPT_PREALLOC_SIZE "prealloc-size"
static QemuOptsList runtime_opts = {
    .name = "preallocate",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
        {
            .name = PREALLOCATE_OPT_PREALLOC_ALIGN,
            .type = QEMU_OPT_SIZE,
            .help = "on preallocation, align file length to this number, "
                "default 1M",
        },
        {
            .name = PREALLOCATE_OPT_PREALLOC_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "how much to preallocate, default 128M",
        },
        { }
    },
};

static bool preallocate_absorb_opts(PreallocateOpts *dest, QDict *options,
                                    BlockDriverState *child_bs, Error **errp)
{
    QemuOpts *opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);

    if (!qemu_opts_absorb_qdict(opts, options, errp)) {
        return false;
    }

    dest->prealloc_align =
        qemu_opt_get_size(opts, PREALLOCATE_OPT_PREALLOC_ALIGN, 1 * MiB);
    dest->prealloc_size =
        qemu_opt_get_size(opts, PREALLOCATE_OPT_PREALLOC_SIZE, 128 * MiB);

    qemu_opts_del(opts);

    if (!QEMU_IS_ALIGNED(dest->prealloc_align, BDRV_SECTOR_SIZE)) {
        error_setg(errp, "prealloc-align parameter of preallocate filter "
                   "is not aligned to %llu", BDRV_SECTOR_SIZE);
        return false;
    }

    if (!QEMU_IS_ALIGNED(dest->prealloc_align,
                         child_bs->bl.request_alignment)) {
        error_setg(errp, "prealloc-align parameter of preallocate filter "
                   "is not aligned to underlying node request alignment "
                   "(%" PRIi32 ")", child_bs->bl.request_alignment);
        return false;
    }

    return true;
}

static int preallocate_open(BlockDriverState *bs, QDict *options, int flags,
                            Error **errp)
{
    BDRVPreallocateState *s = bs->opaque;
    int ret;

    GLOBAL_STATE_CODE();

    s->file_end = s->zero_start = s->data_end = -EINVAL;
    s->drop_resize_bh = qemu_bh_new(preallocate_drop_resize_bh, bs);

    ret = bdrv_open_file_child(NULL, options, "file", bs, errp);
    if (ret < 0) {
        return ret;
    }

    GRAPH_RDLOCK_GUARD_MAINLOOP();

    if (!preallocate_absorb_opts(&s->opts, options, bs->file->bs, errp)) {
        return -EINVAL;
    }

    bs->supported_write_flags = BDRV_REQ_WRITE_UNCHANGED |
        (BDRV_REQ_FUA & bs->file->bs->supported_write_flags);

    bs->supported_zero_flags = BDRV_REQ_WRITE_UNCHANGED |
        ((BDRV_REQ_FUA | BDRV_REQ_MAY_UNMAP | BDRV_REQ_NO_FALLBACK) &
            bs->file->bs->supported_zero_flags);

    return 0;
}

static int GRAPH_RDLOCK
preallocate_truncate_to_real_size(BlockDriverState *bs, Error **errp)
{
    BDRVPreallocateState *s = bs->opaque;
    int ret;

    if (s->file_end < 0) {
        s->file_end = bdrv_getlength(bs->file->bs);
        if (s->file_end < 0) {
            error_setg_errno(errp, -s->file_end, "Failed to get file length");
            return s->file_end;
        }
    }

    if (s->data_end < s->file_end) {
        ret = bdrv_truncate(bs->file, s->data_end, true, PREALLOC_MODE_OFF, 0,
                            NULL);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Failed to drop preallocation");
            s->file_end = ret;
            return ret;
        }
        s->file_end = s->data_end;
    }

    return 0;
}

static void preallocate_close(BlockDriverState *bs)
{
    BDRVPreallocateState *s = bs->opaque;

    GLOBAL_STATE_CODE();
    GRAPH_RDLOCK_GUARD_MAINLOOP();

    qemu_bh_cancel(s->drop_resize_bh);
    qemu_bh_delete(s->drop_resize_bh);

    if (s->data_end >= 0) {
        preallocate_truncate_to_real_size(bs, NULL);
    }
}


static int preallocate_reopen_prepare(BDRVReopenState *reopen_state,
                                      BlockReopenQueue *queue, Error **errp)
{
    PreallocateOpts *opts = g_new0(PreallocateOpts, 1);
    int ret;

    GLOBAL_STATE_CODE();
    GRAPH_RDLOCK_GUARD_MAINLOOP();

    if (!preallocate_absorb_opts(opts, reopen_state->options,
                                 reopen_state->bs->file->bs, errp)) {
        g_free(opts);
        return -EINVAL;
    }

    if ((reopen_state->flags & BDRV_O_RDWR) == 0) {
        ret = preallocate_drop_resize(reopen_state->bs, errp);
        if (ret < 0) {
            g_free(opts);
            return ret;
        }
    }

    reopen_state->opaque = opts;

    return 0;
}

static void preallocate_reopen_commit(BDRVReopenState *state)
{
    BDRVPreallocateState *s = state->bs->opaque;

    s->opts = *(PreallocateOpts *)state->opaque;

    g_free(state->opaque);
    state->opaque = NULL;
}

static void preallocate_reopen_abort(BDRVReopenState *state)
{
    g_free(state->opaque);
    state->opaque = NULL;
}

static int coroutine_fn GRAPH_RDLOCK
preallocate_co_preadv_part(BlockDriverState *bs, int64_t offset, int64_t bytes,
                           QEMUIOVector *qiov, size_t qiov_offset,
                           BdrvRequestFlags flags)
{
    return bdrv_co_preadv_part(bs->file, offset, bytes, qiov, qiov_offset,
                               flags);
}

static int coroutine_fn GRAPH_RDLOCK
preallocate_co_pdiscard(BlockDriverState *bs, int64_t offset, int64_t bytes)
{
    return bdrv_co_pdiscard(bs->file, offset, bytes);
}

static bool can_write_resize(uint64_t perm)
{
    return (perm & BLK_PERM_WRITE) && (perm & BLK_PERM_RESIZE);
}

static bool GRAPH_RDLOCK has_prealloc_perms(BlockDriverState *bs)
{
    BDRVPreallocateState *s = bs->opaque;

    if (can_write_resize(bs->file->perm)) {
        assert(!(bs->file->shared_perm & BLK_PERM_WRITE));
        assert(!(bs->file->shared_perm & BLK_PERM_RESIZE));
        return true;
    }

    assert(s->data_end < 0);
    assert(s->zero_start < 0);
    assert(s->file_end < 0);
    return false;
}

static bool coroutine_fn GRAPH_RDLOCK
handle_write(BlockDriverState *bs, int64_t offset, int64_t bytes,
             bool want_merge_zero)
{
    BDRVPreallocateState *s = bs->opaque;
    int64_t end = offset + bytes;
    int64_t prealloc_start, prealloc_end;
    int ret;
    uint32_t file_align = bs->file->bs->bl.request_alignment;
    uint32_t prealloc_align = MAX(s->opts.prealloc_align, file_align);

    assert(QEMU_IS_ALIGNED(prealloc_align, file_align));

    if (!has_prealloc_perms(bs)) {
        return false;
    }

    if (s->data_end < 0) {
        s->data_end = bdrv_co_getlength(bs->file->bs);
        if (s->data_end < 0) {
            return false;
        }

        if (s->file_end < 0) {
            s->file_end = s->data_end;
        }
    }

    if (end <= s->data_end) {
        return false;
    }

    s->data_end = end;
    if (s->zero_start < 0 || !want_merge_zero) {
        s->zero_start = end;
    }

    if (s->file_end < 0) {
        s->file_end = bdrv_co_getlength(bs->file->bs);
        if (s->file_end < 0) {
            return false;
        }
    }

    if (end <= s->file_end) {
        return want_merge_zero && offset >= s->zero_start;
    }

    prealloc_start = QEMU_ALIGN_UP(
            want_merge_zero ? MIN(offset, s->file_end) : s->file_end,
            file_align);
    prealloc_end = QEMU_ALIGN_UP(
            MAX(prealloc_start, end) + s->opts.prealloc_size,
            prealloc_align);

    want_merge_zero = want_merge_zero && (prealloc_start <= offset);

    ret = bdrv_co_pwrite_zeroes(
            bs->file, prealloc_start, prealloc_end - prealloc_start,
            BDRV_REQ_NO_FALLBACK | BDRV_REQ_SERIALISING | BDRV_REQ_NO_WAIT);
    if (ret < 0) {
        s->file_end = ret;
        return false;
    }

    s->file_end = prealloc_end;
    return want_merge_zero;
}

static int coroutine_fn GRAPH_RDLOCK
preallocate_co_pwrite_zeroes(BlockDriverState *bs, int64_t offset,
                             int64_t bytes, BdrvRequestFlags flags)
{
    bool want_merge_zero =
        !(flags & ~(BDRV_REQ_ZERO_WRITE | BDRV_REQ_NO_FALLBACK));
    if (handle_write(bs, offset, bytes, want_merge_zero)) {
        return 0;
    }

    return bdrv_co_pwrite_zeroes(bs->file, offset, bytes, flags);
}

static int coroutine_fn GRAPH_RDLOCK
preallocate_co_pwritev_part(BlockDriverState *bs, int64_t offset, int64_t bytes,
                            QEMUIOVector *qiov, size_t qiov_offset,
                            BdrvRequestFlags flags)
{
    handle_write(bs, offset, bytes, false);

    return bdrv_co_pwritev_part(bs->file, offset, bytes, qiov, qiov_offset,
                                flags);
}

static int coroutine_fn GRAPH_RDLOCK
preallocate_co_truncate(BlockDriverState *bs, int64_t offset,
                        bool exact, PreallocMode prealloc,
                        BdrvRequestFlags flags, Error **errp)
{
    ERRP_GUARD();
    BDRVPreallocateState *s = bs->opaque;
    int ret;

    if (s->data_end >= 0 && offset > s->data_end) {
        if (s->file_end < 0) {
            s->file_end = bdrv_co_getlength(bs->file->bs);
            if (s->file_end < 0) {
                error_setg(errp, "failed to get file length");
                return s->file_end;
            }
        }

        if (prealloc == PREALLOC_MODE_FALLOC) {
            if (offset <= s->file_end) {
                s->data_end = offset;
                return 0;
            }
        } else {
            if (s->file_end > s->data_end) {
                ret = bdrv_co_truncate(bs->file, s->data_end, true,
                                       PREALLOC_MODE_OFF, 0, errp);
                if (ret < 0) {
                    s->file_end = ret;
                    error_prepend(errp, "preallocate-filter: failed to drop "
                                  "write-zero preallocation: ");
                    return ret;
                }
                s->file_end = s->data_end;
            }
        }

        s->data_end = offset;
    }

    ret = bdrv_co_truncate(bs->file, offset, exact, prealloc, flags, errp);
    if (ret < 0) {
        s->file_end = s->zero_start = s->data_end = ret;
        return ret;
    }

    if (has_prealloc_perms(bs)) {
        s->file_end = s->zero_start = s->data_end = offset;
    }
    return 0;
}

static int coroutine_fn GRAPH_RDLOCK preallocate_co_flush(BlockDriverState *bs)
{
    return bdrv_co_flush(bs->file->bs);
}

static int64_t coroutine_fn GRAPH_RDLOCK
preallocate_co_getlength(BlockDriverState *bs)
{
    int64_t ret;
    BDRVPreallocateState *s = bs->opaque;

    if (s->data_end >= 0) {
        return s->data_end;
    }

    ret = bdrv_co_getlength(bs->file->bs);

    if (has_prealloc_perms(bs)) {
        s->file_end = s->zero_start = s->data_end = ret;
    }

    return ret;
}

static int GRAPH_RDLOCK
preallocate_drop_resize(BlockDriverState *bs, Error **errp)
{
    BDRVPreallocateState *s = bs->opaque;
    int ret;

    if (s->data_end < 0) {
        return 0;
    }

    ret = preallocate_truncate_to_real_size(bs, errp);
    if (ret < 0) {
        return ret;
    }

    s->data_end = s->file_end = s->zero_start = -EINVAL;

    bdrv_child_refresh_perms(bs, bs->file, NULL);

    return 0;
}

static void preallocate_drop_resize_bh(void *opaque)
{
    GLOBAL_STATE_CODE();
    GRAPH_RDLOCK_GUARD_MAINLOOP();

    preallocate_drop_resize(opaque, NULL);
}

static void GRAPH_RDLOCK
preallocate_set_perm(BlockDriverState *bs, uint64_t perm, uint64_t shared)
{
    BDRVPreallocateState *s = bs->opaque;

    if (can_write_resize(perm)) {
        qemu_bh_cancel(s->drop_resize_bh);
        if (s->data_end < 0) {
            s->data_end = s->file_end = s->zero_start =
                bs->file->bs->total_sectors * BDRV_SECTOR_SIZE;
        }
    } else {
        qemu_bh_schedule(s->drop_resize_bh);
    }
}

static void preallocate_child_perm(BlockDriverState *bs, BdrvChild *c,
    BdrvChildRole role, BlockReopenQueue *reopen_queue,
    uint64_t perm, uint64_t shared, uint64_t *nperm, uint64_t *nshared)
{
    BDRVPreallocateState *s = bs->opaque;

    bdrv_default_perms(bs, c, role, reopen_queue, perm, shared, nperm, nshared);

    if (can_write_resize(perm) || s->data_end != -EINVAL) {
        *nperm |= BLK_PERM_WRITE | BLK_PERM_RESIZE;

        *nshared &= ~(BLK_PERM_WRITE | BLK_PERM_RESIZE);
    }
}

static BlockDriver bdrv_preallocate_filter = {
    .format_name = "preallocate",
    .instance_size = sizeof(BDRVPreallocateState),

    .bdrv_co_getlength    = preallocate_co_getlength,
    .bdrv_open            = preallocate_open,
    .bdrv_close           = preallocate_close,

    .bdrv_reopen_prepare  = preallocate_reopen_prepare,
    .bdrv_reopen_commit   = preallocate_reopen_commit,
    .bdrv_reopen_abort    = preallocate_reopen_abort,

    .bdrv_co_preadv_part = preallocate_co_preadv_part,
    .bdrv_co_pwritev_part = preallocate_co_pwritev_part,
    .bdrv_co_pwrite_zeroes = preallocate_co_pwrite_zeroes,
    .bdrv_co_pdiscard = preallocate_co_pdiscard,
    .bdrv_co_flush = preallocate_co_flush,
    .bdrv_co_truncate = preallocate_co_truncate,

    .bdrv_set_perm = preallocate_set_perm,
    .bdrv_child_perm = preallocate_child_perm,

    .is_filter = true,
};

static void bdrv_preallocate_init(void)
{
    bdrv_register(&bdrv_preallocate_filter);
}

block_init(bdrv_preallocate_init);
