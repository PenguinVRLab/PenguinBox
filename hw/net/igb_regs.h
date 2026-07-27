/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This is copied + edited from kernel header files in
 * drivers/net/ethernet/intel/igb
 */

#ifndef HW_IGB_REGS_H_
#define HW_IGB_REGS_H_

#include "e1000x_regs.h"

#define E1000_DEV_ID_82576                 0x10C9
#define E1000_DEV_ID_82576_FIBER           0x10E6
#define E1000_DEV_ID_82576_SERDES          0x10E7
#define E1000_DEV_ID_82576_QUAD_COPPER             0x10E8
#define E1000_DEV_ID_82576_QUAD_COPPER_ET2 0x1526
#define E1000_DEV_ID_82576_NS                      0x150A
#define E1000_DEV_ID_82576_NS_SERDES               0x1518
#define E1000_DEV_ID_82576_SERDES_QUAD             0x150D

struct e1000_adv_tx_context_desc {
    uint32_t vlan_macip_lens;
    uint32_t seqnum_seed;
    uint32_t type_tucmd_mlhl;
    uint32_t mss_l4len_idx;
};

union e1000_adv_tx_desc {
    struct {
        uint64_t buffer_addr;
        uint32_t cmd_type_len;
        uint32_t olinfo_status;
    } read;
    struct {
        uint64_t rsvd;
        uint32_t nxtseq_seed;
        uint32_t status;
    } wb;
};

#define E1000_ADVTXD_POTS_IXSM  0x00000100
#define E1000_ADVTXD_POTS_TXSM  0x00000200

#define E1000_TXD_POPTS_IXSM 0x00000001
#define E1000_TXD_POPTS_TXSM 0x00000002

union e1000_adv_rx_desc {
    struct {
        uint64_t pkt_addr;
        uint64_t hdr_addr;
    } read;
    struct {
        struct {
            struct {
                uint16_t pkt_info;
                uint16_t hdr_info;
            } lo_dword;
            union {
                uint32_t rss;
                struct {
                        uint16_t ip_id;
                        uint16_t csum;
                } csum_ip;
            } hi_dword;
        } lower;
        struct {
            uint32_t status_error;
            uint16_t length;
            uint16_t vlan;
        } upper;
    } wb;
};

#define IGP01E1000_PHY_PORT_CONFIG        0x10
#define IGP01E1000_PHY_PORT_STATUS        0x11
#define IGP01E1000_PHY_PORT_CTRL          0x12
#define IGP01E1000_PHY_LINK_HEALTH        0x13
#define IGP02E1000_PHY_POWER_MGMT         0x19
#define IGP01E1000_PHY_PAGE_SELECT        0x1F
#define IGP01E1000_PHY_PCS_INIT_REG       0x00B4
#define IGP01E1000_PHY_POLARITY_MASK      0x0078
#define IGP01E1000_PSCR_AUTO_MDIX         0x1000
#define IGP01E1000_PSCR_FORCE_MDI_MDIX    0x2000
#define IGP01E1000_PSCFR_SMART_SPEED      0x0080

#define IGP02E1000_PM_D0_LPLU             0x0002
#define IGP02E1000_PM_D3_LPLU             0x0004
#define IGP01E1000_PLHR_SS_DOWNGRADE      0x8000
#define IGP01E1000_PSSR_POLARITY_REVERSED 0x0002
#define IGP01E1000_PSSR_MDIX              0x0800
#define IGP01E1000_PSSR_SPEED_MASK        0xC000
#define IGP01E1000_PSSR_SPEED_1000MBPS    0xC000
#define IGP02E1000_PHY_CHANNEL_NUM        4
#define IGP02E1000_PHY_AGC_A              0x11B1
#define IGP02E1000_PHY_AGC_B              0x12B1
#define IGP02E1000_PHY_AGC_C              0x14B1
#define IGP02E1000_PHY_AGC_D              0x18B1
#define IGP02E1000_AGC_LENGTH_SHIFT       9
#define IGP02E1000_AGC_LENGTH_MASK        0x7F
#define IGP02E1000_AGC_RANGE              15

#define E1000_PCS_CFG_IGN_SD     1

#define IGB_START_ITR            648
#define IGB_4K_ITR               980
#define IGB_20K_ITR              196
#define IGB_70K_ITR              56

