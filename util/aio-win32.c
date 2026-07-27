/*
 * QEMU aio implementation
 *
 * Copyright IBM Corp., 2008
 * Copyright Red Hat Inc., 2012
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Paolo Bonzini     <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "block/block.h"
#include "qemu/main-loop.h"
#include "qemu/lockcnt.h"
#include "qemu/queue.h"
#include "qemu/sockets.h"
#include "qapi/error.h"
#include "qemu/rcu_queue.h"
#include "qemu/error-report.h"

struct AioHandler {
    EventNotifier *e;
    IOHandler *io_read;
    IOHandler *io_write;
    EventNotifierHandler *io_notify;
    GPollFD pfd;
    int deleted;
    void *opaque;
    QLIST_ENTRY(AioHandler) node;
};

static void aio_remove_fd_handler(AioContext *ctx, AioHandler *node)
{
    if (!g_source_is_destroyed(&ctx->source)) {
        g_source_remove_poll(&ctx->source, &node->pfd);
    }

    if (qemu_lockcnt_count(&ctx->list_lock)) {
        node->deleted = 1;
        node->pfd.revents = 0;
    } else {
        QLIST_REMOVE(node, node);
        g_free(node);
    }
}

void aio_set_fd_handler(AioContext *ctx,
                        int fd,
                        IOHandler *io_read,
                        IOHandler *io_write,
                        AioPollFn *io_poll,
                        IOHandler *io_poll_ready,
                        void *opaque)
{
    AioHandler *old_node;
    AioHandler *node = NULL;
    SOCKET s;

    if (!fd_is_socket(fd)) {
        error_report("fd=%d is not a socket, AIO implementation is missing", fd);
        return;
    }

    s = _get_osfhandle(fd);

    qemu_lockcnt_lock(&ctx->list_lock);
    QLIST_FOREACH(old_node, &ctx->aio_handlers, node) {
        if (old_node->pfd.fd == s && !old_node->deleted) {
            break;
        }
    }

    if (io_read || io_write) {
        HANDLE event;
        long bitmask = 0;

        node = g_new0(AioHandler, 1);
        node->pfd.fd = s;

        node->pfd.events = 0;
        if (node->io_read) {
            node->pfd.events |= G_IO_IN;
        }
        if (node->io_write) {
            node->pfd.events |= G_IO_OUT;
        }

        node->e = &ctx->notifier;

        node->opaque = opaque;
        node->io_read = io_read;
        node->io_write = io_write;

        if (io_read) {
            bitmask |= FD_READ | FD_ACCEPT | FD_CLOSE;
        }

        if (io_write) {
            bitmask |= FD_WRITE | FD_CONNECT;
        }

        QLIST_INSERT_HEAD_RCU(&ctx->aio_handlers, node, node);
        event = event_notifier_get_handle(&ctx->notifier);
        qemu_socket_select_nofail(fd, event, bitmask);
    }
    if (old_node) {
        aio_remove_fd_handler(ctx, old_node);
    }

    qemu_lockcnt_unlock(&ctx->list_lock);
    aio_notify(ctx);
}

void aio_set_event_notifier(AioContext *ctx,
                            EventNotifier *e,
                            EventNotifierHandler *io_notify,
                            AioPollFn *io_poll,
                            EventNotifierHandler *io_poll_ready)
{
    AioHandler *node;

    qemu_lockcnt_lock(&ctx->list_lock);
    QLIST_FOREACH(node, &ctx->aio_handlers, node) {
        if (node->e == e && !node->deleted) {
            break;
        }
    }

    if (!io_notify) {
        if (node) {
            aio_remove_fd_handler(ctx, node);
        }
    } else {
        if (node == NULL) {
            node = g_new0(AioHandler, 1);
            node->e = e;
            node->pfd.fd = (uintptr_t)event_notifier_get_handle(e);
            node->pfd.events = G_IO_IN;
            QLIST_INSERT_HEAD_RCU(&ctx->aio_handlers, node, node);

            g_source_add_poll(&ctx->source, &node->pfd);
        }
        node->io_notify = io_notify;
    }

    qemu_lockcnt_unlock(&ctx->list_lock);
    aio_notify(ctx);
}

void aio_set_event_notifier_poll(AioContext *ctx,
                                 EventNotifier *notifier,
                                 EventNotifierHandler *io_poll_begin,
                                 EventNotifierHandler *io_poll_end)
{
}

bool aio_prepare(AioContext *ctx)
{
    static struct timeval tv0;
    AioHandler *node;
    bool have_select_revents = false;
    fd_set rfds, wfds;

    qemu_lockcnt_inc(&ctx->list_lock);

    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    QLIST_FOREACH_RCU(node, &ctx->aio_handlers, node) {
        if (node->io_read) {
            FD_SET ((SOCKET)node->pfd.fd, &rfds);
        }
        if (node->io_write) {
            FD_SET ((SOCKET)node->pfd.fd, &wfds);
        }
    }

    if (select(0, &rfds, &wfds, NULL, &tv0) > 0) {
        QLIST_FOREACH_RCU(node, &ctx->aio_handlers, node) {
            node->pfd.revents = 0;
            if (FD_ISSET(node->pfd.fd, &rfds)) {
                node->pfd.revents |= G_IO_IN;
                have_select_revents = true;
            }

            if (FD_ISSET(node->pfd.fd, &wfds)) {
                node->pfd.revents |= G_IO_OUT;
                have_select_revents = true;
            }
        }
    }

    qemu_lockcnt_dec(&ctx->list_lock);
    return have_select_revents;
}

bool aio_pending(AioContext *ctx)
{
    AioHandler *node;
    bool result = false;

    qemu_lockcnt_inc(&ctx->list_lock);
    QLIST_FOREACH_RCU(node, &ctx->aio_handlers, node) {
        if (node->pfd.revents && node->io_notify) {
            result = true;
            break;
        }

        if ((node->pfd.revents & G_IO_IN) && node->io_read) {
            result = true;
            break;
        }
        if ((node->pfd.revents & G_IO_OUT) && node->io_write) {
            result = true;
            break;
        }
    }

    qemu_lockcnt_dec(&ctx->list_lock);
    return result;
}

static bool aio_dispatch_handlers(AioContext *ctx, HANDLE event)
{
    AioHandler *node;
    bool progress = false;
    AioHandler *tmp;

    QLIST_FOREACH_SAFE_RCU(node, &ctx->aio_handlers, node, tmp) {
        int revents = node->pfd.revents;

        if (!node->deleted &&
            (revents || event_notifier_get_handle(node->e) == event) &&
            node->io_notify) {
            node->pfd.revents = 0;
            node->io_notify(node->e);

            if (node->e != &ctx->notifier) {
                progress = true;
            }
        }

        if (!node->deleted &&
            (node->io_read || node->io_write)) {
            node->pfd.revents = 0;
            if ((revents & G_IO_IN) && node->io_read) {
                node->io_read(node->opaque);
                progress = true;
            }
            if ((revents & G_IO_OUT) && node->io_write) {
                node->io_write(node->opaque);
                progress = true;
            }

            if (event == event_notifier_get_handle(&ctx->notifier)) {
                WSANETWORKEVENTS ev;
                WSAEnumNetworkEvents(node->pfd.fd, event, &ev);
                if (ev.lNetworkEvents) {
                    progress = true;
                }
            }
        }

        if (node->deleted) {
            if (qemu_lockcnt_dec_if_lock(&ctx->list_lock)) {
                QLIST_REMOVE(node, node);
                g_free(node);
                qemu_lockcnt_inc_and_unlock(&ctx->list_lock);
            }
        }
    }

    return progress;
}

void aio_dispatch(AioContext *ctx)
{
    qemu_lockcnt_inc(&ctx->list_lock);
    aio_bh_poll(ctx);
    aio_dispatch_handlers(ctx, INVALID_HANDLE_VALUE);
    qemu_lockcnt_dec(&ctx->list_lock);
    timerlistgroup_run_timers(&ctx->tlg);
}

bool aio_poll(AioContext *ctx, bool blocking)
{
    AioHandler *node;
    HANDLE events[MAXIMUM_WAIT_OBJECTS];
    bool progress, have_select_revents, first;
    unsigned count;
    int timeout;

    assert(in_aio_context_home_thread(ctx == iohandler_get_aio_context() ?
                                      qemu_get_aio_context() : ctx));
    progress = false;

    if (blocking) {
        qatomic_set(&ctx->notify_me, qatomic_read(&ctx->notify_me) + 2);
        smp_mb();
    }

    qemu_lockcnt_inc(&ctx->list_lock);
    have_select_revents = aio_prepare(ctx);

    count = 0;
    QLIST_FOREACH_RCU(node, &ctx->aio_handlers, node) {
        if (!node->deleted && node->io_notify) {
            assert(count < MAXIMUM_WAIT_OBJECTS);
            events[count++] = event_notifier_get_handle(node->e);
        }
    }

    first = true;

    assert(count > 0);

    do {
        HANDLE event;
        int ret;

        timeout = blocking && !have_select_revents
            ? qemu_timeout_ns_to_ms(aio_compute_timeout(ctx)) : 0;
        ret = WaitForMultipleObjects(count, events, FALSE, timeout);
        if (blocking) {
            assert(first);
            qatomic_store_release(&ctx->notify_me,
                                  qatomic_read(&ctx->notify_me) - 2);
            aio_notify_accept(ctx);
        }

        if (first) {
            progress |= aio_bh_poll(ctx);
            first = false;
        }

        event = NULL;
        if ((DWORD) (ret - WAIT_OBJECT_0) < count) {
            event = events[ret - WAIT_OBJECT_0];
            events[ret - WAIT_OBJECT_0] = events[--count];
        } else if (!have_select_revents) {
            break;
        }

        have_select_revents = false;
        blocking = false;

        progress |= aio_dispatch_handlers(ctx, event);
    } while (count > 0);

    qemu_lockcnt_dec(&ctx->list_lock);

    progress |= timerlistgroup_run_timers(&ctx->tlg);
    return progress;
}

bool aio_context_setup(AioContext *ctx, Error **errp)
{
    return true;
}

void aio_context_destroy(AioContext *ctx)
{
}

void aio_context_set_poll_params(AioContext *ctx, int64_t max_ns,
                                 int64_t grow, int64_t shrink, Error **errp)
{
    if (max_ns) {
        error_setg(errp, "AioContext polling is not implemented on Windows");
    }
}

void aio_context_set_aio_params(AioContext *ctx, int64_t max_batch)
{
}
