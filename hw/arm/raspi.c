/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 * Upstreaming code cleanup [including bcm2835_*] (c) 2013 Jan Petrous
 *
 * Rasperry Pi 2 emulation Copyright (c) 2015, Microsoft
 * Written by Andrew Baumann
 *
 * Raspberry Pi 3 emulation Copyright (c) 2018 Zoltán Baldaszti
 * Upstream code cleanup (c) 2018 Pekka Enberg
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "hw/arm/boot.h"
#include "hw/arm/bcm2836.h"
#include "hw/arm/bcm2838.h"
#include "hw/arm/raspi_platform.h"
#include "hw/registerfields.h"
#include "qemu/error-report.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/arm/boot.h"
#include "hw/arm/machines-qom.h"
#include "qom/object.h"

#define TYPE_RASPI_MACHINE  MACHINE_TYPE_NAME("raspi-common")
OBJECT_DECLARE_SIMPLE_TYPE(RaspiMachineState, RASPI_MACHINE)

#define SMPBOOT_ADDR    0x300
#define MVBAR_ADDR      0x400
#define BOARDSETUP_ADDR (MVBAR_ADDR + 0x20)
#define FIRMWARE_ADDR_2 0x8000
#define FIRMWARE_ADDR_3 0x80000
#define SPINTABLE_ADDR  0xd8

struct RaspiMachineState {
    RaspiBaseMachineState parent_obj;
    BCM283XState soc;
};

FIELD(REV_CODE, REVISION,           0, 4);
FIELD(REV_CODE, TYPE,               4, 8);
FIELD(REV_CODE, PROCESSOR,         12, 4);
FIELD(REV_CODE, MANUFACTURER,      16, 4);
FIELD(REV_CODE, MEMORY_SIZE,       20, 3);
FIELD(REV_CODE, STYLE,             23, 1);

typedef enum RaspiProcessorId {
    PROCESSOR_ID_BCM2835 = 0,
    PROCESSOR_ID_BCM2836 = 1,
    PROCESSOR_ID_BCM2837 = 2,
    PROCESSOR_ID_BCM2838 = 3,
} RaspiProcessorId;

static const struct {
    const char *type;
    int cores_count;
} soc_property[] = {
    [PROCESSOR_ID_BCM2835] = {TYPE_BCM2835, 1},
    [PROCESSOR_ID_BCM2836] = {TYPE_BCM2836, BCM283X_NCPUS},
    [PROCESSOR_ID_BCM2837] = {TYPE_BCM2837, BCM283X_NCPUS},
    [PROCESSOR_ID_BCM2838] = {TYPE_BCM2838, BCM283X_NCPUS},
};

uint64_t board_ram_size(uint32_t board_rev)
{
    assert(FIELD_EX32(board_rev, REV_CODE, STYLE));
    return 256 * MiB << FIELD_EX32(board_rev, REV_CODE, MEMORY_SIZE);
}

static RaspiProcessorId board_processor_id(uint32_t board_rev)
{
    int proc_id = FIELD_EX32(board_rev, REV_CODE, PROCESSOR);

    assert(FIELD_EX32(board_rev, REV_CODE, STYLE));
    assert(proc_id < ARRAY_SIZE(soc_property) && soc_property[proc_id].type);

    return proc_id;
}

const char *board_soc_type(uint32_t board_rev)
{
    return soc_property[board_processor_id(board_rev)].type;
}

static int cores_count(uint32_t board_rev)
{
    return soc_property[board_processor_id(board_rev)].cores_count;
}

static const char *board_type(uint32_t board_rev)
{
    static const char *types[] = {
        "A", "B", "A+", "B+", "2B", "Alpha", "CM1", NULL, "3B", "Zero",
        "CM3", NULL, "Zero W", "3B+", "3A+", NULL, "CM3+", "4B",
    };
    assert(FIELD_EX32(board_rev, REV_CODE, STYLE));
    int bt = FIELD_EX32(board_rev, REV_CODE, TYPE);
    if (bt >= ARRAY_SIZE(types) || !types[bt]) {
        return "Unknown";
    }
    return types[bt];
}

static void write_smpboot(ARMCPU *cpu, const struct arm_boot_info *info)
{
    static const ARMInsnFixup smpboot[] = {
        { 0xe1a0e00f },
        { 0xe3a0fe00 + (BOARDSETUP_ADDR >> 4) },
        { 0xee100fb0 },
        { 0xe7e10050 },
        { 0xe59f5014 },
        { 0xe320f001 },
        { 0xe7953200 },
        { 0xe3530000 },
        { 0x0afffffb },
        { 0xe7853200 },
        { 0xe12fff13 },
        { 0x400000cc },
        { 0, FIXUP_TERMINATOR }
    };
    static const uint32_t fixupcontext[FIXUP_MAX] = { 0 };

    QEMU_BUILD_BUG_ON(SMPBOOT_ADDR + sizeof(smpboot) > MVBAR_ADDR);
    QEMU_BUILD_BUG_ON((BOARDSETUP_ADDR & 0xf) != 0
                      || (BOARDSETUP_ADDR >> 4) >= 0x100);

    arm_write_bootloader("raspi_smpboot", arm_boot_address_space(cpu, info),
                         info->smp_loader_start, smpboot, fixupcontext);
}