#define IGB_DEFAULT_TXD          256
#define IGB_DEFAULT_TX_WORK      128
#define IGB_MIN_TXD              80
#define IGB_MAX_TXD              4096

#define IGB_DEFAULT_RXD          256
#define IGB_MIN_RXD              80
#define IGB_MAX_RXD              4096

#define IGB_DEFAULT_ITR           3
#define IGB_MAX_ITR_USECS         10000
#define IGB_MIN_ITR_USECS         10
#define NON_Q_VECTORS             1
#define MAX_Q_VECTORS             8
#define MAX_MSIX_ENTRIES          10

#define IGB_MAX_RX_QUEUES          8
#define IGB_MAX_RX_QUEUES_82575    4
#define IGB_MAX_RX_QUEUES_I211     2
#define IGB_MAX_TX_QUEUES          8
#define IGB_MAX_VF_MC_ENTRIES      30
#define IGB_MAX_VF_FUNCTIONS       8
#define IGB_MAX_VFTA_ENTRIES       128
#define IGB_82576_VF_DEV_ID        0x10CA
#define IGB_I350_VF_DEV_ID         0x1520

#define IGB_TX_FLAGS_VLAN_MASK     0xffff0000
#define IGB_TX_FLAGS_VLAN_SHIFT    16

#define E1000_MRQC_ENABLE_RSS_MQ            0x00000002
#define E1000_MRQC_ENABLE_VMDQ              0x00000003
#define E1000_MRQC_RSS_FIELD_IPV4_UDP       0x00400000
#define E1000_MRQC_ENABLE_VMDQ_RSS_MQ       0x00000005
#define E1000_MRQC_RSS_FIELD_IPV6_UDP       0x00800000
#define E1000_MRQC_RSS_FIELD_IPV6_UDP_EX    0x01000000

#define E1000_ADVTXD_MAC_TSTAMP   0x00080000
#define E1000_ADVTXD_DTYP_CTXT    0x00200000
#define E1000_ADVTXD_DTYP_DATA    0x00300000
#define E1000_ADVTXD_DCMD_EOP     0x01000000
#define E1000_ADVTXD_DCMD_IFCS    0x02000000
#define E1000_ADVTXD_DCMD_RS      0x08000000
#define E1000_ADVTXD_DCMD_DEXT    0x20000000
#define E1000_ADVTXD_DCMD_VLE     0x40000000
#define E1000_ADVTXD_DCMD_TSE     0x80000000
#define E1000_ADVTXD_PAYLEN_SHIFT    14

#define E1000_ADVTXD_MACLEN_SHIFT    9
#define E1000_ADVTXD_TUCMD_L4T_UDP 0x00000000
#define E1000_ADVTXD_TUCMD_IPV4    0x00000400
#define E1000_ADVTXD_TUCMD_L4T_TCP 0x00000800
#define E1000_ADVTXD_TUCMD_L4T_SCTP 0x00001000
#define E1000_ADVTXD_L4LEN_SHIFT     8
#define E1000_ADVTXD_MSS_SHIFT      16

#define E1000_TXDCTL_QUEUE_ENABLE  0x02000000

#define E1000_RXDCTL_QUEUE_ENABLE  0x02000000

#define E1000_DCA_CTRL_DCA_MODE_DISABLE 0x01
#define E1000_DCA_CTRL_DCA_MODE_CB2     0x02

#define E1000_DCA_RXCTRL_CPUID_MASK 0x0000001F
#define E1000_DCA_RXCTRL_DESC_DCA_EN BIT(5)
#define E1000_DCA_RXCTRL_HEAD_DCA_EN BIT(6)
#define E1000_DCA_RXCTRL_DATA_DCA_EN BIT(7)
#define E1000_DCA_RXCTRL_DESC_RRO_EN BIT(9)

#define E1000_DCA_TXCTRL_CPUID_MASK 0x0000001F
#define E1000_DCA_TXCTRL_DESC_DCA_EN BIT(5)
#define E1000_DCA_TXCTRL_DESC_RRO_EN BIT(9)
#define E1000_DCA_TXCTRL_TX_WB_RO_EN BIT(11)
#define E1000_DCA_TXCTRL_DATA_RRO_EN BIT(13)

