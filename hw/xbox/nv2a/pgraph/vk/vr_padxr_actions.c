/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include "vr_padxr_actions.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

static const PadxrActionDef s_actions[PADXR_ACTION_COUNT] = {
    [PADXR_CORE_HAND_AIM] = { "hand_aim", "Hand aim", "play.aimPose",
                              PADXR_KIND_POSE, false, false },
    [PADXR_CORE_PANEL_SELECT] = { "panel_select", "Panel select",
                                  "play.select", PADXR_KIND_BOOL, false,
                                  false },
    [PADXR_SYSTEM] = { "system", "PadXR menu (hold: recentre)", "system",
                       PADXR_KIND_BOOL, true, true },
    [PADXR_MENU_SELECT] = { "menu_select", "Menu select", "menu.select",
                            PADXR_KIND_BOOL, true, false },
    [PADXR_MENU_BACK] = { "menu_back", "Menu back", "menu.back",
                          PADXR_KIND_BOOL, true, false },
    [PADXR_MENU_NAVIGATE] = { "menu_navigate", "Menu navigate",
                              "menu.navigate", PADXR_KIND_VEC2, true, false },
    [PADXR_TALK] = { "talk", "Push to talk", "talk", PADXR_KIND_BOOL, true,
                     false },
};

const PadxrActionDef *padxr_action(PadxrActionId id)
{
    if ((int)id < 0 || id >= PADXR_ACTION_COUNT) {
        return NULL;
    }
    return &s_actions[id];
}

PadxrActionId padxr_find_action(const char *name)
{
    for (int i = 0; i < PADXR_ACTION_COUNT; i++) {
        if (name && strcmp(s_actions[i].name, name) == 0) {
            return (PadxrActionId)i;
        }
    }
    return PADXR_ACTION_COUNT;
}

static const char *const s_profile_path[PADXR_PROFILE_COUNT] = {
    [PADXR_PROFILE_SIMPLE] = "/interaction_profiles/khr/simple_controller",
    [PADXR_PROFILE_TOUCH] = "/interaction_profiles/oculus/touch_controller",
    [PADXR_PROFILE_INDEX] = "/interaction_profiles/valve/index_controller",
    [PADXR_PROFILE_VIVE] = "/interaction_profiles/htc/vive_controller",
    [PADXR_PROFILE_HANDS] = "/interaction_profiles/ext/hand_interaction_ext",
    [PADXR_PROFILE_FRAME] =
        "/interaction_profiles/valve/frame_controller_valve",
};

const char *padxr_profile_path(PadxrProfile p)
{
    return ((int)p >= 0 && p < PADXR_PROFILE_COUNT) ? s_profile_path[p] : NULL;
}

PadxrProfile padxr_find_profile(const char *path)
{
    for (int i = 0; i < PADXR_PROFILE_COUNT; i++) {
        if (path && strcmp(s_profile_path[i], path) == 0) {
            return (PadxrProfile)i;
        }
    }
    return PADXR_PROFILE_COUNT;
}

size_t padxr_profiles_to_suggest(bool frame_extension_enabled,
                                 PadxrProfile *out, size_t cap)
{
    static const PadxrProfile base[] = { PADXR_PROFILE_SIMPLE,
                                         PADXR_PROFILE_TOUCH,
                                         PADXR_PROFILE_INDEX,
                                         PADXR_PROFILE_VIVE };
    size_t n = 0;
    for (size_t i = 0; i < ARRAY_LEN(base) && n < cap; i++) {
        out[n++] = base[i];
    }
    if (frame_extension_enabled && n < cap) {
        out[n++] = PADXR_PROFILE_FRAME;
    }
    return n;
}

#define L PADXR_HAND_LEFT
#define R PADXR_HAND_RIGHT
#define CORE(sel)                                                          \
    { PADXR_CORE_HAND_AIM, L, "/input/aim/pose" },                         \
    { PADXR_CORE_PANEL_SELECT, L, sel },                                   \
    { PADXR_CORE_HAND_AIM, R, "/input/aim/pose" },                         \
    { PADXR_CORE_PANEL_SELECT, R, sel }

static const PadxrBinding s_core_simple[] = { CORE("/input/select/click") };
static const PadxrBinding s_core_touch[] = { CORE("/input/trigger/value") };
static const PadxrBinding s_core_index[] = { CORE("/input/trigger/click") };
static const PadxrBinding s_core_vive[] = { CORE("/input/trigger/click") };
static const PadxrBinding s_core_frame[] = { CORE("/input/trigger/value") };

