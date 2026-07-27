/*
 * QEMU VMWARE VMXNET3 paravirtual NIC interface definitions
 *
 * Copyright (c) 2012 Ravello Systems LTD (http://ravellosystems.com)
 *
 * Developed by Daynix Computing LTD (http://www.daynix.com)
 *
 * Authors:
 * Dmitry Fleytman <dmitry@daynix.com>
 * Tamir Shomer <tamirs@daynix.com>
 * Yan Vugenfirer <yan@daynix.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_VMXNET3_H
#define QEMU_VMXNET3_H

#define VMXNET3_DEVICE_MAX_TX_QUEUES 8
#define VMXNET3_DEVICE_MAX_RX_QUEUES 8

/*
 * VMWARE headers we got from Linux kernel do not fully comply QEMU coding
 * standards in sense of types and defines used.
 * Since we didn't want to change VMWARE code, following set of typedefs
 * and defines needed to compile these headers with QEMU introduced.
 */
#define u64     uint64_t
#define u32     uint32_t
#define u16     uint16_t
#define u8      uint8_t
#define __le16  uint16_t
#define __le32  uint32_t
#define __le64  uint64_t

#if HOST_BIG_ENDIAN
#define __BIG_ENDIAN_BITFIELD
#else
#endif

/*
 * Following is an interface definition for
 * VMXNET3 device as provided by VMWARE
 * See original copyright from Linux kernel v3.2.8
 * header file drivers/net/vmxnet3/vmxnet3_defs.h below.
 */

/*
 * Linux driver for VMware's vmxnet3 ethernet NIC.
 *
 * Copyright (C) 2008-2009, VMware, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <https://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Maintained by: Shreyas Bhatewara <pv-drivers@vmware.com>
 *
 */

struct UPT1_TxStats {
    u64            TSOPktsTxOK;
    u64            TSOBytesTxOK;
    u64            ucastPktsTxOK;
    u64            ucastBytesTxOK;
    u64            mcastPktsTxOK;
    u64            mcastBytesTxOK;
    u64            bcastPktsTxOK;
    u64            bcastBytesTxOK;
    u64            pktsTxError;
    u64            pktsTxDiscard;
};

struct UPT1_RxStats {
    u64            LROPktsRxOK;
    u64            LROBytesRxOK;
    u64            ucastPktsRxOK;
    u64            ucastBytesRxOK;
    u64            mcastPktsRxOK;
    u64            mcastBytesRxOK;
    u64            bcastPktsRxOK;
    u64            bcastBytesRxOK;
    u64            pktsRxOutOfBuf;
    u64            pktsRxError;
};

enum {
    UPT1_IML_NONE        = 0,
    UPT1_IML_HIGHEST    = 7,
    UPT1_IML_ADAPTIVE    = 8,
};
enum {
    UPT1_RSS_HASH_TYPE_NONE      = 0x0,
    UPT1_RSS_HASH_TYPE_IPV4      = 0x01,
    UPT1_RSS_HASH_TYPE_TCP_IPV4  = 0x02,
    UPT1_RSS_HASH_TYPE_IPV6      = 0x04,
    UPT1_RSS_HASH_TYPE_TCP_IPV6  = 0x08,
};

enum {
    UPT1_RSS_HASH_FUNC_NONE      = 0x0,
    UPT1_RSS_HASH_FUNC_TOEPLITZ  = 0x01,
};

#define UPT1_RSS_MAX_KEY_SIZE        40
#define UPT1_RSS_MAX_IND_TABLE_SIZE  128

struct UPT1_RSSConf {
    u16            hashType;
    u16            hashFunc;
    u16            hashKeySize;
    u16            indTableSize;
    u8            hashKey[UPT1_RSS_MAX_KEY_SIZE];
    u8            indTable[UPT1_RSS_MAX_IND_TABLE_SIZE];
};

enum {
    UPT1_F_RXCSUM        = 0x0001,
    UPT1_F_RSS           = 0x0002,
    UPT1_F_RXVLAN        = 0x0004,
    UPT1_F_LRO           = 0x0008,
};

