/*
 * Copyright (C) 2026 Patrick Carey
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include <toml++/toml.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include "xemu-machine-profile.h"

static void put_note(char *note, size_t len, const std::string &s)
{
    if (note && len) {
        snprintf(note, len, "%s", s.c_str());
    }
}

static const char *const reserved_keys[] = { "cpu", "disk", "xbe_clear_devkit_memory_limit" };

static bool valid_ram(int64_t v)
{
    return v == 64 || v == 128;
}

static bool load_table(const char *path, toml::table &out, std::string &why)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return true;
    }
    try {
        out = toml::parse_file(path);
    } catch (const toml::parse_error &err) {
        std::ostringstream o;
        o << "console-upgrades: " << path << " unreadable (" << err.description()
          << ", line " << err.source().begin.line << "): every game uses the global settings";
        why = o.str();
        return false;
    }
    return true;
}

extern "C" int xemu_mp_store_get_ram(const char *store_path, const char *title_id,
                                     int *ram_mb, char *note, size_t notelen)
{
    *ram_mb = 0;
    put_note(note, notelen, "");
    toml::table t;
    std::string why;
    if (!load_table(store_path, t, why)) {
        put_note(note, notelen, why);
        return -1;
    }
    const toml::table *game = t["titles"][title_id].as_table();
    if (!game) {
        return 0;
    }
    std::string notes;
    for (const char *k : reserved_keys) {
        if (game->contains(k)) {
            notes += std::string(notes.empty() ? "" : "; ") + k + " is not built yet (ignored)";
        }
    }
    if (auto v = (*game)["ram_mb"].value<int64_t>()) {
        if (valid_ram(*v)) {
            *ram_mb = (int)*v;
        } else {
            notes += std::string(notes.empty() ? "" : "; ") + "ram_mb must be 64 or 128 (ignored)";
        }
    }
    if (!notes.empty()) {
        put_note(note, notelen, std::string("console-upgrades: ") + title_id + ": " + notes);
    }
    return 0;
}

extern "C" int xemu_mp_store_set_ram(const char *store_path, const char *title_id,
                                     const char *name, const char *xbe_sha256,
                                     int ram_mb, char *err, size_t errlen)
{
    put_note(err, errlen, "");
    if (ram_mb != 0 && !valid_ram(ram_mb)) {
        put_note(err, errlen, "ram_mb must be 0, 64 or 128");
        return -1;
    }
    toml::table t;
    std::string why;
    if (!load_table(store_path, t, why)) {
        put_note(err, errlen, why);
        return -1;
    }
    if (!t["titles"].as_table()) {
        t.insert_or_assign("titles", toml::table{});
    }
    toml::table &titles = *t["titles"].as_table();
    if (ram_mb == 0) {
        if (toml::table *g = titles[title_id].as_table()) {
            g->erase("ram_mb");
            bool only_info = true;
            for (auto &&[k, v] : *g) {
                (void)v;
                if (k.str() != "name" && k.str() != "xbe_sha256") {
                    only_info = false;
                }
            }
            if (only_info) {
                titles.erase(title_id);
            }
        }
    } else {
        if (!titles[title_id].as_table()) {
            titles.insert_or_assign(title_id, toml::table{});
        }
        toml::table &g = *titles[title_id].as_table();
        if (name && name[0]) {
            g.insert_or_assign("name", std::string(name));
        }
        if (xbe_sha256 && xbe_sha256[0]) {
            g.insert_or_assign("xbe_sha256", std::string(xbe_sha256));
        }
        g.insert_or_assign("ram_mb", (int64_t)ram_mb);
    }

    std::string tmp = std::string(store_path) + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            put_note(err, errlen, std::string("cannot write ") + tmp);
            return -1;
        }
        f << "# PenguinBox per-game console upgrades, written by the emulator\n"
             "# (Machine > System > This game). One table per game, keyed by the XBE\n"
             "# certificate title id. A game with no table here uses the global settings.\n"
             "#   ram_mb      64 or 128: the console's system memory for this game\n"
             "#   name, xbe_sha256   the disc seen when the choice was made (information)\n"
             "# Reserved, not built yet: cpu, disk, xbe_clear_devkit_memory_limit.\n\n"
          << t << "\n";
        if (!f) {
            put_note(err, errlen, std::string("cannot write ") + tmp);
            return -1;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, store_path, ec);
    if (ec) {
        put_note(err, errlen, std::string("cannot replace ") + store_path + ": " + ec.message());
        return -1;
    }
    return 0;
}
