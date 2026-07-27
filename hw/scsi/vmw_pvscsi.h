/*
 * VMware PVSCSI header file
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
 * Maintained by: Arvind Kumar <arvindkumar@vmware.com>
 *
 */

#ifndef VMW_PVSCSI_H
#define VMW_PVSCSI_H

#define VMW_PAGE_SIZE  (4096)
#define VMW_PAGE_SHIFT (12)

#define MASK(n)        ((1 << (n)) - 1)

enum HostBusAdapterStatus {
   BTSTAT_SUCCESS       = 0x00,
   BTSTAT_LINKED_COMMAND_COMPLETED           = 0x0a,
   BTSTAT_LINKED_COMMAND_COMPLETED_WITH_FLAG = 0x0b,
   BTSTAT_DATA_UNDERRUN = 0x0c,
   BTSTAT_SELTIMEO      = 0x11,
   BTSTAT_DATARUN       = 0x12,
   BTSTAT_BUSFREE       = 0x13,
   BTSTAT_INVPHASE      = 0x14,
   BTSTAT_LUNMISMATCH   = 0x17,
   BTSTAT_SENSFAILED    = 0x1b,
   BTSTAT_TAGREJECT     = 0x1c,
   BTSTAT_BADMSG        = 0x1d,
   BTSTAT_HAHARDWARE    = 0x20,
   BTSTAT_NORESPONSE    = 0x21,
   BTSTAT_SENTRST       = 0x22,
   BTSTAT_RECVRST       = 0x23,
   BTSTAT_DISCONNECT    = 0x24,
   BTSTAT_BUSRESET      = 0x25,
   BTSTAT_ABORTQUEUE    = 0x26,
   BTSTAT_HASOFTWARE    = 0x27,
   BTSTAT_HATIMEOUT     = 0x30,
   BTSTAT_SCSIPARITY    = 0x34,
};

enum PVSCSIRegOffset {
    PVSCSI_REG_OFFSET_COMMAND        =    0x0,
    PVSCSI_REG_OFFSET_COMMAND_DATA   =    0x4,
    PVSCSI_REG_OFFSET_COMMAND_STATUS =    0x8,
    PVSCSI_REG_OFFSET_LAST_STS_0     =  0x100,
    PVSCSI_REG_OFFSET_LAST_STS_1     =  0x104,
    PVSCSI_REG_OFFSET_LAST_STS_2     =  0x108,
    PVSCSI_REG_OFFSET_LAST_STS_3     =  0x10c,
    PVSCSI_REG_OFFSET_INTR_STATUS    = 0x100c,
    PVSCSI_REG_OFFSET_INTR_MASK      = 0x2010,
    PVSCSI_REG_OFFSET_KICK_NON_RW_IO = 0x3014,
    PVSCSI_REG_OFFSET_DEBUG          = 0x3018,
    PVSCSI_REG_OFFSET_KICK_RW_IO     = 0x4018,
};

enum PVSCSICommands {
    PVSCSI_CMD_FIRST             = 0,

    PVSCSI_CMD_ADAPTER_RESET     = 1,
    PVSCSI_CMD_ISSUE_SCSI        = 2,
    PVSCSI_CMD_SETUP_RINGS       = 3,
    PVSCSI_CMD_RESET_BUS         = 4,
    PVSCSI_CMD_RESET_DEVICE      = 5,
    PVSCSI_CMD_ABORT_CMD         = 6,
    PVSCSI_CMD_CONFIG            = 7,
    PVSCSI_CMD_SETUP_MSG_RING    = 8,
    PVSCSI_CMD_DEVICE_UNPLUG     = 9,

    PVSCSI_CMD_LAST              = 10
};

#define PVSCSI_COMMAND_PROCESSING_SUCCEEDED   (0)
#define PVSCSI_COMMAND_PROCESSING_FAILED     (-1)
#define PVSCSI_COMMAND_NOT_ENOUGH_DATA       (-2)

struct PVSCSICmdDescResetDevice {
    uint32_t    target;
    uint8_t     lun[8];
} QEMU_PACKED;

typedef struct PVSCSICmdDescResetDevice PVSCSICmdDescResetDevice;

