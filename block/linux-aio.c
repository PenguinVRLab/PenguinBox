/*
 * Linux native AIO support.
 *
 * Copyright (C) 2009 IBM, Corp.
 * Copyright (C) 2009 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "block/aio.h"
#include "qemu/queue.h"
#include "block/block.h"
#include "block/raw-aio.h"
#include "qemu/event_notifier.h"
#include "qemu/coroutine.h"
#include "qemu/defer-call.h"
#include "qapi/error.h"
#include "system/block-backend.h"

#include "qemu/coroutine_int.h"

#include <libaio.h>

#define MAX_EVENTS 1024

#define DEFAULT_MAX_BATCH 32

struct qemu_laiocb {
    Coroutine *co;
    LinuxAioState *ctx;
    struct iocb iocb;
    ssize_t ret;
    size_t nbytes;
    QEMUIOVector *qiov;
    bool is_read;
    QSIMPLEQ_ENTRY(qemu_laiocb) next;
};

typedef struct {
    unsigned int in_queue;
    unsigned int in_flight;
    bool blocked;
    QSIMPLEQ_HEAD(, qemu_laiocb) pending;
} LaioQueue;

struct LinuxAioState {
    AioContext *aio_context;

    io_context_t ctx;
    EventNotifier e;

    LaioQueue io_q;
    QEMUBH *completion_bh;
    int event_idx;
    int event_max;
};

static void ioq_submit(LinuxAioState *s);

static inline ssize_t io_event_ret(struct io_event *ev)
{
    return (ssize_t)(((uint64_t)ev->res2 << 32) | ev->res);
}

static void qemu_laio_process_completion(struct qemu_laiocb *laiocb)
{
    int ret;

    ret = laiocb->ret;
    if (ret != -ECANCELED) {
        if (ret == laiocb->nbytes) {
            ret = 0;
        } else if (ret >= 0) {
            if (laiocb->is_read) {
                qemu_iovec_memset(laiocb->qiov, ret, 0,
                    laiocb->qiov->size - ret);
            } else {
                ret = -ENOSPC;
            }
        }
    }

    laiocb->ret = ret;

    assert(laiocb->co->ctx == laiocb->ctx->aio_context);
    if (!qemu_coroutine_entered(laiocb->co)) {
        aio_co_wake(laiocb->co);
    }
}

/**
 * aio_ring buffer which is shared between userspace and kernel.
 *
 * This copied from linux/fs/aio.c, common header does not exist
 * but AIO exists for ages so we assume ABI is stable.
 */
struct aio_ring {
    unsigned    id;
    unsigned    nr;
    unsigned    head;
    unsigned    tail;

    unsigned    magic;
    unsigned    compat_features;
    unsigned    incompat_features;
    unsigned    header_length;

    struct io_event io_events[];
};

static inline unsigned int io_getevents_peek(io_context_t ctx,
                                             struct io_event **events)
{
    struct aio_ring *ring = (struct aio_ring *)ctx;
    unsigned int head = ring->head, tail = ring->tail;
    unsigned int nr;

    nr = tail >= head ? tail - head : ring->nr - head;
    *events = ring->io_events + head;
    smp_rmb();

    return nr;
}

static inline void io_getevents_commit(io_context_t ctx, unsigned int nr)
{
    struct aio_ring *ring = (struct aio_ring *)ctx;

    if (nr) {
        ring->head = (ring->head + nr) % ring->nr;
    }
}

static inline unsigned int
io_getevents_advance_and_peek(io_context_t ctx,
                              struct io_event **events,
                              unsigned int nr)
{
    io_getevents_commit(ctx, nr);
    return io_getevents_peek(ctx, events);
}

static void qemu_laio_process_completions(LinuxAioState *s)
{
    struct io_event *events;

    defer_call_begin();

    qemu_bh_schedule(s->completion_bh);

    while ((s->event_max = io_getevents_advance_and_peek(s->ctx, &events,
                                                         s->event_idx))) {
        for (s->event_idx = 0; s->event_idx < s->event_max; ) {
            struct iocb *iocb = events[s->event_idx].obj;
            struct qemu_laiocb *laiocb =
                container_of(iocb, struct qemu_laiocb, iocb);

            laiocb->ret = io_event_ret(&events[s->event_idx]);

            s->io_q.in_flight--;
            s->event_idx++;
            qemu_laio_process_completion(laiocb);
        }
    }

    qemu_bh_cancel(s->completion_bh);

    s->event_max = 0;
    s->event_idx = 0;

    defer_call_end();
}

