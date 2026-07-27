/*
 * PowerPC cpu parameters for qemu.
 *
 * Copyright (c) 2007 Jocelyn Mayer
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef PPC_CPU_PARAM_H
#define PPC_CPU_PARAM_H

#ifdef TARGET_PPC64
#define TARGET_PHYS_ADDR_SPACE_BITS 62
# ifdef TARGET_ABI32
#  define TARGET_VIRT_ADDR_SPACE_BITS 32
# else
#  define TARGET_VIRT_ADDR_SPACE_BITS 64
# endif
#else
# define TARGET_PHYS_ADDR_SPACE_BITS 36
# define TARGET_VIRT_ADDR_SPACE_BITS 32
#endif

#ifdef CONFIG_USER_ONLY
# define TARGET_PAGE_BITS_VARY
#else
# define TARGET_PAGE_BITS 12
#endif

#define TARGET_INSN_START_EXTRA_WORDS 0

#endif
