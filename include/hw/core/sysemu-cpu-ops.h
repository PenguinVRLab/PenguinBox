/*
 * CPU operations specific to system emulation
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef SYSTEM_CPU_OPS_H
#define SYSTEM_CPU_OPS_H

#include "hw/core/cpu.h"

typedef struct SysemuCPUOps {
    bool (*has_work)(CPUState *cpu);
    bool (*get_memory_mapping)(CPUState *cpu, MemoryMappingList *list,
                               Error **errp);
    bool (*get_paging_enabled)(const CPUState *cpu);
    hwaddr (*get_phys_page_debug)(CPUState *cpu, vaddr addr);
    hwaddr (*get_phys_page_attrs_debug)(CPUState *cpu, vaddr addr,
                                        MemTxAttrs *attrs);
    int (*asidx_from_attrs)(CPUState *cpu, MemTxAttrs attrs);
    GuestPanicInformation* (*get_crash_info)(CPUState *cpu);
    int (*write_elf32_note)(WriteCoreDumpFunction f, CPUState *cpu,
                            int cpuid, DumpState *s);
    int (*write_elf64_note)(WriteCoreDumpFunction f, CPUState *cpu,
                            int cpuid, DumpState *s);
    int (*write_elf32_qemunote)(WriteCoreDumpFunction f, CPUState *cpu,
                                DumpState *s);
    int (*write_elf64_qemunote)(WriteCoreDumpFunction f, CPUState *cpu,
                                DumpState *s);
    bool (*virtio_is_big_endian)(CPUState *cpu);

    const VMStateDescription *legacy_vmsd;

} SysemuCPUOps;

#endif
