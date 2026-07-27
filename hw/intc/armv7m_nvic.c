/*
 * ARM Nested Vectored Interrupt Controller
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 *
 * The ARMv7M System controller is fairly tightly tied in with the
 * NVIC.  Much of that is also implemented here.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/timer.h"
#include "hw/intc/armv7m_nvic.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "system/tcg.h"
#include "system/runstate.h"
#include "target/arm/cpu.h"
#include "target/arm/cpu-features.h"
#include "exec/cputlb.h"
#include "exec/memop.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"

#define NVIC_FIRST_IRQ NVIC_INTERNAL_VECTORS
#define NVIC_MAX_IRQ (NVIC_MAX_VECTORS - NVIC_FIRST_IRQ)

#define NVIC_NOEXC_PRIO 0x100
#define NVIC_NS_PRIO_LIMIT 0x80

static const uint8_t nvic_id[] = {
    0x00, 0xb0, 0x1b, 0x00, 0x0d, 0xe0, 0x05, 0xb1
};

static void signal_sysresetreq(NVICState *s)
{
    if (qemu_irq_is_connected(s->sysresetreq)) {
        qemu_irq_pulse(s->sysresetreq);
    } else {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }
}

static int nvic_pending_prio(NVICState *s)
{
    return s->vectpending_prio;
}

static bool nvic_rettobase(NVICState *s)
{
    int irq, nhand = 0;
    bool check_sec = arm_feature(&s->cpu->env, ARM_FEATURE_M_SECURITY);

    for (irq = ARMV7M_EXCP_RESET; irq < s->num_irq; irq++) {
        if (s->vectors[irq].active ||
            (check_sec && irq < NVIC_INTERNAL_VECTORS &&
             s->sec_vectors[irq].active)) {
            nhand++;
            if (nhand == 2) {
                return 0;
            }
        }
    }

    return 1;
}

static bool nvic_isrpending(NVICState *s)
{
    int irq;

    if (s->vectpending > NVIC_FIRST_IRQ) {
        return true;
    }

    for (irq = NVIC_FIRST_IRQ; irq < s->num_irq; irq++) {
        if (s->vectors[irq].pending) {
            return true;
        }
    }
    return false;
}

static bool exc_is_banked(int exc)
{
    return exc == ARMV7M_EXCP_HARD ||
        exc == ARMV7M_EXCP_MEM ||
        exc == ARMV7M_EXCP_USAGE ||
        exc == ARMV7M_EXCP_SVC ||
        exc == ARMV7M_EXCP_PENDSV ||
        exc == ARMV7M_EXCP_SYSTICK;
}

static inline uint32_t nvic_gprio_mask(NVICState *s, bool secure)
{
    return ~0U << (s->prigroup[secure] + 1);
}

static bool exc_targets_secure(NVICState *s, int exc)
{
    if (!arm_feature(&s->cpu->env, ARM_FEATURE_M_SECURITY)) {
        return false;
    }

    if (exc >= NVIC_FIRST_IRQ) {
        return !s->itns[exc];
    }

    assert(!exc_is_banked(exc));

    switch (exc) {
    case ARMV7M_EXCP_NMI:
    case ARMV7M_EXCP_BUS:
        return !(s->cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK);
    case ARMV7M_EXCP_SECURE:
        return true;
    case ARMV7M_EXCP_DEBUG:
        return false;
    default:
        return true;
    }
}

static int exc_group_prio(NVICState *s, int rawprio, bool targets_secure)
{
    if (rawprio < 0) {
        return rawprio;
    }
    rawprio &= nvic_gprio_mask(s, targets_secure);
    if (!targets_secure &&
        (s->cpu->env.v7m.aircr & R_V7M_AIRCR_PRIS_MASK)) {
        rawprio = (rawprio >> 1) + NVIC_NS_PRIO_LIMIT;
    }
    return rawprio;
}

static void nvic_recompute_state_secure(NVICState *s)
{
    int i, bank;
    int pend_prio = NVIC_NOEXC_PRIO;
    int active_prio = NVIC_NOEXC_PRIO;
    int pend_irq = 0;
    bool pending_is_s_banked = false;
    int pend_subprio = 0;

    for (i = 1; i < s->num_irq; i++) {
        for (bank = M_REG_S; bank >= M_REG_NS; bank--) {
            VecInfo *vec;
            int prio, subprio;
            bool targets_secure;

            if (bank == M_REG_S) {
                if (!exc_is_banked(i)) {
                    continue;
                }
                vec = &s->sec_vectors[i];
                targets_secure = true;
            } else {
                vec = &s->vectors[i];
                targets_secure = !exc_is_banked(i) && exc_targets_secure(s, i);
            }

            prio = exc_group_prio(s, vec->prio, targets_secure);
            subprio = vec->prio & ~nvic_gprio_mask(s, targets_secure);
            if (vec->enabled && vec->pending &&
                ((prio < pend_prio) ||
                 (prio == pend_prio && prio >= 0 && subprio < pend_subprio))) {
                pend_prio = prio;
                pend_subprio = subprio;
                pend_irq = i;
                pending_is_s_banked = (bank == M_REG_S);
            }
            if (vec->active && prio < active_prio) {
                active_prio = prio;
            }
        }
    }

    s->vectpending_is_s_banked = pending_is_s_banked;
    s->vectpending = pend_irq;
    s->vectpending_prio = pend_prio;
    s->exception_prio = active_prio;

    trace_nvic_recompute_state_secure(s->vectpending,
                                      s->vectpending_is_s_banked,
                                      s->vectpending_prio,
                                      s->exception_prio);
}

static void nvic_recompute_state(NVICState *s)
{
    int i;
    int pend_prio = NVIC_NOEXC_PRIO;
    int active_prio = NVIC_NOEXC_PRIO;
    int pend_irq = 0;

    if (arm_feature(&s->cpu->env, ARM_FEATURE_M_SECURITY)) {
        nvic_recompute_state_secure(s);
        return;
    }

    for (i = 1; i < s->num_irq; i++) {
        VecInfo *vec = &s->vectors[i];

        if (vec->enabled && vec->pending && vec->prio < pend_prio) {
            pend_prio = vec->prio;
            pend_irq = i;
        }
        if (vec->active && vec->prio < active_prio) {
            active_prio = vec->prio;
        }
    }

    if (active_prio > 0) {
        active_prio &= nvic_gprio_mask(s, false);
    }

    if (pend_prio > 0) {
        pend_prio &= nvic_gprio_mask(s, false);
    }

    s->vectpending = pend_irq;
    s->vectpending_prio = pend_prio;
    s->exception_prio = active_prio;

    trace_nvic_recompute_state(s->vectpending,
                               s->vectpending_prio,
                               s->exception_prio);
}

static inline int nvic_exec_prio(NVICState *s)
{
    CPUARMState *env = &s->cpu->env;
    int running = NVIC_NOEXC_PRIO;

    if (env->v7m.basepri[M_REG_NS] > 0) {
        running = exc_group_prio(s, env->v7m.basepri[M_REG_NS], M_REG_NS);
    }

    if (env->v7m.basepri[M_REG_S] > 0) {
        int basepri = exc_group_prio(s, env->v7m.basepri[M_REG_S], M_REG_S);
        if (running > basepri) {
            running = basepri;
        }
    }

    if (env->v7m.primask[M_REG_NS]) {
        if (env->v7m.aircr & R_V7M_AIRCR_PRIS_MASK) {
            if (running > NVIC_NS_PRIO_LIMIT) {
                running = NVIC_NS_PRIO_LIMIT;
            }
        } else {
            running = 0;
        }
    }

    if (env->v7m.primask[M_REG_S]) {
        running = 0;
    }

    if (env->v7m.faultmask[M_REG_NS]) {
        if (env->v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK) {
            running = -1;
        } else {
            if (env->v7m.aircr & R_V7M_AIRCR_PRIS_MASK) {
                if (running > NVIC_NS_PRIO_LIMIT) {
                    running = NVIC_NS_PRIO_LIMIT;
                }
            } else {
                running = 0;
            }
        }
    }

    if (env->v7m.faultmask[M_REG_S]) {
        running = (env->v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK) ? -3 : -1;
    }

    return MIN(running, s->exception_prio);
}

bool armv7m_nvic_neg_prio_requested(NVICState *s, bool secure)
{
    if (s->cpu->env.v7m.faultmask[secure]) {
        return true;
    }

    if (secure ? s->sec_vectors[ARMV7M_EXCP_HARD].active :
        s->vectors[ARMV7M_EXCP_HARD].active) {
        return true;
    }

    if (s->vectors[ARMV7M_EXCP_NMI].active &&
        exc_targets_secure(s, ARMV7M_EXCP_NMI) == secure) {
        return true;
    }

    return false;
}

bool armv7m_nvic_can_take_pending_exception(NVICState *s)
{
    return nvic_exec_prio(s) > nvic_pending_prio(s);
}

int armv7m_nvic_raw_execution_priority(NVICState *s)
{
    return s->exception_prio;
}

static void set_prio(NVICState *s, unsigned irq, bool secure, uint8_t prio)
{
    assert(irq > ARMV7M_EXCP_NMI);
    assert(irq < s->num_irq);

    prio &= MAKE_64BIT_MASK(8 - s->num_prio_bits, s->num_prio_bits);

    if (secure) {
        assert(exc_is_banked(irq));
        s->sec_vectors[irq].prio = prio;
    } else {
        s->vectors[irq].prio = prio;
    }

    trace_nvic_set_prio(irq, secure, prio);
}

static int get_prio(NVICState *s, unsigned irq, bool secure)
{
    assert(irq > ARMV7M_EXCP_NMI);
    assert(irq < s->num_irq);

    if (secure) {
        assert(exc_is_banked(irq));
        return s->sec_vectors[irq].prio;
    } else {
        return s->vectors[irq].prio;
    }
}

static void nvic_irq_update(NVICState *s)
{
    int lvl;
    int pend_prio;

    nvic_recompute_state(s);
    pend_prio = nvic_pending_prio(s);

    lvl = (pend_prio < s->exception_prio);
    trace_nvic_irq_update(s->vectpending, pend_prio, s->exception_prio, lvl);
    qemu_set_irq(s->excpout, lvl);
}

static void armv7m_nvic_clear_pending(NVICState *s, int irq, bool secure)
{
    VecInfo *vec;

    assert(irq > ARMV7M_EXCP_RESET && irq < s->num_irq);

    if (secure) {
        assert(exc_is_banked(irq));
        vec = &s->sec_vectors[irq];
    } else {
        vec = &s->vectors[irq];
    }
    trace_nvic_clear_pending(irq, secure, vec->enabled, vec->prio);
    if (vec->pending) {
        vec->pending = 0;
        nvic_irq_update(s);
    }
}

static void do_armv7m_nvic_set_pending(void *opaque, int irq, bool secure,
                                       bool derived)
{

    NVICState *s = (NVICState *)opaque;
    bool banked = exc_is_banked(irq);
    VecInfo *vec;
    bool targets_secure;

    assert(irq > ARMV7M_EXCP_RESET && irq < s->num_irq);
    assert(!secure || banked);

    vec = (banked && secure) ? &s->sec_vectors[irq] : &s->vectors[irq];

    targets_secure = banked ? secure : exc_targets_secure(s, irq);

    trace_nvic_set_pending(irq, secure, targets_secure,
                           derived, vec->enabled, vec->prio);

    if (derived) {
        assert(irq >= ARMV7M_EXCP_HARD && irq < ARMV7M_EXCP_PENDSV);

        if (irq == ARMV7M_EXCP_DEBUG &&
            exc_group_prio(s, vec->prio, secure) >= nvic_exec_prio(s)) {
            return;
        }

        if (irq == ARMV7M_EXCP_HARD && vec->prio >= s->vectpending_prio) {
            cpu_abort(CPU(s->cpu),
                      "Lockup: can't take terminal derived exception "
                      "(original exception priority %d)\n",
                      s->vectpending_prio);
        }
    }

    if (irq >= ARMV7M_EXCP_HARD && irq < ARMV7M_EXCP_PENDSV) {
        int running = nvic_exec_prio(s);
        bool escalate = false;

        if (exc_group_prio(s, vec->prio, secure) >= running) {
            trace_nvic_escalate_prio(irq, vec->prio, running);
            escalate = true;
        } else if (!vec->enabled) {
            trace_nvic_escalate_disabled(irq);
            escalate = true;
        }

        if (escalate) {

            irq = ARMV7M_EXCP_HARD;
            if (arm_feature(&s->cpu->env, ARM_FEATURE_M_SECURITY) &&
                (targets_secure ||
                 !(s->cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK))) {
                vec = &s->sec_vectors[irq];
            } else {
                vec = &s->vectors[irq];
            }
            if (running <= vec->prio) {
                cpu_abort(CPU(s->cpu),
                          "Lockup: can't escalate %d to HardFault "
                          "(current priority %d)\n", irq, running);
            }

            s->cpu->env.v7m.hfsr |= R_V7M_HFSR_FORCED_MASK;
        }
    }

    if (!vec->pending) {
        vec->pending = 1;
        nvic_irq_update(s);
    }
}

void armv7m_nvic_set_pending(NVICState *s, int irq, bool secure)
{
    do_armv7m_nvic_set_pending(s, irq, secure, false);
}

void armv7m_nvic_set_pending_derived(NVICState *s, int irq, bool secure)
{
    do_armv7m_nvic_set_pending(s, irq, secure, true);
}

void armv7m_nvic_set_pending_lazyfp(NVICState *s, int irq, bool secure)
{
    bool banked = exc_is_banked(irq);
    VecInfo *vec;
    bool targets_secure;
    bool escalate = false;
    uint32_t fpccr_s = s->cpu->env.v7m.fpccr[M_REG_S];
    uint32_t fpccr = s->cpu->env.v7m.fpccr[secure];

    assert(irq > ARMV7M_EXCP_RESET && irq < s->num_irq);
    assert(!secure || banked);

    vec = (banked && secure) ? &s->sec_vectors[irq] : &s->vectors[irq];

    targets_secure = banked ? secure : exc_targets_secure(s, irq);

    switch (irq) {
    case ARMV7M_EXCP_DEBUG:
        if (!(fpccr_s & R_V7M_FPCCR_MONRDY_MASK)) {
            return;
        }
        break;
    case ARMV7M_EXCP_MEM:
        escalate = !(fpccr & R_V7M_FPCCR_MMRDY_MASK);
        break;
    case ARMV7M_EXCP_USAGE:
        escalate = !(fpccr & R_V7M_FPCCR_UFRDY_MASK);
        break;
    case ARMV7M_EXCP_BUS:
        escalate = !(fpccr_s & R_V7M_FPCCR_BFRDY_MASK);
        break;
    case ARMV7M_EXCP_SECURE:
        escalate = !(fpccr_s & R_V7M_FPCCR_SFRDY_MASK);
        break;
    default:
        g_assert_not_reached();
    }

    if (escalate) {
        irq = ARMV7M_EXCP_HARD;
        if (arm_feature(&s->cpu->env, ARM_FEATURE_M_SECURITY) &&
            (targets_secure ||
             !(s->cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK))) {
            vec = &s->sec_vectors[irq];
        } else {
            vec = &s->vectors[irq];
        }
    }

    if (!vec->enabled ||
        nvic_exec_prio(s) <= exc_group_prio(s, vec->prio, secure)) {
        if (!(fpccr_s & R_V7M_FPCCR_HFRDY_MASK)) {
            cpu_abort(CPU(s->cpu),
                      "Lockup: can't escalate to HardFault during "
                      "lazy FP register stacking\n");
        }
    }

    if (escalate) {
        s->cpu->env.v7m.hfsr |= R_V7M_HFSR_FORCED_MASK;
    }
    if (!vec->pending) {
        vec->pending = 1;
        nvic_recompute_state(s);
    }
}

void armv7m_nvic_acknowledge_irq(NVICState *s)
{
    CPUARMState *env = &s->cpu->env;
    const int pending = s->vectpending;
    const int running = nvic_exec_prio(s);
    VecInfo *vec;

    assert(pending > ARMV7M_EXCP_RESET && pending < s->num_irq);

    if (s->vectpending_is_s_banked) {
        vec = &s->sec_vectors[pending];
    } else {
        vec = &s->vectors[pending];
    }

    assert(vec->enabled);
    assert(vec->pending);

    assert(s->vectpending_prio < running);

    trace_nvic_acknowledge_irq(pending, s->vectpending_prio);

    vec->active = 1;
    vec->pending = 0;

    write_v7m_exception(env, s->vectpending);

    nvic_irq_update(s);
}

static bool vectpending_targets_secure(NVICState *s)
{
    if (s->vectpending_is_s_banked) {
        return true;
    }
    return !exc_is_banked(s->vectpending) &&
        exc_targets_secure(s, s->vectpending);
}

void armv7m_nvic_get_pending_irq_info(NVICState *s,
                                      int *pirq, bool *ptargets_secure)
{
    const int pending = s->vectpending;
    bool targets_secure;

    assert(pending > ARMV7M_EXCP_RESET && pending < s->num_irq);

    targets_secure = vectpending_targets_secure(s);

    trace_nvic_get_pending_irq_info(pending, targets_secure);

    *ptargets_secure = targets_secure;
    *pirq = pending;
}

int armv7m_nvic_complete_irq(NVICState *s, int irq, bool secure)
{
    VecInfo *vec = NULL;
    int ret = 0;

    assert(irq > ARMV7M_EXCP_RESET && irq < s->num_irq);

    trace_nvic_complete_irq(irq, secure);

    if (secure && exc_is_banked(irq)) {
        vec = &s->sec_vectors[irq];
    } else {
        vec = &s->vectors[irq];
    }

    if (!exc_is_banked(irq) && exc_targets_secure(s, irq) != secure) {
        ret = -1;
        vec = NULL;
    } else if (!vec->active) {
        ret = -1;
    } else {
        ret = nvic_rettobase(s);
    }

    if (arm_feature(&s->cpu->env, ARM_FEATURE_V8)) {
        switch (armv7m_nvic_raw_execution_priority(s)) {
        case -1:
            if (s->cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK) {
                vec = &s->vectors[ARMV7M_EXCP_HARD];
            } else {
                vec = &s->sec_vectors[ARMV7M_EXCP_HARD];
            }
            break;
        case -2:
            vec = &s->vectors[ARMV7M_EXCP_NMI];
            break;
        case -3:
            vec = &s->sec_vectors[ARMV7M_EXCP_HARD];
            break;
        default:
            break;
        }
    }

    if (!vec) {
        return ret;
    }

    vec->active = 0;
    if (vec->level) {
        assert(irq >= NVIC_FIRST_IRQ);
        vec->pending = 1;
    }

    nvic_irq_update(s);

    return ret;
}

bool armv7m_nvic_get_ready_status(NVICState *s, int irq, bool secure)
{
    bool banked = exc_is_banked(irq);
    VecInfo *vec;
    int running = nvic_exec_prio(s);

    assert(irq > ARMV7M_EXCP_RESET && irq < s->num_irq);
    assert(!secure || banked);

    if (irq == ARMV7M_EXCP_HARD) {
        return running > -1;
    }

    vec = (banked && secure) ? &s->sec_vectors[irq] : &s->vectors[irq];

    return vec->enabled &&
        exc_group_prio(s, vec->prio, secure) < running;
}

static void set_irq_level(void *opaque, int n, int level)
{
    NVICState *s = opaque;
    VecInfo *vec;

    n += NVIC_FIRST_IRQ;

    assert(n >= NVIC_FIRST_IRQ && n < s->num_irq);

    trace_nvic_set_irq_level(n, level);

    vec = &s->vectors[n];
    if (level != vec->level) {
        vec->level = level;
        if (level) {
            armv7m_nvic_set_pending(s, n, false);
        }
    }
}

static void nvic_nmi_trigger(void *opaque, int n, int level)
{
    NVICState *s = opaque;

    trace_nvic_set_nmi_level(level);

    if (level) {
        armv7m_nvic_set_pending(s, ARMV7M_EXCP_NMI, false);
    }
}

static uint32_t nvic_readl(NVICState *s, uint32_t offset, MemTxAttrs attrs)
{
    ARMCPU *cpu = s->cpu;
    ARMISARegisters *isar = &cpu->isar;
    uint32_t val;

    switch (offset) {
    case 4:
        if (!arm_feature(&cpu->env, ARM_FEATURE_V7)) {
            goto bad_offset;
        }
        return ((s->num_irq - NVIC_FIRST_IRQ) / 32) - 1;
    case 0xc:
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        return 0;
    case 0x380 ... 0x3bf:
    {
        int startvec = 8 * (offset - 0x380) + NVIC_FIRST_IRQ;
        int i;

        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return 0;
        }
        val = 0;
        for (i = 0; i < 32 && startvec + i < s->num_irq; i++) {
            if (s->itns[startvec + i]) {
                val |= (1 << i);
            }
        }
        return val;
    }
    case 0xcfc:
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8_1M)) {
            goto bad_offset;
        }
        return cpu->revidr;
    case 0xd00:
        return cpu->midr;
    case 0xd04:
        val = cpu->env.v7m.exception;
        if (s->vectpending) {
            int vp = s->vectpending;
            if (!attrs.secure && arm_feature(&cpu->env, ARM_FEATURE_V8_1M) &&
                vectpending_targets_secure(s)) {
                vp = 1;
            }
            val |= (vp & 0x1ff) << 12;
        }
        if (nvic_isrpending(s)) {
            val |= (1 << 22);
        }
        if (nvic_rettobase(s)) {
            val |= (1 << 11);
        }
        if (attrs.secure) {
            if (s->sec_vectors[ARMV7M_EXCP_SYSTICK].pending) {
                val |= (1 << 26);
            }
            if (s->sec_vectors[ARMV7M_EXCP_PENDSV].pending) {
                val |= (1 << 28);
            }
        } else {
            if (s->vectors[ARMV7M_EXCP_SYSTICK].pending) {
                val |= (1 << 26);
            }
            if (s->vectors[ARMV7M_EXCP_PENDSV].pending) {
                val |= (1 << 28);
            }
        }
        if ((attrs.secure || (cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK))
            && s->vectors[ARMV7M_EXCP_NMI].pending) {
            val |= (1 << 31);
        }
        return val;
    case 0xd08:
        return cpu->env.v7m.vecbase[attrs.secure];
    case 0xd0c:
        val = 0xfa050000 | (s->prigroup[attrs.secure] << 8);
        if (attrs.secure) {
            val |= cpu->env.v7m.aircr;
        } else {
            if (arm_feature(&cpu->env, ARM_FEATURE_V8)) {
                val |= cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK;
            }
        }
        return val;
    case 0xd10:
        if (!arm_feature(&cpu->env, ARM_FEATURE_V7)) {
            goto bad_offset;
        }
        return cpu->env.v7m.scr[attrs.secure];
    case 0xd14:
        val = cpu->env.v7m.ccr[attrs.secure];
        val |= cpu->env.v7m.ccr[M_REG_NS] & R_V7M_CCR_BFHFNMIGN_MASK;
        if (!attrs.secure) {
            if (!(cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK)) {
                val &= ~R_V7M_CCR_BFHFNMIGN_MASK;
            }
        }
        return val;
    case 0xd24:
        if (!arm_feature(&cpu->env, ARM_FEATURE_V7)) {
            goto bad_offset;
        }
        val = 0;
        if (attrs.secure) {
            if (s->sec_vectors[ARMV7M_EXCP_MEM].active) {
                val |= (1 << 0);
            }
            if (s->sec_vectors[ARMV7M_EXCP_HARD].active) {
                val |= (1 << 2);
            }
            if (s->sec_vectors[ARMV7M_EXCP_USAGE].active) {
                val |= (1 << 3);
            }
            if (s->sec_vectors[ARMV7M_EXCP_SVC].active) {
                val |= (1 << 7);
            }
            if (s->sec_vectors[ARMV7M_EXCP_PENDSV].active) {
                val |= (1 << 10);
            }
            if (s->sec_vectors[ARMV7M_EXCP_SYSTICK].active) {
                val |= (1 << 11);
            }
            if (s->sec_vectors[ARMV7M_EXCP_USAGE].pending) {
                val |= (1 << 12);
            }
            if (s->sec_vectors[ARMV7M_EXCP_MEM].pending) {
                val |= (1 << 13);
            }
            if (s->sec_vectors[ARMV7M_EXCP_SVC].pending) {
                val |= (1 << 15);
            }
            if (s->sec_vectors[ARMV7M_EXCP_MEM].enabled) {
                val |= (1 << 16);
            }
            if (s->sec_vectors[ARMV7M_EXCP_USAGE].enabled) {
                val |= (1 << 18);
            }
            if (s->sec_vectors[ARMV7M_EXCP_HARD].pending) {
                val |= (1 << 21);
            }
            if (s->vectors[ARMV7M_EXCP_SECURE].active) {
                val |= (1 << 4);
            }
            if (s->vectors[ARMV7M_EXCP_SECURE].enabled) {
                val |= (1 << 19);
            }
            if (s->vectors[ARMV7M_EXCP_SECURE].pending) {
                val |= (1 << 20);
            }
        } else {
            if (s->vectors[ARMV7M_EXCP_MEM].active) {
                val |= (1 << 0);
            }
            if (arm_feature(&cpu->env, ARM_FEATURE_V8)) {
                if (s->vectors[ARMV7M_EXCP_HARD].active) {
                    val |= (1 << 2);
                }
                if (s->vectors[ARMV7M_EXCP_HARD].pending) {
                    val |= (1 << 21);
                }
            }
            if (s->vectors[ARMV7M_EXCP_USAGE].active) {
                val |= (1 << 3);
            }
            if (s->vectors[ARMV7M_EXCP_SVC].active) {
                val |= (1 << 7);
            }
            if (s->vectors[ARMV7M_EXCP_PENDSV].active) {
                val |= (1 << 10);
            }
            if (s->vectors[ARMV7M_EXCP_SYSTICK].active) {
                val |= (1 << 11);
            }
            if (s->vectors[ARMV7M_EXCP_USAGE].pending) {
                val |= (1 << 12);
            }
            if (s->vectors[ARMV7M_EXCP_MEM].pending) {
                val |= (1 << 13);
            }
            if (s->vectors[ARMV7M_EXCP_SVC].pending) {
                val |= (1 << 15);
            }
            if (s->vectors[ARMV7M_EXCP_MEM].enabled) {
                val |= (1 << 16);
            }
            if (s->vectors[ARMV7M_EXCP_USAGE].enabled) {
                val |= (1 << 18);
            }
        }
        if (attrs.secure || (cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK)) {
            if (s->vectors[ARMV7M_EXCP_BUS].active) {
                val |= (1 << 1);
            }
            if (s->vectors[ARMV7M_EXCP_BUS].pending) {
                val |= (1 << 14);
            }
            if (s->vectors[ARMV7M_EXCP_BUS].enabled) {
                val |= (1 << 17);
            }
            if (arm_feature(&cpu->env, ARM_FEATURE_V8) &&
                s->vectors[ARMV7M_EXCP_NMI].active) {
                val |= (1 << 5);
            }
        }

        if (s->vectors[ARMV7M_EXCP_DEBUG].active) {
            val |= (1 << 8);
        }
        return val;
    case 0xd2c:
        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }
        return cpu->env.v7m.hfsr;
    case 0xd30:
        return cpu->env.v7m.dfsr;
    case 0xd34:
        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }
        return cpu->env.v7m.mmfar[attrs.secure];
    case 0xd38:
        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }
        if (!attrs.secure &&
            !(s->cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK)) {
            return 0;
        }
        return cpu->env.v7m.bfar;
    case 0xd3c:
        qemu_log_mask(LOG_UNIMP,
                      "Aux Fault status registers unimplemented\n");
        return 0;
    case 0xd40:
        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }
        return GET_IDREG(isar, ID_PFR0);
    case 0xd44:
        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }
        return GET_IDREG(isar, ID_PFR1);
    case 0xd48:
        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }
        return GET_IDREG(isar, ID_DFR0);
    case 0xd4c:
        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }
        return GET_IDREG(isar, ID_AFR0);
    case 0xd50:
        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }
        return GET_IDREG(isar, ID_MMFR0);
    case 0xd54:
        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }
        return GET_IDREG(isar, ID_MMFR1);
    case 0xd58:
        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }
        return GET_IDREG(isar, ID_MMFR2);
    case 0xd5c:
        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }
        return GET_IDREG(isar, ID_MMFR3);
    case 0xd60:
        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }
        return GET_IDREG(&cpu->isar, ID_ISAR0);
    case 0xd64:
        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }
        return GET_IDREG(&cpu->isar, ID_ISAR1);
    case 0xd68:
        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }
        return GET_IDREG(&cpu->isar, ID_ISAR2);
    case 0xd6c:
        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }
        return GET_IDREG(&cpu->isar, ID_ISAR3);
    case 0xd70:
        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }
        return GET_IDREG(&cpu->isar, ID_ISAR4);
    case 0xd74:
        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }
        return GET_IDREG(&cpu->isar, ID_ISAR5);
    case 0xd78:
        return GET_IDREG(&cpu->isar, CLIDR);
    case 0xd7c:
        return cpu->ctr;
    case 0xd80:
    {
        int idx = cpu->env.v7m.csselr[attrs.secure] & R_V7M_CSSELR_INDEX_MASK;
        return cpu->ccsidr[idx];
    }
    case 0xd84:
        return cpu->env.v7m.csselr[attrs.secure];
    case 0xd88:
        if (!cpu_isar_feature(aa32_vfp_simd, cpu)) {
            return 0;
        }
        return cpu->env.v7m.cpacr[attrs.secure];
    case 0xd8c:
        if (!attrs.secure || !cpu_isar_feature(aa32_vfp_simd, cpu)) {
            return 0;
        }
        return cpu->env.v7m.nsacr;
    case 0xd90:
        return cpu->pmsav7_dregion << 8;
    case 0xd94:
        return cpu->env.v7m.mpu_ctrl[attrs.secure];
    case 0xd98:
        return cpu->env.pmsav7.rnr[attrs.secure];
    case 0xd9c:
    case 0xda4:
    case 0xdac:
    case 0xdb4:
    {
        int region = cpu->env.pmsav7.rnr[attrs.secure];

        if (arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            int aliasno = (offset - 0xd9c) / 8;
            if (aliasno) {
                region = deposit32(region, 0, 2, aliasno);
            }
            if (region >= cpu->pmsav7_dregion) {
                return 0;
            }
            return cpu->env.pmsav8.rbar[attrs.secure][region];
        }

        if (region >= cpu->pmsav7_dregion) {
            return 0;
        }
        return (cpu->env.pmsav7.drbar[region] & ~0x1f) | (region & 0xf);
    }
    case 0xda0:
    case 0xda8:
    case 0xdb0:
    case 0xdb8:
    {
        int region = cpu->env.pmsav7.rnr[attrs.secure];

        if (arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            int aliasno = (offset - 0xda0) / 8;
            if (aliasno) {
                region = deposit32(region, 0, 2, aliasno);
            }
            if (region >= cpu->pmsav7_dregion) {
                return 0;
            }
            return cpu->env.pmsav8.rlar[attrs.secure][region];
        }

        if (region >= cpu->pmsav7_dregion) {
            return 0;
        }
        return ((cpu->env.pmsav7.dracr[region] & 0xffff) << 16) |
            (cpu->env.pmsav7.drsr[region] & 0xffff);
    }
    case 0xdc0:
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        return cpu->env.pmsav8.mair0[attrs.secure];
    case 0xdc4:
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        return cpu->env.pmsav8.mair1[attrs.secure];
    case 0xdd0:
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return 0;
        }
        return cpu->env.sau.ctrl;
    case 0xdd4:
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return 0;
        }
        return cpu->sau_sregion;
    case 0xdd8:
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return 0;
        }
        return cpu->env.sau.rnr;
    case 0xddc:
    {
        int region = cpu->env.sau.rnr;

        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return 0;
        }
        if (region >= cpu->sau_sregion) {
            return 0;
        }
        return cpu->env.sau.rbar[region];
    }
    case 0xde0:
    {
        int region = cpu->env.sau.rnr;

        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return 0;
        }
        if (region >= cpu->sau_sregion) {
            return 0;
        }
        return cpu->env.sau.rlar[region];
    }
    case 0xde4:
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return 0;
        }
        return cpu->env.v7m.sfsr;
    case 0xde8:
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return 0;
        }
        return cpu->env.v7m.sfar;
    case 0xf04:
        if (!cpu_isar_feature(aa32_ras, cpu)) {
            goto bad_offset;
        }
        return 0;
    case 0xf34:
        if (!cpu_isar_feature(aa32_vfp_simd, cpu)) {
            return 0;
        }
        if (attrs.secure) {
            return cpu->env.v7m.fpccr[M_REG_S];
        } else {
            uint32_t value = cpu->env.v7m.fpccr[M_REG_S];
            uint32_t mask = R_V7M_FPCCR_LSPEN_MASK |
                R_V7M_FPCCR_CLRONRET_MASK |
                R_V7M_FPCCR_MONRDY_MASK;

            if (s->cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK) {
                mask |= R_V7M_FPCCR_BFRDY_MASK | R_V7M_FPCCR_HFRDY_MASK;
            }

            value &= mask;

            value |= cpu->env.v7m.fpccr[M_REG_NS];
            return value;
        }
    case 0xf38:
        if (!cpu_isar_feature(aa32_vfp_simd, cpu)) {
            return 0;
        }
        return cpu->env.v7m.fpcar[attrs.secure];
    case 0xf3c:
        if (!cpu_isar_feature(aa32_vfp_simd, cpu)) {
            return 0;
        }
        return cpu->env.v7m.fpdscr[attrs.secure];
    case 0xf40:
        return cpu->isar.mvfr0;
    case 0xf44:
        return cpu->isar.mvfr1;
    case 0xf48:
        return cpu->isar.mvfr2;
    default:
    bad_offset:
        qemu_log_mask(LOG_GUEST_ERROR, "NVIC: Bad read offset 0x%x\n", offset);
        return 0;
    }
}

static void nvic_writel(NVICState *s, uint32_t offset, uint32_t value,
                        MemTxAttrs attrs)
{
    ARMCPU *cpu = s->cpu;

    switch (offset) {
    case 0xc:
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        break;
    case 0x380 ... 0x3bf:
    {
        int startvec = 8 * (offset - 0x380) + NVIC_FIRST_IRQ;
        int i;

        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            break;
        }
        for (i = 0; i < 32 && startvec + i < s->num_irq; i++) {
            s->itns[startvec + i] = (value >> i) & 1;
        }
        nvic_irq_update(s);
        break;
    }
    case 0xd04:
        if (attrs.secure || cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK) {
            if (value & (1 << 31)) {
                armv7m_nvic_set_pending(s, ARMV7M_EXCP_NMI, false);
            } else if (value & (1 << 30) &&
                       arm_feature(&cpu->env, ARM_FEATURE_V8)) {
                armv7m_nvic_clear_pending(s, ARMV7M_EXCP_NMI, false);
            }
        }
        if (value & (1 << 28)) {
            armv7m_nvic_set_pending(s, ARMV7M_EXCP_PENDSV, attrs.secure);
        } else if (value & (1 << 27)) {
            armv7m_nvic_clear_pending(s, ARMV7M_EXCP_PENDSV, attrs.secure);
        }
        if (value & (1 << 26)) {
            armv7m_nvic_set_pending(s, ARMV7M_EXCP_SYSTICK, attrs.secure);
        } else if (value & (1 << 25)) {
            armv7m_nvic_clear_pending(s, ARMV7M_EXCP_SYSTICK, attrs.secure);
        }
        break;
    case 0xd08:
        cpu->env.v7m.vecbase[attrs.secure] = value & 0xffffff80;
        break;
    case 0xd0c:
        if ((value >> R_V7M_AIRCR_VECTKEY_SHIFT) == 0x05fa) {
            if (value & R_V7M_AIRCR_SYSRESETREQ_MASK) {
                if (attrs.secure ||
                    !(cpu->env.v7m.aircr & R_V7M_AIRCR_SYSRESETREQS_MASK)) {
                    signal_sysresetreq(s);
                }
            }
            if (value & R_V7M_AIRCR_VECTCLRACTIVE_MASK) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "Setting VECTCLRACTIVE when not in DEBUG mode "
                              "is UNPREDICTABLE\n");
            }
            if (value & R_V7M_AIRCR_VECTRESET_MASK) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "Setting VECTRESET when not in DEBUG mode "
                              "is UNPREDICTABLE\n");
            }
            if (arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
                s->prigroup[attrs.secure] =
                    extract32(value,
                              R_V7M_AIRCR_PRIGROUP_SHIFT,
                              R_V7M_AIRCR_PRIGROUP_LENGTH);
            }
            if (attrs.secure) {
                cpu->env.v7m.aircr = value &
                    (R_V7M_AIRCR_SYSRESETREQS_MASK |
                     R_V7M_AIRCR_BFHFNMINS_MASK |
                     R_V7M_AIRCR_PRIS_MASK);
                if (cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK) {
                    s->sec_vectors[ARMV7M_EXCP_HARD].prio = -3;
                    s->vectors[ARMV7M_EXCP_HARD].enabled = 1;
                } else {
                    s->sec_vectors[ARMV7M_EXCP_HARD].prio = -1;
                    s->vectors[ARMV7M_EXCP_HARD].enabled = 0;
                }
            }
            nvic_irq_update(s);
        }
        break;
    case 0xd10:
        if (!arm_feature(&cpu->env, ARM_FEATURE_V7)) {
            goto bad_offset;
        }
        value &= ~(R_V7M_SCR_SLEEPDEEP_MASK | R_V7M_SCR_SLEEPDEEPS_MASK);
        cpu->env.v7m.scr[attrs.secure] = value;
        break;
    case 0xd14:
    {
        uint32_t mask;

        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }

        mask = R_V7M_CCR_STKALIGN_MASK |
            R_V7M_CCR_BFHFNMIGN_MASK |
            R_V7M_CCR_DIV_0_TRP_MASK |
            R_V7M_CCR_UNALIGN_TRP_MASK |
            R_V7M_CCR_USERSETMPEND_MASK |
            R_V7M_CCR_NONBASETHRDENA_MASK;
        if (arm_feature(&cpu->env, ARM_FEATURE_V8_1M) && attrs.secure) {
            mask |= R_V7M_CCR_TRD_MASK;
        }
        value &= mask;

        if (arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            value |= R_V7M_CCR_NONBASETHRDENA_MASK
                | R_V7M_CCR_STKALIGN_MASK;
        }
        if (attrs.secure) {
            cpu->env.v7m.ccr[M_REG_NS] =
                (cpu->env.v7m.ccr[M_REG_NS] & ~R_V7M_CCR_BFHFNMIGN_MASK)
                | (value & R_V7M_CCR_BFHFNMIGN_MASK);
            value &= ~R_V7M_CCR_BFHFNMIGN_MASK;
        } else {
            if (!(cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK)) {
                value &= ~R_V7M_CCR_BFHFNMIGN_MASK;
                value |= cpu->env.v7m.ccr[M_REG_NS] & R_V7M_CCR_BFHFNMIGN_MASK;
            }
        }

        cpu->env.v7m.ccr[attrs.secure] = value;
        break;
    }
    case 0xd24:
        if (!arm_feature(&cpu->env, ARM_FEATURE_V7)) {
            goto bad_offset;
        }
        if (attrs.secure) {
            s->sec_vectors[ARMV7M_EXCP_MEM].active = (value & (1 << 0)) != 0;
            s->sec_vectors[ARMV7M_EXCP_USAGE].active = (value & (1 << 3)) != 0;
            s->sec_vectors[ARMV7M_EXCP_SVC].active = (value & (1 << 7)) != 0;
            s->sec_vectors[ARMV7M_EXCP_PENDSV].active =
                (value & (1 << 10)) != 0;
            s->sec_vectors[ARMV7M_EXCP_SYSTICK].active =
                (value & (1 << 11)) != 0;
            s->sec_vectors[ARMV7M_EXCP_USAGE].pending =
                (value & (1 << 12)) != 0;
            s->sec_vectors[ARMV7M_EXCP_MEM].pending = (value & (1 << 13)) != 0;
            s->sec_vectors[ARMV7M_EXCP_SVC].pending = (value & (1 << 15)) != 0;
            s->sec_vectors[ARMV7M_EXCP_MEM].enabled = (value & (1 << 16)) != 0;
            s->sec_vectors[ARMV7M_EXCP_BUS].enabled = (value & (1 << 17)) != 0;
            s->sec_vectors[ARMV7M_EXCP_USAGE].enabled =
                (value & (1 << 18)) != 0;
            s->sec_vectors[ARMV7M_EXCP_HARD].pending = (value & (1 << 21)) != 0;
            s->vectors[ARMV7M_EXCP_SECURE].active = (value & (1 << 4)) != 0;
            s->vectors[ARMV7M_EXCP_SECURE].enabled = (value & (1 << 19)) != 0;
            s->vectors[ARMV7M_EXCP_SECURE].pending = (value & (1 << 20)) != 0;
        } else {
            s->vectors[ARMV7M_EXCP_MEM].active = (value & (1 << 0)) != 0;
            if (arm_feature(&cpu->env, ARM_FEATURE_V8)) {
                s->vectors[ARMV7M_EXCP_HARD].pending = (value & (1 << 21)) != 0;
            }
            s->vectors[ARMV7M_EXCP_USAGE].active = (value & (1 << 3)) != 0;
            s->vectors[ARMV7M_EXCP_SVC].active = (value & (1 << 7)) != 0;
            s->vectors[ARMV7M_EXCP_PENDSV].active = (value & (1 << 10)) != 0;
            s->vectors[ARMV7M_EXCP_SYSTICK].active = (value & (1 << 11)) != 0;
            s->vectors[ARMV7M_EXCP_USAGE].pending = (value & (1 << 12)) != 0;
            s->vectors[ARMV7M_EXCP_MEM].pending = (value & (1 << 13)) != 0;
            s->vectors[ARMV7M_EXCP_SVC].pending = (value & (1 << 15)) != 0;
            s->vectors[ARMV7M_EXCP_MEM].enabled = (value & (1 << 16)) != 0;
            s->vectors[ARMV7M_EXCP_USAGE].enabled = (value & (1 << 18)) != 0;
        }
        if (attrs.secure || (cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK)) {
            s->vectors[ARMV7M_EXCP_BUS].active = (value & (1 << 1)) != 0;
            s->vectors[ARMV7M_EXCP_BUS].pending = (value & (1 << 14)) != 0;
            s->vectors[ARMV7M_EXCP_BUS].enabled = (value & (1 << 17)) != 0;
        }
        if (!attrs.secure && cpu->env.v7m.secure &&
            (cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK) &&
            (value & (1 << 5)) == 0) {
            s->vectors[ARMV7M_EXCP_NMI].active = 0;
        }
        if (!attrs.secure && cpu->env.v7m.secure &&
            (cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK) &&
            (value & (1 << 2)) == 0) {
            s->vectors[ARMV7M_EXCP_HARD].active = 0;
        }

        s->vectors[ARMV7M_EXCP_DEBUG].active = (value & (1 << 8)) != 0;
        nvic_irq_update(s);
        break;
    case 0xd2c:
        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }
        cpu->env.v7m.hfsr &= ~value;
        break;
    case 0xd30:
        cpu->env.v7m.dfsr &= ~value;
        break;
    case 0xd34:
        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }
        cpu->env.v7m.mmfar[attrs.secure] = value;
        return;
    case 0xd38:
        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }
        if (!attrs.secure &&
            !(s->cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK)) {
            return;
        }
        cpu->env.v7m.bfar = value;
        return;
    case 0xd3c:
        qemu_log_mask(LOG_UNIMP,
                      "NVIC: Aux fault status registers unimplemented\n");
        break;
    case 0xd84:
        if (!arm_v7m_csselr_razwi(cpu)) {
            cpu->env.v7m.csselr[attrs.secure] = value & R_V7M_CSSELR_INDEX_MASK;
        }
        break;
    case 0xd88:
        if (cpu_isar_feature(aa32_vfp_simd, cpu)) {
            cpu->env.v7m.cpacr[attrs.secure] = value & (0xf << 20);
        }
        break;
    case 0xd8c:
        if (attrs.secure && cpu_isar_feature(aa32_vfp_simd, cpu)) {
            cpu->env.v7m.nsacr = value & (3 << 10);
        }
        break;
    case 0xd90:
        return;
    case 0xd94:
        if ((value &
             (R_V7M_MPU_CTRL_HFNMIENA_MASK | R_V7M_MPU_CTRL_ENABLE_MASK))
            == R_V7M_MPU_CTRL_HFNMIENA_MASK) {
            qemu_log_mask(LOG_GUEST_ERROR, "MPU_CTRL: HFNMIENA and !ENABLE is "
                          "UNPREDICTABLE\n");
        }
        cpu->env.v7m.mpu_ctrl[attrs.secure]
            = value & (R_V7M_MPU_CTRL_ENABLE_MASK |
                       R_V7M_MPU_CTRL_HFNMIENA_MASK |
                       R_V7M_MPU_CTRL_PRIVDEFENA_MASK);
        tlb_flush(CPU(cpu));
        break;
    case 0xd98:
        if (value >= cpu->pmsav7_dregion) {
            qemu_log_mask(LOG_GUEST_ERROR, "MPU region out of range %"
                          PRIu32 "/%" PRIu32 "\n",
                          value, cpu->pmsav7_dregion);
        } else {
            cpu->env.pmsav7.rnr[attrs.secure] = value;
        }
        break;
    case 0xd9c:
    case 0xda4:
    case 0xdac:
    case 0xdb4:
    {
        int region;

        if (arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            int aliasno = (offset - 0xd9c) / 8;

            region = cpu->env.pmsav7.rnr[attrs.secure];
            if (aliasno) {
                region = deposit32(region, 0, 2, aliasno);
            }
            if (region >= cpu->pmsav7_dregion) {
                return;
            }
            cpu->env.pmsav8.rbar[attrs.secure][region] = value;
            tlb_flush(CPU(cpu));
            return;
        }

        if (value & (1 << 4)) {
            region = extract32(value, 0, 4);
            if (region >= cpu->pmsav7_dregion) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "MPU region out of range %u/%" PRIu32 "\n",
                              region, cpu->pmsav7_dregion);
                return;
            }
            cpu->env.pmsav7.rnr[attrs.secure] = region;
        } else {
            region = cpu->env.pmsav7.rnr[attrs.secure];
        }

        if (region >= cpu->pmsav7_dregion) {
            return;
        }

        cpu->env.pmsav7.drbar[region] = value & ~0x1f;
        tlb_flush(CPU(cpu));
        break;
    }
    case 0xda0:
    case 0xda8:
    case 0xdb0:
    case 0xdb8:
    {
        int region = cpu->env.pmsav7.rnr[attrs.secure];

        if (arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            int aliasno = (offset - 0xd9c) / 8;

            region = cpu->env.pmsav7.rnr[attrs.secure];
            if (aliasno) {
                region = deposit32(region, 0, 2, aliasno);
            }
            if (region >= cpu->pmsav7_dregion) {
                return;
            }
            cpu->env.pmsav8.rlar[attrs.secure][region] = value;
            tlb_flush(CPU(cpu));
            return;
        }

        if (region >= cpu->pmsav7_dregion) {
            return;
        }

        cpu->env.pmsav7.drsr[region] = value & 0xff3f;
        cpu->env.pmsav7.dracr[region] = (value >> 16) & 0x173f;
        tlb_flush(CPU(cpu));
        break;
    }
    case 0xdc0:
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (cpu->pmsav7_dregion) {
            cpu->env.pmsav8.mair0[attrs.secure] = value;
        }
        break;
    case 0xdc4:
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (cpu->pmsav7_dregion) {
            cpu->env.pmsav8.mair1[attrs.secure] = value;
        }
        break;
    case 0xdd0:
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return;
        }
        cpu->env.sau.ctrl = value & 3;
        break;
    case 0xdd4:
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        break;
    case 0xdd8:
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return;
        }
        if (value >= cpu->sau_sregion) {
            qemu_log_mask(LOG_GUEST_ERROR, "SAU region out of range %"
                          PRIu32 "/%" PRIu32 "\n",
                          value, cpu->sau_sregion);
        } else {
            cpu->env.sau.rnr = value;
        }
        break;
    case 0xddc:
    {
        int region = cpu->env.sau.rnr;

        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return;
        }
        if (region >= cpu->sau_sregion) {
            return;
        }
        cpu->env.sau.rbar[region] = value & ~0x1f;
        tlb_flush(CPU(cpu));
        break;
    }
    case 0xde0:
    {
        int region = cpu->env.sau.rnr;

        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return;
        }
        if (region >= cpu->sau_sregion) {
            return;
        }
        cpu->env.sau.rlar[region] = value & ~0x1c;
        tlb_flush(CPU(cpu));
        break;
    }
    case 0xde4:
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return;
        }
        cpu->env.v7m.sfsr &= ~value;
        break;
    case 0xde8:
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return;
        }
        cpu->env.v7m.sfsr = value;
        break;
    case 0xf00:
    {
        int excnum = (value & 0x1ff) + NVIC_FIRST_IRQ;

        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }

        if (excnum < s->num_irq) {
            armv7m_nvic_set_pending(s, excnum, false);
        }
        break;
    }
    case 0xf04:
        if (!cpu_isar_feature(aa32_ras, cpu)) {
            goto bad_offset;
        }
        break;
    case 0xf34:
        if (cpu_isar_feature(aa32_vfp_simd, cpu)) {
            uint32_t fpccr_s;

            if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
                value &= (R_V7M_FPCCR_LSPACT_MASK |
                          R_V7M_FPCCR_USER_MASK |
                          R_V7M_FPCCR_THREAD_MASK |
                          R_V7M_FPCCR_HFRDY_MASK |
                          R_V7M_FPCCR_MMRDY_MASK |
                          R_V7M_FPCCR_BFRDY_MASK |
                          R_V7M_FPCCR_MONRDY_MASK |
                          R_V7M_FPCCR_LSPEN_MASK |
                          R_V7M_FPCCR_ASPEN_MASK);
            }
            value &= ~R_V7M_FPCCR_RES0_MASK;

            if (!attrs.secure) {
                fpccr_s = cpu->env.v7m.fpccr[M_REG_S];
                if (!(fpccr_s & R_V7M_FPCCR_LSPENS_MASK)) {
                    uint32_t lspen = FIELD_EX32(value, V7M_FPCCR, LSPEN);
                    fpccr_s = FIELD_DP32(fpccr_s, V7M_FPCCR, LSPEN, lspen);
                }
                if (!(fpccr_s & R_V7M_FPCCR_CLRONRETS_MASK)) {
                    uint32_t cor = FIELD_EX32(value, V7M_FPCCR, CLRONRET);
                    fpccr_s = FIELD_DP32(fpccr_s, V7M_FPCCR, CLRONRET, cor);
                }
                if ((s->cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK)) {
                    uint32_t hfrdy = FIELD_EX32(value, V7M_FPCCR, HFRDY);
                    uint32_t bfrdy = FIELD_EX32(value, V7M_FPCCR, BFRDY);
                    fpccr_s = FIELD_DP32(fpccr_s, V7M_FPCCR, HFRDY, hfrdy);
                    fpccr_s = FIELD_DP32(fpccr_s, V7M_FPCCR, BFRDY, bfrdy);
                }
                {
                    uint32_t monrdy = FIELD_EX32(value, V7M_FPCCR, MONRDY);
                    fpccr_s = FIELD_DP32(fpccr_s, V7M_FPCCR, MONRDY, monrdy);
                }

                value &= R_V7M_FPCCR_BANKED_MASK;
                cpu->env.v7m.fpccr[M_REG_NS] = value;
            } else {
                fpccr_s = value;
            }
            cpu->env.v7m.fpccr[M_REG_S] = fpccr_s;
        }
        break;
    case 0xf38:
        if (cpu_isar_feature(aa32_vfp_simd, cpu)) {
            value &= ~7;
            cpu->env.v7m.fpcar[attrs.secure] = value;
        }
        break;
    case 0xf3c:
        if (cpu_isar_feature(aa32_vfp_simd, cpu)) {
            uint32_t mask = FPCR_AHP | FPCR_DN | FPCR_FZ | FPCR_RMODE_MASK;
            if (cpu_isar_feature(any_fp16, cpu)) {
                mask |= FPCR_FZ16;
            }
            value &= mask;
            if (cpu_isar_feature(aa32_lob, cpu)) {
                value |= 4 << FPCR_LTPSIZE_SHIFT;
            }
            cpu->env.v7m.fpdscr[attrs.secure] = value;
        }
        break;
    case 0xf50:
    case 0xf58:
    case 0xf5c:
    case 0xf60:
    case 0xf64:
    case 0xf68:
    case 0xf6c:
    case 0xf70:
    case 0xf74:
    case 0xf78:
        break;
    default:
    bad_offset:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "NVIC: Bad write offset 0x%x\n", offset);
    }
}

static bool nvic_user_access_ok(NVICState *s, hwaddr offset, MemTxAttrs attrs)
{
    switch (offset) {
    case 0xf00:
        return s->cpu->env.v7m.ccr[attrs.secure] & R_V7M_CCR_USERSETMPEND_MASK;
    default:
        return false;
    }
}

static int shpr_bank(NVICState *s, int exc, MemTxAttrs attrs)
{
    switch (exc) {
    case ARMV7M_EXCP_MEM:
    case ARMV7M_EXCP_USAGE:
    case ARMV7M_EXCP_SVC:
    case ARMV7M_EXCP_PENDSV:
    case ARMV7M_EXCP_SYSTICK:
        return attrs.secure;
    case ARMV7M_EXCP_BUS:
        if (!attrs.secure &&
            !(s->cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK)) {
            return -1;
        }
        return M_REG_NS;
    case ARMV7M_EXCP_SECURE:
        if (!attrs.secure) {
            return -1;
        }
        return M_REG_NS;
    case ARMV7M_EXCP_DEBUG:
        return M_REG_NS;
    case 8 ... 10:
    case 13:
        return -1;
    default:
        g_assert_not_reached();
    }
}

static MemTxResult nvic_sysreg_read(void *opaque, hwaddr addr,
                                    uint64_t *data, unsigned size,
                                    MemTxAttrs attrs)
{
    NVICState *s = (NVICState *)opaque;
    uint32_t offset = addr;
    unsigned i, startvec, end;
    uint32_t val;

    if (attrs.user && !nvic_user_access_ok(s, addr, attrs)) {
        return MEMTX_ERROR;
    }

    switch (offset) {
    case 0x100 ... 0x13f:
        offset += 0x80;
        /* fall through */
    case 0x180 ... 0x1bf:
        val = 0;
        startvec = 8 * (offset - 0x180) + NVIC_FIRST_IRQ;

        for (i = 0, end = size * 8; i < end && startvec + i < s->num_irq; i++) {
            if (s->vectors[startvec + i].enabled &&
                (attrs.secure || s->itns[startvec + i])) {
                val |= (1 << i);
            }
        }
        break;
    case 0x200 ... 0x23f:
        offset += 0x80;
        /* fall through */
    case 0x280 ... 0x2bf:
        val = 0;
        startvec = 8 * (offset - 0x280) + NVIC_FIRST_IRQ;
        for (i = 0, end = size * 8; i < end && startvec + i < s->num_irq; i++) {
            if (s->vectors[startvec + i].pending &&
                (attrs.secure || s->itns[startvec + i])) {
                val |= (1 << i);
            }
        }
        break;
    case 0x300 ... 0x33f:
        val = 0;

        if (!arm_feature(&s->cpu->env, ARM_FEATURE_V7)) {
            break;
        }

        startvec = 8 * (offset - 0x300) + NVIC_FIRST_IRQ;

        for (i = 0, end = size * 8; i < end && startvec + i < s->num_irq; i++) {
            if (s->vectors[startvec + i].active &&
                (attrs.secure || s->itns[startvec + i])) {
                val |= (1 << i);
            }
        }
        break;
    case 0x400 ... 0x5ef:
        val = 0;
        startvec = offset - 0x400 + NVIC_FIRST_IRQ;

        for (i = 0; i < size && startvec + i < s->num_irq; i++) {
            if (attrs.secure || s->itns[startvec + i]) {
                val |= s->vectors[startvec + i].prio << (8 * i);
            }
        }
        break;
    case 0xd18 ... 0xd1b:
        if (!arm_feature(&s->cpu->env, ARM_FEATURE_M_MAIN)) {
            val = 0;
            break;
        }
        /* fall through */
    case 0xd1c ... 0xd23:
        val = 0;
        for (i = 0; i < size; i++) {
            unsigned hdlidx = (offset - 0xd14) + i;
            int sbank = shpr_bank(s, hdlidx, attrs);

            if (sbank < 0) {
                continue;
            }
            val = deposit32(val, i * 8, 8, get_prio(s, hdlidx, sbank));
        }
        break;
    case 0xd28 ... 0xd2b:
        if (!arm_feature(&s->cpu->env, ARM_FEATURE_M_MAIN)) {
            val = 0;
            break;
        };
        val = s->cpu->env.v7m.cfsr[attrs.secure];
        if (!attrs.secure &&
            !(s->cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK)) {
            val &= ~R_V7M_CFSR_BFSR_MASK;
        } else {
            val |= s->cpu->env.v7m.cfsr[M_REG_NS] & R_V7M_CFSR_BFSR_MASK;
        }
        val = extract32(val, (offset - 0xd28) * 8, size * 8);
        break;
    case 0xfe0 ... 0xfff:
        if (offset & 3) {
            val = 0;
        } else {
            val = nvic_id[(offset - 0xfe0) >> 2];
        }
        break;
    default:
        if (size == 4) {
            val = nvic_readl(s, offset, attrs);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "NVIC: Bad read of size %d at offset 0x%x\n",
                          size, offset);
            val = 0;
        }
    }

    trace_nvic_sysreg_read(addr, val, size);
    *data = val;
    return MEMTX_OK;
}

