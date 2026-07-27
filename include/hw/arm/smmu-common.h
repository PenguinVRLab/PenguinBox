/*
 * ARM SMMU Support
 *
 * Copyright (C) 2015-2016 Broadcom Corporation
 * Copyright (c) 2017 Red Hat, Inc.
 * Written by Prem Mallappa, Eric Auger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef HW_ARM_SMMU_COMMON_H
#define HW_ARM_SMMU_COMMON_H

#include "hw/sysbus.h"
#include "hw/pci/pci.h"
#include "qom/object.h"

#define SMMU_PCI_BUS_MAX                    256
#define SMMU_PCI_DEVFN_MAX                  256
#define SMMU_PCI_DEVFN(sid)                 (sid & 0xFF)

#define VMSA_LEVELS                         4
#define VMSA_MAX_S2_CONCAT                  16

#define VMSA_STRIDE(gran)                   ((gran) - VMSA_LEVELS + 1)
#define VMSA_BIT_LVL(isz, strd, lvl)        ((isz) - (strd) * \
                                             (VMSA_LEVELS - (lvl)))
#define VMSA_IDXMSK(isz, strd, lvl)         ((1ULL << \
                                             VMSA_BIT_LVL(isz, strd, lvl)) - 1)

#define CACHED_ENTRY_TO_ADDR(ent, addr)      ((ent)->entry.translated_addr + \
                                             ((addr) & (ent)->entry.addr_mask))

typedef enum {
    SMMU_PTW_ERR_NONE,
    SMMU_PTW_ERR_WALK_EABT,
    SMMU_PTW_ERR_TRANSLATION,
    SMMU_PTW_ERR_ADDR_SIZE,
    SMMU_PTW_ERR_ACCESS,
    SMMU_PTW_ERR_PERMISSION,
} SMMUPTWEventType;

typedef enum {
    SMMU_STAGE_1 = 1,
    SMMU_STAGE_2,
    SMMU_NESTED,
} SMMUStage;

typedef struct SMMUPTWEventInfo {
    SMMUStage stage;
    SMMUPTWEventType type;
    dma_addr_t addr;
    bool is_ipa_descriptor;
} SMMUPTWEventInfo;

typedef struct SMMUTransTableInfo {
    bool disabled;
    uint64_t ttb;
    uint8_t tsz;
    uint8_t granule_sz;
    bool had;
} SMMUTransTableInfo;

typedef struct SMMUTLBEntry {
    IOMMUTLBEntry entry;
    uint8_t level;
    uint8_t granule;
    IOMMUAccessFlags parent_perm;
} SMMUTLBEntry;

typedef struct SMMUS2Cfg {
    uint8_t tsz;
    uint8_t sl0;
    bool affd;
    bool record_faults;
    uint8_t granule_sz;
    uint8_t eff_ps;
    int vmid;
    uint64_t vttb;
} SMMUS2Cfg;

typedef struct SMMUTransCfg {
    SMMUStage stage;
    bool disabled;
    bool bypassed;
    bool aborted;
    bool affd;
    uint32_t iotlb_hits;
    uint32_t iotlb_misses;
    bool aa64;
    bool record_faults;
    uint8_t oas;
    uint8_t tbi;
    int asid;
    SMMUTransTableInfo tt[2];
    struct SMMUS2Cfg s2cfg;
} SMMUTransCfg;

typedef struct SMMUDevice {
    void               *smmu;
    PCIBus             *bus;
    int                devfn;
    IOMMUMemoryRegion  iommu;
    AddressSpace       as;
    uint32_t           cfg_cache_hits;
    uint32_t           cfg_cache_misses;
    QLIST_ENTRY(SMMUDevice) next;
} SMMUDevice;

typedef struct SMMUPciBus {
    PCIBus       *bus;
    SMMUDevice   *pbdev[];
} SMMUPciBus;

typedef struct SMMUIOTLBKey {
    uint64_t iova;
    int asid;
    int vmid;
    uint8_t tg;
    uint8_t level;
} SMMUIOTLBKey;

typedef struct SMMUSIDRange {
    uint32_t start;
    uint32_t end;
} SMMUSIDRange;

struct SMMUState {
    SysBusDevice  dev;
    const char *mrtypename;
    MemoryRegion iomem;

    GHashTable *smmu_pcibus_by_busptr;
    GHashTable *configs;
    GHashTable *iotlb;
    SMMUPciBus *smmu_pcibus_by_bus_num[SMMU_PCI_BUS_MAX];
    PCIBus *pci_bus;
    QLIST_HEAD(, SMMUDevice) devices_with_notifiers;
    uint8_t bus_num;
    PCIBus *primary_bus;
    bool smmu_per_bus;
};

struct SMMUBaseClass {
    SysBusDeviceClass parent_class;

    DeviceRealize parent_realize;

};

#define TYPE_ARM_SMMU "arm-smmu"
OBJECT_DECLARE_TYPE(SMMUState, SMMUBaseClass, ARM_SMMU)

SMMUPciBus *smmu_find_smmu_pcibus(SMMUState *s, uint8_t bus_num);

static inline uint16_t smmu_get_sid(SMMUDevice *sdev)
{
    return PCI_BUILD_BDF(pci_bus_num(sdev->bus), sdev->devfn);
}

int smmu_ptw(SMMUState *bs, SMMUTransCfg *cfg, dma_addr_t iova,
             IOMMUAccessFlags perm, SMMUTLBEntry *tlbe,
             SMMUPTWEventInfo *info);

SMMUTLBEntry *smmu_translate(SMMUState *bs, SMMUTransCfg *cfg, dma_addr_t addr,
                             IOMMUAccessFlags flag, SMMUPTWEventInfo *info);

SMMUTransTableInfo *select_tt(SMMUTransCfg *cfg, dma_addr_t iova);

SMMUDevice *smmu_find_sdev(SMMUState *s, uint32_t sid);

#define SMMU_IOTLB_MAX_SIZE 256

SMMUTLBEntry *smmu_iotlb_lookup(SMMUState *bs, SMMUTransCfg *cfg,
                                SMMUTransTableInfo *tt, hwaddr iova);
void smmu_iotlb_insert(SMMUState *bs, SMMUTransCfg *cfg, SMMUTLBEntry *entry);
SMMUIOTLBKey smmu_get_iotlb_key(int asid, int vmid, uint64_t iova,
                                uint8_t tg, uint8_t level);
void smmu_iotlb_inv_all(SMMUState *s);
void smmu_iotlb_inv_asid_vmid(SMMUState *s, int asid, int vmid);
void smmu_iotlb_inv_vmid(SMMUState *s, int vmid);
void smmu_iotlb_inv_vmid_s1(SMMUState *s, int vmid);
void smmu_iotlb_inv_iova(SMMUState *s, int asid, int vmid, dma_addr_t iova,
                         uint8_t tg, uint64_t num_pages, uint8_t ttl);
void smmu_iotlb_inv_ipa(SMMUState *s, int vmid, dma_addr_t ipa, uint8_t tg,
                        uint64_t num_pages, uint8_t ttl);
void smmu_configs_inv_sid_range(SMMUState *s, SMMUSIDRange sid_range);
void smmu_inv_notifiers_all(SMMUState *s);

#endif
