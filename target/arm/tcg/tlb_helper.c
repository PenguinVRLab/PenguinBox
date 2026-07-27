/*
 * ARM TLB (Translation lookaside buffer) helpers.
 *
 * This code is licensed under the GNU GPL v2 or later.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "cpu-features.h"

#define HELPER_H "tcg/helper.h"
#include "exec/helper-proto.h.inc"

bool arm_s1_regime_using_lpae_format(CPUARMState *env, ARMMMUIdx mmu_idx)
{
    mmu_idx = stage_1_mmu_idx(mmu_idx);
    return regime_using_lpae_format(env, mmu_idx);
}

static inline uint64_t merge_syn_data_abort(uint32_t template_syn,
                                            ARMMMUFaultInfo *fi,
                                            unsigned int target_el,
                                            bool same_el, bool is_write,
                                            int fsc, bool gcs)
{
    uint64_t syn;

    if (template_syn & ARM_EL_VNCR) {
        assert(!fi->stage2);
        syn = syn_data_abort_vncr(fi->ea, is_write, fsc);
    } else if (!(template_syn & ARM_EL_ISV) || target_el != 2
        || fi->s1ptw || !fi->stage2) {
        syn = syn_data_abort_no_iss(same_el, 0,
                                    fi->ea, 0, fi->s1ptw, is_write, fsc);
    } else {
        syn = syn_data_abort_with_iss(same_el,
                                      0, 0, 0, 0, 0,
                                      fi->ea, 0, fi->s1ptw, is_write, fsc,
                                      true);
        syn |= template_syn;
    }

    syn |= (uint64_t)fi->dirtybit << 37;
    syn |= (uint64_t)gcs << 40;

    return syn;
}

static uint32_t compute_fsr_fsc(CPUARMState *env, ARMMMUFaultInfo *fi,
                                int target_el, int mmu_idx, uint32_t *ret_fsc)
{
    ARMMMUIdx arm_mmu_idx = core_to_arm_mmu_idx(env, mmu_idx);
    uint32_t fsr, fsc;

    if (!arm_feature(env, ARM_FEATURE_M) &&
        (target_el == 2 || arm_el_is_aa64(env, target_el) ||
         arm_s1_regime_using_lpae_format(env, arm_mmu_idx))) {
        fsr = arm_fi_to_lfsc(fi);
        fsc = extract32(fsr, 0, 6);
    } else {
        fsr = arm_fi_to_sfsc(fi);
        fsc = 0x3f;
    }

    *ret_fsc = fsc;
    return fsr;
}

static bool report_as_gpc_exception(ARMCPU *cpu, int current_el,
                                    ARMMMUFaultInfo *fi)
{
    bool ret;

    switch (fi->gpcf) {
    case GPCF_None:
        return false;
    case GPCF_AddressSize:
    case GPCF_Walk:
    case GPCF_EABT:
        ret = true;
        break;
    case GPCF_Fail:
        ret = (cpu->env.cp15.scr_el3 & SCR_GPF) && current_el != 3;
        break;
    default:
        g_assert_not_reached();
    }

    assert(cpu_isar_feature(aa64_rme, cpu));
    assert(fi->type == ARMFault_GPCFOnWalk ||
           fi->type == ARMFault_GPCFOnOutput);
    if (fi->gpcf == GPCF_AddressSize) {
        assert(fi->level == 0);
    } else {
        assert(fi->level >= 0 && fi->level <= 1);
    }

    return ret;
}

static unsigned encode_gpcsc(ARMMMUFaultInfo *fi)
{
    static uint8_t const gpcsc[] = {
        [GPCF_AddressSize] = 0b000000,
        [GPCF_Walk]        = 0b000100,
        [GPCF_Fail]        = 0b001100,
        [GPCF_EABT]        = 0b010100,
    };

    return gpcsc[fi->gpcf] | fi->level;
}

static G_NORETURN
void arm_deliver_fault(ARMCPU *cpu, vaddr addr,
                       MMUAccessType access_type,
                       int mmu_idx, ARMMMUFaultInfo *fi)
{
    CPUARMState *env = &cpu->env;
    int target_el = exception_target_el(env);
    int current_el = arm_current_el(env);
    bool same_el;
    uint32_t exc, fsr, fsc;
    uint64_t syn;

    bool is_vncr = (access_type != MMU_INST_FETCH) &&
        (env->exception.syndrome & ARM_EL_VNCR);

    if (is_vncr) {
        target_el = 2;
    }

    if (report_as_gpc_exception(cpu, current_el, fi)) {
        target_el = 3;

        fsr = compute_fsr_fsc(env, fi, target_el, mmu_idx, &fsc);

        syn = syn_gpc(fi->stage2 && fi->type == ARMFault_GPCFOnWalk,
                      access_type == MMU_INST_FETCH,
                      encode_gpcsc(fi), is_vncr,
                      0, fi->s1ptw,
                      access_type == MMU_DATA_STORE, fsc);

        env->cp15.mfar_el3 = fi->paddr;
        switch (fi->paddr_space) {
        case ARMSS_Secure:
            break;
        case ARMSS_NonSecure:
            env->cp15.mfar_el3 |= R_MFAR_NS_MASK;
            break;
        case ARMSS_Root:
            env->cp15.mfar_el3 |= R_MFAR_NSE_MASK;
            break;
        case ARMSS_Realm:
            env->cp15.mfar_el3 |= R_MFAR_NSE_MASK | R_MFAR_NS_MASK;
            break;
        default:
            g_assert_not_reached();
        }

        exc = EXCP_GPC;
        goto do_raise;
    }

    if (fi->gpcf == GPCF_Fail && target_el < 2) {
        if (arm_hcr_el2_eff(env) & HCR_GPF) {
            target_el = 2;
        }
    }

    if (fi->stage2) {
        target_el = 2;
        env->cp15.hpfar_el2 = extract64(fi->s2addr, 12, 47) << 4;
        if (arm_is_secure_below_el3(env) && fi->s1ns) {
            env->cp15.hpfar_el2 |= HPFAR_NS;
        }
    }

    same_el = current_el == target_el;
    fsr = compute_fsr_fsc(env, fi, target_el, mmu_idx, &fsc);

    if (access_type == MMU_INST_FETCH) {
        if (fi->type == ARMFault_Alignment) {
            syn = syn_pcalignment();
        } else {
            syn = syn_insn_abort(same_el, fi->ea, fi->s1ptw, fsc);
        }
        exc = EXCP_PREFETCH_ABORT;
    } else {
        bool gcs = regime_is_gcs(core_to_arm_mmu_idx(env, mmu_idx));
        syn = merge_syn_data_abort(env->exception.syndrome, fi, target_el,
                                   same_el, access_type == MMU_DATA_STORE,
                                   fsc, gcs);
        if (access_type == MMU_DATA_STORE
            && arm_feature(env, ARM_FEATURE_V6)) {
            fsr |= (1 << 11);
        }
        exc = EXCP_DATA_ABORT;
    }

 do_raise:
    env->exception.vaddress = addr;
    env->exception.fsr = fsr;
    raise_exception(env, exc, syn, target_el);
}

void arm_cpu_do_unaligned_access(CPUState *cs, vaddr vaddr,
                                 MMUAccessType access_type,
                                 int mmu_idx, uintptr_t retaddr)
{
    ARMCPU *cpu = ARM_CPU(cs);
    ARMMMUFaultInfo fi = {};

    cpu_restore_state(cs, retaddr);

    fi.type = ARMFault_Alignment;
    arm_deliver_fault(cpu, vaddr, access_type, mmu_idx, &fi);
}

void helper_exception_pc_alignment(CPUARMState *env, vaddr pc)
{
    ARMMMUFaultInfo fi = { .type = ARMFault_Alignment };
    int target_el = exception_target_el(env);
    int mmu_idx = arm_env_mmu_index(env);
    uint32_t fsc;

    env->exception.vaddress = pc;

    env->exception.fsr = compute_fsr_fsc(env, &fi, target_el, mmu_idx, &fsc);
    raise_exception(env, EXCP_PREFETCH_ABORT, syn_pcalignment(), target_el);
}

#if !defined(CONFIG_USER_ONLY)

void arm_cpu_do_transaction_failed(CPUState *cs, hwaddr physaddr,
                                   vaddr addr, unsigned size,
                                   MMUAccessType access_type,
                                   int mmu_idx, MemTxAttrs attrs,
                                   MemTxResult response, uintptr_t retaddr)
{
    ARMCPU *cpu = ARM_CPU(cs);
    ARMMMUFaultInfo fi = {};

    cpu_restore_state(cs, retaddr);

    fi.ea = arm_extabort_type(response);
    fi.type = ARMFault_SyncExternal;
    arm_deliver_fault(cpu, addr, access_type, mmu_idx, &fi);
}

bool arm_cpu_tlb_fill_align(CPUState *cs, CPUTLBEntryFull *out, vaddr address,
                            MMUAccessType access_type, int mmu_idx,
                            MemOp memop, int size, bool probe, uintptr_t ra)
{
    ARMCPU *cpu = ARM_CPU(cs);
    GetPhysAddrResult res = {};
    ARMMMUFaultInfo local_fi, *fi;

    fi = cpu->env.tlb_fi;
    if (fi) {
        cpu->env.tlb_fi = NULL;
    } else {
        fi = memset(&local_fi, 0, sizeof(local_fi));
    }

    if (access_type == MMU_INST_FETCH && !cpu->env.thumb &&
        (address & 3)) {
        fi->type = ARMFault_Alignment;
    } else if (address & ((1 << memop_alignment_bits(memop)) - 1)) {
        fi->type = ARMFault_Alignment;
    } else if (!get_phys_addr(&cpu->env, address, access_type, memop,
                              core_to_arm_mmu_idx(&cpu->env, mmu_idx),
                              &res, fi)) {
        res.f.extra.arm.pte_attrs = res.cacheattrs.attrs;
        res.f.extra.arm.shareability = res.cacheattrs.shareability;
        *out = res.f;
        return true;
    }
    if (probe) {
        return false;
    }

    cpu_restore_state(cs, ra);
    arm_deliver_fault(cpu, address, access_type, mmu_idx, fi);
}
#else
void arm_cpu_record_sigsegv(CPUState *cs, vaddr addr,
                            MMUAccessType access_type,
                            bool maperr, uintptr_t ra)
{
    ARMMMUFaultInfo fi = {
        .type = maperr ? ARMFault_Translation : ARMFault_Permission,
        .level = 3,
    };
    ARMCPU *cpu = ARM_CPU(cs);

    cpu_restore_state(cs, ra);
    arm_deliver_fault(cpu, addr, access_type, MMU_USER_IDX, &fi);
}

void arm_cpu_record_sigbus(CPUState *cs, vaddr addr,
                           MMUAccessType access_type, uintptr_t ra)
{
    arm_cpu_do_unaligned_access(cs, addr, access_type, MMU_USER_IDX, ra);
}
#endif
