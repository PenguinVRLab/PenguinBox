/*
 *  qemu user cpu loop
 *
 *  Copyright (c) 2003-2008 Fabrice Bellard
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
#include "qemu/timer.h"
#include "user-internals.h"
#include "user/cpu_loop.h"
#include "signal-common.h"

static inline uint64_t cpu_ppc_get_tb(CPUPPCState *env)
{
    return cpu_get_host_ticks();
}

uint64_t cpu_ppc_load_tbl(CPUPPCState *env)
{
    return cpu_ppc_get_tb(env);
}

uint32_t cpu_ppc_load_tbu(CPUPPCState *env)
{
    return cpu_ppc_get_tb(env) >> 32;
}

uint64_t cpu_ppc_load_atbl(CPUPPCState *env)
{
    return cpu_ppc_get_tb(env);
}

uint32_t cpu_ppc_load_atbu(CPUPPCState *env)
{
    return cpu_ppc_get_tb(env) >> 32;
}

uint64_t cpu_ppc_load_vtb(CPUPPCState *env)
{
    return cpu_ppc_get_tb(env);
}

int ppc_dcr_read (ppc_dcr_t *dcr_env, int dcrn, uint32_t *valp)
{
    return -1;
}

int ppc_dcr_write (ppc_dcr_t *dcr_env, int dcrn, uint32_t val)
{
    return -1;
}

void cpu_loop(CPUPPCState *env)
{
    CPUState *cs = env_cpu(env);
    int trapnr, si_signo, si_code;
    target_ulong ret;

    for(;;) {
        bool arch_interrupt;

        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        qemu_process_cpu_events(cs);

        arch_interrupt = true;
        switch (trapnr) {
        case POWERPC_EXCP_NONE:
            break;
        case POWERPC_EXCP_CRITICAL:
            cpu_abort(cs, "Critical interrupt while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_MCHECK:
            cpu_abort(cs, "Machine check exception while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_DSI:
        case POWERPC_EXCP_ISI:
            force_sig_fault(TARGET_SIGSEGV, TARGET_SEGV_MAPERR,
                            env->spr[SPR_DAR]);
            break;
        case POWERPC_EXCP_EXTERNAL:
            cpu_abort(cs, "External interrupt while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_PROGRAM:
        case POWERPC_EXCP_HV_EMU:
            switch (env->error_code & ~0xF) {
            case POWERPC_EXCP_FP:
                si_signo = TARGET_SIGFPE;
                switch (env->error_code & 0xF) {
                case POWERPC_EXCP_FP_OX:
                    si_code = TARGET_FPE_FLTOVF;
                    break;
                case POWERPC_EXCP_FP_UX:
                    si_code = TARGET_FPE_FLTUND;
                    break;
                case POWERPC_EXCP_FP_ZX:
                case POWERPC_EXCP_FP_VXZDZ:
                    si_code = TARGET_FPE_FLTDIV;
                    break;
                case POWERPC_EXCP_FP_XX:
                    si_code = TARGET_FPE_FLTRES;
                    break;
                case POWERPC_EXCP_FP_VXSOFT:
                    si_code = TARGET_FPE_FLTINV;
                    break;
                case POWERPC_EXCP_FP_VXSNAN:
                case POWERPC_EXCP_FP_VXISI:
                case POWERPC_EXCP_FP_VXIDI:
                case POWERPC_EXCP_FP_VXIMZ:
                case POWERPC_EXCP_FP_VXVC:
                case POWERPC_EXCP_FP_VXSQRT:
                case POWERPC_EXCP_FP_VXCVI:
                    si_code = TARGET_FPE_FLTSUB;
                    break;
                default:
                    EXCP_DUMP(env, "Unknown floating point exception (%02x)\n",
                              env->error_code);
                    si_code = 0;
                    break;
                }
                break;
            case POWERPC_EXCP_INVAL:
                si_signo = TARGET_SIGILL;
                switch (env->error_code & 0xF) {
                case POWERPC_EXCP_INVAL_INVAL:
                    si_code = TARGET_ILL_ILLOPC;
                    break;
                case POWERPC_EXCP_INVAL_LSWX:
                    si_code = TARGET_ILL_ILLOPN;
                    break;
                case POWERPC_EXCP_INVAL_SPR:
                    si_code = TARGET_ILL_PRVREG;
                    break;
                case POWERPC_EXCP_INVAL_FP:
                    si_code = TARGET_ILL_COPROC;
                    break;
                default:
                    EXCP_DUMP(env, "Unknown invalid operation (%02x)\n",
                              env->error_code & 0xF);
                    si_code = TARGET_ILL_ILLADR;
                    break;
                }
                break;
            case POWERPC_EXCP_PRIV:
                si_signo = TARGET_SIGILL;
                switch (env->error_code & 0xF) {
                case POWERPC_EXCP_PRIV_OPC:
                    si_code = TARGET_ILL_PRVOPC;
                    break;
                case POWERPC_EXCP_PRIV_REG:
                    si_code = TARGET_ILL_PRVREG;
                    break;
                default:
                    EXCP_DUMP(env, "Unknown privilege violation (%02x)\n",
                              env->error_code & 0xF);
                    si_code = TARGET_ILL_PRVOPC;
                    break;
                }
                break;
            case POWERPC_EXCP_TRAP:
                si_signo = TARGET_SIGTRAP;
                si_code = TARGET_TRAP_BRKPT;
                break;
            default:
                cpu_abort(cs, "Unknown program exception (%02x)\n",
                          env->error_code);
                break;
            }
            force_sig_fault(si_signo, si_code, env->nip);
            break;
        case POWERPC_EXCP_FPU:
        case POWERPC_EXCP_APU:
        case POWERPC_EXCP_SPEU:
        case POWERPC_EXCP_VPU:
            force_sig_fault(TARGET_SIGILL, TARGET_ILL_COPROC, env->nip);
            break;
        case POWERPC_EXCP_SYSCALL:
        case POWERPC_EXCP_SYSCALL_VECTORED:
            cpu_abort(cs, "Syscall exception while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_DECR:
            cpu_abort(cs, "Decrementer interrupt while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_FIT:
            cpu_abort(cs, "Fix interval timer interrupt while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_WDT:
            cpu_abort(cs, "Watchdog timer interrupt while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_DTLB:
            cpu_abort(cs, "Data TLB exception while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_ITLB:
            cpu_abort(cs, "Instruction TLB exception while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_EFPDI:
            cpu_abort(cs, "Embedded floating-point data IRQ not handled\n");
            break;
        case POWERPC_EXCP_EFPRI:
            cpu_abort(cs, "Embedded floating-point round IRQ not handled\n");
            break;
        case POWERPC_EXCP_EPERFM:
            cpu_abort(cs, "Performance monitor exception not handled\n");
            break;
        case POWERPC_EXCP_DOORI:
            cpu_abort(cs, "Doorbell interrupt while in user mode. "
                       "Aborting\n");
            break;
        case POWERPC_EXCP_DOORCI:
            cpu_abort(cs, "Doorbell critical interrupt while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_RESET:
            cpu_abort(cs, "Reset interrupt while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_DSEG:
            cpu_abort(cs, "Data segment exception while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_ISEG:
            cpu_abort(cs, "Instruction segment exception "
                      "while in user mode. Aborting\n");
            break;
        case POWERPC_EXCP_HDECR:
            cpu_abort(cs, "Hypervisor decrementer interrupt "
                      "while in user mode. Aborting\n");
            break;
        case POWERPC_EXCP_TRACE:
            break;
        case POWERPC_EXCP_HDSI:
            cpu_abort(cs, "Hypervisor data storage exception "
                      "while in user mode. Aborting\n");
            break;
        case POWERPC_EXCP_HISI:
            cpu_abort(cs, "Hypervisor instruction storage exception "
                      "while in user mode. Aborting\n");
            break;
        case POWERPC_EXCP_HDSEG:
            cpu_abort(cs, "Hypervisor data segment exception "
                      "while in user mode. Aborting\n");
            break;
        case POWERPC_EXCP_HISEG:
            cpu_abort(cs, "Hypervisor instruction segment exception "
                      "while in user mode. Aborting\n");
            break;
        case POWERPC_EXCP_PIT:
            cpu_abort(cs, "Programmable interval timer interrupt "
                      "while in user mode. Aborting\n");
            break;
        case POWERPC_EXCP_EMUL:
            cpu_abort(cs, "Emulation trap exception not handled\n");
            break;
        case POWERPC_EXCP_IFTLB:
            cpu_abort(cs, "Instruction fetch TLB exception "
                      "while in user-mode. Aborting");
            break;
        case POWERPC_EXCP_DLTLB:
            cpu_abort(cs, "Data load TLB exception while in user-mode. "
                      "Aborting");
            break;
        case POWERPC_EXCP_DSTLB:
            cpu_abort(cs, "Data store TLB exception while in user-mode. "
                      "Aborting");
            break;
        case POWERPC_EXCP_FPA:
            cpu_abort(cs, "Floating-point assist exception not handled\n");
            break;
        case POWERPC_EXCP_IABR:
            cpu_abort(cs, "Instruction address breakpoint exception "
                      "not handled\n");
            break;
        case POWERPC_EXCP_SMI:
            cpu_abort(cs, "System management interrupt while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_THERM:
            cpu_abort(cs, "Thermal interrupt interrupt while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_PERFM:
            cpu_abort(cs, "Performance monitor exception not handled\n");
            break;
        case POWERPC_EXCP_VPUA:
            cpu_abort(cs, "Vector assist exception not handled\n");
            break;
        case POWERPC_EXCP_SOFTP:
            cpu_abort(cs, "Soft patch exception not handled\n");
            break;
        case POWERPC_EXCP_MAINT:
            cpu_abort(cs, "Maintenance exception while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_SYSCALL_USER:
            env->crf[0] &= ~0x1;
            env->nip += 4;
            ret = do_syscall(env, env->gpr[0], env->gpr[3], env->gpr[4],
                             env->gpr[5], env->gpr[6], env->gpr[7],
                             env->gpr[8], 0, 0);
            if (ret == -QEMU_ERESTARTSYS) {
                env->nip -= 4;
                break;
            }
            if (ret == (target_ulong)(-QEMU_ESIGRETURN)) {
                break;
            }
            if (ret > (target_ulong)(-515)) {
                env->crf[0] |= 0x1;
                ret = -ret;
            }
            env->gpr[3] = ret;
            break;
        case EXCP_DEBUG:
            force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_BRKPT, env->nip);
            break;
        case EXCP_INTERRUPT:
            break;
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            arch_interrupt = false;
            break;
        default:
            cpu_abort(cs, "Unknown exception 0x%x. Aborting\n", trapnr);
            break;
        }
        process_pending_signals(env);

        if (arch_interrupt) {
            env->reserve_addr = -1;
        }
    }
}

void init_main_thread(CPUState *cs, struct image_info *info)
{
    CPUArchState *env = cpu_env(cs);
    abi_ptr entry = info->entry;

    env->gpr[1] = info->start_stack;

#ifdef TARGET_PPC64
    if (get_ppc64_abi(info) < 2) {
        uint64_t val;
        get_user_u64(val, entry + 8);
        env->gpr[2] = val + info->load_bias;
        get_user_u64(val, entry);
        entry = val + info->load_bias;
    } else {
        env->gpr[12] = entry;
    }

    int flag = (env->insns_flags2 & PPC2_BOOKE206) ? MSR_CM : MSR_SF;
#if defined(TARGET_ABI32)
    ppc_store_msr(env, env->msr & ~((target_ulong)1 << flag));
#else
    ppc_store_msr(env, env->msr | (target_ulong)1 << flag);
#endif
#endif

    env->nip = entry;
}
