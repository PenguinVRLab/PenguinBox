/*
 * Alpha cpu parameters for qemu.
 *
 * Copyright (c) 2007 Jocelyn Mayer
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef ALPHA_CPU_PARAM_H
#define ALPHA_CPU_PARAM_H

#define TARGET_PHYS_ADDR_SPACE_BITS  44

#ifdef CONFIG_USER_ONLY
# define TARGET_PAGE_BITS_VARY
# define TARGET_VIRT_ADDR_SPACE_BITS  63
#else
# define TARGET_PAGE_BITS 13
# define TARGET_VIRT_ADDR_SPACE_BITS  (30 + TARGET_PAGE_BITS)
#endif

#define TARGET_INSN_START_EXTRA_WORDS 0

#endif
