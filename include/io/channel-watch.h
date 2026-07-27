/*
 * QEMU I/O channels watch helper APIs
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

#ifndef QIO_CHANNEL_WATCH_H
#define QIO_CHANNEL_WATCH_H

#include "io/channel.h"

GSource *qio_channel_create_fd_watch(QIOChannel *ioc,
                                     int fd,
                                     GIOCondition condition);

GSource *qio_channel_create_socket_watch(QIOChannel *ioc,
                                         int fd,
                                         GIOCondition condition);

GSource *qio_channel_create_fd_pair_watch(QIOChannel *ioc,
                                          int fdread,
                                          int fdwrite,
                                          GIOCondition condition);

#endif
