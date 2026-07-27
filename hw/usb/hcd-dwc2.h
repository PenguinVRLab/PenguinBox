/*
 * dwc-hsotg (dwc2) USB host controller state definitions
 *
 * Based on hw/usb/hcd-ehci.h
 *
 * Copyright (c) 2020 Paul Zimmerman <pauldzim@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef HW_USB_HCD_DWC2_H
#define HW_USB_HCD_DWC2_H

#include "qemu/timer.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/usb.h"
#include "system/dma.h"
#include "qom/object.h"

#define DWC2_MMIO_SIZE      0x11000

#define DWC2_NB_CHAN        8
#define DWC2_MAX_XFER_SIZE  65536

typedef struct DWC2Packet DWC2Packet;
typedef struct DWC2State DWC2State;
typedef struct DWC2Class DWC2Class;

enum async_state {
    DWC2_ASYNC_NONE = 0,
    DWC2_ASYNC_INITIALIZED,
    DWC2_ASYNC_INFLIGHT,
    DWC2_ASYNC_FINISHED,
};

struct DWC2Packet {
    USBPacket packet;
    uint32_t devadr;
    uint32_t epnum;
    uint32_t epdir;
    uint32_t mps;
    uint32_t pid;
    uint32_t index;
    uint32_t pcnt;
    uint32_t len;
    int32_t async;
    bool small;
    bool needs_service;
};

struct DWC2State {
    SysBusDevice parent_obj;

    USBBus bus;
    qemu_irq irq;
    MemoryRegion *dma_mr;
    AddressSpace dma_as;
    MemoryRegion container;
    MemoryRegion hsotg;
    MemoryRegion fifos;

    union {
#define DWC2_GLBREG_SIZE    0x70
        uint32_t glbreg[DWC2_GLBREG_SIZE / sizeof(uint32_t)];
        struct {
            uint32_t gotgctl;
            uint32_t gotgint;
            uint32_t gahbcfg;
            uint32_t gusbcfg;
            uint32_t grstctl;
            uint32_t gintsts;
            uint32_t gintmsk;
            uint32_t grxstsr;
            uint32_t grxstsp;
            uint32_t grxfsiz;
            uint32_t gnptxfsiz;
            uint32_t gnptxsts;
            uint32_t gi2cctl;
            uint32_t gpvndctl;
            uint32_t ggpio;
            uint32_t guid;
            uint32_t gsnpsid;
            uint32_t ghwcfg1;
            uint32_t ghwcfg2;
            uint32_t ghwcfg3;
            uint32_t ghwcfg4;
            uint32_t glpmcfg;
            uint32_t gpwrdn;
            uint32_t gdfifocfg;
            uint32_t gadpctl;
            uint32_t grefclk;
            uint32_t gintmsk2;
            uint32_t gintsts2;
        };
    };

    union {
#define DWC2_FSZREG_SIZE    0x04
        uint32_t fszreg[DWC2_FSZREG_SIZE / sizeof(uint32_t)];
        struct {
            uint32_t hptxfsiz;
        };
    };

    union {
#define DWC2_HREG0_SIZE     0x44
        uint32_t hreg0[DWC2_HREG0_SIZE / sizeof(uint32_t)];
        struct {
            uint32_t hcfg;
            uint32_t hfir;
            uint32_t hfnum;
            uint32_t rsvd0;
            uint32_t hptxsts;
            uint32_t haint;
            uint32_t haintmsk;
            uint32_t hflbaddr;
            uint32_t rsvd1[8];
            uint32_t hprt0;
        };
    };

#define DWC2_HREG1_SIZE     (0x20 * DWC2_NB_CHAN)
    uint32_t hreg1[DWC2_HREG1_SIZE / sizeof(uint32_t)];

#define hcchar(_ch)     hreg1[((_ch) << 3) + 0]
#define hcsplt(_ch)     hreg1[((_ch) << 3) + 1]
#define hcint(_ch)      hreg1[((_ch) << 3) + 2]
#define hcintmsk(_ch)   hreg1[((_ch) << 3) + 3]
#define hctsiz(_ch)     hreg1[((_ch) << 3) + 4]
#define hcdma(_ch)      hreg1[((_ch) << 3) + 5]
#define hcdmab(_ch)     hreg1[((_ch) << 3) + 7]

    union {
#define DWC2_PCGREG_SIZE    0x08
        uint32_t pcgreg[DWC2_PCGREG_SIZE / sizeof(uint32_t)];
        struct {
            uint32_t pcgctl;
            uint32_t pcgcctl1;
        };
    };

#define DWC2_HFIFO_SIZE     (0x1000 * DWC2_NB_CHAN)

    QEMUTimer *eof_timer;
    QEMUTimer *frame_timer;
    QEMUBH *async_bh;
    int64_t sof_time;
    int64_t usb_frame_time;
    int64_t usb_bit_time;
    uint32_t usb_version;
    uint16_t frame_number;
    uint16_t fi;
    uint16_t next_chan;
    bool working;
    USBPort uport;
    DWC2Packet packet[DWC2_NB_CHAN];
    uint8_t usb_buf[DWC2_NB_CHAN][DWC2_MAX_XFER_SIZE];
};

struct DWC2Class {
    SysBusDeviceClass parent_class;
    ResettablePhases parent_phases;

};

#define TYPE_DWC2_USB   "dwc2-usb"
OBJECT_DECLARE_TYPE(DWC2State, DWC2Class, DWC2_USB)

#endif