#define E1000_DCA_TXCTRL_CPUID_MASK_82576 0xFF000000
#define E1000_DCA_RXCTRL_CPUID_MASK_82576 0xFF000000
#define E1000_DCA_TXCTRL_CPUID_SHIFT 24
#define E1000_DCA_RXCTRL_CPUID_SHIFT 24

#define E1000_ETQF_FILTER_ENABLE   BIT(26)
#define E1000_ETQF_1588            BIT(30)
#define E1000_ETQF_IMM_INT         BIT(29)
#define E1000_ETQF_QUEUE_ENABLE    BIT(31)
#define E1000_ETQF_QUEUE_SHIFT     16
#define E1000_ETQF_QUEUE_MASK      0x00070000
#define E1000_ETQF_ETYPE_MASK      0x0000FFFF

#define E1000_DTXSWC_MAC_SPOOF_MASK   0x000000FF
#define E1000_DTXSWC_VLAN_SPOOF_MASK  0x0000FF00
#define E1000_DTXSWC_LLE_MASK         0x00FF0000
#define E1000_DTXSWC_VLAN_SPOOF_SHIFT 8
#define E1000_DTXSWC_VMDQ_LOOPBACK_EN BIT(31)

#define E1000_VT_CTL_DEFAULT_POOL_SHIFT 7
#define E1000_VT_CTL_DEFAULT_POOL_MASK  (0x7 << E1000_VT_CTL_DEFAULT_POOL_SHIFT)

#define E1000_VT_CTL_IGNORE_MAC         BIT(28)
#define E1000_VT_CTL_DISABLE_DEF_POOL   BIT(29)
#define E1000_VT_CTL_VM_REPL_EN         BIT(30)

#define E1000_VMOLR_RLPML_MASK 0x00003FFF
#define E1000_VMOLR_LPE        0x00010000
#define E1000_VMOLR_RSSE       0x00020000
#define E1000_VMOLR_AUPE       0x01000000
#define E1000_VMOLR_ROMPE      0x02000000
#define E1000_VMOLR_ROPE       0x04000000
#define E1000_VMOLR_BAM        0x08000000
#define E1000_VMOLR_MPME       0x10000000
#define E1000_VMOLR_STRVLAN    0x40000000
#define E1000_VMOLR_STRCRC     0x80000000

#define E1000_DVMOLR_HIDEVLAN  0x20000000
#define E1000_DVMOLR_STRVLAN   0x40000000
#define E1000_DVMOLR_STRCRC    0x80000000

#define E1000_VLVF_ARRAY_SIZE     32
#define E1000_VLVF_VLANID_MASK    0x00000FFF
#define E1000_VLVF_POOLSEL_SHIFT  12
#define E1000_VLVF_POOLSEL_MASK   (0xFF << E1000_VLVF_POOLSEL_SHIFT)
#define E1000_VLVF_LVLAN          0x00100000
#define E1000_VLVF_VLANID_ENABLE  0x80000000

#define E1000_VMVIR_VLANA_DEFAULT      0x40000000
#define E1000_VMVIR_VLANA_NEVER        0x80000000

#define E1000_IOVCTL 0x05BBC
#define E1000_IOVCTL_REUSE_VFQ 0x00000001

#define E1000_RPLOLR_STRVLAN   0x40000000
#define E1000_RPLOLR_STRCRC    0x80000000

#define E1000_DTXCTL_8023LL     0x0004
#define E1000_DTXCTL_VLAN_ADDED 0x0008
#define E1000_DTXCTL_OOS_ENABLE 0x0010
#define E1000_DTXCTL_MDP_EN     0x0020
#define E1000_DTXCTL_SPOOF_INT  0x0040

#define E1000_CTRL_EXT_PFRSTD   0x00004000

#define E1000_IVAR_VALID     0x80
#define E1000_GPIE_NSICR     0x00000001
#define E1000_GPIE_MSIX_MODE 0x00000010
#define E1000_GPIE_EIAME     0x40000000
#define E1000_GPIE_PBA       0x80000000

#define E1000_TCTL_EN     0x00000002
#define E1000_TCTL_PSP    0x00000008
#define E1000_TCTL_CT     0x00000ff0
#define E1000_TCTL_COLD   0x003ff000
#define E1000_TCTL_RTLC   0x01000000

#define E1000_COLLISION_THRESHOLD       15
#define E1000_CT_SHIFT                  4
#define E1000_COLLISION_DISTANCE        63
#define E1000_COLD_SHIFT                12

