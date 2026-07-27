/*
 * ARM AArch64 specific CPU for bsd-user
 *
 * Copyright (c) 2015 Stacey D. Son <sson at Freebsd>
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

#ifndef BSD_USER_AARCH64_TARGET_SYSCALL_H
#define BSD_USER_AARCH64_TARGET_SYSCALL_H

struct target_pt_regs {
    uint64_t    regs[31];
    uint64_t    sp;
    uint64_t    pc;
    uint64_t    pstate;
};

#define TARGET_HW_MACHINE       "arm64"
#define TARGET_HW_MACHINE_ARCH  "aarch64"

#endif
