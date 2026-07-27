/*
 * QEMU TargetInfo structure definition
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_TARGET_INFO_IMPL_H
#define QEMU_TARGET_INFO_IMPL_H

#include "qapi/qapi-types-common.h"
#include "qapi/qapi-types-machine.h"

typedef struct TargetInfo {
    const char *target_name;
    SysEmuTarget target_arch;
    unsigned long_bits;
    const char *cpu_type;
    const char *machine_typename;
    EndianMode endianness;
} TargetInfo;

const TargetInfo *target_info(void);

#endif
