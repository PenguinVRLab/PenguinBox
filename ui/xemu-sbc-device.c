/*
 * PenguinBox — real Steel Battalion controller: host-side device layer
 *
 * Implements ui/xemu-sbc-device.h — THE CONTRACT. One owner of the device
 * handle, raw reports preserved verbatim, and a source-agnostic interface so
 * the diagnostics panel (W1a) and the input merge layer (W1/W2) consume the
 * same frames whether they came from real hardware, the synthetic generator,
 * or a replayed capture.
 *
 * See docs/features/sbc-zero-setup-plan.md §4 (W1a, W2) and
 * docs/features/sbc-input-plan.md §5.
 *
 * ---------------------------------------------------------------------------
 * FOUR THINGS THIS FILE DELIBERATELY DOES NOT DO LIKE hw/usb/host-libusb.c
 * ---------------------------------------------------------------------------
 * 1. NO explicit libusb_detach_kernel_driver(). We use
 *    libusb_set_auto_detach_kernel_driver(h, 1), which on Linux goes through
 *    USBFS_DISCONNECT_CLAIM — an atomic detach-and-claim the kernel undoes
 *    when our fd closes, INCLUDING on SIGKILL or a segfault. The explicit
 *    path host-libusb.c uses leaves the device driverless until replug if we
 *    die abnormally, which for an input device the user is holding is the
 *    worst possible failure mode.
 * 2. OUR OWN libusb context. Never the NULL default context: sharing one with
 *    the passthrough transport would couple this reader's lifetime, log level
 *    and event pumping to a subsystem we do not own.
 * 3. NO READER THREAD. libusb's pollfds are pushed into the QEMU main loop
 *    (libusb_set_pollfd_notifiers + qemu_set_fd_handler), which is the
 *    in-tree pattern and the one part of host-libusb.c worth copying.
 *    Completion callbacks therefore run on the main-loop thread under the
 *    same BQL as the rest of the input path: publishing needs no new mutex
 *    and introduces no new race. (Platforms with no pollfd support — Windows
 *    — get a fast timer instead; still no thread of ours.)
 * 4. SBC_XFER_COUNT (3) ASYNC TRANSFERS IN FLIGHT, resubmitted from the
 *    completion callback. A single transfer in flight leaves a gap in the bus
 *    schedule every time we turn one around. LIBUSB_TRANSFER_FREE_TRANSFER is
 *    never set on these — they are recycled, not freed, by the callback.
 *
 * The hotplug callback may be delivered from libusb's internal event thread,
 * so it does nothing but set a flag; every structural operation (open, claim,
 * close, free) happens in the supervisor timer on the main loop.
 *
 * ---------------------------------------------------------------------------
 * CAPTURE FILE FORMAT — magic line "# xemu-sbc-capture 1"
 * ---------------------------------------------------------------------------
 * Plain ASCII, line-oriented, append-only. Text rather than binary because a
 * community member with a controller must be able to produce one, eyeball it,
 * and mail it to us, and because our golden-vector tests should parse it in
 * ten lines of any language.
 *
 *   # xemu-sbc-capture 1                 <- magic; first line of a session
 *   # source=device vid=0a7b pid=d000 bus=001 addr=007 started_us=123456789
 *   # columns: seq t_us len raw[len]     <- literal, for humans
 *   4711 1027384512 26 00 1a 00 00 00 00 00 00 00 80 00 80 ...
 *
 * Guarantees a parser may rely on:
 *   - Lines starting with '#' are comments/metadata; blank lines are ignored.
 *   - A data line is whitespace-separated: three unsigned decimal fields
 *     (seq, host-monotonic microseconds at completion, byte count) followed
 *     by exactly `len` two-digit lowercase hex bytes in WIRE ORDER. Nothing
 *     is byte-swapped on the way in or on the way out; the multi-byte fields
 *     stay little-endian exactly as the device sent them.
 *   - `len` is what actually arrived, normally 26 (SBC_RAW_REPORT_EXPECTED).
 *     Short and over-long reads are recorded verbatim rather than dropped —
 *     that is diagnostic data, not corruption to hide.
 *   - `seq` is monotonic within a run but NOT dense across sessions. Starting
 *     a capture appends a fresh header block to the file, so one file may
 *     hold several sessions; treat every magic line as "a new session starts
 *     here". Stopping and restarting is therefore always safe.
 *   - Metadata always carries source=device|synthetic|replay. ONLY
 *     source=device is hardware ground truth. Synthetic frames are ours, not
 *     the controller's, and must never be promoted into a golden vector.
 *
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

#include "xemu-input.h" /* SBC_TOGGLE_BITS_HI — one definition, not two */
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
/* libusb exposes pollfds on POSIX only; Windows has no fd to hand the main
 * loop, so that build pumps events from the supervisor timer instead. Same
 * split hw/usb/host-libusb.c makes. */
