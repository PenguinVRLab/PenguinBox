/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include <stddef.h>
#include "vr_worldproj_profiles.h"

static const uint32_t pgr2_camera_header[] = {
    3, 0, 1, 0, 4, 1, 0, 4, 1, 0, 4, 1, 0, 4, 1, 0, 0, 0, 0,
};
static const VrWpWatch pgr2_cockpit_view = {
    "pgr2-cockpit-view", pgr2_camera_header,
    (int)(sizeof(pgr2_camera_header) / sizeof(pgr2_camera_header[0])),
    0xA0, 0,
};

const VrWpProfile vr_wp_profiles[] = {
    { 0x4D530004u, "Halo: Combat Evolved", .group = 0, .scope_a = 2.0,
      .cyup = -1, .upm = 1.0 / 3.048, .palette = 60, .arms_bones = 8,
      .arms_near = 0.04,
      .layer2d = false, .ortho = 28, .hud_m = 2.0, .hudscale = 0.8,
      .hud_head = true },
    { 0x53430005u, "Richard Burns Rally", .group = 96, .scope_a = 100.0,
      .cyup = -1, .upm = 1.0, .palette = 0, .arms_bones = 0, .arms_near = 0.04,
      .layer2d = false, .ortho = 0, .hud_m = 2.0, .hudscale = 0.8,
      .hud_head = true },
    { 0x4D53004Bu, "Project Gotham Racing 2", .group = 114, .scope_a = 100.0,
      .cyup = -1, .upm = 1.0, .palette = 0, .arms_bones = 0, .arms_near = 0.04,
      .layer2d = false, .ortho = 0, .hud_m = 2.0, .hudscale = 0.8,
      .hud_head = true, .sref = 5592.41, .hide_bone = 164,
      .seat = { -0.30, 0.0, 0.20 }, .requires_mod = "pgr2-cockpit",
      .requires_view = &pgr2_cockpit_view },
#ifdef VR_WP_PROFILES_EXTRA
#include VR_WP_PROFILES_EXTRA
#endif
};

const int vr_wp_profile_count =
    (int)(sizeof(vr_wp_profiles) / sizeof(vr_wp_profiles[0]));

const VrWpProfile *vr_wp_profile_find(uint32_t title_id)
{
    for (int i = 0; i < vr_wp_profile_count; i++) {
        if (vr_wp_profiles[i].title_id == title_id) {
            return &vr_wp_profiles[i];
        }
    }
    return NULL;
}
