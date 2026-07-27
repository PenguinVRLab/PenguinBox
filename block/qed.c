/*
 * QEMU Enhanced Disk Format
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Stefan Hajnoczi   <stefanha@linux.vnet.ibm.com>
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "block/qdict.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "qemu/bswap.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/memalign.h"
#include "trace.h"
#include "qed.h"
#include "system/block-backend.h"
#include "qobject/qdict.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qapi-visit-block-core.h"

static QemuOptsList qed_create_opts;

static int bdrv_qed_probe(const uint8_t *buf, int buf_size,
                          const char *filename)
{
    const QEDHeader *header = (const QEDHeader *)buf;

    if (buf_size < sizeof(*header)) {
        return 0;
    }
    if (le32_to_cpu(header->magic) != QED_MAGIC) {
        return 0;
    }
    return 100;
}

static bool qed_fmt_is_raw(const char *fmt)
{
    return fmt && strcmp(fmt, "raw") == 0;
}

static void qed_header_le_to_cpu(const QEDHeader *le, QEDHeader *cpu)
{
    cpu->magic = le32_to_cpu(le->magic);
    cpu->cluster_size = le32_to_cpu(le->cluster_size);
    cpu->table_size = le32_to_cpu(le->table_size);
    cpu->header_size = le32_to_cpu(le->header_size);
    cpu->features = le64_to_cpu(le->features);
    cpu->compat_features = le64_to_cpu(le->compat_features);
    cpu->autoclear_features = le64_to_cpu(le->autoclear_features);
    cpu->l1_table_offset = le64_to_cpu(le->l1_table_offset);
    cpu->image_size = le64_to_cpu(le->image_size);
    cpu->backing_filename_offset = le32_to_cpu(le->backing_filename_offset);
    cpu->backing_filename_size = le32_to_cpu(le->backing_filename_size);
}

static void qed_header_cpu_to_le(const QEDHeader *cpu, QEDHeader *le)
{
    le->magic = cpu_to_le32(cpu->magic);
    le->cluster_size = cpu_to_le32(cpu->cluster_size);
    le->table_size = cpu_to_le32(cpu->table_size);
    le->header_size = cpu_to_le32(cpu->header_size);
    le->features = cpu_to_le64(cpu->features);
    le->compat_features = cpu_to_le64(cpu->compat_features);
    le->autoclear_features = cpu_to_le64(cpu->autoclear_features);
    le->l1_table_offset = cpu_to_le64(cpu->l1_table_offset);
    le->image_size = cpu_to_le64(cpu->image_size);
    le->backing_filename_offset = cpu_to_le32(cpu->backing_filename_offset);
    le->backing_filename_size = cpu_to_le32(cpu->backing_filename_size);
}

int qed_write_header_sync(BDRVQEDState *s)
{
    QEDHeader le;

    qed_header_cpu_to_le(&s->header, &le);
    return bdrv_pwrite(s->bs->file, 0, sizeof(le), &le, 0);
}

static int coroutine_fn GRAPH_RDLOCK qed_write_header(BDRVQEDState *s)
{

    int nsectors = DIV_ROUND_UP(sizeof(QEDHeader), BDRV_SECTOR_SIZE);
    size_t len = nsectors * BDRV_SECTOR_SIZE;
    uint8_t *buf;
    int ret;

    assert(s->allocating_acb || s->allocating_write_reqs_plugged);

    buf = qemu_blockalign(s->bs, len);

    ret = bdrv_co_pread(s->bs->file, 0, len, buf, 0);
    if (ret < 0) {
        goto out;
    }

    qed_header_cpu_to_le(&s->header, (QEDHeader *) buf);

    ret = bdrv_co_pwrite(s->bs->file, 0, len,  buf, 0);
    if (ret < 0) {
        goto out;
    }

    ret = 0;
out:
    qemu_vfree(buf);
    return ret;
}

static uint64_t qed_max_image_size(uint32_t cluster_size, uint32_t table_size)
{
    uint64_t table_entries;
    uint64_t l2_size;

    table_entries = (table_size * cluster_size) / sizeof(uint64_t);
    l2_size = table_entries * cluster_size;

    return l2_size * table_entries;
}

static bool qed_is_cluster_size_valid(uint32_t cluster_size)
{
    if (cluster_size < QED_MIN_CLUSTER_SIZE ||
        cluster_size > QED_MAX_CLUSTER_SIZE) {
        return false;
    }
    if (cluster_size & (cluster_size - 1)) {
        return false;
    }
    return true;
}

static bool qed_is_table_size_valid(uint32_t table_size)
{
    if (table_size < QED_MIN_TABLE_SIZE ||
        table_size > QED_MAX_TABLE_SIZE) {
        return false;
    }
    if (table_size & (table_size - 1)) {
        return false;
    }
    return true;
}

static bool qed_is_image_size_valid(uint64_t image_size, uint32_t cluster_size,
                                    uint32_t table_size)
{
    if (image_size % BDRV_SECTOR_SIZE != 0) {
        return false;
    }
    if (image_size > qed_max_image_size(cluster_size, table_size)) {
        return false;
    }
    return true;
}

static int coroutine_fn GRAPH_RDLOCK
qed_read_string(BdrvChild *file, uint64_t offset,
                size_t n, char *buf, size_t buflen)
{
    int ret;
    if (n >= buflen) {
        return -EINVAL;
    }
    ret = bdrv_co_pread(file, offset, n, buf, 0);
    if (ret < 0) {
        return ret;
    }
    buf[n] = '\0';
    return 0;
}

static uint64_t qed_alloc_clusters(BDRVQEDState *s, unsigned int n)
{
    uint64_t offset = s->file_size;
    s->file_size += n * s->header.cluster_size;
    return offset;
}

QEDTable *qed_alloc_table(BDRVQEDState *s)
{
    return qemu_blockalign(s->bs,
                           s->header.cluster_size * s->header.table_size);
}

static CachedL2Table *qed_new_l2_table(BDRVQEDState *s)
{
    CachedL2Table *l2_table = qed_alloc_l2_cache_entry(&s->l2_cache);

    l2_table->table = qed_alloc_table(s);
    l2_table->offset = qed_alloc_clusters(s, s->header.table_size);

    memset(l2_table->table->offsets, 0,
           s->header.cluster_size * s->header.table_size);
    return l2_table;
}

static bool coroutine_fn qed_plug_allocating_write_reqs(BDRVQEDState *s)
{
    qemu_co_mutex_lock(&s->table_lock);

    assert(!s->allocating_write_reqs_plugged);
    if (s->allocating_acb != NULL) {
        qemu_co_mutex_unlock(&s->table_lock);
        return false;
    }

    s->allocating_write_reqs_plugged = true;
    qemu_co_mutex_unlock(&s->table_lock);
    return true;
}

static void coroutine_fn qed_unplug_allocating_write_reqs(BDRVQEDState *s)
{
    qemu_co_mutex_lock(&s->table_lock);
    assert(s->allocating_write_reqs_plugged);
    s->allocating_write_reqs_plugged = false;
    qemu_co_queue_next(&s->allocating_write_reqs);
    qemu_co_mutex_unlock(&s->table_lock);
}

static void coroutine_fn GRAPH_RDLOCK qed_need_check_timer(BDRVQEDState *s)
{
    int ret;

    trace_qed_need_check_timer_cb(s);
    assert_bdrv_graph_readable();

    if (!qed_plug_allocating_write_reqs(s)) {
        return;
    }

    ret = bdrv_co_flush(s->bs->file->bs);
    if (ret < 0) {
        qed_unplug_allocating_write_reqs(s);
        return;
    }

    s->header.features &= ~QED_F_NEED_CHECK;
    ret = qed_write_header(s);
    (void) ret;

    qed_unplug_allocating_write_reqs(s);

    ret = bdrv_co_flush(s->bs);
    (void) ret;
}

static void coroutine_fn qed_need_check_timer_entry(void *opaque)
{
    BDRVQEDState *s = opaque;
    GRAPH_RDLOCK_GUARD();

    qed_need_check_timer(opaque);
    bdrv_dec_in_flight(s->bs);
}

static void qed_need_check_timer_cb(void *opaque)
{
    BDRVQEDState *s = opaque;
    Coroutine *co = qemu_coroutine_create(qed_need_check_timer_entry, opaque);

    bdrv_inc_in_flight(s->bs);
    qemu_coroutine_enter(co);
}

static void qed_start_need_check_timer(BDRVQEDState *s)
{
    trace_qed_start_need_check_timer(s);

    timer_mod(s->need_check_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                   NANOSECONDS_PER_SECOND * QED_NEED_CHECK_TIMEOUT);
}

static void qed_cancel_need_check_timer(BDRVQEDState *s)
{
    trace_qed_cancel_need_check_timer(s);
    timer_del(s->need_check_timer);
}

static void bdrv_qed_detach_aio_context(BlockDriverState *bs)
{
    BDRVQEDState *s = bs->opaque;

    qed_cancel_need_check_timer(s);
    timer_free(s->need_check_timer);
    s->need_check_timer = NULL;
}

static void bdrv_qed_attach_aio_context(BlockDriverState *bs,
                                        AioContext *new_context)
{
    BDRVQEDState *s = bs->opaque;

    s->need_check_timer = aio_timer_new(new_context,
                                        QEMU_CLOCK_VIRTUAL, SCALE_NS,
                                        qed_need_check_timer_cb, s);
    if (s->header.features & QED_F_NEED_CHECK) {
        qed_start_need_check_timer(s);
    }
}

static void bdrv_qed_drain_begin(BlockDriverState *bs)
{
    BDRVQEDState *s = bs->opaque;

    if (s->need_check_timer && timer_pending(s->need_check_timer)) {
        Coroutine *co;

        qed_cancel_need_check_timer(s);
        co = qemu_coroutine_create(qed_need_check_timer_entry, s);
        bdrv_inc_in_flight(bs);
        aio_co_enter(bdrv_get_aio_context(bs), co);
    }
}

static void bdrv_qed_init_state(BlockDriverState *bs)
{
    BDRVQEDState *s = bs->opaque;

    memset(s, 0, sizeof(BDRVQEDState));
    s->bs = bs;
    qemu_co_mutex_init(&s->table_lock);
    qemu_co_queue_init(&s->allocating_write_reqs);
}

static int coroutine_fn GRAPH_RDLOCK
bdrv_qed_do_open(BlockDriverState *bs, QDict *options, int flags, Error **errp)
{
    BDRVQEDState *s = bs->opaque;
    QEDHeader le_header;
    int64_t file_size;
    int ret;

    ret = bdrv_co_pread(bs->file, 0, sizeof(le_header), &le_header, 0);
    if (ret < 0) {
        error_setg(errp, "Failed to read QED header");
        return ret;
    }
    qed_header_le_to_cpu(&le_header, &s->header);

    if (s->header.magic != QED_MAGIC) {
        error_setg(errp, "Image not in QED format");
        return -EINVAL;
    }
    if (s->header.features & ~QED_FEATURE_MASK) {
        error_setg(errp, "Unsupported QED features: %" PRIx64,
                   s->header.features & ~QED_FEATURE_MASK);
        return -ENOTSUP;
    }
    if (!qed_is_cluster_size_valid(s->header.cluster_size)) {
        error_setg(errp, "QED cluster size is invalid");
        return -EINVAL;
    }

    file_size = bdrv_co_getlength(bs->file->bs);
    if (file_size < 0) {
        error_setg(errp, "Failed to get file length");
        return file_size;
    }
    s->file_size = qed_start_of_cluster(s, file_size);

    if (!qed_is_table_size_valid(s->header.table_size)) {
        error_setg(errp, "QED table size is invalid");
        return -EINVAL;
    }
    if (!qed_is_image_size_valid(s->header.image_size,
                                 s->header.cluster_size,
                                 s->header.table_size)) {
        error_setg(errp, "QED image size is invalid");
        return -EINVAL;
    }
    if (!qed_check_table_offset(s, s->header.l1_table_offset)) {
        error_setg(errp, "QED table offset is invalid");
        return -EINVAL;
    }

    s->table_nelems = (s->header.cluster_size * s->header.table_size) /
                      sizeof(uint64_t);
    s->l2_shift = ctz32(s->header.cluster_size);
    s->l2_mask = s->table_nelems - 1;
    s->l1_shift = s->l2_shift + ctz32(s->table_nelems);

    if (s->header.header_size > UINT32_MAX / s->header.cluster_size) {
        error_setg(errp, "QED header size is too large");
        return -EINVAL;
    }

    if ((s->header.features & QED_F_BACKING_FILE)) {
        g_autofree char *backing_file_str = NULL;

        if ((uint64_t)s->header.backing_filename_offset +
            s->header.backing_filename_size >
            s->header.cluster_size * s->header.header_size) {
            error_setg(errp, "QED backing filename offset is invalid");
            return -EINVAL;
        }

        backing_file_str = g_malloc(sizeof(bs->backing_file));
        ret = qed_read_string(bs->file, s->header.backing_filename_offset,
                              s->header.backing_filename_size,
                              backing_file_str, sizeof(bs->backing_file));
        if (ret < 0) {
            error_setg(errp, "Failed to read backing filename");
            return ret;
        }

        if (!g_str_equal(backing_file_str, bs->backing_file)) {
            pstrcpy(bs->backing_file, sizeof(bs->backing_file),
                    backing_file_str);
            pstrcpy(bs->auto_backing_file, sizeof(bs->auto_backing_file),
                    backing_file_str);
        }

        if (s->header.features & QED_F_BACKING_FORMAT_NO_PROBE) {
            pstrcpy(bs->backing_format, sizeof(bs->backing_format), "raw");
        }
    }

    if ((s->header.autoclear_features & ~QED_AUTOCLEAR_FEATURE_MASK) != 0 &&
        !bdrv_is_read_only(bs->file->bs) && !(flags & BDRV_O_INACTIVE)) {
        s->header.autoclear_features &= QED_AUTOCLEAR_FEATURE_MASK;

        ret = qed_write_header_sync(s);
        if (ret) {
            error_setg(errp, "Failed to update header");
            return ret;
        }

        bdrv_co_flush(bs->file->bs);
    }

    s->l1_table = qed_alloc_table(s);
    qed_init_l2_cache(&s->l2_cache);

    ret = qed_read_l1_table_sync(s);
    if (ret) {
        error_setg(errp, "Failed to read L1 table");
        goto out;
    }

    if (!(flags & BDRV_O_CHECK) && (s->header.features & QED_F_NEED_CHECK)) {
        if (!bdrv_is_read_only(bs->file->bs) &&
            !(flags & BDRV_O_INACTIVE)) {
            BdrvCheckResult result = {0};

            ret = qed_check(s, &result, true);
            if (ret) {
                error_setg(errp, "Image corrupted");
                goto out;
            }
        }
    }

    bdrv_qed_attach_aio_context(bs, bdrv_get_aio_context(bs));

out:
    if (ret) {
        qed_free_l2_cache(&s->l2_cache);
        qemu_vfree(s->l1_table);
    }
    return ret;
}

typedef struct QEDOpenCo {
    BlockDriverState *bs;
    QDict *options;
    int flags;
    Error **errp;
    int ret;
} QEDOpenCo;

static void coroutine_fn bdrv_qed_open_entry(void *opaque)
{
    QEDOpenCo *qoc = opaque;
    BDRVQEDState *s = qoc->bs->opaque;

    GRAPH_RDLOCK_GUARD();

    qemu_co_mutex_lock(&s->table_lock);
    qoc->ret = bdrv_qed_do_open(qoc->bs, qoc->options, qoc->flags, qoc->errp);
    qemu_co_mutex_unlock(&s->table_lock);
}

static int coroutine_mixed_fn bdrv_qed_open(BlockDriverState *bs, QDict *options,
                                            int flags, Error **errp)
{
    QEDOpenCo qoc = {
        .bs = bs,
        .options = options,
        .flags = flags,
        .errp = errp,
        .ret = -EINPROGRESS
    };
    int ret;

    ret = bdrv_open_file_child(NULL, options, "file", bs, errp);
    if (ret < 0) {
        return ret;
    }

    bdrv_qed_init_state(bs);
    assert(!qemu_in_coroutine());
    assert(qemu_get_current_aio_context() == qemu_get_aio_context());
    qemu_coroutine_enter(qemu_coroutine_create(bdrv_qed_open_entry, &qoc));
    BDRV_POLL_WHILE(bs, qoc.ret == -EINPROGRESS);

    return qoc.ret;
}

static void bdrv_qed_refresh_limits(BlockDriverState *bs, Error **errp)
{
    BDRVQEDState *s = bs->opaque;

    bs->bl.pwrite_zeroes_alignment = s->header.cluster_size;
    bs->bl.max_pwrite_zeroes = QEMU_ALIGN_DOWN(INT_MAX, s->header.cluster_size);
}

static int bdrv_qed_reopen_prepare(BDRVReopenState *state,
                                   BlockReopenQueue *queue, Error **errp)
{
    return 0;
}

static void GRAPH_RDLOCK bdrv_qed_do_close(BlockDriverState *bs)
{
    BDRVQEDState *s = bs->opaque;

    bdrv_qed_detach_aio_context(bs);

    bdrv_flush(bs->file->bs);

    if (s->header.features & QED_F_NEED_CHECK) {
        s->header.features &= ~QED_F_NEED_CHECK;
        qed_write_header_sync(s);
    }

    qed_free_l2_cache(&s->l2_cache);
    qemu_vfree(s->l1_table);
}

static void GRAPH_UNLOCKED bdrv_qed_close(BlockDriverState *bs)
{
    GLOBAL_STATE_CODE();
    GRAPH_RDLOCK_GUARD_MAINLOOP();

    bdrv_qed_do_close(bs);
}

static int coroutine_fn GRAPH_UNLOCKED
bdrv_qed_co_create(BlockdevCreateOptions *opts, Error **errp)
{
    BlockdevCreateOptionsQed *qed_opts;
    BlockBackend *blk = NULL;
    BlockDriverState *bs = NULL;

    QEDHeader header;
    QEDHeader le_header;
    uint8_t *l1_table = NULL;
    size_t l1_size;
    int ret = 0;

    assert(opts->driver == BLOCKDEV_DRIVER_QED);
    qed_opts = &opts->u.qed;

    if (!qed_opts->has_cluster_size) {
        qed_opts->cluster_size = QED_DEFAULT_CLUSTER_SIZE;
    }
    if (!qed_opts->has_table_size) {
        qed_opts->table_size = QED_DEFAULT_TABLE_SIZE;
    }

    if (!qed_is_cluster_size_valid(qed_opts->cluster_size)) {
        error_setg(errp, "QED cluster size must be within range [%u, %u] "
                         "and power of 2",
                   QED_MIN_CLUSTER_SIZE, QED_MAX_CLUSTER_SIZE);
        return -EINVAL;
    }
    if (!qed_is_table_size_valid(qed_opts->table_size)) {
        error_setg(errp, "QED table size must be within range [%u, %u] "
                         "and power of 2",
                   QED_MIN_TABLE_SIZE, QED_MAX_TABLE_SIZE);
        return -EINVAL;
    }
    if (!qed_is_image_size_valid(qed_opts->size, qed_opts->cluster_size,
                                 qed_opts->table_size))
    {
        error_setg(errp, "QED image size must be a non-zero multiple of "
                         "cluster size and less than %" PRIu64 " bytes",
                   qed_max_image_size(qed_opts->cluster_size,
                                      qed_opts->table_size));
        return -EINVAL;
    }

    bs = bdrv_co_open_blockdev_ref(qed_opts->file, errp);
    if (bs == NULL) {
        return -EIO;
    }

    blk = blk_co_new_with_bs(bs, BLK_PERM_WRITE | BLK_PERM_RESIZE, BLK_PERM_ALL,
                             errp);
    if (!blk) {
        ret = -EPERM;
        goto out;
    }
    blk_set_allow_write_beyond_eof(blk, true);

    header = (QEDHeader) {
        .magic = QED_MAGIC,
        .cluster_size = qed_opts->cluster_size,
        .table_size = qed_opts->table_size,
        .header_size = 1,
        .features = 0,
        .compat_features = 0,
        .l1_table_offset = qed_opts->cluster_size,
        .image_size = qed_opts->size,
    };

    l1_size = header.cluster_size * header.table_size;

    ret = blk_co_truncate(blk, 0, true, PREALLOC_MODE_OFF, 0, errp);
    if (ret < 0) {
        goto out;
    }

    if (qed_opts->backing_file) {
        header.features |= QED_F_BACKING_FILE;
        header.backing_filename_offset = sizeof(le_header);
        header.backing_filename_size = strlen(qed_opts->backing_file);

        if (qed_opts->has_backing_fmt) {
            const char *backing_fmt = BlockdevDriver_str(qed_opts->backing_fmt);
            if (qed_fmt_is_raw(backing_fmt)) {
                header.features |= QED_F_BACKING_FORMAT_NO_PROBE;
            }
        }
    }

    qed_header_cpu_to_le(&header, &le_header);
    ret = blk_co_pwrite(blk, 0, sizeof(le_header), &le_header, 0);
    if (ret < 0) {
        goto out;
    }
    ret = blk_co_pwrite(blk, sizeof(le_header), header.backing_filename_size,
                     qed_opts->backing_file, 0);
    if (ret < 0) {
        goto out;
    }

    l1_table = g_malloc0(l1_size);
    ret = blk_co_pwrite(blk, header.l1_table_offset, l1_size, l1_table, 0);
    if (ret < 0) {
        goto out;
    }

    ret = 0;
out:
    g_free(l1_table);
    blk_co_unref(blk);
    bdrv_co_unref(bs);
    return ret;
}

static int coroutine_fn GRAPH_UNLOCKED
bdrv_qed_co_create_opts(BlockDriver *drv, const char *filename,
                        QemuOpts *opts, Error **errp)
{
    BlockdevCreateOptions *create_options = NULL;
    QDict *qdict;
    Visitor *v;
    BlockDriverState *bs = NULL;
    int ret;

    static const QDictRenames opt_renames[] = {
        { BLOCK_OPT_BACKING_FILE,       "backing-file" },
        { BLOCK_OPT_BACKING_FMT,        "backing-fmt" },
        { BLOCK_OPT_CLUSTER_SIZE,       "cluster-size" },
        { BLOCK_OPT_TABLE_SIZE,         "table-size" },
        { NULL, NULL },
    };

    qdict = qemu_opts_to_qdict_filtered(opts, NULL, &qed_create_opts, true);

    if (!qdict_rename_keys(qdict, opt_renames, errp)) {
        ret = -EINVAL;
        goto fail;
    }

    ret = bdrv_co_create_file(filename, opts, true, errp);
    if (ret < 0) {
        goto fail;
    }

    bs = bdrv_co_open(filename, NULL, NULL,
                      BDRV_O_RDWR | BDRV_O_RESIZE | BDRV_O_PROTOCOL, errp);
    if (bs == NULL) {
        ret = -EIO;
        goto fail;
    }

    qdict_put_str(qdict, "driver", "qed");
    qdict_put_str(qdict, "file", bs->node_name);

    v = qobject_input_visitor_new_flat_confused(qdict, errp);
    if (!v) {
        ret = -EINVAL;
        goto fail;
    }

    visit_type_BlockdevCreateOptions(v, NULL, &create_options, errp);
    visit_free(v);
    if (!create_options) {
        ret = -EINVAL;
        goto fail;
    }

    assert(create_options->driver == BLOCKDEV_DRIVER_QED);
    create_options->u.qed.size =
        ROUND_UP(create_options->u.qed.size, BDRV_SECTOR_SIZE);

    ret = bdrv_qed_co_create(create_options, errp);

fail:
    qobject_unref(qdict);
    bdrv_co_unref(bs);
    qapi_free_BlockdevCreateOptions(create_options);
    return ret;
}

static int coroutine_fn GRAPH_RDLOCK
bdrv_qed_co_block_status(BlockDriverState *bs, unsigned int mode,
                         int64_t pos, int64_t bytes, int64_t *pnum,
                         int64_t *map, BlockDriverState **file)
{
    BDRVQEDState *s = bs->opaque;
    size_t len = MIN(bytes, SIZE_MAX);
    int status;
    QEDRequest request = { .l2_table = NULL };
    uint64_t offset;
    int ret;

    qemu_co_mutex_lock(&s->table_lock);
    ret = qed_find_cluster(s, &request, pos, &len, &offset);

    *pnum = len;
    switch (ret) {
    case QED_CLUSTER_FOUND:
        *map = offset | qed_offset_into_cluster(s, pos);
        status = BDRV_BLOCK_DATA | BDRV_BLOCK_OFFSET_VALID;
        *file = bs->file->bs;
        break;
    case QED_CLUSTER_ZERO:
        status = BDRV_BLOCK_ZERO;
        break;
    case QED_CLUSTER_L2:
    case QED_CLUSTER_L1:
        status = 0;
        break;
    default:
        assert(ret < 0);
        status = ret;
        break;
    }

    qed_unref_l2_cache_entry(request.l2_table);
    qemu_co_mutex_unlock(&s->table_lock);

    return status;
}

static BDRVQEDState *acb_to_s(QEDAIOCB *acb)
{
    return acb->bs->opaque;
}

static int coroutine_fn GRAPH_RDLOCK
qed_read_backing_file(BDRVQEDState *s, uint64_t pos, QEMUIOVector *qiov)
{
    if (s->bs->backing) {
        BLKDBG_CO_EVENT(s->bs->file, BLKDBG_READ_BACKING_AIO);
        return bdrv_co_preadv(s->bs->backing, pos, qiov->size, qiov, 0);
    }
    qemu_iovec_memset(qiov, 0, 0, qiov->size);
    return 0;
}

static int coroutine_fn GRAPH_RDLOCK
qed_copy_from_backing_file(BDRVQEDState *s, uint64_t pos, uint64_t len,
                           uint64_t offset)
{
    QEMUIOVector qiov;
    int ret;

    if (len == 0) {
        return 0;
    }

    qemu_iovec_init_buf(&qiov, qemu_blockalign(s->bs, len), len);

    ret = qed_read_backing_file(s, pos, &qiov);

    if (ret) {
        goto out;
    }

    BLKDBG_CO_EVENT(s->bs->file, BLKDBG_COW_WRITE);
    ret = bdrv_co_pwritev(s->bs->file, offset, qiov.size, &qiov, 0);
    if (ret < 0) {
        goto out;
    }
    ret = 0;
out:
    qemu_vfree(qemu_iovec_buf(&qiov));
    return ret;
}

static void coroutine_fn qed_update_l2_table(BDRVQEDState *s, QEDTable *table,
                                             int index, unsigned int n,
                                             uint64_t cluster)
{
    int i;
    for (i = index; i < index + n; i++) {
        table->offsets[i] = cluster;
        if (!qed_offset_is_unalloc_cluster(cluster) &&
            !qed_offset_is_zero_cluster(cluster)) {
            cluster += s->header.cluster_size;
        }
    }
}

static void coroutine_fn qed_aio_complete(QEDAIOCB *acb)
{
    BDRVQEDState *s = acb_to_s(acb);

    qemu_iovec_destroy(&acb->cur_qiov);
    qed_unref_l2_cache_entry(acb->request.l2_table);

    if (acb->flags & QED_AIOCB_ZERO) {
        qemu_vfree(acb->qiov->iov[0].iov_base);
        acb->qiov->iov[0].iov_base = NULL;
    }

    if (acb == s->allocating_acb) {
        s->allocating_acb = NULL;
        if (!qemu_co_queue_empty(&s->allocating_write_reqs)) {
            qemu_co_queue_next(&s->allocating_write_reqs);
        } else if (s->header.features & QED_F_NEED_CHECK) {
            qed_start_need_check_timer(s);
        }
    }
}

static int coroutine_fn GRAPH_RDLOCK qed_aio_write_l1_update(QEDAIOCB *acb)
{
    BDRVQEDState *s = acb_to_s(acb);
    CachedL2Table *l2_table = acb->request.l2_table;
    uint64_t l2_offset = l2_table->offset;
    int index, ret;

    index = qed_l1_index(s, acb->cur_pos);
    s->l1_table->offsets[index] = l2_table->offset;

    ret = qed_write_l1_table(s, index, 1);

    qed_commit_l2_cache_entry(&s->l2_cache, l2_table);

    acb->request.l2_table = qed_find_l2_cache_entry(&s->l2_cache, l2_offset);
    assert(acb->request.l2_table != NULL);

    return ret;
}


static int coroutine_fn GRAPH_RDLOCK
qed_aio_write_l2_update(QEDAIOCB *acb, uint64_t offset)
{
    BDRVQEDState *s = acb_to_s(acb);
    bool need_alloc = acb->find_cluster_ret == QED_CLUSTER_L1;
    int index, ret;

    if (need_alloc) {
        qed_unref_l2_cache_entry(acb->request.l2_table);
        acb->request.l2_table = qed_new_l2_table(s);
    }

    index = qed_l2_index(s, acb->cur_pos);
    qed_update_l2_table(s, acb->request.l2_table->table, index, acb->cur_nclusters,
                         offset);

    if (need_alloc) {
        ret = qed_write_l2_table(s, &acb->request, 0, s->table_nelems, true);
        if (ret) {
            return ret;
        }
        return qed_aio_write_l1_update(acb);
    } else {
        ret = qed_write_l2_table(s, &acb->request, index, acb->cur_nclusters,
                                 false);
        if (ret) {
            return ret;
        }
    }
    return 0;
}

static int coroutine_fn GRAPH_RDLOCK qed_aio_write_main(QEDAIOCB *acb)
{
    BDRVQEDState *s = acb_to_s(acb);
    uint64_t offset = acb->cur_cluster +
                      qed_offset_into_cluster(s, acb->cur_pos);

    trace_qed_aio_write_main(s, acb, 0, offset, acb->cur_qiov.size);

    BLKDBG_CO_EVENT(s->bs->file, BLKDBG_WRITE_AIO);
    return bdrv_co_pwritev(s->bs->file, offset, acb->cur_qiov.size,
                           &acb->cur_qiov, 0);
}

static int coroutine_fn GRAPH_RDLOCK qed_aio_write_cow(QEDAIOCB *acb)
{
    BDRVQEDState *s = acb_to_s(acb);
    uint64_t start, len, offset;
    int ret;

    qemu_co_mutex_unlock(&s->table_lock);

    start = qed_start_of_cluster(s, acb->cur_pos);
    len = qed_offset_into_cluster(s, acb->cur_pos);

    trace_qed_aio_write_prefill(s, acb, start, len, acb->cur_cluster);
    ret = qed_copy_from_backing_file(s, start, len, acb->cur_cluster);
    if (ret < 0) {
        goto out;
    }

    start = acb->cur_pos + acb->cur_qiov.size;
    len = qed_start_of_cluster(s, start + s->header.cluster_size - 1) - start;
    offset = acb->cur_cluster +
             qed_offset_into_cluster(s, acb->cur_pos) +
             acb->cur_qiov.size;

    trace_qed_aio_write_postfill(s, acb, start, len, offset);
    ret = qed_copy_from_backing_file(s, start, len, offset);
    if (ret < 0) {
        goto out;
    }

    ret = qed_aio_write_main(acb);
    if (ret < 0) {
        goto out;
    }

    if (s->bs->backing) {
        ret = bdrv_co_flush(s->bs->file->bs);
    }

out:
    qemu_co_mutex_lock(&s->table_lock);
    return ret;
}

static bool GRAPH_RDLOCK qed_should_set_need_check(BDRVQEDState *s)
{
    if (s->bs->backing) {
        return false;
    }

    return !(s->header.features & QED_F_NEED_CHECK);
}

static int coroutine_fn GRAPH_RDLOCK
qed_aio_write_alloc(QEDAIOCB *acb, size_t len)
{
    BDRVQEDState *s = acb_to_s(acb);
    int ret;

    if (s->allocating_acb == NULL) {
        qed_cancel_need_check_timer(s);
    }

    if (s->allocating_acb != acb || s->allocating_write_reqs_plugged) {
        if (s->allocating_acb != NULL) {
            qemu_co_queue_wait(&s->allocating_write_reqs, &s->table_lock);
            assert(s->allocating_acb == NULL);
        }
        s->allocating_acb = acb;
        return -EAGAIN;
    }

    acb->cur_nclusters = qed_bytes_to_clusters(s,
            qed_offset_into_cluster(s, acb->cur_pos) + len);
    qemu_iovec_concat(&acb->cur_qiov, acb->qiov, acb->qiov_offset, len);

    if (acb->flags & QED_AIOCB_ZERO) {
        if (acb->find_cluster_ret == QED_CLUSTER_ZERO) {
            return 0;
        }
        acb->cur_cluster = 1;
    } else {
        acb->cur_cluster = qed_alloc_clusters(s, acb->cur_nclusters);
    }

    if (qed_should_set_need_check(s)) {
        s->header.features |= QED_F_NEED_CHECK;
        ret = qed_write_header(s);
        if (ret < 0) {
            return ret;
        }
    }

    if (!(acb->flags & QED_AIOCB_ZERO)) {
        ret = qed_aio_write_cow(acb);
        if (ret < 0) {
            return ret;
        }
    }

    return qed_aio_write_l2_update(acb, acb->cur_cluster);
}

static int coroutine_fn GRAPH_RDLOCK
qed_aio_write_inplace(QEDAIOCB *acb, uint64_t offset, size_t len)
{
    BDRVQEDState *s = acb_to_s(acb);
    int r;

    qemu_co_mutex_unlock(&s->table_lock);

    if (acb->flags & QED_AIOCB_ZERO) {
        struct iovec *iov = acb->qiov->iov;

        if (!iov->iov_base) {
            iov->iov_base = qemu_try_blockalign(acb->bs, iov->iov_len);
            if (iov->iov_base == NULL) {
                r = -ENOMEM;
                goto out;
            }
            memset(iov->iov_base, 0, iov->iov_len);
        }
    }

    acb->cur_cluster = offset;
    qemu_iovec_concat(&acb->cur_qiov, acb->qiov, acb->qiov_offset, len);

    r = qed_aio_write_main(acb);
out:
    qemu_co_mutex_lock(&s->table_lock);
    return r;
}

static int coroutine_fn GRAPH_RDLOCK
qed_aio_write_data(void *opaque, int ret, uint64_t offset, size_t len)
{
    QEDAIOCB *acb = opaque;

    trace_qed_aio_write_data(acb_to_s(acb), acb, ret, offset, len);

    acb->find_cluster_ret = ret;

    switch (ret) {
    case QED_CLUSTER_FOUND:
        return qed_aio_write_inplace(acb, offset, len);

    case QED_CLUSTER_L2:
    case QED_CLUSTER_L1:
    case QED_CLUSTER_ZERO:
        return qed_aio_write_alloc(acb, len);

    default:
        g_assert_not_reached();
    }
}

static int coroutine_fn GRAPH_RDLOCK
qed_aio_read_data(void *opaque, int ret, uint64_t offset, size_t len)
{
    QEDAIOCB *acb = opaque;
    BDRVQEDState *s = acb_to_s(acb);
    BlockDriverState *bs = acb->bs;
    int r;

    qemu_co_mutex_unlock(&s->table_lock);

    offset += qed_offset_into_cluster(s, acb->cur_pos);

    trace_qed_aio_read_data(s, acb, ret, offset, len);

    qemu_iovec_concat(&acb->cur_qiov, acb->qiov, acb->qiov_offset, len);

    if (ret == QED_CLUSTER_ZERO) {
        qemu_iovec_memset(&acb->cur_qiov, 0, 0, acb->cur_qiov.size);
        r = 0;
    } else if (ret != QED_CLUSTER_FOUND) {
        r = qed_read_backing_file(s, acb->cur_pos, &acb->cur_qiov);
    } else {
        BLKDBG_CO_EVENT(bs->file, BLKDBG_READ_AIO);
        r = bdrv_co_preadv(bs->file, offset, acb->cur_qiov.size,
                           &acb->cur_qiov, 0);
    }

    qemu_co_mutex_lock(&s->table_lock);
    return r;
}

static int coroutine_fn GRAPH_RDLOCK qed_aio_next_io(QEDAIOCB *acb)
{
    BDRVQEDState *s = acb_to_s(acb);
    uint64_t offset;
    size_t len;
    int ret;

    qemu_co_mutex_lock(&s->table_lock);
    while (1) {
        trace_qed_aio_next_io(s, acb, 0, acb->cur_pos + acb->cur_qiov.size);

        acb->qiov_offset += acb->cur_qiov.size;
        acb->cur_pos += acb->cur_qiov.size;
        qemu_iovec_reset(&acb->cur_qiov);

        if (acb->cur_pos >= acb->end_pos) {
            ret = 0;
            break;
        }

        len = acb->end_pos - acb->cur_pos;
        ret = qed_find_cluster(s, &acb->request, acb->cur_pos, &len, &offset);
        if (ret < 0) {
            break;
        }

        if (acb->flags & QED_AIOCB_WRITE) {
            ret = qed_aio_write_data(acb, ret, offset, len);
        } else {
            ret = qed_aio_read_data(acb, ret, offset, len);
        }

        if (ret < 0 && ret != -EAGAIN) {
            break;
        }
    }

    trace_qed_aio_complete(s, acb, ret);
    qed_aio_complete(acb);
    qemu_co_mutex_unlock(&s->table_lock);
    return ret;
}

static int coroutine_fn GRAPH_RDLOCK
qed_co_request(BlockDriverState *bs, int64_t sector_num, QEMUIOVector *qiov,
               int nb_sectors, int flags)
{
    QEDAIOCB acb = {
        .bs         = bs,
        .cur_pos    = (uint64_t) sector_num * BDRV_SECTOR_SIZE,
        .end_pos    = (sector_num + nb_sectors) * BDRV_SECTOR_SIZE,
        .qiov       = qiov,
        .flags      = flags,
    };
    qemu_iovec_init(&acb.cur_qiov, qiov->niov);

    trace_qed_aio_setup(bs->opaque, &acb, sector_num, nb_sectors, NULL, flags);

    return qed_aio_next_io(&acb);
}

static int coroutine_fn GRAPH_RDLOCK
bdrv_qed_co_readv(BlockDriverState *bs, int64_t sector_num, int nb_sectors,
                  QEMUIOVector *qiov)
{
    return qed_co_request(bs, sector_num, qiov, nb_sectors, 0);
}

static int coroutine_fn GRAPH_RDLOCK
bdrv_qed_co_writev(BlockDriverState *bs, int64_t sector_num, int nb_sectors,
                   QEMUIOVector *qiov, int flags)
{
    return qed_co_request(bs, sector_num, qiov, nb_sectors, QED_AIOCB_WRITE);
}

static int coroutine_fn GRAPH_RDLOCK
bdrv_qed_co_pwrite_zeroes(BlockDriverState *bs, int64_t offset, int64_t bytes,
                          BdrvRequestFlags flags)
{
    BDRVQEDState *s = bs->opaque;

    QEMUIOVector qiov = QEMU_IOVEC_INIT_BUF(qiov, NULL, bytes);

    assert(bytes <= INT_MAX);

    if (qed_offset_into_cluster(s, offset) ||
        qed_offset_into_cluster(s, bytes)) {
        return -ENOTSUP;
    }

    return qed_co_request(bs, offset >> BDRV_SECTOR_BITS, &qiov,
                          bytes >> BDRV_SECTOR_BITS,
                          QED_AIOCB_WRITE | QED_AIOCB_ZERO);
}

static int coroutine_fn GRAPH_RDLOCK
bdrv_qed_co_truncate(BlockDriverState *bs, int64_t offset, bool exact,
                     PreallocMode prealloc, BdrvRequestFlags flags,
                     Error **errp)
{
    BDRVQEDState *s = bs->opaque;
    uint64_t old_image_size;
    int ret;

    if (prealloc != PREALLOC_MODE_OFF) {
        error_setg(errp, "Unsupported preallocation mode '%s'",
                   PreallocMode_str(prealloc));
        return -ENOTSUP;
    }

    if (!qed_is_image_size_valid(offset, s->header.cluster_size,
                                 s->header.table_size)) {
        error_setg(errp, "Invalid image size specified");
        return -EINVAL;
    }

    if ((uint64_t)offset < s->header.image_size) {
        error_setg(errp, "Shrinking images is currently not supported");
        return -ENOTSUP;
    }

    old_image_size = s->header.image_size;
    s->header.image_size = offset;
    ret = qed_write_header_sync(s);
    if (ret < 0) {
        s->header.image_size = old_image_size;
        error_setg_errno(errp, -ret, "Failed to update the image size");
    }
    return ret;
}

static int64_t coroutine_fn bdrv_qed_co_getlength(BlockDriverState *bs)
{
    BDRVQEDState *s = bs->opaque;
    return s->header.image_size;
}

static int coroutine_fn
bdrv_qed_co_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    BDRVQEDState *s = bs->opaque;

    memset(bdi, 0, sizeof(*bdi));
    bdi->cluster_size = s->header.cluster_size;
    bdi->is_dirty = s->header.features & QED_F_NEED_CHECK;
    return 0;
}

static int coroutine_fn GRAPH_RDLOCK
bdrv_qed_co_change_backing_file(BlockDriverState *bs, const char *backing_file,
                                const char *backing_fmt)
{
    BDRVQEDState *s = bs->opaque;
    QEDHeader new_header, le_header;
    void *buffer;
    size_t buffer_len, backing_file_len;
    int ret;

    if (backing_file && (s->header.compat_features &
                         ~QED_COMPAT_FEATURE_MASK)) {
        return -ENOTSUP;
    }

    memcpy(&new_header, &s->header, sizeof(new_header));

    new_header.features &= ~(QED_F_BACKING_FILE |
                             QED_F_BACKING_FORMAT_NO_PROBE);

    if (backing_file) {
        new_header.features |= QED_F_BACKING_FILE;

        if (qed_fmt_is_raw(backing_fmt)) {
            new_header.features |= QED_F_BACKING_FORMAT_NO_PROBE;
        }
    }

    backing_file_len = 0;

    if (backing_file) {
        backing_file_len = strlen(backing_file);
    }

    buffer_len = sizeof(new_header);
    new_header.backing_filename_offset = buffer_len;
    new_header.backing_filename_size = backing_file_len;
    buffer_len += backing_file_len;

    if (buffer_len > new_header.header_size * new_header.cluster_size) {
        return -ENOSPC;
    }

    buffer = g_malloc(buffer_len);

    qed_header_cpu_to_le(&new_header, &le_header);
    memcpy(buffer, &le_header, sizeof(le_header));
    buffer_len = sizeof(le_header);

    if (backing_file) {
        memcpy(buffer + buffer_len, backing_file, backing_file_len);
        buffer_len += backing_file_len;
    }

    ret = bdrv_co_pwrite_sync(bs->file, 0, buffer_len, buffer, 0);
    g_free(buffer);
    if (ret == 0) {
        memcpy(&s->header, &new_header, sizeof(new_header));
    }
    return ret;
}

static void coroutine_fn GRAPH_RDLOCK
bdrv_qed_co_invalidate_cache(BlockDriverState *bs, Error **errp)
{
    ERRP_GUARD();
    BDRVQEDState *s = bs->opaque;
    int ret;

    bdrv_qed_do_close(bs);

    bdrv_qed_init_state(bs);
    qemu_co_mutex_lock(&s->table_lock);
    ret = bdrv_qed_do_open(bs, NULL, bs->open_flags, errp);
    qemu_co_mutex_unlock(&s->table_lock);
    if (ret < 0) {
        error_prepend(errp, "Could not reopen qed layer: ");
    }
}

static int coroutine_fn GRAPH_RDLOCK
bdrv_qed_co_check(BlockDriverState *bs, BdrvCheckResult *result,
                  BdrvCheckMode fix)
{
    BDRVQEDState *s = bs->opaque;
    int ret;

    qemu_co_mutex_lock(&s->table_lock);
    ret = qed_check(s, result, !!fix);
    qemu_co_mutex_unlock(&s->table_lock);

    return ret;
}

static QemuOptsList qed_create_opts = {
    .name = "qed-create-opts",
    .head = QTAILQ_HEAD_INITIALIZER(qed_create_opts.head),
    .desc = {
        {
            .name = BLOCK_OPT_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Virtual disk size"
        },
        {
            .name = BLOCK_OPT_BACKING_FILE,
            .type = QEMU_OPT_STRING,
            .help = "File name of a base image"
        },
        {
            .name = BLOCK_OPT_BACKING_FMT,
            .type = QEMU_OPT_STRING,
            .help = "Image format of the base image"
        },
        {
            .name = BLOCK_OPT_CLUSTER_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Cluster size (in bytes)",
            .def_value_str = stringify(QED_DEFAULT_CLUSTER_SIZE)
        },
        {
            .name = BLOCK_OPT_TABLE_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "L1/L2 table size (in clusters)"
        },
        { }
    }
};

static BlockDriver bdrv_qed = {
    .format_name                    = "qed",
    .instance_size                  = sizeof(BDRVQEDState),
    .create_opts                    = &qed_create_opts,
    .is_format                      = true,
    .supports_backing               = true,

    .bdrv_probe                     = bdrv_qed_probe,
    .bdrv_open                      = bdrv_qed_open,
    .bdrv_close                     = bdrv_qed_close,
    .bdrv_reopen_prepare            = bdrv_qed_reopen_prepare,
    .bdrv_child_perm                = bdrv_default_perms,
    .bdrv_co_create                 = bdrv_qed_co_create,
    .bdrv_co_create_opts            = bdrv_qed_co_create_opts,
    .bdrv_has_zero_init             = bdrv_has_zero_init_1,
    .bdrv_co_block_status           = bdrv_qed_co_block_status,
    .bdrv_co_readv                  = bdrv_qed_co_readv,
    .bdrv_co_writev                 = bdrv_qed_co_writev,
    .bdrv_co_pwrite_zeroes          = bdrv_qed_co_pwrite_zeroes,
    .bdrv_co_truncate               = bdrv_qed_co_truncate,
    .bdrv_co_getlength              = bdrv_qed_co_getlength,
    .bdrv_co_get_info               = bdrv_qed_co_get_info,
    .bdrv_refresh_limits            = bdrv_qed_refresh_limits,
    .bdrv_co_change_backing_file    = bdrv_qed_co_change_backing_file,
    .bdrv_co_invalidate_cache       = bdrv_qed_co_invalidate_cache,
    .bdrv_co_check                  = bdrv_qed_co_check,
    .bdrv_detach_aio_context        = bdrv_qed_detach_aio_context,
    .bdrv_attach_aio_context        = bdrv_qed_attach_aio_context,
    .bdrv_drain_begin               = bdrv_qed_drain_begin,
};

static void bdrv_qed_init(void)
{
    bdrv_register(&bdrv_qed);
}

block_init(bdrv_qed_init);
