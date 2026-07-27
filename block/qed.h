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

#ifndef BLOCK_QED_H
#define BLOCK_QED_H

#include "block/block_int.h"
#include "qemu/cutils.h"

#define  QED_DEFAULT_CLUSTER_SIZE  65536
enum {
    QED_MAGIC = 'Q' | 'E' << 8 | 'D' << 16 | '\0' << 24,

    QED_F_BACKING_FILE = 0x01,

    QED_F_NEED_CHECK = 0x02,

    QED_F_BACKING_FORMAT_NO_PROBE = 0x04,

    QED_FEATURE_MASK = QED_F_BACKING_FILE |
                       QED_F_NEED_CHECK |
                       QED_F_BACKING_FORMAT_NO_PROBE,
    QED_COMPAT_FEATURE_MASK = 0,
    QED_AUTOCLEAR_FEATURE_MASK = 0,

    QED_MIN_CLUSTER_SIZE = 4 * 1024,
    QED_MAX_CLUSTER_SIZE = 64 * 1024 * 1024,

    QED_MIN_TABLE_SIZE = 1,
    QED_MAX_TABLE_SIZE = 16,
    QED_DEFAULT_TABLE_SIZE = 4,

    QED_NEED_CHECK_TIMEOUT = 5,
};

typedef struct {
    uint32_t magic;

    uint32_t cluster_size;
    uint32_t table_size;
    uint32_t header_size;

    uint64_t features;
    uint64_t compat_features;
    uint64_t autoclear_features;

    uint64_t l1_table_offset;
    uint64_t image_size;

    uint32_t backing_filename_offset;
    uint32_t backing_filename_size;
} QEMU_PACKED QEDHeader;

typedef struct {
    uint64_t offsets[0];
} QEDTable;

typedef struct CachedL2Table {
    QEDTable *table;
    uint64_t offset;
    QTAILQ_ENTRY(CachedL2Table) node;
    int ref;
} CachedL2Table;

typedef struct {
    QTAILQ_HEAD(, CachedL2Table) entries;
    unsigned int n_entries;
} L2TableCache;

typedef struct QEDRequest {
    CachedL2Table *l2_table;
} QEDRequest;

enum {
    QED_AIOCB_WRITE = 0x0001,
    QED_AIOCB_ZERO  = 0x0002,
};

typedef struct QEDAIOCB {
    BlockDriverState *bs;
    QSIMPLEQ_ENTRY(QEDAIOCB) next;
    int flags;
    uint64_t end_pos;

    QEMUIOVector *qiov;
    size_t qiov_offset;

    QEMUIOVector cur_qiov;
    uint64_t cur_pos;
    uint64_t cur_cluster;
    unsigned int cur_nclusters;
    int find_cluster_ret;

    QEDRequest request;
} QEDAIOCB;

typedef struct {
    BlockDriverState *bs;

    QEDHeader header;

    CoMutex table_lock;
    QEDTable *l1_table;
    L2TableCache l2_cache;
    uint32_t table_nelems;
    uint32_t l1_shift;
    uint32_t l2_shift;
    uint32_t l2_mask;
    uint64_t file_size;

    QEDAIOCB *allocating_acb;
    CoQueue allocating_write_reqs;
    bool allocating_write_reqs_plugged;

    QEMUTimer *need_check_timer;
} BDRVQEDState;

enum {
    QED_CLUSTER_FOUND,
    QED_CLUSTER_ZERO,
    QED_CLUSTER_L2,
    QED_CLUSTER_L1,
};

int GRAPH_RDLOCK qed_write_header_sync(BDRVQEDState *s);

void qed_init_l2_cache(L2TableCache *l2_cache);
void qed_free_l2_cache(L2TableCache *l2_cache);
CachedL2Table *qed_alloc_l2_cache_entry(L2TableCache *l2_cache);
void qed_unref_l2_cache_entry(CachedL2Table *entry);
CachedL2Table *qed_find_l2_cache_entry(L2TableCache *l2_cache, uint64_t offset);
void qed_commit_l2_cache_entry(L2TableCache *l2_cache, CachedL2Table *l2_table);

int coroutine_fn GRAPH_RDLOCK qed_read_l1_table_sync(BDRVQEDState *s);

int coroutine_fn GRAPH_RDLOCK
qed_write_l1_table(BDRVQEDState *s, unsigned int index, unsigned int n);

int coroutine_fn GRAPH_RDLOCK
qed_write_l1_table_sync(BDRVQEDState *s, unsigned int index, unsigned int n);

int coroutine_fn GRAPH_RDLOCK
qed_read_l2_table_sync(BDRVQEDState *s, QEDRequest *request, uint64_t offset);

int coroutine_fn GRAPH_RDLOCK
qed_read_l2_table(BDRVQEDState *s, QEDRequest *request, uint64_t offset);

int coroutine_fn GRAPH_RDLOCK
qed_write_l2_table(BDRVQEDState *s, QEDRequest *request, unsigned int index,
                   unsigned int n, bool flush);

int coroutine_fn GRAPH_RDLOCK
qed_write_l2_table_sync(BDRVQEDState *s, QEDRequest *request,
                        unsigned int index, unsigned int n, bool flush);

int coroutine_fn GRAPH_RDLOCK
qed_find_cluster(BDRVQEDState *s, QEDRequest *request, uint64_t pos,
                 size_t *len, uint64_t *img_offset);

int coroutine_fn GRAPH_RDLOCK
qed_check(BDRVQEDState *s, BdrvCheckResult *result, bool fix);

QEDTable *qed_alloc_table(BDRVQEDState *s);

static inline uint64_t qed_start_of_cluster(BDRVQEDState *s, uint64_t offset)
{
    return offset & ~(uint64_t)(s->header.cluster_size - 1);
}

static inline uint64_t qed_offset_into_cluster(BDRVQEDState *s, uint64_t offset)
{
    return offset & (s->header.cluster_size - 1);
}

static inline uint64_t qed_bytes_to_clusters(BDRVQEDState *s, uint64_t bytes)
{
    return qed_start_of_cluster(s, bytes + (s->header.cluster_size - 1)) /
           (s->header.cluster_size - 1);
}

static inline unsigned int qed_l1_index(BDRVQEDState *s, uint64_t pos)
{
    return pos >> s->l1_shift;
}

static inline unsigned int qed_l2_index(BDRVQEDState *s, uint64_t pos)
{
    return (pos >> s->l2_shift) & s->l2_mask;
}

static inline bool qed_check_cluster_offset(BDRVQEDState *s, uint64_t offset)
{
    uint64_t header_size = (uint64_t)s->header.header_size *
                           s->header.cluster_size;

    if (offset & (s->header.cluster_size - 1)) {
        return false;
    }
    return offset >= header_size && offset < s->file_size;
}

static inline bool qed_check_table_offset(BDRVQEDState *s, uint64_t offset)
{
    uint64_t end_offset = offset + (s->header.table_size - 1) *
                          s->header.cluster_size;

    if (end_offset <= offset) {
        return false;
    }

    return qed_check_cluster_offset(s, offset) &&
           qed_check_cluster_offset(s, end_offset);
}

static inline bool qed_offset_is_cluster_aligned(BDRVQEDState *s,
                                                 uint64_t offset)
{
    if (qed_offset_into_cluster(s, offset)) {
        return false;
    }
    return true;
}

static inline bool qed_offset_is_unalloc_cluster(uint64_t offset)
{
    if (offset == 0) {
        return true;
    }
    return false;
}

static inline bool qed_offset_is_zero_cluster(uint64_t offset)
{
    if (offset == 1) {
        return true;
    }
    return false;
}

#endif
