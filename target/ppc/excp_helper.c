/*
 *  PowerPC exception emulation helpers for QEMU.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
#include "qemu/main-loop.h"
#include "qemu/log.h"
#include "system/memory.h"
#include "system/tcg.h"
#include "system/system.h"
#include "system/runstate.h"
#include "cpu.h"
#include "internal.h"
#include "helper_regs.h"
#include "hw/ppc/ppc.h"
#include "qemu/plugin.h"

#include "trace.h"

#ifndef CONFIG_USER_ONLY

static const char *powerpc_excp_name(int excp)
{
    switch (excp) {
    case POWERPC_EXCP_CRITICAL: return "CRITICAL";
    case POWERPC_EXCP_MCHECK:   return "MCHECK";
    case POWERPC_EXCP_DSI:      return "DSI";
    case POWERPC_EXCP_ISI:      return "ISI";
    case POWERPC_EXCP_EXTERNAL: return "EXTERNAL";
    case POWERPC_EXCP_ALIGN:    return "ALIGN";
    case POWERPC_EXCP_PROGRAM:  return "PROGRAM";
    case POWERPC_EXCP_FPU:      return "FPU";
    case POWERPC_EXCP_SYSCALL:  return "SYSCALL";
    case POWERPC_EXCP_APU:      return "APU";
    case POWERPC_EXCP_DECR:     return "DECR";
    case POWERPC_EXCP_FIT:      return "FIT";
    case POWERPC_EXCP_WDT:      return "WDT";
    case POWERPC_EXCP_DTLB:     return "DTLB";
    case POWERPC_EXCP_ITLB:     return "ITLB";
    case POWERPC_EXCP_DEBUG:    return "DEBUG";
    case POWERPC_EXCP_SPEU:     return "SPEU";
    case POWERPC_EXCP_EFPDI:    return "EFPDI";
    case POWERPC_EXCP_EFPRI:    return "EFPRI";
    case POWERPC_EXCP_EPERFM:   return "EPERFM";
    case POWERPC_EXCP_DOORI:    return "DOORI";
    case POWERPC_EXCP_DOORCI:   return "DOORCI";
    case POWERPC_EXCP_GDOORI:   return "GDOORI";
    case POWERPC_EXCP_GDOORCI:  return "GDOORCI";
    case POWERPC_EXCP_HYPPRIV:  return "HYPPRIV";
    case POWERPC_EXCP_RESET:    return "RESET";
    case POWERPC_EXCP_DSEG:     return "DSEG";
    case POWERPC_EXCP_ISEG:     return "ISEG";
    case POWERPC_EXCP_HDECR:    return "HDECR";
    case POWERPC_EXCP_TRACE:    return "TRACE";
    case POWERPC_EXCP_HDSI:     return "HDSI";
    case POWERPC_EXCP_HISI:     return "HISI";
    case POWERPC_EXCP_HDSEG:    return "HDSEG";
    case POWERPC_EXCP_HISEG:    return "HISEG";
    case POWERPC_EXCP_VPU:      return "VPU";
    case POWERPC_EXCP_PIT:      return "PIT";
    case POWERPC_EXCP_EMUL:     return "EMUL";
    case POWERPC_EXCP_IFTLB:    return "IFTLB";
    case POWERPC_EXCP_DLTLB:    return "DLTLB";
    case POWERPC_EXCP_DSTLB:    return "DSTLB";
    case POWERPC_EXCP_FPA:      return "FPA";
    case POWERPC_EXCP_DABR:     return "DABR";
    case POWERPC_EXCP_IABR:     return "IABR";
    case POWERPC_EXCP_SMI:      return "SMI";
    case POWERPC_EXCP_PERFM:    return "PERFM";
    case POWERPC_EXCP_THERM:    return "THERM";
    case POWERPC_EXCP_VPUA:     return "VPUA";
    case POWERPC_EXCP_SOFTP:    return "SOFTP";
    case POWERPC_EXCP_MAINT:    return "MAINT";
    case POWERPC_EXCP_MEXTBR:   return "MEXTBR";
    case POWERPC_EXCP_NMEXTBR:  return "NMEXTBR";
    case POWERPC_EXCP_ITLBE:    return "ITLBE";
    case POWERPC_EXCP_DTLBE:    return "DTLBE";
    case POWERPC_EXCP_VSXU:     return "VSXU";
    case POWERPC_EXCP_FU:       return "FU";
    case POWERPC_EXCP_HV_EMU:   return "HV_EMU";
    case POWERPC_EXCP_HV_MAINT: return "HV_MAINT";
    case POWERPC_EXCP_HV_FU:    return "HV_FU";
    case POWERPC_EXCP_SDOOR:    return "SDOOR";
    case POWERPC_EXCP_SDOOR_HV: return "SDOOR_HV";
    case POWERPC_EXCP_HVIRT:    return "HVIRT";
    case POWERPC_EXCP_SYSCALL_VECTORED: return "SYSCALL_VECTORED";
    default:
        g_assert_not_reached();
    }
}

static void dump_syscall(CPUPPCState *env)
{
    qemu_log_mask(CPU_LOG_INT, "syscall r0=%016" PRIx64
                  " r3=%016" PRIx64 " r4=%016" PRIx64 " r5=%016" PRIx64
                  " r6=%016" PRIx64 " r7=%016" PRIx64 " r8=%016" PRIx64
                  " nip=" TARGET_FMT_lx "\n",
                  ppc_dump_gpr(env, 0), ppc_dump_gpr(env, 3),
                  ppc_dump_gpr(env, 4), ppc_dump_gpr(env, 5),
                  ppc_dump_gpr(env, 6), ppc_dump_gpr(env, 7),
                  ppc_dump_gpr(env, 8), env->nip);
}

static void dump_hcall(CPUPPCState *env)
{
    qemu_log_mask(CPU_LOG_INT, "hypercall r3=%016" PRIx64
                  " r4=%016" PRIx64 " r5=%016" PRIx64 " r6=%016" PRIx64
                  " r7=%016" PRIx64 " r8=%016" PRIx64 " r9=%016" PRIx64
                  " r10=%016" PRIx64 " r11=%016" PRIx64 " r12=%016" PRIx64
                  " nip=" TARGET_FMT_lx "\n",
                  ppc_dump_gpr(env, 3), ppc_dump_gpr(env, 4),
                  ppc_dump_gpr(env, 5), ppc_dump_gpr(env, 6),
                  ppc_dump_gpr(env, 7), ppc_dump_gpr(env, 8),
                  ppc_dump_gpr(env, 9), ppc_dump_gpr(env, 10),
                  ppc_dump_gpr(env, 11), ppc_dump_gpr(env, 12),
                  env->nip);
}

static void ppc_excp_debug_sw_tlb(CPUPPCState *env, int excp)
{
    const char *es;
    target_ulong *miss, *cmp;
    int en;

    if (!qemu_loglevel_mask(CPU_LOG_MMU)) {
        return;
    }

    if (excp == POWERPC_EXCP_IFTLB) {
        es = "I";
        en = 'I';
        miss = &env->spr[SPR_IMISS];
        cmp = &env->spr[SPR_ICMP];
    } else {
        if (excp == POWERPC_EXCP_DLTLB) {
            es = "DL";
        } else {
            es = "DS";
        }
        en = 'D';
        miss = &env->spr[SPR_DMISS];
        cmp = &env->spr[SPR_DCMP];
    }
    qemu_log("6xx %sTLB miss: %cM " TARGET_FMT_lx " %cC "
             TARGET_FMT_lx " H1 " TARGET_FMT_lx " H2 "
             TARGET_FMT_lx " %08x\n", es, en, *miss, en, *cmp,
             env->spr[SPR_HASH1], env->spr[SPR_HASH2],
             env->error_code);
}

#ifdef TARGET_PPC64
static int powerpc_reset_wakeup(CPUPPCState *env, int excp, target_ulong *msr)
{
    env->resume_as_sreset = false;

    *msr |= SRR1_WS_NOLOSS;

    if (excp == POWERPC_EXCP_MCHECK) {
        return excp;
    }
    switch (excp) {
    case POWERPC_EXCP_RESET:
        *msr |= SRR1_WAKERESET;
        break;
    case POWERPC_EXCP_EXTERNAL:
        *msr |= SRR1_WAKEEE;
        break;
    case POWERPC_EXCP_DECR:
        *msr |= SRR1_WAKEDEC;
        break;
    case POWERPC_EXCP_SDOOR:
        *msr |= SRR1_WAKEDBELL;
        break;
    case POWERPC_EXCP_SDOOR_HV:
        *msr |= SRR1_WAKEHDBELL;
        break;
    case POWERPC_EXCP_HV_MAINT:
        *msr |= SRR1_WAKEHMI;
        break;
    case POWERPC_EXCP_HVIRT:
        *msr |= SRR1_WAKEHVI;
        break;
    default:
        cpu_abort(env_cpu(env),
                  "Unsupported exception %d in Power Save mode\n", excp);
    }
    return POWERPC_EXCP_RESET;
}

static void ppc_excp_apply_ail(PowerPCCPU *cpu, int excp, target_ulong msr,
                               target_ulong *new_msr, target_ulong *vector)
{
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    CPUPPCState *env = &cpu->env;
    bool mmu_all_on = ((msr >> MSR_IR) & 1) && ((msr >> MSR_DR) & 1);
    bool hv_escalation = !(msr & MSR_HVB) && (*new_msr & MSR_HVB);
    int ail = 0;

    if (excp == POWERPC_EXCP_MCHECK ||
        excp == POWERPC_EXCP_RESET ||
        excp == POWERPC_EXCP_HV_MAINT) {
        return;
    }

    if (!(pcc->lpcr_mask & LPCR_AIL)) {
        return;
    }

    if (!(pcc->lpcr_mask & LPCR_HAIL)) {
        if (!mmu_all_on) {
            return;
        }
        if (hv_escalation && !(env->spr[SPR_LPCR] & LPCR_HR)) {
            return;
        }

        ail = (env->spr[SPR_LPCR] & LPCR_AIL) >> LPCR_AIL_SHIFT;
        if (ail == 0 || ail == 1) {
            return;
        }

    } else {
        if (!mmu_all_on && !hv_escalation) {
            return;
        }

        if (*new_msr & MSR_HVB) {
            if (!(env->spr[SPR_LPCR] & LPCR_HAIL)) {
                return;
            }
            ail = 3;
        } else {
            ail = (env->spr[SPR_LPCR] & LPCR_AIL) >> LPCR_AIL_SHIFT;
        }
        if (ail == 0 || ail == 1 || ail == 2) {
            return;
        }
    }

    *new_msr |= (1 << MSR_IR) | (1 << MSR_DR);

    if (excp != POWERPC_EXCP_SYSCALL_VECTORED) {
        if (ail == 2) {
            *vector |= 0x0000000000018000ull;
        } else if (ail == 3) {
            *vector |= 0xc000000000004000ull;
        }
    } else {
        if (ail == 3) {
            *vector &= ~0x0000000000017000ull;
            *vector |= 0xc000000000003000ull;
        }
    }
}
#endif

static void powerpc_reset_excp_state(PowerPCCPU *cpu)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;

    cs->exception_index = POWERPC_EXCP_NONE;
    env->error_code = 0;
}

static void powerpc_set_excp_state(PowerPCCPU *cpu, target_ulong vector,
                                   target_ulong msr)
{
    CPUPPCState *env = &cpu->env;

    assert((msr & env->msr_mask) == msr);

    env->nip = vector;
    env->msr = msr;
    hreg_compute_hflags(env);
    ppc_maybe_interrupt(env);

    powerpc_reset_excp_state(cpu);

    check_tlb_flush(env, false);

    env->reserve_addr = -1;
}

static void powerpc_mcheck_checkstop(CPUPPCState *env)
{
    if (FIELD_EX64(env->msr, MSR, ME)) {
        return;
    }
    assert(tcg_enabled());
    powerpc_checkstop(env, "machine check with MSR[ME]=0");
}

static void powerpc_do_plugin_vcpu_interrupt_cb(CPUState *cs, int excp,
                                                uint64_t from)
{
    switch (excp) {
    case POWERPC_EXCP_NONE:
        break;
    case POWERPC_EXCP_FIT:
    case POWERPC_EXCP_WDT:
    case POWERPC_EXCP_PIT:
    case POWERPC_EXCP_SMI:
    case POWERPC_EXCP_PERFM:
    case POWERPC_EXCP_THERM:
        qemu_plugin_vcpu_interrupt_cb(cs, from);
        break;
    default:
        qemu_plugin_vcpu_exception_cb(cs, from);
    }
}

static void powerpc_excp_40x(PowerPCCPU *cpu, int excp)
{
    CPUPPCState *env = &cpu->env;
    target_ulong msr, new_msr, vector;
    int srr0 = SPR_SRR0, srr1 = SPR_SRR1;
    uint64_t last_pc = env->nip;

    msr = env->msr & ~0x783f0000ULL;

    new_msr = env->msr & (((target_ulong)1 << MSR_ME));

    if (excp == POWERPC_EXCP_HV_EMU) {
        excp = POWERPC_EXCP_PROGRAM;
    }

    vector = env->excp_vectors[excp];
    if (vector == (target_ulong)-1ULL) {
        cpu_abort(env_cpu(env),
                  "Raised an exception without defined vector %d\n", excp);
    }
    vector |= env->excp_prefix;

    switch (excp) {
    case POWERPC_EXCP_CRITICAL:
        srr0 = SPR_40x_SRR2;
        srr1 = SPR_40x_SRR3;
        break;
    case POWERPC_EXCP_MCHECK:
        powerpc_mcheck_checkstop(env);
        new_msr &= ~((target_ulong)1 << MSR_ME);
        srr0 = SPR_40x_SRR2;
        srr1 = SPR_40x_SRR3;
        break;
    case POWERPC_EXCP_DSI:
        trace_ppc_excp_dsi(env->spr[SPR_40x_ESR], env->spr[SPR_40x_DEAR]);
        break;
    case POWERPC_EXCP_ISI:
        trace_ppc_excp_isi(msr, env->nip);
        break;
    case POWERPC_EXCP_EXTERNAL:
        break;
    case POWERPC_EXCP_ALIGN:
        break;
    case POWERPC_EXCP_PROGRAM:
        switch (env->error_code & ~0xF) {
        case POWERPC_EXCP_FP:
            if (!FIELD_EX64_FE(env->msr) || !FIELD_EX64(env->msr, MSR, FP)) {
                trace_ppc_excp_fp_ignore();
                powerpc_reset_excp_state(cpu);
                qemu_plugin_vcpu_exception_cb(env_cpu(env), last_pc);
                return;
            }
            env->spr[SPR_40x_ESR] = ESR_FP;
            break;
        case POWERPC_EXCP_INVAL:
            trace_ppc_excp_inval(env->nip);
            env->spr[SPR_40x_ESR] = ESR_PIL;
            break;
        case POWERPC_EXCP_PRIV:
            env->spr[SPR_40x_ESR] = ESR_PPR;
            break;
        case POWERPC_EXCP_TRAP:
            env->spr[SPR_40x_ESR] = ESR_PTR;
            break;
        default:
            cpu_abort(env_cpu(env), "Invalid program exception %d. Aborting\n",
                      env->error_code);
            break;
        }
        break;
    case POWERPC_EXCP_SYSCALL:
        dump_syscall(env);

        env->nip += 4;
        break;
    case POWERPC_EXCP_FIT:
        trace_ppc_excp_print("FIT");
        break;
    case POWERPC_EXCP_WDT:
        trace_ppc_excp_print("WDT");
        break;
    case POWERPC_EXCP_DTLB:
    case POWERPC_EXCP_ITLB:
        break;
    case POWERPC_EXCP_PIT:
        trace_ppc_excp_print("PIT");
        break;
    case POWERPC_EXCP_DEBUG:
        cpu_abort(env_cpu(env), "%s exception not implemented\n",
                  powerpc_excp_name(excp));
        break;
    default:
        cpu_abort(env_cpu(env), "Invalid PowerPC exception %d. Aborting\n",
                  excp);
        break;
    }

    env->spr[srr0] = env->nip;
    env->spr[srr1] = msr;
    powerpc_set_excp_state(cpu, vector, new_msr);
    powerpc_do_plugin_vcpu_interrupt_cb(env_cpu(env), excp, last_pc);
}

static void powerpc_excp_6xx(PowerPCCPU *cpu, int excp)
{
    CPUPPCState *env = &cpu->env;
    target_ulong msr, new_msr, vector;
    uint64_t last_pc = env->nip;

    msr = env->msr & ~0x783f0000ULL;

    new_msr = env->msr & ((target_ulong)1 << MSR_ME);

    if (excp == POWERPC_EXCP_HV_EMU) {
        excp = POWERPC_EXCP_PROGRAM;
    }

    vector = env->excp_vectors[excp];
    if (vector == (target_ulong)-1ULL) {
        cpu_abort(env_cpu(env),
                  "Raised an exception without defined vector %d\n", excp);
    }
    vector |= env->excp_prefix;

    switch (excp) {
    case POWERPC_EXCP_CRITICAL:
        break;
    case POWERPC_EXCP_MCHECK:
        powerpc_mcheck_checkstop(env);
        new_msr &= ~((target_ulong)1 << MSR_ME);
        break;
    case POWERPC_EXCP_DSI:
        trace_ppc_excp_dsi(env->spr[SPR_DSISR], env->spr[SPR_DAR]);
        break;
    case POWERPC_EXCP_ISI:
        trace_ppc_excp_isi(msr, env->nip);
        msr |= env->error_code;
        break;
    case POWERPC_EXCP_EXTERNAL:
        break;
    case POWERPC_EXCP_ALIGN:
        env->spr[SPR_DSISR] |= (env->error_code & 0x03FF0000) >> 16;
        break;
    case POWERPC_EXCP_PROGRAM:
        switch (env->error_code & ~0xF) {
        case POWERPC_EXCP_FP:
            if (!FIELD_EX64_FE(env->msr) || !FIELD_EX64(env->msr, MSR, FP)) {
                trace_ppc_excp_fp_ignore();
                powerpc_reset_excp_state(cpu);
                qemu_plugin_vcpu_exception_cb(env_cpu(env), last_pc);
                return;
            }
            msr |= 0x00100000;
            break;
        case POWERPC_EXCP_INVAL:
            trace_ppc_excp_inval(env->nip);
            msr |= 0x00080000;
            break;
        case POWERPC_EXCP_PRIV:
            msr |= 0x00040000;
            break;
        case POWERPC_EXCP_TRAP:
            msr |= 0x00020000;
            break;
        default:
            cpu_abort(env_cpu(env), "Invalid program exception %d. Aborting\n",
                      env->error_code);
            break;
        }
        break;
    case POWERPC_EXCP_SYSCALL:
        dump_syscall(env);

        env->nip += 4;
        break;
    case POWERPC_EXCP_FPU:
    case POWERPC_EXCP_DECR:
        break;
    case POWERPC_EXCP_DTLB:
    case POWERPC_EXCP_ITLB:
        break;
    case POWERPC_EXCP_RESET:
        if (FIELD_EX64(env->msr, MSR, POW)) {
            cpu_abort(env_cpu(env),
                      "Trying to deliver power-saving system reset exception "
                      "%d with no HV support\n", excp);
        }
        break;
    case POWERPC_EXCP_TRACE:
        break;
    case POWERPC_EXCP_IFTLB:
    case POWERPC_EXCP_DLTLB:
    case POWERPC_EXCP_DSTLB:
        if (!(new_msr & ((target_ulong)1 << MSR_TGPR))) {
            new_msr |= (target_ulong)1 << MSR_TGPR;
            hreg_swap_gpr_tgpr(env);
        }

        ppc_excp_debug_sw_tlb(env, excp);

        msr |= env->crf[0] << 28;
        msr |= env->error_code;
        msr |= ((env->last_way + 1) & (env->nb_ways - 1)) << 17;
        break;
    case POWERPC_EXCP_FPA:
    case POWERPC_EXCP_DABR:
    case POWERPC_EXCP_IABR:
    case POWERPC_EXCP_SMI:
    case POWERPC_EXCP_MEXTBR:
    case POWERPC_EXCP_NMEXTBR:
        cpu_abort(env_cpu(env), "%s exception not implemented\n",
                  powerpc_excp_name(excp));
        break;
    default:
        cpu_abort(env_cpu(env), "Invalid PowerPC exception %d. Aborting\n",
                  excp);
        break;
    }

    if (ppc_interrupts_little_endian(cpu, !!(new_msr & MSR_HVB))) {
        new_msr |= (target_ulong)1 << MSR_LE;
    }
    env->spr[SPR_SRR0] = env->nip;
    env->spr[SPR_SRR1] = msr;
    powerpc_set_excp_state(cpu, vector, new_msr);
    powerpc_do_plugin_vcpu_interrupt_cb(env_cpu(env), excp, last_pc);
}

static void powerpc_excp_7xx(PowerPCCPU *cpu, int excp)
{
    CPUPPCState *env = &cpu->env;
    target_ulong msr, new_msr, vector;
    uint64_t last_pc = env->nip;

    msr = env->msr & ~0x783f0000ULL;

    new_msr = env->msr & ((target_ulong)1 << MSR_ME);

    if (excp == POWERPC_EXCP_HV_EMU) {
        excp = POWERPC_EXCP_PROGRAM;
    }

    vector = env->excp_vectors[excp];
    if (vector == (target_ulong)-1ULL) {
        cpu_abort(env_cpu(env),
                  "Raised an exception without defined vector %d\n", excp);
    }
    vector |= env->excp_prefix;

    switch (excp) {
    case POWERPC_EXCP_MCHECK:
        powerpc_mcheck_checkstop(env);
        new_msr &= ~((target_ulong)1 << MSR_ME);
        break;
    case POWERPC_EXCP_DSI:
        trace_ppc_excp_dsi(env->spr[SPR_DSISR], env->spr[SPR_DAR]);
        break;
    case POWERPC_EXCP_ISI:
        trace_ppc_excp_isi(msr, env->nip);
        msr |= env->error_code;
        break;
    case POWERPC_EXCP_EXTERNAL:
        break;
    case POWERPC_EXCP_ALIGN:
        env->spr[SPR_DSISR] |= (env->error_code & 0x03FF0000) >> 16;
        break;
    case POWERPC_EXCP_PROGRAM:
        switch (env->error_code & ~0xF) {
        case POWERPC_EXCP_FP:
            if (!FIELD_EX64_FE(env->msr) || !FIELD_EX64(env->msr, MSR, FP)) {
                trace_ppc_excp_fp_ignore();
                powerpc_reset_excp_state(cpu);
                qemu_plugin_vcpu_exception_cb(env_cpu(env), last_pc);
                return;
            }
            msr |= 0x00100000;
            break;
        case POWERPC_EXCP_INVAL:
            trace_ppc_excp_inval(env->nip);
            msr |= 0x00080000;
            break;
        case POWERPC_EXCP_PRIV:
            msr |= 0x00040000;
            break;
        case POWERPC_EXCP_TRAP:
            msr |= 0x00020000;
            break;
        default:
            cpu_abort(env_cpu(env), "Invalid program exception %d. Aborting\n",
                      env->error_code);
            break;
        }
        break;
    case POWERPC_EXCP_SYSCALL:
    {
        int lev = env->error_code;

        if (lev == 1 && cpu->vhyp) {
            dump_hcall(env);
        } else {
            dump_syscall(env);
        }

        env->nip += 4;

        if (lev == 1 && cpu->vhyp) {
            cpu->vhyp_class->hypercall(cpu->vhyp, cpu);
            powerpc_reset_excp_state(cpu);
            qemu_plugin_vcpu_hostcall_cb(env_cpu(env), last_pc);
            return;
        }

        break;
    }
    case POWERPC_EXCP_FPU:
    case POWERPC_EXCP_DECR:
        break;
    case POWERPC_EXCP_RESET:
        if (FIELD_EX64(env->msr, MSR, POW)) {
            cpu_abort(env_cpu(env),
                      "Trying to deliver power-saving system reset exception "
                      "%d with no HV support\n", excp);
        }
        break;
    case POWERPC_EXCP_TRACE:
        break;
    case POWERPC_EXCP_IFTLB:
    case POWERPC_EXCP_DLTLB:
    case POWERPC_EXCP_DSTLB:
        ppc_excp_debug_sw_tlb(env, excp);
        msr |= env->crf[0] << 28;
        msr |= env->error_code;
        msr |= ((env->last_way + 1) & (env->nb_ways - 1)) << 17;
        break;
    case POWERPC_EXCP_IABR:
    case POWERPC_EXCP_SMI:
    case POWERPC_EXCP_THERM:
    case POWERPC_EXCP_PERFM:
        cpu_abort(env_cpu(env), "%s exception not implemented\n",
                  powerpc_excp_name(excp));
        break;
    default:
        cpu_abort(env_cpu(env), "Invalid PowerPC exception %d. Aborting\n",
                  excp);
        break;
    }

    if (ppc_interrupts_little_endian(cpu, !!(new_msr & MSR_HVB))) {
        new_msr |= (target_ulong)1 << MSR_LE;
    }
    env->spr[SPR_SRR0] = env->nip;
    env->spr[SPR_SRR1] = msr;
    powerpc_set_excp_state(cpu, vector, new_msr);
    powerpc_do_plugin_vcpu_interrupt_cb(env_cpu(env), excp, last_pc);
}

static void powerpc_excp_74xx(PowerPCCPU *cpu, int excp)
{
    CPUPPCState *env = &cpu->env;
    target_ulong msr, new_msr, vector;
    uint64_t last_pc = env->nip;

    msr = env->msr & ~0x783f0000ULL;

    new_msr = env->msr & ((target_ulong)1 << MSR_ME);

    if (excp == POWERPC_EXCP_HV_EMU) {
        excp = POWERPC_EXCP_PROGRAM;
    }

    vector = env->excp_vectors[excp];
    if (vector == (target_ulong)-1ULL) {
        cpu_abort(env_cpu(env),
                  "Raised an exception without defined vector %d\n", excp);
    }
    vector |= env->excp_prefix;

    switch (excp) {
    case POWERPC_EXCP_MCHECK:
        powerpc_mcheck_checkstop(env);
        new_msr &= ~((target_ulong)1 << MSR_ME);
        break;
    case POWERPC_EXCP_DSI:
        trace_ppc_excp_dsi(env->spr[SPR_DSISR], env->spr[SPR_DAR]);
        break;
    case POWERPC_EXCP_ISI:
        trace_ppc_excp_isi(msr, env->nip);
        msr |= env->error_code;
        break;
    case POWERPC_EXCP_EXTERNAL:
        break;
    case POWERPC_EXCP_ALIGN:
        env->spr[SPR_DSISR] |= (env->error_code & 0x03FF0000) >> 16;
        break;
    case POWERPC_EXCP_PROGRAM:
        switch (env->error_code & ~0xF) {
        case POWERPC_EXCP_FP:
            if (!FIELD_EX64_FE(env->msr) || !FIELD_EX64(env->msr, MSR, FP)) {
                trace_ppc_excp_fp_ignore();
                powerpc_reset_excp_state(cpu);
                qemu_plugin_vcpu_exception_cb(env_cpu(env), last_pc);
                return;
            }
            msr |= 0x00100000;
            break;
        case POWERPC_EXCP_INVAL:
            trace_ppc_excp_inval(env->nip);
            msr |= 0x00080000;
            break;
        case POWERPC_EXCP_PRIV:
            msr |= 0x00040000;
            break;
        case POWERPC_EXCP_TRAP:
            msr |= 0x00020000;
            break;
        default:
            cpu_abort(env_cpu(env), "Invalid program exception %d. Aborting\n",
                      env->error_code);
            break;
        }
        break;
    case POWERPC_EXCP_SYSCALL:
    {
        int lev = env->error_code;

        if (lev == 1 && cpu->vhyp) {
            dump_hcall(env);
        } else {
            dump_syscall(env);
        }

        env->nip += 4;

        if (lev == 1 && cpu->vhyp) {
            cpu->vhyp_class->hypercall(cpu->vhyp, cpu);
            powerpc_reset_excp_state(cpu);
            qemu_plugin_vcpu_hostcall_cb(env_cpu(env), last_pc);
            return;
        }

        break;
    }
    case POWERPC_EXCP_FPU:
    case POWERPC_EXCP_DECR:
        break;
    case POWERPC_EXCP_RESET:
        if (FIELD_EX64(env->msr, MSR, POW)) {
            cpu_abort(env_cpu(env),
                      "Trying to deliver power-saving system reset "
                      "exception %d with no HV support\n", excp);
        }
        break;
    case POWERPC_EXCP_TRACE:
        break;
    case POWERPC_EXCP_VPU:
        break;
    case POWERPC_EXCP_IABR:
    case POWERPC_EXCP_SMI:
    case POWERPC_EXCP_THERM:
    case POWERPC_EXCP_PERFM:
    case POWERPC_EXCP_VPUA:
        cpu_abort(env_cpu(env), "%s exception not implemented\n",
                  powerpc_excp_name(excp));
        break;
    default:
        cpu_abort(env_cpu(env), "Invalid PowerPC exception %d. Aborting\n",
                  excp);
        break;
    }

    if (ppc_interrupts_little_endian(cpu, !!(new_msr & MSR_HVB))) {
        new_msr |= (target_ulong)1 << MSR_LE;
    }
    env->spr[SPR_SRR0] = env->nip;
    env->spr[SPR_SRR1] = msr;
    powerpc_set_excp_state(cpu, vector, new_msr);
    powerpc_do_plugin_vcpu_interrupt_cb(env_cpu(env), excp, last_pc);
}

static void powerpc_excp_ppe42(PowerPCCPU *cpu, int excp)
{
    CPUPPCState *env = &cpu->env;
    target_ulong msr, new_msr, vector;
    target_ulong mcs = PPE42_ISR_MCS_INSTRUCTION;
    bool promote_unmaskable;

    msr = env->msr;

    new_msr = env->msr & (((target_ulong)1 << MSR_ME) | R_MSR_SIBRC_MASK);

    if (excp == POWERPC_EXCP_HV_EMU) {
        excp = POWERPC_EXCP_PROGRAM;
    }

    promote_unmaskable = !(msr & ((target_ulong)1 << MSR_UIE));


    switch (excp) {
    case POWERPC_EXCP_MCHECK:
        break;
    case POWERPC_EXCP_DSI:
        trace_ppc_excp_dsi(env->spr[SPR_PPE42_ISR], env->spr[SPR_PPE42_EDR]);
        if (promote_unmaskable) {
            excp = POWERPC_EXCP_MCHECK;
            mcs = PPE42_ISR_MCS_DSI;
        }
        break;
    case POWERPC_EXCP_ISI:
        trace_ppc_excp_isi(msr, env->nip);
        if (promote_unmaskable) {
            excp = POWERPC_EXCP_MCHECK;
            mcs = PPE42_ISR_MCS_ISI;
        }
        break;
    case POWERPC_EXCP_EXTERNAL:
        break;
    case POWERPC_EXCP_ALIGN:
        if (promote_unmaskable) {
            excp = POWERPC_EXCP_MCHECK;
            mcs = PPE42_ISR_MCS_ALIGNMENT;
        }
        break;
    case POWERPC_EXCP_PROGRAM:
        if (promote_unmaskable) {
            excp = POWERPC_EXCP_MCHECK;
            mcs = PPE42_ISR_MCS_PROGRAM;
        }
        switch (env->error_code & ~0xF) {
        case POWERPC_EXCP_INVAL:
            trace_ppc_excp_inval(env->nip);
            env->spr[SPR_PPE42_ISR] &= ~((target_ulong)1 << PPE42_ISR_PTR);
            break;
        case POWERPC_EXCP_TRAP:
            env->spr[SPR_PPE42_ISR] |= ((target_ulong)1 << PPE42_ISR_PTR);
            break;
        default:
            cpu_abort(env_cpu(env), "Invalid program exception %d. Aborting\n",
                      env->error_code);
            break;
        }
#ifdef CONFIG_TCG
        env->spr[SPR_PPE42_EDR] = ppc_ldl_code(env, env->nip);
#endif
        break;
    case POWERPC_EXCP_DECR:
        break;
    case POWERPC_EXCP_FIT:
        trace_ppc_excp_print("FIT");
        break;
    case POWERPC_EXCP_WDT:
        trace_ppc_excp_print("WDT");
        break;
    case POWERPC_EXCP_RESET:
        new_msr &= ~((target_ulong)1 << MSR_ME);
        break;
    default:
        cpu_abort(env_cpu(env), "Invalid PPE42 exception %d. Aborting\n",
                  excp);
        break;
    }

    env->spr[SPR_SRR0] = env->nip;
    env->spr[SPR_SRR1] = msr;

    vector = env->excp_vectors[excp];
    if (vector == (target_ulong)-1ULL) {
        cpu_abort(env_cpu(env),
                  "Raised an exception without defined vector %d\n", excp);
    }
    vector |= env->spr[SPR_PPE42_IVPR];

    if (excp == POWERPC_EXCP_MCHECK) {
        env->spr[SPR_PPE42_ISR] &= ~R_PPE42_ISR_MCS_MASK;
        env->spr[SPR_PPE42_ISR] |= (mcs & R_PPE42_ISR_MCS_MASK);
        env->spr[SPR_PPE42_ISR] &= ~((target_ulong)1 << PPE42_ISR_MFE);

        powerpc_mcheck_checkstop(env);

        new_msr &= ~((target_ulong)1 << MSR_ME);
    }

    powerpc_set_excp_state(cpu, vector, new_msr);
}

static void powerpc_excp_booke(PowerPCCPU *cpu, int excp)
{
    CPUPPCState *env = &cpu->env;
    target_ulong msr, new_msr, vector;
    int srr0 = SPR_SRR0, srr1 = SPR_SRR1;
    uint64_t last_pc = env->nip;

    msr = env->msr;

    new_msr = env->msr & ((target_ulong)1 << MSR_ME);

    if (excp == POWERPC_EXCP_HV_EMU) {
        excp = POWERPC_EXCP_PROGRAM;
    }

#ifdef TARGET_PPC64
    if (excp == POWERPC_EXCP_VPU) {
        excp = POWERPC_EXCP_SPEU;
    }
#endif

    vector = env->excp_vectors[excp];
    if (vector == (target_ulong)-1ULL) {
        cpu_abort(env_cpu(env),
                  "Raised an exception without defined vector %d\n", excp);
    }
    vector |= env->excp_prefix;

    switch (excp) {
    case POWERPC_EXCP_CRITICAL:
        srr0 = SPR_BOOKE_CSRR0;
        srr1 = SPR_BOOKE_CSRR1;
        break;
    case POWERPC_EXCP_MCHECK:
        powerpc_mcheck_checkstop(env);
        new_msr &= ~((target_ulong)1 << MSR_ME);

        srr0 = SPR_BOOKE_MCSRR0;
        srr1 = SPR_BOOKE_MCSRR1;

        env->spr[SPR_BOOKE_CSRR0] = env->nip;
        env->spr[SPR_BOOKE_CSRR1] = msr;

        break;
    case POWERPC_EXCP_DSI:
        trace_ppc_excp_dsi(env->spr[SPR_BOOKE_ESR], env->spr[SPR_BOOKE_DEAR]);
        break;
    case POWERPC_EXCP_ISI:
        trace_ppc_excp_isi(msr, env->nip);
        break;
    case POWERPC_EXCP_EXTERNAL:
        if (env->mpic_proxy) {
            CPUState *cs = env_cpu(env);
            env->spr[SPR_BOOKE_EPR] = ldl_phys(cs->as, env->mpic_iack);
        }
        break;
    case POWERPC_EXCP_ALIGN:
        break;
    case POWERPC_EXCP_PROGRAM:
        switch (env->error_code & ~0xF) {
        case POWERPC_EXCP_FP:
            if (!FIELD_EX64_FE(env->msr) || !FIELD_EX64(env->msr, MSR, FP)) {
                trace_ppc_excp_fp_ignore();
                powerpc_reset_excp_state(cpu);
                qemu_plugin_vcpu_exception_cb(env_cpu(env), last_pc);
                return;
            }
            msr |= 0x00100000;
            env->spr[SPR_BOOKE_ESR] = ESR_FP;
            break;
        case POWERPC_EXCP_INVAL:
            trace_ppc_excp_inval(env->nip);
            msr |= 0x00080000;
            env->spr[SPR_BOOKE_ESR] = ESR_PIL;
            break;
        case POWERPC_EXCP_PRIV:
            msr |= 0x00040000;
            env->spr[SPR_BOOKE_ESR] = ESR_PPR;
            break;
        case POWERPC_EXCP_TRAP:
            msr |= 0x00020000;
            env->spr[SPR_BOOKE_ESR] = ESR_PTR;
            break;
        default:
            cpu_abort(env_cpu(env), "Invalid program exception %d. Aborting\n",
                      env->error_code);
            break;
        }
        break;
    case POWERPC_EXCP_SYSCALL:
        dump_syscall(env);

        env->nip += 4;
        break;
    case POWERPC_EXCP_FPU:
    case POWERPC_EXCP_APU:
    case POWERPC_EXCP_DECR:
        break;
    case POWERPC_EXCP_FIT:
        trace_ppc_excp_print("FIT");
        break;
    case POWERPC_EXCP_WDT:
        trace_ppc_excp_print("WDT");
        srr0 = SPR_BOOKE_CSRR0;
        srr1 = SPR_BOOKE_CSRR1;
        break;
    case POWERPC_EXCP_DTLB:
    case POWERPC_EXCP_ITLB:
        break;
    case POWERPC_EXCP_DEBUG:
        if (env->flags & POWERPC_FLAG_DE) {
            srr0 = SPR_BOOKE_DSRR0;
            srr1 = SPR_BOOKE_DSRR1;

            env->spr[SPR_BOOKE_CSRR0] = env->nip;
            env->spr[SPR_BOOKE_CSRR1] = msr;

        } else {
            cpu_abort(env_cpu(env),
                      "Debug exception triggered on unsupported model\n");
        }
        break;
    case POWERPC_EXCP_SPEU:
        env->spr[SPR_BOOKE_ESR] = ESR_SPV;
        break;
    case POWERPC_EXCP_DOORI:
        break;
    case POWERPC_EXCP_DOORCI:
        srr0 = SPR_BOOKE_CSRR0;
        srr1 = SPR_BOOKE_CSRR1;
        break;
    case POWERPC_EXCP_RESET:
        if (FIELD_EX64(env->msr, MSR, POW)) {
            cpu_abort(env_cpu(env),
                      "Trying to deliver power-saving system reset "
                      "exception %d with no HV support\n", excp);
        }
        break;
    case POWERPC_EXCP_EFPDI:
    case POWERPC_EXCP_EFPRI:
        cpu_abort(env_cpu(env), "%s exception not implemented\n",
                  powerpc_excp_name(excp));
        break;
    default:
        cpu_abort(env_cpu(env), "Invalid PowerPC exception %d. Aborting\n",
                  excp);
        break;
    }

#ifdef TARGET_PPC64
    if (env->spr[SPR_BOOKE_EPCR] & EPCR_ICM) {
        new_msr |= (target_ulong)1 << MSR_CM;
    } else {
        vector = (uint32_t)vector;
    }
#endif

    env->spr[srr0] = env->nip;
    env->spr[srr1] = msr;
    powerpc_set_excp_state(cpu, vector, new_msr);
    powerpc_do_plugin_vcpu_interrupt_cb(env_cpu(env), excp, last_pc);
}

static bool books_vhyp_promotes_external_to_hvirt(PowerPCCPU *cpu)
{
    if (cpu->vhyp) {
        return vhyp_cpu_in_nested(cpu);
    }
    return false;
}

#ifdef TARGET_PPC64
static bool books_vhyp_handles_hcall(PowerPCCPU *cpu)
{
    if (cpu->vhyp) {
        return !vhyp_cpu_in_nested(cpu);
    }
    return false;
}

static bool books_vhyp_handles_hv_excp(PowerPCCPU *cpu)
{
    if (cpu->vhyp) {
        return vhyp_cpu_in_nested(cpu);
    }
    return false;
}

#ifdef CONFIG_TCG
static bool is_prefix_insn(CPUPPCState *env, uint32_t insn)
{
    if (!(env->insns_flags2 & PPC2_ISA310)) {
        return false;
    }
    return ((insn & 0xfc000000) == 0x04000000);
}

static bool is_prefix_insn_excp(PowerPCCPU *cpu, int excp)
{
    CPUPPCState *env = &cpu->env;

    if (!(env->insns_flags2 & PPC2_ISA310)) {
        return false;
    }

    if (!tcg_enabled()) {
        return false;
    }

    switch (excp) {
    case POWERPC_EXCP_MCHECK:
        if (!(env->error_code & PPC_BIT(42))) {
            return false;
        }
        break;
    case POWERPC_EXCP_HDSI:
        if ((env->spr[SPR_HDSISR] & DSISR_PRTABLE_FAULT) &&
            (env->error_code == MMU_INST_FETCH)) {
            return false;
        }
        break;

    case POWERPC_EXCP_DSI:
    case POWERPC_EXCP_DSEG:
    case POWERPC_EXCP_ALIGN:
    case POWERPC_EXCP_PROGRAM:
    case POWERPC_EXCP_FPU:
    case POWERPC_EXCP_TRACE:
    case POWERPC_EXCP_HV_EMU:
    case POWERPC_EXCP_VPU:
    case POWERPC_EXCP_VSXU:
    case POWERPC_EXCP_FU:
    case POWERPC_EXCP_HV_FU:
        break;
    default:
        return false;
    }

    return is_prefix_insn(env, ppc_ldl_code(env, env->nip));
}
#else
static bool is_prefix_insn_excp(PowerPCCPU *cpu, int excp)
{
    return false;
}
#endif

static void powerpc_excp_books(PowerPCCPU *cpu, int excp)
{
    CPUPPCState *env = &cpu->env;
    target_ulong msr, new_msr, vector;
    int srr0 = SPR_SRR0, srr1 = SPR_SRR1, lev = -1;
    uint64_t last_pc = env->nip;

    msr = env->msr & ~0x783f0000ULL;

    new_msr = env->msr & (((target_ulong)1 << MSR_ME) | MSR_HVB);

    if (env->resume_as_sreset) {
        excp = powerpc_reset_wakeup(env, excp, &msr);
    }

    if (excp == POWERPC_EXCP_HV_EMU && !(env->msr_mask & MSR_HVB) &&
            !books_vhyp_handles_hv_excp(cpu)) {
        excp = POWERPC_EXCP_PROGRAM;
    }

    vector = env->excp_vectors[excp];
    if (vector == (target_ulong)-1ULL) {
        cpu_abort(env_cpu(env),
                  "Raised an exception without defined vector %d\n", excp);
    }
    vector |= env->excp_prefix;

    if (is_prefix_insn_excp(cpu, excp)) {
        msr |= PPC_BIT(34);
    }

    switch (excp) {
    case POWERPC_EXCP_MCHECK:
        powerpc_mcheck_checkstop(env);
        if (env->msr_mask & MSR_HVB) {
            new_msr |= (target_ulong)MSR_HVB;

            new_msr &= ~((target_ulong)1 << MSR_ME);
        }

        msr |= env->error_code;
        break;

    case POWERPC_EXCP_DSI:
        trace_ppc_excp_dsi(env->spr[SPR_DSISR], env->spr[SPR_DAR]);
        break;
    case POWERPC_EXCP_ISI:
        trace_ppc_excp_isi(msr, env->nip);
        msr |= env->error_code;
        break;
    case POWERPC_EXCP_EXTERNAL:
    {
        bool lpes0;

        if (!env->has_hv_mode) {
            break;
        }
        lpes0 = !!(env->spr[SPR_LPCR] & LPCR_LPES0);
        if (!lpes0) {
            new_msr |= (target_ulong)MSR_HVB;
            new_msr |= env->msr & ((target_ulong)1 << MSR_RI);
            srr0 = SPR_HSRR0;
            srr1 = SPR_HSRR1;
        }
        break;
    }
    case POWERPC_EXCP_ALIGN:
        if (!(env->insns_flags2 & PPC2_ISA300)) {
            env->spr[SPR_DSISR] |= (env->error_code & 0x03FF0000) >> 16;
        }
        break;
    case POWERPC_EXCP_PROGRAM:
        switch (env->error_code & ~0xF) {
        case POWERPC_EXCP_FP:
            if (!FIELD_EX64_FE(env->msr) || !FIELD_EX64(env->msr, MSR, FP)) {
                trace_ppc_excp_fp_ignore();
                powerpc_reset_excp_state(cpu);
                qemu_plugin_vcpu_exception_cb(env_cpu(env), last_pc);
                return;
            }
            msr |= 0x00100000;
            break;
        case POWERPC_EXCP_INVAL:
            trace_ppc_excp_inval(env->nip);
            msr |= 0x00080000;
            break;
        case POWERPC_EXCP_PRIV:
            msr |= 0x00040000;
            break;
        case POWERPC_EXCP_TRAP:
            msr |= 0x00020000;
            break;
        default:
            cpu_abort(env_cpu(env), "Invalid program exception %d. Aborting\n",
                      env->error_code);
            break;
        }
        break;
    case POWERPC_EXCP_SYSCALL:
        lev = env->error_code;

        if (lev == 1 && cpu->vhyp) {
            dump_hcall(env);
        } else {
            dump_syscall(env);
        }

        env->nip += 4;

        if (lev == 1 && books_vhyp_handles_hcall(cpu)) {
            cpu->vhyp_class->hypercall(cpu->vhyp, cpu);
            powerpc_reset_excp_state(cpu);
            qemu_plugin_vcpu_hostcall_cb(env_cpu(env), last_pc);
            return;
        }
        if (env->insns_flags2 & PPC2_ISA310) {
            msr |= lev << 20;
        }
        if (lev == 1) {
            new_msr |= (target_ulong)MSR_HVB;
        }
        break;
    case POWERPC_EXCP_SYSCALL_VECTORED:
        lev = env->error_code;
        dump_syscall(env);
        env->nip += 4;
        new_msr |= env->msr & ((target_ulong)1 << MSR_EE);
        new_msr |= env->msr & ((target_ulong)1 << MSR_RI);

        vector += lev * 0x20;

        env->lr = env->nip;
        env->ctr = msr;
        break;
    case POWERPC_EXCP_FPU:
    case POWERPC_EXCP_DECR:
        break;
    case POWERPC_EXCP_RESET:
        if (FIELD_EX64(env->msr, MSR, POW)) {
            msr |= 0x10000;
            new_msr |= ((target_ulong)1 << MSR_ME);
        }
        if (env->msr_mask & MSR_HVB) {
            new_msr |= (target_ulong)MSR_HVB;
        } else {
            if (FIELD_EX64(env->msr, MSR, POW)) {
                cpu_abort(env_cpu(env),
                          "Trying to deliver power-saving system reset "
                          "exception %d with no HV support\n", excp);
            }
        }
        break;
    case POWERPC_EXCP_TRACE:
        msr |= env->error_code;
        /* fall through */
    case POWERPC_EXCP_DSEG:
    case POWERPC_EXCP_ISEG:
    case POWERPC_EXCP_SDOOR:
    case POWERPC_EXCP_PERFM:
        break;
    case POWERPC_EXCP_HISI:
        msr |= env->error_code;
        /* fall through */
    case POWERPC_EXCP_HDECR:
    case POWERPC_EXCP_HDSI:
    case POWERPC_EXCP_SDOOR_HV:
    case POWERPC_EXCP_HVIRT:
        srr0 = SPR_HSRR0;
        srr1 = SPR_HSRR1;
        new_msr |= (target_ulong)MSR_HVB;
        new_msr |= env->msr & ((target_ulong)1 << MSR_RI);
        break;
