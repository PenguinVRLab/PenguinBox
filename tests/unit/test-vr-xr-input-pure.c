/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../../hw/xbox/nv2a/pgraph/vk/vr_xr_input_pure.h"

static int g_fail, g_n;
#define CHECK(c) do { if (!(c)) { printf("# %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define RUN(t) do { int f0 = g_fail; t(); printf("%sok %d - %s\n", g_fail == f0 ? "" : "not ", ++g_n, #t); } while (0)

typedef struct H { bool aim, poke, sel, chg, rep; } H;
static void gate(H *h, bool owns)
{
    vr_input_gate_hand(owns, &h->aim, &h->poke, &h->sel, &h->chg, &h->rep);
}

static void ui_closed_passes_everything(void)
{
    H h = { true, true, true, true, false };
    gate(&h, false);
    CHECK(h.aim && h.poke && h.sel && h.chg && h.rep);
}

static void ui_owns_gates_aim_and_select(void)
{
    H h = { true, true, true, false, false };
    gate(&h, false);
    gate(&h, true);
    CHECK(!h.aim && !h.sel);
    CHECK(h.chg);
    gate(&h, true);
    CHECK(!h.chg);
}

static void ui_owns_gates_poke(void)
{
    H h = { true, true, false, false, false };
    gate(&h, true);
    CHECK(!h.poke);
}

static bool has(const PadxrBinding *l, size_t n, PadxrActionId a, int hand,
                const char *sub)
{
    for (size_t i = 0; i < n; i++) {
        if ((int)l[i].action == (int)a && l[i].hand == hand &&
            strcmp(l[i].subpath, sub) == 0) {
            return true;
        }
    }
    return false;
}

static PadxrKind kind_of(PadxrActionId a)
{
    return (int)a == (int)VR_INPUT_POKE_ACTION ? PADXR_KIND_POSE :
                                                padxr_action(a)->kind;
}

static size_t combined(PadxrBinding *out)
{
    const PadxrBinding *core, *px;
    size_t nc = vr_input_hands_core_bindings(&core);
    size_t np = vr_input_hands_padxr_bindings(true, &px);
    return padxr_merge_suggestions(core, nc, px, np, out, PADXR_MAX_BINDINGS);
}

static void hands_one_suggestion_has_core_and_padxr(void)
{
    PadxrBinding m[PADXR_MAX_BINDINGS];
    size_t n = combined(m);
    for (int h = 0; h < 2; h++) {
        CHECK(has(m, n, PADXR_CORE_HAND_AIM, h, "/input/aim/pose"));
        CHECK(has(m, n, PADXR_CORE_PANEL_SELECT, h, "/input/pinch_ext/value"));
        CHECK(has(m, n, VR_INPUT_POKE_ACTION, h, "/input/poke_ext/pose"));
        CHECK(has(m, n, PADXR_MENU_SELECT, h, "/input/pinch_ext/value"));
        CHECK(!has(m, n, PADXR_SYSTEM, h, "/input/pinch_ext/value"));
    }
    CHECK(n == 8);
}

static void hands_core_first_in_order(void)
{
    PadxrBinding m[PADXR_MAX_BINDINGS];
    const PadxrBinding *core;
    size_t nc = vr_input_hands_core_bindings(&core);
    size_t n = combined(m);
    CHECK(nc == 6 && n >= nc);
    for (size_t i = 0; i < nc && i < n; i++) {
        CHECK(m[i].action == core[i].action && m[i].hand == core[i].hand &&
              strcmp(m[i].subpath, core[i].subpath) == 0);
    }
}

static void hands_paths_allowed(void)
{
    PadxrBinding m[PADXR_MAX_BINDINGS];
    size_t n = combined(m);
    for (size_t i = 0; i < n; i++) {
        bool ok = padxr_path_allowed(PADXR_PROFILE_HANDS, m[i].hand,
                                     m[i].subpath, kind_of(m[i].action));
        if (!ok) {
            printf("# not allowed: %s\n", m[i].subpath);
        }
        CHECK(ok);
    }
}

static void hands_without_padxr_set(void)
{
    const PadxrBinding *px;
    CHECK(vr_input_hands_padxr_bindings(false, &px) == 0);
}

static const XrQuaternionf k_palm_up = { -0.70710678f, 0.f, 0.f, 0.70710678f };
static const XrSpaceLocationFlags k_valid =
    XR_SPACE_LOCATION_POSITION_VALID_BIT |
    XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;

static VrHandJoints located(float gap)
{
    VrHandJoints h;
    memset(&h, 0, sizeof(h));
    h.active = true;
    for (int k = 0; k < VR_HANDS_JOINTS; k++) {
        h.j[k].valid = h.j[k].tracked = true;
        h.j[k].flags = k_valid;
        h.j[k].pose.orientation = k_palm_up;
        h.j[k].pose.position = (XrVector3f){ 0.001f * k, 0.f, -0.3f };
        h.j[k].radius = 0.01f;
    }
    h.j[XR_HAND_JOINT_WRIST_EXT].pose.position = (XrVector3f){ 0, 0, -0.3f };
    h.j[XR_HAND_JOINT_THUMB_TIP_EXT].pose.position =
        (XrVector3f){ 0, 0, -0.3f };
    h.j[XR_HAND_JOINT_INDEX_TIP_EXT].pose.position =
        (XrVector3f){ gap, 0, -0.3f };
    return h;
}

