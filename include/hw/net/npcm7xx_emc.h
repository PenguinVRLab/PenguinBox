/*
 * Nuvoton NPCM7xx EMC Module
 *
 * Copyright 2020 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#ifndef NPCM7XX_EMC_H
#define NPCM7XX_EMC_H

#include "hw/irq.h"
#include "hw/sysbus.h"
#include "net/net.h"

enum NPCM7xxPWMRegister {
    REG_CAMCMR,
    REG_CAMEN,

    REG_CAMM_BASE,
    REG_CAML_BASE,
    REG_CAMML_LAST = 0x21,

    REG_TXDLSA = 0x22,
    REG_RXDLSA,
    REG_MCMDR,
    REG_MIID,
    REG_MIIDA,
    REG_FFTCR,
    REG_TSDR,
    REG_RSDR,
    REG_DMARFC,
    REG_MIEN,

    REG_MISTA,
    REG_MGSTA,
    REG_MPCNT,
    REG_MRPC,
    REG_MRPCC,
    REG_MREPC,
    REG_DMARFS,
    REG_CTXDSA,
    REG_CTXBSA,
    REG_CRXDSA,
    REG_CRXBSA,

    NPCM7XX_NUM_EMC_REGS,
};

#define REG_CAMCMR_ECMP (1 << 4)
#define REG_CAMCMR_CCAM (1 << 3)
#define REG_CAMCMR_ABP (1 << 2)
#define REG_CAMCMR_AMP (1 << 1)
#define REG_CAMCMR_AUP (1 << 0)

#define REG_MCMDR_SWR (1 << 24)
#define REG_MCMDR_LBK (1 << 21)
#define REG_MCMDR_OPMOD (1 << 20)
#define REG_MCMDR_ENMDC (1 << 19)
#define REG_MCMDR_FDUP (1 << 18)
#define REG_MCMDR_ENSEQ (1 << 17)
#define REG_MCMDR_SDPZ (1 << 16)
#define REG_MCMDR_NDEF (1 << 9)
#define REG_MCMDR_TXON (1 << 8)
#define REG_MCMDR_SPCRC (1 << 5)
#define REG_MCMDR_AEP (1 << 4)
#define REG_MCMDR_ACP (1 << 3)
#define REG_MCMDR_ARP (1 << 2)
#define REG_MCMDR_ALP (1 << 1)
#define REG_MCMDR_RXON (1 << 0)

#define REG_MIEN_ENTDU (1 << 23)
#define REG_MIEN_ENTXCP (1 << 18)
#define REG_MIEN_ENTXINTR (1 << 16)
#define REG_MIEN_ENRDU (1 << 10)
#define REG_MIEN_ENRXGD (1 << 4)
#define REG_MIEN_ENRXINTR (1 << 0)

#define REG_MISTA_TXBERR (1 << 24)
#define REG_MISTA_TDU (1 << 23)
#define REG_MISTA_TXCP (1 << 18)
#define REG_MISTA_TXINTR (1 << 16)
#define REG_MISTA_RXBERR (1 << 11)
#define REG_MISTA_RDU (1 << 10)
#define REG_MISTA_DENI (1 << 9)
#define REG_MISTA_DFOI (1 << 8)
#define REG_MISTA_RXGD (1 << 4)
#define REG_MISTA_PTLE (1 << 3)
#define REG_MISTA_RXINTR (1 << 0)

#define REG_MGSTA_TXHA (1 << 11)
#define REG_MGSTA_RXHA (1 << 11)

#define REG_DMARFC_RXMS(word) extract32((word), 0, 16)

#define REG_MIIDA_BUSY (1 << 17)

typedef struct NPCM7xxEMCTxDesc NPCM7xxEMCTxDesc;
typedef struct NPCM7xxEMCRxDesc NPCM7xxEMCRxDesc;

struct NPCM7xxEMCTxDesc {
    uint32_t flags;
    uint32_t txbsa;
    uint32_t status_and_length;
    uint32_t ntxdsa;
};

struct NPCM7xxEMCRxDesc {
    uint32_t status_and_length;
    uint32_t rxbsa;
    uint32_t reserved;
    uint32_t nrxdsa;
};

#define TX_DESC_FLAG_OWNER_MASK (1 << 31)
#define TX_DESC_FLAG_INTEN (1 << 2)
#define TX_DESC_FLAG_CRCAPP (1 << 1)
#define TX_DESC_FLAG_PADEN (1 << 0)

#define TX_DESC_STATUS_CCNT_SHIFT 28
#define TX_DESC_STATUS_CCNT_BITSIZE 4
#define TX_DESC_STATUS_SQE (1 << 26)
#define TX_DESC_STATUS_PAU (1 << 25)
#define TX_DESC_STATUS_TXHA (1 << 24)
#define TX_DESC_STATUS_LC (1 << 23)
#define TX_DESC_STATUS_TXABT (1 << 22)
#define TX_DESC_STATUS_NCS (1 << 21)
#define TX_DESC_STATUS_EXDEF (1 << 20)
#define TX_DESC_STATUS_TXCP (1 << 19)
#define TX_DESC_STATUS_DEF (1 << 17)
#define TX_DESC_STATUS_TXINTR (1 << 16)

#define TX_DESC_PKT_LEN(word) extract32((word), 0, 16)

#define TX_DESC_TXBSA(word) ((uint32_t) (word) & ~3u)

#define TX_DESC_NTXDSA(word) ((uint32_t) (word) & ~3u)

#define RX_DESC_STATUS_OWNER_SHIFT 30
#define RX_DESC_STATUS_OWNER_BITSIZE 2
#define RX_DESC_STATUS_OWNER_MASK (3 << RX_DESC_STATUS_OWNER_SHIFT)
#define RX_DESC_STATUS_RP (1 << 22)
#define RX_DESC_STATUS_ALIE (1 << 21)
#define RX_DESC_STATUS_RXGD (1 << 20)
#define RX_DESC_STATUS_PTLE (1 << 19)
#define RX_DESC_STATUS_CRCE (1 << 17)
#define RX_DESC_STATUS_RXINTR (1 << 16)

#define RX_DESC_PKT_LEN(word) extract32((word), 0, 16)

#define RX_DESC_RXBSA(word) ((uint32_t) (word) & ~3u)

#define RX_DESC_NRXDSA(word) ((uint32_t) (word) & ~3u)

#define MIN_PACKET_LENGTH 64

struct NPCM7xxEMCState {
    SysBusDevice parent;

    MemoryRegion iomem;

    qemu_irq tx_irq;
    qemu_irq rx_irq;

    NICState *nic;
    NICConf conf;

    uint8_t emc_num;

    uint32_t regs[NPCM7XX_NUM_EMC_REGS];

    bool tx_active;

    bool rx_active;
};

#define TYPE_NPCM7XX_EMC "npcm7xx-emc"
OBJECT_DECLARE_SIMPLE_TYPE(NPCM7xxEMCState, NPCM7XX_EMC)

#endif