struct PVSCSICmdDescAbortCmd {
    uint64_t    context;
    uint32_t    target;
    uint32_t    pad;
} QEMU_PACKED;

typedef struct PVSCSICmdDescAbortCmd PVSCSICmdDescAbortCmd;

#define PVSCSI_SETUP_RINGS_MAX_NUM_PAGES        32
struct PVSCSICmdDescSetupRings {
    uint32_t    reqRingNumPages;
    uint32_t    cmpRingNumPages;
    uint64_t    ringsStatePPN;
    uint64_t    reqRingPPNs[PVSCSI_SETUP_RINGS_MAX_NUM_PAGES];
    uint64_t    cmpRingPPNs[PVSCSI_SETUP_RINGS_MAX_NUM_PAGES];
} QEMU_PACKED;

typedef struct PVSCSICmdDescSetupRings PVSCSICmdDescSetupRings;

#define PVSCSI_SETUP_MSG_RING_MAX_NUM_PAGES  16

struct PVSCSICmdDescSetupMsgRing {
    uint32_t    numPages;
    uint32_t    pad;
    uint64_t    ringPPNs[PVSCSI_SETUP_MSG_RING_MAX_NUM_PAGES];
} QEMU_PACKED;

typedef struct PVSCSICmdDescSetupMsgRing PVSCSICmdDescSetupMsgRing;

enum PVSCSIMsgType {
    PVSCSI_MSG_DEV_ADDED          = 0,
    PVSCSI_MSG_DEV_REMOVED        = 1,
    PVSCSI_MSG_LAST               = 2,
};

struct PVSCSIRingMsgDesc {
    uint32_t    type;
    uint32_t    args[31];
} QEMU_PACKED;

typedef struct PVSCSIRingMsgDesc PVSCSIRingMsgDesc;

struct PVSCSIMsgDescDevStatusChanged {
    uint32_t    type;
    uint32_t    bus;
    uint32_t    target;
    uint8_t     lun[8];
    uint32_t    pad[27];
} QEMU_PACKED;

typedef struct PVSCSIMsgDescDevStatusChanged PVSCSIMsgDescDevStatusChanged;

struct PVSCSIRingsState {
    uint32_t    reqProdIdx;
    uint32_t    reqConsIdx;
    uint32_t    reqNumEntriesLog2;

    uint32_t    cmpProdIdx;
    uint32_t    cmpConsIdx;
    uint32_t    cmpNumEntriesLog2;

    uint8_t     pad[104];

    uint32_t    msgProdIdx;
    uint32_t    msgConsIdx;
    uint32_t    msgNumEntriesLog2;
} QEMU_PACKED;

typedef struct PVSCSIRingsState PVSCSIRingsState;

#define PVSCSI_FLAG_CMD_WITH_SG_LIST        (1 << 0)
#define PVSCSI_FLAG_CMD_OUT_OF_BAND_CDB     (1 << 1)
#define PVSCSI_FLAG_CMD_DIR_NONE            (1 << 2)
#define PVSCSI_FLAG_CMD_DIR_TOHOST          (1 << 3)
#define PVSCSI_FLAG_CMD_DIR_TODEVICE        (1 << 4)

#define PVSCSI_KNOWN_FLAGS \
  (PVSCSI_FLAG_CMD_WITH_SG_LIST     | \
   PVSCSI_FLAG_CMD_OUT_OF_BAND_CDB  | \
   PVSCSI_FLAG_CMD_DIR_NONE         | \
   PVSCSI_FLAG_CMD_DIR_TOHOST       | \
   PVSCSI_FLAG_CMD_DIR_TODEVICE)

struct PVSCSIRingReqDesc {
    uint64_t    context;
    uint64_t    dataAddr;
    uint64_t    dataLen;
    uint64_t    senseAddr;
    uint32_t    senseLen;
    uint32_t    flags;
    uint8_t     cdb[16];
    uint8_t     cdbLen;
    uint8_t     lun[8];
    uint8_t     tag;
    uint8_t     bus;
    uint8_t     target;
    uint8_t     vcpuHint;
    uint8_t     unused[59];
} QEMU_PACKED;

typedef struct PVSCSIRingReqDesc PVSCSIRingReqDesc;

