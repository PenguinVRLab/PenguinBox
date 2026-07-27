/*
 * QEMU network listener
 *
 * Copyright (c) 2016-2017 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef QIO_NET_LISTENER_H
#define QIO_NET_LISTENER_H

#include "io/channel-socket.h"
#include "qom/object.h"

#define TYPE_QIO_NET_LISTENER "qio-net-listener"
OBJECT_DECLARE_SIMPLE_TYPE(QIONetListener,
                           QIO_NET_LISTENER)

typedef struct QIONetListenerSource QIONetListenerSource;

typedef void (*QIONetListenerClientFunc)(QIONetListener *listener,
                                         QIOChannelSocket *sioc,
                                         gpointer data);

struct QIONetListener {
    Object parent;

    char *name;
    QIONetListenerSource **source;
    size_t nsioc;
    GMainContext *context;
    AioContext *aio_context;

    bool connected;

    QemuMutex lock;
    QIONetListenerClientFunc io_func;
    gpointer io_data;
    GDestroyNotify io_notify;
};



QIONetListener *qio_net_listener_new(void);


void qio_net_listener_set_name(QIONetListener *listener,
                               const char *name);

int qio_net_listener_open_sync(QIONetListener *listener,
                               SocketAddress *addr,
                               int num,
                               Error **errp);

void qio_net_listener_add(QIONetListener *listener,
                          QIOChannelSocket *sioc);

void qio_net_listener_set_client_func_full(QIONetListener *listener,
                                           QIONetListenerClientFunc func,
                                           gpointer data,
                                           GDestroyNotify notify,
                                           GMainContext *context);

void qio_net_listener_set_client_func(QIONetListener *listener,
                                      QIONetListenerClientFunc func,
                                      gpointer data,
                                      GDestroyNotify notify);

void qio_net_listener_set_client_aio_func(QIONetListener *listener,
                                          QIONetListenerClientFunc func,
                                          void *data,
                                          AioContext *context);

QIOChannelSocket *qio_net_listener_wait_client(QIONetListener *listener);


void qio_net_listener_disconnect(QIONetListener *listener);


bool qio_net_listener_is_connected(QIONetListener *listener);


size_t qio_net_listener_nsioc(QIONetListener *listener);


QIOChannelSocket *qio_net_listener_sioc(QIONetListener *listener, size_t n);

SocketAddress *
qio_net_listener_get_local_address(QIONetListener *listener, size_t n,
                                   Error **errp);

#endif
