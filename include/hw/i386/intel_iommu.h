/*
 * QEMU emulation of an Intel IOMMU (VT-d)
 *   (DMA Remapping device)
 *
 * Copyright (C) 2013 Knut Omang, Oracle <knut.omang@oracle.com>
 * Copyright (C) 2014 Le Tan, <tamlokveer@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef INTEL_IOMMU_H
#define INTEL_IOMMU_H

#include "hw/i386/x86-iommu.h"
#include "qemu/iova-tree.h"
#include "qom/object.h"

#define TYPE_INTEL_IOMMU_DEVICE "intel-iommu"
OBJECT_DECLARE_SIMPLE_TYPE(IntelIOMMUState, INTEL_IOMMU_DEVICE)

#define TYPE_INTEL_IOMMU_MEMORY_REGION "intel-iommu-iommu-memory-region"

#define Q35_HOST_BRIDGE_IOMMU_ADDR  0xfed90000ULL

#define VTD_PCI_BUS_MAX             256
#define VTD_PCI_SLOT_MAX            32
#define VTD_PCI_FUNC_MAX            8
#define VTD_PCI_SLOT(devfn)         (((devfn) >> 3) & 0x1f)
#define VTD_PCI_FUNC(devfn)         ((devfn) & 0x07)
#define VTD_SID_TO_BUS(sid)         (((sid) >> 8) & 0xff)
#define VTD_SID_TO_DEVFN(sid)       ((sid) & 0xff)

#define DMAR_REG_SIZE               0x230
#define VTD_HOST_AW_39BIT           39
#define VTD_HOST_AW_48BIT           48
#define VTD_HOST_ADDRESS_WIDTH      VTD_HOST_AW_48BIT
#define VTD_HAW_MASK(aw)            ((1ULL << (aw)) - 1)
#define VTD_MGAW_FROM_CAP(cap)      ((cap >> 16) & 0x3fULL)

#define DMAR_REPORT_F_INTR          (1)

#define  VTD_MSI_ADDR_HI_MASK        (0xffffffff00000000ULL)
#define  VTD_MSI_ADDR_HI_SHIFT       (32)
#define  VTD_MSI_ADDR_LO_MASK        (0x00000000ffffffffULL)

typedef struct VTDContextEntry VTDContextEntry;
typedef struct VTDContextCacheEntry VTDContextCacheEntry;
typedef struct VTDAddressSpace VTDAddressSpace;
typedef struct VTDIOTLBEntry VTDIOTLBEntry;
typedef union VTD_IR_TableEntry VTD_IR_TableEntry;
typedef union VTD_IR_MSIAddress VTD_IR_MSIAddress;
typedef struct VTDPASIDDirEntry VTDPASIDDirEntry;
typedef struct VTDPASIDEntry VTDPASIDEntry;

struct VTDContextEntry {
    union {
        struct {
            uint64_t lo;
            uint64_t hi;
        };
        struct {
            uint64_t val[4];
        };
    };
};

struct VTDContextCacheEntry {
    uint32_t context_cache_gen;
    struct VTDContextEntry context_entry;
};

struct VTDPASIDDirEntry {
    uint64_t val;
};

struct VTDPASIDEntry {
    uint64_t val[8];
};

typedef struct VTDPASIDCacheEntry {
    struct VTDPASIDEntry pasid_entry;
    bool valid;
} VTDPASIDCacheEntry;

struct VTDAddressSpace {
    PCIBus *bus;
    uint8_t devfn;
    uint32_t pasid;
    AddressSpace as;
    IOMMUMemoryRegion iommu;
    MemoryRegion root;
    MemoryRegion nodmar;
    MemoryRegion iommu_ir;
    MemoryRegion iommu_ir_fault;
    IntelIOMMUState *iommu_state;
    VTDContextCacheEntry context_cache_entry;
    VTDPASIDCacheEntry pasid_cache_entry;
    QLIST_ENTRY(VTDAddressSpace) next;
    IOMMUNotifierFlag notifier_flags;
    IOMMUPRINotifier *pri_notifier;
    IOVATree *iova_tree;
};

struct VTDIOTLBEntry {
    uint64_t gfn;
    uint16_t domain_id;
    uint32_t pasid;
    uint64_t pte;
    uint64_t mask;
    uint8_t access_flags;
    uint8_t pgtt;
};

enum {
    VTD_SQ_FULL = 0x00,
    VTD_SQ_IGN_3 = 0x01,
    VTD_SQ_IGN_2_3 = 0x02,
    VTD_SQ_IGN_1_3 = 0x03,
    VTD_SQ_MAX,
};

enum {
    VTD_SVT_NONE = 0x00,
    VTD_SVT_ALL = 0x01,
    VTD_SVT_BUS = 0x02,
    VTD_SVT_MAX,
};

union VTD_IR_TableEntry {
    struct {
#if HOST_BIG_ENDIAN
        uint64_t dest_id:32;
        uint64_t __reserved_1:8;
        uint64_t vector:8;
        uint64_t irte_mode:1;
        uint64_t __reserved_0:3;
        uint64_t __avail:4;
        uint64_t delivery_mode:3;
        uint64_t trigger_mode:1;
        uint64_t redir_hint:1;
        uint64_t dest_mode:1;
        uint64_t fault_disable:1;
        uint64_t present:1;
#else
        uint64_t present:1;
        uint64_t fault_disable:1;
        uint64_t dest_mode:1;
        uint64_t redir_hint:1;
        uint64_t trigger_mode:1;
        uint64_t delivery_mode:3;
        uint64_t __avail:4;
        uint64_t __reserved_0:3;
        uint64_t irte_mode:1;
        uint64_t vector:8;
        uint64_t __reserved_1:8;
        uint64_t dest_id:32;
#endif
#if HOST_BIG_ENDIAN
        uint64_t __reserved_2:44;
        uint64_t sid_vtype:2;
        uint64_t sid_q:2;
        uint64_t source_id:16;
#else
        uint64_t source_id:16;
        uint64_t sid_q:2;
        uint64_t sid_vtype:2;
        uint64_t __reserved_2:44;
#endif
    } QEMU_PACKED irte;
    uint64_t data[2];
};

#define VTD_IR_INT_FORMAT_COMPAT     (0)
#define VTD_IR_INT_FORMAT_REMAP      (1)

union VTD_IR_MSIAddress {
    struct {
#if HOST_BIG_ENDIAN
        uint32_t __head:12;
        uint32_t index_l:15;
        uint32_t int_mode:1;
        uint32_t sub_valid:1;
        uint32_t index_h:1;
        uint32_t __not_care:2;
#else
        uint32_t __not_care:2;
        uint32_t index_h:1;
        uint32_t sub_valid:1;
        uint32_t int_mode:1;
        uint32_t index_l:15;
        uint32_t __head:12;
#endif
    } QEMU_PACKED addr;
    uint32_t data;
};

#define VTD_IR_MSI_DATA          (0)

struct IntelIOMMUState {
    X86IOMMUState x86_iommu;
    MemoryRegion csrmem;
    MemoryRegion mr_nodmar;
    MemoryRegion mr_ir;
    MemoryRegion mr_sys_alias;
    uint8_t csr[DMAR_REG_SIZE];
    uint8_t wmask[DMAR_REG_SIZE];
    uint8_t w1cmask[DMAR_REG_SIZE];
    uint8_t womask[DMAR_REG_SIZE];
    uint32_t version;

    bool caching_mode;
    bool scalable_mode;
    bool flts;
    bool snoop_control;

    dma_addr_t root;
    bool root_scalable;
    bool dmar_enabled;

    uint16_t iq_head;
    uint16_t iq_tail;
    dma_addr_t iq;
    uint16_t iq_size;
    bool iq_dw;
    bool qi_enabled;
    uint8_t iq_last_desc_type;

    uint16_t next_frcd_reg;

    uint64_t cap;
    uint64_t ecap;

    uint32_t context_cache_gen;
    GHashTable *iotlb;

    GHashTable *vtd_address_spaces;
    VTDAddressSpace *vtd_as_cache[VTD_PCI_BUS_MAX];
    QLIST_HEAD(, VTDAddressSpace) vtd_as_with_notifiers;

    GHashTable *vtd_host_iommu_dev;

    bool intr_enabled;
    dma_addr_t intr_root;
    uint32_t intr_size;
    bool intr_eime;
    OnOffAuto intr_eim;
    bool buggy_eim;
    uint8_t aw_bits;
    bool dma_drain;
    bool pasid;
    bool fs1gp;

    bool stale_tm;

    QemuMutex iommu_lock;
};

VTDAddressSpace *vtd_find_add_as(IntelIOMMUState *s, PCIBus *bus,
                                 int devfn, unsigned int pasid);

#endif
