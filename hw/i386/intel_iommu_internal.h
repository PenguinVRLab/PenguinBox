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
 *
 * Lots of defines copied from kernel/include/linux/intel-iommu.h:
 *   Copyright (C) 2006-2008 Intel Corporation
 *   Author: Ashok Raj <ashok.raj@intel.com>
 *   Author: Anil S Keshavamurthy <anil.s.keshavamurthy@intel.com>
 *
 */

#ifndef HW_I386_INTEL_IOMMU_INTERNAL_H
#define HW_I386_INTEL_IOMMU_INTERNAL_H
#include "hw/i386/intel_iommu.h"

#define DMAR_VER_REG            0x0
#define DMAR_CAP_REG            0x8
#define DMAR_CAP_REG_HI         0xc
#define DMAR_ECAP_REG           0x10
#define DMAR_ECAP_REG_HI        0X14
#define DMAR_GCMD_REG           0x18
#define DMAR_GSTS_REG           0x1c
#define DMAR_RTADDR_REG         0x20
#define DMAR_RTADDR_REG_HI      0X24
#define DMAR_CCMD_REG           0x28
#define DMAR_CCMD_REG_HI        0x2c
#define DMAR_FSTS_REG           0x34
#define DMAR_FECTL_REG          0x38
#define DMAR_FEDATA_REG         0x3c
#define DMAR_FEADDR_REG         0x40
#define DMAR_FEUADDR_REG        0x44
#define DMAR_AFLOG_REG          0x58
#define DMAR_AFLOG_REG_HI       0X5c
#define DMAR_PMEN_REG           0x64
#define DMAR_PLMBASE_REG        0x68
#define DMAR_PLMLIMIT_REG       0x6c
#define DMAR_PHMBASE_REG        0x70
#define DMAR_PHMBASE_REG_HI     0X74
#define DMAR_PHMLIMIT_REG       0x78
#define DMAR_PHMLIMIT_REG_HI    0x7c
#define DMAR_IQH_REG            0x80
#define DMAR_IQH_REG_HI         0X84
#define DMAR_IQT_REG            0x88
#define DMAR_IQT_REG_HI         0X8c
#define DMAR_IQA_REG            0x90
#define DMAR_IQA_REG_HI         0x94
#define DMAR_ICS_REG            0x9c
#define DMAR_IRTA_REG           0xb8
#define DMAR_IRTA_REG_HI        0xbc
#define DMAR_IECTL_REG          0xa0
#define DMAR_IEDATA_REG         0xa4
#define DMAR_IEADDR_REG         0xa8
#define DMAR_IEUADDR_REG        0xac
#define DMAR_PQH_REG            0xc0
#define DMAR_PQH_REG_HI         0xc4
#define DMAR_PQT_REG            0xc8
#define DMAR_PQT_REG_HI         0xcc
#define DMAR_PQA_REG            0xd0
#define DMAR_PQA_REG_HI         0xd4
#define DMAR_PRS_REG            0xdc
#define DMAR_PECTL_REG          0xe0
#define DMAR_PEDATA_REG         0xe4
#define DMAR_PEADDR_REG         0xe8
#define DMAR_PEUADDR_REG        0xec
#define DMAR_MTRRCAP_REG        0x100
#define DMAR_MTRRCAP_REG_HI     0x104
#define DMAR_MTRRDEF_REG        0x108
#define DMAR_MTRRDEF_REG_HI     0x10c

#define DMAR_IOTLB_REG_OFFSET   0xf0
#define DMAR_IVA_REG            DMAR_IOTLB_REG_OFFSET
#define DMAR_IVA_REG_HI         (DMAR_IVA_REG + 4)
#define DMAR_IOTLB_REG          (DMAR_IOTLB_REG_OFFSET + 0x8)
#define DMAR_IOTLB_REG_HI       (DMAR_IOTLB_REG + 4)

#define DMAR_FRCD_REG_OFFSET    0x220
#define DMAR_FRCD_REG_NR        1ULL

#define DMAR_FRCD_REG_0_0       0x220
#define DMAR_FRCD_REG_0_1       0x224
#define DMAR_FRCD_REG_0_2       0x228
#define DMAR_FRCD_REG_0_3       0x22c