enum {
    VMXNET3_REG_VRRS    = 0x0,
    VMXNET3_REG_UVRS    = 0x8,
    VMXNET3_REG_DSAL    = 0x10,
    VMXNET3_REG_DSAH    = 0x18,
    VMXNET3_REG_CMD        = 0x20,
    VMXNET3_REG_MACL    = 0x28,
    VMXNET3_REG_MACH    = 0x30,
    VMXNET3_REG_ICR        = 0x38,
    VMXNET3_REG_ECR        = 0x40
};

enum {
    VMXNET3_REG_IMR        = 0x0,
    VMXNET3_REG_TXPROD    = 0x600,
    VMXNET3_REG_RXPROD    = 0x800,
    VMXNET3_REG_RXPROD2    = 0xA00
};

#define VMXNET3_PT_REG_SIZE     4096
#define VMXNET3_VD_REG_SIZE     4096

#define VMXNET3_REG_ALIGN       8
#define VMXNET3_REG_ALIGN_MASK  0x7

#define VMXNET3_IO_TYPE_PT              0
#define VMXNET3_IO_TYPE_VD              1
#define VMXNET3_IO_ADDR(type, reg)      (((type) << 24) | ((reg) & 0xFFFFFF))
#define VMXNET3_IO_TYPE(addr)           ((addr) >> 24)
#define VMXNET3_IO_REG(addr)            ((addr) & 0xFFFFFF)

enum {
    VMXNET3_CMD_FIRST_SET = 0xCAFE0000,
    VMXNET3_CMD_ACTIVATE_DEV = VMXNET3_CMD_FIRST_SET,
    VMXNET3_CMD_QUIESCE_DEV,
    VMXNET3_CMD_RESET_DEV,
    VMXNET3_CMD_UPDATE_RX_MODE,
    VMXNET3_CMD_UPDATE_MAC_FILTERS,
    VMXNET3_CMD_UPDATE_VLAN_FILTERS,
    VMXNET3_CMD_UPDATE_RSSIDT,
    VMXNET3_CMD_UPDATE_IML,
    VMXNET3_CMD_UPDATE_PMCFG,
    VMXNET3_CMD_UPDATE_FEATURE,
    VMXNET3_CMD_LOAD_PLUGIN,

    VMXNET3_CMD_FIRST_GET = 0xF00D0000,
    VMXNET3_CMD_GET_QUEUE_STATUS = VMXNET3_CMD_FIRST_GET,
    VMXNET3_CMD_GET_STATS,
    VMXNET3_CMD_GET_LINK,
    VMXNET3_CMD_GET_PERM_MAC_LO,
    VMXNET3_CMD_GET_PERM_MAC_HI,
    VMXNET3_CMD_GET_DID_LO,
    VMXNET3_CMD_GET_DID_HI,
    VMXNET3_CMD_GET_DEV_EXTRA_INFO,
    VMXNET3_CMD_GET_CONF_INTR,
    VMXNET3_CMD_GET_ADAPTIVE_RING_INFO
};

#define VMXNET3_DISABLE_ADAPTIVE_RING 1

struct Vmxnet3_TxDesc {
    __le64 addr;

    union {
        struct {
#ifdef __BIG_ENDIAN_BITFIELD
            u32 msscof:14;
            u32 ext1:1;
            u32 dtype:1;
            u32 rsvd:1;
            u32 gen:1;
            u32 len:14;
#else
            u32 len:14;
            u32 gen:1;
            u32 rsvd:1;
            u32 dtype:1;
            u32 ext1:1;
            u32 msscof:14;
#endif
        };
        u32 val1;
    };
    
    union {
        struct {
#ifdef __BIG_ENDIAN_BITFIELD
            u32 tci:16;
            u32 ti:1;
            u32 ext2:1;
            u32 cq:1;
            u32 eop:1;
            u32 om:2;
            u32 hlen:10;
#else
            u32 hlen:10;
            u32 om:2;
            u32 eop:1;
            u32 cq:1;
            u32 ext2:1;
            u32 ti:1;
            u32 tci:16;
#endif
        };
        u32 val2;
    };
};