#ifdef CONFIG_TCG
    case POWERPC_EXCP_HV_EMU: {
        uint32_t insn = ppc_ldl_code(env, env->nip);
        env->spr[SPR_HEIR] = insn;
        if (is_prefix_insn(env, insn)) {
            uint32_t insn2 = ppc_ldl_code(env, env->nip + 4);
            env->spr[SPR_HEIR] <<= 32;
            env->spr[SPR_HEIR] |= insn2;
        }
        srr0 = SPR_HSRR0;
        srr1 = SPR_HSRR1;
        new_msr |= (target_ulong)MSR_HVB;
        new_msr |= env->msr & ((target_ulong)1 << MSR_RI);
        break;
    }
#endif
    case POWERPC_EXCP_VPU:
    case POWERPC_EXCP_VSXU:
    case POWERPC_EXCP_FU:
        env->spr[SPR_FSCR] |= ((target_ulong)env->error_code << 56);
        break;
    case POWERPC_EXCP_HV_FU:
        env->spr[SPR_HFSCR] |= ((target_ulong)env->error_code << FSCR_IC_POS);
        srr0 = SPR_HSRR0;
        srr1 = SPR_HSRR1;
        new_msr |= (target_ulong)MSR_HVB;
        new_msr |= env->msr & ((target_ulong)1 << MSR_RI);
        break;
    case POWERPC_EXCP_PERFM_EBB:
    case POWERPC_EXCP_EXTERNAL_EBB:
        env->spr[SPR_BESCR] &= ~BESCR_GE;

        env->spr[SPR_EBBRR] = env->nip;
        powerpc_set_excp_state(cpu, env->spr[SPR_EBBHR], env->msr);

        return;
    case POWERPC_EXCP_THERM:
    case POWERPC_EXCP_VPUA:
    case POWERPC_EXCP_MAINT:
    case POWERPC_EXCP_HV_MAINT:
        cpu_abort(env_cpu(env), "%s exception not implemented\n",
                  powerpc_excp_name(excp));
        break;
    default:
        cpu_abort(env_cpu(env), "Invalid PowerPC exception %d. Aborting\n",
                  excp);
        break;
    }

    if (ppc_interrupts_little_endian(cpu, !!(new_msr & MSR_HVB))) {
        new_msr |= (target_ulong)1 << MSR_LE;
    }
    new_msr |= (target_ulong)1 << MSR_SF;

    if (excp != POWERPC_EXCP_SYSCALL_VECTORED) {
        env->spr[srr0] = env->nip;
        env->spr[srr1] = msr;
    }

    if ((new_msr & MSR_HVB) && books_vhyp_handles_hv_excp(cpu)) {
        cpu->vhyp_class->deliver_hv_excp(cpu, excp);
        powerpc_reset_excp_state(cpu);
    } else {
        if (!(env->msr_mask & MSR_HVB) && srr0 == SPR_HSRR0) {
            cpu_abort(env_cpu(env), "Trying to deliver HV exception (HSRR) %d "
                      "with no HV support\n", excp);
        }
        ppc_excp_apply_ail(cpu, excp, msr, &new_msr, &vector);
        powerpc_set_excp_state(cpu, vector, new_msr);
    }
    powerpc_do_plugin_vcpu_interrupt_cb(env_cpu(env), excp, last_pc);
}
#else
static inline void powerpc_excp_books(PowerPCCPU *cpu, int excp)
{
    g_assert_not_reached();
}
#endif

