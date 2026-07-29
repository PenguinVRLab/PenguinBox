/*
 * xemu Settings Management
 *
 * Primary storage for non-volatile user configuration. Basic key-value storage
 * that gets saved to an INI file. All entries should be accessed through the
 * appropriate getter/setter functions.
 *
 * Copyright (C) 2020-2021 Matt Borgerson
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

#ifndef XEMU_SETTINGS_H
#define XEMU_SETTINGS_H

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "xemu-config.h"

#ifdef __cplusplus
using GamepadMappings = struct config::input::gamepad_mappings;
#else
typedef struct gamepad_mappings GamepadMappings;
#endif

extern struct config g_config;

void xemu_settings_set_path(const char *path);

const char *xemu_settings_get_base_path(void);

const char *xemu_settings_get_path(void);

const char *xemu_settings_get_default_eeprom_path(void);

const char *xemu_settings_get_error_message(void);

bool xemu_settings_load(void);

void xemu_settings_save(void);

void xemu_settings_note_requested_renderer(CONFIG_DISPLAY_RENDERER r);

bool xemu_settings_save_if_changed(void);

static inline void xemu_settings_set_string(const char **str, const char *new_str)
{
    assert(new_str);
    free((char*)*str);
    *str = strdup(new_str);
}

void add_net_nat_forward_ports(int host, int guest, CONFIG_NET_NAT_FORWARD_PORTS_PROTOCOL protocol);
void remove_net_nat_forward_ports(unsigned int index);


bool xemu_settings_load_gamepad_mapping(const char *guid,
                                        GamepadMappings **mapping);

void xemu_settings_reset_controller_mapping(const char *guid);

void xemu_settings_reset_keyboard_mapping(void);

#ifdef __cplusplus
}
#endif

#endif
