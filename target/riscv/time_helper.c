/*
 * RISC-V timer helper implementation.
 *
 * Copyright (c) 2022 Rivos Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu_bits.h"
#include "time_helper.h"
#include "hw/intc/riscv_aclint.h"

static void riscv_vstimer_cb(void *opaque)
{
    RISCVCPU *cpu = opaque;
    CPURISCVState *env = &cpu->env;
    env->vstime_irq = 1;
    riscv_cpu_update_mip(env, 0, BOOL_TO_MASK(1));
}

static void riscv_stimer_cb(void *opaque)
{
    RISCVCPU *cpu = opaque;
    riscv_cpu_update_mip(&cpu->env, MIP_STIP, BOOL_TO_MASK(1));
}

void riscv_timer_write_timecmp(CPURISCVState *env, QEMUTimer *timer,
                               uint64_t timecmp, uint64_t delta,
                               uint32_t timer_irq)
{
    uint64_t diff, ns_diff, next;
    RISCVAclintMTimerState *mtimer = env->rdtime_fn_arg;
    uint32_t timebase_freq;
    uint64_t rtc_r;

    if (!riscv_cpu_cfg(env)->ext_sstc || !env->rdtime_fn ||
        !env->rdtime_fn_arg || !get_field(env->menvcfg, MENVCFG_STCE)) {
        return;
    }

    if (timer_irq == MIP_VSTIP &&
        (!riscv_has_ext(env, RVH) || !get_field(env->henvcfg, HENVCFG_STCE))) {
        return;
    }

    timebase_freq = mtimer->timebase_freq;
    rtc_r = env->rdtime_fn(env->rdtime_fn_arg) + delta;

    if (timecmp <= rtc_r) {
        if (timer_irq == MIP_VSTIP) {
            env->vstime_irq = 1;
            riscv_cpu_update_mip(env, 0, BOOL_TO_MASK(1));
        } else {
            riscv_cpu_update_mip(env, MIP_STIP, BOOL_TO_MASK(1));
        }
        return;
    }

    if (timer_irq == MIP_VSTIP) {
        env->vstime_irq = 0;
        riscv_cpu_update_mip(env, 0, BOOL_TO_MASK(0));
    } else {
        riscv_cpu_update_mip(env, timer_irq, BOOL_TO_MASK(0));
    }

    if (timecmp == UINT64_MAX) {
        timer_del(timer);
        return;
    }

    diff = timecmp - rtc_r;
    ns_diff = muldiv64(diff, NANOSECONDS_PER_SECOND, timebase_freq);

    if ((NANOSECONDS_PER_SECOND > timebase_freq && ns_diff < diff) ||
        ns_diff > INT64_MAX) {
        next = INT64_MAX;
    } else {
        next = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + ns_diff;
        next = MIN(next, INT64_MAX);
    }

    timer_mod(timer, next);
}

static void riscv_timer_disable_timecmp(CPURISCVState *env, QEMUTimer *timer,
                                 uint32_t timer_irq)
{
    if ((timer_irq == MIP_STIP) && !get_field(env->menvcfg, MENVCFG_STCE)) {
        riscv_cpu_update_mip(env, timer_irq, BOOL_TO_MASK(0));
        timer_del(timer);
        return;
    }

    if ((timer_irq == MIP_VSTIP) &&
        (!get_field(env->menvcfg, MENVCFG_STCE) ||
         !get_field(env->henvcfg, HENVCFG_STCE))) {
        env->vstime_irq = 0;
        riscv_cpu_update_mip(env, 0, BOOL_TO_MASK(0));
        timer_del(timer);
        return;
    }
}

void riscv_timer_stce_changed(CPURISCVState *env, bool is_m_mode, bool enable)
{
    if (enable) {
        riscv_timer_write_timecmp(env, env->vstimer, env->vstimecmp,
                                  env->htimedelta, MIP_VSTIP);
    } else {
        riscv_timer_disable_timecmp(env, env->vstimer, MIP_VSTIP);
    }

    if (is_m_mode) {
        if (enable) {
            riscv_timer_write_timecmp(env, env->stimer, env->stimecmp, 0, MIP_STIP);
        } else {
            riscv_timer_disable_timecmp(env, env->stimer, MIP_STIP);
        }
    }
}

void riscv_timer_init(RISCVCPU *cpu)
{
    CPURISCVState *env;

    if (!cpu) {
        return;
    }

    env = &cpu->env;
    env->stimer = timer_new_ns(QEMU_CLOCK_VIRTUAL, &riscv_stimer_cb, cpu);
    env->stimecmp = 0;

    env->vstimer = timer_new_ns(QEMU_CLOCK_VIRTUAL, &riscv_vstimer_cb, cpu);
    env->vstimecmp = 0;
}