#ifndef CONFIG_WIN32
#define SBC_HAVE_POLLFDS 1
#endif
#endif /* CONFIG_USB_LIBUSB */

#define SBC_LOG_PREFIX "sbc-host-reader: "

/* Interrupt IN endpoint of the real controller (and of our emulated one:
 * STEEL_BATTALION_IN_ENDPOINT_ID | USB_DIR_IN, hw/xbox/xid-steel-battalion.c).
 * Only a fallback — the descriptors are walked first. */
#define SBC_DEFAULT_EP_IN 0x82
#define SBC_DEFAULT_INTERFACE 0

/* TRAP 4: 2-4 in flight. Three keeps the schedule fed with one turning around
 * and one spare, at a cost of 3 * 64 bytes of DMA buffer. */
#define SBC_XFER_COUNT 3
#define SBC_XFER_BUFSZ 64

/* host-libusb.c rescans the bus every 2000 ms; there is no reason for a
 * controller-shaped fallback poll to be more aggressive than QEMU's own. */
#define SBC_RESCAN_MS 2000
/* Only used where libusb offers no pollfds (Windows): we must pump the event
 * loop ourselves, so the tick has to be short while a device is open. */
#define SBC_PUMP_OPEN_MS 10
#define SBC_PUMP_IDLE_MS 250
/* Retry cadence while a close is being drained. */
#define SBC_DRAIN_MS 20

/* Synthetic/replay cadence. The real device's bInterval is 4 (4 ms at full
 * speed), so a synthetic source ticking at 4 ms produces a frame rate a
 * consumer cannot distinguish from hardware. */
#define SBC_SOURCE_TICK_MS 4

/* A replay must not stall on a gap in the capture (the user walked away mid
 * recording); inter-frame deltas are clamped at load time. */
#define SBC_REPLAY_MAX_GAP_US 100000
#define SBC_REPLAY_MAX_FRAMES (1u << 20) /* ~50 MB of records; plenty */
#define SBC_REPLAY_BURST 64              /* frames published per tick, max */

#define SBC_CAPTURE_MAGIC "# xemu-sbc-capture 1"
#define SBC_CAPTURE_FLUSH_EVERY 64

typedef enum SbcSource {
    SBC_SRC_DEVICE = 0, /* real hardware via libusb */
    SBC_SRC_SYNTHETIC,  /* generated frames */
    SBC_SRC_REPLAY,     /* a capture file */
} SbcSource;

typedef struct SbcCaptureFrame {
    uint32_t rel_us; /* offset from the previous frame, gap-clamped */
    uint8_t len;
    uint8_t raw[SBC_RAW_REPORT_MAX];
} SbcCaptureFrame;

typedef struct SbcDeviceCtx {
    bool module_ready; /* timers created */
    bool reader_live;  /* libusb context up and the lever armed */

    SbcSource source;
    SbcDeviceStatus status;
    char detail[192];

    SbcFrame frame;
    uint64_t seq;
    uint64_t frames_received;
    uint64_t transfer_errors;
    uint64_t malformed_reports;

    /* capture */
    FILE *cap_fp;
    uint64_t cap_count;
    unsigned cap_unflushed;

    /* synthetic + replay share one timer */
    QEMUTimer *source_timer;
    uint64_t synth_tick;
    SbcCaptureFrame *replay;
    size_t replay_count;
    size_t replay_idx;
    uint64_t replay_epoch_us; /* host time the current pass started */
    uint64_t replay_cursor_us; /* accumulated rel_us of frames published */
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
    bool poll_driven; /* libusb handed us pollfds (i.e. not Windows) */
    int rescan_flag;  /* set by the hotplug callback, possibly off-thread */
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
#endif /* CONFIG_USB_LIBUSB */
} SbcDeviceCtx;

