/*
 * OpenRISC system instructions helper routines
 *
 * Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
 *                         Zhizhou Zhang <etouzh@gmail.com>
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
#include "cpu.h"
#include "exec/cputlb.h"
#include "exec/target_page.h"
#include "exec/helper-proto.h"
#include "exception.h"
#ifndef CONFIG_USER_ONLY
#include "hw/boards.h"
#endif
#include "tcg/insn-start-words.h"

#define TO_SPR(group, number) (((group) << 11) + (number))

static inline bool is_user(CPUOpenRISCState *env)
{
#ifdef CONFIG_USER_ONLY
    return true;
#else
    return (env->sr & SR_SM) == 0;
#endif
}

void HELPER(mtspr)(CPUOpenRISCState *env, uint32_t spr, uint32_t rb)
{
    OpenRISCCPU *cpu = env_archcpu(env);
#ifndef CONFIG_USER_ONLY
    CPUState *cs = env_cpu(env);
    uint32_t mr;
    int idx;
#endif

    switch (spr) {
    case TO_SPR(0, 20):
        cpu_set_fpcsr(env, rb);
        return;
    }

    if (is_user(env)) {
        raise_exception(cpu, EXCP_ILLEGAL);
    }

#ifndef CONFIG_USER_ONLY
    switch (spr) {
    case TO_SPR(0, 11):
        env->evbar = rb;
        break;

    case TO_SPR(0, 16):
        cpu_restore_state(cs, GETPC());
        if (env->pc != rb) {
            env->pc = rb;
            env->dflag = 0;
        }
        cpu_loop_exit(cs);
        break;

    case TO_SPR(0, 17):
        cpu_set_sr(env, rb);
        break;

    case TO_SPR(0, 32):
        env->epcr = rb;
        break;

    case TO_SPR(0, 48):
        env->eear = rb;
        break;

    case TO_SPR(0, 64):
        env->esr = rb;
        break;

    case TO_SPR(0, 1024) ... TO_SPR(0, 1024 + (16 * 32)):
        idx = (spr - 1024);
        env->shadow_gpr[idx / 32][idx % 32] = rb;
        break;

    case TO_SPR(1, 512) ... TO_SPR(1, 512 + TLB_SIZE - 1):
        idx = spr - TO_SPR(1, 512);
        mr = env->tlb.dtlb[idx].mr;
        if (mr & 1) {
            tlb_flush_page(cs, mr & TARGET_PAGE_MASK);
        }
        if (rb & 1) {
            tlb_flush_page(cs, rb & TARGET_PAGE_MASK);
        }
        env->tlb.dtlb[idx].mr = rb;
        break;
    case TO_SPR(1, 640) ... TO_SPR(1, 640 + TLB_SIZE - 1):
        idx = spr - TO_SPR(1, 640);
        env->tlb.dtlb[idx].tr = rb;
        break;
    case TO_SPR(1, 768) ... TO_SPR(1, 895):
    case TO_SPR(1, 896) ... TO_SPR(1, 1023):
    case TO_SPR(1, 1024) ... TO_SPR(1, 1151):
    case TO_SPR(1, 1152) ... TO_SPR(1, 1279):
    case TO_SPR(1, 1280) ... TO_SPR(1, 1407):
    case TO_SPR(1, 1408) ... TO_SPR(1, 1535):
        break;

    case TO_SPR(2, 512) ... TO_SPR(2, 512 + TLB_SIZE - 1):
        idx = spr - TO_SPR(2, 512);
        mr = env->tlb.itlb[idx].mr;
        if (mr & 1) {
            tlb_flush_page(cs, mr & TARGET_PAGE_MASK);
        }
        if (rb & 1) {
            tlb_flush_page(cs, rb & TARGET_PAGE_MASK);
        }
        env->tlb.itlb[idx].mr = rb;
        break;
    case TO_SPR(2, 640) ... TO_SPR(2, 640 + TLB_SIZE - 1):
        idx = spr - TO_SPR(2, 640);
        env->tlb.itlb[idx].tr = rb;
        break;
    case TO_SPR(2, 768) ... TO_SPR(2, 895):
    case TO_SPR(2, 896) ... TO_SPR(2, 1023):
    case TO_SPR(2, 1024) ... TO_SPR(2, 1151):
    case TO_SPR(2, 1152) ... TO_SPR(2, 1279):
    case TO_SPR(2, 1280) ... TO_SPR(2, 1407):
    case TO_SPR(2, 1408) ... TO_SPR(2, 1535):
        break;

    case TO_SPR(5, 1):
        env->mac = deposit64(env->mac, 0, 32, rb);
        break;
    case TO_SPR(5, 2):
        env->mac = deposit64(env->mac, 32, 32, rb);
        break;
    case TO_SPR(8, 0):
        env->pmr = rb;
        if (env->pmr & PMR_DME || env->pmr & PMR_SME) {
            cpu_restore_state(cs, GETPC());
            env->pc += 4;
            cs->halted = 1;
            raise_exception(cpu, EXCP_HALTED);
        }
        break;
    case TO_SPR(9, 0):
        env->picmr = rb;
        bql_lock();
        if (env->picsr & env->picmr) {
            cpu_interrupt(cs, CPU_INTERRUPT_HARD);
        } else {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
        }
        bql_unlock();
        break;
    case TO_SPR(9, 2):
        env->picsr &= ~rb;
        break;
    case TO_SPR(10, 0):
        {
            bql_lock();
            if ((env->ttmr & TTMR_M) ^ (rb & TTMR_M)) {
                switch (rb & TTMR_M) {
                case TIMER_NONE:
                    cpu_openrisc_count_stop(cpu);
                    break;
                case TIMER_INTR:
                case TIMER_SHOT:
                case TIMER_CONT:
                    cpu_openrisc_count_start(cpu);
                    break;
                default:
                    break;
                }
            }

            int ip = env->ttmr & TTMR_IP;

            if (rb & TTMR_IP) {
                env->ttmr = (rb & ~TTMR_IP) | ip;
            } else {
                env->ttmr = rb & ~TTMR_IP;
                cpu_reset_interrupt(cs, CPU_INTERRUPT_TIMER);
            }
            cpu_openrisc_timer_update(cpu);
            bql_unlock();
        }
        break;

    case TO_SPR(10, 1):
        bql_lock();
        cpu_openrisc_count_set(cpu, rb);
        cpu_openrisc_timer_update(cpu);
        bql_unlock();
        break;
    }
#endif
}

uint32_t HELPER(mfspr)(CPUOpenRISCState *env, uint32_t rd, uint32_t spr)
{
    OpenRISCCPU *cpu = env_archcpu(env);
#ifndef CONFIG_USER_ONLY
    uint64_t data[INSN_START_WORDS];
    MachineState *ms = MACHINE(qdev_get_machine());
    CPUState *cs = env_cpu(env);
    int idx;
#endif

    switch (spr) {
    case TO_SPR(0, 20):
        return env->fpcsr;
    }

    if (is_user(env)) {
        raise_exception(cpu, EXCP_ILLEGAL);
    }

#ifndef CONFIG_USER_ONLY
    switch (spr) {
    case TO_SPR(0, 0):
        return env->vr;

    case TO_SPR(0, 1):
        return env->upr;

    case TO_SPR(0, 2):
        return env->cpucfgr;

    case TO_SPR(0, 3):
        return env->dmmucfgr;

    case TO_SPR(0, 4):
        return env->immucfgr;

    case TO_SPR(0, 9):
        return env->vr2;

    case TO_SPR(0, 10):
        return env->avr;

    case TO_SPR(0, 11):
        return env->evbar;

    case TO_SPR(0, 16):
        if (cpu_unwind_state_data(cs, GETPC(), data)) {
            return data[0];
        }
        return env->pc;

    case TO_SPR(0, 17):
        return cpu_get_sr(env);

    case TO_SPR(0, 18):
        if (cpu_unwind_state_data(cs, GETPC(), data)) {
            if (data[1] & 2) {
                return data[0] - 4;
            }
        }
        return env->ppc;

    case TO_SPR(0, 32):
        return env->epcr;

    case TO_SPR(0, 48):
        return env->eear;

    case TO_SPR(0, 64):
        return env->esr;

    case TO_SPR(0, 128):
        return cpu->parent_obj.cpu_index;

    case TO_SPR(0, 129):
        return ms->smp.max_cpus;

    case TO_SPR(0, 1024) ... TO_SPR(0, 1024 + (16 * 32)):
        idx = (spr - 1024);
        return env->shadow_gpr[idx / 32][idx % 32];

    case TO_SPR(1, 512) ... TO_SPR(1, 512 + TLB_SIZE - 1):
        idx = spr - TO_SPR(1, 512);
        return env->tlb.dtlb[idx].mr;

    case TO_SPR(1, 640) ... TO_SPR(1, 640 + TLB_SIZE - 1):
        idx = spr - TO_SPR(1, 640);
        return env->tlb.dtlb[idx].tr;

    case TO_SPR(1, 768) ... TO_SPR(1, 895):
    case TO_SPR(1, 896) ... TO_SPR(1, 1023):
    case TO_SPR(1, 1024) ... TO_SPR(1, 1151):
    case TO_SPR(1, 1152) ... TO_SPR(1, 1279):
    case TO_SPR(1, 1280) ... TO_SPR(1, 1407):
    case TO_SPR(1, 1408) ... TO_SPR(1, 1535):
        break;

    case TO_SPR(2, 512) ... TO_SPR(2, 512 + TLB_SIZE - 1):
        idx = spr - TO_SPR(2, 512);
        return env->tlb.itlb[idx].mr;

    case TO_SPR(2, 640) ... TO_SPR(2, 640 + TLB_SIZE - 1):
        idx = spr - TO_SPR(2, 640);
        return env->tlb.itlb[idx].tr;

    case TO_SPR(2, 768) ... TO_SPR(2, 895):
    case TO_SPR(2, 896) ... TO_SPR(2, 1023):
    case TO_SPR(2, 1024) ... TO_SPR(2, 1151):
    case TO_SPR(2, 1152) ... TO_SPR(2, 1279):
    case TO_SPR(2, 1280) ... TO_SPR(2, 1407):
    case TO_SPR(2, 1408) ... TO_SPR(2, 1535):
        break;

    case TO_SPR(5, 1):
        return (uint32_t)env->mac;
        break;
    case TO_SPR(5, 2):
        return env->mac >> 32;
        break;

    case TO_SPR(8, 0):
        return env->pmr;

    case TO_SPR(9, 0):
        return env->picmr;

    case TO_SPR(9, 2):
        return env->picsr;

    case TO_SPR(10, 0):
        return env->ttmr;

    case TO_SPR(10, 1):
        bql_lock();
        cpu_openrisc_count_update(cpu);
        bql_unlock();
        return cpu_openrisc_count_get(cpu);
    }
#endif

    return rd;
}
