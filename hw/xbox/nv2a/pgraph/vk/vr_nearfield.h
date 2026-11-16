/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_VR_NEARFIELD_H
#define HW_XBOX_NV2A_PGRAPH_VK_VR_NEARFIELD_H

#include <stdbool.h>
#include <stdint.h>

#include "hw/xbox/nv2a/pgraph/pgraph.h"
#include "vr_xr_loader.h"

void vr_nearfield_start(void);

void vr_nearfield_stop(void);

void vr_nearfield_submitted(uint32_t layer_count);

bool vr_nearfield_wanted(void);

bool vr_nearfield_frame(PGRAPHState *pg, XrTime display_time, bool should_render,
                        XrCompositionLayerProjection *layer,
                        XrCompositionLayerProjectionView views[2]);

#endif
