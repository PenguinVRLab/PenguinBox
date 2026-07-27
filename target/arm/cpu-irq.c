/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * QEMU ARM CPU - interrupt_request handling
 *
 * Copyright (c) 2003-2025 QEMU contributors
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "accel/tcg/cpu-ops.h"
#include "internals.h"

#ifdef CONFIG_TCG
static inline bool arm_excp_unmasked(CPUState *cs, unsigned int excp_idx,
                                     unsigned int target_el,
                                     unsigned int cur_el, bool secure,
                                     uint64_t hcr_el2)
{
    CPUARMState *env = cpu_env(cs);
    bool pstate_unmasked;
    bool unmasked = false;
    bool allIntMask = false;

    if (cur_el > target_el) {
        return false;
    }

    if (cpu_isar_feature(aa64_nmi, env_archcpu(env)) &&
        env->cp15.sctlr_el[target_el] & SCTLR_NMI && cur_el == target_el) {
        allIntMask = env->pstate & PSTATE_ALLINT ||
                     ((env->cp15.sctlr_el[target_el] & SCTLR_SPINTMASK) &&
                      (env->pstate & PSTATE_SP));
    }

    switch (excp_idx) {
    case EXCP_NMI:
        pstate_unmasked = !allIntMask;
        break;

    case EXCP_VINMI:
        if (!(hcr_el2 & HCR_IMO) || (hcr_el2 & HCR_TGE)) {
            return false;
        }
        return !allIntMask;
    case EXCP_VFNMI:
        if (!(hcr_el2 & HCR_FMO) || (hcr_el2 & HCR_TGE)) {
            return false;
        }
        return !allIntMask;
    case EXCP_FIQ:
        pstate_unmasked = (!(env->daif & PSTATE_F)) && (!allIntMask);
        break;

    case EXCP_IRQ:
        pstate_unmasked = (!(env->daif & PSTATE_I)) && (!allIntMask);
        break;

    case EXCP_VFIQ:
        if (!(hcr_el2 & HCR_FMO) || (hcr_el2 & HCR_TGE)) {
            return false;
        }
        return !(env->daif & PSTATE_F) && (!allIntMask);
    case EXCP_VIRQ:
        if (!(hcr_el2 & HCR_IMO) || (hcr_el2 & HCR_TGE)) {
            return false;
        }
        return !(env->daif & PSTATE_I) && (!allIntMask);
    case EXCP_VSERR:
        if (!(hcr_el2 & HCR_AMO) || (hcr_el2 & HCR_TGE)) {
            return false;
        }
        return !(env->daif & PSTATE_A);
    default:
        g_assert_not_reached();
    }

    if ((target_el > cur_el) && (target_el != 1)) {
        if (arm_feature(env, ARM_FEATURE_AARCH64)) {
            switch (target_el) {
            case 2:
                if ((hcr_el2 & (HCR_E2H | HCR_TGE)) != (HCR_E2H | HCR_TGE)) {
                        unmasked = true;
                }
                break;
            case 3:
                unmasked = true;
                break;
            default:
                g_assert_not_reached();
            }
        } else {
            bool hcr, scr;

            switch (excp_idx) {
            case EXCP_FIQ:
                hcr = hcr_el2 & HCR_FMO;
                scr = (env->cp15.scr_el3 & SCR_FIQ);

                scr = scr && !((env->cp15.scr_el3 & SCR_FW) && !hcr);
                break;
            case EXCP_IRQ:
                hcr = hcr_el2 & HCR_IMO;
                scr = false;
                break;
            default:
                g_assert_not_reached();
            }

            if ((scr || hcr) && !secure) {
                unmasked = true;
            }
        }
    }

    return unmasked || pstate_unmasked;
}

