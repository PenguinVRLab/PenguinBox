/*
 * CAN common CAN bus emulation support
 *
 * Copyright (c) 2013-2014 Jin Yang
 * Copyright (c) 2014-2018 Pavel Pisa
 *
 * Initial development supported by Google GSoC 2013 from RTEMS project slot
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef NET_CAN_EMU_H
#define NET_CAN_EMU_H

#include "qemu/queue.h"
#include "qom/object.h"

/* NOTE: the following two structures is copied from <linux/can.h>. */

typedef uint32_t qemu_canid_t;

typedef struct qemu_can_frame {
    qemu_canid_t    can_id;
    uint8_t         can_dlc;
    uint8_t         flags;
    uint8_t         data[64] QEMU_ALIGNED(8);
} qemu_can_frame;

/* Keep defines for QEMU separate from Linux ones for now */

#define QEMU_CAN_EFF_FLAG 0x80000000U
#define QEMU_CAN_RTR_FLAG 0x40000000U
#define QEMU_CAN_ERR_FLAG 0x20000000U

#define QEMU_CAN_SFF_MASK 0x000007FFU
#define QEMU_CAN_EFF_MASK 0x1FFFFFFFU

#define QEMU_CAN_FRMF_BRS     0x01
#define QEMU_CAN_FRMF_ESI     0x02
#define QEMU_CAN_FRMF_TYPE_FD 0x10

typedef struct qemu_can_filter {
    qemu_canid_t    can_id;
    qemu_canid_t    can_mask;
} qemu_can_filter;

#define QEMU_CAN_INV_FILTER 0x20000000U

typedef struct CanBusClientState CanBusClientState;

#define TYPE_CAN_BUS "can-bus"
OBJECT_DECLARE_SIMPLE_TYPE(CanBusState, CAN_BUS)

typedef struct CanBusClientInfo {
    bool (*can_receive)(CanBusClientState *);
    ssize_t (*receive)(CanBusClientState *,
        const struct qemu_can_frame *frames, size_t frames_cnt);
} CanBusClientInfo;

struct CanBusClientState {
    CanBusClientInfo *info;
    CanBusState *bus;
    int link_down;
    QTAILQ_ENTRY(CanBusClientState) next;
    CanBusClientState *peer;
    char *model;
    char *name;
    void (*destructor)(CanBusClientState *);
    bool fd_mode;
};

int can_bus_filter_match(struct qemu_can_filter *filter, qemu_canid_t can_id);

int can_bus_insert_client(CanBusState *bus, CanBusClientState *client);

int can_bus_remove_client(CanBusClientState *client);

ssize_t can_bus_client_send(CanBusClientState *,
                            const struct qemu_can_frame *frames,
                            size_t frames_cnt);

int can_bus_client_set_filters(CanBusClientState *,
                               const struct qemu_can_filter *filters,
                               size_t filters_cnt);

uint8_t can_dlc2len(uint8_t can_dlc);

uint8_t can_len2dlc(uint8_t len);

#endif
