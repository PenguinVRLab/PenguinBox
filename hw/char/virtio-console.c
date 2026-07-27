/*
 * Virtio Console and Generic Serial Port Devices
 *
 * Copyright Red Hat, Inc. 2009, 2010
 *
 * Authors:
 *  Amit Shah <amit.shah@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "chardev/char-fe.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "trace.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/virtio/virtio-serial.h"
#include "qapi/error.h"
#include "qapi/qapi-events-char.h"
#include "qom/object.h"

#define TYPE_VIRTIO_CONSOLE_SERIAL_PORT "virtserialport"
typedef struct VirtConsole VirtConsole;
DECLARE_INSTANCE_CHECKER(VirtConsole, VIRTIO_CONSOLE,
                         TYPE_VIRTIO_CONSOLE_SERIAL_PORT)

struct VirtConsole {
    VirtIOSerialPort parent_obj;

    CharFrontend chr;
    guint watch;
};

static gboolean chr_write_unblocked(void *do_not_use, GIOCondition cond,
                                    void *opaque)
{
    VirtConsole *vcon = opaque;

    vcon->watch = 0;
    virtio_serial_throttle_port(VIRTIO_SERIAL_PORT(vcon), false);
    return G_SOURCE_REMOVE;
}

static ssize_t flush_buf(VirtIOSerialPort *port,
                         const uint8_t *buf, ssize_t len)
{
    VirtConsole *vcon = VIRTIO_CONSOLE(port);
    ssize_t ret;

    if (!qemu_chr_fe_backend_connected(&vcon->chr)) {
        return len;
    }

    ret = qemu_chr_fe_write(&vcon->chr, buf, len);
    trace_virtio_console_flush_buf(port->id, len, ret);

    if (ret < len) {
        VirtIOSerialPortClass *k = VIRTIO_SERIAL_PORT_GET_CLASS(port);

        if (ret < 0)
            ret = 0;

        if (!k->is_console) {
            virtio_serial_throttle_port(port, true);
            if (!vcon->watch) {
                vcon->watch = qemu_chr_fe_add_watch(&vcon->chr,
                                                    G_IO_OUT|G_IO_HUP,
                                                    chr_write_unblocked, vcon);
            }
        }
    }
    return ret;
}

static void set_guest_connected(VirtIOSerialPort *port, int guest_connected)
{
    VirtConsole *vcon = VIRTIO_CONSOLE(port);
    DeviceState *dev = DEVICE(port);
    VirtIOSerialPortClass *k = VIRTIO_SERIAL_PORT_GET_CLASS(port);

    if (!k->is_console) {
        qemu_chr_fe_set_open(&vcon->chr, guest_connected);
    }

    if (dev->id) {
        qapi_event_send_vserport_change(dev->id, guest_connected);
    }
}

static void guest_writable(VirtIOSerialPort *port)
{
    VirtConsole *vcon = VIRTIO_CONSOLE(port);

    qemu_chr_fe_accept_input(&vcon->chr);
}

static int chr_can_read(void *opaque)
{
    VirtConsole *vcon = opaque;

    return virtio_serial_guest_ready(VIRTIO_SERIAL_PORT(vcon));
}

static void chr_read(void *opaque, const uint8_t *buf, int size)
{
    VirtConsole *vcon = opaque;
    VirtIOSerialPort *port = VIRTIO_SERIAL_PORT(vcon);

    trace_virtio_console_chr_read(port->id, size);
    virtio_serial_write(port, buf, size);
}

static void chr_event(void *opaque, QEMUChrEvent event)
{
    VirtConsole *vcon = opaque;
    VirtIOSerialPort *port = VIRTIO_SERIAL_PORT(vcon);

    trace_virtio_console_chr_event(port->id, event);
    switch (event) {
    case CHR_EVENT_OPENED:
        virtio_serial_open(port);
        break;
    case CHR_EVENT_CLOSED:
        if (vcon->watch) {
            g_source_remove(vcon->watch);
            vcon->watch = 0;
        }
        virtio_serial_close(port);
        break;
    case CHR_EVENT_BREAK:
    case CHR_EVENT_MUX_IN:
    case CHR_EVENT_MUX_OUT:
        break;
    }
}

static int chr_be_change(void *opaque)
{
    VirtConsole *vcon = opaque;
    VirtIOSerialPort *port = VIRTIO_SERIAL_PORT(vcon);
    VirtIOSerialPortClass *k = VIRTIO_SERIAL_PORT_GET_CLASS(port);

    if (k->is_console) {
        qemu_chr_fe_set_handlers(&vcon->chr, chr_can_read, chr_read,
                                 NULL, chr_be_change, vcon, NULL, true);
    } else {
        qemu_chr_fe_set_handlers(&vcon->chr, chr_can_read, chr_read,
                                 chr_event, chr_be_change, vcon, NULL, false);
    }

    if (vcon->watch) {
        g_source_remove(vcon->watch);
        vcon->watch = qemu_chr_fe_add_watch(&vcon->chr,
                                            G_IO_OUT | G_IO_HUP,
                                            chr_write_unblocked, vcon);
    }

    return 0;
}

static void virtconsole_enable_backend(VirtIOSerialPort *port, bool enable)
{
    VirtConsole *vcon = VIRTIO_CONSOLE(port);

    if (!qemu_chr_fe_backend_connected(&vcon->chr)) {
        return;
    }

    if (enable) {
        VirtIOSerialPortClass *k = VIRTIO_SERIAL_PORT_GET_CLASS(port);

        qemu_chr_fe_set_handlers(&vcon->chr, chr_can_read, chr_read,
                                 k->is_console ? NULL : chr_event,
                                 chr_be_change, vcon, NULL, false);
    } else {
        qemu_chr_fe_set_handlers(&vcon->chr, NULL, NULL, NULL,
                                 NULL, NULL, NULL, false);
    }
}

static void virtconsole_realize(DeviceState *dev, Error **errp)
{
    VirtIOSerialPort *port = VIRTIO_SERIAL_PORT(dev);
    VirtConsole *vcon = VIRTIO_CONSOLE(dev);
    VirtIOSerialPortClass *k = VIRTIO_SERIAL_PORT_GET_CLASS(dev);

    if (port->id == 0 && !k->is_console) {
        error_setg(errp, "Port number 0 on virtio-serial devices reserved "
                   "for virtconsole devices for backward compatibility.");
        return;
    }

    if (qemu_chr_fe_backend_connected(&vcon->chr)) {
        if (k->is_console) {
            qemu_chr_fe_set_handlers(&vcon->chr, chr_can_read, chr_read,
                                     NULL, chr_be_change,
                                     vcon, NULL, true);
            virtio_serial_open(port);
        } else {
            qemu_chr_fe_set_handlers(&vcon->chr, chr_can_read, chr_read,
                                     chr_event, chr_be_change,
                                     vcon, NULL, false);
        }
    }
}

static void virtconsole_unrealize(DeviceState *dev)
{
    VirtConsole *vcon = VIRTIO_CONSOLE(dev);

    if (vcon->watch) {
        g_source_remove(vcon->watch);
    }
}

static void virtconsole_class_init(ObjectClass *klass, const void *data)
{
    VirtIOSerialPortClass *k = VIRTIO_SERIAL_PORT_CLASS(klass);

    k->is_console = true;
}

static const TypeInfo virtconsole_info = {
    .name          = "virtconsole",
    .parent        = TYPE_VIRTIO_CONSOLE_SERIAL_PORT,
    .class_init    = virtconsole_class_init,
};

static const Property virtserialport_properties[] = {
    DEFINE_PROP_CHR("chardev", VirtConsole, chr),
};

static void virtserialport_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOSerialPortClass *k = VIRTIO_SERIAL_PORT_CLASS(klass);

    k->realize = virtconsole_realize;
    k->unrealize = virtconsole_unrealize;
    k->have_data = flush_buf;
    k->set_guest_connected = set_guest_connected;
    k->enable_backend = virtconsole_enable_backend;
    k->guest_writable = guest_writable;
    device_class_set_props(dc, virtserialport_properties);
}

static const TypeInfo virtserialport_info = {
    .name          = TYPE_VIRTIO_CONSOLE_SERIAL_PORT,
    .parent        = TYPE_VIRTIO_SERIAL_PORT,
    .instance_size = sizeof(VirtConsole),
    .class_init    = virtserialport_class_init,
};

static void virtconsole_register_types(void)
{
    type_register_static(&virtserialport_info);
    type_register_static(&virtconsole_info);
}

type_init(virtconsole_register_types)