static void joints_inactive_read_not_tracked(void)
{
    VrHandJoints h = located(0.05f);
    h.active = false;
    PadxrHandJoints j = vr_input_joints_from(&h);
    CHECK(!j.tracked && !j.wrist.valid && !j.tips_valid);
}

static void joints_held_read_not_tracked(void)
{
    VrHandJoints h = located(0.05f);
    h.held = true;
    PadxrHandJoints j = vr_input_joints_from(&h);
    CHECK(!j.tracked);
}

static void joints_invalid_tip_is_not_valid(void)
{
    VrHandJoints h = located(0.05f);
    h.j[XR_HAND_JOINT_THUMB_TIP_EXT].valid = false;
    PadxrHandJoints j = vr_input_joints_from(&h);
    CHECK(j.tracked && j.wrist.valid && !j.tips_valid);
    h = located(0.05f);
    h.j[XR_HAND_JOINT_WRIST_EXT].valid = false;
    j = vr_input_joints_from(&h);
    CHECK(j.tracked && !j.wrist.valid && j.tips_valid);
}

static void joints_pick_wrist_thumb_index(void)
{
    VrHandJoints h = located(0.05f);
    for (int k = 0; k < VR_HANDS_JOINTS; k++) {
        h.j[k].pose.position = (XrVector3f){ (float)k, 2.f * k, 3.f * k };
        h.j[k].pose.orientation = (XrQuaternionf){ 0.1f * k, 0, 0, 1 };
    }
    PadxrHandJoints j = vr_input_joints_from(&h);
    const int w = XR_HAND_JOINT_WRIST_EXT, t = XR_HAND_JOINT_THUMB_TIP_EXT,
              i = XR_HAND_JOINT_INDEX_TIP_EXT;
    CHECK(j.wrist.position[0] == w && j.wrist.position[1] == 2.f * w &&
          j.wrist.position[2] == 3.f * w);
    CHECK(j.wrist.orientation[0] == 0.1f * w && j.wrist.orientation[3] == 1);
    CHECK(j.thumb_tip[0] == t && j.thumb_tip[1] == 2.f * t &&
          j.thumb_tip[2] == 3.f * t);
    CHECK(j.index_tip[0] == i && j.index_tip[1] == 2.f * i &&
          j.index_tip[2] == 3.f * i);
}

static void joints_feed_one_pinch_press(void)
{
    PadxrPinchParams pp = padxr_pinch_default_params();
    PadxrPinchPose head = { { 0, 0, 0 }, { 0, 0, 0, 1 }, true };
    const VrHandJoints open = located(0.05f), shut = located(0.01f);
    for (int ctrl = 0; ctrl < 2; ctrl++) {
        PadxrPinchDetector d;
        memset(&d, 0, sizeof(d));
        int presses = 0;
        PadxrHandJoints j = vr_input_joints_from(&open);
        presses += padxr_pinch_step(&d, &j, &head, ctrl, &pp).pressed;
        j = vr_input_joints_from(&shut);
        PadxrPinchOutput o = padxr_pinch_step(&d, &j, &head, ctrl, &pp);
        presses += o.pressed;
        presses += padxr_pinch_step(&d, &j, &head, ctrl, &pp).pressed;
        CHECK(presses == (ctrl ? 0 : 1));
        if (!ctrl) {
            CHECK(o.facing && o.held);
        }
    }
}

static void joints_taken_once_per_feed(void)
{
    PadxrHandJoints slot, j;
    bool fed = false;
    const VrHandJoints h = located(0.05f);
    j = vr_input_joints_take(&slot, &fed);
    CHECK(!j.tracked);
    slot = vr_input_joints_from(&h);
    fed = true;
    j = vr_input_joints_take(&slot, &fed);
    CHECK(j.tracked && !fed);
    j = vr_input_joints_take(&slot, &fed);
    CHECK(!j.tracked);
}

int main(void)
{
    RUN(ui_closed_passes_everything);
    RUN(ui_owns_gates_aim_and_select);
    RUN(ui_owns_gates_poke);
    RUN(hands_one_suggestion_has_core_and_padxr);
    RUN(hands_core_first_in_order);
    RUN(hands_paths_allowed);
    RUN(hands_without_padxr_set);
    RUN(joints_inactive_read_not_tracked);
    RUN(joints_held_read_not_tracked);
    RUN(joints_invalid_tip_is_not_valid);
    RUN(joints_pick_wrist_thumb_index);
    RUN(joints_feed_one_pinch_press);
    RUN(joints_taken_once_per_feed);
    printf("1..%d\n", g_n);
    return g_fail ? 1 : 0;
}