#define VMXNET3_OM_NONE        0
#define VMXNET3_OM_CSUM        2
#define VMXNET3_OM_TSO        3

#define VMXNET3_TXD_EOP_SHIFT    12
#define VMXNET3_TXD_CQ_SHIFT    13
#define VMXNET3_TXD_GEN_SHIFT    14
#define VMXNET3_TXD_EOP_DWORD_SHIFT 3
#define VMXNET3_TXD_GEN_DWORD_SHIFT 2

#define VMXNET3_TXD_CQ        (1 << VMXNET3_TXD_CQ_SHIFT)
#define VMXNET3_TXD_EOP        (1 << VMXNET3_TXD_EOP_SHIFT)
#define VMXNET3_TXD_GEN        (1 << VMXNET3_TXD_GEN_SHIFT)

#define VMXNET3_HDR_COPY_SIZE   128


struct Vmxnet3_TxDataDesc {
    u8        data[VMXNET3_HDR_COPY_SIZE];
};

#define VMXNET3_TCD_GEN_SHIFT    31
#define VMXNET3_TCD_GEN_SIZE    1
#define VMXNET3_TCD_TXIDX_SHIFT    0
#define VMXNET3_TCD_TXIDX_SIZE    12
#define VMXNET3_TCD_GEN_DWORD_SHIFT    3

struct Vmxnet3_TxCompDesc {
    union {
        struct {
#ifdef __BIG_ENDIAN_BITFIELD
            u32 ext1:20;
            u32 txdIdx:12;
#else
            u32 txdIdx:12;
            u32 ext1:20;
#endif
        };
        u32 val1;
    };
    __le32        ext2;
    __le32        ext3;

    union {
        struct {
#ifdef __BIG_ENDIAN_BITFIELD
            u32 gen:1;
            u32 type:7;
            u32 rsvd:24;
#else
            u32 rsvd:24;
            u32 type:7;
            u32 gen:1;
#endif
        };
        u32 val2;
    };
};

struct Vmxnet3_RxDesc {
    __le64        addr;
    union {
        struct {
#ifdef __BIG_ENDIAN_BITFIELD
            u32 gen:1;
            u32 rsvd:15;
            u32 dtype:1;
            u32 btype:1;
            u32 len:14;
#else
            u32 len:14;
            u32 btype:1;
            u32 dtype:1;
            u32 rsvd:15;
            u32 gen:1;
#endif
        };
        u32 val1;
    };
    u32        ext1;
};

#define VMXNET3_RXD_BTYPE_HEAD   0
#define VMXNET3_RXD_BTYPE_BODY   1

#define VMXNET3_RXD_BTYPE_SHIFT  14
#define VMXNET3_RXD_GEN_SHIFT    31

struct Vmxnet3_RxCompDesc {
    union {
        struct {
#ifdef __BIG_ENDIAN_BITFIELD
            u32 ext2:1;
            u32 cnc:1;
            u32 rssType:4;
            u32 rqID:10;
            u32 sop:1;
            u32 eop:1;
            u32 ext1:2;
            u32 rxdIdx:12;
#else
            u32 rxdIdx:12;
            u32 ext1:2;
            u32 eop:1;
            u32 sop:1;
            u32 rqID:10;
            u32 rssType:4;
            u32 cnc:1;
            u32 ext2:1;
#endif
        };
        u32 val1;
    };

    __le32        rssHash;

    union {
        struct {
#ifdef __BIG_ENDIAN_BITFIELD
            u32 tci:16;
            u32 ts:1;
            u32 err:1;
            u32 len:14;
#else
            u32 len:14;
            u32 err:1;
            u32 ts:1;
            u32 tci:16;
#endif
        };
        u32 val2;
    };