static MemTxResult nvic_sysreg_write(void *opaque, hwaddr addr,
                                     uint64_t value, unsigned size,
                                     MemTxAttrs attrs)
{
    NVICState *s = (NVICState *)opaque;
    uint32_t offset = addr;
    unsigned i, startvec, end;
    unsigned setval = 0;

    trace_nvic_sysreg_write(addr, value, size);

    if (attrs.user && !nvic_user_access_ok(s, addr, attrs)) {
        return MEMTX_ERROR;
    }

    switch (offset) {
    case 0x100 ... 0x13f:
        offset += 0x80;
        setval = 1;
        /* fall through */
    case 0x180 ... 0x1bf:
        startvec = 8 * (offset - 0x180) + NVIC_FIRST_IRQ;

        for (i = 0, end = size * 8; i < end && startvec + i < s->num_irq; i++) {
            if (value & (1 << i) &&
                (attrs.secure || s->itns[startvec + i])) {
                s->vectors[startvec + i].enabled = setval;
            }
        }
        nvic_irq_update(s);
        goto exit_ok;
    case 0x200 ... 0x23f:
        offset += 0x80;
        setval = 1;
        /* fall through */
    case 0x280 ... 0x2bf:
        startvec = 8 * (offset - 0x280) + NVIC_FIRST_IRQ;

        for (i = 0, end = size * 8; i < end && startvec + i < s->num_irq; i++) {
            if (value & (1 << i) &&
                (attrs.secure || s->itns[startvec + i]) &&
                !(setval == 0 && s->vectors[startvec + i].level &&
                  !s->vectors[startvec + i].active)) {
                s->vectors[startvec + i].pending = setval;
            }
        }
        nvic_irq_update(s);
        goto exit_ok;
    case 0x300 ... 0x33f:
        goto exit_ok;
    case 0x400 ... 0x5ef:
        startvec = (offset - 0x400) + NVIC_FIRST_IRQ;

        for (i = 0; i < size && startvec + i < s->num_irq; i++) {
            if (attrs.secure || s->itns[startvec + i]) {
                set_prio(s, startvec + i, false, (value >> (i * 8)) & 0xff);
            }
        }
        nvic_irq_update(s);
        goto exit_ok;
    case 0xd18 ... 0xd1b:
        if (!arm_feature(&s->cpu->env, ARM_FEATURE_M_MAIN)) {
            goto exit_ok;
        }
        /* fall through */
    case 0xd1c ... 0xd23:
        for (i = 0; i < size; i++) {
            unsigned hdlidx = (offset - 0xd14) + i;
            int newprio = extract32(value, i * 8, 8);
            int sbank = shpr_bank(s, hdlidx, attrs);

            if (sbank < 0) {
                continue;
            }
            set_prio(s, hdlidx, sbank, newprio);
        }
        nvic_irq_update(s);
        goto exit_ok;
    case 0xd28 ... 0xd2b:
        if (!arm_feature(&s->cpu->env, ARM_FEATURE_M_MAIN)) {
            goto exit_ok;
        }
        value <<= ((offset - 0xd28) * 8);

        if (!attrs.secure &&
            !(s->cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK)) {
            value &= ~R_V7M_CFSR_BFSR_MASK;
        }

        s->cpu->env.v7m.cfsr[attrs.secure] &= ~value;
        if (attrs.secure) {
            s->cpu->env.v7m.cfsr[M_REG_NS] &= ~(value & R_V7M_CFSR_BFSR_MASK);
        }
        goto exit_ok;
    }
    if (size == 4) {
        nvic_writel(s, offset, value, attrs);
        goto exit_ok;
    }
    qemu_log_mask(LOG_GUEST_ERROR,
                  "NVIC: Bad write of size %d at offset 0x%x\n", size, offset);

 exit_ok:
    if (tcg_enabled()) {
        arm_rebuild_hflags(&s->cpu->env);
    }
    return MEMTX_OK;
}

