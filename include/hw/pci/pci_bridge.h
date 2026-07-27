/*
 * QEMU PCI bridge
 *
 * Copyright (c) 2004 Fabrice Bellard
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <https://www.gnu.org/licenses/>.
 *
 * split out pci bus specific stuff from pci.[hc] to pci_bridge.[hc]
 * Copyright (c) 2009 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 */

#ifndef QEMU_PCI_BRIDGE_H
#define QEMU_PCI_BRIDGE_H

#include "hw/pci/pci_device.h"
#include "hw/pci/pci_bus.h"
#include "hw/cxl/cxl.h"
#include "qom/object.h"

typedef struct PCIBridgeWindows PCIBridgeWindows;

struct PCIBridgeWindows {
    MemoryRegion alias_pref_mem;
    MemoryRegion alias_mem;
    MemoryRegion alias_io;
    MemoryRegion alias_vga[QEMU_PCI_VGA_NUM_REGIONS];
};

#define TYPE_PCI_BRIDGE "base-pci-bridge"
OBJECT_DECLARE_SIMPLE_TYPE(PCIBridge, PCI_BRIDGE)
#define IS_PCI_BRIDGE(dev) object_dynamic_cast(OBJECT(dev), TYPE_PCI_BRIDGE)

struct PCIBridge {
    PCIDevice parent_obj;

    PCIBus sec_bus;
    MemoryRegion address_space_mem;
    MemoryRegion address_space_io;
    AddressSpace as_mem;
    AddressSpace as_io;

    PCIBridgeWindows windows;

    pci_map_irq_fn map_irq;
    const char *bus_name;

    bool pcie_writeable_slt_bug;
};

#define PCI_BRIDGE_DEV_PROP_CHASSIS_NR "chassis_nr"
#define PCI_BRIDGE_DEV_PROP_MSI        "msi"
#define PCI_BRIDGE_DEV_PROP_SHPC       "shpc"
typedef struct CXLHost CXLHost;

typedef struct PXBDev {
    PCIDevice parent_obj;

    uint8_t bus_nr;
    uint16_t numa_node;
    bool bypass_iommu;
} PXBDev;

typedef struct PXBPCIEDev {
    PXBDev parent_obj;
} PXBPCIEDev;

#define TYPE_PXB_PCIE_BUS "pxb-pcie-bus"
#define TYPE_PXB_CXL_BUS "pxb-cxl-bus"
#define TYPE_PXB_DEV "pxb"
OBJECT_DECLARE_SIMPLE_TYPE(PXBDev, PXB_DEV)

typedef struct PXBCXLDev {
    PXBPCIEDev parent_obj;

    bool hdm_for_passthrough;
    CXLHost *cxl_host_bridge;
} PXBCXLDev;

#define TYPE_PXB_CXL_DEV "pxb-cxl"
OBJECT_DECLARE_SIMPLE_TYPE(PXBCXLDev, PXB_CXL_DEV)

int pci_bridge_ssvid_init(PCIDevice *dev, uint8_t offset,
                          uint16_t svid, uint16_t ssid,
                          Error **errp);

PCIDevice *pci_bridge_get_device(PCIBus *bus);
PCIBus *pci_bridge_get_sec_bus(PCIBridge *br);

pcibus_t pci_bridge_get_base(const PCIDevice *bridge, uint8_t type);
pcibus_t pci_bridge_get_limit(const PCIDevice *bridge, uint8_t type);

void pci_bridge_update_mappings(PCIBridge *br);
void pci_bridge_write_config(PCIDevice *d,
                             uint32_t address, uint32_t val, int len);
void pci_bridge_disable_base_limit(PCIDevice *dev);
void pci_bridge_reset(DeviceState *qdev);

void pci_bridge_initfn(PCIDevice *pci_dev, const char *typename);
void pci_bridge_exitfn(PCIDevice *pci_dev);

void pci_bridge_dev_plug_cb(HotplugHandler *hotplug_dev, DeviceState *dev,
                            Error **errp);
void pci_bridge_dev_unplug_cb(HotplugHandler *hotplug_dev, DeviceState *dev,
                              Error **errp);
void pci_bridge_dev_unplug_request_cb(HotplugHandler *hotplug_dev,
                                      DeviceState *dev, Error **errp);

void pci_bridge_map_irq(PCIBridge *br, const char* bus_name,
                        pci_map_irq_fn map_irq);

#define  PCI_BRIDGE_CTL_VGA_16BIT       0x10
#define  PCI_BRIDGE_CTL_DISCARD         0x100
#define  PCI_BRIDGE_CTL_SEC_DISCARD     0x200
#define  PCI_BRIDGE_CTL_DISCARD_STATUS  0x400
#define  PCI_BRIDGE_CTL_DISCARD_SERR    0x800

typedef struct PCIBridgeQemuCap {
    uint8_t id;
    uint8_t next;
    uint8_t len;
    uint8_t type;

    uint32_t bus_res;
    uint64_t io;
    uint32_t mem;
    uint32_t mem_pref_32;
    uint64_t mem_pref_64;
} PCIBridgeQemuCap;

#define REDHAT_PCI_CAP_TYPE_OFFSET      3
#define REDHAT_PCI_CAP_RESOURCE_RESERVE 1

typedef struct PCIResReserve {
    uint32_t bus;
    uint64_t io;
    uint64_t mem_non_pref;
    uint64_t mem_pref_32;
    uint64_t mem_pref_64;
} PCIResReserve;

#define REDHAT_PCI_CAP_RES_RESERVE_BUS_RES     4
#define REDHAT_PCI_CAP_RES_RESERVE_IO          8
#define REDHAT_PCI_CAP_RES_RESERVE_MEM         16
#define REDHAT_PCI_CAP_RES_RESERVE_PREF_MEM_32 20
#define REDHAT_PCI_CAP_RES_RESERVE_PREF_MEM_64 24
#define REDHAT_PCI_CAP_RES_RESERVE_CAP_SIZE    32

int pci_bridge_qemu_reserve_cap_init(PCIDevice *dev, int cap_offset,
                               PCIResReserve res_reserve, Error **errp);

#endif