#define E1000_RAH_POOL_MASK 0x03FC0000
#define E1000_RAH_POOL_1 0x00040000

#define E1000_ICR_VMMB         0x00000100
#define E1000_ICR_TS           0x00080000
#define E1000_ICR_DRSTA        0x40000000
#define E1000_ICR_INT_ASSERTED 0x80000000
#define E1000_ICR_DOUTSYNC     0x10000000

#define E1000_EICR_RX_QUEUE0    0x00000001
#define E1000_EICR_RX_QUEUE1    0x00000002
#define E1000_EICR_RX_QUEUE2    0x00000004
#define E1000_EICR_RX_QUEUE3    0x00000008
#define E1000_EICR_TX_QUEUE0    0x00000100
#define E1000_EICR_TX_QUEUE1    0x00000200
#define E1000_EICR_TX_QUEUE2    0x00000400
#define E1000_EICR_TX_QUEUE3    0x00000800
#define E1000_EICR_OTHER        0x80000000

#define E1000_EITR_CNT_IGNR     0x80000000

#define E1000_TSYNCTXCTL_VALID    0x00000001
#define E1000_TSYNCTXCTL_ENABLED  0x00000010

#define E1000_GCR_CMPL_TMOUT_MASK       0x0000F000
#define E1000_GCR_CMPL_TMOUT_10ms       0x00001000
#define E1000_GCR_CMPL_TMOUT_RESEND     0x00010000
#define E1000_GCR_CAP_VER2              0x00040000

#define PHY_REVISION_MASK      0xFFFFFFF0
#define MAX_PHY_REG_ADDRESS    0x1F
#define MAX_PHY_MULTI_PAGE_REG 0xF

#define IGP03E1000_E_PHY_ID 0x02A80390

#define E1000_P2VMAILBOX_STS  0x00000001
#define E1000_P2VMAILBOX_ACK  0x00000002
#define E1000_P2VMAILBOX_VFU  0x00000004
#define E1000_P2VMAILBOX_PFU  0x00000008
#define E1000_P2VMAILBOX_RVFU 0x00000010

#define E1000_MBVFICR_VFREQ_MASK 0x000000FF
#define E1000_MBVFICR_VFREQ_VF1  0x00000001
#define E1000_MBVFICR_VFACK_MASK 0x00FF0000
#define E1000_MBVFICR_VFACK_VF1  0x00010000

#define E1000_V2PMAILBOX_SIZE 16

#define E1000_VT_MSGTYPE_ACK 0x80000000
#define E1000_VT_MSGTYPE_NACK 0x40000000
#define E1000_VT_MSGTYPE_CTS 0x20000000
#define E1000_VT_MSGINFO_SHIFT 16
#define E1000_VT_MSGINFO_MASK (0xFF << E1000_VT_MSGINFO_SHIFT)

#define E1000_VF_RESET                 0x01
#define E1000_VF_SET_MAC_ADDR          0x02
#define E1000_VF_MAC_FILTER_CLR        (0x01 << E1000_VT_MSGINFO_SHIFT)
#define E1000_VF_MAC_FILTER_ADD        (0x02 << E1000_VT_MSGINFO_SHIFT)
#define E1000_VF_SET_MULTICAST         0x03
#define E1000_VF_SET_VLAN              0x04
#define E1000_VF_SET_LPE               0x05
#define E1000_VF_SET_PROMISC           0x06
#define E1000_VF_SET_PROMISC_MULTICAST (0x02 << E1000_VT_MSGINFO_SHIFT)

#define E1000_PF_CONTROL_MSG 0x0100

#define E1000_EICR      0x01580
#define E1000_EITR(_n)  (0x01680 + (0x4 * (_n)))
#define E1000_EICS      0x01520
#define E1000_EIMS      0x01524
#define E1000_EIMC      0x01528
#define E1000_EIAC      0x0152C
#define E1000_EIAM      0x01530
#define E1000_GPIE      0x01514
#define E1000_IVAR0     0x01700
#define E1000_IVAR_MISC 0x01740
#define E1000_FRTIMER   0x01048
#define E1000_FCRTV     0x02460

#define E1000_TSYNCRXCFG 0x05F50

