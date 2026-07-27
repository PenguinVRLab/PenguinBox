/*
 * Memory transaction attributes
 *
 * Copyright (c) 2015 Linaro Limited.
 *
 * Authors:
 *  Peter Maydell <peter.maydell@linaro.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef MEMATTRS_H
#define MEMATTRS_H

typedef struct MemTxAttrs {
    unsigned int secure:1;
    unsigned int space:2;
    unsigned int user:1;
    unsigned int memory:1;
    unsigned int debug:1;
    unsigned int requester_id:16;

    unsigned int pid:8;

    unsigned int address_type:1;

    bool unspecified;

    uint8_t _reserved1;
    uint16_t _reserved2;
} MemTxAttrs;

QEMU_BUILD_BUG_ON(sizeof(MemTxAttrs) > 8);

#define MEMTXATTRS_UNSPECIFIED ((MemTxAttrs) { .unspecified = true })

#define MEMTX_OK 0
#define MEMTX_ERROR             (1U << 0)
#define MEMTX_DECODE_ERROR      (1U << 1)
#define MEMTX_ACCESS_ERROR      (1U << 2)
typedef uint32_t MemTxResult;

#endif
