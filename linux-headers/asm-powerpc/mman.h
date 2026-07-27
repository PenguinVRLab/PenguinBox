/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_POWERPC_MMAN_H
#define _ASM_POWERPC_MMAN_H

#include <asm-generic/mman-common.h>


#define PROT_SAO	0x10

#define MAP_RENAME      MAP_ANONYMOUS
#define MAP_NORESERVE   0x40
#define MAP_LOCKED	0x80

#define MAP_GROWSDOWN	0x0100
#define MAP_DENYWRITE	0x0800
#define MAP_EXECUTABLE	0x1000


#define MCL_CURRENT     0x2000
#define MCL_FUTURE      0x4000
#define MCL_ONFAULT	0x8000

#define PKEY_DISABLE_EXECUTE   0x4
#undef PKEY_ACCESS_MASK
#define PKEY_ACCESS_MASK       (PKEY_DISABLE_ACCESS |\
				PKEY_DISABLE_WRITE  |\
				PKEY_DISABLE_EXECUTE)
#endif
