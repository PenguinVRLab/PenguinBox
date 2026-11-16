/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VR_MODS_H
#define HW_XBOX_NV2A_PGRAPH_VR_MODS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VR_MOD_FIND_MAX 256

typedef struct VrModWord {
    uint32_t at;
    uint32_t value;
    uint32_t original;
    bool has_original;
} VrModWord;

typedef struct VrModGuard {
    uint32_t at;
    uint32_t equals;
    uint8_t width;
} VrModGuard;

typedef struct VrModFind {
    uint8_t bytes[VR_MOD_FIND_MAX];
    uint8_t mask[VR_MOD_FIND_MAX];
    int len;
    uint32_t lo, hi;
    int align;
} VrModFind;

typedef struct VrMod {
    char *name;
    char *slug;
    char *version;
    char *source;
    uint32_t title_id;
    uint32_t *xbe_versions;
    int n_xbe_versions;
    bool enabled;
    bool has_find;
    VrModFind find;
    VrModGuard *guards;
    int n_guards;
    VrModWord *words;
    int n_words;
} VrMod;

void vr_mods_set_catalogue(VrMod *mods, int n);

void vr_mods_free(VrMod *m);

void vr_mods_frame(uint8_t *ram, uint64_t ram_size, int64_t title_id,
                   int64_t xbe_version);

void vr_mods_forget(void);

bool vr_mods_armed(const char *slug);

int vr_mods_load(const char *user_dir, const char *shipped_dir, VrMod **out);

int vr_mods_parse_file(const char *path, const char *text, size_t len,
                       VrMod *out, char *why, size_t why_len);

#ifdef __cplusplus
}
#endif

#endif
