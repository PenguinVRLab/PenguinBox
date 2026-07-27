/*
 * ARMV7M System emulation.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "hw/arm/armv7m.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/arm/boot.h"
#include "hw/loader.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-clock.h"
#include "elf.h"
#include "system/reset.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "target/arm/idau.h"
#include "target/arm/cpu.h"
#include "target/arm/cpu-features.h"
#include "target/arm/cpu-qom.h"
#include "migration/vmstate.h"

static inline hwaddr bitband_addr(BitBandState *s, hwaddr offset)
{
    return s->base | (offset & 0x1ffffff) >> 5;
}

static MemTxResult bitband_read(void *opaque, hwaddr offset,
                                uint64_t *data, unsigned size, MemTxAttrs attrs)
{
    BitBandState *s = opaque;
    uint8_t buf[4];
    MemTxResult res;
    int bitpos, bit;
    hwaddr addr;

    assert(size <= 4);

    addr = bitband_addr(s, offset) & (-size);
    res = address_space_read(&s->source_as, addr, attrs, buf, size);
    if (res) {
        return res;
    }
    bitpos = (offset >> 2) & ((size * 8) - 1);
    bit = (buf[bitpos >> 3] >> (bitpos & 7)) & 1;
    *data = bit;
    return MEMTX_OK;
}

static MemTxResult bitband_write(void *opaque, hwaddr offset, uint64_t value,
                                 unsigned size, MemTxAttrs attrs)
{
    BitBandState *s = opaque;
    uint8_t buf[4];
    MemTxResult res;
    int bitpos, bit;
    hwaddr addr;

    assert(size <= 4);

    addr = bitband_addr(s, offset) & (-size);
    res = address_space_read(&s->source_as, addr, attrs, buf, size);
    if (res) {
        return res;
    }
    bitpos = (offset >> 2) & ((size * 8) - 1);
    bit = 1 << (bitpos & 7);
    if (value & 1) {
        buf[bitpos >> 3] |= bit;
    } else {
        buf[bitpos >> 3] &= ~bit;
    }
    return address_space_write(&s->source_as, addr, attrs, buf, size);
}

static const MemoryRegionOps bitband_ops = {
    .read_with_attrs = bitband_read,
    .write_with_attrs = bitband_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static void bitband_init(Object *obj)
{
    BitBandState *s = BITBAND(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &bitband_ops, s,
                          "bitband", 0x02000000);
    sysbus_init_mmio(dev, &s->iomem);
}

static void bitband_realize(DeviceState *dev, Error **errp)
{
    BitBandState *s = BITBAND(dev);

    if (!s->source_memory) {
        error_setg(errp, "source-memory property not set");
        return;
    }

    address_space_init(&s->source_as, s->source_memory, "bitband-source");
}

static const hwaddr bitband_input_addr[ARMV7M_NUM_BITBANDS] = {
    0x20000000, 0x40000000
};

static const hwaddr bitband_output_addr[ARMV7M_NUM_BITBANDS] = {
    0x22000000, 0x42000000
};

static MemTxResult v7m_sysreg_ns_write(void *opaque, hwaddr addr,
                                       uint64_t value, unsigned size,
                                       MemTxAttrs attrs)
{
    MemoryRegion *mr = opaque;

    if (attrs.secure) {
        attrs.secure = 0;
        return memory_region_dispatch_write(mr, addr, value,
                                            size_memop(size) | MO_LE, attrs);
    } else {
        if (attrs.user) {
            return MEMTX_ERROR;
        }
        return MEMTX_OK;
    }
}

static MemTxResult v7m_sysreg_ns_read(void *opaque, hwaddr addr,
                                      uint64_t *data, unsigned size,
                                      MemTxAttrs attrs)
{
    MemoryRegion *mr = opaque;

    if (attrs.secure) {
        attrs.secure = 0;
        return memory_region_dispatch_read(mr, addr, data,
                                           size_memop(size) | MO_LE, attrs);
    } else {
        if (attrs.user) {
            return MEMTX_ERROR;
        }
        *data = 0;
        return MEMTX_OK;
    }
}

static const MemoryRegionOps v7m_sysreg_ns_ops = {
    .read_with_attrs = v7m_sysreg_ns_read,
    .write_with_attrs = v7m_sysreg_ns_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static MemTxResult v7m_systick_write(void *opaque, hwaddr addr,
                                     uint64_t value, unsigned size,
                                     MemTxAttrs attrs)
{
    ARMv7MState *s = opaque;
    MemoryRegion *mr;

    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->systick[attrs.secure]), 0);
    return memory_region_dispatch_write(mr, addr, value,
                                        size_memop(size) | MO_LE, attrs);
}

static MemTxResult v7m_systick_read(void *opaque, hwaddr addr,
                                    uint64_t *data, unsigned size,
                                    MemTxAttrs attrs)
{
    ARMv7MState *s = opaque;
    MemoryRegion *mr;

    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->systick[attrs.secure]), 0);
    return memory_region_dispatch_read(mr, addr, data,
                                       size_memop(size) | MO_LE, attrs);
}

static const MemoryRegionOps v7m_systick_ops = {
    .read_with_attrs = v7m_systick_read,
    .write_with_attrs = v7m_systick_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static MemTxResult ppb_default_read(void *opaque, hwaddr addr,
                                    uint64_t *data, unsigned size,
                                    MemTxAttrs attrs)
{
    qemu_log_mask(LOG_UNIMP, "Read of unassigned area of PPB: offset 0x%x\n",
                  (uint32_t)addr);
    if (attrs.user) {
        return MEMTX_ERROR;
    }
    *data = 0;
    return MEMTX_OK;
}

static MemTxResult ppb_default_write(void *opaque, hwaddr addr,
                                     uint64_t value, unsigned size,
                                     MemTxAttrs attrs)
{
    qemu_log_mask(LOG_UNIMP, "Write of unassigned area of PPB: offset 0x%x\n",
                  (uint32_t)addr);
    if (attrs.user) {
        return MEMTX_ERROR;
    }
    return MEMTX_OK;
}

static const MemoryRegionOps ppb_default_ops = {
    .read_with_attrs = ppb_default_read,
    .write_with_attrs = ppb_default_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 8,
};

static void armv7m_instance_init(Object *obj)
{
    ARMv7MState *s = ARMV7M(obj);
    int i;

    memory_region_init(&s->container, obj, "armv7m-container", UINT64_MAX);

    object_initialize_child(obj, "nvic", &s->nvic, TYPE_NVIC);
    object_property_add_alias(obj, "num-irq",
                              OBJECT(&s->nvic), "num-irq");
    object_property_add_alias(obj, "num-prio-bits",
                              OBJECT(&s->nvic), "num-prio-bits");

    object_initialize_child(obj, "systick-reg-ns", &s->systick[M_REG_NS],
                            TYPE_SYSTICK);

    for (i = 0; i < ARRAY_SIZE(s->bitband); i++) {
        object_initialize_child(obj, "bitband[*]", &s->bitband[i],
                                TYPE_BITBAND);
    }

    s->refclk = qdev_init_clock_in(DEVICE(obj), "refclk", NULL, NULL, 0);
    s->cpuclk = qdev_init_clock_in(DEVICE(obj), "cpuclk", NULL, NULL, 0);
}

static void armv7m_realize(DeviceState *dev, Error **errp)
{
    ARMv7MState *s = ARMV7M(dev);
    SysBusDevice *sbd;
    Error *err = NULL;
    int i;

    if (!s->board_memory) {
        error_setg(errp, "memory property was not set");
        return;
    }

    if (!clock_has_source(s->cpuclk)) {
        error_setg(errp, "armv7m: cpuclk must be connected");
        return;
    }

    memory_region_add_subregion_overlap(&s->container, 0, s->board_memory, -1);

    s->cpu = ARM_CPU(object_new_with_props(s->cpu_type, OBJECT(s), "cpu",
                                           &err, NULL));
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }

    object_property_set_link(OBJECT(s->cpu), "memory", OBJECT(&s->container),
                             &error_abort);
    if (object_property_find(OBJECT(s->cpu), "idau")) {
        object_property_set_link(OBJECT(s->cpu), "idau", s->idau,
                                 &error_abort);
    }
    if (object_property_find(OBJECT(s->cpu), "init-svtor")) {
        if (!object_property_set_uint(OBJECT(s->cpu), "init-svtor",
                                      s->init_svtor, errp)) {
            return;
        }
    }
    if (object_property_find(OBJECT(s->cpu), "init-nsvtor")) {
        if (!object_property_set_uint(OBJECT(s->cpu), "init-nsvtor",
                                      s->init_nsvtor, errp)) {
            return;
        }
    }
    if (object_property_find(OBJECT(s->cpu), "vfp")) {
        if (!object_property_set_bool(OBJECT(s->cpu), "vfp", s->vfp, errp)) {
            return;
        }
    }
    if (object_property_find(OBJECT(s->cpu), "dsp")) {
        if (!object_property_set_bool(OBJECT(s->cpu), "dsp", s->dsp, errp)) {
            return;
        }
    }
    object_property_set_bool(OBJECT(s->cpu), "start-powered-off",
                             s->start_powered_off, &error_abort);

    if (arm_feature(&s->cpu->env, ARM_FEATURE_M_SECURITY) &&
        s->mpu_ns_regions != s->mpu_s_regions) {
        error_setg(errp,
                   "mpu-ns-regions and mpu-s-regions properties must have the same value");
        return;
    }
    if (s->mpu_ns_regions != UINT_MAX &&
        object_property_find(OBJECT(s->cpu), "pmsav7-dregion")) {
        if (!object_property_set_uint(OBJECT(s->cpu), "pmsav7-dregion",
                                      s->mpu_ns_regions, errp)) {
            return;
        }
    }

    s->cpu->env.nvic = &s->nvic;
    s->nvic.cpu = s->cpu;

    if (!qdev_realize(DEVICE(s->cpu), NULL, errp)) {
        return;
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->nvic), errp)) {
        return;
    }

    qdev_pass_gpios(DEVICE(&s->nvic), dev, NULL);
    qdev_pass_gpios(DEVICE(&s->nvic), dev, "SYSRESETREQ");
    qdev_pass_gpios(DEVICE(&s->nvic), dev, "NMI");

    memory_region_init_io(&s->defaultmem, OBJECT(s), &ppb_default_ops, s,
                          "nvic-default", 0x100000);
    memory_region_add_subregion_overlap(&s->container, 0xe0000000,
                                        &s->defaultmem, -1);

    sbd = SYS_BUS_DEVICE(&s->nvic);
    sysbus_connect_irq(sbd, 0,
                       qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_IRQ));

    memory_region_add_subregion(&s->container, 0xe000e000,
                                sysbus_mmio_get_region(sbd, 0));
    if (arm_feature(&s->cpu->env, ARM_FEATURE_V8)) {
        memory_region_init_io(&s->sysreg_ns_mem, OBJECT(s),
                              &v7m_sysreg_ns_ops,
                              sysbus_mmio_get_region(sbd, 0),
                              "nvic_sysregs_ns", 0x1000);
        s->sysreg_ns_mem.disable_reentrancy_guard = true;
        memory_region_add_subregion(&s->container, 0xe002e000,
                                    &s->sysreg_ns_mem);
    }

    if (clock_has_source(s->refclk)) {
        qdev_connect_clock_in(DEVICE(&s->systick[M_REG_NS]), "refclk",
                              s->refclk);
    }
    qdev_connect_clock_in(DEVICE(&s->systick[M_REG_NS]), "cpuclk", s->cpuclk);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->systick[M_REG_NS]), errp)) {
        return;
    }
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->systick[M_REG_NS]), 0,
                       qdev_get_gpio_in_named(DEVICE(&s->nvic),
                                              "systick-trigger", M_REG_NS));

    if (arm_feature(&s->cpu->env, ARM_FEATURE_M_SECURITY)) {
        object_initialize_child(OBJECT(dev), "systick-reg-s",
                                &s->systick[M_REG_S], TYPE_SYSTICK);
        if (clock_has_source(s->refclk)) {
            qdev_connect_clock_in(DEVICE(&s->systick[M_REG_S]), "refclk",
                                  s->refclk);
        }
        qdev_connect_clock_in(DEVICE(&s->systick[M_REG_S]), "cpuclk",
                              s->cpuclk);

        if (!sysbus_realize(SYS_BUS_DEVICE(&s->systick[M_REG_S]), errp)) {
            return;
        }
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->systick[M_REG_S]), 0,
                           qdev_get_gpio_in_named(DEVICE(&s->nvic),
                                                  "systick-trigger", M_REG_S));
    }

    memory_region_init_io(&s->systickmem, OBJECT(s),
                          &v7m_systick_ops, s,
                          "v7m_systick", 0xe0);

    memory_region_add_subregion_overlap(&s->container, 0xe000e010,
                                        &s->systickmem, 1);
    if (arm_feature(&s->cpu->env, ARM_FEATURE_V8)) {
        memory_region_init_io(&s->systick_ns_mem, OBJECT(s),
                              &v7m_sysreg_ns_ops, &s->systickmem,
                              "v7m_systick_ns", 0xe0);
        s->systick_ns_mem.disable_reentrancy_guard = true;
        memory_region_add_subregion_overlap(&s->container, 0xe002e010,
                                            &s->systick_ns_mem, 1);
    }

    if (cpu_isar_feature(aa32_ras, s->cpu)) {
        object_initialize_child(OBJECT(dev), "armv7m-ras",
                                &s->ras, TYPE_ARMV7M_RAS);
        sbd = SYS_BUS_DEVICE(&s->ras);
        if (!sysbus_realize(sbd, errp)) {
            return;
        }
        memory_region_add_subregion_overlap(&s->container, 0xe0005000,
                                            sysbus_mmio_get_region(sbd, 0), 1);
    }

    for (i = 0; i < ARRAY_SIZE(s->bitband); i++) {
        if (s->enable_bitband) {
            Object *obj = OBJECT(&s->bitband[i]);
            sbd = SYS_BUS_DEVICE(&s->bitband[i]);

            if (!object_property_set_int(obj, "base",
                                         bitband_input_addr[i], errp)) {
                return;
            }
            object_property_set_link(obj, "source-memory",
                                     OBJECT(s->board_memory), &error_abort);
            if (!sysbus_realize(SYS_BUS_DEVICE(obj), errp)) {
                return;
            }

            memory_region_add_subregion(&s->container, bitband_output_addr[i],
                                        sysbus_mmio_get_region(sbd, 0));
        } else {
            object_unparent(OBJECT(&s->bitband[i]));
        }
    }
}

static const Property armv7m_properties[] = {
    DEFINE_PROP_STRING("cpu-type", ARMv7MState, cpu_type),
    DEFINE_PROP_LINK("memory", ARMv7MState, board_memory, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_LINK("idau", ARMv7MState, idau, TYPE_IDAU_INTERFACE, Object *),
    DEFINE_PROP_UINT32("init-svtor", ARMv7MState, init_svtor, 0),
    DEFINE_PROP_UINT32("init-nsvtor", ARMv7MState, init_nsvtor, 0),
    DEFINE_PROP_BOOL("enable-bitband", ARMv7MState, enable_bitband, false),
    DEFINE_PROP_BOOL("start-powered-off", ARMv7MState, start_powered_off,
                     false),
    DEFINE_PROP_BOOL("vfp", ARMv7MState, vfp, true),
    DEFINE_PROP_BOOL("dsp", ARMv7MState, dsp, true),
    DEFINE_PROP_UINT32("mpu-ns-regions", ARMv7MState, mpu_ns_regions, UINT_MAX),
    DEFINE_PROP_UINT32("mpu-s-regions", ARMv7MState, mpu_s_regions, UINT_MAX),
};

static const VMStateDescription vmstate_armv7m = {
    .name = "armv7m",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(refclk, ARMv7MState),
        VMSTATE_CLOCK(cpuclk, ARMv7MState),
        VMSTATE_END_OF_LIST()
    }
};

static void armv7m_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = armv7m_realize;
    dc->vmsd = &vmstate_armv7m;
    device_class_set_props(dc, armv7m_properties);
}

static const TypeInfo armv7m_info = {
    .name = TYPE_ARMV7M,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ARMv7MState),
    .instance_init = armv7m_instance_init,
    .class_init = armv7m_class_init,
};

static void armv7m_reset(void *opaque)
{
    ARMCPU *cpu = opaque;

    cpu_reset(CPU(cpu));
}

void armv7m_load_kernel(ARMCPU *cpu, const char *kernel_filename,
                        hwaddr mem_base, int mem_size)
{
    ssize_t image_size;
    uint64_t entry;
    AddressSpace *as;
    int asidx;
    CPUState *cs = CPU(cpu);

    if (arm_feature(&cpu->env, ARM_FEATURE_EL3)) {
        asidx = ARMASIdx_S;
    } else {
        asidx = ARMASIdx_NS;
    }
    as = cpu_get_address_space(cs, asidx);

    if (kernel_filename) {
        image_size = load_elf_as(kernel_filename, NULL, NULL, NULL,
                                 &entry, NULL, NULL,
                                 NULL, ELFDATA2LSB, EM_ARM, 1, 0, as);
        if (image_size < 0) {
            image_size = load_image_targphys_as(kernel_filename, mem_base,
                                                mem_size, as, NULL);
        }
        if (image_size < 0) {
            error_report("Could not load kernel '%s'", kernel_filename);
            exit(1);
        }
    }

    qemu_register_reset(armv7m_reset, cpu);
}

static const Property bitband_properties[] = {
    DEFINE_PROP_UINT32("base", BitBandState, base, 0),
    DEFINE_PROP_LINK("source-memory", BitBandState, source_memory,
                     TYPE_MEMORY_REGION, MemoryRegion *),
};

static void bitband_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = bitband_realize;
    device_class_set_props(dc, bitband_properties);
}

static const TypeInfo bitband_info = {
    .name          = TYPE_BITBAND,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BitBandState),
    .instance_init = bitband_init,
    .class_init    = bitband_class_init,
};

static void armv7m_register_types(void)
{
    type_register_static(&bitband_info);
    type_register_static(&armv7m_info);
}

type_init(armv7m_register_types)
