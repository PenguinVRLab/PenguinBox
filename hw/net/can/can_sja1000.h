/*
 * CAN device - SJA1000 chip emulation for QEMU
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
#ifndef HW_CAN_SJA1000_H
#define HW_CAN_SJA1000_H

#include "exec/hwaddr.h"
#include "net/can_emu.h"

#define CAN_SJA_MEM_SIZE      128

#define SJA_MSG_MAX_LEN       13
#define SJA_RCV_BUF_LEN       64

typedef struct CanSJA1000State {
    uint8_t         mode;
    uint8_t         status_pel;
    uint8_t         interrupt_pel;
    uint8_t         interrupt_en;
    uint8_t         rxmsg_cnt;
    uint8_t         rxbuf_start;
    uint8_t         clock;

    uint8_t         code_mask[8];
    uint8_t         tx_buff[13];

    uint8_t         rx_buff[SJA_RCV_BUF_LEN];
    uint32_t        rx_ptr;
    uint32_t        rx_cnt;

    uint8_t         control;
    uint8_t         status_bas;
    uint8_t         interrupt_bas;
    uint8_t         code;
    uint8_t         mask;

    qemu_can_filter filter[4];

    qemu_irq          irq;
    CanBusClientState bus_client;
} CanSJA1000State;

enum SJA1000_PeliCAN_regs {
        SJA_MOD      = 0x00,
        SJA_CMR      = 0x01,
        SJA_SR       = 0x02,
        SJA_IR       = 0x03,
        SJA_IER      = 0x04,
        SJA_BTR0     = 0x06,
        SJA_BTR1     = 0x07,
        SJA_OCR      = 0x08,
        SJA_ALC      = 0x0b,
        SJA_ECC      = 0x0c,
        SJA_EWLR     = 0x0d,
        SJA_RXERR    = 0x0e,
        SJA_TXERR0   = 0x0e,
        SJA_TXERR1   = 0x0f,
        SJA_RMC      = 0x1d,
        SJA_RBSA     = 0x1e,
        SJA_FRM      = 0x10,
        SJA_ID0      = 0x11,
        SJA_ID1      = 0x12,
        SJA_ID2      = 0x13,
        SJA_ID3      = 0x14,

        SJA_DATS     = 0x13,
        SJA_DATE     = 0x15,
        SJA_ACR0     = 0x10,
        SJA_AMR0     = 0x14,
        SJA_PeliCAN_AC_LEN = 4,
        SJA_CDR      = 0x1f
};


enum SJA1000_BasicCAN_regs {
        SJA_BCAN_CTR = 0x00,
        SJA_BCAN_CMR = 0x01,
        SJA_BCAN_SR  = 0x02,
        SJA_BCAN_IR  = 0x03
};

void can_sja_hardware_reset(CanSJA1000State *s);

void can_sja_mem_write(CanSJA1000State *s, hwaddr addr, uint64_t val,
                       unsigned size);

uint64_t can_sja_mem_read(CanSJA1000State *s, hwaddr addr, unsigned size);

int can_sja_connect_to_bus(CanSJA1000State *s, CanBusState *bus);

void can_sja_disconnect(CanSJA1000State *s);

int can_sja_init(CanSJA1000State *s, qemu_irq irq);

bool can_sja_can_receive(CanBusClientState *client);

ssize_t can_sja_receive(CanBusClientState *client,
                        const qemu_can_frame *frames, size_t frames_cnt);

extern const VMStateDescription vmstate_can_sja;

#endif
