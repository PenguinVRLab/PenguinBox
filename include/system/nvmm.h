/*
 * Copyright (c) 2018-2019 Maxime Villard, All rights reserved.
 *
 * NetBSD Virtual Machine Monitor (NVMM) accelerator support.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/* header to be included in non-NVMM-specific code */

#ifndef QEMU_NVMM_H
#define QEMU_NVMM_H

#ifdef COMPILING_PER_TARGET
# ifdef CONFIG_NVMM
#  define CONFIG_NVMM_IS_POSSIBLE
# endif
#else
# define CONFIG_NVMM_IS_POSSIBLE
#endif

#ifdef CONFIG_NVMM_IS_POSSIBLE
extern bool nvmm_allowed;
#define nvmm_enabled() (nvmm_allowed)
#else
#define nvmm_enabled() 0
#endif

#endif