void powerpc_excp(PowerPCCPU *cpu, int excp)
{
    CPUPPCState *env = &cpu->env;

    if (excp <= POWERPC_EXCP_NONE || excp >= POWERPC_EXCP_NB) {
        cpu_abort(env_cpu(env), "Invalid PowerPC exception %d. Aborting\n",
                  excp);
    }

    qemu_log_mask(CPU_LOG_INT, "Raise exception at " TARGET_FMT_lx
                  " => %s (%d) error=%02x\n", env->nip, powerpc_excp_name(excp),
                  excp, env->error_code);
    env->excp_stats[excp]++;

    switch (env->excp_model) {
    case POWERPC_EXCP_40x:
        powerpc_excp_40x(cpu, excp);
        break;
    case POWERPC_EXCP_6xx:
        powerpc_excp_6xx(cpu, excp);
        break;
    case POWERPC_EXCP_7xx:
        powerpc_excp_7xx(cpu, excp);
        break;
    case POWERPC_EXCP_74xx:
        powerpc_excp_74xx(cpu, excp);
        break;
    case POWERPC_EXCP_BOOKE:
        powerpc_excp_booke(cpu, excp);
        break;
    case POWERPC_EXCP_970:
    case POWERPC_EXCP_POWER7:
    case POWERPC_EXCP_POWER8:
    case POWERPC_EXCP_POWER9:
    case POWERPC_EXCP_POWER10:
    case POWERPC_EXCP_POWER11:
        powerpc_excp_books(cpu, excp);
        break;
    case POWERPC_EXCP_PPE42:
        powerpc_excp_ppe42(cpu, excp);
        break;
    default:
        g_assert_not_reached();
    }
}

