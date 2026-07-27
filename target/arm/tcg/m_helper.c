/*
 * ARM generic helpers.
 *
 * This code is licensed under the GNU GPL v2 or later.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "cpu-features.h"
#include "gdbstub/helpers.h"
#include "exec/helper-proto.h"
#include "qemu/main-loop.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "exec/page-protection.h"
#ifdef CONFIG_TCG
#include "accel/tcg/cpu-ldst.h"
#include "semihosting/common-semi.h"
#endif
#if !defined(CONFIG_USER_ONLY)
#include "hw/intc/armv7m_nvic.h"
#endif
#include "qemu/plugin.h"

static void v7m_msr_xpsr(CPUARMState *env, uint32_t mask,
                         uint32_t reg, uint32_t val)
{
    if (!(reg & 4)) {
        uint32_t apsrmask = 0;

        if (mask & 8) {
            apsrmask |= XPSR_NZCV | XPSR_Q;
        }
        if ((mask & 4) && arm_feature(env, ARM_FEATURE_THUMB_DSP)) {
            apsrmask |= XPSR_GE;
        }
        xpsr_write(env, val, apsrmask);
    }
}

static uint32_t v7m_mrs_xpsr(CPUARMState *env, uint32_t reg, unsigned el)
{
    uint32_t mask = 0;

    if ((reg & 1) && el) {
        mask |= XPSR_EXCP;
    }
    if (!(reg & 4)) {
        mask |= XPSR_NZCV | XPSR_Q;
        if (arm_feature(env, ARM_FEATURE_THUMB_DSP)) {
            mask |= XPSR_GE;
        }
    }
    return xpsr_read(env) & mask;
}

uint32_t arm_v7m_mrs_control(CPUARMState *env, uint32_t secure)
{
    uint32_t value = env->v7m.control[secure];

    if (!secure) {
        value |= env->v7m.control[M_REG_S] & R_V7M_CONTROL_FPCA_MASK;
    }
    return value;
}

#ifdef CONFIG_USER_ONLY

void HELPER(v7m_msr)(CPUARMState *env, uint32_t maskreg, uint32_t val)
{
    uint32_t mask = extract32(maskreg, 8, 4);
    uint32_t reg = extract32(maskreg, 0, 8);

    switch (reg) {
    case 0 ... 7:
        v7m_msr_xpsr(env, mask, reg, val);
        break;
    case 20:
        break;
    default:
        break;
    }
}

uint32_t HELPER(v7m_mrs)(CPUARMState *env, uint32_t reg)
{
    switch (reg) {
    case 0 ... 7:
        return v7m_mrs_xpsr(env, reg, 0);
    case 20:
        return arm_v7m_mrs_control(env, 0);
    default:
        return 0;
    }
}

void HELPER(v7m_bxns)(CPUARMState *env, uint32_t dest)
{
    g_assert_not_reached();
}

void HELPER(v7m_blxns)(CPUARMState *env, uint32_t dest)
{
    g_assert_not_reached();
}

void HELPER(v7m_preserve_fp_state)(CPUARMState *env)
{
    g_assert_not_reached();
}

void HELPER(v7m_vlstm)(CPUARMState *env, uint32_t fptr)
{
    g_assert_not_reached();
}

void HELPER(v7m_vlldm)(CPUARMState *env, uint32_t fptr)
{
    g_assert_not_reached();
}

uint32_t HELPER(v7m_tt)(CPUARMState *env, uint32_t addr, uint32_t op)
{
    return 0;
}

ARMMMUIdx arm_v7m_mmu_idx_for_secstate(CPUARMState *env, bool secstate)
{
    return ARMMMUIdx_MUser;
}

#else

static ARMMMUIdx arm_v7m_mmu_idx_all(CPUARMState *env,
                                     bool secstate, bool priv, bool negpri)
{
    ARMMMUIdx mmu_idx = ARM_MMU_IDX_M;

    if (priv) {
        mmu_idx |= ARM_MMU_IDX_M_PRIV;
    }

    if (negpri) {
        mmu_idx |= ARM_MMU_IDX_M_NEGPRI;
    }

    if (secstate) {
        mmu_idx |= ARM_MMU_IDX_M_S;
    }

    return mmu_idx;
}

static ARMMMUIdx arm_v7m_mmu_idx_for_secstate_and_priv(CPUARMState *env,
                                                       bool secstate, bool priv)
{
    bool negpri = armv7m_nvic_neg_prio_requested(env->nvic, secstate);

    return arm_v7m_mmu_idx_all(env, secstate, priv, negpri);
}

ARMMMUIdx arm_v7m_mmu_idx_for_secstate(CPUARMState *env, bool secstate)
{
    bool priv = arm_v7m_is_handler_mode(env) ||
        !(env->v7m.control[secstate] & 1);

    return arm_v7m_mmu_idx_for_secstate_and_priv(env, secstate, priv);
}

typedef enum StackingMode {
    STACK_NORMAL,
    STACK_IGNFAULTS,
    STACK_LAZYFP,
} StackingMode;

static bool v7m_stack_write(ARMCPU *cpu, uint32_t addr, uint32_t value,
                            ARMMMUIdx mmu_idx, StackingMode mode)
{
    CPUState *cs = CPU(cpu);
    CPUARMState *env = &cpu->env;
    MemTxResult txres;
    GetPhysAddrResult res = {};
    ARMMMUFaultInfo fi = {};
    bool secure = mmu_idx & ARM_MMU_IDX_M_S;
    int exc;
    bool exc_secure;

    if (get_phys_addr(env, addr, MMU_DATA_STORE, 0, mmu_idx, &res, &fi)) {
        if (fi.type == ARMFault_QEMU_SFault) {
            if (mode == STACK_LAZYFP) {
                qemu_log_mask(CPU_LOG_INT,
                              "...SecureFault with SFSR.LSPERR "
                              "during lazy stacking\n");
                env->v7m.sfsr |= R_V7M_SFSR_LSPERR_MASK;
            } else {
                qemu_log_mask(CPU_LOG_INT,
                              "...SecureFault with SFSR.AUVIOL "
                              "during stacking\n");
                env->v7m.sfsr |= R_V7M_SFSR_AUVIOL_MASK;
            }
            env->v7m.sfsr |= R_V7M_SFSR_SFARVALID_MASK;
            env->v7m.sfar = addr;
            exc = ARMV7M_EXCP_SECURE;
            exc_secure = false;
        } else {
            if (mode == STACK_LAZYFP) {
                qemu_log_mask(CPU_LOG_INT,
                              "...MemManageFault with CFSR.MLSPERR\n");
                env->v7m.cfsr[secure] |= R_V7M_CFSR_MLSPERR_MASK;
            } else {
                qemu_log_mask(CPU_LOG_INT,
                              "...MemManageFault with CFSR.MSTKERR\n");
                env->v7m.cfsr[secure] |= R_V7M_CFSR_MSTKERR_MASK;
            }
            exc = ARMV7M_EXCP_MEM;
            exc_secure = secure;
        }
        goto pend_fault;
    }
    address_space_stl_le(arm_addressspace(cs, res.f.attrs), res.f.phys_addr,
                         value, res.f.attrs, &txres);
    if (txres != MEMTX_OK) {
        if (mode == STACK_LAZYFP) {
            qemu_log_mask(CPU_LOG_INT, "...BusFault with BFSR.LSPERR\n");
            env->v7m.cfsr[M_REG_NS] |= R_V7M_CFSR_LSPERR_MASK;
        } else {
            qemu_log_mask(CPU_LOG_INT, "...BusFault with BFSR.STKERR\n");
            env->v7m.cfsr[M_REG_NS] |= R_V7M_CFSR_STKERR_MASK;
        }
        exc = ARMV7M_EXCP_BUS;
        exc_secure = false;
        goto pend_fault;
    }
    return true;

pend_fault:
    switch (mode) {
    case STACK_NORMAL:
        armv7m_nvic_set_pending_derived(env->nvic, exc, exc_secure);
        break;
    case STACK_LAZYFP:
        armv7m_nvic_set_pending_lazyfp(env->nvic, exc, exc_secure);
        break;
    case STACK_IGNFAULTS:
        break;
    }
    return false;
}

static bool v7m_stack_read(ARMCPU *cpu, uint32_t *dest, uint32_t addr,
                           ARMMMUIdx mmu_idx)
{
    CPUState *cs = CPU(cpu);
    CPUARMState *env = &cpu->env;
    MemTxResult txres;
    GetPhysAddrResult res = {};
    ARMMMUFaultInfo fi = {};
    bool secure = mmu_idx & ARM_MMU_IDX_M_S;
    int exc;
    bool exc_secure;
    uint32_t value;

    if (get_phys_addr(env, addr, MMU_DATA_LOAD, 0, mmu_idx, &res, &fi)) {
        if (fi.type == ARMFault_QEMU_SFault) {
            qemu_log_mask(CPU_LOG_INT,
                          "...SecureFault with SFSR.AUVIOL during unstack\n");
            env->v7m.sfsr |= R_V7M_SFSR_AUVIOL_MASK | R_V7M_SFSR_SFARVALID_MASK;
            env->v7m.sfar = addr;
            exc = ARMV7M_EXCP_SECURE;
            exc_secure = false;
        } else {
            qemu_log_mask(CPU_LOG_INT,
                          "...MemManageFault with CFSR.MUNSTKERR\n");
            env->v7m.cfsr[secure] |= R_V7M_CFSR_MUNSTKERR_MASK;
            exc = ARMV7M_EXCP_MEM;
            exc_secure = secure;
        }
        goto pend_fault;
    }

    value = address_space_ldl(arm_addressspace(cs, res.f.attrs),
                              res.f.phys_addr, res.f.attrs, &txres);
    if (txres != MEMTX_OK) {
        qemu_log_mask(CPU_LOG_INT, "...BusFault with BFSR.UNSTKERR\n");
        env->v7m.cfsr[M_REG_NS] |= R_V7M_CFSR_UNSTKERR_MASK;
        exc = ARMV7M_EXCP_BUS;
        exc_secure = false;
        goto pend_fault;
    }

    *dest = value;
    return true;

pend_fault:
    armv7m_nvic_set_pending(env->nvic, exc, exc_secure);
    return false;
}

void HELPER(v7m_preserve_fp_state)(CPUARMState *env)
{
    ARMCPU *cpu = env_archcpu(env);
    bool is_secure = env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_S_MASK;
    bool negpri = !(env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_HFRDY_MASK);
    bool is_priv = !(env->v7m.fpccr[is_secure] & R_V7M_FPCCR_USER_MASK);
    bool splimviol = env->v7m.fpccr[is_secure] & R_V7M_FPCCR_SPLIMVIOL_MASK;
    uint32_t fpcar = env->v7m.fpcar[is_secure];
    bool stacked_ok = true;
    bool ts = is_secure && (env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_TS_MASK);
    bool take_exception;

    bql_lock();

    if (!v7m_cpacr_pass(env, is_secure, is_priv)) {
        armv7m_nvic_set_pending_lazyfp(env->nvic, ARMV7M_EXCP_USAGE, is_secure);
        env->v7m.cfsr[is_secure] |= R_V7M_CFSR_NOCP_MASK;
        stacked_ok = false;
    } else if (!is_secure && !extract32(env->v7m.nsacr, 10, 1)) {
        armv7m_nvic_set_pending_lazyfp(env->nvic, ARMV7M_EXCP_USAGE, M_REG_S);
        env->v7m.cfsr[M_REG_S] |= R_V7M_CFSR_NOCP_MASK;
        stacked_ok = false;
    }

    if (!splimviol && stacked_ok) {
        int i;
        ARMMMUIdx mmu_idx;

        mmu_idx = arm_v7m_mmu_idx_all(env, is_secure, is_priv, negpri);
        for (i = 0; i < (ts ? 32 : 16); i += 2) {
            uint64_t dn = *aa32_vfp_dreg(env, i / 2);
            uint32_t faddr = fpcar + 4 * i;
            uint32_t slo = extract64(dn, 0, 32);
            uint32_t shi = extract64(dn, 32, 32);

            if (i >= 16) {
                faddr += 8;
            }
            stacked_ok = stacked_ok &&
                v7m_stack_write(cpu, faddr, slo, mmu_idx, STACK_LAZYFP) &&
                v7m_stack_write(cpu, faddr + 4, shi, mmu_idx, STACK_LAZYFP);
        }

        stacked_ok = stacked_ok &&
            v7m_stack_write(cpu, fpcar + 0x40,
                            vfp_get_fpscr(env), mmu_idx, STACK_LAZYFP);
        if (cpu_isar_feature(aa32_mve, cpu)) {
            stacked_ok = stacked_ok &&
                v7m_stack_write(cpu, fpcar + 0x44,
                                env->v7m.vpr, mmu_idx, STACK_LAZYFP);
        }
    }

    take_exception = !stacked_ok &&
        armv7m_nvic_can_take_pending_exception(env->nvic);

    bql_unlock();

    if (take_exception) {
        raise_exception_ra(env, EXCP_LAZYFP, 0, 1, GETPC());
    }

    env->v7m.fpccr[is_secure] &= ~R_V7M_FPCCR_LSPACT_MASK;

    if (ts) {
        int i;

        for (i = 0; i < 32; i += 2) {
            *aa32_vfp_dreg(env, i / 2) = 0;
        }
        vfp_set_fpscr(env, 0);
        if (cpu_isar_feature(aa32_mve, cpu)) {
            env->v7m.vpr = 0;
        }
    }
}

static void write_v7m_control_spsel_for_secstate(CPUARMState *env,
                                                 bool new_spsel,
                                                 bool secstate)
{
    bool old_is_psp = v7m_using_psp(env);

    env->v7m.control[secstate] =
        deposit32(env->v7m.control[secstate],
                  R_V7M_CONTROL_SPSEL_SHIFT,
                  R_V7M_CONTROL_SPSEL_LENGTH, new_spsel);

    if (secstate == env->v7m.secure) {
        bool new_is_psp = v7m_using_psp(env);
        uint32_t tmp;

        if (old_is_psp != new_is_psp) {
            tmp = env->v7m.other_sp;
            env->v7m.other_sp = env->regs[13];
            env->regs[13] = tmp;
        }
    }
}

static void write_v7m_control_spsel(CPUARMState *env, bool new_spsel)
{
    write_v7m_control_spsel_for_secstate(env, new_spsel, env->v7m.secure);
}

void write_v7m_exception(CPUARMState *env, uint32_t new_exc)
{
    bool new_is_psp, old_is_psp = v7m_using_psp(env);
    uint32_t tmp;

    env->v7m.exception = new_exc;

    new_is_psp = v7m_using_psp(env);

    if (old_is_psp != new_is_psp) {
        tmp = env->v7m.other_sp;
        env->v7m.other_sp = env->regs[13];
        env->regs[13] = tmp;
    }
}

static void switch_v7m_security_state(CPUARMState *env, bool new_secstate)
{
    uint32_t new_ss_msp, new_ss_psp;

    if (env->v7m.secure == new_secstate) {
        return;
    }

    new_ss_msp = env->v7m.other_ss_msp;
    new_ss_psp = env->v7m.other_ss_psp;

    if (v7m_using_psp(env)) {
        env->v7m.other_ss_psp = env->regs[13];
        env->v7m.other_ss_msp = env->v7m.other_sp;
    } else {
        env->v7m.other_ss_msp = env->regs[13];
        env->v7m.other_ss_psp = env->v7m.other_sp;
    }

    env->v7m.secure = new_secstate;

    if (v7m_using_psp(env)) {
        env->regs[13] = new_ss_psp;
        env->v7m.other_sp = new_ss_msp;
    } else {
        env->regs[13] = new_ss_msp;
        env->v7m.other_sp = new_ss_psp;
    }
}

void HELPER(v7m_bxns)(CPUARMState *env, uint32_t dest)
{
    uint32_t min_magic;

    if (arm_feature(env, ARM_FEATURE_M_SECURITY)) {
        min_magic = FNC_RETURN_MIN_MAGIC;
    } else {
        min_magic = EXC_RETURN_MIN_MAGIC;
    }

    if (dest >= min_magic) {
        env->regs[15] = dest & ~1;
        env->thumb = dest & 1;
        HELPER(exception_internal)(env, EXCP_EXCEPTION_EXIT);
    }

    assert(env->v7m.secure);

    if (!(dest & 1)) {
        env->v7m.control[M_REG_S] &= ~R_V7M_CONTROL_SFPA_MASK;
    }
    switch_v7m_security_state(env, dest & 1);
    env->thumb = true;
    env->regs[15] = dest & ~1;
    arm_rebuild_hflags(env);
}

void HELPER(v7m_blxns)(CPUARMState *env, uint32_t dest)
{

    uint32_t nextinst = env->regs[15] | 1;
    uint32_t sp = env->regs[13] - 8;
    uint32_t saved_psr;

    assert(env->v7m.secure);

    if (dest & 1) {
        env->regs[14] = nextinst;
        env->thumb = true;
        env->regs[15] = dest & ~1;
        return;
    }

    if (!QEMU_IS_ALIGNED(sp, 8)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "BLXNS with misaligned SP is UNPREDICTABLE\n");
    }

    if (sp < v7m_sp_limit(env)) {
        raise_exception(env, EXCP_STKOF, 0, 1);
    }

    saved_psr = env->v7m.exception;
    if (env->v7m.control[M_REG_S] & R_V7M_CONTROL_SFPA_MASK) {
        saved_psr |= XPSR_SFPA;
    }

    ARMMMUIdx mmu_idx = arm_mmu_idx(env);
    MemOpIdx oi = make_memop_idx(MO_TEUL | MO_ALIGN,
                                 arm_to_core_mmu_idx(mmu_idx));
    cpu_stl_mmu(env, sp, nextinst, oi, GETPC());
    cpu_stl_mmu(env, sp + 4, saved_psr, oi, GETPC());

    env->regs[13] = sp;
    env->regs[14] = 0xfeffffff;
    if (arm_v7m_is_handler_mode(env)) {
        write_v7m_exception(env, 1);
    }
    env->v7m.control[M_REG_S] &= ~R_V7M_CONTROL_SFPA_MASK;
    switch_v7m_security_state(env, 0);
    env->thumb = true;
    env->regs[15] = dest;
    arm_rebuild_hflags(env);
}

static bool arm_v7m_load_vector(ARMCPU *cpu, int exc, bool targets_secure,
                                uint32_t *pvec)
{
    CPUState *cs = CPU(cpu);
    CPUARMState *env = &cpu->env;
    MemTxResult result;
    uint32_t addr = env->v7m.vecbase[targets_secure] + exc * 4;
    uint32_t vector_entry;
    MemTxAttrs attrs = {};
    ARMMMUIdx mmu_idx;
    bool exc_secure;

    qemu_log_mask(CPU_LOG_INT,
                  "...loading from element %d of %s vector table at 0x%x\n",
                  exc, targets_secure ? "secure" : "non-secure", addr);

    mmu_idx = arm_v7m_mmu_idx_for_secstate_and_priv(env, targets_secure, true);

    attrs.secure = targets_secure;
    attrs.user = false;

    if (arm_feature(env, ARM_FEATURE_M_SECURITY)) {
        V8M_SAttributes sattrs = {};

        v8m_security_lookup(env, addr, MMU_DATA_LOAD, mmu_idx,
                            targets_secure, &sattrs);
        if (sattrs.ns) {
            attrs.secure = false;
        } else if (!targets_secure) {
            exc_secure = true;
            goto load_fail;
        }
    }

    vector_entry = address_space_ldl(arm_addressspace(cs, attrs), addr,
                                     attrs, &result);
    if (result != MEMTX_OK) {
        exc_secure = !(cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK);
        goto load_fail;
    }
    *pvec = vector_entry;
    qemu_log_mask(CPU_LOG_INT, "...loaded new PC 0x%x\n", *pvec);
    return true;

load_fail:
    if (!(cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK)) {
        exc_secure = true;
    }
    env->v7m.hfsr |= R_V7M_HFSR_VECTTBL_MASK;
    if (!arm_feature(env, ARM_FEATURE_V8_1M)) {
        env->v7m.hfsr |= R_V7M_HFSR_FORCED_MASK;
    }
    armv7m_nvic_set_pending_derived(env->nvic, ARMV7M_EXCP_HARD, exc_secure);
    return false;
}

static uint32_t v7m_integrity_sig(CPUARMState *env, uint32_t lr)
{
    uint32_t sig = 0xfefa125a;

    if (!cpu_isar_feature(aa32_vfp_simd, env_archcpu(env))
        || (lr & R_V7M_EXCRET_FTYPE_MASK)) {
        sig |= 1;
    }
    return sig;
}

static bool v7m_push_callee_stack(ARMCPU *cpu, uint32_t lr, bool dotailchain,
                                  bool ignore_faults)
{
    CPUARMState *env = &cpu->env;
    uint32_t *frame_sp_p;
    uint32_t frameptr;
    ARMMMUIdx mmu_idx;
    bool stacked_ok;
    uint32_t limit;
    bool want_psp;
    uint32_t sig;
    StackingMode smode = ignore_faults ? STACK_IGNFAULTS : STACK_NORMAL;

    if (dotailchain) {
        bool mode = lr & R_V7M_EXCRET_MODE_MASK;
        bool priv = !(env->v7m.control[M_REG_S] & R_V7M_CONTROL_NPRIV_MASK) ||
            !mode;

        mmu_idx = arm_v7m_mmu_idx_for_secstate_and_priv(env, M_REG_S, priv);
        frame_sp_p = arm_v7m_get_sp_ptr(env, M_REG_S, mode,
                                        lr & R_V7M_EXCRET_SPSEL_MASK);
        want_psp = mode && (lr & R_V7M_EXCRET_SPSEL_MASK);
        if (want_psp) {
            limit = env->v7m.psplim[M_REG_S];
        } else {
            limit = env->v7m.msplim[M_REG_S];
        }
    } else {
        mmu_idx = arm_mmu_idx(env);
        frame_sp_p = &env->regs[13];
        limit = v7m_sp_limit(env);
    }

    frameptr = *frame_sp_p - 0x28;
    if (frameptr < limit) {
        qemu_log_mask(CPU_LOG_INT,
                      "...STKOF during callee-saves register stacking\n");
        env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_STKOF_MASK;
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE,
                                env->v7m.secure);
        *frame_sp_p = limit;
        return true;
    }

    sig = v7m_integrity_sig(env, lr);
    stacked_ok =
        v7m_stack_write(cpu, frameptr, sig, mmu_idx, smode) &&
        v7m_stack_write(cpu, frameptr + 0x8, env->regs[4], mmu_idx, smode) &&
        v7m_stack_write(cpu, frameptr + 0xc, env->regs[5], mmu_idx, smode) &&
        v7m_stack_write(cpu, frameptr + 0x10, env->regs[6], mmu_idx, smode) &&
        v7m_stack_write(cpu, frameptr + 0x14, env->regs[7], mmu_idx, smode) &&
        v7m_stack_write(cpu, frameptr + 0x18, env->regs[8], mmu_idx, smode) &&
        v7m_stack_write(cpu, frameptr + 0x1c, env->regs[9], mmu_idx, smode) &&
        v7m_stack_write(cpu, frameptr + 0x20, env->regs[10], mmu_idx, smode) &&
        v7m_stack_write(cpu, frameptr + 0x24, env->regs[11], mmu_idx, smode);

    *frame_sp_p = frameptr;

    return !stacked_ok;
}

static void v7m_exception_taken(ARMCPU *cpu, uint32_t lr, bool dotailchain,
                                bool ignore_stackfaults)
{
    CPUARMState *env = &cpu->env;
    uint32_t addr;
    bool targets_secure;
    int exc;
    bool push_failed = false;

    armv7m_nvic_get_pending_irq_info(env->nvic, &exc, &targets_secure);
    qemu_log_mask(CPU_LOG_INT, "...taking pending %s exception %d\n",
                  targets_secure ? "secure" : "nonsecure", exc);

    if (dotailchain) {
        if (!cpu_isar_feature(aa32_vfp_simd, cpu)) {
            lr |= R_V7M_EXCRET_FTYPE_MASK;
        }
        lr = deposit32(lr, 24, 8, 0xff);
    }

    if (arm_feature(env, ARM_FEATURE_V8)) {
        if (arm_feature(env, ARM_FEATURE_M_SECURITY) &&
            (lr & R_V7M_EXCRET_S_MASK)) {
            if (targets_secure) {
                if (dotailchain && !(lr & R_V7M_EXCRET_ES_MASK)) {
                    lr &= ~R_V7M_EXCRET_DCRS_MASK;
                }
            } else {
                if (lr & R_V7M_EXCRET_DCRS_MASK &&
                    !(dotailchain && !(lr & R_V7M_EXCRET_ES_MASK))) {
                    push_failed = v7m_push_callee_stack(cpu, lr, dotailchain,
                                                        ignore_stackfaults);
                }
                lr |= R_V7M_EXCRET_DCRS_MASK;
            }
        }

        lr &= ~R_V7M_EXCRET_ES_MASK;
        if (targets_secure) {
            lr |= R_V7M_EXCRET_ES_MASK;
        }
        lr &= ~R_V7M_EXCRET_SPSEL_MASK;
        if (env->v7m.control[targets_secure] & R_V7M_CONTROL_SPSEL_MASK) {
            lr |= R_V7M_EXCRET_SPSEL_MASK;
        }

        if (arm_feature(env, ARM_FEATURE_M_SECURITY)) {
            if (!targets_secure || arm_feature(env, ARM_FEATURE_V8_1M)) {
                int i;
                bool zero_callee_saves = !targets_secure &&
                    (lr & R_V7M_EXCRET_S_MASK);

                for (i = 0; i < 13; i++) {
                    if (i < 4 || i > 11 || zero_callee_saves) {
                        env->regs[i] = 0;
                    }
                }
                xpsr_write(env, 0, XPSR_NZCV | XPSR_Q | XPSR_GE | XPSR_IT);
            }
        }
    }

    if (push_failed && !ignore_stackfaults) {
        qemu_log_mask(CPU_LOG_INT,
                      "...derived exception on callee-saves register stacking");
        v7m_exception_taken(cpu, lr, true, true);
        return;
    }

    if (!arm_v7m_load_vector(cpu, exc, targets_secure, &addr)) {
        qemu_log_mask(CPU_LOG_INT, "...derived exception on vector table load");
        v7m_exception_taken(cpu, lr, true, true);
        return;
    }

    armv7m_nvic_acknowledge_irq(env->nvic);

    switch_v7m_security_state(env, targets_secure);
    write_v7m_control_spsel(env, 0);
    arm_clear_exclusive(env);
    env->v7m.control[M_REG_S] &=
        ~(R_V7M_CONTROL_FPCA_MASK | R_V7M_CONTROL_SFPA_MASK);
    env->condexec_bits = 0;
    env->regs[14] = lr;
    env->regs[15] = addr & 0xfffffffe;
    env->thumb = addr & 1;
    arm_rebuild_hflags(env);
}

static void v7m_update_fpccr(CPUARMState *env, uint32_t frameptr,
                             bool apply_splim)
{
    bool is_secure = env->v7m.secure;
    NVICState *nvic = env->nvic;
    uint32_t *fpccr_s = &env->v7m.fpccr[M_REG_S];
    uint32_t *fpccr_ns = &env->v7m.fpccr[M_REG_NS];
    uint32_t *fpccr = &env->v7m.fpccr[is_secure];
    bool hfrdy, bfrdy, mmrdy, ns_ufrdy, s_ufrdy, sfrdy, monrdy;

    env->v7m.fpcar[is_secure] = frameptr & ~0x7;

    if (apply_splim && arm_feature(env, ARM_FEATURE_V8)) {
        bool splimviol;
        uint32_t splim = v7m_sp_limit(env);
        bool ign = armv7m_nvic_neg_prio_requested(nvic, is_secure) &&
            (env->v7m.ccr[is_secure] & R_V7M_CCR_STKOFHFNMIGN_MASK);

        splimviol = !ign && frameptr < splim;
        *fpccr = FIELD_DP32(*fpccr, V7M_FPCCR, SPLIMVIOL, splimviol);
    }

    *fpccr = FIELD_DP32(*fpccr, V7M_FPCCR, LSPACT, 1);

    *fpccr_s = FIELD_DP32(*fpccr_s, V7M_FPCCR, S, is_secure);

    *fpccr = FIELD_DP32(*fpccr, V7M_FPCCR, USER, arm_current_el(env) == 0);

    *fpccr = FIELD_DP32(*fpccr, V7M_FPCCR, THREAD,
                        !arm_v7m_is_handler_mode(env));

    hfrdy = armv7m_nvic_get_ready_status(nvic, ARMV7M_EXCP_HARD, false);
    *fpccr_s = FIELD_DP32(*fpccr_s, V7M_FPCCR, HFRDY, hfrdy);

    bfrdy = armv7m_nvic_get_ready_status(nvic, ARMV7M_EXCP_BUS, false);
    *fpccr_s = FIELD_DP32(*fpccr_s, V7M_FPCCR, BFRDY, bfrdy);

    mmrdy = armv7m_nvic_get_ready_status(nvic, ARMV7M_EXCP_MEM, is_secure);
    *fpccr = FIELD_DP32(*fpccr, V7M_FPCCR, MMRDY, mmrdy);

    ns_ufrdy = armv7m_nvic_get_ready_status(nvic, ARMV7M_EXCP_USAGE, false);
    *fpccr_ns = FIELD_DP32(*fpccr_ns, V7M_FPCCR, UFRDY, ns_ufrdy);

    monrdy = armv7m_nvic_get_ready_status(nvic, ARMV7M_EXCP_DEBUG, false);
    *fpccr_s = FIELD_DP32(*fpccr_s, V7M_FPCCR, MONRDY, monrdy);

    if (arm_feature(env, ARM_FEATURE_M_SECURITY)) {
        s_ufrdy = armv7m_nvic_get_ready_status(nvic, ARMV7M_EXCP_USAGE, true);
        *fpccr_s = FIELD_DP32(*fpccr_s, V7M_FPCCR, UFRDY, s_ufrdy);

        sfrdy = armv7m_nvic_get_ready_status(nvic, ARMV7M_EXCP_SECURE, false);
        *fpccr_s = FIELD_DP32(*fpccr_s, V7M_FPCCR, SFRDY, sfrdy);
    }
}

void HELPER(v7m_vlstm)(CPUARMState *env, uint32_t fptr)
{
    ARMCPU *cpu = env_archcpu(env);
    bool s = env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_S_MASK;
    bool lspact = env->v7m.fpccr[s] & R_V7M_FPCCR_LSPACT_MASK;
    uintptr_t ra = GETPC();
    ARMMMUIdx mmu_idx = arm_mmu_idx(env);
    MemOpIdx oi = make_memop_idx(MO_TEUL | MO_ALIGN,
                                 arm_to_core_mmu_idx(mmu_idx));

    assert(env->v7m.secure);

    if (!(env->v7m.control[M_REG_S] & R_V7M_CONTROL_SFPA_MASK)) {
        return;
    }

    if (!v7m_cpacr_pass(env, true, arm_current_el(env) != 0)) {
        raise_exception_ra(env, EXCP_NOCP, 0, 1, GETPC());
    }

    if (lspact) {
        raise_exception_ra(env, EXCP_LSERR, 0, 1, GETPC());
    }

    if (fptr & 7) {
        raise_exception_ra(env, EXCP_UNALIGNED, 0, 1, GETPC());
    }

    if (!(env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_LSPEN_MASK)) {
        bool ts = env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_TS_MASK;
        int i;

        for (i = 0; i < (ts ? 32 : 16); i += 2) {
            uint64_t dn = *aa32_vfp_dreg(env, i / 2);
            uint32_t faddr = fptr + 4 * i;
            uint32_t slo = extract64(dn, 0, 32);
            uint32_t shi = extract64(dn, 32, 32);

            if (i >= 16) {
                faddr += 8;
            }
            cpu_stl_mmu(env, faddr, slo, oi, ra);
            cpu_stl_mmu(env, faddr + 4, shi, oi, ra);
        }
        cpu_stl_mmu(env, fptr + 0x40, vfp_get_fpscr(env), oi, ra);
        if (cpu_isar_feature(aa32_mve, cpu)) {
            cpu_stl_mmu(env, fptr + 0x44, env->v7m.vpr, oi, ra);
        }

        if (ts) {
            for (i = 0; i < 32; i += 2) {
                *aa32_vfp_dreg(env, i / 2) = 0;
            }
            vfp_set_fpscr(env, 0);
            if (cpu_isar_feature(aa32_mve, cpu)) {
                env->v7m.vpr = 0;
            }
        }
    } else {
        v7m_update_fpccr(env, fptr, false);
    }

    env->v7m.control[M_REG_S] &= ~R_V7M_CONTROL_FPCA_MASK;
}

void HELPER(v7m_vlldm)(CPUARMState *env, uint32_t fptr)
{
    ARMCPU *cpu = env_archcpu(env);
    uintptr_t ra = GETPC();
    ARMMMUIdx mmu_idx = arm_mmu_idx(env);
    MemOpIdx oi = make_memop_idx(MO_TEUL | MO_ALIGN,
                                 arm_to_core_mmu_idx(mmu_idx));

    assert(env->v7m.secure);

    if (!(env->v7m.control[M_REG_S] & R_V7M_CONTROL_SFPA_MASK)) {
        return;
    }

    if (!v7m_cpacr_pass(env, true, arm_current_el(env) != 0)) {
        raise_exception_ra(env, EXCP_NOCP, 0, 1, GETPC());
    }

    if (env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_LSPACT_MASK) {
        env->v7m.fpccr[M_REG_S] &= ~R_V7M_FPCCR_LSPACT_MASK;
    } else {
        bool ts = env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_TS_MASK;
        int i;
        uint32_t fpscr;

        if (fptr & 7) {
            raise_exception_ra(env, EXCP_UNALIGNED, 0, 1, GETPC());
        }

        for (i = 0; i < (ts ? 32 : 16); i += 2) {
            uint32_t slo, shi;
            uint64_t dn;
            uint32_t faddr = fptr + 4 * i;

            if (i >= 16) {
                faddr += 8;
            }

            slo = cpu_ldl_mmu(env, faddr, oi, ra);
            shi = cpu_ldl_mmu(env, faddr + 4, oi, ra);

            dn = (uint64_t) shi << 32 | slo;
            *aa32_vfp_dreg(env, i / 2) = dn;
        }
        fpscr = cpu_ldl_mmu(env, fptr + 0x40, oi, ra);
        vfp_set_fpscr(env, fpscr);
        if (cpu_isar_feature(aa32_mve, cpu)) {
            env->v7m.vpr = cpu_ldl_mmu(env, fptr + 0x44, oi, ra);
        }
    }

    env->v7m.control[M_REG_S] |= R_V7M_CONTROL_FPCA_MASK;
}

static bool v7m_push_stack(ARMCPU *cpu)
{
    bool stacked_ok = true, limitviol = false;
    CPUARMState *env = &cpu->env;
    uint32_t xpsr = xpsr_read(env);
    uint32_t frameptr = env->regs[13];
    ARMMMUIdx mmu_idx = arm_mmu_idx(env);
    uint32_t framesize;
    bool nsacr_cp10 = extract32(env->v7m.nsacr, 10, 1);

    if ((env->v7m.control[M_REG_S] & R_V7M_CONTROL_FPCA_MASK) &&
        (env->v7m.secure || nsacr_cp10)) {
        if (env->v7m.secure &&
            env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_TS_MASK) {
            framesize = 0xa8;
        } else {
            framesize = 0x68;
        }
    } else {
        framesize = 0x20;
    }

    if ((frameptr & 4) &&
        (env->v7m.ccr[env->v7m.secure] & R_V7M_CCR_STKALIGN_MASK)) {
        frameptr -= 4;
        xpsr |= XPSR_SPREALIGN;
    }

    xpsr &= ~XPSR_SFPA;
    if (env->v7m.secure &&
        (env->v7m.control[M_REG_S] & R_V7M_CONTROL_SFPA_MASK)) {
        xpsr |= XPSR_SFPA;
    }

    frameptr -= framesize;

    if (arm_feature(env, ARM_FEATURE_V8)) {
        uint32_t limit = v7m_sp_limit(env);

        if (frameptr < limit) {
            qemu_log_mask(CPU_LOG_INT,
                          "...STKOF during stacking\n");
            env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_STKOF_MASK;
            armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE,
                                    env->v7m.secure);
            env->regs[13] = limit;
            limitviol = true;
            stacked_ok = false;
        }
    }

    stacked_ok = stacked_ok &&
        v7m_stack_write(cpu, frameptr, env->regs[0], mmu_idx, STACK_NORMAL) &&
        v7m_stack_write(cpu, frameptr + 4, env->regs[1],
                        mmu_idx, STACK_NORMAL) &&
        v7m_stack_write(cpu, frameptr + 8, env->regs[2],
                        mmu_idx, STACK_NORMAL) &&
        v7m_stack_write(cpu, frameptr + 12, env->regs[3],
                        mmu_idx, STACK_NORMAL) &&
        v7m_stack_write(cpu, frameptr + 16, env->regs[12],
                        mmu_idx, STACK_NORMAL) &&
        v7m_stack_write(cpu, frameptr + 20, env->regs[14],
                        mmu_idx, STACK_NORMAL) &&
        v7m_stack_write(cpu, frameptr + 24, env->regs[15],
                        mmu_idx, STACK_NORMAL) &&
        v7m_stack_write(cpu, frameptr + 28, xpsr, mmu_idx, STACK_NORMAL);

    if (env->v7m.control[M_REG_S] & R_V7M_CONTROL_FPCA_MASK) {
        bool fpccr_s = env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_S_MASK;
        bool lspact = env->v7m.fpccr[fpccr_s] & R_V7M_FPCCR_LSPACT_MASK;

        if (lspact && arm_feature(env, ARM_FEATURE_M_SECURITY)) {
            qemu_log_mask(CPU_LOG_INT,
                          "...SecureFault because LSPACT and FPCA both set\n");
            env->v7m.sfsr |= R_V7M_SFSR_LSERR_MASK;
            armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_SECURE, false);
        } else if (!env->v7m.secure && !nsacr_cp10) {
            qemu_log_mask(CPU_LOG_INT,
                          "...Secure UsageFault with CFSR.NOCP because "
                          "NSACR.CP10 prevents stacking FP regs\n");
            armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE, M_REG_S);
            env->v7m.cfsr[M_REG_S] |= R_V7M_CFSR_NOCP_MASK;
        } else {
            if (!(env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_LSPEN_MASK)) {
                int i;
                bool cpacr_pass = v7m_cpacr_pass(env, env->v7m.secure,
                                                 arm_current_el(env) != 0);

                if (stacked_ok && !cpacr_pass) {
                    qemu_log_mask(CPU_LOG_INT,
                                  "...UsageFault with CFSR.NOCP because "
                                  "CPACR.CP10 prevents stacking FP regs\n");
                    armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE,
                                            env->v7m.secure);
                    env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_NOCP_MASK;
                    stacked_ok = false;
                }

                for (i = 0; i < ((framesize == 0xa8) ? 32 : 16); i += 2) {
                    uint64_t dn = *aa32_vfp_dreg(env, i / 2);
                    uint32_t faddr = frameptr + 0x20 + 4 * i;
                    uint32_t slo = extract64(dn, 0, 32);
                    uint32_t shi = extract64(dn, 32, 32);

                    if (i >= 16) {
                        faddr += 8;
                    }
                    stacked_ok = stacked_ok &&
                        v7m_stack_write(cpu, faddr, slo,
                                        mmu_idx, STACK_NORMAL) &&
                        v7m_stack_write(cpu, faddr + 4, shi,
                                        mmu_idx, STACK_NORMAL);
                }
                stacked_ok = stacked_ok &&
                    v7m_stack_write(cpu, frameptr + 0x60,
                                    vfp_get_fpscr(env), mmu_idx, STACK_NORMAL);
                if (cpu_isar_feature(aa32_mve, cpu)) {
                    stacked_ok = stacked_ok &&
                        v7m_stack_write(cpu, frameptr + 0x64,
                                        env->v7m.vpr, mmu_idx, STACK_NORMAL);
                }
                if (cpacr_pass) {
                    for (i = 0; i < ((framesize == 0xa8) ? 32 : 16); i += 2) {
                        *aa32_vfp_dreg(env, i / 2) = 0;
                    }
                    vfp_set_fpscr(env, 0);
                    if (cpu_isar_feature(aa32_mve, cpu)) {
                        env->v7m.vpr = 0;
                    }
                }
            } else {
                v7m_update_fpccr(env, frameptr + 0x20, true);
            }
        }
    }

    if (!limitviol) {
        env->regs[13] = frameptr;
    }

    return !stacked_ok;
}

static void do_v7m_exception_exit(ARMCPU *cpu)
{
    CPUARMState *env = &cpu->env;
    uint32_t excret;
    uint32_t xpsr, xpsr_mask;
    bool ufault = false;
    bool sfault = false;
    bool return_to_sp_process;
    bool return_to_handler;
    bool rettobase = false;
    bool exc_secure = false;
    bool return_to_secure;
    bool ftype;
    bool restore_s16_s31 = false;

    if (!arm_v7m_is_handler_mode(env)) {
        return;
    }

    excret = env->regs[15];
    if (env->thumb) {
        excret |= 1;
    }

    qemu_log_mask(CPU_LOG_INT, "Exception return: magic PC %" PRIx32
                  " previous exception %d\n",
                  excret, env->v7m.exception);

    if ((excret & R_V7M_EXCRET_RES1_MASK) != R_V7M_EXCRET_RES1_MASK) {
        qemu_log_mask(LOG_GUEST_ERROR, "M profile: zero high bits in exception "
                      "exit PC value 0x%" PRIx32 " are UNPREDICTABLE\n",
                      excret);
    }

    ftype = excret & R_V7M_EXCRET_FTYPE_MASK;

    if (!ftype && !cpu_isar_feature(aa32_vfp_simd, cpu)) {
        qemu_log_mask(LOG_GUEST_ERROR, "M profile: zero FTYPE in exception "
                      "exit PC value 0x%" PRIx32 " is UNPREDICTABLE "
                      "if FPU not present\n",
                      excret);
        ftype = true;
    }

    if (arm_feature(env, ARM_FEATURE_M_SECURITY)) {
        if (!env->v7m.secure &&
            ((excret & R_V7M_EXCRET_ES_MASK) ||
             !(excret & R_V7M_EXCRET_DCRS_MASK))) {
            sfault = 1;
            excret &= ~R_V7M_EXCRET_ES_MASK;
        }
        exc_secure = excret & R_V7M_EXCRET_ES_MASK;
    }

    if (env->v7m.exception != ARMV7M_EXCP_NMI) {
        if (arm_feature(env, ARM_FEATURE_M_SECURITY)) {
            if (armv7m_nvic_raw_execution_priority(env->nvic) >= 0) {
                env->v7m.faultmask[exc_secure] = 0;
            }
        } else {
            env->v7m.faultmask[M_REG_NS] = 0;
        }
    }

    switch (armv7m_nvic_complete_irq(env->nvic, env->v7m.exception,
                                     exc_secure)) {
    case -1:
        ufault = true;
        break;
    case 0:
        break;
    case 1:
        rettobase = true;
        break;
    default:
        g_assert_not_reached();
    }

    return_to_handler = !(excret & R_V7M_EXCRET_MODE_MASK);
    return_to_sp_process = excret & R_V7M_EXCRET_SPSEL_MASK;
    return_to_secure = arm_feature(env, ARM_FEATURE_M_SECURITY) &&
        (excret & R_V7M_EXCRET_S_MASK);

    if (arm_feature(env, ARM_FEATURE_V8)) {
        if (!arm_feature(env, ARM_FEATURE_M_SECURITY)) {
            if ((excret & R_V7M_EXCRET_S_MASK) ||
                (excret & R_V7M_EXCRET_ES_MASK) ||
                !(excret & R_V7M_EXCRET_DCRS_MASK)) {
                ufault = true;
            }
        }
        if (excret & R_V7M_EXCRET_RES0_MASK) {
            ufault = true;
        }
    } else {
        switch (excret & 0xf) {
        case 1:
            break;
        case 13:
        case 9:
            if (!rettobase &&
                !(env->v7m.ccr[env->v7m.secure] &
                  R_V7M_CCR_NONBASETHRDENA_MASK)) {
                ufault = true;
            }
            break;
        default:
            ufault = true;
        }
    }

    write_v7m_control_spsel_for_secstate(env, return_to_sp_process, exc_secure);

    if ((env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_CLRONRET_MASK) &&
        (env->v7m.control[M_REG_S] & R_V7M_CONTROL_FPCA_MASK)) {
        if (env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_LSPACT_MASK) {
            env->v7m.sfsr |= R_V7M_SFSR_LSERR_MASK;
            armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_SECURE, false);
            qemu_log_mask(CPU_LOG_INT, "...taking SecureFault on existing "
                          "stackframe: error during lazy state deactivation\n");
            v7m_exception_taken(cpu, excret, true, false);
            return;
        } else {
            if (arm_feature(env, ARM_FEATURE_V8_1M)) {
                bool nsacr_pass = exc_secure ||
                    extract32(env->v7m.nsacr, 10, 1);
                bool cpacr_pass = v7m_cpacr_pass(env, exc_secure, true);
                if (!nsacr_pass) {
                    armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE, true);
                    env->v7m.cfsr[M_REG_S] |= R_V7M_CFSR_NOCP_MASK;
                    qemu_log_mask(CPU_LOG_INT, "...taking UsageFault on existing "
                        "stackframe: NSACR prevents clearing FPU registers\n");
                    v7m_exception_taken(cpu, excret, true, false);
                    return;
                } else if (!cpacr_pass) {
                    armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE,
                                            exc_secure);
                    env->v7m.cfsr[exc_secure] |= R_V7M_CFSR_NOCP_MASK;
                    qemu_log_mask(CPU_LOG_INT, "...taking UsageFault on existing "
                        "stackframe: CPACR prevents clearing FPU registers\n");
                    v7m_exception_taken(cpu, excret, true, false);
                    return;
                }
            }
            int i;

            for (i = 0; i < 16; i += 2) {
                *aa32_vfp_dreg(env, i / 2) = 0;
            }
            vfp_set_fpscr(env, 0);
            if (cpu_isar_feature(aa32_mve, cpu)) {
                env->v7m.vpr = 0;
            }
        }
    }

    if (sfault) {
        env->v7m.sfsr |= R_V7M_SFSR_INVER_MASK;
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_SECURE, false);
        qemu_log_mask(CPU_LOG_INT, "...taking SecureFault on existing "
                      "stackframe: failed EXC_RETURN.ES validity check\n");
        v7m_exception_taken(cpu, excret, true, false);
        return;
    }

    if (ufault) {
        env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_INVPC_MASK;
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE, env->v7m.secure);
        qemu_log_mask(CPU_LOG_INT, "...taking UsageFault on existing "
                      "stackframe: failed exception return integrity check\n");
        v7m_exception_taken(cpu, excret, true, false);
        return;
    }

    if (armv7m_nvic_can_take_pending_exception(env->nvic)) {
        qemu_log_mask(CPU_LOG_INT, "...tailchaining to pending exception\n");
        v7m_exception_taken(cpu, excret, true, false);
        return;
    }

    switch_v7m_security_state(env, return_to_secure);

    {
        bool spsel = env->v7m.control[return_to_secure] & R_V7M_CONTROL_SPSEL_MASK;
        uint32_t *frame_sp_p = arm_v7m_get_sp_ptr(env, return_to_secure,
                                                  !return_to_handler, spsel);
        uint32_t frameptr = *frame_sp_p;
        bool pop_ok = true;
        ARMMMUIdx mmu_idx;
        bool return_to_priv = return_to_handler ||
            !(env->v7m.control[return_to_secure] & R_V7M_CONTROL_NPRIV_MASK);

        mmu_idx = arm_v7m_mmu_idx_for_secstate_and_priv(env, return_to_secure,
                                                        return_to_priv);

        if (!QEMU_IS_ALIGNED(frameptr, 8) &&
            arm_feature(env, ARM_FEATURE_V8)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "M profile exception return with non-8-aligned SP "
                          "for destination state is UNPREDICTABLE\n");
        }

        if (return_to_secure &&
            ((excret & R_V7M_EXCRET_ES_MASK) == 0 ||
             (excret & R_V7M_EXCRET_DCRS_MASK) == 0)) {
            uint32_t actual_sig;

            pop_ok = v7m_stack_read(cpu, &actual_sig, frameptr, mmu_idx);

            if (pop_ok && v7m_integrity_sig(env, excret) != actual_sig) {
                env->v7m.sfsr |= R_V7M_SFSR_INVIS_MASK;
                armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_SECURE, false);
                qemu_log_mask(CPU_LOG_INT, "...taking SecureFault on existing "
                              "stackframe: failed exception return integrity "
                              "signature check\n");
                v7m_exception_taken(cpu, excret, true, false);
                return;
            }

            pop_ok = pop_ok &&
                v7m_stack_read(cpu, &env->regs[4], frameptr + 0x8, mmu_idx) &&
                v7m_stack_read(cpu, &env->regs[5], frameptr + 0xc, mmu_idx) &&
                v7m_stack_read(cpu, &env->regs[6], frameptr + 0x10, mmu_idx) &&
                v7m_stack_read(cpu, &env->regs[7], frameptr + 0x14, mmu_idx) &&
                v7m_stack_read(cpu, &env->regs[8], frameptr + 0x18, mmu_idx) &&
                v7m_stack_read(cpu, &env->regs[9], frameptr + 0x1c, mmu_idx) &&
                v7m_stack_read(cpu, &env->regs[10], frameptr + 0x20, mmu_idx) &&
                v7m_stack_read(cpu, &env->regs[11], frameptr + 0x24, mmu_idx);

            frameptr += 0x28;
        }

        pop_ok = pop_ok &&
            v7m_stack_read(cpu, &env->regs[0], frameptr, mmu_idx) &&
            v7m_stack_read(cpu, &env->regs[1], frameptr + 0x4, mmu_idx) &&
            v7m_stack_read(cpu, &env->regs[2], frameptr + 0x8, mmu_idx) &&
            v7m_stack_read(cpu, &env->regs[3], frameptr + 0xc, mmu_idx) &&
            v7m_stack_read(cpu, &env->regs[12], frameptr + 0x10, mmu_idx) &&
            v7m_stack_read(cpu, &env->regs[14], frameptr + 0x14, mmu_idx) &&
            v7m_stack_read(cpu, &env->regs[15], frameptr + 0x18, mmu_idx) &&
            v7m_stack_read(cpu, &xpsr, frameptr + 0x1c, mmu_idx);

        if (!pop_ok) {
            qemu_log_mask(CPU_LOG_INT, "...derived exception on unstacking\n");
            v7m_exception_taken(cpu, excret, true, false);
            return;
        }

        if (env->regs[15] & 1) {
            env->regs[15] &= ~1U;
            if (!arm_feature(env, ARM_FEATURE_V8)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "M profile return from interrupt with misaligned "
                              "PC is UNPREDICTABLE on v7M\n");
            }
        }

        if (arm_feature(env, ARM_FEATURE_V8)) {
            bool will_be_handler = (xpsr & XPSR_EXCP) != 0;
            if (return_to_handler != will_be_handler) {
                armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE,
                                        env->v7m.secure);
                env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_INVPC_MASK;
                qemu_log_mask(CPU_LOG_INT, "...taking UsageFault on existing "
                              "stackframe: failed exception return integrity "
                              "check\n");
                v7m_exception_taken(cpu, excret, true, false);
                return;
            }
        }

        if (!ftype) {
            if (!return_to_secure &&
                (env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_LSPACT_MASK)) {
                armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_SECURE, false);
                env->v7m.sfsr |= R_V7M_SFSR_LSERR_MASK;
                qemu_log_mask(CPU_LOG_INT,
                              "...taking SecureFault on existing stackframe: "
                              "Secure LSPACT set but exception return is "
                              "not to secure state\n");
                v7m_exception_taken(cpu, excret, true, false);
                return;
            }

            restore_s16_s31 = return_to_secure &&
                (env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_TS_MASK);

            if (env->v7m.fpccr[return_to_secure] & R_V7M_FPCCR_LSPACT_MASK) {
                env->v7m.fpccr[return_to_secure] &= ~R_V7M_FPCCR_LSPACT_MASK;
            } else {
                int i;
                uint32_t fpscr;
                bool cpacr_pass, nsacr_pass;

                cpacr_pass = v7m_cpacr_pass(env, return_to_secure,
                                            return_to_priv);
                nsacr_pass = return_to_secure ||
                    extract32(env->v7m.nsacr, 10, 1);

                if (!cpacr_pass) {
                    armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE,
                                            return_to_secure);
                    env->v7m.cfsr[return_to_secure] |= R_V7M_CFSR_NOCP_MASK;
                    qemu_log_mask(CPU_LOG_INT,
                                  "...taking UsageFault on existing "
                                  "stackframe: CPACR.CP10 prevents unstacking "
                                  "FP regs\n");
                    v7m_exception_taken(cpu, excret, true, false);
                    return;
                } else if (!nsacr_pass) {
                    armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE, true);
                    env->v7m.cfsr[M_REG_S] |= R_V7M_CFSR_INVPC_MASK;
                    qemu_log_mask(CPU_LOG_INT,
                                  "...taking Secure UsageFault on existing "
                                  "stackframe: NSACR.CP10 prevents unstacking "
                                  "FP regs\n");
                    v7m_exception_taken(cpu, excret, true, false);
                    return;
                }

                for (i = 0; i < (restore_s16_s31 ? 32 : 16); i += 2) {
                    uint32_t slo, shi;
                    uint64_t dn;
                    uint32_t faddr = frameptr + 0x20 + 4 * i;

                    if (i >= 16) {
                        faddr += 8;
                    }

                    pop_ok = pop_ok &&
                        v7m_stack_read(cpu, &slo, faddr, mmu_idx) &&
                        v7m_stack_read(cpu, &shi, faddr + 4, mmu_idx);

                    if (!pop_ok) {
                        break;
                    }

                    dn = (uint64_t)shi << 32 | slo;
                    *aa32_vfp_dreg(env, i / 2) = dn;
                }
                pop_ok = pop_ok &&
                    v7m_stack_read(cpu, &fpscr, frameptr + 0x60, mmu_idx);
                if (pop_ok) {
                    vfp_set_fpscr(env, fpscr);
                }
                if (cpu_isar_feature(aa32_mve, cpu)) {
                    pop_ok = pop_ok &&
                        v7m_stack_read(cpu, &env->v7m.vpr,
                                       frameptr + 0x64, mmu_idx);
                }
                if (!pop_ok) {
                    for (i = 0; i < (restore_s16_s31 ? 32 : 16); i += 2) {
                        *aa32_vfp_dreg(env, i / 2) = 0;
                    }
                    vfp_set_fpscr(env, 0);
                    if (cpu_isar_feature(aa32_mve, cpu)) {
                        env->v7m.vpr = 0;
                    }
                }
            }
        }
        env->v7m.control[M_REG_S] = FIELD_DP32(env->v7m.control[M_REG_S],
                                               V7M_CONTROL, FPCA, !ftype);

        frameptr += 0x20;
        if (!ftype) {
            frameptr += 0x48;
            if (restore_s16_s31) {
                frameptr += 0x40;
            }
        }
        if (xpsr & XPSR_SPREALIGN) {
            frameptr |= 4;
        }
        *frame_sp_p = frameptr;
    }

    xpsr_mask = ~(XPSR_SPREALIGN | XPSR_SFPA);
    if (!arm_feature(env, ARM_FEATURE_THUMB_DSP)) {
        xpsr_mask &= ~XPSR_GE;
    }
    xpsr_write(env, xpsr, xpsr_mask);

    if (env->v7m.secure) {
        bool sfpa = xpsr & XPSR_SFPA;

        env->v7m.control[M_REG_S] = FIELD_DP32(env->v7m.control[M_REG_S],
                                               V7M_CONTROL, SFPA, sfpa);
    }

    if (return_to_handler != arm_v7m_is_handler_mode(env)) {
        bool ignore_stackfaults;

        assert(!arm_feature(env, ARM_FEATURE_V8));
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE, false);
        env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_INVPC_MASK;
        ignore_stackfaults = v7m_push_stack(cpu);
        qemu_log_mask(CPU_LOG_INT, "...taking UsageFault on new stackframe: "
                      "failed exception return integrity check\n");
        v7m_exception_taken(cpu, excret, false, ignore_stackfaults);
        return;
    }

    arm_clear_exclusive(env);
    arm_rebuild_hflags(env);
    qemu_log_mask(CPU_LOG_INT, "...successful exception return\n");
}

static bool do_v7m_function_return(ARMCPU *cpu)
{
    CPUARMState *env = &cpu->env;
    uint32_t newpc, newpsr, newpsr_exc;

    qemu_log_mask(CPU_LOG_INT, "...really v7M secure function return\n");

    {
        bool threadmode, spsel;
        MemOpIdx oi;
        ARMMMUIdx mmu_idx;
        uint32_t *frame_sp_p;
        uint32_t frameptr;

        threadmode = !arm_v7m_is_handler_mode(env);
        spsel = env->v7m.control[M_REG_S] & R_V7M_CONTROL_SPSEL_MASK;

        frame_sp_p = arm_v7m_get_sp_ptr(env, true, threadmode, spsel);
        frameptr = *frame_sp_p;

        mmu_idx = arm_v7m_mmu_idx_for_secstate(env, true);
        oi = make_memop_idx(MO_LEUL | MO_ALIGN, arm_to_core_mmu_idx(mmu_idx));
        newpc = cpu_ldl_mmu(env, frameptr, oi, 0);
        newpsr = cpu_ldl_mmu(env, frameptr + 4, oi, 0);

        newpsr_exc = newpsr & XPSR_EXCP;
        if (!((env->v7m.exception == 0 && newpsr_exc == 0) ||
              (env->v7m.exception == 1 && newpsr_exc != 0))) {
            env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_INVPC_MASK;
            armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE,
                                    env->v7m.secure);
            qemu_log_mask(CPU_LOG_INT,
                          "...taking INVPC UsageFault: "
                          "IPSR consistency check failed\n");
            return false;
        }

        *frame_sp_p = frameptr + 8;
    }

    switch_v7m_security_state(env, true);
    env->v7m.exception = newpsr_exc;
    env->v7m.control[M_REG_S] &= ~R_V7M_CONTROL_SFPA_MASK;
    if (newpsr & XPSR_SFPA) {
        env->v7m.control[M_REG_S] |= R_V7M_CONTROL_SFPA_MASK;
    }
    xpsr_write(env, 0, XPSR_IT);
    env->thumb = newpc & 1;
    env->regs[15] = newpc & ~1;
    arm_rebuild_hflags(env);

    qemu_log_mask(CPU_LOG_INT, "...function return successful\n");
    return true;
}

static bool v7m_read_half_insn(ARMCPU *cpu, ARMMMUIdx mmu_idx, bool secure,
                               uint32_t addr, uint16_t *insn)
{
    CPUState *cs = CPU(cpu);
    CPUARMState *env = &cpu->env;
    V8M_SAttributes sattrs = {};
    GetPhysAddrResult res = {};
    ARMMMUFaultInfo fi = {};
    MemTxResult txres;

    v8m_security_lookup(env, addr, MMU_INST_FETCH, mmu_idx, secure, &sattrs);
    if (!sattrs.nsc || sattrs.ns) {
        env->v7m.sfsr |= R_V7M_SFSR_INVEP_MASK;
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_SECURE, false);
        qemu_log_mask(CPU_LOG_INT,
                      "...really SecureFault with SFSR.INVEP\n");
        return false;
    }
    if (get_phys_addr(env, addr, MMU_INST_FETCH, 0, mmu_idx, &res, &fi)) {
        env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_IACCVIOL_MASK;
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_MEM, env->v7m.secure);
        qemu_log_mask(CPU_LOG_INT, "...really MemManage with CFSR.IACCVIOL\n");
        return false;
    }
    *insn = address_space_lduw_le(arm_addressspace(cs, res.f.attrs),
                                  res.f.phys_addr, res.f.attrs, &txres);
    if (txres != MEMTX_OK) {
        env->v7m.cfsr[M_REG_NS] |= R_V7M_CFSR_IBUSERR_MASK;
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_BUS, false);
        qemu_log_mask(CPU_LOG_INT, "...really BusFault with CFSR.IBUSERR\n");
        return false;
    }
    return true;
}

static bool v7m_read_sg_stack_word(ARMCPU *cpu, ARMMMUIdx mmu_idx,
                                   uint32_t addr, uint32_t *spdata)
{
    CPUState *cs = CPU(cpu);
    CPUARMState *env = &cpu->env;
    MemTxResult txres;
    GetPhysAddrResult res = {};
    ARMMMUFaultInfo fi = {};
    uint32_t value;

    if (get_phys_addr(env, addr, MMU_DATA_LOAD, 0, mmu_idx, &res, &fi)) {
        if (fi.type == ARMFault_QEMU_SFault) {
            qemu_log_mask(CPU_LOG_INT,
                          "...SecureFault during stack word read\n");
            env->v7m.sfsr |= R_V7M_SFSR_AUVIOL_MASK | R_V7M_SFSR_SFARVALID_MASK;
            env->v7m.sfar = addr;
            armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_SECURE, false);
        } else {
            qemu_log_mask(CPU_LOG_INT,
                          "...MemManageFault during stack word read\n");
            env->v7m.cfsr[M_REG_S] |= R_V7M_CFSR_DACCVIOL_MASK |
                R_V7M_CFSR_MMARVALID_MASK;
            env->v7m.mmfar[M_REG_S] = addr;
            armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_MEM, false);
        }
        return false;
    }
    value = address_space_ldl(arm_addressspace(cs, res.f.attrs),
                              res.f.phys_addr, res.f.attrs, &txres);
    if (txres != MEMTX_OK) {
        qemu_log_mask(CPU_LOG_INT,
                      "...BusFault during stack word read\n");
        env->v7m.cfsr[M_REG_NS] |=
            (R_V7M_CFSR_PRECISERR_MASK | R_V7M_CFSR_BFARVALID_MASK);
        env->v7m.bfar = addr;
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_BUS, false);
        return false;
    }

    *spdata = value;
    return true;
}

static bool v7m_handle_execute_nsc(ARMCPU *cpu)
{
    CPUARMState *env = &cpu->env;
    ARMMMUIdx mmu_idx;
    uint16_t insn;

    assert(!env->v7m.secure);
    assert(arm_feature(env, ARM_FEATURE_M_SECURITY));

    mmu_idx = arm_v7m_mmu_idx_for_secstate(env, true);

    if (!v7m_read_half_insn(cpu, mmu_idx, true, env->regs[15], &insn)) {
        return false;
    }

    if (!env->thumb) {
        goto gen_invep;
    }

    if (insn != 0xe97f) {
        goto gen_invep;
    }

    if (!v7m_read_half_insn(cpu, mmu_idx, true, env->regs[15] + 2, &insn)) {
        return false;
    }

    if (insn != 0xe97f) {
        goto gen_invep;
    }

    qemu_log_mask(CPU_LOG_INT, "...really an SG instruction at 0x%08" PRIx32
                  ", executing it\n", env->regs[15]);

    if (cpu_isar_feature(aa32_m_sec_state, cpu) &&
        !arm_v7m_is_handler_mode(env)) {
        uint32_t spdata, sp;

        sp = v7m_using_psp(env) ? env->v7m.other_ss_psp : env->v7m.other_ss_msp;
        if (!v7m_read_sg_stack_word(cpu, mmu_idx, sp, &spdata)) {
            return false;
        }

        if (env->v7m.ccr[M_REG_S] & R_V7M_CCR_TRD_MASK) {
            if (((spdata & ~1) == 0xfefa125a) ||
                !(env->v7m.control[M_REG_S] & 1)) {
                goto gen_invep;
            }
        }
    }

    env->regs[14] &= ~1;
    env->v7m.control[M_REG_S] &= ~R_V7M_CONTROL_SFPA_MASK;
    switch_v7m_security_state(env, true);
    xpsr_write(env, 0, XPSR_IT);
    env->regs[15] += 4;
    arm_rebuild_hflags(env);
    return true;

gen_invep:
    env->v7m.sfsr |= R_V7M_SFSR_INVEP_MASK;
    armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_SECURE, false);
    qemu_log_mask(CPU_LOG_INT,
                  "...really SecureFault with SFSR.INVEP\n");
    return false;
}

void arm_v7m_cpu_do_interrupt(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    uint32_t lr;
    bool ignore_stackfaults;
    uint64_t last_pc = env->regs[15];

    arm_log_exception(cs);

    switch (cs->exception_index) {
    case EXCP_UDEF:
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE, env->v7m.secure);
        env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_UNDEFINSTR_MASK;
        break;
    case EXCP_NOCP:
    {
        int target_secstate;

        if (env->exception.target_el == 3) {
            target_secstate = M_REG_S;
        } else {
            target_secstate = env->v7m.secure;
        }
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE, target_secstate);
        env->v7m.cfsr[target_secstate] |= R_V7M_CFSR_NOCP_MASK;
        break;
    }
    case EXCP_INVSTATE:
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE, env->v7m.secure);
        env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_INVSTATE_MASK;
        break;
    case EXCP_STKOF:
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE, env->v7m.secure);
        env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_STKOF_MASK;
        break;
    case EXCP_LSERR:
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_SECURE, false);
        env->v7m.sfsr |= R_V7M_SFSR_LSERR_MASK;
        break;
    case EXCP_UNALIGNED:
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE, env->v7m.secure);
        env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_UNALIGNED_MASK;
        break;
    case EXCP_DIVBYZERO:
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE, env->v7m.secure);
        env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_DIVBYZERO_MASK;
        break;
    case EXCP_SWI:
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_SVC, env->v7m.secure);
        break;
    case EXCP_PREFETCH_ABORT:
    case EXCP_DATA_ABORT:
        qemu_log_mask(CPU_LOG_INT, "...at fault address 0x%x\n",
                      (uint32_t)env->exception.vaddress);
        switch (env->exception.fsr & 0xf) {
        case M_FAKE_FSR_NSC_EXEC:
            if (v7m_handle_execute_nsc(cpu)) {
                return;
            }
            break;
        case M_FAKE_FSR_SFAULT:
            switch (cs->exception_index) {
            case EXCP_PREFETCH_ABORT:
                if (env->v7m.secure) {
                    env->v7m.sfsr |= R_V7M_SFSR_INVTRAN_MASK;
                    qemu_log_mask(CPU_LOG_INT,
                                  "...really SecureFault with SFSR.INVTRAN\n");
                } else {
                    env->v7m.sfsr |= R_V7M_SFSR_INVEP_MASK;
                    qemu_log_mask(CPU_LOG_INT,
                                  "...really SecureFault with SFSR.INVEP\n");
                }
                break;
            case EXCP_DATA_ABORT:
                env->v7m.sfsr |= R_V7M_SFSR_AUVIOL_MASK;
                qemu_log_mask(CPU_LOG_INT,
                              "...really SecureFault with SFSR.AUVIOL\n");
                break;
            }
            armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_SECURE, false);
            break;
        case 0x8:
            switch (cs->exception_index) {
            case EXCP_PREFETCH_ABORT:
                env->v7m.cfsr[M_REG_NS] |= R_V7M_CFSR_IBUSERR_MASK;
                qemu_log_mask(CPU_LOG_INT, "...with CFSR.IBUSERR\n");
                break;
            case EXCP_DATA_ABORT:
                env->v7m.cfsr[M_REG_NS] |=
                    (R_V7M_CFSR_PRECISERR_MASK | R_V7M_CFSR_BFARVALID_MASK);
                env->v7m.bfar = env->exception.vaddress;
                qemu_log_mask(CPU_LOG_INT,
                              "...with CFSR.PRECISERR and BFAR 0x%x\n",
                              env->v7m.bfar);
                break;
            }
            armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_BUS, false);
            break;
        case 0x1:
            qemu_log_mask(CPU_LOG_INT,
                          "...really UsageFault with UFSR.UNALIGNED\n");
            env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_UNALIGNED_MASK;
            armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE,
                                    env->v7m.secure);
            break;
        default:
            switch (cs->exception_index) {
            case EXCP_PREFETCH_ABORT:
                env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_IACCVIOL_MASK;
                qemu_log_mask(CPU_LOG_INT, "...with CFSR.IACCVIOL\n");
                break;
            case EXCP_DATA_ABORT:
                env->v7m.cfsr[env->v7m.secure] |=
                    (R_V7M_CFSR_DACCVIOL_MASK | R_V7M_CFSR_MMARVALID_MASK);
                env->v7m.mmfar[env->v7m.secure] = env->exception.vaddress;
                qemu_log_mask(CPU_LOG_INT,
                              "...with CFSR.DACCVIOL and MMFAR 0x%x\n",
                              env->v7m.mmfar[env->v7m.secure]);
                break;
            }
            armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_MEM,
                                    env->v7m.secure);
            break;
        }
        break;
    case EXCP_SEMIHOST:
        qemu_log_mask(CPU_LOG_INT,
                      "...handling as semihosting call 0x%x\n",
                      env->regs[0]);
#ifdef CONFIG_TCG
        do_common_semihosting(cs);
#else
        g_assert_not_reached();
#endif
        env->regs[15] += env->thumb ? 2 : 4;
        qemu_plugin_vcpu_hostcall_cb(cs, last_pc);
        return;
    case EXCP_BKPT:
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_DEBUG, false);
        break;
    case EXCP_IRQ:
        break;
    case EXCP_EXCEPTION_EXIT:
        if (env->regs[15] < EXC_RETURN_MIN_MAGIC) {
            assert(env->regs[15] >= FNC_RETURN_MIN_MAGIC);
            assert(arm_feature(env, ARM_FEATURE_M_SECURITY));
            if (do_v7m_function_return(cpu)) {
                return;
            }
        } else {
            do_v7m_exception_exit(cpu);
            return;
        }
        break;
    case EXCP_LAZYFP:
        break;
    default:
        cpu_abort(cs, "Unhandled exception 0x%x\n", cs->exception_index);
        return;
    }

    if (arm_feature(env, ARM_FEATURE_V8)) {
        lr = R_V7M_EXCRET_RES1_MASK |
            R_V7M_EXCRET_DCRS_MASK;
        if (env->v7m.secure) {
            lr |= R_V7M_EXCRET_S_MASK;
        }
    } else {
        lr = R_V7M_EXCRET_RES1_MASK |
            R_V7M_EXCRET_S_MASK |
            R_V7M_EXCRET_DCRS_MASK |
            R_V7M_EXCRET_ES_MASK;
        if (env->v7m.control[M_REG_NS] & R_V7M_CONTROL_SPSEL_MASK) {
            lr |= R_V7M_EXCRET_SPSEL_MASK;
        }
    }
    if (!(env->v7m.control[M_REG_S] & R_V7M_CONTROL_FPCA_MASK)) {
        lr |= R_V7M_EXCRET_FTYPE_MASK;
    }
    if (!arm_v7m_is_handler_mode(env)) {
        lr |= R_V7M_EXCRET_MODE_MASK;
    }

    ignore_stackfaults = v7m_push_stack(cpu);
    v7m_exception_taken(cpu, lr, false, ignore_stackfaults);

    arm_do_plugin_vcpu_discon_cb(cs, last_pc);
}

uint32_t HELPER(v7m_mrs)(CPUARMState *env, uint32_t reg)
{
    unsigned el = arm_current_el(env);

    switch (reg) {
    case 0 ... 7:
        return v7m_mrs_xpsr(env, reg, el);
    case 20:
        return arm_v7m_mrs_control(env, env->v7m.secure);
    case 0x94:
        if (!env->v7m.secure) {
            return 0;
        }
        return env->v7m.control[M_REG_NS] |
            (env->v7m.control[M_REG_S] & R_V7M_CONTROL_FPCA_MASK);
    }

    if (el == 0) {
        return 0;
    }

    if (arm_feature(env, ARM_FEATURE_M_SECURITY)) {
        switch (reg) {
        case 0x88:
            if (!env->v7m.secure) {
                return 0;
            }
            return env->v7m.other_ss_msp;
        case 0x89:
            if (!env->v7m.secure) {
                return 0;
            }
            return env->v7m.other_ss_psp;
        case 0x8a:
            if (!env->v7m.secure) {
                return 0;
            }
            return env->v7m.msplim[M_REG_NS];
        case 0x8b:
            if (!env->v7m.secure) {
                return 0;
            }
            return env->v7m.psplim[M_REG_NS];
        case 0x90:
            if (!env->v7m.secure) {
                return 0;
            }
            return env->v7m.primask[M_REG_NS];
        case 0x91:
            if (!arm_feature(env, ARM_FEATURE_M_MAIN)) {
                goto bad_reg;
            }
            if (!env->v7m.secure) {
                return 0;
            }
            return env->v7m.basepri[M_REG_NS];
        case 0x93:
            if (!arm_feature(env, ARM_FEATURE_M_MAIN)) {
                goto bad_reg;
            }
            if (!env->v7m.secure) {
                return 0;
            }
            return env->v7m.faultmask[M_REG_NS];
        case 0x98:
        {
            bool spsel = env->v7m.control[M_REG_NS] & R_V7M_CONTROL_SPSEL_MASK;

            if (!env->v7m.secure) {
                return 0;
            }
            if (!arm_v7m_is_handler_mode(env) && spsel) {
                return env->v7m.other_ss_psp;
            } else {
                return env->v7m.other_ss_msp;
            }
        }
        default:
            break;
        }
    }

    switch (reg) {
    case 8:
        return v7m_using_psp(env) ? env->v7m.other_sp : env->regs[13];
    case 9:
        return v7m_using_psp(env) ? env->regs[13] : env->v7m.other_sp;
    case 10:
        if (!arm_feature(env, ARM_FEATURE_V8)) {
            goto bad_reg;
        }
        return env->v7m.msplim[env->v7m.secure];
    case 11:
        if (!arm_feature(env, ARM_FEATURE_V8)) {
            goto bad_reg;
        }
        return env->v7m.psplim[env->v7m.secure];
    case 16:
        return env->v7m.primask[env->v7m.secure];
    case 17:
    case 18:
        if (!arm_feature(env, ARM_FEATURE_M_MAIN)) {
            goto bad_reg;
        }
        return env->v7m.basepri[env->v7m.secure];
    case 19:
        if (!arm_feature(env, ARM_FEATURE_M_MAIN)) {
            goto bad_reg;
        }
        return env->v7m.faultmask[env->v7m.secure];
    default:
    bad_reg:
        qemu_log_mask(LOG_GUEST_ERROR, "Attempt to read unknown special"
                                       " register %d\n", reg);
        return 0;
    }
}

void HELPER(v7m_msr)(CPUARMState *env, uint32_t maskreg, uint32_t val)
{
    uint32_t mask = extract32(maskreg, 8, 4);
    uint32_t reg = extract32(maskreg, 0, 8);
    int cur_el = arm_current_el(env);

    if (cur_el == 0 && reg > 7 && reg != 20) {
        return;
    }

    if (arm_feature(env, ARM_FEATURE_M_SECURITY)) {
        switch (reg) {
        case 0x88:
            if (!env->v7m.secure) {
                return;
            }
            env->v7m.other_ss_msp = val & ~3;
            return;
        case 0x89:
            if (!env->v7m.secure) {
                return;
            }
            env->v7m.other_ss_psp = val & ~3;
            return;
        case 0x8a:
            if (!env->v7m.secure) {
                return;
            }
            env->v7m.msplim[M_REG_NS] = val & ~7;
            return;
        case 0x8b:
            if (!env->v7m.secure) {
                return;
            }
            env->v7m.psplim[M_REG_NS] = val & ~7;
            return;
        case 0x90:
            if (!env->v7m.secure) {
                return;
            }
            env->v7m.primask[M_REG_NS] = val & 1;
            return;
        case 0x91:
            if (!arm_feature(env, ARM_FEATURE_M_MAIN)) {
                goto bad_reg;
            }
            if (!env->v7m.secure) {
                return;
            }
            env->v7m.basepri[M_REG_NS] = val & 0xff;
            return;
        case 0x93:
            if (!arm_feature(env, ARM_FEATURE_M_MAIN)) {
                goto bad_reg;
            }
            if (!env->v7m.secure) {
                return;
            }
            env->v7m.faultmask[M_REG_NS] = val & 1;
            return;
        case 0x94:
            if (!env->v7m.secure) {
                return;
            }
            write_v7m_control_spsel_for_secstate(env,
                                                 val & R_V7M_CONTROL_SPSEL_MASK,
                                                 M_REG_NS);
            if (arm_feature(env, ARM_FEATURE_M_MAIN)) {
                env->v7m.control[M_REG_NS] &= ~R_V7M_CONTROL_NPRIV_MASK;
                env->v7m.control[M_REG_NS] |= val & R_V7M_CONTROL_NPRIV_MASK;
            }
            if (cpu_isar_feature(aa32_vfp_simd, env_archcpu(env)) &&
                extract32(env->v7m.nsacr, 10, 1)) {
                env->v7m.control[M_REG_S] &= ~R_V7M_CONTROL_FPCA_MASK;
                env->v7m.control[M_REG_S] |= val & R_V7M_CONTROL_FPCA_MASK;
            }
            return;
        case 0x98:
        {
            bool spsel = env->v7m.control[M_REG_NS] & R_V7M_CONTROL_SPSEL_MASK;
            bool is_psp = !arm_v7m_is_handler_mode(env) && spsel;
            uint32_t limit;

            if (!env->v7m.secure) {
                return;
            }

            limit = is_psp ? env->v7m.psplim[false] : env->v7m.msplim[false];

            val &= ~0x3;

            if (val < limit) {
                raise_exception_ra(env, EXCP_STKOF, 0, 1, GETPC());
            }

            if (is_psp) {
                env->v7m.other_ss_psp = val;
            } else {
                env->v7m.other_ss_msp = val;
            }
            return;
        }
        default:
            break;
        }
    }

    switch (reg) {
    case 0 ... 7:
        v7m_msr_xpsr(env, mask, reg, val);
        break;
    case 8:
        if (v7m_using_psp(env)) {
            env->v7m.other_sp = val & ~3;
        } else {
            env->regs[13] = val & ~3;
        }
        break;
    case 9:
        if (v7m_using_psp(env)) {
            env->regs[13] = val & ~3;
        } else {
            env->v7m.other_sp = val & ~3;
        }
        break;
    case 10:
        if (!arm_feature(env, ARM_FEATURE_V8)) {
            goto bad_reg;
        }
        env->v7m.msplim[env->v7m.secure] = val & ~7;
        break;
    case 11:
        if (!arm_feature(env, ARM_FEATURE_V8)) {
            goto bad_reg;
        }
        env->v7m.psplim[env->v7m.secure] = val & ~7;
        break;
    case 16:
        env->v7m.primask[env->v7m.secure] = val & 1;
        break;
    case 17:
        if (!arm_feature(env, ARM_FEATURE_M_MAIN)) {
            goto bad_reg;
        }
        env->v7m.basepri[env->v7m.secure] = val & 0xff;
        break;
    case 18:
        if (!arm_feature(env, ARM_FEATURE_M_MAIN)) {
            goto bad_reg;
        }
        val &= 0xff;
        if (val != 0 && (val < env->v7m.basepri[env->v7m.secure]
                         || env->v7m.basepri[env->v7m.secure] == 0)) {
            env->v7m.basepri[env->v7m.secure] = val;
        }
        break;
    case 19:
        if (!arm_feature(env, ARM_FEATURE_M_MAIN)) {
            goto bad_reg;
        }
        env->v7m.faultmask[env->v7m.secure] = val & 1;
        break;
    case 20:
        if (cur_el > 0 && (arm_feature(env, ARM_FEATURE_V8) ||
                           !arm_v7m_is_handler_mode(env))) {
            write_v7m_control_spsel(env, (val & R_V7M_CONTROL_SPSEL_MASK) != 0);
        }
        if (cur_el > 0 && arm_feature(env, ARM_FEATURE_M_MAIN)) {
            env->v7m.control[env->v7m.secure] &= ~R_V7M_CONTROL_NPRIV_MASK;
            env->v7m.control[env->v7m.secure] |= val & R_V7M_CONTROL_NPRIV_MASK;
        }
        if (cpu_isar_feature(aa32_vfp_simd, env_archcpu(env))) {
            if (env->v7m.secure) {
                env->v7m.control[M_REG_S] &= ~R_V7M_CONTROL_SFPA_MASK;
                env->v7m.control[M_REG_S] |= val & R_V7M_CONTROL_SFPA_MASK;
            }
            if (cur_el > 0 &&
                (env->v7m.secure || !arm_feature(env, ARM_FEATURE_M_SECURITY) ||
                 extract32(env->v7m.nsacr, 10, 1))) {
                env->v7m.control[M_REG_S] &= ~R_V7M_CONTROL_FPCA_MASK;
                env->v7m.control[M_REG_S] |= val & R_V7M_CONTROL_FPCA_MASK;
            }
        }
        break;
    default:
    bad_reg:
        qemu_log_mask(LOG_GUEST_ERROR, "Attempt to write unknown special"
                                       " register %d\n", reg);
        return;
    }
}

uint32_t HELPER(v7m_tt)(CPUARMState *env, uint32_t addr, uint32_t op)
{
    bool forceunpriv = op & 1;
    bool alt = op & 2;
    V8M_SAttributes sattrs = {};
    uint32_t tt_resp;
    bool r, rw, nsr, nsrw, mrvalid;
    ARMMMUIdx mmu_idx;
    uint32_t mregion;
    bool targetpriv;
    bool targetsec = env->v7m.secure;

    if (alt) {
        targetsec = !targetsec;
    }

    if (forceunpriv) {
        targetpriv = false;
    } else {
        targetpriv = arm_v7m_is_handler_mode(env) ||
            !(env->v7m.control[targetsec] & R_V7M_CONTROL_NPRIV_MASK);
    }

    mmu_idx = arm_v7m_mmu_idx_for_secstate_and_priv(env, targetsec, targetpriv);

    if (arm_current_el(env) != 0 || alt) {
        GetPhysAddrResult res = {};
        ARMMMUFaultInfo fi = {};

        pmsav8_mpu_lookup(env, addr, MMU_DATA_LOAD, PAGE_READ, mmu_idx,
                          targetsec, &res, &fi, &mregion);
        if (mregion == -1) {
            mrvalid = false;
            mregion = 0;
        } else {
            mrvalid = true;
        }
        r = res.f.prot & PAGE_READ;
        rw = res.f.prot & PAGE_WRITE;
    } else {
        r = false;
        rw = false;
        mrvalid = false;
        mregion = 0;
    }

    if (env->v7m.secure) {
        v8m_security_lookup(env, addr, MMU_DATA_LOAD, mmu_idx,
                            targetsec, &sattrs);
        nsr = sattrs.ns && r;
        nsrw = sattrs.ns && rw;
    } else {
        sattrs.ns = true;
        nsr = false;
        nsrw = false;
    }

    tt_resp = (sattrs.iregion << 24) |
        (sattrs.irvalid << 23) |
        ((!sattrs.ns) << 22) |
        (nsrw << 21) |
        (nsr << 20) |
        (rw << 19) |
        (r << 18) |
        (sattrs.srvalid << 17) |
        (mrvalid << 16) |
        (sattrs.sregion << 8) |
        mregion;

    return tt_resp;
}

#endif

uint32_t *arm_v7m_get_sp_ptr(CPUARMState *env, bool secure, bool threadmode,
                             bool spsel)
{
    bool want_psp = threadmode && spsel;

    if (secure == env->v7m.secure) {
        if (want_psp == v7m_using_psp(env)) {
            return &env->regs[13];
        } else {
            return &env->v7m.other_sp;
        }
    } else {
        if (want_psp) {
            return &env->v7m.other_ss_psp;
        } else {
            return &env->v7m.other_ss_msp;
        }
    }
}