    union {
        struct {
#ifdef __BIG_ENDIAN_BITFIELD
            u32 gen:1;
            u32 type:7;
            u32 fcs:1;
            u32 frg:1;
            u32 v4:1;
            u32 v6:1;
            u32 ipc:1;
            u32 tcp:1;
            u32 udp:1;
            u32 tuc:1;
            u32 csum:16;
#else
            u32 csum:16;
            u32 tuc:1;
            u32 udp:1;
            u32 tcp:1;
            u32 ipc:1;
            u32 v6:1;
            u32 v4:1;
            u32 frg:1;
            u32 fcs:1;
            u32 type:7;
            u32 gen:1;
#endif
        };
        u32 val3;
    };
};

#define VMXNET3_RCD_TUC_SHIFT    16
#define VMXNET3_RCD_IPC_SHIFT    19

#define VMXNET3_RCD_TYPE_SHIFT    56
#define VMXNET3_RCD_GEN_SHIFT    63

#define VMXNET3_RCD_CSUM_OK (1 << VMXNET3_RCD_TUC_SHIFT | \
                     1 << VMXNET3_RCD_IPC_SHIFT)
#define VMXNET3_TXD_GEN_SIZE 1
#define VMXNET3_TXD_EOP_SIZE 1

enum {
    VMXNET3_RCD_RSS_TYPE_NONE     = 0,
    VMXNET3_RCD_RSS_TYPE_IPV4     = 1,
    VMXNET3_RCD_RSS_TYPE_TCPIPV4  = 2,
    VMXNET3_RCD_RSS_TYPE_IPV6     = 3,
    VMXNET3_RCD_RSS_TYPE_TCPIPV6  = 4,
};


union Vmxnet3_GenericDesc {
    __le64                qword[2];
    __le32                dword[4];
    __le16                word[8];
    struct Vmxnet3_TxDesc        txd;
    struct Vmxnet3_RxDesc        rxd;
    struct Vmxnet3_TxCompDesc    tcd;
    struct Vmxnet3_RxCompDesc    rcd;
};

#define VMXNET3_INIT_GEN       1

#define VMXNET3_MAX_TX_BUF_SIZE  (1 << 14)

#define VMXNET3_TXD_NEEDED(size) (((size) + VMXNET3_MAX_TX_BUF_SIZE - 1) / \
                    VMXNET3_MAX_TX_BUF_SIZE)

#define VMXNET3_MAX_TXD_PER_PKT 16

#define VMXNET3_MAX_RX_BUF_SIZE  ((1 << 14) - 1)
#define VMXNET3_MIN_T0_BUF_SIZE  128
#define VMXNET3_MAX_CSUM_OFFSET  1024

#define VMXNET3_RING_BA_ALIGN   512
#define VMXNET3_RING_BA_MASK    (VMXNET3_RING_BA_ALIGN - 1)

#define VMXNET3_RING_SIZE_ALIGN 32
#define VMXNET3_RING_SIZE_MASK  (VMXNET3_RING_SIZE_ALIGN - 1)

#define VMXNET3_TX_RING_MAX_SIZE   4096
#define VMXNET3_TC_RING_MAX_SIZE   4096
#define VMXNET3_RX_RING_MAX_SIZE   4096
#define VMXNET3_RC_RING_MAX_SIZE   8192

enum {
 VMXNET3_ERR_NOEOP        = 0x80000000,
 VMXNET3_ERR_TXD_REUSE    = 0x80000001,
 VMXNET3_ERR_BIG_PKT      = 0x80000002,
 VMXNET3_ERR_DESC_NOT_SPT = 0x80000003,
 VMXNET3_ERR_SMALL_BUF    = 0x80000004,
 VMXNET3_ERR_STRESS       = 0x80000005,
 VMXNET3_ERR_SWITCH       = 0x80000006,
 VMXNET3_ERR_TXD_INVALID  = 0x80000007,
};

#define VMXNET3_CDTYPE_TXCOMP      0
#define VMXNET3_CDTYPE_RXCOMP      3

enum {
    VMXNET3_GOS_BITS_UNK    = 0,
    VMXNET3_GOS_BITS_32     = 1,
    VMXNET3_GOS_BITS_64     = 2,
};