void ppc_cpu_do_interrupt(CPUState *cs)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);

    powerpc_excp(cpu, cs->exception_index);
}

#ifdef TARGET_PPC64
#define P7_UNUSED_INTERRUPTS \
    (PPC_INTERRUPT_RESET | PPC_INTERRUPT_HVIRT | PPC_INTERRUPT_CEXT |       \
     PPC_INTERRUPT_WDT | PPC_INTERRUPT_CDOORBELL | PPC_INTERRUPT_FIT |      \
     PPC_INTERRUPT_PIT | PPC_INTERRUPT_DOORBELL | PPC_INTERRUPT_HDOORBELL | \
     PPC_INTERRUPT_THERM | PPC_INTERRUPT_EBB)

static int p7_interrupt_powersave(uint32_t pending_interrupts,
                                  target_ulong lpcr)
{
    if ((pending_interrupts & PPC_INTERRUPT_EXT) &&
        (lpcr & LPCR_P7_PECE0)) {
        return PPC_INTERRUPT_EXT;
    }
    if ((pending_interrupts & PPC_INTERRUPT_DECR) &&
        (lpcr & LPCR_P7_PECE1)) {
        return PPC_INTERRUPT_DECR;
    }
    if ((pending_interrupts & PPC_INTERRUPT_MCK) &&
        (lpcr & LPCR_P7_PECE2)) {
        return PPC_INTERRUPT_MCK;
    }
    if ((pending_interrupts & PPC_INTERRUPT_HMI) &&
        (lpcr & LPCR_P7_PECE2)) {
        return PPC_INTERRUPT_HMI;
    }
    if (pending_interrupts & PPC_INTERRUPT_RESET) {
        return PPC_INTERRUPT_RESET;
    }
    return 0;
}

