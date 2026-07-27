/*
 * HP-PARISC Dino PCI chipset emulation, as in B160L and similar machines
 *
 * (C) 2017-2019 by Helge Deller <deller@gmx.de>
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 * Documentation available at:
 * https://parisc.wiki.kernel.org/images-parisc/9/91/Dino_ers.pdf
 * https://parisc.wiki.kernel.org/images-parisc/7/70/Dino_3_1_Errata.pdf
 */

#ifndef DINO_H
#define DINO_H

#include "hw/pci/pci_host.h"

#define TYPE_DINO_PCI_HOST_BRIDGE "dino-pcihost"
OBJECT_DECLARE_SIMPLE_TYPE(DinoState, DINO_PCI_HOST_BRIDGE)

#define DINO_IAR0               0x004
#define DINO_IODC               0x008
#define DINO_IRR0               0x00C
#define DINO_IAR1               0x010
#define DINO_IRR1               0x014
#define DINO_IMR                0x018
#define DINO_IPR                0x01C
#define DINO_TOC_ADDR           0x020
#define DINO_ICR                0x024
#define DINO_ILR                0x028
#define DINO_IO_COMMAND         0x030
#define DINO_IO_STATUS          0x034
#define DINO_IO_CONTROL         0x038
#define DINO_IO_GSC_ERR_RESP    0x040
#define DINO_IO_ERR_INFO        0x044
#define DINO_IO_PCI_ERR_RESP    0x048
#define DINO_IO_FBB_EN          0x05c
#define DINO_IO_ADDR_EN         0x060
#define DINO_PCI_CONFIG_ADDR    0x064
#define DINO_PCI_CONFIG_DATA    0x068
#define DINO_PCI_IO_DATA        0x06c
#define DINO_PCI_MEM_DATA       0x070
#define DINO_GSC2X_CONFIG       0x7b4
#define DINO_GMASK              0x800
#define DINO_PAMR               0x804
#define DINO_PAPR               0x808
#define DINO_DAMODE             0x80c
#define DINO_PCICMD             0x810
#define DINO_PCISTS             0x814
#define DINO_MLTIM              0x81c
#define DINO_BRDG_FEAT          0x820
#define DINO_PCIROR             0x824
#define DINO_PCIWOR             0x828
#define DINO_TLTIM              0x830

#define DINO_IRQS         11
#define DINO_IRR_MASK     0x5ff
#define DINO_LOCAL_IRQS   (DINO_IRQS + 1)
#define DINO_MASK_IRQ(x)  (1 << (x))

#define DINO_IRQ_PCIINTA   0
#define DINO_IRQ_PCIINTB   1
#define DINO_IRQ_PCIINTC   2
#define DINO_IRQ_PCIINTD   3
#define DINO_IRQ_PCIINTE   4
#define DINO_IRQ_PCIINTF   5
#define DINO_IRQ_GSCEXTINT 6
#define DINO_IRQ_BUSERRINT 7
#define DINO_IRQ_PS2INT    8
#define DINO_IRQ_UNUSED    9
#define DINO_IRQ_RS232INT  10

#define PCIINTA   0x001
#define PCIINTB   0x002
#define PCIINTC   0x004
#define PCIINTD   0x008
#define PCIINTE   0x010
#define PCIINTF   0x020
#define GSCEXTINT 0x040
#define RS232INT  0x400

#define DINO_MEM_CHUNK_SIZE (8 * MiB)

#define DINO800_REGS (1 + (DINO_TLTIM - DINO_GMASK) / 4)
static const uint32_t reg800_keep_bits[DINO800_REGS] = {
    MAKE_64BIT_MASK(0, 1),
    MAKE_64BIT_MASK(0, 7),
    MAKE_64BIT_MASK(0, 7),
    MAKE_64BIT_MASK(0, 8),
    MAKE_64BIT_MASK(0, 7),
    MAKE_64BIT_MASK(0, 9),
    MAKE_64BIT_MASK(0, 32),
    MAKE_64BIT_MASK(0, 8),
    MAKE_64BIT_MASK(0, 30),
    MAKE_64BIT_MASK(0, 24),
    MAKE_64BIT_MASK(0, 22),
    MAKE_64BIT_MASK(0, 32),
    MAKE_64BIT_MASK(0, 9),
};

#define DINO_PCI_ADDR           0x064
#define DINO_CONFIG_DATA        0x068
#define DINO_IO_DATA            0x06c

struct DinoState {
    PCIHostState parent_obj;

    uint32_t config_reg_dino;

    uint32_t iar0;
    uint32_t iar1;
    uint32_t imr;
    uint32_t ipr;
    uint32_t icr;
    uint32_t ilr;
    uint32_t io_fbb_en;
    uint32_t io_addr_en;
    uint32_t io_control;
    uint32_t toc_addr;

    uint32_t reg800[DINO800_REGS];

    MemoryRegion this_mem;
    MemoryRegion pci_mem;
    MemoryRegion pci_mem_alias[32];

    MemoryRegion *memory_as;

    AddressSpace bm_as;
    MemoryRegion bm;
    MemoryRegion bm_ram_alias;
    MemoryRegion bm_pci_alias;
    MemoryRegion bm_cpu_alias;

    qemu_irq irqs[DINO_IRQS];
};

#endif
