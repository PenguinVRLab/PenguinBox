/*
 *  PowerPC emulation helpers for QEMU.
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
#include "cpu.h"
#include "hw/ppc/ppc.h"
#include "exec/helper-proto.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"

target_ulong helper_load_tbl(CPUPPCState *env)
{
    return (target_ulong)cpu_ppc_load_tbl(env);
}

target_ulong helper_load_tbu(CPUPPCState *env)
{
    return cpu_ppc_load_tbu(env);
}

target_ulong helper_load_atbl(CPUPPCState *env)
{
    return (target_ulong)cpu_ppc_load_atbl(env);
}

target_ulong helper_load_atbu(CPUPPCState *env)
{
    return cpu_ppc_load_atbu(env);
}

target_ulong helper_load_vtb(CPUPPCState *env)
{
    return cpu_ppc_load_vtb(env);
}

#if defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY)
target_ulong helper_load_purr(CPUPPCState *env)
{
    return (target_ulong)cpu_ppc_load_purr(env);
}

void helper_store_purr(CPUPPCState *env, target_ulong val)
{
    CPUState *cs = env_cpu(env);
    CPUState *ccs;

    if (ppc_cpu_lpar_single_threaded(cs)) {
        cpu_ppc_store_purr(env, val);
        return;
    }

    THREAD_SIBLING_FOREACH(cs, ccs) {
        CPUPPCState *cenv = &POWERPC_CPU(ccs)->env;
        cpu_ppc_store_purr(cenv, val);
    }
}
#endif

#if !defined(CONFIG_USER_ONLY)
void helper_store_tbl(CPUPPCState *env, target_ulong val)
{
    CPUState *cs = env_cpu(env);
    CPUState *ccs;

    if (ppc_cpu_lpar_single_threaded(cs)) {
        cpu_ppc_store_tbl(env, val);
        return;
    }

    THREAD_SIBLING_FOREACH(cs, ccs) {
        CPUPPCState *cenv = &POWERPC_CPU(ccs)->env;
        cpu_ppc_store_tbl(cenv, val);
    }
}

void helper_store_tbu(CPUPPCState *env, target_ulong val)
{
    CPUState *cs = env_cpu(env);
    CPUState *ccs;

    if (ppc_cpu_lpar_single_threaded(cs)) {
        cpu_ppc_store_tbu(env, val);
        return;
    }

    THREAD_SIBLING_FOREACH(cs, ccs) {
        CPUPPCState *cenv = &POWERPC_CPU(ccs)->env;
        cpu_ppc_store_tbu(cenv, val);
    }
}

void helper_store_atbl(CPUPPCState *env, target_ulong val)
{
    cpu_ppc_store_atbl(env, val);
}

void helper_store_atbu(CPUPPCState *env, target_ulong val)
{
    cpu_ppc_store_atbu(env, val);
}

target_ulong helper_load_decr(CPUPPCState *env)
{
    return cpu_ppc_load_decr(env);
}

void helper_store_decr(CPUPPCState *env, target_ulong val)
{
    cpu_ppc_store_decr(env, val);
}

target_ulong helper_load_hdecr(CPUPPCState *env)
{
    return cpu_ppc_load_hdecr(env);
}

void helper_store_hdecr(CPUPPCState *env, target_ulong val)
{
    CPUState *cs = env_cpu(env);
    CPUState *ccs;

    if (ppc_cpu_lpar_single_threaded(cs)) {
        cpu_ppc_store_hdecr(env, val);
        return;
    }

    THREAD_SIBLING_FOREACH(cs, ccs) {
        CPUPPCState *cenv = &POWERPC_CPU(ccs)->env;
        cpu_ppc_store_hdecr(cenv, val);
    }
}

void helper_store_vtb(CPUPPCState *env, target_ulong val)
{
    CPUState *cs = env_cpu(env);
    CPUState *ccs;

    if (ppc_cpu_lpar_single_threaded(cs)) {
        cpu_ppc_store_vtb(env, val);
        return;
    }

    THREAD_SIBLING_FOREACH(cs, ccs) {
        CPUPPCState *cenv = &POWERPC_CPU(ccs)->env;
        cpu_ppc_store_vtb(cenv, val);
    }
}

void helper_store_tbu40(CPUPPCState *env, target_ulong val)
{
    CPUState *cs = env_cpu(env);
    CPUState *ccs;

    if (ppc_cpu_lpar_single_threaded(cs)) {
        cpu_ppc_store_tbu40(env, val);
        return;
    }

    THREAD_SIBLING_FOREACH(cs, ccs) {
        CPUPPCState *cenv = &POWERPC_CPU(ccs)->env;
        cpu_ppc_store_tbu40(cenv, val);
    }
}

target_ulong helper_load_40x_pit(CPUPPCState *env)
{
    return load_40x_pit(env);
}

void helper_store_40x_pit(CPUPPCState *env, target_ulong val)
{
    store_40x_pit(env, val);
}

void helper_store_40x_tcr(CPUPPCState *env, target_ulong val)
{
    store_40x_tcr(env, val);
}

void helper_store_40x_tsr(CPUPPCState *env, target_ulong val)
{
    store_40x_tsr(env, val);
}

void helper_store_booke_tcr(CPUPPCState *env, target_ulong val)
{
    store_booke_tcr(env, val);
}

void helper_store_booke_tsr(CPUPPCState *env, target_ulong val)
{
    store_booke_tsr(env, val);
}

#if defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY)
#include "hw/ppc/pnv_core.h"
#include "hw/ppc/pnv_chip.h"

static unsigned int tfmr_get_tb_state(uint64_t tfmr)
{
    return (tfmr & TFMR_TBST_ENCODED) >> (63 - 31);
}

static uint64_t tfmr_new_tb_state(uint64_t tfmr, unsigned int tbst)
{
    tfmr &= ~TFMR_TBST_LAST;
    tfmr |= (tfmr & TFMR_TBST_ENCODED) >> 4;
    tfmr &= ~TFMR_TBST_ENCODED;
    tfmr |= (uint64_t)tbst << (63 - 31);

    if (tbst == TBST_TB_RUNNING) {
        tfmr |= TFMR_TB_VALID;
    } else {
        tfmr &= ~TFMR_TB_VALID;
    }

    return tfmr;
}

static void write_tfmr(CPUPPCState *env, target_ulong val)
{
    CPUState *cs = env_cpu(env);

    if (ppc_cpu_core_single_threaded(cs)) {
        env->spr[SPR_TFMR] = val;
    } else {
        CPUState *ccs;
        THREAD_SIBLING_FOREACH(cs, ccs) {
            CPUPPCState *cenv = &POWERPC_CPU(ccs)->env;
            cenv->spr[SPR_TFMR] = val;
        }
    }
}

static PnvCoreTODState *cpu_get_tbst(PowerPCCPU *cpu)
{
    PnvCore *pc = pnv_cpu_state(cpu)->pnv_core;

    if (pc->big_core && pc->tod_state.big_core_quirk) {
        int core_id = CPU_CORE(pc)->core_id;
        if (core_id & 1) {
            pc = pc->chip->cores[core_id & ~1];
        }
    }

    return &pc->tod_state;
}

static void tb_state_machine_step(CPUPPCState *env)
{
    PowerPCCPU *cpu = env_archcpu(env);
    PnvCoreTODState *tod_state = cpu_get_tbst(cpu);
    uint64_t tfmr = env->spr[SPR_TFMR];
    unsigned int tbst = tfmr_get_tb_state(tfmr);

    if (!(tfmr & TFMR_TB_ECLIPZ) || tbst == TBST_TB_ERROR) {
        return;
    }

    if (tod_state->tb_sync_pulse_timer) {
        tod_state->tb_sync_pulse_timer--;
    } else {
        tfmr |= TFMR_TB_SYNC_OCCURED;
        write_tfmr(env, tfmr);
    }

    if (tod_state->tb_state_timer) {
        tod_state->tb_state_timer--;
        return;
    }

    if (tfmr & TFMR_LOAD_TOD_MOD) {
        tfmr &= ~TFMR_LOAD_TOD_MOD;
        if (tbst == TBST_GET_TOD) {
            tfmr = tfmr_new_tb_state(tfmr, TBST_TB_ERROR);
            tfmr |= TFMR_FIRMWARE_CONTROL_ERROR;
        } else {
            tfmr = tfmr_new_tb_state(tfmr, TBST_SEND_TOD_MOD);
            tfmr = tfmr_new_tb_state(tfmr, TBST_NOT_SET);
        }
    } else if (tfmr & TFMR_MOVE_CHIP_TOD_TO_TB) {
        if (tbst == TBST_SYNC_WAIT) {
            tfmr = tfmr_new_tb_state(tfmr, TBST_GET_TOD);
            tod_state->tb_state_timer = 3;
        } else if (tbst == TBST_GET_TOD) {
            if (tod_state->tod_sent_to_tb) {
                tfmr = tfmr_new_tb_state(tfmr, TBST_TB_RUNNING);
                tfmr &= ~TFMR_MOVE_CHIP_TOD_TO_TB;
                tod_state->tb_ready_for_tod = 0;
                tod_state->tod_sent_to_tb = 0;
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "TFMR error: MOVE_CHIP_TOD_TO_TB "
                          "state machine in invalid state 0x%x\n", tbst);
            tfmr = tfmr_new_tb_state(tfmr, TBST_TB_ERROR);
            tfmr |= TFMR_FIRMWARE_CONTROL_ERROR;
            tod_state->tb_ready_for_tod = 0;
        }
    }

    write_tfmr(env, tfmr);
}

target_ulong helper_load_tfmr(CPUPPCState *env)
{
    tb_state_machine_step(env);

    return env->spr[SPR_TFMR] | TFMR_TB_ECLIPZ;
}

void helper_store_tfmr(CPUPPCState *env, target_ulong val)
{
    PowerPCCPU *cpu = env_archcpu(env);
    PnvCoreTODState *tod_state = cpu_get_tbst(cpu);
    uint64_t tfmr = env->spr[SPR_TFMR];
    uint64_t clear_on_write;
    unsigned int tbst = tfmr_get_tb_state(tfmr);

    if (!(val & TFMR_TB_ECLIPZ)) {
        qemu_log_mask(LOG_UNIMP, "TFMR non-ECLIPZ mode not implemented\n");
        tfmr &= ~TFMR_TBST_ENCODED;
        tfmr &= ~TFMR_TBST_LAST;
        goto out;
    }

    tfmr = (tfmr & ~TFMR_CONTROL_MASK) | (val & TFMR_CONTROL_MASK);

    clear_on_write = val & TFMR_FIRMWARE_CONTROL_ERROR;
    tfmr &= ~clear_on_write;

    tfmr &= ~TFMR_TB_SYNC_OCCURED;
    tod_state->tb_sync_pulse_timer = 1;

    if (((tfmr | val) & (TFMR_LOAD_TOD_MOD | TFMR_MOVE_CHIP_TOD_TO_TB)) ==
                        (TFMR_LOAD_TOD_MOD | TFMR_MOVE_CHIP_TOD_TO_TB)) {
        qemu_log_mask(LOG_GUEST_ERROR, "TFMR error: LOAD_TOD_MOD and "
                                       "MOVE_CHIP_TOD_TO_TB both set\n");
        tfmr = tfmr_new_tb_state(tfmr, TBST_TB_ERROR);
        tfmr |= TFMR_FIRMWARE_CONTROL_ERROR;
        tod_state->tb_ready_for_tod = 0;
        goto out;
    }

    if (tfmr & TFMR_CLEAR_TB_ERRORS) {
        tfmr = tfmr_new_tb_state(tfmr, TBST_RESET);
        tfmr &= ~TFMR_CLEAR_TB_ERRORS;
        tfmr &= ~TFMR_LOAD_TOD_MOD;
        tfmr &= ~TFMR_MOVE_CHIP_TOD_TO_TB;
        tfmr &= ~TFMR_FIRMWARE_CONTROL_ERROR;
        tod_state->tb_ready_for_tod = 0;
        tod_state->tod_sent_to_tb = 0;
        goto out;
    }

    if (tbst == TBST_TB_ERROR) {
        qemu_log_mask(LOG_GUEST_ERROR, "TFMR error: mtspr TFMR in TB_ERROR"
                                       " state\n");
        tfmr |= TFMR_FIRMWARE_CONTROL_ERROR;
        return;
    }

    if (tfmr & TFMR_LOAD_TOD_MOD) {
        tod_state->tb_state_timer = 3;
    } else if (tfmr & TFMR_MOVE_CHIP_TOD_TO_TB) {
        if (tbst == TBST_NOT_SET) {
            tfmr = tfmr_new_tb_state(tfmr, TBST_SYNC_WAIT);
            tod_state->tb_ready_for_tod = 1;
            tod_state->tb_state_timer = 3;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "TFMR error: MOVE_CHIP_TOD_TO_TB "
                                           "not in TB not set state 0x%x\n",
                                           tbst);
            tfmr = tfmr_new_tb_state(tfmr, TBST_TB_ERROR);
            tfmr |= TFMR_FIRMWARE_CONTROL_ERROR;
            tod_state->tb_ready_for_tod = 0;
        }
    }

out:
    write_tfmr(env, tfmr);
}
#endif

target_ulong helper_load_dcr(CPUPPCState *env, target_ulong dcrn)
{
    uint32_t val = 0;

    if (unlikely(env->dcr_env == NULL)) {
        qemu_log_mask(LOG_GUEST_ERROR, "No DCR environment\n");
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_INVAL |
                               POWERPC_EXCP_INVAL_INVAL, GETPC());
    } else {
        int ret;

        bql_lock();
        ret = ppc_dcr_read(env->dcr_env, (uint32_t)dcrn, &val);
        bql_unlock();
        if (unlikely(ret != 0)) {
            qemu_log_mask(LOG_GUEST_ERROR, "DCR read error %d %03x\n",
                          (uint32_t)dcrn, (uint32_t)dcrn);
            raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                                   POWERPC_EXCP_INVAL |
                                   POWERPC_EXCP_INVAL_INVAL, GETPC());
        }
    }
    return val;
}

void helper_store_dcr(CPUPPCState *env, target_ulong dcrn, target_ulong val)
{
    if (unlikely(env->dcr_env == NULL)) {
        qemu_log_mask(LOG_GUEST_ERROR, "No DCR environment\n");
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_INVAL |
                               POWERPC_EXCP_INVAL_INVAL, GETPC());
    } else {
        int ret;
        bql_lock();
        ret = ppc_dcr_write(env->dcr_env, (uint32_t)dcrn, (uint32_t)val);
        bql_unlock();
        if (unlikely(ret != 0)) {
            qemu_log_mask(LOG_GUEST_ERROR, "DCR write error %d %03x\n",
                          (uint32_t)dcrn, (uint32_t)dcrn);
            raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                                   POWERPC_EXCP_INVAL |
                                   POWERPC_EXCP_INVAL_INVAL, GETPC());
        }
    }
}
#endif