static int p7_next_unmasked_interrupt(CPUPPCState *env,
                                      uint32_t pending_interrupts,
                                      target_ulong lpcr)
{
    CPUState *cs = env_cpu(env);

    bool msr_ee = FIELD_EX64(env->msr, MSR, EE) || env->resume_as_sreset;

    assert((pending_interrupts & P7_UNUSED_INTERRUPTS) == 0);

    if (cs->halted) {
        return p7_interrupt_powersave(pending_interrupts, lpcr);
    }

    if (pending_interrupts & PPC_INTERRUPT_MCK) {
        return PPC_INTERRUPT_MCK;
    }

    if (pending_interrupts & PPC_INTERRUPT_HDECR) {
        bool hdice = !!(env->spr[SPR_LPCR] & LPCR_HDICE);
        if ((msr_ee || !FIELD_EX64_HV(env->msr)) && hdice) {
            return PPC_INTERRUPT_HDECR;
        }
    }

    if (pending_interrupts & PPC_INTERRUPT_EXT) {
        bool lpes0 = !!(lpcr & LPCR_LPES0);
        bool heic = !!(lpcr & LPCR_HEIC);
        if ((msr_ee && !(heic && FIELD_EX64_HV(env->msr) &&
            !FIELD_EX64(env->msr, MSR, PR))) ||
            (env->has_hv_mode && !FIELD_EX64_HV(env->msr) && !lpes0)) {
            return PPC_INTERRUPT_EXT;
        }
    }
    if (msr_ee != 0) {
        if (pending_interrupts & PPC_INTERRUPT_DECR) {
            return PPC_INTERRUPT_DECR;
        }
        if (pending_interrupts & PPC_INTERRUPT_PERFM) {
            return PPC_INTERRUPT_PERFM;
        }
    }

    return 0;
}

