/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#ifndef XEMU_SIM_TARGET_H
#define XEMU_SIM_TARGET_H

#include <stdint.h>

#include "xemu-sim-merge.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SIM_SBC_BUTTONS(M) \
    M(main_weapon, MAIN_WEAPON, 0) M(sub_weapon, SUB_WEAPON, 1) M(lock_on, LOCK_ON, 2) M(eject, EJECT, 3) \
    M(cockpit_hatch, COCKPIT_HATCH, 4) M(ignition, IGNITION, 5) M(start, START, 6) M(open_close, OPEN_CLOSE, 7) \
    M(map_zoom_in_out, MAP_ZOOM_IN_OUT, 8) M(mode_select, MODE_SELECT, 9) \
    M(sub_monitor_mode_select, SUB_MONITOR_MODE_SELECT, 10) M(zoom_in, ZOOM_IN, 11) M(zoom_out, ZOOM_OUT, 12) \
    M(fss, FSS, 13) M(manipulator, MANIPULATOR, 14) M(line_color_change, LINE_COLOR_CHANGE, 15) \
    M(washing, WASHING, 16) M(extinguisher, EXTINGUISHER, 17) M(chaff, CHAFF, 18) M(tank_detach, TANK_DETACH, 19) \
    M(override, OVERRIDE, 20) M(night_scope, NIGHT_SCOPE, 21) M(func1, FUNC1, 22) M(func2, FUNC2, 23) \
    M(func3, FUNC3, 24) M(main_weapon_control, MAIN_WEAPON_CONTROL, 25) \
    M(sub_weapon_control, SUB_WEAPON_CONTROL, 26) M(magazine_change, MAGAZINE_CHANGE, 27) M(com1, COM1, 28) \
    M(com2, COM2, 29) M(com3, COM3, 30) M(com4, COM4, 31) M(com5, COM5, 32) M(sight_change, SIGHT_CHANGE, 33) \
    M(filt_control_system, FILT_CONTROL_SYSTEM, 34) M(oxygen_supply_system, OXYGEN_SUPPLY_SYSTEM, 35) \
    M(fuel_flow_rate, FUEL_FLOW_RATE, 36) M(buffer_material, BUFFER_MATERIAL, 37) \
    M(vt_location_measurement, VT_LOCATION_MEASUREMENT, 38) M(gear_up, GEAR_UP, 39) M(gear_down, GEAR_DOWN, 40) \
    M(tuner_left, TUNER_LEFT, 41) M(tuner_right, TUNER_RIGHT, 42)

#define SIM_SBC_AXES(M) \
    M(aiming_x, AIMING_X, 0) M(aiming_y, AIMING_Y, 1) M(rotation_lever, ROTATION_LEVER, 2) \
    M(left_pedal, LEFT_PEDAL, 3) M(middle_pedal, MIDDLE_PEDAL, 4) M(right_pedal, RIGHT_PEDAL, 5) \
    M(sight_change_x, SIGHT_CHANGE_X, 6) M(sight_change_y, SIGHT_CHANGE_Y, 7)
#define SIM_SBC_NAXES 8

#define SIM_DUKE_BUTTONS(M) \
    M(a, A, 0) M(b, B, 1) M(x, X, 2) M(y, Y, 3) M(dpad_left, DPAD_LEFT, 4) M(dpad_up, DPAD_UP, 5) \
    M(dpad_right, DPAD_RIGHT, 6) M(dpad_down, DPAD_DOWN, 7) M(back, BACK, 8) M(start, START, 9) \
    M(white, WHITE, 10) M(black, BLACK, 11) M(lstick, LSTICK, 12) M(rstick, RSTICK, 13)

#define SIM_DUKE_AXES(M) \
    M(left_trigger, LTRIG, 0, 1) M(right_trigger, RTRIG, 1, 1) M(left_stick_x, LSTICK_X, 2, 0) \
    M(left_stick_y, LSTICK_Y, 3, 0) M(right_stick_x, RSTICK_X, 4, 0) M(right_stick_y, RSTICK_Y, 5, 0)
#define SIM_DUKE_NAXES 6

void sim_target_write_sbc(const SimMerge *m, uint64_t *buttons, uint64_t *previous_buttons, int16_t *axis,
                          uint8_t *gear_lever, uint8_t *tuner_dial, uint8_t *toggle_switches);

void sim_target_write_duke(const SimMerge *m, uint16_t *buttons, int16_t *axis);

#ifdef __cplusplus
}
#endif

#endif
