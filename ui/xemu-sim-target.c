/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#include "xemu-sim-target.h"

#include <math.h>

static long clampl(double v, long lo, long hi)
{
    return v < lo ? lo : v > hi ? hi : (long)v;
}

static bool edge(uint64_t now, uint64_t prev, int bit)
{
    return ((now >> bit) & 1) && !((prev >> bit) & 1);
}

void sim_target_write_sbc(const SimMerge *m, uint64_t *buttons, uint64_t *previous_buttons, int16_t *axis,
                          uint8_t *gear_lever, uint8_t *tuner_dial, uint8_t *toggle_switches)
{
    uint64_t b = 0;
    const SimMergeOut *o;
#define SIM_SBC_BUTTON(id, name, bit) \
    if ((o = sim_merge_find(m, #id)) && o->pressed) { \
        b |= 1ULL << (bit); \
    }
    SIM_SBC_BUTTONS(SIM_SBC_BUTTON)
#undef SIM_SBC_BUTTON
    *buttons = b;

    if (*gear_lever == 0) {
        *gear_lever = 255;
    }
    for (int bit = 34; bit <= 38; bit++) {
        if (edge(b, *previous_buttons, bit)) {
            *toggle_switches ^= (uint8_t)(1u << (bit - 32));
        }
    }
    if (edge(b, *previous_buttons, 41)) {
        *tuner_dial = *tuner_dial == 0 ? 15 : *tuner_dial - 1;
    }
    if (edge(b, *previous_buttons, 42)) {
        *tuner_dial = *tuner_dial == 15 ? 0 : *tuner_dial + 1;
    }
    if (edge(b, *previous_buttons, 39) && *gear_lever != 5) {
        *gear_lever = *gear_lever == 255 ? 1 : *gear_lever + 1;
    }
    if (edge(b, *previous_buttons, 40) && *gear_lever != 254) {
        *gear_lever = *gear_lever == 1 ? 255 : *gear_lever - 1;
    }

    for (int i = 0; i < SIM_SBC_NAXES; i++) {
        axis[i] = 0;
    }
#define SIM_SBC_AXIS(id, name, index) \
    if ((o = sim_merge_find(m, #id))) { \
        if ((index) <= 1) { \
            axis[index] = sim_merge_sbc_aiming_axis(o->raw); \
        } else if ((index) >= 3 && (index) <= 5) { \
            axis[index] = (int16_t)(uint16_t)clampl(o->raw, 0, 65535); \
        } else { \
            axis[index] = (int16_t)clampl(o->raw, -32768, 32767); \
        } \
    }
    SIM_SBC_AXES(SIM_SBC_AXIS)
#undef SIM_SBC_AXIS
    *previous_buttons = b;
}

void sim_target_write_duke(const SimMerge *m, uint16_t *buttons, int16_t *axis)
{
    uint16_t b = 0;
    const SimMergeOut *o;
#define SIM_DUKE_BUTTON(id, name, bit) \
    if ((o = sim_merge_find(m, #id)) && o->pressed) { \
        b |= (uint16_t)(1u << (bit)); \
    }
    SIM_DUKE_BUTTONS(SIM_DUKE_BUTTON)
#undef SIM_DUKE_BUTTON
    *buttons = b;
    for (int i = 0; i < SIM_DUKE_NAXES; i++) {
        axis[i] = 0;
    }
#define SIM_DUKE_AXIS(id, name, index, trigger) \
    if ((o = sim_merge_find(m, #id))) { \
        axis[index] = (trigger) ? (int16_t)(clampl(o->raw, 0, 255) << 7) : (int16_t)clampl(o->raw, -32768, 32767); \
    }
    SIM_DUKE_AXES(SIM_DUKE_AXIS)
#undef SIM_DUKE_AXIS
}
