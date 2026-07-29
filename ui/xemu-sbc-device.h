/*
 * PenguinBox — real Steel Battalion controller: host-side device interface
 *
 * THE CONTRACT between the libusb device layer (xemu-sbc-device.c) and its
 * consumers (the diagnostics panel today; the input merge layer later).
 * Both sides build against this header and nothing else.
 *
 * Design rules this interface exists to enforce:
 *
 *  1. ONE OWNER of the device handle. The diagnostics panel does NOT open the
 *     controller itself. It calls sbc_device_* like any other consumer. When
 *     the input reader lands it consumes the same interface, so there is never
 *     a second libusb claim to collide with.
 *
 *  2. RAW REPORTS ARE THE TRUTH. A frame carries the 26 wire bytes verbatim
 *     alongside the decoded view. Consumers that only need decoded values use
 *     it; the panel shows both side by side, which is what distinguishes "the
 *     device is broken" from "our decode is wrong". Decoding never destroys
 *     the source bytes.
 *
 *  3. SOURCE-AGNOSTIC. A synthetic/replay source implements the same
 *     interface, so everything above this line is developable and testable
 *     with no controller attached.
 *
 * See docs/features/sbc-zero-setup-plan.md (W1a) and sbc-input-plan.md §5.
 */

#ifndef XEMU_SBC_DEVICE_H
#define XEMU_SBC_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SBC_USB_VENDOR_ID  0x0a7b
#define SBC_USB_PRODUCT_ID 0xd000

/* The interrupt-IN report is 26 bytes (xid-steel-battalion.c:36). We keep a
 * fixed 32-byte buffer (wMaxPacketSize) and record how many bytes actually
 * arrived — a short or over-long read is diagnostic information, not an
 * error to hide. */
#define SBC_RAW_REPORT_MAX 32
#define SBC_RAW_REPORT_EXPECTED 26

/*
 * Why a status enum and not a bool: the whole reason the diagnostics panel is
 * being built first is that "it did not work" must become "here is precisely
 * what to do". Every value below maps to a distinct user-facing instruction.
 */
typedef enum SbcDeviceStatus {
    SBC_DEV_ABSENT = 0,      /* no matching VID:PID on the bus */
    SBC_DEV_READY,           /* open, claimed, frames arriving */
    SBC_DEV_NO_PERMISSION,   /* present; LIBUSB_ERROR_ACCESS — udev rule (Linux) */
    SBC_DEV_NO_DRIVER,       /* present; Windows, not bound to WinUSB — Zadig */
    SBC_DEV_BUSY,            /* present; another process holds the interface */
    SBC_DEV_ERROR,           /* present; anything else — see status_detail */
} SbcDeviceStatus;

/*
 * One immutable frame. Absolute values only — never deltas, never edge flags:
 * the merge layer's contract (sbc-input-plan.md §4) requires every source to
 * publish complete absolute state, and toggles/gear/tuner are LEVELS here,
 * not the XOR-edge model the keyboard path uses.
 */
typedef struct SbcFrame {
    /* Monotonic; lets a consumer detect "device stopped reporting" without
     * timestamps, and lets the panel show a live/stale indicator. */
    uint64_t seq;
    /* Host monotonic microseconds at completion; for jitter/latency readout. */
    uint64_t timestamp_us;

    /* --- raw, exactly as the wire delivered it --- */
    uint8_t raw[SBC_RAW_REPORT_MAX];
    uint8_t raw_len;

    /* --- decoded view (mirrors SteelBattalionState field-for-field so the
     *     merge layer can adopt it without translation) --- */
    uint64_t buttons;      /* bits 0-38, level (not edges) */
    uint8_t toggle_levels; /* the five latching toggles, TRUE PHYSICAL LEVEL */
    uint8_t gear_lever;    /* wire encoding, decoded by the consumer */
    uint8_t tuner_dial;    /* 0..15 */
    uint16_t aiming_x, aiming_y;
    int16_t rotation_lever;
    int16_t sight_change_x, sight_change_y;
    uint16_t left_pedal, middle_pedal, right_pedal; /* unsigned 0..0xFFFF */
} SbcFrame;

typedef struct SbcDeviceState {
    SbcDeviceStatus status;
    /* Human-readable specifics for SBC_DEV_ERROR and extra context otherwise
     * (libusb error name, bus/address). Never NULL; "" when uninteresting. */
    const char *status_detail;
    /* Most recent frame. Valid only when frames_received > 0. */
    SbcFrame frame;
    uint64_t frames_received;
    uint64_t transfer_errors;
    /* Short reads / unexpected lengths — a real fault signal worth surfacing
     * rather than silently tolerating. */
    uint64_t malformed_reports;
} SbcDeviceState;

/*
 * Lifecycle. Safe to call when the feature is disabled or libusb is absent:
 * init returns false, everything else no-ops, status stays SBC_DEV_ABSENT.
 * Poll runs on the main loop; it never blocks.
 */
bool sbc_device_init(void);
void sbc_device_shutdown(void);

/* Snapshot the current state. Cheap; safe to call every UI frame. */
void sbc_device_get_state(SbcDeviceState *out);

/*
 * Capture: the deliverable that turns any community member with a controller
 * into the source of our golden vectors, without asking them to learn usbmon.
 * Records raw frames to `path` until stopped. Returns false if the file could
 * not be opened.
 */
bool sbc_device_capture_start(const char *path);
void sbc_device_capture_stop(void);
bool sbc_device_capture_active(void);
uint64_t sbc_device_capture_count(void);

/*
 * Synthetic source — the reason W1a is buildable with no hardware. When
 * enabled, the device layer stops touching libusb and publishes generated
 * frames instead; consumers cannot tell the difference. Also replays a file
 * written by sbc_device_capture_start().
 */
void sbc_device_set_synthetic(bool enabled);
bool sbc_device_synthetic_enabled(void);
bool sbc_device_replay_file(const char *path);

/* Decode 26 wire bytes into the decoded half of a frame. Pure function, no
 * device required — this is what the off-rig golden-vector tests exercise. */
bool sbc_decode_report(const uint8_t *raw, uint8_t len, SbcFrame *out);

#ifdef __cplusplus
}
#endif

#endif /* XEMU_SBC_DEVICE_H */
