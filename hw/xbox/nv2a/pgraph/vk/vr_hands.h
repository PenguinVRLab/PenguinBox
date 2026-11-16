/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_VR_HANDS_H
#define HW_XBOX_NV2A_PGRAPH_VK_VR_HANDS_H

#include <stdbool.h>
#include <stdint.h>

#include "vr_xr_loader.h"
#include "vr_hands_geom.h"

bool vr_hands_init(void);

void vr_hands_destroy(void);

bool vr_hands_ready(void);

void vr_hands_locate(XrTime time, VrHandJoints out[2], int64_t *cost_us);

#endif
