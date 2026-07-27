/*
 * Block node draining tests
 *
 * Copyright (c) 2017 Kevin Wolf <kwolf@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "block/block_int.h"
#include "block/blockjob_int.h"
#include "system/block-backend.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "iothread.h"

static QemuEvent done_event;

typedef struct BDRVTestState {
    int drain_count;
    AioContext *bh_indirection_ctx;
    bool sleep_in_drain_begin;
} BDRVTestState;

static void coroutine_fn sleep_in_drain_begin(void *opaque)
{
    BlockDriverState *bs = opaque;

    qemu_co_sleep_ns(QEMU_CLOCK_REALTIME, 100000);
    bdrv_dec_in_flight(bs);
}

static void bdrv_test_drain_begin(BlockDriverState *bs)
{
    BDRVTestState *s = bs->opaque;
    s->drain_count++;
    if (s->sleep_in_drain_begin) {
        Coroutine *co = qemu_coroutine_create(sleep_in_drain_begin, bs);
        bdrv_inc_in_flight(bs);
        aio_co_enter(bdrv_get_aio_context(bs), co);
    }
}

static void bdrv_test_drain_end(BlockDriverState *bs)
{
    BDRVTestState *s = bs->opaque;
    s->drain_count--;
}

static void bdrv_test_close(BlockDriverState *bs)
{
    BDRVTestState *s = bs->opaque;
    g_assert_cmpint(s->drain_count, >, 0);
}

static void co_reenter_bh(void *opaque)
{
    aio_co_wake(opaque);
}

static int coroutine_fn bdrv_test_co_preadv(BlockDriverState *bs,
                                            int64_t offset, int64_t bytes,
                                            QEMUIOVector *qiov,
                                            BdrvRequestFlags flags)
{
    BDRVTestState *s = bs->opaque;

    qemu_co_sleep_ns(QEMU_CLOCK_REALTIME, 100000);

    if (s->bh_indirection_ctx) {
        aio_bh_schedule_oneshot(s->bh_indirection_ctx, co_reenter_bh,
                                qemu_coroutine_self());
        qemu_coroutine_yield();
    }

    return 0;
}

static int bdrv_test_co_change_backing_file(BlockDriverState *bs,
                                            const char *backing_file,
                                            const char *backing_fmt)
{
    return 0;
}

static BlockDriver bdrv_test = {
    .format_name            = "test",
    .instance_size          = sizeof(BDRVTestState),
    .supports_backing       = true,

    .bdrv_close             = bdrv_test_close,
    .bdrv_co_preadv         = bdrv_test_co_preadv,

    .bdrv_drain_begin       = bdrv_test_drain_begin,
    .bdrv_drain_end         = bdrv_test_drain_end,

    .bdrv_child_perm        = bdrv_default_perms,

    .bdrv_co_change_backing_file = bdrv_test_co_change_backing_file,
};

static void aio_ret_cb(void *opaque, int ret)
{
    int *aio_ret = opaque;
    *aio_ret = ret;
}

typedef struct CallInCoroutineData {
    void (*entry)(void);
    bool done;
} CallInCoroutineData;

static coroutine_fn void call_in_coroutine_entry(void *opaque)
{
    CallInCoroutineData *data = opaque;

    data->entry();
    data->done = true;
}

static void call_in_coroutine(void (*entry)(void))
{
    Coroutine *co;
    CallInCoroutineData data = {
        .entry  = entry,
        .done   = false,
    };

    co = qemu_coroutine_create(call_in_coroutine_entry, &data);
    qemu_coroutine_enter(co);
    while (!data.done) {
        aio_poll(qemu_get_aio_context(), true);
    }
}

enum drain_type {
    BDRV_DRAIN_ALL,
    BDRV_DRAIN,
    DRAIN_TYPE_MAX,
};

static void do_drain_begin(enum drain_type drain_type, BlockDriverState *bs)
{
    switch (drain_type) {
    case BDRV_DRAIN_ALL:        bdrv_drain_all_begin(); break;
    case BDRV_DRAIN:            bdrv_drained_begin(bs); break;
    default:                    g_assert_not_reached();
    }
}

static void do_drain_end(enum drain_type drain_type, BlockDriverState *bs)
{
    switch (drain_type) {
    case BDRV_DRAIN_ALL:        bdrv_drain_all_end(); break;
    case BDRV_DRAIN:            bdrv_drained_end(bs); break;
    default:                    g_assert_not_reached();
    }
}

static void do_drain_begin_unlocked(enum drain_type drain_type, BlockDriverState *bs)
{
    do_drain_begin(drain_type, bs);
}

static BlockBackend * no_coroutine_fn test_setup(void)
{
    BlockBackend *blk;
    BlockDriverState *bs, *backing;

    blk = blk_new(qemu_get_aio_context(), BLK_PERM_ALL, BLK_PERM_ALL);
    bs = bdrv_new_open_driver(&bdrv_test, "test-node", BDRV_O_RDWR,
                              &error_abort);
    blk_insert_bs(blk, bs, &error_abort);

    backing = bdrv_new_open_driver(&bdrv_test, "backing", 0, &error_abort);
    bdrv_graph_wrlock_drained();
    bdrv_set_backing_hd(bs, backing, &error_abort);
    bdrv_graph_wrunlock();

    bdrv_unref(backing);
    bdrv_unref(bs);

    return blk;
}

static void do_drain_end_unlocked(enum drain_type drain_type, BlockDriverState *bs)
{
    do_drain_end(drain_type, bs);
}

static void coroutine_mixed_fn TSA_NO_TSA
test_drv_cb_common(BlockBackend *blk, enum drain_type drain_type,
                   bool recursive)
{
    BlockDriverState *bs = blk_bs(blk);
    BlockDriverState *backing = bs->backing->bs;
    BDRVTestState *s, *backing_s;
    BlockAIOCB *acb;
    int aio_ret;

    QEMUIOVector qiov = QEMU_IOVEC_INIT_BUF(qiov, NULL, 0);

    s = bs->opaque;
    backing_s = backing->opaque;

    g_assert_cmpint(s->drain_count, ==, 0);
    g_assert_cmpint(backing_s->drain_count, ==, 0);

    do_drain_begin(drain_type, bs);

    g_assert_cmpint(s->drain_count, ==, 1);
    g_assert_cmpint(backing_s->drain_count, ==, !!recursive);

    do_drain_end(drain_type, bs);

    g_assert_cmpint(s->drain_count, ==, 0);
    g_assert_cmpint(backing_s->drain_count, ==, 0);

    aio_ret = -EINPROGRESS;
    acb = blk_aio_preadv(blk, 0, &qiov, 0, aio_ret_cb, &aio_ret);
    g_assert(acb != NULL);
    g_assert_cmpint(aio_ret, ==, -EINPROGRESS);

    g_assert_cmpint(s->drain_count, ==, 0);
    g_assert_cmpint(backing_s->drain_count, ==, 0);

    do_drain_begin(drain_type, bs);

    g_assert_cmpint(aio_ret, ==, 0);
    g_assert_cmpint(s->drain_count, ==, 1);
    g_assert_cmpint(backing_s->drain_count, ==, !!recursive);

    do_drain_end(drain_type, bs);

    g_assert_cmpint(s->drain_count, ==, 0);
    g_assert_cmpint(backing_s->drain_count, ==, 0);
}

static void test_drv_cb_drain_all(void)
{
    BlockBackend *blk = test_setup();
    test_drv_cb_common(blk, BDRV_DRAIN_ALL, true);
    blk_unref(blk);
}

static void test_drv_cb_drain(void)
{
    BlockBackend *blk = test_setup();
    test_drv_cb_common(blk, BDRV_DRAIN, false);
    blk_unref(blk);
}

static void coroutine_fn test_drv_cb_co_drain_all_entry(void)
{
    BlockBackend *blk = blk_all_next(NULL);
    test_drv_cb_common(blk, BDRV_DRAIN_ALL, true);
}

static void test_drv_cb_co_drain_all(void)
{
    BlockBackend *blk = test_setup();
    call_in_coroutine(test_drv_cb_co_drain_all_entry);
    blk_unref(blk);
}

static void coroutine_fn test_drv_cb_co_drain_entry(void)
{
    BlockBackend *blk = blk_all_next(NULL);
    test_drv_cb_common(blk, BDRV_DRAIN, false);
}

static void test_drv_cb_co_drain(void)
{
    BlockBackend *blk = test_setup();
    call_in_coroutine(test_drv_cb_co_drain_entry);
    blk_unref(blk);
}

static void coroutine_mixed_fn TSA_NO_TSA
test_quiesce_common(BlockBackend *blk, enum drain_type drain_type,
                    bool recursive)
{
    BlockDriverState *bs = blk_bs(blk);
    BlockDriverState *backing = bs->backing->bs;

    g_assert_cmpint(bs->quiesce_counter, ==, 0);
    g_assert_cmpint(backing->quiesce_counter, ==, 0);

    do_drain_begin(drain_type, bs);

    if (drain_type == BDRV_DRAIN_ALL) {
        g_assert_cmpint(bs->quiesce_counter, ==, 2);
    } else {
        g_assert_cmpint(bs->quiesce_counter, ==, 1);
    }
    g_assert_cmpint(backing->quiesce_counter, ==, !!recursive);

    do_drain_end(drain_type, bs);

    g_assert_cmpint(bs->quiesce_counter, ==, 0);
    g_assert_cmpint(backing->quiesce_counter, ==, 0);
}

static void test_quiesce_drain_all(void)
{
    BlockBackend *blk = test_setup();
    test_quiesce_common(blk, BDRV_DRAIN_ALL, true);
    blk_unref(blk);
}

static void test_quiesce_drain(void)
{
    BlockBackend *blk = test_setup();
    test_quiesce_common(blk, BDRV_DRAIN, false);
    blk_unref(blk);
}

static void coroutine_fn test_quiesce_co_drain_all_entry(void)
{
    BlockBackend *blk = blk_all_next(NULL);
    test_quiesce_common(blk, BDRV_DRAIN_ALL, true);
}

static void test_quiesce_co_drain_all(void)
{
    BlockBackend *blk = test_setup();
    call_in_coroutine(test_quiesce_co_drain_all_entry);
    blk_unref(blk);
}

static void coroutine_fn test_quiesce_co_drain_entry(void)
{
    BlockBackend *blk = blk_all_next(NULL);
    test_quiesce_common(blk, BDRV_DRAIN, false);
}

static void test_quiesce_co_drain(void)
{
    BlockBackend *blk = test_setup();
    call_in_coroutine(test_quiesce_co_drain_entry);
    blk_unref(blk);
}

static void test_nested(void)
{
    BlockBackend *blk;
    BlockDriverState *bs, *backing;
    BDRVTestState *s, *backing_s;
    enum drain_type outer, inner;

    blk = blk_new(qemu_get_aio_context(), BLK_PERM_ALL, BLK_PERM_ALL);
    bs = bdrv_new_open_driver(&bdrv_test, "test-node", BDRV_O_RDWR,
                              &error_abort);
    s = bs->opaque;
    blk_insert_bs(blk, bs, &error_abort);

    backing = bdrv_new_open_driver(&bdrv_test, "backing", 0, &error_abort);
    backing_s = backing->opaque;
    bdrv_graph_wrlock_drained();
    bdrv_set_backing_hd(bs, backing, &error_abort);
    bdrv_graph_wrunlock();

    for (outer = 0; outer < DRAIN_TYPE_MAX; outer++) {
        for (inner = 0; inner < DRAIN_TYPE_MAX; inner++) {
            int backing_quiesce = (outer == BDRV_DRAIN_ALL) +
                                  (inner == BDRV_DRAIN_ALL);

            g_assert_cmpint(bs->quiesce_counter, ==, 0);
            g_assert_cmpint(backing->quiesce_counter, ==, 0);
            g_assert_cmpint(s->drain_count, ==, 0);
            g_assert_cmpint(backing_s->drain_count, ==, 0);

            do_drain_begin(outer, bs);
            do_drain_begin(inner, bs);

            g_assert_cmpint(bs->quiesce_counter, ==, 2 + !!backing_quiesce);
            g_assert_cmpint(backing->quiesce_counter, ==, backing_quiesce);
            g_assert_cmpint(s->drain_count, ==, 1);
            g_assert_cmpint(backing_s->drain_count, ==, !!backing_quiesce);

            do_drain_end(inner, bs);
            do_drain_end(outer, bs);

            g_assert_cmpint(bs->quiesce_counter, ==, 0);
            g_assert_cmpint(backing->quiesce_counter, ==, 0);
            g_assert_cmpint(s->drain_count, ==, 0);
            g_assert_cmpint(backing_s->drain_count, ==, 0);
        }
    }

    bdrv_unref(backing);
    bdrv_unref(bs);
    blk_unref(blk);
}

static void test_graph_change_drain_all(void)
{
    BlockBackend *blk_a, *blk_b;
    BlockDriverState *bs_a, *bs_b;
    BDRVTestState *a_s, *b_s;

    blk_a = blk_new(qemu_get_aio_context(), BLK_PERM_ALL, BLK_PERM_ALL);
    bs_a = bdrv_new_open_driver(&bdrv_test, "test-node-a", BDRV_O_RDWR,
                                &error_abort);
    a_s = bs_a->opaque;
    blk_insert_bs(blk_a, bs_a, &error_abort);

    g_assert_cmpint(bs_a->quiesce_counter, ==, 0);
    g_assert_cmpint(a_s->drain_count, ==, 0);

    bdrv_drain_all_begin();

    g_assert_cmpint(bs_a->quiesce_counter, ==, 1);
    g_assert_cmpint(a_s->drain_count, ==, 1);

    blk_b = blk_new(qemu_get_aio_context(), BLK_PERM_ALL, BLK_PERM_ALL);
    bs_b = bdrv_new_open_driver(&bdrv_test, "test-node-b", BDRV_O_RDWR,
                                &error_abort);
    b_s = bs_b->opaque;
    blk_insert_bs(blk_b, bs_b, &error_abort);

    g_assert_cmpint(bs_a->quiesce_counter, ==, 1);
    g_assert_cmpint(bs_b->quiesce_counter, ==, 1);
    g_assert_cmpint(a_s->drain_count, ==, 1);
    g_assert_cmpint(b_s->drain_count, ==, 1);

    blk_unref(blk_a);

    g_assert_cmpint(bs_a->quiesce_counter, ==, 1);
    g_assert_cmpint(bs_b->quiesce_counter, ==, 1);
    g_assert_cmpint(a_s->drain_count, ==, 1);
    g_assert_cmpint(b_s->drain_count, ==, 1);

    bdrv_unref(bs_a);

    g_assert_cmpint(bs_b->quiesce_counter, ==, 1);
    g_assert_cmpint(b_s->drain_count, ==, 1);

    bdrv_drain_all_end();

    g_assert_cmpint(bs_b->quiesce_counter, ==, 0);
    g_assert_cmpint(b_s->drain_count, ==, 0);

    bdrv_unref(bs_b);
    blk_unref(blk_b);
}

struct test_iothread_data {
    BlockDriverState *bs;
    enum drain_type drain_type;
    int *aio_ret;
    bool co_done;
};

static void coroutine_fn test_iothread_drain_co_entry(void *opaque)
{
    struct test_iothread_data *data = opaque;

    do_drain_begin(data->drain_type, data->bs);
    g_assert_cmpint(*data->aio_ret, ==, 0);
    do_drain_end(data->drain_type, data->bs);

    data->co_done = true;
    aio_wait_kick();
}

static void test_iothread_aio_cb(void *opaque, int ret)
{
    int *aio_ret = opaque;
    *aio_ret = ret;
    qemu_event_set(&done_event);
}

static void test_iothread_main_thread_bh(void *opaque)
{
    struct test_iothread_data *data = opaque;

    bdrv_flush(data->bs);
    bdrv_dec_in_flight(data->bs);
}

static void test_iothread_common(enum drain_type drain_type, int drain_thread)
{
    BlockBackend *blk;
    BlockDriverState *bs;
    BDRVTestState *s;
    BlockAIOCB *acb;
    Coroutine *co;
    int aio_ret;
    struct test_iothread_data data;

    IOThread *a = iothread_new();
    IOThread *b = iothread_new();
    AioContext *ctx_a = iothread_get_aio_context(a);
    AioContext *ctx_b = iothread_get_aio_context(b);

    QEMUIOVector qiov = QEMU_IOVEC_INIT_BUF(qiov, NULL, 0);

    if (drain_type == BDRV_DRAIN_ALL && drain_thread != 0) {
        goto out;
    }

    blk = blk_new(qemu_get_aio_context(), BLK_PERM_ALL, BLK_PERM_ALL);
    bs = bdrv_new_open_driver(&bdrv_test, "test-node", BDRV_O_RDWR,
                              &error_abort);
    s = bs->opaque;
    blk_insert_bs(blk, bs, &error_abort);
    blk_set_disable_request_queuing(blk, true);

    blk_set_aio_context(blk, ctx_a, &error_abort);

    s->bh_indirection_ctx = ctx_b;

    aio_ret = -EINPROGRESS;
    qemu_event_reset(&done_event);

    if (drain_thread == 0) {
        acb = blk_aio_preadv(blk, 0, &qiov, 0, test_iothread_aio_cb, &aio_ret);
    } else {
        acb = blk_aio_preadv(blk, 0, &qiov, 0, aio_ret_cb, &aio_ret);
    }
    g_assert(acb != NULL);
    g_assert_cmpint(aio_ret, ==, -EINPROGRESS);

    data = (struct test_iothread_data) {
        .bs         = bs,
        .drain_type = drain_type,
        .aio_ret    = &aio_ret,
    };

    switch (drain_thread) {
    case 0:
        bdrv_inc_in_flight(bs);
        aio_bh_schedule_oneshot(ctx_a, test_iothread_main_thread_bh, &data);

        do_drain_begin(drain_type, bs);
        g_assert_cmpint(bs->in_flight, ==, 0);

        qemu_event_wait(&done_event);

        g_assert_cmpint(aio_ret, ==, 0);
        do_drain_end(drain_type, bs);
        break;
    case 1:
        co = qemu_coroutine_create(test_iothread_drain_co_entry, &data);
        aio_co_enter(ctx_a, co);
        AIO_WAIT_WHILE_UNLOCKED(NULL, !data.co_done);
        break;
    default:
        g_assert_not_reached();
    }

    blk_set_aio_context(blk, qemu_get_aio_context(), &error_abort);

    bdrv_unref(bs);
    blk_unref(blk);

out:
    iothread_join(a);
    iothread_join(b);
}

static void test_iothread_drain_all(void)
{
    test_iothread_common(BDRV_DRAIN_ALL, 0);
    test_iothread_common(BDRV_DRAIN_ALL, 1);
}

static void test_iothread_drain(void)
{
    test_iothread_common(BDRV_DRAIN, 0);
    test_iothread_common(BDRV_DRAIN, 1);
}


typedef struct TestBlockJob {
    BlockJob common;
    BlockDriverState *bs;
    int run_ret;
    int prepare_ret;

    bool running;
    bool should_complete;
} TestBlockJob;

static int test_job_prepare(Job *job)
{
    TestBlockJob *s = container_of(job, TestBlockJob, common.job);

    bdrv_flush(s->bs);
    return s->prepare_ret;
}

static void test_job_commit(Job *job)
{
    TestBlockJob *s = container_of(job, TestBlockJob, common.job);

    bdrv_flush(s->bs);
}

static void test_job_abort(Job *job)
{
    TestBlockJob *s = container_of(job, TestBlockJob, common.job);

    bdrv_flush(s->bs);
}

static int coroutine_fn test_job_run(Job *job, Error **errp)
{
    TestBlockJob *s = container_of(job, TestBlockJob, common.job);

    qatomic_set(&s->running, true);

    job_transition_to_ready(&s->common.job);
    while (!qatomic_read(&s->should_complete)) {
        qemu_co_sleep_ns(QEMU_CLOCK_REALTIME, 1000000);

        job_pause_point(&s->common.job);
    }

    return s->run_ret;
}

static void test_job_complete(Job *job, Error **errp)
{
    TestBlockJob *s = container_of(job, TestBlockJob, common.job);
    qatomic_set(&s->should_complete, true);
}

BlockJobDriver test_job_driver = {
    .job_driver = {
        .instance_size  = sizeof(TestBlockJob),
        .free           = block_job_free,
        .user_resume    = block_job_user_resume,
        .run            = test_job_run,
        .complete       = test_job_complete,
        .prepare        = test_job_prepare,
        .commit         = test_job_commit,
        .abort          = test_job_abort,
    },
};

enum test_job_result {
    TEST_JOB_SUCCESS,
    TEST_JOB_FAIL_RUN,
    TEST_JOB_FAIL_PREPARE,
};

enum test_job_drain_node {
    TEST_JOB_DRAIN_SRC,
    TEST_JOB_DRAIN_SRC_CHILD,
};

static void test_blockjob_common_drain_node(enum drain_type drain_type,
                                            bool use_iothread,
                                            enum test_job_result result,
                                            enum test_job_drain_node drain_node)
{
    BlockBackend *blk_src, *blk_target;
    BlockDriverState *src, *src_backing, *src_overlay, *target, *drain_bs;
    BlockJob *job;
    TestBlockJob *tjob;
    IOThread *iothread = NULL;
    int ret = -1;

    src = bdrv_new_open_driver(&bdrv_test, "source", BDRV_O_RDWR,
                               &error_abort);
    src_backing = bdrv_new_open_driver(&bdrv_test, "source-backing",
                                       BDRV_O_RDWR, &error_abort);
    src_overlay = bdrv_new_open_driver(&bdrv_test, "source-overlay",
                                       BDRV_O_RDWR, &error_abort);

    bdrv_graph_wrlock_drained();
    bdrv_set_backing_hd(src_overlay, src, &error_abort);
    bdrv_unref(src);
    bdrv_set_backing_hd(src, src_backing, &error_abort);
    bdrv_unref(src_backing);
    bdrv_graph_wrunlock();

    blk_src = blk_new(qemu_get_aio_context(), BLK_PERM_ALL, BLK_PERM_ALL);
    blk_insert_bs(blk_src, src_overlay, &error_abort);

    switch (drain_node) {
    case TEST_JOB_DRAIN_SRC:
        drain_bs = src;
        break;
    case TEST_JOB_DRAIN_SRC_CHILD:
        drain_bs = src_backing;
        break;
    default:
        g_assert_not_reached();
    }

    if (use_iothread) {
        AioContext *ctx;

        iothread = iothread_new();
        ctx = iothread_get_aio_context(iothread);
        blk_set_aio_context(blk_src, ctx, &error_abort);
    }

    target = bdrv_new_open_driver(&bdrv_test, "target", BDRV_O_RDWR,
                                  &error_abort);
    blk_target = blk_new(qemu_get_aio_context(), BLK_PERM_ALL, BLK_PERM_ALL);
    blk_insert_bs(blk_target, target, &error_abort);
    blk_set_allow_aio_context_change(blk_target, true);

    tjob = block_job_create("job0", &test_job_driver, NULL, src,
                            0, BLK_PERM_ALL,
                            0, 0, NULL, NULL, &error_abort);
    tjob->bs = src;
    job = &tjob->common;

    bdrv_graph_wrlock_drained();
    block_job_add_bdrv(job, "target", target, 0, BLK_PERM_ALL, &error_abort);
    bdrv_graph_wrunlock();

    switch (result) {
    case TEST_JOB_SUCCESS:
        break;
    case TEST_JOB_FAIL_RUN:
        tjob->run_ret = -EIO;
        break;
    case TEST_JOB_FAIL_PREPARE:
        tjob->prepare_ret = -EIO;
        break;
    }

    job_start(&job->job);

    if (use_iothread) {
        while (!qatomic_read(&tjob->running)) {
            aio_poll(qemu_get_aio_context(), false);
        }
    }

    WITH_JOB_LOCK_GUARD() {
        g_assert_cmpint(job->job.pause_count, ==, 0);
        g_assert_false(job->job.paused);
        g_assert_true(qatomic_read(&tjob->running));
        g_assert_true(job->job.busy);
    }

    do_drain_begin_unlocked(drain_type, drain_bs);

    WITH_JOB_LOCK_GUARD() {
        if (drain_type == BDRV_DRAIN_ALL) {
            g_assert_cmpint(job->job.pause_count, ==, 2);
        } else {
            g_assert_cmpint(job->job.pause_count, ==, 1);
        }
        g_assert_true(job->job.paused);
        g_assert_false(job->job.busy);
    }

    do_drain_end_unlocked(drain_type, drain_bs);

    if (use_iothread) {
        while (job_is_paused(&job->job)) {
            aio_poll(qemu_get_aio_context(), false);
        }
    }

    WITH_JOB_LOCK_GUARD() {
        g_assert_cmpint(job->job.pause_count, ==, 0);
        g_assert_false(job->job.paused);
        g_assert_true(job->job.busy);
    }

    do_drain_begin_unlocked(drain_type, target);

    WITH_JOB_LOCK_GUARD() {
        if (drain_type == BDRV_DRAIN_ALL) {
            g_assert_cmpint(job->job.pause_count, ==, 2);
        } else {
            g_assert_cmpint(job->job.pause_count, ==, 1);
        }
        g_assert_true(job->job.paused);
        g_assert_false(job->job.busy);
    }

    do_drain_end_unlocked(drain_type, target);

    if (use_iothread) {
        while (job_is_paused(&job->job)) {
            aio_poll(qemu_get_aio_context(), false);
        }
    }

    WITH_JOB_LOCK_GUARD() {
        g_assert_cmpint(job->job.pause_count, ==, 0);
        g_assert_false(job->job.paused);
        g_assert_true(job->job.busy);
    }

    WITH_JOB_LOCK_GUARD() {
        ret = job_complete_sync_locked(&job->job, &error_abort);
    }
    g_assert_cmpint(ret, ==, (result == TEST_JOB_SUCCESS ? 0 : -EIO));

    if (use_iothread) {
        blk_set_aio_context(blk_src, qemu_get_aio_context(), &error_abort);
        assert(blk_get_aio_context(blk_target) == qemu_get_aio_context());
    }

    blk_unref(blk_src);
    blk_unref(blk_target);
    bdrv_unref(src_overlay);
    bdrv_unref(target);

    if (iothread) {
        iothread_join(iothread);
    }
}

static void test_blockjob_common(enum drain_type drain_type, bool use_iothread,
                                 enum test_job_result result)
{
    test_blockjob_common_drain_node(drain_type, use_iothread, result,
                                    TEST_JOB_DRAIN_SRC);
    test_blockjob_common_drain_node(drain_type, use_iothread, result,
                                    TEST_JOB_DRAIN_SRC_CHILD);
}

static void test_blockjob_drain_all(void)
{
    test_blockjob_common(BDRV_DRAIN_ALL, false, TEST_JOB_SUCCESS);
}

static void test_blockjob_drain(void)
{
    test_blockjob_common(BDRV_DRAIN, false, TEST_JOB_SUCCESS);
}

static void test_blockjob_error_drain_all(void)
{
    test_blockjob_common(BDRV_DRAIN_ALL, false, TEST_JOB_FAIL_RUN);
    test_blockjob_common(BDRV_DRAIN_ALL, false, TEST_JOB_FAIL_PREPARE);
}

static void test_blockjob_error_drain(void)
{
    test_blockjob_common(BDRV_DRAIN, false, TEST_JOB_FAIL_RUN);
    test_blockjob_common(BDRV_DRAIN, false, TEST_JOB_FAIL_PREPARE);
}

static void test_blockjob_iothread_drain_all(void)
{
    test_blockjob_common(BDRV_DRAIN_ALL, true, TEST_JOB_SUCCESS);
}

static void test_blockjob_iothread_drain(void)
{
    test_blockjob_common(BDRV_DRAIN, true, TEST_JOB_SUCCESS);
}

static void test_blockjob_iothread_error_drain_all(void)
{
    test_blockjob_common(BDRV_DRAIN_ALL, true, TEST_JOB_FAIL_RUN);
    test_blockjob_common(BDRV_DRAIN_ALL, true, TEST_JOB_FAIL_PREPARE);
}

static void test_blockjob_iothread_error_drain(void)
{
    test_blockjob_common(BDRV_DRAIN, true, TEST_JOB_FAIL_RUN);
    test_blockjob_common(BDRV_DRAIN, true, TEST_JOB_FAIL_PREPARE);
}


typedef struct BDRVTestTopState {
    BdrvChild *wait_child;
} BDRVTestTopState;

static void bdrv_test_top_close(BlockDriverState *bs)
{
    BdrvChild *c, *next_c;

    bdrv_graph_wrlock_drained();
    QLIST_FOREACH_SAFE(c, &bs->children, next, next_c) {
        bdrv_unref_child(bs, c);
    }
    bdrv_graph_wrunlock();
}

static int coroutine_fn GRAPH_RDLOCK
bdrv_test_top_co_preadv(BlockDriverState *bs, int64_t offset, int64_t bytes,
                        QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    BDRVTestTopState *tts = bs->opaque;
    return bdrv_co_preadv(tts->wait_child, offset, bytes, qiov, flags);
}

static BlockDriver bdrv_test_top_driver = {
    .format_name            = "test_top_driver",
    .instance_size          = sizeof(BDRVTestTopState),

    .bdrv_close             = bdrv_test_top_close,
    .bdrv_co_preadv         = bdrv_test_top_co_preadv,

    .bdrv_child_perm        = bdrv_default_perms,
};

typedef struct TestCoDeleteByDrainData {
    BlockBackend *blk;
    bool detach_instead_of_delete;
    bool done;
} TestCoDeleteByDrainData;

static void coroutine_fn test_co_delete_by_drain(void *opaque)
{
    TestCoDeleteByDrainData *dbdd = opaque;
    BlockBackend *blk = dbdd->blk;
    BlockDriverState *bs = blk_bs(blk);
    BDRVTestTopState *tts = bs->opaque;
    void *buffer = g_malloc(65536);
    QEMUIOVector qiov = QEMU_IOVEC_INIT_BUF(qiov, buffer, 65536);

    bdrv_graph_co_rdlock();
    bdrv_co_preadv(tts->wait_child, 0, 65536, &qiov, 0);
    bdrv_graph_co_rdunlock();

    g_assert_cmpint(bs->refcnt, ==, 1);

    if (!dbdd->detach_instead_of_delete) {
        blk_co_unref(blk);
    } else {
        BdrvChild *c, *next_c;
        bdrv_graph_co_rdlock();
        QLIST_FOREACH_SAFE(c, &bs->children, next, next_c) {
            bdrv_graph_co_rdunlock();
            bdrv_drain_all_begin();
            bdrv_co_unref_child(bs, c);
            bdrv_drain_all_end();
            bdrv_graph_co_rdlock();
        }
        bdrv_graph_co_rdunlock();
    }

    dbdd->done = true;
    g_free(buffer);
}

static void do_test_delete_by_drain(bool detach_instead_of_delete,
                                    enum drain_type drain_type)
{
    BlockBackend *blk;
    BlockDriverState *bs, *child_bs, *null_bs;
    BDRVTestTopState *tts;
    TestCoDeleteByDrainData dbdd;
    Coroutine *co;

    bs = bdrv_new_open_driver(&bdrv_test_top_driver, "top", BDRV_O_RDWR,
                              &error_abort);
    bs->total_sectors = 65536 >> BDRV_SECTOR_BITS;
    tts = bs->opaque;

    null_bs = bdrv_open("null-co://", NULL, NULL, BDRV_O_RDWR | BDRV_O_PROTOCOL,
                        &error_abort);
    bdrv_graph_wrlock_drained();
    bdrv_attach_child(bs, null_bs, "null-child", &child_of_bds,
                      BDRV_CHILD_DATA, &error_abort);
    bdrv_graph_wrunlock();

    child_bs = bdrv_new_open_driver(&bdrv_test, "child", BDRV_O_RDWR,
                                    &error_abort);
    child_bs->total_sectors = 65536 >> BDRV_SECTOR_BITS;
    bdrv_graph_wrlock_drained();
    tts->wait_child = bdrv_attach_child(bs, child_bs, "wait-child",
                                        &child_of_bds,
                                        BDRV_CHILD_DATA | BDRV_CHILD_PRIMARY,
                                        &error_abort);
    bdrv_graph_wrunlock();

    null_bs = bdrv_open("null-co://", NULL, NULL, BDRV_O_RDWR | BDRV_O_PROTOCOL,
                        &error_abort);
    bdrv_graph_wrlock_drained();
    bdrv_attach_child(bs, null_bs, "null-child", &child_of_bds, BDRV_CHILD_DATA,
                      &error_abort);
    bdrv_graph_wrunlock();

    blk = blk_new(qemu_get_aio_context(), BLK_PERM_ALL, BLK_PERM_ALL);
    blk_insert_bs(blk, bs, &error_abort);

    bdrv_unref(bs);

    g_assert_cmpint(bs->refcnt, ==, 1);
    g_assert_cmpint(child_bs->refcnt, ==, 1);
    g_assert_cmpint(null_bs->refcnt, ==, 1);


    dbdd = (TestCoDeleteByDrainData){
        .blk = blk,
        .detach_instead_of_delete = detach_instead_of_delete,
        .done = false,
    };
    co = qemu_coroutine_create(test_co_delete_by_drain, &dbdd);
    qemu_coroutine_enter(co);

    switch (drain_type) {
    case BDRV_DRAIN:
        bdrv_ref(child_bs);
        bdrv_drain(child_bs);
        bdrv_unref(child_bs);
        break;
    case BDRV_DRAIN_ALL:
        bdrv_drain_all_begin();
        bdrv_drain_all_end();
        break;
    default:
        g_assert_not_reached();
    }

    while (!dbdd.done) {
        aio_poll(qemu_get_aio_context(), true);
    }

    if (detach_instead_of_delete) {
        blk_unref(blk);
    }
}

static void test_delete_by_drain(void)
{
    do_test_delete_by_drain(false, BDRV_DRAIN);
}

static void test_detach_by_drain_all(void)
{
    do_test_delete_by_drain(true, BDRV_DRAIN_ALL);
}

static void test_detach_by_drain(void)
{
    do_test_delete_by_drain(true, BDRV_DRAIN);
}


struct detach_by_parent_data {
    BlockDriverState *parent_b;
    BdrvChild *child_b;
    BlockDriverState *c;
    BdrvChild *child_c;
    bool by_parent_cb;
    bool detach_on_drain;
};
static struct detach_by_parent_data detach_by_parent_data;

static void no_coroutine_fn detach_indirect_bh(void *opaque)
{
    struct detach_by_parent_data *data = opaque;

    bdrv_dec_in_flight(data->child_b->bs);

    bdrv_graph_wrlock_drained();
    bdrv_unref_child(data->parent_b, data->child_b);

    bdrv_ref(data->c);
    data->child_c = bdrv_attach_child(data->parent_b, data->c, "PB-C",
                                      &child_of_bds, BDRV_CHILD_DATA,
                                      &error_abort);
    bdrv_graph_wrunlock();
}

static void coroutine_mixed_fn detach_by_parent_aio_cb(void *opaque, int ret)
{
    struct detach_by_parent_data *data = &detach_by_parent_data;

    g_assert_cmpint(ret, ==, 0);
    if (data->by_parent_cb) {
        bdrv_inc_in_flight(data->child_b->bs);
        aio_bh_schedule_oneshot(qemu_get_current_aio_context(),
                                detach_indirect_bh, &detach_by_parent_data);
    }
}

static void GRAPH_RDLOCK detach_by_driver_cb_drained_begin(BdrvChild *child)
{
    struct detach_by_parent_data *data = &detach_by_parent_data;

    if (!data->detach_on_drain) {
        return;
    }
    data->detach_on_drain = false;

    bdrv_inc_in_flight(data->child_b->bs);
    aio_bh_schedule_oneshot(qemu_get_current_aio_context(),
                            detach_indirect_bh, &detach_by_parent_data);
    child_of_bds.drained_begin(child);
}

static BdrvChildClass detach_by_driver_cb_class;

static void TSA_NO_TSA test_detach_indirect(bool by_parent_cb)
{
    BlockBackend *blk;
    BlockDriverState *parent_a, *parent_b, *a, *b, *c;
    BdrvChild *child_a, *child_b;
    BlockAIOCB *acb;

    QEMUIOVector qiov = QEMU_IOVEC_INIT_BUF(qiov, NULL, 0);

    if (!by_parent_cb) {
        detach_by_driver_cb_class = child_of_bds;
        detach_by_driver_cb_class.drained_begin =
            detach_by_driver_cb_drained_begin;
        detach_by_driver_cb_class.drained_end = NULL;
        detach_by_driver_cb_class.drained_poll = NULL;
    }

    detach_by_parent_data = (struct detach_by_parent_data) {
        .detach_on_drain = false,
    };

    parent_a = bdrv_new_open_driver(&bdrv_test, "parent-a", BDRV_O_RDWR,
                                    &error_abort);
    parent_b = bdrv_new_open_driver(&bdrv_test, "parent-b", 0,
                                    &error_abort);

    a = bdrv_new_open_driver(&bdrv_test, "a", BDRV_O_RDWR, &error_abort);
    b = bdrv_new_open_driver(&bdrv_test, "b", BDRV_O_RDWR, &error_abort);
    c = bdrv_new_open_driver(&bdrv_test, "c", BDRV_O_RDWR, &error_abort);

    blk = blk_new(qemu_get_aio_context(), BLK_PERM_ALL, BLK_PERM_ALL);
    blk_insert_bs(blk, parent_a, &error_abort);
    bdrv_unref(parent_a);

    if (!by_parent_cb) {
        BDRVTestState *s = parent_a->opaque;
        s->sleep_in_drain_begin = true;
    }

    bdrv_ref(b);
    bdrv_ref(a);
    bdrv_graph_wrlock_drained();
    child_b = bdrv_attach_child(parent_b, b, "PB-B", &child_of_bds,
                                BDRV_CHILD_DATA, &error_abort);
    child_a = bdrv_attach_child(parent_b, a, "PB-A", &child_of_bds,
                                BDRV_CHILD_COW, &error_abort);

    bdrv_ref(a);
    bdrv_attach_child(parent_a, a, "PA-A",
                      by_parent_cb ? &child_of_bds : &detach_by_driver_cb_class,
                      BDRV_CHILD_DATA, &error_abort);
    bdrv_graph_wrunlock();

    g_assert_cmpint(parent_a->refcnt, ==, 1);
    g_assert_cmpint(parent_b->refcnt, ==, 1);
    g_assert_cmpint(a->refcnt, ==, 3);
    g_assert_cmpint(b->refcnt, ==, 2);
    g_assert_cmpint(c->refcnt, ==, 1);

    g_assert(QLIST_FIRST(&parent_b->children) == child_a);
    g_assert(QLIST_NEXT(child_a, next) == child_b);
    g_assert(QLIST_NEXT(child_b, next) == NULL);

    detach_by_parent_data = (struct detach_by_parent_data) {
        .parent_b = parent_b,
        .child_b = child_b,
        .c = c,
        .by_parent_cb = by_parent_cb,
        .detach_on_drain = true,
    };
    acb = blk_aio_preadv(blk, 0, &qiov, 0, detach_by_parent_aio_cb, NULL);
    g_assert(acb != NULL);

    bdrv_drained_begin(parent_b);
    bdrv_drained_begin(a);
    bdrv_drained_begin(b);
    bdrv_drained_begin(c);

    g_assert(detach_by_parent_data.child_c != NULL);

    g_assert_cmpint(parent_a->refcnt, ==, 1);
    g_assert_cmpint(parent_b->refcnt, ==, 1);
    g_assert_cmpint(a->refcnt, ==, 3);
    g_assert_cmpint(b->refcnt, ==, 1);
    g_assert_cmpint(c->refcnt, ==, 2);

    g_assert(QLIST_FIRST(&parent_b->children) == detach_by_parent_data.child_c);
    g_assert(QLIST_NEXT(detach_by_parent_data.child_c, next) == child_a);
    g_assert(QLIST_NEXT(child_a, next) == NULL);

    g_assert_cmpint(parent_a->quiesce_counter, ==, 1);
    g_assert_cmpint(parent_b->quiesce_counter, ==, 3);
    g_assert_cmpint(a->quiesce_counter, ==, 1);
    g_assert_cmpint(b->quiesce_counter, ==, 1);
    g_assert_cmpint(c->quiesce_counter, ==, 1);

    bdrv_drained_end(parent_b);
    bdrv_drained_end(a);
    bdrv_drained_end(b);
    bdrv_drained_end(c);

    bdrv_unref(parent_b);
    blk_unref(blk);

    g_assert_cmpint(a->refcnt, ==, 1);
    g_assert_cmpint(b->refcnt, ==, 1);
    g_assert_cmpint(c->refcnt, ==, 1);
    bdrv_unref(a);
    bdrv_unref(b);
    bdrv_unref(c);
}

static void test_detach_by_parent_cb(void)
{
    test_detach_indirect(true);
}

static void test_detach_by_driver_cb(void)
{
    test_detach_indirect(false);
}

static void test_append_to_drained(void)
{
    BlockBackend *blk;
    BlockDriverState *base, *overlay;
    BDRVTestState *base_s, *overlay_s;

    blk = blk_new(qemu_get_aio_context(), BLK_PERM_ALL, BLK_PERM_ALL);
    base = bdrv_new_open_driver(&bdrv_test, "base", BDRV_O_RDWR, &error_abort);
    base_s = base->opaque;
    blk_insert_bs(blk, base, &error_abort);

    overlay = bdrv_new_open_driver(&bdrv_test, "overlay", BDRV_O_RDWR,
                                   &error_abort);
    overlay_s = overlay->opaque;

    do_drain_begin(BDRV_DRAIN, base);
    g_assert_cmpint(base->quiesce_counter, ==, 1);
    g_assert_cmpint(base_s->drain_count, ==, 1);
    g_assert_cmpint(base->in_flight, ==, 0);

    bdrv_append(overlay, base, &error_abort);

    g_assert_cmpint(base->in_flight, ==, 0);
    g_assert_cmpint(overlay->in_flight, ==, 0);

    g_assert_cmpint(base->quiesce_counter, ==, 1);
    g_assert_cmpint(base_s->drain_count, ==, 1);
    g_assert_cmpint(overlay->quiesce_counter, ==, 1);
    g_assert_cmpint(overlay_s->drain_count, ==, 1);

    do_drain_end(BDRV_DRAIN, base);

    g_assert_cmpint(base->quiesce_counter, ==, 0);
    g_assert_cmpint(base_s->drain_count, ==, 0);
    g_assert_cmpint(overlay->quiesce_counter, ==, 0);
    g_assert_cmpint(overlay_s->drain_count, ==, 0);

    bdrv_unref(overlay);
    bdrv_unref(base);
    blk_unref(blk);
}

static void test_set_aio_context(void)
{
    BlockDriverState *bs;
    IOThread *a = iothread_new();
    IOThread *b = iothread_new();
    AioContext *ctx_a = iothread_get_aio_context(a);
    AioContext *ctx_b = iothread_get_aio_context(b);

    bs = bdrv_new_open_driver(&bdrv_test, "test-node", BDRV_O_RDWR,
                              &error_abort);

    bdrv_try_change_aio_context(bs, ctx_a, NULL, &error_abort);

    bdrv_try_change_aio_context(bs, ctx_b, NULL, &error_abort);
    bdrv_try_change_aio_context(bs, qemu_get_aio_context(), NULL, &error_abort);

    bdrv_unref(bs);
    iothread_join(a);
    iothread_join(b);
}


typedef struct TestDropBackingBlockJob {
    BlockJob common;
    bool *did_complete;
    BlockDriverState *detach_also;
    BlockDriverState *bs;

    bool should_complete;
} TestDropBackingBlockJob;

static int coroutine_fn test_drop_backing_job_run(Job *job, Error **errp)
{
    TestDropBackingBlockJob *s =
        container_of(job, TestDropBackingBlockJob, common.job);

    while (!qatomic_read(&s->should_complete)) {
        job_sleep_ns(job, 0);
    }

    return 0;
}

static void test_drop_backing_job_commit(Job *job)
{
    TestDropBackingBlockJob *s =
        container_of(job, TestDropBackingBlockJob, common.job);

    bdrv_graph_wrlock_drained();
    bdrv_set_backing_hd(s->bs, NULL, &error_abort);
    bdrv_set_backing_hd(s->detach_also, NULL, &error_abort);
    bdrv_graph_wrunlock();

    *s->did_complete = true;
}

static const BlockJobDriver test_drop_backing_job_driver = {
    .job_driver = {
        .instance_size  = sizeof(TestDropBackingBlockJob),
        .free           = block_job_free,
        .user_resume    = block_job_user_resume,
        .run            = test_drop_backing_job_run,
        .commit         = test_drop_backing_job_commit,
    }
};

static void test_blockjob_commit_by_drained_end(void)
{
    BlockDriverState *bs_child, *bs_parents[3];
    TestDropBackingBlockJob *job;
    bool job_has_completed = false;
    int i;

    bs_child = bdrv_new_open_driver(&bdrv_test, "child-node", BDRV_O_RDWR,
                                    &error_abort);

    for (i = 0; i < 3; i++) {
        char name[32];
        snprintf(name, sizeof(name), "parent-node-%i", i);
        bs_parents[i] = bdrv_new_open_driver(&bdrv_test, name, BDRV_O_RDWR,
                                             &error_abort);
        bdrv_graph_wrlock_drained();
        bdrv_set_backing_hd(bs_parents[i], bs_child, &error_abort);
        bdrv_graph_wrunlock();
    }

    job = block_job_create("job", &test_drop_backing_job_driver, NULL,
                           bs_parents[2], 0, BLK_PERM_ALL, 0, 0, NULL, NULL,
                           &error_abort);
    job->bs = bs_parents[2];

    job->detach_also = bs_parents[0];
    job->did_complete = &job_has_completed;

    job_start(&job->common.job);

    qatomic_set(&job->should_complete, true);
    bdrv_drained_begin(bs_child);
    g_assert(!job_has_completed);
    bdrv_drained_end(bs_child);
    aio_poll(qemu_get_aio_context(), false);
    g_assert(job_has_completed);

    bdrv_unref(bs_parents[0]);
    bdrv_unref(bs_parents[1]);
    bdrv_unref(bs_parents[2]);
    bdrv_unref(bs_child);
}


typedef struct TestSimpleBlockJob {
    BlockJob common;
    bool *did_complete;

    bool should_complete;
} TestSimpleBlockJob;

static int coroutine_fn test_simple_job_run(Job *job, Error **errp)
{
    TestSimpleBlockJob *s = container_of(job, TestSimpleBlockJob, common.job);

    while (!qatomic_read(&s->should_complete)) {
        job_sleep_ns(job, 0);
    }

    return 0;
}

static void test_simple_job_clean(Job *job)
{
    TestSimpleBlockJob *s = container_of(job, TestSimpleBlockJob, common.job);
    *s->did_complete = true;
}

static const BlockJobDriver test_simple_job_driver = {
    .job_driver = {
        .instance_size  = sizeof(TestSimpleBlockJob),
        .free           = block_job_free,
        .user_resume    = block_job_user_resume,
        .run            = test_simple_job_run,
        .clean          = test_simple_job_clean,
    },
};

static int drop_intermediate_poll_update_filename(BdrvChild *child,
                                                  BlockDriverState *new_base,
                                                  const char *filename,
                                                  bool backing_mask_protocol,
                                                  Error **errp)
{

    aio_poll(qemu_get_current_aio_context(), false);
    aio_poll(qemu_get_current_aio_context(), false);

    return 0;
}

static void test_drop_intermediate_poll(void)
{
    static BdrvChildClass chain_child_class;
    BlockDriverState *chain[3];
    TestSimpleBlockJob *job;
    BlockDriverState *job_node;
    bool job_has_completed = false;
    int i;
    int ret;

    chain_child_class = child_of_bds;
    chain_child_class.update_filename = drop_intermediate_poll_update_filename;

    for (i = 0; i < 3; i++) {
        char name[32];
        snprintf(name, 32, "node-%i", i);

        chain[i] = bdrv_new_open_driver(&bdrv_test, name, 0, &error_abort);
    }

    job_node = bdrv_new_open_driver(&bdrv_test, "job-node", BDRV_O_RDWR,
                                    &error_abort);
    bdrv_graph_wrlock_drained();
    bdrv_set_backing_hd(job_node, chain[1], &error_abort);

    for (i = 0; i < 3; i++) {
        if (i) {
            bdrv_attach_child(chain[i], chain[i - 1], "chain",
                              &chain_child_class, BDRV_CHILD_COW, &error_abort);
        }
    }
    bdrv_graph_wrunlock();

    job = block_job_create("job", &test_simple_job_driver, NULL, job_node,
                           0, BLK_PERM_ALL, 0, 0, NULL, NULL, &error_abort);

    bdrv_unref(job_node);

    job->did_complete = &job_has_completed;

    job_start(&job->common.job);
    qatomic_set(&job->should_complete, true);

    g_assert(!job_has_completed);
    ret = bdrv_drop_intermediate(chain[1], chain[0], NULL, false);
    aio_poll(qemu_get_aio_context(), false);
    g_assert(ret == 0);
    g_assert(job_has_completed);

    bdrv_unref(chain[2]);
}


typedef struct BDRVReplaceTestState {
    bool setup_completed;
    bool was_drained;
    bool was_undrained;
    bool has_read;

    int drain_count;

    bool yield_before_read;
    Coroutine *io_co;
    Coroutine *drain_co;
} BDRVReplaceTestState;

static void bdrv_replace_test_close(BlockDriverState *bs)
{
}

static int coroutine_fn GRAPH_RDLOCK
bdrv_replace_test_co_preadv(BlockDriverState *bs, int64_t offset, int64_t bytes,
                            QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    BDRVReplaceTestState *s = bs->opaque;

    if (bs->backing) {
        int ret;

        g_assert(!s->drain_count);

        s->io_co = qemu_coroutine_self();
        if (s->yield_before_read) {
            s->yield_before_read = false;
            qemu_coroutine_yield();
        }
        s->io_co = NULL;

        ret = bdrv_co_preadv(bs->backing, offset, bytes, qiov, 0);
        s->has_read = true;

        if (s->drain_co) {
            aio_co_wake(s->drain_co);
        }

        return ret;
    }

    s->has_read = true;
    return 0;
}

static void coroutine_fn bdrv_replace_test_drain_co(void *opaque)
{
    BlockDriverState *bs = opaque;
    BDRVReplaceTestState *s = bs->opaque;

    while (s->io_co) {
        aio_co_wake(s->io_co);
        s->io_co = NULL;
        qemu_coroutine_yield();
    }
    s->drain_co = NULL;
    bdrv_dec_in_flight(bs);
}

static void bdrv_replace_test_drain_begin(BlockDriverState *bs)
{
    BDRVReplaceTestState *s = bs->opaque;

    if (!s->setup_completed) {
        return;
    }

    if (!s->drain_count) {
        s->drain_co = qemu_coroutine_create(bdrv_replace_test_drain_co, bs);
        bdrv_inc_in_flight(bs);
        aio_co_enter(bdrv_get_aio_context(bs), s->drain_co);
        s->was_drained = true;
    }
    s->drain_count++;
}

static void coroutine_fn bdrv_replace_test_read_entry(void *opaque)
{
    BlockDriverState *bs = opaque;
    char data;
    QEMUIOVector qiov = QEMU_IOVEC_INIT_BUF(qiov, &data, 1);
    int ret;

    bdrv_graph_co_rdlock();
    ret = bdrv_replace_test_co_preadv(bs, 0, 1, &qiov, 0);
    bdrv_graph_co_rdunlock();

    g_assert(ret >= 0);
    bdrv_dec_in_flight(bs);
}

static void bdrv_replace_test_drain_end(BlockDriverState *bs)
{
    BDRVReplaceTestState *s = bs->opaque;

    GRAPH_RDLOCK_GUARD_MAINLOOP();

    if (!s->setup_completed) {
        return;
    }

    g_assert(s->drain_count > 0);
    if (!--s->drain_count) {
        s->was_undrained = true;

        if (bs->backing) {
            Coroutine *co = qemu_coroutine_create(bdrv_replace_test_read_entry,
                                                  bs);
            bdrv_inc_in_flight(bs);
            aio_co_enter(bdrv_get_aio_context(bs), co);
        }
    }
}

static BlockDriver bdrv_replace_test = {
    .format_name            = "replace_test",
    .instance_size          = sizeof(BDRVReplaceTestState),
    .supports_backing       = true,

    .bdrv_close             = bdrv_replace_test_close,
    .bdrv_co_preadv         = bdrv_replace_test_co_preadv,

    .bdrv_drain_begin       = bdrv_replace_test_drain_begin,
    .bdrv_drain_end         = bdrv_replace_test_drain_end,

    .bdrv_child_perm        = bdrv_default_perms,
};

static void coroutine_fn test_replace_child_mid_drain_read_co(void *opaque)
{
    int ret;
    char data;

    ret = blk_co_pread(opaque, 0, 1, &data, 0);
    g_assert(ret >= 0);
}

static void do_test_replace_child_mid_drain(int old_drain_count,
                                            int new_drain_count)
{
    BlockBackend *parent_blk;
    BlockDriverState *parent_bs;
    BlockDriverState *old_child_bs, *new_child_bs;
    BDRVReplaceTestState *parent_s;
    BDRVReplaceTestState *old_child_s, *new_child_s;
    Coroutine *io_co;
    int i;

    parent_bs = bdrv_new_open_driver(&bdrv_replace_test, "parent", 0,
                                     &error_abort);
    parent_s = parent_bs->opaque;

    parent_blk = blk_new(qemu_get_aio_context(),
                         BLK_PERM_CONSISTENT_READ, BLK_PERM_ALL);
    blk_insert_bs(parent_blk, parent_bs, &error_abort);

    old_child_bs = bdrv_new_open_driver(&bdrv_replace_test, "old-child", 0,
                                        &error_abort);
    new_child_bs = bdrv_new_open_driver(&bdrv_replace_test, "new-child", 0,
                                        &error_abort);
    old_child_s = old_child_bs->opaque;
    new_child_s = new_child_bs->opaque;

    parent_bs->total_sectors = 1;
    old_child_bs->total_sectors = 1;
    new_child_bs->total_sectors = 1;

    bdrv_ref(old_child_bs);
    bdrv_graph_wrlock_drained();
    bdrv_attach_child(parent_bs, old_child_bs, "child", &child_of_bds,
                      BDRV_CHILD_COW, &error_abort);
    bdrv_graph_wrunlock();
    parent_s->setup_completed = true;

    for (i = 0; i < old_drain_count; i++) {
        bdrv_drained_begin(old_child_bs);
    }
    for (i = 0; i < new_drain_count; i++) {
        bdrv_drained_begin(new_child_bs);
    }

    if (!old_drain_count) {
        parent_s->yield_before_read = true;
        io_co = qemu_coroutine_create(test_replace_child_mid_drain_read_co,
                                      parent_blk);
        qemu_coroutine_enter(io_co);
    }

    g_assert(!parent_s->has_read);

    parent_s->was_drained = false;
    parent_s->was_undrained = false;

    g_assert(parent_bs->quiesce_counter == old_drain_count);
    bdrv_drained_begin(old_child_bs);
    bdrv_drained_begin(new_child_bs);
    bdrv_graph_wrlock();
    bdrv_replace_node(old_child_bs, new_child_bs, &error_abort);
    bdrv_graph_wrunlock();
    bdrv_drained_end(new_child_bs);
    bdrv_drained_end(old_child_bs);
    g_assert(parent_bs->quiesce_counter == new_drain_count);

    if (!old_drain_count && !new_drain_count) {
        g_assert(parent_s->was_drained && parent_s->was_undrained);
    } else if (!old_drain_count && new_drain_count) {
        g_assert(parent_s->was_drained && !parent_s->was_undrained);
    } else if (old_drain_count && !new_drain_count) {
        g_assert(!parent_s->was_drained && parent_s->was_undrained);
    } else {
        g_assert(!parent_s->was_drained && !parent_s->was_undrained);
    }

    if (!old_drain_count || !new_drain_count) {
        g_assert(parent_s->has_read);
    } else {
        g_assert(!parent_s->has_read);
    }

    g_assert(!(old_drain_count && old_child_s->has_read));
    g_assert(!(new_drain_count && new_child_s->has_read));

    for (i = 0; i < new_drain_count; i++) {
        bdrv_drained_end(new_child_bs);
    }
    for (i = 0; i < old_drain_count; i++) {
        bdrv_drained_end(old_child_bs);
    }

    g_assert(parent_s->has_read);
    g_assert(new_child_s->has_read);

    blk_unref(parent_blk);
    bdrv_unref(parent_bs);
    bdrv_unref(old_child_bs);
    bdrv_unref(new_child_bs);
}

static void test_replace_child_mid_drain(void)
{
    int old_drain_count, new_drain_count;

    for (old_drain_count = 0; old_drain_count < 2; old_drain_count++) {
        for (new_drain_count = 0; new_drain_count < 2; new_drain_count++) {
            do_test_replace_child_mid_drain(old_drain_count, new_drain_count);
        }
    }
}

int main(int argc, char **argv)
{
    int ret;

    bdrv_init();
    qemu_init_main_loop(&error_abort);

    g_test_init(&argc, &argv, NULL);
    qemu_event_init(&done_event, false);

    g_test_add_func("/bdrv-drain/driver-cb/drain_all", test_drv_cb_drain_all);
    g_test_add_func("/bdrv-drain/driver-cb/drain", test_drv_cb_drain);

    g_test_add_func("/bdrv-drain/driver-cb/co/drain_all",
                    test_drv_cb_co_drain_all);
    g_test_add_func("/bdrv-drain/driver-cb/co/drain", test_drv_cb_co_drain);

    g_test_add_func("/bdrv-drain/quiesce/drain_all", test_quiesce_drain_all);
    g_test_add_func("/bdrv-drain/quiesce/drain", test_quiesce_drain);

    g_test_add_func("/bdrv-drain/quiesce/co/drain_all",
                    test_quiesce_co_drain_all);
    g_test_add_func("/bdrv-drain/quiesce/co/drain", test_quiesce_co_drain);

    g_test_add_func("/bdrv-drain/nested", test_nested);

    g_test_add_func("/bdrv-drain/graph-change/drain_all",
                    test_graph_change_drain_all);

    g_test_add_func("/bdrv-drain/iothread/drain_all", test_iothread_drain_all);
    g_test_add_func("/bdrv-drain/iothread/drain", test_iothread_drain);

    g_test_add_func("/bdrv-drain/blockjob/drain_all", test_blockjob_drain_all);
    g_test_add_func("/bdrv-drain/blockjob/drain", test_blockjob_drain);

    g_test_add_func("/bdrv-drain/blockjob/error/drain_all",
                    test_blockjob_error_drain_all);
    g_test_add_func("/bdrv-drain/blockjob/error/drain",
                    test_blockjob_error_drain);

    g_test_add_func("/bdrv-drain/blockjob/iothread/drain_all",
                    test_blockjob_iothread_drain_all);
    g_test_add_func("/bdrv-drain/blockjob/iothread/drain",
                    test_blockjob_iothread_drain);

    g_test_add_func("/bdrv-drain/blockjob/iothread/error/drain_all",
                    test_blockjob_iothread_error_drain_all);
    g_test_add_func("/bdrv-drain/blockjob/iothread/error/drain",
                    test_blockjob_iothread_error_drain);

    g_test_add_func("/bdrv-drain/deletion/drain", test_delete_by_drain);
    g_test_add_func("/bdrv-drain/detach/drain_all", test_detach_by_drain_all);
    g_test_add_func("/bdrv-drain/detach/drain", test_detach_by_drain);
    g_test_add_func("/bdrv-drain/detach/parent_cb", test_detach_by_parent_cb);
    g_test_add_func("/bdrv-drain/detach/driver_cb", test_detach_by_driver_cb);

    g_test_add_func("/bdrv-drain/attach/drain", test_append_to_drained);

    g_test_add_func("/bdrv-drain/set_aio_context", test_set_aio_context);

    g_test_add_func("/bdrv-drain/blockjob/commit_by_drained_end",
                    test_blockjob_commit_by_drained_end);

    g_test_add_func("/bdrv-drain/bdrv_drop_intermediate/poll",
                    test_drop_intermediate_poll);

    g_test_add_func("/bdrv-drain/replace_child/mid-drain",
                    test_replace_child_mid_drain);

    ret = g_test_run();
    qemu_event_destroy(&done_event);
    return ret;
}