#ifdef PADXR_PLANTED_BAD_PATH
#define TOUCH_SYSTEM "/input/menu/clik"
#else
#define TOUCH_SYSTEM "/input/menu/click"
#endif

static const PadxrBinding s_px_simple[] = {
    { PADXR_SYSTEM, L, "/input/menu/click" },
    { PADXR_SYSTEM, R, "/input/menu/click" },
    { PADXR_MENU_SELECT, L, "/input/select/click" },
    { PADXR_MENU_SELECT, R, "/input/select/click" },
};
static const PadxrBinding s_px_touch[] = {
    { PADXR_SYSTEM, L, TOUCH_SYSTEM },
    { PADXR_MENU_SELECT, L, "/input/trigger/value" },
    { PADXR_MENU_SELECT, R, "/input/trigger/value" },
    { PADXR_MENU_SELECT, R, "/input/a/click" },
    { PADXR_MENU_BACK, R, "/input/b/click" },
    { PADXR_MENU_NAVIGATE, L, "/input/thumbstick" },
    { PADXR_MENU_NAVIGATE, R, "/input/thumbstick" },
};
static const PadxrBinding s_px_index[] = {
    { PADXR_SYSTEM, L, "/input/trackpad/force" },
    { PADXR_MENU_SELECT, L, "/input/trigger/click" },
    { PADXR_MENU_SELECT, R, "/input/trigger/click" },
    { PADXR_MENU_SELECT, R, "/input/a/click" },
    { PADXR_MENU_BACK, R, "/input/b/click" },
    { PADXR_MENU_NAVIGATE, L, "/input/thumbstick" },
    { PADXR_MENU_NAVIGATE, R, "/input/thumbstick" },
};
static const PadxrBinding s_px_vive[] = {
    { PADXR_SYSTEM, L, "/input/menu/click" },
    { PADXR_SYSTEM, R, "/input/menu/click" },
    { PADXR_MENU_SELECT, L, "/input/trigger/click" },
    { PADXR_MENU_SELECT, R, "/input/trigger/click" },
    { PADXR_MENU_NAVIGATE, L, "/input/trackpad" },
    { PADXR_MENU_NAVIGATE, R, "/input/trackpad" },
};
static const PadxrBinding s_px_hands[] = {
    { PADXR_MENU_SELECT, L, "/input/pinch_ext/value" },
    { PADXR_MENU_SELECT, R, "/input/pinch_ext/value" },
};
static const PadxrBinding s_px_frame[] = {
    { PADXR_SYSTEM, L, "/input/view/click" },
    { PADXR_MENU_SELECT, L, "/input/trigger/value" },
    { PADXR_MENU_SELECT, R, "/input/trigger/value" },
    { PADXR_MENU_SELECT, R, "/input/a/click" },
    { PADXR_MENU_BACK, R, "/input/b/click" },
    { PADXR_MENU_NAVIGATE, L, "/input/thumbstick" },
    { PADXR_MENU_NAVIGATE, R, "/input/thumbstick" },
};
#undef CORE

size_t padxr_core_bindings(PadxrProfile p, const PadxrBinding **out)
{
    switch (p) {
    case PADXR_PROFILE_SIMPLE:
        *out = s_core_simple;
        return ARRAY_LEN(s_core_simple);
    case PADXR_PROFILE_TOUCH:
        *out = s_core_touch;
        return ARRAY_LEN(s_core_touch);
    case PADXR_PROFILE_INDEX:
        *out = s_core_index;
        return ARRAY_LEN(s_core_index);
    case PADXR_PROFILE_VIVE:
        *out = s_core_vive;
        return ARRAY_LEN(s_core_vive);
    case PADXR_PROFILE_FRAME:
        *out = s_core_frame;
        return ARRAY_LEN(s_core_frame);
    default:
        *out = NULL;
        return 0;
    }
}

