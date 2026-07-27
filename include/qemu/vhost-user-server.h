/*
 * Sharing QEMU devices via vhost-user protocol
 *
 * Copyright (c) Coiby Xu <coiby.xu@gmail.com>.
 * Copyright (c) 2020 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#ifndef VHOST_USER_SERVER_H
#define VHOST_USER_SERVER_H

#include "subprojects/libvhost-user/libvhost-user.h"
#include "io/channel-socket.h"
#include "io/channel-file.h"
#include "io/net-listener.h"
#include "qapi/error.h"
#include "standard-headers/linux/virtio_blk.h"

typedef struct VuFdWatch {
    VuDev *vu_dev;
    int fd;
    void *pvt;
    vu_watch_cb cb;
    QTAILQ_ENTRY(VuFdWatch) next;
} VuFdWatch;

typedef struct {
    QIONetListener *listener;
    QEMUBH *restart_listener_bh;
    AioContext *ctx;
    int max_queues;
    const VuDevIface *vu_iface;

    unsigned int in_flight;

    bool in_qio_channel_yield;
    bool wait_idle;
    bool quiescing;
    VuDev vu_dev;
    QIOChannel *ioc;
    QIOChannelSocket *sioc;
    QTAILQ_HEAD(, VuFdWatch) vu_fd_watches;

    Coroutine *co_trip;
} VuServer;

bool vhost_user_server_start(VuServer *server,
                             SocketAddress *unix_socket,
                             AioContext *ctx,
                             uint16_t max_queues,
                             const VuDevIface *vu_iface,
                             Error **errp);

void vhost_user_server_stop(VuServer *server);

void vhost_user_server_inc_in_flight(VuServer *server);
void vhost_user_server_dec_in_flight(VuServer *server);
bool vhost_user_server_has_in_flight(VuServer *server);

void vhost_user_server_attach_aio_context(VuServer *server, AioContext *ctx);
void vhost_user_server_detach_aio_context(VuServer *server);

#endif
