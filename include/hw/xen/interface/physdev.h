/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2006, Keir Fraser
 */

#ifndef __XEN_PUBLIC_PHYSDEV_H__
#define __XEN_PUBLIC_PHYSDEV_H__

#include "xen.h"

#define PHYSDEVOP_eoi                   12
struct physdev_eoi {
    uint32_t irq;
};
typedef struct physdev_eoi physdev_eoi_t;
DEFINE_XEN_GUEST_HANDLE(physdev_eoi_t);

#define PHYSDEVOP_pirq_eoi_gmfn_v1       17
#define PHYSDEVOP_pirq_eoi_gmfn_v2       28
struct physdev_pirq_eoi_gmfn {
    xen_pfn_t gmfn;
};
typedef struct physdev_pirq_eoi_gmfn physdev_pirq_eoi_gmfn_t;
DEFINE_XEN_GUEST_HANDLE(physdev_pirq_eoi_gmfn_t);

#define PHYSDEVOP_irq_status_query       5
struct physdev_irq_status_query {
    uint32_t irq;
    uint32_t flags;
};
typedef struct physdev_irq_status_query physdev_irq_status_query_t;
DEFINE_XEN_GUEST_HANDLE(physdev_irq_status_query_t);

#define _XENIRQSTAT_needs_eoi   (0)
#define  XENIRQSTAT_needs_eoi   (1U<<_XENIRQSTAT_needs_eoi)

#define _XENIRQSTAT_shared      (1)
#define  XENIRQSTAT_shared      (1U<<_XENIRQSTAT_shared)

#define PHYSDEVOP_set_iopl               6
struct physdev_set_iopl {
    uint32_t iopl;
};
typedef struct physdev_set_iopl physdev_set_iopl_t;
DEFINE_XEN_GUEST_HANDLE(physdev_set_iopl_t);

#define PHYSDEVOP_set_iobitmap           7
struct physdev_set_iobitmap {
#if __XEN_INTERFACE_VERSION__ >= 0x00030205
    XEN_GUEST_HANDLE(uint8) bitmap;
#else
    uint8_t *bitmap;
#endif
    uint32_t nr_ports;
};
typedef struct physdev_set_iobitmap physdev_set_iobitmap_t;
DEFINE_XEN_GUEST_HANDLE(physdev_set_iobitmap_t);

#define PHYSDEVOP_apic_read              8
#define PHYSDEVOP_apic_write             9
struct physdev_apic {
    unsigned long apic_physbase;
    uint32_t reg;
    uint32_t value;
};
typedef struct physdev_apic physdev_apic_t;
DEFINE_XEN_GUEST_HANDLE(physdev_apic_t);

#define PHYSDEVOP_alloc_irq_vector      10
#define PHYSDEVOP_free_irq_vector       11
struct physdev_irq {
    uint32_t irq;
    uint32_t vector;
};
typedef struct physdev_irq physdev_irq_t;
DEFINE_XEN_GUEST_HANDLE(physdev_irq_t);

#define MAP_PIRQ_TYPE_MSI               0x0
#define MAP_PIRQ_TYPE_GSI               0x1
#define MAP_PIRQ_TYPE_UNKNOWN           0x2
#define MAP_PIRQ_TYPE_MSI_SEG           0x3
#define MAP_PIRQ_TYPE_MULTI_MSI         0x4

#define PHYSDEVOP_map_pirq               13
struct physdev_map_pirq {
    domid_t domid;
    int type;
    int index;
    int pirq;
    int bus;
    int devfn;
    int entry_nr;
    uint64_t table_base;
};
typedef struct physdev_map_pirq physdev_map_pirq_t;
DEFINE_XEN_GUEST_HANDLE(physdev_map_pirq_t);

#define PHYSDEVOP_unmap_pirq             14
struct physdev_unmap_pirq {
    domid_t domid;
    int pirq;
};

typedef struct physdev_unmap_pirq physdev_unmap_pirq_t;
DEFINE_XEN_GUEST_HANDLE(physdev_unmap_pirq_t);

#define PHYSDEVOP_manage_pci_add         15
#define PHYSDEVOP_manage_pci_remove      16
struct physdev_manage_pci {
    uint8_t bus;
    uint8_t devfn;
};

typedef struct physdev_manage_pci physdev_manage_pci_t;
DEFINE_XEN_GUEST_HANDLE(physdev_manage_pci_t);

#define PHYSDEVOP_restore_msi            19
struct physdev_restore_msi {
    uint8_t bus;
    uint8_t devfn;
};
typedef struct physdev_restore_msi physdev_restore_msi_t;
DEFINE_XEN_GUEST_HANDLE(physdev_restore_msi_t);

#define PHYSDEVOP_manage_pci_add_ext     20
struct physdev_manage_pci_ext {
    uint8_t bus;
    uint8_t devfn;
    uint32_t is_extfn;
    uint32_t is_virtfn;
    struct {
        uint8_t bus;
        uint8_t devfn;
    } physfn;
};