#define P8_UNUSED_INTERRUPTS \
    (PPC_INTERRUPT_RESET | PPC_INTERRUPT_DEBUG | PPC_INTERRUPT_HVIRT |  \
    PPC_INTERRUPT_CEXT | PPC_INTERRUPT_WDT | PPC_INTERRUPT_CDOORBELL |  \
    PPC_INTERRUPT_FIT | PPC_INTERRUPT_PIT | PPC_INTERRUPT_THERM)

static int p8_interrupt_powersave(uint32_t pending_interrupts,
                                  target_ulong lpcr)
{
    if ((pending_interrupts & PPC_INTERRUPT_EXT) &&
        (lpcr & LPCR_P8_PECE2)) {
        return PPC_INTERRUPT_EXT;
    }
    if ((pending_interrupts & PPC_INTERRUPT_DECR) &&
        (lpcr & LPCR_P8_PECE3)) {
        return PPC_INTERRUPT_DECR;
    }
    if ((pending_interrupts & PPC_INTERRUPT_MCK) &&
        (lpcr & LPCR_P8_PECE4)) {
        return PPC_INTERRUPT_MCK;
    }
    if ((pending_interrupts & PPC_INTERRUPT_HMI) &&
        (lpcr & LPCR_P8_PECE4)) {
        return PPC_INTERRUPT_HMI;
    }
    if ((pending_interrupts & PPC_INTERRUPT_DOORBELL) &&
        (lpcr & LPCR_P8_PECE0)) {
        return PPC_INTERRUPT_DOORBELL;
    }
    if ((pending_interrupts & PPC_INTERRUPT_HDOORBELL) &&
        (lpcr & LPCR_P8_PECE1)) {
        return PPC_INTERRUPT_HDOORBELL;
    }
    if (pending_interrupts & PPC_INTERRUPT_RESET) {
        return PPC_INTERRUPT_RESET;
    }
    return 0;
}

static int p8_next_unmasked_interrupt(CPUPPCState *env,
                                      uint32_t pending_interrupts,
                                      target_ulong lpcr)
{
    CPUState *cs = env_cpu(env);

    bool msr_ee = FIELD_EX64(env->msr, MSR, EE) || env->resume_as_sreset;

    assert((env->pending_interrupts & P8_UNUSED_INTERRUPTS) == 0);

    if (cs->halted) {
        return p8_interrupt_powersave(pending_interrupts, lpcr);
    }

    if (pending_interrupts & PPC_INTERRUPT_MCK) {
        return PPC_INTERRUPT_MCK;
    }

    if (pending_interrupts & PPC_INTERRUPT_HDECR) {
        bool hdice = !!(lpcr & LPCR_HDICE);
        if ((msr_ee || !FIELD_EX64_HV(env->msr)) && hdice) {
            return PPC_INTERRUPT_HDECR;
        }
    }

    if (pending_interrupts & PPC_INTERRUPT_EXT) {
        bool lpes0 = !!(lpcr & LPCR_LPES0);
        bool heic = !!(lpcr & LPCR_HEIC);
        if ((msr_ee && !(heic && FIELD_EX64_HV(env->msr) &&
            !FIELD_EX64(env->msr, MSR, PR))) ||
            (env->has_hv_mode && !FIELD_EX64_HV(env->msr) && !lpes0)) {
            return PPC_INTERRUPT_EXT;
        }
    }
    if (msr_ee != 0) {
        if (pending_interrupts & PPC_INTERRUPT_DECR) {
            return PPC_INTERRUPT_DECR;
        }
        if (pending_interrupts & PPC_INTERRUPT_DOORBELL) {
            return PPC_INTERRUPT_DOORBELL;
        }
        if (pending_interrupts & PPC_INTERRUPT_HDOORBELL) {
            return PPC_INTERRUPT_HDOORBELL;
        }
        if (pending_interrupts & PPC_INTERRUPT_PERFM) {
            return PPC_INTERRUPT_PERFM;
        }
        if (pending_interrupts & PPC_INTERRUPT_EBB) {
            if (FIELD_EX64(env->msr, MSR, PR) &&
                (env->spr[SPR_BESCR] & BESCR_GE)) {
                return PPC_INTERRUPT_EBB;
            }
        }
    }

    return 0;
}

#define P9_UNUSED_INTERRUPTS \
    (PPC_INTERRUPT_RESET | PPC_INTERRUPT_DEBUG | PPC_INTERRUPT_CEXT |   \
     PPC_INTERRUPT_WDT | PPC_INTERRUPT_CDOORBELL | PPC_INTERRUPT_FIT |  \
     PPC_INTERRUPT_PIT | PPC_INTERRUPT_THERM)

static int p9_interrupt_powersave(CPUPPCState *env,
                                  uint32_t pending_interrupts,
                                  target_ulong lpcr)
{

    if ((pending_interrupts & PPC_INTERRUPT_EXT) &&
        (lpcr & LPCR_EEE)) {
        bool heic = !!(lpcr & LPCR_HEIC);
        if (!heic || !FIELD_EX64_HV(env->msr) ||
            FIELD_EX64(env->msr, MSR, PR)) {
            return PPC_INTERRUPT_EXT;
        }
    }
    if ((pending_interrupts & PPC_INTERRUPT_DECR) &&
        (lpcr & LPCR_DEE)) {
        return PPC_INTERRUPT_DECR;
    }
    if (lpcr & LPCR_OEE) {
        if (pending_interrupts & PPC_INTERRUPT_MCK) {
            return PPC_INTERRUPT_MCK;
        }
        if (pending_interrupts & PPC_INTERRUPT_HMI) {
            return PPC_INTERRUPT_HMI;
        }
    }
    if ((pending_interrupts & PPC_INTERRUPT_DOORBELL) &&
        (lpcr & LPCR_PDEE)) {
        return PPC_INTERRUPT_DOORBELL;
    }
    if ((pending_interrupts & PPC_INTERRUPT_HDOORBELL) &&
        (lpcr & LPCR_HDEE)) {
        return PPC_INTERRUPT_HDOORBELL;
    }
    if ((pending_interrupts & PPC_INTERRUPT_HVIRT) &&
        (lpcr & LPCR_HVEE)) {
        return PPC_INTERRUPT_HVIRT;
    }
    if (pending_interrupts & PPC_INTERRUPT_RESET) {
        return PPC_INTERRUPT_RESET;
    }
    return 0;
}

