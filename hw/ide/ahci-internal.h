/*
 * QEMU AHCI Emulation
 *
 * Copyright (c) 2010 qiaochong@loongson.cn
 * Copyright (c) 2010 Roland Elek <elek.roland@gmail.com>
 * Copyright (c) 2010 Sebastian Herbszt <herbszt@gmx.de>
 * Copyright (c) 2010 Alexander Graf <agraf@suse.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef HW_IDE_AHCI_INTERNAL_H
#define HW_IDE_AHCI_INTERNAL_H

#include "hw/ide/ahci.h"
#include "ide-internal.h"

#define AHCI_MEM_BAR_SIZE         0x1000
#define AHCI_MAX_PORTS            32
#define AHCI_MAX_SG               168
#define AHCI_DMA_BOUNDARY         0xffffffff
#define AHCI_USE_CLUSTERING       0
#define AHCI_MAX_CMDS             32
#define AHCI_CMD_SZ               32
#define AHCI_CMD_SLOT_SZ          (AHCI_MAX_CMDS * AHCI_CMD_SZ)
#define AHCI_RX_FIS_SZ            256
#define AHCI_CMD_TBL_CDB          0x40
#define AHCI_CMD_TBL_HDR_SZ       0x80
#define AHCI_CMD_TBL_SZ           (AHCI_CMD_TBL_HDR_SZ + (AHCI_MAX_SG * 16))
#define AHCI_CMD_TBL_AR_SZ        (AHCI_CMD_TBL_SZ * AHCI_MAX_CMDS)
#define AHCI_PORT_PRIV_DMA_SZ     (AHCI_CMD_SLOT_SZ + AHCI_CMD_TBL_AR_SZ + \
                                   AHCI_RX_FIS_SZ)

#define AHCI_IRQ_ON_SG            (1U << 31)
#define AHCI_CMD_ATAPI            (1 << 5)
#define AHCI_CMD_WRITE            (1 << 6)
#define AHCI_CMD_PREFETCH         (1 << 7)
#define AHCI_CMD_RESET            (1 << 8)
#define AHCI_CMD_CLR_BUSY         (1 << 10)

#define RX_FIS_D2H_REG            0x40
#define RX_FIS_SDB                0x58
#define RX_FIS_UNK                0x60

enum AHCIHostReg {
    AHCI_HOST_REG_CAP        = 0,
    AHCI_HOST_REG_CTL        = 1,
    AHCI_HOST_REG_IRQ_STAT   = 2,
    AHCI_HOST_REG_PORTS_IMPL = 3,
    AHCI_HOST_REG_VERSION    = 4,
    AHCI_HOST_REG_CCC_CTL    = 5,
    AHCI_HOST_REG_CCC_PORTS  = 6,
    AHCI_HOST_REG_EM_LOC     = 7,
    AHCI_HOST_REG_EM_CTL     = 8,
    AHCI_HOST_REG_CAP2       = 9,
    AHCI_HOST_REG_BOHC       = 10,
    AHCI_HOST_REG__COUNT     = 11
};

#define HOST_CTL_RESET            (1 << 0)
#define HOST_CTL_IRQ_EN           (1 << 1)
#define HOST_CTL_AHCI_EN          (1U << 31)

#define HOST_CAP_SSC              (1 << 14)
#define HOST_CAP_AHCI             (1 << 18)
#define HOST_CAP_CLO              (1 << 24)
#define HOST_CAP_SSS              (1 << 27)
#define HOST_CAP_NCQ              (1 << 30)
#define HOST_CAP_64               (1U << 31)

enum AHCIPortReg {
    AHCI_PORT_REG_LST_ADDR    = 0,
    AHCI_PORT_REG_LST_ADDR_HI = 1,
    AHCI_PORT_REG_FIS_ADDR    = 2,
    AHCI_PORT_REG_FIS_ADDR_HI = 3,
    AHCI_PORT_REG_IRQ_STAT    = 4,
    AHCI_PORT_REG_IRQ_MASK    = 5,
    AHCI_PORT_REG_CMD         = 6,
    AHCI_PORT_REG_TFDATA      = 8,
    AHCI_PORT_REG_SIG         = 9,
    AHCI_PORT_REG_SCR_STAT    = 10,
    AHCI_PORT_REG_SCR_CTL     = 11,
    AHCI_PORT_REG_SCR_ERR     = 12,
    AHCI_PORT_REG_SCR_ACT     = 13,
    AHCI_PORT_REG_CMD_ISSUE   = 14,
    AHCI_PORT_REG_SCR_NOTIF   = 15,
    AHCI_PORT_REG_FIS_CTL     = 16,
    AHCI_PORT_REG_DEV_SLEEP   = 17,
    AHCI_PORT_REG_VENDOR_1    = 28,
    AHCI_PORT_REG_VENDOR_2    = 29,
    AHCI_PORT_REG_VENDOR_3    = 30,
    AHCI_PORT_REG_VENDOR_4    = 31,
    AHCI_PORT_REG__COUNT      = 32
};

enum AHCIPortIRQ {
    AHCI_PORT_IRQ_BIT_DHRS = 0,
    AHCI_PORT_IRQ_BIT_PSS  = 1,
    AHCI_PORT_IRQ_BIT_DSS  = 2,
    AHCI_PORT_IRQ_BIT_SDBS = 3,
    AHCI_PORT_IRQ_BIT_UFS  = 4,
    AHCI_PORT_IRQ_BIT_DPS  = 5,
    AHCI_PORT_IRQ_BIT_PCS  = 6,
    AHCI_PORT_IRQ_BIT_DMPS = 7,
    AHCI_PORT_IRQ_BIT_PRCS = 22,
    AHCI_PORT_IRQ_BIT_IPMS = 23,
    AHCI_PORT_IRQ_BIT_OFS  = 24,
    AHCI_PORT_IRQ_BIT_INFS = 26,
    AHCI_PORT_IRQ_BIT_IFS  = 27,
    AHCI_PORT_IRQ_BIT_HBDS = 28,
    AHCI_PORT_IRQ_BIT_HBFS = 29,
    AHCI_PORT_IRQ_BIT_TFES = 30,
    AHCI_PORT_IRQ_BIT_CPDS = 31,
    AHCI_PORT_IRQ__COUNT   = 32
};


#define PORT_IRQ_COLD_PRES        (1U << 31)
#define PORT_IRQ_TF_ERR           (1 << 30)
#define PORT_IRQ_HBUS_ERR         (1 << 29)
#define PORT_IRQ_HBUS_DATA_ERR    (1 << 28)
#define PORT_IRQ_IF_ERR           (1 << 27)
#define PORT_IRQ_IF_NONFATAL      (1 << 26)
#define PORT_IRQ_OVERFLOW         (1 << 24)
#define PORT_IRQ_BAD_PMP          (1 << 23)
#define PORT_IRQ_PHYRDY           (1 << 22)
#define PORT_IRQ_DEV_ILCK         (1 << 7)
#define PORT_IRQ_CONNECT          (1 << 6)
#define PORT_IRQ_SG_DONE          (1 << 5)
#define PORT_IRQ_UNK_FIS          (1 << 4)
#define PORT_IRQ_SDB_FIS          (1 << 3)
#define PORT_IRQ_DMAS_FIS         (1 << 2)
#define PORT_IRQ_PIOS_FIS         (1 << 1)
#define PORT_IRQ_D2H_REG_FIS      (1 << 0)

#define PORT_IRQ_FREEZE           (PORT_IRQ_HBUS_ERR | PORT_IRQ_IF_ERR |   \
                                   PORT_IRQ_CONNECT | PORT_IRQ_PHYRDY |    \
                                   PORT_IRQ_UNK_FIS)
#define PORT_IRQ_ERROR            (PORT_IRQ_FREEZE | PORT_IRQ_TF_ERR |     \
                                   PORT_IRQ_HBUS_DATA_ERR)
#define DEF_PORT_IRQ              (PORT_IRQ_ERROR | PORT_IRQ_SG_DONE |     \
                                   PORT_IRQ_SDB_FIS | PORT_IRQ_DMAS_FIS |  \
                                   PORT_IRQ_PIOS_FIS | PORT_IRQ_D2H_REG_FIS)

#define PORT_CMD_ATAPI            (1 << 24)
#define PORT_CMD_LIST_ON          (1 << 15)
#define PORT_CMD_FIS_ON           (1 << 14)
#define PORT_CMD_FIS_RX           (1 << 4)
#define PORT_CMD_CLO              (1 << 3)
#define PORT_CMD_POWER_ON         (1 << 2)
#define PORT_CMD_SPIN_UP          (1 << 1)
#define PORT_CMD_START            (1 << 0)

#define PORT_CMD_ICC_MASK        (0xfU << 28)
#define PORT_CMD_ICC_ACTIVE       (0x1 << 28)
#define PORT_CMD_ICC_PARTIAL      (0x2 << 28)
#define PORT_CMD_ICC_SLUMBER      (0x6 << 28)

#define PORT_CMD_RO_MASK          0x007dffe0

#define AHCI_FLAG_NO_NCQ                  (1 << 24)
#define AHCI_FLAG_IGN_IRQ_IF_ERR          (1 << 25)
#define AHCI_FLAG_HONOR_PI                (1 << 26)
#define AHCI_FLAG_IGN_SERR_INTERNAL       (1 << 27)
#define AHCI_FLAG_32BIT_ONLY              (1 << 28)

#define ATA_SRST                          (1 << 2)

#define STATE_RUN                         0
#define STATE_RESET                       1

#define SATA_SCR_SSTATUS_DET_NODEV        0x0
#define SATA_SCR_SSTATUS_DET_DEV_PRESENT_PHY_UP 0x3

#define SATA_SCR_SSTATUS_SPD_NODEV        0x00
#define SATA_SCR_SSTATUS_SPD_GEN1         0x10

#define SATA_SCR_SSTATUS_IPM_NODEV        0x000
#define SATA_SCR_SSTATUS_IPM_ACTIVE       0X100

#define AHCI_SCR_SCTL_DET                 0xf

#define SATA_FIS_TYPE_REGISTER_H2D        0x27
#define   SATA_FIS_REG_H2D_UPDATE_COMMAND_REGISTER 0x80
#define SATA_FIS_TYPE_REGISTER_D2H        0x34
#define SATA_FIS_TYPE_PIO_SETUP           0x5f
#define SATA_FIS_TYPE_SDB                 0xA1

#define AHCI_CMD_HDR_CMD_FIS_LEN           0x1f
#define AHCI_CMD_HDR_PRDT_LEN              16

#define SATA_SIGNATURE_CDROM               0xeb140101
#define SATA_SIGNATURE_DISK                0x00000101

#define AHCI_GENERIC_HOST_CONTROL_REGS_MAX_ADDR 0x2c

#define AHCI_PORT_REGS_START_ADDR          0x100
#define AHCI_PORT_ADDR_OFFSET_MASK         0x7f
#define AHCI_PORT_ADDR_OFFSET_LEN          0x80

#define AHCI_NUM_COMMAND_SLOTS             31
#define AHCI_SUPPORTED_SPEED               20
#define AHCI_SUPPORTED_SPEED_GEN1          1
#define AHCI_VERSION_1_0                   0x10000

#define AHCI_PROGMODE_MAJOR_REV_1          1

#define AHCI_COMMAND_TABLE_ACMD            0x40

#define AHCI_PRDT_SIZE_MASK                0x3fffff

#define IDE_FEATURE_DMA                    1

#define READ_FPDMA_QUEUED                  0x60
#define WRITE_FPDMA_QUEUED                 0x61
#define NCQ_NON_DATA                       0x63
#define RECEIVE_FPDMA_QUEUED               0x65
#define SEND_FPDMA_QUEUED                  0x64

#define NCQ_FIS_FUA_MASK                   0x80
#define NCQ_FIS_RARC_MASK                  0x01

#define RES_FIS_DSFIS                      0x00
#define RES_FIS_PSFIS                      0x20
#define RES_FIS_RFIS                       0x40
#define RES_FIS_SDBFIS                     0x58
#define RES_FIS_UFIS                       0x60

#define SATA_CAP_SIZE           0x8
#define SATA_CAP_REV            0x2
#define SATA_CAP_BAR            0x4

typedef struct AHCIPortRegs {
    uint32_t    lst_addr;
    uint32_t    lst_addr_hi;
    uint32_t    fis_addr;
    uint32_t    fis_addr_hi;
    uint32_t    irq_stat;
    uint32_t    irq_mask;
    uint32_t    cmd;
    uint32_t    unused0;
    uint32_t    tfdata;
    uint32_t    sig;
    uint32_t    scr_stat;
    uint32_t    scr_ctl;
    uint32_t    scr_err;
    uint32_t    scr_act;
    uint32_t    cmd_issue;
    uint32_t    reserved;
} AHCIPortRegs;

typedef struct AHCICmdHdr {
    uint16_t    opts;
    uint16_t    prdtl;
    uint32_t    status;
    uint64_t    tbl_addr;
    uint32_t    reserved[4];
} QEMU_PACKED AHCICmdHdr;

typedef struct AHCI_SG {
    uint64_t    addr;
    uint32_t    reserved;
    uint32_t    flags_size;
} QEMU_PACKED AHCI_SG;

typedef struct NCQTransferState {
    AHCIDevice *drive;
    BlockAIOCB *aiocb;
    AHCICmdHdr *cmdh;
    QEMUSGList sglist;
    BlockAcctCookie acct;
    uint32_t sector_count;
    uint64_t lba;
    uint8_t tag;
    uint8_t cmd;
    uint8_t slot;
    bool used;
    bool halt;
} NCQTransferState;

struct AHCIDevice {
    IDEDMA dma;
    IDEBus port;
    int port_no;
    uint32_t port_state;
    uint32_t finished;
    AHCIPortRegs port_regs;
    struct AHCIState *hba;
    QEMUBH *check_bh;
    uint8_t *lst;
    uint8_t *res_fis;
    bool done_first_drq;
    int32_t busy_slot;
    bool init_d2h_sent;
    AHCICmdHdr *cur_cmd;
    NCQTransferState ncq_tfs[AHCI_MAX_CMDS];
    MemReentrancyGuard mem_reentrancy_guard;
};

extern const VMStateDescription vmstate_ahci;

#define VMSTATE_AHCI(_field, _state) {                               \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(AHCIState),                                 \
    .vmsd       = &vmstate_ahci,                                     \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, AHCIState),   \
}

typedef struct NCQFrame {
    uint8_t fis_type;
    uint8_t c;
    uint8_t command;
    uint8_t sector_count_low;
    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t fua;
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t sector_count_high;
    uint8_t tag;
    uint8_t prio;
    uint8_t icc;
    uint8_t control;
    uint8_t aux0;
    uint8_t aux1;
    uint8_t aux2;
    uint8_t aux3;
} QEMU_PACKED NCQFrame;

typedef struct SDBFIS {
    uint8_t type;
    uint8_t flags;
    uint8_t status;
    uint8_t error;
    uint32_t payload;
} QEMU_PACKED SDBFIS;

void ahci_realize(AHCIState *s, DeviceState *qdev, AddressSpace *as);
void ahci_init(AHCIState *s, DeviceState *qdev);
void ahci_uninit(AHCIState *s);

void ahci_reset(AHCIState *s);

#endif
