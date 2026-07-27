/*
 * Internal memory barrier helpers for QEMU (target agnostic)
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef ACCEL_TCG_BACKEND_LDST_H
#define ACCEL_TCG_BACKEND_LDST_H

#include "tcg-target-mo.h"

#define tcg_req_mo(guest_mo, type) \
    ((type) & guest_mo & ~TCG_TARGET_DEFAULT_MO)

#define cpu_req_mo(cpu, type)     \
    do {                          \
        if (tcg_req_mo(cpu->cc->tcg_ops->guest_default_memory_order, type)) { \
            smp_mb();             \
        }                         \
    } while (0)

#endif
