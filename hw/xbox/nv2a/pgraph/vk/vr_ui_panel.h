/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_VR_UI_PANEL_H
#define HW_XBOX_NV2A_PGRAPH_VK_VR_UI_PANEL_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VR_UI_PANEL_W 1280
#define VR_UI_PANEL_H 720

#define VR_UI_PANEL_DIST_M      1.2f
#define VR_UI_PANEL_WIDTH_M     1.0f
#define VR_UI_PANEL_HEIGHT_M    0.5625f
#define VR_UI_PANEL_DROP_M      0.05f
#define VR_UI_PANEL_COMEBACK_DEG 40.0f
#define VR_UI_PANEL_COMEBACK_MS  1000

typedef struct VrUiPanelPose {
    float pos[3];
    float yaw;
} VrUiPanelPose;

static inline float vr_ui_panel_yaw_of(const float q[4])
{
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float fx = -(2.f * (x * z + w * y));
    const float fz = -(1.f - 2.f * (x * x + y * y));
#ifdef VR_UI_PANEL_PLANTED_NO_YAW
    (void)fx;
    (void)fz;
    return 0.f;
#else
    return atan2f(-fx, -fz);
#endif
}

static inline VrUiPanelPose vr_ui_panel_place(const float head_pos[3],
                                              const float head_q[4])
{
    VrUiPanelPose p;
    p.yaw = vr_ui_panel_yaw_of(head_q);
    p.pos[0] = head_pos[0] - sinf(p.yaw) * VR_UI_PANEL_DIST_M;
    p.pos[1] = head_pos[1] - VR_UI_PANEL_DROP_M;
    p.pos[2] = head_pos[2] - cosf(p.yaw) * VR_UI_PANEL_DIST_M;
    return p;
}

static inline void vr_ui_panel_orientation(const VrUiPanelPose *p, float q[4])
{
    q[0] = 0.f;
    q[1] = sinf(p->yaw * 0.5f);
    q[2] = 0.f;
    q[3] = cosf(p->yaw * 0.5f);
}

static inline bool vr_ui_panel_ray_hit(const VrUiPanelPose *p,
                                       const float o[3], const float d[3],
                                       float *u, float *v)
{
    const float n[3] = { sinf(p->yaw), 0.f, cosf(p->yaw) };
    const float right[3] = { cosf(p->yaw), 0.f, -sinf(p->yaw) };
    const float denom = n[0] * d[0] + n[1] * d[1] + n[2] * d[2];
    if (!(denom < -1e-6f)) {
        return false;
    }
    const float t = (n[0] * (p->pos[0] - o[0]) + n[1] * (p->pos[1] - o[1]) +
                     n[2] * (p->pos[2] - o[2])) / denom;
    if (!(t > 0.f)) {
        return false;
    }
    const float rel[3] = { o[0] + d[0] * t - p->pos[0],
                           o[1] + d[1] * t - p->pos[1],
                           o[2] + d[2] * t - p->pos[2] };
    const float lx = rel[0] * right[0] + rel[1] * right[1] + rel[2] * right[2];
    const float ly = rel[1];
    const float uu = lx / VR_UI_PANEL_WIDTH_M + 0.5f;
    const float vv = 0.5f - ly / VR_UI_PANEL_HEIGHT_M;
    if (uu < 0.f || uu > 1.f || vv < 0.f || vv > 1.f) {
        return false;
    }
    *u = uu;
    *v = vv;
    return true;
}

typedef struct VrUiComeback {
    bool away;
    int64_t away_since_ms;
} VrUiComeback;

static inline bool vr_ui_panel_comeback_step(VrUiComeback *cb, float head_yaw,
                                             float panel_yaw, int64_t now_ms)
{
    float dy = head_yaw - panel_yaw;
    while (dy > 3.14159265f) {
        dy -= 6.28318531f;
    }
    while (dy < -3.14159265f) {
        dy += 6.28318531f;
    }
    if (fabsf(dy) * (180.f / 3.14159265f) <= VR_UI_PANEL_COMEBACK_DEG) {
        cb->away = false;
        return false;
    }
    if (!cb->away) {
        cb->away = true;
        cb->away_since_ms = now_ms;
        return false;
    }
    if (now_ms - cb->away_since_ms >= VR_UI_PANEL_COMEBACK_MS) {
        cb->away = false;
        return true;
    }
    return false;
}

