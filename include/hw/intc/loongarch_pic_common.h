/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch 7A1000 I/O interrupt controller definitions
 * Copyright (c) 2024 Loongson Technology Corporation Limited
 */

#ifndef HW_LOONGARCH_PIC_COMMON_H
#define HW_LOONGARCH_PIC_COMMON_H

#include "hw/loongarch/virt.h"
#include "hw/sysbus.h"
#include "system/memory.h"

#define PCH_PIC_INT_ID                  0x00
#define  PCH_PIC_INT_ID_VAL             0x7
#define  PCH_PIC_INT_ID_VER             0x1
#define PCH_PIC_INT_MASK                0x20
#define PCH_PIC_HTMSI_EN                0x40
#define PCH_PIC_INT_EDGE                0x60
#define PCH_PIC_INT_CLEAR               0x80
#define PCH_PIC_AUTO_CTRL0              0xc0
#define PCH_PIC_AUTO_CTRL1              0xe0
#define PCH_PIC_ROUTE_ENTRY             0x100
#define PCH_PIC_ROUTE_ENTRY_END         0x13f
#define PCH_PIC_HTMSI_VEC               0x200
#define PCH_PIC_HTMSI_VEC_END           0x23f
#define PCH_PIC_INT_REQUEST             0x380
#define PCH_PIC_INT_STATUS              0x3a0
#define PCH_PIC_INT_POL                 0x3e0

#define TYPE_LOONGARCH_PIC_COMMON "loongarch_pic_common"
OBJECT_DECLARE_TYPE(LoongArchPICCommonState,
                    LoongArchPICCommonClass, LOONGARCH_PIC_COMMON)

union LoongArchPIC_ID {
    struct {
        uint8_t _reserved_0[3];
        uint8_t id;
        uint8_t version;
        uint8_t _reserved_1;
        uint8_t irq_num;
        uint8_t _reserved_2;
    } QEMU_PACKED desc;
    uint64_t data;
};

struct LoongArchPICCommonState {
    SysBusDevice parent_obj;

    qemu_irq parent_irq[64];
    union LoongArchPIC_ID id;
    uint64_t int_mask;
    uint64_t htmsi_en;
    uint64_t intedge;
    uint64_t intclr;
    uint64_t auto_crtl0;
    uint64_t auto_crtl1;
    uint64_t last_intirr;
    uint64_t intirr;
    uint64_t intisr;
    uint64_t int_polarity;

    uint8_t route_entry[64];
    uint8_t htmsi_vector[64];

    MemoryRegion iomem;
    unsigned int irq_num;
};

struct LoongArchPICCommonClass {
    SysBusDeviceClass parent_class;

    DeviceRealize parent_realize;
    ResettablePhases parent_phases;
    int (*pre_save)(LoongArchPICCommonState *s);
    int (*post_load)(LoongArchPICCommonState *s, int version_id);
};
#endif
