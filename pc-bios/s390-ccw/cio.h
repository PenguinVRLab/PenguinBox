/*
 * Channel IO definitions
 *
 * Copyright (c) 2013 Alexander Graf <agraf@suse.de>
 *
 * Inspired by various s390 headers in Linux 3.9.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef CIO_H
#define CIO_H

struct pmcw {
    u32 intparm;
    u32 qf:1;
    u32 w:1;
    u32 isc:3;
    u32 res5:3;
    u32 ena:1;
    u32 lm:2;
    u32 mme:2;
    u32 mp:1;
    u32 tf:1;
    u32 dnv:1;
    u32 dev:16;
    u8  lpm;
    u8  pnom;
    u8  lpum;
    u8  pim;
    u16 mbi;
    u8  pom;
    u8  pam;
    u8  chpid[8];
    u32 unused1:8;
    u32 st:3;
    u32 unused2:18;
    u32 mbfc:1;
    u32 xmwme:1;
    u32 csense:1;
} __attribute__ ((packed));

struct schib_config {
    u64 mba;
    u32 intparm;
    u16 mbi;
    u32 isc:3;
    u32 ena:1;
    u32 mme:2;
    u32 mp:1;
    u32 csense:1;
    u32 mbfc:1;
} __attribute__ ((packed));

struct scsw {
    u16 flags;
    u16 ctrl;
    u32 cpa;
    u8 dstat;
    u8 cstat;
    u16 count;
} __attribute__ ((packed));

#define SCSW_FCTL_START_FUNC 0x4000
#define SCSW_FCTL_HALT_FUNC 0x2000
#define SCSW_FCTL_CLEAR_FUNC 0x1000

#define SCSW_ACTL_RESUME_PEND   0x0800
#define SCSW_ACTL_START_PEND    0x0400
#define SCSW_ACTL_HALT_PEND     0x0200
#define SCSW_ACTL_CLEAR_PEND    0x0100
#define SCSW_ACTL_CH_ACTIVE     0x0080
#define SCSW_ACTL_DEV_ACTIVE    0x0040
#define SCSW_ACTL_SUSPENDED     0x0020

#define SCSW_SCTL_ALERT         0x0010
#define SCSW_SCTL_INTERMED      0x0008
#define SCSW_SCTL_PRIMARY       0x0004
#define SCSW_SCTL_SECONDARY     0x0002
#define SCSW_SCTL_STATUS_PEND   0x0001

#define SCSW_DSTAT_ATTN     0x80
#define SCSW_DSTAT_STATMOD  0x40
#define SCSW_DSTAT_CUEND    0x20
#define SCSW_DSTAT_BUSY     0x10
#define SCSW_DSTAT_CHEND    0x08
#define SCSW_DSTAT_DEVEND   0x04
#define SCSW_DSTAT_UCHK     0x02
#define SCSW_DSTAT_UEXCP    0x01

#define SCSW_CSTAT_PCINT    0x80
#define SCSW_CSTAT_BADLEN   0x40
#define SCSW_CSTAT_PROGCHK  0x20
#define SCSW_CSTAT_PROTCHK  0x10
#define SCSW_CSTAT_CHDCHK   0x08
#define SCSW_CSTAT_CHCCHK   0x04
#define SCSW_CSTAT_ICCHK    0x02
#define SCSW_CSTAT_CHAINCHK 0x01

typedef struct schib {
    struct pmcw pmcw;
    struct scsw scsw;
    u64 mba;
    u8 mda[4];
} __attribute__ ((packed, aligned(4))) Schib;

typedef struct subchannel_id {
    union {
        struct {
            u16 cssid:8;
            u16 reserved:4;
            u16 m:1;
            u16 ssid:2;
            u16 one:1;
        };
        u16 sch_id;
    };
    u16 sch_no;
} __attribute__ ((packed, aligned(4))) SubChannelId;

struct chsc_header {
    u16 length;
    u16 code;
} __attribute__((packed));

typedef struct chsc_area_sda {
    struct chsc_header request;
    u8 reserved1:4;
    u8 format:4;
    u8 reserved2;
    u16 operation_code;
    u32 reserved3;
    u32 reserved4;
    u32 operation_data_area[252];
    struct chsc_header response;
    u32 reserved5:4;
    u32 format2:4;
    u32 reserved6:24;
} __attribute__((packed)) ChscAreaSda;

struct tpi_info {
    struct subchannel_id schid;
    u32 intparm;
    u32 adapter_IO:1;
    u32 reserved2:1;
    u32 isc:3;
    u32 reserved3:12;
    u32 int_type:3;
    u32 reserved4:12;
} __attribute__ ((packed, aligned(4)));

typedef struct ccw0 {
    u8 cmd_code;
    u32 cda:24;
    u32 chainData:1;
    u32 chain:1;
    u32 sli:1;
    u32 skip:1;
    u32 pci:1;
    u32 ida:1;
    u32 suspend:1;
    u32 mida:1;
    u8 reserved;
    u16 count;
} __attribute__ ((packed, aligned(8))) Ccw0;

typedef struct ccw1 {
    u8 cmd_code;
    u8 flags;
    u16 count;
    u32 cda;
} __attribute__ ((packed, aligned(8))) Ccw1;

#define CCW_FMT0                 0x00
#define CCW_FMT1                 0x01

#define CCW_FLAG_DC              0x80
#define CCW_FLAG_CC              0x40
#define CCW_FLAG_SLI             0x20
#define CCW_FLAG_SKIP            0x10
#define CCW_FLAG_PCI             0x08
#define CCW_FLAG_IDA             0x04
#define CCW_FLAG_SUSPEND         0x02

#define CCW_CMD_READ_IPL         0x02
#define CCW_CMD_NOOP             0x03
#define CCW_CMD_BASIC_SENSE      0x04
#define CCW_CMD_TIC              0x08
#define CCW_CMD_SENSE_ID         0xe4

#define CCW_CMD_SET_VQ           0x13
#define CCW_CMD_VDEV_RESET       0x33
#define CCW_CMD_READ_FEAT        0x12
#define CCW_CMD_WRITE_FEAT       0x11
#define CCW_CMD_READ_CONF        0x22
#define CCW_CMD_WRITE_CONF       0x21
#define CCW_CMD_WRITE_STATUS     0x31
#define CCW_CMD_SET_IND          0x43
#define CCW_CMD_SET_CONF_IND     0x53
#define CCW_CMD_READ_VQ_CONF     0x32

#define CCW_CMD_DASD_READ             0x06
#define CCW_CMD_DASD_SEEK             0x07
#define CCW_CMD_DASD_SEARCH_ID_EQ     0x31
#define CCW_CMD_DASD_READ_MT          0x86

typedef struct cmd_orb {
    u32 intparm;
    u32 key:4;
    u32 spnd:1;
    u32 res1:1;
    u32 mod:1;
    u32 sync:1;
    u32 fmt:1;
    u32 pfch:1;
    u32 isic:1;
    u32 alcc:1;
    u32 ssic:1;
    u32 res2:1;
    u32 c64:1;
    u32 i2k:1;
    u32 lpm:8;
    u32 ils:1;
    u32 zero:6;
    u32 orbx:1;
    u32 cpa;
}  __attribute__ ((packed, aligned(4))) CmdOrb;

struct ciw {
    u8 type;
    u8 command;
    u16 count;
};

#define CU_TYPE_UNKNOWN         0x0000
#define CU_TYPE_DASD_2107       0x2107
#define CU_TYPE_VIRTIO          0x3832
#define CU_TYPE_DASD_3990       0x3990

typedef struct senseid {
    u8  reserved;
    u16 cu_type;
    u8  cu_model;
    u16 dev_type;
    u8  dev_model;
    u8  unused;
    struct ciw ciw[62];
}  __attribute__ ((packed, aligned(4))) SenseId;

#define SNS_STAT0_CMD_REJECT         0x80
#define SNS_STAT0_INTERVENTION_REQ   0x40
#define SNS_STAT0_BUS_OUT_CHECK      0x20
#define SNS_STAT0_EQUIPMENT_CHECK    0x10
#define SNS_STAT0_DATA_CHECK         0x08
#define SNS_STAT0_OVERRUN            0x04
#define SNS_STAT0_INCOMPL_DOMAIN     0x01

#define SNS_STAT1_PERM_ERR           0x80
#define SNS_STAT1_INV_TRACK_FORMAT   0x40
#define SNS_STAT1_EOC                0x20
#define SNS_STAT1_MESSAGE_TO_OPER    0x10
#define SNS_STAT1_NO_REC_FOUND       0x08
#define SNS_STAT1_FILE_PROTECTED     0x04
#define SNS_STAT1_WRITE_INHIBITED    0x02
#define SNS_STAT1_IMPRECISE_END      0x01

#define SNS_STAT2_REQ_INH_WRITE      0x80
#define SNS_STAT2_CORRECTABLE        0x40
#define SNS_STAT2_FIRST_LOG_ERR      0x20
#define SNS_STAT2_ENV_DATA_PRESENT   0x10
#define SNS_STAT2_IMPRECISE_END      0x04

#define SENSE24_FMT_PROG_SYS    0x0
#define SENSE24_FMT_EQUIPMENT   0x2
#define SENSE24_FMT_CONTROLLER  0x3
#define SENSE24_FMT_MISC        0xF

typedef struct SenseDataEckdDasd {
    uint8_t common_status;
    uint8_t status[2];
    uint8_t res_count;
    uint8_t phys_drive_id;
    uint8_t low_cyl_addr;
    uint8_t head_high_cyl_addr;
    uint8_t fmt_msg;
    uint64_t fmt_dependent_info[2];
    uint8_t reserved;
    uint8_t program_action_code;
    uint16_t config_info;
    uint8_t mcode_hicyl;
    uint8_t cyl_head_addr[3];
}  __attribute__ ((packed, aligned(4))) SenseDataEckdDasd;

#define ECKD_SENSE24_GET_FMT(sd)     (sd->fmt_msg & 0xF0 >> 4)
#define ECKD_SENSE24_GET_MSG(sd)     (sd->fmt_msg & 0x0F)

#define unit_check(irb)         ((irb)->scsw.dstat & SCSW_DSTAT_UCHK)
#define iface_ctrl_check(irb)   ((irb)->scsw.cstat & SCSW_CSTAT_ICCHK)

typedef struct irb {
    struct scsw scsw;
    u32 esw[5];
    u32 ecw[8];
    u32 emw[8];
}  __attribute__ ((packed, aligned(4))) Irb;

typedef struct CcwSeekData {
    uint16_t reserved;
    uint16_t cyl;
    uint16_t head;
} __attribute__((packed)) CcwSeekData;

typedef struct CcwSearchIdData {
    uint16_t cyl;
    uint16_t head;
    uint8_t record;
} __attribute__((packed)) CcwSearchIdData;

extern SubChannelId net_schid;

int enable_mss_facility(void);
void enable_subchannel(SubChannelId schid);
uint16_t cu_type(SubChannelId schid);
int basic_sense(SubChannelId schid, uint16_t cutype, void *sense_data,
                 uint16_t data_size);
int do_cio(SubChannelId schid, uint16_t cutype, uint32_t ccw_addr, int fmt);

static inline int stsch_err(struct subchannel_id schid, struct schib *addr)
{
    register struct subchannel_id reg1 asm ("1") = schid;
    int ccode = -EIO;

    asm volatile(
        "    stsch    0(%3)\n"
        "0:  ipm    %0\n"
        "    srl    %0,28\n"
        "1:\n"
        : "+d" (ccode), "=m" (*addr)
        : "d" (reg1), "a" (addr)
        : "cc");
    return ccode;
}

static inline int msch(struct subchannel_id schid, struct schib *addr)
{
    register struct subchannel_id reg1 asm ("1") = schid;
    int ccode;

    asm volatile(
        "    msch    0(%2)\n"
        "    ipm    %0\n"
        "    srl    %0,28"
        : "=d" (ccode)
        : "d" (reg1), "a" (addr), "m" (*addr)
        : "cc");
    return ccode;
}

static inline int msch_err(struct subchannel_id schid, struct schib *addr)
{
    register struct subchannel_id reg1 asm ("1") = schid;
    int ccode = -EIO;

    asm volatile(
        "    msch    0(%2)\n"
        "0:  ipm    %0\n"
        "    srl    %0,28\n"
        "1:\n"
        : "+d" (ccode)
        : "d" (reg1), "a" (addr), "m" (*addr)
        : "cc");
    return ccode;
}

static inline int tsch(struct subchannel_id schid, struct irb *addr)
{
    register struct subchannel_id reg1 asm ("1") = schid;
    int ccode;

    asm volatile(
        "    tsch    0(%3)\n"
        "    ipm    %0\n"
        "    srl    %0,28"
        : "=d" (ccode), "=m" (*addr)
        : "d" (reg1), "a" (addr)
        : "cc");
    return ccode;
}

static inline int ssch(struct subchannel_id schid, struct cmd_orb *addr)
{
    register struct subchannel_id reg1 asm("1") = schid;
    int ccode = -EIO;

    asm volatile(
        "    ssch    0(%2)\n"
        "0:  ipm    %0\n"
        "    srl    %0,28\n"
        "1:\n"
        : "+d" (ccode)
        : "d" (reg1), "a" (addr), "m" (*addr)
        : "cc", "memory");
    return ccode;
}

static inline int csch(struct subchannel_id schid)
{
    register struct subchannel_id reg1 asm("1") = schid;
    int ccode;

    asm volatile(
        "    csch\n"
        "    ipm    %0\n"
        "    srl    %0,28"
        : "=d" (ccode)
        : "d" (reg1)
        : "cc");
    return ccode;
}

static inline int tpi(struct tpi_info *addr)
{
    int ccode;

    asm volatile(
        "    tpi    0(%2)\n"
        "    ipm    %0\n"
        "    srl    %0,28"
        : "=d" (ccode), "=m" (*addr)
        : "a" (addr)
        : "cc");
    return ccode;
}

static inline int chsc(void *chsc_area)
{
    typedef struct { char _[4096]; } addr_type;
    int cc;

    asm volatile(
        "    .insn    rre,0xb25f0000,%2,0\n"
        "    ipm    %0\n"
        "    srl    %0,28\n"
        : "=d" (cc), "=m" (*(addr_type *) chsc_area)
        : "d" (chsc_area), "m" (*(addr_type *) chsc_area)
        : "cc");
    return cc;
}

#endif