#define VTD_INTERRUPT_ADDR_FIRST    0xfee00000ULL
#define VTD_INTERRUPT_ADDR_LAST     0xfeefffffULL
#define VTD_INTERRUPT_ADDR_SIZE     (VTD_INTERRUPT_ADDR_LAST - \
                                     VTD_INTERRUPT_ADDR_FIRST + 1)

#define VTD_IOTLB_SID_SHIFT         26
#define VTD_IOTLB_LVL_SHIFT         42
#define VTD_IOTLB_PASID_SHIFT       44
#define VTD_IOTLB_MAX_SIZE          1024

#define VTD_TLB_GLOBAL_FLUSH        (1ULL << 60)
#define VTD_TLB_DSI_FLUSH           (2ULL << 60)
#define VTD_TLB_PSI_FLUSH           (3ULL << 60)
#define VTD_TLB_FLUSH_GRANU_MASK    (3ULL << 60)
#define VTD_TLB_GLOBAL_FLUSH_A      (1ULL << 57)
#define VTD_TLB_DSI_FLUSH_A         (2ULL << 57)
#define VTD_TLB_PSI_FLUSH_A         (3ULL << 57)
#define VTD_TLB_FLUSH_GRANU_MASK_A  (3ULL << 57)
#define VTD_TLB_IVT                 (1ULL << 63)
#define VTD_TLB_DID(val)            (((val) >> 32) & VTD_DOMAIN_ID_MASK)

#define VTD_IVA_ADDR(val)       ((val) & ~0xfffULL)
#define VTD_IVA_AM(val)         ((val) & 0x3fULL)

#define VTD_GCMD_TE                 (1UL << 31)
#define VTD_GCMD_SRTP               (1UL << 30)
#define VTD_GCMD_SFL                (1UL << 29)
#define VTD_GCMD_EAFL               (1UL << 28)
#define VTD_GCMD_WBF                (1UL << 27)
#define VTD_GCMD_QIE                (1UL << 26)
#define VTD_GCMD_IRE                (1UL << 25)
#define VTD_GCMD_SIRTP              (1UL << 24)
#define VTD_GCMD_CFI                (1UL << 23)

#define VTD_GSTS_TES                (1UL << 31)
#define VTD_GSTS_RTPS               (1UL << 30)
#define VTD_GSTS_FLS                (1UL << 29)
#define VTD_GSTS_AFLS               (1UL << 28)
#define VTD_GSTS_WBFS               (1UL << 27)
#define VTD_GSTS_QIES               (1UL << 26)
#define VTD_GSTS_IRES               (1UL << 25)
#define VTD_GSTS_IRTPS              (1UL << 24)
#define VTD_GSTS_CFIS               (1UL << 23)

#define VTD_CCMD_ICC                (1ULL << 63)
#define VTD_CCMD_GLOBAL_INVL        (1ULL << 61)
#define VTD_CCMD_DOMAIN_INVL        (2ULL << 61)
#define VTD_CCMD_DEVICE_INVL        (3ULL << 61)
#define VTD_CCMD_CIRG_MASK          (3ULL << 61)
#define VTD_CCMD_GLOBAL_INVL_A      (1ULL << 59)
#define VTD_CCMD_DOMAIN_INVL_A      (2ULL << 59)
#define VTD_CCMD_DEVICE_INVL_A      (3ULL << 59)
#define VTD_CCMD_CAIG_MASK          (3ULL << 59)
#define VTD_CCMD_DID(val)           ((val) & VTD_DOMAIN_ID_MASK)
#define VTD_CCMD_SID(val)           (((val) >> 16) & 0xffffULL)
#define VTD_CCMD_FM(val)            (((val) >> 32) & 3ULL)

#define VTD_RTADDR_SMT              (1ULL << 10)
#define VTD_RTADDR_ADDR_MASK(aw)    (VTD_HAW_MASK(aw) ^ 0xfffULL)

#define VTD_IRTA_ADDR_MASK(aw)      (VTD_HAW_MASK(aw) ^ 0xfffULL)
#define VTD_IRTA_EIME               (1ULL << 11)
#define VTD_IRTA_SIZE_MASK          (0xfULL)

