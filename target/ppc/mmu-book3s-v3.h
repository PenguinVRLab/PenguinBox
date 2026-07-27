/*
 *  PowerPC ISAV3 BookS emulation generic mmu definitions for qemu.
 *
 *  Copyright (c) 2017 Suraj Jitindar Singh, IBM Corporation
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

#ifndef PPC_MMU_BOOK3S_V3_H
#define PPC_MMU_BOOK3S_V3_H

#ifndef CONFIG_USER_ONLY

#define PTCR_PATB               0x0FFFFFFFFFFFF000ULL
#define PTCR_PATS               0x000000000000001FULL

#define PATE0_HR 0x8000000000000000

#define PATE1_GR 0x8000000000000000

struct prtb_entry {
    uint64_t prtbe0, prtbe1;
};

#ifdef TARGET_PPC64

#define TLBIE_F_RIC_SHIFT       0
#define TLBIE_F_PRS_SHIFT       2
#define TLBIE_F_R_SHIFT         3
#define TLBIE_F_LOCAL_SHIFT     4

#define TLBIE_F_RIC_MASK        (3 << TLBIE_F_RIC_SHIFT)
#define TLBIE_F_PRS             (1 << TLBIE_F_PRS_SHIFT)
#define TLBIE_F_R               (1 << TLBIE_F_R_SHIFT)
#define TLBIE_F_LOCAL           (1 << TLBIE_F_LOCAL_SHIFT)

static inline bool ppc64_use_proc_tbl(PowerPCCPU *cpu)
{
    return !!(cpu->env.spr[SPR_LPCR] & LPCR_UPRT);
}

bool ppc64_v3_get_pate(PowerPCCPU *cpu, target_ulong lpid,
                       ppc_v3_pate_t *entry);

static inline bool ppc64_v3_radix(PowerPCCPU *cpu)
{
    return !!(cpu->env.spr[SPR_LPCR] & LPCR_HR);
}

#endif

#endif

#endif
