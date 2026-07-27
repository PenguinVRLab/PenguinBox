/*
 * QEMU IGVM interface
 *
 * Copyright (C) 2024 SUSE
 *
 * Authors:
 *  Roy Hopkins <roy.hopkins@randomman.co.uk>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_IGVM_CFG_H
#define QEMU_IGVM_CFG_H

#include "qom/object.h"

typedef struct IgvmCfg {
    ObjectClass parent_class;

    char *filename;
} IgvmCfg;

typedef struct IgvmCfgClass {
    ObjectClass parent_class;

    int (*process)(IgvmCfg *cfg, ConfidentialGuestSupport *cgs,
                   bool onlyVpContext, Error **errp);

} IgvmCfgClass;

#define TYPE_IGVM_CFG "igvm-cfg"

OBJECT_DECLARE_TYPE(IgvmCfg, IgvmCfgClass, IGVM_CFG)

#endif