#define VTD_ECAP_IRO                (DMAR_IOTLB_REG_OFFSET << 4)
#define VTD_ECAP_QI                 (1ULL << 1)
#define VTD_ECAP_DT                 (1ULL << 2)
#define VTD_ECAP_IR                 (1ULL << 3)
#define VTD_ECAP_EIM                (1ULL << 4)
#define VTD_ECAP_PT                 (1ULL << 6)
#define VTD_ECAP_SC                 (1ULL << 7)
#define VTD_ECAP_PRS                (1ULL << 29)
#define VTD_ECAP_MHMV               (15ULL << 20)
#define VTD_ECAP_SRS                (1ULL << 31)
#define VTD_ECAP_PSS                (7ULL << 35)
#define VTD_ECAP_PASID              (1ULL << 40)
#define VTD_ECAP_SMTS               (1ULL << 43)
#define VTD_ECAP_SLTS               (1ULL << 46)
#define VTD_ECAP_FLTS               (1ULL << 47)

#define VTD_CAP_FRO                 (DMAR_FRCD_REG_OFFSET << 20)
#define VTD_CAP_NFR                 ((DMAR_FRCD_REG_NR - 1) << 40)
#define VTD_DOMAIN_ID_SHIFT         16
#define VTD_DOMAIN_ID_MASK          ((1UL << VTD_DOMAIN_ID_SHIFT) - 1)
#define VTD_CAP_ND                  (((VTD_DOMAIN_ID_SHIFT - 4) / 2) & 7ULL)
#define VTD_ADDRESS_SIZE(aw)        (1ULL << (aw))
#define VTD_CAP_MGAW(aw)            ((((aw) - 1) & 0x3fULL) << 16)
#define VTD_MAMV                    18ULL
#define VTD_CAP_MAMV                (VTD_MAMV << 48)
#define VTD_CAP_PSI                 (1ULL << 39)
#define VTD_CAP_SLLPS               ((1ULL << 34) | (1ULL << 35))
#define VTD_CAP_DRAIN_WRITE         (1ULL << 54)
#define VTD_CAP_DRAIN_READ          (1ULL << 55)
#define VTD_CAP_FS1GP               (1ULL << 56)
#define VTD_CAP_ESRTPS              (1ULL << 63)
#define VTD_CAP_DRAIN               (VTD_CAP_DRAIN_READ | VTD_CAP_DRAIN_WRITE)
#define VTD_CAP_CM                  (1ULL << 7)
#define VTD_PASID_ID_SHIFT          20
#define VTD_PASID_ID_MASK           ((1ULL << VTD_PASID_ID_SHIFT) - 1)

#define VTD_CAP_SAGAW_SHIFT         8
#define VTD_CAP_SAGAW_MASK          (0x1fULL << VTD_CAP_SAGAW_SHIFT)
#define VTD_CAP_SAGAW_39bit         (0x2ULL << VTD_CAP_SAGAW_SHIFT)
#define VTD_CAP_SAGAW_48bit         (0x4ULL << VTD_CAP_SAGAW_SHIFT)

#define VTD_IQT_QT(dw_bit, val)     (dw_bit ? (((val) >> 5) & 0x3fffULL) : \
                                     (((val) >> 4) & 0x7fffULL))
#define VTD_IQT_QT_256_RSV_BIT      0x10

#define VTD_IQA_IQA_MASK(aw)        (VTD_HAW_MASK(aw) ^ 0xfffULL)
#define VTD_IQA_QS                  0x7ULL
#define VTD_IQA_DW_MASK             0x800

#define VTD_IQH_QH_SHIFT_4          4
#define VTD_IQH_QH_SHIFT_5          5
#define VTD_IQH_QH_MASK             0x7fff0ULL

#define VTD_ICS_IWC                 1UL

#define VTD_IECTL_IM                (1UL << 31)
#define VTD_IECTL_IP                (1UL << 30)

#define VTD_FSTS_FRI_MASK       0xff00UL
#define VTD_FSTS_FRI(val)       ((((uint32_t)(val)) << 8) & VTD_FSTS_FRI_MASK)
#define VTD_FSTS_IQE            (1UL << 4)
#define VTD_FSTS_PPF            (1UL << 1)
#define VTD_FSTS_PFO            1UL

#define VTD_FECTL_IM            (1UL << 31)
#define VTD_FECTL_IP            (1UL << 30)

