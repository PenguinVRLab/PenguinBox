/*
 *  PowerPC MMU, TLB, SLB and BAT emulation helpers for QEMU.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
 *  Copyright (c) 2013 David Gibson, IBM Corporation
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
#include "qemu/units.h"
#include "cpu.h"
#include "exec/page-protection.h"
#include "qemu/error-report.h"
#include "qemu/qemu-print.h"
#include "system/hw_accel.h"
#include "system/memory.h"
#include "kvm_ppc.h"
#include "mmu-hash64.h"
#include "exec/log.h"
#include "hw/hw.h"
#include "internal.h"
#include "mmu-book3s-v3.h"
#include "mmu-books.h"
#include "helper_regs.h"

#ifdef CONFIG_TCG
#include "exec/helper-proto.h"
#endif

#ifdef DEBUG_SLB
#  define LOG_SLB(...) qemu_log_mask(CPU_LOG_MMU, __VA_ARGS__)
#else
#  define LOG_SLB(...) do { } while (0)
#endif

static ppc_slb_t *slb_lookup(PowerPCCPU *cpu, target_ulong eaddr)
{
    CPUPPCState *env = &cpu->env;
    uint64_t esid_256M, esid_1T;
    int n;

    LOG_SLB("%s: eaddr " TARGET_FMT_lx "\n", __func__, eaddr);

    esid_256M = (eaddr & SEGMENT_MASK_256M) | SLB_ESID_V;
    esid_1T = (eaddr & SEGMENT_MASK_1T) | SLB_ESID_V;

    for (n = 0; n < cpu->hash64_opts->slb_size; n++) {
        ppc_slb_t *slb = &env->slb[n];

        LOG_SLB("%s: slot %d %016" PRIx64 " %016"
                    PRIx64 "\n", __func__, n, slb->esid, slb->vsid);
        if (((slb->esid == esid_256M) &&
             ((slb->vsid & SLB_VSID_B) == SLB_VSID_B_256M))
            || ((slb->esid == esid_1T) &&
                ((slb->vsid & SLB_VSID_B) == SLB_VSID_B_1T))) {
            return slb;
        }
    }

    return NULL;
}

void dump_slb(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;
    int i;
    uint64_t slbe, slbv;

    cpu_synchronize_state(CPU(cpu));

    qemu_printf("SLB\tESID\t\t\tVSID\n");
    for (i = 0; i < cpu->hash64_opts->slb_size; i++) {
        slbe = env->slb[i].esid;
        slbv = env->slb[i].vsid;
        if (slbe == 0 && slbv == 0) {
            continue;
        }
        qemu_printf("%d\t0x%016" PRIx64 "\t0x%016" PRIx64 "\n",
                    i, slbe, slbv);
    }
}

#ifdef CONFIG_TCG
void helper_SLBIA(CPUPPCState *env, uint32_t ih)
{
    PowerPCCPU *cpu = env_archcpu(env);
    int starting_entry;
    int n;

    env->tlb_need_flush |= TLB_NEED_LOCAL_FLUSH;

    starting_entry = 1;

    if (env->mmu_model == POWERPC_MMU_3_00) {
        switch (ih) {
        case 0x7:
            return;

        case 0x3:
        case 0x4:
            starting_entry = 0;
            break;

        case 0x5:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "slbia undefined IH field %u.\n", ih);
            break;

        default:
            break;
        }
    }

    for (n = starting_entry; n < cpu->hash64_opts->slb_size; n++) {
        ppc_slb_t *slb = &env->slb[n];

        if (!(slb->esid & SLB_ESID_V)) {
            continue;
        }
        if (env->mmu_model == POWERPC_MMU_3_00) {
            if (ih == 0x3 && (slb->vsid & SLB_VSID_C) == 0) {
                continue;
            }
        }

        slb->esid &= ~SLB_ESID_V;
    }
}

#if defined(TARGET_PPC64)
void helper_SLBIAG(CPUPPCState *env, target_ulong rs, uint32_t l)
{
    PowerPCCPU *cpu = env_archcpu(env);
    int n;

    env->tlb_need_flush |= TLB_NEED_LOCAL_FLUSH;

    for (n = 0; n < cpu->hash64_opts->slb_size; n++) {
        ppc_slb_t *slb = &env->slb[n];
        slb->esid &= ~SLB_ESID_V;
    }
}
#endif

static void __helper_slbie(CPUPPCState *env, target_ulong addr,
                           target_ulong global)
{
    PowerPCCPU *cpu = env_archcpu(env);
    ppc_slb_t *slb;

    slb = slb_lookup(cpu, addr);
    if (!slb) {
        return;
    }

    if (slb->esid & SLB_ESID_V) {
        slb->esid &= ~SLB_ESID_V;

        env->tlb_need_flush |=
            (global == false ? TLB_NEED_LOCAL_FLUSH : TLB_NEED_GLOBAL_FLUSH);
    }
}

void helper_SLBIE(CPUPPCState *env, target_ulong addr)
{
    __helper_slbie(env, addr, false);
}

void helper_SLBIEG(CPUPPCState *env, target_ulong addr)
{
    __helper_slbie(env, addr, true);
}
#endif

int ppc_store_slb(PowerPCCPU *cpu, target_ulong slot,
                  target_ulong esid, target_ulong vsid)
{
    CPUPPCState *env = &cpu->env;
    ppc_slb_t *slb = &env->slb[slot];
    const PPCHash64SegmentPageSizes *sps = NULL;
    int i;

    if (slot >= cpu->hash64_opts->slb_size) {
        return -1;
    }
    if (esid & ~(SLB_ESID_ESID | SLB_ESID_V)) {
        return -1;
    }
    if (vsid & (SLB_VSID_B & ~SLB_VSID_B_1T)) {
        return -1;
    }
    if ((vsid & SLB_VSID_B) && !(ppc_hash64_has(cpu, PPC_HASH64_1TSEG))) {
        return -1;
    }

    for (i = 0; i < PPC_PAGE_SIZES_MAX_SZ; i++) {
        const PPCHash64SegmentPageSizes *sps1 = &cpu->hash64_opts->sps[i];

        if (!sps1->page_shift) {
            break;
        }

        if ((vsid & SLB_VSID_LLP_MASK) == sps1->slb_enc) {
            sps = sps1;
            break;
        }
    }

    if (!sps) {
        error_report("Bad page size encoding in SLB store: slot "TARGET_FMT_lu
                     " esid 0x"TARGET_FMT_lx" vsid 0x"TARGET_FMT_lx,
                     slot, esid, vsid);
        return -1;
    }

    slb->esid = esid;
    slb->vsid = vsid;
    slb->sps = sps;

    LOG_SLB("%s: " TARGET_FMT_lu " " TARGET_FMT_lx " - " TARGET_FMT_lx
            " => %016" PRIx64 " %016" PRIx64 "\n", __func__, slot, esid, vsid,
            slb->esid, slb->vsid);

    return 0;
}

#ifdef CONFIG_TCG
static int ppc_load_slb_esid(PowerPCCPU *cpu, target_ulong rb,
                             target_ulong *rt)
{
    CPUPPCState *env = &cpu->env;
    int slot = rb & 0xfff;
    ppc_slb_t *slb = &env->slb[slot];

    if (slot >= cpu->hash64_opts->slb_size) {
        return -1;
    }

    *rt = slb->esid;
    return 0;
}

static int ppc_load_slb_vsid(PowerPCCPU *cpu, target_ulong rb,
                             target_ulong *rt)
{
    CPUPPCState *env = &cpu->env;
    int slot = rb & 0xfff;
    ppc_slb_t *slb = &env->slb[slot];

    if (slot >= cpu->hash64_opts->slb_size) {
        return -1;
    }

    *rt = slb->vsid;
    return 0;
}

static int ppc_find_slb_vsid(PowerPCCPU *cpu, target_ulong rb,
                             target_ulong *rt)
{
    CPUPPCState *env = &cpu->env;
    ppc_slb_t *slb;

    if (!msr_is_64bit(env, env->msr)) {
        rb &= 0xffffffff;
    }
    slb = slb_lookup(cpu, rb);
    if (slb == NULL) {
        *rt = (target_ulong)-1ul;
    } else {
        *rt = slb->vsid;
    }
    return 0;
}

void helper_SLBMTE(CPUPPCState *env, target_ulong rb, target_ulong rs)
{
    PowerPCCPU *cpu = env_archcpu(env);

    if (ppc_store_slb(cpu, rb & 0xfff, rb & ~0xfffULL, rs) < 0) {
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_INVAL, GETPC());
    }
}

target_ulong helper_SLBMFEE(CPUPPCState *env, target_ulong rb)
{
    PowerPCCPU *cpu = env_archcpu(env);
    target_ulong rt = 0;

    if (ppc_load_slb_esid(cpu, rb, &rt) < 0) {
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_INVAL, GETPC());
    }
    return rt;
}

target_ulong helper_SLBFEE(CPUPPCState *env, target_ulong rb)
{
    PowerPCCPU *cpu = env_archcpu(env);
    target_ulong rt = 0;

    if (ppc_find_slb_vsid(cpu, rb, &rt) < 0) {
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_INVAL, GETPC());
    }
    return rt;
}

target_ulong helper_SLBMFEV(CPUPPCState *env, target_ulong rb)
{
    PowerPCCPU *cpu = env_archcpu(env);
    target_ulong rt = 0;

    if (ppc_load_slb_vsid(cpu, rb, &rt) < 0) {
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_INVAL, GETPC());
    }
    return rt;
}
#endif

static inline int ppc_hash64_pte_noexec_guard(PowerPCCPU *cpu,
                                              ppc_hash_pte64_t pte)
{
    return (pte.pte1 & HPTE64_R_N) || (pte.pte1 & HPTE64_R_G) ?
            PAGE_READ | PAGE_WRITE : PAGE_READ | PAGE_WRITE | PAGE_EXEC;
}

static int ppc_hash64_pte_prot(int mmu_idx,
                               ppc_slb_t *slb, ppc_hash_pte64_t pte)
{
    unsigned pp, key;
    int prot = 0;

    key = !!(mmuidx_pr(mmu_idx) ? (slb->vsid & SLB_VSID_KP)
             : (slb->vsid & SLB_VSID_KS));
    pp = (pte.pte1 & HPTE64_R_PP) | ((pte.pte1 & HPTE64_R_PP0) >> 61);

    if (key == 0) {
        switch (pp) {
        case 0x0:
        case 0x1:
        case 0x2:
            prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
            break;

        case 0x3:
        case 0x6:
            prot = PAGE_READ | PAGE_EXEC;
            break;
        }
    } else {
        switch (pp) {
        case 0x0:
        case 0x6:
            break;

        case 0x1:
        case 0x3:
            prot = PAGE_READ | PAGE_EXEC;
            break;

        case 0x2:
            prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
            break;
        }
    }

    return prot;
}

static int ppc_hash64_iamr_prot(PowerPCCPU *cpu, int key)
{
    CPUPPCState *env = &cpu->env;
    int iamr_bits = (env->spr[SPR_IAMR] >> 2 * (31 - key)) & 0x3;

    return (iamr_bits & 0x1) ? PAGE_READ | PAGE_WRITE :
                               PAGE_READ | PAGE_WRITE | PAGE_EXEC;
}

static int ppc_hash64_amr_prot(PowerPCCPU *cpu, ppc_hash_pte64_t pte)
{
    CPUPPCState *env = &cpu->env;
    int key, amrbits;
    int prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;

    if (!ppc_hash64_has(cpu, PPC_HASH64_AMR)) {
        return prot;
    }

    key = HPTE64_R_KEY(pte.pte1);
    amrbits = (env->spr[SPR_AMR] >> 2 * (31 - key)) & 0x3;

    if (amrbits & 0x2) {
        prot &= ~PAGE_WRITE;
    }
    if (amrbits & 0x1) {
        prot &= ~PAGE_READ;
    }

    switch (env->mmu_model) {
    case POWERPC_MMU_2_07:
    case POWERPC_MMU_3_00:
        prot &= ppc_hash64_iamr_prot(cpu, key);
        break;
    default:
        break;
    }

    return prot;
}

static hwaddr ppc_hash64_hpt_base(PowerPCCPU *cpu)
{
    uint64_t base;

    if (cpu->vhyp) {
        return 0;
    }
    if (cpu->env.mmu_model == POWERPC_MMU_3_00) {
        ppc_v3_pate_t pate;

        if (!ppc64_v3_get_pate(cpu, cpu->env.spr[SPR_LPIDR], &pate)) {
            return 0;
        }
        base = pate.dw0;
    } else {
        base = cpu->env.spr[SPR_SDR1];
    }
    return base & SDR_64_HTABORG;
}

static hwaddr ppc_hash64_hpt_mask(PowerPCCPU *cpu)
{
    uint64_t base;

    if (cpu->vhyp) {
        return cpu->vhyp_class->hpt_mask(cpu->vhyp);
    }
    if (cpu->env.mmu_model == POWERPC_MMU_3_00) {
        ppc_v3_pate_t pate;

        if (!ppc64_v3_get_pate(cpu, cpu->env.spr[SPR_LPIDR], &pate)) {
            return 0;
        }
        base = pate.dw0;
    } else {
        base = cpu->env.spr[SPR_SDR1];
    }
    return (1ULL << ((base & SDR_64_HTABSIZE) + 18 - 7)) - 1;
}

const ppc_hash_pte64_t *ppc_hash64_map_hptes(PowerPCCPU *cpu,
                                             hwaddr ptex, int n)
{
    hwaddr pte_offset = ptex * HASH_PTE_SIZE_64;
    hwaddr base;
    hwaddr plen = n * HASH_PTE_SIZE_64;
    const ppc_hash_pte64_t *hptes;

    if (cpu->vhyp) {
        return cpu->vhyp_class->map_hptes(cpu->vhyp, ptex, n);
    }
    base = ppc_hash64_hpt_base(cpu);

    if (!base) {
        return NULL;
    }

    hptes = address_space_map(CPU(cpu)->as, base + pte_offset, &plen, false,
                              MEMTXATTRS_UNSPECIFIED);
    if (plen < (n * HASH_PTE_SIZE_64)) {
        hw_error("%s: Unable to map all requested HPTEs\n", __func__);
    }
    return hptes;
}

void ppc_hash64_unmap_hptes(PowerPCCPU *cpu, const ppc_hash_pte64_t *hptes,
                            hwaddr ptex, int n)
{
    if (cpu->vhyp) {
        cpu->vhyp_class->unmap_hptes(cpu->vhyp, hptes, ptex, n);
        return;
    }

    address_space_unmap(CPU(cpu)->as, (void *)hptes, n * HASH_PTE_SIZE_64,
                        false, n * HASH_PTE_SIZE_64);
}

bool ppc_hash64_valid_ptex(PowerPCCPU *cpu, target_ulong ptex)
{
    if (((ptex & ~7ULL) / HPTES_PER_GROUP) & ~ppc_hash64_hpt_mask(cpu)) {
        return false;
    }
    return true;
}

static unsigned hpte_page_shift(const PPCHash64SegmentPageSizes *sps,
                                uint64_t pte0, uint64_t pte1)
{
    int i;

    if (!(pte0 & HPTE64_V_LARGE)) {
        if (sps->page_shift != 12) {
            return 0;
        }
        return 12;
    }

    for (i = 0; i < PPC_PAGE_SIZES_MAX_SZ; i++) {
        const PPCHash64PageSize *ps = &sps->enc[i];
        uint64_t mask;

        if (!ps->page_shift) {
            break;
        }

        if (ps->page_shift == 12) {
            continue;
        }

        mask = ((1ULL << ps->page_shift) - 1) & HPTE64_R_RPN;

        if ((pte1 & mask) == ((uint64_t)ps->pte_enc << HPTE64_R_RPN_SHIFT)) {
            return ps->page_shift;
        }
    }

    return 0;
}

static void ppc64_v3_new_to_old_hpte(target_ulong *pte0, target_ulong *pte1)
{
    *pte0 = (*pte0 & HPTE64_V_COMMON_BITS) |
            ((*pte1 & HPTE64_R_3_0_SSIZE_MASK) <<
             (HPTE64_V_SSIZE_SHIFT - HPTE64_R_3_0_SSIZE_SHIFT));

    *pte1 = *pte1 & ~HPTE64_R_3_0_SSIZE_MASK;
}


static hwaddr ppc_hash64_pteg_search(PowerPCCPU *cpu, hwaddr hash,
                                     const PPCHash64SegmentPageSizes *sps,
                                     target_ulong ptem,
                                     ppc_hash_pte64_t *pte, unsigned *pshift)
{
    int i;
    const ppc_hash_pte64_t *pteg;
    target_ulong pte0, pte1;
    target_ulong ptex;

    ptex = (hash & ppc_hash64_hpt_mask(cpu)) * HPTES_PER_GROUP;
    pteg = ppc_hash64_map_hptes(cpu, ptex, HPTES_PER_GROUP);
    if (!pteg) {
        return -1;
    }
    for (i = 0; i < HPTES_PER_GROUP; i++) {
        pte0 = ppc_hash64_hpte0(cpu, pteg, i);
        smp_rmb();
        pte1 = ppc_hash64_hpte1(cpu, pteg, i);

        if (cpu->env.mmu_model == POWERPC_MMU_3_00 && !cpu->vhyp) {
            ppc64_v3_new_to_old_hpte(&pte0, &pte1);
        }

        if (HPTE64_V_COMPARE(pte0, ptem)) {
            *pshift = hpte_page_shift(sps, pte0, pte1);
            if (*pshift == 0) {
                continue;
            }
            pte->pte0 = pte0;
            pte->pte1 = pte1;
            ppc_hash64_unmap_hptes(cpu, pteg, ptex, HPTES_PER_GROUP);
            return ptex + i;
        }
    }
    ppc_hash64_unmap_hptes(cpu, pteg, ptex, HPTES_PER_GROUP);
    return -1;
}

static hwaddr ppc_hash64_htab_lookup(PowerPCCPU *cpu,
                                     ppc_slb_t *slb, target_ulong eaddr,
                                     ppc_hash_pte64_t *pte, unsigned *pshift)
{
    CPUPPCState *env = &cpu->env;
    hwaddr hash, ptex;
    uint64_t vsid, epnmask, epn, ptem;
    const PPCHash64SegmentPageSizes *sps = slb->sps;

    assert(sps);

    if (env->spr[SPR_LPCR] & LPCR_ISL) {
        sps = &cpu->hash64_opts->sps[0];
        assert(sps->page_shift == 12);
    }

    epnmask = ~((1ULL << sps->page_shift) - 1);

    if (slb->vsid & SLB_VSID_B) {
        vsid = (slb->vsid & SLB_VSID_VSID) >> SLB_VSID_SHIFT_1T;
        epn = (eaddr & ~SEGMENT_MASK_1T) & epnmask;
        hash = vsid ^ (vsid << 25) ^ (epn >> sps->page_shift);
    } else {
        vsid = (slb->vsid & SLB_VSID_VSID) >> SLB_VSID_SHIFT;
        epn = (eaddr & ~SEGMENT_MASK_256M) & epnmask;
        hash = vsid ^ (epn >> sps->page_shift);
    }
    ptem = (slb->vsid & SLB_VSID_PTEM) | ((epn >> 16) & HPTE64_V_AVPN);
    ptem |= HPTE64_V_VALID;

    qemu_log_mask(CPU_LOG_MMU,
            "htab_base " HWADDR_FMT_plx " htab_mask " HWADDR_FMT_plx
            " hash " HWADDR_FMT_plx "\n",
            ppc_hash64_hpt_base(cpu), ppc_hash64_hpt_mask(cpu), hash);

    qemu_log_mask(CPU_LOG_MMU,
            "0 htab=" HWADDR_FMT_plx "/" HWADDR_FMT_plx
            " vsid=" TARGET_FMT_lx " ptem=" TARGET_FMT_lx
            " hash=" HWADDR_FMT_plx "\n",
            ppc_hash64_hpt_base(cpu), ppc_hash64_hpt_mask(cpu),
            vsid, ptem,  hash);
    ptex = ppc_hash64_pteg_search(cpu, hash, sps, ptem, pte, pshift);

    if (ptex == -1) {
        ptem |= HPTE64_V_SECONDARY;
        qemu_log_mask(CPU_LOG_MMU,
                "1 htab=" HWADDR_FMT_plx "/" HWADDR_FMT_plx
                " vsid=" TARGET_FMT_lx " api=" TARGET_FMT_lx
                " hash=" HWADDR_FMT_plx "\n", ppc_hash64_hpt_base(cpu),
                ppc_hash64_hpt_mask(cpu), vsid, ptem, ~hash);

        ptex = ppc_hash64_pteg_search(cpu, ~hash, sps, ptem, pte, pshift);
    }

    return ptex;
}

unsigned ppc_hash64_hpte_page_shift_noslb(PowerPCCPU *cpu,
                                          uint64_t pte0, uint64_t pte1)
{
    int i;

    if (!(pte0 & HPTE64_V_LARGE)) {
        return 12;
    }

    for (i = 0; i < PPC_PAGE_SIZES_MAX_SZ; i++) {
        const PPCHash64SegmentPageSizes *sps = &cpu->hash64_opts->sps[i];
        unsigned shift;

        if (!sps->page_shift) {
            break;
        }

        shift = hpte_page_shift(sps, pte0, pte1);
        if (shift) {
            return shift;
        }
    }

    return 0;
}

static bool ppc_hash64_use_vrma(CPUPPCState *env)
{
    switch (env->mmu_model) {
    case POWERPC_MMU_3_00:
        return true;

    default:
        return !!(env->spr[SPR_LPCR] & LPCR_VPM0);
    }
}

static void ppc_hash64_set_isi(CPUState *cs, int mmu_idx, uint64_t slb_vsid,
                               uint64_t error_code)
{
    CPUPPCState *env = &POWERPC_CPU(cs)->env;
    bool vpm;

    if (!mmuidx_real(mmu_idx)) {
        vpm = !!(env->spr[SPR_LPCR] & LPCR_VPM1);
    } else {
        vpm = ppc_hash64_use_vrma(env);
    }
    if (vpm && !mmuidx_hv(mmu_idx)) {
        cs->exception_index = POWERPC_EXCP_HISI;
        env->spr[SPR_ASDR] = slb_vsid;
    } else {
        cs->exception_index = POWERPC_EXCP_ISI;
    }
    env->error_code = error_code;
}

static void ppc_hash64_set_dsi(CPUState *cs, int mmu_idx, uint64_t slb_vsid,
                               uint64_t dar, uint64_t dsisr)
{
    CPUPPCState *env = &POWERPC_CPU(cs)->env;
    bool vpm;

    if (!mmuidx_real(mmu_idx)) {
        vpm = !!(env->spr[SPR_LPCR] & LPCR_VPM1);
    } else {
        vpm = ppc_hash64_use_vrma(env);
    }
    if (vpm && !mmuidx_hv(mmu_idx)) {
        cs->exception_index = POWERPC_EXCP_HDSI;
        env->spr[SPR_HDAR] = dar;
        env->spr[SPR_HDSISR] = dsisr;
        env->spr[SPR_ASDR] = slb_vsid;
    } else {
        cs->exception_index = POWERPC_EXCP_DSI;
        env->spr[SPR_DAR] = dar;
        env->spr[SPR_DSISR] = dsisr;
   }
    env->error_code = 0;
}


static void ppc_hash64_set_r(PowerPCCPU *cpu, hwaddr ptex, uint64_t pte1)
{
    hwaddr base, offset = ptex * HASH_PTE_SIZE_64 + HPTE64_DW1_R;

    if (cpu->vhyp) {
        cpu->vhyp_class->hpte_set_r(cpu->vhyp, ptex, pte1);
        return;
    }
    base = ppc_hash64_hpt_base(cpu);


    stb_phys(CPU(cpu)->as, base + offset, ((pte1 >> 8) & 0xff) | 0x01);
}

static void ppc_hash64_set_c(PowerPCCPU *cpu, hwaddr ptex, uint64_t pte1)
{
    hwaddr base, offset = ptex * HASH_PTE_SIZE_64 + HPTE64_DW1_C;

    if (cpu->vhyp) {
        cpu->vhyp_class->hpte_set_c(cpu->vhyp, ptex, pte1);
        return;
    }
    base = ppc_hash64_hpt_base(cpu);

    stb_phys(CPU(cpu)->as, base + offset, (pte1 & 0xff) | 0x80);
}

static target_ulong rmls_limit(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;
    static const target_ulong rma_sizes[16] = {
        [0] = 256 * GiB,
        [1] = 16 * GiB,
        [2] = 1 * GiB,
        [3] = 64 * MiB,
        [4] = 256 * MiB,
        [7] = 128 * MiB,
        [8] = 32 * MiB,
    };
    target_ulong rmls = (env->spr[SPR_LPCR] & LPCR_RMLS) >> LPCR_RMLS_SHIFT;

    return rma_sizes[rmls];
}

static uint64_t get_vrma_llp(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;
    uint64_t llp;

    if (env->mmu_model == POWERPC_MMU_3_00) {
        ppc_v3_pate_t pate;
        uint64_t ps, l, lp;

        if (!ppc64_v3_get_pate(cpu, cpu->env.spr[SPR_LPIDR], &pate)) {
            error_report("Bad VRMA with no partition table entry");
            return 0;
        }
        ps = PATE0_GET_PS(pate.dw0);
        l = (ps >> 2) & 0x1;
        lp = ps & 0x3;
        llp = (l << SLB_VSID_L_SHIFT) | (lp << SLB_VSID_LP_SHIFT);

    } else {
        uint64_t lpcr = env->spr[SPR_LPCR];
        target_ulong vrmasd = (lpcr & LPCR_VRMASD) >> LPCR_VRMASD_SHIFT;

        llp = (vrmasd << SLB_VSID_LP_SHIFT) & SLB_VSID_LLP_MASK;
    }

    return llp;
}

static int build_vrma_slbe(PowerPCCPU *cpu, ppc_slb_t *slb)
{
    uint64_t llp = get_vrma_llp(cpu);
    target_ulong vsid = SLB_VSID_VRMA | llp;
    int i;

    for (i = 0; i < PPC_PAGE_SIZES_MAX_SZ; i++) {
        const PPCHash64SegmentPageSizes *sps = &cpu->hash64_opts->sps[i];

        if (!sps->page_shift) {
            break;
        }

        if ((vsid & SLB_VSID_LLP_MASK) == sps->slb_enc) {
            slb->esid = SLB_ESID_V;
            slb->vsid = vsid;
            slb->sps = sps;
            return 0;
        }
    }

    error_report("Bad VRMA page size encoding 0x" TARGET_FMT_lx, llp);

    return -1;
}

bool ppc_hash64_xlate(PowerPCCPU *cpu, vaddr eaddr, MMUAccessType access_type,
                      hwaddr *raddrp, int *psizep, int *protp, int mmu_idx,
                      bool guest_visible)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    ppc_slb_t vrma_slbe;
    ppc_slb_t *slb;
    unsigned apshift;
    hwaddr ptex;
    ppc_hash_pte64_t pte;
    int exec_prot, pp_prot, amr_prot, prot;
    int need_prot;
    hwaddr raddr;
    bool vrma = false;

    if (mmuidx_real(mmu_idx)) {
        raddr = eaddr & 0x0FFFFFFFFFFFFFFFULL;

        if (cpu->vhyp) {
        } else if (mmuidx_hv(mmu_idx) || !env->has_hv_mode) {
            if (!(eaddr >> 63)) {
                raddr |= env->spr[SPR_HRMOR];
            }
        } else if (ppc_hash64_use_vrma(env)) {
            vrma = true;
            slb = &vrma_slbe;
            if (build_vrma_slbe(cpu, slb) != 0) {
                if (guest_visible) {
                    cs->exception_index = POWERPC_EXCP_MCHECK;
                    env->error_code = 0;
                }
                return false;
            }

            goto skip_slb_search;
        } else {
            target_ulong limit = rmls_limit(cpu);

            if (raddr >= limit) {
                if (!guest_visible) {
                    return false;
                }
                switch (access_type) {
                case MMU_INST_FETCH:
                    ppc_hash64_set_isi(cs, mmu_idx, 0, SRR1_PROTFAULT);
                    break;
                case MMU_DATA_LOAD:
                    ppc_hash64_set_dsi(cs, mmu_idx, 0, eaddr, DSISR_PROTFAULT);
                    break;
                case MMU_DATA_STORE:
                    ppc_hash64_set_dsi(cs, mmu_idx, 0, eaddr,
                                       DSISR_PROTFAULT | DSISR_ISSTORE);
                    break;
                default:
                    g_assert_not_reached();
                }
                return false;
            }

            raddr |= env->spr[SPR_RMOR];
        }

        *raddrp = raddr;
        *protp = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        *psizep = TARGET_PAGE_BITS;
        return true;
    }

    slb = slb_lookup(cpu, eaddr);
    if (!slb) {
        if (ppc64_use_proc_tbl(cpu)) {
            error_report("Segment Table Support Unimplemented");
            exit(1);
        }
        if (!guest_visible) {
            return false;
        }
        switch (access_type) {
        case MMU_INST_FETCH:
            cs->exception_index = POWERPC_EXCP_ISEG;
            env->error_code = 0;
            break;
        case MMU_DATA_LOAD:
        case MMU_DATA_STORE:
            cs->exception_index = POWERPC_EXCP_DSEG;
            env->error_code = 0;
            env->spr[SPR_DAR] = eaddr;
            break;
        default:
            g_assert_not_reached();
        }
        return false;
    }

 skip_slb_search:

    if (access_type == MMU_INST_FETCH && (slb->vsid & SLB_VSID_N)) {
        if (guest_visible) {
            ppc_hash64_set_isi(cs, mmu_idx, slb->vsid, SRR1_NOEXEC_GUARD);
        }
        return false;
    }

    ptex = ppc_hash64_htab_lookup(cpu, slb, eaddr, &pte, &apshift);
    if (ptex == -1) {
        if (!guest_visible) {
            return false;
        }
        switch (access_type) {
        case MMU_INST_FETCH:
            ppc_hash64_set_isi(cs, mmu_idx, slb->vsid, SRR1_NOPTE);
            break;
        case MMU_DATA_LOAD:
            ppc_hash64_set_dsi(cs, mmu_idx, slb->vsid, eaddr, DSISR_NOPTE);
            break;
        case MMU_DATA_STORE:
            ppc_hash64_set_dsi(cs, mmu_idx, slb->vsid, eaddr,
                               DSISR_NOPTE | DSISR_ISSTORE);
            break;
        default:
            g_assert_not_reached();
        }
        return false;
    }
    qemu_log_mask(CPU_LOG_MMU,
                  "found PTE at index %08" HWADDR_PRIx "\n", ptex);

    exec_prot = ppc_hash64_pte_noexec_guard(cpu, pte);
    pp_prot = ppc_hash64_pte_prot(mmu_idx, slb, pte);
    if (vrma) {
        amr_prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
    } else {
        amr_prot = ppc_hash64_amr_prot(cpu, pte);
    }
    prot = exec_prot & pp_prot & amr_prot;

    need_prot = check_prot_access_type(PAGE_RWX, access_type);
    if (need_prot & ~prot) {
        qemu_log_mask(CPU_LOG_MMU, "PTE access rejected\n");
        if (!guest_visible) {
            return false;
        }
        if (access_type == MMU_INST_FETCH) {
            int srr1 = 0;
            if (PAGE_EXEC & ~exec_prot) {
                srr1 |= SRR1_NOEXEC_GUARD;
            } else if (PAGE_EXEC & ~pp_prot) {
                srr1 |= SRR1_PROTFAULT;
            }
            if (PAGE_EXEC & ~amr_prot) {
                srr1 |= SRR1_IAMR;
            }
            ppc_hash64_set_isi(cs, mmu_idx, slb->vsid, srr1);
        } else {
            int dsisr = 0;
            if (need_prot & ~pp_prot) {
                dsisr |= DSISR_PROTFAULT;
            }
            if (access_type == MMU_DATA_STORE) {
                dsisr |= DSISR_ISSTORE;
            }
            if (need_prot & ~amr_prot) {
                dsisr |= DSISR_AMR;
            }
            ppc_hash64_set_dsi(cs, mmu_idx, slb->vsid, eaddr, dsisr);
        }
        return false;
    }

    qemu_log_mask(CPU_LOG_MMU, "PTE access granted !\n");

    if (!(pte.pte1 & HPTE64_R_R)) {
        ppc_hash64_set_r(cpu, ptex, pte.pte1);
    }
    if (!(pte.pte1 & HPTE64_R_C)) {
        if (access_type == MMU_DATA_STORE) {
            ppc_hash64_set_c(cpu, ptex, pte.pte1);
        } else {
            prot &= ~PAGE_WRITE;
        }
    }

    *raddrp = deposit64(pte.pte1 & HPTE64_R_RPN, 0, apshift, eaddr);
    *protp = prot;
    *psizep = apshift;
    return true;
}

void ppc_hash64_tlb_flush_hpte(PowerPCCPU *cpu, target_ulong ptex,
                               target_ulong pte0, target_ulong pte1)
{
    cpu->env.tlb_need_flush = TLB_NEED_GLOBAL_FLUSH | TLB_NEED_LOCAL_FLUSH;
}

#ifdef CONFIG_TCG
void helper_store_lpcr(CPUPPCState *env, target_ulong val)
{
    PowerPCCPU *cpu = env_archcpu(env);

    ppc_store_lpcr(cpu, val);
}
#endif

void ppc_hash64_init(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);

    if (!pcc->hash64_opts) {
        assert(!mmu_is_64bit(env->mmu_model));
        return;
    }

    cpu->hash64_opts = g_memdup2(pcc->hash64_opts, sizeof(*cpu->hash64_opts));
}

void ppc_hash64_finalize(PowerPCCPU *cpu)
{
    g_free(cpu->hash64_opts);
}

const PPCHash64Options ppc_hash64_opts_basic = {
    .flags = 0,
    .slb_size = 64,
    .sps = {
        { .page_shift = 12,
          .slb_enc = 0,
          .enc = { { .page_shift = 12, .pte_enc = 0 } }
        },
        { .page_shift = 24,
          .slb_enc = 0x100,
          .enc = { { .page_shift = 24, .pte_enc = 0 } }
        },
    },
};

const PPCHash64Options ppc_hash64_opts_POWER7 = {
    .flags = PPC_HASH64_1TSEG | PPC_HASH64_AMR | PPC_HASH64_CI_LARGEPAGE,
    .slb_size = 32,
    .sps = {
        {
            .page_shift = 12,
            .slb_enc = 0,
            .enc = { { .page_shift = 12, .pte_enc = 0 },
                     { .page_shift = 16, .pte_enc = 0x7 },
                     { .page_shift = 24, .pte_enc = 0x38 }, },
        },
        {
            .page_shift = 16,
            .slb_enc = SLB_VSID_64K,
            .enc = { { .page_shift = 16, .pte_enc = 0x1 },
                     { .page_shift = 24, .pte_enc = 0x8 }, },
        },
        {
            .page_shift = 24,
            .slb_enc = SLB_VSID_16M,
            .enc = { { .page_shift = 24, .pte_enc = 0 }, },
        },
        {
            .page_shift = 34,
            .slb_enc = SLB_VSID_16G,
            .enc = { { .page_shift = 34, .pte_enc = 0x3 }, },
        },
    }
};