size_t padxr_default_bindings(PadxrProfile p, const PadxrBinding **out)
{
    switch (p) {
    case PADXR_PROFILE_SIMPLE:
        *out = s_px_simple;
        return ARRAY_LEN(s_px_simple);
    case PADXR_PROFILE_TOUCH:
        *out = s_px_touch;
        return ARRAY_LEN(s_px_touch);
    case PADXR_PROFILE_INDEX:
        *out = s_px_index;
        return ARRAY_LEN(s_px_index);
    case PADXR_PROFILE_VIVE:
        *out = s_px_vive;
        return ARRAY_LEN(s_px_vive);
    case PADXR_PROFILE_HANDS:
        *out = s_px_hands;
        return ARRAY_LEN(s_px_hands);
    case PADXR_PROFILE_FRAME:
        *out = s_px_frame;
        return ARRAY_LEN(s_px_frame);
    default:
        *out = NULL;
        return 0;
    }
}

size_t padxr_merge_suggestions(const PadxrBinding *core, size_t ncore,
                               const PadxrBinding *padxr, size_t npadxr,
                               PadxrBinding *out, size_t cap)
{
    size_t n = 0;
#ifdef PADXR_PLANTED_DROP_CORE
    if (ncore > 0) {
        ncore--;
    }
#endif
    for (size_t i = 0; i < ncore && n < cap; i++) {
        out[n++] = core[i];
    }
    for (size_t i = 0; i < npadxr && n < cap; i++) {
        out[n++] = padxr[i];
    }
    return n;
}

PadxrSuggestResult padxr_suggest_with_retry(PadxrProfile p,
                                            const PadxrBinding *core,
                                            size_t ncore,
                                            const PadxrBinding *padxr,
                                            size_t npadxr,
                                            PadxrSubmitFn submit, void *ctx)
{
    PadxrSuggestResult r = { .outcome = PADXR_SUGGEST_REFUSED };
    PadxrBinding merged[PADXR_MAX_BINDINGS];
    size_t n = padxr_merge_suggestions(core, ncore, padxr, npadxr, merged,
                                       ARRAY_LEN(merged));
    if (submit(ctx, p, merged, n)) {
        r.outcome = PADXR_SUGGEST_MERGED;
        r.count = n;
        return r;
    }
    if (npadxr == 0 || ncore == 0) {
        return r;
    }
#ifdef PADXR_PLANTED_NO_RETRY
    return r;
#endif
    snprintf(r.log, sizeof(r.log),
             "PadXR: bindings refused for %s; core bindings kept",
             padxr_profile_path(p) ? padxr_profile_path(p) : "?");
    if (submit(ctx, p, core, ncore)) {
        r.outcome = PADXR_SUGGEST_CORE_ONLY;
        r.count = ncore;
    } else {
        snprintf(r.log, sizeof(r.log),
                 "PadXR: bindings refused for %s; core bindings refused too",
                 padxr_profile_path(p) ? padxr_profile_path(p) : "?");
    }
    return r;
}

typedef struct Comp {
    const char *subpath;
    PadxrKind kind;
    int8_t hand;
} Comp;

#define B PADXR_KIND_BOOL
#define F PADXR_KIND_FLOAT
#define V PADXR_KIND_VEC2
#define P PADXR_KIND_POSE
#define H PADXR_KIND_HAPTIC

