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

#ifndef XEMU_SIM_PROFILES_H
#define XEMU_SIM_PROFILES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SimDeviceKind {
    SIM_DEV_SBC = 0,
} SimDeviceKind;

typedef struct SimRequirement {
    SimDeviceKind device;
    bool required;
    const char *label;
} SimRequirement;

typedef struct SimProfile {
    uint32_t title_id;
    const char *display_name;
    const SimRequirement *requirements;
    unsigned num_requirements;
    const char *notes;
} SimProfile;

const SimProfile *sim_profile_lookup(uint32_t title_id);

void sim_gate_init(bool device_layer_up);

const SimProfile *sim_gate_active_profile(void);

bool sim_gate_device_layer_up(void);

void sim_gate_release(bool skipped);

#ifdef __cplusplus
}
#endif

#endif
