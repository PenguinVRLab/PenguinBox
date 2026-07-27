/*
 * QEMU Enhanced Disk Format Cluster functions
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
#include "qed.h"

static unsigned int qed_count_contiguous_clusters(BDRVQEDState *s,
                                                  QEDTable *table,
                                                  unsigned int index,
                                                  unsigned int n,
                                                  uint64_t *offset)
{
    unsigned int end = MIN(index + n, s->table_nelems);
    uint64_t last = table->offsets[index];
    unsigned int i;

    *offset = last;

    for (i = index + 1; i < end; i++) {
        if (qed_offset_is_unalloc_cluster(last)) {
            if (!qed_offset_is_unalloc_cluster(table->offsets[i])) {
                break;
            }
        } else if (qed_offset_is_zero_cluster(last)) {
            if (!qed_offset_is_zero_cluster(table->offsets[i])) {
                break;
            }
        } else {
            if (table->offsets[i] != last + s->header.cluster_size) {
                break;
            }
            last = table->offsets[i];
        }
    }
    return i - index;
}

int coroutine_fn qed_find_cluster(BDRVQEDState *s, QEDRequest *request,
                                  uint64_t pos, size_t *len,
                                  uint64_t *img_offset)
{
    uint64_t l2_offset;
    uint64_t offset = 0;
    unsigned int index;
    unsigned int n;
    int ret;

    *len = MIN(*len, (((pos >> s->l1_shift) + 1) << s->l1_shift) - pos);

    l2_offset = s->l1_table->offsets[qed_l1_index(s, pos)];
    if (qed_offset_is_unalloc_cluster(l2_offset)) {
        *img_offset = 0;
        return QED_CLUSTER_L1;
    }
    if (!qed_check_table_offset(s, l2_offset)) {
        *img_offset = *len = 0;
        return -EINVAL;
    }

    ret = qed_read_l2_table(s, request, l2_offset);
    if (ret) {
        goto out;
    }

    index = qed_l2_index(s, pos);
    n = qed_bytes_to_clusters(s, qed_offset_into_cluster(s, pos) + *len);
    n = qed_count_contiguous_clusters(s, request->l2_table->table,
                                      index, n, &offset);

    if (qed_offset_is_unalloc_cluster(offset)) {
        ret = QED_CLUSTER_L2;
    } else if (qed_offset_is_zero_cluster(offset)) {
        ret = QED_CLUSTER_ZERO;
    } else if (qed_check_cluster_offset(s, offset)) {
        ret = QED_CLUSTER_FOUND;
    } else {
        ret = -EINVAL;
    }

    *len = MIN(*len,
               n * s->header.cluster_size - qed_offset_into_cluster(s, pos));

out:
    *img_offset = offset;
    return ret;
}
