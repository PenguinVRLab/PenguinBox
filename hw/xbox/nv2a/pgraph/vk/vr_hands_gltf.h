/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_VR_HANDS_GLTF_H
#define HW_XBOX_NV2A_PGRAPH_VK_VR_HANDS_GLTF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "openxr/openxr.h"
#include "vr_hands_geom.h"

#define VR_GLTF_MAX_TRIS  10000u
#define VR_GLTF_MAX_VERTS 16384u

typedef enum VrGltfSabotage {
    VR_GLTF_SAB_NONE = 0,
    VR_GLTF_SAB_JOINTMAP,
    VR_GLTF_SAB_NOIBM,
    VR_GLTF_SAB_ONEWEIGHT,
    VR_GLTF_SAB_NONORMAL,
    VR_GLTF_SAB_LAXWEIGHTS,
    VR_GLTF_SAB_MISSING,
    VR_GLTF_SAB_LAXJOINTS,
    VR_GLTF_SAB_PARENTREL,
} VrGltfSabotage;

typedef struct VrNfVertexC {
    float p[3];
    float n[3];
    float c[3];
} VrNfVertexC;

typedef struct VrGltfHand {
    uint32_t nv, ni;
    float *pos;
    float *nrm;
    float *col;
    uint8_t *jnt;
    float *wgt;
    uint32_t *idx;
    float ibm[VR_HANDS_JOINTS][16];
    bool weighted[VR_HANDS_JOINTS];
    XrHandEXT side;
    float wrist_to_middle_tip_m;
} VrGltfHand;

const char *vr_hands_gltf_joint_name(int joint);

bool vr_hands_gltf_load(const void *glb, size_t size, XrHandEXT side,
                        VrGltfSabotage sab, VrGltfHand *out, char *why,
                        size_t why_len);

void vr_hands_gltf_free(VrGltfHand *h);

bool vr_hands_gltf_drawable(const VrGltfHand *g, const VrHandJoints *h,
                            VrGltfSabotage sab);

uint32_t vr_hands_gltf_skin(const VrGltfHand *g, const VrHandJoints *h,
                            VrGltfSabotage sab, VrNfVertexC *out);

void vr_hands_gltf_joint_matrix(const VrGltfHand *g, const XrPosef *pose,
                                int joint, VrGltfSabotage sab, float m[12]);

#endif
