/*
 *  QEMU Alpha clock helpers.
 *
 *  Copyright (c) 2007 Jocelyn Mayer
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "exec/helper-proto.h"
#include "cpu.h"

uint64_t helper_load_pcc(CPUAlphaState *env)
{
#ifndef CONFIG_USER_ONLY
    return (((uint64_t)env->pcc_ofs << 32)
            | (uint32_t)(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) >> 2));
#else
    return (uint32_t)cpu_get_host_ticks();
#endif
}
