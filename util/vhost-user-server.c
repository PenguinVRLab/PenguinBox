/*
 * Sharing QEMU devices via vhost-user protocol
 *
 * Copyright (c) Coiby Xu <coiby.xu@gmail.com>.
 * Copyright (c) 2020 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/vhost-user-server.h"
#include "block/aio-wait.h"

static void vmsg_close_fds(VhostUserMsg *vmsg)
{
    int i;
    for (i = 0; i < vmsg->fd_num; i++) {
        close(vmsg->fds[i]);
    }
}

static bool vmsg_unblock_fds(VhostUserMsg *vmsg, Error **errp)
{
    int i;

    if (vmsg->request == VHOST_USER_ADD_MEM_REG ||
        vmsg->request == VHOST_USER_SET_MEM_TABLE) {
        return true;
    }

    for (i = 0; i < vmsg->fd_num; i++) {
        if (!qemu_set_blocking(vmsg->fds[i], false, errp)) {
            return false;
        }
    }

    return true;
}

static void panic_cb(VuDev *vu_dev, const char *buf)
{
    error_report("vu_panic: %s", buf);
}

void vhost_user_server_inc_in_flight(VuServer *server)
{
    assert(!server->wait_idle);
    qatomic_inc(&server->in_flight);
}

void vhost_user_server_dec_in_flight(VuServer *server)
{
    if (qatomic_fetch_dec(&server->in_flight) == 1) {
        if (server->wait_idle) {
            aio_co_wake(server->co_trip);
        }
    }
}

bool vhost_user_server_has_in_flight(VuServer *server)
{
    return qatomic_load_acquire(&server->in_flight) > 0;
}

static bool coroutine_fn
vu_message_read(VuDev *vu_dev, int conn_fd, VhostUserMsg *vmsg)
{
    struct iovec iov = {
        .iov_base = (char *)vmsg,
        .iov_len = VHOST_USER_HDR_SIZE,
    };
    int rc, read_bytes = 0;
    Error *local_err = NULL;
    const size_t max_fds = G_N_ELEMENTS(vmsg->fds);
    VuServer *server = container_of(vu_dev, VuServer, vu_dev);
    QIOChannel *ioc = server->ioc;

    vmsg->fd_num = 0;
    if (!ioc) {
        goto fail;
    }

    assert(qemu_in_coroutine());
    do {
        size_t nfds = 0;
        int *fds = NULL;

        rc = qio_channel_readv_full(ioc, &iov, 1, &fds, &nfds, 0, &local_err);
        if (rc < 0) {
            if (rc == QIO_CHANNEL_ERR_BLOCK) {
                assert(local_err == NULL);
                if (server->ctx) {
                    server->in_qio_channel_yield = true;
                    qio_channel_yield(ioc, G_IO_IN);
                    server->in_qio_channel_yield = false;
                } else {
                    return false;
                }
                continue;
            } else {
                error_report_err(local_err);
                goto fail;
            }
        }

        if (nfds > 0) {
            if (vmsg->fd_num + nfds > max_fds) {
                error_report("A maximum of %zu fds are allowed, "
                             "however got %zu fds now",
                             max_fds, vmsg->fd_num + nfds);
                g_free(fds);
                goto fail;
            }
            memcpy(vmsg->fds + vmsg->fd_num, fds, nfds * sizeof(vmsg->fds[0]));
            vmsg->fd_num += nfds;
            g_free(fds);
        }

        if (rc == 0) {
            goto fail;
        }

        iov.iov_base += rc;
        iov.iov_len -= rc;
        read_bytes += rc;
    } while (read_bytes != VHOST_USER_HDR_SIZE);

    if (!vmsg_unblock_fds(vmsg, &local_err)) {
        error_report_err(local_err);
        goto fail;
    }
    if (vmsg->size > sizeof(vmsg->payload)) {
        error_report("Error: too big message request: %d, "
                     "size: vmsg->size: %u, "
                     "while sizeof(vmsg->payload) = %zu",
                     vmsg->request, vmsg->size, sizeof(vmsg->payload));
        goto fail;
    }

    struct iovec iov_payload = {
        .iov_base = (char *)&vmsg->payload,
        .iov_len = vmsg->size,
    };
    if (vmsg->size) {
        rc = qio_channel_readv_all_eof(ioc, &iov_payload, 1, &local_err);
        if (rc != 1) {
            if (local_err) {
                error_report_err(local_err);
            }
            goto fail;
        }
    }

    return true;

fail:
    vmsg_close_fds(vmsg);

    return false;
}

static coroutine_fn void vu_client_trip(void *opaque)
{
    VuServer *server = opaque;
    VuDev *vu_dev = &server->vu_dev;

    while (!vu_dev->broken) {
        if (server->quiescing) {
            server->co_trip = NULL;
            aio_wait_kick();
            return;
        }
        if (!vu_dispatch(vu_dev) && server->ctx) {
            break;
        }
    }

    if (vhost_user_server_has_in_flight(server)) {
        server->wait_idle = true;
        qemu_coroutine_yield();
        server->wait_idle = false;
    }
    assert(!vhost_user_server_has_in_flight(server));

    vu_deinit(vu_dev);

    assert(QTAILQ_EMPTY(&server->vu_fd_watches));

    object_unref(OBJECT(server->sioc));
    server->sioc = NULL;

    object_unref(OBJECT(server->ioc));
    server->ioc = NULL;

    server->co_trip = NULL;
    if (server->restart_listener_bh) {
        qemu_bh_schedule(server->restart_listener_bh);
    }
    aio_wait_kick();
}

static void kick_handler(void *opaque)
{
    VuFdWatch *vu_fd_watch = opaque;
    VuDev *vu_dev = vu_fd_watch->vu_dev;

    vu_fd_watch->cb(vu_dev, 0, vu_fd_watch->pvt);

    if (vu_dev->broken) {
        VuServer *server = container_of(vu_dev, VuServer, vu_dev);

        qio_channel_shutdown(server->ioc, QIO_CHANNEL_SHUTDOWN_BOTH, NULL);
    }
}

static VuFdWatch *find_vu_fd_watch(VuServer *server, int fd)
{

    VuFdWatch *vu_fd_watch, *next;
    QTAILQ_FOREACH_SAFE(vu_fd_watch, &server->vu_fd_watches, next, next) {
        if (vu_fd_watch->fd == fd) {
            return vu_fd_watch;
        }
    }
    return NULL;
}

static void
set_watch(VuDev *vu_dev, int fd, int vu_evt,
          vu_watch_cb cb, void *pvt)
{

    VuServer *server = container_of(vu_dev, VuServer, vu_dev);
    g_assert(vu_dev);
    g_assert(fd >= 0);
    g_assert(cb);

    VuFdWatch *vu_fd_watch = find_vu_fd_watch(server, fd);

    if (!vu_fd_watch) {
        vu_fd_watch = g_new0(VuFdWatch, 1);

        QTAILQ_INSERT_TAIL(&server->vu_fd_watches, vu_fd_watch, next);

        vu_fd_watch->fd = fd;
        vu_fd_watch->cb = cb;
        qemu_set_blocking(fd, false, &error_abort);
        aio_set_fd_handler(server->ctx, fd, kick_handler,
                           NULL, NULL, NULL, vu_fd_watch);
        vu_fd_watch->vu_dev = vu_dev;
        vu_fd_watch->pvt = pvt;
    }
}


static void remove_watch(VuDev *vu_dev, int fd)
{
    VuServer *server;
    g_assert(vu_dev);
    g_assert(fd >= 0);

    server = container_of(vu_dev, VuServer, vu_dev);

    VuFdWatch *vu_fd_watch = find_vu_fd_watch(server, fd);

    if (!vu_fd_watch) {
        return;
    }
    aio_set_fd_handler(server->ctx, fd, NULL, NULL, NULL, NULL, NULL);

    QTAILQ_REMOVE(&server->vu_fd_watches, vu_fd_watch, next);
    g_free(vu_fd_watch);
}


static void vu_accept(QIONetListener *listener, QIOChannelSocket *sioc,
                      gpointer opaque)
{
    VuServer *server = opaque;
    Error *local_err = NULL;

    if (server->sioc) {
        warn_report("Only one vhost-user client is allowed to "
                    "connect the server one time");
        return;
    }

    if (!vu_init(&server->vu_dev, server->max_queues, sioc->fd, panic_cb,
                 vu_message_read, set_watch, remove_watch, server->vu_iface)) {
        error_report("Failed to initialize libvhost-user");
        return;
    }

    qio_net_listener_set_client_func(server->listener,
                                     NULL,
                                     NULL,
                                     NULL);
    server->sioc = sioc;
    object_ref(OBJECT(server->sioc));
    qio_channel_set_name(QIO_CHANNEL(sioc), "vhost-user client");
    server->ioc = QIO_CHANNEL(sioc);
    object_ref(OBJECT(server->ioc));

    if (!qio_channel_set_blocking(server->ioc, false, &local_err)) {
        error_report_err(local_err);
        vu_deinit(&server->vu_dev);
        return;
    }

    qio_channel_set_follow_coroutine_ctx(server->ioc, true);

    vhost_user_server_attach_aio_context(server, server->ctx);
}

void vhost_user_server_stop(VuServer *server)
{
    qemu_bh_delete(server->restart_listener_bh);
    server->restart_listener_bh = NULL;

    if (server->sioc) {
        VuFdWatch *vu_fd_watch;

        QTAILQ_FOREACH(vu_fd_watch, &server->vu_fd_watches, next) {
            aio_set_fd_handler(server->ctx, vu_fd_watch->fd,
                               NULL, NULL, NULL, NULL, vu_fd_watch);
        }

        qio_channel_shutdown(server->ioc, QIO_CHANNEL_SHUTDOWN_BOTH, NULL);

        AIO_WAIT_WHILE(server->ctx, server->co_trip);
    }

    if (server->listener) {
        qio_net_listener_disconnect(server->listener);
        object_unref(OBJECT(server->listener));
    }
}

static void restart_listener_bh(void *opaque)
{
    VuServer *server = opaque;

    qio_net_listener_set_client_func(server->listener, vu_accept, server,
                                     NULL);
}

void vhost_user_server_attach_aio_context(VuServer *server, AioContext *ctx)
{
    VuFdWatch *vu_fd_watch;

    server->ctx = ctx;

    if (!server->sioc) {
        return;
    }

    QTAILQ_FOREACH(vu_fd_watch, &server->vu_fd_watches, next) {
        aio_set_fd_handler(ctx, vu_fd_watch->fd, kick_handler, NULL,
                           NULL, NULL, vu_fd_watch);
    }

    if (server->co_trip) {
        AioContext *co_ctx = qemu_coroutine_get_aio_context(server->co_trip);

        assert(!server->quiescing);
        assert(!co_ctx || co_ctx == ctx);
    } else {
        server->co_trip = qemu_coroutine_create(vu_client_trip, server);
        assert(!server->in_qio_channel_yield);
        aio_co_schedule(ctx, server->co_trip);
    }
}

void vhost_user_server_detach_aio_context(VuServer *server)
{
    if (server->sioc) {
        VuFdWatch *vu_fd_watch;

        QTAILQ_FOREACH(vu_fd_watch, &server->vu_fd_watches, next) {
            aio_set_fd_handler(server->ctx, vu_fd_watch->fd,
                               NULL, NULL, NULL, NULL, vu_fd_watch);
        }
    }

    server->ctx = NULL;

    if (server->ioc) {
        if (server->in_qio_channel_yield) {
            qio_channel_wake_read(server->ioc);
        }
    }
}

bool vhost_user_server_start(VuServer *server,
                             SocketAddress *socket_addr,
                             AioContext *ctx,
                             uint16_t max_queues,
                             const VuDevIface *vu_iface,
                             Error **errp)
{
    QEMUBH *bh;
    QIONetListener *listener;

    if (socket_addr->type != SOCKET_ADDRESS_TYPE_UNIX &&
        socket_addr->type != SOCKET_ADDRESS_TYPE_FD) {
        error_setg(errp, "Only socket address types 'unix' and 'fd' are supported");
        return false;
    }

    listener = qio_net_listener_new();
    if (qio_net_listener_open_sync(listener, socket_addr, 1,
                                   errp) < 0) {
        object_unref(OBJECT(listener));
        return false;
    }

    bh = qemu_bh_new(restart_listener_bh, server);

    *server = (VuServer) {
        .listener              = listener,
        .restart_listener_bh   = bh,
        .vu_iface              = vu_iface,
        .max_queues            = max_queues,
        .ctx                   = ctx,
    };

    qio_net_listener_set_name(server->listener, "vhost-user-backend-listener");

    qio_net_listener_set_client_func(server->listener,
                                     vu_accept,
                                     server,
                                     NULL);

    QTAILQ_INIT(&server->vu_fd_watches);
    return true;
}
