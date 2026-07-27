/*
 * QEMU file monitor helper
 *
 * Copyright (c) 2018 Red Hat, Inc.
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

#ifndef QEMU_FILEMONITOR_H
#define QEMU_FILEMONITOR_H



typedef struct QFileMonitor QFileMonitor;

typedef enum {
    QFILE_MONITOR_EVENT_CREATED,
    QFILE_MONITOR_EVENT_MODIFIED,
    QFILE_MONITOR_EVENT_DELETED,
    QFILE_MONITOR_EVENT_ATTRIBUTES,
    QFILE_MONITOR_EVENT_IGNORED,
} QFileMonitorEvent;


typedef void (*QFileMonitorHandler)(int64_t id,
                                    QFileMonitorEvent event,
                                    const char *filename,
                                    void *opaque);

QFileMonitor *qemu_file_monitor_new(Error **errp);

void qemu_file_monitor_free(QFileMonitor *mon);

int64_t qemu_file_monitor_add_watch(QFileMonitor *mon,
                                    const char *dirpath,
                                    const char *filename,
                                    QFileMonitorHandler cb,
                                    void *opaque,
                                    Error **errp);

void qemu_file_monitor_remove_watch(QFileMonitor *mon,
                                    const char *dirpath,
                                    int64_t id);

#endif