#define VMXNET3_GOS_TYPE_UNK        0
#define VMXNET3_GOS_TYPE_LINUX      1
#define VMXNET3_GOS_TYPE_WIN        2
#define VMXNET3_GOS_TYPE_SOLARIS    3
#define VMXNET3_GOS_TYPE_FREEBSD    4
#define VMXNET3_GOS_TYPE_PXE        5

struct Vmxnet3_GOSInfo {
#ifdef __BIG_ENDIAN_BITFIELD
    u32        gosMisc:10;
    u32        gosVer:16;
    u32        gosType:4;
    u32        gosBits:2;
#else
    u32        gosBits:2;
    u32        gosType:4;
    u32        gosVer:16;
    u32        gosMisc:10;
#endif
};

struct Vmxnet3_DriverInfo {
    __le32                version;
    struct Vmxnet3_GOSInfo        gos;
    __le32                vmxnet3RevSpt;
    __le32                uptVerSpt;
};


#define VMXNET3_REV1_MAGIC  0xbabefee1

#define VMXNET3_QUEUE_DESC_ALIGN  128


struct Vmxnet3_MiscConf {
    struct Vmxnet3_DriverInfo driverInfo;
    __le64        uptFeatures;
    __le64        ddPA;
    __le64        queueDescPA;
    __le32        ddLen;
    __le32        queueDescLen;
    __le32        mtu;
    __le16        maxNumRxSG;
    u8        numTxQueues;
    u8        numRxQueues;
    __le32        reserved[4];
};


struct Vmxnet3_TxQueueConf {
    __le64        txRingBasePA;
    __le64        dataRingBasePA;
    __le64        compRingBasePA;
    __le64        ddPA;
    __le64        reserved;
    __le32        txRingSize;
    __le32        dataRingSize;
    __le32        compRingSize;
    __le32        ddLen;
    u8        intrIdx;
    u8        _pad[7];
};


struct Vmxnet3_RxQueueConf {
    __le64        rxRingBasePA[2];
    __le64        compRingBasePA;
    __le64        ddPA;
    __le64        reserved;
    __le32        rxRingSize[2];
    __le32        compRingSize;
    __le32        ddLen;
    u8        intrIdx;
    u8        _pad[7];
};


enum vmxnet3_intr_mask_mode {
    VMXNET3_IMM_AUTO   = 0,
    VMXNET3_IMM_ACTIVE = 1,
    VMXNET3_IMM_LAZY   = 2
};

enum vmxnet3_intr_type {
    VMXNET3_IT_AUTO = 0,
    VMXNET3_IT_INTX = 1,
    VMXNET3_IT_MSI  = 2,
    VMXNET3_IT_MSIX = 3
};

#define VMXNET3_MAX_TX_QUEUES  8
#define VMXNET3_MAX_RX_QUEUES  16
#define VMXNET3_MAX_INTRS      25

#define VMXNET3_IC_DISABLE_ALL  0x1


struct Vmxnet3_IntrConf {
    bool        autoMask;
    u8        numIntrs;
    u8        eventIntrIdx;
    u8        modLevels[VMXNET3_MAX_INTRS];
    __le32        intrCtrl;
    __le32        reserved[2];
};

#define VMXNET3_VFT_SIZE  (4096/(sizeof(uint32_t)*8))


struct Vmxnet3_QueueStatus {
    bool        stopped;
    u8        _pad[3];
    __le32        error;
};


struct Vmxnet3_TxQueueCtrl {
    __le32        txNumDeferred;
    __le32        txThreshold;
    __le64        reserved;
};


struct Vmxnet3_RxQueueCtrl {
    bool        updateRxProd;
    u8        _pad[7];
    __le64        reserved;
};

enum {
    VMXNET3_RXM_UCAST     = 0x01,
    VMXNET3_RXM_MCAST     = 0x02,
    VMXNET3_RXM_BCAST     = 0x04,
    VMXNET3_RXM_ALL_MULTI = 0x08,
    VMXNET3_RXM_PROMISC   = 0x10
};

struct Vmxnet3_RxFilterConf {
    __le32        rxMode;
    __le16        mfTableLen;
    __le16        _pad1;
    __le64        mfTablePA;
    __le32        vfTable[VMXNET3_VFT_SIZE];
};


