/*
 * QEMU RISC-V PMP (Physical Memory Protection)
 *
 * Author: Daire McNamara, daire.mcnamara@emdalo.com
 *         Ivan Griffin, ivan.griffin@emdalo.com
 *
 * This provides a RISC-V Physical Memory Protection implementation
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
#include "qapi/error.h"
#include "cpu.h"
#include "trace.h"
#include "exec/cputlb.h"
#include "exec/page-protection.h"
#include "exec/target_page.h"

static bool pmp_write_cfg(CPURISCVState *env, uint32_t addr_index,
                          uint8_t val);
static uint8_t pmp_read_cfg(CPURISCVState *env, uint32_t addr_index);

static inline uint8_t pmp_get_smepmp_operation(uint8_t cfg)
{
    return ((cfg & PMP_LOCK) >> 4) | ((cfg & PMP_READ) << 2) |
           (cfg & PMP_WRITE) | ((cfg & PMP_EXEC) >> 2);
}

static inline uint8_t pmp_get_a_field(uint8_t cfg)
{
    uint8_t a = cfg >> 3;
    return a & 0x3;
}

static inline int pmp_is_locked(CPURISCVState *env, uint32_t pmp_index)
{
    if (env->pmp_state.pmp[pmp_index].cfg_reg & PMP_LOCK) {
        return 1;
    }

    return 0;
}

static int pmp_is_readonly(CPURISCVState *env, uint32_t pmp_index)
{
    return pmp_is_locked(env, pmp_index) && !MSECCFG_RLB_ISSET(env);
}

static int pmp_is_invalid_smepmp_cfg(CPURISCVState *env, uint8_t val)
{
    if (!MSECCFG_MML_ISSET(env) || MSECCFG_RLB_ISSET(env)) {
        return 0;
    }

    switch (pmp_get_smepmp_operation(val)) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 12:
    case 14:
    case 15:
        return 0;
    case 9:
    case 10:
    case 11:
    case 13:
        return 1;
    default:
        g_assert_not_reached();
    }
}
static inline int pmp_get_granularity_g(CPURISCVState *env)
{
    return __builtin_ctz(riscv_cpu_cfg(env)->pmp_granularity >> 2);
}


uint32_t pmp_get_num_rules(CPURISCVState *env)
{
     return env->pmp_state.num_rules;
}

static inline uint8_t pmp_read_cfg(CPURISCVState *env, uint32_t pmp_index)
{
    uint8_t pmp_regions = riscv_cpu_cfg(env)->pmp_regions;

    if (pmp_index < pmp_regions) {
        return env->pmp_state.pmp[pmp_index].cfg_reg;
    }

    return 0;
}


static bool pmp_write_cfg(CPURISCVState *env, uint32_t pmp_index, uint8_t val)
{
    uint8_t pmp_regions = riscv_cpu_cfg(env)->pmp_regions;

    if (pmp_index < pmp_regions) {
        if (env->pmp_state.pmp[pmp_index].cfg_reg == val) {
            return false;
        }

        if (pmp_is_readonly(env, pmp_index)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ignoring pmpcfg write - read only\n");
        } else if (pmp_is_invalid_smepmp_cfg(env, val)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ignoring pmpcfg write - invalid\n");
        } else {
            uint8_t a_field = pmp_get_a_field(val);
            if ((riscv_cpu_cfg(env)->pmp_granularity >
                MIN_RISCV_PMP_GRANULARITY) && (a_field == PMP_AMATCH_NA4)) {
                    return false;
            }
            env->pmp_state.pmp[pmp_index].cfg_reg = val;
            pmp_update_rule_addr(env, pmp_index);
            return true;
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ignoring pmpcfg write - out of bounds\n");
    }

    return false;
}

void pmp_unlock_entries(CPURISCVState *env)
{
    uint32_t pmp_num = pmp_get_num_rules(env);
    int i;

    for (i = 0; i < pmp_num; i++) {
        env->pmp_state.pmp[i].cfg_reg &= ~(PMP_LOCK | PMP_AMATCH);
    }
}

static void pmp_decode_napot(hwaddr a, hwaddr *sa, hwaddr *ea)
{
    a = (a << 2) | 0x3;
    *sa = a & (a + 1);
    *ea = a | (a + 1);
}

void pmp_update_rule_addr(CPURISCVState *env, uint32_t pmp_index)
{
    uint8_t this_cfg = env->pmp_state.pmp[pmp_index].cfg_reg;
    target_ulong this_addr = env->pmp_state.pmp[pmp_index].addr_reg;
    target_ulong prev_addr = 0u;
    hwaddr sa = 0u;
    hwaddr ea = 0u;
    int g = pmp_get_granularity_g(env);

    if (pmp_index >= 1u) {
        prev_addr = env->pmp_state.pmp[pmp_index - 1].addr_reg;
    }

    switch (pmp_get_a_field(this_cfg)) {
    case PMP_AMATCH_OFF:
        sa = 0u;
        ea = -1;
        break;

    case PMP_AMATCH_TOR:
        if (g >= 1) {
            prev_addr &= ~((1ULL << g) - 1ULL);
            this_addr &= ~((1ULL << g) - 1ULL);
        }
        if (prev_addr >= this_addr) {
            sa = ea = 0u;
            break;
        }
        sa = prev_addr << 2;
        ea = (this_addr << 2) - 1u;
        break;

    case PMP_AMATCH_NA4:
        sa = this_addr << 2;
        ea = (sa + 4u) - 1u;
        break;

    case PMP_AMATCH_NAPOT:
        pmp_decode_napot(this_addr, &sa, &ea);
        break;

    default:
        sa = 0u;
        ea = 0u;
        break;
    }

    env->pmp_state.addr[pmp_index].sa = sa;
    env->pmp_state.addr[pmp_index].ea = ea;
}

void pmp_update_rule_nums(CPURISCVState *env)
{
    int i;
    uint8_t pmp_regions = riscv_cpu_cfg(env)->pmp_regions;

    env->pmp_state.num_rules = 0;
    for (i = 0; i < pmp_regions; i++) {
        const uint8_t a_field =
            pmp_get_a_field(env->pmp_state.pmp[i].cfg_reg);
        if (PMP_AMATCH_OFF != a_field) {
            env->pmp_state.num_rules++;
        }
    }
}

static int pmp_is_in_range(CPURISCVState *env, int pmp_index, hwaddr addr)
{
    int result = 0;

    if ((addr >= env->pmp_state.addr[pmp_index].sa) &&
        (addr <= env->pmp_state.addr[pmp_index].ea)) {
        result = 1;
    } else {
        result = 0;
    }

    return result;
}

static bool pmp_hart_has_privs_default(CPURISCVState *env, pmp_priv_t privs,
                                       pmp_priv_t *allowed_privs,
                                       target_ulong mode)
{
    bool ret;

    if (MSECCFG_MMWP_ISSET(env)) {
        *allowed_privs = 0;
        return false;
    } else if (MSECCFG_MML_ISSET(env)) {
        if (mode == PRV_M && !(privs & PMP_EXEC)) {
            ret = true;
            *allowed_privs = PMP_READ | PMP_WRITE;
        } else {
            ret = false;
            *allowed_privs = 0;
        }

        return ret;
    }

    if (!riscv_cpu_cfg(env)->pmp || (mode == PRV_M)) {
        ret = true;
        *allowed_privs = PMP_READ | PMP_WRITE | PMP_EXEC;
    } else {
        ret = false;
        *allowed_privs = 0;
    }

    return ret;
}


bool pmp_hart_has_privs(CPURISCVState *env, hwaddr addr,
                        target_ulong size, pmp_priv_t privs,
                        pmp_priv_t *allowed_privs, target_ulong mode)
{
    int i = 0;
    int pmp_size = 0;
    hwaddr s = 0;
    hwaddr e = 0;
    uint8_t pmp_regions = riscv_cpu_cfg(env)->pmp_regions;

    if (0 == pmp_get_num_rules(env)) {
        return pmp_hart_has_privs_default(env, privs, allowed_privs, mode);
    }

    if (size == 0) {
        if (riscv_cpu_cfg(env)->mmu) {
            pmp_size = -(addr | TARGET_PAGE_MASK);
        } else {
            pmp_size = 2 << riscv_cpu_mxl(env);
        }
    } else {
        pmp_size = size;
    }

    for (i = 0; i < pmp_regions; i++) {
        s = pmp_is_in_range(env, i, addr);
        e = pmp_is_in_range(env, i, addr + pmp_size - 1);

        if ((s + e) == 1) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "pmp violation - access is partially inside\n");
            *allowed_privs = 0;
            return false;
        }

        const uint8_t a_field =
            pmp_get_a_field(env->pmp_state.pmp[i].cfg_reg);

        if (((s + e) == 2) && (PMP_AMATCH_OFF != a_field)) {
            if (!MSECCFG_MML_ISSET(env)) {
                *allowed_privs = PMP_READ | PMP_WRITE | PMP_EXEC;
                if ((mode != PRV_M) || pmp_is_locked(env, i)) {
                    *allowed_privs &= env->pmp_state.pmp[i].cfg_reg;
                }
            } else {
                const uint8_t smepmp_operation =
                    pmp_get_smepmp_operation(env->pmp_state.pmp[i].cfg_reg);

                if (mode == PRV_M) {
                    switch (smepmp_operation) {
                    case 0:
                    case 1:
                    case 4:
                    case 5:
                    case 6:
                    case 7:
                    case 8:
                        *allowed_privs = 0;
                        break;
                    case 2:
                    case 3:
                    case 14:
                        *allowed_privs = PMP_READ | PMP_WRITE;
                        break;
                    case 9:
                    case 10:
                        *allowed_privs = PMP_EXEC;
                        break;
                    case 11:
                    case 13:
                        *allowed_privs = PMP_READ | PMP_EXEC;
                        break;
                    case 12:
                    case 15:
                        *allowed_privs = PMP_READ;
                        break;
                    default:
                        g_assert_not_reached();
                    }
                } else {
                    switch (smepmp_operation) {
                    case 0:
                    case 8:
                    case 9:
                    case 12:
                    case 13:
                    case 14:
                        *allowed_privs = 0;
                        break;
                    case 1:
                    case 10:
                    case 11:
                        *allowed_privs = PMP_EXEC;
                        break;
                    case 2:
                    case 4:
                    case 15:
                        *allowed_privs = PMP_READ;
                        break;
                    case 3:
                    case 6:
                        *allowed_privs = PMP_READ | PMP_WRITE;
                        break;
                    case 5:
                        *allowed_privs = PMP_READ | PMP_EXEC;
                        break;
                    case 7:
                        *allowed_privs = PMP_READ | PMP_WRITE | PMP_EXEC;
                        break;
                    default:
                        g_assert_not_reached();
                    }
                }
            }

            return (privs & *allowed_privs) == privs;
        }
    }

    return pmp_hart_has_privs_default(env, privs, allowed_privs, mode);
}

void pmpcfg_csr_write(CPURISCVState *env, uint32_t reg_index,
                      target_ulong val)
{
    int i;
    uint8_t cfg_val;
    int pmpcfg_nums = 2 << riscv_cpu_mxl(env);
    bool modified = false;

    trace_pmpcfg_csr_write(env->mhartid, reg_index, val);

    for (i = 0; i < pmpcfg_nums; i++) {
        cfg_val = (val >> 8 * i)  & 0xff;
        modified |= pmp_write_cfg(env, (reg_index * 4) + i, cfg_val);
    }

    if (modified) {
        pmp_update_rule_nums(env);
        tlb_flush(env_cpu(env));
    }
}


target_ulong pmpcfg_csr_read(CPURISCVState *env, uint32_t reg_index)
{
    int i;
    target_ulong cfg_val = 0;
    target_ulong val = 0;
    int pmpcfg_nums = 2 << riscv_cpu_mxl(env);

    for (i = 0; i < pmpcfg_nums; i++) {
        val = pmp_read_cfg(env, (reg_index * 4) + i);
        cfg_val |= (val << (i * 8));
    }
    trace_pmpcfg_csr_read(env->mhartid, reg_index, cfg_val);

    return cfg_val;
}


void pmpaddr_csr_write(CPURISCVState *env, uint32_t addr_index,
                       target_ulong val)
{
    trace_pmpaddr_csr_write(env->mhartid, addr_index, val);
    bool is_next_cfg_tor = false;
    uint8_t pmp_regions = riscv_cpu_cfg(env)->pmp_regions;

    if (addr_index < pmp_regions) {
        if (env->pmp_state.pmp[addr_index].addr_reg == val) {
            return;
        }

        if (addr_index + 1 < pmp_regions) {
            uint8_t pmp_cfg = env->pmp_state.pmp[addr_index + 1].cfg_reg;
            is_next_cfg_tor = PMP_AMATCH_TOR == pmp_get_a_field(pmp_cfg);

            if (pmp_is_readonly(env, addr_index + 1) && is_next_cfg_tor) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "ignoring pmpaddr write - pmpcfg+1 read only\n");
                return;
            }
        }

        if (!pmp_is_readonly(env, addr_index)) {
            env->pmp_state.pmp[addr_index].addr_reg = val;
            pmp_update_rule_addr(env, addr_index);
            if (is_next_cfg_tor) {
                pmp_update_rule_addr(env, addr_index + 1);
            }
            tlb_flush(env_cpu(env));
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ignoring pmpaddr write - read only\n");
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ignoring pmpaddr write - out of bounds\n");
    }
}


target_ulong pmpaddr_csr_read(CPURISCVState *env, uint32_t addr_index)
{
    target_ulong val = 0;
    uint8_t pmp_regions = riscv_cpu_cfg(env)->pmp_regions;

    if (addr_index < pmp_regions) {
        val = env->pmp_state.pmp[addr_index].addr_reg;
        int g = pmp_get_granularity_g(env);
        switch (pmp_get_a_field(env->pmp_state.pmp[addr_index].cfg_reg)) {
        case PMP_AMATCH_OFF:
            /* fallthrough */
        case PMP_AMATCH_TOR:
            if (g >= 1 && g < TARGET_LONG_BITS) {
                val &= ~((1ULL << g) - 1ULL);
            }
            break;
        case PMP_AMATCH_NAPOT:
            if (g >= 2 && g < TARGET_LONG_BITS) {
                val |= ((1ULL << (g - 1)) - 1ULL);
            }
            break;
        default:
            break;
        }
        trace_pmpaddr_csr_read(env->mhartid, addr_index, val);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ignoring pmpaddr read - out of bounds\n");
    }

    return val;
}

