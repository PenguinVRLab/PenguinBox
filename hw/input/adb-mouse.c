/*
 * QEMU ADB mouse support
 *
 * Copyright (c) 2004 Fabrice Bellard
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

#include "qemu/osdep.h"
#include "ui/console.h"
#include "hw/input/adb.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "adb-internal.h"
#include "trace.h"
#include "qom/object.h"

OBJECT_DECLARE_TYPE(MouseState, ADBMouseClass, ADB_MOUSE)

struct MouseState {
    ADBDevice parent_obj;

    QemuInputHandlerState *hs;
    int buttons_state, last_buttons_state;
    int dx, dy, dz;
};


struct ADBMouseClass {
    ADBDeviceClass parent_class;

    DeviceRealize parent_realize;
};

#define ADB_MOUSE_BUTTON_LEFT   0x01
#define ADB_MOUSE_BUTTON_RIGHT  0x02

static void adb_mouse_handle_event(DeviceState *dev, QemuConsole *src,
                                   InputEvent *evt)
{
    MouseState *s = (MouseState *)dev;
    InputMoveEvent *move;
    InputBtnEvent *btn;
    static const int bmap[INPUT_BUTTON__MAX] = {
        [INPUT_BUTTON_LEFT]   = ADB_MOUSE_BUTTON_LEFT,
        [INPUT_BUTTON_RIGHT]  = ADB_MOUSE_BUTTON_RIGHT,
    };

    switch (evt->type) {
    case INPUT_EVENT_KIND_REL:
        move = evt->u.rel.data;
        if (move->axis == INPUT_AXIS_X) {
            s->dx += move->value;
        } else if (move->axis == INPUT_AXIS_Y) {
            s->dy += move->value;
        }
        break;

    case INPUT_EVENT_KIND_BTN:
        btn = evt->u.btn.data;
        if (bmap[btn->button]) {
            if (btn->down) {
                s->buttons_state |= bmap[btn->button];
            } else {
                s->buttons_state &= ~bmap[btn->button];
            }
        }
        break;

    default:
        break;
    }
}

static const QemuInputHandler adb_mouse_handler = {
    .name  = "QEMU ADB Mouse",
    .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_REL,
    .event = adb_mouse_handle_event,
};

static int adb_mouse_poll(ADBDevice *d, uint8_t *obuf)
{
    MouseState *s = ADB_MOUSE(d);
    int dx, dy;

    if (s->last_buttons_state == s->buttons_state &&
        s->dx == 0 && s->dy == 0) {
        return 0;
    }

    dx = s->dx;
    if (dx < -63) {
        dx = -63;
    } else if (dx > 63) {
        dx = 63;
    }

    dy = s->dy;
    if (dy < -63) {
        dy = -63;
    } else if (dy > 63) {
        dy = 63;
    }

    s->dx -= dx;
    s->dy -= dy;
    s->last_buttons_state = s->buttons_state;

    dx &= 0x7f;
    dy &= 0x7f;

    if (!(s->buttons_state & ADB_MOUSE_BUTTON_LEFT)) {
        dy |= 0x80;
    }
    if (!(s->buttons_state & ADB_MOUSE_BUTTON_RIGHT)) {
        dx |= 0x80;
    }

    obuf[0] = dy;
    obuf[1] = dx;
    return 2;
}

static int adb_mouse_request(ADBDevice *d, uint8_t *obuf,
                             const uint8_t *buf, int len)
{
    MouseState *s = ADB_MOUSE(d);
    int cmd, reg, olen;

    if ((buf[0] & 0x0f) == ADB_FLUSH) {
        s->buttons_state = s->last_buttons_state;
        s->dx = 0;
        s->dy = 0;
        s->dz = 0;
        trace_adb_device_mouse_flush();
        return 0;
    }

    cmd = buf[0] & 0xc;
    reg = buf[0] & 0x3;
    olen = 0;
    switch (cmd) {
    case ADB_WRITEREG:
        trace_adb_device_mouse_writereg(reg, buf[1]);
        switch (reg) {
        case 2:
            break;
        case 3:
            if (len != 3) {
                return 0;
            }

            switch (buf[2]) {
            case ADB_CMD_SELF_TEST:
                break;
            case ADB_CMD_CHANGE_ID:
            case ADB_CMD_CHANGE_ID_AND_ACT:
            case ADB_CMD_CHANGE_ID_AND_ENABLE:
                d->devaddr = buf[1] & 0xf;
                trace_adb_device_mouse_request_change_addr(d->devaddr);
                break;
            default:
                d->devaddr = buf[1] & 0xf;
                if (buf[2] == 1 || buf[2] == 2) {
                    d->handler = buf[2];
                }

                trace_adb_device_mouse_request_change_addr_and_handler(
                    d->devaddr, d->handler);
                break;
            }
        }
        break;
    case ADB_READREG:
        switch (reg) {
        case 0:
            olen = adb_mouse_poll(d, obuf);
            break;
        case 1:
            break;
        case 3:
            obuf[0] = d->devaddr;
            obuf[1] = d->handler;
            olen = 2;
            break;
        }
        trace_adb_device_mouse_readreg(reg, obuf[0], obuf[1]);
        break;
    }
    return olen;
}

static bool adb_mouse_has_data(ADBDevice *d)
{
    MouseState *s = ADB_MOUSE(d);

    return !(s->last_buttons_state == s->buttons_state &&
             s->dx == 0 && s->dy == 0);
}

static void adb_mouse_reset(DeviceState *dev)
{
    ADBDevice *d = ADB_DEVICE(dev);
    MouseState *s = ADB_MOUSE(dev);

    d->handler = 2;
    d->devaddr = ADB_DEVID_MOUSE;
    s->last_buttons_state = s->buttons_state = 0;
    s->dx = s->dy = s->dz = 0;
}

static const VMStateDescription vmstate_adb_mouse = {
    .name = "adb_mouse",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(parent_obj, MouseState, 0, vmstate_adb_device,
                       ADBDevice),
        VMSTATE_INT32(buttons_state, MouseState),
        VMSTATE_INT32(last_buttons_state, MouseState),
        VMSTATE_INT32(dx, MouseState),
        VMSTATE_INT32(dy, MouseState),
        VMSTATE_INT32(dz, MouseState),
        VMSTATE_END_OF_LIST()
    }
};

static void adb_mouse_realizefn(DeviceState *dev, Error **errp)
{
    MouseState *s = ADB_MOUSE(dev);
    ADBMouseClass *amc = ADB_MOUSE_GET_CLASS(dev);

    amc->parent_realize(dev, errp);

    s->hs = qemu_input_handler_register(dev, &adb_mouse_handler);
}

static void adb_mouse_initfn(Object *obj)
{
    ADBDevice *d = ADB_DEVICE(obj);

    d->devaddr = ADB_DEVID_MOUSE;
}

static void adb_mouse_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ADBDeviceClass *adc = ADB_DEVICE_CLASS(oc);
    ADBMouseClass *amc = ADB_MOUSE_CLASS(oc);

    device_class_set_parent_realize(dc, adb_mouse_realizefn,
                                    &amc->parent_realize);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);

    adc->devreq = adb_mouse_request;
    adc->devhasdata = adb_mouse_has_data;
    device_class_set_legacy_reset(dc, adb_mouse_reset);
    dc->vmsd = &vmstate_adb_mouse;
}

static const TypeInfo adb_mouse_type_info = {
    .name = TYPE_ADB_MOUSE,
    .parent = TYPE_ADB_DEVICE,
    .instance_size = sizeof(MouseState),
    .instance_init = adb_mouse_initfn,
    .class_init = adb_mouse_class_init,
    .class_size = sizeof(ADBMouseClass),
};

static void adb_mouse_register_types(void)
{
    type_register_static(&adb_mouse_type_info);
}

type_init(adb_mouse_register_types)
