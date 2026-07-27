/*
 * QEMU authorization framework base class
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

#ifndef QAUTHZ_BASE_H
#define QAUTHZ_BASE_H

#include "qapi/error.h"
#include "qom/object.h"


#define TYPE_QAUTHZ "authz"

OBJECT_DECLARE_TYPE(QAuthZ, QAuthZClass,
                    QAUTHZ)


struct QAuthZ {
    Object parent_obj;
};


struct QAuthZClass {
    ObjectClass parent_class;

    bool (*is_allowed)(QAuthZ *authz,
                       const char *identity,
                       Error **errp);
};


bool qauthz_is_allowed(QAuthZ *authz,
                       const char *identity,
                       Error **errp);


bool qauthz_is_allowed_by_id(const char *authzid,
                             const char *identity,
                             Error **errp);

#endif
