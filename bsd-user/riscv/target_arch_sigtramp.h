/*
 * RISC-V sigcode
 *
 * Copyright (c) 2019 Mark Corbin
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

#ifndef TARGET_ARCH_SIGTRAMP_H
#define TARGET_ARCH_SIGTRAMP_H

static inline abi_long setup_sigtramp(abi_ulong offset, unsigned sigf_uc,
        unsigned sys_sigreturn)
{
    uint32_t sys_exit = TARGET_FREEBSD_NR_exit;

    uint32_t sigtramp_code[] = {
 const_le32(0x00010513),
 const_le32(0x00050513 + (sigf_uc << 20)),
 const_le32(0x00000293 + (sys_sigreturn << 20)),
 const_le32(0x00000073),
 const_le32(0x00000293 + (sys_exit << 20)),
 const_le32(0x00000073),
 const_le32(0xFF1FF06F)
    };

    return memcpy_to_target(offset, sigtramp_code, TARGET_SZSIGCODE);
}
#endif
