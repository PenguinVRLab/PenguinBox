/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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

#ifndef QEMU_NET_UTIL_H
#define QEMU_NET_UTIL_H


struct ip {
#if HOST_BIG_ENDIAN
    uint8_t ip_v:4,
            ip_hl:4;
#else
    uint8_t ip_hl:4,
            ip_v:4;
#endif
    uint8_t ip_tos;
    uint16_t ip_len;
    uint16_t ip_id;
    uint16_t ip_off;
#define IP_DF 0x4000
#define IP_MF 0x2000
#define IP_OFFMASK 0x1fff
    uint8_t ip_ttl;
    uint8_t ip_p;
    uint16_t ip_sum;
    struct in_addr ip_src, ip_dst;
} QEMU_PACKED;

static inline bool in6_equal_net(const struct in6_addr *a,
                                 const struct in6_addr *b,
                                 int prefix_len)
{
    if (memcmp(a, b, prefix_len / 8) != 0) {
        return 0;
    }

    if (prefix_len % 8 == 0) {
        return 1;
    }

    return a->s6_addr[prefix_len / 8] >> (8 - (prefix_len % 8))
        == b->s6_addr[prefix_len / 8] >> (8 - (prefix_len % 8));
}

#define TCPS_CLOSED             0
#define TCPS_LISTEN             1
#define TCPS_SYN_SENT           2
#define TCPS_SYN_RECEIVED       3
#define TCPS_ESTABLISHED        4
#define TCPS_CLOSE_WAIT         5
#define TCPS_FIN_WAIT_1         6
#define TCPS_CLOSING            7
#define TCPS_LAST_ACK           8
#define TCPS_FIN_WAIT_2         9
#define TCPS_TIME_WAIT          10

int net_parse_macaddr(uint8_t *macaddr, const char *p);

#endif
