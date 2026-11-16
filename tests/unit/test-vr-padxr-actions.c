/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../../hw/xbox/nv2a/pgraph/vk/vr_padxr_actions.h"

static int s_fail_count;
static int s_check_failed;

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

static bool same(const PadxrBinding *a, const PadxrBinding *b)
{
    return a->action == b->action && a->hand == b->hand &&
           strcmp(a->subpath, b->subpath) == 0;
}

static void core_prefix_unchanged(void)
{
    static const struct { PadxrProfile p; size_t core, merged; } want[] = {
        { PADXR_PROFILE_SIMPLE, 4, 8 },  { PADXR_PROFILE_TOUCH, 4, 11 },
        { PADXR_PROFILE_INDEX, 4, 11 },  { PADXR_PROFILE_VIVE, 4, 10 },
        { PADXR_PROFILE_FRAME, 4, 11 },
    };
    for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
        const PadxrBinding *core, *px;
        PadxrBinding out[PADXR_MAX_BINDINGS];
        size_t nc = padxr_core_bindings(want[i].p, &core);
        size_t np = padxr_default_bindings(want[i].p, &px);
        size_t n = padxr_merge_suggestions(core, nc, px, np, out,
                                           PADXR_MAX_BINDINGS);
        CHECK(nc == want[i].core);
        CHECK(n == want[i].merged);
        for (size_t k = 0; k < nc && k < n; k++) {
            CHECK(same(&out[k], &core[k]));
        }
        for (size_t k = 0; k < np && nc + k < n; k++) {
            CHECK(same(&out[nc + k], &px[k]));
        }
    }
}

static void core_tables_match_the_old_loop(void)
{
    static const struct { PadxrProfile p; const char *sel; } want[] = {
        { PADXR_PROFILE_SIMPLE, "/input/select/click" },
        { PADXR_PROFILE_TOUCH, "/input/trigger/value" },
        { PADXR_PROFILE_INDEX, "/input/trigger/click" },
        { PADXR_PROFILE_VIVE, "/input/trigger/click" },
    };
    for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
        const PadxrBinding *core;
        size_t nc = padxr_core_bindings(want[i].p, &core);
        CHECK(nc == 4);
        if (nc != 4) {
            continue;
        }
        for (int h = 0; h < 2; h++) {
            PadxrBinding aim = { PADXR_CORE_HAND_AIM, (uint8_t)h,
                                 "/input/aim/pose" };
            PadxrBinding sel = { PADXR_CORE_PANEL_SELECT, (uint8_t)h,
                                 want[i].sel };
            CHECK(same(&core[2 * h], &aim));
            CHECK(same(&core[2 * h + 1], &sel));
        }
    }
    const PadxrBinding *none;
    CHECK(padxr_core_bindings(PADXR_PROFILE_HANDS, &none) == 0);
}

static void every_default_path_is_allowed(void)
{
    for (int p = 0; p < PADXR_PROFILE_COUNT; p++) {
        const PadxrBinding *list[2];
        size_t n[2];
        n[0] = padxr_core_bindings((PadxrProfile)p, &list[0]);
        n[1] = padxr_default_bindings((PadxrProfile)p, &list[1]);
        for (int s = 0; s < 2; s++) {
            for (size_t k = 0; k < n[s]; k++) {
                const PadxrBinding *b = &list[s][k];
                bool ok = padxr_path_allowed((PadxrProfile)p, b->hand,
                                             b->subpath,
                                             padxr_action(b->action)->kind);
                if (!ok) {
                    fprintf(stderr, "    not allowed: %s hand %d %s\n",
                            padxr_profile_path((PadxrProfile)p), b->hand,
                            b->subpath);
                }
                CHECK(ok);
            }
        }
    }
}

static void path_allowed_controls(void)
{
    CHECK(padxr_path_allowed(PADXR_PROFILE_TOUCH, PADXR_HAND_LEFT,
                             "/input/menu/click", PADXR_KIND_BOOL));
    CHECK(!padxr_path_allowed(PADXR_PROFILE_TOUCH, PADXR_HAND_RIGHT,
                              "/input/menu/click", PADXR_KIND_BOOL));
    CHECK(!padxr_path_allowed(PADXR_PROFILE_TOUCH, PADXR_HAND_RIGHT,
                              "/input/system/click", PADXR_KIND_BOOL));
    CHECK(!padxr_path_allowed(PADXR_PROFILE_INDEX, PADXR_HAND_LEFT,
                              "/input/system/click", PADXR_KIND_BOOL));
    CHECK(!padxr_path_allowed(PADXR_PROFILE_TOUCH, PADXR_HAND_LEFT,
                              "/input/menu/clik", PADXR_KIND_BOOL));
    CHECK(!padxr_path_allowed(PADXR_PROFILE_TOUCH, PADXR_HAND_LEFT,
                              "/input/thumbstick/click", PADXR_KIND_VEC2));
    CHECK(padxr_path_allowed(PADXR_PROFILE_INDEX, PADXR_HAND_LEFT,
                             "/input/trackpad/force", PADXR_KIND_BOOL));
}

