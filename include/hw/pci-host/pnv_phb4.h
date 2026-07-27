/*
 * QEMU PowerPC PowerNV (POWER9) PHB4 model
 *
 * Copyright (c) 2018-2020, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PCI_HOST_PNV_PHB4_H
#define PCI_HOST_PNV_PHB4_H

#include "hw/pci-host/pnv_phb.h"
#include "hw/pci/pci_bus.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_nest_pervasive.h"
#include "hw/ppc/xive.h"
#include "qom/object.h"


#define TYPE_PNV_PHB4 "pnv-phb4"
OBJECT_DECLARE_SIMPLE_TYPE(PnvPHB4, PNV_PHB4)

typedef struct PnvPhb4PecStack PnvPhb4PecStack;

typedef struct PnvPhb4DMASpace {
    PCIBus *bus;
    uint8_t devfn;
    int pe_num;
#define PHB_INVALID_PE (-1)
    PnvPHB4 *phb;
    AddressSpace dma_as;
    IOMMUMemoryRegion dma_mr;
    MemoryRegion msi32_mr;
    MemoryRegion msi64_mr;
    QLIST_ENTRY(PnvPhb4DMASpace) list;
} PnvPhb4DMASpace;

#define TYPE_PNV_PHB4_ROOT_BUS "pnv-phb4-root"
struct PnvPHB4RootBus {
    PCIBus parent;

    uint32_t chip_id;
    uint32_t phb_id;
};
OBJECT_DECLARE_SIMPLE_TYPE(PnvPHB4RootBus, PNV_PHB4_ROOT_BUS)

#define PNV_PHB4_MAX_LSIs          8
#define PNV_PHB4_MAX_INTs          4096
#define PNV_PHB4_MAX_MIST          (PNV_PHB4_MAX_INTs >> 2)
#define PNV_PHB4_MAX_MMIO_WINDOWS  32
#define PNV_PHB4_MIN_MMIO_WINDOWS  16
#define PNV_PHB4_NUM_REGS          (0x3000 >> 3)
#define PNV_PHB4_MAX_PEs           512
#define PNV_PHB4_MAX_TVEs          (PNV_PHB4_MAX_PEs * 2)
#define PNV_PHB4_MAX_PEEVs         (PNV_PHB4_MAX_PEs / 64)
#define PNV_PHB4_MAX_MBEs          (PNV_PHB4_MAX_MMIO_WINDOWS * 2)

#define PNV_PHB4_VERSION           0x000000a400000002ull
#define PNV_PHB4_DEVICE_ID         0x04c1

#define PCI_MMIO_TOTAL_SIZE        (0x1ull << 60)

struct PnvPHB4 {
    DeviceState parent;

    PnvPHB *phb_base;

    uint32_t chip_id;
    uint32_t phb_id;

    PnvPhb4PecState *pec;

    char bus_path[8];

    uint64_t regs[PNV_PHB4_NUM_REGS];
    MemoryRegion mr_regs;

    uint64_t scom_hv_ind_addr_reg;

    bool big_phb;

    MemoryRegion mr_mmio[PNV_PHB4_MAX_MMIO_WINDOWS];

    MemoryRegion pci_mmio;
    MemoryRegion pci_io;

#define PHB4_PEC_PCI_STK_REGS_COUNT  0xf
    uint64_t pci_regs[PHB4_PEC_PCI_STK_REGS_COUNT];
    MemoryRegion pci_regs_mr;

#define PHB4_PEC_NEST_STK_REGS_COUNT  0x18
    uint64_t nest_regs[PHB4_PEC_NEST_STK_REGS_COUNT];
    MemoryRegion nest_regs_mr;

    MemoryRegion phb_regs_mr;

    MemoryRegion phbbar;
    MemoryRegion intbar;
    MemoryRegion mmbar0;
    MemoryRegion mmbar1;
    uint64_t mmio0_base;
    uint64_t mmio0_size;
    uint64_t mmio1_base;
    uint64_t mmio1_size;

    uint64_t ioda_LIST[PNV_PHB4_MAX_LSIs];
    uint64_t ioda_MIST[PNV_PHB4_MAX_MIST];
    uint64_t ioda_TVT[PNV_PHB4_MAX_TVEs];
    uint64_t ioda_MBT[PNV_PHB4_MAX_MBEs];
    uint64_t ioda_MDT[PNV_PHB4_MAX_PEs];
    uint64_t ioda_PEEV[PNV_PHB4_MAX_PEEVs];

    uint8_t  ioda_PEST_AB[PNV_PHB4_MAX_PEs];

    XiveSource xsrc;
    qemu_irq *qirqs;

    QLIST_HEAD(, PnvPhb4DMASpace) dma_spaces;
};

void pnv_phb4_pic_print_info(PnvPHB4 *phb, GString *buf);
int pnv_phb4_pec_get_phb_id(PnvPhb4PecState *pec, int stack_index);
PnvPhb4PecState *pnv_pec_add_phb(PnvChip *chip, PnvPHB *phb, Error **errp);
void pnv_phb4_bus_init(DeviceState *dev, PnvPHB4 *phb);
extern const MemoryRegionOps pnv_phb4_xscom_ops;

#define TYPE_PNV_PHB4_PEC "pnv-phb4-pec"
OBJECT_DECLARE_TYPE(PnvPhb4PecState, PnvPhb4PecClass, PNV_PHB4_PEC)

struct PnvPhb4PecState {
    DeviceState parent;

    uint32_t index;
    uint32_t chip_id;

    PnvNestChipletPervasive nest_pervasive;

#define PHB4_PEC_NEST_REGS_COUNT    0xf
    uint64_t nest_regs[PHB4_PEC_NEST_REGS_COUNT];
    MemoryRegion nest_regs_mr;

#define PHB4_PEC_PCI_REGS_COUNT     0x3
    uint64_t pci_regs[PHB4_PEC_PCI_REGS_COUNT];
    MemoryRegion pci_regs_mr;

    uint32_t num_phbs;
#define MAX_PHBS_PER_PEC        3
    PnvPHB *phbs[MAX_PHBS_PER_PEC];

    PnvChip *chip;
};


struct PnvPhb4PecClass {
    DeviceClass parent_class;

    uint32_t (*xscom_cplt_base)(PnvPhb4PecState *pec);
    uint32_t (*xscom_nest_base)(PnvPhb4PecState *pec);
    uint32_t xscom_nest_size;
    uint32_t (*xscom_pci_base)(PnvPhb4PecState *pec);
    uint32_t xscom_pci_size;
    const char *compat;
    int compat_size;
    const char *stk_compat;
    int stk_compat_size;
    uint64_t version;
    const char *phb_type;
    const uint32_t *num_phbs;
};

#define TYPE_PNV_PHB5 "pnv-phb5"
#define PNV_PHB5(obj) \
    OBJECT_CHECK(PnvPhb4, (obj), TYPE_PNV_PHB5)

#define PNV_PHB5_VERSION           0x000000a500000002ull

#define TYPE_PNV_PHB5_PEC "pnv-phb5-pec"
#define PNV_PHB5_PEC(obj) \
    OBJECT_CHECK(PnvPhb4PecState, (obj), TYPE_PNV_PHB5_PEC)

#endif
