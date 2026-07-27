/*
 * ARM AArch64 VM parameters definitions for bsd-user.
 *
 * Copyright (c) 2015 Stacey D. Son <sson at FreeBSD>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TARGET_ARCH_VMPARAM_H
#define TARGET_ARCH_VMPARAM_H

#include "cpu.h"

#define TARGET_MAXTSIZ      (1 * GiB)
#define TARGET_DFLDSIZ      (128 * MiB)
#define TARGET_MAXDSIZ      (1 * GiB)
#define TARGET_DFLSSIZ      (128 * MiB)
#define TARGET_MAXSSIZ      (1 * GiB)
#define TARGET_SGROWSIZ     (128 * KiB)

#define TARGET_VM_MAXUSER_ADDRESS   (0x00007fffff000000ULL - (512 * MiB))
#define TARGET_USRSTACK             TARGET_VM_MAXUSER_ADDRESS

static inline abi_ulong get_sp_from_cpustate(CPUARMState *state)
{
    return state->xregs[31];
}

static inline void set_second_rval(CPUARMState *state, abi_ulong retval2)
{
    state->xregs[1] = retval2;
}

static inline abi_ulong get_second_rval(CPUARMState *state)
{
    return state->xregs[1];
}

#endif
