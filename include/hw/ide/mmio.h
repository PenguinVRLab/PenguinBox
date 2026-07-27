/*
 * QEMU IDE Emulation: mmio support (for embedded).
 *
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2006 Openedhand Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_IDE_MMIO_H
#define HW_IDE_MMIO_H

#include "qom/object.h"

#define TYPE_MMIO_IDE "mmio-ide"
OBJECT_DECLARE_SIMPLE_TYPE(MMIOIDEState, MMIO_IDE)

void mmio_ide_init_drives(DeviceState *dev, DriveInfo *hd0, DriveInfo *hd1);

#endif
