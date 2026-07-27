/*
 * QEMU support -- ARM Power Control specific functions.
 *
 * Copyright (c) 2016 Jean-Christophe Dubois
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_ARM_POWERCTL_H
#define QEMU_ARM_POWERCTL_H

#include "kvm-consts.h"

#define QEMU_ARM_POWERCTL_RET_SUCCESS QEMU_PSCI_RET_SUCCESS
#define QEMU_ARM_POWERCTL_INVALID_PARAM QEMU_PSCI_RET_INVALID_PARAMS
#define QEMU_ARM_POWERCTL_ALREADY_ON QEMU_PSCI_RET_ALREADY_ON
#define QEMU_ARM_POWERCTL_IS_OFF QEMU_PSCI_RET_DENIED
#define QEMU_ARM_POWERCTL_ON_PENDING QEMU_PSCI_RET_ON_PENDING

CPUState *arm_get_cpu_by_id(uint64_t cpuid);

int arm_set_cpu_on(uint64_t cpuid, uint64_t entry, uint64_t context_id,
                   uint32_t target_el, bool target_aa64);

int arm_set_cpu_off(uint64_t cpuid);

int arm_reset_cpu(uint64_t cpuid);

int arm_set_cpu_on_and_reset(uint64_t cpuid);

#endif
