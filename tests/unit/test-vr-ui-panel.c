/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <stdio.h>

#include "../../hw/xbox/nv2a/pgraph/vk/vr_ui_panel.h"

static int s_fail_count, s_check_failed;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "    %s:%d: CHECK(%s) failed\n", __FILE__,     \
                    __LINE__, #cond);                                      \
            s_check_failed = 1;                                            \
        }                                                                  \
    } while (0)
#define RUN(fn)                                                            \
    do {                                                                   \
        s_check_failed = 0;                                                \
        fn();                                                              \
        printf("%s %s\n", s_check_failed ? "FAIL" : "ok  ", #fn);          \
        s_fail_count += s_check_failed;                                    \
        n_run++;                                                           \
    } while (0)
#define NEAR(a, b) (fabsf((a) - (b)) < 1e-4f)

static void yaw_q(float yaw, float q[4])
{
    q[0] = 0.f;
    q[1] = sinf(yaw * 0.5f);
    q[2] = 0.f;
    q[3] = cosf(yaw * 0.5f);
}

static void place_ahead_of_head(void)
{
    const float hp[3] = { 0.1f, 1.6f, 0.2f };
    float q[4];
    yaw_q(0.f, q);
    VrUiPanelPose p = vr_ui_panel_place(hp, q);
    CHECK(NEAR(p.pos[0], 0.1f));
    CHECK(NEAR(p.pos[1], 1.6f - VR_UI_PANEL_DROP_M));
    CHECK(NEAR(p.pos[2], 0.2f - VR_UI_PANEL_DIST_M));
    CHECK(NEAR(p.yaw, 0.f));
}

static void place_follows_yaw(void)
{
    const float hp[3] = { 0.f, 1.5f, 0.f };
    float q[4];
    yaw_q(1.5707963f, q);
    VrUiPanelPose p = vr_ui_panel_place(hp, q);
    CHECK(NEAR(p.pos[0], -VR_UI_PANEL_DIST_M));
    CHECK(NEAR(p.pos[2], 0.f));
    CHECK(NEAR(p.yaw, 1.5707963f));
}

static void place_ignores_pitch(void)
{
    const float hp[3] = { 0.f, 1.5f, 0.f };
    const float q[4] = { sinf(-0.2617994f), 0.f, 0.f, cosf(-0.2617994f) };
    VrUiPanelPose p = vr_ui_panel_place(hp, q);
    CHECK(NEAR(p.yaw, 0.f));
    CHECK(NEAR(p.pos[1], 1.5f - VR_UI_PANEL_DROP_M));
}

static void ray_from_head_hits_centre(void)
{
    const float hp[3] = { 0.f, 1.5f, 0.f };
    float q[4], u = -1, v = -1;
    yaw_q(0.7f, q);
    VrUiPanelPose p = vr_ui_panel_place(hp, q);
    const float o[3] = { 0.f, p.pos[1], 0.f };
    const float d[3] = { -sinf(0.7f), 0.f, -cosf(0.7f) };
    CHECK(vr_ui_panel_ray_hit(&p, o, d, &u, &v));
    CHECK(NEAR(u, 0.5f) && NEAR(v, 0.5f));
}

static void ray_hit_axes(void)
{
    const float hp[3] = { 0.f, 0.f, 0.f };
    float q[4], u, v;
    yaw_q(0.f, q);
    VrUiPanelPose p = vr_ui_panel_place(hp, q);
    const float d[3] = { 0.f, 0.f, -1.f };
    const float o_right_up[3] = { 0.25f, p.pos[1] + 0.1f, 0.f };
    CHECK(vr_ui_panel_ray_hit(&p, o_right_up, d, &u, &v));
    CHECK(NEAR(u, 0.75f));
    CHECK(NEAR(v, 0.5f - 0.1f / VR_UI_PANEL_HEIGHT_M));
    const float o_off[3] = { 0.6f, p.pos[1], 0.f };
    CHECK(!vr_ui_panel_ray_hit(&p, o_off, d, &u, &v));
}

static void ray_from_behind_or_away_misses(void)
{
    const float hp[3] = { 0.f, 0.f, 0.f };
    float q[4], u, v;
    yaw_q(0.f, q);
    VrUiPanelPose p = vr_ui_panel_place(hp, q);
    const float away[3] = { 0.f, 0.f, 1.f };
    const float o[3] = { 0.f, p.pos[1], 0.f };
    CHECK(!vr_ui_panel_ray_hit(&p, o, away, &u, &v));
    const float behind[3] = { 0.f, p.pos[1], -3.f };
    const float back[3] = { 0.f, 0.f, 1.f };
    CHECK(!vr_ui_panel_ray_hit(&p, behind, back, &u, &v));
    const float fwd[3] = { 0.f, 0.f, -1.f };
    CHECK(!vr_ui_panel_ray_hit(&p, behind, fwd, &u, &v));
}

static void comeback_after_one_second_away(void)
{
    VrUiComeback cb = { 0 };
    const float away = 50.f * 3.14159265f / 180.f;
    CHECK(!vr_ui_panel_comeback_step(&cb, away, 0.f, 0));
    CHECK(!vr_ui_panel_comeback_step(&cb, away, 0.f, 999));
    CHECK(vr_ui_panel_comeback_step(&cb, away, 0.f, 1000));
}

static void comeback_not_within_40_deg_or_when_glancing(void)
{
    VrUiComeback cb = { 0 };
    const float near_ = 35.f * 3.14159265f / 180.f;
    const float away = 60.f * 3.14159265f / 180.f;
    for (int64_t t = 0; t < 5000; t += 100) {
        CHECK(!vr_ui_panel_comeback_step(&cb, near_, 0.f, t));
    }
    CHECK(!vr_ui_panel_comeback_step(&cb, away, 0.f, 6000));
    CHECK(!vr_ui_panel_comeback_step(&cb, 0.f, 0.f, 6500));
    CHECK(!vr_ui_panel_comeback_step(&cb, away, 0.f, 6600));
    CHECK(!vr_ui_panel_comeback_step(&cb, away, 0.f, 7200));
    VrUiComeback cb2 = { 0 };
    const float a = 170.f * 3.14159265f / 180.f;
    CHECK(!vr_ui_panel_comeback_step(&cb2, a, -a, 0));
    CHECK(!vr_ui_panel_comeback_step(&cb2, a, -a, 5000));
}

int main(void)
{
    int n_run = 0;
    RUN(place_ahead_of_head);
    RUN(place_follows_yaw);
    RUN(place_ignores_pitch);
    RUN(ray_from_head_hits_centre);
    RUN(ray_hit_axes);
    RUN(ray_from_behind_or_away_misses);
    RUN(comeback_after_one_second_away);
    RUN(comeback_not_within_40_deg_or_when_glancing);
    printf("%d/%d passed\n", n_run - s_fail_count, n_run);
    return s_fail_count;
}
