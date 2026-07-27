/*
 * System instructions for address translation
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "cpu-features.h"
#include "internals.h"
#include "cpregs.h"


static int par_el1_shareability(GetPhysAddrResult *res)
{
    if (((res->cacheattrs.attrs & 0xf0) == 0) ||
        res->cacheattrs.attrs == 0x44 || res->cacheattrs.attrs == 0x40) {
        return 2;
    }
    return res->cacheattrs.shareability;
}

static uint64_t do_ats_write(CPUARMState *env, uint64_t value,
                             unsigned prot_check, ARMMMUIdx mmu_idx,
                             ARMSecuritySpace ss)
{
    uint64_t par64;
    bool format64 = false;
    ARMMMUFaultInfo fi = {};
    GetPhysAddrResult res = {};
    bool ret = get_phys_addr_for_at(env, value, prot_check,
                                    mmu_idx, ss, &res, &fi);

    assert(!res.cacheattrs.is_s2_format);

    if (ret) {
        int current_el = arm_current_el(env);
        int target_el;
        uint32_t syn, fsr, fsc;
        bool take_exc = false;

        if (fi.s1ptw && current_el == 1
            && arm_mmu_idx_is_stage1_of_2(mmu_idx)) {
            if (fi.type == ARMFault_SyncExternalOnWalk &&
                (env->cp15.scr_el3 & SCR_EA)) {
                target_el = 3;
            } else {
                env->cp15.hpfar_el2 = extract64(fi.s2addr, 12, 47) << 4;
                if (arm_is_secure_below_el3(env) && fi.s1ns) {
                    env->cp15.hpfar_el2 |= HPFAR_NS;
                }
                target_el = 2;
            }
            take_exc = true;
        } else if (fi.type == ARMFault_SyncExternalOnWalk) {
            if (fi.stage2) {
                if (current_el == 3) {
                    target_el = 3;
                } else {
                    target_el = 2;
                }
            } else {
                target_el = exception_target_el(env);
            }
            take_exc = true;
        }

        if (take_exc) {
            if (target_el == 2 || arm_el_is_aa64(env, target_el) ||
                arm_s1_regime_using_lpae_format(env, mmu_idx)) {
                fsr = arm_fi_to_lfsc(&fi);
                fsc = extract32(fsr, 0, 6);
            } else {
                fsr = arm_fi_to_sfsc(&fi);
                fsc = 0x3f;
            }
            syn = syn_data_abort_no_iss(current_el == target_el, 0,
                                        fi.ea, 1, fi.s1ptw, 1, fsc);
            env->exception.vaddress = value;
            env->exception.fsr = fsr;
            raise_exception(env, EXCP_DATA_ABORT, syn, target_el);
        }
    }

    if (is_a64(env)) {
        format64 = true;
    } else if (arm_feature(env, ARM_FEATURE_LPAE)) {
        format64 = arm_s1_regime_using_lpae_format(env, mmu_idx);

        if (arm_feature(env, ARM_FEATURE_EL2)) {
            if (mmu_idx == ARMMMUIdx_E10_0 ||
                mmu_idx == ARMMMUIdx_E10_1 ||
                mmu_idx == ARMMMUIdx_E10_1_PAN) {
                format64 |= env->cp15.hcr_el2 & (HCR_VM | HCR_DC);
            } else {
                format64 |= arm_current_el(env) == 2;
            }
        }
    }

    if (format64) {
        par64 = (1 << 11);
        if (!ret) {
            par64 |= res.f.phys_addr & ~0xfffULL;
            if (!res.f.attrs.secure) {
                par64 |= (1 << 9);
            }
            par64 |= (uint64_t)res.cacheattrs.attrs << 56;
            par64 |= par_el1_shareability(&res) << 7;
        } else {
            uint32_t fsr = arm_fi_to_lfsc(&fi);

            par64 |= 1;
            par64 |= (fsr & 0x3f) << 1;
            if (fi.stage2) {
                par64 |= (1 << 9);
            }
            if (fi.s1ptw) {
                par64 |= (1 << 8);
            }
        }
    } else {
        if (!ret) {
            if (res.f.lg_page_size == 24
                && arm_feature(env, ARM_FEATURE_V7)) {
                par64 = (res.f.phys_addr & 0xff000000) | (1 << 1);
            } else {
                par64 = res.f.phys_addr & 0xfffff000;
            }
            if (!res.f.attrs.secure) {
                par64 |= (1 << 9);
            }
        } else {
            uint32_t fsr = arm_fi_to_sfsc(&fi);

            par64 = ((fsr & (1 << 10)) >> 5) | ((fsr & (1 << 12)) >> 6) |
                    ((fsr & 0xf) << 1) | 1;
        }
    }
    return par64;
}

static void ats_write(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value)
{
    unsigned access_perm = ri->opc2 & 1 ? PAGE_WRITE : PAGE_READ;
    uint64_t par64;
    ARMMMUIdx mmu_idx;
    int el = arm_current_el(env);
    ARMSecuritySpace ss = arm_security_space(env);

    switch (ri->opc2 & 6) {
    case 0:
        switch (el) {
        case 3:
            if (ri->crm == 9 && arm_pan_enabled(env)) {
                mmu_idx = ARMMMUIdx_E30_3_PAN;
            } else {
                mmu_idx = ARMMMUIdx_E3;
            }
            break;
        case 2:
            g_assert(ss != ARMSS_Secure);
            /* fall through */
        case 1:
            if (ri->crm == 9 && arm_pan_enabled(env)) {
                mmu_idx = ARMMMUIdx_Stage1_E1_PAN;
            } else {
                mmu_idx = ARMMMUIdx_Stage1_E1;
            }
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case 2:
        switch (el) {
        case 3:
            mmu_idx = ARMMMUIdx_E30_0;
            break;
        case 2:
            g_assert(ss != ARMSS_Secure);
            mmu_idx = ARMMMUIdx_Stage1_E0;
            break;
        case 1:
            mmu_idx = ARMMMUIdx_Stage1_E0;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case 4:
        mmu_idx = ARMMMUIdx_E10_1;
        ss = ARMSS_NonSecure;
        break;
    case 6:
        mmu_idx = ARMMMUIdx_E10_0;
        ss = ARMSS_NonSecure;
        break;
    default:
        g_assert_not_reached();
    }

    par64 = do_ats_write(env, value, access_perm, mmu_idx, ss);

    A32_BANKED_CURRENT_REG_SET(env, par, par64);
}

