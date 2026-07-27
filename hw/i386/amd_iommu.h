/*
 * QEMU emulation of an AMD IOMMU (AMD-Vi)
 *
 * Copyright (C) 2011 Eduard - Gabriel Munteanu
 * Copyright (C) 2015, 2016 David Kiarie Kahurani
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

#ifndef AMD_IOMMU_H
#define AMD_IOMMU_H

#include "hw/pci/pci.h"
#include "hw/i386/x86-iommu.h"
#include "qom/object.h"

#define GENMASK64(h, l)  (((~0ULL) >> (63 - (h) + (l))) << (l))

#define AMDVI_CAPAB_BAR_LOW           0x04
#define AMDVI_CAPAB_BAR_HIGH          0x08
#define AMDVI_CAPAB_RANGE             0x0C
#define AMDVI_CAPAB_MISC              0x10

#define AMDVI_CAPAB_SIZE              0x18
#define AMDVI_CAPAB_REG_SIZE          0x04

#define AMDVI_CAPAB_ID_SEC            0xf
#define AMDVI_CAPAB_FLAT_EXT          (1 << 28)
#define AMDVI_CAPAB_EFR_SUP           (1 << 27)
#define AMDVI_CAPAB_FLAG_NPCACHE      (1 << 26)
#define AMDVI_CAPAB_FLAG_HTTUNNEL     (1 << 25)
#define AMDVI_CAPAB_FLAG_IOTLBSUP     (1 << 24)
#define AMDVI_CAPAB_INIT_TYPE         (3 << 16)

#define AMDVI_MMIO_REGS_HIGH  7
#define AMDVI_MMIO_REGS_LOW   8

#define AMDVI_MMIO_DEVICE_TABLE       0x0000
#define AMDVI_MMIO_COMMAND_BASE       0x0008
#define AMDVI_MMIO_EVENT_BASE         0x0010
#define AMDVI_MMIO_CONTROL            0x0018
#define AMDVI_MMIO_EXCL_BASE          0x0020
#define AMDVI_MMIO_EXCL_LIMIT         0x0028
#define AMDVI_MMIO_EXT_FEATURES       0x0030
#define AMDVI_MMIO_COMMAND_HEAD       0x2000
#define AMDVI_MMIO_COMMAND_TAIL       0x2008
#define AMDVI_MMIO_EVENT_HEAD         0x2010
#define AMDVI_MMIO_EVENT_TAIL         0x2018
#define AMDVI_MMIO_STATUS             0x2020
#define AMDVI_MMIO_PPR_BASE           0x0038
#define AMDVI_MMIO_PPR_HEAD           0x2030
#define AMDVI_MMIO_PPR_TAIL           0x2038

#define AMDVI_MMIO_SIZE               0x4000

#define AMDVI_MMIO_DEVTAB_SIZE_MASK     GENMASK64(8, 0)
#define AMDVI_MMIO_DEVTAB_BASE_MASK     GENMASK64(51, 12)

#define AMDVI_MMIO_DEVTAB_ENTRY_SIZE  32
#define AMDVI_MMIO_DEVTAB_SIZE_UNIT   4096

#define AMDVI_MMIO_CMDBUF_SIZE_BYTE       (AMDVI_MMIO_COMMAND_BASE + 7)
#define AMDVI_MMIO_CMDBUF_SIZE_MASK       0x0f
#define AMDVI_MMIO_CMDBUF_BASE_MASK       GENMASK64(51, 12)
#define AMDVI_MMIO_CMDBUF_HEAD_MASK       GENMASK64(18, 4)
#define AMDVI_MMIO_CMDBUF_TAIL_MASK       GENMASK64(18, 4)

#define AMDVI_MMIO_EVTLOG_SIZE_BYTE       (AMDVI_MMIO_EVENT_BASE + 7)
#define AMDVI_MMIO_EVTLOG_SIZE_MASK       0x0f
#define AMDVI_MMIO_EVTLOG_BASE_MASK       GENMASK64(51, 12)
#define AMDVI_MMIO_EVTLOG_HEAD_MASK       GENMASK64(18, 4)
#define AMDVI_MMIO_EVTLOG_TAIL_MASK       GENMASK64(18, 4)

#define AMDVI_MMIO_PPRLOG_SIZE_BYTE       (AMDVI_MMIO_PPR_BASE + 7)
#define AMDVI_MMIO_PPRLOG_SIZE_MASK       0x0f
#define AMDVI_MMIO_PPRLOG_BASE_MASK       GENMASK64(51, 12)
#define AMDVI_MMIO_PPRLOG_HEAD_MASK       GENMASK64(18, 4)
#define AMDVI_MMIO_PPRLOG_TAIL_MASK       GENMASK64(18, 4)

#define AMDVI_MMIO_EXCL_ENABLED_MASK      (1ULL << 0)
#define AMDVI_MMIO_EXCL_ALLOW_MASK        (1ULL << 1)
#define AMDVI_MMIO_EXCL_LIMIT_MASK        GENMASK64(51, 12)
#define AMDVI_MMIO_EXCL_LIMIT_LOW         0xfff

#define AMDVI_MMIO_CONTROL_AMDVIEN        (1ULL << 0)
#define AMDVI_MMIO_CONTROL_HTTUNEN        (1ULL << 1)
#define AMDVI_MMIO_CONTROL_EVENTLOGEN     (1ULL << 2)
#define AMDVI_MMIO_CONTROL_EVENTINTEN     (1ULL << 3)
#define AMDVI_MMIO_CONTROL_COMWAITINTEN   (1ULL << 4)
#define AMDVI_MMIO_CONTROL_CMDBUFLEN      (1ULL << 12)
#define AMDVI_MMIO_CONTROL_GAEN           (1ULL << 17)

#define AMDVI_MMIO_STATUS_CMDBUF_RUN  (1 << 4)
#define AMDVI_MMIO_STATUS_EVT_RUN     (1 << 3)
#define AMDVI_MMIO_STATUS_COMP_INT    (1 << 2)
#define AMDVI_MMIO_STATUS_EVENT_INT   (1 << 1)
#define AMDVI_MMIO_STATUS_EVT_OVF     (1 << 0)

#define AMDVI_CMDBUF_ID_BYTE              0x07
#define AMDVI_CMDBUF_ID_RSHIFT            4

#define AMDVI_CMD_COMPLETION_WAIT         0x01
#define AMDVI_CMD_INVAL_DEVTAB_ENTRY      0x02
#define AMDVI_CMD_INVAL_AMDVI_PAGES       0x03
#define AMDVI_CMD_INVAL_IOTLB_PAGES       0x04
#define AMDVI_CMD_INVAL_INTR_TABLE        0x05
#define AMDVI_CMD_PREFETCH_AMDVI_PAGES    0x06
#define AMDVI_CMD_COMPLETE_PPR_REQUEST    0x07
#define AMDVI_CMD_INVAL_AMDVI_ALL         0x08


#define AMDVI_CMD_INVAL_IOMMU_PAGES_S   (1ULL << 0)
#define AMDVI_INV_ALL_PAGES             (1ULL << 52)

#define AMDVI_DEVTAB_ENTRY_SIZE           32

#define AMDVI_DEV_VALID                   (1ULL << 0)
#define AMDVI_DEV_TRANSLATION_VALID       (1ULL << 1)
#define AMDVI_DEV_MODE_MASK               0x7
#define AMDVI_DEV_MODE_RSHIFT             9
#define AMDVI_DEV_PT_ROOT_MASK            GENMASK64(51, 12)
#define AMDVI_DEV_PT_ROOT_RSHIFT          12
#define AMDVI_DEV_PERM_SHIFT              61
#define AMDVI_DEV_PERM_READ               (1ULL << 61)
#define AMDVI_DEV_PERM_WRITE              (1ULL << 62)

#define AMDVI_DEV_DOMID_ID_MASK             GENMASK64(15, 0)

#define AMDVI_EVENT_ILLEGAL_DEVTAB_ENTRY  (0x1U << 12)
#define AMDVI_EVENT_IOPF                  (0x2U << 12)
#define   AMDVI_EVENT_IOPF_I              (1U << 3)
#define AMDVI_EVENT_DEV_TAB_HW_ERROR      (0x3U << 12)
#define AMDVI_EVENT_PAGE_TAB_HW_ERROR     (0x4U << 12)
#define AMDVI_EVENT_ILLEGAL_COMMAND_ERROR (0x5U << 12)
#define AMDVI_EVENT_COMMAND_HW_ERROR      (0x6U << 12)

#define AMDVI_EVENT_LEN                  16
#define AMDVI_PERM_READ             (1 << 0)
#define AMDVI_PERM_WRITE            (1 << 1)

#define AMDVI_FEATURE_PREFETCH            (1ULL << 0)
#define AMDVI_FEATURE_PPR                 (1ULL << 1)
#define AMDVI_FEATURE_XT                  (1ULL << 2)
#define AMDVI_FEATURE_GT                  (1ULL << 4)
#define AMDVI_FEATURE_IA                  (1ULL << 6)
#define AMDVI_FEATURE_GA                  (1ULL << 7)
#define AMDVI_FEATURE_HE                  (1ULL << 8)
#define AMDVI_FEATURE_PC                  (1ULL << 9)

#define AMDVI_DTE_QUAD0_RESERVED        (GENMASK64(6, 2) | GENMASK64(63, 63))
#define AMDVI_DTE_QUAD1_RESERVED        0
#define AMDVI_DTE_QUAD2_RESERVED        GENMASK64(53, 52)
#define AMDVI_DTE_QUAD3_RESERVED        (GENMASK64(14, 0) | GENMASK64(53, 48))

#define AMDVI_GATS_MODE                 (2ULL <<  12)
#define AMDVI_HATS_MODE                 (2ULL <<  10)
#define AMDVI_HATS_MODE_RESERVED        (3ULL <<  10)

#define AMDVI_PTE_PR                    (1ULL << 0)
#define AMDVI_PTE_NEXT_LEVEL_MASK       GENMASK64(11, 9)

#define IOMMU_PTE_PRESENT(pte)          ((pte) & AMDVI_PTE_PR)

#define PT_LEVEL_SHIFT(level)           (12 + ((level) * 9))

#define PT_LEVEL_INDEX(level, iova)     (((iova) >> PT_LEVEL_SHIFT(level)) & \
                                        GENMASK64(8, 0))

#define PT_LEVEL_MAX_ADDR(x)    (((x) < 5) ? \
                                ((1ULL << PT_LEVEL_SHIFT((x + 1))) - 1) : \
                                (~(0ULL)))

#define PTE_NEXT_LEVEL(pte)     (((pte) & AMDVI_PTE_NEXT_LEVEL_MASK) >> 9)

#define PTE_LEVEL_PAGE_SIZE(level)      (1ULL << (PT_LEVEL_SHIFT(level)))

#define NEXT_PTE_ADDR(pte, level, iova) (((pte) & AMDVI_DEV_PT_ROOT_MASK) + \
                                        (PT_LEVEL_INDEX(level, iova) * 8))

#define PTE_LARGE_PAGE_SIZE(pte)    (1ULL << (1 + cto64(((pte) | 0xfffULL))))

#define PAGE_SIZE_PTE_COUNT(pgsz)       (1ULL << ((ctz64(pgsz) - 12) % 9))

#define AMDVI_IOTLB_MAX_SIZE        1024
#define AMDVI_GET_IOTLB_GFN(addr)   (addr >> AMDVI_PAGE_SHIFT_4K)

#define AMDVI_DEFAULT_EXT_FEATURES \
        (AMDVI_FEATURE_PREFETCH | AMDVI_FEATURE_PPR | \
        AMDVI_FEATURE_IA | AMDVI_FEATURE_GT | AMDVI_FEATURE_HE | \
        AMDVI_GATS_MODE | AMDVI_HATS_MODE | AMDVI_FEATURE_GA)

#define AMDVI_CAPAB_FEATURES (AMDVI_CAPAB_FLAT_EXT | \
        AMDVI_CAPAB_FLAG_NPCACHE | AMDVI_CAPAB_FLAG_IOTLBSUP \
        | AMDVI_CAPAB_ID_SEC | AMDVI_CAPAB_INIT_TYPE | \
        AMDVI_CAPAB_FLAG_HTTUNNEL |  AMDVI_CAPAB_EFR_SUP)

#define AMDVI_BASE_ADDR 0xfed80000ULL

#define AMDVI_PAGE_SHIFT 12
#define AMDVI_PAGE_SIZE  (1ULL << AMDVI_PAGE_SHIFT)

#define AMDVI_PAGE_SHIFT_4K 12
#define AMDVI_PAGE_MASK_4K      GENMASK64(63, 12)

#define AMDVI_MAX_GVA_ADDR      (2UL << 5)
#define AMDVI_MAX_PH_ADDR       (40UL << 8)
#define AMDVI_MAX_VA_ADDR       (48UL << 15)

#define AMDVI_COMPLETION_DATA_SIZE    8

#define AMDVI_COMMAND_SIZE   16

#define AMDVI_INT_ADDR_FIRST    0xfee00000
#define AMDVI_INT_ADDR_LAST     0xfeefffff
#define AMDVI_INT_ADDR_SIZE     (AMDVI_INT_ADDR_LAST - AMDVI_INT_ADDR_FIRST + 1)

#define AMDVI_IOAPIC_SB_DEVID   PCI_BUILD_BDF(0, PCI_DEVFN(0x14, 0))

#define AMDVI_IR_ERR            0x1
#define AMDVI_IR_GET_IRTE       0x2
#define AMDVI_IR_TARGET_ABORT   0x3

#define AMDVI_IR_REMAP_ENABLE           1ULL
#define AMDVI_IR_INTCTL_SHIFT           60
#define AMDVI_IR_INTCTL_ABORT           0
#define AMDVI_IR_INTCTL_PASS            1
#define AMDVI_IR_INTCTL_REMAP           2

#define AMDVI_IR_PHYS_ADDR_MASK         GENMASK64(51, 6)

#define AMDVI_IRTE_OFFSET               0x7ff

#define AMDVI_IOAPIC_INT_TYPE_FIXED          0x0
#define AMDVI_IOAPIC_INT_TYPE_ARBITRATED     0x1
#define AMDVI_IOAPIC_INT_TYPE_SMI            0x2
#define AMDVI_IOAPIC_INT_TYPE_NMI            0x4
#define AMDVI_IOAPIC_INT_TYPE_INIT           0x5
#define AMDVI_IOAPIC_INT_TYPE_EINT           0x7

#define AMDVI_DEV_INT_PASS_MASK         (1ULL << 56)
#define AMDVI_DEV_EINT_PASS_MASK        (1ULL << 57)
#define AMDVI_DEV_NMI_PASS_MASK         (1ULL << 58)
#define AMDVI_DEV_LINT0_PASS_MASK       (1ULL << 62)
#define AMDVI_DEV_LINT1_PASS_MASK       (1ULL << 63)

union irte {
    uint32_t val;
    struct {
        uint32_t valid:1,
                 no_fault:1,
                 int_type:3,
                 rq_eoi:1,
                 dm:1,
                 guest_mode:1,
                 destination:8,
                 vector:8,
                 rsvd:8;
    } fields;
};

union irte_ga_lo {
  uint64_t val;

  struct {
      uint64_t  valid:1,
                no_fault:1,
                int_type:3,
                rq_eoi:1,
                dm:1,
                guest_mode:1,
                destination:24,
                rsvd_1:32;
  } fields_remap;
};

union irte_ga_hi {
  uint64_t val;
  struct {
      uint64_t  vector:8,
                rsvd_2:48,
                destination_hi:8;
  } fields;
};

struct irte_ga {
  union irte_ga_lo lo;
  union irte_ga_hi hi;
};

#define TYPE_AMD_IOMMU_DEVICE "amd-iommu"
OBJECT_DECLARE_SIMPLE_TYPE(AMDVIState, AMD_IOMMU_DEVICE)

#define TYPE_AMD_IOMMU_PCI "AMDVI-PCI"
OBJECT_DECLARE_SIMPLE_TYPE(AMDVIPCIState, AMD_IOMMU_PCI)

#define TYPE_AMD_IOMMU_MEMORY_REGION "amd-iommu-iommu-memory-region"

typedef struct AMDVIAddressSpace AMDVIAddressSpace;

struct AMDVIPCIState {
    PCIDevice dev;
    uint32_t capab_offset;
};

struct AMDVIState {
    X86IOMMUState iommu;
    AMDVIPCIState *pci;
    char *pci_id;

    uint32_t version;

    uint64_t mmio_addr;

    bool enabled;
    bool cmdbuf_enabled;
    bool evtlog_enabled;
    bool excl_enabled;

    hwaddr devtab;
    uint64_t devtab_len;

    hwaddr cmdbuf;
    uint64_t cmdbuf_len;
    uint32_t cmdbuf_head;
    uint32_t cmdbuf_tail;
    bool completion_wait_intr;

    hwaddr evtlog;
    bool evtlog_intr;
    uint32_t evtlog_len;
    uint32_t evtlog_head;
    uint32_t evtlog_tail;

    hwaddr excl_base;
    hwaddr excl_limit;
    bool excl_allow;
    bool excl_enable;

    hwaddr ppr_log;
    uint32_t pprlog_len;
    uint32_t pprlog_head;
    uint32_t pprlog_tail;

    MemoryRegion mr_mmio;
    MemoryRegion mr_sys;
    MemoryRegion mr_nodma;
    MemoryRegion mr_ir;
    uint8_t mmior[AMDVI_MMIO_SIZE];
    uint8_t w1cmask[AMDVI_MMIO_SIZE];
    uint8_t romask[AMDVI_MMIO_SIZE];
    bool mmio_enabled;

    GHashTable *address_spaces;

    QLIST_HEAD(, AMDVIAddressSpace) amdvi_as_with_notifiers;

    GHashTable *iotlb;

    bool ga_enabled;
    bool xtsup;

    bool dma_remap;
};

uint64_t amdvi_extended_feature_register(AMDVIState *s);

#endif
