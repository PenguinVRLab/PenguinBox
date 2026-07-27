/*
 * Nuvoton NPCM7xx/8xx GMAC Module
 *
 * Copyright 2024 Google LLC
 * Authors:
 * Hao Wu <wuhaotsh@google.com>
 * Nabih Estefan <nabihestefan@google.com>
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

#ifndef NPCM_GMAC_H
#define NPCM_GMAC_H

#include "hw/irq.h"
#include "hw/sysbus.h"
#include "net/net.h"

#define NPCM_GMAC_NR_REGS (0x1060 / sizeof(uint32_t))

#define NPCM_GMAC_MAX_PHYS 32
#define NPCM_GMAC_MAX_PHY_REGS 32

struct NPCMGMACRxDesc {
    uint32_t rdes0;
    uint32_t rdes1;
    uint32_t rdes2;
    uint32_t rdes3;
};

#define RX_DESC_RDES0_OWN BIT(31)
#define RX_DESC_RDES0_DEST_ADDR_FILT_FAIL BIT(30)
#define RX_DESC_RDES0_FRAME_LEN_MASK(word) extract32(word, 16, 14)
#define RX_DESC_RDES0_FRAME_LEN_SHIFT 16
#define RX_DESC_RDES0_ERR_SUMM_MASK BIT(15)
#define RX_DESC_RDES0_DESC_ERR_MASK BIT(14)
#define RX_DESC_RDES0_SRC_ADDR_FILT_FAIL_MASK BIT(13)
#define RX_DESC_RDES0_LEN_ERR_MASK BIT(12)
#define RX_DESC_RDES0_OVRFLW_ERR_MASK BIT(11)
#define RX_DESC_RDES0_VLAN_TAG_MASK BIT(10)
#define RX_DESC_RDES0_FIRST_DESC_MASK BIT(9)
#define RX_DESC_RDES0_LAST_DESC_MASK BIT(8)
#define RX_DESC_RDES0_IPC_CHKSM_ERR_GNT_FRM_MASK BIT(7)
#define RX_DESC_RDES0_LT_COLL_MASK BIT(6)
#define RX_DESC_RDES0_FRM_TYPE_MASK BIT(5)
#define RX_DESC_RDES0_REC_WTCHDG_TMT_MASK BIT(4)
#define RX_DESC_RDES0_RCV_ERR_MASK BIT(3)
#define RX_DESC_RDES0_DRBL_BIT_ERR_MASK BIT(2)
#define RX_DESC_RDES0_CRC_ERR_MASK BIT(1)
#define RC_DESC_RDES0_RCE_MASK BIT(0)

#define RX_DESC_RDES1_DIS_INTR_COMP_MASK BIT(31)
#define RX_DESC_RDES1_RC_END_RING_MASK BIT(25)
#define RX_DESC_RDES1_SEC_ADDR_CHND_MASK BIT(24)
#define RX_DESC_RDES1_BFFR2_SZ_SHIFT 11
#define RX_DESC_RDES1_BFFR2_SZ_MASK(word) extract32(word, \
    RX_DESC_RDES1_BFFR2_SZ_SHIFT, 11)
#define RX_DESC_RDES1_BFFR1_SZ_MASK(word) extract32(word, 0, 11)


struct NPCMGMACTxDesc {
    uint32_t tdes0;
    uint32_t tdes1;
    uint32_t tdes2;
    uint32_t tdes3;
};

#define TX_DESC_TDES0_OWN BIT(31)
#define TX_DESC_TDES0_TTSS_MASK BIT(17)
#define TX_DESC_TDES0_IP_HEAD_ERR_MASK BIT(16)
#define TX_DESC_TDES0_ERR_SUMM_MASK BIT(15)
#define TX_DESC_TDES0_JBBR_TMT_MASK BIT(14)
#define TX_DESC_TDES0_FRM_FLSHD_MASK BIT(13)
#define TX_DESC_TDES0_PYLD_CHKSM_ERR_MASK BIT(12)
#define TX_DESC_TDES0_LSS_CARR_MASK BIT(11)
#define TX_DESC_TDES0_NO_CARR_MASK BIT(10)
#define TX_DESC_TDES0_LATE_COLL_MASK BIT(9)
#define TX_DESC_TDES0_EXCS_COLL_MASK BIT(8)
#define TX_DESC_TDES0_VLAN_FRM_MASK BIT(7)
#define TX_DESC_TDES0_COLL_CNT_MASK(word) extract32(word, 3, 4)
#define TX_DESC_TDES0_EXCS_DEF_MASK BIT(2)
#define TX_DESC_TDES0_UNDRFLW_ERR_MASK BIT(1)
#define TX_DESC_TDES0_DFRD_BIT_MASK BIT(0)

#define TX_DESC_TDES1_INTERR_COMP_MASK BIT(31)
#define TX_DESC_TDES1_LAST_SEG_MASK BIT(30)
#define TX_DESC_TDES1_FIRST_SEG_MASK BIT(29)
#define TX_DESC_TDES1_CHKSM_INS_CTRL_MASK(word) extract32(word, 27, 2)
#define TX_DESC_TDES1_DIS_CDC_MASK BIT(26)
#define TX_DESC_TDES1_TX_END_RING_MASK BIT(25)
#define TX_DESC_TDES1_SEC_ADDR_CHND_MASK BIT(24)
#define TX_DESC_TDES1_BFFR2_SZ_MASK(word) extract32(word, 11, 11)
#define TX_DESC_TDES1_BFFR1_SZ_MASK(word) extract32(word, 0, 11)

typedef struct NPCMGMACState {
    SysBusDevice parent;

    MemoryRegion iomem;
    qemu_irq irq;

    NICState *nic;
    NICConf conf;

    uint32_t regs[NPCM_GMAC_NR_REGS];
    uint16_t phy_regs[NPCM_GMAC_MAX_PHYS][NPCM_GMAC_MAX_PHY_REGS];
} NPCMGMACState;

#define TYPE_NPCM_GMAC "npcm-gmac"
OBJECT_DECLARE_SIMPLE_TYPE(NPCMGMACState, NPCM_GMAC)

#define NPCM_DMA_STATUS_RO_MASK(word) (word & 0xfffe0000)
#define NPCM_DMA_STATUS_W1C_MASK(word) (word & 0x1e7ff)

#define NPCM_DMA_STATUS_TX_PROCESS_STATE_SHIFT 20
#define NPCM_DMA_STATUS_TX_STOPPED_STATE \
    (0b000)
#define NPCM_DMA_STATUS_TX_RUNNING_FETCHING_STATE \
    (0b001)
#define NPCM_DMA_STATUS_TX_RUNNING_WAITING_STATE \
    (0b010)
#define NPCM_DMA_STATUS_TX_RUNNING_READ_STATE \
    (0b011)
#define NPCM_DMA_STATUS_TX_SUSPENDED_STATE \
    (0b110)
#define NPCM_DMA_STATUS_TX_RUNNING_CLOSING_STATE \
    (0b111)
#define NPCM_DMA_STATUS_RX_PROCESS_STATE_SHIFT 17
#define NPCM_DMA_STATUS_RX_STOPPED_STATE \
    (0b000)
#define NPCM_DMA_STATUS_RX_RUNNING_FETCHING_STATE \
    (0b001)
#define NPCM_DMA_STATUS_RX_RUNNING_WAITING_STATE \
    (0b011)
#define NPCM_DMA_STATUS_RX_SUSPENDED_STATE \
    (0b100)
#define NPCM_DMA_STATUS_RX_RUNNING_CLOSING_STATE \
    (0b101)
#define NPCM_DMA_STATUS_RX_RUNNING_TRANSFERRING_STATE \
    (0b111)


#define NPCM_DMA_STATUS_ERI BIT(14)
#define NPCM_DMA_STATUS_FBI BIT(13)
#define NPCM_DMA_STATUS_ETI BIT(10)
#define NPCM_DMA_STATUS_RWT BIT(9)
#define NPCM_DMA_STATUS_RPS BIT(8)
#define NPCM_DMA_STATUS_RU BIT(7)
#define NPCM_DMA_STATUS_RI BIT(6)
#define NPCM_DMA_STATUS_UNF BIT(5)
#define NPCM_DMA_STATUS_OVF BIT(4)
#define NPCM_DMA_STATUS_TJT BIT(3)
#define NPCM_DMA_STATUS_TU BIT(2)
#define NPCM_DMA_STATUS_TPS BIT(1)
#define NPCM_DMA_STATUS_TI BIT(0)

#define NPCM_DMA_STATUS_NIS BIT(16)
#define NPCM_DMA_STATUS_NIS_BITS (NPCM_DMA_STATUS_TI | \
                                  NPCM_DMA_STATUS_TU | \
                                  NPCM_DMA_STATUS_RI | \
                                  NPCM_DMA_STATUS_ERI)
#define NPCM_DMA_STATUS_AIS BIT(15)
#define NPCM_DMA_STATUS_AIS_BITS (NPCM_DMA_STATUS_TPS | \
                                  NPCM_DMA_STATUS_TJT | \
                                  NPCM_DMA_STATUS_OVF | \
                                  NPCM_DMA_STATUS_UNF | \
                                  NPCM_DMA_STATUS_RU  | \
                                  NPCM_DMA_STATUS_RPS | \
                                  NPCM_DMA_STATUS_RWT | \
                                  NPCM_DMA_STATUS_ETI | \
                                  NPCM_DMA_STATUS_FBI)

#define NPCM_DMA_INTR_ENAB_ERE BIT(14)
#define NPCM_DMA_INTR_ENAB_FBE BIT(13)
#define NPCM_DMA_INTR_ENAB_ETE BIT(10)
#define NPCM_DMA_INTR_ENAB_RWE BIT(9)
#define NPCM_DMA_INTR_ENAB_RSE BIT(8)
#define NPCM_DMA_INTR_ENAB_RUE BIT(7)
#define NPCM_DMA_INTR_ENAB_RIE BIT(6)
#define NPCM_DMA_INTR_ENAB_UNE BIT(5)
#define NPCM_DMA_INTR_ENAB_OVE BIT(4)
#define NPCM_DMA_INTR_ENAB_TJE BIT(3)
#define NPCM_DMA_INTR_ENAB_TUE BIT(2)
#define NPCM_DMA_INTR_ENAB_TSE BIT(1)
#define NPCM_DMA_INTR_ENAB_TIE BIT(0)

#define NPCM_DMA_INTR_ENAB_NIE BIT(16)
#define NPCM_DMA_INTR_ENAB_NIE_BITS (NPCM_DMA_INTR_ENAB_TIE | \
                                     NPCM_DMA_INTR_ENAB_TUE | \
                                     NPCM_DMA_INTR_ENAB_RIE | \
                                     NPCM_DMA_INTR_ENAB_ERE)
#define NPCM_DMA_INTR_ENAB_AIE BIT(15)
#define NPCM_DMA_INTR_ENAB_AIE_BITS (NPCM_DMA_INTR_ENAB_TSE | \
                                     NPCM_DMA_INTR_ENAB_TJE | \
                                     NPCM_DMA_INTR_ENAB_OVE | \
                                     NPCM_DMA_INTR_ENAB_UNE | \
                                     NPCM_DMA_INTR_ENAB_RUE | \
                                     NPCM_DMA_INTR_ENAB_RSE | \
                                     NPCM_DMA_INTR_ENAB_RWE | \
                                     NPCM_DMA_INTR_ENAB_ETE | \
                                     NPCM_DMA_INTR_ENAB_FBE)

#define NPCM_DMA_CONTROL_FLUSH_MASK BIT(24)
#define NPCM_DMA_CONTROL_START_STOP_TX BIT(13)
#define NPCM_DMA_CONTROL_START_STOP_RX BIT(1)
#define NPCM_DMA_HOST_RX_DESC_MASK(word) ((uint32_t) (word) & ~3u)
#define NPCM_DMA_HOST_TX_DESC_MASK(word) ((uint32_t) (word) & ~3u)

#define NPCM_GMAC_MAC_CONFIG_RX_EN BIT(2)
#define NPCM_GMAC_MAC_CONFIG_TX_EN BIT(3)

#define NPCM_GMAC_FRAME_FILTER_REC_ALL_MASK BIT(31)
#define NPCM_GMAC_FRAME_FILTER_HPF_MASK BIT(10)
#define NPCM_GMAC_FRAME_FILTER_SAF_MASK BIT(9)
#define NPCM_GMAC_FRAME_FILTER_SAIF_MASK BIT(8)
#define NPCM_GMAC_FRAME_FILTER_PCF_MASK BIT(word) extract32((word), 6, 2)
#define NPCM_GMAC_FRAME_FILTER_DBF_MASK BIT(5)
#define NPCM_GMAC_FRAME_FILTER_PM_MASK BIT(4)
#define NPCM_GMAC_FRAME_FILTER_DAIF_MASK BIT(3)
#define NPCM_GMAC_FRAME_FILTER_HMC_MASK BIT(2)
#define NPCM_GMAC_FRAME_FILTER_HUC_MASK BIT(1)
#define NPCM_GMAC_FRAME_FILTER_PR_MASK BIT(0)

#endif