static void ats1h_write(CPUARMState *env, const ARMCPRegInfo *ri,
                        uint64_t value)
{
    unsigned access_perm = ri->opc2 & 1 ? PAGE_WRITE : PAGE_READ;
    uint64_t par64;

    par64 = do_ats_write(env, value, access_perm, ARMMMUIdx_E2,
                         ARMSS_NonSecure);

    A32_BANKED_CURRENT_REG_SET(env, par, par64);
}

static CPAccessResult at_e012_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                     bool isread)
{
    if ((env->cp15.scr_el3 & (SCR_NSE | SCR_NS)) == SCR_NSE) {
        return CP_ACCESS_UNDEFINED;
    }
    return CP_ACCESS_OK;
}

static CPAccessResult at_s1e2_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                     bool isread)
{
    if (arm_current_el(env) == 3 &&
        !(env->cp15.scr_el3 & (SCR_NS | SCR_EEL2))) {
        return CP_ACCESS_UNDEFINED;
    }
    return at_e012_access(env, ri, isread);
}

static CPAccessResult at_s1e01_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                      bool isread)
{
    if (arm_current_el(env) == 1 && (arm_hcr_el2_eff(env) & HCR_AT)) {
        return CP_ACCESS_TRAP_EL2;
    }
    return at_e012_access(env, ri, isread);
}

