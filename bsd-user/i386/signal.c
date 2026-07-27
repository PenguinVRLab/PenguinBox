/*
 *  i386 dependent signal definitions
 *
 *  Copyright (c) 2013 Stacey D. Son
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu.h"

abi_long set_sigtramp_args(CPUX86State *env, int sig,
                           struct target_sigframe *frame,
                           abi_ulong frame_addr,
                           struct target_sigaction *ka)
{
    return 0;
}

abi_long setup_sigframe_arch(CPUX86State *env, abi_ulong frame_addr,
                             struct target_sigframe *frame, int flags)
{
    target_mcontext_t *mcp = &frame->sf_uc.uc_mcontext;

    get_mcontext(env, mcp, flags);
    return 0;
}

abi_long get_mcontext(CPUX86State *regs, target_mcontext_t *mcp, int flags)
{
    return -TARGET_EOPNOTSUPP;
}

abi_long set_mcontext(CPUX86State *regs, target_mcontext_t *mcp, int srflag)
{
    return -TARGET_EOPNOTSUPP;
}

abi_long get_ucontext_sigreturn(CPUX86State *regs, abi_ulong target_sf,
                                abi_ulong *target_uc)
{
    *target_uc = 0;
    return -TARGET_EOPNOTSUPP;
}
