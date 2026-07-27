/*
 * Definitions for talking to the PMU.  The PMU is a microcontroller
 * which controls battery charging and system power on PowerBook 3400
 * and 2400 models as well as the RTC and various other things.
 *
 * Copyright (C) 1998 Paul Mackerras.
 * Copyright (C) 2016 Ben Herrenschmidt
 */

#ifndef PMU_H
#define PMU_H

#include "hw/input/adb.h"
#include "hw/misc/mos6522.h"
#include "hw/misc/macio/gpio.h"
#include "qom/object.h"

#define PMU_POWER_CTRL0            0x10
#define PMU_POWER_CTRL             0x11
#define PMU_ADB_CMD                0x20
#define PMU_ADB_POLL_OFF           0x21
#define PMU_WRITE_NVRAM            0x33
#define PMU_READ_NVRAM             0x3b
#define PMU_SET_RTC                0x30
#define PMU_READ_RTC               0x38
#define PMU_SET_VOLBUTTON          0x40
#define PMU_BACKLIGHT_BRIGHT       0x41
#define PMU_GET_VOLBUTTON          0x48
#define PMU_PCEJECT                0x4c
#define PMU_BATTERY_STATE          0x6b
#define PMU_SMART_BATTERY_STATE    0x6f
#define PMU_SET_INTR_MASK          0x70
#define PMU_INT_ACK                0x78
#define PMU_SHUTDOWN               0x7e
#define PMU_CPU_SPEED              0x7d
#define PMU_SLEEP                  0x7f
#define PMU_POWER_EVENTS           0x8f
#define PMU_I2C_CMD                0x9a
#define PMU_RESET                  0xd0
#define PMU_GET_BRIGHTBUTTON       0xd9
#define PMU_GET_COVER              0xdc
#define PMU_SYSTEM_READY           0xdf
#define PMU_DOWNLOAD_STATUS        0xe2
#define PMU_READ_PMU_RAM           0xe8
#define PMU_GET_VERSION            0xea

#define PMU_POW0_ON            0x80
#define PMU_POW0_OFF           0x00
#define PMU_POW0_HARD_DRIVE    0x04

#define PMU_POW_ON             0x80
#define PMU_POW_OFF            0x00
#define PMU_POW_BACKLIGHT      0x01
#define PMU_POW_CHARGER        0x02
#define PMU_POW_IRLED          0x04
#define PMU_POW_MEDIABAY       0x08

#define PMU_INT_PCEJECT        0x04
#define PMU_INT_SNDBRT         0x08
#define PMU_INT_ADB            0x10
#define PMU_INT_BATTERY        0x20
#define PMU_INT_ENVIRONMENT    0x40
#define PMU_INT_TICK           0x80

#define PMU_INT_ADB_AUTO           0x04
#define PMU_INT_WAITING_CHARGER    0x01
#define PMU_INT_AUTO_SRQ_POLL      0x02

#define PMU_ENV_LID_CLOSED     0x01

#define PMU_I2C_MODE_SIMPLE    0
#define PMU_I2C_MODE_STDSUB    1
#define PMU_I2C_MODE_COMBINED  2

#define PMU_I2C_BUS_STATUS     0
#define PMU_I2C_BUS_SYSCLK     1
#define PMU_I2C_BUS_POWER      2

#define PMU_I2C_STATUS_OK          0
#define PMU_I2C_STATUS_DATAREAD    1
#define PMU_I2C_STATUS_BUSY        0xfe

enum {
    PMU_UNKNOWN,
    PMU_OHARE_BASED,
    PMU_HEATHROW_BASED,
    PMU_PADDINGTON_BASED,
    PMU_KEYLARGO_BASED,
    PMU_68K_V1,
    PMU_68K_V2,
};

enum {
    PMU_PWR_GET_POWERUP_EVENTS = 0x00,
    PMU_PWR_SET_POWERUP_EVENTS = 0x01,
    PMU_PWR_CLR_POWERUP_EVENTS = 0x02,
    PMU_PWR_GET_WAKEUP_EVENTS = 0x03,
    PMU_PWR_SET_WAKEUP_EVENTS = 0x04,
    PMU_PWR_CLR_WAKEUP_EVENTS = 0x05,
};

