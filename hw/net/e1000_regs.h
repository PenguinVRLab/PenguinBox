/*******************************************************************************

  Intel PRO/1000 Linux driver
  Copyright(c) 1999 - 2006 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, see <http://www.gnu.org/licenses/>.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  Linux NICS <linux.nics@intel.com>
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

/* e1000_hw.h
 * Structures, enums, and macros for the MAC
 */

#ifndef HW_E1000_REGS_H
#define HW_E1000_REGS_H

#include "e1000x_regs.h"

#define E1000_ITR      0x000C4
#define E1000_EIAC     0x000DC
#define E1000_IVAR     0x000E4
#define E1000_EITR     0x000E8
#define E1000_RDBAL1   0x02900
#define E1000_RDBAH1   0x02904
#define E1000_RDLEN1   0x02908
#define E1000_RDH1     0x02910
#define E1000_RDT1     0x02918
#define E1000_FCRTV    0x05F40
#define E1000_TXCW     0x00178
#define E1000_RXCW     0x00180
#define E1000_TBT      0x00448
#define E1000_AIT      0x00458
#define E1000_EXTCNF_CTRL  0x00F00
#define E1000_EXTCNF_SIZE  0x00F08
#define E1000_PHY_CTRL     0x00F10
#define E1000_PBA      0x01000
#define E1000_PBM      0x10000
#define E1000_PBS      0x01008
#define E1000_FLASHT   0x01028
#define E1000_EEWR     0x0102C
#define E1000_FLSWCTL  0x01030
#define E1000_FLSWDATA 0x01034
#define E1000_FLSWCNT  0x01038
#define E1000_FLOL     0x01050
#define E1000_ERT      0x02008
#define E1000_FCRTH_A  0x00160
#define E1000_PSRCTL   0x02170
#define E1000_RDBAL    0x02800
#define E1000_RDBAH    0x02804
#define E1000_RDLEN    0x02808
#define E1000_RDH      0x02810
#define E1000_RDT      0x02818
#define E1000_RDTR     0x02820
#define E1000_RDTR_A   0x00108
#define E1000_RDBAL0   E1000_RDBAL
#define E1000_RDBAL0_A 0x00110
#define E1000_RDBAH0   E1000_RDBAH
#define E1000_RDBAH0_A 0x00114
#define E1000_RDLEN0   E1000_RDLEN
#define E1000_RDLEN0_A 0x00118
#define E1000_RDH0     E1000_RDH
#define E1000_RDH0_A   0x00120
#define E1000_RDT0     E1000_RDT
#define E1000_RDT0_A   0x00128
#define E1000_RDTR0    E1000_RDTR
#define E1000_RXDCTL   0x02828
#define E1000_RXDCTL1  0x02928
#define E1000_RADV     0x0282C
#define E1000_RSRPD    0x02C00
#define E1000_RAID     0x02C08
#define E1000_POEMB    0x00F10
#define E1000_TDBAL    0x03800
#define E1000_TDBAL_A  0x00420
#define E1000_TDBAH    0x03804
#define E1000_TDBAH_A  0x00424
#define E1000_TDLEN    0x03808
#define E1000_TDLEN_A  0x00428
#define E1000_TDH      0x03810
#define E1000_TDH_A    0x00430
#define E1000_TDT      0x03818
#define E1000_TDT_A    0x00438
#define E1000_TIDV     0x03820
#define E1000_TIDV_A   0x00440
#define E1000_TXDCTL   0x03828
#define E1000_TADV     0x0382C
#define E1000_TSPMT    0x03830
#define E1000_TARC0    0x03840
#define E1000_TDBAL1   0x03900
#define E1000_TDBAH1   0x03904
#define E1000_TDLEN1   0x03908
#define E1000_TDH1     0x03910
#define E1000_TDT1     0x03918
#define E1000_TXDCTL1  0x03928
#define E1000_TARC1    0x03940
#define E1000_SEQEC    0x04038
#define E1000_CEXTERR  0x0403C
#define E1000_TSCTFC   0x040FC
#define E1000_ICRXATC  0x04108
#define E1000_ICTXPTC  0x0410C
#define E1000_ICTXATC  0x04110
#define E1000_ICTXQEC  0x04118
#define E1000_ICTXQMTC 0x0411C
#define E1000_ICRXOC   0x04124
#define E1000_MFUTP01  0x05828
#define E1000_MFUTP23  0x05830
#define E1000_FFLT     0x05F00
#define E1000_HOST_IF  0x08800
#define E1000_FFVT     0x09800

