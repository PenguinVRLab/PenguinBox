/*
 * Copyright (C) 2026 Patrick Carey
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "xemu-disc-xbe.h"

#define SECTOR 2048u

typedef struct Img {
    uint64_t size, win_off;
    uint8_t *win;
    size_t win_len;
} Img;

static int img_read(void *opaque, uint64_t off, void *buf, size_t len)
{
    Img *m = opaque;
    if (off + len > m->size) {
        return -1;
    }
    memset(buf, 0, len);
    for (size_t i = 0; i < len; i++) {
        uint64_t o = off + i;
        if (o >= m->win_off && o < m->win_off + m->win_len) {
            ((uint8_t *)buf)[i] = m->win[o - m->win_off];
        }
    }
    return 0;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}
static void put16(uint8_t *p, uint16_t v)
{
    p[0] = v; p[1] = v >> 8;
}

static uint32_t dirent(uint8_t *dir, uint32_t w, uint16_t left, uint16_t right,
                       uint32_t sector, uint32_t size, uint8_t attr, const char *name)
{
    uint8_t *e = dir + w * 4;
    size_t n = strlen(name);
    put16(e, left); put16(e + 2, right); put32(e + 4, sector); put32(e + 8, size);
    e[12] = attr; e[13] = (uint8_t)n;
    memcpy(e + 14, name, n);
    return w + (uint32_t)((14 + n + 3) / 4);
}

enum { XBE_SECTOR = 300, ROOT_SECTOR = 264, XBE_SIZE = 0x3000 };

static uint8_t *build_partition(size_t *win_len, const char *xbe_name, int flags)
{
    size_t len = (XBE_SECTOR * SECTOR) + XBE_SIZE;
    uint8_t *p = calloc(1, len);
    uint8_t *vd = p + 32 * SECTOR;
    if (!(flags & 1)) {
        memcpy(vd, "MICROSOFT*XBOX*MEDIA", 20);
        memcpy(vd + 0x7EC, "MICROSOFT*XBOX*MEDIA", 20);
    }
    uint32_t root_size = (flags & 16) ? 0x7FFFFFFF : 72;
    put32(vd + 20, ROOT_SECTOR);
    put32(vd + 24, root_size);
    uint8_t *dir = p + ROOT_SECTOR * SECTOR;
    memset(dir, 0xFF, SECTOR);
    uint32_t w = dirent(dir, 0, 0, 0, 280, SECTOR, 0x10, "media");
    uint32_t wa = w;
    w = dirent(dir, wa, 0xFFFF, 0xFFFF, 281, 10, 0x20, "readme.txt");
    uint32_t wb = w;
    dirent(dir, wb, 0xFFFF, 0xFFFF, XBE_SECTOR, XBE_SIZE, 0x20, (flags & 2) ? "other.xbe" : xbe_name);
    if (flags & 32) {
        put16(dir, (uint16_t)wb);
        put16(dir + 2, (uint16_t)wa);
        put16(dir + wa * 4, (uint16_t)wa);
        put16(dir + wa * 4 + 2, 0);
    } else {
        put16(dir, (uint16_t)wa);
        put16(dir + 2, (uint16_t)wb);
    }
    uint8_t *x = p + XBE_SECTOR * SECTOR;
    if (!(flags & 4)) {
        memcpy(x, "XBEH", 4);
    }
    put32(x + 0x104, 0x10000);
    put32(x + 0x108, 0x1000);
    put32(x + 0x118, (flags & 8) ? 0x10FF0 : 0x10200);
    put32(x + 0x124, 0x5);
    uint8_t *c = x + 0x200;
    put32(c + 0x08, 0x4E4F5401);
    const char *nm = "SYNTH TITLE \xC3\xA9";
    put16(c + 0x0C + 0, 'S'); put16(c + 0x0E, 'Y'); put16(c + 0x10, 'N'); put16(c + 0x12, 'T');
    put16(c + 0x14, 'H'); put16(c + 0x16, ' '); put16(c + 0x18, 'T'); put16(c + 0x1A, 'I');
    put16(c + 0x1C, 'T'); put16(c + 0x1E, 'L'); put16(c + 0x20, 'E'); put16(c + 0x22, ' ');
    put16(c + 0x24, 0x00E9);
    (void)nm;
    put32(c + 0xA0, 7);
    put32(c + 0xAC, 3);
    *win_len = len;
    return p;
}

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static int run(uint64_t base, const char *xbe_name, int flags, XemuDiscXbeInfo *info, char *err)
{
    Img m;
    m.win = build_partition(&m.win_len, xbe_name, flags);
    m.win_off = base;
    m.size = base + m.win_len + 4096;
    int r = xemu_disc_read_xbe_info_fn(img_read, &m, m.size, info, err, 256);
    free(m.win);
    return r;
}

static void expect_ok(const char *label, uint64_t base, const char *xbe_name)
{
    XemuDiscXbeInfo i;
    char err[256];
    int r = run(base, xbe_name, 0, &i, err);
    CHECK(r == 0, "%s: read failed: %s", label, err);
    if (r) {
        return;
    }
    char id[9];
    xemu_disc_title_id_str(i.title_id, id);
    CHECK(!strcmp(id, "4E4F5401"), "%s: title id %s", label, id);
    CHECK(i.init_flags == 5, "%s: init flags %x", label, i.init_flags);
    CHECK(i.init_flags & XEMU_XBE_INIT_LIMIT_DEVKIT_MEMORY, "%s: limit flag", label);
    CHECK(i.partition_base == base, "%s: base %llx", label, (unsigned long long)i.partition_base);
    CHECK(i.xbe_offset == base + XBE_SECTOR * SECTOR, "%s: xbe offset", label);
    CHECK(i.xbe_size == XBE_SIZE, "%s: xbe size", label);
    CHECK(i.cert_offset == 0x200, "%s: cert offset 0x%x", label, i.cert_offset);
    CHECK(i.game_region == 7 && i.version == 3, "%s: region %u version %u", label, i.game_region, i.version);
    CHECK(!strcmp(i.title_name, "SYNTH TITLE \xC3\xA9"), "%s: name '%s'", label, i.title_name);
    printf("ok   %s: %s '%s' flags 0x%x base 0x%llx\n", label, id, i.title_name, i.init_flags,
           (unsigned long long)i.partition_base);
}

static void expect_fail(const char *label, int flags, const char *want)
{
    XemuDiscXbeInfo i;
    char err[256];
    int r = run(0, "default.xbe", flags, &i, err);
    CHECK(r == -1, "%s: accepted a bad image", label);
    CHECK(strstr(err, want) != NULL, "%s: reason '%s', want '%s'", label, err, want);
    if (r == -1) {
        printf("ok   %s refused: %s\n", label, err);
    }
}

int main(void)
{
    expect_ok("xiso", 0, "default.xbe");
    expect_ok("redump-xgd1", 0x18300000ull, "default.xbe");
    expect_ok("upper-case name", 0, "DEFAULT.XBE");
    expect_fail("no volume", 1, "not an Xbox disc image");
    expect_fail("no default.xbe", 2, "no default.xbe");
    expect_fail("no XBEH", 4, "XBEH");
    expect_fail("certificate out of range", 8, "certificate");
    expect_fail("root out of range", 16, "root directory out of range");
    {
        XemuDiscXbeInfo i;
        char err[256];
        int r = run(0, "default.xbe", 32, &i, err);
        CHECK(r == 0 && i.title_id == 0x4E4F5401, "loop: %s", err);
        if (!r) {
            printf("ok   looping tree terminates\n");
        }
    }
    {
        char path[] = "/tmp/disc-xbe-test-XXXXXX";
        int fd = mkstemp(path);
        size_t len;
        uint8_t *p = build_partition(&len, "default.xbe", 0);
        FILE *f = fdopen(fd, "wb");
        fwrite(p, 1, len, f);
        fclose(f);
        free(p);
        XemuDiscXbeInfo i;
        char err[256];
        int r = xemu_disc_read_xbe_info(path, &i, err, sizeof(err));
        CHECK(r == 0 && i.title_id == 0x4E4F5401, "file path: %s", err);
        remove(path);
        r = xemu_disc_read_xbe_info(path, &i, err, sizeof(err));
        CHECK(r == -1, "missing file accepted");
        r = xemu_disc_read_xbe_info("", &i, err, sizeof(err));
        CHECK(r == -1 && strstr(err, "no disc image"), "empty path: %s", err);
        printf("ok   file path API\n");
    }
    printf("disc-xbe-test: %s (%d checks, %d failed)\n", fails ? "FAIL" : "PASS", checks, fails);
    return fails ? 1 : 0;
}
