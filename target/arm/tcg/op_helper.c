/*
 *  ARM helper routines
 *
 *  Copyright (c) 2005-2007 CodeSourcery, LLC
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
#include "cpu.h"
#include "exec/helper-proto.h"
#include "exec/target_page.h"
#include "internals.h"
#include "cpu-features.h"
#include "accel/tcg/cpu-ldst.h"
#include "accel/tcg/probe.h"
#include "cpregs.h"

#define SIGNBIT (uint32_t)0x80000000
#define SIGNBIT64 ((uint64_t)1 << 63)

int exception_target_el(CPUARMState *env)
{
    int target_el = MAX(1, arm_current_el(env));

    if (arm_is_secure(env) && !arm_el_is_aa64(env, 3) && target_el == 1) {
        target_el = 3;
    }

    return target_el;
}

void raise_exception(CPUARMState *env, uint32_t excp,
                     uint64_t syndrome, uint32_t target_el)
{
    CPUState *cs = env_cpu(env);

    if (target_el == 1 && (arm_hcr_el2_eff(env) & HCR_TGE)) {
        target_el = 2;
        if (syn_get_ec(syndrome) == EC_ADVSIMDFPACCESSTRAP) {
            syndrome = syn_uncategorized();
        }
    }

    assert(!excp_is_internal(excp));
    cs->exception_index = excp;
    env->exception.syndrome = syndrome;
    env->exception.target_el = target_el;
    cpu_loop_exit(cs);
}

void raise_exception_ra(CPUARMState *env, uint32_t excp, uint64_t syndrome,
                        uint32_t target_el, uintptr_t ra)
{
    CPUState *cs = env_cpu(env);

    cpu_restore_state(cs, ra);
    raise_exception(env, excp, syndrome, target_el);
}

uint64_t HELPER(neon_tbl)(CPUARMState *env, uint32_t desc,
                          uint64_t ireg, uint64_t def)
{
    uint64_t tmp, val = 0;
    uint32_t maxindex = ((desc & 3) + 1) * 8;
    uint32_t base_reg = desc >> 2;
    uint32_t shift, index, reg;

    for (shift = 0; shift < 64; shift += 8) {
        index = (ireg >> shift) & 0xff;
        if (index < maxindex) {
            reg = base_reg + (index >> 3);
            tmp = *aa32_vfp_dreg(env, reg);
            tmp = ((tmp >> ((index & 7) << 3)) & 0xff) << shift;
        } else {
            tmp = def & (0xffull << shift);
        }
        val |= tmp;
    }
    return val;
}

void HELPER(v8m_stackcheck)(CPUARMState *env, uint32_t newvalue)
{
    if (newvalue < v7m_sp_limit(env)) {
        raise_exception_ra(env, EXCP_STKOF, 0, 1, GETPC());
    }
}

uint32_t HELPER(sxtb16)(uint32_t x)
{
    uint32_t res;
    res = (uint16_t)(int8_t)x;
    res |= (uint32_t)(int8_t)(x >> 16) << 16;
    return res;
}

static void handle_possible_div0_trap(CPUARMState *env, uintptr_t ra)
{
    if (arm_feature(env, ARM_FEATURE_M)
        && (env->v7m.ccr[env->v7m.secure] & R_V7M_CCR_DIV_0_TRP_MASK)) {
        raise_exception_ra(env, EXCP_DIVBYZERO, 0, 1, ra);
    }
}

uint32_t HELPER(uxtb16)(uint32_t x)
{
    uint32_t res;
    res = (uint16_t)(uint8_t)x;
    res |= (uint32_t)(uint8_t)(x >> 16) << 16;
    return res;
}

int32_t HELPER(sdiv)(CPUARMState *env, int32_t num, int32_t den)
{
    if (den == 0) {
        handle_possible_div0_trap(env, GETPC());
        return 0;
    }
    if (num == INT_MIN && den == -1) {
        return INT_MIN;
    }
    return num / den;
}

uint32_t HELPER(udiv)(CPUARMState *env, uint32_t num, uint32_t den)
{
    if (den == 0) {
        handle_possible_div0_trap(env, GETPC());
        return 0;
    }
    return num / den;
}

uint32_t HELPER(rbit)(uint32_t x)
{
    return revbit32(x);
}

uint32_t HELPER(add_setq)(CPUARMState *env, uint32_t a, uint32_t b)
{
    uint32_t res = a + b;
    if (((res ^ a) & SIGNBIT) && !((a ^ b) & SIGNBIT))
        env->QF = 1;
    return res;
}

uint32_t HELPER(add_saturate)(CPUARMState *env, uint32_t a, uint32_t b)
{
    uint32_t res = a + b;
    if (((res ^ a) & SIGNBIT) && !((a ^ b) & SIGNBIT)) {
        env->QF = 1;
        res = ~(((int32_t)a >> 31) ^ SIGNBIT);
    }
    return res;
}

uint32_t HELPER(sub_saturate)(CPUARMState *env, uint32_t a, uint32_t b)
{
    uint32_t res = a - b;
    if (((res ^ a) & SIGNBIT) && ((a ^ b) & SIGNBIT)) {
        env->QF = 1;
        res = ~(((int32_t)a >> 31) ^ SIGNBIT);
    }
    return res;
}

uint32_t HELPER(add_usaturate)(CPUARMState *env, uint32_t a, uint32_t b)
{
    uint32_t res = a + b;
    if (res < a) {
        env->QF = 1;
        res = ~0;
    }
    return res;
}

uint32_t HELPER(sub_usaturate)(CPUARMState *env, uint32_t a, uint32_t b)
{
    uint32_t res = a - b;
    if (res > a) {
        env->QF = 1;
        res = 0;
    }
    return res;
}

static inline uint32_t do_ssat(CPUARMState *env, int32_t val, int shift)
{
    int32_t top;
    uint32_t mask;

    top = val >> shift;
    mask = (1u << shift) - 1;
    if (top > 0) {
        env->QF = 1;
        return mask;
    } else if (top < -1) {
        env->QF = 1;
        return ~mask;
    }
    return val;
}

static inline uint32_t do_usat(CPUARMState *env, int32_t val, int shift)
{
    uint32_t max;

    max = (1u << shift) - 1;
    if (val < 0) {
        env->QF = 1;
        return 0;
    } else if (val > max) {
        env->QF = 1;
        return max;
    }
    return val;
}

uint32_t HELPER(ssat)(CPUARMState *env, uint32_t x, uint32_t shift)
{
    return do_ssat(env, x, shift);
}

uint32_t HELPER(ssat16)(CPUARMState *env, uint32_t x, uint32_t shift)
{
    uint32_t res;

    res = (uint16_t)do_ssat(env, (int16_t)x, shift);
    res |= do_ssat(env, ((int32_t)x) >> 16, shift) << 16;
    return res;
}

uint32_t HELPER(usat)(CPUARMState *env, uint32_t x, uint32_t shift)
{
    return do_usat(env, x, shift);
}

uint32_t HELPER(usat16)(CPUARMState *env, uint32_t x, uint32_t shift)
{
    uint32_t res;

    res = (uint16_t)do_usat(env, (int16_t)x, shift);
    res |= do_usat(env, ((int32_t)x) >> 16, shift) << 16;
    return res;
}

void HELPER(setend)(CPUARMState *env)
{
    env->uncached_cpsr ^= CPSR_E;
    arm_rebuild_hflags(env);
}

void HELPER(check_bxj_trap)(CPUARMState *env, uint32_t rm)
{
    if (env->cp15.hstr_el2 & HSTR_TJDBX) {
        uint32_t syn = syn_bxjtrap(1, 0xe, rm);
        raise_exception_ra(env, EXCP_HYP_TRAP, syn, 2, GETPC());
    }
}

#ifndef CONFIG_USER_ONLY
static inline int check_wfx_trap(CPUARMState *env, bool is_wfe, uint32_t *excp)
{
    int cur_el = arm_current_el(env);
    uint64_t mask;

    *excp = EXCP_UDEF;

    if (arm_feature(env, ARM_FEATURE_M)) {
        return 0;
    }

    if (cur_el < 1 && arm_feature(env, ARM_FEATURE_V8)) {
        mask = is_wfe ? SCTLR_nTWE : SCTLR_nTWI;
        if (!(arm_sctlr(env, cur_el) & mask)) {
            return exception_target_el(env);
        }
    }

    if (cur_el < 2) {
        mask = is_wfe ? HCR_TWE : HCR_TWI;
        if (arm_hcr_el2_eff(env) & mask) {
            return 2;
        }
    }

    if (arm_feature(env, ARM_FEATURE_V8) && !arm_is_el3_or_mon(env)) {
        mask = (is_wfe) ? SCR_TWE : SCR_TWI;
        if (env->cp15.scr_el3 & mask) {
            if (!arm_el_is_aa64(env, 3)) {
                *excp = EXCP_MON_TRAP;
            }
            return 3;
        }
    }

    return 0;
}
#endif

void HELPER(wfi)(CPUARMState *env, uint32_t insn_len)
{
#ifdef CONFIG_USER_ONLY
    return;
#else
    CPUState *cs = env_cpu(env);
    uint32_t excp;
    int target_el = check_wfx_trap(env, false, &excp);

    if (cpu_has_work(cs)) {
        return;
    }

    if (target_el) {
        if (env->aarch64) {
            env->pc -= insn_len;
        } else {
            env->regs[15] -= insn_len;
        }

        raise_exception(env, excp, syn_wfx(1, 0xe, 0, insn_len == 2),
                        target_el);
    }

    cs->exception_index = EXCP_HLT;
    cs->halted = 1;
    cpu_loop_exit(cs);
#endif
}

void HELPER(wfit)(CPUARMState *env, uint64_t timeout)
{
#ifdef CONFIG_USER_ONLY
    return;
#else
    ARMCPU *cpu = env_archcpu(env);
    CPUState *cs = env_cpu(env);
    uint32_t excp;
    int target_el = check_wfx_trap(env, false, &excp);
    uint64_t cntval = gt_get_countervalue(env);
    uint64_t offset = gt_direct_access_timer_offset(env, GTIMER_VIRT);
    uint64_t cntvct = cntval - offset;
    uint64_t nexttick;

    if (cpu_has_work(cs) || cntvct >= timeout) {
        return;
    }

    if (target_el) {
        env->pc -= 4;
        raise_exception(env, excp, syn_wfx(1, 0xe, 0, false), target_el);
    }

    if (uadd64_overflow(timeout, offset, &nexttick)) {
        nexttick = UINT64_MAX;
    }
    if (nexttick > INT64_MAX / gt_cntfrq_period_ns(cpu)) {
        timer_mod_ns(cpu->wfxt_timer, INT64_MAX);
    } else {
        timer_mod(cpu->wfxt_timer, nexttick);
    }
    cs->exception_index = EXCP_HLT;
    cs->halted = 1;
    cpu_loop_exit(cs);
#endif
}

void HELPER(wfe)(CPUARMState *env)
{
    HELPER(yield)(env);
}

void HELPER(yield)(CPUARMState *env)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = EXCP_YIELD;
    cpu_loop_exit(cs);
}

void HELPER(exception_internal)(CPUARMState *env, uint32_t excp)
{
    CPUState *cs = env_cpu(env);

    assert(excp_is_internal(excp));
    cs->exception_index = excp;
    cpu_loop_exit(cs);
}

void HELPER(exception_with_syndrome_el)(CPUARMState *env, uint32_t excp,
                                        uint32_t syndrome, uint32_t target_el)
{
    raise_exception(env, excp, syndrome, target_el);
}

void HELPER(exception_with_syndrome)(CPUARMState *env, uint32_t excp,
                                     uint32_t syndrome)
{
    raise_exception(env, excp, syndrome, exception_target_el(env));
}

uint32_t HELPER(cpsr_read)(CPUARMState *env)
{
    return cpsr_read(env) & ~CPSR_EXEC;
}

void HELPER(cpsr_write)(CPUARMState *env, uint32_t val, uint32_t mask)
{
    cpsr_write(env, val, mask, CPSRWriteByInstr);
    arm_rebuild_hflags(env);
}

void HELPER(cpsr_write_eret)(CPUARMState *env, uint32_t val)
{
    uint32_t mask;

    bql_lock();
    arm_call_pre_el_change_hook(env_archcpu(env));
    bql_unlock();

    mask = aarch32_cpsr_valid_mask(env->features, &env_archcpu(env)->isar);
    cpsr_write(env, val, mask, CPSRWriteExceptionReturn);

    env->regs[15] &= (env->thumb ? ~1 : ~3);
    arm_rebuild_hflags(env);

    bql_lock();
    arm_call_el_change_hook(env_archcpu(env));
    bql_unlock();
}

uint32_t HELPER(get_user_reg)(CPUARMState *env, uint32_t regno)
{
    uint32_t val;

    if (regno == 13) {
        val = env->banked_r13[BANK_USRSYS];
    } else if (regno == 14) {
        val = env->banked_r14[BANK_USRSYS];
    } else if (regno >= 8
               && (env->uncached_cpsr & 0x1f) == ARM_CPU_MODE_FIQ) {
        val = env->usr_regs[regno - 8];
    } else {
        val = env->regs[regno];
    }
    return val;
}

void HELPER(set_user_reg)(CPUARMState *env, uint32_t regno, uint32_t val)
{
    if (regno == 13) {
        env->banked_r13[BANK_USRSYS] = val;
    } else if (regno == 14) {
        env->banked_r14[BANK_USRSYS] = val;
    } else if (regno >= 8
               && (env->uncached_cpsr & 0x1f) == ARM_CPU_MODE_FIQ) {
        env->usr_regs[regno - 8] = val;
    } else {
        env->regs[regno] = val;
    }
}

void HELPER(set_r13_banked)(CPUARMState *env, uint32_t mode, uint32_t val)
{
    if ((env->uncached_cpsr & CPSR_M) == mode) {
        env->regs[13] = val;
    } else {
        env->banked_r13[bank_number(mode)] = val;
    }
}

uint32_t HELPER(get_r13_banked)(CPUARMState *env, uint32_t mode)
{
    if ((env->uncached_cpsr & CPSR_M) == ARM_CPU_MODE_SYS) {
        raise_exception(env, EXCP_UDEF, syn_uncategorized(),
                        exception_target_el(env));
    }

    if ((env->uncached_cpsr & CPSR_M) == mode) {
        return env->regs[13];
    } else {
        return env->banked_r13[bank_number(mode)];
    }
}

static void msr_mrs_banked_exc_checks(CPUARMState *env, uint32_t tgtmode,
                                      uint32_t regno)
{
    int curmode = env->uncached_cpsr & CPSR_M;

    if (tgtmode == ARM_CPU_MODE_HYP) {
        switch (regno) {
        case 16 ... 17:
            if (curmode != ARM_CPU_MODE_HYP && curmode != ARM_CPU_MODE_MON) {
                goto undef;
            }
            break;
        case 13:
            if (curmode != ARM_CPU_MODE_MON) {
                goto undef;
            }
            break;
        default:
            g_assert_not_reached();
        }
        return;
    }

    if (curmode == tgtmode) {
        goto undef;
    }

    if (tgtmode == ARM_CPU_MODE_USR) {
        switch (regno) {
        case 8 ... 12:
            if (curmode != ARM_CPU_MODE_FIQ) {
                goto undef;
            }
            break;
        case 13:
            if (curmode == ARM_CPU_MODE_SYS) {
                goto undef;
            }
            break;
        case 14:
            if (curmode == ARM_CPU_MODE_HYP || curmode == ARM_CPU_MODE_SYS) {
                goto undef;
            }
            break;
        default:
            break;
        }
    }

    return;

undef:
    raise_exception(env, EXCP_UDEF, syn_uncategorized(),
                    exception_target_el(env));
}

void HELPER(msr_banked)(CPUARMState *env, uint32_t value, uint32_t tgtmode,
                        uint32_t regno)
{
    msr_mrs_banked_exc_checks(env, tgtmode, regno);

    switch (regno) {
    case 16:
        if (tgtmode == (env->uncached_cpsr & CPSR_M)) {
            env->spsr = value;
        } else {
            env->banked_spsr[bank_number(tgtmode)] = value;
        }
        break;
    case 17:
        env->elr_el[2] = value;
        break;
    case 13:
        env->banked_r13[bank_number(tgtmode)] = value;
        break;
    case 14:
        env->banked_r14[r14_bank_number(tgtmode)] = value;
        break;
    case 8 ... 12:
        switch (tgtmode) {
        case ARM_CPU_MODE_USR:
            env->usr_regs[regno - 8] = value;
            break;
        case ARM_CPU_MODE_FIQ:
            env->fiq_regs[regno - 8] = value;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    default:
        g_assert_not_reached();
    }
}

uint32_t HELPER(mrs_banked)(CPUARMState *env, uint32_t tgtmode, uint32_t regno)
{
    msr_mrs_banked_exc_checks(env, tgtmode, regno);

    switch (regno) {
    case 16:
        if (tgtmode == (env->uncached_cpsr & CPSR_M)) {
            return env->spsr;
        } else {
            return env->banked_spsr[bank_number(tgtmode)];
        }
    case 17:
        return env->elr_el[2];
    case 13:
        return env->banked_r13[bank_number(tgtmode)];
    case 14:
        return env->banked_r14[r14_bank_number(tgtmode)];
    case 8 ... 12:
        switch (tgtmode) {
        case ARM_CPU_MODE_USR:
            return env->usr_regs[regno - 8];
        case ARM_CPU_MODE_FIQ:
            return env->fiq_regs[regno - 8];
        default:
            g_assert_not_reached();
        }
    default:
        g_assert_not_reached();
    }
}

const void *HELPER(access_check_cp_reg)(CPUARMState *env, uint32_t key,
                                        uint32_t syndrome, uint32_t isread)
{
    ARMCPU *cpu = env_archcpu(env);
    const ARMCPRegInfo *ri = get_arm_cp_reginfo(cpu->cp_regs, key);
    CPAccessResult res = CP_ACCESS_OK;
    int target_el;
    uint32_t excp;

    assert(ri != NULL);

    if (ri->accessfn) {
        res = ri->accessfn(env, ri, isread);
    }

    if (res != CP_ACCESS_OK && ((res & CP_ACCESS_EL_MASK) < 2) &&
        arm_current_el(env) == 0) {
        goto fail;
    }

    if (!is_a64(env) && arm_current_el(env) == 0 && ri->cp == 15 &&
        arm_is_el2_enabled(env) &&
        (arm_hcr_el2_eff(env) & (HCR_E2H | HCR_TGE)) != (HCR_E2H | HCR_TGE)) {
        uint32_t mask = 1 << ri->crn;

        if (ri->type & ARM_CP_64BIT) {
            mask = 1 << ri->crm;
        }

        mask &= ~((1 << 4) | (1 << 14));

        if (env->cp15.hstr_el2 & mask) {
            res = CP_ACCESS_TRAP_EL2;
            goto fail;
        }
    }

    if (arm_fgt_active(env, arm_current_el(env))) {
        uint64_t trapword = 0;
        unsigned int idx = FIELD_EX32(ri->fgt, FGT, IDX);
        unsigned int bitpos = FIELD_EX32(ri->fgt, FGT, BITPOS);
        bool rev = FIELD_EX32(ri->fgt, FGT, REV);
        bool nxs = FIELD_EX32(ri->fgt, FGT, NXS);
        bool trapbit;

        if (ri->fgt & FGT_EXEC) {
            assert(idx < ARRAY_SIZE(env->cp15.fgt_exec));
            trapword = env->cp15.fgt_exec[idx];
        } else if (isread && (ri->fgt & FGT_R)) {
            assert(idx < ARRAY_SIZE(env->cp15.fgt_read));
            trapword = env->cp15.fgt_read[idx];
        } else if (!isread && (ri->fgt & FGT_W)) {
            assert(idx < ARRAY_SIZE(env->cp15.fgt_write));
            trapword = env->cp15.fgt_write[idx];
        }

        if (nxs && (arm_hcrx_el2_eff(env) & HCRX_FGTNXS)) {
            trapbit = 0;
        } else {
            trapbit = extract64(trapword, bitpos, 1);
        }
        if (trapbit != rev) {
            res = CP_ACCESS_TRAP_EL2;
            goto fail;
        }
    }

    if (likely(res == CP_ACCESS_OK)) {
        return ri;
    }

 fail:
    excp = EXCP_UDEF;
    switch (res) {
    case CP_ACCESS_TRAP_EL3:
        if (!arm_el_is_aa64(env, 3)) {
            excp = EXCP_MON_TRAP;
        }
        break;
    case CP_ACCESS_TRAP_EL2:
    case CP_ACCESS_TRAP_EL1:
        break;
    case CP_ACCESS_UNDEFINED:
        if (cpu_isar_feature(aa64_ids, cpu) && isread &&
            arm_cpreg_in_idspace(ri)) {
            break;
        }
        syndrome = syn_uncategorized();
        break;
    case CP_ACCESS_EXLOCK:
        syndrome = syn_gcs_exlock();
        break;
    default:
        g_assert_not_reached();
    }

    target_el = res & CP_ACCESS_EL_MASK;
    switch (target_el) {
    case 0:
        target_el = exception_target_el(env);
        break;
    case 1:
        assert(arm_current_el(env) < 2);
        break;
    case 2:
        assert(arm_current_el(env) != 3);
        assert(arm_is_el2_enabled(env));
        break;
    case 3:
        assert(arm_feature(env, ARM_FEATURE_EL3));
        break;
    default:
        g_assert_not_reached();
    }

    raise_exception(env, excp, syndrome, target_el);
}

const void *HELPER(lookup_cp_reg)(CPUARMState *env, uint32_t key)
{
    ARMCPU *cpu = env_archcpu(env);
    const ARMCPRegInfo *ri = get_arm_cp_reginfo(cpu->cp_regs, key);

    assert(ri != NULL);
    return ri;
}

void HELPER(tidcp_el1)(CPUARMState *env, uint32_t syndrome)
{
    if (arm_hcr_el2_eff(env) & HCR_TIDCP) {
        raise_exception_ra(env, EXCP_UDEF, syndrome, 2, GETPC());
    }
}

void HELPER(tidcp_el0)(CPUARMState *env, uint32_t syndrome)
{
    ARMMMUIdx mmu_idx = arm_mmu_idx_el(env, 0);
    int target_el;

    switch (mmu_idx) {
    case ARMMMUIdx_E20_0:
        target_el = 2;
        break;
    case ARMMMUIdx_E30_0:
        target_el = 3;
        break;
    default:
        target_el = 1;
        break;
    }

    if ((env->cp15.sctlr_el[target_el] & SCTLR_TIDCP)
        && arm_el_is_aa64(env, target_el)) {
        raise_exception_ra(env, EXCP_UDEF, syndrome, target_el, GETPC());
    }
}

void HELPER(set_cp_reg)(CPUARMState *env, const void *rip, uint32_t value)
{
    const ARMCPRegInfo *ri = rip;

    if (ri->type & ARM_CP_IO) {
        bql_lock();
        ri->writefn(env, ri, value);
        bql_unlock();
    } else {
        ri->writefn(env, ri, value);
    }
}

uint32_t HELPER(get_cp_reg)(CPUARMState *env, const void *rip)
{
    const ARMCPRegInfo *ri = rip;
    uint32_t res;

    if (ri->type & ARM_CP_IO) {
        bql_lock();
        res = ri->readfn(env, ri);
        bql_unlock();
    } else {
        res = ri->readfn(env, ri);
    }

    return res;
}

void HELPER(set_cp_reg64)(CPUARMState *env, const void *rip, uint64_t value)
{
    const ARMCPRegInfo *ri = rip;

    if (ri->type & ARM_CP_IO) {
        bql_lock();
        ri->writefn(env, ri, value);
        bql_unlock();
    } else {
        ri->writefn(env, ri, value);
    }
}

uint64_t HELPER(get_cp_reg64)(CPUARMState *env, const void *rip)
{
    const ARMCPRegInfo *ri = rip;
    uint64_t res;

    if (ri->type & ARM_CP_IO) {
        bql_lock();
        res = ri->readfn(env, ri);
        bql_unlock();
    } else {
        res = ri->readfn(env, ri);
    }

    return res;
}

void HELPER(pre_hvc)(CPUARMState *env)
{
    ARMCPU *cpu = env_archcpu(env);
    int cur_el = arm_current_el(env);
    bool secure = false;
    bool undef;

    if (arm_is_psci_call(cpu, EXCP_HVC)) {
        return;
    }

    if (!arm_feature(env, ARM_FEATURE_EL2)) {
        undef = true;
    } else if (arm_feature(env, ARM_FEATURE_EL3)) {
        undef = !(env->cp15.scr_el3 & SCR_HCE);
    } else {
        undef = env->cp15.hcr_el2 & HCR_HCD;
    }

    if (secure && (!is_a64(env) || cur_el == 1)) {
        undef = true;
    }

    if (undef) {
        raise_exception(env, EXCP_UDEF, syn_uncategorized(),
                        exception_target_el(env));
    }
}

void HELPER(pre_smc)(CPUARMState *env, uint32_t syndrome)
{
    ARMCPU *cpu = env_archcpu(env);
    int cur_el = arm_current_el(env);
    bool secure = arm_is_secure(env);
    bool smd_flag = env->cp15.scr_el3 & SCR_SMD;

    bool smd = arm_feature(env, ARM_FEATURE_AARCH64) ? smd_flag
                                                     : smd_flag && !secure;

    if (!arm_feature(env, ARM_FEATURE_EL3) &&
        !(arm_hcr_el2_eff(env) & HCR_NV) &&
        cpu->psci_conduit != QEMU_PSCI_CONDUIT_SMC) {
        raise_exception(env, EXCP_UDEF, syn_uncategorized(),
                        exception_target_el(env));
    }

    if (cur_el == 1 && (arm_hcr_el2_eff(env) & HCR_TSC)) {
        raise_exception(env, EXCP_HYP_TRAP, syndrome, 2);
    }

    if (!arm_is_psci_call(cpu, EXCP_SMC) &&
        (smd || !arm_feature(env, ARM_FEATURE_EL3))) {
        raise_exception(env, EXCP_UDEF, syn_uncategorized(),
                        exception_target_el(env));
    }
}

uint32_t HELPER(shl_cc)(CPUARMState *env, uint32_t x, uint32_t i)
{
    int shift = i & 0xff;
    if (shift >= 32) {
        if (shift == 32)
            env->CF = x & 1;
        else
            env->CF = 0;
        return 0;
    } else if (shift != 0) {
        env->CF = (x >> (32 - shift)) & 1;
        return x << shift;
    }
    return x;
}

uint32_t HELPER(shr_cc)(CPUARMState *env, uint32_t x, uint32_t i)
{
    int shift = i & 0xff;
    if (shift >= 32) {
        if (shift == 32)
            env->CF = (x >> 31) & 1;
        else
            env->CF = 0;
        return 0;
    } else if (shift != 0) {
        env->CF = (x >> (shift - 1)) & 1;
        return x >> shift;
    }
    return x;
}

uint32_t HELPER(sar_cc)(CPUARMState *env, uint32_t x, uint32_t i)
{
    int shift = i & 0xff;
    if (shift >= 32) {
        env->CF = (x >> 31) & 1;
        return (int32_t)x >> 31;
    } else if (shift != 0) {
        env->CF = (x >> (shift - 1)) & 1;
        return (int32_t)x >> shift;
    }
    return x;
}

uint32_t HELPER(ror_cc)(CPUARMState *env, uint32_t x, uint32_t i)
{
    int shift1, shift;
    shift1 = i & 0xff;
    shift = shift1 & 0x1f;
    if (shift == 0) {
        if (shift1 != 0)
            env->CF = (x >> 31) & 1;
        return x;
    } else {
        env->CF = (x >> (shift - 1)) & 1;
        return ((uint32_t)x >> shift) | (x << (32 - shift));
    }
}

void HELPER(probe_access)(CPUARMState *env, vaddr ptr,
                          uint32_t access_type, uint32_t mmu_idx,
                          uint32_t size)
{
    uint32_t in_page = -((uint32_t)ptr | TARGET_PAGE_SIZE);
    uintptr_t ra = GETPC();

    if (likely(size <= in_page)) {
        probe_access(env, ptr, size, access_type, mmu_idx, ra);
    } else {
        probe_access(env, ptr, in_page, access_type, mmu_idx, ra);
        probe_access(env, ptr + in_page, size - in_page,
                     access_type, mmu_idx, ra);
    }
}

void HELPER(vesb)(CPUARMState *env)
{
    uint64_t hcr = arm_hcr_el2_eff(env);
    bool enabled = !(hcr & HCR_TGE) && (hcr & HCR_AMO);
    bool pending = enabled && (hcr & HCR_VSE);
    bool masked  = (env->daif & PSTATE_A);

    if (pending && masked) {
        uint32_t syndrome;

        if (arm_el_is_aa64(env, 1)) {
            syndrome = env->cp15.vsesr_el2 & 0x1ffffff;
        } else {
            ARMMMUFaultInfo fi = { .type = ARMFault_AsyncExternal };

            if (extended_addresses_enabled(env)) {
                syndrome = arm_fi_to_lfsc(&fi);
            } else {
                syndrome = arm_fi_to_sfsc(&fi);
            }
            syndrome |= env->cp15.vsesr_el2 & 0xd000;
        }

        env->cp15.vdisr_el2 = syndrome | (1u << 31);

        env->cp15.hcr_el2 &= ~HCR_VSE;
        cpu_reset_interrupt(env_cpu(env), CPU_INTERRUPT_VSERR);
    }
}