typedef struct FakeRt {
    int calls;
    size_t last_n;
    bool refuse_any_padxr;
} FakeRt;

static bool fake_submit(void *ctx, PadxrProfile p, const PadxrBinding *list,
                        size_t n)
{
    FakeRt *rt = ctx;
    rt->calls++;
    for (size_t k = 0; k < n; k++) {
        if (rt->refuse_any_padxr && padxr_action(list[k].action)->padxr_set) {
            return false;
        }
        if (!padxr_path_allowed(p, list[k].hand, list[k].subpath,
                                padxr_action(list[k].action)->kind)) {
            return false;
        }
    }
    rt->last_n = n;
    return true;
}

static void merged_touch_is_accepted(void)
{
    const PadxrBinding *core, *px;
    size_t nc = padxr_core_bindings(PADXR_PROFILE_TOUCH, &core);
    size_t np = padxr_default_bindings(PADXR_PROFILE_TOUCH, &px);
    FakeRt rt = { 0 };
    PadxrSuggestResult r = padxr_suggest_with_retry(PADXR_PROFILE_TOUCH, core,
                                                    nc, px, np, fake_submit,
                                                    &rt);
    CHECK(r.outcome == PADXR_SUGGEST_MERGED);
    CHECK(r.count == 11);
    CHECK(rt.calls == 1);
    CHECK(r.log[0] == '\0');
}

static void refused_merge_keeps_core_bindings(void)
{
    const PadxrBinding *core, *px;
    size_t nc = padxr_core_bindings(PADXR_PROFILE_TOUCH, &core);
    size_t np = padxr_default_bindings(PADXR_PROFILE_TOUCH, &px);
    FakeRt rt = { .refuse_any_padxr = true };
    PadxrSuggestResult r = padxr_suggest_with_retry(PADXR_PROFILE_TOUCH, core,
                                                    nc, px, np, fake_submit,
                                                    &rt);
    CHECK(r.outcome == PADXR_SUGGEST_CORE_ONLY);
    CHECK(r.count == 4);
    CHECK(rt.calls == 2);
    CHECK(rt.last_n == 4);
    CHECK(strcmp(r.log, "PadXR: bindings refused for "
                        "/interaction_profiles/oculus/touch_controller; "
                        "core bindings kept") == 0);
}

static void profiles_to_suggest_unchanged(void)
{
    PadxrProfile out[8];
    size_t n = padxr_profiles_to_suggest(false, out, 8);
    CHECK(n == 4);
    CHECK(n == 4 && out[0] == PADXR_PROFILE_SIMPLE &&
          out[1] == PADXR_PROFILE_TOUCH && out[2] == PADXR_PROFILE_INDEX &&
          out[3] == PADXR_PROFILE_VIVE);
    n = padxr_profiles_to_suggest(true, out, 8);
    CHECK(n == 5 && out[4] == PADXR_PROFILE_FRAME);
    for (size_t i = 0; i < n; i++) {
        CHECK(out[i] != PADXR_PROFILE_HANDS);
    }
}

static void talk_is_unbound_and_system_reserved(void)
{
    for (int p = 0; p < PADXR_PROFILE_COUNT; p++) {
        const PadxrBinding *px;
        size_t np = padxr_default_bindings((PadxrProfile)p, &px);
        for (size_t k = 0; k < np; k++) {
            CHECK(px[k].action != PADXR_TALK);
            CHECK(padxr_action(px[k].action)->padxr_set);
        }
    }
    CHECK(padxr_action(PADXR_SYSTEM)->reserved);
    CHECK(strcmp(padxr_action(PADXR_SYSTEM)->name, "system") == 0);
    CHECK(padxr_find_action("menu_navigate") == PADXR_MENU_NAVIGATE);
    CHECK(padxr_find_action("hand_aim") == PADXR_CORE_HAND_AIM);
    CHECK(!padxr_action(PADXR_CORE_PANEL_SELECT)->padxr_set);
    CHECK(padxr_find_profile("/interaction_profiles/valve/index_controller") ==
          PADXR_PROFILE_INDEX);
}

