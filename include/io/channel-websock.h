/*
 * QEMU I/O channels driver websockets
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef QIO_CHANNEL_WEBSOCK_H
#define QIO_CHANNEL_WEBSOCK_H

#include "io/channel.h"
#include "qemu/buffer.h"
#include "io/task.h"
#include "qom/object.h"

#define TYPE_QIO_CHANNEL_WEBSOCK "qio-channel-websock"
OBJECT_DECLARE_SIMPLE_TYPE(QIOChannelWebsock, QIO_CHANNEL_WEBSOCK)

typedef union QIOChannelWebsockMask QIOChannelWebsockMask;

union QIOChannelWebsockMask {
    char c[4];
    uint32_t u;
};

struct QIOChannelWebsock {
    QIOChannel parent;
    QIOChannel *master;
    Buffer encinput;
    Buffer encoutput;
    Buffer rawinput;
    size_t payload_remain;
    size_t pong_remain;
    QIOChannelWebsockMask mask;
    guint hs_io_tag;
    guint io_tag;
    Error *io_err;
    gboolean io_eof;
    uint8_t opcode;
};

QIOChannelWebsock *
qio_channel_websock_new_server(QIOChannel *master);

void qio_channel_websock_handshake(QIOChannelWebsock *ioc,
                                   QIOTaskFunc func,
                                   gpointer opaque,
                                   GDestroyNotify destroy);

#endif
