/*
 * MicroBlaze cpu parameters for qemu.
 *
 * Copyright (c) 2009 Edgar E. Iglesias
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef MICROBLAZE_CPU_PARAM_H
#define MICROBLAZE_CPU_PARAM_H

#ifdef CONFIG_USER_ONLY
#define TARGET_PHYS_ADDR_SPACE_BITS 32
#define TARGET_VIRT_ADDR_SPACE_BITS 32
#else
#define TARGET_PHYS_ADDR_SPACE_BITS 64
#define TARGET_VIRT_ADDR_SPACE_BITS 64
#endif

#define TARGET_PAGE_BITS 12

#define TARGET_INSN_START_EXTRA_WORDS 1

#endif