static const MemoryRegionOps nvic_sysreg_ops = {
    .read_with_attrs = nvic_sysreg_read,
    .write_with_attrs = nvic_sysreg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int nvic_post_load(void *opaque, int version_id)
{
    NVICState *s = opaque;
    unsigned i;
    int resetprio;

    resetprio = arm_feature(&s->cpu->env, ARM_FEATURE_V8) ? -4 : -3;

    if (s->vectors[ARMV7M_EXCP_RESET].prio != resetprio ||
        s->vectors[ARMV7M_EXCP_NMI].prio != -2 ||
        s->vectors[ARMV7M_EXCP_HARD].prio != -1) {
        return 1;
    }
    for (i = ARMV7M_EXCP_MEM; i < s->num_irq; i++) {
        if (s->vectors[i].prio & ~0xff) {
            return 1;
        }
    }

    nvic_recompute_state(s);

    return 0;
}

static const VMStateDescription vmstate_VecInfo = {
    .name = "armv7m_nvic_info",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_INT16(prio, VecInfo),
        VMSTATE_UINT8(enabled, VecInfo),
        VMSTATE_UINT8(pending, VecInfo),
        VMSTATE_UINT8(active, VecInfo),
        VMSTATE_UINT8(level, VecInfo),
        VMSTATE_END_OF_LIST()
    }
};