static const Comp s_c_simple[] = {
    { "/input/select/click", B, -1 }, { "/input/menu/click", B, -1 },
    { "/input/grip/pose", P, -1 },    { "/input/aim/pose", P, -1 },
    { "/output/haptic", H, -1 },
};
static const Comp s_c_touch[] = {
    { "/input/x/click", B, L },          { "/input/x/touch", B, L },
    { "/input/y/click", B, L },          { "/input/y/touch", B, L },
    { "/input/menu/click", B, L },       { "/input/a/click", B, R },
    { "/input/a/touch", B, R },          { "/input/b/click", B, R },
    { "/input/b/touch", B, R },          { "/input/squeeze/value", F, -1 },
    { "/input/trigger/value", F, -1 },   { "/input/trigger/touch", B, -1 },
    { "/input/thumbstick", V, -1 },      { "/input/thumbstick/x", F, -1 },
    { "/input/thumbstick/y", F, -1 },    { "/input/thumbstick/click", B, -1 },
    { "/input/thumbstick/touch", B, -1 }, { "/input/thumbrest/touch", B, -1 },
    { "/input/grip/pose", P, -1 },       { "/input/aim/pose", P, -1 },
    { "/output/haptic", H, -1 },
};
static const Comp s_c_index[] = {
    { "/input/a/click", B, -1 },         { "/input/a/touch", B, -1 },
    { "/input/b/click", B, -1 },         { "/input/b/touch", B, -1 },
    { "/input/squeeze/value", F, -1 },   { "/input/squeeze/force", F, -1 },
    { "/input/trigger/click", B, -1 },   { "/input/trigger/value", F, -1 },
    { "/input/trigger/touch", B, -1 },   { "/input/thumbstick", V, -1 },
    { "/input/thumbstick/x", F, -1 },    { "/input/thumbstick/y", F, -1 },
    { "/input/thumbstick/click", B, -1 }, { "/input/thumbstick/touch", B, -1 },
    { "/input/trackpad", V, -1 },        { "/input/trackpad/x", F, -1 },
    { "/input/trackpad/y", F, -1 },      { "/input/trackpad/force", F, -1 },
    { "/input/trackpad/touch", B, -1 },  { "/input/grip/pose", P, -1 },
    { "/input/aim/pose", P, -1 },        { "/output/haptic", H, -1 },
};
static const Comp s_c_vive[] = {
    { "/input/squeeze/click", B, -1 },  { "/input/menu/click", B, -1 },
    { "/input/trigger/click", B, -1 },  { "/input/trigger/value", F, -1 },
    { "/input/trackpad", V, -1 },       { "/input/trackpad/x", F, -1 },
    { "/input/trackpad/y", F, -1 },     { "/input/trackpad/click", B, -1 },
    { "/input/trackpad/touch", B, -1 }, { "/input/grip/pose", P, -1 },
    { "/input/aim/pose", P, -1 },       { "/output/haptic", H, -1 },
};
static const Comp s_c_hands[] = {
    { "/input/pinch_ext/pose", P, -1 },
    { "/input/poke_ext/pose", P, -1 },
    { "/input/grip/pose", P, -1 },
    { "/input/aim/pose", P, -1 },
    { "/input/pinch_ext/value", F, -1 },
    { "/input/pinch_ext/ready_ext", B, -1 },
    { "/input/grasp_ext/value", F, -1 },
    { "/input/grasp_ext/ready_ext", B, -1 },
    { "/input/aim_activate_ext/value", F, -1 },
    { "/input/aim_activate_ext/ready_ext", B, -1 },
};
static const Comp s_c_frame[] = {
    { "/input/squeeze/touch", B, -1 },    { "/input/squeeze/click", B, -1 },
    { "/input/squeeze/value", F, -1 },    { "/input/trigger/touch", B, -1 },
    { "/input/trigger/click", B, -1 },    { "/input/trigger/value", F, -1 },
    { "/input/thumbstick/touch", B, -1 }, { "/input/thumbstick/click", B, -1 },
    { "/input/thumbstick/x", F, -1 },     { "/input/thumbstick/y", F, -1 },
    { "/input/thumbstick", V, -1 },       { "/input/grip/pose", P, -1 },
    { "/input/aim/pose", P, -1 },         { "/output/haptic", H, -1 },
    { "/input/dpad_up/click", B, L },     { "/input/dpad_left/click", B, L },
    { "/input/dpad_down/click", B, L },   { "/input/dpad_right/click", B, L },
    { "/input/view/touch", B, L },        { "/input/view/click", B, L },
    { "/input/a/touch", B, R },           { "/input/a/click", B, R },
    { "/input/b/touch", B, R },           { "/input/b/click", B, R },
    { "/input/x/touch", B, R },           { "/input/x/click", B, R },
    { "/input/y/touch", B, R },           { "/input/y/click", B, R },
};
#undef B
#undef F
#undef V
#undef P
#undef H
#undef L
#undef R

static bool kind_converts(PadxrKind component, PadxrKind action)
{
    switch (action) {
    case PADXR_KIND_BOOL:
    case PADXR_KIND_FLOAT:
        return component == PADXR_KIND_BOOL || component == PADXR_KIND_FLOAT;
    default:
        return component == action;
    }
}

