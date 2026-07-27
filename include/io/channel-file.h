/*
 * QEMU I/O channels files driver
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

#ifndef QIO_CHANNEL_FILE_H
#define QIO_CHANNEL_FILE_H

#include "io/channel.h"
#include "qom/object.h"

#define TYPE_QIO_CHANNEL_FILE "qio-channel-file"
OBJECT_DECLARE_SIMPLE_TYPE(QIOChannelFile, QIO_CHANNEL_FILE)


struct QIOChannelFile {
    QIOChannel parent;
    int fd;
};


QIOChannelFile *
qio_channel_file_new_fd(int fd);

QIOChannelFile *
qio_channel_file_new_dupfd(int fd, Error **errp);

QIOChannelFile *
qio_channel_file_new_path(const char *path,
                          int flags,
                          mode_t mode,
                          Error **errp);

#endif
