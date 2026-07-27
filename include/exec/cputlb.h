/*
 *  Common CPU TLB handling
 *
 *  Copyright (c) 2003 Fabrice Bellard
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

#ifndef CPUTLB_H
#define CPUTLB_H

#include "exec/cpu-common.h"
#include "exec/hwaddr.h"
#include "exec/memattrs.h"
#include "exec/vaddr.h"

#if defined(CONFIG_TCG) && !defined(CONFIG_USER_ONLY)
void tlb_protect_code(ram_addr_t ram_addr);
void tlb_unprotect_code(ram_addr_t ram_addr);
#endif

#ifndef CONFIG_USER_ONLY
void tlb_reset_dirty(CPUState *cpu, uintptr_t start, uintptr_t length);
void tlb_reset_dirty_range_all(ram_addr_t start, ram_addr_t length);
#ifdef XBOX
void tlb_invalidate_ram_range_by_ref(CPUState *cpu, uintptr_t host_start,
                                     uintptr_t length);
#endif
#endif

void tlb_set_page_full(CPUState *cpu, int mmu_idx, vaddr addr,
                       CPUTLBEntryFull *full);

void tlb_set_page_with_attrs(CPUState *cpu, vaddr addr,
                             hwaddr paddr, MemTxAttrs attrs,
                             int prot, int mmu_idx, vaddr size);

void tlb_set_page(CPUState *cpu, vaddr addr,
                  hwaddr paddr, int prot,
                  int mmu_idx, vaddr size);

#if defined(CONFIG_TCG) && !defined(CONFIG_USER_ONLY)
void tlb_flush_page(CPUState *cpu, vaddr addr);

void tlb_flush_page_all_cpus_synced(CPUState *src, vaddr addr);

void tlb_flush(CPUState *cpu);

void tlb_flush_all_cpus_synced(CPUState *src_cpu);

void tlb_flush_page_by_mmuidx(CPUState *cpu, vaddr addr,
                              MMUIdxMap idxmap);

void tlb_flush_page_by_mmuidx_all_cpus_synced(CPUState *cpu, vaddr addr,
                                              MMUIdxMap idxmap);

void tlb_flush_by_mmuidx(CPUState *cpu, MMUIdxMap idxmap);

void tlb_flush_by_mmuidx_all_cpus_synced(CPUState *cpu, MMUIdxMap idxmap);

void tlb_flush_page_bits_by_mmuidx(CPUState *cpu, vaddr addr,
                                   MMUIdxMap idxmap, unsigned bits);

void tlb_flush_page_bits_by_mmuidx_all_cpus_synced(CPUState *cpu, vaddr addr,
                                                   MMUIdxMap idxmap,
                                                   unsigned bits);

void tlb_flush_range_by_mmuidx(CPUState *cpu, vaddr addr,
                               vaddr len, MMUIdxMap idxmap,
                               unsigned bits);

void tlb_flush_range_by_mmuidx_all_cpus_synced(CPUState *cpu,
                                               vaddr addr,
                                               vaddr len,
                                               MMUIdxMap idxmap,
                                               unsigned bits);
#else
static inline void tlb_flush_page(CPUState *cpu, vaddr addr)
{
}
static inline void tlb_flush_page_all_cpus_synced(CPUState *src, vaddr addr)
{
}
static inline void tlb_flush(CPUState *cpu)
{
}
static inline void tlb_flush_all_cpus_synced(CPUState *src_cpu)
{
}
static inline void tlb_flush_page_by_mmuidx(CPUState *cpu,
                                            vaddr addr, MMUIdxMap idxmap)
{
}

static inline void tlb_flush_by_mmuidx(CPUState *cpu, MMUIdxMap idxmap)
{
}
static inline void tlb_flush_page_by_mmuidx_all_cpus_synced(CPUState *cpu,
                                                            vaddr addr,
                                                            MMUIdxMap idxmap)
{
}
static inline void tlb_flush_by_mmuidx_all_cpus_synced(CPUState *cpu,
                                                       MMUIdxMap idxmap)
{
}
static inline void tlb_flush_page_bits_by_mmuidx(CPUState *cpu,
                                                 vaddr addr,
                                                 MMUIdxMap idxmap,
                                                 unsigned bits)
{
}
static inline void
tlb_flush_page_bits_by_mmuidx_all_cpus_synced(CPUState *cpu, vaddr addr,
                                              MMUIdxMap idxmap, unsigned bits)
{
}
static inline void tlb_flush_range_by_mmuidx(CPUState *cpu, vaddr addr,
                                             vaddr len, MMUIdxMap idxmap,
                                             unsigned bits)
{
}
static inline void tlb_flush_range_by_mmuidx_all_cpus_synced(CPUState *cpu,
                                                             vaddr addr,
                                                             vaddr len,
                                                             MMUIdxMap idxmap,
                                                             unsigned bits)
{
}
#endif
#endif