#define VTD_FRCD_F              (1ULL << 63)
#define VTD_FRCD_T              (1ULL << 62)
#define VTD_FRCD_FR(val)        (((val) & 0xffULL) << 32)
#define VTD_FRCD_SID_MASK       0xffffULL
#define VTD_FRCD_SID(val)       ((val) & VTD_FRCD_SID_MASK)
#define VTD_FRCD_PV(val)        (((val) & 0xffffULL) << 40)
#define VTD_FRCD_PP(val)        (((val) & 0x1ULL) << 31)
#define VTD_FRCD_FI(val)        ((val) & ~0xfffULL)
#define VTD_FRCD_IR_IDX(val)    (((val) & 0xffffULL) << 48)

typedef enum VTDFaultReason {
    VTD_FR_RESERVED = 0,
    VTD_FR_ROOT_ENTRY_P = 1,
    VTD_FR_CONTEXT_ENTRY_P,
    VTD_FR_CONTEXT_ENTRY_INV,
    VTD_FR_ADDR_BEYOND_MGAW,
    VTD_FR_WRITE,
    VTD_FR_READ,
    VTD_FR_PAGING_ENTRY_INV,
    VTD_FR_ROOT_TABLE_INV,
    VTD_FR_CONTEXT_TABLE_INV,
    VTD_FR_ROOT_ENTRY_RSVD,
    VTD_FR_CONTEXT_ENTRY_RSVD,
    VTD_FR_PAGING_ENTRY_RSVD,
    VTD_FR_CONTEXT_ENTRY_TT,
    VTD_FR_INTERRUPT_ADDR = 0xE,

    VTD_FR_IR_REQ_RSVD = 0x20,
    VTD_FR_IR_INDEX_OVER = 0x21,
    VTD_FR_IR_ENTRY_P = 0x22,
    VTD_FR_IR_ROOT_INVAL = 0x23,
    VTD_FR_IR_IRTE_RSVD = 0x24,
    VTD_FR_IR_REQ_COMPAT = 0x25,
    VTD_FR_IR_SID_ERR = 0x26,

    VTD_FR_RTADDR_INV_TTM = 0x31,

    VTD_FR_SM_PRE_ABS = 0x47,

    VTD_FR_PASID_DIR_ACCESS_ERR = 0x50,
    VTD_FR_PASID_DIR_ENTRY_P = 0x51,
    VTD_FR_PASID_TABLE_ACCESS_ERR = 0x58,
    VTD_FR_PASID_ENTRY_P = 0x59,
    VTD_FR_PASID_TABLE_ENTRY_INV = 0x5b,

    VTD_FR_FS_PAGING_ENTRY_INV = 0x70,
    VTD_FR_FS_PAGING_ENTRY_P = 0x71,
    VTD_FR_FS_PAGING_ENTRY_RSVD = 0x72,
    VTD_FR_PASID_ENTRY_FSPTPTR_INV = 0x73,
    VTD_FR_FS_NON_CANONICAL = 0x80,
    VTD_FR_FS_PAGING_ENTRY_US = 0x81,
    VTD_FR_SM_WRITE = 0x85,

    VTD_FR_SM_INTERRUPT_ADDR = 0x87,
    VTD_FR_FS_BIT_UPDATE_FAILED = 0x91,
    VTD_FR_MAX,
} VTDFaultReason;

#define VTD_CONTEXT_CACHE_GEN_MAX       0xffffffffUL

struct VTDInvDescIEC {
#if HOST_BIG_ENDIAN
    uint64_t reserved_2:16;
    uint64_t index:16;
    uint64_t index_mask:5;
    uint64_t resved_1:22;
    uint64_t granularity:1;
    uint64_t type:4;
#else
    uint32_t type:4;
    uint32_t granularity:1;
    uint32_t resved_1:22;
    uint32_t index_mask:5;
    uint32_t index:16;
    uint32_t reserved_2:16;
#endif
};
typedef struct VTDInvDescIEC VTDInvDescIEC;

union VTDInvDesc {
    struct {
        uint64_t lo;
        uint64_t hi;
    };
    struct {
        uint64_t val[4];
    };
    union {
        VTDInvDescIEC iec;
    };
};
typedef union VTDInvDesc VTDInvDesc;

union VTDPRDesc {
    struct {
        uint64_t lo;
        uint64_t hi;
    };
    struct {
        uint64_t val[4];
    };
};
typedef union VTDPRDesc VTDPRDesc;

#define VTD_INV_DESC_ALL_ONE            -1ULL
#define VTD_INV_DESC_TYPE(val)          ((((val) >> 5) & 0x70ULL) | \
                                         ((val) & 0xfULL))
