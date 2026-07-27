/*
 * QEMU generic buffers
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

#ifndef QEMU_BUFFER_H
#define QEMU_BUFFER_H


typedef struct Buffer Buffer;

struct Buffer {
    char *name;
    size_t capacity;
    size_t offset;
    uint64_t avg_size;
    uint8_t *buffer;
};

void buffer_init(Buffer *buffer, const char *name, ...)
        G_GNUC_PRINTF(2, 3);

void buffer_shrink(Buffer *buffer);

void buffer_reserve(Buffer *buffer, size_t len);

void buffer_reset(Buffer *buffer);

void buffer_free(Buffer *buffer);

void buffer_append(Buffer *buffer, const void *data, size_t len);

void buffer_advance(Buffer *buffer, size_t len);

uint8_t *buffer_end(Buffer *buffer);

gboolean buffer_empty(Buffer *buffer);

void buffer_move_empty(Buffer *to, Buffer *from);

void buffer_move(Buffer *to, Buffer *from);

#endif