static int p9_next_unmasked_interrupt(CPUPPCState *env,
                                      uint32_t pending_interrupts,
                                      target_ulong lpcr)
{
    CPUState *cs = env_cpu(env);

    bool msr_ee = FIELD_EX64(env->msr, MSR, EE) || env->resume_as_sreset;

    assert((pending_interrupts & P9_UNUSED_INTERRUPTS) == 0);

    if (cs->halted) {
        if (env->spr[SPR_PSSCR] & PSSCR_EC) {
            return p9_interrupt_powersave(env, pending_interrupts, lpcr);
        } else {
            msr_ee = true;
        }
    }

    if (pending_interrupts & PPC_INTERRUPT_MCK) {
        return PPC_INTERRUPT_MCK;
    }

    if (pending_interrupts & PPC_INTERRUPT_HDECR) {
        bool hdice = !!(lpcr & LPCR_HDICE);
        if ((msr_ee || !FIELD_EX64_HV(env->msr)) && hdice) {
            return PPC_INTERRUPT_HDECR;
        }
    }

    if (pending_interrupts & PPC_INTERRUPT_HVIRT) {
        bool hvice = !!(lpcr & LPCR_HVICE);
        if ((msr_ee || !FIELD_EX64_HV(env->msr)) && hvice) {
            return PPC_INTERRUPT_HVIRT;
        }
    }

    if (pending_interrupts & PPC_INTERRUPT_EXT) {
        bool lpes0 = !!(lpcr & LPCR_LPES0);
        bool heic = !!(lpcr & LPCR_HEIC);
        if ((msr_ee && !(heic && FIELD_EX64_HV(env->msr) &&
            !FIELD_EX64(env->msr, MSR, PR))) ||
            (env->has_hv_mode && !FIELD_EX64_HV(env->msr) && !lpes0)) {
            return PPC_INTERRUPT_EXT;
        }
    }
    if (msr_ee != 0) {
        if (pending_interrupts & PPC_INTERRUPT_DECR) {
            return PPC_INTERRUPT_DECR;
        }
        if (pending_interrupts & PPC_INTERRUPT_DOORBELL) {
            return PPC_INTERRUPT_DOORBELL;
        }
        if (pending_interrupts & PPC_INTERRUPT_HDOORBELL) {
            return PPC_INTERRUPT_HDOORBELL;
        }
        if (pending_interrupts & PPC_INTERRUPT_PERFM) {
            return PPC_INTERRUPT_PERFM;
        }
        if (pending_interrupts & PPC_INTERRUPT_EBB) {
            if (FIELD_EX64(env->msr, MSR, PR) &&
                (env->spr[SPR_BESCR] & BESCR_GE)) {
                return PPC_INTERRUPT_EBB;
            }
        }
    }

    return 0;
}
#endif

static int ppe42_next_unmasked_interrupt(CPUPPCState *env)
{
    bool async_deliver;

    if (env->pending_interrupts & PPC_INTERRUPT_RESET) {
        return PPC_INTERRUPT_RESET;
    }
    if (env->pending_interrupts & PPC_INTERRUPT_MCK) {
        return PPC_INTERRUPT_MCK;
    }

    async_deliver = FIELD_EX64(env->msr, MSR, EE);

    if (async_deliver != 0) {
        if (env->pending_interrupts & PPC_INTERRUPT_WDT) {
            return PPC_INTERRUPT_WDT;
        }
        if (env->pending_interrupts & PPC_INTERRUPT_EXT) {
            return PPC_INTERRUPT_EXT;
        }
        if (env->pending_interrupts & PPC_INTERRUPT_FIT) {
            return PPC_INTERRUPT_FIT;
        }
        if (env->pending_interrupts & PPC_INTERRUPT_DECR) {
            return PPC_INTERRUPT_DECR;
        }
    }

    return 0;
}

static int ppc_next_unmasked_interrupt(CPUPPCState *env)
{
    uint32_t pending_interrupts = env->pending_interrupts;
    target_ulong lpcr = env->spr[SPR_LPCR];
    bool async_deliver;

    if (unlikely(env->quiesced)) {
        return 0;
    }

#ifdef TARGET_PPC64
    switch (env->excp_model) {
    case POWERPC_EXCP_POWER7:
        return p7_next_unmasked_interrupt(env, pending_interrupts, lpcr);
    case POWERPC_EXCP_POWER8:
        return p8_next_unmasked_interrupt(env, pending_interrupts, lpcr);
    case POWERPC_EXCP_POWER9:
    case POWERPC_EXCP_POWER10:
    case POWERPC_EXCP_POWER11:
        return p9_next_unmasked_interrupt(env, pending_interrupts, lpcr);
    default:
        break;
    }
#endif

    if (env->excp_model == POWERPC_EXCP_PPE42) {
        return ppe42_next_unmasked_interrupt(env);
    }

    if (pending_interrupts & PPC_INTERRUPT_RESET) {
        return PPC_INTERRUPT_RESET;
    }
    if (pending_interrupts & PPC_INTERRUPT_MCK) {
        return PPC_INTERRUPT_MCK;
    }
#if 0
    if (env->pending_interrupts & PPC_INTERRUPT_DEBUG) {
        return PPC_INTERRUPT_DEBUG;
    }
#endif

    async_deliver = FIELD_EX64(env->msr, MSR, EE) || env->resume_as_sreset;

    if (pending_interrupts & PPC_INTERRUPT_HDECR) {
        bool hdice = !!(lpcr & LPCR_HDICE);
        if ((async_deliver || !FIELD_EX64_HV(env->msr)) && hdice) {
            return PPC_INTERRUPT_HDECR;
        }
    }

    if (pending_interrupts & PPC_INTERRUPT_HVIRT) {
        bool hvice = !!(lpcr & LPCR_HVICE);
        if ((async_deliver || !FIELD_EX64_HV(env->msr)) && hvice) {
            return PPC_INTERRUPT_HVIRT;
        }
    }

    if (pending_interrupts & PPC_INTERRUPT_EXT) {
        bool lpes0 = !!(lpcr & LPCR_LPES0);
        bool heic = !!(lpcr & LPCR_HEIC);
        if ((async_deliver && !(heic && FIELD_EX64_HV(env->msr) &&
            !FIELD_EX64(env->msr, MSR, PR))) ||
            (env->has_hv_mode && !FIELD_EX64_HV(env->msr) && !lpes0)) {
            return PPC_INTERRUPT_EXT;
        }
    }
    if (FIELD_EX64(env->msr, MSR, CE)) {
        if (pending_interrupts & PPC_INTERRUPT_CEXT) {
            return PPC_INTERRUPT_CEXT;
        }
    }
    if (async_deliver != 0) {
        if (pending_interrupts & PPC_INTERRUPT_WDT) {
            return PPC_INTERRUPT_WDT;
        }
        if (pending_interrupts & PPC_INTERRUPT_CDOORBELL) {
            return PPC_INTERRUPT_CDOORBELL;
        }
        if (pending_interrupts & PPC_INTERRUPT_FIT) {
            return PPC_INTERRUPT_FIT;
        }
        if (pending_interrupts & PPC_INTERRUPT_PIT) {
            return PPC_INTERRUPT_PIT;
        }
        if (pending_interrupts & PPC_INTERRUPT_DECR) {
            return PPC_INTERRUPT_DECR;
        }
        if (pending_interrupts & PPC_INTERRUPT_DOORBELL) {
            return PPC_INTERRUPT_DOORBELL;
        }
        if (pending_interrupts & PPC_INTERRUPT_HDOORBELL) {
            return PPC_INTERRUPT_HDOORBELL;
        }
        if (pending_interrupts & PPC_INTERRUPT_PERFM) {
            return PPC_INTERRUPT_PERFM;
        }
        if (pending_interrupts & PPC_INTERRUPT_THERM) {
            return PPC_INTERRUPT_THERM;
        }
        if (pending_interrupts & PPC_INTERRUPT_EBB) {
            if (FIELD_EX64(env->msr, MSR, PR) &&
                (env->spr[SPR_BESCR] & BESCR_GE)) {
                return PPC_INTERRUPT_EBB;
            }
        }
    }

    return 0;
}

void ppc_maybe_interrupt(CPUPPCState *env)
{
    CPUState *cs = env_cpu(env);
    BQL_LOCK_GUARD();

    if (ppc_next_unmasked_interrupt(env)) {
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    } else {
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }
}

#ifdef TARGET_PPC64
static void p7_deliver_interrupt(CPUPPCState *env, int interrupt)
{
    PowerPCCPU *cpu = env_archcpu(env);

    switch (interrupt) {
    case PPC_INTERRUPT_MCK:
        env->pending_interrupts &= ~PPC_INTERRUPT_MCK;
        powerpc_excp(cpu, POWERPC_EXCP_MCHECK);
        break;

    case PPC_INTERRUPT_HDECR:
        env->pending_interrupts &= ~PPC_INTERRUPT_HDECR;
        powerpc_excp(cpu, POWERPC_EXCP_HDECR);
        break;

    case PPC_INTERRUPT_EXT:
        if (books_vhyp_promotes_external_to_hvirt(cpu)) {
            powerpc_excp(cpu, POWERPC_EXCP_HVIRT);
        } else {
            powerpc_excp(cpu, POWERPC_EXCP_EXTERNAL);
        }
        break;

    case PPC_INTERRUPT_DECR:
        powerpc_excp(cpu, POWERPC_EXCP_DECR);
        break;
    case PPC_INTERRUPT_PERFM:
        powerpc_excp(cpu, POWERPC_EXCP_PERFM);
        break;
    case 0:
        assert(!env->resume_as_sreset);
        break;
    default:
        cpu_abort(env_cpu(env), "Invalid PowerPC interrupt %d. Aborting\n",
                  interrupt);
    }
}

