/*
 * usb packet capture
 *
 * Copyright (c) 2021 Gerd Hoffmann <kraxel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/usb.h"

#define PCAP_MAGIC                   0xa1b2c3d4
#define PCAP_MAJOR                   2
#define PCAP_MINOR                   4

struct pcap_hdr {
    uint32_t magic_number;
    uint16_t version_major;
    uint16_t version_minor;
    int32_t  thiszone;
    uint32_t sigfigs;
    uint32_t snaplen;
    uint32_t network;
};

struct pcaprec_hdr {
    uint32_t ts_sec;
    uint32_t ts_usec;
    uint32_t incl_len;
    uint32_t orig_len;
};

#define LINKTYPE_USB_LINUX           189
#define LINKTYPE_USB_LINUX_MMAPPED   220

struct usbmon_packet {
    uint64_t id;
    unsigned char type;
    unsigned char xfer_type;
    unsigned char epnum;
    unsigned char devnum;
    uint16_t busnum;
    char flag_setup;
    char flag_data;
    int64_t ts_sec;
    int32_t ts_usec;
    int32_t status;
    unsigned int length;
    unsigned int len_cap;
    union {
        unsigned char setup[8];
        struct iso_rec {
            int32_t error_count;
            int32_t numdesc;
        } iso;
    } s;
    int32_t interval;
    int32_t start_frame;
    uint32_t xfer_flags;
    uint32_t ndesc;
};

#define CTRL_LEN                     4096
#define DATA_LEN                     256

static int usbmon_status(USBPacket *p)
{
    switch (p->status) {
    case USB_RET_SUCCESS:
        return 0;
    case USB_RET_NODEV:
        return -19;
    default:
        return -121;
    }
}

static unsigned int usbmon_epnum(USBPacket *p)
{
    unsigned epnum = 0;

    epnum |= p->ep->nr;
    epnum |= (p->pid == USB_TOKEN_IN) ? 0x80 : 0;
    return epnum;
}

static unsigned char usbmon_xfer_type[] = {
    [USB_ENDPOINT_XFER_CONTROL] = 2,
    [USB_ENDPOINT_XFER_ISOC]    = 0,
    [USB_ENDPOINT_XFER_BULK]    = 3,
    [USB_ENDPOINT_XFER_INT]     = 1,
};

static void do_usb_pcap_header(FILE *fp, struct usbmon_packet *packet)
{
    struct pcaprec_hdr header;
    struct timeval tv;

    gettimeofday(&tv, NULL);
    packet->ts_sec  = tv.tv_sec;
    packet->ts_usec = tv.tv_usec;

    header.ts_sec   = packet->ts_sec;
    header.ts_usec  = packet->ts_usec;
    header.incl_len = packet->len_cap;
    header.orig_len = packet->length + sizeof(*packet);
    fwrite(&header, sizeof(header), 1, fp);
    fwrite(packet, sizeof(*packet), 1, fp);
}

static void do_usb_pcap_ctrl(FILE *fp, USBPacket *p, bool setup)
{
    USBDevice *dev = p->ep->dev;
    bool in = dev->setup_buf[0] & USB_DIR_IN;
    struct usbmon_packet packet = {
        .id         = 0,
        .type       = setup ? 'S' : 'C',
        .xfer_type  = usbmon_xfer_type[USB_ENDPOINT_XFER_CONTROL],
        .epnum      = in ? 0x80 : 0,
        .devnum     = dev->addr,
        .flag_setup = setup ? 0 : '-',
        .flag_data  = '=',
        .length     = dev->setup_len,
    };
    int data_len = dev->setup_len;

    if (data_len > CTRL_LEN) {
        data_len = CTRL_LEN;
    }
    if (setup) {
        memcpy(packet.s.setup, dev->setup_buf, 8);
    } else {
        packet.status = usbmon_status(p);
    }

    if (in && setup) {
        packet.flag_data = '<';
        packet.length = 0;
        data_len  = 0;
    }
    if (!in && !setup) {
        packet.flag_data = '>';
        packet.length = 0;
        data_len  = 0;
    }

    packet.len_cap = data_len + sizeof(packet);
    do_usb_pcap_header(fp, &packet);
    if (data_len) {
        fwrite(dev->data_buf, data_len, 1, fp);
    }

    fflush(fp);
}

static void do_usb_pcap_data(FILE *fp, USBPacket *p, bool setup)
{
    struct usbmon_packet packet = {
        .id         = p->id,
        .type       = setup ? 'S' : 'C',
        .xfer_type  = usbmon_xfer_type[p->ep->type],
        .epnum      = usbmon_epnum(p),
        .devnum     = p->ep->dev->addr,
        .flag_setup = '-',
        .flag_data  = '=',
        .length     = p->iov.size,
    };
    int data_len = p->iov.size;

    if (p->ep->nr == 0) {
        return;
    }

    if (data_len > DATA_LEN) {
        data_len = DATA_LEN;
    }
    if (!setup) {
        packet.status = usbmon_status(p);
        if (packet.length > p->actual_length) {
            packet.length = p->actual_length;
        }
        if (data_len > p->actual_length) {
            data_len = p->actual_length;
        }
    }

    if (p->pid == USB_TOKEN_IN && setup) {
        packet.flag_data = '<';
        packet.length = 0;
        data_len  = 0;
    }
    if (p->pid == USB_TOKEN_OUT && !setup) {
        packet.flag_data = '>';
        packet.length = 0;
        data_len  = 0;
    }

    packet.len_cap = data_len + sizeof(packet);
    do_usb_pcap_header(fp, &packet);
    if (data_len) {
        void *buf = g_malloc(data_len);
        iov_to_buf(p->iov.iov, p->iov.niov, 0, buf, data_len);
        fwrite(buf, data_len, 1, fp);
        g_free(buf);
    }

    fflush(fp);
}

void usb_pcap_init(FILE *fp)
{
    struct pcap_hdr header = {
        .magic_number  = PCAP_MAGIC,
        .version_major = 2,
        .version_minor = 4,
        .snaplen       = MAX(CTRL_LEN, DATA_LEN) + sizeof(struct usbmon_packet),
        .network       = LINKTYPE_USB_LINUX_MMAPPED,
    };

    fwrite(&header, sizeof(header), 1, fp);
}

void usb_pcap_ctrl(USBPacket *p, bool setup)
{
    FILE *fp = p->ep->dev->pcap;

    if (!fp) {
        return;
    }

    do_usb_pcap_ctrl(fp, p, setup);
}

void usb_pcap_data(USBPacket *p, bool setup)
{
    FILE *fp = p->ep->dev->pcap;

    if (!fp) {
        return;
    }

    do_usb_pcap_data(fp, p, setup);
}
