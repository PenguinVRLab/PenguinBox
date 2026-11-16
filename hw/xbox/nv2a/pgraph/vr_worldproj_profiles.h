/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VR_WORLDPROJ_PROFILES_H
#define HW_XBOX_NV2A_PGRAPH_VR_WORLDPROJ_PROFILES_H

#include <stdbool.h>
#include <stdint.h>

typedef struct VrWpWatch {
    const char *name;
    const uint32_t *words;
    int n_words;
    uint32_t offset;
    uint32_t equals;
} VrWpWatch;

typedef struct VrWpProfile {
    uint32_t title_id;
    const char *name;
    int group;
    double scope_a;
    int cyup;
    double upm;
    int palette;
    int arms_bones;
    double arms_near;
    bool layer2d;
    int ortho;
    double hud_m;
    double hudscale;
    bool hud_head;
    double sref;
    int hide_bone;
    double seat[3];
    const char *requires_mod;
    const VrWpWatch *requires_view;
} VrWpProfile;

extern const VrWpProfile vr_wp_profiles[];
extern const int vr_wp_profile_count;

const VrWpProfile *vr_wp_profile_find(uint32_t title_id);

#endif
