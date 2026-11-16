/*
 * Copyright (C) 2026 Patrick Carey
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef XEMU_DISC_XBE_STANDALONE
#include "qemu/osdep.h"
#else
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/types.h>
#endif
#include "xemu-disc-xbe.h"

#define SECTOR 2048u
#define VOLUME_OFFSET (32u * SECTOR)
#define MAGIC "MICROSOFT*XBOX*MEDIA"
#define MAGIC_LEN 20
#define MAX_DIR_BYTES (4u * 1024u * 1024u)
#define XBE_HEADER_READ 0x1000u

static const uint64_t known_bases[] = { 0, 0x18300000ull, 0x0FD90000ull,
                                        0x02080000ull };

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static void set_err(char *err, size_t errlen, const char *fmt, ...)
{
    va_list ap;
    if (!err || !errlen) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(err, errlen, fmt, ap);
    va_end(ap);
}

static int ieq(const uint8_t *a, size_t alen, const char *b)
{
    size_t blen = strlen(b);
    if (alen != blen) {
        return 0;
    }
    for (size_t i = 0; i < alen; i++) {
        unsigned char x = a[i], y = (unsigned char)b[i];
        if (x >= 'A' && x <= 'Z') {
            x += 32;
        }
        if (y >= 'A' && y <= 'Z') {
            y += 32;
        }
        if (x != y) {
            return 0;
        }
    }
    return 1;
}

static void utf16_to_utf8(const uint8_t *in, size_t n, char *out,
                          size_t outlen)
{
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t c = le16(in + 2 * i);
        if (c == 0) {
            break;
        }
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n) {
            uint32_t d = le16(in + 2 * (i + 1));
            if (d >= 0xDC00 && d <= 0xDFFF) {
                c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
                i++;
            }
        }
        char tmp[4];
        size_t k;
        if (c < 0x80) {
            tmp[0] = (char)c; k = 1;
        } else if (c < 0x800) {
            tmp[0] = (char)(0xC0 | (c >> 6));
            tmp[1] = (char)(0x80 | (c & 0x3F)); k = 2;
        } else if (c < 0x10000) {
            tmp[0] = (char)(0xE0 | (c >> 12));
            tmp[1] = (char)(0x80 | ((c >> 6) & 0x3F));
            tmp[2] = (char)(0x80 | (c & 0x3F)); k = 3;
        } else {
            tmp[0] = (char)(0xF0 | (c >> 18));
            tmp[1] = (char)(0x80 | ((c >> 12) & 0x3F));
            tmp[2] = (char)(0x80 | ((c >> 6) & 0x3F));
            tmp[3] = (char)(0x80 | (c & 0x3F)); k = 4;
        }
        if (o + k + 1 > outlen) {
            break;
        }
        memcpy(out + o, tmp, k);
        o += k;
    }
    out[o] = 0;
}

void xemu_disc_title_id_str(uint32_t title_id, char out[9])
{
    snprintf(out, 9, "%08X", title_id);
}

static int find_in_dir(const uint8_t *dir, uint32_t dir_size, const char *name,
                       uint32_t *sector, uint32_t *size)
{
    uint32_t stack[64];
    int sp = 0, r = -1;
    uint32_t words = dir_size / 4u + 1u;
    uint8_t *seen = calloc(words / 8u + 1u, 1);
    if (!seen) {
        return -1;
    }
    stack[sp++] = 0;
    while (sp > 0) {
        uint32_t off = stack[--sp], w = off / 4u;
        if (off + 14 > dir_size || (seen[w / 8u] & (1u << (w % 8u)))) {
            continue;
        }
        seen[w / 8u] |= (uint8_t)(1u << (w % 8u));
        const uint8_t *e = dir + off;
        uint16_t left = le16(e), right = le16(e + 2);
        uint8_t namelen = e[13];
        if (left == 0xFFFF && right == 0xFFFF && le32(e + 4) == 0xFFFFFFFFu) {
            continue;
        }
        if (off + 14 + namelen <= dir_size &&
            ieq(e + 14, namelen, name) && !(e[12] & 0x10)) {
            *sector = le32(e + 4);
            *size = le32(e + 8);
            r = 0;
            break;
        }
        if (left != 0 && left != 0xFFFF && sp < 64) {
            stack[sp++] = (uint32_t)left * 4u;
        }
        if (right != 0 && right != 0xFFFF && sp < 64) {
            stack[sp++] = (uint32_t)right * 4u;
        }
    }
    free(seen);
    return r;
}

int xemu_disc_read_xbe_info_fn(XemuDiscReadFn read_fn, void *opaque,
                               uint64_t image_size, XemuDiscXbeInfo *out,
                               char *err, size_t errlen)
{
    uint8_t vd[SECTOR];
    uint64_t base = 0;
    int found = 0;

    memset(out, 0, sizeof(*out));
    set_err(err, errlen, "%s", "");
    for (size_t i = 0; i < sizeof(known_bases) / sizeof(known_bases[0]); i++) {
        uint64_t b = known_bases[i];
        if (b + VOLUME_OFFSET + SECTOR > image_size) {
            continue;
        }
        if (read_fn(opaque, b + VOLUME_OFFSET, vd, SECTOR)) {
            continue;
        }
        if (!memcmp(vd, MAGIC, MAGIC_LEN) && !memcmp(vd + 0x7EC, MAGIC, MAGIC_LEN)) {
            base = b;
            found = 1;
            break;
        }
    }
    if (!found) {
        set_err(err, errlen, "not an Xbox disc image (no XDVDFS volume at a known offset)");
        return -1;
    }

    uint32_t root_sector = le32(vd + 20), root_size = le32(vd + 24);
    if (root_size == 0 || root_size > MAX_DIR_BYTES ||
        base + (uint64_t)root_sector * SECTOR + root_size > image_size) {
        set_err(err, errlen, "root directory out of range (sector %u, %u bytes)",
                root_sector, root_size);
        return -1;
    }
    uint8_t *dir = malloc(root_size);
    if (!dir) {
        set_err(err, errlen, "out of memory");
        return -1;
    }
    if (read_fn(opaque, base + (uint64_t)root_sector * SECTOR, dir, root_size)) {
        free(dir);
        set_err(err, errlen, "root directory read failed");
        return -1;
    }
    uint32_t xsec = 0, xsize = 0;
    int r = find_in_dir(dir, root_size, "default.xbe", &xsec, &xsize);
    free(dir);
    if (r) {
        set_err(err, errlen, "no default.xbe in the disc's root directory");
        return -1;
    }
    uint64_t xoff = base + (uint64_t)xsec * SECTOR;
    if (xsize < 0x180 || xoff + xsize > image_size) {
        set_err(err, errlen, "default.xbe out of range (sector %u, %u bytes)", xsec, xsize);
        return -1;
    }

    uint32_t hlen = xsize < XBE_HEADER_READ ? xsize : XBE_HEADER_READ;
    uint8_t hdr[XBE_HEADER_READ];
    if (read_fn(opaque, xoff, hdr, hlen)) {
        set_err(err, errlen, "default.xbe header read failed");
        return -1;
    }
    if (memcmp(hdr, "XBEH", 4)) {
        set_err(err, errlen, "default.xbe has no XBEH magic");
        return -1;
    }
    uint32_t xbase = le32(hdr + 0x104), sizeof_headers = le32(hdr + 0x108);
    uint32_t cert_va = le32(hdr + 0x118);
    if (cert_va < xbase || sizeof_headers > xsize) {
        set_err(err, errlen, "default.xbe certificate address out of range");
        return -1;
    }
    uint32_t cert_off = cert_va - xbase;
    uint8_t cert[0xB0];
    if (cert_off + sizeof(cert) > sizeof_headers ||
        read_fn(opaque, xoff + cert_off, cert, sizeof(cert))) {
        set_err(err, errlen, "default.xbe certificate unreadable");
        return -1;
    }

    out->partition_base = base;
    out->xbe_offset = xoff;
    out->xbe_size = xsize;
    out->init_flags = le32(hdr + 0x124);
    out->cert_offset = cert_off;
    out->title_id = le32(cert + 0x08);
    utf16_to_utf8(cert + 0x0C, 40, out->title_name, sizeof(out->title_name));
    out->game_region = le32(cert + 0xA0);
    out->version = le32(cert + 0xAC);
    return 0;
}

static int file_read(void *opaque, uint64_t offset, void *buf, size_t len)
{
    FILE *f = opaque;
    if (fseeko(f, (off_t)offset, SEEK_SET)) {
        return -1;
    }
    return fread(buf, 1, len, f) == len ? 0 : -1;
}

int xemu_disc_read_xbe_info(const char *image_path, XemuDiscXbeInfo *out,
                            char *err, size_t errlen)
{
    if (!image_path || !image_path[0]) {
        set_err(err, errlen, "no disc image");
        return -1;
    }
    FILE *f = fopen(image_path, "rb");
    if (!f) {
        set_err(err, errlen, "cannot open the disc image");
        return -1;
    }
    if (fseeko(f, 0, SEEK_END)) {
        fclose(f);
        set_err(err, errlen, "cannot size the disc image");
        return -1;
    }
    uint64_t size = (uint64_t)ftello(f);
    int r = xemu_disc_read_xbe_info_fn(file_read, f, size, out, err, errlen);
    fclose(f);
    return r;
}