static void qemu_laio_process_completions_and_submit(LinuxAioState *s)
{
    qemu_laio_process_completions(s);

    if (!QSIMPLEQ_EMPTY(&s->io_q.pending)) {
        ioq_submit(s);
    }
}

static void qemu_laio_completion_bh(void *opaque)
{
    LinuxAioState *s = opaque;

    qemu_laio_process_completions_and_submit(s);
}

static void qemu_laio_completion_cb(EventNotifier *e)
{
    LinuxAioState *s = container_of(e, LinuxAioState, e);

    if (event_notifier_test_and_clear(&s->e)) {
        qemu_laio_process_completions_and_submit(s);
    }
}

static bool qemu_laio_poll_cb(void *opaque)
{
    EventNotifier *e = opaque;
    LinuxAioState *s = container_of(e, LinuxAioState, e);
    struct io_event *events;

    return io_getevents_peek(s->ctx, &events);
}

static void qemu_laio_poll_ready(EventNotifier *opaque)
{
    EventNotifier *e = opaque;
    LinuxAioState *s = container_of(e, LinuxAioState, e);

    qemu_laio_process_completions_and_submit(s);
}

static void ioq_init(LaioQueue *io_q)
{
    QSIMPLEQ_INIT(&io_q->pending);
    io_q->in_queue = 0;
    io_q->in_flight = 0;
    io_q->blocked = false;
}

static void ioq_submit(LinuxAioState *s)
{
    int ret, len;
    struct qemu_laiocb *aiocb;
    QEMU_UNINITIALIZED struct iocb *iocbs[MAX_EVENTS];
    QSIMPLEQ_HEAD(, qemu_laiocb) completed;

    do {
        if (s->io_q.in_flight >= MAX_EVENTS) {
            break;
        }
        len = 0;
        QSIMPLEQ_FOREACH(aiocb, &s->io_q.pending, next) {
            iocbs[len++] = &aiocb->iocb;
            if (s->io_q.in_flight + len >= MAX_EVENTS) {
                break;
            }
        }

        ret = io_submit(s->ctx, len, iocbs);
        if (ret == -EAGAIN) {
            break;
        }
        if (ret < 0) {
            aiocb = QSIMPLEQ_FIRST(&s->io_q.pending);
            QSIMPLEQ_REMOVE_HEAD(&s->io_q.pending, next);
            s->io_q.in_queue--;
            aiocb->ret = ret;
            qemu_laio_process_completion(aiocb);
            continue;
        }

        s->io_q.in_flight += ret;
        s->io_q.in_queue  -= ret;
        aiocb = container_of(iocbs[ret - 1], struct qemu_laiocb, iocb);
        QSIMPLEQ_SPLIT_AFTER(&s->io_q.pending, aiocb, next, &completed);
    } while (ret == len && !QSIMPLEQ_EMPTY(&s->io_q.pending));
    s->io_q.blocked = (s->io_q.in_queue > 0);

    if (s->io_q.in_flight) {
        qemu_laio_process_completions(s);
    }
}

static uint64_t laio_max_batch(LinuxAioState *s, uint64_t dev_max_batch)
{
    uint64_t max_batch = s->aio_context->aio_max_batch ?: DEFAULT_MAX_BATCH;

    max_batch = MIN_NON_ZERO(dev_max_batch, max_batch);

    max_batch = MIN_NON_ZERO(MAX_EVENTS - s->io_q.in_flight, max_batch);

    return max_batch;
}

static void laio_deferred_fn(void *opaque)
{
    LinuxAioState *s = opaque;

    if (!s->io_q.blocked && !QSIMPLEQ_EMPTY(&s->io_q.pending)) {
        ioq_submit(s);
    }
}