#define VTD_INV_DESC_CC                 0x1
#define VTD_INV_DESC_IOTLB              0x2
#define VTD_INV_DESC_DEVICE             0x3
#define VTD_INV_DESC_IEC                0x4
#define VTD_INV_DESC_WAIT               0x5
#define VTD_INV_DESC_PIOTLB             0x6
#define VTD_INV_DESC_PC                 0x7
#define VTD_INV_DESC_DEV_PIOTLB         0x8
#define VTD_INV_DESC_PGRESP             0x9
#define VTD_INV_DESC_NONE               0

#define VTD_INV_DESC_WAIT_SW            (1ULL << 5)
#define VTD_INV_DESC_WAIT_IF            (1ULL << 4)
#define VTD_INV_DESC_WAIT_FN            (1ULL << 6)
#define VTD_INV_DESC_WAIT_DATA_SHIFT    32
#define VTD_INV_DESC_WAIT_RSVD_LO       0Xfffff180ULL
#define VTD_INV_DESC_WAIT_RSVD_HI       3ULL

#define VTD_INV_DESC_CC_G               (3ULL << 4)
#define VTD_INV_DESC_CC_GLOBAL          (1ULL << 4)
#define VTD_INV_DESC_CC_DOMAIN          (2ULL << 4)
#define VTD_INV_DESC_CC_DEVICE          (3ULL << 4)
#define VTD_INV_DESC_CC_DID(val)        (((val) >> 16) & VTD_DOMAIN_ID_MASK)
#define VTD_INV_DESC_CC_SID(val)        (((val) >> 32) & 0xffffUL)
#define VTD_INV_DESC_CC_FM(val)         (((val) >> 48) & 3UL)
#define VTD_INV_DESC_CC_RSVD            0xfffc00000000f1c0ULL

#define VTD_INV_DESC_IOTLB_G            (3ULL << 4)
#define VTD_INV_DESC_IOTLB_GLOBAL       (1ULL << 4)
#define VTD_INV_DESC_IOTLB_DOMAIN       (2ULL << 4)
#define VTD_INV_DESC_IOTLB_PAGE         (3ULL << 4)
#define VTD_INV_DESC_IOTLB_DID(val)     (((val) >> 16) & VTD_DOMAIN_ID_MASK)
#define VTD_INV_DESC_IOTLB_ADDR(val)    ((val) & ~0xfffULL)
#define VTD_INV_DESC_IOTLB_AM(val)      ((val) & 0x3fULL)
#define VTD_INV_DESC_IOTLB_RSVD_LO      0xffffffff0000f100ULL
#define VTD_INV_DESC_IOTLB_RSVD_HI      0xf80ULL

#define VTD_INV_DESC_DEVICE_IOTLB_ADDR(val) ((val) & 0xfffffffffffff000ULL)
#define VTD_INV_DESC_DEVICE_IOTLB_SIZE(val) ((val) & 0x1)
#define VTD_INV_DESC_DEVICE_IOTLB_SID(val) (((val) >> 32) & 0xFFFFULL)
#define VTD_INV_DESC_DEVICE_IOTLB_RSVD_HI 0xffeULL
#define VTD_INV_DESC_DEVICE_IOTLB_RSVD_LO 0xffff0000ffe0f1f0

#define VTD_INV_DESC_IEC_RSVD           0xffff000007fff1e0ULL

#define VTD_INV_DESC_PASID_DEVICE_IOTLB_ADDR(val) ((val) & \
                                                   0xfffffffffffff000ULL)
#define VTD_INV_DESC_PASID_DEVICE_IOTLB_SIZE(val) ((val >> 11) & 0x1)
#define VTD_INV_DESC_PASID_DEVICE_IOTLB_GLOBAL(val) ((val) & 0x1)
#define VTD_INV_DESC_PASID_DEVICE_IOTLB_SID(val) (((val) >> 16) & 0xffffULL)
#define VTD_INV_DESC_PASID_DEVICE_IOTLB_PASID(val) ((val >> 32) & 0xfffffULL)
#define VTD_INV_DESC_PASID_DEVICE_IOTLB_RSVD_VAL0 0xfff000000000f000ULL
#define VTD_INV_DESC_PASID_DEVICE_IOTLB_RSVD_VAL1 0x7feULL

