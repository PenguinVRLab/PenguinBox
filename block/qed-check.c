/*
 * QEMU Enhanced Disk Format Consistency Check
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Stefan Hajnoczi   <stefanha@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "block/block-io.h"
#include "qed.h"

typedef struct {
    BDRVQEDState *s;
    BdrvCheckResult *result;
    bool fix;

    uint64_t nclusters;
    uint32_t *used_clusters;

    QEDRequest request;
} QEDCheck;

static bool qed_test_bit(uint32_t *bitmap, uint64_t n) {
    return !!(bitmap[n / 32] & (1 << (n % 32)));
}

static void qed_set_bit(uint32_t *bitmap, uint64_t n) {
    bitmap[n / 32] |= 1 << (n % 32);
}

static bool qed_set_used_clusters(QEDCheck *check, uint64_t offset,
                                  unsigned int n)
{
    uint64_t cluster = qed_bytes_to_clusters(check->s, offset);
    unsigned int corruptions = 0;

    while (n-- != 0) {
        if (qed_test_bit(check->used_clusters, cluster)) {
            corruptions++;
        }

        qed_set_bit(check->used_clusters, cluster);
        cluster++;
    }

    check->result->corruptions += corruptions;
    return corruptions == 0;
}

static unsigned int qed_check_l2_table(QEDCheck *check, QEDTable *table)
{
    BDRVQEDState *s = check->s;
    unsigned int i, num_invalid = 0;
    uint64_t last_offset = 0;

    for (i = 0; i < s->table_nelems; i++) {
        uint64_t offset = table->offsets[i];

        if (qed_offset_is_unalloc_cluster(offset) ||
            qed_offset_is_zero_cluster(offset)) {
            continue;
        }
        check->result->bfi.allocated_clusters++;
        if (last_offset && (last_offset + s->header.cluster_size != offset)) {
            check->result->bfi.fragmented_clusters++;
        }
        last_offset = offset;

        if (!qed_check_cluster_offset(s, offset)) {
            if (check->fix) {
                table->offsets[i] = 0;
                check->result->corruptions_fixed++;
            } else {
                check->result->corruptions++;
            }

            num_invalid++;
            continue;
        }

        qed_set_used_clusters(check, offset, 1);
    }

    return num_invalid;
}

static int coroutine_fn GRAPH_RDLOCK
qed_check_l1_table(QEDCheck *check, QEDTable *table)
{
    BDRVQEDState *s = check->s;
    unsigned int i, num_invalid_l1 = 0;
    int ret, last_error = 0;

    qed_set_used_clusters(check, s->header.l1_table_offset,
                          s->header.table_size);

    for (i = 0; i < s->table_nelems; i++) {
        unsigned int num_invalid_l2;
        uint64_t offset = table->offsets[i];

        if (qed_offset_is_unalloc_cluster(offset)) {
            continue;
        }

        if (!qed_check_table_offset(s, offset)) {
            if (check->fix) {
                table->offsets[i] = 0;
                check->result->corruptions_fixed++;
            } else {
                check->result->corruptions++;
            }

            num_invalid_l1++;
            continue;
        }

        if (!qed_set_used_clusters(check, offset, s->header.table_size)) {
            continue;
        }

        ret = qed_read_l2_table_sync(s, &check->request, offset);
        if (ret) {
            check->result->check_errors++;
            last_error = ret;
            continue;
        }

        num_invalid_l2 = qed_check_l2_table(check,
                                            check->request.l2_table->table);

        if (num_invalid_l2 > 0 && check->fix) {
            ret = qed_write_l2_table_sync(s, &check->request, 0,
                                          s->table_nelems, false);
            if (ret) {
                check->result->check_errors++;
                last_error = ret;
                continue;
            }
        }
    }

    qed_unref_l2_cache_entry(check->request.l2_table);
    check->request.l2_table = NULL;

    if (num_invalid_l1 > 0 && check->fix) {
        ret = qed_write_l1_table_sync(s, 0, s->table_nelems);
        if (ret) {
            check->result->check_errors++;
            last_error = ret;
        }
    }

    return last_error;
}

static void qed_check_for_leaks(QEDCheck *check)
{
    BDRVQEDState *s = check->s;
    uint64_t i;

    for (i = s->header.header_size; i < check->nclusters; i++) {
        if (!qed_test_bit(check->used_clusters, i)) {
            check->result->leaks++;
        }
    }
}

static void coroutine_fn GRAPH_RDLOCK
qed_check_mark_clean(BDRVQEDState *s, BdrvCheckResult *result)
{
    if (result->corruptions > 0 || result->check_errors > 0) {
        return;
    }

    if (!(s->header.features & QED_F_NEED_CHECK)) {
        return;
    }

    bdrv_co_flush(s->bs);

    s->header.features &= ~QED_F_NEED_CHECK;
    qed_write_header_sync(s);
}

int coroutine_fn qed_check(BDRVQEDState *s, BdrvCheckResult *result, bool fix)
{
    QEDCheck check = {
        .s = s,
        .result = result,
        .nclusters = qed_bytes_to_clusters(s, s->file_size),
        .request = { .l2_table = NULL },
        .fix = fix,
    };
    int ret;

    check.used_clusters = g_try_new0(uint32_t, (check.nclusters + 31) / 32);
    if (check.nclusters && check.used_clusters == NULL) {
        return -ENOMEM;
    }

    check.result->bfi.total_clusters =
        DIV_ROUND_UP(s->header.image_size, s->header.cluster_size);
    ret = qed_check_l1_table(&check, s->l1_table);
    if (ret == 0) {
        qed_check_for_leaks(&check);

        if (fix) {
            qed_check_mark_clean(s, result);
        }
    }

    g_free(check.used_clusters);
    return ret;
}