#define E1000_SAQF(_n) (0x5980 + 4 * (_n))
#define E1000_DAQF(_n) (0x59A0 + 4 * (_n))
#define E1000_SPQF(_n) (0x59C0 + 4 * (_n))
#define E1000_FTQF(_n) (0x59E0 + 4 * (_n))
#define E1000_SAQF0 E1000_SAQF(0)
#define E1000_DAQF0 E1000_DAQF(0)
#define E1000_SPQF0 E1000_SPQF(0)
#define E1000_FTQF0 E1000_FTQF(0)
#define E1000_SYNQF(_n) (0x055FC + (4 * (_n)))
#define E1000_ETQF(_n)  (0x05CB0 + (4 * (_n)))

#define E1000_RQDPC(_n) (0x0C030 + ((_n) * 0x40))

#define E1000_RXPBS 0x02404
#define E1000_TXPBS 0x03404

#define E1000_DTXCTL 0x03590

#define E1000_HTCBDPC     0x04124
#define E1000_RLPML       0x05004
#define E1000_RA2         0x054E0
#define E1000_PSRTYPE(_i) (0x05480 + ((_i) * 4))
#define E1000_VT_CTL   0x0581C

#define E1000_MBVFICR   0x00C80
#define E1000_MBVFIMR   0x00C84
#define E1000_VFLRE     0x00C88
#define E1000_VFRE      0x00C8C
#define E1000_VFTE      0x00C90
#define E1000_QDE       0x02408
#define E1000_DTXSWC    0x03500
#define E1000_WVBR      0x03554
#define E1000_RPLOLR    0x05AF0
#define E1000_UTA       0x0A000
#define E1000_IOVTCL    0x05BBC
#define E1000_TXSWC     0x05ACC
#define E1000_LVMMC     0x03548
#define E1000_P2VMAILBOX(_n)   (0x00C00 + (4 * (_n)))
#define E1000_VMBMEM(_n)       (0x00800 + (64 * (_n)))
#define E1000_VMOLR(_n)        (0x05AD0 + (4 * (_n)))
#define E1000_DVMOLR(_n)       (0x0C038 + (64 * (_n)))
#define E1000_VLVF(_n)         (0x05D00 + (4 * (_n)))
#define E1000_VMVIR(_n)        (0x03700 + (4 * (_n)))

#define E1000_SRRCTL_BSIZEPKT_SHIFT            10
#define E1000_SRRCTL_BSIZEHDRSIZE_MASK         0x00000F00
#define E1000_SRRCTL_BSIZEHDRSIZE_SHIFT        2
#define E1000_SRRCTL_DESCTYPE_ADV_ONEBUF       0x02000000
#define E1000_SRRCTL_DESCTYPE_HDR_SPLIT        0x04000000
#define E1000_SRRCTL_DESCTYPE_HDR_SPLIT_ALWAYS 0x0A000000
#define E1000_SRRCTL_DESCTYPE_MASK             0x0E000000
#define E1000_SRRCTL_DROP_EN                   0x80000000

#define E1000_SRRCTL_BSIZEPKT_MASK             0x0000007F
#define E1000_SRRCTL_BSIZEHDR_MASK             0x00003F00

#define E1000_V2PMAILBOX_REQ      0x00000001
#define E1000_V2PMAILBOX_ACK      0x00000002
#define E1000_V2PMAILBOX_VFU      0x00000004
#define E1000_V2PMAILBOX_PFU      0x00000008
#define E1000_V2PMAILBOX_PFSTS    0x00000010
#define E1000_V2PMAILBOX_PFACK    0x00000020
#define E1000_V2PMAILBOX_RSTI     0x00000040
#define E1000_V2PMAILBOX_RSTD     0x00000080
#define E1000_V2PMAILBOX_R2C_BITS 0x000000B0

#define E1000_VFMAILBOX_SIZE      16

#define E1000_VT_MSGTYPE_ACK      0x80000000
#define E1000_VT_MSGTYPE_NACK     0x40000000
#define E1000_VT_MSGTYPE_CTS      0x20000000

#define E1000_VF_MBX_INIT_TIMEOUT 2000
#define E1000_VF_MBX_INIT_DELAY   500

#define E1000_VT_MSGINFO_SHIFT    16
#define E1000_VT_MSGINFO_MASK     (0xFF << E1000_VT_MSGINFO_SHIFT)