static void p8_deliver_interrupt(CPUPPCState *env, int interrupt)
{
    PowerPCCPU *cpu = env_archcpu(env);

    switch (interrupt) {
    case PPC_INTERRUPT_MCK:
        env->pending_interrupts &= ~PPC_INTERRUPT_MCK;
        powerpc_excp(cpu, POWERPC_EXCP_MCHECK);
        break;

    case PPC_INTERRUPT_HDECR:
        env->pending_interrupts &= ~PPC_INTERRUPT_HDECR;
        powerpc_excp(cpu, POWERPC_EXCP_HDECR);
        break;

    case PPC_INTERRUPT_EXT:
        if (books_vhyp_promotes_external_to_hvirt(cpu)) {
            powerpc_excp(cpu, POWERPC_EXCP_HVIRT);
        } else {
            powerpc_excp(cpu, POWERPC_EXCP_EXTERNAL);
        }
        break;

    case PPC_INTERRUPT_DECR:
        powerpc_excp(cpu, POWERPC_EXCP_DECR);
        break;
    case PPC_INTERRUPT_DOORBELL:
        if (!env->resume_as_sreset) {
            env->pending_interrupts &= ~PPC_INTERRUPT_DOORBELL;
        }
        if (is_book3s_arch2x(env)) {
            powerpc_excp(cpu, POWERPC_EXCP_SDOOR);
        } else {
            powerpc_excp(cpu, POWERPC_EXCP_DOORI);
        }
        break;
    case PPC_INTERRUPT_HDOORBELL:
        if (!env->resume_as_sreset) {
            env->pending_interrupts &= ~PPC_INTERRUPT_HDOORBELL;
        }
        powerpc_excp(cpu, POWERPC_EXCP_SDOOR_HV);
        break;
    case PPC_INTERRUPT_PERFM:
        powerpc_excp(cpu, POWERPC_EXCP_PERFM);
        break;
    case PPC_INTERRUPT_EBB:
        env->pending_interrupts &= ~PPC_INTERRUPT_EBB;
        if (env->spr[SPR_BESCR] & BESCR_PMEO) {
            powerpc_excp(cpu, POWERPC_EXCP_PERFM_EBB);
        } else if (env->spr[SPR_BESCR] & BESCR_EEO) {
            powerpc_excp(cpu, POWERPC_EXCP_EXTERNAL_EBB);
        }
        break;
    case 0:
        assert(!env->resume_as_sreset);
        break;
    default:
        cpu_abort(env_cpu(env), "Invalid PowerPC interrupt %d. Aborting\n",
                  interrupt);
    }
}

static void p9_deliver_interrupt(CPUPPCState *env, int interrupt)
{
    PowerPCCPU *cpu = env_archcpu(env);
    CPUState *cs = env_cpu(env);

    if (cs->halted && !(env->spr[SPR_PSSCR] & PSSCR_EC) &&
        !FIELD_EX64(env->msr, MSR, EE)) {
        return;
    }

    switch (interrupt) {
    case PPC_INTERRUPT_MCK:
        env->pending_interrupts &= ~PPC_INTERRUPT_MCK;
        powerpc_excp(cpu, POWERPC_EXCP_MCHECK);
        break;

    case PPC_INTERRUPT_HDECR:
        env->pending_interrupts &= ~PPC_INTERRUPT_HDECR;
        powerpc_excp(cpu, POWERPC_EXCP_HDECR);
        break;
    case PPC_INTERRUPT_HVIRT:
        powerpc_excp(cpu, POWERPC_EXCP_HVIRT);
        break;

    case PPC_INTERRUPT_EXT:
        if (books_vhyp_promotes_external_to_hvirt(cpu)) {
            powerpc_excp(cpu, POWERPC_EXCP_HVIRT);
        } else {
            powerpc_excp(cpu, POWERPC_EXCP_EXTERNAL);
        }
        break;

    case PPC_INTERRUPT_DECR:
        powerpc_excp(cpu, POWERPC_EXCP_DECR);
        break;
    case PPC_INTERRUPT_DOORBELL:
        if (!env->resume_as_sreset) {
            env->pending_interrupts &= ~PPC_INTERRUPT_DOORBELL;
        }
        powerpc_excp(cpu, POWERPC_EXCP_SDOOR);
        break;
    case PPC_INTERRUPT_HDOORBELL:
        if (!env->resume_as_sreset) {
            env->pending_interrupts &= ~PPC_INTERRUPT_HDOORBELL;
        }
        powerpc_excp(cpu, POWERPC_EXCP_SDOOR_HV);
        break;
    case PPC_INTERRUPT_PERFM:
        powerpc_excp(cpu, POWERPC_EXCP_PERFM);
        break;
    case PPC_INTERRUPT_EBB:
        env->pending_interrupts &= ~PPC_INTERRUPT_EBB;
        if (env->spr[SPR_BESCR] & BESCR_PMEO) {
            powerpc_excp(cpu, POWERPC_EXCP_PERFM_EBB);
        } else if (env->spr[SPR_BESCR] & BESCR_EEO) {
            powerpc_excp(cpu, POWERPC_EXCP_EXTERNAL_EBB);
        }
        break;
    case 0:
        assert(!env->resume_as_sreset);
        break;
    default:
        cpu_abort(env_cpu(env), "Invalid PowerPC interrupt %d. Aborting\n",
                  interrupt);
    }
}
#endif

static void ppc_deliver_interrupt(CPUPPCState *env, int interrupt)
{
#ifdef TARGET_PPC64
    switch (env->excp_model) {
    case POWERPC_EXCP_POWER7:
        return p7_deliver_interrupt(env, interrupt);
    case POWERPC_EXCP_POWER8:
        return p8_deliver_interrupt(env, interrupt);
    case POWERPC_EXCP_POWER9:
    case POWERPC_EXCP_POWER10:
    case POWERPC_EXCP_POWER11:
        return p9_deliver_interrupt(env, interrupt);
    default:
        break;
    }
#endif
    PowerPCCPU *cpu = env_archcpu(env);

    switch (interrupt) {
    case PPC_INTERRUPT_RESET:
        env->pending_interrupts &= ~PPC_INTERRUPT_RESET;
        powerpc_excp(cpu, POWERPC_EXCP_RESET);
        break;
    case PPC_INTERRUPT_MCK:
        env->pending_interrupts &= ~PPC_INTERRUPT_MCK;
        powerpc_excp(cpu, POWERPC_EXCP_MCHECK);
        break;

    case PPC_INTERRUPT_HDECR:
        env->pending_interrupts &= ~PPC_INTERRUPT_HDECR;
        powerpc_excp(cpu, POWERPC_EXCP_HDECR);
        break;
    case PPC_INTERRUPT_HVIRT:
        powerpc_excp(cpu, POWERPC_EXCP_HVIRT);
        break;

    case PPC_INTERRUPT_EXT:
        if (books_vhyp_promotes_external_to_hvirt(cpu)) {
            powerpc_excp(cpu, POWERPC_EXCP_HVIRT);
        } else {
            powerpc_excp(cpu, POWERPC_EXCP_EXTERNAL);
        }
        break;
    case PPC_INTERRUPT_CEXT:
        powerpc_excp(cpu, POWERPC_EXCP_CRITICAL);
        break;

    case PPC_INTERRUPT_WDT:
        env->pending_interrupts &= ~PPC_INTERRUPT_WDT;
        powerpc_excp(cpu, POWERPC_EXCP_WDT);
        break;
    case PPC_INTERRUPT_CDOORBELL:
        env->pending_interrupts &= ~PPC_INTERRUPT_CDOORBELL;
        powerpc_excp(cpu, POWERPC_EXCP_DOORCI);
        break;
    case PPC_INTERRUPT_FIT:
        env->pending_interrupts &= ~PPC_INTERRUPT_FIT;
        powerpc_excp(cpu, POWERPC_EXCP_FIT);
        break;
    case PPC_INTERRUPT_PIT:
        env->pending_interrupts &= ~PPC_INTERRUPT_PIT;
        powerpc_excp(cpu, POWERPC_EXCP_PIT);
        break;
    case PPC_INTERRUPT_DECR:
        if (ppc_decr_clear_on_delivery(env)) {
            env->pending_interrupts &= ~PPC_INTERRUPT_DECR;
        }
        powerpc_excp(cpu, POWERPC_EXCP_DECR);
        break;
    case PPC_INTERRUPT_DOORBELL:
        env->pending_interrupts &= ~PPC_INTERRUPT_DOORBELL;
        if (is_book3s_arch2x(env)) {
            powerpc_excp(cpu, POWERPC_EXCP_SDOOR);
        } else {
            powerpc_excp(cpu, POWERPC_EXCP_DOORI);
        }
        break;
    case PPC_INTERRUPT_HDOORBELL:
        env->pending_interrupts &= ~PPC_INTERRUPT_HDOORBELL;
        powerpc_excp(cpu, POWERPC_EXCP_SDOOR_HV);
        break;
    case PPC_INTERRUPT_PERFM:
        powerpc_excp(cpu, POWERPC_EXCP_PERFM);
        break;
    case PPC_INTERRUPT_THERM:
        env->pending_interrupts &= ~PPC_INTERRUPT_THERM;
        powerpc_excp(cpu, POWERPC_EXCP_THERM);
        break;
    case PPC_INTERRUPT_EBB:
        env->pending_interrupts &= ~PPC_INTERRUPT_EBB;
        if (env->spr[SPR_BESCR] & BESCR_PMEO) {
            powerpc_excp(cpu, POWERPC_EXCP_PERFM_EBB);
        } else if (env->spr[SPR_BESCR] & BESCR_EEO) {
            powerpc_excp(cpu, POWERPC_EXCP_EXTERNAL_EBB);
        }
        break;
    case 0:
        assert(!env->resume_as_sreset);
        break;
    default:
        cpu_abort(env_cpu(env), "Invalid PowerPC interrupt %d. Aborting\n",
                  interrupt);
    }
}

void ppc_cpu_do_system_reset(CPUState *cs)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);

    cs->halted = 0;
    powerpc_excp(cpu, POWERPC_EXCP_RESET);
}

void ppc_cpu_do_fwnmi_machine_check(CPUState *cs, target_ulong vector)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    target_ulong msr = 0;

    msr = (1ULL << MSR_ME);
    msr |= env->msr & (1ULL << MSR_SF);
    if (ppc_interrupts_little_endian(cpu, false)) {
        msr |= (1ULL << MSR_LE);
    }

    cs->halted = 0;
    powerpc_set_excp_state(cpu, vector, msr);
}

bool ppc_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    CPUPPCState *env = cpu_env(cs);
    int interrupt;

    if ((interrupt_request & CPU_INTERRUPT_HARD) == 0) {
        return false;
    }

    interrupt = ppc_next_unmasked_interrupt(env);
    if (interrupt == 0) {
        return false;
    }

    ppc_deliver_interrupt(env, interrupt);
    if (env->pending_interrupts == 0) {
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }
    return true;
}

#endif
