/*
 * Helpers for loads and stores
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
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
#include "qemu/log.h"
#include "qemu/range.h"
#include "cpu.h"
#include "tcg/tcg.h"
#include "exec/helper-proto.h"
#include "exec/cputlb.h"
#include "exec/page-protection.h"
#include "exec/target_page.h"
#include "accel/tcg/cpu-ldst.h"
#include "system/memory.h"
#ifdef CONFIG_USER_ONLY
#include "user/page-protection.h"
#endif
#include "asi.h"

#ifdef DEBUG_MMU
#define DPRINTF_MMU(fmt, ...)                                   \
    do { printf("MMU: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF_MMU(fmt, ...) do {} while (0)
#endif

#ifdef DEBUG_MXCC
#define DPRINTF_MXCC(fmt, ...)                                  \
    do { printf("MXCC: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF_MXCC(fmt, ...) do {} while (0)
#endif

#ifdef DEBUG_ASI
#define DPRINTF_ASI(fmt, ...)                                   \
    do { printf("ASI: " fmt , ## __VA_ARGS__); } while (0)
#endif

#ifdef DEBUG_CACHE_CONTROL
#define DPRINTF_CACHE_CONTROL(fmt, ...)                                 \
    do { printf("CACHE_CONTROL: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF_CACHE_CONTROL(fmt, ...) do {} while (0)
#endif

#ifdef TARGET_SPARC64
#ifndef TARGET_ABI32
#define AM_CHECK(env1) ((env1)->pstate & PS_AM)
#else
#define AM_CHECK(env1) (1)
#endif
#endif

#if defined(TARGET_SPARC64) && !defined(CONFIG_USER_ONLY)
static uint64_t ultrasparc_tsb_pointer(CPUSPARCState *env,
                                       const SparcV9MMU *mmu, const int idx)
{
    uint64_t tsb_register;
    int page_size;
    if (cpu_has_hypervisor(env)) {
        int tsb_index = 0;
        int ctx = mmu->tag_access & 0x1fffULL;
        uint64_t ctx_register = mmu->sun4v_ctx_config[ctx ? 1 : 0];
        tsb_index = idx;
        tsb_index |= ctx ? 2 : 0;
        page_size = idx ? ctx_register >> 8 : ctx_register;
        page_size &= 7;
        tsb_register = mmu->sun4v_tsb_pointers[tsb_index];
    } else {
        page_size = idx;
        tsb_register = mmu->tsb;
    }
    int tsb_split = (tsb_register & 0x1000ULL) ? 1 : 0;
    int tsb_size  = tsb_register & 0xf;

    uint64_t tsb_base_mask = (~0x1fffULL) << tsb_size;

    uint64_t va = mmu->tag_access >> (3 * page_size + 9);

    if (tsb_split) {
        if (idx == 0) {
            va &= ~(1ULL << (13 + tsb_size));
        } else {
            va |= (1ULL << (13 + tsb_size));
        }
        tsb_base_mask <<= 1;
    }

    return ((tsb_register & tsb_base_mask) | (va & ~tsb_base_mask)) & ~0xfULL;
}

static uint64_t ultrasparc_tag_target(uint64_t tag_access_register)
{
    return ((tag_access_register & 0x1fff) << 48) | (tag_access_register >> 22);
}

static void replace_tlb_entry(SparcTLBEntry *tlb,
                              uint64_t tlb_tag, uint64_t tlb_tte,
                              CPUSPARCState *env)
{
    target_ulong mask, size, va, offset;

    if (TTE_IS_VALID(tlb->tte)) {
        CPUState *cs = env_cpu(env);

        size = 8192ULL << 3 * TTE_PGSIZE(tlb->tte);
        mask = 1ULL + ~size;

        va = tlb->tag & mask;

        for (offset = 0; offset < size; offset += TARGET_PAGE_SIZE) {
            tlb_flush_page(cs, va + offset);
        }
    }

    tlb->tag = tlb_tag;
    tlb->tte = tlb_tte;
}

static void demap_tlb(SparcTLBEntry *tlb, target_ulong demap_addr,
                      const char *strmmu, CPUSPARCState *env1)
{
    unsigned int i;
    target_ulong mask;
    uint64_t context;

    int is_demap_context = (demap_addr >> 6) & 1;

    switch ((demap_addr >> 4) & 3) {
    case 0:
        context = env1->dmmu.mmu_primary_context;
        break;
    case 1:
        context = env1->dmmu.mmu_secondary_context;
        break;
    case 2:
        context = 0;
        break;
    case 3:
    default:
        return;
    }

    for (i = 0; i < 64; i++) {
        if (TTE_IS_VALID(tlb[i].tte)) {

            if (is_demap_context) {
                if (TTE_IS_GLOBAL(tlb[i].tte) ||
                    !tlb_compare_context(&tlb[i], context)) {
                    continue;
                }
            } else {
                mask = 0xffffffffffffe000ULL;
                mask <<= 3 * ((tlb[i].tte >> 61) & 3);

                if (!compare_masked(demap_addr, tlb[i].tag, mask)) {
                    continue;
                }

                if (!TTE_IS_GLOBAL(tlb[i].tte) &&
                    !tlb_compare_context(&tlb[i], context)) {
                    continue;
                }
            }

            replace_tlb_entry(&tlb[i], 0, 0, env1);
#ifdef DEBUG_MMU
            DPRINTF_MMU("%s demap invalidated entry [%02u]\n", strmmu, i);
            dump_mmu(env1);
#endif
        }
    }
}

static uint64_t sun4v_tte_to_sun4u(CPUSPARCState *env, uint64_t tag,
                                   uint64_t sun4v_tte)
{
    uint64_t sun4u_tte;
    if (!(cpu_has_hypervisor(env) && (tag & TLB_UST1_IS_SUN4V_BIT))) {
        return sun4v_tte;
    }
    sun4u_tte = TTE_PA(sun4v_tte) | (sun4v_tte & TTE_VALID_BIT);
    sun4u_tte |= (sun4v_tte & 3ULL) << 61;
    sun4u_tte |= CONVERT_BIT(sun4v_tte, TTE_NFO_BIT_UA2005, TTE_NFO_BIT);
    sun4u_tte |= CONVERT_BIT(sun4v_tte, TTE_USED_BIT_UA2005, TTE_USED_BIT);
    sun4u_tte |= CONVERT_BIT(sun4v_tte, TTE_W_OK_BIT_UA2005, TTE_W_OK_BIT);
    sun4u_tte |= CONVERT_BIT(sun4v_tte, TTE_SIDEEFFECT_BIT_UA2005,
                             TTE_SIDEEFFECT_BIT);
    sun4u_tte |= CONVERT_BIT(sun4v_tte, TTE_PRIV_BIT_UA2005, TTE_PRIV_BIT);
    sun4u_tte |= CONVERT_BIT(sun4v_tte, TTE_LOCKED_BIT_UA2005, TTE_LOCKED_BIT);
    return sun4u_tte;
}

static void replace_tlb_1bit_lru(SparcTLBEntry *tlb,
                                 uint64_t tlb_tag, uint64_t tlb_tte,
                                 const char *strmmu, CPUSPARCState *env1,
                                 uint64_t addr)
{
    unsigned int i, replace_used;

    tlb_tte = sun4v_tte_to_sun4u(env1, addr, tlb_tte);
    if (cpu_has_hypervisor(env1)) {
        uint64_t new_vaddr = tlb_tag & ~0x1fffULL;
        uint64_t new_size = 8192ULL << 3 * TTE_PGSIZE(tlb_tte);
        uint32_t new_ctx = tlb_tag & 0x1fffU;
        for (i = 0; i < 64; i++) {
            uint32_t ctx = tlb[i].tag & 0x1fffU;
            if (new_ctx == ctx) {
                uint64_t vaddr = tlb[i].tag & ~0x1fffULL;
                uint64_t size = 8192ULL << 3 * TTE_PGSIZE(tlb[i].tte);
                if (ranges_overlap(new_vaddr, new_size, vaddr, size)) {
                    DPRINTF_MMU("auto demap entry [%d] %lx->%lx\n", i, vaddr,
                                new_vaddr);
                    replace_tlb_entry(&tlb[i], tlb_tag, tlb_tte, env1);
                    return;
                }
            }

        }
    }
    for (i = 0; i < 64; i++) {
        if (!TTE_IS_VALID(tlb[i].tte)) {
            replace_tlb_entry(&tlb[i], tlb_tag, tlb_tte, env1);
#ifdef DEBUG_MMU
            DPRINTF_MMU("%s lru replaced invalid entry [%i]\n", strmmu, i);
            dump_mmu(env1);
#endif
            return;
        }
    }

    for (replace_used = 0; replace_used < 2; ++replace_used) {

        for (i = 0; i < 64; i++) {
            if (!TTE_IS_LOCKED(tlb[i].tte) && !TTE_IS_USED(tlb[i].tte)) {

                replace_tlb_entry(&tlb[i], tlb_tag, tlb_tte, env1);
#ifdef DEBUG_MMU
                DPRINTF_MMU("%s lru replaced unlocked %s entry [%i]\n",
                            strmmu, (replace_used ? "used" : "unused"), i);
                dump_mmu(env1);
#endif
                return;
            }
        }

        for (i = 0; i < 64; i++) {
            TTE_SET_UNUSED(tlb[i].tte);
        }
    }

#ifdef DEBUG_MMU
    DPRINTF_MMU("%s lru replacement: no free entries available, "
                "replacing the last one\n", strmmu);
#endif
    replace_tlb_entry(&tlb[63], tlb_tag, tlb_tte, env1);
}

#endif

#ifdef TARGET_SPARC64
static inline int is_translating_asi(int asi)
{
    switch (asi) {
    case 0x04 ... 0x11:
    case 0x16 ... 0x19:
    case 0x1E ... 0x1F:
    case 0x24 ... 0x2C:
    case 0x70 ... 0x73:
    case 0x78 ... 0x79:
    case 0x80 ... 0xFF:
        return 1;

    default:
        return 0;
    }
}

static inline target_ulong address_mask(CPUSPARCState *env1, target_ulong addr)
{
    if (AM_CHECK(env1)) {
        addr &= 0xffffffffULL;
    }
    return addr;
}

static inline target_ulong asi_address_mask(CPUSPARCState *env,
                                            int asi, target_ulong addr)
{
    if (is_translating_asi(asi)) {
        addr = address_mask(env, addr);
    }
    return addr;
}

#ifndef CONFIG_USER_ONLY
static inline void do_check_asi(CPUSPARCState *env, int asi, uintptr_t ra)
{
    if (asi < 0x80
        && !cpu_hypervisor_mode(env)
        && (!cpu_supervisor_mode(env)
            || (asi >= 0x30 && cpu_has_hypervisor(env)))) {
        cpu_raise_exception_ra(env, TT_PRIV_ACT, ra);
    }
}
#endif
#endif

#if defined(TARGET_SPARC64) || !defined(CONFIG_USER_ONLY)
static void do_check_align(CPUSPARCState *env, target_ulong addr,
                           uint32_t align, uintptr_t ra)
{
    if (addr & align) {
        cpu_raise_exception_ra(env, TT_UNALIGNED, ra);
    }
}
#endif

#if !defined(TARGET_SPARC64) && !defined(CONFIG_USER_ONLY) &&   \
    defined(DEBUG_MXCC)
static void dump_mxcc(CPUSPARCState *env)
{
    printf("mxccdata: %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64
           "\n",
           env->mxccdata[0], env->mxccdata[1],
           env->mxccdata[2], env->mxccdata[3]);
    printf("mxccregs: %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64
           "\n"
           "          %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64
           "\n",
           env->mxccregs[0], env->mxccregs[1],
           env->mxccregs[2], env->mxccregs[3],
           env->mxccregs[4], env->mxccregs[5],
           env->mxccregs[6], env->mxccregs[7]);
}
#endif

#if (defined(TARGET_SPARC64) || !defined(CONFIG_USER_ONLY))     \
    && defined(DEBUG_ASI)
static void dump_asi(const char *txt, target_ulong addr, int asi, int size,
                     uint64_t r1)
{
    switch (size) {
    case 1:
        DPRINTF_ASI("%s "TARGET_FMT_lx " asi 0x%02x = %02" PRIx64 "\n", txt,
                    addr, asi, r1 & 0xff);
        break;
    case 2:
        DPRINTF_ASI("%s "TARGET_FMT_lx " asi 0x%02x = %04" PRIx64 "\n", txt,
                    addr, asi, r1 & 0xffff);
        break;
    case 4:
        DPRINTF_ASI("%s "TARGET_FMT_lx " asi 0x%02x = %08" PRIx64 "\n", txt,
                    addr, asi, r1 & 0xffffffff);
        break;
    case 8:
        DPRINTF_ASI("%s "TARGET_FMT_lx " asi 0x%02x = %016" PRIx64 "\n", txt,
                    addr, asi, r1);
        break;
    }
}
#endif

#ifndef CONFIG_USER_ONLY
#ifndef TARGET_SPARC64
static void sparc_raise_mmu_fault(CPUState *cs, hwaddr addr,
                                  bool is_write, bool is_exec, int is_asi,
                                  unsigned size, uintptr_t retaddr)
{
    CPUSPARCState *env = cpu_env(cs);
    int fault_type;

#ifdef DEBUG_UNASSIGNED
    if (is_asi) {
        printf("Unassigned mem %s access of %d byte%s to " HWADDR_FMT_plx
               " asi 0x%02x from " TARGET_FMT_lx "\n",
               is_exec ? "exec" : is_write ? "write" : "read", size,
               size == 1 ? "" : "s", addr, is_asi, env->pc);
    } else {
        printf("Unassigned mem %s access of %d byte%s to " HWADDR_FMT_plx
               " from " TARGET_FMT_lx "\n",
               is_exec ? "exec" : is_write ? "write" : "read", size,
               size == 1 ? "" : "s", addr, env->pc);
    }
#endif
    fault_type = (env->mmuregs[3] & 0x1c) >> 2;
    if ((fault_type > 4) || (fault_type == 0)) {
        env->mmuregs[3] = 0;
        if (is_asi) {
            env->mmuregs[3] |= 1 << 16;
        }
        if (env->psrs) {
            env->mmuregs[3] |= 1 << 5;
        }
        if (is_exec) {
            env->mmuregs[3] |= 1 << 6;
        }
        if (is_write) {
            env->mmuregs[3] |= 1 << 7;
        }
        env->mmuregs[3] |= (5 << 2) | 2;
        if (!is_exec) {
            env->mmuregs[4] = addr;
        }
    }
    if (fault_type == ((env->mmuregs[3] & 0x1c)) >> 2) {
        env->mmuregs[3] |= 1;
    }

    if ((env->mmuregs[0] & MMU_E) && !(env->mmuregs[0] & MMU_NF)) {
        int tt = is_exec ? TT_CODE_ACCESS : TT_DATA_ACCESS;
        cpu_raise_exception_ra(env, tt, retaddr);
    }

    if (env->mmuregs[0] & MMU_NF) {
        tlb_flush(cs);
    }
}
#else
static void sparc_raise_mmu_fault(CPUState *cs, hwaddr addr,
                                  bool is_write, bool is_exec, int is_asi,
                                  unsigned size, uintptr_t retaddr)
{
    CPUSPARCState *env = cpu_env(cs);

#ifdef DEBUG_UNASSIGNED
    printf("Unassigned mem access to " HWADDR_FMT_plx " from " TARGET_FMT_lx
           "\n", addr, env->pc);
#endif

    if (is_exec) {
        if (env->lsu & (IMMU_E)) {
            cpu_raise_exception_ra(env, TT_CODE_ACCESS, retaddr);
        } else if (cpu_has_hypervisor(env) && !(env->hpstate & HS_PRIV)) {
            cpu_raise_exception_ra(env, TT_INSN_REAL_TRANSLATION_MISS, retaddr);
        }
    } else {
        if (env->lsu & (DMMU_E)) {
            cpu_raise_exception_ra(env, TT_DATA_ACCESS, retaddr);
        } else if (cpu_has_hypervisor(env) && !(env->hpstate & HS_PRIV)) {
            cpu_raise_exception_ra(env, TT_DATA_REAL_TRANSLATION_MISS, retaddr);
        }
    }
}
#endif
#endif

#ifndef TARGET_SPARC64
#ifndef CONFIG_USER_ONLY


static void leon3_cache_control_st(CPUSPARCState *env, target_ulong addr,
                                   uint64_t val, int size)
{
    DPRINTF_CACHE_CONTROL("st addr:%08x, val:%" PRIx64 ", size:%d\n",
                          addr, val, size);

    if (size != 4) {
        DPRINTF_CACHE_CONTROL("32bits only\n");
        return;
    }

    switch (addr) {
    case 0x00:

        val &= ~CACHE_CTRL_FD;
        val &= ~CACHE_CTRL_FI;
        val &= ~CACHE_CTRL_IB;
        val &= ~CACHE_CTRL_IP;
        val &= ~CACHE_CTRL_DP;

        env->cache_control = val;
        break;
    case 0x04:
    case 0x08:
        break;
    default:
        DPRINTF_CACHE_CONTROL("write unknown register %08x\n", addr);
        break;
    };
}

static uint64_t leon3_cache_control_ld(CPUSPARCState *env, target_ulong addr,
                                       int size)
{
    uint64_t ret = 0;

    if (size != 4) {
        DPRINTF_CACHE_CONTROL("32bits only\n");
        return 0;
    }

    switch (addr) {
    case 0x00:
        ret = env->cache_control;
        break;

    case 0x04:
        ret = 0x10220000;
        break;
    case 0x08:
        ret = 0x18220000;
        break;
    default:
        DPRINTF_CACHE_CONTROL("read unknown register %08x\n", addr);
        break;
    };
    DPRINTF_CACHE_CONTROL("ld addr:%08x, ret:0x%" PRIx64 ", size:%d\n",
                          addr, ret, size);
    return ret;
}

uint64_t helper_ld_asi(CPUSPARCState *env, target_ulong addr,
                       int asi, uint32_t memop)
{
    int size = 1 << (memop & MO_SIZE);
    int sign = memop & MO_SIGN;
    CPUState *cs = env_cpu(env);
    uint64_t ret = 0;
#if defined(DEBUG_MXCC) || defined(DEBUG_ASI)
    uint32_t last_addr = addr;
#endif

    do_check_align(env, addr, size - 1, GETPC());
    switch (asi) {
    case ASI_M_MXCC:
        switch (addr) {
        case 0x00:
        case 0x08:
        case 0x0C:
            if (env->def.features & CPU_FEATURE_CACHE_CTRL) {
                ret = leon3_cache_control_ld(env, addr, size);
            } else {
                qemu_log_mask(LOG_UNIMP, "0x" TARGET_FMT_lx ": unimplemented"
                                         " address, size: %d\n", addr, size);
            }
            break;
        case 0x01c00a00:
            if (size == 8) {
                ret = env->mxccregs[3];
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }
            break;
        case 0x01c00a04:
            if (size == 4) {
                ret = env->mxccregs[3];
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }
            break;
        case 0x01c00c00:
            if (size == 8) {
                ret = env->mxccregs[5];
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }
            break;
        case 0x01c00f00:
            if (size == 8) {
                ret = env->mxccregs[7];
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }
            break;
        default:
            qemu_log_mask(LOG_UNIMP,
                          "%08x: unimplemented address, size: %d\n", addr,
                          size);
            break;
        }
        DPRINTF_MXCC("asi = %d, size = %d, sign = %d, "
                     "addr = %08x -> ret = %" PRIx64 ","
                     "addr = %08x\n", asi, size, sign, last_addr, ret, addr);
#ifdef DEBUG_MXCC
        dump_mxcc(env);
#endif
        break;
    case ASI_M_FLUSH_PROBE:
    case ASI_LEON_MMUFLUSH:
        {
            int mmulev;

            mmulev = (addr >> 8) & 15;
            if (mmulev > 4) {
                ret = 0;
            } else {
                ret = mmu_probe(env, addr, mmulev);
            }
            DPRINTF_MMU("mmu_probe: 0x%08x (lev %d) -> 0x%08" PRIx64 "\n",
                        addr, mmulev, ret);
        }
        break;
    case ASI_M_MMUREGS:
    case ASI_LEON_MMUREGS:
        {
            int reg = (addr >> 8) & 0x1f;

            ret = env->mmuregs[reg];
            if (reg == 3) {
                env->mmuregs[3] = 0;
            } else if (reg == 0x13) {
                ret = env->mmuregs[3];
            } else if (reg == 0x14) {
                ret = env->mmuregs[4];
            }
            DPRINTF_MMU("mmu_read: reg[%d] = 0x%08" PRIx64 "\n", reg, ret);
        }
        break;
    case ASI_M_TLBDIAG:
    case ASI_M_DIAGS:
    case ASI_M_IODIAG:
        break;
    case ASI_M_TXTC_TAG:
    case ASI_M_TXTC_DATA:
    case ASI_M_DATAC_TAG:
    case ASI_M_DATAC_DATA:
        break;
    case 0x21 ... 0x2f:
    {
        MemTxResult result;
        hwaddr access_addr = (hwaddr)addr | ((hwaddr)(asi & 0xf) << 32);

        switch (size) {
        case 1:
            ret = address_space_ldub(cs->as, access_addr,
                                     MEMTXATTRS_UNSPECIFIED, &result);
            break;
        case 2:
            ret = address_space_lduw(cs->as, access_addr,
                                     MEMTXATTRS_UNSPECIFIED, &result);
            break;
        default:
        case 4:
            ret = address_space_ldl(cs->as, access_addr,
                                    MEMTXATTRS_UNSPECIFIED, &result);
            break;
        case 8:
            ret = address_space_ldq(cs->as, access_addr,
                                    MEMTXATTRS_UNSPECIFIED, &result);
            break;
        }

        if (result != MEMTX_OK) {
            sparc_raise_mmu_fault(cs, access_addr, false, false, false,
                                  size, GETPC());
        }
        break;
    }
    case 0x30:
    case 0x31:
    case 0x32:
    case 0x39:
        ret = 0;
        break;
    case 0x38:
        {
            int reg = (addr >> 8) & 3;

            switch (reg) {
            case 0:
                ret = env->mmubpregs[reg];
                break;
            case 1:
                ret = env->mmubpregs[reg];
                break;
            case 2:
                ret = env->mmubpregs[reg];
                break;
            case 3:
                ret = env->mmubpregs[reg];
                env->mmubpregs[reg] = 0ULL;
                break;
            }
            DPRINTF_MMU("read breakpoint reg[%d] 0x%016" PRIx64 "\n", reg,
                        ret);
        }
        break;
    case 0x49:
        ret = env->mmubpctrv;
        break;
    case 0x4a:
        ret = env->mmubpctrc;
        break;
    case 0x4b:
        ret = env->mmubpctrs;
        break;
    case 0x4c:
        ret = env->mmubpaction;
        break;
    default:
        sparc_raise_mmu_fault(cs, addr, false, false, asi, size, GETPC());
        ret = 0;
        break;

    case ASI_USERDATA:
    case ASI_KERNELDATA:
    case ASI_USERTXT:
    case ASI_KERNELTXT:
    case ASI_P:
    case ASI_M_BYPASS:
    case ASI_LEON_BYPASS:
        g_assert_not_reached();
    }
    if (sign) {
        switch (size) {
        case 1:
            ret = (int8_t) ret;
            break;
        case 2:
            ret = (int16_t) ret;
            break;
        case 4:
            ret = (int32_t) ret;
            break;
        default:
            break;
        }
    }
#ifdef DEBUG_ASI
    dump_asi("read ", last_addr, asi, size, ret);
#endif
    return ret;
}

void helper_st_asi(CPUSPARCState *env, target_ulong addr, uint64_t val,
                   int asi, uint32_t memop)
{
    int size = 1 << (memop & MO_SIZE);
    CPUState *cs = env_cpu(env);

    do_check_align(env, addr, size - 1, GETPC());
    switch (asi) {
    case ASI_M_MXCC:
        switch (addr) {
        case 0x00:
        case 0x08:
        case 0x0C:
            if (env->def.features & CPU_FEATURE_CACHE_CTRL) {
                leon3_cache_control_st(env, addr, val, size);
            } else {
                qemu_log_mask(LOG_UNIMP, "0x" TARGET_FMT_lx ": unimplemented"
                                         " address, size: %d\n", addr, size);
            }
            break;

        case 0x01c00000:
            if (size == 8) {
                env->mxccdata[0] = val;
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }
            break;
        case 0x01c00008:
            if (size == 8) {
                env->mxccdata[1] = val;
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }
            break;
        case 0x01c00010:
            if (size == 8) {
                env->mxccdata[2] = val;
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }
            break;
        case 0x01c00018:
            if (size == 8) {
                env->mxccdata[3] = val;
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }
            break;
        case 0x01c00100:
        {
            int i;

            if (size == 8) {
                env->mxccregs[0] = val;
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }

            for (i = 0; i < 4; i++) {
                MemTxResult result;
                hwaddr access_addr = (env->mxccregs[0] & 0xffffffffULL) + 8 * i;

                env->mxccdata[i] = address_space_ldq(cs->as,
                                                     access_addr,
                                                     MEMTXATTRS_UNSPECIFIED,
                                                     &result);
                if (result != MEMTX_OK) {
                    sparc_raise_mmu_fault(cs, access_addr, false, false,
                                          false, size, GETPC());
                }
            }
            break;
        }
        case 0x01c00200:
        {
            int i;

            if (size == 8) {
                env->mxccregs[1] = val;
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }

            for (i = 0; i < 4; i++) {
                MemTxResult result;
                hwaddr access_addr = (env->mxccregs[1] & 0xffffffffULL) + 8 * i;

                address_space_stq(cs->as, access_addr, env->mxccdata[i],
                                  MEMTXATTRS_UNSPECIFIED, &result);

                if (result != MEMTX_OK) {
                    sparc_raise_mmu_fault(cs, access_addr, true, false,
                                          false, size, GETPC());
                }
            }
            break;
        }
        case 0x01c00a00:
            if (size == 8) {
                env->mxccregs[3] = val;
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }
            break;
        case 0x01c00a04:
            if (size == 4) {
                env->mxccregs[3] = (env->mxccregs[3] & 0xffffffff00000000ULL)
                    | val;
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }
            break;
        case 0x01c00e00:
            if (size == 8) {
                env->mxccregs[6] &= ~val;
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }
            break;
        case 0x01c00f00:
            if (size == 8) {
                env->mxccregs[7] = val;
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }
            break;
        default:
            qemu_log_mask(LOG_UNIMP,
                          "%08x: unimplemented address, size: %d\n", addr,
                          size);
            break;
        }
        DPRINTF_MXCC("asi = %d, size = %d, addr = %08x, val = %" PRIx64 "\n",
                     asi, size, addr, val);
#ifdef DEBUG_MXCC
        dump_mxcc(env);
#endif
        break;
    case ASI_M_FLUSH_PROBE:
    case ASI_LEON_MMUFLUSH:
        {
            int mmulev;

            mmulev = (addr >> 8) & 15;
            DPRINTF_MMU("mmu flush level %d\n", mmulev);
            switch (mmulev) {
            case 0:
                tlb_flush_page(cs, addr & 0xfffff000);
                break;
            case 1:
            case 2:
            case 3:
            case 4:
                tlb_flush(cs);
                break;
            default:
                break;
            }
#ifdef DEBUG_MMU
            dump_mmu(env);
#endif
        }
        break;
    case ASI_M_MMUREGS:
    case ASI_LEON_MMUREGS:
        {
            int reg = (addr >> 8) & 0x1f;
            uint32_t oldreg;

            oldreg = env->mmuregs[reg];
            switch (reg) {
            case 0:
                env->mmuregs[reg] = (env->mmuregs[reg] & 0xff000000) |
                    (val & 0x00ffffff);
                if ((oldreg ^ env->mmuregs[reg])
                    & (MMU_NF | env->def.mmu_bm)) {
                    tlb_flush(cs);
                }
                break;
            case 1:
                env->mmuregs[reg] = val & env->def.mmu_ctpr_mask;
                break;
            case 2:
                env->mmuregs[reg] = val & env->def.mmu_cxr_mask;
                if (oldreg != env->mmuregs[reg]) {
                    tlb_flush(cs);
                }
                break;
            case 3:
            case 4:
                break;
            case 0x10:
                env->mmuregs[reg] = val & env->def.mmu_trcr_mask;
                break;
            case 0x13:
                env->mmuregs[3] = val & env->def.mmu_sfsr_mask;
                break;
            case 0x14:
                env->mmuregs[4] = val;
                break;
            default:
                env->mmuregs[reg] = val;
                break;
            }
            if (oldreg != env->mmuregs[reg]) {
                DPRINTF_MMU("mmu change reg[%d]: 0x%08x -> 0x%08x\n",
                            reg, oldreg, env->mmuregs[reg]);
            }
#ifdef DEBUG_MMU
            dump_mmu(env);
#endif
        }
        break;
    case ASI_M_TLBDIAG:
    case ASI_M_DIAGS:
    case ASI_M_IODIAG:
        break;
    case ASI_M_TXTC_TAG:
    case ASI_M_TXTC_DATA:
    case ASI_M_DATAC_TAG:
    case ASI_M_DATAC_DATA:
    case ASI_M_FLUSH_PAGE:
    case ASI_M_FLUSH_SEG:
    case ASI_M_FLUSH_REGION:
    case ASI_M_FLUSH_CTX:
    case ASI_M_FLUSH_USER:
        break;
    case 0x21 ... 0x2f:
        {
            MemTxResult result;
            hwaddr access_addr = (hwaddr)addr | ((hwaddr)(asi & 0xf) << 32);

            switch (size) {
            case 1:
                address_space_stb(cs->as, access_addr, val,
                                  MEMTXATTRS_UNSPECIFIED, &result);
                break;
            case 2:
                address_space_stw(cs->as, access_addr, val,
                                  MEMTXATTRS_UNSPECIFIED, &result);
                break;
            case 4:
            default:
                address_space_stl(cs->as, access_addr, val,
                                  MEMTXATTRS_UNSPECIFIED, &result);
                break;
            case 8:
                address_space_stq(cs->as, access_addr, val,
                                  MEMTXATTRS_UNSPECIFIED, &result);
                break;
            }
            if (result != MEMTX_OK) {
                sparc_raise_mmu_fault(cs, access_addr, true, false, false,
                                      size, GETPC());
            }
        }
        break;
    case 0x30:
    case 0x31:
    case 0x32:
    case 0x36:
    case 0x37:
        break;
    case 0x38:
        {
            int reg = (addr >> 8) & 3;

            switch (reg) {
            case 0:
                env->mmubpregs[reg] = (val & 0xfffffffffULL);
                break;
            case 1:
                env->mmubpregs[reg] = (val & 0xfffffffffULL);
                break;
            case 2:
                env->mmubpregs[reg] = (val & 0x7fULL);
                break;
            case 3:
                env->mmubpregs[reg] = (val & 0xfULL);
                break;
            }
            DPRINTF_MMU("write breakpoint reg[%d] 0x%016x\n", reg,
                        env->mmuregs[reg]);
        }
        break;
    case 0x49:
        env->mmubpctrv = val & 0xffffffff;
        break;
    case 0x4a:
        env->mmubpctrc = val & 0x3;
        break;
    case 0x4b:
        env->mmubpctrs = val & 0x3;
        break;
    case 0x4c:
        env->mmubpaction = val & 0x1fff;
        break;
    case ASI_USERTXT:
    case ASI_KERNELTXT:
    default:
        sparc_raise_mmu_fault(cs, addr, true, false, asi, size, GETPC());
        break;

    case ASI_USERDATA:
    case ASI_KERNELDATA:
    case ASI_P:
    case ASI_M_BYPASS:
    case ASI_LEON_BYPASS:
    case ASI_M_BCOPY:
    case ASI_M_BFILL:
        g_assert_not_reached();
    }
#ifdef DEBUG_ASI
    dump_asi("write", addr, asi, size, val);
#endif
}

uint64_t helper_ld_code(CPUSPARCState *env, target_ulong addr, uint32_t oi)
{
    MemOp mop = get_memop(oi);
    uintptr_t ra = GETPC();
    uint64_t ret;

    switch (mop & MO_SIZE) {
    case MO_8:
        ret = cpu_ldb_code_mmu(env, addr, oi, ra);
        if (mop & MO_SIGN) {
            ret = (int8_t)ret;
        }
        break;
    case MO_16:
        ret = cpu_ldw_code_mmu(env, addr, oi, ra);
        if ((mop & MO_BSWAP) != MO_TE) {
            ret = bswap16(ret);
        }
        if (mop & MO_SIGN) {
            ret = (int16_t)ret;
        }
        break;
    case MO_32:
        ret = cpu_ldl_code_mmu(env, addr, oi, ra);
        if ((mop & MO_BSWAP) != MO_TE) {
            ret = bswap32(ret);
        }
        if (mop & MO_SIGN) {
            ret = (int32_t)ret;
        }
        break;
    case MO_64:
        ret = cpu_ldq_code_mmu(env, addr, oi, ra);
        if ((mop & MO_BSWAP) != MO_TE) {
            ret = bswap64(ret);
        }
        break;
    default:
        g_assert_not_reached();
    }
    return ret;
}

#endif
#else

#ifdef CONFIG_USER_ONLY
uint64_t helper_ld_asi(CPUSPARCState *env, target_ulong addr,
                       int asi, uint32_t memop)
{
    int size = 1 << (memop & MO_SIZE);
    int sign = memop & MO_SIGN;
    uint64_t ret = 0;

    if (asi < 0x80) {
        cpu_raise_exception_ra(env, TT_PRIV_ACT, GETPC());
    }
    do_check_align(env, addr, size - 1, GETPC());
    addr = asi_address_mask(env, asi, addr);

    switch (asi) {
    case ASI_PNF:
    case ASI_PNFL:
    case ASI_SNF:
    case ASI_SNFL:
        if (!page_check_range(addr, size, PAGE_READ)) {
            ret = 0;
            break;
        }
        switch (size) {
        case 1:
            ret = cpu_ldub_data(env, addr);
            break;
        case 2:
            ret = cpu_lduw_data(env, addr);
            break;
        case 4:
            ret = cpu_ldl_data(env, addr);
            break;
        case 8:
            ret = cpu_ldq_data(env, addr);
            break;
        default:
            g_assert_not_reached();
        }
        break;
        break;

    case ASI_P:
    case ASI_PL:
    case ASI_S:
    case ASI_SL:
        g_assert_not_reached();

    default:
        cpu_raise_exception_ra(env, TT_DATA_ACCESS, GETPC());
    }

    switch (asi) {
    case ASI_PNFL:
    case ASI_SNFL:
        switch (size) {
        case 2:
            ret = bswap16(ret);
            break;
        case 4:
            ret = bswap32(ret);
            break;
        case 8:
            ret = bswap64(ret);
            break;
        }
    }

    if (sign) {
        switch (size) {
        case 1:
            ret = (int8_t) ret;
            break;
        case 2:
            ret = (int16_t) ret;
            break;
        case 4:
            ret = (int32_t) ret;
            break;
        }
    }
#ifdef DEBUG_ASI
    dump_asi("read", addr, asi, size, ret);
#endif
    return ret;
}

void helper_st_asi(CPUSPARCState *env, target_ulong addr, target_ulong val,
                   int asi, uint32_t memop)
{
    int size = 1 << (memop & MO_SIZE);
#ifdef DEBUG_ASI
    dump_asi("write", addr, asi, size, val);
#endif
    if (asi < 0x80) {
        cpu_raise_exception_ra(env, TT_PRIV_ACT, GETPC());
    }
    do_check_align(env, addr, size - 1, GETPC());

    switch (asi) {
    case ASI_P:
    case ASI_PL:
    case ASI_S:
    case ASI_SL:
        g_assert_not_reached();

    case ASI_PNF:
    case ASI_SNF:
    case ASI_PNFL:
    case ASI_SNFL:
    default:
        cpu_raise_exception_ra(env, TT_DATA_ACCESS, GETPC());
    }
}

#else

uint64_t helper_ld_asi(CPUSPARCState *env, target_ulong addr,
                       int asi, uint32_t memop)
{
    int size = 1 << (memop & MO_SIZE);
    int sign = memop & MO_SIGN;
    CPUState *cs = env_cpu(env);
    uint64_t ret = 0;
#if defined(DEBUG_ASI)
    target_ulong last_addr = addr;
#endif

    asi &= 0xff;

    do_check_asi(env, asi, GETPC());
    do_check_align(env, addr, size - 1, GETPC());
    addr = asi_address_mask(env, asi, addr);

    switch (asi) {
    case ASI_PNF:
    case ASI_PNFL:
    case ASI_SNF:
    case ASI_SNFL:
        {
            MemOpIdx oi;
            int idx = (env->pstate & PS_PRIV
                       ? (asi & 1 ? MMU_KERNEL_SECONDARY_IDX : MMU_KERNEL_IDX)
                       : (asi & 1 ? MMU_USER_SECONDARY_IDX : MMU_USER_IDX));

            if (cpu_get_phys_page_nofault(env, addr, idx) == -1ULL) {
#ifdef DEBUG_ASI
                dump_asi("read ", last_addr, asi, size, ret);
#endif
                cpu_raise_exception_ra(env, cs->exception_index, GETPC());
            }
            oi = make_memop_idx(memop, idx);
            switch (size) {
            case 1:
                ret = cpu_ldb_mmu(env, addr, oi, GETPC());
                break;
            case 2:
                ret = cpu_ldw_mmu(env, addr, oi, GETPC());
                break;
            case 4:
                ret = cpu_ldl_mmu(env, addr, oi, GETPC());
                break;
            case 8:
                ret = cpu_ldq_mmu(env, addr, oi, GETPC());
                break;
            default:
                g_assert_not_reached();
            }
        }
        break;

    case ASI_AIUP:
    case ASI_AIUS:
    case ASI_AIUPL:
    case ASI_AIUSL:
    case ASI_P:
    case ASI_S:
    case ASI_PL:
    case ASI_SL:
    case ASI_REAL:
    case ASI_REAL_IO:
    case ASI_REAL_L:
    case ASI_REAL_IO_L:
    case ASI_N:
    case ASI_NL:
    case ASI_NUCLEUS_QUAD_LDD:
    case ASI_NUCLEUS_QUAD_LDD_L:
    case ASI_TWINX_AIUP:
    case ASI_TWINX_AIUS:
    case ASI_TWINX_REAL:
    case ASI_TWINX_AIUP_L:
    case ASI_TWINX_AIUS_L:
    case ASI_TWINX_REAL_L:
    case ASI_TWINX_N:
    case ASI_TWINX_NL:
    case ASI_TWINX_P:
    case ASI_TWINX_PL:
    case ASI_TWINX_S:
    case ASI_TWINX_SL:
    case ASI_MON_P:
    case ASI_MON_S:
    case ASI_MON_AIUP:
    case ASI_MON_AIUS:
        g_assert_not_reached();

    case ASI_UPA_CONFIG:
        break;
    case ASI_LSU_CONTROL:
        ret = env->lsu;
        break;
    case ASI_IMMU:
        {
            int reg = (addr >> 3) & 0xf;
            switch (reg) {
            case 0:
                ret = ultrasparc_tag_target(env->immu.tag_access);
                break;
            case 3:
                ret = env->immu.sfsr;
                break;
            case 5:
                ret = env->immu.tsb;
                break;
            case 6:
                ret = env->immu.tag_access;
                break;
            default:
                sparc_raise_mmu_fault(cs, addr, false, false, 1, size, GETPC());
                ret = 0;
            }
            break;
        }
    case ASI_IMMU_TSB_8KB_PTR:
        {
            ret = ultrasparc_tsb_pointer(env, &env->immu, 0);
            break;
        }
    case ASI_IMMU_TSB_64KB_PTR:
        {
            ret = ultrasparc_tsb_pointer(env, &env->immu, 1);
            break;
        }
    case ASI_ITLB_DATA_ACCESS:
        {
            int reg = (addr >> 3) & 0x3f;

            ret = env->itlb[reg].tte;
            break;
        }
    case ASI_ITLB_TAG_READ:
        {
            int reg = (addr >> 3) & 0x3f;

            ret = env->itlb[reg].tag;
            break;
        }
    case ASI_DMMU:
        {
            int reg = (addr >> 3) & 0xf;
            switch (reg) {
            case 0:
                ret = ultrasparc_tag_target(env->dmmu.tag_access);
                break;
            case 1:
                ret = env->dmmu.mmu_primary_context;
                break;
            case 2:
                ret = env->dmmu.mmu_secondary_context;
                break;
            case 3:
                ret = env->dmmu.sfsr;
                break;
            case 4:
                ret = env->dmmu.sfar;
                break;
            case 5:
                ret = env->dmmu.tsb;
                break;
            case 6:
                ret = env->dmmu.tag_access;
                break;
            case 7:
                ret = env->dmmu.virtual_watchpoint;
                break;
            case 8:
                ret = env->dmmu.physical_watchpoint;
                break;
            default:
                sparc_raise_mmu_fault(cs, addr, false, false, 1, size, GETPC());
                ret = 0;
            }
            break;
        }
    case ASI_DMMU_TSB_8KB_PTR:
        {
            ret = ultrasparc_tsb_pointer(env, &env->dmmu, 0);
            break;
        }
    case ASI_DMMU_TSB_64KB_PTR:
        {
            ret = ultrasparc_tsb_pointer(env, &env->dmmu, 1);
            break;
        }
    case ASI_DTLB_DATA_ACCESS:
        {
            int reg = (addr >> 3) & 0x3f;

            ret = env->dtlb[reg].tte;
            break;
        }
    case ASI_DTLB_TAG_READ:
        {
            int reg = (addr >> 3) & 0x3f;

            ret = env->dtlb[reg].tag;
            break;
        }
    case ASI_INTR_DISPATCH_STAT:
        break;
    case ASI_INTR_RECEIVE:
        ret = env->ivec_status;
        break;
    case ASI_INTR_R:
        {
            int reg = (addr >> 4) & 0x3;
            if (reg < 3) {
                ret = env->ivec_data[reg];
            }
            break;
        }
    case ASI_SCRATCHPAD:
        if (unlikely((addr >= 0x20) && (addr < 0x30))) {
            sparc_raise_mmu_fault(cs, addr, false, false, 1, size, GETPC());
        }
        /* fall through */
    case ASI_HYP_SCRATCHPAD:
        {
            unsigned int i = (addr >> 3) & 0x7;
            ret = env->scratch[i];
            break;
        }
    case ASI_MMU:
        switch ((addr >> 3) & 0x3) {
        case 1:
            ret = env->dmmu.mmu_primary_context;
            break;
        case 2:
            ret = env->dmmu.mmu_secondary_context;
            break;
        default:
          sparc_raise_mmu_fault(cs, addr, true, false, 1, size, GETPC());
        }
        break;
    case ASI_DCACHE_DATA:
    case ASI_DCACHE_TAG:
    case ASI_ESTATE_ERROR_EN:
    case ASI_AFSR:
    case ASI_AFAR:
    case ASI_EC_TAG_DATA:
    case ASI_IC_INSTR:
    case ASI_IC_TAG:
    case ASI_IC_PRE_DECODE:
    case ASI_IC_NEXT_FIELD:
    case ASI_EC_W:
    case ASI_EC_R:
        break;
    case ASI_DMMU_TSB_DIRECT_PTR:
    case ASI_ITLB_DATA_IN:
    case ASI_IMMU_DEMAP:
    case ASI_DTLB_DATA_IN:
    case ASI_DMMU_DEMAP:
    case ASI_INTR_W:
    default:
        sparc_raise_mmu_fault(cs, addr, false, false, 1, size, GETPC());
        ret = 0;
        break;
    }

    if (sign) {
        switch (size) {
        case 1:
            ret = (int8_t) ret;
            break;
        case 2:
            ret = (int16_t) ret;
            break;
        case 4:
            ret = (int32_t) ret;
            break;
        default:
            break;
        }
    }