static uint32_t press(PadxrEventMachine *m, int64_t *t, int64_t ms,
                      bool ui_open)
{
    uint32_t ev = 0;
    for (int64_t e = 0; e <= ms; e += 10) {
        ev |= padxr_event_step(m, true, true, ui_open, *t);
        *t += 10;
    }
    ev |= padxr_event_step(m, false, true, ui_open, *t);
    *t += 10;
    return ev;
}

static void armed(PadxrEventMachine *m, int64_t *t)
{
    padxr_event_reset(m);
    padxr_event_step(m, false, true, false, *t);
    *t += 10;
}

static void short_press_opens(void)
{
    PadxrEventMachine m;
    int64_t t = 1000;
    armed(&m, &t);
    CHECK(press(&m, &t, 200, false) == PADXR_EVENT_MENU_OPEN);
}

static void short_press_with_ui_open_closes(void)
{
    PadxrEventMachine m;
    int64_t t = 1000;
    armed(&m, &t);
    CHECK(press(&m, &t, 200, true) == PADXR_EVENT_MENU_CLOSE);
}

static void long_press_recentres_once_and_does_not_open(void)
{
    PadxrEventMachine m;
    int64_t t = 1000;
    int fired = 0;
    int64_t fired_at = -1;
    armed(&m, &t);
    int64_t t0 = t;
    for (int64_t e = 0; e <= 2000; e += 10) {
        uint32_t ev = padxr_event_step(&m, true, true, false, t);
        if (ev & PADXR_EVENT_RECENTRE) {
            fired++;
            fired_at = t - t0;
        }
        CHECK(!(ev & (PADXR_EVENT_MENU_OPEN | PADXR_EVENT_MENU_CLOSE)));
        t += 10;
    }
    CHECK(fired == 1);
    CHECK(fired_at == PADXR_RECENTRE_HOLD_MS);
    CHECK(padxr_event_step(&m, false, true, false, t) == PADXR_EVENT_NONE);
}

static void long_hold_with_menu_open_recentres_menu_stays(void)
{
    PadxrEventMachine m;
    int64_t t = 1000;
    armed(&m, &t);
    CHECK(press(&m, &t, 1200, true) == PADXR_EVENT_RECENTRE);
}

static void sparse_poll_long_release_recentres(void)
{
    PadxrEventMachine m;
    int64_t t = 1000;
    armed(&m, &t);
    CHECK(padxr_event_step(&m, true, true, false, t) == PADXR_EVENT_NONE);
    CHECK(padxr_event_step(&m, false, true, false, t + 900) ==
          PADXR_EVENT_RECENTRE);
}

static void input_loss_drops_press_and_waits_for_release(void)
{
    PadxrEventMachine m;
    int64_t t = 1000;
    armed(&m, &t);
    CHECK(padxr_event_step(&m, true, true, false, t) == PADXR_EVENT_NONE);
    CHECK(padxr_event_step(&m, true, false, false, t + 100) ==
          PADXR_EVENT_NONE);
    CHECK(padxr_event_step(&m, true, true, false, t + 200) ==
          PADXR_EVENT_NONE);
    CHECK(padxr_event_step(&m, true, true, false, t + 1500) ==
          PADXR_EVENT_NONE);
    CHECK(padxr_event_step(&m, false, true, false, t + 1600) ==
          PADXR_EVENT_NONE);
    t += 2000;
    CHECK(press(&m, &t, 100, false) == PADXR_EVENT_MENU_OPEN);
}

static void first_tick_held_never_fires(void)
{
    PadxrEventMachine m = { 0 };
    uint32_t ev = 0;
    for (int64_t t = 0; t < 2000; t += 10) {
        ev |= padxr_event_step(&m, true, true, false, t);
    }
    ev |= padxr_event_step(&m, false, true, false, 2000);
    CHECK(ev == PADXR_EVENT_NONE);
}

static void profile_latch_logs_changes_only(void)
{
    PadxrProfileLatch l = { 0 };
    char line[300];
    CHECK(padxr_profile_changed(&l, NULL, "", line, sizeof(line)));
    CHECK(strcmp(line, "xemu-vr: input: profile left=none right=none") == 0);
    CHECK(!padxr_profile_changed(&l, "", NULL, line, sizeof(line)));
    CHECK(padxr_profile_changed(
        &l, "/interaction_profiles/oculus/touch_controller", NULL, line,
        sizeof(line)));
    CHECK(strcmp(line, "xemu-vr: input: profile "
                       "left=/interaction_profiles/oculus/touch_controller "
                       "right=none") == 0);
    CHECK(!padxr_profile_changed(
        &l, "/interaction_profiles/oculus/touch_controller", "", line,
        sizeof(line)));
}