static bool nvic_security_needed(void *opaque)
{
    NVICState *s = opaque;

    return arm_feature(&s->cpu->env, ARM_FEATURE_M_SECURITY);
}

static int nvic_security_post_load(void *opaque, int version_id)
{
    NVICState *s = opaque;
    int i;

    if (s->sec_vectors[ARMV7M_EXCP_HARD].prio != -1
        && s->sec_vectors[ARMV7M_EXCP_HARD].prio != -3) {
        return 1;
    }
    for (i = ARMV7M_EXCP_MEM; i < ARRAY_SIZE(s->sec_vectors); i++) {
        if (s->sec_vectors[i].prio & ~0xff) {
            return 1;
        }
    }
    return 0;
}

static const VMStateDescription vmstate_nvic_security = {
    .name = "armv7m_nvic/m-security",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = nvic_security_needed,
    .post_load = &nvic_security_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(sec_vectors, NVICState, NVIC_INTERNAL_VECTORS, 1,
                             vmstate_VecInfo, VecInfo),
        VMSTATE_UINT32(prigroup[M_REG_S], NVICState),
        VMSTATE_BOOL_ARRAY(itns, NVICState, NVIC_MAX_VECTORS),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_nvic = {
    .name = "armv7m_nvic",
    .version_id = 4,
    .minimum_version_id = 4,
    .post_load = &nvic_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(vectors, NVICState, NVIC_MAX_VECTORS, 1,
                             vmstate_VecInfo, VecInfo),
        VMSTATE_UINT32(prigroup[M_REG_NS], NVICState),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_nvic_security,
        NULL
    }
};

