/*
 * Reset container
 *
 * Copyright (c) 2024 Linaro, Ltd
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_RESETCONTAINER_H
#define HW_RESETCONTAINER_H

#include "qom/object.h"

#define TYPE_RESETTABLE_CONTAINER "resettable-container"
OBJECT_DECLARE_SIMPLE_TYPE(ResettableContainer, RESETTABLE_CONTAINER)

void resettable_container_add(ResettableContainer *rc, Object *obj);

void resettable_container_remove(ResettableContainer *rc, Object *obj);

#endif
