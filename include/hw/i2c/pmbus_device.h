/*
 * QEMU PMBus device emulation
 *
 * Copyright 2021 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_PMBUS_DEVICE_H
#define HW_PMBUS_DEVICE_H

#include "qemu/bitops.h"
#include "hw/i2c/smbus_slave.h"

enum pmbus_registers {
    PMBUS_PAGE                      = 0x00,
    PMBUS_OPERATION                 = 0x01,
    PMBUS_ON_OFF_CONFIG             = 0x02,
    PMBUS_CLEAR_FAULTS              = 0x03,
    PMBUS_PHASE                     = 0x04,
    PMBUS_PAGE_PLUS_WRITE           = 0x05,
    PMBUS_PAGE_PLUS_READ            = 0x06,
    PMBUS_WRITE_PROTECT             = 0x10,
    PMBUS_STORE_DEFAULT_ALL         = 0x11,
    PMBUS_RESTORE_DEFAULT_ALL       = 0x12,
    PMBUS_STORE_DEFAULT_CODE        = 0x13,
    PMBUS_RESTORE_DEFAULT_CODE      = 0x14,
    PMBUS_STORE_USER_ALL            = 0x15,
    PMBUS_RESTORE_USER_ALL          = 0x16,
    PMBUS_STORE_USER_CODE           = 0x17,
    PMBUS_RESTORE_USER_CODE         = 0x18,
    PMBUS_CAPABILITY                = 0x19,
    PMBUS_QUERY                     = 0x1A,
    PMBUS_SMBALERT_MASK             = 0x1B,
    PMBUS_VOUT_MODE                 = 0x20,
    PMBUS_VOUT_COMMAND              = 0x21,
    PMBUS_VOUT_TRIM                 = 0x22,
    PMBUS_VOUT_CAL_OFFSET           = 0x23,
    PMBUS_VOUT_MAX                  = 0x24,
    PMBUS_VOUT_MARGIN_HIGH          = 0x25,
    PMBUS_VOUT_MARGIN_LOW           = 0x26,
    PMBUS_VOUT_TRANSITION_RATE      = 0x27,
    PMBUS_VOUT_DROOP                = 0x28,
    PMBUS_VOUT_SCALE_LOOP           = 0x29,
    PMBUS_VOUT_SCALE_MONITOR        = 0x2A,
    PMBUS_VOUT_MIN                  = 0x2B,
    PMBUS_COEFFICIENTS              = 0x30,
    PMBUS_POUT_MAX                  = 0x31,
    PMBUS_MAX_DUTY                  = 0x32,
    PMBUS_FREQUENCY_SWITCH          = 0x33,
    PMBUS_VIN_ON                    = 0x35,
    PMBUS_VIN_OFF                   = 0x36,
    PMBUS_INTERLEAVE                = 0x37,
    PMBUS_IOUT_CAL_GAIN             = 0x38,
    PMBUS_IOUT_CAL_OFFSET           = 0x39,
    PMBUS_FAN_CONFIG_1_2            = 0x3A,
    PMBUS_FAN_COMMAND_1             = 0x3B,
    PMBUS_FAN_COMMAND_2             = 0x3C,
    PMBUS_FAN_CONFIG_3_4            = 0x3D,
    PMBUS_FAN_COMMAND_3             = 0x3E,
    PMBUS_FAN_COMMAND_4             = 0x3F,
    PMBUS_VOUT_OV_FAULT_LIMIT       = 0x40,
    PMBUS_VOUT_OV_FAULT_RESPONSE    = 0x41,
    PMBUS_VOUT_OV_WARN_LIMIT        = 0x42,
    PMBUS_VOUT_UV_WARN_LIMIT        = 0x43,
    PMBUS_VOUT_UV_FAULT_LIMIT       = 0x44,
    PMBUS_VOUT_UV_FAULT_RESPONSE    = 0x45,
    PMBUS_IOUT_OC_FAULT_LIMIT       = 0x46,
    PMBUS_IOUT_OC_FAULT_RESPONSE    = 0x47,
    PMBUS_IOUT_OC_LV_FAULT_LIMIT    = 0x48,
    PMBUS_IOUT_OC_LV_FAULT_RESPONSE = 0x49,
    PMBUS_IOUT_OC_WARN_LIMIT        = 0x4A,
    PMBUS_IOUT_UC_FAULT_LIMIT       = 0x4B,
    PMBUS_IOUT_UC_FAULT_RESPONSE    = 0x4C,
    PMBUS_OT_FAULT_LIMIT            = 0x4F,
    PMBUS_OT_FAULT_RESPONSE         = 0x50,
    PMBUS_OT_WARN_LIMIT             = 0x51,
    PMBUS_UT_WARN_LIMIT             = 0x52,
    PMBUS_UT_FAULT_LIMIT            = 0x53,
    PMBUS_UT_FAULT_RESPONSE         = 0x54,
    PMBUS_VIN_OV_FAULT_LIMIT        = 0x55,
    PMBUS_VIN_OV_FAULT_RESPONSE     = 0x56,
    PMBUS_VIN_OV_WARN_LIMIT         = 0x57,
    PMBUS_VIN_UV_WARN_LIMIT         = 0x58,
    PMBUS_VIN_UV_FAULT_LIMIT        = 0x59,
    PMBUS_VIN_UV_FAULT_RESPONSE     = 0x5A,
    PMBUS_IIN_OC_FAULT_LIMIT        = 0x5B,
    PMBUS_IIN_OC_FAULT_RESPONSE     = 0x5C,
    PMBUS_IIN_OC_WARN_LIMIT         = 0x5D,
    PMBUS_POWER_GOOD_ON             = 0x5E,
    PMBUS_POWER_GOOD_OFF            = 0x5F,
    PMBUS_TON_DELAY                 = 0x60,
    PMBUS_TON_RISE                  = 0x61,
    PMBUS_TON_MAX_FAULT_LIMIT       = 0x62,
    PMBUS_TON_MAX_FAULT_RESPONSE    = 0x63,
    PMBUS_TOFF_DELAY                = 0x64,
    PMBUS_TOFF_FALL                 = 0x65,
    PMBUS_TOFF_MAX_WARN_LIMIT       = 0x66,
    PMBUS_POUT_OP_FAULT_LIMIT       = 0x68,
    PMBUS_POUT_OP_FAULT_RESPONSE    = 0x69,
    PMBUS_POUT_OP_WARN_LIMIT        = 0x6A,
    PMBUS_PIN_OP_WARN_LIMIT         = 0x6B,
    PMBUS_STATUS_BYTE               = 0x78,
    PMBUS_STATUS_WORD               = 0x79,
    PMBUS_STATUS_VOUT               = 0x7A,
    PMBUS_STATUS_IOUT               = 0x7B,
    PMBUS_STATUS_INPUT              = 0x7C,
    PMBUS_STATUS_TEMPERATURE        = 0x7D,
    PMBUS_STATUS_CML                = 0x7E,
    PMBUS_STATUS_OTHER              = 0x7F,
    PMBUS_STATUS_MFR_SPECIFIC       = 0x80,
    PMBUS_STATUS_FANS_1_2           = 0x81,
    PMBUS_STATUS_FANS_3_4           = 0x82,
    PMBUS_READ_EIN                  = 0x86,
    PMBUS_READ_EOUT                 = 0x87,
    PMBUS_READ_VIN                  = 0x88,
    PMBUS_READ_IIN                  = 0x89,
    PMBUS_READ_VCAP                 = 0x8A,
    PMBUS_READ_VOUT                 = 0x8B,
    PMBUS_READ_IOUT                 = 0x8C,
    PMBUS_READ_TEMPERATURE_1        = 0x8D,
    PMBUS_READ_TEMPERATURE_2        = 0x8E,
    PMBUS_READ_TEMPERATURE_3        = 0x8F,
    PMBUS_READ_FAN_SPEED_1          = 0x90,
    PMBUS_READ_FAN_SPEED_2          = 0x91,
    PMBUS_READ_FAN_SPEED_3          = 0x92,
    PMBUS_READ_FAN_SPEED_4          = 0x93,
    PMBUS_READ_DUTY_CYCLE           = 0x94,
    PMBUS_READ_FREQUENCY            = 0x95,
    PMBUS_READ_POUT                 = 0x96,
    PMBUS_READ_PIN                  = 0x97,
    PMBUS_REVISION                  = 0x98,
    PMBUS_MFR_ID                    = 0x99,
    PMBUS_MFR_MODEL                 = 0x9A,
    PMBUS_MFR_REVISION              = 0x9B,
    PMBUS_MFR_LOCATION              = 0x9C,
    PMBUS_MFR_DATE                  = 0x9D,
    PMBUS_MFR_SERIAL                = 0x9E,
    PMBUS_APP_PROFILE_SUPPORT       = 0x9F,
    PMBUS_MFR_VIN_MIN               = 0xA0,
    PMBUS_MFR_VIN_MAX               = 0xA1,
    PMBUS_MFR_IIN_MAX               = 0xA2,
    PMBUS_MFR_PIN_MAX               = 0xA3,
    PMBUS_MFR_VOUT_MIN              = 0xA4,
    PMBUS_MFR_VOUT_MAX              = 0xA5,
    PMBUS_MFR_IOUT_MAX              = 0xA6,
    PMBUS_MFR_POUT_MAX              = 0xA7,
    PMBUS_MFR_TAMBIENT_MAX          = 0xA8,
    PMBUS_MFR_TAMBIENT_MIN          = 0xA9,
    PMBUS_MFR_EFFICIENCY_LL         = 0xAA,
    PMBUS_MFR_EFFICIENCY_HL         = 0xAB,
    PMBUS_MFR_PIN_ACCURACY          = 0xAC,
    PMBUS_IC_DEVICE_ID              = 0xAD,
    PMBUS_IC_DEVICE_REV             = 0xAE,
    PMBUS_MFR_MAX_TEMP_1            = 0xC0,
    PMBUS_MFR_MAX_TEMP_2            = 0xC1,
    PMBUS_MFR_MAX_TEMP_3            = 0xC2,
    PMBUS_IDLE_STATE                = 0xFF,
};

#define PB_STATUS_VOUT           BIT(15)
#define PB_STATUS_IOUT_POUT      BIT(14)
#define PB_STATUS_INPUT          BIT(13)
#define PB_STATUS_WORD_MFR       BIT(12)
#define PB_STATUS_POWER_GOOD_N   BIT(11)
#define PB_STATUS_FAN            BIT(10)
#define PB_STATUS_OTHER          BIT(9)
#define PB_STATUS_UNKNOWN        BIT(8)
#define PB_STATUS_BUSY           BIT(7)
#define PB_STATUS_OFF            BIT(6)
#define PB_STATUS_VOUT_OV        BIT(5)
#define PB_STATUS_IOUT_OC        BIT(4)
#define PB_STATUS_VIN_UV         BIT(3)
#define PB_STATUS_TEMPERATURE    BIT(2)
#define PB_STATUS_CML            BIT(1)
#define PB_STATUS_NONE_ABOVE     BIT(0)

#define PB_STATUS_VOUT_OV_FAULT         BIT(7)
#define PB_STATUS_VOUT_OV_WARN          BIT(6)
#define PB_STATUS_VOUT_UV_WARN          BIT(5)
#define PB_STATUS_VOUT_UV_FAULT         BIT(4)
#define PB_STATUS_VOUT_MAX              BIT(3)
#define PB_STATUS_VOUT_TON_MAX_FAULT    BIT(2)
#define PB_STATUS_VOUT_TOFF_MAX_WARN    BIT(1)

#define PB_STATUS_IOUT_OC_FAULT    BIT(7)
#define PB_STATUS_IOUT_OC_LV_FAULT BIT(6)
#define PB_STATUS_IOUT_OC_WARN     BIT(5)
#define PB_STATUS_IOUT_UC_FAULT    BIT(4)
#define PB_STATUS_CURR_SHARE       BIT(3)
#define PB_STATUS_PWR_LIM_MODE     BIT(2)
#define PB_STATUS_POUT_OP_FAULT    BIT(1)
#define PB_STATUS_POUT_OP_WARN     BIT(0)

#define PB_STATUS_INPUT_VIN_OV_FAULT    BIT(7)
#define PB_STATUS_INPUT_VIN_OV_WARN     BIT(6)
#define PB_STATUS_INPUT_VIN_UV_WARN     BIT(5)
#define PB_STATUS_INPUT_VIN_UV_FAULT    BIT(4)
#define PB_STATUS_INPUT_IIN_OC_FAULT    BIT(2)
#define PB_STATUS_INPUT_IIN_OC_WARN     BIT(1)
#define PB_STATUS_INPUT_PIN_OP_WARN     BIT(0)

#define PB_STATUS_OT_FAULT              BIT(7)
#define PB_STATUS_OT_WARN               BIT(6)
#define PB_STATUS_UT_WARN               BIT(5)
#define PB_STATUS_UT_FAULT              BIT(4)

#define PB_CML_FAULT_INVALID_CMD     BIT(7)
#define PB_CML_FAULT_INVALID_DATA    BIT(6)
#define PB_CML_FAULT_PEC             BIT(5)
#define PB_CML_FAULT_MEMORY          BIT(4)
#define PB_CML_FAULT_PROCESSOR       BIT(3)
#define PB_CML_FAULT_OTHER_COMM      BIT(1)
#define PB_CML_FAULT_OTHER_MEM_LOGIC BIT(0)

#define PB_OP_ON                BIT(7)
#define PB_OP_MARGIN_HIGH       BIT(5)
#define PB_OP_MARGIN_LOW        BIT(4)

#define PB_MAX_PAGES            0x1F
#define PB_ALL_PAGES            0xFF

#define PMBUS_ERR_BYTE          0xFF

#define TYPE_PMBUS_DEVICE "pmbus-device"
OBJECT_DECLARE_TYPE(PMBusDevice, PMBusDeviceClass,
                    PMBUS_DEVICE)

#define PB_HAS_COEFFICIENTS        BIT_ULL(9)
#define PB_HAS_VIN                 BIT_ULL(10)
#define PB_HAS_VOUT                BIT_ULL(11)
#define PB_HAS_VOUT_MARGIN         BIT_ULL(12)
#define PB_HAS_VIN_RATING          BIT_ULL(13)
#define PB_HAS_VOUT_RATING         BIT_ULL(14)
#define PB_HAS_VOUT_MODE           BIT_ULL(15)
#define PB_HAS_VCAP                BIT_ULL(16)
#define PB_HAS_IOUT                BIT_ULL(21)
#define PB_HAS_IIN                 BIT_ULL(22)
#define PB_HAS_IOUT_RATING         BIT_ULL(23)
#define PB_HAS_IIN_RATING          BIT_ULL(24)
#define PB_HAS_IOUT_GAIN           BIT_ULL(25)
#define PB_HAS_POUT                BIT_ULL(30)
#define PB_HAS_PIN                 BIT_ULL(31)
#define PB_HAS_EIN                 BIT_ULL(32)
#define PB_HAS_EOUT                BIT_ULL(33)
#define PB_HAS_POUT_RATING         BIT_ULL(34)
#define PB_HAS_PIN_RATING          BIT_ULL(35)
#define PB_HAS_TEMPERATURE         BIT_ULL(40)
#define PB_HAS_TEMP2               BIT_ULL(41)
#define PB_HAS_TEMP3               BIT_ULL(42)
#define PB_HAS_TEMP_RATING         BIT_ULL(43)
#define PB_HAS_FAN                 BIT_ULL(44)
#define PB_HAS_MFR_INFO            BIT_ULL(50)
#define PB_HAS_STATUS_MFR_SPECIFIC BIT_ULL(51)

struct PMBusDeviceClass {
    SMBusDeviceClass parent_class;
    uint8_t device_num_pages;

    void (*quick_cmd)(PMBusDevice *dev, uint8_t read);
    int (*write_data)(PMBusDevice *dev, const uint8_t *buf, uint8_t len);
    uint8_t (*receive_byte)(PMBusDevice *dev);
};

typedef struct PMBusPage {
    uint64_t page_flags;

    uint8_t page;
    uint8_t operation;
    uint8_t on_off_config;
    uint8_t write_protect;
    uint8_t phase;
    uint8_t vout_mode;
    uint16_t vout_command;
    uint16_t vout_trim;
    uint16_t vout_cal_offset;
    uint16_t vout_max;
    uint16_t vout_margin_high;
    uint16_t vout_margin_low;
    uint16_t vout_transition_rate;
    uint16_t vout_droop;
    uint16_t vout_scale_loop;
    uint16_t vout_scale_monitor;
    uint16_t vout_min;
    uint8_t coefficients[5];
    uint16_t pout_max;
    uint16_t max_duty;
    uint16_t frequency_switch;
    uint16_t vin_on;
    uint16_t vin_off;
    uint16_t iout_cal_gain;
    uint16_t iout_cal_offset;
    uint8_t fan_config_1_2;
    uint16_t fan_command_1;
    uint16_t fan_command_2;
    uint8_t fan_config_3_4;
    uint16_t fan_command_3;
    uint16_t fan_command_4;
    uint16_t vout_ov_fault_limit;
    uint8_t vout_ov_fault_response;
    uint16_t vout_ov_warn_limit;
    uint16_t vout_uv_warn_limit;
    uint16_t vout_uv_fault_limit;
    uint8_t vout_uv_fault_response;
    uint16_t iout_oc_fault_limit;
    uint8_t iout_oc_fault_response;
    uint16_t iout_oc_lv_fault_limit;
    uint8_t iout_oc_lv_fault_response;
    uint16_t iout_oc_warn_limit;
    uint16_t iout_uc_fault_limit;
    uint8_t iout_uc_fault_response;
    uint16_t ot_fault_limit;
    uint8_t ot_fault_response;
    uint16_t ot_warn_limit;
    uint16_t ut_warn_limit;
    uint16_t ut_fault_limit;
    uint8_t ut_fault_response;
    uint16_t vin_ov_fault_limit;
    uint8_t vin_ov_fault_response;
    uint16_t vin_ov_warn_limit;
    uint16_t vin_uv_warn_limit;
    uint16_t vin_uv_fault_limit;
    uint8_t vin_uv_fault_response;
    uint16_t iin_oc_fault_limit;
    uint8_t iin_oc_fault_response;
    uint16_t iin_oc_warn_limit;
    uint16_t power_good_on;
    uint16_t power_good_off;
    uint16_t ton_delay;
    uint16_t ton_rise;
    uint16_t ton_max_fault_limit;
    uint8_t ton_max_fault_response;
    uint16_t toff_delay;
    uint16_t toff_fall;
    uint16_t toff_max_warn_limit;
    uint16_t pout_op_fault_limit;
    uint8_t pout_op_fault_response;
    uint16_t pout_op_warn_limit;
    uint16_t pin_op_warn_limit;
    uint16_t status_word;
    uint8_t status_vout;
    uint8_t status_iout;
    uint8_t status_input;
    uint8_t status_temperature;
    uint8_t status_cml;
    uint8_t status_other;
    uint8_t status_mfr_specific;
    uint8_t status_fans_1_2;
    uint8_t status_fans_3_4;
    uint8_t read_ein[5];
    uint8_t read_eout[5];
    uint16_t read_vin;
    uint16_t read_iin;
    uint16_t read_vcap;
    uint16_t read_vout;
    uint16_t read_iout;
    uint16_t read_temperature_1;
    uint16_t read_temperature_2;
    uint16_t read_temperature_3;
    uint16_t read_fan_speed_1;
    uint16_t read_fan_speed_2;
    uint16_t read_fan_speed_3;
    uint16_t read_fan_speed_4;
    uint16_t read_duty_cycle;
    uint16_t read_frequency;
    uint16_t read_pout;
    uint16_t read_pin;
    uint8_t revision;
    const char *mfr_id;
    const char *mfr_model;
    const char *mfr_revision;
    const char *mfr_location;
    const char *mfr_date;
    const char *mfr_serial;
    const char *app_profile_support;
    uint16_t mfr_vin_min;
    uint16_t mfr_vin_max;
    uint16_t mfr_iin_max;
    uint16_t mfr_pin_max;
    uint16_t mfr_vout_min;
    uint16_t mfr_vout_max;
    uint16_t mfr_iout_max;
    uint16_t mfr_pout_max;
    uint16_t mfr_tambient_max;
    uint16_t mfr_tambient_min;
    uint8_t mfr_efficiency_ll[14];
    uint8_t mfr_efficiency_hl[14];
    uint8_t mfr_pin_accuracy;
    uint16_t mfr_max_temp_1;
    uint16_t mfr_max_temp_2;
    uint16_t mfr_max_temp_3;
} PMBusPage;

struct PMBusDevice {
    SMBusDevice smb;

    uint8_t num_pages;
    uint8_t code;
    uint8_t page;

    PMBusPage *pages;
    uint8_t capability;


    int32_t in_buf_len;
    uint8_t *in_buf;
    int32_t out_buf_len;
    uint8_t out_buf[SMBUS_DATA_MAX_LEN];
};

typedef struct PMBusCoefficients {
    int32_t m;
    int64_t b;
    int32_t R;
} PMBusCoefficients;

typedef struct PMBusVoutMode {
    uint8_t  mode:3;
    int8_t   exp:5;
} PMBusVoutMode;

uint16_t pmbus_data2direct_mode(PMBusCoefficients c, uint32_t value);

uint32_t pmbus_direct_mode2data(PMBusCoefficients c, uint16_t value);

uint16_t pmbus_data2linear_mode(uint16_t value, int exp);

uint16_t pmbus_linear_mode2data(uint16_t value, int exp);

void pmbus_send(PMBusDevice *state, const uint8_t *data, uint16_t len);
void pmbus_send8(PMBusDevice *state, uint8_t data);
void pmbus_send16(PMBusDevice *state, uint16_t data);
void pmbus_send32(PMBusDevice *state, uint32_t data);
void pmbus_send64(PMBusDevice *state, uint64_t data);

void pmbus_send_string(PMBusDevice *state, const char *data);

uint8_t pmbus_receive_block(PMBusDevice *pmdev, uint8_t *dest, size_t len);

uint8_t pmbus_receive8(PMBusDevice *pmdev);
uint16_t pmbus_receive16(PMBusDevice *pmdev);
uint32_t pmbus_receive32(PMBusDevice *pmdev);
uint64_t pmbus_receive64(PMBusDevice *pmdev);

int pmbus_page_config(PMBusDevice *pmdev, uint8_t page_index, uint64_t flags);

void pmbus_check_limits(PMBusDevice *pmdev);

void pmbus_idle(PMBusDevice *pmdev);

extern const VMStateDescription vmstate_pmbus_device;

#define VMSTATE_PMBUS_DEVICE(_field, _state) {                       \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(PMBusDevice),                               \
    .vmsd       = &vmstate_pmbus_device,                             \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, PMBusDevice), \
}

#endif
