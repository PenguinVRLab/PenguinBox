/*
 * QEMU target info API (returning native types)
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_TARGET_INFO_H
#define QEMU_TARGET_INFO_H

const char *target_name(void);

unsigned target_long_bits(void);

const char *target_machine_typename(void);

const char *target_cpu_type(void);

bool target_big_endian(void);

bool target_base_arm(void);

bool target_arm(void);

bool target_aarch64(void);

#endif