static SbcDeviceCtx g_sbc;

/* ------------------------------------------------------------------------ */
/* Small helpers                                                            */
/* ------------------------------------------------------------------------ */

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

/*
 * Lever contract (docs/internal/A2-perf-levers.md): XEMU_SBC_HOST_READER
 * arms, XEMU_NO_SBC_HOST_READER kills, kill wins, getenv-once, default OFF,
 * stderr banner when engaged so a script can prove the run had it on.
 *
 * NOTE the scope: this gates the LIBUSB path only. The synthetic and replay
 * sources are how the diagnostics panel is developed with no hardware at all,
 * so they are gated by their own explicit API calls and stay available
 * whatever this returns.
 */
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

/* ------------------------------------------------------------------------ */
/* Decode — pure, no device, no libusb. The off-rig golden-vector tests call  */
/* this directly.                                                            */
/* ------------------------------------------------------------------------ */

/*
 * Wire layout, XIDSteelBattalionReport (hw/xbox/xid-steel-battalion.c:36-55),
 * 26 bytes, QEMU_PACKED, little-endian:
 *
 *   0      bReportId            (0)
 *   1      bLength              (0x1a = 26)
 *   2..5   dwButtons            button bits 0..31
 *   6      bMoreButtons         button bits 32..38; bits 34-38 are the five
 *                               LATCHING toggles (SBC_TOGGLE_BITS_HI = 0x7c)
 *   7      reserved             the one input byte nobody has ever dumped;
 *                               preserved in raw[], never invented
 *   8..9   aiming X, uint16     QEMU models this as bAimingX@9 "only high
 *                               byte is used"; byte 8 is the undumped low
 *                               half. We publish the full 16-bit LE value so
 *                               (aiming_x >> 8) == bAimingX exactly.
 *   10..11 aiming Y, uint16     ditto (QEMU's bPadding@10 is the low half)
 *   12..13 sRotationLever int16 only high byte used by the guest
 *   14..15 sSightChangeX  int16 only high byte used by the guest
 *   16..17 sSightChangeY  int16 only high byte used by the guest
 *   18..19 wLeftPedal    uint16 UNSIGNED 0..0xFFFF (SBC_PEDAL_FULL) — this is
 *   20..21 wMiddlePedal  uint16 the D3 trap: the shared axis[] is int16_t and
 *   22..23 wRightPedal   uint16 32767 reads as only ~50% travel to the game.
 *   24     ucTunerDial          low nibble 0..15 (9 o'clock = 0, 6 = 12); the
 *                               upper nibble is undumped, so it is masked off
 *                               here and left visible in raw[].
 *   25     ucGearLever          254=R 255=N 1..5; 0 = "between gears" and is
 *                               emitted transiently by real hardware.
 *
 * buttons carries all 39 bits AT THEIR PHYSICAL LEVEL, toggles included;
 * toggle_levels carries the five toggles again, still in their bMoreButtons
 * bit positions so update_sbc_input()'s
 *   bMoreButtons = (buttons >> 32) & 0x7f & ~SBC_TOGGLE_BITS_HI | toggles
 * reproduces the wire byte with no translation. This is the level model, not
 * the keyboard path's XOR-edge model: feeding a real switch's edges into the
 * XOR latch sticks it ON forever (sbc-input-plan.md §8, breakage 2).
 *
 * On failure the decoded half is zeroed, so a consumer never renders stale
 * values as if they were fresh.
 */
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

    /* Mirror the source bytes into the frame unless the caller is already
     * decoding a frame in place (rule 2: decoding never destroys them). */
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

/* ------------------------------------------------------------------------ */
/* Capture                                                                   */
/* ------------------------------------------------------------------------ */

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

    /* Idempotent: restarting simply closes the previous session and appends a
     * fresh header block. */
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

/* ------------------------------------------------------------------------ */
/* Publish — the single funnel every source goes through                     */
/* ------------------------------------------------------------------------ */

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

    /* A short read, an over-long read, or a bLength that disagrees with the
     * four independently confirmed 26 is a real fault signal; surface it
     * rather than tolerate it silently. */
    if (!ok || len != SBC_RAW_REPORT_EXPECTED ||
        f->raw[1] != SBC_RAW_REPORT_EXPECTED) {
        g_sbc.malformed_reports++;
    }

    g_sbc.frames_received++;
    sbc_capture_write(f);
}