static void ats_write64(CPUARMState *env, const ARMCPRegInfo *ri,
                        uint64_t value)
{
    unsigned access_perm = ri->opc2 & 1 ? PAGE_WRITE : PAGE_READ;
    ARMMMUIdx mmu_idx;
    uint64_t hcr_el2 = arm_hcr_el2_eff(env);
    bool regime_e20 = (hcr_el2 & (HCR_E2H | HCR_TGE)) == (HCR_E2H | HCR_TGE);
    bool for_el3 = false;
    ARMSecuritySpace ss;

    switch (ri->opc2 & 6) {
    case 0:
        switch (ri->opc1) {
        case 0:
            if (ri->crm == 9 && arm_pan_enabled(env)) {
                mmu_idx = regime_e20 ?
                          ARMMMUIdx_E20_2_PAN : ARMMMUIdx_Stage1_E1_PAN;
            } else {
                mmu_idx = regime_e20 ? ARMMMUIdx_E20_2 : ARMMMUIdx_Stage1_E1;
            }
            break;
        case 4:
            mmu_idx = hcr_el2 & HCR_E2H ? ARMMMUIdx_E20_2 : ARMMMUIdx_E2;
            break;
        case 6:
            mmu_idx = ARMMMUIdx_E3;
            for_el3 = true;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case 2:
        mmu_idx = regime_e20 ? ARMMMUIdx_E20_0 : ARMMMUIdx_Stage1_E0;
        break;
    case 4:
        mmu_idx = regime_e20 ? ARMMMUIdx_E20_2 : ARMMMUIdx_E10_1;
        break;
    case 6:
        mmu_idx = regime_e20 ? ARMMMUIdx_E20_0 : ARMMMUIdx_E10_0;
        break;
    default:
        g_assert_not_reached();
    }

    ss = for_el3 ? arm_security_space(env) : arm_security_space_below_el3(env);
    env->cp15.par_el[1] = do_ats_write(env, value, access_perm, mmu_idx, ss);
}

static CPAccessResult ats_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                 bool isread)
{
    if (ri->opc2 & 4) {
        if (arm_current_el(env) == 1) {
            if (arm_is_secure_below_el3(env)) {
                if (env->cp15.scr_el3 & SCR_EEL2) {
                    return CP_ACCESS_TRAP_EL2;
                }
                return CP_ACCESS_TRAP_EL3;
            }
            return CP_ACCESS_UNDEFINED;
        }
    }
    return CP_ACCESS_OK;
}

static const ARMCPRegInfo vapa_ats_reginfo[] = {
    { .name = "ATS", .cp = 15, .crn = 7, .crm = 8, .opc1 = 0, .opc2 = CP_ANY,
      .access = PL1_W, .accessfn = ats_access,
      .writefn = ats_write, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC },
};

static const ARMCPRegInfo v8_ats_reginfo[] = {
    { .name = "AT_S1E1R", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 8, .opc2 = 0,
      .access = PL1_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .fgt = FGT_ATS1E1R,
      .accessfn = at_s1e01_access, .writefn = ats_write64 },
    { .name = "AT_S1E1W", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 8, .opc2 = 1,
      .access = PL1_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .fgt = FGT_ATS1E1W,
      .accessfn = at_s1e01_access, .writefn = ats_write64 },
    { .name = "AT_S1E0R", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 8, .opc2 = 2,
      .access = PL1_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .fgt = FGT_ATS1E0R,
      .accessfn = at_s1e01_access, .writefn = ats_write64 },
    { .name = "AT_S1E0W", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 8, .opc2 = 3,
      .access = PL1_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .fgt = FGT_ATS1E0W,
      .accessfn = at_s1e01_access, .writefn = ats_write64 },
    { .name = "AT_S12E1R", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 7, .crm = 8, .opc2 = 4,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .accessfn = at_e012_access, .writefn = ats_write64 },
    { .name = "AT_S12E1W", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 7, .crm = 8, .opc2 = 5,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .accessfn = at_e012_access, .writefn = ats_write64 },
    { .name = "AT_S12E0R", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 7, .crm = 8, .opc2 = 6,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .accessfn = at_e012_access, .writefn = ats_write64 },
    { .name = "AT_S12E0W", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 7, .crm = 8, .opc2 = 7,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .accessfn = at_e012_access, .writefn = ats_write64 },
    { .name = "AT_S1E3R", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 7, .crm = 8, .opc2 = 0,
      .access = PL3_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .writefn = ats_write64 },
    { .name = "AT_S1E3W", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 7, .crm = 8, .opc2 = 1,
      .access = PL3_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .writefn = ats_write64 },
};