bool padxr_path_allowed(PadxrProfile p, int hand, const char *subpath,
                        PadxrKind action_kind)
{
    const Comp *c = NULL;
    size_t n = 0;
    switch (p) {
    case PADXR_PROFILE_SIMPLE: c = s_c_simple; n = ARRAY_LEN(s_c_simple); break;
    case PADXR_PROFILE_TOUCH:  c = s_c_touch;  n = ARRAY_LEN(s_c_touch);  break;
    case PADXR_PROFILE_INDEX:  c = s_c_index;  n = ARRAY_LEN(s_c_index);  break;
    case PADXR_PROFILE_VIVE:   c = s_c_vive;   n = ARRAY_LEN(s_c_vive);   break;
    case PADXR_PROFILE_HANDS:  c = s_c_hands;  n = ARRAY_LEN(s_c_hands);  break;
    case PADXR_PROFILE_FRAME:  c = s_c_frame;  n = ARRAY_LEN(s_c_frame);  break;
    default: return false;
    }
    if (hand < 0 || hand >= PADXR_NUM_HANDS || subpath == NULL) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if ((c[i].hand < 0 || c[i].hand == hand) &&
            strcmp(c[i].subpath, subpath) == 0) {
            return kind_converts(c[i].kind, action_kind);
        }
    }
    return false;
}

const char *padxr_event_name(uint32_t e)
{
    switch (e) {
    case PADXR_EVENT_MENU_OPEN:  return "menu.open";
    case PADXR_EVENT_MENU_CLOSE: return "menu.close";
    case PADXR_EVENT_RECENTRE:   return "recentre";
    default:                     return "none";
    }
}

void padxr_event_reset(PadxrEventMachine *m)
{
    m->phase = PADXR_PHASE_WAIT_RELEASE;
    m->press_ms = 0;
}

uint32_t padxr_event_step(PadxrEventMachine *m, bool system_held,
                          bool input_live, bool ui_open, int64_t now_ms)
{
#ifdef PADXR_PLANTED_ZERO_HOLD
    const int64_t hold_ms = 0;
#else
    const int64_t hold_ms = PADXR_RECENTRE_HOLD_MS;
#endif
    if (!input_live) {
        m->phase = PADXR_PHASE_WAIT_RELEASE;
        return PADXR_EVENT_NONE;
    }
    int64_t elapsed = now_ms > m->press_ms ? now_ms - m->press_ms : 0;

    switch (m->phase) {
    case PADXR_PHASE_WAIT_RELEASE:
        if (!system_held) {
            m->phase = PADXR_PHASE_IDLE;
        }
        return PADXR_EVENT_NONE;
    case PADXR_PHASE_IDLE:
        if (!system_held) {
            return PADXR_EVENT_NONE;
        }
        m->phase = PADXR_PHASE_PRESSED;
        m->press_ms = now_ms;
        if (hold_ms <= 0) {
            m->phase = PADXR_PHASE_LONG_FIRED;
            return PADXR_EVENT_RECENTRE;
        }
        return PADXR_EVENT_NONE;
    case PADXR_PHASE_PRESSED:
        if (!system_held) {
            m->phase = PADXR_PHASE_IDLE;
            if (elapsed >= hold_ms) {
                return PADXR_EVENT_RECENTRE;
            }
            return ui_open ? PADXR_EVENT_MENU_CLOSE : PADXR_EVENT_MENU_OPEN;
        }
        if (elapsed >= hold_ms) {
            m->phase = PADXR_PHASE_LONG_FIRED;
            return PADXR_EVENT_RECENTRE;
        }
        return PADXR_EVENT_NONE;
    case PADXR_PHASE_LONG_FIRED:
        if (!system_held) {
            m->phase = PADXR_PHASE_IDLE;
        }
        return PADXR_EVENT_NONE;
    }
    return PADXR_EVENT_NONE;
}

bool padxr_profile_changed(PadxrProfileLatch *l, const char *left,
                           const char *right, char *line, size_t cap)
{
    const char *lv = (left && *left) ? left : "none";
    const char *rv = (right && *right) ? right : "none";
    if (l->any && strcmp(l->left, lv) == 0 && strcmp(l->right, rv) == 0) {
        return false;
    }
    l->any = true;
    snprintf(l->left, sizeof(l->left), "%s", lv);
    snprintf(l->right, sizeof(l->right), "%s", rv);
    if (line && cap) {
        snprintf(line, cap, "xemu-vr: input: profile left=%s right=%s", lv,
                 rv);
    }
    return true;
}

PadxrPinchParams padxr_pinch_default_params(void)
{
    return (PadxrPinchParams){
        .palm_to_head_max_deg = PADXR_PINCH_PALM_TO_HEAD_MAX_DEG,
        .require_gaze = true,
        .gaze_max_deg = PADXR_PINCH_GAZE_MAX_DEG,
        .pinch_close_m = PADXR_PINCH_CLOSE_M,
        .pinch_open_m = PADXR_PINCH_OPEN_M,
    };
}

