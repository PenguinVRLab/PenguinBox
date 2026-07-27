/*
 * QEMU page protection declarations.
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */
#ifndef USER_PAGE_PROTECTION_H
#define USER_PAGE_PROTECTION_H

#ifndef CONFIG_USER_ONLY
#error Cannot include this header from system emulation
#endif

#include "exec/vaddr.h"
#include "exec/translation-block.h"

int page_unprotect(CPUState *cpu, tb_page_addr_t address, uintptr_t pc);

int page_get_flags(vaddr address);

void page_set_flags(vaddr start, vaddr last, int set_flags, int clr_flags);

void page_reset_target_data(vaddr start, vaddr last);

bool page_check_range(vaddr start, vaddr last, int flags);

bool page_check_range_empty(vaddr start, vaddr last);

vaddr page_find_range_empty(vaddr min, vaddr max, vaddr len, vaddr align);

__attribute__((returns_nonnull))
void *page_get_target_data(vaddr address, size_t size);

typedef int (*walk_memory_regions_fn)(void *, vaddr, vaddr, int);
int walk_memory_regions(void *, walk_memory_regions_fn);

void page_dump(FILE *f);

#endif
