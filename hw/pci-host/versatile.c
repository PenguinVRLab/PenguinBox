/*
 * ARM Versatile/PB PCI host controller
 *
 * Copyright (c) 2006-2009 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the LGPL.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci_host.h"
#include "hw/qdev-properties.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qom/object.h"

enum {
    PCI_VPB_IRQMAP_ASSUME_OK,
    PCI_VPB_IRQMAP_BROKEN,
    PCI_VPB_IRQMAP_FORCE_OK,
};

struct PCIVPBState {
    PCIHostState parent_obj;

    qemu_irq irq[4];
    MemoryRegion controlregs;
    MemoryRegion mem_config;
    MemoryRegion mem_config2;
    MemoryRegion pci_io_space;
    MemoryRegion pci_mem_space;
    MemoryRegion pci_io_window;
    MemoryRegion pci_mem_window[3];
    PCIBus pci_bus;
    PCIDevice pci_dev;

    int realview;
    uint32_t mem_win_size[3];
    uint8_t irq_mapping_prop;

    uint32_t imap[3];
    uint32_t smap[3];
    uint32_t selfid;
    uint32_t flags;
    uint8_t irq_mapping;
};
typedef struct PCIVPBState PCIVPBState;

static void pci_vpb_update_window(PCIVPBState *s, int i)
{
    hwaddr offset;
    if (s->realview) {
        offset = s->imap[i] & ~(s->mem_win_size[i] - 1);
    } else {
        offset = s->imap[i] << 28;
    }
    memory_region_set_alias_offset(&s->pci_mem_window[i], offset);
}

static void pci_vpb_update_all_windows(PCIVPBState *s)
{
    int i;

    for (i = 0; i < 3; i++) {
        pci_vpb_update_window(s, i);
    }
}

static int pci_vpb_post_load(void *opaque, int version_id)
{
    PCIVPBState *s = opaque;
    pci_vpb_update_all_windows(s);
    return 0;
}

static const VMStateDescription pci_vpb_vmstate = {
    .name = "versatile-pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = pci_vpb_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(imap, PCIVPBState, 3),
        VMSTATE_UINT32_ARRAY(smap, PCIVPBState, 3),
        VMSTATE_UINT32(selfid, PCIVPBState),
        VMSTATE_UINT32(flags, PCIVPBState),
        VMSTATE_UINT8(irq_mapping, PCIVPBState),
        VMSTATE_END_OF_LIST()
    }
};

#define TYPE_VERSATILE_PCI "versatile_pci"
DECLARE_INSTANCE_CHECKER(PCIVPBState, PCI_VPB,
                         TYPE_VERSATILE_PCI)

#define TYPE_VERSATILE_PCI_HOST "versatile_pci_host"
DECLARE_INSTANCE_CHECKER(PCIDevice, PCI_VPB_HOST,
                         TYPE_VERSATILE_PCI_HOST)

typedef enum {
    PCI_IMAP0 = 0x0,
    PCI_IMAP1 = 0x4,
    PCI_IMAP2 = 0x8,
    PCI_SELFID = 0xc,
    PCI_FLAGS = 0x10,
    PCI_SMAP0 = 0x14,
    PCI_SMAP1 = 0x18,
    PCI_SMAP2 = 0x1c,
} PCIVPBControlRegs;

static void pci_vpb_reg_write(void *opaque, hwaddr addr,
                              uint64_t val, unsigned size)
{
    PCIVPBState *s = opaque;

    switch (addr) {
    case PCI_IMAP0:
    case PCI_IMAP1:
    case PCI_IMAP2:
    {
        int win = (addr - PCI_IMAP0) >> 2;
        s->imap[win] = val;
        pci_vpb_update_window(s, win);
        break;
    }
    case PCI_SELFID:
        s->selfid = val;
        break;
    case PCI_FLAGS:
        s->flags = val;
        break;
    case PCI_SMAP0:
    case PCI_SMAP1:
    case PCI_SMAP2:
    {
        int win = (addr - PCI_SMAP0) >> 2;
        s->smap[win] = val;
        break;
    }
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pci_vpb_reg_write: Bad offset %x\n", (int)addr);
        break;
    }
}

static uint64_t pci_vpb_reg_read(void *opaque, hwaddr addr,
                                 unsigned size)
{
    PCIVPBState *s = opaque;

    switch (addr) {
    case PCI_IMAP0:
    case PCI_IMAP1:
    case PCI_IMAP2:
    {
        int win = (addr - PCI_IMAP0) >> 2;
        return s->imap[win];
    }
    case PCI_SELFID:
        return s->selfid;
    case PCI_FLAGS:
        return s->flags;
    case PCI_SMAP0:
    case PCI_SMAP1:
    case PCI_SMAP2:
    {
        int win = (addr - PCI_SMAP0) >> 2;
        return s->smap[win];
    }
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pci_vpb_reg_read: Bad offset %x\n", (int)addr);
        return 0;
    }
}

static const MemoryRegionOps pci_vpb_reg_ops = {
    .read = pci_vpb_reg_read,
    .write = pci_vpb_reg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static int pci_vpb_broken_irq(int slot, int irq)
{
    slot %= PCI_NUM_PINS;

    if (irq == 27) {
        if (slot == 2) {
            return PCI_VPB_IRQMAP_ASSUME_OK;
        }
        return PCI_VPB_IRQMAP_BROKEN;
    }
    if (irq == slot + 27) {
        return PCI_VPB_IRQMAP_BROKEN;
    }
    if (irq == slot + 27 + 64) {
        return PCI_VPB_IRQMAP_BROKEN;
    }
    return PCI_VPB_IRQMAP_FORCE_OK;
}

static void pci_vpb_config_write(void *opaque, hwaddr addr,
                                 uint64_t val, unsigned size)
{
    PCIVPBState *s = opaque;
    if (!s->realview && (addr & 0xff) == PCI_INTERRUPT_LINE
        && s->irq_mapping == PCI_VPB_IRQMAP_ASSUME_OK) {
        uint8_t devfn = addr >> 8;
        s->irq_mapping = pci_vpb_broken_irq(PCI_SLOT(devfn), val);
    }
    pci_data_write(&s->pci_bus, addr, val, size);
}

static uint64_t pci_vpb_config_read(void *opaque, hwaddr addr,
                                    unsigned size)
{
    PCIVPBState *s = opaque;
    uint32_t val;
    val = pci_data_read(&s->pci_bus, addr, size);
    return val;
}

static const MemoryRegionOps pci_vpb_config_ops = {
    .read = pci_vpb_config_read,
    .write = pci_vpb_config_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static int pci_vpb_map_irq(PCIDevice *d, int irq_num)
{
    PCIVPBState *s = container_of(pci_get_bus(d), PCIVPBState, pci_bus);

    if (s->irq_mapping == PCI_VPB_IRQMAP_BROKEN) {
        return irq_num;
    }

    return pci_swizzle_map_irq_fn(d, irq_num + 2);
}

static int pci_vpb_rv_map_irq(PCIDevice *d, int irq_num)
{
    return pci_swizzle_map_irq_fn(d, irq_num + 3);
}

static void pci_vpb_set_irq(void *opaque, int irq_num, int level)
{
    qemu_irq *pic = opaque;

    qemu_set_irq(pic[irq_num], level);
}

static void pci_vpb_reset(DeviceState *d)
{
    PCIVPBState *s = PCI_VPB(d);

    s->imap[0] = 0;
    s->imap[1] = 0;
    s->imap[2] = 0;
    s->smap[0] = 0;
    s->smap[1] = 0;
    s->smap[2] = 0;
    s->selfid = 0;
    s->flags = 0;
    s->irq_mapping = s->irq_mapping_prop;

    pci_vpb_update_all_windows(s);
}

static void pci_vpb_init(Object *obj)
{
    PCIVPBState *s = PCI_VPB(obj);

    s->mem_win_size[0] = 0x0c000000;
    s->mem_win_size[1] = 0x10000000;
    s->mem_win_size[2] = 0x10000000;
}

static void pci_vpb_realize(DeviceState *dev, Error **errp)
{
    PCIVPBState *s = PCI_VPB(dev);
    PCIHostState *h = PCI_HOST_BRIDGE(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    pci_map_irq_fn mapfn;
    int i;

    memory_region_init(&s->pci_io_space, OBJECT(s), "pci_io", 4 * GiB);
    memory_region_init(&s->pci_mem_space, OBJECT(s), "pci_mem", 4 * GiB);

    pci_root_bus_init(&s->pci_bus, sizeof(s->pci_bus), dev, "pci",
                      &s->pci_mem_space, &s->pci_io_space,
                      PCI_DEVFN(11, 0), TYPE_PCI_BUS);
    h->bus = &s->pci_bus;

    object_initialize(&s->pci_dev, sizeof(s->pci_dev), TYPE_VERSATILE_PCI_HOST);

    for (i = 0; i < 4; i++) {
        sysbus_init_irq(sbd, &s->irq[i]);
    }

    if (s->realview) {
        mapfn = pci_vpb_rv_map_irq;
    } else {
        mapfn = pci_vpb_map_irq;
    }

    pci_bus_irqs(&s->pci_bus, pci_vpb_set_irq, s->irq, 4);
    pci_bus_map_irqs(&s->pci_bus, mapfn);

    memory_region_init_io(&s->controlregs, OBJECT(s), &pci_vpb_reg_ops, s,
                          "pci-vpb-regs", 0x1000);
    sysbus_init_mmio(sbd, &s->controlregs);
    memory_region_init_io(&s->mem_config, OBJECT(s), &pci_vpb_config_ops, s,
                          "pci-vpb-selfconfig", 0x1000000);
    sysbus_init_mmio(sbd, &s->mem_config);
    memory_region_init_io(&s->mem_config2, OBJECT(s), &pci_vpb_config_ops, s,
                          "pci-vpb-config", 0x1000000);
    sysbus_init_mmio(sbd, &s->mem_config2);

    memory_region_init_alias(&s->pci_io_window, OBJECT(s), "pci-vbp-io-window",
                             &s->pci_io_space, 0, 0x100000);

    sysbus_init_mmio(sbd, &s->pci_io_space);

    for (i = 0; i < 3; i++) {
        memory_region_init_alias(&s->pci_mem_window[i], OBJECT(s), "pci-vbp-window",
                                 &s->pci_mem_space, 0, s->mem_win_size[i]);
        sysbus_init_mmio(sbd, &s->pci_mem_window[i]);
    }

    qdev_realize(DEVICE(&s->pci_dev), BUS(&s->pci_bus), errp);
}

static void versatile_pci_host_realize(PCIDevice *d, Error **errp)
{
    pci_set_word(d->config + PCI_STATUS,
                 PCI_STATUS_66MHZ | PCI_STATUS_DEVSEL_MEDIUM);
    pci_set_byte(d->config + PCI_LATENCY_TIMER, 0x10);
}

static void versatile_pci_host_class_init(ObjectClass *klass, const void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->realize = versatile_pci_host_realize;
    k->vendor_id = PCI_VENDOR_ID_XILINX;
    k->device_id = PCI_DEVICE_ID_XILINX_XC2VP30;
    k->class_id = PCI_CLASS_PROCESSOR_CO;
    dc->user_creatable = false;
}

static const TypeInfo versatile_pci_host_info = {
    .name          = TYPE_VERSATILE_PCI_HOST,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIDevice),
    .class_init    = versatile_pci_host_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static const Property pci_vpb_properties[] = {
    DEFINE_PROP_UINT8("broken-irq-mapping", PCIVPBState, irq_mapping_prop,
                      PCI_VPB_IRQMAP_ASSUME_OK),
};

static void pci_vpb_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pci_vpb_realize;
    device_class_set_legacy_reset(dc, pci_vpb_reset);
    dc->vmsd = &pci_vpb_vmstate;
    device_class_set_props(dc, pci_vpb_properties);
}

static const TypeInfo pci_vpb_info = {
    .name          = TYPE_VERSATILE_PCI,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(PCIVPBState),
    .instance_init = pci_vpb_init,
    .class_init    = pci_vpb_class_init,
};

static void pci_realview_init(Object *obj)
{
    PCIVPBState *s = PCI_VPB(obj);

    s->realview = 1;
    s->mem_win_size[0] = 0x01000000;
    s->mem_win_size[1] = 0x04000000;
    s->mem_win_size[2] = 0x08000000;
}

static const TypeInfo pci_realview_info = {
    .name          = "realview_pci",
    .parent        = TYPE_VERSATILE_PCI,
    .instance_init = pci_realview_init,
};

static void versatile_pci_register_types(void)
{
    type_register_static(&pci_vpb_info);
    type_register_static(&pci_realview_info);
    type_register_static(&versatile_pci_host_info);
}

type_init(versatile_pci_register_types)
