/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_VR_XR_EYES_H
#define HW_XBOX_NV2A_PGRAPH_VK_VR_XR_EYES_H

#include <stdbool.h>
#include <stdint.h>

#include "openxr/openxr.h"


typedef struct VrEyesPlan {
    uint32_t chains;
    uint32_t src_layer[2];
} VrEyesPlan;

static inline VrEyesPlan vr_eyes_plan(bool stereo, int fault)
{
    VrEyesPlan p = { stereo ? 2u : 1u, { 0u, 1u } };
    (void)fault;
    return p;
}

static inline XrEyeVisibility vr_eyes_visibility(bool stereo, uint32_t k)
{
    if (!stereo) {
        return XR_EYE_VISIBILITY_BOTH;
    }
    return k == 0 ? XR_EYE_VISIBILITY_LEFT : XR_EYE_VISIBILITY_RIGHT;
}

static inline uint32_t vr_eyes_quads(bool stereo,
                                     const XrCompositionLayerQuad *tmpl,
                                     const XrSwapchain chains[2],
                                     XrCompositionLayerQuad out[2])
{
    uint32_t n = stereo ? 2u : 1u;
    for (uint32_t k = 0; k < n; k++) {
        out[k] = *tmpl;
        out[k].eyeVisibility = vr_eyes_visibility(stereo, k);
        out[k].subImage.swapchain = chains[k];
    }
    return n;
}

static inline uint32_t
vr_eyes_cylinders(bool stereo, const XrCompositionLayerCylinderKHR *tmpl,
                  const XrSwapchain chains[2],
                  XrCompositionLayerCylinderKHR out[2])
{
    uint32_t n = stereo ? 2u : 1u;
    for (uint32_t k = 0; k < n; k++) {
        out[k] = *tmpl;
        out[k].eyeVisibility = vr_eyes_visibility(stereo, k);
        out[k].subImage.swapchain = chains[k];
    }
    return n;
}

static inline void
vr_eyes_proj_views(bool stereo, const XrCompositionLayerProjectionView *tmpl,
                   const XrSwapchain chains[2],
                   XrCompositionLayerProjectionView out[2])
{
    for (uint32_t e = 0; e < 2; e++) {
        out[e] = *tmpl;
        out[e].subImage.swapchain = chains[stereo ? e : 0u];
    }
}

#endif
