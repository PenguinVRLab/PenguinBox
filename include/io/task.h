/*
 * QEMU I/O task
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

#ifndef QIO_TASK_H
#define QIO_TASK_H

typedef struct QIOTask QIOTask;

typedef void (*QIOTaskFunc)(QIOTask *task,
                            gpointer opaque);

typedef void (*QIOTaskWorker)(QIOTask *task,
                              gpointer opaque);

QIOTask *qio_task_new(Object *source,
                      QIOTaskFunc func,
                      gpointer opaque,
                      GDestroyNotify destroy);

void qio_task_run_in_thread(QIOTask *task,
                            QIOTaskWorker worker,
                            gpointer opaque,
                            GDestroyNotify destroy,
                            GMainContext *context);


void qio_task_wait_thread(QIOTask *task);


void qio_task_complete(QIOTask *task);


void qio_task_set_error(QIOTask *task,
                        Error *err);


bool qio_task_propagate_error(QIOTask *task,
                              Error **errp);


void qio_task_set_result_pointer(QIOTask *task,
                                 gpointer result,
                                 GDestroyNotify notify);


gpointer qio_task_get_result_pointer(QIOTask *task);


Object *qio_task_get_source(QIOTask *task);

#endif
