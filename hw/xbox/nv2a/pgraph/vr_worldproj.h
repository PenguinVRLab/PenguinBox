/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VR_WORLDPROJ_H
#define HW_XBOX_NV2A_PGRAPH_VR_WORLDPROJ_H

#include "vr_worldproj_math.h"
#include "vr_worldproj_stamp.h"

typedef struct PGRAPHState PGRAPHState;

void vr_worldproj_set_view(const VrWpView *view, bool on);

bool vr_worldproj_active(void);

typedef struct VrWpHmdPose {
    int64_t display_time;
    double q[4], p[3];
    double ref_q[4], ref_p[3];
    double tan[4];
} VrWpHmdPose;

typedef bool (*VrWpPoseSource)(bool fresh, VrWpHmdPose *out);

void vr_worldproj_set_pose_source(VrWpPoseSource src);

void vr_worldproj_set_slave(bool on, double body_yaw, double body_pitch);

bool vr_worldproj_script_head(double *yaw_left, double *pitch_up);

void vr_worldproj_flip(PGRAPHState *pg, uint64_t surface_key);

void vr_worldproj_present(uint64_t key);

uint64_t vr_worldproj_flip_key(void);
uint64_t vr_worldproj_last_present(void);

void vr_worldproj_invalidate(void);

VrWpStampSource vr_worldproj_presented(VrWpStamp *out);

bool vr_worldproj_end_skips(PGRAPHState *pg, bool vertex_program);

void vr_worldproj_draw_extent(PGRAPHState *pg, const float *xyzw,
                              unsigned int n);

void vr_worldproj_draw_target(uint64_t key, bool writes_colour);

void vr_worldproj_draw_relative(bool relative);

void vr_worldproj_report_clear(void);
bool vr_worldproj_report_take(double *scale, double *cap);

void vr_worldproj_apply(bool fixed_function, float c[][4]);

#define VR_WORLDPROJ_MAX_ROWS 11
int vr_worldproj_renderer_rows(int *rows);

#endif
