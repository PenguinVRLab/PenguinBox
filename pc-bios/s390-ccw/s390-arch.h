/*
 * S390 Basic Architecture
 *
 * Copyright (c) 2019 Jason J. Herne <jjherne@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef S390_ARCH_H
#define S390_ARCH_H

typedef struct PSW {
    uint64_t mask;
    uint64_t addr;
} __attribute__ ((aligned(8))) PSW;
_Static_assert(sizeof(struct PSW) == 16, "PSW size incorrect");

typedef struct PSWLegacy {
    uint32_t mask;
    uint32_t addr;
} __attribute__ ((aligned(8))) PSWLegacy;
_Static_assert(sizeof(struct PSWLegacy) == 8, "PSWLegacy size incorrect");

#define PSW_MASK_IOINT      0x0200000000000000ULL
#define PSW_MASK_SHORTPSW   0x0008000000000000ULL
#define PSW_MASK_WAIT       0x0002000000000000ULL
#define PSW_MASK_EAMODE     0x0000000100000000ULL
#define PSW_MASK_BAMODE     0x0000000080000000ULL
#define PSW_MASK_SHORT_ADDR 0x000000007fffffffULL
#define PSW_MASK_64         (PSW_MASK_EAMODE | PSW_MASK_BAMODE)

typedef struct LowCore {
    PSWLegacy       ipl_psw;
    uint32_t        ccw1[2];
    union {
        uint32_t        ccw2[2];
        struct {
            uint32_t reserved10;
            uint32_t ptr_iplb;
        };
    };
    uint8_t         pad1[0x80 - 0x18];
    uint32_t        ext_params;
    uint16_t        cpu_addr;
    uint16_t        ext_int_code;
    uint16_t        svc_ilen;
    uint16_t        svc_code;
    uint16_t        pgm_ilen;
    uint16_t        pgm_code;
    uint32_t        data_exc_code;
    uint16_t        mon_class_num;
    uint16_t        per_perc_atmid;
    uint64_t        per_address;
    uint8_t         exc_access_id;
    uint8_t         per_access_id;
    uint8_t         op_access_id;
    uint8_t         ar_access_id;
    uint8_t         pad2[0xA8 - 0xA4];
    uint64_t        trans_exc_code;
    uint64_t        monitor_code;
    uint16_t        subchannel_id;
    uint16_t        subchannel_nr;
    uint32_t        io_int_parm;
    uint32_t        io_int_word;
    uint8_t         pad3[0xc8 - 0xc4];
    uint32_t        stfl_fac_list;
    uint8_t         pad4[0xe8 - 0xcc];
    uint64_t        mcic;
    uint8_t         pad5[0xf4 - 0xf0];
    uint32_t        external_damage_code;
    uint64_t        failing_storage_address;
    uint8_t         pad6[0x110 - 0x100];
    uint64_t        per_breaking_event_addr;
    uint8_t         pad7[0x120 - 0x118];
    PSW             restart_old_psw;
    PSW             external_old_psw;
    PSW             svc_old_psw;
    PSW             program_old_psw;
    PSW             mcck_old_psw;
    PSW             io_old_psw;
    uint8_t         pad8[0x1a0 - 0x180];
    PSW             restart_new_psw;
    PSW             external_new_psw;
    PSW             svc_new_psw;
    PSW             program_new_psw;
    PSW             mcck_new_psw;
    PSW             io_new_psw;
} __attribute__((packed, aligned(8192))) LowCore;

extern LowCore *lowcore;

#define S390EP 0x10008

static inline void set_prefix(uint32_t address)
{
    asm volatile("spx %0" : : "m" (address) : "memory");
}

static inline uint32_t store_prefix(void)
{
    uint32_t address;

    asm volatile("stpx %0" : "=m" (address));
    return address;
}

#endif
