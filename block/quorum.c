/*
 * Quorum Block filter
 *
 * Copyright (C) 2012-2014 Nodalink, EURL.
 *
 * Author:
 *   Benoît Canet <benoit.canet@irqsave.net>
 *
 * Based on the design and code of blkverify.c (Copyright (C) 2010 IBM, Corp)
 * and blkmirror.c (Copyright (C) 2011 Red Hat, Inc).
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/memalign.h"
#include "block/block_int.h"
#include "block/coroutines.h"
#include "block/qdict.h"
#include "qapi/error.h"
#include "qapi/qapi-events-block.h"
#include "qobject/qdict.h"
#include "qapi/qmp/qerror.h"
#include "qobject/qlist.h"
#include "qobject/qstring.h"
#include "crypto/hash.h"

#define HASH_LENGTH 32

#define INDEXSTR_LEN 32

#define QUORUM_OPT_VOTE_THRESHOLD "vote-threshold"
#define QUORUM_OPT_BLKVERIFY      "blkverify"
#define QUORUM_OPT_REWRITE        "rewrite-corrupted"
#define QUORUM_OPT_READ_PATTERN   "read-pattern"

typedef union QuorumVoteValue {
    uint8_t h[HASH_LENGTH];
    int64_t l;
} QuorumVoteValue;

typedef struct QuorumVoteItem {
    int index;
    QLIST_ENTRY(QuorumVoteItem) next;
} QuorumVoteItem;

typedef struct QuorumVoteVersion {
    QuorumVoteValue value;
    int index;
    int vote_count;
    QLIST_HEAD(, QuorumVoteItem) items;
    QLIST_ENTRY(QuorumVoteVersion) next;
} QuorumVoteVersion;

typedef struct QuorumVotes {
    QLIST_HEAD(, QuorumVoteVersion) vote_list;
    bool (*compare)(QuorumVoteValue *a, QuorumVoteValue *b);
} QuorumVotes;

typedef struct BDRVQuorumState {
    BdrvChild **children;
    int num_children;
    unsigned next_child_index;
    int threshold;
    bool is_blkverify;
    bool rewrite_corrupted;

    QuorumReadPattern read_pattern;
} BDRVQuorumState;

typedef struct QuorumAIOCB QuorumAIOCB;

typedef struct QuorumChildRequest {
    BlockDriverState *bs;
    QEMUIOVector qiov;
    uint8_t *buf;
    int ret;
    QuorumAIOCB *parent;
} QuorumChildRequest;

struct QuorumAIOCB {
    BlockDriverState *bs;
    Coroutine *co;

    uint64_t offset;
    uint64_t bytes;
    int flags;

    QEMUIOVector *qiov;

    QuorumChildRequest *qcrs;
    int count;
    int success_count;

    int rewrite_count;

    QuorumVotes votes;

    bool is_read;
    int vote_ret;
    int children_read;
};

typedef struct QuorumCo {
    QuorumAIOCB *acb;
    int idx;
} QuorumCo;

static void quorum_aio_finalize(QuorumAIOCB *acb)
{
    g_free(acb->qcrs);
    g_free(acb);
}

static bool quorum_sha256_compare(QuorumVoteValue *a, QuorumVoteValue *b)
{
    return !memcmp(a->h, b->h, HASH_LENGTH);
}

static bool quorum_64bits_compare(QuorumVoteValue *a, QuorumVoteValue *b)
{
    return a->l == b->l;
}

static QuorumAIOCB *coroutine_fn quorum_aio_get(BlockDriverState *bs,
                                                QEMUIOVector *qiov,
                                                uint64_t offset, uint64_t bytes,
                                                int flags)
{
    BDRVQuorumState *s = bs->opaque;
    QuorumAIOCB *acb = g_new(QuorumAIOCB, 1);
    int i;

    *acb = (QuorumAIOCB) {
        .co                 = qemu_coroutine_self(),
        .bs                 = bs,
        .offset             = offset,
        .bytes              = bytes,
        .flags              = flags,
        .qiov               = qiov,
        .votes.compare      = quorum_sha256_compare,
        .votes.vote_list    = QLIST_HEAD_INITIALIZER(acb.votes.vote_list),
    };

    acb->qcrs = g_new0(QuorumChildRequest, s->num_children);
    for (i = 0; i < s->num_children; i++) {
        acb->qcrs[i].buf = NULL;
        acb->qcrs[i].ret = 0;
        acb->qcrs[i].parent = acb;
    }

    return acb;
}

static void quorum_report_bad(QuorumOpType type, uint64_t offset,
                              uint64_t bytes, char *node_name, int ret)
{
    const char *msg = NULL;
    int64_t start_sector = offset / BDRV_SECTOR_SIZE;
    int64_t end_sector = DIV_ROUND_UP(offset + bytes, BDRV_SECTOR_SIZE);

    if (ret < 0) {
        msg = strerror(-ret);
    }

    qapi_event_send_quorum_report_bad(type, msg, node_name, start_sector,
                                      end_sector - start_sector);
}

static void GRAPH_RDLOCK quorum_report_failure(QuorumAIOCB *acb)
{
    const char *reference = bdrv_get_device_or_node_name(acb->bs);
    int64_t start_sector = acb->offset / BDRV_SECTOR_SIZE;
    int64_t end_sector = DIV_ROUND_UP(acb->offset + acb->bytes,
                                      BDRV_SECTOR_SIZE);

    qapi_event_send_quorum_failure(reference, start_sector,
                                   end_sector - start_sector);
}

static int quorum_vote_error(QuorumAIOCB *acb);

static bool GRAPH_RDLOCK quorum_has_too_much_io_failed(QuorumAIOCB *acb)
{
    BDRVQuorumState *s = acb->bs->opaque;

    if (acb->success_count < s->threshold) {
        acb->vote_ret = quorum_vote_error(acb);
        quorum_report_failure(acb);
        return true;
    }

    return false;
}

static void quorum_copy_qiov(QEMUIOVector *dest, QEMUIOVector *source)
{
    int i;
    assert(dest->niov == source->niov);
    assert(dest->size == source->size);
    for (i = 0; i < source->niov; i++) {
        assert(dest->iov[i].iov_len == source->iov[i].iov_len);
        memcpy(dest->iov[i].iov_base,
               source->iov[i].iov_base,
               source->iov[i].iov_len);
    }
}

static void quorum_report_bad_acb(QuorumChildRequest *sacb, int ret)
{
    QuorumAIOCB *acb = sacb->parent;
    QuorumOpType type = acb->is_read ? QUORUM_OP_TYPE_READ : QUORUM_OP_TYPE_WRITE;
    quorum_report_bad(type, acb->offset, acb->bytes, sacb->bs->node_name, ret);
}

static void quorum_report_bad_versions(BDRVQuorumState *s,
                                       QuorumAIOCB *acb,
                                       QuorumVoteValue *value)
{
    QuorumVoteVersion *version;
    QuorumVoteItem *item;

    QLIST_FOREACH(version, &acb->votes.vote_list, next) {
        if (acb->votes.compare(&version->value, value)) {
            continue;
        }
        QLIST_FOREACH(item, &version->items, next) {
            quorum_report_bad(QUORUM_OP_TYPE_READ, acb->offset, acb->bytes,
                              s->children[item->index]->bs->node_name, 0);
        }
    }
}

static void coroutine_fn GRAPH_RDLOCK quorum_rewrite_entry(void *opaque)
{
    QuorumCo *co = opaque;
    QuorumAIOCB *acb = co->acb;
    BDRVQuorumState *s = acb->bs->opaque;

    bdrv_co_pwritev(s->children[co->idx], acb->offset, acb->bytes,
                    acb->qiov, acb->flags & ~BDRV_REQ_WRITE_UNCHANGED);

    acb->rewrite_count--;
    if (!acb->rewrite_count) {
        qemu_coroutine_enter_if_inactive(acb->co);
    }
}

static bool coroutine_fn GRAPH_RDLOCK
quorum_rewrite_bad_versions(QuorumAIOCB *acb, QuorumVoteValue *value)
{
    QuorumVoteVersion *version;
    QuorumVoteItem *item;
    int count = 0;

    QLIST_FOREACH(version, &acb->votes.vote_list, next) {
        if (acb->votes.compare(&version->value, value)) {
            continue;
        }
        QLIST_FOREACH(item, &version->items, next) {
            count++;
        }
    }

    acb->rewrite_count = count;

    QLIST_FOREACH(version, &acb->votes.vote_list, next) {
        if (acb->votes.compare(&version->value, value)) {
            continue;
        }
        QLIST_FOREACH(item, &version->items, next) {
            Coroutine *co;
            QuorumCo data = {
                .acb = acb,
                .idx = item->index,
            };

            co = qemu_coroutine_create(quorum_rewrite_entry, &data);
            qemu_coroutine_enter(co);
        }
    }

    return count;
}

static void quorum_count_vote(QuorumVotes *votes,
                              QuorumVoteValue *value,
                              int index)
{
    QuorumVoteVersion *v = NULL, *version = NULL;
    QuorumVoteItem *item;

    QLIST_FOREACH(v, &votes->vote_list, next) {
        if (votes->compare(&v->value, value)) {
            version = v;
            break;
        }
    }

    if (!version) {
        version = g_new0(QuorumVoteVersion, 1);
        QLIST_INIT(&version->items);
        memcpy(&version->value, value, sizeof(version->value));
        version->index = index;
        version->vote_count = 0;
        QLIST_INSERT_HEAD(&votes->vote_list, version, next);
    }

    version->vote_count++;

    item = g_new0(QuorumVoteItem, 1);
    item->index = index;
    QLIST_INSERT_HEAD(&version->items, item, next);
}

static void quorum_free_vote_list(QuorumVotes *votes)
{
    QuorumVoteVersion *version, *next_version;
    QuorumVoteItem *item, *next_item;

    QLIST_FOREACH_SAFE(version, &votes->vote_list, next, next_version) {
        QLIST_REMOVE(version, next);
        QLIST_FOREACH_SAFE(item, &version->items, next, next_item) {
            QLIST_REMOVE(item, next);
            g_free(item);
        }
        g_free(version);
    }
}

static int quorum_compute_hash(QuorumAIOCB *acb, int i, QuorumVoteValue *hash)
{
    QEMUIOVector *qiov = &acb->qcrs[i].qiov;
    size_t len = sizeof(hash->h);
    uint8_t *data = hash->h;

    if (qcrypto_hash_bytesv(QCRYPTO_HASH_ALGO_SHA256,
                            qiov->iov, qiov->niov,
                            &data, &len,
                            NULL) < 0) {
        return -EINVAL;
    }

    return 0;
}

static QuorumVoteVersion *quorum_get_vote_winner(QuorumVotes *votes)
{
    int max = 0;
    QuorumVoteVersion *candidate, *winner = NULL;

    QLIST_FOREACH(candidate, &votes->vote_list, next) {
        if (candidate->vote_count > max) {
            max = candidate->vote_count;
            winner = candidate;
        }
    }

    return winner;
}

static bool quorum_iovec_compare(QEMUIOVector *a, QEMUIOVector *b)
{
    int i;
    int result;

    assert(a->niov == b->niov);
    for (i = 0; i < a->niov; i++) {
        assert(a->iov[i].iov_len == b->iov[i].iov_len);
        result = memcmp(a->iov[i].iov_base,
                        b->iov[i].iov_base,
                        a->iov[i].iov_len);
        if (result) {
            return false;
        }
    }

    return true;
}

static bool quorum_compare(QuorumAIOCB *acb, QEMUIOVector *a, QEMUIOVector *b)
{
    BDRVQuorumState *s = acb->bs->opaque;
    ssize_t offset;

    if (s->is_blkverify) {
        offset = qemu_iovec_compare(a, b);
        if (offset != -1) {
            fprintf(stderr, "quorum: offset=%" PRIu64 " bytes=%" PRIu64
                    " contents mismatch at offset %" PRIu64 "\n",
                    acb->offset, acb->bytes, acb->offset + offset);
            exit(1);
        }
        return true;
    }

    return quorum_iovec_compare(a, b);
}

static int quorum_vote_error(QuorumAIOCB *acb)
{
    BDRVQuorumState *s = acb->bs->opaque;
    QuorumVoteVersion *winner = NULL;
    QuorumVotes error_votes;
    QuorumVoteValue result_value;
    int i, ret = 0;
    bool error = false;

    QLIST_INIT(&error_votes.vote_list);
    error_votes.compare = quorum_64bits_compare;

    for (i = 0; i < s->num_children; i++) {
        ret = acb->qcrs[i].ret;
        if (ret) {
            error = true;
            result_value.l = ret;
            quorum_count_vote(&error_votes, &result_value, i);
        }
    }

    if (error) {
        winner = quorum_get_vote_winner(&error_votes);
        ret = winner->value.l;
    }

    quorum_free_vote_list(&error_votes);

    return ret;
}

static void coroutine_fn GRAPH_RDLOCK quorum_vote(QuorumAIOCB *acb)
{
    bool quorum = true;
    int i, j, ret;
    QuorumVoteValue hash;
    BDRVQuorumState *s = acb->bs->opaque;
    QuorumVoteVersion *winner;

    if (quorum_has_too_much_io_failed(acb)) {
        return;
    }

    for (i = 0; i < s->num_children; i++) {
        if (!acb->qcrs[i].ret) {
            break;
        }
    }

    assert(i < s->num_children);

    for (j = i + 1; j < s->num_children; j++) {
        if (acb->qcrs[j].ret) {
            continue;
        }
        quorum = quorum_compare(acb, &acb->qcrs[i].qiov, &acb->qcrs[j].qiov);
        if (!quorum) {
            break;
       }
    }

    if (quorum) {
        quorum_copy_qiov(acb->qiov, &acb->qcrs[i].qiov);
        return;
    }

    for (i = 0; i < s->num_children; i++) {
        if (acb->qcrs[i].ret) {
            continue;
        }
        ret = quorum_compute_hash(acb, i, &hash);
        if (ret < 0) {
            acb->vote_ret = ret;
            goto free_exit;
        }
        quorum_count_vote(&acb->votes, &hash, i);
    }

    winner = quorum_get_vote_winner(&acb->votes);

    if (winner->vote_count < s->threshold) {
        quorum_report_failure(acb);
        acb->vote_ret = -EIO;
        goto free_exit;
    }

    quorum_copy_qiov(acb->qiov, &acb->qcrs[winner->index].qiov);

    quorum_report_bad_versions(s, acb, &winner->value);

    if (s->rewrite_corrupted) {
        quorum_rewrite_bad_versions(acb, &winner->value);
    }

free_exit:
    quorum_free_vote_list(&acb->votes);
}

static void coroutine_fn GRAPH_RDLOCK read_quorum_children_entry(void *opaque)
{
    QuorumCo *co = opaque;
    QuorumAIOCB *acb = co->acb;
    BDRVQuorumState *s = acb->bs->opaque;
    int i = co->idx;
    QuorumChildRequest *sacb = &acb->qcrs[i];

    sacb->bs = s->children[i]->bs;
    sacb->ret = bdrv_co_preadv(s->children[i], acb->offset, acb->bytes,
                               &acb->qcrs[i].qiov, 0);

    if (sacb->ret == 0) {
        acb->success_count++;
    } else {
        quorum_report_bad_acb(sacb, sacb->ret);
    }

    acb->count++;
    assert(acb->count <= s->num_children);
    assert(acb->success_count <= s->num_children);

    if (acb->count == s->num_children) {
        qemu_coroutine_enter_if_inactive(acb->co);
    }
}

static int coroutine_fn GRAPH_RDLOCK read_quorum_children(QuorumAIOCB *acb)
{
    BDRVQuorumState *s = acb->bs->opaque;
    int i;

    acb->children_read = s->num_children;
    for (i = 0; i < s->num_children; i++) {
        acb->qcrs[i].buf = qemu_blockalign(s->children[i]->bs, acb->qiov->size);
        qemu_iovec_init(&acb->qcrs[i].qiov, acb->qiov->niov);
        qemu_iovec_clone(&acb->qcrs[i].qiov, acb->qiov, acb->qcrs[i].buf);
    }

    for (i = 0; i < s->num_children; i++) {
        Coroutine *co;
        QuorumCo data = {
            .acb = acb,
            .idx = i,
        };

        co = qemu_coroutine_create(read_quorum_children_entry, &data);
        qemu_coroutine_enter(co);
    }

    while (acb->count < s->num_children) {
        qemu_coroutine_yield();
    }

    quorum_vote(acb);
    for (i = 0; i < s->num_children; i++) {
        qemu_vfree(acb->qcrs[i].buf);
        qemu_iovec_destroy(&acb->qcrs[i].qiov);
    }

    while (acb->rewrite_count) {
        qemu_coroutine_yield();
    }

    return acb->vote_ret;
}

static int coroutine_fn GRAPH_RDLOCK read_fifo_child(QuorumAIOCB *acb)
{
    BDRVQuorumState *s = acb->bs->opaque;
    int n, ret;

    do {
        n = acb->children_read++;
        acb->qcrs[n].bs = s->children[n]->bs;
        ret = bdrv_co_preadv(s->children[n], acb->offset, acb->bytes,
                             acb->qiov, 0);
        if (ret < 0) {
            quorum_report_bad_acb(&acb->qcrs[n], ret);
        }
    } while (ret < 0 && acb->children_read < s->num_children);

    return ret;
}

static int coroutine_fn GRAPH_RDLOCK
quorum_co_preadv(BlockDriverState *bs, int64_t offset, int64_t bytes,
                 QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    BDRVQuorumState *s = bs->opaque;
    QuorumAIOCB *acb = quorum_aio_get(bs, qiov, offset, bytes, flags);
    int ret;

    acb->is_read = true;
    acb->children_read = 0;

    if (s->read_pattern == QUORUM_READ_PATTERN_QUORUM) {
        ret = read_quorum_children(acb);
    } else {
        ret = read_fifo_child(acb);
    }
    quorum_aio_finalize(acb);

    return ret;
}

static void coroutine_fn GRAPH_RDLOCK write_quorum_entry(void *opaque)
{
    QuorumCo *co = opaque;
    QuorumAIOCB *acb = co->acb;
    BDRVQuorumState *s = acb->bs->opaque;
    int i = co->idx;
    QuorumChildRequest *sacb = &acb->qcrs[i];

    sacb->bs = s->children[i]->bs;
    if (acb->flags & BDRV_REQ_ZERO_WRITE) {
        sacb->ret = bdrv_co_pwrite_zeroes(s->children[i], acb->offset,
                                          acb->bytes, acb->flags);
    } else {
        sacb->ret = bdrv_co_pwritev(s->children[i], acb->offset, acb->bytes,
                                    acb->qiov, acb->flags);
    }
    if (sacb->ret == 0) {
        acb->success_count++;
    } else {
        quorum_report_bad_acb(sacb, sacb->ret);
    }
    acb->count++;
    assert(acb->count <= s->num_children);
    assert(acb->success_count <= s->num_children);

    if (acb->count == s->num_children) {
        qemu_coroutine_enter_if_inactive(acb->co);
    }
}

static int coroutine_fn GRAPH_RDLOCK
quorum_co_pwritev(BlockDriverState *bs, int64_t offset, int64_t bytes,
                  QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    BDRVQuorumState *s = bs->opaque;
    QuorumAIOCB *acb = quorum_aio_get(bs, qiov, offset, bytes, flags);
    int i, ret;

    for (i = 0; i < s->num_children; i++) {
        Coroutine *co;
        QuorumCo data = {
            .acb = acb,
            .idx = i,
        };

        co = qemu_coroutine_create(write_quorum_entry, &data);
        qemu_coroutine_enter(co);
    }

    while (acb->count < s->num_children) {
        qemu_coroutine_yield();
    }

    quorum_has_too_much_io_failed(acb);

    ret = acb->vote_ret;
    quorum_aio_finalize(acb);

    return ret;
}

static int coroutine_fn GRAPH_RDLOCK
quorum_co_pwrite_zeroes(BlockDriverState *bs, int64_t offset, int64_t bytes,
                        BdrvRequestFlags flags)
{
    return quorum_co_pwritev(bs, offset, bytes, NULL,
                             flags | BDRV_REQ_ZERO_WRITE);
}

static int64_t coroutine_fn GRAPH_RDLOCK
quorum_co_getlength(BlockDriverState *bs)
{
    BDRVQuorumState *s = bs->opaque;
    int64_t result;
    int i;

    result = bdrv_co_getlength(s->children[0]->bs);
    if (result < 0) {
        return result;
    }
    for (i = 1; i < s->num_children; i++) {
        int64_t value = bdrv_co_getlength(s->children[i]->bs);
        if (value < 0) {
            return value;
        }
        if (value != result) {
            return -EIO;
        }
    }

    return result;
}

static coroutine_fn GRAPH_RDLOCK int quorum_co_flush(BlockDriverState *bs)
{
    BDRVQuorumState *s = bs->opaque;
    QuorumVoteVersion *winner = NULL;
    QuorumVotes error_votes;
    QuorumVoteValue result_value;
    int i;
    int result = 0;
    int success_count = 0;

    QLIST_INIT(&error_votes.vote_list);
    error_votes.compare = quorum_64bits_compare;

    for (i = 0; i < s->num_children; i++) {
        result = bdrv_co_flush(s->children[i]->bs);
        if (result) {
            quorum_report_bad(QUORUM_OP_TYPE_FLUSH, 0, 0,
                              s->children[i]->bs->node_name, result);
            result_value.l = result;
            quorum_count_vote(&error_votes, &result_value, i);
        } else {
            success_count++;
        }
    }

    if (success_count >= s->threshold) {
        result = 0;
    } else {
        winner = quorum_get_vote_winner(&error_votes);
        result = winner->value.l;
    }
    quorum_free_vote_list(&error_votes);

    return result;
}

static bool GRAPH_RDLOCK
quorum_recurse_can_replace(BlockDriverState *bs, BlockDriverState *to_replace)
{
    BDRVQuorumState *s = bs->opaque;
    int i;

    for (i = 0; i < s->num_children; i++) {
        if (s->children[i]->bs == to_replace) {
            return QLIST_FIRST(&to_replace->parents) == s->children[i] &&
                QLIST_NEXT(s->children[i], next_parent) == NULL;
        }
    }

    return false;
}

static int quorum_valid_threshold(int threshold, int num_children, Error **errp)
{

    if (threshold < 1) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   "vote-threshold", "a value >= 1");
        return -ERANGE;
    }

    if (threshold > num_children) {
        error_setg(errp, "threshold may not exceed children count");
        return -ERANGE;
    }

    return 0;
}

static QemuOptsList quorum_runtime_opts = {
    .name = "quorum",
    .head = QTAILQ_HEAD_INITIALIZER(quorum_runtime_opts.head),
    .desc = {
        {
            .name = QUORUM_OPT_VOTE_THRESHOLD,
            .type = QEMU_OPT_NUMBER,
            .help = "The number of vote needed for reaching quorum",
        },
        {
            .name = QUORUM_OPT_BLKVERIFY,
            .type = QEMU_OPT_BOOL,
            .help = "Trigger block verify mode if set",
        },
        {
            .name = QUORUM_OPT_REWRITE,
            .type = QEMU_OPT_BOOL,
            .help = "Rewrite corrupted block on read quorum",
        },
        {
            .name = QUORUM_OPT_READ_PATTERN,
            .type = QEMU_OPT_STRING,
            .help = "Allowed pattern: quorum, fifo. Quorum is default",
        },
        { }
    },
};

static void quorum_refresh_flags(BlockDriverState *bs)
{
    BDRVQuorumState *s = bs->opaque;
    int i;

    bs->supported_zero_flags =
        BDRV_REQ_FUA | BDRV_REQ_MAY_UNMAP | BDRV_REQ_NO_FALLBACK;

    for (i = 0; i < s->num_children; i++) {
        bs->supported_zero_flags &= s->children[i]->bs->supported_zero_flags;
    }

    bs->supported_zero_flags |= BDRV_REQ_WRITE_UNCHANGED;
}

static int quorum_open(BlockDriverState *bs, QDict *options, int flags,
                       Error **errp)
{
    BDRVQuorumState *s = bs->opaque;
    QemuOpts *opts = NULL;
    const char *pattern_str;
    bool *opened;
    int i;
    int ret = 0;

    qdict_flatten(options);

    s->num_children = qdict_array_entries(options, "children.");
    if (s->num_children < 0) {
        error_setg(errp, "Option children is not a valid array");
        ret = -EINVAL;
        goto exit;
    }
    if (s->num_children < 1) {
        error_setg(errp, "Number of provided children must be 1 or more");
        ret = -EINVAL;
        goto exit;
    }

    opts = qemu_opts_create(&quorum_runtime_opts, NULL, 0, &error_abort);
    if (!qemu_opts_absorb_qdict(opts, options, errp)) {
        ret = -EINVAL;
        goto exit;
    }

    s->threshold = qemu_opt_get_number(opts, QUORUM_OPT_VOTE_THRESHOLD, 0);
    ret = quorum_valid_threshold(s->threshold, s->num_children, errp);
    if (ret < 0) {
        goto exit;
    }

    pattern_str = qemu_opt_get(opts, QUORUM_OPT_READ_PATTERN);
    if (!pattern_str) {
        ret = QUORUM_READ_PATTERN_QUORUM;
    } else {
        ret = qapi_enum_parse(&QuorumReadPattern_lookup, pattern_str,
                              -EINVAL, NULL);
    }
    if (ret < 0) {
        error_setg(errp, "Please set read-pattern as fifo or quorum");
        goto exit;
    }
    s->read_pattern = ret;

    if (s->read_pattern == QUORUM_READ_PATTERN_QUORUM) {
        s->is_blkverify = qemu_opt_get_bool(opts, QUORUM_OPT_BLKVERIFY, false);
        if (s->is_blkverify && (s->num_children != 2 || s->threshold != 2)) {
            error_setg(errp, "blkverify=on can only be set if there are "
                       "exactly two files and vote-threshold is 2");
            ret = -EINVAL;
            goto exit;
        }

        s->rewrite_corrupted = qemu_opt_get_bool(opts, QUORUM_OPT_REWRITE,
                                                 false);
        if (s->rewrite_corrupted && s->is_blkverify) {
            error_setg(errp,
                       "rewrite-corrupted=on cannot be used with blkverify=on");
            ret = -EINVAL;
            goto exit;
        }
    }

    s->children = g_new0(BdrvChild *, s->num_children);
    opened = g_new0(bool, s->num_children);

    for (i = 0; i < s->num_children; i++) {
        char indexstr[INDEXSTR_LEN];
        ret = snprintf(indexstr, INDEXSTR_LEN, "children.%d", i);
        assert(ret < INDEXSTR_LEN);

        s->children[i] = bdrv_open_child(NULL, options, indexstr, bs,
                                         &child_of_bds, BDRV_CHILD_DATA, false,
                                         errp);
        if (!s->children[i]) {
            ret = -EINVAL;
            goto close_exit;
        }

        opened[i] = true;
    }
    s->next_child_index = s->num_children;

    bs->supported_write_flags = BDRV_REQ_WRITE_UNCHANGED;
    quorum_refresh_flags(bs);

    g_free(opened);
    goto exit;

close_exit:
    bdrv_graph_wrlock_drained();
    for (i = 0; i < s->num_children; i++) {
        if (!opened[i]) {
            continue;
        }
        bdrv_unref_child(bs, s->children[i]);
    }
    bdrv_graph_wrunlock();
    g_free(s->children);
    g_free(opened);
exit:
    qemu_opts_del(opts);
    return ret;
}

static void quorum_close(BlockDriverState *bs)
{
    BDRVQuorumState *s = bs->opaque;
    int i;

    bdrv_graph_wrlock_drained();
    for (i = 0; i < s->num_children; i++) {
        bdrv_unref_child(bs, s->children[i]);
    }
    bdrv_graph_wrunlock();

    g_free(s->children);
}

static void GRAPH_WRLOCK
quorum_add_child(BlockDriverState *bs, BlockDriverState *child_bs, Error **errp)
{
    BDRVQuorumState *s = bs->opaque;
    BdrvChild *child;
    char indexstr[INDEXSTR_LEN];
    int ret;

    if (s->is_blkverify) {
        error_setg(errp, "Cannot add a child to a quorum in blkverify mode");
        return;
    }

    assert(s->num_children <= INT_MAX / sizeof(BdrvChild *));
    if (s->num_children == INT_MAX / sizeof(BdrvChild *) ||
        s->next_child_index == UINT_MAX) {
        error_setg(errp, "Too many children");
        return;
    }

    ret = snprintf(indexstr, INDEXSTR_LEN, "children.%u", s->next_child_index);
    if (ret < 0 || ret >= INDEXSTR_LEN) {
        error_setg(errp, "cannot generate child name");
        return;
    }
    s->next_child_index++;

    bdrv_ref(child_bs);

    child = bdrv_attach_child(bs, child_bs, indexstr, &child_of_bds,
                              BDRV_CHILD_DATA, errp);
    if (child == NULL) {
        s->next_child_index--;
        return;
    }
    s->children = g_renew(BdrvChild *, s->children, s->num_children + 1);
    s->children[s->num_children++] = child;
    quorum_refresh_flags(bs);
}

static void GRAPH_WRLOCK
quorum_del_child(BlockDriverState *bs, BdrvChild *child, Error **errp)
{
    BDRVQuorumState *s = bs->opaque;
    char indexstr[INDEXSTR_LEN];
    int i;

    for (i = 0; i < s->num_children; i++) {
        if (s->children[i] == child) {
            break;
        }
    }

    assert(i < s->num_children);

    if (s->num_children <= s->threshold) {
        error_setg(errp,
            "The number of children cannot be lower than the vote threshold %d",
            s->threshold);
        return;
    }

    assert(!s->is_blkverify);

    snprintf(indexstr, INDEXSTR_LEN, "children.%u", s->next_child_index - 1);
    if (!strncmp(child->name, indexstr, INDEXSTR_LEN)) {
        s->next_child_index--;
    }

    memmove(&s->children[i], &s->children[i + 1],
            (s->num_children - i - 1) * sizeof(BdrvChild *));
    s->children = g_renew(BdrvChild *, s->children, --s->num_children);

    bdrv_unref_child(bs, child);

    quorum_refresh_flags(bs);
}

static void quorum_gather_child_options(BlockDriverState *bs, QDict *target,
                                        bool backing_overridden)
{
    BDRVQuorumState *s = bs->opaque;
    QList *children_list;
    int i;

    children_list = qlist_new();
    qdict_put(target, "children", children_list);

    for (i = 0; i < s->num_children; i++) {
        qlist_append(children_list,
                     qobject_ref(s->children[i]->bs->full_open_options));
    }
}

static char *quorum_dirname(BlockDriverState *bs, Error **errp)
{
    error_setg(errp, "Cannot generate a base directory for quorum nodes");
    return NULL;
}

static void quorum_child_perm(BlockDriverState *bs, BdrvChild *c,
                              BdrvChildRole role,
                              BlockReopenQueue *reopen_queue,
                              uint64_t perm, uint64_t shared,
                              uint64_t *nperm, uint64_t *nshared)
{
    BDRVQuorumState *s = bs->opaque;

    *nperm = perm & DEFAULT_PERM_PASSTHROUGH;
    if (s->rewrite_corrupted) {
        *nperm |= BLK_PERM_WRITE;
    }

    *nshared = (shared & (BLK_PERM_CONSISTENT_READ |
                          BLK_PERM_WRITE_UNCHANGED))
             | DEFAULT_PERM_UNCHANGED;
}

static int coroutine_fn GRAPH_RDLOCK
quorum_co_block_status(BlockDriverState *bs, unsigned int mode,
                       int64_t offset, int64_t count,
                       int64_t *pnum, int64_t *map, BlockDriverState **file)
{
    BDRVQuorumState *s = bs->opaque;
    int i, ret;
    int64_t pnum_zero = count;
    int64_t pnum_data = 0;

    for (i = 0; i < s->num_children; i++) {
        int64_t bytes;
        ret = bdrv_co_common_block_status_above(s->children[i]->bs, NULL, false,
                                                mode, offset, count,
                                                &bytes, NULL, NULL, NULL);
        if (ret < 0) {
            quorum_report_bad(QUORUM_OP_TYPE_READ, offset, count,
                              s->children[i]->bs->node_name, ret);
            pnum_data = count;
            break;
        }
        if (ret & BDRV_BLOCK_ZERO) {
            pnum_zero = MIN(pnum_zero, bytes);
        } else {
            pnum_data = MAX(pnum_data, bytes);
        }
    }

    if (pnum_data) {
        *pnum = pnum_data;
        return BDRV_BLOCK_DATA;
    } else {
        *pnum = pnum_zero;
        return BDRV_BLOCK_ZERO;
    }
}

static const char *const quorum_strong_runtime_opts[] = {
    QUORUM_OPT_VOTE_THRESHOLD,
    QUORUM_OPT_BLKVERIFY,
    QUORUM_OPT_REWRITE,
    QUORUM_OPT_READ_PATTERN,

    NULL
};

static BlockDriver bdrv_quorum = {
    .format_name                        = "quorum",

    .instance_size                      = sizeof(BDRVQuorumState),

    .bdrv_open                          = quorum_open,
    .bdrv_close                         = quorum_close,
    .bdrv_gather_child_options          = quorum_gather_child_options,
    .bdrv_dirname                       = quorum_dirname,
    .bdrv_co_block_status               = quorum_co_block_status,

    .bdrv_co_flush                      = quorum_co_flush,

    .bdrv_co_getlength                  = quorum_co_getlength,

    .bdrv_co_preadv                     = quorum_co_preadv,
    .bdrv_co_pwritev                    = quorum_co_pwritev,
    .bdrv_co_pwrite_zeroes              = quorum_co_pwrite_zeroes,

    .bdrv_add_child                     = quorum_add_child,
    .bdrv_del_child                     = quorum_del_child,

    .bdrv_child_perm                    = quorum_child_perm,

    .bdrv_recurse_can_replace           = quorum_recurse_can_replace,

    .strong_runtime_opts                = quorum_strong_runtime_opts,
};

static void bdrv_quorum_init(void)
{
    if (!qcrypto_hash_supports(QCRYPTO_HASH_ALGO_SHA256)) {
        return;
    }
    bdrv_register(&bdrv_quorum);
}

block_init(bdrv_quorum_init);
