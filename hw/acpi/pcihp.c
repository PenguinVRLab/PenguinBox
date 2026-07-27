/*
 * QEMU<->ACPI BIOS PCI hotplug interface
 *
 * QEMU supports PCI hotplug via ACPI. This module
 * implements the interface between QEMU and the ACPI BIOS.
 * Interface specification - see docs/specs/acpi_pci_hotplug.rst
 *
 * Copyright (c) 2013, Red Hat Inc, Michael S. Tsirkin (mst@redhat.com)
 * Copyright (c) 2006 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/acpi/pcihp.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/acpi_aml_interface.h"
#include "hw/pci-host/i440fx.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_host.h"
#include "hw/pci/pcie_port.h"
#include "hw/pci-bridge/xio3130_downstream.h"
#include "hw/i386/acpi-build.h"
#include "hw/acpi/acpi.h"
#include "hw/pci/pci_bus.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qom/qom-qobject.h"
#include "qobject/qnum.h"
#include "trace.h"

#define PCI_UP_BASE 0x0000
#define PCI_DOWN_BASE 0x0004
#define PCI_EJ_BASE 0x0008
#define PCI_RMV_BASE 0x000c
#define PCI_SEL_BASE 0x0010
#define PCI_AIDX_BASE 0x0014

typedef struct AcpiPciHpFind {
    int bsel;
    PCIBus *bus;
} AcpiPciHpFind;

static int acpi_pcihp_get_bsel(PCIBus *bus)
{
    Error *local_err = NULL;
    uint64_t bsel = object_property_get_uint(OBJECT(bus), ACPI_PCIHP_PROP_BSEL,
                                             &local_err);

    if (local_err || bsel >= ACPI_PCIHP_MAX_HOTPLUG_BUS) {
        if (local_err) {
            error_free(local_err);
        }
        return -1;
    } else {
        return bsel;
    }
}

typedef struct {
    unsigned bsel_alloc;
    bool has_bridge_hotplug;
} BSELInfo;

static void *acpi_set_bsel(PCIBus *bus, void *opaque)
{
    BSELInfo *info = opaque;
    unsigned *bus_bsel;
    DeviceState *br = bus->qbus.parent;
    bool is_bridge = IS_PCI_BRIDGE(br);

    if (qbus_is_hotpluggable(BUS(bus))) {
        if (!is_bridge || (!br->hotplugged && info->has_bridge_hotplug)) {
            bus_bsel = g_malloc(sizeof *bus_bsel);

            *bus_bsel = info->bsel_alloc++;
            object_property_add_uint32_ptr(OBJECT(bus), ACPI_PCIHP_PROP_BSEL,
                                           bus_bsel, OBJ_PROP_FLAG_READ);
        }
    }

    return info;
}

static void acpi_set_pci_info(AcpiPciHpState *s)
{
    static bool bsel_is_set;
    bool has_bridge_hotplug = s->use_acpi_hotplug_bridge;
    PCIBus *bus;
    BSELInfo info = { .bsel_alloc = ACPI_PCIHP_BSEL_DEFAULT,
                      .has_bridge_hotplug = has_bridge_hotplug };

    if (bsel_is_set) {
        return;
    }
    bsel_is_set = true;


    bus = s->root;
    if (bus) {
        pci_for_each_bus_depth_first(bus, acpi_set_bsel, NULL, &info);
    }
}

static void acpi_pcihp_test_hotplug_bus(PCIBus *bus, void *opaque)
{
    AcpiPciHpFind *find = opaque;
    if (find->bsel == acpi_pcihp_get_bsel(bus)) {
        find->bus = bus;
    }
}

static PCIBus *acpi_pcihp_find_hotplug_bus(AcpiPciHpState *s, int bsel)
{
    AcpiPciHpFind find = { .bsel = bsel, .bus = NULL };

    if (bsel < 0) {
        return NULL;
    }

    pci_for_each_bus(s->root, acpi_pcihp_test_hotplug_bus, &find);

    if (!bsel && !find.bus) {
        find.bus = s->root;
    }

    if (find.bus && !qbus_is_hotpluggable(BUS(find.bus))) {
        find.bus = NULL;
    }
    return find.bus;
}

static bool acpi_pcihp_pc_no_hotplug(AcpiPciHpState *s, PCIDevice *dev)
{
    DeviceClass *dc = DEVICE_GET_CLASS(dev);
    return (IS_PCI_BRIDGE(dev) && !dev->qdev.hotplugged) || !dc->hotpluggable ||
           pci_is_vf(dev);
}

static void acpi_pcihp_eject_slot(AcpiPciHpState *s, unsigned bsel, unsigned slots)
{
    HotplugHandler *hotplug_ctrl;
    BusChild *kid, *next;
    int slot = ctz32(slots);
    PCIBus *bus = acpi_pcihp_find_hotplug_bus(s, bsel);

    trace_acpi_pci_eject_slot(bsel, slot);

    if (!bus || slot > 31) {
        return;
    }

    s->acpi_pcihp_pci_status[bsel].down &= ~(1U << slot);
    s->acpi_pcihp_pci_status[bsel].up &= ~(1U << slot);

    QTAILQ_FOREACH_SAFE(kid, &bus->qbus.children, sibling, next) {
        DeviceState *qdev = kid->child;
        PCIDevice *dev = PCI_DEVICE(qdev);
        if (PCI_SLOT(dev->devfn) == slot) {
            if (!acpi_pcihp_pc_no_hotplug(s, dev)) {
                if (dev->partially_hotplugged) {
                    qdev->pending_deleted_event = false;
                } else {
                    hotplug_ctrl = qdev_get_hotplug_handler(qdev);
                    hotplug_handler_unplug(hotplug_ctrl, qdev, &error_abort);
                    object_unparent(OBJECT(qdev));
                }
            }
        }
    }
}

static void acpi_pcihp_update_hotplug_bus(AcpiPciHpState *s, int bsel)
{
    BusChild *kid, *next;
    PCIBus *bus = acpi_pcihp_find_hotplug_bus(s, bsel);

    while (s->acpi_pcihp_pci_status[bsel].down) {
        acpi_pcihp_eject_slot(s, bsel, s->acpi_pcihp_pci_status[bsel].down);
    }

    s->acpi_pcihp_pci_status[bsel].hotplug_enable = ~0;

    if (!bus) {
        return;
    }
    QTAILQ_FOREACH_SAFE(kid, &bus->qbus.children, sibling, next) {
        DeviceState *qdev = kid->child;
        PCIDevice *pdev = PCI_DEVICE(qdev);
        int slot = PCI_SLOT(pdev->devfn);

        if (acpi_pcihp_pc_no_hotplug(s, pdev)) {
            s->acpi_pcihp_pci_status[bsel].hotplug_enable &= ~(1U << slot);
        }
    }
}

static void acpi_pcihp_update(AcpiPciHpState *s)
{
    int i;

    for (i = 0; i < ACPI_PCIHP_MAX_HOTPLUG_BUS; ++i) {
        acpi_pcihp_update_hotplug_bus(s, i);
    }
}

void acpi_pcihp_reset(AcpiPciHpState *s)
{
    acpi_set_pci_info(s);
    acpi_pcihp_update(s);
}

void acpi_pcihp_device_pre_plug_cb(HotplugHandler *hotplug_dev,
                                   DeviceState *dev, Error **errp)
{
    PCIDevice *pdev = PCI_DEVICE(dev);

    if (dev->hotplugged &&
        acpi_pcihp_get_bsel(pci_get_bus(pdev)) < 0) {
        error_setg(errp, "Unsupported bus. Bus doesn't have property '"
                   ACPI_PCIHP_PROP_BSEL "' set");
        return;
    }
}

void acpi_pcihp_device_plug_cb(HotplugHandler *hotplug_dev, AcpiPciHpState *s,
                               DeviceState *dev, Error **errp)
{
    PCIDevice *pdev = PCI_DEVICE(dev);
    int slot = PCI_SLOT(pdev->devfn);
    PCIDevice *bridge;
    PCIBus *bus;
    int bsel;

    if (!dev->hotplugged) {
        if (s->use_acpi_hotplug_bridge &&
            object_dynamic_cast(OBJECT(dev), TYPE_PCI_BRIDGE)) {
            PCIBus *sec = pci_bridge_get_sec_bus(PCI_BRIDGE(pdev));

            qbus_set_hotplug_handler(BUS(sec), OBJECT(hotplug_dev));
            assert(QLIST_EMPTY(&sec->child));
        }

        return;
    }

    bus = pci_get_bus(pdev);
    bridge = pci_bridge_get_device(bus);
    if (object_dynamic_cast(OBJECT(bridge), TYPE_PCIE_ROOT_PORT) ||
        object_dynamic_cast(OBJECT(bridge), TYPE_XIO3130_DOWNSTREAM)) {
        pcie_cap_slot_enable_power(bridge);
    }

    bsel = acpi_pcihp_get_bsel(bus);
    g_assert(bsel >= 0);
    s->acpi_pcihp_pci_status[bsel].up |= (1U << slot);
    acpi_send_event(DEVICE(hotplug_dev), ACPI_PCI_HOTPLUG_STATUS);
}

void acpi_pcihp_device_unplug_cb(HotplugHandler *hotplug_dev, AcpiPciHpState *s,
                                 DeviceState *dev, Error **errp)
{
    PCIDevice *pdev = PCI_DEVICE(dev);

    trace_acpi_pci_unplug(PCI_SLOT(pdev->devfn),
                          acpi_pcihp_get_bsel(pci_get_bus(pdev)));

    qdev_unrealize(dev);
}

void acpi_pcihp_device_unplug_request_cb(HotplugHandler *hotplug_dev,
                                         AcpiPciHpState *s, DeviceState *dev,
                                         Error **errp)
{
    PCIDevice *pdev = PCI_DEVICE(dev);
    int slot = PCI_SLOT(pdev->devfn);
    int bsel = acpi_pcihp_get_bsel(pci_get_bus(pdev));

    trace_acpi_pci_unplug_request(bsel, slot);

    if (bsel < 0) {
        error_setg(errp, "Unsupported bus. Bus doesn't have property '"
                   ACPI_PCIHP_PROP_BSEL "' set");
        return;
    }

    pdev->qdev.pending_deleted_event = true;
    pdev->qdev.pending_deleted_expires_ms =
        qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);

    s->acpi_pcihp_pci_status[bsel].down |= (1U << slot);
    acpi_send_event(DEVICE(hotplug_dev), ACPI_PCI_HOTPLUG_STATUS);
}

bool acpi_pcihp_is_hotpluggable_bus(AcpiPciHpState *s, BusState *bus)
{
    Object *o = OBJECT(bus->parent);

    if (s->use_acpi_hotplug_bridge &&
        object_dynamic_cast(o, TYPE_PCI_BRIDGE)) {
        if (object_dynamic_cast(o, TYPE_PCIE_SLOT) && !PCIE_SLOT(o)->hotplug) {
            return false;
        }
        return true;
    }

    if (s->use_acpi_root_pci_hotplug) {
        return true;
    }
    return false;
}

static uint64_t pci_read(void *opaque, hwaddr addr, unsigned int size)
{
    AcpiPciHpState *s = opaque;
    uint32_t val = 0;
    int bsel = s->hotplug_select;

    if (bsel < 0 || bsel >= ACPI_PCIHP_MAX_HOTPLUG_BUS) {
        return 0;
    }

    switch (addr) {
    case PCI_UP_BASE:
        val = s->acpi_pcihp_pci_status[bsel].up;
        if (s->use_acpi_hotplug_bridge) {
            s->acpi_pcihp_pci_status[bsel].up = 0;
        }
        trace_acpi_pci_up_read(val);
        break;
    case PCI_DOWN_BASE:
        val = s->acpi_pcihp_pci_status[bsel].down;
        trace_acpi_pci_down_read(val);
        break;
    case PCI_EJ_BASE:
        trace_acpi_pci_features_read(val);
        break;
    case PCI_RMV_BASE:
        val = s->acpi_pcihp_pci_status[bsel].hotplug_enable;
        trace_acpi_pci_rmv_read(val);
        break;
    case PCI_SEL_BASE:
        val = s->hotplug_select;
        trace_acpi_pci_sel_read(val);
        break;
    case PCI_AIDX_BASE:
        val = s->acpi_index;
        s->acpi_index = 0;
        trace_acpi_pci_acpi_index_read(val);
        break;
    default:
        break;
    }

    return val;
}

static void pci_write(void *opaque, hwaddr addr, uint64_t data,
                      unsigned int size)
{
    int slot;
    PCIBus *bus;
    BusChild *kid, *next;
    AcpiPciHpState *s = opaque;

    s->acpi_index = 0;
    switch (addr) {
    case PCI_AIDX_BASE:
        slot = ctz32(data);

        if (s->hotplug_select >= ACPI_PCIHP_MAX_HOTPLUG_BUS) {
            break;
        }

        bus = acpi_pcihp_find_hotplug_bus(s, s->hotplug_select);
        if (!bus) {
            break;
        }
        QTAILQ_FOREACH_SAFE(kid, &bus->qbus.children, sibling, next) {
            Object *o = OBJECT(kid->child);
            PCIDevice *dev = PCI_DEVICE(o);
            if (PCI_SLOT(dev->devfn) == slot) {
                s->acpi_index = object_property_get_uint(o, "acpi-index", NULL);
                break;
            }
        }
        trace_acpi_pci_acpi_index_write(s->hotplug_select, slot, s->acpi_index);
        break;
    case PCI_EJ_BASE:
        if (s->hotplug_select >= ACPI_PCIHP_MAX_HOTPLUG_BUS) {
            break;
        }
        acpi_pcihp_eject_slot(s, s->hotplug_select, data);
        trace_acpi_pci_ej_write(addr, data);
        break;
    case PCI_SEL_BASE:
        s->hotplug_select = s->use_acpi_hotplug_bridge ? data :
            ACPI_PCIHP_BSEL_DEFAULT;
        trace_acpi_pci_sel_write(addr, data);
    default:
        break;
    }
}

static const MemoryRegionOps acpi_pcihp_io_ops = {
    .read = pci_read,
    .write = pci_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

void acpi_pcihp_init(Object *owner, AcpiPciHpState *s,
                     MemoryRegion *io, uint16_t io_base)
{
    s->io_len = ACPI_PCIHP_SIZE;
    s->io_base = io_base;

    assert(s->root);

    memory_region_init_io(&s->io, owner, &acpi_pcihp_io_ops, s,
                          "acpi-pci-hotplug", s->io_len);
    memory_region_add_subregion(io, s->io_base, &s->io);

    object_property_add_uint16_ptr(owner, ACPI_PCIHP_IO_BASE_PROP, &s->io_base,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint16_ptr(owner, ACPI_PCIHP_IO_LEN_PROP, &s->io_len,
                                   OBJ_PROP_FLAG_READ);
}

void build_append_pci_dsm_func0_common(Aml *ctx, Aml *retvar)
{
    Aml *UUID, *ifctx1;
    uint8_t byte_list[1] = { 0 };

    aml_append(ctx, aml_store(aml_buffer(1, byte_list), retvar));
    UUID = aml_touuid("E5C937D0-3553-4D7A-9117-EA4D19C3434D");
    ifctx1 = aml_if(aml_lnot(aml_equal(aml_arg(0), UUID)));
    {
        aml_append(ifctx1, aml_return(retvar));
    }
    aml_append(ctx, ifctx1);

    ifctx1 = aml_if(aml_lless(aml_arg(1), aml_int(2)));
    {
        aml_append(ifctx1, aml_return(retvar));
    }
    aml_append(ctx, ifctx1);
}

static Aml *aml_pci_pdsm(void)
{
    Aml *method, *ifctx, *ifctx1;
    Aml *ret = aml_local(0);
    Aml *caps = aml_local(1);
    Aml *acpi_index = aml_local(2);
    Aml *zero = aml_int(0);
    Aml *one = aml_int(1);
    Aml *not_supp = aml_int(0xFFFFFFFF);
    Aml *func = aml_arg(2);
    Aml *params = aml_arg(4);
    Aml *bnum = aml_derefof(aml_index(params, aml_int(0)));
    Aml *sunum = aml_derefof(aml_index(params, aml_int(1)));

    method = aml_method("PDSM", 5, AML_SERIALIZED);

    ifctx = aml_if(aml_equal(func, zero));
    {
        build_append_pci_dsm_func0_common(ifctx, ret);

        aml_append(ifctx, aml_store(zero, caps));
        aml_append(ifctx,
            aml_store(aml_call2("AIDX", bnum, sunum), acpi_index));
        ifctx1 = aml_if(aml_lnot(
                     aml_or(aml_equal(acpi_index, zero),
                            aml_equal(acpi_index, not_supp), NULL)
                 ));
        {
            aml_append(ifctx1, aml_or(caps, one, caps));
            aml_append(ifctx1,
                aml_or(caps, aml_shiftleft(one, aml_int(7)), caps));
        }
        aml_append(ifctx, ifctx1);

        aml_append(ifctx, aml_store(caps, aml_index(ret, zero)));
        aml_append(ifctx, aml_return(ret));
    }
    aml_append(method, ifctx);

    ifctx = aml_if(aml_equal(func, aml_int(7)));
    {
       Aml *pkg = aml_package(2);

       aml_append(ifctx, aml_store(aml_call2("AIDX", bnum, sunum), acpi_index));
       aml_append(ifctx, aml_store(pkg, ret));
       ifctx1 = aml_if(aml_lnot(aml_lor(
                    aml_equal(acpi_index, zero), aml_equal(acpi_index, not_supp)
                )));
       {
           aml_append(ifctx1, aml_store(acpi_index, aml_index(ret, zero)));
           aml_append(ifctx1, aml_store(aml_string("%s", ""),
                                        aml_index(ret, one)));
       }
       aml_append(ifctx, ifctx1);

       aml_append(ifctx, aml_return(ret));
    }

    aml_append(method, ifctx);
    return method;
}

void build_acpi_pci_hotplug(Aml *table, AmlRegionSpace rs, uint64_t pcihp_addr)
{
    Aml *scope;
    Aml *field;
    Aml *method;

    scope =  aml_scope("_SB.PCI0");

    aml_append(scope,
        aml_operation_region("PCST", rs, aml_int(pcihp_addr), 0x08));
    field = aml_field("PCST", AML_DWORD_ACC, AML_NOLOCK, AML_WRITE_AS_ZEROS);
    aml_append(field, aml_named_field("PCIU", 32));
    aml_append(field, aml_named_field("PCID", 32));
    aml_append(scope, field);

    aml_append(scope,
        aml_operation_region("SEJ", rs,
                             aml_int(pcihp_addr + ACPI_PCIHP_SEJ_BASE), 0x04));
    field = aml_field("SEJ", AML_DWORD_ACC, AML_NOLOCK, AML_WRITE_AS_ZEROS);
    aml_append(field, aml_named_field("B0EJ", 32));
    aml_append(scope, field);

    aml_append(scope,
        aml_operation_region("BNMR", rs,
                             aml_int(pcihp_addr + ACPI_PCIHP_BNMR_BASE), 0x08));
    field = aml_field("BNMR", AML_DWORD_ACC, AML_NOLOCK, AML_WRITE_AS_ZEROS);
    aml_append(field, aml_named_field("BNUM", 32));
    aml_append(field, aml_named_field("PIDX", 32));
    aml_append(scope, field);

    aml_append(scope, aml_mutex("BLCK", 0));

        method = aml_method("PCEJ", 2, AML_NOTSERIALIZED);
    aml_append(method, aml_acquire(aml_name("BLCK"), 0xFFFF));
    aml_append(method, aml_store(aml_arg(0), aml_name("BNUM")));
    aml_append(method,
        aml_store(aml_shiftleft(aml_int(1), aml_arg(1)), aml_name("B0EJ")));
    aml_append(method, aml_release(aml_name("BLCK")));
    aml_append(method, aml_return(aml_int(0)));
    aml_append(scope, method);

    method = aml_method("AIDX", 2, AML_NOTSERIALIZED);
    aml_append(method, aml_acquire(aml_name("BLCK"), 0xFFFF));
    aml_append(method, aml_store(aml_arg(0), aml_name("BNUM")));
    aml_append(method,
        aml_store(aml_shiftleft(aml_int(1), aml_arg(1)), aml_name("PIDX")));
    aml_append(method, aml_store(aml_name("PIDX"), aml_local(0)));
    aml_append(method, aml_release(aml_name("BLCK")));
    aml_append(method, aml_return(aml_local(0)));
    aml_append(scope, method);

    aml_append(scope, aml_pci_pdsm());

    aml_append(table, scope);
}

void build_append_pcihp_resources(Aml *scope ,
                                  uint64_t io_addr, uint64_t io_len)
{
    Aml *dev, *crs;

    dev = aml_device("PHPR");
    aml_append(dev, aml_name_decl("_HID", aml_string("PNP0A06")));
    aml_append(dev,
               aml_name_decl("_UID", aml_string("PCI Hotplug resources")));
    aml_append(dev, aml_name_decl("_STA", aml_int(0xB)));
    crs = aml_resource_template();
    aml_append(crs, aml_io(AML_DECODE16, io_addr, io_addr, 1, io_len));
    aml_append(dev, aml_name_decl("_CRS", crs));
    aml_append(scope, dev);
}

bool build_append_notification_callback(Aml *parent_scope, const PCIBus *bus)
{
    Aml *method;
    PCIBus *sec;
    QObject *bsel;
    int nr_notifiers = 0;
    GQueue *pcnt_bus_list = g_queue_new();

    QLIST_FOREACH(sec, &bus->child, sibling) {
        Aml *br_scope = aml_scope("S%.02X", sec->parent_dev->devfn);
        if (pci_bus_is_root(sec)) {
            continue;
        }
        nr_notifiers = nr_notifiers +
                       build_append_notification_callback(br_scope, sec);
        if (nr_notifiers) {
            g_queue_push_tail(pcnt_bus_list, sec);
            aml_append(parent_scope, br_scope);
        }
    }

    method = aml_method("PCNT", 0, AML_NOTSERIALIZED);

    bsel = object_property_get_qobject(OBJECT(bus), ACPI_PCIHP_PROP_BSEL, NULL);
    if (bsel) {
        uint64_t bsel_val = qnum_get_uint(qobject_to(QNum, bsel));

        aml_append(method, aml_store(aml_int(bsel_val), aml_name("BNUM")));
        aml_append(method, aml_call2("DVNT", aml_name("PCIU"),
                                     aml_int(1)));
        aml_append(method, aml_call2("DVNT", aml_name("PCID"),
                                     aml_int(3)));
        nr_notifiers++;
    }

    while ((sec = g_queue_pop_head(pcnt_bus_list))) {
        aml_append(method, aml_name("^S%.02X.PCNT", sec->parent_dev->devfn));
    }

    aml_append(parent_scope, method);
    qobject_unref(bsel);
    g_queue_free(pcnt_bus_list);
    return !!nr_notifiers;
}

static Aml *aml_pci_device_dsm(void)
{
    Aml *method;

    method = aml_method("_DSM", 4, AML_SERIALIZED);
    {
        Aml *params = aml_local(0);
        Aml *pkg = aml_package(2);
        aml_append(pkg, aml_int(0));
        aml_append(pkg, aml_int(0));
        aml_append(method, aml_store(pkg, params));
        aml_append(method,
            aml_store(aml_name("BSEL"), aml_index(params, aml_int(0))));
        aml_append(method,
            aml_store(aml_name("ASUN"), aml_index(params, aml_int(1))));
        aml_append(method,
            aml_return(aml_call5("PDSM", aml_arg(0), aml_arg(1),
                                 aml_arg(2), aml_arg(3), params))
        );
    }
    return method;
}

static Aml *aml_pci_static_endpoint_dsm(PCIDevice *pdev)
{
    Aml *method;

    g_assert(pdev->acpi_index != 0);
    method = aml_method("_DSM", 4, AML_SERIALIZED);
    {
        Aml *params = aml_local(0);
        Aml *pkg = aml_package(1);
        aml_append(pkg, aml_int(pdev->acpi_index));
        aml_append(method, aml_store(pkg, params));
        aml_append(method,
            aml_return(aml_call5("EDSM", aml_arg(0), aml_arg(1),
                                 aml_arg(2), aml_arg(3), params))
        );
    }
    return method;
}

static void build_append_pcihp_notify_entry(Aml *method, int slot)
{
    Aml *if_ctx;
    int32_t devfn = PCI_DEVFN(slot, 0);

    if_ctx = aml_if(aml_and(aml_arg(0), aml_int(0x1U << slot), NULL));
    aml_append(if_ctx, aml_notify(aml_name("S%.02X", devfn), aml_arg(1)));
    aml_append(method, if_ctx);
}

static bool is_devfn_ignored_generic(const int devfn, const PCIBus *bus)
{
    const PCIDevice *pdev = bus->devices[devfn];

    if (PCI_FUNC(devfn)) {
        if (IS_PCI_BRIDGE(pdev)) {
            if (DEVICE(pdev)->hotplugged) {
                return true;
            }
        }
    }
    return false;
}

static bool is_devfn_ignored_hotplug(const int devfn, const PCIBus *bus)
{
    PCIDevice *pdev = bus->devices[devfn];
    if (pdev) {
        return is_devfn_ignored_generic(devfn, bus) ||
               !DEVICE_GET_CLASS(pdev)->hotpluggable ||
               (IS_PCI_BRIDGE(pdev) && !DEVICE(pdev)->hotplugged);
    } else {
        if (PCI_FUNC(devfn) ||
            (pci_bus_is_express(bus) && PCI_SLOT(devfn) > 0)) {
            return true;
        }
    }
    return false;
}

void build_append_pcihp_slots(Aml *parent_scope, PCIBus *bus)
{
    int devfn;
    Aml *dev, *notify_method = NULL, *method;
    QObject *bsel = object_property_get_qobject(OBJECT(bus),
                        ACPI_PCIHP_PROP_BSEL, NULL);
    uint64_t bsel_val = qnum_get_uint(qobject_to(QNum, bsel));
    qobject_unref(bsel);

    aml_append(parent_scope, aml_name_decl("BSEL", aml_int(bsel_val)));
    notify_method = aml_method("DVNT", 2, AML_NOTSERIALIZED);

    for (devfn = 0; devfn < ARRAY_SIZE(bus->devices); devfn++) {
        int slot = PCI_SLOT(devfn);
        int adr = slot << 16 | PCI_FUNC(devfn);

        if (is_devfn_ignored_hotplug(devfn, bus)) {
            continue;
        }

        if (bus->devices[devfn]) {
            dev = aml_scope("S%.02X", devfn);
        } else {
            dev = aml_device("S%.02X", devfn);
            aml_append(dev, aml_name_decl("_ADR", aml_int(adr)));
        }

        aml_append(dev, aml_name_decl("ASUN", aml_int(slot)));
        aml_append(dev, aml_pci_device_dsm());

        aml_append(dev, aml_name_decl("_SUN", aml_int(slot)));
        method = aml_method("_EJ0", 1, AML_NOTSERIALIZED);
        aml_append(method,
            aml_call2("PCEJ", aml_name("BSEL"), aml_name("_SUN"))
        );
        aml_append(dev, method);

        build_append_pcihp_notify_entry(notify_method, slot);

        aml_append(parent_scope, dev);
    }
    aml_append(parent_scope, notify_method);
}

void build_append_pci_bus_devices(Aml *parent_scope, PCIBus *bus)
{
    int devfn;
    Aml *dev;

    for (devfn = 0; devfn < ARRAY_SIZE(bus->devices); devfn++) {
        int adr = PCI_SLOT(devfn) << 16 | PCI_FUNC(devfn);
        PCIDevice *pdev = bus->devices[devfn];

        if (!pdev || is_devfn_ignored_generic(devfn, bus)) {
            continue;
        }

        dev = aml_device("S%.02X", devfn);
        aml_append(dev, aml_name_decl("_ADR", aml_int(adr)));

        call_dev_aml_func(DEVICE(bus->devices[devfn]), dev);
        if (pdev->acpi_index &&
            !object_property_get_bool(OBJECT(pdev), "hotpluggable",
                                      &error_abort)) {
            aml_append(dev, aml_pci_static_endpoint_dsm(pdev));
        }

        aml_append(parent_scope, dev);
    }
}

const VMStateDescription vmstate_acpi_pcihp_pci_status = {
    .name = "acpi_pcihp_pci_status",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(up, AcpiPciHpPciStatus),
        VMSTATE_UINT32(down, AcpiPciHpPciStatus),
        VMSTATE_END_OF_LIST()
    }
};
