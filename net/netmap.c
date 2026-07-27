/*
 * netmap access for qemu
 *
 * Copyright (c) 2012-2013 Luigi Rizzo
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
#include <sys/ioctl.h>
#include <net/if.h>
#define NETMAP_WITH_LIBS
#include <net/netmap.h>
#include <net/netmap_user.h>

#include "net/net.h"
#include "net/tap.h"
#include "clients.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qemu/iov.h"
#include "qemu/cutils.h"
#include "qemu/main-loop.h"

typedef struct NetmapState {
    NetClientState      nc;
    struct nm_desc      *nmd;
    char                ifname[IFNAMSIZ];
    struct netmap_ring  *tx;
    struct netmap_ring  *rx;
    bool                read_poll;
    bool                write_poll;
    struct iovec        iov[IOV_MAX];
    int                 vnet_hdr_len;
} NetmapState;

#ifndef __FreeBSD__
#define pkt_copy bcopy
#else
static inline void
pkt_copy(const void *_src, void *_dst, int l)
{
    const uint64_t *src = _src;
    uint64_t *dst = _dst;
    if (unlikely(l >= 1024)) {
        bcopy(src, dst, l);
        return;
    }
    for (; l > 0; l -= 64) {
        *dst++ = *src++;
        *dst++ = *src++;
        *dst++ = *src++;
        *dst++ = *src++;
        *dst++ = *src++;
        *dst++ = *src++;
        *dst++ = *src++;
        *dst++ = *src++;
    }
}
#endif

static struct nm_desc *netmap_open(const NetdevNetmapOptions *nm_opts,
                                   Error **errp)
{
    struct nm_desc *nmd;
    struct nmreq req;

    memset(&req, 0, sizeof(req));

    nmd = nm_open(nm_opts->ifname, &req, NETMAP_NO_TX_POLL,
                  NULL);
    if (nmd == NULL) {
        error_setg_errno(errp, errno, "Failed to nm_open() %s",
                         nm_opts->ifname);
        return NULL;
    }

    return nmd;
}

static void netmap_send(void *opaque);
static void netmap_writable(void *opaque);

static void netmap_update_fd_handler(NetmapState *s)
{
    qemu_set_fd_handler(s->nmd->fd,
                        s->read_poll ? netmap_send : NULL,
                        s->write_poll ? netmap_writable : NULL,
                        s);
}

static void netmap_read_poll(NetmapState *s, bool enable)
{
    if (s->read_poll != enable) {
        s->read_poll = enable;
        netmap_update_fd_handler(s);
    }
}

static void netmap_write_poll(NetmapState *s, bool enable)
{
    if (s->write_poll != enable) {
        s->write_poll = enable;
        netmap_update_fd_handler(s);
    }
}

static void netmap_poll(NetClientState *nc, bool enable)
{
    NetmapState *s = DO_UPCAST(NetmapState, nc, nc);

    if (s->read_poll != enable || s->write_poll != enable) {
        s->write_poll = enable;
        s->read_poll  = enable;
        netmap_update_fd_handler(s);
    }
}

static void netmap_writable(void *opaque)
{
    NetmapState *s = opaque;

    netmap_write_poll(s, false);
    qemu_flush_queued_packets(&s->nc);
}

static ssize_t netmap_receive_iov(NetClientState *nc,
                    const struct iovec *iov, int iovcnt)
{
    NetmapState *s = DO_UPCAST(NetmapState, nc, nc);
    struct netmap_ring *ring = s->tx;
    unsigned int tail = ring->tail;
    ssize_t totlen = 0;
    uint32_t last;
    uint32_t idx;
    uint8_t *dst;
    int j;
    uint32_t i;

    last = i = ring->head;

    if (nm_ring_space(ring) < iovcnt) {
        ring->cur = tail;
        netmap_write_poll(s, true);
        return 0;
    }

    for (j = 0; j < iovcnt; j++) {
        int iov_frag_size = iov[j].iov_len;
        int offset = 0;
        int nm_frag_size;

        totlen += iov_frag_size;

        while (iov_frag_size) {
            nm_frag_size = MIN(iov_frag_size, ring->nr_buf_size);

            if (unlikely(i == tail)) {
                ring->cur = tail;
                netmap_write_poll(s, true);
                return 0;
            }

            idx = ring->slot[i].buf_idx;
            dst = (uint8_t *)NETMAP_BUF(ring, idx);

            ring->slot[i].len = nm_frag_size;
            ring->slot[i].flags = NS_MOREFRAG;
            pkt_copy(iov[j].iov_base + offset, dst, nm_frag_size);

            last = i;
            i = nm_ring_next(ring, i);

            offset += nm_frag_size;
            iov_frag_size -= nm_frag_size;
        }
    }
    ring->slot[last].flags &= ~NS_MOREFRAG;

    ring->head = ring->cur = i;

    ioctl(s->nmd->fd, NIOCTXSYNC, NULL);

    return totlen;
}

static ssize_t netmap_receive(NetClientState *nc,
      const uint8_t *buf, size_t size)
{
    struct iovec iov;

    iov.iov_base = (void *)buf;
    iov.iov_len = size;

    return netmap_receive_iov(nc, &iov, 1);
}

static void netmap_send_completed(NetClientState *nc, ssize_t len)
{
    NetmapState *s = DO_UPCAST(NetmapState, nc, nc);

    netmap_read_poll(s, true);
}

static void netmap_send(void *opaque)
{
    NetmapState *s = opaque;
    struct netmap_ring *ring = s->rx;
    unsigned int tail = ring->tail;

    while (ring->head != tail) {
        uint32_t i = ring->head;
        uint32_t idx;
        bool morefrag;
        int iovcnt = 0;
        int iovsize;

        do {
            idx = ring->slot[i].buf_idx;
            morefrag = (ring->slot[i].flags & NS_MOREFRAG);
            s->iov[iovcnt].iov_base = (void *)NETMAP_BUF(ring, idx);
            s->iov[iovcnt].iov_len = ring->slot[i].len;
            iovcnt++;
            i = nm_ring_next(ring, i);
        } while (i != tail && morefrag);

        ring->cur = i;

        if (unlikely(morefrag)) {
            break;
        }

        iovsize = qemu_sendv_packet_async(&s->nc, s->iov, iovcnt,
                                            netmap_send_completed);

        ring->head = i;

        if (iovsize == 0) {
            netmap_read_poll(s, false);
            break;
        }
    }
}

static void netmap_cleanup(NetClientState *nc)
{
    NetmapState *s = DO_UPCAST(NetmapState, nc, nc);

    qemu_purge_queued_packets(nc);

    netmap_poll(nc, false);
    nm_close(s->nmd);
    s->nmd = NULL;
}

static int netmap_fd_set_vnet_hdr_len(NetmapState *s, int len)
{
    struct nmreq req;

    memset(&req, 0, sizeof(req));
    pstrcpy(req.nr_name, sizeof(req.nr_name), s->ifname);
    req.nr_version = NETMAP_API;
    req.nr_cmd = NETMAP_BDG_VNET_HDR;
    req.nr_arg1 = len;

    return ioctl(s->nmd->fd, NIOCREGIF, &req);
}

static bool netmap_has_vnet_hdr_len(NetClientState *nc, int len)
{
    NetmapState *s = DO_UPCAST(NetmapState, nc, nc);
    int prev_len = s->vnet_hdr_len;

    if (netmap_fd_set_vnet_hdr_len(s, len)) {
        return false;
    }

    if (netmap_fd_set_vnet_hdr_len(s, prev_len)) {
        error_report("Failed to restore vnet-hdr length %d on %s: %s",
                     prev_len, s->ifname, strerror(errno));
        abort();
    }

    return true;
}

static bool netmap_has_vnet_hdr(NetClientState *nc)
{
    return netmap_has_vnet_hdr_len(nc, sizeof(struct virtio_net_hdr));
}

static void netmap_set_vnet_hdr_len(NetClientState *nc, int len)
{
    NetmapState *s = DO_UPCAST(NetmapState, nc, nc);
    int err;

    err = netmap_fd_set_vnet_hdr_len(s, len);
    if (err) {
        error_report("Unable to set vnet-hdr length %d on %s: %s",
                     len, s->ifname, strerror(errno));
    } else {
        s->vnet_hdr_len = len;
    }
}

static void netmap_set_offload(NetClientState *nc, const NetOffloads *ol)
{
    NetmapState *s = DO_UPCAST(NetmapState, nc, nc);

    if (!s->vnet_hdr_len) {
        netmap_set_vnet_hdr_len(nc, sizeof(struct virtio_net_hdr));
    }
}

static NetClientInfo net_netmap_info = {
    .type = NET_CLIENT_DRIVER_NETMAP,
    .size = sizeof(NetmapState),
    .receive = netmap_receive,
    .receive_iov = netmap_receive_iov,
    .poll = netmap_poll,
    .cleanup = netmap_cleanup,
    .has_ufo = netmap_has_vnet_hdr,
    .has_vnet_hdr = netmap_has_vnet_hdr,
    .has_vnet_hdr_len = netmap_has_vnet_hdr_len,
    .set_offload = netmap_set_offload,
    .set_vnet_hdr_len = netmap_set_vnet_hdr_len,
};

int net_init_netmap(const Netdev *netdev,
                    const char *name, NetClientState *peer, Error **errp)
{
    const NetdevNetmapOptions *netmap_opts = &netdev->u.netmap;
    struct nm_desc *nmd;
    NetClientState *nc;
    Error *err = NULL;
    NetmapState *s;

    nmd = netmap_open(netmap_opts, &err);
    if (err) {
        error_propagate(errp, err);
        return -1;
    }
    nc = qemu_new_net_client(&net_netmap_info, peer, "netmap", name);
    s = DO_UPCAST(NetmapState, nc, nc);
    s->nmd = nmd;
    s->tx = NETMAP_TXRING(nmd->nifp, 0);
    s->rx = NETMAP_RXRING(nmd->nifp, 0);
    s->vnet_hdr_len = 0;
    pstrcpy(s->ifname, sizeof(s->ifname), netmap_opts->ifname);
    netmap_read_poll(s, true);

    return 0;
}