void mseccfg_csr_write(CPURISCVState *env, target_ulong val)
{
    int i;
    uint64_t mask = MSECCFG_MMWP | MSECCFG_MML;
    uint8_t pmp_regions = riscv_cpu_cfg(env)->pmp_regions;
    if (riscv_cpu_cfg(env)->ext_smmpm &&
        riscv_cpu_mxl(env) == MXL_RV64 &&
        get_field(val, MSECCFG_PMM) != PMM_FIELD_RESERVED) {
        mask |= MSECCFG_PMM;
    }

    trace_mseccfg_csr_write(env->mhartid, val);

    if (!MSECCFG_RLB_ISSET(env)) {
        for (i = 0; i < pmp_regions; i++) {
            if (pmp_is_locked(env, i)) {
                val &= ~MSECCFG_RLB;
                break;
            }
        }
    }

    if (riscv_cpu_cfg(env)->ext_smepmp) {
        val |= (env->mseccfg & mask);
        if ((val ^ env->mseccfg) & mask) {
            tlb_flush(env_cpu(env));
        }
    } else {
        mask |= MSECCFG_RLB;
        val &= ~(mask);
    }

    if (env_archcpu(env)->cfg.ext_zicfilp) {
        val |= (val & MSECCFG_MLPE);
    }

    env->mseccfg = val;
}