static void write_smpboot64(ARMCPU *cpu, const struct arm_boot_info *info)
{
    AddressSpace *as = arm_boot_address_space(cpu, info);
    static const ARMInsnFixup smpboot[] = {
        { 0xd2801b05 },
        { 0xd53800a6 },
        { 0x924004c6 },
        { 0xd503205f },
        { 0xf86678a4 },
        { 0xb4ffffc4 },
        { 0xd2800000 },
        { 0xd2800001 },
        { 0xd2800002 },
        { 0xd2800003 },
        { 0xd61f0080 },
        { 0, FIXUP_TERMINATOR }
    };
    static const uint32_t fixupcontext[FIXUP_MAX] = { 0 };

    static const uint64_t spintables[] = {
        0, 0, 0, 0
    };

    arm_write_bootloader("raspi_smpboot", as, info->smp_loader_start,
                         smpboot, fixupcontext);
    rom_add_blob_fixed_as("raspi_spintables", spintables, sizeof(spintables),
                          SPINTABLE_ADDR, as);
}

static void write_board_setup(ARMCPU *cpu, const struct arm_boot_info *info)
{
    arm_write_secure_board_setup_dummy_smc(cpu, info, MVBAR_ADDR);
}

static void reset_secondary(ARMCPU *cpu, const struct arm_boot_info *info)
{
    CPUState *cs = CPU(cpu);
    cpu_set_pc(cs, info->smp_loader_start);
}

static void setup_boot(MachineState *machine, ARMCPU *cpu,
                       RaspiProcessorId processor_id, size_t ram_size)
{
    RaspiBaseMachineState *s = RASPI_BASE_MACHINE(machine);
    int r;

    s->binfo.ram_size = ram_size;

    if (processor_id <= PROCESSOR_ID_BCM2836) {
        s->binfo.board_setup_addr = BOARDSETUP_ADDR;
        s->binfo.write_board_setup = write_board_setup;
        s->binfo.secure_board_setup = true;
        s->binfo.secure_boot = true;
    }

    if (processor_id >= PROCESSOR_ID_BCM2836) {
        s->binfo.smp_loader_start = SMPBOOT_ADDR;
        if (processor_id == PROCESSOR_ID_BCM2836) {
            s->binfo.write_secondary_boot = write_smpboot;
        } else {
            s->binfo.write_secondary_boot = write_smpboot64;
        }
        s->binfo.secondary_cpu_reset_hook = reset_secondary;
    }

    if (machine->firmware) {
        hwaddr firmware_addr = processor_id <= PROCESSOR_ID_BCM2836
                             ? FIRMWARE_ADDR_2 : FIRMWARE_ADDR_3;
        r = load_image_targphys(machine->firmware, firmware_addr,
                                ram_size - firmware_addr, NULL);
        if (r < 0) {
            error_report("Failed to load firmware from %s", machine->firmware);
            exit(1);
        }

        s->binfo.entry = firmware_addr;
        s->binfo.firmware_loaded = true;
    }

    arm_load_kernel(cpu, machine, &s->binfo);
}

void raspi_base_machine_init(MachineState *machine,
                             BCM283XBaseState *soc)
{
    RaspiBaseMachineClass *mc = RASPI_BASE_MACHINE_GET_CLASS(machine);
    uint32_t board_rev = mc->board_rev;
    uint64_t ram_size = board_ram_size(board_rev);
    uint32_t vcram_base, vcram_size;
    size_t boot_ram_size;
    DriveInfo *di;
    BlockBackend *blk;
    BusState *bus;
    DeviceState *carddev;

    if (machine->ram_size != ram_size) {
        char *size_str = size_to_str(ram_size);
        error_report("Invalid RAM size, should be %s", size_str);
        g_free(size_str);
        exit(1);
    }

    memory_region_add_subregion_overlap(get_system_memory(), 0,
                                        machine->ram, 0);

    object_property_add_const_link(OBJECT(soc), "ram", OBJECT(machine->ram));
    object_property_set_int(OBJECT(soc), "board-rev", board_rev,
                            &error_abort);
    object_property_set_str(OBJECT(soc), "command-line",
                            machine->kernel_cmdline, &error_abort);
    qdev_realize(DEVICE(soc), NULL, &error_fatal);

    di = drive_get(IF_SD, 0, 0);
    blk = di ? blk_by_legacy_dinfo(di) : NULL;
    bus = qdev_get_child_bus(DEVICE(soc), "sd-bus");
    if (bus == NULL) {
        error_report("No SD bus found in SOC object");
        exit(1);
    }
    carddev = qdev_new(TYPE_SD_CARD);
    qdev_prop_set_drive_err(carddev, "drive", blk, &error_fatal);
    qdev_realize_and_unref(carddev, bus, &error_fatal);

    vcram_size = object_property_get_uint(OBJECT(soc), "vcram-size",
                                          &error_abort);
    vcram_base = object_property_get_uint(OBJECT(soc), "vcram-base",
                                          &error_abort);

    if (vcram_base == 0) {
        vcram_base = ram_size - vcram_size;
    }
    boot_ram_size = MIN(vcram_base, UPPER_RAM_BASE - vcram_size);

    setup_boot(machine, &soc->cpu[0].core, board_processor_id(board_rev),
               boot_ram_size);
}