#define VR_UI_PAUSE_TICK_MIN_MS 4

typedef struct VrUiPauseTickIn {
    bool guest_running;
    bool guest_stopped;
    bool session_running;
    bool panel_shown;
    bool sync_runs;
    bool tick_pending;
    int64_t now_ms;
    int64_t last_post_ms;
} VrUiPauseTickIn;

static inline bool vr_ui_pause_tick_settled_stop(bool paused, bool debug,
                                                 bool prelaunch)
{
#ifdef VR_UI_PAUSE_TICK_PLANTED_NO_PRELAUNCH
    (void)prelaunch;
    return paused || debug;
#else
    return paused || debug || prelaunch;
#endif
}

static inline bool vr_ui_pause_tick_state_ok(const VrUiPauseTickIn *in)
{
#ifdef VR_UI_PAUSE_TICK_PLANTED_NEVER
    (void)in;
    return false;
#elif defined(VR_UI_PAUSE_TICK_PLANTED_WHILE_RUNNING)
    return in->session_running && in->panel_shown;
#elif defined(VR_UI_PAUSE_TICK_PLANTED_NO_PANEL_GATE)
    return !in->guest_running && in->guest_stopped && in->session_running;
#else
    return !in->guest_running && in->guest_stopped && in->session_running &&
           in->panel_shown;
#endif
}

static inline bool vr_ui_pause_tick_post(const VrUiPauseTickIn *in)
{
    if (!vr_ui_pause_tick_state_ok(in)) {
        return false;
    }
#ifndef VR_UI_PAUSE_TICK_PLANTED_IGNORES_SYNC
    if (in->sync_runs) {
        return false;
    }
#endif
    if (in->tick_pending) {
        return false;
    }
#ifndef VR_UI_PAUSE_TICK_PLANTED_NO_RATE
    if (in->last_post_ms >= 0 &&
        in->now_ms - in->last_post_ms < VR_UI_PAUSE_TICK_MIN_MS) {
        return false;
    }
#endif
    return true;
}

static inline bool vr_ui_pause_tick_run(const VrUiPauseTickIn *in)
{
#ifdef VR_UI_PAUSE_TICK_PLANTED_RUN_NO_RECHECK
    (void)in;
    return true;
#else
    return vr_ui_pause_tick_state_ok(in) && !in->sync_runs;
#endif
}

typedef struct VrUiPanelInput {
    bool pointer_valid;
    int hand;
    float u, v;
    bool select;
    bool back;
    float nav_x, nav_y;
    uint32_t seq;
} VrUiPanelInput;

bool vr_ui_panel_wanted(void);
void vr_ui_panel_publish(const uint8_t *rgba, uint32_t w, uint32_t h);
void vr_ui_panel_set_open(bool open);
uint32_t vr_ui_panel_take_events(void);
bool vr_ui_panel_take_input(VrUiPanelInput *out);

void vr_ui_panel_post_events(uint32_t events);
void vr_ui_panel_post_input(const VrUiPanelInput *in);
bool vr_ui_panel_pose(VrUiPanelPose *out);

bool vr_ui_panel_owns_input(void);

struct VRCompositor;
struct PGRAPHState;
struct XrCompositionLayerQuad;
void vr_ui_panel_frame(struct VRCompositor *c, struct PGRAPHState *pg,
                       int64_t now_ms);
bool vr_ui_panel_layer(struct VRCompositor *c,
                       struct XrCompositionLayerQuad *quad);
void vr_ui_panel_destroy(struct VRCompositor *c);
uint32_t vr_ui_panel_uploads(void);

bool xemu_vr_pause_tick(struct PGRAPHState *pg);
bool vr_compositor_pause_frame(struct PGRAPHState *pg);

#ifdef __cplusplus
}
#endif

#endif
