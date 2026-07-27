/*
 * QEMU aio implementation
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_AIO_H
#define QEMU_AIO_H

#ifdef CONFIG_LINUX_IO_URING
#include <liburing.h>
#endif
#include "qemu/coroutine-core.h"
#include "qemu/queue.h"
#include "qemu/event_notifier.h"
#include "qemu/lockcnt.h"
#include "qemu/thread.h"
#include "qemu/timer.h"
#include "block/graph-lock.h"
#include "hw/qdev-core.h"


typedef struct BlockAIOCB BlockAIOCB;
typedef void BlockCompletionFunc(void *opaque, int ret);

typedef struct AIOCBInfo {
    void (*cancel_async)(BlockAIOCB *acb);
    size_t aiocb_size;
} AIOCBInfo;

struct BlockAIOCB {
    const AIOCBInfo *aiocb_info;
    BlockDriverState *bs;
    BlockCompletionFunc *cb;
    void *opaque;
    int refcnt;
};

void *qemu_aio_get(const AIOCBInfo *aiocb_info, BlockDriverState *bs,
                   BlockCompletionFunc *cb, void *opaque);
void qemu_aio_unref(void *p);
void qemu_aio_ref(void *p);

typedef struct AioHandler AioHandler;
typedef QLIST_HEAD(, AioHandler) AioHandlerList;
typedef void QEMUBHFunc(void *opaque);
typedef bool AioPollFn(void *opaque);
typedef void IOHandler(void *opaque);

struct ThreadPoolAio;
struct LinuxAioState;
typedef struct LuringState LuringState;

bool aio_poll_disabled(AioContext *ctx);

#ifdef CONFIG_LINUX_IO_URING
typedef struct CqeHandler CqeHandler;
struct CqeHandler {
    void (*cb)(CqeHandler *handler);

    QSIMPLEQ_ENTRY(CqeHandler) next;

    struct io_uring_cqe cqe;
};

typedef QSIMPLEQ_HEAD(, CqeHandler) CqeHandlerSimpleQ;
#endif

typedef struct {
    void (*update)(AioContext *ctx, AioHandler *old_node, AioHandler *new_node);

    int (*wait)(AioContext *ctx, AioHandlerList *ready_list, int64_t timeout);

    bool (*need_wait)(AioContext *ctx);

    bool (*dispatch)(AioContext *ctx);

    void (*gsource_prepare)(AioContext *ctx);

    bool (*gsource_check)(AioContext *ctx);

    void (*gsource_dispatch)(AioContext *ctx, AioHandlerList *ready_list);

#ifdef CONFIG_LINUX_IO_URING
    void (*add_sqe)(AioContext *ctx,
                    void (*prep_sqe)(struct io_uring_sqe *sqe, void *opaque),
                    void *opaque, CqeHandler *cqe_handler);
#endif
} FDMonOps;

typedef QSLIST_HEAD(, QEMUBH) BHList;
typedef struct BHListSlice BHListSlice;
struct BHListSlice {
    BHList bh_list;
    QSIMPLEQ_ENTRY(BHListSlice) next;
};

typedef QSLIST_HEAD(, AioHandler) AioHandlerSList;

typedef struct AioPolledEvent {
    int64_t ns;
} AioPolledEvent;

struct AioContext {
    GSource source;

    QemuRecMutex lock;

    BdrvGraphRWlock *bdrv_graph;

    AioHandlerList aio_handlers;

    AioHandlerList deleted_aio_handlers;

    uint32_t notify_me;

    QemuLockCnt list_lock;

    BHList bh_list;

    QSIMPLEQ_HEAD(, BHListSlice) bh_slice_list;

    bool notified;
    EventNotifier notifier;

    QSLIST_HEAD(, Coroutine) scheduled_coroutines;
    QEMUBH *co_schedule_bh;

    int thread_pool_min;
    int thread_pool_max;
    struct ThreadPoolAio *thread_pool;

#ifdef CONFIG_LINUX_AIO
    struct LinuxAioState *linux_aio;
#endif
#ifdef CONFIG_LINUX_IO_URING
    struct io_uring fdmon_io_uring;
    AioHandlerSList submit_list;
    void *io_uring_fd_tag;

    CqeHandlerSimpleQ cqe_handler_ready_list;
#endif

    QEMUTimerListGroup tlg;

    int poll_disable_cnt;

    int64_t poll_max_ns;
    int64_t poll_grow;
    int64_t poll_shrink;

    int64_t aio_max_batch;

    AioHandlerList poll_aio_handlers;

    bool poll_started;

    int epollfd;

    void *epollfd_tag;

    const FDMonOps *fdmon_ops;

    bool initialized;
};

AioContext *aio_context_new(Error **errp);

void aio_context_ref(AioContext *ctx);

void aio_context_unref(AioContext *ctx);

void aio_bh_schedule_oneshot_full(AioContext *ctx, QEMUBHFunc *cb, void *opaque,
                                  const char *name);

#define aio_bh_schedule_oneshot(ctx, cb, opaque) \
    aio_bh_schedule_oneshot_full((ctx), (cb), (opaque), (stringify(cb)))

QEMUBH *aio_bh_new_full(AioContext *ctx, QEMUBHFunc *cb, void *opaque,
                        const char *name, MemReentrancyGuard *reentrancy_guard);

#define aio_bh_new(ctx, cb, opaque) \
    aio_bh_new_full((ctx), (cb), (opaque), (stringify(cb)), NULL)

#define aio_bh_new_guarded(ctx, cb, opaque, guard) \
    aio_bh_new_full((ctx), (cb), (opaque), (stringify(cb)), guard)

void aio_notify(AioContext *ctx);

void aio_notify_accept(AioContext *ctx);

void aio_bh_call(QEMUBH *bh);

int aio_bh_poll(AioContext *ctx);

void qemu_bh_schedule(QEMUBH *bh);

void qemu_bh_cancel(QEMUBH *bh);

void qemu_bh_delete(QEMUBH *bh);

bool aio_prepare(AioContext *ctx);

bool aio_pending(AioContext *ctx);

void aio_dispatch(AioContext *ctx);

bool no_coroutine_fn aio_poll(AioContext *ctx, bool blocking);

void aio_set_fd_handler(AioContext *ctx,
                        int fd,
                        IOHandler *io_read,
                        IOHandler *io_write,
                        AioPollFn *io_poll,
                        IOHandler *io_poll_ready,
                        void *opaque);

void aio_set_event_notifier(AioContext *ctx,
                            EventNotifier *notifier,
                            EventNotifierHandler *io_read,
                            AioPollFn *io_poll,
                            EventNotifierHandler *io_poll_ready);

void aio_set_event_notifier_poll(AioContext *ctx,
                                 EventNotifier *notifier,
                                 EventNotifierHandler *io_poll_begin,
                                 EventNotifierHandler *io_poll_end);

GSource *aio_get_g_source(AioContext *ctx);

struct ThreadPoolAio *aio_get_thread_pool(AioContext *ctx);

struct LinuxAioState *aio_setup_linux_aio(AioContext *ctx, Error **errp);

struct LinuxAioState *aio_get_linux_aio(AioContext *ctx);

static inline QEMUTimer *aio_timer_new_with_attrs(AioContext *ctx,
                                                  QEMUClockType type,
                                                  int scale, int attributes,
                                                  QEMUTimerCB *cb, void *opaque)
{
    return timer_new_full(&ctx->tlg, type, scale, attributes, cb, opaque);
}

static inline QEMUTimer *aio_timer_new(AioContext *ctx, QEMUClockType type,
                                       int scale,
                                       QEMUTimerCB *cb, void *opaque)
{
    return timer_new_full(&ctx->tlg, type, scale, 0, cb, opaque);
}

static inline void aio_timer_init_with_attrs(AioContext *ctx,
                                             QEMUTimer *ts, QEMUClockType type,
                                             int scale, int attributes,
                                             QEMUTimerCB *cb, void *opaque)
{
    timer_init_full(ts, &ctx->tlg, type, scale, attributes, cb, opaque);
}

static inline void aio_timer_init(AioContext *ctx,
                                  QEMUTimer *ts, QEMUClockType type,
                                  int scale,
                                  QEMUTimerCB *cb, void *opaque)
{
    timer_init_full(ts, &ctx->tlg, type, scale, 0, cb, opaque);
}

int64_t aio_compute_timeout(AioContext *ctx);

void aio_co_schedule(AioContext *ctx, Coroutine *co);

void coroutine_fn aio_co_reschedule_self(AioContext *new_ctx);

void aio_co_wake(Coroutine *co);

void aio_co_enter(AioContext *ctx, Coroutine *co);

AioContext *qemu_get_current_aio_context(void);

void qemu_set_current_aio_context(AioContext *ctx);

bool aio_context_setup(AioContext *ctx, Error **errp);

void aio_context_destroy(AioContext *ctx);

void aio_context_set_poll_params(AioContext *ctx, int64_t max_ns,
                                 int64_t grow, int64_t shrink,
                                 Error **errp);

void aio_context_set_aio_params(AioContext *ctx, int64_t max_batch);

void aio_context_set_thread_pool_params(AioContext *ctx, int64_t min,
                                        int64_t max, Error **errp);

#ifdef CONFIG_LINUX_IO_URING
static inline bool aio_has_io_uring(void)
{
    AioContext *ctx = qemu_get_current_aio_context();
    return ctx->fdmon_ops->add_sqe;
}

void aio_add_sqe(void (*prep_sqe)(struct io_uring_sqe *sqe, void *opaque),
                 void *opaque, CqeHandler *cqe_handler);
#endif

#endif
