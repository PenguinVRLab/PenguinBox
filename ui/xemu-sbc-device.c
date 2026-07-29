/*
 * Copyright (C) 2026 the PenguinBox authors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "qemu/cutils.h"
#include "qemu/main-loop.h"
#include "qemu/timer.h"

#include "xemu-input.h"
#include "xemu-sbc-device.h"

#ifdef CONFIG_USB_LIBUSB
#ifndef CONFIG_WIN32
#include <poll.h>
#endif
#include <libusb.h>
#ifndef LIBUSB_API_VERSION
#define LIBUSB_API_VERSION LIBUSBX_API_VERSION
#endif
#if LIBUSB_API_VERSION >= 0x01000102
#define SBC_HAVE_HOTPLUG 1
#endif
#ifndef CONFIG_WIN32
#define SBC_HAVE_POLLFDS 1
#endif
#endif

#define SBC_LOG_PREFIX "sbc-host-reader: "

#define SBC_DEFAULT_EP_IN 0x82
#define SBC_DEFAULT_INTERFACE 0

#define SBC_XFER_COUNT 3
#define SBC_XFER_BUFSZ 64

#define SBC_RESCAN_MS 2000
#define SBC_PUMP_OPEN_MS 10
#define SBC_PUMP_IDLE_MS 250
#define SBC_DRAIN_MS 20

#define SBC_SOURCE_TICK_MS 4

#define SBC_REPLAY_MAX_GAP_US 100000
#define SBC_REPLAY_MAX_FRAMES (1u << 20)
#define SBC_REPLAY_BURST 64

#define SBC_CAPTURE_MAGIC "# xemu-sbc-capture 1"
#define SBC_CAPTURE_FLUSH_EVERY 64

typedef enum SbcSource {
    SBC_SRC_DEVICE = 0,
    SBC_SRC_SYNTHETIC,
    SBC_SRC_REPLAY,
} SbcSource;

typedef struct SbcCaptureFrame {
    uint32_t rel_us;
    uint8_t len;
    uint8_t raw[SBC_RAW_REPORT_MAX];
} SbcCaptureFrame;

typedef struct SbcDeviceCtx {
    bool module_ready;
    bool reader_live;

    SbcSource source;
    SbcDeviceStatus status;
    char detail[192];

    SbcFrame frame;
    uint64_t seq;
    uint64_t frames_received;
    uint64_t transfer_errors;
    uint64_t malformed_reports;

    FILE *cap_fp;
    uint64_t cap_count;
    unsigned cap_unflushed;

    QEMUTimer *source_timer;
    uint64_t synth_tick;
    SbcCaptureFrame *replay;
    size_t replay_count;
    size_t replay_idx;
    uint64_t replay_epoch_us;
    uint64_t replay_cursor_us;
    char replay_path[512];

#ifdef CONFIG_USB_LIBUSB
    libusb_context *ctx;
    libusb_device_handle *handle;
    QEMUTimer *supervisor;
    struct libusb_transfer *xfer[SBC_XFER_COUNT];
    uint8_t xbuf[SBC_XFER_COUNT][SBC_XFER_BUFSZ];
    bool xfer_active[SBC_XFER_COUNT];
    int in_flight;
    int iface;
    uint8_t ep_in;
    int xfer_len;
    bool claimed;
    bool close_pending;
    bool poll_driven;
    int rescan_flag;
    uint64_t last_scan_us;
    uint8_t bus, addr;
#ifdef SBC_HAVE_POLLFDS
    int fds[16];
    int nfds;
#endif
#ifdef SBC_HAVE_HOTPLUG
    libusb_hotplug_callback_handle hotplug_cb;
    bool hotplug_registered;
#endif
#endif
} SbcDeviceCtx;

static SbcDeviceCtx g_sbc;

static uint64_t sbc_now_us(void)
{
    return (uint64_t)qemu_clock_get_us(QEMU_CLOCK_REALTIME);
}

static void G_GNUC_PRINTF(1, 2) sbc_set_detail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_sbc.detail, sizeof(g_sbc.detail), fmt, ap);
    va_end(ap);
}

static bool sbc_host_reader_armed(void)
{
    static int cached = -1;

    if (cached < 0) {
        bool kill = getenv("XEMU_NO_SBC_HOST_READER") != NULL;
        bool want = getenv("XEMU_SBC_HOST_READER") != NULL;

        cached = (want && !kill) ? 1 : 0;
        if (kill && want) {
            fprintf(stderr, SBC_LOG_PREFIX
                    "XEMU_NO_SBC_HOST_READER overrides XEMU_SBC_HOST_READER "
                    "- host reader OFF\n");
        }
        if (cached) {
            fprintf(stderr, SBC_LOG_PREFIX
                    "ARMED (XEMU_SBC_HOST_READER) - libusb reader for "
                    "%04x:%04x, %d transfers in flight\n",
                    SBC_USB_VENDOR_ID, SBC_USB_PRODUCT_ID, SBC_XFER_COUNT);
        }
    }
    return cached != 0;
}

bool sbc_decode_report(const uint8_t *raw, uint8_t len, SbcFrame *out)
{
    if (out == NULL) {
        return false;
    }

    out->buttons = 0;
    out->toggle_levels = 0;
    out->gear_lever = 0;
    out->tuner_dial = 0;
    out->aiming_x = 0;
    out->aiming_y = 0;
    out->rotation_lever = 0;
    out->sight_change_x = 0;
    out->sight_change_y = 0;
    out->left_pedal = 0;
    out->middle_pedal = 0;
    out->right_pedal = 0;

    if (raw == NULL) {
        return false;
    }

    if (out->raw != raw) {
        unsigned n = MIN((unsigned)len, (unsigned)SBC_RAW_REPORT_MAX);
        memset(out->raw, 0, sizeof(out->raw));
        memcpy(out->raw, raw, n);
        out->raw_len = (uint8_t)n;
    }

    if (len < SBC_RAW_REPORT_EXPECTED) {
        return false;
    }

    out->buttons = (uint64_t)raw[2] | ((uint64_t)raw[3] << 8) |
                   ((uint64_t)raw[4] << 16) | ((uint64_t)raw[5] << 24) |
                   ((uint64_t)(raw[6] & 0x7f) << 32);
    out->toggle_levels = raw[6] & SBC_TOGGLE_BITS_HI;

    out->aiming_x = (uint16_t)(raw[8] | ((uint16_t)raw[9] << 8));
    out->aiming_y = (uint16_t)(raw[10] | ((uint16_t)raw[11] << 8));

    out->rotation_lever = (int16_t)(uint16_t)(raw[12] | ((uint16_t)raw[13] << 8));
    out->sight_change_x = (int16_t)(uint16_t)(raw[14] | ((uint16_t)raw[15] << 8));
    out->sight_change_y = (int16_t)(uint16_t)(raw[16] | ((uint16_t)raw[17] << 8));

    out->left_pedal = (uint16_t)(raw[18] | ((uint16_t)raw[19] << 8));
    out->middle_pedal = (uint16_t)(raw[20] | ((uint16_t)raw[21] << 8));
    out->right_pedal = (uint16_t)(raw[22] | ((uint16_t)raw[23] << 8));

    out->tuner_dial = raw[24] & 0x0f;
    out->gear_lever = raw[25];

    return true;
}

static const char *sbc_source_name(void)
{
    switch (g_sbc.source) {
    case SBC_SRC_SYNTHETIC:
        return "synthetic";
    case SBC_SRC_REPLAY:
        return "replay";
    case SBC_SRC_DEVICE:
    default:
        return "device";
    }
}

static void sbc_capture_write(const SbcFrame *f)
{
    char line[8 + 24 + 24 + 8 + SBC_RAW_REPORT_MAX * 3];
    int off;
    unsigned i;

    if (g_sbc.cap_fp == NULL) {
        return;
    }

    off = snprintf(line, sizeof(line), "%" PRIu64 " %" PRIu64 " %u", f->seq,
                   f->timestamp_us, (unsigned)f->raw_len);
    for (i = 0; i < (unsigned)f->raw_len && off > 0 &&
                (size_t)off < sizeof(line) - 4;
         i++) {
        off += snprintf(line + off, sizeof(line) - off, " %02x", f->raw[i]);
    }
    fputs(line, g_sbc.cap_fp);
    fputc('\n', g_sbc.cap_fp);

    g_sbc.cap_count++;
    if (++g_sbc.cap_unflushed >= SBC_CAPTURE_FLUSH_EVERY) {
        g_sbc.cap_unflushed = 0;
        fflush(g_sbc.cap_fp);
    }
}

bool sbc_device_capture_start(const char *path)
{
    FILE *fp;

    if (path == NULL || path[0] == '\0') {
        return false;
    }

    sbc_device_capture_stop();

    fp = fopen(path, "a");
    if (fp == NULL) {
        return false;
    }

    fprintf(fp, "%s\n", SBC_CAPTURE_MAGIC);
    fprintf(fp, "# source=%s vid=%04x pid=%04x", sbc_source_name(),
            SBC_USB_VENDOR_ID, SBC_USB_PRODUCT_ID);
#ifdef CONFIG_USB_LIBUSB
    if (g_sbc.handle != NULL) {
        fprintf(fp, " bus=%03u addr=%03u", (unsigned)g_sbc.bus,
                (unsigned)g_sbc.addr);
    }
#endif
    fprintf(fp, " started_us=%" PRIu64 "\n", sbc_now_us());
    fprintf(fp, "# columns: seq t_us len raw[len]\n");
    fflush(fp);

    g_sbc.cap_fp = fp;
    g_sbc.cap_count = 0;
    g_sbc.cap_unflushed = 0;
    return true;
}

void sbc_device_capture_stop(void)
{
    if (g_sbc.cap_fp == NULL) {
        return;
    }
    fflush(g_sbc.cap_fp);
    fclose(g_sbc.cap_fp);
    g_sbc.cap_fp = NULL;
    g_sbc.cap_unflushed = 0;
}

bool sbc_device_capture_active(void)
{
    return g_sbc.cap_fp != NULL;
}

uint64_t sbc_device_capture_count(void)
{
    return g_sbc.cap_count;
}

static void sbc_publish(const uint8_t *raw, unsigned len)
{
    SbcFrame *f = &g_sbc.frame;
    unsigned n = MIN(len, (unsigned)SBC_RAW_REPORT_MAX);
    bool ok;

    memset(f->raw, 0, sizeof(f->raw));
    memcpy(f->raw, raw, n);
    f->raw_len = (uint8_t)n;
    f->seq = ++g_sbc.seq;
    f->timestamp_us = sbc_now_us();

    ok = sbc_decode_report(f->raw, f->raw_len, f);

    if (!ok || len != SBC_RAW_REPORT_EXPECTED ||
        f->raw[1] != SBC_RAW_REPORT_EXPECTED) {
        g_sbc.malformed_reports++;
    }

    g_sbc.frames_received++;
    sbc_capture_write(f);
}

static uint16_t sbc_tri_u16(uint64_t t, uint32_t period, uint32_t phase)
{
    uint32_t half, x, v;

    if (period < 2) {
        return 0;
    }
    half = period / 2;
    x = (uint32_t)((t + phase) % period);
    v = (x < half) ? x : (period - x);
    return (uint16_t)(((uint64_t)v * 0xffffu) / half);
}

static int16_t sbc_tri_s16(uint64_t t, uint32_t period, uint32_t phase)
{
    return (int16_t)((int32_t)sbc_tri_u16(t, period, phase) - 0x8000);
}

static void sbc_synth_build(uint64_t t, uint8_t *raw)
{
    static const uint8_t gears[] = { 255, 1, 2, 3, 4, 5, 0, 254 };
    uint64_t buttons;
    unsigned chaser, toggles, tuner, gear;
    uint16_t ax, ay, lp, mp, rp;
    int16_t rot, scx, scy;

    memset(raw, 0, SBC_RAW_REPORT_EXPECTED);
    raw[0] = 0;
    raw[1] = SBC_RAW_REPORT_EXPECTED;

    chaser = (unsigned)((t / 50) % 34);
    toggles = (unsigned)((t / 250) % 32);
    tuner = (unsigned)((t / 175) % 16);
    gear = (unsigned)((t / 375) % ARRAY_SIZE(gears));

    buttons = 1ULL << chaser;
    buttons &= ~((uint64_t)SBC_TOGGLE_BITS_HI << 32);
    buttons |= (uint64_t)(toggles & 0x1f) << 34;

    raw[2] = (uint8_t)(buttons & 0xff);
    raw[3] = (uint8_t)((buttons >> 8) & 0xff);
    raw[4] = (uint8_t)((buttons >> 16) & 0xff);
    raw[5] = (uint8_t)((buttons >> 24) & 0xff);
    raw[6] = (uint8_t)((buttons >> 32) & 0x7f);

    ax = sbc_tri_u16(t, 1250, 0);
    ay = sbc_tri_u16(t, 1750, 300);
    raw[8] = (uint8_t)(ax & 0xff);
    raw[9] = (uint8_t)(ax >> 8);
    raw[10] = (uint8_t)(ay & 0xff);
    raw[11] = (uint8_t)(ay >> 8);

    rot = sbc_tri_s16(t, 900, 0);
    scx = sbc_tri_s16(t, 700, 120);
    scy = sbc_tri_s16(t, 1100, 260);
    raw[12] = (uint8_t)((uint16_t)rot & 0xff);
    raw[13] = (uint8_t)((uint16_t)rot >> 8);
    raw[14] = (uint8_t)((uint16_t)scx & 0xff);
    raw[15] = (uint8_t)((uint16_t)scx >> 8);
    raw[16] = (uint8_t)((uint16_t)scy & 0xff);
    raw[17] = (uint8_t)((uint16_t)scy >> 8);

    lp = sbc_tri_u16(t, 800, 0);
    mp = sbc_tri_u16(t, 800, 266);
    rp = sbc_tri_u16(t, 800, 533);
    raw[18] = (uint8_t)(lp & 0xff);
    raw[19] = (uint8_t)(lp >> 8);
    raw[20] = (uint8_t)(mp & 0xff);
    raw[21] = (uint8_t)(mp >> 8);
    raw[22] = (uint8_t)(rp & 0xff);
    raw[23] = (uint8_t)(rp >> 8);

    raw[24] = (uint8_t)tuner;
    raw[25] = gears[gear];
}

static void sbc_replay_free(void)
{
    g_free(g_sbc.replay);
    g_sbc.replay = NULL;
    g_sbc.replay_count = 0;
    g_sbc.replay_idx = 0;
    g_sbc.replay_cursor_us = 0;
    g_sbc.replay_path[0] = '\0';
}

static bool sbc_replay_load(const char *path)
{
    FILE *fp;
    char line[4096];
    SbcCaptureFrame *arr = NULL;
    size_t cap = 0, count = 0;
    uint64_t prev_t = 0;
    bool have_prev = false;
    unsigned long long bad_lines = 0;

    fp = fopen(path, "r");
    if (fp == NULL) {
        return false;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *p = line;
        char *end;
        unsigned long long seq, t_us, len;
        SbcCaptureFrame fr;
        unsigned i;

        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0') {
            continue;
        }

        seq = strtoull(p, &end, 10);
        if (end == p) {
            bad_lines++;
            continue;
        }
        p = end;
        t_us = strtoull(p, &end, 10);
        if (end == p) {
            bad_lines++;
            continue;
        }
        p = end;
        len = strtoull(p, &end, 10);
        if (end == p || len > SBC_RAW_REPORT_MAX) {
            bad_lines++;
            continue;
        }
        p = end;
        (void)seq;

        memset(&fr, 0, sizeof(fr));
        for (i = 0; i < len; i++) {
            unsigned long b = strtoul(p, &end, 16);
            if (end == p || b > 0xff) {
                break;
            }
            fr.raw[i] = (uint8_t)b;
            p = end;
        }
        if (i != len) {
            bad_lines++;
            continue;
        }
        fr.len = (uint8_t)len;

        if (!have_prev || t_us < prev_t) {
            fr.rel_us = 0;
        } else {
            uint64_t d = t_us - prev_t;
            fr.rel_us = (uint32_t)MIN(d, (uint64_t)SBC_REPLAY_MAX_GAP_US);
        }
        prev_t = t_us;
        have_prev = true;

        if (count == cap) {
            if (cap >= SBC_REPLAY_MAX_FRAMES) {
                break;
            }
            cap = cap ? cap * 2 : 1024;
            cap = MIN(cap, (size_t)SBC_REPLAY_MAX_FRAMES);
            arr = g_renew(SbcCaptureFrame, arr, cap);
        }
        arr[count++] = fr;
    }
    fclose(fp);

    if (count == 0) {
        g_free(arr);
        sbc_set_detail("replay: %s contains no usable frames (%llu "
                       "unparsable lines)",
                       path, bad_lines);
        return false;
    }

    sbc_replay_free();
    g_sbc.replay = arr;
    g_sbc.replay_count = count;
    g_sbc.replay_idx = 0;
    g_sbc.replay_cursor_us = 0;
    g_sbc.replay_epoch_us = sbc_now_us();
    pstrcpy(g_sbc.replay_path, sizeof(g_sbc.replay_path), path);
    return true;
}

#ifdef CONFIG_USB_LIBUSB

static void sbc_supervise(void *opaque);
static void sbc_schedule_supervise(int ms);

#ifdef SBC_HAVE_POLLFDS

static void sbc_handle_fd(void *opaque)
{
    struct timeval tv = { 0, 0 };

    if (g_sbc.ctx != NULL) {
        libusb_handle_events_timeout(g_sbc.ctx, &tv);
    }
}

static void LIBUSB_CALL sbc_add_fd(int fd, short events, void *user_data)
{
    int i;

    for (i = 0; i < g_sbc.nfds; i++) {
        if (g_sbc.fds[i] == fd) {
            break;
        }
    }
    if (i == g_sbc.nfds && g_sbc.nfds < (int)ARRAY_SIZE(g_sbc.fds)) {
        g_sbc.fds[g_sbc.nfds++] = fd;
    }
    g_sbc.poll_driven = true;
    qemu_set_fd_handler(fd, (events & POLLIN) ? sbc_handle_fd : NULL,
                        (events & POLLOUT) ? sbc_handle_fd : NULL, NULL);
}

static void LIBUSB_CALL sbc_del_fd(int fd, void *user_data)
{
    int i;

    qemu_set_fd_handler(fd, NULL, NULL, NULL);
    for (i = 0; i < g_sbc.nfds; i++) {
        if (g_sbc.fds[i] == fd) {
            g_sbc.fds[i] = g_sbc.fds[--g_sbc.nfds];
            break;
        }
    }
}

static void sbc_drop_all_fds(void)
{
    while (g_sbc.nfds > 0) {
        qemu_set_fd_handler(g_sbc.fds[--g_sbc.nfds], NULL, NULL, NULL);
    }
}

#else

static void sbc_drop_all_fds(void)
{
}

#endif

static const char *sbc_err_name(int rc)
{
    const char *n = libusb_error_name(rc);
    return n ? n : "LIBUSB_ERROR_UNKNOWN";
}

static SbcDeviceStatus sbc_status_from_libusb(int rc)
{
    switch (rc) {
    case LIBUSB_ERROR_ACCESS:
        return SBC_DEV_NO_PERMISSION;
    case LIBUSB_ERROR_NOT_SUPPORTED:
        return SBC_DEV_NO_DRIVER;
    case LIBUSB_ERROR_BUSY:
        return SBC_DEV_BUSY;
    case LIBUSB_ERROR_NO_DEVICE:
        return SBC_DEV_ABSENT;
    case LIBUSB_ERROR_NOT_FOUND:
#ifdef CONFIG_WIN32
        return SBC_DEV_NO_DRIVER;
#else
        return SBC_DEV_ABSENT;
#endif
    default:
        return SBC_DEV_ERROR;
    }
}

static void sbc_fail(int rc, const char *what)
{
    unsigned bus = g_sbc.bus, addr = g_sbc.addr;

    g_sbc.status = sbc_status_from_libusb(rc);
    switch (g_sbc.status) {
    case SBC_DEV_NO_PERMISSION:
        sbc_set_detail("%s failed: %s (bus %03u addr %03u) - no permission to "
                       "open %04x:%04x. Install the udev rule and replug.",
                       what, sbc_err_name(rc), bus, addr, SBC_USB_VENDOR_ID,
                       SBC_USB_PRODUCT_ID);
        break;
    case SBC_DEV_NO_DRIVER:
        sbc_set_detail("%s failed: %s (bus %03u addr %03u) - %04x:%04x is not "
                       "bound to a WinUSB-family driver. Run Zadig and bind "
                       "WinUSB to it, then replug.",
                       what, sbc_err_name(rc), bus, addr, SBC_USB_VENDOR_ID,
                       SBC_USB_PRODUCT_ID);
        break;
    case SBC_DEV_BUSY:
        sbc_set_detail("%s failed: %s (bus %03u addr %03u) - another process "
                       "holds interface %d of %04x:%04x (USB passthrough, or "
                       "a second copy of xemu). Close it and retry.",
                       what, sbc_err_name(rc), bus, addr, g_sbc.iface,
                       SBC_USB_VENDOR_ID, SBC_USB_PRODUCT_ID);
        break;
    case SBC_DEV_ABSENT:
        sbc_set_detail("%s failed: %s (bus %03u addr %03u) - device is gone "
                       "or was never openable there; replug it.",
                       what, sbc_err_name(rc), bus, addr);
        break;
    default:
        sbc_set_detail("%s failed: %s (%d) on bus %03u addr %03u.", what,
                       sbc_err_name(rc), rc, bus, addr);
        break;
    }
    fprintf(stderr, SBC_LOG_PREFIX "%s\n", g_sbc.detail);
}

static void LIBUSB_CALL sbc_xfer_cb(struct libusb_transfer *t)
{
    int slot = (int)(intptr_t)t->user_data;
    int rc;

    if (slot < 0 || slot >= SBC_XFER_COUNT) {
        return;
    }
    g_sbc.xfer_active[slot] = false;
    g_sbc.in_flight--;

    switch (t->status) {
    case LIBUSB_TRANSFER_COMPLETED:
        if (t->actual_length > 0) {
            sbc_publish(t->buffer, (unsigned)t->actual_length);
            if (g_sbc.status != SBC_DEV_READY) {
                g_sbc.status = SBC_DEV_READY;
            }
        }
        break;

    case LIBUSB_TRANSFER_TIMED_OUT:
        break;

    case LIBUSB_TRANSFER_CANCELLED:
        return;

    case LIBUSB_TRANSFER_NO_DEVICE:
        g_sbc.status = SBC_DEV_ABSENT;
        sbc_set_detail("controller disconnected (bus %03u addr %03u)",
                       (unsigned)g_sbc.bus, (unsigned)g_sbc.addr);
        g_sbc.close_pending = true;
        sbc_schedule_supervise(0);
        return;

    case LIBUSB_TRANSFER_STALL:
    case LIBUSB_TRANSFER_OVERFLOW:
    case LIBUSB_TRANSFER_ERROR:
    default:
        g_sbc.transfer_errors++;
        break;
    }

    if (g_sbc.close_pending || g_sbc.handle == NULL) {
        return;
    }

    rc = libusb_submit_transfer(t);
    if (rc == 0) {
        g_sbc.xfer_active[slot] = true;
        g_sbc.in_flight++;
    } else {
        g_sbc.transfer_errors++;
        if (g_sbc.in_flight == 0) {
            sbc_fail(rc, "resubmit");
            g_sbc.close_pending = true;
            sbc_schedule_supervise(0);
        }
    }
}

static void sbc_cancel_all(void)
{
    int i;

    for (i = 0; i < SBC_XFER_COUNT; i++) {
        if (g_sbc.xfer_active[i] && g_sbc.xfer[i] != NULL) {
            libusb_cancel_transfer(g_sbc.xfer[i]);
        }
    }
}

static void sbc_finish_close(void)
{
    int i;

    for (i = 0; i < SBC_XFER_COUNT; i++) {
        if (g_sbc.xfer[i] != NULL) {
            libusb_free_transfer(g_sbc.xfer[i]);
            g_sbc.xfer[i] = NULL;
        }
        g_sbc.xfer_active[i] = false;
    }
    if (g_sbc.handle != NULL) {
        if (g_sbc.claimed) {
            libusb_release_interface(g_sbc.handle, g_sbc.iface);
            g_sbc.claimed = false;
        }
        libusb_close(g_sbc.handle);
        g_sbc.handle = NULL;
    }
    g_sbc.close_pending = false;
    g_sbc.in_flight = 0;
}

static void sbc_drain_and_close(void)
{
    int guard = 0;

    if (g_sbc.handle == NULL) {
        sbc_finish_close();
        return;
    }
    g_sbc.close_pending = true;
    sbc_cancel_all();
    while (g_sbc.in_flight > 0 && guard++ < 200) {
        struct timeval tv = { 0, 5000 };
        libusb_handle_events_timeout(g_sbc.ctx, &tv);
    }
    sbc_finish_close();
}

static void sbc_pick_endpoint(libusb_device *dev)
{
    struct libusb_config_descriptor *cfg = NULL;
    int i, j, k;

    g_sbc.iface = SBC_DEFAULT_INTERFACE;
    g_sbc.ep_in = SBC_DEFAULT_EP_IN;
    g_sbc.xfer_len = SBC_RAW_REPORT_MAX;

    if (libusb_get_active_config_descriptor(dev, &cfg) != 0 || cfg == NULL) {
        return;
    }
    for (i = 0; i < cfg->bNumInterfaces; i++) {
        const struct libusb_interface *itf = &cfg->interface[i];
        for (j = 0; j < itf->num_altsetting; j++) {
            const struct libusb_interface_descriptor *id = &itf->altsetting[j];
            for (k = 0; k < id->bNumEndpoints; k++) {
                const struct libusb_endpoint_descriptor *ep =
                    &id->endpoint[k];
                if ((ep->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) !=
                    LIBUSB_ENDPOINT_IN) {
                    continue;
                }
                if ((ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) !=
                    LIBUSB_TRANSFER_TYPE_INTERRUPT) {
                    continue;
                }
                g_sbc.iface = id->bInterfaceNumber;
                g_sbc.ep_in = ep->bEndpointAddress;
                g_sbc.xfer_len =
                    MIN((int)ep->wMaxPacketSize, SBC_XFER_BUFSZ);
                if (g_sbc.xfer_len <= 0) {
                    g_sbc.xfer_len = SBC_RAW_REPORT_MAX;
                }
                libusb_free_config_descriptor(cfg);
                return;
            }
        }
    }
    libusb_free_config_descriptor(cfg);
}

static bool sbc_submit_all(void)
{
    int i, rc;

    for (i = 0; i < SBC_XFER_COUNT; i++) {
        if (g_sbc.xfer[i] == NULL) {
            g_sbc.xfer[i] = libusb_alloc_transfer(0);
            if (g_sbc.xfer[i] == NULL) {
                break;
            }
        }
        libusb_fill_interrupt_transfer(g_sbc.xfer[i], g_sbc.handle,
                                       g_sbc.ep_in, g_sbc.xbuf[i],
                                       g_sbc.xfer_len, sbc_xfer_cb,
                                       (void *)(intptr_t)i, 0);
        g_sbc.xfer[i]->flags = 0;

        rc = libusb_submit_transfer(g_sbc.xfer[i]);
        if (rc != 0) {
            if (g_sbc.in_flight == 0) {
                sbc_fail(rc, "submit");
                return false;
            }
            break;
        }
        g_sbc.xfer_active[i] = true;
        g_sbc.in_flight++;
    }
    return g_sbc.in_flight > 0;
}

static void sbc_try_open(void)
{
    libusb_device **list = NULL;
    ssize_t n, i;
    bool seen = false;
    int rc;

    if (g_sbc.ctx == NULL || g_sbc.handle != NULL) {
        return;
    }

    n = libusb_get_device_list(g_sbc.ctx, &list);
    if (n < 0) {
        g_sbc.status = SBC_DEV_ERROR;
        sbc_set_detail("libusb_get_device_list failed: %s",
                       sbc_err_name((int)n));
        return;
    }

    for (i = 0; i < n; i++) {
        struct libusb_device_descriptor desc;

        if (libusb_get_device_descriptor(list[i], &desc) != 0) {
            continue;
        }
        if (desc.idVendor != SBC_USB_VENDOR_ID ||
            desc.idProduct != SBC_USB_PRODUCT_ID) {
            continue;
        }

        seen = true;
        g_sbc.bus = libusb_get_bus_number(list[i]);
        g_sbc.addr = libusb_get_device_address(list[i]);

        rc = libusb_open(list[i], &g_sbc.handle);
        if (rc != 0) {
            g_sbc.handle = NULL;
            sbc_fail(rc, "open");
            break;
        }

        rc = libusb_set_auto_detach_kernel_driver(g_sbc.handle, 1);
        if (rc != 0 && rc != LIBUSB_ERROR_NOT_SUPPORTED) {
            fprintf(stderr,
                    SBC_LOG_PREFIX "auto-detach unavailable: %s (continuing)\n",
                    sbc_err_name(rc));
        }

        sbc_pick_endpoint(list[i]);

        rc = libusb_claim_interface(g_sbc.handle, g_sbc.iface);
        if (rc != 0) {
            sbc_fail(rc, "claim_interface");
            libusb_close(g_sbc.handle);
            g_sbc.handle = NULL;
            break;
        }
        g_sbc.claimed = true;

        if (!sbc_submit_all()) {
            sbc_finish_close();
            break;
        }

        g_sbc.status = SBC_DEV_READY;
        sbc_set_detail("open on bus %03u addr %03u, interface %d, EP 0x%02x, "
                       "%d transfers in flight",
                       (unsigned)g_sbc.bus, (unsigned)g_sbc.addr, g_sbc.iface,
                       (unsigned)g_sbc.ep_in, g_sbc.in_flight);
        fprintf(stderr, SBC_LOG_PREFIX "%s\n", g_sbc.detail);
        break;
    }

    if (!seen) {
        g_sbc.status = SBC_DEV_ABSENT;
        sbc_set_detail("no %04x:%04x on any USB bus", SBC_USB_VENDOR_ID,
                       SBC_USB_PRODUCT_ID);
    }

    libusb_free_device_list(list, 1);
}

#ifdef SBC_HAVE_HOTPLUG
static int LIBUSB_CALL sbc_hotplug_cb(libusb_context *ctx, libusb_device *dev,
                                      libusb_hotplug_event event,
                                      void *user_data)
{
    qatomic_set(&g_sbc.rescan_flag, 1);
    return 0;
}
#endif

static int sbc_tick_ms(void)
{
    if (g_sbc.close_pending) {
        return SBC_DRAIN_MS;
    }
    if (!g_sbc.poll_driven) {
        return g_sbc.handle ? SBC_PUMP_OPEN_MS : SBC_PUMP_IDLE_MS;
    }
    return SBC_RESCAN_MS;
}

static void sbc_schedule_supervise(int ms)
{
    if (g_sbc.supervisor == NULL) {
        return;
    }
    timer_mod(g_sbc.supervisor,
              qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + ms);
}

static void sbc_supervise(void *opaque)
{
    struct timeval tv = { 0, 0 };
    uint64_t now;

    if (g_sbc.ctx == NULL) {
        return;
    }

    libusb_handle_events_timeout(g_sbc.ctx, &tv);

    if (g_sbc.close_pending) {
        if (g_sbc.in_flight > 0) {
            sbc_cancel_all();
            sbc_schedule_supervise(SBC_DRAIN_MS);
            return;
        }
        sbc_finish_close();
    }

    if (g_sbc.source != SBC_SRC_DEVICE) {
        sbc_schedule_supervise(SBC_RESCAN_MS);
        return;
    }

    now = sbc_now_us();
    if (g_sbc.handle == NULL) {
        bool hot = qatomic_xchg(&g_sbc.rescan_flag, 0) != 0;
        if (hot || now - g_sbc.last_scan_us >= SBC_RESCAN_MS * 1000ULL) {
            g_sbc.last_scan_us = now;
            sbc_try_open();
        }
    } else {
        qatomic_set(&g_sbc.rescan_flag, 0);
    }

    sbc_schedule_supervise(sbc_tick_ms());
}

static bool sbc_transport_init(void)
{
#ifdef SBC_HAVE_POLLFDS
    const struct libusb_pollfd **poll;
#endif
    int rc;

    rc = libusb_init(&g_sbc.ctx);
    if (rc != 0) {
        g_sbc.ctx = NULL;
        g_sbc.status = SBC_DEV_ERROR;
        sbc_set_detail("libusb_init failed: %s", sbc_err_name(rc));
        fprintf(stderr, SBC_LOG_PREFIX "%s\n", g_sbc.detail);
        return false;
    }

#ifdef SBC_HAVE_POLLFDS
    libusb_set_pollfd_notifiers(g_sbc.ctx, sbc_add_fd, sbc_del_fd, NULL);
    poll = libusb_get_pollfds(g_sbc.ctx);
    if (poll != NULL) {
        int i;
        for (i = 0; poll[i] != NULL; i++) {
            sbc_add_fd(poll[i]->fd, poll[i]->events, NULL);
        }
#if LIBUSB_API_VERSION >= 0x01000104
        libusb_free_pollfds(poll);
#else
        free((void *)poll);
#endif
    }
#endif

    g_sbc.supervisor =
        timer_new_ms(QEMU_CLOCK_REALTIME, sbc_supervise, NULL);

#ifdef SBC_HAVE_HOTPLUG
    if (libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
        rc = libusb_hotplug_register_callback(
            g_sbc.ctx,
            LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED |
                LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT,
            LIBUSB_HOTPLUG_NO_FLAGS, SBC_USB_VENDOR_ID, SBC_USB_PRODUCT_ID,
            LIBUSB_HOTPLUG_MATCH_ANY, sbc_hotplug_cb, NULL,
            &g_sbc.hotplug_cb);
        g_sbc.hotplug_registered = (rc == LIBUSB_SUCCESS);
        if (!g_sbc.hotplug_registered) {
            fprintf(stderr,
                    SBC_LOG_PREFIX "hotplug unavailable (%s); falling back to "
                                   "a %d ms rescan\n",
                    sbc_err_name(rc), SBC_RESCAN_MS);
        }
    }
#endif

    g_sbc.reader_live = true;
    sbc_try_open();
    sbc_schedule_supervise(sbc_tick_ms());
    return true;
}

static void sbc_transport_shutdown(void)
{
    if (g_sbc.ctx == NULL) {
        return;
    }

    if (g_sbc.supervisor != NULL) {
        timer_del(g_sbc.supervisor);
    }

    sbc_drain_and_close();

#ifdef SBC_HAVE_HOTPLUG
    if (g_sbc.hotplug_registered) {
        libusb_hotplug_deregister_callback(g_sbc.ctx, g_sbc.hotplug_cb);
        g_sbc.hotplug_registered = false;
    }
#endif

#ifdef SBC_HAVE_POLLFDS
    libusb_set_pollfd_notifiers(g_sbc.ctx, NULL, NULL, NULL);
#endif
    sbc_drop_all_fds();

    libusb_exit(g_sbc.ctx);
    g_sbc.ctx = NULL;

    if (g_sbc.supervisor != NULL) {
        timer_free(g_sbc.supervisor);
        g_sbc.supervisor = NULL;
    }
    g_sbc.reader_live = false;
}

static void sbc_transport_set_source_changed(void)
{
    if (g_sbc.ctx == NULL) {
        return;
    }
    if (g_sbc.source != SBC_SRC_DEVICE && g_sbc.handle != NULL) {
        g_sbc.close_pending = true;
    }
    if (g_sbc.source == SBC_SRC_DEVICE) {
        g_sbc.last_scan_us = 0;
    }
    sbc_schedule_supervise(0);
}

#else

static bool sbc_transport_init(void)
{
    g_sbc.status = SBC_DEV_ABSENT;
    sbc_set_detail("built without libusb: the host-side Steel Battalion "
                   "reader is unavailable in this build");
    fprintf(stderr, SBC_LOG_PREFIX "%s\n", g_sbc.detail);
    return false;
}

static void sbc_transport_shutdown(void)
{
}

static void sbc_transport_set_source_changed(void)
{
}

#endif

static void sbc_source_tick(void *opaque);

static void sbc_source_timer_arm(void)
{
    if (g_sbc.source_timer == NULL) {
        return;
    }
    if (g_sbc.source == SBC_SRC_DEVICE) {
        timer_del(g_sbc.source_timer);
        return;
    }
    timer_mod(g_sbc.source_timer,
              qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + SBC_SOURCE_TICK_MS);
}

static void sbc_source_tick(void *opaque)
{
    uint8_t raw[SBC_RAW_REPORT_EXPECTED];

    switch (g_sbc.source) {
    case SBC_SRC_SYNTHETIC:
        sbc_synth_build(g_sbc.synth_tick++, raw);
        sbc_publish(raw, sizeof(raw));
        g_sbc.status = SBC_DEV_READY;
        break;

    case SBC_SRC_REPLAY: {
        uint64_t elapsed = sbc_now_us() - g_sbc.replay_epoch_us;
        unsigned burst = 0;

        while (g_sbc.replay_count > 0 && burst < SBC_REPLAY_BURST) {
            const SbcCaptureFrame *fr = &g_sbc.replay[g_sbc.replay_idx];
            uint64_t due = g_sbc.replay_cursor_us + fr->rel_us;

            if (due > elapsed) {
                break;
            }
            sbc_publish(fr->raw, fr->len);
            g_sbc.replay_cursor_us = due;
            burst++;

            if (++g_sbc.replay_idx >= g_sbc.replay_count) {
                g_sbc.replay_idx = 0;
                g_sbc.replay_cursor_us = 0;
                g_sbc.replay_epoch_us = sbc_now_us();
                elapsed = 0;
            }
        }
        g_sbc.status = SBC_DEV_READY;
        break;
    }

    case SBC_SRC_DEVICE:
    default:
        return;
    }

    sbc_source_timer_arm();
}

static void sbc_module_ready(void)
{
    if (g_sbc.module_ready) {
        return;
    }
    g_sbc.module_ready = true;
    g_sbc.status = SBC_DEV_ABSENT;
    g_sbc.source = SBC_SRC_DEVICE;
    g_sbc.detail[0] = '\0';
    g_sbc.source_timer = timer_new_ms(QEMU_CLOCK_REALTIME, sbc_source_tick,
                                      NULL);
}

bool sbc_device_init(void)
{
    sbc_module_ready();

#ifdef CONFIG_USB_LIBUSB
    if (g_sbc.ctx != NULL) {
        return true;
    }
#endif

    if (!sbc_host_reader_armed()) {
        g_sbc.status = SBC_DEV_ABSENT;
        sbc_set_detail("host reader disabled (set XEMU_SBC_HOST_READER=1 to "
                       "enable)");
        return false;
    }

    return sbc_transport_init();
}

void sbc_device_shutdown(void)
{
    sbc_transport_shutdown();

    if (g_sbc.source_timer != NULL) {
        timer_del(g_sbc.source_timer);
        timer_free(g_sbc.source_timer);
        g_sbc.source_timer = NULL;
    }
    sbc_device_capture_stop();
    sbc_replay_free();

    g_sbc.source = SBC_SRC_DEVICE;
    g_sbc.status = SBC_DEV_ABSENT;
    g_sbc.module_ready = false;
}

void sbc_device_get_state(SbcDeviceState *out)
{
    if (out == NULL) {
        return;
    }
    out->status = g_sbc.status;
    out->status_detail = g_sbc.detail;
    out->frame = g_sbc.frame;
    out->frames_received = g_sbc.frames_received;
    out->transfer_errors = g_sbc.transfer_errors;
    out->malformed_reports = g_sbc.malformed_reports;
}

void sbc_device_set_synthetic(bool enabled)
{
    sbc_module_ready();

    if (enabled) {
        if (g_sbc.source == SBC_SRC_SYNTHETIC) {
            return;
        }
        sbc_replay_free();
        g_sbc.source = SBC_SRC_SYNTHETIC;
        g_sbc.synth_tick = 0;
        g_sbc.status = SBC_DEV_READY;
        sbc_set_detail("SYNTHETIC source - generated frames, no hardware "
                       "involved; do not treat these as ground truth");
    } else {
        if (g_sbc.source == SBC_SRC_DEVICE) {
            return;
        }
        sbc_replay_free();
        g_sbc.source = SBC_SRC_DEVICE;
        g_sbc.status = SBC_DEV_ABSENT;
        sbc_set_detail("%s", "");
    }

    sbc_transport_set_source_changed();
    sbc_source_timer_arm();
}

bool sbc_device_synthetic_enabled(void)
{
    return g_sbc.source != SBC_SRC_DEVICE;
}

bool sbc_device_replay_file(const char *path)
{
    sbc_module_ready();

    if (path == NULL || path[0] == '\0') {
        return false;
    }
    if (!sbc_replay_load(path)) {
        return false;
    }

    g_sbc.source = SBC_SRC_REPLAY;
    g_sbc.status = SBC_DEV_READY;
    sbc_set_detail("REPLAY source - %zu frames from %s (looping); not "
                   "hardware ground truth",
                   g_sbc.replay_count, g_sbc.replay_path);

    sbc_transport_set_source_changed();
    sbc_source_timer_arm();
    return true;
}