#define E1000_VF_RESET            0x01
#define E1000_VF_SET_MAC_ADDR     0x02
#define E1000_VF_MAC_FILTER_CLR   (0x01 << E1000_VT_MSGINFO_SHIFT)
#define E1000_VF_MAC_FILTER_ADD   (0x02 << E1000_VT_MSGINFO_SHIFT)
#define E1000_VF_SET_MULTICAST    0x03
#define E1000_VF_SET_VLAN         0x04
#define E1000_VF_SET_LPE          0x05

#define E1000_PF_CONTROL_MSG      0x0100

#define E1000_VFGPRC   0x00F10
#define E1000_VFGORC   0x00F18
#define E1000_VFMPRC   0x00F3C
#define E1000_VFGPTC   0x00F14
#define E1000_VFGOTC   0x00F34
#define E1000_VFGOTLBC 0x00F50
#define E1000_VFGPTLBC 0x00F44
#define E1000_VFGORLBC 0x00F48
#define E1000_VFGPRLBC 0x00F40

#define E1000_V2PMAILBOX(_n) (0x00C40 + (4 * (_n)))
#define E1000_VMBMEM(_n)     (0x00800 + (64 * (_n)))

#define E1000_DEV_ID_82576_VF 0x10CA

#define E1000_RDBAL(_n)    (0x0C000 + (0x40  * (_n)))
#define E1000_RDBAL_A(_n)  (0x02800 + (0x100 * (_n)))

#define E1000_RDBAH(_n)    (0x0C004 + (0x40  * (_n)))
#define E1000_RDBAH_A(_n)  (0x02804 + (0x100 * (_n)))

#define E1000_RDLEN(_n)    (0x0C008 + (0x40  * (_n)))
#define E1000_RDLEN_A(_n)  (0x02808 + (0x100 * (_n)))

#define E1000_SRRCTL(_n)   (0x0C00C + (0x40  * (_n)))
#define E1000_SRRCTL_A(_n) (0x0280C + (0x100 * (_n)))

#define E1000_RDH(_n)      (0x0C010 + (0x40  * (_n)))
#define E1000_RDH_A(_n)    (0x02810 + (0x100 * (_n)))

#define E1000_RXCTL(_n)    (0x0C014 + (0x40  * (_n)))
#define E1000_RXCTL_A(_n)  (0x02814 + (0x100 * (_n)))

#define E1000_RDT(_n)      (0x0C018 + (0x40  * (_n)))
#define E1000_RDT_A(_n)    (0x02818 + (0x100 * (_n)))

#define E1000_RXDCTL(_n)   (0x0C028 + (0x40  * (_n)))
#define E1000_RXDCTL_A(_n) (0x02828 + (0x100 * (_n)))

#define E1000_RQDPC_A(_n)  (0x02830 + (0x100 * (_n)))

#define E1000_TDBAL(_n)    (0x0E000 + (0x40  * (_n)))
#define E1000_TDBAL_A(_n)  (0x03800 + (0x100 * (_n)))

#define E1000_TDBAH(_n)    (0x0E004 + (0x40  * (_n)))
#define E1000_TDBAH_A(_n)  (0x03804 + (0x100 * (_n)))

#define E1000_TDLEN(_n)    (0x0E008 + (0x40  * (_n)))
#define E1000_TDLEN_A(_n)  (0x03808 + (0x100 * (_n)))

#define E1000_TDH(_n)      (0x0E010 + (0x40  * (_n)))
#define E1000_TDH_A(_n)    (0x03810 + (0x100 * (_n)))

#define E1000_TXCTL(_n)    (0x0E014 + (0x40  * (_n)))
#define E1000_TXCTL_A(_n)  (0x03814 + (0x100 * (_n)))

#define E1000_TDT(_n)      (0x0E018 + (0x40  * (_n)))
#define E1000_TDT_A(_n)    (0x03818 + (0x100 * (_n)))

#define E1000_TXDCTL(_n)   (0x0E028 + (0x40  * (_n)))
#define E1000_TXDCTL_A(_n) (0x03828 + (0x100 * (_n)))

#define E1000_TDWBAL(_n)   (0x0E038 + (0x40  * (_n)))
#define E1000_TDWBAL_A(_n) (0x03838 + (0x100 * (_n)))

