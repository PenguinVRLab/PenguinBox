/*
 * QEMU Object Model - QObject wrappers
 *
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_QOM_QOBJECT_H
#define QEMU_QOM_QOBJECT_H

struct QObject *object_property_get_qobject(Object *obj, const char *name,
                                            struct Error **errp);

bool object_property_set_qobject(Object *obj,
                                 const char *name, struct QObject *value,
                                 struct Error **errp);

#endif