bool arm_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    CPUARMState *env = cpu_env(cs);
    uint32_t cur_el = arm_current_el(env);
    bool secure = arm_is_secure(env);
    uint64_t hcr_el2 = arm_hcr_el2_eff(env);
    uint32_t target_el;
    uint32_t excp_idx;

    if (cpu_isar_feature(aa64_nmi, env_archcpu(env)) &&
        (arm_sctlr(env, cur_el) & SCTLR_NMI)) {
        if (interrupt_request & CPU_INTERRUPT_NMI) {
            excp_idx = EXCP_NMI;
            target_el = arm_phys_excp_target_el(cs, excp_idx, cur_el, secure);
            if (arm_excp_unmasked(cs, excp_idx, target_el,
                                  cur_el, secure, hcr_el2)) {
                goto found;
            }
        }
        if (interrupt_request & CPU_INTERRUPT_VINMI) {
            excp_idx = EXCP_VINMI;
            target_el = 1;
            if (arm_excp_unmasked(cs, excp_idx, target_el,
                                  cur_el, secure, hcr_el2)) {
                goto found;
            }
        }
        if (interrupt_request & CPU_INTERRUPT_VFNMI) {
            excp_idx = EXCP_VFNMI;
            target_el = 1;
            if (arm_excp_unmasked(cs, excp_idx, target_el,
                                  cur_el, secure, hcr_el2)) {
                goto found;
            }
        }
    } else {
        if (interrupt_request & CPU_INTERRUPT_NMI) {
            interrupt_request |= CPU_INTERRUPT_HARD;
        }
        if (interrupt_request & CPU_INTERRUPT_VINMI) {
            interrupt_request |= CPU_INTERRUPT_VIRQ;
        }
        if (interrupt_request & CPU_INTERRUPT_VFNMI) {
            interrupt_request |= CPU_INTERRUPT_VFIQ;
        }
    }

    if (interrupt_request & CPU_INTERRUPT_FIQ) {
        excp_idx = EXCP_FIQ;
        target_el = arm_phys_excp_target_el(cs, excp_idx, cur_el, secure);
        if (arm_excp_unmasked(cs, excp_idx, target_el,
                              cur_el, secure, hcr_el2)) {
            goto found;
        }
    }
    if (interrupt_request & CPU_INTERRUPT_HARD) {
        excp_idx = EXCP_IRQ;
        target_el = arm_phys_excp_target_el(cs, excp_idx, cur_el, secure);
        if (arm_excp_unmasked(cs, excp_idx, target_el,
                              cur_el, secure, hcr_el2)) {
            goto found;
        }
    }
    if (interrupt_request & CPU_INTERRUPT_VIRQ) {
        excp_idx = EXCP_VIRQ;
        target_el = 1;
        if (arm_excp_unmasked(cs, excp_idx, target_el,
                              cur_el, secure, hcr_el2)) {
            goto found;
        }
    }
    if (interrupt_request & CPU_INTERRUPT_VFIQ) {
        excp_idx = EXCP_VFIQ;
        target_el = 1;
        if (arm_excp_unmasked(cs, excp_idx, target_el,
                              cur_el, secure, hcr_el2)) {
            goto found;
        }
    }
    if (interrupt_request & CPU_INTERRUPT_VSERR) {
        excp_idx = EXCP_VSERR;
        target_el = 1;
        if (arm_excp_unmasked(cs, excp_idx, target_el,
                              cur_el, secure, hcr_el2)) {
            env->cp15.hcr_el2 &= ~HCR_VSE;
            cpu_reset_interrupt(cs, CPU_INTERRUPT_VSERR);
            goto found;
        }
    }
    return false;

 found:
    cs->exception_index = excp_idx;
    env->exception.target_el = target_el;
    cs->cc->tcg_ops->do_interrupt(cs);
    return true;
}
#endif

void arm_cpu_update_virq(ARMCPU *cpu)
{
    CPUARMState *env = &cpu->env;
    CPUState *cs = CPU(cpu);

    bool new_state = ((arm_hcr_el2_eff(env) & HCR_VI) &&
        !(arm_hcrx_el2_eff(env) & HCRX_VINMI)) ||
        (env->irq_line_state & CPU_INTERRUPT_VIRQ);

    if (new_state != cpu_test_interrupt(cs, CPU_INTERRUPT_VIRQ)) {
        if (new_state) {
            cpu_interrupt(cs, CPU_INTERRUPT_VIRQ);
        } else {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_VIRQ);
        }
    }
}

void arm_cpu_update_vfiq(ARMCPU *cpu)
{
    CPUARMState *env = &cpu->env;
    CPUState *cs = CPU(cpu);

    bool new_state = ((arm_hcr_el2_eff(env) & HCR_VF) &&
        !(arm_hcrx_el2_eff(env) & HCRX_VFNMI)) ||
        (env->irq_line_state & CPU_INTERRUPT_VFIQ);

    if (new_state != cpu_test_interrupt(cs, CPU_INTERRUPT_VFIQ)) {
        if (new_state) {
            cpu_interrupt(cs, CPU_INTERRUPT_VFIQ);
        } else {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_VFIQ);
        }
    }
}

void arm_cpu_update_vinmi(ARMCPU *cpu)
{
    CPUARMState *env = &cpu->env;
    CPUState *cs = CPU(cpu);

    bool new_state = ((arm_hcr_el2_eff(env) & HCR_VI) &&
                      (arm_hcrx_el2_eff(env) & HCRX_VINMI)) ||
        (env->irq_line_state & CPU_INTERRUPT_VINMI);

    if (new_state != cpu_test_interrupt(cs, CPU_INTERRUPT_VINMI)) {
        if (new_state) {
            cpu_interrupt(cs, CPU_INTERRUPT_VINMI);
        } else {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_VINMI);
        }
    }
}

void arm_cpu_update_vfnmi(ARMCPU *cpu)
{
    CPUARMState *env = &cpu->env;
    CPUState *cs = CPU(cpu);

    bool new_state = (arm_hcr_el2_eff(env) & HCR_VF) &&
                      (arm_hcrx_el2_eff(env) & HCRX_VFNMI);

    if (new_state != cpu_test_interrupt(cs, CPU_INTERRUPT_VFNMI)) {
        if (new_state) {
            cpu_interrupt(cs, CPU_INTERRUPT_VFNMI);
        } else {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_VFNMI);
        }
    }
}

void arm_cpu_update_vserr(ARMCPU *cpu)
{
    CPUARMState *env = &cpu->env;
    CPUState *cs = CPU(cpu);

    bool new_state = env->cp15.hcr_el2 & HCR_VSE;

    if (new_state != cpu_test_interrupt(cs, CPU_INTERRUPT_VSERR)) {
        if (new_state) {
            cpu_interrupt(cs, CPU_INTERRUPT_VSERR);
        } else {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_VSERR);
        }
    }
}