typedef struct physdev_manage_pci_ext physdev_manage_pci_ext_t;
DEFINE_XEN_GUEST_HANDLE(physdev_manage_pci_ext_t);

struct physdev_op {
    uint32_t cmd;
    union {
        physdev_irq_status_query_t irq_status_query;
        physdev_set_iopl_t         set_iopl;
        physdev_set_iobitmap_t     set_iobitmap;
        physdev_apic_t             apic_op;
        physdev_irq_t              irq_op;
    } u;
};
typedef struct physdev_op physdev_op_t;
DEFINE_XEN_GUEST_HANDLE(physdev_op_t);

#define PHYSDEVOP_setup_gsi    21
struct physdev_setup_gsi {
    int gsi;
    uint8_t triggering;
    uint8_t polarity;
};

typedef struct physdev_setup_gsi physdev_setup_gsi_t;
DEFINE_XEN_GUEST_HANDLE(physdev_setup_gsi_t);

#define PHYSDEVOP_get_free_pirq    23
struct physdev_get_free_pirq {
    int type;
    uint32_t pirq;
};

typedef struct physdev_get_free_pirq physdev_get_free_pirq_t;
DEFINE_XEN_GUEST_HANDLE(physdev_get_free_pirq_t);

#define XEN_PCI_MMCFG_RESERVED         0x1

#define PHYSDEVOP_pci_mmcfg_reserved    24
struct physdev_pci_mmcfg_reserved {
    uint64_t address;
    uint16_t segment;
    uint8_t start_bus;
    uint8_t end_bus;
    uint32_t flags;
};
typedef struct physdev_pci_mmcfg_reserved physdev_pci_mmcfg_reserved_t;
DEFINE_XEN_GUEST_HANDLE(physdev_pci_mmcfg_reserved_t);

#define XEN_PCI_DEV_EXTFN              0x1
#define XEN_PCI_DEV_VIRTFN             0x2
#define XEN_PCI_DEV_PXM                0x4

#define PHYSDEVOP_pci_device_add        25
struct physdev_pci_device_add {
    uint16_t seg;
    uint8_t bus;
    uint8_t devfn;
    uint32_t flags;
    struct {
        uint8_t bus;
        uint8_t devfn;
    } physfn;
    uint32_t optarr[XEN_FLEX_ARRAY_DIM];
};
typedef struct physdev_pci_device_add physdev_pci_device_add_t;
DEFINE_XEN_GUEST_HANDLE(physdev_pci_device_add_t);

#define PHYSDEVOP_pci_device_remove     26
#define PHYSDEVOP_restore_msi_ext       27
#define PHYSDEVOP_prepare_msix          30
#define PHYSDEVOP_release_msix          31
struct physdev_pci_device {
    uint16_t seg;
    uint8_t bus;
    uint8_t devfn;
};
typedef struct physdev_pci_device physdev_pci_device_t;
DEFINE_XEN_GUEST_HANDLE(physdev_pci_device_t);

#define PHYSDEVOP_DBGP_RESET_PREPARE    1
#define PHYSDEVOP_DBGP_RESET_DONE       2

#define PHYSDEVOP_DBGP_BUS_UNKNOWN      0
#define PHYSDEVOP_DBGP_BUS_PCI          1

#define PHYSDEVOP_dbgp_op               29
struct physdev_dbgp_op {
    uint8_t op;
    uint8_t bus;
    union {
        physdev_pci_device_t pci;
    } u;
};
typedef struct physdev_dbgp_op physdev_dbgp_op_t;
DEFINE_XEN_GUEST_HANDLE(physdev_dbgp_op_t);

#define PHYSDEVOP_IRQ_UNMASK_NOTIFY      4

#if __XEN_INTERFACE_VERSION__ < 0x00040600
#define PHYSDEVOP_IRQ_STATUS_QUERY       PHYSDEVOP_irq_status_query
#define PHYSDEVOP_SET_IOPL               PHYSDEVOP_set_iopl
#define PHYSDEVOP_SET_IOBITMAP           PHYSDEVOP_set_iobitmap
#define PHYSDEVOP_APIC_READ              PHYSDEVOP_apic_read
#define PHYSDEVOP_APIC_WRITE             PHYSDEVOP_apic_write
#define PHYSDEVOP_ASSIGN_VECTOR          PHYSDEVOP_alloc_irq_vector
#define PHYSDEVOP_FREE_VECTOR            PHYSDEVOP_free_irq_vector
#define PHYSDEVOP_IRQ_NEEDS_UNMASK_NOTIFY XENIRQSTAT_needs_eoi
#define PHYSDEVOP_IRQ_SHARED             XENIRQSTAT_shared
#endif

#if __XEN_INTERFACE_VERSION__ < 0x00040200
#define PHYSDEVOP_pirq_eoi_gmfn PHYSDEVOP_pirq_eoi_gmfn_v1
#else
#define PHYSDEVOP_pirq_eoi_gmfn PHYSDEVOP_pirq_eoi_gmfn_v2
#endif

#endif

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
