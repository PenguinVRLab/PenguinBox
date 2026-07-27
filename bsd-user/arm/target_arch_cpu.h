/*
 *  arm cpu init and loop
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

#ifndef TARGET_ARCH_CPU_H
#define TARGET_ARCH_CPU_H

#include "target_arch.h"
#include "signal-common.h"

#define TARGET_DEFAULT_CPU_MODEL "any"

static inline void target_cpu_init(CPUARMState *env,
        struct target_pt_regs *regs)
{
    int i;

    cpsr_write(env, regs->uregs[16], CPSR_USER | CPSR_EXEC,
               CPSRWriteByInstr);
    for (i = 0; i < 16; i++) {
        env->regs[i] = regs->uregs[i];
    }
}

static inline G_NORETURN void target_cpu_loop(CPUARMState *env)
{
    int trapnr, si_signo, si_code;
    CPUState *cs = env_cpu(env);

    for (;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        qemu_process_cpu_events(cs);
        switch (trapnr) {
        case EXCP_UDEF:
        case EXCP_NOCP:
        case EXCP_INVSTATE:
            force_sig_fault(TARGET_SIGILL, TARGET_ILL_ILLOPC, env->regs[15]);
            break;
        case EXCP_SWI:
            {
                int ret;
                abi_ulong params = get_sp_from_cpustate(env);
                int32_t syscall_nr = env->regs[7];
                int32_t arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8;

                if (syscall_nr == TARGET_FREEBSD_NR_syscall) {
                    syscall_nr = env->regs[0];
                    arg1 = env->regs[1];
                    arg2 = env->regs[2];
                    arg3 = env->regs[3];
                    get_user_s32(arg4, params);
                    params += sizeof(int32_t);
                    get_user_s32(arg5, params);
                    params += sizeof(int32_t);
                    get_user_s32(arg6, params);
                    params += sizeof(int32_t);
                    get_user_s32(arg7, params);
                    arg8 = 0;
                } else if (syscall_nr == TARGET_FREEBSD_NR___syscall) {
                    syscall_nr = env->regs[0];
                    arg1 = env->regs[2];
                    arg2 = env->regs[3];
                    get_user_s32(arg3, params);
                    params += sizeof(int32_t);
                    get_user_s32(arg4, params);
                    params += sizeof(int32_t);
                    get_user_s32(arg5, params);
                    params += sizeof(int32_t);
                    get_user_s32(arg6, params);
                    arg7 = 0;
                    arg8 = 0;
                } else {
                    arg1 = env->regs[0];
                    arg2 = env->regs[1];
                    arg3 = env->regs[2];
                    arg4 = env->regs[3];
                    get_user_s32(arg5, params);
                    params += sizeof(int32_t);
                    get_user_s32(arg6, params);
                    params += sizeof(int32_t);
                    get_user_s32(arg7, params);
                    params += sizeof(int32_t);
                    get_user_s32(arg8, params);
                }
                ret = do_freebsd_syscall(env, syscall_nr, arg1, arg2, arg3,
                                         arg4, arg5, arg6, arg7, arg8);
                if (-TARGET_EJUSTRETURN == ret) {
                    break;
                }
                if (-TARGET_ERESTART == ret) {
                    env->regs[15] -= env->thumb ? 2 : 4;
                    break;
                }
                if ((unsigned int)ret >= (unsigned int)(-515)) {
                    ret = -ret;
                    cpsr_write(env, CPSR_C, CPSR_C, CPSRWriteByInstr);
                    env->regs[0] = ret;
                } else {
                    cpsr_write(env, 0, CPSR_C, CPSRWriteByInstr);
                    env->regs[0] = ret;
                }
            }
            break;
        case EXCP_INTERRUPT:
            break;
        case EXCP_PREFETCH_ABORT:
        case EXCP_DATA_ABORT:

            switch (env->exception.fsr & 0x1f) {
            case 0x1:
                si_signo = TARGET_SIGBUS;
                si_code = TARGET_BUS_ADRALN;
                break;
            case 0x3:
            case 0x6:
            case 0x9:
            case 0xb:
            case 0xd:
            case 0xf:
                si_signo = TARGET_SIGSEGV;
                si_code = TARGET_SEGV_ACCERR;
                break;
            case 0x5:
            case 0x7:
                si_signo = TARGET_SIGSEGV;
                si_code = TARGET_SEGV_MAPERR;
                break;
            default:
                g_assert_not_reached();
            }
            force_sig_fault(si_signo, si_code, env->exception.vaddress);
            break;
        case EXCP_DEBUG:
        case EXCP_BKPT:
            force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_BRKPT, env->regs[15]);
            break;
        case EXCP_YIELD:
            break;
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        default:
            fprintf(stderr, "qemu: unhandled CPU exception 0x%x - aborting\n",
                    trapnr);
            cpu_dump_state(cs, stderr, 0);
            abort();
        }
        process_pending_signals(env);
    }
}

static inline void target_cpu_clone_regs(CPUARMState *env, target_ulong newsp)
{
    if (newsp) {
        env->regs[13] = newsp;
    }
    env->regs[0] = 0;
}

static inline void target_cpu_reset(CPUArchState *env)
{
}

#endif
