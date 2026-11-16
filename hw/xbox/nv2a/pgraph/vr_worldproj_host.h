/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VR_WORLDPROJ_HOST_H
#define HW_XBOX_NV2A_PGRAPH_VR_WORLDPROJ_HOST_H

#include <stdbool.h>
#include <stdint.h>

bool vr_wp_host_view_projection(void);

void vr_wp_host_title_request(bool forget);

int64_t vr_wp_host_title_id(void);

int64_t vr_wp_host_title_version(void);

bool vr_wp_host_mod_armed(const char *slug);

#endif