#define VTD_INV_DESC_PGRESP_RSVD_HI             0xfffffffffffff003ULL
#define VTD_INV_DESC_PGRESP_RSVD_LO             0xfff00000000001e0ULL
#define VTD_INV_DESC_PGRESP_PP(val)             (((val) >> 4) & 0x1ULL)
#define VTD_INV_DESC_PGRESP_RC(val)             (((val) >> 12) & 0xfULL)
#define VTD_INV_DESC_PGRESP_RID(val)            (((val) >> 16) & 0xffffULL)
#define VTD_INV_DESC_PGRESP_PASID(val)          (((val) >> 32) & 0xfffffULL)
#define VTD_INV_DESC_PGRESP_PRGI(val)           (((val) >> 3) & 0x1ffULL)

#define VTD_SPTE_SNP 0x800ULL

#define VTD_SPTE_PAGE_L1_RSVD_MASK(aw, stale_tm) \
        stale_tm ? \
        (0x800ULL | ~(VTD_HAW_MASK(aw) | VTD_SL_IGN_COM | VTD_SL_TM)) : \
        (0x800ULL | ~(VTD_HAW_MASK(aw) | VTD_SL_IGN_COM))
#define VTD_SPTE_PAGE_L2_RSVD_MASK(aw) \
        (0x800ULL | ~(VTD_HAW_MASK(aw) | VTD_SL_IGN_COM))
#define VTD_SPTE_PAGE_L3_RSVD_MASK(aw) \
        (0x800ULL | ~(VTD_HAW_MASK(aw) | VTD_SL_IGN_COM))
#define VTD_SPTE_PAGE_L4_RSVD_MASK(aw) \
        (0x880ULL | ~(VTD_HAW_MASK(aw) | VTD_SL_IGN_COM))

#define VTD_SPTE_LPAGE_L2_RSVD_MASK(aw, stale_tm) \
        stale_tm ? \
        (0x1ff800ULL | ~(VTD_HAW_MASK(aw) | VTD_SL_IGN_COM | VTD_SL_TM)) : \
        (0x1ff800ULL | ~(VTD_HAW_MASK(aw) | VTD_SL_IGN_COM))
#define VTD_SPTE_LPAGE_L3_RSVD_MASK(aw, stale_tm) \
        stale_tm ? \
        (0x3ffff800ULL | ~(VTD_HAW_MASK(aw) | VTD_SL_IGN_COM | VTD_SL_TM)) : \
        (0x3ffff800ULL | ~(VTD_HAW_MASK(aw) | VTD_SL_IGN_COM))

#define VTD_FS_UPPER_IGNORED 0xfff0000000000000ULL
#define VTD_FPTE_PAGE_L1_RSVD_MASK(aw) \
        (~(VTD_HAW_MASK(aw) | VTD_FS_UPPER_IGNORED))
#define VTD_FPTE_PAGE_L2_RSVD_MASK(aw) \
        (~(VTD_HAW_MASK(aw) | VTD_FS_UPPER_IGNORED))
#define VTD_FPTE_PAGE_L3_RSVD_MASK(aw) \
        (~(VTD_HAW_MASK(aw) | VTD_FS_UPPER_IGNORED))
#define VTD_FPTE_PAGE_L4_RSVD_MASK(aw) \
        (0x80ULL | ~(VTD_HAW_MASK(aw) | VTD_FS_UPPER_IGNORED))

#define VTD_FPTE_LPAGE_L2_RSVD_MASK(aw) \
        (0x1fe000ULL | ~(VTD_HAW_MASK(aw) | VTD_FS_UPPER_IGNORED))
#define VTD_FPTE_LPAGE_L3_RSVD_MASK(aw) \
        (0x3fffe000ULL | ~(VTD_HAW_MASK(aw) | VTD_FS_UPPER_IGNORED))

#define VTD_INV_DESC_PIOTLB_G             (3ULL << 4)
#define VTD_INV_DESC_PIOTLB_ALL_IN_PASID  (2ULL << 4)
#define VTD_INV_DESC_PIOTLB_PSI_IN_PASID  (3ULL << 4)
#define VTD_INV_DESC_PIOTLB_DID(val)      (((val) >> 16) & VTD_DOMAIN_ID_MASK)
#define VTD_INV_DESC_PIOTLB_PASID(val)    (((val) >> 32) & 0xfffffULL)
#define VTD_INV_DESC_PIOTLB_AM(val)       ((val) & 0x3fULL)
#define VTD_INV_DESC_PIOTLB_IH(val)       (((val) >> 6) & 0x1)
#define VTD_INV_DESC_PIOTLB_ADDR(val)     ((val) & ~0xfffULL)
#define VTD_INV_DESC_PIOTLB_RSVD_VAL0     0xfff000000000f1c0ULL
#define VTD_INV_DESC_PIOTLB_RSVD_VAL1     0xf80ULL

