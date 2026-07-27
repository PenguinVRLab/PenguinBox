/*
 * QEMU I/O channels memory buffer driver
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

#ifndef QIO_CHANNEL_BUFFER_H
#define QIO_CHANNEL_BUFFER_H

#include "io/channel.h"
#include "qom/object.h"

#define TYPE_QIO_CHANNEL_BUFFER "qio-channel-buffer"
OBJECT_DECLARE_SIMPLE_TYPE(QIOChannelBuffer, QIO_CHANNEL_BUFFER)


struct QIOChannelBuffer {
    QIOChannel parent;
    size_t capacity;
    size_t usage;
    size_t offset;
    uint8_t *data;
};


QIOChannelBuffer *
qio_channel_buffer_new(size_t capacity);

#endif
