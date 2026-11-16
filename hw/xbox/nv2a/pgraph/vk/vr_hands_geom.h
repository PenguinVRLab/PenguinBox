/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_VR_HANDS_GEOM_H
#define HW_XBOX_NV2A_PGRAPH_VK_VR_HANDS_GEOM_H

#include <stdbool.h>
#include <stdint.h>

#include "openxr/openxr.h"

#define VR_HANDS_JOINTS 26
#define VR_HANDS_BONES  24
#define VR_HANDS_MAX_CAPSULES (VR_HANDS_BONES + VR_HANDS_JOINTS)

typedef enum VrHandsSabotage {
    VR_HANDS_SAB_NONE = 0,
    VR_HANDS_SAB_CROSS,
    VR_HANDS_SAB_INVALID,
    VR_HANDS_SAB_RADIUS,
    VR_HANDS_SAB_FLIP,
    VR_HANDS_SAB_WIND,
    VR_HANDS_SAB_ALWAYS,
    VR_HANDS_SAB_UNTRACKED,
    VR_HANDS_SAB_HOLDFOREVER,
} VrHandsSabotage;

typedef struct VrHandJoint {
    bool valid;
    bool tracked;
    uint64_t flags;
    XrPosef pose;
    float radius;
} VrHandJoint;

typedef struct VrHandJoints {
    bool active;
    bool held;
    VrHandJoint j[VR_HANDS_JOINTS];
} VrHandJoints;

typedef struct VrHandsFilterCfg {
    bool require_tracked;
    int64_t hold_ns;
} VrHandsFilterCfg;

typedef struct VrHandsHold {
    bool have;
    XrTime t_good;
    VrHandJoints last;
} VrHandsHold;

bool vr_hands_filter(VrHandJoints *h, VrHandsHold *st, XrTime t,
                     const VrHandsFilterCfg *cfg, VrHandsSabotage sab);

typedef struct VrCapsule {
    XrVector3f a, b;
    float ra, rb;
} VrCapsule;

typedef struct VrHandBone {
    uint8_t from, to;
} VrHandBone;

const VrHandBone *vr_hands_bones(VrHandsSabotage sab);

uint32_t vr_hands_capsules(const VrHandJoints *h, VrHandsSabotage sab,
                           VrCapsule *out);

bool vr_hands_layer_needed(const VrHandJoints h[2], VrHandsSabotage sab);

typedef struct VrNfVertex {
    float p[3];
    float n[3];
} VrNfVertex;

typedef struct VrNfMesh {
    VrNfVertex *v;
    uint32_t *i;
    uint32_t nv, ni;
    uint32_t vcap, icap;
    bool overflow;
} VrNfMesh;

#define VR_HANDS_SEG   12
#define VR_HANDS_RINGS  6

uint32_t vr_hands_capsule_vertices(const VrCapsule *c);
uint32_t vr_hands_capsule_indices(const VrCapsule *c);

void vr_hands_mesh_capsule(VrNfMesh *m, const VrCapsule *c,
                           VrHandsSabotage sab);

uint32_t vr_hands_mesh(VrNfMesh *m, const VrHandJoints *h,
                       VrHandsSabotage sab);

#endif