#define VTD_INV_DESC_PASIDC_G(x)        extract64((x)->val[0], 4, 2)
#define VTD_INV_DESC_PASIDC_G_DSI       0
#define VTD_INV_DESC_PASIDC_G_PASID_SI  1
#define VTD_INV_DESC_PASIDC_G_GLOBAL    3
#define VTD_INV_DESC_PASIDC_DID(x)      extract64((x)->val[0], 16, 16)
#define VTD_INV_DESC_PASIDC_PASID(x)    extract64((x)->val[0], 32, 20)
#define VTD_INV_DESC_PASIDC_RSVD_VAL0   0xfff000000000f1c0ULL

#define VTD_PRD_TYPE            (1ULL)
#define VTD_PRD_PP(val)         (((val) & 1ULL) << 8)
#define VTD_PRD_RID(val)        (((val) & 0xffffULL) << 16)
#define VTD_PRD_PASID(val)      (((val) & 0xfffffULL) << 32)
#define VTD_PRD_EXR(val)        (((val) & 1ULL) << 52)
#define VTD_PRD_PMR(val)        (((val) & 1ULL) << 53)
#define VTD_PRD_RDR(val)        ((val) & 1ULL)
#define VTD_PRD_WRR(val)        (((val) & 1ULL) << 1)
#define VTD_PRD_LPIG(val)       (((val) & 1ULL) << 2)
#define VTD_PRD_PRGI(val)       (((val) & 0x1ffULL) << 3)
#define VTD_PRD_ADDR(val)       ((val) & 0xfffffffffffff000ULL)

#define VTD_PQA_ENTRY_SIZE      32
#define VTD_PQA_ADDR            0xfffffffffffff000ULL
#define VTD_PQA_SIZE            0x7ULL
#define VTD_PR_STATUS_PPR       1UL
#define VTD_PR_STATUS_PRO       2UL
#define VTD_PR_PECTL_IP         0x40000000UL
#define VTD_PR_PECTL_IM         0x80000000UL

struct VTDIOTLBPageInvInfo {
    uint16_t domain_id;
    uint32_t pasid;
    uint64_t addr;
    uint64_t mask;
};
typedef struct VTDIOTLBPageInvInfo VTDIOTLBPageInvInfo;

#define VTD_PAGE_SHIFT              12
#define VTD_PAGE_SIZE               (1ULL << VTD_PAGE_SHIFT)

#define VTD_PAGE_SHIFT_4K           12
#define VTD_PAGE_MASK_4K            (~((1ULL << VTD_PAGE_SHIFT_4K) - 1))
#define VTD_PAGE_SHIFT_2M           21
#define VTD_PAGE_MASK_2M            (~((1ULL << VTD_PAGE_SHIFT_2M) - 1))
#define VTD_PAGE_SHIFT_1G           30
#define VTD_PAGE_MASK_1G            (~((1ULL << VTD_PAGE_SHIFT_1G) - 1))

struct VTDRootEntry {
    uint64_t lo;
    uint64_t hi;
};
typedef struct VTDRootEntry VTDRootEntry;

#define VTD_ROOT_ENTRY_P            1ULL
#define VTD_ROOT_ENTRY_CTP          (~0xfffULL)

#define VTD_ROOT_ENTRY_NR           (VTD_PAGE_SIZE / sizeof(VTDRootEntry))
#define VTD_ROOT_ENTRY_RSVD(aw)     (0xffeULL | ~VTD_HAW_MASK(aw))

#define VTD_DEVFN_CHECK_MASK        0x80

