/*
 * ARM cpu parameters for qemu.
 *
 * Copyright (c) 2003 Fabrice Bellard
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef ARM_CPU_PARAM_H
#define ARM_CPU_PARAM_H

#ifdef TARGET_AARCH64
# define TARGET_PHYS_ADDR_SPACE_BITS  52
# define TARGET_VIRT_ADDR_SPACE_BITS  52
#else
# define TARGET_PHYS_ADDR_SPACE_BITS  40
# define TARGET_VIRT_ADDR_SPACE_BITS  32
#endif

#ifdef CONFIG_USER_ONLY
# if defined(TARGET_AARCH64) && defined(CONFIG_LINUX)
#  define TARGET_PAGE_BITS_VARY
# else
#  define TARGET_PAGE_BITS 12
# endif
#else
# define TARGET_PAGE_BITS_VARY
# define TARGET_PAGE_BITS_LEGACY 10
#endif

#define TARGET_INSN_START_EXTRA_WORDS 2

#endif
