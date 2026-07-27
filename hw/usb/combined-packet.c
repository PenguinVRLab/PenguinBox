/*
 * QEMU USB packet combining code (for input pipelining)
 *
 * Copyright(c) 2012 Red Hat, Inc.
 *
 * Red Hat Authors:
 * Hans de Goede <hdegoede@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/usb.h"
#include "qemu/iov.h"
#include "trace.h"

static void usb_combined_packet_add(USBCombinedPacket *combined, USBPacket *p)
{
    qemu_iovec_concat(&combined->iov, &p->iov, 0, p->iov.size);
    QTAILQ_INSERT_TAIL(&combined->packets, p, combined_entry);
    p->combined = combined;
}

static void usb_combined_packet_remove(USBCombinedPacket *combined,
                                       USBPacket *p)
{
    assert(p->combined == combined);
    p->combined = NULL;
    QTAILQ_REMOVE(&combined->packets, p, combined_entry);
    if (QTAILQ_EMPTY(&combined->packets)) {
        qemu_iovec_destroy(&combined->iov);
        g_free(combined);
    }
}

void usb_combined_input_packet_complete(USBDevice *dev, USBPacket *p)
{
    USBCombinedPacket *combined = p->combined;
    USBEndpoint *ep = p->ep;
    USBPacket *next;
    int status, actual_length;
    bool short_not_ok, done = false;

    if (combined == NULL) {
        usb_packet_complete_one(dev, p);
        goto leave;
    }

    assert(combined->first == p && p == QTAILQ_FIRST(&combined->packets));

    status = combined->first->status;
    actual_length = combined->first->actual_length;
    short_not_ok = QTAILQ_LAST(&combined->packets)->short_not_ok;

    QTAILQ_FOREACH_SAFE(p, &combined->packets, combined_entry, next) {
        if (!done) {
            if (actual_length >= p->iov.size) {
                p->actual_length = p->iov.size;
            } else {
                p->actual_length = actual_length;
                done = true;
            }
            if (done || next == NULL) {
                p->status = status;
            } else {
                p->status = USB_RET_SUCCESS;
            }
            p->short_not_ok = short_not_ok;
            usb_combined_packet_remove(combined, p);
            usb_packet_complete_one(dev, p);
            actual_length -= p->actual_length;
        } else {
            p->status = USB_RET_REMOVE_FROM_QUEUE;
            dev->port->ops->complete(dev->port, p);
        }
    }
leave:
    usb_ep_combine_input_packets(ep);
}

void usb_combined_packet_cancel(USBDevice *dev, USBPacket *p)
{
    USBCombinedPacket *combined = p->combined;
    assert(combined != NULL);
    USBPacket *first = p->combined->first;

    usb_combined_packet_remove(combined, p);
    if (p == first) {
        usb_device_cancel_packet(dev, p);
    }
}

void usb_ep_combine_input_packets(USBEndpoint *ep)
{
    USBPacket *p, *u, *next, *prev = NULL, *first = NULL;
    USBPort *port = ep->dev->port;
    int totalsize;

    assert(ep->pipeline);
    assert(ep->pid == USB_TOKEN_IN);

    QTAILQ_FOREACH_SAFE(p, &ep->queue, queue, next) {
        if (ep->halted) {
            p->status = USB_RET_REMOVE_FROM_QUEUE;
            port->ops->complete(port, p);
            continue;
        }

        if (p->state == USB_PACKET_ASYNC) {
            prev = p;
            continue;
        }
        usb_packet_check_state(p, USB_PACKET_QUEUED);

        if (prev && prev->short_not_ok) {
            break;
        }

        if (first) {
            if (first->combined == NULL) {
                USBCombinedPacket *combined = g_new0(USBCombinedPacket, 1);

                combined->first = first;
                QTAILQ_INIT(&combined->packets);
                qemu_iovec_init(&combined->iov, 2);
                usb_combined_packet_add(combined, first);
            }
            usb_combined_packet_add(first->combined, p);
        } else {
            first = p;
        }

        totalsize = (p->combined) ? p->combined->iov.size : p->iov.size;
        if ((p->iov.size % ep->max_packet_size) != 0 || !p->short_not_ok ||
                next == NULL ||
                (totalsize == (16 * KiB - 36) && p->int_req) ||
                totalsize > 1 * MiB - ep->max_packet_size) {
            usb_device_handle_data(ep->dev, first);
            assert(first->status == USB_RET_ASYNC);
            if (first->combined) {
                QTAILQ_FOREACH(u, &first->combined->packets, combined_entry) {
                    usb_packet_set_state(u, USB_PACKET_ASYNC);
                }
            } else {
                usb_packet_set_state(first, USB_PACKET_ASYNC);
            }
            first = NULL;
            prev = p;
        }
    }
}
