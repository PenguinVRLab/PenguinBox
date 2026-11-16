/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_VR_NEARFIELD_MATH_H
#define HW_XBOX_NV2A_PGRAPH_VK_VR_NEARFIELD_MATH_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "openxr/openxr.h"

typedef enum VrNfSabotage {
    VR_NF_SAB_NONE = 0,
    VR_NF_SAB_SYMFOV,
    VR_NF_SAB_NOCLAMP,
} VrNfSabotage;

#define VR_NF_SCALE_MIN 0.25f
#define VR_NF_EXTENT_MIN 16u

static inline uint32_t vr_nf_scaled_extent(uint32_t full, float scale,
                                           VrNfSabotage sab)
{
    if (!(scale > 0.0f) || !isfinite(scale)) {
        scale = 1.0f;
    }
    if (sab != VR_NF_SAB_NOCLAMP) {
        scale = scale < VR_NF_SCALE_MIN ? VR_NF_SCALE_MIN :
                scale > 1.0f            ? 1.0f :
                                          scale;
    }
    uint32_t n = (uint32_t)((float)full * scale + 0.5f);
    if (n < VR_NF_EXTENT_MIN) {
        n = VR_NF_EXTENT_MIN;
    }
    return n < full ? n : full;
}

static inline void vr_nf_mat_mul(const float a[16], const float b[16],
                                 float out[16])
{
    float r[16];
    for (int c = 0; c < 4; c++) {
        for (int rr = 0; rr < 4; rr++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++) {
                s += a[k * 4 + rr] * b[c * 4 + k];
            }
            r[c * 4 + rr] = s;
        }
    }
    for (int i = 0; i < 16; i++) {
        out[i] = r[i];
    }
}

static inline void vr_nf_mat_apply(const float m[16], const float v[3],
                                   float out[3], float *w)
{
    float r[4];
    for (int i = 0; i < 4; i++) {
        r[i] = m[0 * 4 + i] * v[0] + m[1 * 4 + i] * v[1] +
               m[2 * 4 + i] * v[2] + m[3 * 4 + i];
    }
    out[0] = r[0];
    out[1] = r[1];
    out[2] = r[2];
    *w = r[3];
}

static inline XrVector3f vr_nf_quat_rotate(XrQuaternionf q, XrVector3f v)
{
    const float tx = 2.0f * (q.y * v.z - q.z * v.y);
    const float ty = 2.0f * (q.z * v.x - q.x * v.z);
    const float tz = 2.0f * (q.x * v.y - q.y * v.x);
    return (XrVector3f){
        v.x + q.w * tx + (q.y * tz - q.z * ty),
        v.y + q.w * ty + (q.z * tx - q.x * tz),
        v.z + q.w * tz + (q.x * ty - q.y * tx),
    };
}

static inline void vr_nf_view_matrix(const XrPosef *eye, float out[16])
{
    const XrQuaternionf q = eye->orientation;
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    float r[3][3] = {
        { 1.0f - 2.0f * (yy + zz), 2.0f * (xy - wz), 2.0f * (xz + wy) },
        { 2.0f * (xy + wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz - wx) },
        { 2.0f * (xz - wy), 2.0f * (yz + wx), 1.0f - 2.0f * (xx + yy) },
    };
    for (int c = 0; c < 3; c++) {
        for (int rr = 0; rr < 3; rr++) {
            out[c * 4 + rr] = r[c][rr];
        }
        out[c * 4 + 3] = 0.0f;
    }
    const float t[3] = { eye->position.x, eye->position.y, eye->position.z };
    for (int rr = 0; rr < 3; rr++) {
        out[12 + rr] = -(r[0][rr] * t[0] + r[1][rr] * t[1] + r[2][rr] * t[2]);
    }
    out[15] = 1.0f;
}

static inline void vr_nf_projection(const XrFovf *fov, float near_z,
                                    float far_z, VrNfSabotage sab,
                                    float out[16])
{
    float tl = tanf(fov->angleLeft), tr = tanf(fov->angleRight);
    float tu = tanf(fov->angleUp), td = tanf(fov->angleDown);
    if (sab == VR_NF_SAB_SYMFOV) {
        tl = -tr;
        td = -tu;
    }
    const float tw = tr - tl;
    const float th = td - tu;
    for (int i = 0; i < 16; i++) {
        out[i] = 0.0f;
    }
    out[0] = 2.0f / tw;
    out[8] = (tr + tl) / tw;
    out[5] = 2.0f / th;
    out[9] = (tu + td) / th;
    out[10] = -far_z / (far_z - near_z);
    out[14] = -(far_z * near_z) / (far_z - near_z);
    out[11] = -1.0f;
}

#endif
