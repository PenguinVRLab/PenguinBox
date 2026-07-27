/*
 * ARM kernel loader.
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the LGPL.
 *
 */

#ifndef HW_ARM_BOOT_H
#define HW_ARM_BOOT_H

#include "target/arm/cpu-qom.h"
#include "qemu/notify.h"

typedef enum {
    ARM_ENDIANNESS_UNKNOWN = 0,
    ARM_ENDIANNESS_LE,
    ARM_ENDIANNESS_BE8,
    ARM_ENDIANNESS_BE32,
} arm_endianness;

void armv7m_load_kernel(ARMCPU *cpu, const char *kernel_filename,
                        hwaddr mem_base, int mem_size);

struct arm_boot_info {
    uint64_t ram_size;
    const char *kernel_filename;
    const char *kernel_cmdline;
    const char *initrd_filename;
    const char *dtb_filename;
    hwaddr loader_start;
    hwaddr dtb_start;
    hwaddr dtb_limit;
    bool skip_dtb_autoload;
    hwaddr smp_loader_start;
    hwaddr smp_bootreg_addr;
    hwaddr gic_cpu_if_addr;
    int board_id;
    bool secure_boot;
    int (*atag_board)(const struct arm_boot_info *info, void *p);
    void (*write_secondary_boot)(ARMCPU *cpu,
                                 const struct arm_boot_info *info);
    void (*secondary_cpu_reset_hook)(ARMCPU *cpu,
                                     const struct arm_boot_info *info);
    void *(*get_dtb)(const struct arm_boot_info *info, int *size);
    void (*modify_dtb)(const struct arm_boot_info *info, void *fdt);
    int psci_conduit;
    int is_linux;
    hwaddr initrd_start;
    hwaddr initrd_size;
    hwaddr entry;

    bool firmware_loaded;

    hwaddr board_setup_addr;
    void (*write_board_setup)(ARMCPU *cpu,
                              const struct arm_boot_info *info);

    bool secure_board_setup;

    arm_endianness endianness;

    ARMCPU *primary_cpu;
};

void arm_load_kernel(ARMCPU *cpu, MachineState *ms, struct arm_boot_info *info);

AddressSpace *arm_boot_address_space(ARMCPU *cpu,
                                     const struct arm_boot_info *info);

int arm_load_dtb(hwaddr addr, const struct arm_boot_info *binfo,
                 hwaddr addr_limit, AddressSpace *as, MachineState *ms,
                 ARMCPU *cpu);

void arm_write_secure_board_setup_dummy_smc(ARMCPU *cpu,
                                            const struct arm_boot_info *info,
                                            hwaddr mvbar_addr);

typedef enum {
    FIXUP_NONE = 0,
    FIXUP_TERMINATOR,
    FIXUP_BOARDID,
    FIXUP_BOARD_SETUP,
    FIXUP_ARGPTR_LO,
    FIXUP_ARGPTR_HI,
    FIXUP_ENTRYPOINT_LO,
    FIXUP_ENTRYPOINT_HI,
    FIXUP_GIC_CPU_IF,
    FIXUP_BOOTREG,
    FIXUP_DSB,
    FIXUP_MAX,
} FixupType;

typedef struct ARMInsnFixup {
    uint32_t insn;
    FixupType fixup;
} ARMInsnFixup;

void arm_write_bootloader(const char *name,
                          AddressSpace *as, hwaddr addr,
                          const ARMInsnFixup *insns,
                          const uint32_t *fixupcontext);

#endif
