/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef PPC_TARGET_ELF_H
#define PPC_TARGET_ELF_H

#include "target_ptrace.h"

#define ELF_MACHINE             PPC_ELF_MACHINE

#ifdef TARGET_PPC64
# define ELF_CLASS              ELFCLASS64
#else
# define ELF_CLASS              ELFCLASS32
# define EXSTACK_DEFAULT        true
#endif

#define HAVE_ELF_HWCAP          1
#define HAVE_ELF_HWCAP2         1
#define HAVE_ELF_CORE_DUMP      1

typedef struct target_elf_gregset_t {
    union {
        struct target_pt_regs pt;
        abi_ulong reserved[48];
    };
} target_elf_gregset_t;

#ifndef TARGET_PPC64
# define VDSO_HEADER  "vdso-32.c.inc"
#elif TARGET_BIG_ENDIAN
# define VDSO_HEADER  "vdso-64.c.inc"
#else
# define VDSO_HEADER  "vdso-64le.c.inc"
#endif

#define DLINFO_ARCH_ITEMS       5
#define ARCH_DLINFO                                     \
    do {                                                \
        PowerPCCPU *cpu = POWERPC_CPU(thread_cpu);              \
                                             \
        NEW_AUX_ENT(AT_IGNOREPPC, AT_IGNOREPPC);        \
        NEW_AUX_ENT(AT_IGNOREPPC, AT_IGNOREPPC);        \
        NEW_AUX_ENT(AT_DCACHEBSIZE, cpu->env.dcache_line_size); \
        NEW_AUX_ENT(AT_ICACHEBSIZE, cpu->env.icache_line_size); \
        NEW_AUX_ENT(AT_UCACHEBSIZE, 0);                 \
    } while (0)

#endif
