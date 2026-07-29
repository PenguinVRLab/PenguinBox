/*
 * xemu Settings Management
 *
 * Copyright (C) 2020-2022 Matt Borgerson
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
#include <stdlib.h>
#include <SDL3/SDL_filesystem.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <toml++/toml.h>
#include <cnode.h>
#include <sstream>
#include <iostream>
#include <locale.h>

#include "xemu-controllers.h"
#include "xemu-settings.h"

#define DEFINE_CONFIG_TREE
#include "xemu-config.h"

struct config g_config;

static CONFIG_DISPLAY_RENDERER g_requested_renderer = -1;

void xemu_settings_note_requested_renderer(CONFIG_DISPLAY_RENDERER r)
{
    g_requested_renderer = r;
}

static const char *filename = "xemu.toml";
static const char *settings_path;
static std::string error_msg;

const char *xemu_settings_get_error_message(void)
{
    return error_msg.length() ? error_msg.c_str() : NULL;
}

static bool xemu_settings_detect_portable_mode(void)
{
    bool val = false;
    char *portable_path = g_strdup_printf("%s%s", SDL_GetBasePath(), filename);
    FILE *tmpfile;
    if ((tmpfile = qemu_fopen(portable_path, "r"))) {
        fclose(tmpfile);
        val = true;
    }

    free(portable_path);
    return val;
}

void xemu_settings_set_path(const char *path)
{
    assert(path != NULL);
    assert(settings_path == NULL);
    settings_path = path;
    fprintf(stderr, "%s: config path: %s\n", __func__, settings_path);
}

const char *xemu_settings_get_base_path(void)
{
    static const char *base_path = NULL;
    if (base_path != NULL) {
        return base_path;
    }

    if (xemu_settings_detect_portable_mode()) {
        const char *base = SDL_GetBasePath();
        assert(base != NULL);
        base_path = g_strdup(base);
    } else {
        char *base = SDL_GetPrefPath("xemu", "xemu");
        assert(base != NULL);
        base_path = g_strdup(base);
        SDL_free(base);
    }
    fprintf(stderr, "%s: base path: %s\n", __func__, base_path);
    return base_path;
}

const char *xemu_settings_get_path(void)
{
    if (settings_path != NULL) {
        return settings_path;
    }

    const char *base = xemu_settings_get_base_path();
    assert(base != NULL);
    settings_path = g_strdup_printf("%s%s", base, filename);
    fprintf(stderr, "%s: config path: %s\n", __func__, settings_path);
    return settings_path;
}

const char *xemu_settings_get_default_eeprom_path(void)
{
    static char *eeprom_path = NULL;
    if (eeprom_path != NULL) {
        return eeprom_path;
    }

    const char *base = xemu_settings_get_base_path();
    assert(base != NULL);
    eeprom_path = g_strdup_printf("%s%s", base, "eeprom.bin");
    return eeprom_path;
}

static ssize_t get_file_size(FILE *fd)
{
    if (fseek(fd, 0, SEEK_END)) {
        return -1;
    }

    size_t file_size = ftell(fd);

    if (fseek(fd, 0, SEEK_SET)) {
        return -1;
    }

    return file_size;
}

static const char *read_file(FILE *fd)
{
    ssize_t size = get_file_size(fd);
    if (size < 0) {
        return NULL;
    }

    char *buf = (char *)malloc(size + 1);
    if (!buf) {
        return NULL;
    }

    if (size > 0) {
        if (fread(buf, size, 1, fd) != 1) {
            free(buf);
            return NULL;
        }
    }

    buf[size] = '\x00';
    return buf;
}

static std::string s_last_written_toml;
static std::string xemu_settings_generate_toml(void);

bool xemu_settings_load(void)
{
    const char *settings_path = xemu_settings_get_path();
    bool success = false;

    if (qemu_access(settings_path, F_OK) == -1) {
        fprintf(stderr, "Config file not found, starting with default settings.\n");
        success = true;
    } else {
        FILE *fd = qemu_fopen(settings_path, "rb");
        if (fd) {
            const char *buf = read_file(fd);
            if (buf) {
                char *previous_numeric_locale = setlocale(LC_NUMERIC, NULL);
                if (previous_numeric_locale) {
                    previous_numeric_locale = g_strdup(previous_numeric_locale);
                }

                setlocale(LC_NUMERIC, "C");

                try {
                    config_tree.update_from_table(toml::parse(buf));
                    success = true;
                } catch (const toml::parse_error& err) {
                   std::ostringstream oss;
                   oss << "Error parsing config file at " << err.source().begin << ":\n"
                       << "    " << err.description() << "\n"
                       << "Please fix the error or delete the file to continue.\n";
                   error_msg = oss.str();
                }
                free((char*)buf);

                if (previous_numeric_locale) {
                    setlocale(LC_NUMERIC, previous_numeric_locale);
                    g_free(previous_numeric_locale);
                }
            } else {
                error_msg = "Failed to read config file.\n";
            }
            fclose(fd);
        } else {
            error_msg = "Failed to open config file for reading. Check permissions.\n";
        }
    }

    config_tree.store_to_struct(&g_config);

    s_last_written_toml = xemu_settings_generate_toml();

    return success;
}

static std::string xemu_settings_generate_toml(void)
{
    char *previous_numeric_locale = setlocale(LC_NUMERIC, NULL);
    if (previous_numeric_locale) {
        previous_numeric_locale = g_strdup(previous_numeric_locale);
    }

    setlocale(LC_NUMERIC, "C");

    g_config.input.allow_vibration = true;

    config_tree.update_from_struct(&g_config);
    std::string toml = config_tree.generate_delta_toml();

    if (previous_numeric_locale) {
        setlocale(LC_NUMERIC, previous_numeric_locale);
        g_free(previous_numeric_locale);
    }

    return toml;
}

static void xemu_settings_apply_requested_renderer(void)
{
    if (g_requested_renderer >= 0) {
        g_config.display.renderer = g_requested_renderer;
    }
}

static bool xemu_settings_write_toml(const std::string &toml)
{
    FILE *fd = qemu_fopen(xemu_settings_get_path(), "wb");
    if (!fd) {
        fprintf(stderr, "Failed to open config file for writing. Check permissions.\n");
        return false;
    }

    fprintf(fd, "%s", toml.c_str());
    fclose(fd);
    return true;
}

void xemu_settings_save(void)
{
    xemu_settings_apply_requested_renderer();
    std::string toml = xemu_settings_generate_toml();
    if (xemu_settings_write_toml(toml)) {
        s_last_written_toml = toml;
    }
}

bool xemu_settings_save_if_changed(void)
{
    std::string toml = xemu_settings_generate_toml();
    if (toml == s_last_written_toml) {
        return false;
    }
    if (!xemu_settings_write_toml(toml)) {
        return false;
    }
    s_last_written_toml = toml;
    return true;
}

void add_net_nat_forward_ports(int host, int guest, CONFIG_NET_NAT_FORWARD_PORTS_PROTOCOL protocol)
{
    auto cnode = config_tree.child("net")
                            ->child("nat")
                            ->child("forward_ports");
    cnode->update_from_struct(&g_config);
    cnode->children.push_back(*cnode->array_item_type);
    auto &e = cnode->children.back();
    e.child("host")->set_integer(host);
    e.child("guest")->set_integer(guest);
    e.child("protocol")->set_enum_by_index(protocol);
    cnode->free_allocations(&g_config);
    cnode->store_to_struct(&g_config);
}

void remove_net_nat_forward_ports(unsigned int index)
{
    auto cnode = config_tree.child("net")
                            ->child("nat")
                            ->child("forward_ports");
    cnode->update_from_struct(&g_config);
    cnode->children.erase(cnode->children.begin()+index);
    cnode->free_allocations(&g_config);
    cnode->store_to_struct(&g_config);
}

bool xemu_settings_load_gamepad_mapping(const char *guid,
                                        GamepadMappings **mapping)
{
    unsigned int i;
    unsigned int gamepad_mappings_count = g_config.input.gamepad_mappings_count;
    for (i = 0; i < gamepad_mappings_count; ++i) {
        *mapping = &g_config.input.gamepad_mappings[i];
        if (strcmp((*mapping)->gamepad_id, guid) != 0) {
            continue;
        }

        if (!g_config.input.allow_vibration) {
            (*mapping)->enable_rumble = g_config.input.allow_vibration;
        }

        return false;
    }

    auto cnode = config_tree.child("input")->child("gamepad_mappings");
    cnode->update_from_struct(&g_config);
    cnode->free_allocations(&g_config);

    cnode->children.push_back(*cnode->array_item_type);
    CNode *mapping_node = &cnode->children.back();

    mapping_node->child("gamepad_id")->set_string(guid);

    cnode->store_to_struct(&g_config);

    *mapping =
        &g_config.input
             .gamepad_mappings[g_config.input.gamepad_mappings_count - 1];

    if (!g_config.input.allow_vibration) {
        (*mapping)->enable_rumble = g_config.input.allow_vibration;
    }

    return true;
}

void xemu_settings_reset_controller_mapping(const char *guid)
{
    unsigned int gamepad_mappings_count = g_config.input.gamepad_mappings_count;

    unsigned int i;
    struct config::input::gamepad_mappings *mapping;

    for (i = 0; i < gamepad_mappings_count; ++i) {
        mapping = &g_config.input.gamepad_mappings[i];
        if (strcmp(mapping->gamepad_id, guid) == 0) {
            break;
        }
    }

    if (i == gamepad_mappings_count) {
        return;
    }

    CNode *cnode = config_tree.child("input")->child("gamepad_mappings");
    cnode->update_from_struct(&g_config);

    CNode *mapping_node = &cnode->children[i];
    mapping_node->reset_to_defaults();
    mapping_node->child("gamepad_id")->set_string(guid);
    mapping_node->store_to_struct(mapping);
}

void xemu_settings_reset_keyboard_mapping(void)
{
  auto cnode = config_tree.child("input")->child("keyboard_controller_scancode_map");
  cnode->update_from_struct(&g_config);
  cnode->reset_to_defaults();
  cnode->store_to_struct(&g_config);
}