#define E1000_KUMCTRLSTA 0x00034
#define E1000_MDPHYA     0x0003C

#define E1000_GCR2      0x05B64
#define E1000_FFLT_DBG  0x05F04
#define E1000_HICR      0x08F00

#define E1000_RXMTRL     0x0B634
#define E1000_RXUDP      0x0B638
#define E1000_RXCFGL     0x0B634

#define E1000_MRQC_ENABLED(mrqc) (((mrqc) & (BIT(0) | BIT(1))) == BIT(0))

#define E1000_CPUVEC    0x02C10
#define E1000_RSSIM     0x05864
#define E1000_RSSIR     0x05868

#define E1000_RSS_QUEUE(reta, hash) ((E1000_RETA_VAL(reta, hash) & BIT(7)) >> 7)

#define E1000_XDBAL_MASK            (~(BIT(4) - 1))
#define E1000_XDLEN_MASK            ((BIT(20) - 1) & (~(BIT(7) - 1)))

#define E1000_IVAR_INT_ALLOC_VALID  (0x8)

#define E1000_IVAR_RXQ0_SHIFT       (0)
#define E1000_IVAR_RXQ1_SHIFT       (4)
#define E1000_IVAR_TXQ0_SHIFT       (8)
#define E1000_IVAR_TXQ1_SHIFT       (12)
#define E1000_IVAR_OTHER_SHIFT      (16)

#define E1000_IVAR_ENTRY_MASK       (0xF)
#define E1000_IVAR_ENTRY_VALID_MASK E1000_IVAR_INT_ALLOC_VALID
#define E1000_IVAR_ENTRY_VEC_MASK   (0x7)

#define E1000_IVAR_RXQ0(x)          ((x) >> E1000_IVAR_RXQ0_SHIFT)
#define E1000_IVAR_RXQ1(x)          ((x) >> E1000_IVAR_RXQ1_SHIFT)
#define E1000_IVAR_TXQ0(x)          ((x) >> E1000_IVAR_TXQ0_SHIFT)
#define E1000_IVAR_TXQ1(x)          ((x) >> E1000_IVAR_TXQ1_SHIFT)
#define E1000_IVAR_OTHER(x)         ((x) >> E1000_IVAR_OTHER_SHIFT)

#define E1000_IVAR_ENTRY_VALID(x)   ((x) & E1000_IVAR_ENTRY_VALID_MASK)
#define E1000_IVAR_ENTRY_VEC(x)     ((x) & E1000_IVAR_ENTRY_VEC_MASK)

#define E1000_IVAR_TX_INT_EVERY_WB  BIT(31)

#define E1000_RFCTL_ACK_DIS             0x00001000
#define E1000_RFCTL_ACK_DATA_DIS        0x00002000

#define E1000_PSRCTL_BSIZE0_MASK   0x0000007F
#define E1000_PSRCTL_BSIZE1_MASK   0x00003F00
#define E1000_PSRCTL_BSIZE2_MASK   0x003F0000
#define E1000_PSRCTL_BSIZE3_MASK   0x3F000000

#define E1000_PSRCTL_BSIZE0_SHIFT  0
#define E1000_PSRCTL_BSIZE1_SHIFT  8
#define E1000_PSRCTL_BSIZE2_SHIFT  16
#define E1000_PSRCTL_BSIZE3_SHIFT  24

#define E1000_PSRCTL_BUFFS_PER_DESC 4

#define PHY_COPPER_CTRL1      0x10
#define PHY_COPPER_STAT1      0x11
#define PHY_COPPER_INT_ENABLE 0x12
#define PHY_COPPER_STAT2      0x13
#define PHY_COPPER_CTRL3      0x14
#define PHY_COPPER_CTRL2      0x1A
#define PHY_RX_ERR_CNTR       0x15
#define PHY_PAGE              0x16
#define PHY_OEM_BITS          0x19
#define PHY_BIAS_1            0x1d
#define PHY_BIAS_2            0x1e