target_ulong mseccfg_csr_read(CPURISCVState *env)
{
    trace_mseccfg_csr_read(env->mhartid, env->mseccfg);
    return env->mseccfg;
}

target_ulong pmp_get_tlb_size(CPURISCVState *env, hwaddr addr)
{
    hwaddr pmp_sa;
    hwaddr pmp_ea;
    hwaddr tlb_sa = addr & ~(TARGET_PAGE_SIZE - 1);
    hwaddr tlb_ea = tlb_sa + TARGET_PAGE_SIZE - 1;
    int i;
    uint8_t pmp_regions = riscv_cpu_cfg(env)->pmp_regions;

    if (!riscv_cpu_cfg(env)->pmp || !pmp_get_num_rules(env)) {
        return TARGET_PAGE_SIZE;
    }

    for (i = 0; i < pmp_regions; i++) {
        if (pmp_get_a_field(env->pmp_state.pmp[i].cfg_reg) == PMP_AMATCH_OFF) {
            continue;
        }

        pmp_sa = env->pmp_state.addr[i].sa;
        pmp_ea = env->pmp_state.addr[i].ea;

        if (pmp_sa <= tlb_sa && pmp_ea >= tlb_ea) {
            return TARGET_PAGE_SIZE;
        } else if ((pmp_sa >= tlb_sa && pmp_sa <= tlb_ea) ||
                   (pmp_ea >= tlb_sa && pmp_ea <= tlb_ea)) {
            return 1;
        }
    }

    return TARGET_PAGE_SIZE;
}

int pmp_priv_to_page_prot(pmp_priv_t pmp_priv)
{
    int prot = 0;

    if (pmp_priv & PMP_READ) {
        prot |= PAGE_READ;
    }
    if (pmp_priv & PMP_WRITE) {
        prot |= PAGE_WRITE;
    }
    if (pmp_priv & PMP_EXEC) {
        prot |= PAGE_EXEC;
    }

    return prot;
}
