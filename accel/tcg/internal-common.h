/*
 * Internal execution defines for qemu (target agnostic)
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef ACCEL_TCG_INTERNAL_COMMON_H
#define ACCEL_TCG_INTERNAL_COMMON_H

#include "exec/cpu-common.h"
#include "exec/translation-block.h"
#include "exec/mmap-lock.h"
#include "accel/tcg/tb-cpu-state.h"

extern int64_t max_delay;
extern int64_t max_advance;

extern bool one_insn_per_tb;

extern bool icount_align_option;

static inline bool cpu_in_serial_context(CPUState *cs)
{
    return !tcg_cflags_has(cs, CF_PARALLEL) || cpu_in_exclusive_context(cs);
}

static inline bool cpu_plugin_mem_cbs_enabled(const CPUState *cpu)
{
#ifdef CONFIG_PLUGIN
    return !!cpu->neg.plugin_mem_cbs;
#else
    return false;
#endif
}

TranslationBlock *tb_gen_code(CPUState *cpu, TCGTBCPUState s);
void page_init(void);
void tb_htable_init(void);
TranslationBlock *inv_tb_htable_lookup(CPUState *cpu, TCGTBCPUState s);
void tb_reset_jump(TranslationBlock *tb, int n);
TranslationBlock *tb_link_page(TranslationBlock *tb);
void cpu_restore_state_from_tb(CPUState *cpu, TranslationBlock *tb,
                               uintptr_t host_pc);

void tlb_init(CPUState *cpu);
void tlb_destroy(CPUState *cpu);

bool tcg_exec_realizefn(CPUState *cpu, Error **errp);
void tcg_exec_unrealizefn(CPUState *cpu);

uint32_t curr_cflags(CPUState *cpu);

void tb_check_watchpoint(CPUState *cpu, uintptr_t retaddr);

tb_page_addr_t get_page_addr_code_hostp(CPUArchState *env, vaddr addr,
                                        void **hostp);

static inline tb_page_addr_t get_page_addr_code(CPUArchState *env,
                                                vaddr addr)
{
    return get_page_addr_code_hostp(env, addr, NULL);
}

#ifdef CONFIG_USER_ONLY
#define assert_memory_lock() tcg_debug_assert(have_mmap_lock())
#else
#define assert_memory_lock()
#endif

#if defined(CONFIG_SOFTMMU) && defined(CONFIG_DEBUG_TCG)
void assert_no_pages_locked(void);
#else
static inline void assert_no_pages_locked(void) { }
#endif

#ifdef CONFIG_USER_ONLY
static inline void page_table_config_init(void) { }
#else
void page_table_config_init(void);
#endif

#ifndef CONFIG_USER_ONLY
G_NORETURN void cpu_io_recompile(CPUState *cpu, uintptr_t retaddr);
#endif

void tb_phys_invalidate(TranslationBlock *tb, tb_page_addr_t page_addr);
void tb_set_jmp_target(TranslationBlock *tb, int n, uintptr_t addr);

void tcg_get_stats(AccelState *accel, GString *buf);

#endif