enum {
    PMU_PWR_WAKEUP_KEY = 0x01,
    PMU_PWR_WAKEUP_AC_INSERT = 0x02,
    PMU_PWR_WAKEUP_AC_CHANGE = 0x04,
    PMU_PWR_WAKEUP_LID_OPEN = 0x08,
    PMU_PWR_WAKEUP_RING = 0x10,
};

static const int8_t pmu_data_len[256][2] = {
    {-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
    {-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},
    { 1,  0},{ 1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
    { 0,  1},{ 0,  1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{ 0,  0},
    {-1,  0},{ 0,  0},{ 2,  0},{ 1,  0},{ 1,  0},{-1,  0},{-1,  0},{-1,  0},
    { 0, -1},{ 0, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{ 0, -1},
    { 4,  0},{20,  0},{-1,  0},{ 3,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
    { 0,  4},{ 0, 20},{ 2, -1},{ 2,  1},{ 3, -1},{-1, -1},{-1, -1},{ 4,  0},
    { 1,  0},{ 1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
    { 0,  1},{ 0,  1},{-1, -1},{ 1,  0},{ 1,  0},{-1, -1},{-1, -1},{-1, -1},
    { 1,  0},{ 0,  0},{ 2,  0},{ 2,  0},{-1,  0},{ 1,  0},{ 3,  0},{ 1,  0},
    { 0,  1},{ 1,  0},{ 0,  2},{ 0,  2},{ 0, -1},{-1, -1},{-1, -1},{-1, -1},
    { 2,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
    { 0,  3},{ 0,  3},{ 0,  2},{ 0,  8},{ 0, -1},{ 0, -1},{-1, -1},{-1, -1},
    { 1,  0},{ 1,  0},{ 1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
    { 0, -1},{ 0, -1},{-1, -1},{-1, -1},{-1, -1},{ 5,  1},{ 4,  1},{ 4,  1},
    { 4,  0},{-1,  0},{ 0,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
    { 0,  5},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},
    { 1,  0},{ 2,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
    { 0,  1},{ 0,  1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},
    { 2,  0},{ 2,  0},{ 2,  0},{ 4,  0},{-1,  0},{ 0,  0},{-1,  0},{-1,  0},
    { 1,  1},{ 1,  0},{ 3,  0},{ 2,  0},{-1, -1},{-1, -1},{-1, -1},{-1, -1},
    {-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
    {-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},
    {-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
    {-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},
    { 0,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
    { 1,  1},{ 1,  1},{-1, -1},{-1, -1},{ 0,  1},{ 0, -1},{-1, -1},{-1, -1},
    {-1,  0},{ 4,  0},{ 0,  1},{-1,  0},{-1,  0},{ 4,  0},{-1,  0},{-1,  0},
    { 3, -1},{-1, -1},{ 0,  1},{-1, -1},{ 0, -1},{-1, -1},{-1, -1},{ 0,  0},
    {-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
    {-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},
};

typedef enum {
    pmu_state_idle,
    pmu_state_cmd,
    pmu_state_rsp,
} PMUCmdState;

struct MOS6522PMUState {
    MOS6522State parent_obj;
};

#define TYPE_MOS6522_PMU "mos6522-pmu"
OBJECT_DECLARE_SIMPLE_TYPE(MOS6522PMUState, MOS6522_PMU)

struct PMUState {
    SysBusDevice parent_obj;

    MemoryRegion mem;
    uint64_t frequency;

    MOS6522PMUState mos6522_pmu;

    PMUCmdState cmd_state;
    uint8_t last_b;
    uint8_t cmd;
    uint32_t cmdlen;
    uint32_t rsplen;
    uint8_t cmd_buf_pos;
    uint8_t cmd_buf[128];
    uint8_t cmd_rsp_pos;
    uint8_t cmd_rsp_sz;
    uint8_t cmd_rsp[128];

    uint8_t intbits;
    uint8_t intmask;

    bool has_adb;
    ADBBusState adb_bus;
    uint8_t adb_reply_size;
    uint8_t adb_reply[ADB_MAX_OUT_LEN];

    uint32_t tick_offset;
    QEMUTimer *one_sec_timer;
    int64_t one_sec_target;

    MacIOGPIOState *gpio;
};

#define TYPE_VIA_PMU "via-pmu"
OBJECT_DECLARE_SIMPLE_TYPE(PMUState, VIA_PMU)

#endif
