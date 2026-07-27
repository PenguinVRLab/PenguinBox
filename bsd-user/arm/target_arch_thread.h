/*
 *  arm thread support
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

#ifndef TARGET_ARCH_THREAD_H
#define TARGET_ARCH_THREAD_H

static inline void target_thread_set_upcall(CPUARMState *env, abi_ulong entry,
    abi_ulong arg, abi_ulong stack_base, abi_ulong stack_size)
{
    abi_ulong sp;

    sp = (u_int)(stack_base + stack_size) & ~0x7;

    env->regs[13] = sp;
    env->regs[15] = entry & 0xfffffffe;
    env->regs[0] = arg;
    env->spsr = ARM_CPU_MODE_USR;
    cpsr_write(env, (entry & 1) * CPSR_T, CPSR_T, CPSRWriteByInstr);
}

static inline void target_thread_init(struct target_pt_regs *regs,
        struct image_info *infop)
{
    abi_long stack = infop->start_stack;
    memset(regs, 0, sizeof(*regs));
    regs->ARM_cpsr = ARM_CPU_MODE_USR;
    if (infop->entry & 1) {
        regs->ARM_cpsr |= CPSR_T;
    }
    regs->ARM_pc = infop->entry & 0xfffffffe;
    regs->ARM_sp = stack;
    regs->ARM_lr = infop->entry & 0xfffffffe;
}

#endif