static void v3_sub(const float a[3], const float b[3], float o[3])
{
    o[0] = a[0] - b[0];
    o[1] = a[1] - b[1];
    o[2] = a[2] - b[2];
}

static float v3_dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void v3_cross(const float a[3], const float b[3], float o[3])
{
    float t[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
                   a[0] * b[1] - a[1] * b[0] };
    memcpy(o, t, sizeof(t));
}

static void quat_rotate(const float q[4], const float v[3], float o[3])
{
    float t[3], t2[3], c[3];
    v3_cross(q, v, t);
    t2[0] = 2.f * t[0];
    t2[1] = 2.f * t[1];
    t2[2] = 2.f * t[2];
    v3_cross(q, t2, c);
    o[0] = v[0] + q[3] * t2[0] + c[0];
    o[1] = v[1] + q[3] * t2[1] + c[1];
    o[2] = v[2] + q[3] * t2[2] + c[2];
}

static float angle_deg(const float a[3], const float b[3])
{
    float la = sqrtf(v3_dot(a, a)), lb = sqrtf(v3_dot(b, b));
    if (!(la > 1e-6f) || !(lb > 1e-6f)) {
        return 180.f;
    }
    float c = v3_dot(a, b) / (la * lb);
    c = c < -1.f ? -1.f : (c > 1.f ? 1.f : c);
    return acosf(c) * (180.f / 3.14159265358979f);
}

bool padxr_pinch_is_facing(const PadxrHandJoints *hand, const PadxrPinchPose *head,
                           const PadxrPinchParams *params)
{
    static const float minus_y[3] = { 0.f, -1.f, 0.f };
    static const float minus_z[3] = { 0.f, 0.f, -1.f };
    float palm[3], to_head[3], gaze[3], to_wrist[3];

    if (!hand->wrist.valid || !head->valid) {
        return false;
    }
    quat_rotate(hand->wrist.orientation, minus_y, palm);
    v3_sub(head->position, hand->wrist.position, to_head);
    if (angle_deg(palm, to_head) > params->palm_to_head_max_deg) {
        return false;
    }
    if (!params->require_gaze) {
        return true;
    }
    quat_rotate(head->orientation, minus_z, gaze);
    v3_sub(hand->wrist.position, head->position, to_wrist);
    return angle_deg(gaze, to_wrist) <= params->gaze_max_deg;
}

void padxr_pinch_reset(PadxrPinchDetector *d)
{
    d->initialised = true;
    d->pinched = true;
    d->held = false;
}

PadxrPinchOutput padxr_pinch_step(PadxrPinchDetector *d,
                                  const PadxrHandJoints *hand,
                                  const PadxrPinchPose *head,
                                  bool holds_controller,
                                  const PadxrPinchParams *params)
{
    PadxrPinchOutput out = { 0 };
    float diff[3];

    if (!d->initialised) {
        padxr_pinch_reset(d);
    }
#ifndef PADXR_PLANTED_PINCH_IGNORES_CONTROLLER
    if (holds_controller) {
        padxr_pinch_reset(d);
        return out;
    }
#else
    (void)holds_controller;
#endif
    if (!hand->tracked || !hand->tips_valid || !hand->wrist.valid ||
        !head->valid) {
        padxr_pinch_reset(d);
        return out;
    }

    v3_sub(hand->thumb_tip, hand->index_tip, diff);
    float dist = sqrtf(v3_dot(diff, diff));
#ifdef PADXR_PLANTED_PINCH_NO_HYSTERESIS
    float open_m = params->pinch_close_m;
#else
    float open_m = params->pinch_open_m > params->pinch_close_m
                       ? params->pinch_open_m
                       : params->pinch_close_m;
#endif
    bool was = d->pinched;
    if (d->pinched) {
        if (dist > open_m) {
            d->pinched = false;
        }
    } else if (dist < params->pinch_close_m) {
        d->pinched = true;
    }

    out.facing = padxr_pinch_is_facing(hand, head, params);
    out.pinched = d->pinched;
    if (!was && d->pinched && out.facing) {
        d->held = true;
        out.pressed = true;
    }
    if (!d->pinched) {
        d->held = false;
    }
    out.held = d->held;
    return out;
}