/* ------------------------------------------------------------------------ */
/* Synthetic source                                                          */
/* ------------------------------------------------------------------------ */

/* Integer triangle wave, 0..0xFFFF over `period` ticks. Integer-only so the
 * generated stream is bit-for-bit reproducible on every host — a synthetic
 * capture that differs between machines is useless as a regression input. */
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

/*
 * Builds a plausible moving frame that touches every control, so the
 * diagnostics panel can be exercised end to end with nothing plugged in:
 *
 *   - a chaser walks momentary button bits 0..33, one at a time;
 *   - the five toggles run a 5-bit counter, so each one is seen both up and
 *     down, as LEVELS (never edges);
 *   - the gear lever visits 0 (between gears), R, N and 1..5;
 *   - the tuner dial counts 0..15;
 *   - aiming X/Y trace a Lissajous over the full 16-bit range;
 *   - rotation lever and sight-change X/Y sweep the signed range;
 *   - the three pedals sweep 0..0xFFFF at different phases and DO reach
 *     0xFFFF, which is exactly the travel the D3 bug loses.
 *
 * Byte 7 and the aiming low bytes stay 0: they are the fields no capture has
 * ever pinned down, and a synthetic source must not invent values there.
 */
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

    chaser = (unsigned)((t / 50) % 34);       /* ~200 ms per bit  */
    toggles = (unsigned)((t / 250) % 32);     /* ~1 s per step    */
    tuner = (unsigned)((t / 175) % 16);       /* ~700 ms per step */
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

/* ------------------------------------------------------------------------ */
/* Replay                                                                    */
/* ------------------------------------------------------------------------ */

static void sbc_replay_free(void)
{
    g_free(g_sbc.replay);
    g_sbc.replay = NULL;
    g_sbc.replay_count = 0;
    g_sbc.replay_idx = 0;
    g_sbc.replay_cursor_us = 0;
    g_sbc.replay_path[0] = '\0';
}

/* Parses the capture format documented at the top of this file. Tolerant of
 * anything a human might do to it (extra blank lines, comments, CRLF); strict
 * about the three decimal fields and exactly `len` hex bytes. */
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

/* ------------------------------------------------------------------------ */
/* libusb transport                                                          */
/* ------------------------------------------------------------------------ */

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

#else /* !SBC_HAVE_POLLFDS */

static void sbc_drop_all_fds(void)
{
}

#endif /* SBC_HAVE_POLLFDS */

static const char *sbc_err_name(int rc)
{
    const char *n = libusb_error_name(rc);
    return n ? n : "LIBUSB_ERROR_UNKNOWN";
}

/*
 * The point of the status enum: every value below must turn into a distinct
 * instruction the user can act on without reading this file. status_detail
 * therefore always carries the libusb error name and the bus/address, plus
 * the fix.
 */
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
        /* Windows enumerates the device but libusb cannot open it unless a
         * WinUSB-family driver is bound; that surfaces as NOT_FOUND, which on
         * any other platform would mean something else entirely. */
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
                       "open %04x:%04x. Install the udev rule and replug "
                       "(see docs/features/sbc-zero-setup-plan.md W4).",
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

/* Completion callback. Runs on the main-loop thread (fd handler or supervisor
 * timer), i.e. under the BQL, which is why publishing needs no lock. */
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
        /* Submitted with timeout 0, so this should not happen; resubmit
         * quietly if some backend produces it anyway. */
        break;

    case LIBUSB_TRANSFER_CANCELLED:
        /* Teardown in progress: do NOT resubmit. */
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

    /* TRAP 4: recycled, never LIBUSB_TRANSFER_FREE_TRANSFER. */
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

/*
 * Finish a close. Only ever called with in_flight == 0, from the supervisor
 * timer or from shutdown after the queue has been drained — never from a
 * completion callback, where closing a handle whose transfers libusb is still
 * walking is a use-after-free waiting to happen.
 */
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

/* Drain a pending close synchronously; used only on the shutdown path, where
 * the main loop will not run again to do it for us. */
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