struct PVSCSISGElement {
    uint64_t    addr;
    uint32_t    length;
    uint32_t    flags;
} QEMU_PACKED;

typedef struct PVSCSISGElement PVSCSISGElement;

struct PVSCSIRingCmpDesc {
    uint64_t    context;
    uint64_t    dataLen;
    uint32_t    senseLen;
    uint16_t    hostStatus;
    uint16_t    scsiStatus;
    uint32_t    pad[2];
} QEMU_PACKED;

typedef struct PVSCSIRingCmpDesc PVSCSIRingCmpDesc;

#define PVSCSI_INTR_CMPL_0                 (1 << 0)
#define PVSCSI_INTR_CMPL_1                 (1 << 1)
#define PVSCSI_INTR_CMPL_MASK              MASK(2)

#define PVSCSI_INTR_MSG_0                  (1 << 2)
#define PVSCSI_INTR_MSG_1                  (1 << 3)
#define PVSCSI_INTR_MSG_MASK               (MASK(2) << 2)

#define PVSCSI_INTR_ALL_SUPPORTED          MASK(4)

#define PVSCSI_MAX_INTRS        24

#define PVSCSI_VECTOR_COMPLETION   0

#define PVSCSI_MAX_NUM_PAGES_REQ_RING   PVSCSI_SETUP_RINGS_MAX_NUM_PAGES
#define PVSCSI_MAX_NUM_PAGES_CMP_RING   PVSCSI_SETUP_RINGS_MAX_NUM_PAGES
#define PVSCSI_MAX_NUM_PAGES_MSG_RING   PVSCSI_SETUP_MSG_RING_MAX_NUM_PAGES

#define PVSCSI_MAX_NUM_REQ_ENTRIES_PER_PAGE \
                (VMW_PAGE_SIZE / sizeof(struct PVSCSIRingReqDesc))

#define PVSCSI_MAX_NUM_CMP_ENTRIES_PER_PAGE \
                (VMW_PAGE_SIZE / sizeof(PVSCSIRingCmpDesc))

#define PVSCSI_MAX_NUM_MSG_ENTRIES_PER_PAGE \
                (VMW_PAGE_SIZE / sizeof(PVSCSIRingMsgDesc))

#define PVSCSI_MAX_REQ_QUEUE_DEPTH \
    (PVSCSI_MAX_NUM_PAGES_REQ_RING * PVSCSI_MAX_NUM_REQ_ENTRIES_PER_PAGE)

#define PVSCSI_MEM_SPACE_COMMAND_NUM_PAGES     1
#define PVSCSI_MEM_SPACE_INTR_STATUS_NUM_PAGES 1
#define PVSCSI_MEM_SPACE_MISC_NUM_PAGES        2
#define PVSCSI_MEM_SPACE_KICK_IO_NUM_PAGES     2
#define PVSCSI_MEM_SPACE_MSIX_NUM_PAGES        2

enum PVSCSIMemSpace {
    PVSCSI_MEM_SPACE_COMMAND_PAGE       = 0,
    PVSCSI_MEM_SPACE_INTR_STATUS_PAGE   = 1,
    PVSCSI_MEM_SPACE_MISC_PAGE          = 2,
    PVSCSI_MEM_SPACE_KICK_IO_PAGE       = 4,
    PVSCSI_MEM_SPACE_MSIX_TABLE_PAGE    = 6,
    PVSCSI_MEM_SPACE_MSIX_PBA_PAGE      = 7,
};

#define PVSCSI_MEM_SPACE_NUM_PAGES \
    (PVSCSI_MEM_SPACE_COMMAND_NUM_PAGES +       \
     PVSCSI_MEM_SPACE_INTR_STATUS_NUM_PAGES +   \
     PVSCSI_MEM_SPACE_MISC_NUM_PAGES +          \
     PVSCSI_MEM_SPACE_KICK_IO_NUM_PAGES +       \
     PVSCSI_MEM_SPACE_MSIX_NUM_PAGES)

#define PVSCSI_MEM_SPACE_SIZE    (PVSCSI_MEM_SPACE_NUM_PAGES * VMW_PAGE_SIZE)

#endif