static const ARMCPRegInfo el2_ats_reginfo[] = {
    { .name = "AT_S1E2R", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 7, .crm = 8, .opc2 = 0,
      .access = PL2_W, .accessfn = at_s1e2_access,
      .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = ats_write64 },
    { .name = "AT_S1E2W", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 7, .crm = 8, .opc2 = 1,
      .access = PL2_W, .accessfn = at_s1e2_access,
      .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = ats_write64 },
    { .name = "ATS1HR", .cp = 15, .opc1 = 4, .crn = 7, .crm = 8, .opc2 = 0,
      .access = PL2_W,
      .writefn = ats1h_write, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC },
    { .name = "ATS1HW", .cp = 15, .opc1 = 4, .crn = 7, .crm = 8, .opc2 = 1,
      .access = PL2_W,
      .writefn = ats1h_write, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC },
};

static const ARMCPRegInfo ats1e1_reginfo[] = {
    { .name = "AT_S1E1RP", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 9, .opc2 = 0,
      .access = PL1_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .fgt = FGT_ATS1E1RP,
      .accessfn = at_s1e01_access, .writefn = ats_write64 },
    { .name = "AT_S1E1WP", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 9, .opc2 = 1,
      .access = PL1_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .fgt = FGT_ATS1E1WP,
      .accessfn = at_s1e01_access, .writefn = ats_write64 },
};

static const ARMCPRegInfo ats1cp_reginfo[] = {
    { .name = "ATS1CPRP",
      .cp = 15, .opc1 = 0, .crn = 7, .crm = 9, .opc2 = 0,
      .access = PL1_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .writefn = ats_write },
    { .name = "ATS1CPWP",
      .cp = 15, .opc1 = 0, .crn = 7, .crm = 9, .opc2 = 1,
      .access = PL1_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .writefn = ats_write },
};

static void ats_s1e1a(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value)
{
    uint64_t hcr_el2 = arm_hcr_el2_eff(env);
    bool regime_e20 = (hcr_el2 & (HCR_E2H | HCR_TGE)) == (HCR_E2H | HCR_TGE);
    ARMMMUIdx mmu_idx = regime_e20 ? ARMMMUIdx_E20_2 : ARMMMUIdx_Stage1_E1;
    ARMSecuritySpace ss = arm_security_space_below_el3(env);

    env->cp15.par_el[1] = do_ats_write(env, value, 0, mmu_idx, ss);
}

static void ats_s1e2a(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value)
{
    uint64_t hcr_el2 = arm_hcr_el2_eff(env);
    ARMMMUIdx mmu_idx = hcr_el2 & HCR_E2H ? ARMMMUIdx_E20_2 : ARMMMUIdx_E2;
    ARMSecuritySpace ss = arm_security_space_below_el3(env);

    env->cp15.par_el[1] = do_ats_write(env, value, 0, mmu_idx, ss);
}

static void ats_s1e3a(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value)
{
    env->cp15.par_el[1] = do_ats_write(env, value, 0, ARMMMUIdx_E3,
                                       arm_security_space(env));
}

static const ARMCPRegInfo ats1a_reginfo[] = {
    { .name = "AT_S1E1A", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 9, .opc2 = 2,
      .access = PL1_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .fgt = FGT_ATS1E1A,
      .accessfn = at_s1e01_access, .writefn = ats_s1e1a },
    { .name = "AT_S1E2A", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 7, .crm = 9, .opc2 = 2,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .accessfn = at_s1e2_access, .writefn = ats_s1e2a },
    { .name = "AT_S1E3A", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 7, .crm = 9, .opc2 = 2,
      .access = PL3_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .writefn = ats_s1e3a },
};

void define_at_insn_regs(ARMCPU *cpu)
{
    CPUARMState *env = &cpu->env;

    if (arm_feature(env, ARM_FEATURE_VAPA)) {
        define_arm_cp_regs(cpu, vapa_ats_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_V8)) {
        define_arm_cp_regs(cpu, v8_ats_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_EL2)
        || (arm_feature(env, ARM_FEATURE_EL3)
            && arm_feature(env, ARM_FEATURE_V8))) {
        define_arm_cp_regs(cpu, el2_ats_reginfo);
    }
    if (cpu_isar_feature(aa64_ats1e1, cpu)) {
        define_arm_cp_regs(cpu, ats1e1_reginfo);
    }
    if (cpu_isar_feature(aa32_ats1e1, cpu)) {
        define_arm_cp_regs(cpu, ats1cp_reginfo);
    }
    if (cpu_isar_feature(aa64_ats1a, cpu)) {
        define_arm_cp_regs(cpu, ats1a_reginfo);
    }
}
