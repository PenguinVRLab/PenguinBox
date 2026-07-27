/*
 * QEMU snapshots
 *
 * Copyright (c) 2004-2008 Fabrice Bellard
 * Copyright (c) 2009-2015 Red Hat Inc
 *
 * Authors:
 *  Juan Quintela <quintela@redhat.com>
 *
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_MIGRATION_SNAPSHOT_H
#define QEMU_MIGRATION_SNAPSHOT_H

#include "qapi/qapi-builtin-types.h"
#include "qapi/qapi-types-run-state.h"

bool save_snapshot(const char *name, bool overwrite,
                   const char *vmstate,
                   bool has_devices, strList *devices,
                   Error **errp);

bool load_snapshot(const char *name,
                   const char *vmstate,
                   bool has_devices, strList *devices,
                   Error **errp);

bool delete_snapshot(const char *name,
                    bool has_devices, strList *devices,
                    Error **errp);

void load_snapshot_resume(RunState state);

#endif
