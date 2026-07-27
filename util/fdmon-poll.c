/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * poll(2) file descriptor monitoring
 *
 * Uses ppoll(2) when available, g_poll() otherwise.
 */

#include "qemu/osdep.h"
#include "aio-posix.h"
#include "qemu/rcu_queue.h"

static __thread GPollFD *pollfds;
static __thread AioHandler **nodes;
static __thread unsigned npfd, nalloc;
static __thread Notifier pollfds_cleanup_notifier;

static void pollfds_cleanup(Notifier *n, void *unused)
{
    g_assert(npfd == 0);
    g_free(pollfds);
    g_free(nodes);
    nalloc = 0;
}

static void add_pollfd(AioHandler *node)
{
    if (npfd == nalloc) {
        if (nalloc == 0) {
            pollfds_cleanup_notifier.notify = pollfds_cleanup;
            qemu_thread_atexit_add(&pollfds_cleanup_notifier);
            nalloc = 8;
        } else {
            g_assert(nalloc <= INT_MAX);
            nalloc *= 2;
        }
        pollfds = g_renew(GPollFD, pollfds, nalloc);
        nodes = g_renew(AioHandler *, nodes, nalloc);
    }
    nodes[npfd] = node;
    pollfds[npfd] = (GPollFD) {
        .fd = node->pfd.fd,
        .events = node->pfd.events,
    };
    npfd++;
}

static int fdmon_poll_wait(AioContext *ctx, AioHandlerList *ready_list,
                            int64_t timeout)
{
    AioHandler *node;
    int ret;

    assert(npfd == 0);

    QLIST_FOREACH_RCU(node, &ctx->aio_handlers, node) {
        if (!QLIST_IS_INSERTED(node, node_deleted) && node->pfd.events) {
            add_pollfd(node);
        }
    }

    if (fdmon_epoll_try_upgrade(ctx, npfd)) {
        QLIST_FOREACH_RCU(node, &ctx->aio_handlers, node) {
            if (!QLIST_IS_INSERTED(node, node_deleted) && node->pfd.events) {
                g_source_remove_poll(&ctx->source, &node->pfd);
            }
        }
        npfd = 0;
        return ctx->fdmon_ops->wait(ctx, ready_list, timeout);
    }

    ret = qemu_poll_ns(pollfds, npfd, timeout);
    if (ret > 0) {
        int i;

        for (i = 0; i < npfd; i++) {
            int revents = pollfds[i].revents;

            if (revents) {
                aio_add_ready_handler(ready_list, nodes[i], revents);
            }
        }
    }

    npfd = 0;
    return ret;
}

static void fdmon_poll_update(AioContext *ctx,
                              AioHandler *old_node,
                              AioHandler *new_node)
{
    if (old_node) {
        if (!g_source_is_destroyed(&ctx->source)) {
            g_source_remove_poll(&ctx->source, &old_node->pfd);
        }
    }

    if (new_node) {
        g_source_add_poll(&ctx->source, &new_node->pfd);
    }
}

static void fdmon_poll_gsource_prepare(AioContext *ctx)
{
}

static bool fdmon_poll_gsource_check(AioContext *ctx)
{
    AioHandler *node;
    bool result = false;

    qemu_lockcnt_inc(&ctx->list_lock);

    QLIST_FOREACH_RCU(node, &ctx->aio_handlers, node) {
        int revents = node->pfd.revents & node->pfd.events;

        if (revents & (G_IO_IN | G_IO_HUP | G_IO_ERR) && node->io_read) {
            result = true;
            break;
        }
        if (revents & (G_IO_OUT | G_IO_ERR) && node->io_write) {
            result = true;
            break;
        }
    }

    qemu_lockcnt_dec(&ctx->list_lock);

    return result;
}

static void fdmon_poll_gsource_dispatch(AioContext *ctx,
                                        AioHandlerList *ready_list)
{
    AioHandler *node;

    QLIST_FOREACH_RCU(node, &ctx->aio_handlers, node) {
        int revents = node->pfd.revents;

        if (revents) {
            aio_add_ready_handler(ready_list, node, revents);
        }
    }
}

const FDMonOps fdmon_poll_ops = {
    .update = fdmon_poll_update,
    .wait = fdmon_poll_wait,
    .need_wait = aio_poll_disabled,
    .gsource_prepare = fdmon_poll_gsource_prepare,
    .gsource_check = fdmon_poll_gsource_check,
    .gsource_dispatch = fdmon_poll_gsource_dispatch,
};

void fdmon_poll_downgrade(AioContext *ctx)
{
    AioHandler *node;

    ctx->fdmon_ops = &fdmon_poll_ops;

    QLIST_FOREACH_RCU(node, &ctx->aio_handlers, node) {
        if (!QLIST_IS_INSERTED(node, node_deleted) && node->pfd.events) {
            g_source_add_poll(&ctx->source, &node->pfd);
        }
    }
}
