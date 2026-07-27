/*
 * CanoKey QEMU device header.
 *
 * Copyright (c) 2021-2022 Canokeys.org <contact@canokeys.org>
 * Written by Hongren (Zenithal) Zheng <i@zenithal.me>
 *
 * This code is licensed under the GPL v2 or later.
 */

#ifndef CANOKEY_H
#define CANOKEY_H

#include "hw/qdev-core.h"

#define TYPE_CANOKEY "canokey"
#define CANOKEY(obj) \
    OBJECT_CHECK(CanoKeyState, (obj), TYPE_CANOKEY)

#define CANOKEY_EP_NUM 3
#define CANOKEY_EP_IN_BUFFER_SIZE 2048

typedef enum {
    CANOKEY_EP_IN_WAIT,
    CANOKEY_EP_IN_READY,
    CANOKEY_EP_IN_STALL
} CanoKeyEPState;

typedef struct CanoKeyState {
    USBDevice dev;

    uint8_t ep_in[CANOKEY_EP_NUM][CANOKEY_EP_IN_BUFFER_SIZE];
    uint32_t ep_in_size[CANOKEY_EP_NUM];
    uint32_t ep_in_pos[CANOKEY_EP_NUM];
    CanoKeyEPState ep_in_state[CANOKEY_EP_NUM];

    uint8_t *ep_out[CANOKEY_EP_NUM];
    uint32_t ep_out_size[CANOKEY_EP_NUM];

    char *file;
} CanoKeyState;

#endif