#define VMXNET3_PM_MAX_FILTERS        6
#define VMXNET3_PM_MAX_PATTERN_SIZE   128
#define VMXNET3_PM_MAX_MASK_SIZE      (VMXNET3_PM_MAX_PATTERN_SIZE / 8)

#define VMXNET3_PM_WAKEUP_MAGIC  cpu_to_le16(0x01)
#define VMXNET3_PM_WAKEUP_FILTER cpu_to_le16(0x02)


struct Vmxnet3_PM_PktFilter {
    u8        maskSize;
    u8        patternSize;
    u8        mask[VMXNET3_PM_MAX_MASK_SIZE];
    u8        pattern[VMXNET3_PM_MAX_PATTERN_SIZE];
    u8        pad[6];
};


struct Vmxnet3_PMConf {
    __le16        wakeUpEvents;
    u8        numFilters;
    u8        pad[5];
    struct Vmxnet3_PM_PktFilter filters[VMXNET3_PM_MAX_FILTERS];
};


struct Vmxnet3_VariableLenConfDesc {
    __le32        confVer;
    __le32        confLen;
    __le64        confPA;
};


struct Vmxnet3_TxQueueDesc {
    struct Vmxnet3_TxQueueCtrl        ctrl;
    struct Vmxnet3_TxQueueConf        conf;

    struct Vmxnet3_QueueStatus        status;
    struct UPT1_TxStats            stats;
    u8                    _pad[88];
};


struct Vmxnet3_RxQueueDesc {
    struct Vmxnet3_RxQueueCtrl        ctrl;
    struct Vmxnet3_RxQueueConf        conf;
    struct Vmxnet3_QueueStatus        status;
    struct UPT1_RxStats            stats;
    u8                      __pad[88];
};


struct Vmxnet3_DSDevRead {
    struct Vmxnet3_MiscConf            misc;
    struct Vmxnet3_IntrConf            intrConf;
    struct Vmxnet3_RxFilterConf        rxFilterConf;
    struct Vmxnet3_VariableLenConfDesc    rssConfDesc;
    struct Vmxnet3_VariableLenConfDesc    pmConfDesc;
    struct Vmxnet3_VariableLenConfDesc    pluginConfDesc;
};

struct Vmxnet3_DriverShared {
    __le32              magic;
    __le32              pad;
    struct Vmxnet3_DSDevRead    devRead;
    __le32              ecr;
    __le32              reserved[5];
};


#define VMXNET3_ECR_RQERR       (1 << 0)
#define VMXNET3_ECR_TQERR       (1 << 1)
#define VMXNET3_ECR_LINK        (1 << 2)
#define VMXNET3_ECR_DIC         (1 << 3)
#define VMXNET3_ECR_DEBUG       (1 << 4)

#define VMXNET3_FLIP_RING_GEN(gen) ((gen) = (gen) ^ 0x1)

#define VMXNET3_INC_RING_IDX_ONLY(idx, ring_size) \
    do {\
        (idx)++;\
        if (unlikely((idx) == (ring_size))) {\
            (idx) = 0;\
        } \
    } while (0)

#define VMXNET3_SET_VFTABLE_ENTRY(vfTable, vid) \
    (vfTable[vid >> 5] |= (1 << (vid & 31)))
#define VMXNET3_CLEAR_VFTABLE_ENTRY(vfTable, vid) \
    (vfTable[vid >> 5] &= ~(1 << (vid & 31)))

#define VMXNET3_VFTABLE_ENTRY_IS_SET(vfTable, vid) \
    ((vfTable[vid >> 5] & (1 << (vid & 31))) != 0)

#define VMXNET3_MAX_MTU     9000
#define VMXNET3_MIN_MTU     60

#define VMXNET3_LINK_UP         (10000 << 16 | 1)
#define VMXNET3_LINK_DOWN       0

#undef u64
#undef u32
#undef u16
#undef u8
#undef __le16
#undef __le32
#undef __le64
#if HOST_BIG_ENDIAN
#undef __BIG_ENDIAN_BITFIELD
#endif

#endif
