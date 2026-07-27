/* SPDX-License-Identifier: MIT */
/*
 * kbdif.h -- Xen virtual keyboard/mouse
 *
 * Copyright (C) 2005 Anthony Liguori <aliguori@us.ibm.com>
 * Copyright (C) 2006 Red Hat, Inc., Markus Armbruster <armbru@redhat.com>
 */

#ifndef __XEN_PUBLIC_IO_KBDIF_H__
#define __XEN_PUBLIC_IO_KBDIF_H__

#define XENKBD_TYPE_MOTION             1
#define XENKBD_TYPE_RESERVED           2
#define XENKBD_TYPE_KEY                3
#define XENKBD_TYPE_POS                4
#define XENKBD_TYPE_MTOUCH             5

#define XENKBD_MT_EV_DOWN              0
#define XENKBD_MT_EV_UP                1
#define XENKBD_MT_EV_MOTION            2
#define XENKBD_MT_EV_SYN               3
#define XENKBD_MT_EV_SHAPE             4
#define XENKBD_MT_EV_ORIENT            5

#define XENKBD_DRIVER_NAME             "vkbd"

#define XENKBD_FIELD_FEAT_DSBL_KEYBRD  "feature-disable-keyboard"
#define XENKBD_FIELD_FEAT_DSBL_POINTER "feature-disable-pointer"
#define XENKBD_FIELD_FEAT_ABS_POINTER  "feature-abs-pointer"
#define XENKBD_FIELD_FEAT_RAW_POINTER  "feature-raw-pointer"
#define XENKBD_FIELD_FEAT_MTOUCH       "feature-multi-touch"
#define XENKBD_FIELD_REQ_ABS_POINTER   "request-abs-pointer"
#define XENKBD_FIELD_REQ_RAW_POINTER   "request-raw-pointer"
#define XENKBD_FIELD_REQ_MTOUCH        "request-multi-touch"
#define XENKBD_FIELD_RING_GREF         "page-gref"
#define XENKBD_FIELD_EVT_CHANNEL       "event-channel"
#define XENKBD_FIELD_WIDTH             "width"
#define XENKBD_FIELD_HEIGHT            "height"
#define XENKBD_FIELD_MT_WIDTH          "multi-touch-width"
#define XENKBD_FIELD_MT_HEIGHT         "multi-touch-height"
#define XENKBD_FIELD_MT_NUM_CONTACTS   "multi-touch-num-contacts"
#define XENKBD_FIELD_UNIQUE_ID         "unique-id"

#define XENKBD_FIELD_RING_REF          "page-ref"

struct xenkbd_motion
{
    uint8_t type;
    int32_t rel_x;
    int32_t rel_y;
    int32_t rel_z;
};

/*
 * Key event (includes pointer buttons)
 *         0                1                 2               3        octet
 * +----------------+----------------+----------------+----------------+
 * |  _TYPE_KEY     |     pressed    |            reserved             | 4
 * +----------------+----------------+----------------+----------------+
 * |                              keycode                              | 8
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 12
 * +----------------+----------------+----------------+----------------+
 * |/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/|
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 40
 * +----------------+----------------+----------------+----------------+
 *
 * pressed - uint8_t, 1 if pressed; 0 otherwise
 * keycode - uint32_t, KEY_* from linux/input.h
 */

struct xenkbd_key
{
    uint8_t type;
    uint8_t pressed;
    uint32_t keycode;
};

struct xenkbd_position
{
    uint8_t type;
    int32_t abs_x;
    int32_t abs_y;
    int32_t rel_z;
};

struct xenkbd_mtouch {
    uint8_t type;
    uint8_t event_type;
    uint8_t contact_id;
    uint8_t reserved[5];
    union {
        struct {
            int32_t abs_x;
            int32_t abs_y;
        } pos;
        struct {
            uint32_t major;
            uint32_t minor;
        } shape;
        int16_t orientation;
    } u;
};

#define XENKBD_IN_EVENT_SIZE 40

union xenkbd_in_event
{
    uint8_t type;
    struct xenkbd_motion motion;
    struct xenkbd_key key;
    struct xenkbd_position pos;
    struct xenkbd_mtouch mtouch;
    char pad[XENKBD_IN_EVENT_SIZE];
};

#define XENKBD_OUT_EVENT_SIZE 40

union xenkbd_out_event
{
    uint8_t type;
    char pad[XENKBD_OUT_EVENT_SIZE];
};

#define XENKBD_IN_RING_SIZE 2048
#define XENKBD_IN_RING_LEN (XENKBD_IN_RING_SIZE / XENKBD_IN_EVENT_SIZE)
#define XENKBD_IN_RING_OFFS 1024
#define XENKBD_IN_RING(page) \
    ((union xenkbd_in_event *)((char *)(page) + XENKBD_IN_RING_OFFS))
#define XENKBD_IN_RING_REF(page, idx) \
    (XENKBD_IN_RING((page))[(idx) % XENKBD_IN_RING_LEN])

#define XENKBD_OUT_RING_SIZE 1024
#define XENKBD_OUT_RING_LEN (XENKBD_OUT_RING_SIZE / XENKBD_OUT_EVENT_SIZE)
#define XENKBD_OUT_RING_OFFS (XENKBD_IN_RING_OFFS + XENKBD_IN_RING_SIZE)
#define XENKBD_OUT_RING(page) \
    ((union xenkbd_out_event *)((char *)(page) + XENKBD_OUT_RING_OFFS))
#define XENKBD_OUT_RING_REF(page, idx) \
    (XENKBD_OUT_RING((page))[(idx) % XENKBD_OUT_RING_LEN])

struct xenkbd_page
{
    uint32_t in_cons, in_prod;
    uint32_t out_cons, out_prod;
};

#endif

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