#define VTD_CONTEXT_ENTRY_P         (1ULL << 0)
#define VTD_CONTEXT_ENTRY_FPD       (1ULL << 1)
#define VTD_CONTEXT_ENTRY_TT        (3ULL << 2)
#define VTD_CONTEXT_TT_MULTI_LEVEL  0
#define VTD_CONTEXT_TT_DEV_IOTLB    (1ULL << 2)
#define VTD_CONTEXT_TT_PASS_THROUGH (2ULL << 2)
#define VTD_CONTEXT_ENTRY_SLPTPTR   (~0xfffULL)
#define VTD_CONTEXT_ENTRY_RSVD_LO(aw) (0xff0ULL | ~VTD_HAW_MASK(aw))
#define VTD_CONTEXT_ENTRY_AW        7ULL
#define VTD_CONTEXT_ENTRY_DID(val)  (((val) >> 8) & VTD_DOMAIN_ID_MASK)
#define VTD_CONTEXT_ENTRY_RSVD_HI   0xffffffffff000080ULL

#define VTD_CONTEXT_ENTRY_NR        (VTD_PAGE_SIZE / sizeof(VTDContextEntry))

#define VTD_CTX_ENTRY_LEGACY_SIZE     16
#define VTD_CTX_ENTRY_SCALABLE_SIZE   32

#define VTD_SM_CONTEXT_ENTRY_RID2PASID_MASK 0xfffff
#define VTD_SM_CONTEXT_ENTRY_RSVD_VAL0(aw)  (0x1e0ULL | ~VTD_HAW_MASK(aw))
#define VTD_SM_CONTEXT_ENTRY_RSVD_VAL1      0xffffffffffe00000ULL
#define VTD_SM_CONTEXT_ENTRY_PRE            0x10ULL

typedef struct VTDPASIDCacheInfo {
    uint8_t type;
    uint16_t did;
    uint32_t pasid;
} VTDPASIDCacheInfo;

#define VTD_PASID_DIR_BASE_ADDR_MASK  (~0xfffULL)
#define VTD_PASID_TABLE_BASE_ADDR_MASK (~0xfffULL)
#define VTD_PASID_DIR_ENTRY_SIZE      8
#define VTD_PASID_ENTRY_SIZE          64
#define VTD_PASID_DIR_BITS_MASK       (0x3fffULL)
#define VTD_PASID_DIR_INDEX(pasid)    (((pasid) >> 6) & VTD_PASID_DIR_BITS_MASK)
#define VTD_PASID_DIR_FPD             (1ULL << 1)
#define VTD_PASID_TABLE_BITS_MASK     (0x3fULL)
#define VTD_PASID_TABLE_INDEX(pasid)  ((pasid) & VTD_PASID_TABLE_BITS_MASK)
#define VTD_PASID_ENTRY_FPD           (1ULL << 1)

#define VTD_PASID_ENTRY_P              1ULL
#define VTD_SM_PASID_ENTRY_PGTT        (7ULL << 6)
#define VTD_SM_PASID_ENTRY_FLT         (1ULL << 6)
#define VTD_SM_PASID_ENTRY_SLT         (2ULL << 6)
#define VTD_SM_PASID_ENTRY_NESTED      (3ULL << 6)
#define VTD_SM_PASID_ENTRY_PT          (4ULL << 6)

#define VTD_SM_PASID_ENTRY_AW          7ULL
#define VTD_SM_PASID_ENTRY_DID(val)    ((val) & VTD_DOMAIN_ID_MASK)

#define VTD_SM_PASID_ENTRY_FLPM          3ULL
#define VTD_SM_PASID_ENTRY_FLPTPTR       (~0xfffULL)

#define VTD_FL_P                    1ULL
#define VTD_FL_RW                   (1ULL << 1)
#define VTD_FL_US                   (1ULL << 2)
#define VTD_FL_A                    (1ULL << 5)
#define VTD_FL_D                    (1ULL << 6)

#define VTD_SM_PASID_ENTRY_SLPTPTR     (~0xfffULL)

#define VTD_SL_RW_MASK              3ULL
#define VTD_SL_R                    1ULL
#define VTD_SL_W                    (1ULL << 1)
#define VTD_SL_IGN_COM              0xbff0000000000000ULL
#define VTD_SL_TM                   (1ULL << 62)

#define VTD_PML4_LEVEL           4
#define VTD_PDP_LEVEL            3
#define VTD_PD_LEVEL             2
#define VTD_PT_LEVEL             1
#define VTD_PT_ENTRY_NR          512
#define VTD_PT_PAGE_SIZE_MASK    (1ULL << 7)
#define VTD_PT_BASE_ADDR_MASK(aw) (~(VTD_PAGE_SIZE - 1) & VTD_HAW_MASK(aw))

#define VTD_LEVEL_BITS           9

#endif