static void quat_from_to(const float a[3], const float b[3], float q[4])
{
    float c[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
                   a[0] * b[1] - a[1] * b[0] };
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    float w = 1.f + d;
    float n = sqrtf(c[0] * c[0] + c[1] * c[1] + c[2] * c[2] + w * w);
    q[0] = c[0] / n;
    q[1] = c[1] / n;
    q[2] = c[2] / n;
    q[3] = w / n;
}

static PadxrPinchPose head_at_origin(void)
{
    PadxrPinchPose h = { .orientation = { 0, 0, 0, 1 }, .valid = true };
    return h;
}

static PadxrHandJoints hand(bool facing, float dist_m)
{
    PadxrHandJoints j = { 0 };
    j.wrist.position[1] = -0.2f;
    j.wrist.position[2] = -0.4f;
    j.wrist.valid = true;
    j.wrist.orientation[3] = 1.f;
    if (facing) {
        const float minus_y[3] = { 0, -1, 0 };
        float to_head[3] = { 0, 0.2f, 0.4f };
        float n = sqrtf(0.2f * 0.2f + 0.4f * 0.4f);
        to_head[1] /= n;
        to_head[2] /= n;
        quat_from_to(minus_y, to_head, j.wrist.orientation);
    }
    j.thumb_tip[0] = 0.05f;
    j.thumb_tip[1] = -0.15f;
    j.thumb_tip[2] = -0.45f;
    memcpy(j.index_tip, j.thumb_tip, sizeof(j.index_tip));
    j.index_tip[0] += dist_m;
    j.tips_valid = true;
    j.tracked = true;
    return j;
}

static void pinch_facing_controls(void)
{
    PadxrPinchParams p = padxr_pinch_default_params();
    PadxrPinchPose h = head_at_origin();
    PadxrHandJoints yes = hand(true, 0.05f), no = hand(false, 0.05f);
    CHECK(padxr_pinch_is_facing(&yes, &h, &p));
    CHECK(!padxr_pinch_is_facing(&no, &h, &p));
    PadxrPinchPose away = h;
    const float mz[3] = { 0, 0, -1 }, mx[3] = { -1, 0, 0 };
    quat_from_to(mz, mx, away.orientation);
    CHECK(!padxr_pinch_is_facing(&yes, &away, &p));
}

static void facing_pinch_presses_once(void)
{
    PadxrPinchParams p = padxr_pinch_default_params();
    PadxrPinchPose h = head_at_origin();
    PadxrPinchDetector d = { 0 };
    PadxrHandJoints open = hand(true, 0.06f), shut = hand(true, 0.01f);
    int presses = 0;
    CHECK(!padxr_pinch_step(&d, &open, &h, false, &p).held);
    for (int i = 0; i < 50; i++) {
        PadxrPinchOutput o = padxr_pinch_step(&d, &shut, &h, false, &p);
        presses += o.pressed;
        CHECK(o.held);
    }
    CHECK(presses == 1);
    CHECK(!padxr_pinch_step(&d, &open, &h, false, &p).held);
}

static void pinch_while_not_facing_never_presses(void)
{
    PadxrPinchParams p = padxr_pinch_default_params();
    PadxrPinchPose h = head_at_origin();
    PadxrPinchDetector d = { 0 };
    PadxrHandJoints open = hand(false, 0.06f), shut = hand(false, 0.01f),
                    shut_facing = hand(true, 0.01f);
    padxr_pinch_step(&d, &open, &h, false, &p);
    CHECK(!padxr_pinch_step(&d, &shut, &h, false, &p).pressed);
    for (int i = 0; i < 10; i++) {
        PadxrPinchOutput o = padxr_pinch_step(&d, &shut_facing, &h, false, &p);
        CHECK(!o.pressed && !o.held);
    }
}

static void pinch_jitter_is_one_press(void)
{
    PadxrPinchParams p = padxr_pinch_default_params();
    PadxrPinchPose h = head_at_origin();
    PadxrPinchDetector d = { 0 };
    PadxrHandJoints open = hand(true, 0.06f);
    int presses = 0;
    padxr_pinch_step(&d, &open, &h, false, &p);
    for (int i = 0; i < 40; i++) {
        PadxrHandJoints j = hand(true, (i & 1) ? 0.019f : 0.026f);
        presses += padxr_pinch_step(&d, &j, &h, false, &p).pressed;
    }
    CHECK(presses == 1);
}

