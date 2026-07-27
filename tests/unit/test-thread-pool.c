#include "qemu/osdep.h"
#include "block/aio.h"
#include "block/thread-pool.h"
#include "block/block.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"

static AioContext *ctx;
static int active;

typedef struct {
    BlockAIOCB *aiocb;
    int n;
    int ret;
} WorkerTestData;

static int worker_cb(void *opaque)
{
    WorkerTestData *data = opaque;
    return qatomic_fetch_inc(&data->n);
}

static int long_cb(void *opaque)
{
    WorkerTestData *data = opaque;
    if (qatomic_cmpxchg(&data->n, 0, 1) == 0) {
        g_usleep(2000000);
        qatomic_or(&data->n, 2);
    }
    return 0;
}

static void done_cb(void *opaque, int ret)
{
    WorkerTestData *data = opaque;
    g_assert(data->ret == -EINPROGRESS || data->ret == -ECANCELED);
    data->ret = ret;
    data->aiocb = NULL;

    active--;
}

static void test_submit_no_complete(void)
{
    WorkerTestData data = { .n = 0 };
    thread_pool_submit_aio(worker_cb, &data, NULL, NULL);
    while (data.n == 0) {
        aio_poll(ctx, true);
    }
    g_assert_cmpint(data.n, ==, 1);
}

static void test_submit_aio(void)
{
    WorkerTestData data = { .n = 0, .ret = -EINPROGRESS };
    data.aiocb = thread_pool_submit_aio(worker_cb, &data,
                                        done_cb, &data);

    active = 1;
    g_assert_cmpint(data.ret, ==, -EINPROGRESS);
    while (data.ret == -EINPROGRESS) {
        aio_poll(ctx, true);
    }
    g_assert_cmpint(active, ==, 0);
    g_assert_cmpint(data.n, ==, 1);
    g_assert_cmpint(data.ret, ==, 0);
}

static void coroutine_fn co_test_cb(void *opaque)
{
    WorkerTestData *data = opaque;

    active = 1;
    data->n = 0;
    data->ret = -EINPROGRESS;
    thread_pool_submit_co(worker_cb, data);

    g_assert_cmpint(data->n, ==, 1);
    data->ret = 0;
    active--;

}

static void test_submit_co(void)
{
    WorkerTestData data;
    Coroutine *co = qemu_coroutine_create(co_test_cb, &data);

    qemu_coroutine_enter(co);

    g_assert_cmpint(active, ==, 1);
    g_assert_cmpint(data.ret, ==, -EINPROGRESS);

    while (data.ret == -EINPROGRESS) {
        aio_poll(ctx, true);
    }

    g_assert_cmpint(active, ==, 0);
    g_assert_cmpint(data.ret, ==, 0);
}

static void test_submit_many(void)
{
    WorkerTestData data[100];
    int i;

    for (i = 0; i < 100; i++) {
        data[i].n = 0;
        data[i].ret = -EINPROGRESS;
        thread_pool_submit_aio(worker_cb, &data[i], done_cb, &data[i]);
    }

    active = 100;
    while (active > 0) {
        aio_poll(ctx, true);
    }
    for (i = 0; i < 100; i++) {
        g_assert_cmpint(data[i].n, ==, 1);
        g_assert_cmpint(data[i].ret, ==, 0);
    }
}

static void do_test_cancel(bool sync)
{
    WorkerTestData data[100];
    int num_canceled;
    int i;

    test_submit_many();

    for (i = 0; i < 100; i++) {
        data[i].n = 0;
        data[i].ret = -EINPROGRESS;
        data[i].aiocb = thread_pool_submit_aio(long_cb, &data[i],
                                               done_cb, &data[i]);
    }

    active = 100;
    aio_notify(ctx);
    aio_poll(ctx, false);

    g_assert_cmpint(active, ==, 100);
    g_usleep(1000000);
    g_assert_cmpint(active, >, 50);

    num_canceled = 0;
    for (i = 0; i < 100; i++) {
        if (qatomic_cmpxchg(&data[i].n, 0, 4) == 0) {
            data[i].ret = -ECANCELED;
            if (sync) {
                bdrv_aio_cancel(data[i].aiocb);
            } else {
                bdrv_aio_cancel_async(data[i].aiocb);
            }
            num_canceled++;
        }
    }
    g_assert_cmpint(active, >, 0);
    g_assert_cmpint(num_canceled, <, 100);

    for (i = 0; i < 100; i++) {
        if (data[i].aiocb && qatomic_read(&data[i].n) < 4) {
            if (sync) {
                bdrv_aio_cancel(data[i].aiocb);
            } else {
                bdrv_aio_cancel_async(data[i].aiocb);
            }
        }
    }

    while (active > 0) {
        aio_poll(ctx, true);
    }
    g_assert_cmpint(active, ==, 0);
    for (i = 0; i < 100; i++) {
        g_assert(data[i].aiocb == NULL);
        switch (data[i].n) {
        case 0:
            fprintf(stderr, "Callback not canceled but never started?\n");
            abort();
        case 3:
            g_assert_cmpint(data[i].ret, ==, 0);
            break;
        case 4:
            g_assert_cmpint(data[i].ret, ==, -ECANCELED);
            break;
        default:
            fprintf(stderr, "Callback aborted while running?\n");
            abort();
        }
    }
}

static void test_cancel(void)
{
    do_test_cancel(true);
}

static void test_cancel_async(void)
{
    do_test_cancel(false);
}

int main(int argc, char **argv)
{
    qemu_init_main_loop(&error_abort);
    ctx = qemu_get_current_aio_context();

    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/thread-pool/submit-no-complete", test_submit_no_complete);
    g_test_add_func("/thread-pool/submit-aio", test_submit_aio);
    g_test_add_func("/thread-pool/submit-co", test_submit_co);
    g_test_add_func("/thread-pool/submit-many", test_submit_many);
    g_test_add_func("/thread-pool/cancel", test_cancel);
    g_test_add_func("/thread-pool/cancel-async", test_cancel_async);

    return g_test_run();
}
