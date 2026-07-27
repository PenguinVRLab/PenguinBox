/*
 * RISC-V Emulation Helpers for QEMU.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
 * Copyright (c) 2022      VRULL GmbH
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
#include "cpu.h"
#include "internals.h"
#include "exec/cputlb.h"
#include "accel/tcg/cpu-ldst.h"
#include "accel/tcg/probe.h"
#include "exec/helper-proto.h"
#include "exec/tlb-flags.h"
#include "trace.h"

#ifndef CONFIG_USER_ONLY
static inline MemOp mo_endian_env(CPURISCVState *env)
{
    return MO_TE;
}
#endif

G_NORETURN void riscv_raise_exception(CPURISCVState *env,
                                      RISCVException exception,
                                      uintptr_t pc)
{
    CPUState *cs = env_cpu(env);

    trace_riscv_exception(exception,
                          riscv_cpu_get_trap_name(exception, false),
                          env->pc);

    cs->exception_index = exception;
    cpu_loop_exit_restore(cs, pc);
}

void helper_raise_exception(CPURISCVState *env, uint32_t exception)
{
    riscv_raise_exception(env, exception, 0);
}

target_ulong helper_csrr(CPURISCVState *env, int csr)
{
    if (csr == CSR_SEED) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    target_ulong val = 0;
    RISCVException ret = riscv_csrr(env, csr, &val);

    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }
    return val;
}

void helper_csrw(CPURISCVState *env, int csr, target_ulong src)
{
    target_ulong mask = env->xl == MXL_RV32 ? UINT32_MAX : (target_ulong)-1;
    RISCVException ret = riscv_csrrw(env, csr, NULL, src, mask, GETPC());

    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }
}

target_ulong helper_csrrw(CPURISCVState *env, int csr,
                          target_ulong src, target_ulong write_mask)
{
    target_ulong val = 0;
    RISCVException ret = riscv_csrrw(env, csr, &val, src, write_mask, GETPC());

    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }
    return val;
}

target_ulong helper_csrr_i128(CPURISCVState *env, int csr)
{
    Int128 rv = int128_zero();
    RISCVException ret = riscv_csrr_i128(env, csr, &rv);

    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }

    env->retxh = int128_gethi(rv);
    return int128_getlo(rv);
}

void helper_csrw_i128(CPURISCVState *env, int csr,
                      target_ulong srcl, target_ulong srch)
{
    RISCVException ret = riscv_csrrw_i128(env, csr, NULL,
                                          int128_make128(srcl, srch),
                                          UINT128_MAX, GETPC());

    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }
}

target_ulong helper_csrrw_i128(CPURISCVState *env, int csr,
                               target_ulong srcl, target_ulong srch,
                               target_ulong maskl, target_ulong maskh)
{
    Int128 rv = int128_zero();
    RISCVException ret = riscv_csrrw_i128(env, csr, &rv,
                                          int128_make128(srcl, srch),
                                          int128_make128(maskl, maskh),
                                          GETPC());

    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }

    env->retxh = int128_gethi(rv);
    return int128_getlo(rv);
}


static void check_zicbo_envcfg(CPURISCVState *env, target_ulong envbits,
                                uintptr_t ra)
{
#ifndef CONFIG_USER_ONLY
    if ((env->priv < PRV_M) && !get_field(env->menvcfg, envbits)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, ra);
    }

    if (env->virt_enabled &&
        (((env->priv <= PRV_S) && !get_field(env->henvcfg, envbits)) ||
         ((env->priv < PRV_S) && !get_field(env->senvcfg, envbits)))) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, ra);
    }

    if ((env->priv < PRV_S) && !get_field(env->senvcfg, envbits)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, ra);
    }
#endif
}

void helper_cbo_zero(CPURISCVState *env, target_ulong address)
{
    RISCVCPU *cpu = env_archcpu(env);
    uint16_t cbozlen = cpu->cfg.cboz_blocksize;
    int mmu_idx = riscv_env_mmu_index(env, false);
    uintptr_t ra = GETPC();
    void *mem;

    check_zicbo_envcfg(env, MENVCFG_CBZE, ra);

    address &= ~(cbozlen - 1);

    mem = probe_write(env, address, cbozlen, mmu_idx, ra);

    if (likely(mem)) {
        memset(mem, 0, cbozlen);
    } else {
        for (int i = 0; i < cbozlen; i++) {
            cpu_stb_mmuidx_ra(env, address + i, 0, mmu_idx, ra);
        }
    }
}

static void check_zicbom_access(CPURISCVState *env,
                                target_ulong address,
                                uintptr_t ra)
{
    RISCVCPU *cpu = env_archcpu(env);
    int mmu_idx = riscv_env_mmu_index(env, false);
    uint16_t cbomlen = cpu->cfg.cbom_blocksize;
    void *phost;
    int ret;

    address &= ~(cbomlen - 1);

    ret = probe_access_flags(env, address, cbomlen, MMU_DATA_LOAD,
                             mmu_idx, true, &phost, ra);
    if (ret != TLB_INVALID_MASK) {
        return;
    }

    probe_write(env, address, cbomlen, mmu_idx, ra);
}

void helper_cbo_clean_flush(CPURISCVState *env, target_ulong address)
{
    uintptr_t ra = GETPC();
    check_zicbo_envcfg(env, MENVCFG_CBCFE, ra);
    check_zicbom_access(env, address, ra);

}

void helper_cbo_inval(CPURISCVState *env, target_ulong address)
{
    uintptr_t ra = GETPC();
    check_zicbo_envcfg(env, MENVCFG_CBIE, ra);
    check_zicbom_access(env, address, ra);

}

#ifndef CONFIG_USER_ONLY

target_ulong helper_sret(CPURISCVState *env)
{
    uint64_t mstatus;
    target_ulong prev_priv, prev_virt = env->virt_enabled;
    const target_ulong src_priv = env->priv;
    const bool src_virt = env->virt_enabled;

    if (!(env->priv >= PRV_S)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    target_ulong retpc = env->sepc & get_xepc_mask(env);
    if (!riscv_cpu_allow_16bit_insn(&env_archcpu(env)->cfg,
                                    env->priv_ver,
                                    env->misa_ext) && (retpc & 0x3)) {
        riscv_raise_exception(env, RISCV_EXCP_INST_ADDR_MIS, GETPC());
    }

    if (get_field(env->mstatus, MSTATUS_TSR) && !(env->priv >= PRV_M)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    if (env->virt_enabled && get_field(env->hstatus, HSTATUS_VTSR)) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, GETPC());
    }

    mstatus = env->mstatus;
    prev_priv = get_field(mstatus, MSTATUS_SPP);
    mstatus = set_field(mstatus, MSTATUS_SIE,
                        get_field(mstatus, MSTATUS_SPIE));
    mstatus = set_field(mstatus, MSTATUS_SPIE, 1);
    mstatus = set_field(mstatus, MSTATUS_SPP, PRV_U);

    if (riscv_cpu_cfg(env)->ext_ssdbltrp) {
        if (riscv_has_ext(env, RVH)) {
            target_ulong prev_vu = get_field(env->hstatus, HSTATUS_SPV) &&
                                   prev_priv == PRV_U;
            if (!env->virt_enabled && prev_vu) {
                env->vsstatus = set_field(env->vsstatus, MSTATUS_SDT, 0);
            }
        }
        mstatus = set_field(mstatus, MSTATUS_SDT, 0);
    }
    if (riscv_cpu_cfg(env)->ext_smdbltrp && env->priv >= PRV_M) {
        mstatus = set_field(mstatus, MSTATUS_MDT, 0);
    }
    if (env->priv_ver >= PRIV_VERSION_1_12_0) {
        mstatus = set_field(mstatus, MSTATUS_MPRV, 0);
    }
    env->mstatus = mstatus;

    if (riscv_has_ext(env, RVH) && !env->virt_enabled) {
        target_ulong hstatus = env->hstatus;

        prev_virt = get_field(hstatus, HSTATUS_SPV);
        hstatus = set_field(hstatus, HSTATUS_SPV, 0);

        env->hstatus = hstatus;

        if (prev_virt) {
            riscv_cpu_swap_hypervisor_regs(env);
        }
    }

    riscv_cpu_set_mode(env, prev_priv, prev_virt);

    if (cpu_get_fcfien(env)) {
        env->elp = get_field(env->mstatus, MSTATUS_SPELP);
    }
    env->mstatus = set_field(env->mstatus, MSTATUS_SPELP, 0);

    if (riscv_cpu_cfg(env)->ext_smctr || riscv_cpu_cfg(env)->ext_ssctr) {
        riscv_ctr_add_entry(env, env->pc, retpc, CTRDATA_TYPE_EXCEP_INT_RET,
                            src_priv, src_virt);
    }

    return retpc;
}

static void check_ret_from_m_mode(CPURISCVState *env, target_ulong retpc,
                                  target_ulong prev_priv,
                                  uintptr_t ra)
{
    if (!(env->priv >= PRV_M)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, ra);
    }

    if (!riscv_cpu_allow_16bit_insn(&env_archcpu(env)->cfg,
                                    env->priv_ver,
                                    env->misa_ext) && (retpc & 0x3)) {
        riscv_raise_exception(env, RISCV_EXCP_INST_ADDR_MIS, ra);
    }

    if (riscv_cpu_cfg(env)->pmp &&
        !pmp_get_num_rules(env) && (prev_priv != PRV_M)) {
        riscv_raise_exception(env, RISCV_EXCP_INST_ACCESS_FAULT, ra);
    }
}
static target_ulong ssdbltrp_mxret(CPURISCVState *env, target_ulong mstatus,
                                   target_ulong prev_priv,
                                   target_ulong prev_virt)
{
    if (prev_priv == PRV_U || (prev_virt &&
        (prev_priv == PRV_S || prev_priv == PRV_U))) {
        mstatus = set_field(mstatus, MSTATUS_SDT, 0);
        if (prev_virt && prev_priv == PRV_U) {
            env->vsstatus = set_field(env->vsstatus, MSTATUS_SDT, 0);
        }
    }

    return mstatus;
}

target_ulong helper_mret(CPURISCVState *env)
{
    target_ulong retpc = env->mepc & get_xepc_mask(env);
    uint64_t mstatus = env->mstatus;
    target_ulong prev_priv = get_field(mstatus, MSTATUS_MPP);
    uintptr_t ra = GETPC();

    check_ret_from_m_mode(env, retpc, prev_priv, ra);

    target_ulong prev_virt = get_field(env->mstatus, MSTATUS_MPV) &&
                             (prev_priv != PRV_M);
    mstatus = set_field(mstatus, MSTATUS_MIE,
                        get_field(mstatus, MSTATUS_MPIE));
    mstatus = set_field(mstatus, MSTATUS_MPIE, 1);
    mstatus = set_field(mstatus, MSTATUS_MPP,
                        riscv_has_ext(env, RVU) ? PRV_U : PRV_M);
    mstatus = set_field(mstatus, MSTATUS_MPV, 0);
    if (riscv_cpu_cfg(env)->ext_ssdbltrp) {
        mstatus = ssdbltrp_mxret(env, mstatus, prev_priv, prev_virt);
    }
    if (riscv_cpu_cfg(env)->ext_smdbltrp) {
        mstatus = set_field(mstatus, MSTATUS_MDT, 0);
    }
    if ((env->priv_ver >= PRIV_VERSION_1_12_0) && (prev_priv != PRV_M)) {
        mstatus = set_field(mstatus, MSTATUS_MPRV, 0);
    }
    env->mstatus = mstatus;

    if (riscv_has_ext(env, RVH) && prev_virt) {
        riscv_cpu_swap_hypervisor_regs(env);
    }

    riscv_cpu_set_mode(env, prev_priv, prev_virt);
    if (cpu_get_fcfien(env)) {
        env->elp = get_field(env->mstatus, MSTATUS_MPELP);
    }
    env->mstatus = set_field(env->mstatus, MSTATUS_MPELP, 0);

    if (riscv_cpu_cfg(env)->ext_smctr || riscv_cpu_cfg(env)->ext_ssctr) {
        riscv_ctr_add_entry(env, env->pc, retpc, CTRDATA_TYPE_EXCEP_INT_RET,
                            PRV_M, false);
    }

    return retpc;
}

target_ulong helper_mnret(CPURISCVState *env)
{
    target_ulong retpc = env->mnepc;
    target_ulong prev_priv = get_field(env->mnstatus, MNSTATUS_MNPP);
    target_ulong prev_virt;
    uintptr_t ra = GETPC();

    check_ret_from_m_mode(env, retpc, prev_priv, ra);

    prev_virt = get_field(env->mnstatus, MNSTATUS_MNPV) &&
                (prev_priv != PRV_M);
    env->mnstatus = set_field(env->mnstatus, MNSTATUS_NMIE, true);

    if (prev_priv < PRV_M) {
        env->mstatus = set_field(env->mstatus, MSTATUS_MPRV, false);
    }
    if (riscv_cpu_cfg(env)->ext_ssdbltrp) {
        env->mstatus = ssdbltrp_mxret(env, env->mstatus, prev_priv, prev_virt);
    }

    if (riscv_cpu_cfg(env)->ext_smdbltrp) {
        if (prev_priv < PRV_M) {
            env->mstatus = set_field(env->mstatus, MSTATUS_MDT, 0);
        }
    }

    if (riscv_has_ext(env, RVH) && prev_virt) {
        riscv_cpu_swap_hypervisor_regs(env);
    }

    riscv_cpu_set_mode(env, prev_priv, prev_virt);

    if (cpu_get_fcfien(env)) {
        env->elp = get_field(env->mnstatus, MNSTATUS_MNPELP);
    }
    env->mnstatus = set_field(env->mnstatus, MNSTATUS_MNPELP, 0);

    return retpc;
}

void helper_ctr_add_entry(CPURISCVState *env, target_ulong src,
                          target_ulong dest, target_ulong type)
{
    riscv_ctr_add_entry(env, src, dest, (enum CTRType)type,
                        env->priv, env->virt_enabled);
}

void helper_ctr_clear(CPURISCVState *env)
{
    RISCVException ret = smstateen_acc_ok(env, 0, SMSTATEEN0_CTR);
    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }

    if (env->priv == PRV_U) {
        uint32_t excep = env->virt_enabled ? RISCV_EXCP_VIRT_INSTRUCTION_FAULT :
            RISCV_EXCP_ILLEGAL_INST;
        riscv_raise_exception(env, excep, GETPC());
    }

    riscv_ctr_clear(env);
}

void helper_wfi(CPURISCVState *env)
{
    CPUState *cs = env_cpu(env);
    bool rvs = riscv_has_ext(env, RVS);
    bool prv_u = env->priv == PRV_U;
    bool prv_s = env->priv == PRV_S;

    if (((prv_s || (!rvs && prv_u)) && get_field(env->mstatus, MSTATUS_TW)) ||
        (rvs && prv_u && !env->virt_enabled)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    } else if (env->virt_enabled &&
               (prv_u || (prv_s && get_field(env->hstatus, HSTATUS_VTW)))) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, GETPC());
    } else {
        cs->halted = 1;
        cs->exception_index = EXCP_HLT;
        cpu_loop_exit(cs);
    }
}

void helper_wrs_nto(CPURISCVState *env)
{
    if (env->virt_enabled && (env->priv == PRV_S || env->priv == PRV_U) &&
        get_field(env->hstatus, HSTATUS_VTW) &&
        !get_field(env->mstatus, MSTATUS_TW)) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, GETPC());
    } else if (env->priv != PRV_M && get_field(env->mstatus, MSTATUS_TW)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }
}

void helper_tlb_flush(CPURISCVState *env)
{
    CPUState *cs = env_cpu(env);
    if (!env->virt_enabled &&
        (env->priv == PRV_U ||
         (env->priv == PRV_S && get_field(env->mstatus, MSTATUS_TVM)))) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    } else if (env->virt_enabled &&
               (env->priv == PRV_U || get_field(env->hstatus, HSTATUS_VTVM))) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, GETPC());
    } else {
        tlb_flush(cs);
    }
}

void helper_tlb_flush_all(CPURISCVState *env)
{
    CPUState *cs = env_cpu(env);
    tlb_flush_all_cpus_synced(cs);
}

void helper_hyp_tlb_flush(CPURISCVState *env)
{
    CPUState *cs = env_cpu(env);

    if (env->virt_enabled) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, GETPC());
    }

    if (env->priv == PRV_M ||
        (env->priv == PRV_S && !env->virt_enabled)) {
        tlb_flush(cs);
        return;
    }

    riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
}

void helper_hyp_gvma_tlb_flush(CPURISCVState *env)
{
    if (env->priv == PRV_S && !env->virt_enabled &&
        get_field(env->mstatus, MSTATUS_TVM)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    helper_hyp_tlb_flush(env);
}

static int check_access_hlsv(CPURISCVState *env, bool x, uintptr_t ra)
{
    if (env->priv == PRV_M) {
    } else if (env->virt_enabled) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, ra);
    } else if (env->priv == PRV_U && !get_field(env->hstatus, HSTATUS_HU)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, ra);
    }

    int mode = get_field(env->hstatus, HSTATUS_SPVP);
    if (!x && mode == PRV_S && get_field(env->vsstatus, MSTATUS_SUM)) {
        mode = MMUIdx_S_SUM;
    }
    return mode | MMU_2STAGE_BIT;
}

target_ulong helper_hyp_hlv_bu(CPURISCVState *env, target_ulong addr)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, false, ra);
    MemOpIdx oi = make_memop_idx(MO_UB, mmu_idx);

    return cpu_ldb_mmu(env, adjust_addr_virt(env, addr), oi, ra);
}

target_ulong helper_hyp_hlv_hu(CPURISCVState *env, target_ulong addr)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, false, ra);
    MemOpIdx oi = make_memop_idx(mo_endian_env(env) | MO_UW, mmu_idx);

    return cpu_ldw_mmu(env, adjust_addr_virt(env, addr), oi, ra);
}

target_ulong helper_hyp_hlv_wu(CPURISCVState *env, target_ulong addr)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, false, ra);
    MemOpIdx oi = make_memop_idx(mo_endian_env(env) | MO_UL, mmu_idx);

    return cpu_ldl_mmu(env, adjust_addr_virt(env, addr), oi, ra);
}

target_ulong helper_hyp_hlv_d(CPURISCVState *env, target_ulong addr)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, false, ra);
    MemOpIdx oi = make_memop_idx(mo_endian_env(env) | MO_UQ, mmu_idx);

    return cpu_ldq_mmu(env, adjust_addr_virt(env, addr), oi, ra);
}

void helper_hyp_hsv_b(CPURISCVState *env, target_ulong addr, target_ulong val)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, false, ra);
    MemOpIdx oi = make_memop_idx(MO_UB, mmu_idx);

    cpu_stb_mmu(env, adjust_addr_virt(env, addr), val, oi, ra);
}

void helper_hyp_hsv_h(CPURISCVState *env, target_ulong addr, target_ulong val)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, false, ra);
    MemOpIdx oi = make_memop_idx(mo_endian_env(env) | MO_UW, mmu_idx);

    cpu_stw_mmu(env, adjust_addr_virt(env, addr), val, oi, ra);
}

void helper_hyp_hsv_w(CPURISCVState *env, target_ulong addr, target_ulong val)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, false, ra);
    MemOpIdx oi = make_memop_idx(mo_endian_env(env) | MO_UL, mmu_idx);

    cpu_stl_mmu(env, adjust_addr_virt(env, addr), val, oi, ra);
}

void helper_hyp_hsv_d(CPURISCVState *env, target_ulong addr, target_ulong val)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, false, ra);
    MemOpIdx oi = make_memop_idx(mo_endian_env(env) | MO_UQ, mmu_idx);

    cpu_stq_mmu(env, adjust_addr_virt(env, addr), val, oi, ra);
}

target_ulong helper_hyp_hlvx_hu(CPURISCVState *env, target_ulong addr)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, true, ra);
    MemOpIdx oi = make_memop_idx(mo_endian_env(env) | MO_UW, mmu_idx);

    return cpu_ldw_code_mmu(env, addr, oi, GETPC());
}

target_ulong helper_hyp_hlvx_wu(CPURISCVState *env, target_ulong addr)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, true, ra);
    MemOpIdx oi = make_memop_idx(mo_endian_env(env) | MO_UL, mmu_idx);

    return cpu_ldl_code_mmu(env, addr, oi, ra);
}

void helper_ssamoswap_disabled(CPURISCVState *env)
{
    int exception = RISCV_EXCP_ILLEGAL_INST;

    if (!get_field(env->menvcfg, MENVCFG_SSE)) {
        exception = RISCV_EXCP_ILLEGAL_INST;
        goto done;
    }

    if (!riscv_has_ext(env, RVS)) {
        exception = RISCV_EXCP_ILLEGAL_INST;
        goto done;
    } else if (env->virt_enabled) {
        exception = RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        goto done;
    } else {
        exception = RISCV_EXCP_ILLEGAL_INST;
        goto done;
    }

done:
    riscv_raise_exception(env, exception, GETPC());
}

#endif