#ifdef DEBUG_ASI
    dump_asi("read ", last_addr, asi, size, ret);
#endif
    return ret;
}

void helper_st_asi(CPUSPARCState *env, target_ulong addr, target_ulong val,
                   int asi, uint32_t memop)
{
    int size = 1 << (memop & MO_SIZE);
    CPUState *cs = env_cpu(env);

#ifdef DEBUG_ASI
    dump_asi("write", addr, asi, size, val);
#endif

    asi &= 0xff;

    do_check_asi(env, asi, GETPC());
    do_check_align(env, addr, size - 1, GETPC());
    addr = asi_address_mask(env, asi, addr);

    switch (asi) {
    case ASI_AIUP:
    case ASI_AIUS:
    case ASI_AIUPL:
    case ASI_AIUSL:
    case ASI_P:
    case ASI_S:
    case ASI_PL:
    case ASI_SL:
    case ASI_REAL:
    case ASI_REAL_IO:
    case ASI_REAL_L:
    case ASI_REAL_IO_L:
    case ASI_N:
    case ASI_NL:
    case ASI_NUCLEUS_QUAD_LDD:
    case ASI_NUCLEUS_QUAD_LDD_L:
    case ASI_TWINX_AIUP:
    case ASI_TWINX_AIUS:
    case ASI_TWINX_REAL:
    case ASI_TWINX_AIUP_L:
    case ASI_TWINX_AIUS_L:
    case ASI_TWINX_REAL_L:
    case ASI_TWINX_N:
    case ASI_TWINX_NL:
    case ASI_TWINX_P:
    case ASI_TWINX_PL:
    case ASI_TWINX_S:
    case ASI_TWINX_SL:
        g_assert_not_reached();
    case 0x31:
    case 0x32:
    case 0x39:
    case 0x3a:
        if (cpu_has_hypervisor(env)) {
            int idx = ((asi & 2) >> 1) | ((asi & 8) >> 2);
            env->dmmu.sun4v_tsb_pointers[idx] = val;
        } else {
            goto illegal_insn;
        }
        break;
    case 0x33:
    case 0x3b:
        if (cpu_has_hypervisor(env)) {
            env->dmmu.sun4v_ctx_config[(asi & 8) >> 3] = val;
        } else {
            goto illegal_insn;
        }
        break;
    case 0x35:
    case 0x36:
    case 0x3d:
    case 0x3e:
        if (cpu_has_hypervisor(env)) {
            int idx = ((asi & 2) >> 1) | ((asi & 8) >> 2);
            env->immu.sun4v_tsb_pointers[idx] = val;
        } else {
            goto illegal_insn;
        }
      break;
    case 0x37:
    case 0x3f:
        if (cpu_has_hypervisor(env)) {
            env->immu.sun4v_ctx_config[(asi & 8) >> 3] = val;
        } else {
            goto illegal_insn;
        }
        break;
    case ASI_UPA_CONFIG:
        return;
    case ASI_LSU_CONTROL:
        env->lsu = val & (DMMU_E | IMMU_E);
        return;
    case ASI_IMMU:
        {
            int reg = (addr >> 3) & 0xf;
            uint64_t oldreg;

            oldreg = env->immu.mmuregs[reg];
            switch (reg) {
            case 0:
                return;
            case 1:
            case 2:
                return;
            case 3:
                if ((val & 1) == 0) {
                    val = 0;
                }
                env->immu.sfsr = val;
                break;
            case 4:
                return;
            case 5:
                DPRINTF_MMU("immu TSB write: 0x%016" PRIx64 " -> 0x%016"
                            PRIx64 "\n", env->immu.tsb, val);
                env->immu.tsb = val;
                break;
            case 6:
                env->immu.tag_access = val;
                break;
            case 7:
            case 8:
                return;
            default:
                sparc_raise_mmu_fault(cs, addr, true, false, 1, size, GETPC());
                break;
            }

            if (oldreg != env->immu.mmuregs[reg]) {
                DPRINTF_MMU("immu change reg[%d]: 0x%016" PRIx64 " -> 0x%016"
                            PRIx64 "\n", reg, oldreg, env->immuregs[reg]);
            }
#ifdef DEBUG_MMU
            dump_mmu(env);
#endif
            return;
        }
    case ASI_ITLB_DATA_IN:
        if (!(addr & TLB_UST1_IS_REAL_BIT)) {
            replace_tlb_1bit_lru(env->itlb, env->immu.tag_access,
                                 val, "immu", env, addr);
        }
        return;
    case ASI_ITLB_DATA_ACCESS:
        {

            unsigned int i = (addr >> 3) & 0x3f;

            if (!(addr & TLB_UST1_IS_REAL_BIT)) {
                replace_tlb_entry(&env->itlb[i], env->immu.tag_access,
                                  sun4v_tte_to_sun4u(env, addr, val), env);
            }
#ifdef DEBUG_MMU
            DPRINTF_MMU("immu data access replaced entry [%i]\n", i);
            dump_mmu(env);
#endif
            return;
        }
    case ASI_IMMU_DEMAP:
        demap_tlb(env->itlb, addr, "immu", env);
        return;
    case ASI_DMMU:
        {
            int reg = (addr >> 3) & 0xf;
            uint64_t oldreg;

            oldreg = env->dmmu.mmuregs[reg];
            switch (reg) {
            case 0:
            case 4:
                return;
            case 3:
                if ((val & 1) == 0) {
                    val = 0;
                    env->dmmu.sfar = 0;
                }
                env->dmmu.sfsr = val;
                break;
            case 1:
                env->dmmu.mmu_primary_context = val;
                tlb_flush(cs);
                break;
            case 2:
                env->dmmu.mmu_secondary_context = val;
                tlb_flush(cs);
                break;
            case 5:
                DPRINTF_MMU("dmmu TSB write: 0x%016" PRIx64 " -> 0x%016"
                            PRIx64 "\n", env->dmmu.tsb, val);
                env->dmmu.tsb = val;
                break;
            case 6:
                env->dmmu.tag_access = val;
                break;
            case 7:
                env->dmmu.virtual_watchpoint = val;
                break;
            case 8:
                env->dmmu.physical_watchpoint = val;
                break;
            default:
                sparc_raise_mmu_fault(cs, addr, true, false, 1, size, GETPC());
                break;
            }

            if (oldreg != env->dmmu.mmuregs[reg]) {
                DPRINTF_MMU("dmmu change reg[%d]: 0x%016" PRIx64 " -> 0x%016"
                            PRIx64 "\n", reg, oldreg, env->dmmuregs[reg]);
            }
#ifdef DEBUG_MMU
            dump_mmu(env);
#endif
            return;
        }
    case ASI_DTLB_DATA_IN:
      if (!(addr & TLB_UST1_IS_REAL_BIT)) {
          replace_tlb_1bit_lru(env->dtlb, env->dmmu.tag_access,
                               val, "dmmu", env, addr);
      }
      return;
    case ASI_DTLB_DATA_ACCESS:
        {
            unsigned int i = (addr >> 3) & 0x3f;

            if (!(addr & TLB_UST1_IS_REAL_BIT)) {
                replace_tlb_entry(&env->dtlb[i], env->dmmu.tag_access,
                                  sun4v_tte_to_sun4u(env, addr, val), env);
            }
#ifdef DEBUG_MMU
            DPRINTF_MMU("dmmu data access replaced entry [%i]\n", i);
            dump_mmu(env);
#endif
            return;
        }
    case ASI_DMMU_DEMAP:
        demap_tlb(env->dtlb, addr, "dmmu", env);
        return;
    case ASI_INTR_RECEIVE:
        env->ivec_status = val & 0x20;
        return;
    case ASI_SCRATCHPAD:
        if (unlikely((addr >= 0x20) && (addr < 0x30))) {
            sparc_raise_mmu_fault(cs, addr, true, false, 1, size, GETPC());
        }
        /* fall through */
    case ASI_HYP_SCRATCHPAD:
        {
            unsigned int i = (addr >> 3) & 0x7;
            env->scratch[i] = val;
            return;
        }
    case ASI_MMU:
        {
          switch ((addr >> 3) & 0x3) {
          case 1:
              env->dmmu.mmu_primary_context = val;
              env->immu.mmu_primary_context = val;
              tlb_flush_by_mmuidx(cs,
                                  (1 << MMU_USER_IDX) | (1 << MMU_KERNEL_IDX));
              break;
          case 2:
              env->dmmu.mmu_secondary_context = val;
              env->immu.mmu_secondary_context = val;
              tlb_flush_by_mmuidx(cs,
                                  (1 << MMU_USER_SECONDARY_IDX) |
                                  (1 << MMU_KERNEL_SECONDARY_IDX));
              break;
          default:
              sparc_raise_mmu_fault(cs, addr, true, false, 1, size, GETPC());
          }
        }
        return;
    case ASI_QUEUE:
    case ASI_DCACHE_DATA:
    case ASI_DCACHE_TAG:
    case ASI_ESTATE_ERROR_EN:
    case ASI_AFSR:
    case ASI_AFAR:
    case ASI_EC_TAG_DATA:
    case ASI_IC_INSTR:
    case ASI_IC_TAG:
    case ASI_IC_PRE_DECODE:
    case ASI_IC_NEXT_FIELD:
    case ASI_EC_W:
    case ASI_EC_R:
        return;
    case ASI_IMMU_TSB_8KB_PTR:
    case ASI_IMMU_TSB_64KB_PTR:
    case ASI_ITLB_TAG_READ:
    case ASI_DMMU_TSB_8KB_PTR:
    case ASI_DMMU_TSB_64KB_PTR:
    case ASI_DMMU_TSB_DIRECT_PTR:
    case ASI_DTLB_TAG_READ:
    case ASI_INTR_DISPATCH_STAT:
    case ASI_INTR_R:
    case ASI_PNF:
    case ASI_SNF:
    case ASI_PNFL:
    case ASI_SNFL:
    default:
        sparc_raise_mmu_fault(cs, addr, true, false, 1, size, GETPC());
        return;
    illegal_insn:
        cpu_raise_exception_ra(env, TT_ILL_INSN, GETPC());
    }
}
#endif
#endif

#if !defined(CONFIG_USER_ONLY)

void sparc_cpu_do_transaction_failed(CPUState *cs, hwaddr physaddr,
                                     vaddr addr, unsigned size,
                                     MMUAccessType access_type,
                                     int mmu_idx, MemTxAttrs attrs,
                                     MemTxResult response, uintptr_t retaddr)
{
    bool is_write = access_type == MMU_DATA_STORE;
    bool is_exec = access_type == MMU_INST_FETCH;
    bool is_asi = false;

    sparc_raise_mmu_fault(cs, physaddr, is_write, is_exec,
                          is_asi, size, retaddr);
}
#endif