#define E1000_TDWBAH(_n)   (0x0E03C + (0x40  * (_n)))
#define E1000_TDWBAH_A(_n) (0x0383C + (0x100 * (_n)))

#define E1000_MTA_A        0x0200

#define E1000_XDBAL_MASK (~(BIT(5) - 1))

#define E1000_ICR_MACSEC   0x00000020
#define E1000_ICR_RX0      0x00000040
#define E1000_ICR_GPI_SDP0 0x00000800
#define E1000_ICR_GPI_SDP1 0x00001000
#define E1000_ICR_GPI_SDP2 0x00002000
#define E1000_ICR_GPI_SDP3 0x00004000
#define E1000_ICR_PTRAP    0x00008000
#define E1000_ICR_MNG      0x00040000
#define E1000_ICR_OMED     0x00100000
#define E1000_ICR_FER      0x00400000
#define E1000_ICR_NFER     0x00800000
#define E1000_ICR_CSRTO    0x01000000
#define E1000_ICR_SCE      0x02000000
#define E1000_ICR_SW_WD    0x04000000

#define E1000_EICR_MSIX_MASK   0x01FFFFFF
#define E1000_EICR_LEGACY_MASK 0x4000FFFF

#define E1000_PVTCTRL(_n) (0x10000 + (_n) * 0x100)

#define E1000_PVFGPRC(_n) (0x10010 + (_n) * 0x100)

#define E1000_PVFGPTC(_n) (0x10014 + (_n) * 0x100)

#define E1000_PVFGORC(_n) (0x10018 + (_n) * 0x100)

#define E1000_PVTEICS(_n) (0x10020 + (_n) * 0x100)

#define E1000_PVTEIMS(_n) (0x10024 + (_n) * 0x100)

#define E1000_PVTEIMC(_n) (0x10028 + (_n) * 0x100)

#define E1000_PVTEIAC(_n) (0x1002C + (_n) * 0x100)

#define E1000_PVTEIAM(_n) (0x10030 + (_n) * 0x100)

#define E1000_PVFGOTC(_n) (0x10034 + (_n) * 0x100)

#define E1000_PVFMPRC(_n) (0x1003C + (_n) * 0x100)

#define E1000_PVFGPRLBC(_n) (0x10040 + (_n) * 0x100)

#define E1000_PVFGPTLBC(_n) (0x10044 + (_n) * 0x100)

#define E1000_PVFGORLBC(_n) (0x10048 + (_n) * 0x100)

#define E1000_PVFGOTLBC(_n) (0x10050 + (_n) * 0x100)

#define E1000_PVTEICR(_n) (0x10080 + (_n) * 0x100)

#define E1000_VTIVAR      0x11700
#define E1000_VTIVAR_MISC 0x11720

#define E1000_RSS_QUEUE(reta, hash) (E1000_RETA_VAL(reta, hash) & 0x0F)

#define E1000_MRQ_RSS_TYPE_IPV4UDP 7
#define E1000_MRQ_RSS_TYPE_IPV6UDP 8

#define E1000_STATUS_IOV_MODE 0x00040000

#define E1000_STATUS_NUM_VFS_SHIFT 14

#define E1000_ADVRXD_PKT_IP4  BIT(0)
#define E1000_ADVRXD_PKT_IP6  BIT(2)
#define E1000_ADVRXD_PKT_IP6E BIT(3)
#define E1000_ADVRXD_PKT_TCP  BIT(4)
#define E1000_ADVRXD_PKT_UDP  BIT(5)
#define E1000_ADVRXD_PKT_SCTP BIT(6)

#define IGB_MAX_PS_BUFFERS 2

#define E1000_ADVRXD_HDR_LEN_OFFSET    (21 - 16)
#define E1000_ADVRXD_ADV_HDR_LEN_MASK  ((BIT(10) - 1) << \
                                        E1000_ADVRXD_HDR_LEN_OFFSET)
#define E1000_ADVRXD_HDR_SPH           BIT(15)
#define E1000_ADVRXD_ST_ERR_HBO_OFFSET BIT(3 + 20)

static inline uint8_t igb_ivar_entry_rx(uint8_t i)
{
    return i < 8 ? i * 4 : (i - 8) * 4 + 2;
}

static inline uint8_t igb_ivar_entry_tx(uint8_t i)
{
    return i < 8 ? i * 4 + 1 : (i - 8) * 4 + 3;
}

#endif