#define PHY_MAC_CTRL1         0x10
#define PHY_MAC_INT_ENABLE    0x12
#define PHY_MAC_STAT          0x13
#define PHY_MAC_CTRL2         0x15

#define PHY_LED_03_FUNC_CTRL1 0x10
#define PHY_LED_03_POL_CTRL   0x11
#define PHY_LED_TIMER_CTRL    0x12
#define PHY_LED_45_CTRL       0x13

#define PHY_1000T_SKEW        0x14
#define PHY_1000T_SWAP        0x15

#define PHY_CRC_COUNTERS      0x11

#define PHY_PAGE_RW_MASK 0x7F

#define MAX_PHY_REG_ADDRESS        0x1F
#define MAX_PHY_MULTI_PAGE_REG     0xF

#define M88E1000_PHY_SPEC_CTRL     0x10
#define M88E1000_PHY_SPEC_STATUS   0x11
#define M88E1000_INT_ENABLE        0x12
#define M88E1000_INT_STATUS        0x13
#define M88E1000_EXT_PHY_SPEC_CTRL 0x14
#define M88E1000_RX_ERR_CNTR       0x15

#define M88E1000_PHY_EXT_CTRL      0x1A
#define M88E1000_PHY_PAGE_SELECT   0x1D
#define M88E1000_PHY_GEN_CONTROL   0x1E
#define M88E1000_PHY_VCO_REG_BIT8  0x100
#define M88E1000_PHY_VCO_REG_BIT11 0x800

#define E1000_STATUS_FUNC_MASK  0x0000000C
#define E1000_STATUS_FUNC_SHIFT 2
#define E1000_STATUS_FUNC_0     0x00000000
#define E1000_STATUS_FUNC_1     0x00000004
#define E1000_STATUS_TXOFF      0x00000010
#define E1000_STATUS_TBIMODE    0x00000020
#define E1000_STATUS_SPEED_MASK 0x000000C0
#define E1000_STATUS_LAN_INIT_DONE 0x00000200
#define E1000_STATUS_ASDV       0x00000300
#define E1000_STATUS_ASDV_10    0x00000000
#define E1000_STATUS_ASDV_100   0x00000100
#define E1000_STATUS_ASDV_1000  0x00000200
#define E1000_STATUS_DOCK_CI    0x00000800
#define E1000_STATUS_MTXCKOK    0x00000400
#define E1000_STATUS_PCI66      0x00000800
#define E1000_STATUS_BUS64      0x00001000
#define E1000_STATUS_PCIX_MODE  0x00002000
#define E1000_STATUS_PCIX_SPEED 0x0000C000
#define E1000_STATUS_BMC_SKU_0  0x00100000
#define E1000_STATUS_BMC_SKU_1  0x00200000
#define E1000_STATUS_BMC_SKU_2  0x00400000
#define E1000_STATUS_BMC_CRYPTO 0x00800000
#define E1000_STATUS_BMC_LITE   0x01000000
#define E1000_STATUS_RGMII_ENABLE 0x02000000
#define E1000_STATUS_FUSE_8       0x04000000
#define E1000_STATUS_FUSE_9       0x08000000
#define E1000_STATUS_SERDES0_DIS  0x10000000
#define E1000_STATUS_SERDES1_DIS  0x20000000
#define E1000_STATUS_SPEED_SHIFT  6
#define E1000_STATUS_ASDV_SHIFT   8

struct e1000_tx_desc {
    uint64_t buffer_addr;
    union {
        uint32_t data;
        struct {
            uint16_t length;
            uint8_t cso;
            uint8_t cmd;
        } flags;
    } lower;
    union {
        uint32_t data;
        struct {
            uint8_t status;
            uint8_t css;
            uint16_t special;
        } fields;
    } upper;
};

#define E1000_TXD_POPTS_IXSM 0x01
#define E1000_TXD_POPTS_TXSM 0x02

#endif
