/*
 * ARM AArch64 specific signal definitions for bsd-user
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
#include "qemu/osdep.h"

#include "qemu.h"

abi_long set_sigtramp_args(CPUARMState *regs, int sig,
                           struct target_sigframe *frame,
                           abi_ulong frame_addr,
                           struct target_sigaction *ka)
{

    regs->xregs[0] = sig;
    regs->xregs[1] = frame_addr +
        offsetof(struct target_sigframe, sf_si);
    regs->xregs[2] = frame_addr +
        offsetof(struct target_sigframe, sf_uc);

    regs->pc = ka->_sa_handler;
    regs->xregs[TARGET_REG_SP] = frame_addr;
    regs->xregs[TARGET_REG_LR] = TARGET_PS_STRINGS - TARGET_SZSIGCODE;

    return 0;
}

abi_long get_mcontext(CPUARMState *regs, target_mcontext_t *mcp, int flags)
{
    int err = 0, i;
    uint64_t *gr = mcp->mc_gpregs.gp_x;

    mcp->mc_gpregs.gp_spsr = pstate_read(regs);
    if (flags & TARGET_MC_GET_CLEAR_RET) {
        gr[0] = 0UL;
        mcp->mc_gpregs.gp_spsr &= ~CPSR_C;
    } else {
        gr[0] = tswap64(regs->xregs[0]);
    }

    for (i = 1; i < 30; i++) {
        gr[i] = tswap64(regs->xregs[i]);
    }

    mcp->mc_gpregs.gp_sp = tswap64(regs->xregs[TARGET_REG_SP]);
    mcp->mc_gpregs.gp_lr = tswap64(regs->xregs[TARGET_REG_LR]);
    mcp->mc_gpregs.gp_elr = tswap64(regs->pc);

    return err;
}

abi_long setup_sigframe_arch(CPUARMState *env, abi_ulong frame_addr,
                             struct target_sigframe *frame, int flags)
{
    target_mcontext_t *mcp = &frame->sf_uc.uc_mcontext;

    get_mcontext(env, mcp, flags);
    return 0;
}

abi_long set_mcontext(CPUARMState *regs, target_mcontext_t *mcp, int srflag)
{
    int err = 0, i;
    const uint64_t *gr = mcp->mc_gpregs.gp_x;

    for (i = 0; i < 30; i++) {
        regs->xregs[i] = tswap64(gr[i]);
    }

    regs->xregs[TARGET_REG_SP] = tswap64(mcp->mc_gpregs.gp_sp);
    regs->xregs[TARGET_REG_LR] = tswap64(mcp->mc_gpregs.gp_lr);
    regs->pc = mcp->mc_gpregs.gp_elr;
    pstate_write(regs, mcp->mc_gpregs.gp_spsr);

    return err;
}

abi_long get_ucontext_sigreturn(CPUARMState *regs, abi_ulong target_sf,
                                abi_ulong *target_uc)
{
    uint32_t pstate = pstate_read(regs);

    *target_uc = 0;

    if ((pstate & PSTATE_M) != PSTATE_MODE_EL0t  ||
        (pstate & (PSTATE_F | PSTATE_I | PSTATE_A | PSTATE_D)) != 0) {
        return -TARGET_EINVAL;
    }

    *target_uc = target_sf;

    return 0;
}