static int laio_do_submit(int fd, struct qemu_laiocb *laiocb, off_t offset,
                          int type, BdrvRequestFlags flags,
                          uint64_t dev_max_batch)
{
    LinuxAioState *s = laiocb->ctx;
    struct iocb *iocbs = &laiocb->iocb;
    QEMUIOVector *qiov = laiocb->qiov;

    switch (type) {
    case QEMU_AIO_WRITE:
#ifdef HAVE_IO_PREP_PWRITEV2
    {
        int laio_flags = (flags & BDRV_REQ_FUA) ? RWF_DSYNC : 0;
        io_prep_pwritev2(iocbs, fd, qiov->iov, qiov->niov, offset, laio_flags);
    }
#else
        assert(flags == 0);
        io_prep_pwritev(iocbs, fd, qiov->iov, qiov->niov, offset);
#endif
        break;
    case QEMU_AIO_ZONE_APPEND:
        io_prep_pwritev(iocbs, fd, qiov->iov, qiov->niov, offset);
        break;
    case QEMU_AIO_READ:
        io_prep_preadv(iocbs, fd, qiov->iov, qiov->niov, offset);
        break;
    case QEMU_AIO_FLUSH:
        io_prep_fdsync(iocbs, fd);
        break;
    default:
        fprintf(stderr, "%s: invalid AIO request type 0x%x.\n",
                        __func__, type);
        return -EIO;
    }
    io_set_eventfd(&laiocb->iocb, event_notifier_get_fd(&s->e));

    QSIMPLEQ_INSERT_TAIL(&s->io_q.pending, laiocb, next);
    s->io_q.in_queue++;
    if (!s->io_q.blocked) {
        if (s->io_q.in_queue >= laio_max_batch(s, dev_max_batch)) {
            ioq_submit(s);
        } else {
            defer_call(laio_deferred_fn, s);
        }
    }

    return 0;
}

int coroutine_fn laio_co_submit(int fd, uint64_t offset, QEMUIOVector *qiov,
                                int type, BdrvRequestFlags flags,
                                uint64_t dev_max_batch)
{
    int ret;
    AioContext *ctx = qemu_get_current_aio_context();
    struct qemu_laiocb laiocb = {
        .co         = qemu_coroutine_self(),
        .nbytes     = qiov ? qiov->size : 0,
        .ctx        = aio_get_linux_aio(ctx),
        .ret        = -EINPROGRESS,
        .is_read    = (type == QEMU_AIO_READ),
        .qiov       = qiov,
    };

    ret = laio_do_submit(fd, &laiocb, offset, type, flags, dev_max_batch);
    if (ret < 0) {
        return ret;
    }

    if (laiocb.ret == -EINPROGRESS) {
        qemu_coroutine_yield();
    }
    return laiocb.ret;
}

void laio_detach_aio_context(LinuxAioState *s, AioContext *old_context)
{
    aio_set_event_notifier(old_context, &s->e, NULL, NULL, NULL);
    qemu_bh_delete(s->completion_bh);
    s->aio_context = NULL;
}

void laio_attach_aio_context(LinuxAioState *s, AioContext *new_context)
{
    s->aio_context = new_context;
    s->completion_bh = aio_bh_new(new_context, qemu_laio_completion_bh, s);
    aio_set_event_notifier(new_context, &s->e,
                           qemu_laio_completion_cb,
                           qemu_laio_poll_cb,
                           qemu_laio_poll_ready);
}

LinuxAioState *laio_init(Error **errp)
{
    int rc;
    LinuxAioState *s;

    s = g_malloc0(sizeof(*s));
    rc = event_notifier_init(&s->e, false);
    if (rc < 0) {
        error_setg_errno(errp, -rc, "failed to initialize event notifier");
        goto out_free_state;
    }

    rc = io_setup(MAX_EVENTS, &s->ctx);
    if (rc < 0) {
        error_setg_errno(errp, -rc, "failed to create linux AIO context");
        goto out_close_efd;
    }

    ioq_init(&s->io_q);

    return s;

out_close_efd:
    event_notifier_cleanup(&s->e);
out_free_state:
    g_free(s);
    return NULL;
}

void laio_cleanup(LinuxAioState *s)
{
    event_notifier_cleanup(&s->e);

    if (io_destroy(s->ctx) != 0) {
        fprintf(stderr, "%s: destroy AIO context %p failed\n",
                        __func__, &s->ctx);
    }
    g_free(s);
}

bool laio_has_fdsync(int fd)
{
    struct iocb cb;
    struct iocb *cbs[] = {&cb, NULL};

    io_context_t ctx = 0;
    io_setup(1, &ctx);

    io_prep_fdsync(&cb, fd);
    int ret = io_submit(ctx, 1, cbs);

    io_destroy(ctx);
    return (ret == -EINVAL) ? false : true;
}

bool laio_has_fua(void)
{
#ifdef HAVE_IO_PREP_PWRITEV2
    return true;
#else
    return false;
#endif
}
