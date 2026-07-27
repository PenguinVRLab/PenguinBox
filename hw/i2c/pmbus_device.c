/*
 * PMBus wrapper over SMBus
 *
 * Copyright 2021 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include <math.h>
#include "hw/i2c/pmbus_device.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "qemu/log.h"

uint16_t pmbus_data2direct_mode(PMBusCoefficients c, uint32_t value)
{
    uint16_t y = (c.m * value + c.b) * pow(10, c.R);
    return y;
}

uint32_t pmbus_direct_mode2data(PMBusCoefficients c, uint16_t value)
{
    uint32_t x = (value / pow(10, c.R) - c.b) / c.m;
    return x;
}

uint16_t pmbus_data2linear_mode(uint16_t value, int exp)
{
    if (exp < 0) {
        return value << (-exp);
    }
    return value >> exp;
}

uint16_t pmbus_linear_mode2data(uint16_t value, int exp)
{
    if (exp < 0) {
        return value >> (-exp);
    }
    return value << exp;
}

void pmbus_send(PMBusDevice *pmdev, const uint8_t *data, uint16_t len)
{
    if (pmdev->out_buf_len + len > SMBUS_DATA_MAX_LEN) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "PMBus device tried to send too much data");
        len = 0;
    }

    for (int i = len - 1; i >= 0; i--) {
        pmdev->out_buf[i + pmdev->out_buf_len] = data[len - i - 1];
    }
    pmdev->out_buf_len += len;
}

static void pmbus_send_uint(PMBusDevice *pmdev, uint64_t data, uint8_t size)
{
    uint8_t bytes[8];
    g_assert(size <= 8);

    for (int i = 0; i < size; i++) {
        bytes[i] = data & 0xFF;
        data = data >> 8;
    }
    pmbus_send(pmdev, bytes, size);
}

void pmbus_send8(PMBusDevice *pmdev, uint8_t data)
{
    pmbus_send_uint(pmdev, data, 1);
}

void pmbus_send16(PMBusDevice *pmdev, uint16_t data)
{
    pmbus_send_uint(pmdev, data, 2);
}

void pmbus_send32(PMBusDevice *pmdev, uint32_t data)
{
    pmbus_send_uint(pmdev, data, 4);
}

void pmbus_send64(PMBusDevice *pmdev, uint64_t data)
{
    pmbus_send_uint(pmdev, data, 8);
}

void pmbus_send_string(PMBusDevice *pmdev, const char *data)
{
    if (!data) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: %s: uninitialised read from 0x%02x\n",
                      __func__, DEVICE(pmdev)->canonical_path, pmdev->code);
        return;
    }

    size_t len = strlen(data);
    g_assert(len + pmdev->out_buf_len < SMBUS_DATA_MAX_LEN);
    pmdev->out_buf[len + pmdev->out_buf_len] = len;

    for (int i = len - 1; i >= 0; i--) {
        pmdev->out_buf[i + pmdev->out_buf_len] = data[len - 1 - i];
    }
    pmdev->out_buf_len += len + 1;
}

uint8_t pmbus_receive_block(PMBusDevice *pmdev, uint8_t *dest, size_t len)
{
    memset(dest, 0, len);

    pmdev->in_buf++;
    pmdev->in_buf_len--;

    uint8_t sent_len = pmdev->in_buf[0];

    if (sent_len != pmdev->in_buf_len - 1) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: length mismatch. Expected %d bytes, got %d bytes\n",
                      __func__, sent_len, pmdev->in_buf_len - 1);
    }

    pmdev->in_buf++;
    pmdev->in_buf_len--;

    if (pmdev->in_buf_len < len) {
        len = pmdev->in_buf_len;
    }
    memcpy(dest, pmdev->in_buf, len);
    return len;
}


static uint64_t pmbus_receive_uint(PMBusDevice *pmdev)
{
    uint64_t ret = 0;

    pmdev->in_buf++;
    pmdev->in_buf_len--;

    for (int i = pmdev->in_buf_len - 1; i >= 0; i--) {
        ret = ret << 8 | pmdev->in_buf[i];
    }
    return ret;
}

uint8_t pmbus_receive8(PMBusDevice *pmdev)
{
    if (pmdev->in_buf_len - 1 != 1) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: length mismatch. Expected 1 byte, got %d bytes\n",
                      __func__, pmdev->in_buf_len - 1);
    }
    return pmbus_receive_uint(pmdev);
}

uint16_t pmbus_receive16(PMBusDevice *pmdev)
{
    if (pmdev->in_buf_len - 1 != 2) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: length mismatch. Expected 2 bytes, got %d bytes\n",
                      __func__, pmdev->in_buf_len - 1);
    }
    return pmbus_receive_uint(pmdev);
}

uint32_t pmbus_receive32(PMBusDevice *pmdev)
{
    if (pmdev->in_buf_len - 1 != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: length mismatch. Expected 4 bytes, got %d bytes\n",
                      __func__, pmdev->in_buf_len - 1);
    }
    return pmbus_receive_uint(pmdev);
}

uint64_t pmbus_receive64(PMBusDevice *pmdev)
{
    if (pmdev->in_buf_len - 1 != 8) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: length mismatch. Expected 8 bytes, got %d bytes\n",
                      __func__, pmdev->in_buf_len - 1);
    }
    return pmbus_receive_uint(pmdev);
}

static uint8_t pmbus_out_buf_pop(PMBusDevice *pmdev)
{
    if (pmdev->out_buf_len == 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: tried to read from empty buffer",
                      __func__);
        return PMBUS_ERR_BYTE;
    }
    uint8_t data = pmdev->out_buf[pmdev->out_buf_len - 1];
    pmdev->out_buf_len--;
    return data;
}

static void pmbus_quick_cmd(SMBusDevice *smd, uint8_t read)
{
    PMBusDevice *pmdev = PMBUS_DEVICE(smd);
    PMBusDeviceClass *pmdc = PMBUS_DEVICE_GET_CLASS(pmdev);

    if (pmdc->quick_cmd) {
        pmdc->quick_cmd(pmdev, read);
    }
}

static uint8_t pmbus_pages_num(PMBusDevice *pmdev)
{
    const PMBusDeviceClass *k = PMBUS_DEVICE_GET_CLASS(pmdev);

    return k->device_num_pages ? : 1;
}

static void pmbus_pages_alloc(PMBusDevice *pmdev)
{
    pmdev->num_pages = pmbus_pages_num(pmdev);
    pmdev->pages = g_new0(PMBusPage, pmdev->num_pages);
}

void pmbus_check_limits(PMBusDevice *pmdev)
{
    for (int i = 0; i < pmdev->num_pages; i++) {
        if ((pmdev->pages[i].operation & PB_OP_ON) == 0) {
            continue;
        }

        if (pmdev->pages[i].read_vout > pmdev->pages[i].vout_ov_fault_limit) {
            pmdev->pages[i].status_word |= PB_STATUS_VOUT;
            pmdev->pages[i].status_vout |= PB_STATUS_VOUT_OV_FAULT;
        }

        if (pmdev->pages[i].read_vout > pmdev->pages[i].vout_ov_warn_limit) {
            pmdev->pages[i].status_word |= PB_STATUS_VOUT;
            pmdev->pages[i].status_vout |= PB_STATUS_VOUT_OV_WARN;
        }

        if (pmdev->pages[i].read_vout < pmdev->pages[i].vout_uv_warn_limit) {
            pmdev->pages[i].status_word |= PB_STATUS_VOUT;
            pmdev->pages[i].status_vout |= PB_STATUS_VOUT_UV_WARN;
        }

        if (pmdev->pages[i].read_vout < pmdev->pages[i].vout_uv_fault_limit) {
            pmdev->pages[i].status_word |= PB_STATUS_VOUT;
            pmdev->pages[i].status_vout |= PB_STATUS_VOUT_UV_FAULT;
        }

        if (pmdev->pages[i].read_vin > pmdev->pages[i].vin_ov_warn_limit) {
            pmdev->pages[i].status_word |= PB_STATUS_INPUT;
            pmdev->pages[i].status_input |= PB_STATUS_INPUT_VIN_OV_WARN;
        }

        if (pmdev->pages[i].read_vin < pmdev->pages[i].vin_uv_warn_limit) {
            pmdev->pages[i].status_word |= PB_STATUS_INPUT;
            pmdev->pages[i].status_input |= PB_STATUS_INPUT_VIN_UV_WARN;
        }

        if (pmdev->pages[i].read_iout > pmdev->pages[i].iout_oc_warn_limit) {
            pmdev->pages[i].status_word |= PB_STATUS_IOUT_POUT;
            pmdev->pages[i].status_iout |= PB_STATUS_IOUT_OC_WARN;
        }

        if (pmdev->pages[i].read_iout > pmdev->pages[i].iout_oc_fault_limit) {
            pmdev->pages[i].status_word |= PB_STATUS_IOUT_POUT;
            pmdev->pages[i].status_iout |= PB_STATUS_IOUT_OC_FAULT;
        }

        if (pmdev->pages[i].read_pin > pmdev->pages[i].pin_op_warn_limit) {
            pmdev->pages[i].status_word |= PB_STATUS_INPUT;
            pmdev->pages[i].status_input |= PB_STATUS_INPUT_PIN_OP_WARN;
        }

        if (pmdev->pages[i].read_temperature_1
                > pmdev->pages[i].ot_fault_limit) {
            pmdev->pages[i].status_word |= PB_STATUS_TEMPERATURE;
            pmdev->pages[i].status_temperature |= PB_STATUS_OT_FAULT;
        }

        if (pmdev->pages[i].read_temperature_1
                > pmdev->pages[i].ot_warn_limit) {
            pmdev->pages[i].status_word |= PB_STATUS_TEMPERATURE;
            pmdev->pages[i].status_temperature |= PB_STATUS_OT_WARN;
        }
    }
}

void pmbus_idle(PMBusDevice *pmdev)
{
    pmdev->code = PMBUS_IDLE_STATE;
}

static void pmbus_cml_error(PMBusDevice *pmdev)
{
    for (int i = 0; i < pmdev->num_pages; i++) {
        pmdev->pages[i].status_word |= PMBUS_STATUS_CML;
        pmdev->pages[i].status_cml |= PB_CML_FAULT_INVALID_CMD;
    }
}

static uint8_t pmbus_receive_byte(SMBusDevice *smd)
{
    PMBusDevice *pmdev = PMBUS_DEVICE(smd);
    PMBusDeviceClass *pmdc = PMBUS_DEVICE_GET_CLASS(pmdev);
    uint8_t ret = PMBUS_ERR_BYTE;
    uint8_t index;

    if (pmdev->out_buf_len != 0) {
        ret = pmbus_out_buf_pop(pmdev);
        return ret;
    }

    if (pmdev->page == PB_ALL_PAGES) {
        index = 0;
    } else if (pmdev->page > pmdev->num_pages - 1) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: page %d is out of range\n",
                      __func__, pmdev->page);
        pmbus_cml_error(pmdev);
        return PMBUS_ERR_BYTE;
    } else {
        index = pmdev->page;
    }

    switch (pmdev->code) {
    case PMBUS_PAGE:
        pmbus_send8(pmdev, pmdev->page);
        break;

    case PMBUS_OPERATION:
        pmbus_send8(pmdev, pmdev->pages[index].operation);
        break;

    case PMBUS_ON_OFF_CONFIG:
        pmbus_send8(pmdev, pmdev->pages[index].on_off_config);
        break;

    case PMBUS_PHASE:
        pmbus_send8(pmdev, pmdev->pages[index].phase);
        break;

    case PMBUS_WRITE_PROTECT:
        pmbus_send8(pmdev, pmdev->pages[index].write_protect);
        break;

    case PMBUS_CAPABILITY:
        pmbus_send8(pmdev, pmdev->capability);
        if (pmdev->capability & BIT(7)) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: PEC is enabled but not yet supported.\n",
                          __func__);
        }
        break;

    case PMBUS_VOUT_MODE:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT_MODE) {
            pmbus_send8(pmdev, pmdev->pages[index].vout_mode);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_VOUT_COMMAND:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmbus_send16(pmdev, pmdev->pages[index].vout_command);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_VOUT_TRIM:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmbus_send16(pmdev, pmdev->pages[index].vout_trim);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_VOUT_CAL_OFFSET:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmbus_send16(pmdev, pmdev->pages[index].vout_cal_offset);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_VOUT_MAX:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmbus_send16(pmdev, pmdev->pages[index].vout_max);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_VOUT_MARGIN_HIGH:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT_MARGIN) {
            pmbus_send16(pmdev, pmdev->pages[index].vout_margin_high);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_VOUT_MARGIN_LOW:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT_MARGIN) {
            pmbus_send16(pmdev, pmdev->pages[index].vout_margin_low);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_VOUT_TRANSITION_RATE:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmbus_send16(pmdev, pmdev->pages[index].vout_transition_rate);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_VOUT_DROOP:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmbus_send16(pmdev, pmdev->pages[index].vout_droop);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_VOUT_SCALE_LOOP:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmbus_send16(pmdev, pmdev->pages[index].vout_scale_loop);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_VOUT_SCALE_MONITOR:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmbus_send16(pmdev, pmdev->pages[index].vout_scale_monitor);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_VOUT_MIN:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT_RATING) {
            pmbus_send16(pmdev, pmdev->pages[index].vout_min);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_POUT_MAX:
        if (pmdev->pages[index].page_flags & PB_HAS_POUT) {
            pmbus_send16(pmdev, pmdev->pages[index].pout_max);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_VIN_ON:
        if (pmdev->pages[index].page_flags & PB_HAS_VIN) {
            pmbus_send16(pmdev, pmdev->pages[index].vin_on);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_VIN_OFF:
        if (pmdev->pages[index].page_flags & PB_HAS_VIN) {
            pmbus_send16(pmdev, pmdev->pages[index].vin_off);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_IOUT_CAL_GAIN:
        if (pmdev->pages[index].page_flags & PB_HAS_IOUT_GAIN) {
            pmbus_send16(pmdev, pmdev->pages[index].iout_cal_gain);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_FAN_CONFIG_1_2:
        if (pmdev->pages[index].page_flags & PB_HAS_FAN) {
            pmbus_send8(pmdev, pmdev->pages[index].fan_config_1_2);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_FAN_COMMAND_1:
        if (pmdev->pages[index].page_flags & PB_HAS_FAN) {
            pmbus_send16(pmdev, pmdev->pages[index].fan_command_1);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_FAN_COMMAND_2:
        if (pmdev->pages[index].page_flags & PB_HAS_FAN) {
            pmbus_send16(pmdev, pmdev->pages[index].fan_command_2);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_FAN_CONFIG_3_4:
        if (pmdev->pages[index].page_flags & PB_HAS_FAN) {
            pmbus_send8(pmdev, pmdev->pages[index].fan_config_3_4);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_FAN_COMMAND_3:
        if (pmdev->pages[index].page_flags & PB_HAS_FAN) {
            pmbus_send16(pmdev, pmdev->pages[index].fan_command_3);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_FAN_COMMAND_4:
        if (pmdev->pages[index].page_flags & PB_HAS_FAN) {
            pmbus_send16(pmdev, pmdev->pages[index].fan_command_4);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_VOUT_OV_FAULT_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmbus_send16(pmdev, pmdev->pages[index].vout_ov_fault_limit);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_VOUT_OV_FAULT_RESPONSE:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmbus_send8(pmdev, pmdev->pages[index].vout_ov_fault_response);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_VOUT_OV_WARN_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmbus_send16(pmdev, pmdev->pages[index].vout_ov_warn_limit);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_VOUT_UV_WARN_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmbus_send16(pmdev, pmdev->pages[index].vout_uv_warn_limit);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_VOUT_UV_FAULT_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmbus_send16(pmdev, pmdev->pages[index].vout_uv_fault_limit);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_VOUT_UV_FAULT_RESPONSE:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmbus_send8(pmdev, pmdev->pages[index].vout_uv_fault_response);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_IOUT_OC_FAULT_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_IOUT) {
            pmbus_send16(pmdev, pmdev->pages[index].iout_oc_fault_limit);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_IOUT_OC_FAULT_RESPONSE:
        if (pmdev->pages[index].page_flags & PB_HAS_IOUT) {
            pmbus_send8(pmdev, pmdev->pages[index].iout_oc_fault_response);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_IOUT_OC_LV_FAULT_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_IOUT) {
            pmbus_send16(pmdev, pmdev->pages[index].iout_oc_lv_fault_limit);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_IOUT_OC_LV_FAULT_RESPONSE:
        if (pmdev->pages[index].page_flags & PB_HAS_IOUT) {
            pmbus_send8(pmdev, pmdev->pages[index].iout_oc_lv_fault_response);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_IOUT_OC_WARN_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_IOUT) {
            pmbus_send16(pmdev, pmdev->pages[index].iout_oc_warn_limit);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_IOUT_UC_FAULT_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_IOUT) {
            pmbus_send16(pmdev, pmdev->pages[index].iout_uc_fault_limit);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_IOUT_UC_FAULT_RESPONSE:
        if (pmdev->pages[index].page_flags & PB_HAS_IOUT) {
            pmbus_send8(pmdev, pmdev->pages[index].iout_uc_fault_response);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_OT_FAULT_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_TEMPERATURE) {
            pmbus_send16(pmdev, pmdev->pages[index].ot_fault_limit);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_OT_FAULT_RESPONSE:
        if (pmdev->pages[index].page_flags & PB_HAS_TEMPERATURE) {
            pmbus_send8(pmdev, pmdev->pages[index].ot_fault_response);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_OT_WARN_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_TEMPERATURE) {
            pmbus_send16(pmdev, pmdev->pages[index].ot_warn_limit);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_UT_WARN_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_TEMPERATURE) {
            pmbus_send16(pmdev, pmdev->pages[index].ut_warn_limit);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_UT_FAULT_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_TEMPERATURE) {
            pmbus_send16(pmdev, pmdev->pages[index].ut_fault_limit);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_UT_FAULT_RESPONSE:
        if (pmdev->pages[index].page_flags & PB_HAS_TEMPERATURE) {
            pmbus_send8(pmdev, pmdev->pages[index].ut_fault_response);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_VIN_OV_FAULT_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_VIN) {
            pmbus_send16(pmdev, pmdev->pages[index].vin_ov_fault_limit);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_VIN_OV_FAULT_RESPONSE:
        if (pmdev->pages[index].page_flags & PB_HAS_VIN) {
            pmbus_send8(pmdev, pmdev->pages[index].vin_ov_fault_response);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_VIN_OV_WARN_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_VIN) {
            pmbus_send16(pmdev, pmdev->pages[index].vin_ov_warn_limit);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_VIN_UV_WARN_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_VIN) {
            pmbus_send16(pmdev, pmdev->pages[index].vin_uv_warn_limit);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_VIN_UV_FAULT_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_VIN) {
            pmbus_send16(pmdev, pmdev->pages[index].vin_uv_fault_limit);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_VIN_UV_FAULT_RESPONSE:
        if (pmdev->pages[index].page_flags & PB_HAS_VIN) {
            pmbus_send8(pmdev, pmdev->pages[index].vin_uv_fault_response);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_IIN_OC_FAULT_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_IIN) {
            pmbus_send16(pmdev, pmdev->pages[index].iin_oc_fault_limit);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_IIN_OC_FAULT_RESPONSE:
        if (pmdev->pages[index].page_flags & PB_HAS_IIN) {
            pmbus_send8(pmdev, pmdev->pages[index].iin_oc_fault_response);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_IIN_OC_WARN_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_IIN) {
            pmbus_send16(pmdev, pmdev->pages[index].iin_oc_warn_limit);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_POUT_OP_FAULT_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_POUT) {
            pmbus_send16(pmdev, pmdev->pages[index].pout_op_fault_limit);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_POUT_OP_FAULT_RESPONSE:
        if (pmdev->pages[index].page_flags & PB_HAS_POUT) {
            pmbus_send8(pmdev, pmdev->pages[index].pout_op_fault_response);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_POUT_OP_WARN_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_POUT) {
            pmbus_send16(pmdev, pmdev->pages[index].pout_op_warn_limit);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_PIN_OP_WARN_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_PIN) {
            pmbus_send16(pmdev, pmdev->pages[index].pin_op_warn_limit);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_STATUS_BYTE:
        pmbus_send8(pmdev, pmdev->pages[index].status_word & 0xFF);
        break;

    case PMBUS_STATUS_WORD:
        pmbus_send16(pmdev, pmdev->pages[index].status_word);
        break;

    case PMBUS_STATUS_VOUT:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmbus_send8(pmdev, pmdev->pages[index].status_vout);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_STATUS_IOUT:
        if (pmdev->pages[index].page_flags & PB_HAS_IOUT) {
            pmbus_send8(pmdev, pmdev->pages[index].status_iout);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_STATUS_INPUT:
        if (pmdev->pages[index].page_flags & PB_HAS_VIN ||
            pmdev->pages[index].page_flags & PB_HAS_IIN ||
            pmdev->pages[index].page_flags & PB_HAS_PIN) {
            pmbus_send8(pmdev, pmdev->pages[index].status_input);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_STATUS_TEMPERATURE:
        if (pmdev->pages[index].page_flags & PB_HAS_TEMPERATURE) {
            pmbus_send8(pmdev, pmdev->pages[index].status_temperature);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_STATUS_CML:
        pmbus_send8(pmdev, pmdev->pages[index].status_cml);
        break;

    case PMBUS_STATUS_OTHER:
        pmbus_send8(pmdev, pmdev->pages[index].status_other);
        break;

    case PMBUS_STATUS_MFR_SPECIFIC:
        pmbus_send8(pmdev, pmdev->pages[index].status_mfr_specific);
        break;

    case PMBUS_STATUS_FANS_1_2:
        if (pmdev->pages[index].page_flags & PB_HAS_FAN) {
            pmbus_send8(pmdev, pmdev->pages[index].status_fans_1_2);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_STATUS_FANS_3_4:
        if (pmdev->pages[index].page_flags & PB_HAS_FAN) {
            pmbus_send8(pmdev, pmdev->pages[index].status_fans_3_4);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_READ_EIN:
        if (pmdev->pages[index].page_flags & PB_HAS_EIN) {
            pmbus_send(pmdev, pmdev->pages[index].read_ein, 5);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_READ_EOUT:
        if (pmdev->pages[index].page_flags & PB_HAS_EOUT) {
            pmbus_send(pmdev, pmdev->pages[index].read_eout, 5);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_READ_VIN:
        if (pmdev->pages[index].page_flags & PB_HAS_VIN) {
            pmbus_send16(pmdev, pmdev->pages[index].read_vin);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_READ_IIN:
        if (pmdev->pages[index].page_flags & PB_HAS_IIN) {
            pmbus_send16(pmdev, pmdev->pages[index].read_iin);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_READ_VCAP:
        if (pmdev->pages[index].page_flags & PB_HAS_VCAP) {
            pmbus_send16(pmdev, pmdev->pages[index].read_vcap);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_READ_VOUT:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmbus_send16(pmdev, pmdev->pages[index].read_vout);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_READ_IOUT:
        if (pmdev->pages[index].page_flags & PB_HAS_IOUT) {
            pmbus_send16(pmdev, pmdev->pages[index].read_iout);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_READ_TEMPERATURE_1:
        if (pmdev->pages[index].page_flags & PB_HAS_TEMPERATURE) {
            pmbus_send16(pmdev, pmdev->pages[index].read_temperature_1);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_READ_TEMPERATURE_2:
        if (pmdev->pages[index].page_flags & PB_HAS_TEMP2) {
            pmbus_send16(pmdev, pmdev->pages[index].read_temperature_2);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_READ_TEMPERATURE_3:
        if (pmdev->pages[index].page_flags & PB_HAS_TEMP3) {
            pmbus_send16(pmdev, pmdev->pages[index].read_temperature_3);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_READ_FAN_SPEED_1:
        if (pmdev->pages[index].page_flags & PB_HAS_FAN) {
            pmbus_send16(pmdev, pmdev->pages[index].read_fan_speed_1);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_READ_FAN_SPEED_2:
        if (pmdev->pages[index].page_flags & PB_HAS_FAN) {
            pmbus_send16(pmdev, pmdev->pages[index].read_fan_speed_2);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_READ_FAN_SPEED_3:
        if (pmdev->pages[index].page_flags & PB_HAS_FAN) {
            pmbus_send16(pmdev, pmdev->pages[index].read_fan_speed_3);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_READ_FAN_SPEED_4:
        if (pmdev->pages[index].page_flags & PB_HAS_FAN) {
            pmbus_send16(pmdev, pmdev->pages[index].read_fan_speed_4);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_READ_DUTY_CYCLE:
        if (pmdev->pages[index].page_flags & PB_HAS_FAN) {
            pmbus_send16(pmdev, pmdev->pages[index].read_duty_cycle);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_READ_FREQUENCY:
        if (pmdev->pages[index].page_flags & PB_HAS_FAN) {
            pmbus_send16(pmdev, pmdev->pages[index].read_frequency);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_READ_POUT:
        if (pmdev->pages[index].page_flags & PB_HAS_POUT) {
            pmbus_send16(pmdev, pmdev->pages[index].read_pout);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_READ_PIN:
        if (pmdev->pages[index].page_flags & PB_HAS_PIN) {
            pmbus_send16(pmdev, pmdev->pages[index].read_pin);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_REVISION:
        pmbus_send8(pmdev, pmdev->pages[index].revision);
        break;

    case PMBUS_MFR_ID:
        if (pmdev->pages[index].page_flags & PB_HAS_MFR_INFO) {
            pmbus_send_string(pmdev, pmdev->pages[index].mfr_id);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_MFR_MODEL:
        if (pmdev->pages[index].page_flags & PB_HAS_MFR_INFO) {
            pmbus_send_string(pmdev, pmdev->pages[index].mfr_model);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_MFR_REVISION:
        if (pmdev->pages[index].page_flags & PB_HAS_MFR_INFO) {
            pmbus_send_string(pmdev, pmdev->pages[index].mfr_revision);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_MFR_LOCATION:
        if (pmdev->pages[index].page_flags & PB_HAS_MFR_INFO) {
            pmbus_send_string(pmdev, pmdev->pages[index].mfr_location);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_MFR_VIN_MIN:
        if (pmdev->pages[index].page_flags & PB_HAS_VIN_RATING) {
            pmbus_send16(pmdev, pmdev->pages[index].mfr_vin_min);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_MFR_VIN_MAX:
        if (pmdev->pages[index].page_flags & PB_HAS_VIN_RATING) {
            pmbus_send16(pmdev, pmdev->pages[index].mfr_vin_max);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_MFR_IIN_MAX:
        if (pmdev->pages[index].page_flags & PB_HAS_IIN_RATING) {
            pmbus_send16(pmdev, pmdev->pages[index].mfr_iin_max);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_MFR_PIN_MAX:
        if (pmdev->pages[index].page_flags & PB_HAS_PIN_RATING) {
            pmbus_send16(pmdev, pmdev->pages[index].mfr_pin_max);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_MFR_VOUT_MIN:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT_RATING) {
            pmbus_send16(pmdev, pmdev->pages[index].mfr_vout_min);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_MFR_VOUT_MAX:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT_RATING) {
            pmbus_send16(pmdev, pmdev->pages[index].mfr_vout_max);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_MFR_IOUT_MAX:
        if (pmdev->pages[index].page_flags & PB_HAS_IOUT_RATING) {
            pmbus_send16(pmdev, pmdev->pages[index].mfr_iout_max);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_MFR_POUT_MAX:
        if (pmdev->pages[index].page_flags & PB_HAS_POUT_RATING) {
            pmbus_send16(pmdev, pmdev->pages[index].mfr_pout_max);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_MFR_MAX_TEMP_1:
        if (pmdev->pages[index].page_flags & PB_HAS_TEMP_RATING) {
            pmbus_send16(pmdev, pmdev->pages[index].mfr_max_temp_1);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_MFR_MAX_TEMP_2:
        if (pmdev->pages[index].page_flags & PB_HAS_TEMP_RATING) {
            pmbus_send16(pmdev, pmdev->pages[index].mfr_max_temp_2);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_MFR_MAX_TEMP_3:
        if (pmdev->pages[index].page_flags & PB_HAS_TEMP_RATING) {
            pmbus_send16(pmdev, pmdev->pages[index].mfr_max_temp_3);
        } else {
            goto passthough;
        }
        break;

    case PMBUS_IDLE_STATE:
        pmbus_send8(pmdev, PMBUS_ERR_BYTE);
        break;

    case PMBUS_CLEAR_FAULTS:
    case PMBUS_PAGE_PLUS_WRITE:
    case PMBUS_STORE_DEFAULT_ALL:
    case PMBUS_RESTORE_DEFAULT_ALL:
    case PMBUS_STORE_DEFAULT_CODE:
    case PMBUS_RESTORE_DEFAULT_CODE:
    case PMBUS_STORE_USER_ALL:
    case PMBUS_RESTORE_USER_ALL:
    case PMBUS_STORE_USER_CODE:
    case PMBUS_RESTORE_USER_CODE:
    case PMBUS_QUERY:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: reading from write only register 0x%02x\n",
                      __func__, pmdev->code);
        break;

passthough:
    default:
        if (pmdc->receive_byte) {
            ret = pmdc->receive_byte(pmdev);
        }
        break;
    }

    if (pmdev->out_buf_len != 0) {
        ret = pmbus_out_buf_pop(pmdev);
        return ret;
    }

    return ret;
}

static void pmbus_clear_faults(PMBusDevice *pmdev)
{
    for (uint8_t i = 0; i < pmdev->num_pages; i++) {
        pmdev->pages[i].status_word = 0;
        pmdev->pages[i].status_vout = 0;
        pmdev->pages[i].status_iout = 0;
        pmdev->pages[i].status_input = 0;
        pmdev->pages[i].status_temperature = 0;
        pmdev->pages[i].status_cml = 0;
        pmdev->pages[i].status_other = 0;
        pmdev->pages[i].status_mfr_specific = 0;
        pmdev->pages[i].status_fans_1_2 = 0;
        pmdev->pages[i].status_fans_3_4 = 0;
    }

}

static void pmbus_operation(PMBusDevice *pmdev)
{
    uint8_t index = pmdev->page;
    if ((pmdev->pages[index].operation & PB_OP_ON) == 0) {
        pmdev->pages[index].read_vout = 0;
        pmdev->pages[index].read_iout = 0;
        pmdev->pages[index].read_pout = 0;
        return;
    }

    if (pmdev->pages[index].operation & (PB_OP_ON | PB_OP_MARGIN_HIGH)) {
        pmdev->pages[index].read_vout = pmdev->pages[index].vout_margin_high;
    }

    if (pmdev->pages[index].operation & (PB_OP_ON | PB_OP_MARGIN_LOW)) {
        pmdev->pages[index].read_vout = pmdev->pages[index].vout_margin_low;
    }
    pmbus_check_limits(pmdev);
}

static int pmbus_write_data(SMBusDevice *smd, uint8_t *buf, uint8_t len)
{
    PMBusDevice *pmdev = PMBUS_DEVICE(smd);
    PMBusDeviceClass *pmdc = PMBUS_DEVICE_GET_CLASS(pmdev);
    int ret = 0;
    uint8_t index;

    if (len == 0) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: writing empty data\n", __func__);
        return PMBUS_ERR_BYTE;
    }

    if (!pmdev->pages) {
        pmbus_pages_alloc(pmdev);
    }

    pmdev->in_buf_len = len;
    pmdev->in_buf = buf;

    pmdev->code = buf[0];

    if (pmdev->code == PMBUS_CLEAR_FAULTS) {
        pmbus_clear_faults(pmdev);
    }

    if (len == 1) {
        return 0;
    }

    if (pmdev->code == PMBUS_PAGE) {
        pmdev->page = pmbus_receive8(pmdev);

        if (pmdev->page > pmdev->num_pages - 1 && pmdev->page != PB_ALL_PAGES) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: page %u is out of range\n",
                          __func__, pmdev->page);
            pmdev->page = 0;
            pmbus_cml_error(pmdev);
            return PMBUS_ERR_BYTE;
        }
        return 0;
    }

    if (pmdev->page == PB_ALL_PAGES) {
        for (int i = 0; i < pmdev->num_pages; i++) {
            pmdev->page = i;
            pmbus_write_data(smd, buf, len);
        }
        pmdev->page = PB_ALL_PAGES;
        return 0;
    }

    index = pmdev->page;

    switch (pmdev->code) {
    case PMBUS_OPERATION:
        pmdev->pages[index].operation = pmbus_receive8(pmdev);
        pmbus_operation(pmdev);
        break;

    case PMBUS_ON_OFF_CONFIG:
        pmdev->pages[index].on_off_config = pmbus_receive8(pmdev);
        break;

    case PMBUS_CLEAR_FAULTS:
        pmbus_clear_faults(pmdev);
        break;

    case PMBUS_PHASE:
        pmdev->pages[index].phase = pmbus_receive8(pmdev);
        break;

    case PMBUS_PAGE_PLUS_WRITE:
    case PMBUS_WRITE_PROTECT:
        pmdev->pages[index].write_protect = pmbus_receive8(pmdev);
        break;

    case PMBUS_VOUT_MODE:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT_MODE) {
            pmdev->pages[index].vout_mode = pmbus_receive8(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_VOUT_COMMAND:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmdev->pages[index].vout_command = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_VOUT_TRIM:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmdev->pages[index].vout_trim = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_VOUT_CAL_OFFSET:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmdev->pages[index].vout_cal_offset = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_VOUT_MAX:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmdev->pages[index].vout_max = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_VOUT_MARGIN_HIGH:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT_MARGIN) {
            pmdev->pages[index].vout_margin_high = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_VOUT_MARGIN_LOW:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT_MARGIN) {
            pmdev->pages[index].vout_margin_low = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_VOUT_TRANSITION_RATE:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmdev->pages[index].vout_transition_rate = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_VOUT_DROOP:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmdev->pages[index].vout_droop = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_VOUT_SCALE_LOOP:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmdev->pages[index].vout_scale_loop = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_VOUT_SCALE_MONITOR:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmdev->pages[index].vout_scale_monitor = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_VOUT_MIN:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT_RATING) {
            pmdev->pages[index].vout_min = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_POUT_MAX:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmdev->pages[index].pout_max = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_VIN_ON:
        if (pmdev->pages[index].page_flags & PB_HAS_VIN) {
            pmdev->pages[index].vin_on = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_VIN_OFF:
        if (pmdev->pages[index].page_flags & PB_HAS_VIN) {
            pmdev->pages[index].vin_off = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_IOUT_CAL_GAIN:
        if (pmdev->pages[index].page_flags & PB_HAS_IOUT_GAIN) {
            pmdev->pages[index].iout_cal_gain = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_FAN_CONFIG_1_2:
        if (pmdev->pages[index].page_flags & PB_HAS_FAN) {
            pmdev->pages[index].fan_config_1_2 = pmbus_receive8(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_FAN_COMMAND_1:
        if (pmdev->pages[index].page_flags & PB_HAS_FAN) {
            pmdev->pages[index].fan_command_1 = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_FAN_COMMAND_2:
        if (pmdev->pages[index].page_flags & PB_HAS_FAN) {
            pmdev->pages[index].fan_command_2 = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_FAN_CONFIG_3_4:
        if (pmdev->pages[index].page_flags & PB_HAS_FAN) {
            pmdev->pages[index].fan_config_3_4 = pmbus_receive8(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_FAN_COMMAND_3:
        if (pmdev->pages[index].page_flags & PB_HAS_FAN) {
            pmdev->pages[index].fan_command_3 = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_FAN_COMMAND_4:
        if (pmdev->pages[index].page_flags & PB_HAS_FAN) {
            pmdev->pages[index].fan_command_4 = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_VOUT_OV_FAULT_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmdev->pages[index].vout_ov_fault_limit = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_VOUT_OV_FAULT_RESPONSE:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmdev->pages[index].vout_ov_fault_response = pmbus_receive8(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_VOUT_OV_WARN_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmdev->pages[index].vout_ov_warn_limit = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_VOUT_UV_WARN_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmdev->pages[index].vout_uv_warn_limit = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_VOUT_UV_FAULT_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmdev->pages[index].vout_uv_fault_limit = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_VOUT_UV_FAULT_RESPONSE:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmdev->pages[index].vout_uv_fault_response = pmbus_receive8(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_IOUT_OC_FAULT_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_IOUT) {
            pmdev->pages[index].iout_oc_fault_limit = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_IOUT_OC_FAULT_RESPONSE:
        if (pmdev->pages[index].page_flags & PB_HAS_IOUT) {
            pmdev->pages[index].iout_oc_fault_response = pmbus_receive8(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_IOUT_OC_LV_FAULT_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_IOUT) {
            pmdev->pages[index].iout_oc_lv_fault_limit = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_IOUT_OC_LV_FAULT_RESPONSE:
        if (pmdev->pages[index].page_flags & PB_HAS_IOUT) {
            pmdev->pages[index].iout_oc_lv_fault_response
                = pmbus_receive8(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_IOUT_OC_WARN_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_IOUT) {
            pmdev->pages[index].iout_oc_warn_limit = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_IOUT_UC_FAULT_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_IOUT) {
            pmdev->pages[index].iout_uc_fault_limit = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_IOUT_UC_FAULT_RESPONSE:
        if (pmdev->pages[index].page_flags & PB_HAS_IOUT) {
            pmdev->pages[index].iout_uc_fault_response = pmbus_receive8(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_OT_FAULT_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_TEMPERATURE) {
            pmdev->pages[index].ot_fault_limit = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_OT_FAULT_RESPONSE:
        if (pmdev->pages[index].page_flags & PB_HAS_TEMPERATURE) {
            pmdev->pages[index].ot_fault_response = pmbus_receive8(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_OT_WARN_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_TEMPERATURE) {
            pmdev->pages[index].ot_warn_limit = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_UT_WARN_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_TEMPERATURE) {
            pmdev->pages[index].ut_warn_limit = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_UT_FAULT_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_TEMPERATURE) {
            pmdev->pages[index].ut_fault_limit = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_UT_FAULT_RESPONSE:
        if (pmdev->pages[index].page_flags & PB_HAS_TEMPERATURE) {
            pmdev->pages[index].ut_fault_response = pmbus_receive8(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_VIN_OV_FAULT_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_VIN) {
            pmdev->pages[index].vin_ov_fault_limit = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_VIN_OV_FAULT_RESPONSE:
        if (pmdev->pages[index].page_flags & PB_HAS_VIN) {
            pmdev->pages[index].vin_ov_fault_response = pmbus_receive8(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_VIN_OV_WARN_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_VIN) {
            pmdev->pages[index].vin_ov_warn_limit = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_VIN_UV_WARN_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_VIN) {
            pmdev->pages[index].vin_uv_warn_limit = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_VIN_UV_FAULT_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_VIN) {
            pmdev->pages[index].vin_uv_fault_limit = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_VIN_UV_FAULT_RESPONSE:
        if (pmdev->pages[index].page_flags & PB_HAS_VIN) {
            pmdev->pages[index].vin_uv_fault_response = pmbus_receive8(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_IIN_OC_FAULT_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_IIN) {
            pmdev->pages[index].iin_oc_fault_limit = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_IIN_OC_FAULT_RESPONSE:
        if (pmdev->pages[index].page_flags & PB_HAS_IIN) {
            pmdev->pages[index].iin_oc_fault_response = pmbus_receive8(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_IIN_OC_WARN_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_IIN) {
            pmdev->pages[index].iin_oc_warn_limit = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_POUT_OP_FAULT_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmdev->pages[index].pout_op_fault_limit = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_POUT_OP_FAULT_RESPONSE:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmdev->pages[index].pout_op_fault_response = pmbus_receive8(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_POUT_OP_WARN_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmdev->pages[index].pout_op_warn_limit = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_PIN_OP_WARN_LIMIT:
        if (pmdev->pages[index].page_flags & PB_HAS_PIN) {
            pmdev->pages[index].pin_op_warn_limit = pmbus_receive16(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_STATUS_BYTE:
        pmdev->pages[index].status_word = pmbus_receive8(pmdev);
        break;

    case PMBUS_STATUS_WORD:
        pmdev->pages[index].status_word = pmbus_receive16(pmdev);
        break;

    case PMBUS_STATUS_VOUT:
        if (pmdev->pages[index].page_flags & PB_HAS_VOUT) {
            pmdev->pages[index].status_vout = pmbus_receive8(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_STATUS_IOUT:
        if (pmdev->pages[index].page_flags & PB_HAS_IOUT) {
            pmdev->pages[index].status_iout = pmbus_receive8(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_STATUS_INPUT:
        pmdev->pages[index].status_input = pmbus_receive8(pmdev);
        break;

    case PMBUS_STATUS_TEMPERATURE:
        if (pmdev->pages[index].page_flags & PB_HAS_TEMPERATURE) {
            pmdev->pages[index].status_temperature = pmbus_receive8(pmdev);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_STATUS_CML:
        pmdev->pages[index].status_cml = pmbus_receive8(pmdev);
        break;

    case PMBUS_STATUS_OTHER:
        pmdev->pages[index].status_other = pmbus_receive8(pmdev);
        break;

    case PMBUS_STATUS_MFR_SPECIFIC:
        pmdev->pages[index].status_mfr_specific = pmbus_receive8(pmdev);
        break;

    case PMBUS_STATUS_FANS_1_2:
        if (pmdev->pages[index].page_flags & PB_HAS_FAN) {
            pmbus_send8(pmdev, pmdev->pages[index].status_fans_1_2);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_STATUS_FANS_3_4:
        if (pmdev->pages[index].page_flags & PB_HAS_FAN) {
            pmbus_send8(pmdev, pmdev->pages[index].status_fans_3_4);
        } else {
            goto passthrough;
        }
        break;

    case PMBUS_PAGE_PLUS_READ:
    case PMBUS_CAPABILITY:
    case PMBUS_COEFFICIENTS:
    case PMBUS_READ_EIN:
    case PMBUS_READ_EOUT:
    case PMBUS_READ_VIN:
    case PMBUS_READ_IIN:
    case PMBUS_READ_VCAP:
    case PMBUS_READ_VOUT:
    case PMBUS_READ_IOUT:
    case PMBUS_READ_TEMPERATURE_1:
    case PMBUS_READ_TEMPERATURE_2:
    case PMBUS_READ_TEMPERATURE_3:
    case PMBUS_READ_FAN_SPEED_1:
    case PMBUS_READ_FAN_SPEED_2:
    case PMBUS_READ_FAN_SPEED_3:
    case PMBUS_READ_FAN_SPEED_4:
    case PMBUS_READ_DUTY_CYCLE:
    case PMBUS_READ_FREQUENCY:
    case PMBUS_READ_POUT:
    case PMBUS_READ_PIN:
    case PMBUS_REVISION:
    case PMBUS_APP_PROFILE_SUPPORT:
    case PMBUS_MFR_VIN_MIN:
    case PMBUS_MFR_VIN_MAX:
    case PMBUS_MFR_IIN_MAX:
    case PMBUS_MFR_PIN_MAX:
    case PMBUS_MFR_VOUT_MIN:
    case PMBUS_MFR_VOUT_MAX:
    case PMBUS_MFR_IOUT_MAX:
    case PMBUS_MFR_POUT_MAX:
    case PMBUS_MFR_TAMBIENT_MAX:
    case PMBUS_MFR_TAMBIENT_MIN:
    case PMBUS_MFR_EFFICIENCY_LL:
    case PMBUS_MFR_EFFICIENCY_HL:
    case PMBUS_MFR_PIN_ACCURACY:
    case PMBUS_IC_DEVICE_ID:
    case PMBUS_IC_DEVICE_REV:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: writing to read-only register 0x%02x\n",
                      __func__, pmdev->code);
        break;

passthrough:
    default:
        if (pmdc->write_data) {
            ret = pmdc->write_data(pmdev, buf, len);
        }
        break;
    }
    pmbus_check_limits(pmdev);
    pmdev->in_buf_len = 0;
    return ret;
}

int pmbus_page_config(PMBusDevice *pmdev, uint8_t index, uint64_t flags)
{
    if (!pmdev->pages) {
        pmbus_pages_alloc(pmdev);
    }

    if (index == PB_ALL_PAGES) {
        for (int i = 0; i < pmdev->num_pages; i++) {
            pmdev->pages[i].page_flags = flags;
        }
        return 0;
    }

    if (index > pmdev->num_pages - 1) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: index %u is out of range\n",
                      __func__, index);
        return -1;
    }

    pmdev->pages[index].page_flags = flags;

    return 0;
}

const VMStateDescription vmstate_pmbus_device = {
    .name = TYPE_PMBUS_DEVICE,
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_SMBUS_DEVICE(smb, PMBusDevice),
        VMSTATE_UINT8(num_pages, PMBusDevice),
        VMSTATE_UINT8(code, PMBusDevice),
        VMSTATE_UINT8(page, PMBusDevice),
        VMSTATE_UINT8(capability, PMBusDevice),
        VMSTATE_END_OF_LIST()
    }
};

static void pmbus_device_finalize(Object *obj)
{
    PMBusDevice *pmdev = PMBUS_DEVICE(obj);
    g_free(pmdev->pages);
}

static void pmbus_device_class_init(ObjectClass *klass, const void *data)
{
    SMBusDeviceClass *k = SMBUS_DEVICE_CLASS(klass);

    k->quick_cmd = pmbus_quick_cmd;
    k->write_data = pmbus_write_data;
    k->receive_byte = pmbus_receive_byte;
}

static const TypeInfo pmbus_device_type_info = {
    .name = TYPE_PMBUS_DEVICE,
    .parent = TYPE_SMBUS_DEVICE,
    .instance_size = sizeof(PMBusDevice),
    .instance_finalize = pmbus_device_finalize,
    .abstract = true,
    .class_size = sizeof(PMBusDeviceClass),
    .class_init = pmbus_device_class_init,
};

static void pmbus_device_register_types(void)
{
    type_register_static(&pmbus_device_type_info);
}

type_init(pmbus_device_register_types)
