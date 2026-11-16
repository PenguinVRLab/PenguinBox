/*
 * Copyright (C) 2026 Patrick Carey
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef XEMU_MACHINE_PROFILE_H
#define XEMU_MACHINE_PROFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "xemu-disc-xbe.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum XemuMpModKind {
    XEMU_MP_MOD_NONE = 0,
    XEMU_MP_MOD_RECOMMENDS = 1,
    XEMU_MP_MOD_REQUIRES = 2,
} XemuMpModKind;

typedef struct XemuMpModMachine {
    XemuMpModKind kind;
    int ram_mb;
    bool clear_devkit_limit;
    char mod_name[96];
    char variant[96];
    char file[256];
} XemuMpModMachine;

typedef struct XemuMachineProfileState {
    bool disc_known;
    char disc_error[160];
    XemuDiscXbeInfo xbe;
    char title_id[9];
    int user_ram_mb;
    int global_ram_mb;
    int effective_ram_mb;
    bool from_profile;
    XemuMpModMachine mod;
    char store_note[160];
} XemuMachineProfileState;

int xemu_mp_store_get_ram(const char *store_path, const char *title_id,
                          int *ram_mb, char *note, size_t notelen);

int xemu_mp_store_set_ram(const char *store_path, const char *title_id,
                          const char *name, const char *xbe_sha256,
                          int ram_mb, char *err, size_t errlen);

int xemu_mp_mods_find(const char *mods_dir, const char *title_id,
                      const char *xbe_sha256, XemuMpModMachine *out);

void xemu_machine_profile_note_argv(int argc, char **argv);

int xemu_machine_profile_launch_ram_mb(const char *dvd_path, int global_mb);

void xemu_machine_profile_start(void);

int xemu_machine_profile_running_ram_mb(void);

void xemu_machine_profile_evaluate(const char *dvd_path,
                                   XemuMachineProfileState *out);

int xemu_machine_profile_set_choice(const XemuMachineProfileState *st,
                                    int ram_mb, char *err, size_t errlen);

int xemu_machine_profile_restart(char *err, size_t errlen);

const char *xemu_machine_profile_store_path(void);
const char *xemu_machine_profile_mods_dir(void);

#ifdef __cplusplus
}
#endif

#endif