void raspi_machine_init(MachineState *machine)
{
    RaspiMachineState *s = RASPI_MACHINE(machine);
    RaspiBaseMachineState *s_base = RASPI_BASE_MACHINE(machine);
    RaspiBaseMachineClass *mc = RASPI_BASE_MACHINE_GET_CLASS(machine);
    BCM283XState *soc = &s->soc;

    s_base->binfo.board_id = MACH_TYPE_BCM2708;

    object_initialize_child(OBJECT(machine), "soc", soc,
                            board_soc_type(mc->board_rev));
    raspi_base_machine_init(machine, &soc->parent_obj);
}

void raspi_machine_class_common_init(MachineClass *mc,
                                     uint32_t board_rev)
{
    mc->desc = g_strdup_printf("Raspberry Pi %s (revision 1.%u)",
                               board_type(board_rev),
                               FIELD_EX32(board_rev, REV_CODE, REVISION));
    mc->block_default_type = IF_SD;
    mc->no_parallel = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->default_cpus = mc->min_cpus = mc->max_cpus = cores_count(board_rev);
    mc->default_ram_size = board_ram_size(board_rev);
    mc->default_ram_id = "ram";
};

static void raspi_machine_class_init(MachineClass *mc,
                                     uint32_t board_rev)
{
    raspi_machine_class_common_init(mc, board_rev);
    mc->init = raspi_machine_init;
};

static void raspi0_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    RaspiBaseMachineClass *rmc = RASPI_BASE_MACHINE_CLASS(oc);

    mc->auto_create_sdcard = true;
    rmc->board_rev = 0x920092;
    raspi_machine_class_init(mc, rmc->board_rev);
};

static void raspi1ap_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    RaspiBaseMachineClass *rmc = RASPI_BASE_MACHINE_CLASS(oc);

    mc->auto_create_sdcard = true;
    rmc->board_rev = 0x900021;
    raspi_machine_class_init(mc, rmc->board_rev);
};

static void raspi2b_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    RaspiBaseMachineClass *rmc = RASPI_BASE_MACHINE_CLASS(oc);

    mc->auto_create_sdcard = true;
    rmc->board_rev = 0xa21041;
    raspi_machine_class_init(mc, rmc->board_rev);
};

#ifdef TARGET_AARCH64
static void raspi3ap_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    RaspiBaseMachineClass *rmc = RASPI_BASE_MACHINE_CLASS(oc);

    mc->auto_create_sdcard = true;
    rmc->board_rev = 0x9020e0;
    raspi_machine_class_init(mc, rmc->board_rev);
};

static void raspi3b_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    RaspiBaseMachineClass *rmc = RASPI_BASE_MACHINE_CLASS(oc);

    mc->auto_create_sdcard = true;
    rmc->board_rev = 0xa02082;
    raspi_machine_class_init(mc, rmc->board_rev);
};
#endif

static const TypeInfo raspi_machine_types[] = {
    {
        .name           = MACHINE_TYPE_NAME("raspi0"),
        .parent         = TYPE_RASPI_MACHINE,
        .class_init     = raspi0_machine_class_init,
        .interfaces     = arm_machine_interfaces,
    }, {
        .name           = MACHINE_TYPE_NAME("raspi1ap"),
        .parent         = TYPE_RASPI_MACHINE,
        .class_init     = raspi1ap_machine_class_init,
        .interfaces     = arm_machine_interfaces,
    }, {
        .name           = MACHINE_TYPE_NAME("raspi2b"),
        .parent         = TYPE_RASPI_MACHINE,
        .class_init     = raspi2b_machine_class_init,
        .interfaces     = arm_machine_interfaces,
#ifdef TARGET_AARCH64
    }, {
        .name           = MACHINE_TYPE_NAME("raspi3ap"),
        .parent         = TYPE_RASPI_MACHINE,
        .class_init     = raspi3ap_machine_class_init,
        .interfaces     = aarch64_machine_interfaces,
    }, {
        .name           = MACHINE_TYPE_NAME("raspi3b"),
        .parent         = TYPE_RASPI_MACHINE,
        .class_init     = raspi3b_machine_class_init,
        .interfaces     = aarch64_machine_interfaces,
#endif
    }, {
        .name           = TYPE_RASPI_MACHINE,
        .parent         = TYPE_RASPI_BASE_MACHINE,
        .instance_size  = sizeof(RaspiMachineState),
        .abstract       = true,
    }, {
        .name           = TYPE_RASPI_BASE_MACHINE,
        .parent         = TYPE_MACHINE,
        .instance_size  = sizeof(RaspiBaseMachineState),
        .class_size     = sizeof(RaspiBaseMachineClass),
        .abstract       = true,
    }
};

DEFINE_TYPES(raspi_machine_types)
