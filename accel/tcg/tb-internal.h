/*
 * TranslationBlock internal declarations (target specific)
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef ACCEL_TCG_TB_INTERNAL_TARGET_H
#define ACCEL_TCG_TB_INTERNAL_TARGET_H

#include "exec/translation-block.h"

#define GETPC_ADJ   2

void tb_lock_page0(tb_page_addr_t);

#ifdef CONFIG_USER_ONLY
static inline void tb_lock_page1(tb_page_addr_t p0, tb_page_addr_t p1)
{
    tb_lock_page0(p1);
}

static inline void tb_unlock_page1(tb_page_addr_t p0, tb_page_addr_t p1) { }
static inline void tb_unlock_pages(TranslationBlock *tb) { }
#else
void tb_lock_page1(tb_page_addr_t, tb_page_addr_t);
void tb_unlock_page1(tb_page_addr_t, tb_page_addr_t);
void tb_unlock_pages(TranslationBlock *);
#endif

#ifdef CONFIG_SOFTMMU
void tb_invalidate_phys_range_fast(CPUState *cpu, ram_addr_t ram_addr,
                                   unsigned size, uintptr_t retaddr);
#endif

bool tb_invalidate_phys_page_unwind(CPUState *cpu, tb_page_addr_t addr,
                                    uintptr_t pc);

#endif