static const Property props_nvic[] = {
    DEFINE_PROP_UINT32("num-irq", NVICState, num_irq, 64),
    DEFINE_PROP_UINT8("num-prio-bits", NVICState, num_prio_bits, 0),
};

static void armv7m_nvic_reset(DeviceState *dev)
{
    int resetprio;
    NVICState *s = NVIC(dev);

    memset(s->vectors, 0, sizeof(s->vectors));
    memset(s->sec_vectors, 0, sizeof(s->sec_vectors));
    s->prigroup[M_REG_NS] = 0;
    s->prigroup[M_REG_S] = 0;

    s->vectors[ARMV7M_EXCP_NMI].enabled = 1;
    s->vectors[ARMV7M_EXCP_SVC].enabled = 1;
    s->vectors[ARMV7M_EXCP_PENDSV].enabled = 1;
    s->vectors[ARMV7M_EXCP_SYSTICK].enabled = 1;

    s->vectors[ARMV7M_EXCP_DEBUG].enabled = 0;

    resetprio = arm_feature(&s->cpu->env, ARM_FEATURE_V8) ? -4 : -3;
    s->vectors[ARMV7M_EXCP_RESET].prio = resetprio;
    s->vectors[ARMV7M_EXCP_NMI].prio = -2;
    s->vectors[ARMV7M_EXCP_HARD].prio = -1;

    if (arm_feature(&s->cpu->env, ARM_FEATURE_M_SECURITY)) {
        s->sec_vectors[ARMV7M_EXCP_HARD].enabled = 1;
        s->sec_vectors[ARMV7M_EXCP_SVC].enabled = 1;
        s->sec_vectors[ARMV7M_EXCP_PENDSV].enabled = 1;
        s->sec_vectors[ARMV7M_EXCP_SYSTICK].enabled = 1;

        s->sec_vectors[ARMV7M_EXCP_HARD].prio = -1;
        s->vectors[ARMV7M_EXCP_HARD].enabled = 0;
    } else {
        s->vectors[ARMV7M_EXCP_HARD].enabled = 1;
    }

    s->exception_prio = NVIC_NOEXC_PRIO;
    s->vectpending = 0;
    s->vectpending_is_s_banked = false;
    s->vectpending_prio = NVIC_NOEXC_PRIO;

    if (arm_feature(&s->cpu->env, ARM_FEATURE_M_SECURITY)) {
        memset(s->itns, 0, sizeof(s->itns));
    } else {
        int i;

        for (i = NVIC_FIRST_IRQ; i < ARRAY_SIZE(s->itns); i++) {
            s->itns[i] = true;
        }
    }

    if (tcg_enabled()) {
        arm_rebuild_hflags(&s->cpu->env);
    }
}