static void pinch_never_fires_while_holding_controller(void)
{
    PadxrPinchParams p = padxr_pinch_default_params();
    PadxrPinchPose h = head_at_origin();
    PadxrPinchDetector d = { 0 };
    PadxrHandJoints open = hand(true, 0.06f), shut = hand(true, 0.01f);
    for (int k = 0; k < 5; k++) {
        CHECK(!padxr_pinch_step(&d, &open, &h, true, &p).pressed);
        PadxrPinchOutput o = padxr_pinch_step(&d, &shut, &h, true, &p);
        CHECK(!o.pressed && !o.held);
    }
    CHECK(!padxr_pinch_step(&d, &shut, &h, false, &p).pressed);
    CHECK(!padxr_pinch_step(&d, &open, &h, false, &p).pressed);
    CHECK(padxr_pinch_step(&d, &shut, &h, false, &p).pressed);
}

static void pinch_tracking_loss_releases_and_rearms(void)
{
    PadxrPinchParams p = padxr_pinch_default_params();
    PadxrPinchPose h = head_at_origin();
    PadxrPinchDetector d = { 0 };
    PadxrHandJoints open = hand(true, 0.06f), shut = hand(true, 0.01f),
                    lost = shut;
    lost.tracked = false;
    padxr_pinch_step(&d, &open, &h, false, &p);
    CHECK(padxr_pinch_step(&d, &shut, &h, false, &p).held);
    CHECK(!padxr_pinch_step(&d, &lost, &h, false, &p).held);
    CHECK(!padxr_pinch_step(&d, &shut, &h, false, &p).pressed);
    padxr_pinch_step(&d, &open, &h, false, &p);
    CHECK(padxr_pinch_step(&d, &shut, &h, false, &p).pressed);
}

static void pinch_drives_the_same_events(void)
{
    PadxrPinchParams p = padxr_pinch_default_params();
    PadxrPinchPose h = head_at_origin();
    PadxrPinchDetector d = { 0 };
    PadxrEventMachine m;
    PadxrHandJoints open = hand(true, 0.06f), shut = hand(true, 0.01f);
    int64_t t = 0;
    uint32_t ev = 0;
    armed(&m, &t);
    padxr_pinch_step(&d, &open, &h, false, &p);
    for (int i = 0; i < 20; i++, t += 10) {
        ev |= padxr_event_step(
            &m, padxr_pinch_step(&d, &shut, &h, false, &p).held, true, false,
            t);
    }
    ev |= padxr_event_step(
        &m, padxr_pinch_step(&d, &open, &h, false, &p).held, true, false, t);
    t += 10;
    CHECK(ev == PADXR_EVENT_MENU_OPEN);
    ev = 0;
    for (int i = 0; i < 100; i++, t += 10) {
        ev |= padxr_event_step(
            &m, padxr_pinch_step(&d, &shut, &h, false, &p).held, true, false,
            t);
    }
    ev |= padxr_event_step(
        &m, padxr_pinch_step(&d, &open, &h, false, &p).held, true, false, t);
    CHECK(ev == PADXR_EVENT_RECENTRE);
}

int main(void)
{
    int n_run = 0;
    RUN(core_prefix_unchanged);
    RUN(core_tables_match_the_old_loop);
    RUN(every_default_path_is_allowed);
    RUN(path_allowed_controls);
    RUN(merged_touch_is_accepted);
    RUN(refused_merge_keeps_core_bindings);
    RUN(profiles_to_suggest_unchanged);
    RUN(talk_is_unbound_and_system_reserved);
    RUN(short_press_opens);
    RUN(short_press_with_ui_open_closes);
    RUN(long_press_recentres_once_and_does_not_open);
    RUN(long_hold_with_menu_open_recentres_menu_stays);
    RUN(sparse_poll_long_release_recentres);
    RUN(input_loss_drops_press_and_waits_for_release);
    RUN(first_tick_held_never_fires);
    RUN(profile_latch_logs_changes_only);
    RUN(pinch_facing_controls);
    RUN(facing_pinch_presses_once);
    RUN(pinch_while_not_facing_never_presses);
    RUN(pinch_jitter_is_one_press);
    RUN(pinch_never_fires_while_holding_controller);
    RUN(pinch_tracking_loss_releases_and_rearms);
    RUN(pinch_drives_the_same_events);
    printf("%d/%d passed\n", n_run - s_fail_count, n_run);
    return s_fail_count;
}
