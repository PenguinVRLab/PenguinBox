#ifndef XEMU_SBC_DEVICE_H
#define XEMU_SBC_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SBC_USB_VENDOR_ID  0x0a7b
#define SBC_USB_PRODUCT_ID 0xd000

#define SBC_RAW_REPORT_MAX 32
#define SBC_RAW_REPORT_EXPECTED 26

typedef enum SbcDeviceStatus {
    SBC_DEV_ABSENT = 0,
    SBC_DEV_READY,
    SBC_DEV_NO_PERMISSION,
    SBC_DEV_NO_DRIVER,
    SBC_DEV_BUSY,
    SBC_DEV_ERROR,
} SbcDeviceStatus;

typedef struct SbcFrame {
    uint64_t seq;
    uint64_t timestamp_us;

    uint8_t raw[SBC_RAW_REPORT_MAX];
    uint8_t raw_len;

    uint64_t buttons;
    uint8_t toggle_levels;
    uint8_t gear_lever;
    uint8_t tuner_dial;
    uint16_t aiming_x, aiming_y;
    int16_t rotation_lever;
    int16_t sight_change_x, sight_change_y;
    uint16_t left_pedal, middle_pedal, right_pedal;
} SbcFrame;

typedef struct SbcDeviceState {
    SbcDeviceStatus status;
    const char *status_detail;
    SbcFrame frame;
    uint64_t frames_received;
    uint64_t transfer_errors;
    uint64_t malformed_reports;
} SbcDeviceState;

bool sbc_device_init(void);
void sbc_device_shutdown(void);

void sbc_device_get_state(SbcDeviceState *out);

bool sbc_device_capture_start(const char *path);
void sbc_device_capture_stop(void);
bool sbc_device_capture_active(void);
uint64_t sbc_device_capture_count(void);

void sbc_device_set_synthetic(bool enabled);
bool sbc_device_synthetic_enabled(void);
bool sbc_device_replay_file(const char *path);

bool sbc_decode_report(const uint8_t *raw, uint8_t len, SbcFrame *out);

#ifdef __cplusplus
}
#endif

#endif