static void nvic_systick_trigger(void *opaque, int n, int level)
{
    NVICState *s = opaque;

    if (level) {
        armv7m_nvic_set_pending(s, ARMV7M_EXCP_SYSTICK, n);
    }
}

static void armv7m_nvic_realize(DeviceState *dev, Error **errp)
{
    NVICState *s = NVIC(dev);

    if (!s->cpu || !arm_feature(&s->cpu->env, ARM_FEATURE_M)) {
        error_setg(errp, "The NVIC can only be used with a Cortex-M CPU");
        return;
    }

    if (s->num_irq > NVIC_MAX_IRQ) {
        error_setg(errp, "num-irq %d exceeds NVIC maximum", s->num_irq);
        return;
    }

    qdev_init_gpio_in(dev, set_irq_level, s->num_irq);

    s->num_irq += NVIC_FIRST_IRQ;

    if (s->num_prio_bits == 0) {
        s->num_prio_bits = arm_feature(&s->cpu->env, ARM_FEATURE_V7) ? 8 : 2;
    } else {
        uint8_t min_prio_bits =
            arm_feature(&s->cpu->env, ARM_FEATURE_V7) ? 3 : 2;
        if (s->num_prio_bits < min_prio_bits || s->num_prio_bits > 8) {
            error_setg(errp,
                       "num-prio-bits %d is outside "
                       "NVIC acceptable range [%d-8]",
                       s->num_prio_bits, min_prio_bits);
            return;
        }
    }

    memory_region_init_io(&s->sysregmem, OBJECT(s), &nvic_sysreg_ops, s,
                          "nvic_sysregs", 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->sysregmem);
}

static void armv7m_nvic_instance_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    NVICState *nvic = NVIC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    sysbus_init_irq(sbd, &nvic->excpout);
    qdev_init_gpio_out_named(dev, &nvic->sysresetreq, "SYSRESETREQ", 1);
    qdev_init_gpio_in_named(dev, nvic_systick_trigger, "systick-trigger",
                            M_REG_NUM_BANKS);
    qdev_init_gpio_in_named(dev, nvic_nmi_trigger, "NMI", 1);
}

static void armv7m_nvic_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd  = &vmstate_nvic;
    device_class_set_props(dc, props_nvic);
    device_class_set_legacy_reset(dc, armv7m_nvic_reset);
    dc->realize = armv7m_nvic_realize;
}

static const TypeInfo armv7m_nvic_info = {
    .name          = TYPE_NVIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = armv7m_nvic_instance_init,
    .instance_size = sizeof(NVICState),
    .class_init    = armv7m_nvic_class_init,
    .class_size    = sizeof(SysBusDeviceClass),
};

static void armv7m_nvic_register_types(void)
{
    type_register_static(&armv7m_nvic_info);
}

type_init(armv7m_nvic_register_types)
