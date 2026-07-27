/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Definition of TranslationBlock.
 *  Copyright (c) 2003 Fabrice Bellard
 */

#ifndef EXEC_TRANSLATION_BLOCK_H
#define EXEC_TRANSLATION_BLOCK_H

#include "qemu/atomic.h"
#include "qemu/thread.h"
#include "exec/cpu-common.h"
#include "exec/vaddr.h"
#ifdef CONFIG_USER_ONLY
#include "qemu/interval-tree.h"
#include "exec/target_page.h"
#endif

#if defined(CONFIG_USER_ONLY)
typedef vaddr tb_page_addr_t;
#define TB_PAGE_ADDR_FMT "%" VADDR_PRIx
#else
typedef ram_addr_t tb_page_addr_t;
#define TB_PAGE_ADDR_FMT RAM_ADDR_FMT
#endif

struct tb_tc {
    const void *ptr;
    size_t size;
};

struct TranslationBlock {
    /*
     * Guest PC corresponding to this block.  This must be the true
     * virtual address.  Therefore e.g. x86 stores EIP + CS_BASE, and
     * targets like Arm, MIPS, HP-PA, which reuse low bits for ISA or
     * privilege, must store those bits elsewhere.
     *
     * If CF_PCREL, the opcodes for the TranslationBlock are written
     * such that the TB is associated only with the physical page and
     * may be run in any virtual address context.  In this case, PC
     * must always be taken from ENV in a target-specific manner.
     * Unwind information is taken as offsets from the page, to be
     * deposited into the "current" PC.
     */
    vaddr pc;

    uint64_t cs_base;

    uint32_t flags;
    uint32_t cflags;

#define CF_COUNT_MASK    0x000001ff
#define CF_NO_GOTO_TB    0x00000200
#define CF_NO_GOTO_PTR   0x00000400
#define CF_SINGLE_STEP   0x00000800
#define CF_MEMI_ONLY     0x00001000
#define CF_USE_ICOUNT    0x00002000
#define CF_INVALID       0x00004000
#define CF_PARALLEL      0x00008000
#define CF_NOIRQ         0x00010000
#define CF_PCREL         0x00020000
#define CF_BP_PAGE       0x00040000
#define CF_CLUSTER_MASK  0xff000000
#define CF_CLUSTER_SHIFT 24

    uint16_t size;
    uint16_t icount;
    uint64_t ihash;

    struct tb_tc tc;

#ifdef CONFIG_USER_ONLY
    IntervalTreeNode itree;
#else
    uintptr_t page_next[2];
    tb_page_addr_t page_addr[2];
#endif

    QemuSpin jmp_lock;

#define TB_JMP_OFFSET_INVALID 0xffff
    uint16_t jmp_reset_offset[2];
    uint16_t jmp_insn_offset[2];
    uintptr_t jmp_target_addr[2];

    uintptr_t jmp_list_head;
    uintptr_t jmp_list_next[2];
    uintptr_t jmp_dest[2];
};

#define CODE_GEN_ALIGN  16

static inline uint32_t tb_cflags(const TranslationBlock *tb)
{
    return qatomic_read(&tb->cflags);
}

bool tcg_cflags_has(CPUState *cpu, uint32_t flags);
void tcg_cflags_set(CPUState *cpu, uint32_t flags);

static inline tb_page_addr_t tb_page_addr0(const TranslationBlock *tb)
{
#ifdef CONFIG_USER_ONLY
    return tb->itree.start;
#else
    return tb->page_addr[0];
#endif
}

static inline tb_page_addr_t tb_page_addr1(const TranslationBlock *tb)
{
#ifdef CONFIG_USER_ONLY
    tb_page_addr_t next = tb->itree.last & TARGET_PAGE_MASK;
    return next == (tb->itree.start & TARGET_PAGE_MASK) ? -1 : next;
#else
    return tb->page_addr[1];
#endif
}

static inline void tb_set_page_addr0(TranslationBlock *tb,
                                     tb_page_addr_t addr)
{
#ifdef CONFIG_USER_ONLY
    tb->itree.start = addr;
    tb->itree.last = addr;
#else
    tb->page_addr[0] = addr;
#endif
}

static inline void tb_set_page_addr1(TranslationBlock *tb,
                                     tb_page_addr_t addr)
{
#ifdef CONFIG_USER_ONLY
    tb->itree.last = addr;
#else
    tb->page_addr[1] = addr;
#endif
}

void tb_invalidate_phys_range(CPUState *cpu, tb_page_addr_t start,
                              tb_page_addr_t last);

#endif