/* Walk the active config for interface 0's interrupt IN endpoint, so a
 * relabelled clone still works; SBC_DEFAULT_EP_IN is only the fallback. */
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
        /* TRAP 4 again, explicitly: these transfers are resubmitted from the
         * callback, so LIBUSB_TRANSFER_FREE_TRANSFER must stay clear. */
        g_sbc.xfer[i]->flags = 0;

        rc = libusb_submit_transfer(g_sbc.xfer[i]);
        if (rc != 0) {
            if (g_sbc.in_flight == 0) {
                sbc_fail(rc, "submit");
                return false;
            }
            break; /* partial queue is still a working queue */
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

        /*
         * TRAP 1. Auto-detach, never libusb_detach_kernel_driver(): this goes
         * through USBFS_DISCONNECT_CLAIM, so the kernel re-probes the device
         * when our fd closes even if we are SIGKILLed. Not fatal if the
         * platform does not support it (Windows returns NOT_SUPPORTED and has
         * no kernel driver to detach in the first place).
         */
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
/*
 * May be delivered from libusb's internal event thread on some backends, so
 * it does the absolute minimum: raise a flag and let the supervisor timer on
 * the main loop do every structural thing.
 */
static int LIBUSB_CALL sbc_hotplug_cb(libusb_context *ctx, libusb_device *dev,
                                      libusb_hotplug_event event,
                                      void *user_data)
{
    qatomic_set(&g_sbc.rescan_flag, 1);
    return 0; /* keep the callback registered */
}
#endif

static int sbc_tick_ms(void)
{
    if (g_sbc.close_pending) {
        return SBC_DRAIN_MS;
    }
    if (!g_sbc.poll_driven) {
        /* No pollfds (Windows): we are the event pump. */
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

/*
 * The only place that opens, claims, closes or frees anything. Runs on
 * QEMU_CLOCK_REALTIME on purpose: a VIRTUAL-clock timer (which is what
 * host-libusb.c and PR #2854 use) stops advancing whenever the VM is paused,
 * and a controller that cannot be noticed while the emulator is paused is
 * exactly the zero-setup failure this workstream exists to remove.
 */
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
        /* Synthetic/replay: the device layer stops touching libusb entirely,
         * releasing the claim so other tools can read the controller while
         * the UI is being developed against generated frames. */
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

    rc = libusb_init(&g_sbc.ctx); /* TRAP 2: our own context, never NULL */
    if (rc != 0) {
        g_sbc.ctx = NULL;
        g_sbc.status = SBC_DEV_ERROR;
        sbc_set_detail("libusb_init failed: %s", sbc_err_name(rc));
        fprintf(stderr, SBC_LOG_PREFIX "%s\n", g_sbc.detail);
        return false;
    }

#ifdef SBC_HAVE_POLLFDS
    /* TRAP 3: no reader thread. libusb's fds go straight into the QEMU main
     * loop, so completion callbacks land on the main-loop thread under the
     * BQL, alongside the rest of the input path. */
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
#endif /* SBC_HAVE_POLLFDS */

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

    /* Drop our fd handlers before the context goes away; a handler left
     * pointing at a closed fd wedges the main loop. */
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

#else /* !CONFIG_USB_LIBUSB */

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

#endif /* CONFIG_USB_LIBUSB */

/* ------------------------------------------------------------------------ */
/* Source timer (synthetic + replay)                                         */
/* ------------------------------------------------------------------------ */

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
                /* Loop, so the panel keeps moving for as long as it is open. */
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

/* ------------------------------------------------------------------------ */
/* Public lifecycle                                                          */
/* ------------------------------------------------------------------------ */

/* Idempotent, and safe to reach from any entry point: the panel may call
 * set_synthetic() or get_state() before init(), and neither should explode. */
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
        return true; /* already up */
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
    out->status_detail = g_sbc.detail; /* never NULL; "" when uninteresting */
    out->frame = g_sbc.frame;
    out->frames_received = g_sbc.frames_received;
    out->transfer_errors = g_sbc.transfer_errors;
    out->malformed_reports = g_sbc.malformed_reports;
}

/* ------------------------------------------------------------------------ */
/* Source selection                                                          */
/* ------------------------------------------------------------------------ */

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

/* True whenever frames are coming from something other than real hardware —
 * synthetic OR replay. Consumers ask this to decide whether what they are
 * showing may be believed about the physical controller. */
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
