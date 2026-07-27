/*
 * QEMU list file authorization driver
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

#ifndef QAUTHZ_LISTFILE_H
#define QAUTHZ_LISTFILE_H

#include "authz/list.h"
#include "qemu/filemonitor.h"
#include "qom/object.h"

#define TYPE_QAUTHZ_LIST_FILE "authz-list-file"

OBJECT_DECLARE_SIMPLE_TYPE(QAuthZListFile,
                           QAUTHZ_LIST_FILE)



struct QAuthZListFile {
    QAuthZ parent_obj;

    QAuthZ *list;
    char *filename;
    bool refresh;
    QFileMonitor *file_monitor;
    int64_t file_watch;
};




QAuthZListFile *qauthz_list_file_new(const char *id,
                                     const char *filename,
                                     bool refresh,
                                     Error **errp);

#endif
