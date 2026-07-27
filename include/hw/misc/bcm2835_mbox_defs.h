/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef BCM2835_MBOX_DEFS_H
#define BCM2835_MBOX_DEFS_H

#define MBOX_CHAN_POWER    0
#define MBOX_CHAN_FB       1
#define MBOX_CHAN_VCHIQ    3
#define MBOX_CHAN_PROPERTY 8
#define MBOX_CHAN_COUNT    9

#define MBOX_SIZE          32
#define MBOX_INVALID_DATA  0x0f

#define MBOX_AS_CHAN_SHIFT 4
#define MBOX_AS_DATA       0
#define MBOX_AS_PENDING    4

#endif
