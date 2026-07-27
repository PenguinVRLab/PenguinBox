/*
 * ARM MemTag operation helpers.
 *
 * This code is licensed under the GNU GPL v2 or later.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef TARGET_ARM_MTE_H
#define TARGET_ARM_MTE_H

#include "exec/mmu-access-type.h"

uint8_t *allocation_tag_mem_probe(CPUARMState *env, int ptr_mmu_idx,
                                  uint64_t ptr, MMUAccessType ptr_access,
                                  int ptr_size, MMUAccessType tag_access,
                                  bool probe, uintptr_t ra);

int load_tag1(uint64_t ptr, uint8_t *mem);

void store_tag1(uint64_t ptr, uint8_t *mem, int tag);

#endif
