/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"

#include "qemu/timer.h"

#include <math.h>

#include "vr_internal.h"
#include "vr_padxr_actions.h"
#include "vr_public.h"
#include "vr_ui_panel.h"
#include "vr_xr_input_pure.h"
#include "vr_xr_input.h"
#include "hw/xbox/nv2a/pgraph/vr_padxr.h"

typedef struct VrInputState {
    bool ready;
    XrActionSet set;
    XrAction aim;
    XrAction select;
    XrAction poke;
    XrPath hand_path[VR_INPUT_NUM_HANDS];
    XrSpace aim_space[VR_INPUT_NUM_HANDS];
    XrSpace poke_space[VR_INPUT_NUM_HANDS];
    VrHandState hands[VR_INPUT_NUM_HANDS];

    bool px_ready;
    XrActionSet px_set;
    XrAction px_action[PADXR_NUM_PADXR_ACTIONS];
    PadxrHandActions px_hands[VR_INPUT_NUM_HANDS];
    PadxrEventMachine em;
    PFN_xrGetActionStateVector2f get_vec2;
    PFN_xrGetCurrentInteractionProfile get_profile;
    PFN_xrPathToString path_to_string;
    PadxrProfileLatch latch;
    bool holds_controller[VR_INPUT_NUM_HANDS];
    PadxrPinchDetector pinch[VR_INPUT_NUM_HANDS];
    PadxrHandJoints joints[VR_INPUT_NUM_HANDS];
    bool joints_fed[VR_INPUT_NUM_HANDS];
    bool sbc_select_reported[VR_INPUT_NUM_HANDS];
    char profile_path[VR_INPUT_NUM_HANDS][XR_MAX_PATH_LENGTH];
} VrInputState;

static VrInputState s_in;
static bool s_frame_ext;

static bool check_in(XrResult res, const char *what)
{
    if (XR_FAILED(res)) {
        fprintf(stderr, "xemu-vr: input: %s failed (%d)\n", what, (int)res);
        return false;
    }
    return true;
}

static XrPath path_of(const char *s)
{
    XrPath p = XR_NULL_PATH;
    if (!check_in(xr_pfn.StringToPath(g_xemu_vr.xr.instance, s, &p),
                  "xrStringToPath")) {
        return XR_NULL_PATH;
    }
    return p;
}

static XrAction action_handle(PadxrActionId id)
{
    if ((int)id == (int)VR_INPUT_POKE_ACTION) {
        return s_in.poke;
    }
    switch (id) {
    case PADXR_CORE_HAND_AIM:     return s_in.aim;
    case PADXR_CORE_PANEL_SELECT: return s_in.select;
    default:
        if (id >= PADXR_FIRST_PADXR_ACTION && id < PADXR_ACTION_COUNT) {
            return s_in.px_action[id - PADXR_FIRST_PADXR_ACTION];
        }
        return XR_NULL_HANDLE;
    }
}

static bool submit_suggestion(void *ctx, PadxrProfile p,
                              const PadxrBinding *list, size_t n)
{
    static const char *const side[VR_INPUT_NUM_HANDS] = { "left", "right" };
    XrActionSuggestedBinding b[PADXR_MAX_BINDINGS];
    char buf[128];
    uint32_t m = 0;

    (void)ctx;
    for (size_t i = 0; i < n && m < PADXR_MAX_BINDINGS; i++) {
        XrAction a = action_handle(list[i].action);
        if (a == XR_NULL_HANDLE) {
            return false;
        }
        snprintf(buf, sizeof(buf), "/user/hand/%s%s", side[list[i].hand],
                 list[i].subpath);
        b[m++] = (XrActionSuggestedBinding){ a, path_of(buf) };
    }
    XrInteractionProfileSuggestedBinding sb = {
        .type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING,
        .interactionProfile = path_of(padxr_profile_path(p)),
        .countSuggestedBindings = m,
        .suggestedBindings = b,
    };
    if (XR_FAILED(xr_pfn.SuggestInteractionProfileBindings(
            g_xemu_vr.xr.instance, &sb))) {
        fprintf(stderr, "xemu-vr: input: runtime rejected bindings for %s "
                        "(continuing)\n", padxr_profile_path(p));
        return false;
    }
    return true;
}

static void suggest_profile(PadxrProfile p)
{
    const PadxrBinding *core, *px;
    size_t nc = padxr_core_bindings(p, &core);
    size_t np = s_in.px_set != XR_NULL_HANDLE ?
                    padxr_default_bindings(p, &px) : 0;
    if (np == 0) {
        px = NULL;
    }
    PadxrSuggestResult r = padxr_suggest_with_retry(p, core, nc, px, np,
                                                    submit_suggestion, NULL);
    if (r.log[0]) {
        fprintf(stderr, "xemu-vr: input: %s\n", r.log);
    }
    if (r.outcome != PADXR_SUGGEST_REFUSED) {
        fprintf(stderr, "xemu-vr: input: suggested %zu bindings for %s\n",
                r.count, padxr_profile_path(p));
    }
}

static void create_padxr_set(void)
{
    VRXrSession *xr = &g_xemu_vr.xr;
    xr_pfn.GetInstanceProcAddr(xr->instance, "xrGetActionStateVector2f",
                               (PFN_xrVoidFunction *)&s_in.get_vec2);
    xr_pfn.GetInstanceProcAddr(xr->instance,
                               "xrGetCurrentInteractionProfile",
                               (PFN_xrVoidFunction *)&s_in.get_profile);
    xr_pfn.GetInstanceProcAddr(xr->instance, "xrPathToString",
                               (PFN_xrVoidFunction *)&s_in.path_to_string);

    XrActionSetCreateInfo asci = {
        .type = XR_TYPE_ACTION_SET_CREATE_INFO,
        .priority = PADXR_ACTION_SET_PRIORITY,
    };
    pstrcpy(asci.actionSetName, sizeof(asci.actionSetName),
            PADXR_ACTION_SET_NAME);
    pstrcpy(asci.localizedActionSetName, sizeof(asci.localizedActionSetName),
            PADXR_ACTION_SET_LOCALIZED);
    if (!check_in(xr_pfn.CreateActionSet(xr->instance, &asci, &s_in.px_set),
                  "xrCreateActionSet(padxr)")) {
        s_in.px_set = XR_NULL_HANDLE;
        return;
    }
    for (int i = 0; i < PADXR_NUM_PADXR_ACTIONS; i++) {
        const PadxrActionDef *d = padxr_action(PADXR_FIRST_PADXR_ACTION + i);
        XrActionCreateInfo aci = {
            .type = XR_TYPE_ACTION_CREATE_INFO,
            .actionType = d->kind == PADXR_KIND_VEC2 ?
                              XR_ACTION_TYPE_VECTOR2F_INPUT :
                              XR_ACTION_TYPE_BOOLEAN_INPUT,
            .countSubactionPaths = VR_INPUT_NUM_HANDS,
            .subactionPaths = s_in.hand_path,
        };
        pstrcpy(aci.actionName, sizeof(aci.actionName), d->name);
        pstrcpy(aci.localizedActionName, sizeof(aci.localizedActionName),
                d->localized);
        if (!check_in(xr_pfn.CreateAction(s_in.px_set, &aci,
                                          &s_in.px_action[i]),
                      "xrCreateAction(padxr)")) {
            xr_pfn.DestroyActionSet(s_in.px_set);
            s_in.px_set = XR_NULL_HANDLE;
            memset(s_in.px_action, 0, sizeof(s_in.px_action));
            return;
        }
    }
}

void vr_input_set_frame_extension(bool enabled)
{
    s_frame_ext = enabled;
}

static void suggest_hand_interaction(void)
{
    const PadxrBinding *core, *px;
    size_t nc = vr_input_hands_core_bindings(&core);
    size_t np = vr_input_hands_padxr_bindings(s_in.px_set != XR_NULL_HANDLE,
                                              &px);
    PadxrSuggestResult r = padxr_suggest_with_retry(
        PADXR_PROFILE_HANDS, core, nc, np ? px : NULL, np, submit_suggestion,
        NULL);
    if (r.log[0]) {
        fprintf(stderr, "xemu-vr: input: %s\n", r.log);
    }
    if (r.outcome == PADXR_SUGGEST_REFUSED) {
        fprintf(stderr, "xemu-vr: input: runtime rejected hand-interaction "
                        "bindings (continuing; controllers only)\n");
    } else {
        fprintf(stderr, "xemu-vr: input: suggested %zu bindings for %s\n",
                r.count, padxr_profile_path(PADXR_PROFILE_HANDS));
    }
}

void vr_input_init(void)
{
    VRXrSession *xr = &g_xemu_vr.xr;

    if (s_in.ready || xr->instance == XR_NULL_HANDLE ||
        xr->session == XR_NULL_HANDLE) {
        return;
    }
    if (xr_pfn.CreateActionSet == NULL || xr_pfn.SyncActions == NULL) {
        fprintf(stderr, "xemu-vr: input: action PFNs unresolved; no pointer "
                        "this session\n");
        return;
    }

    s_in.hand_path[VR_INPUT_HAND_LEFT] = path_of("/user/hand/left");
    s_in.hand_path[VR_INPUT_HAND_RIGHT] = path_of("/user/hand/right");

    XrActionSetCreateInfo asci = {
        .type = XR_TYPE_ACTION_SET_CREATE_INFO,
        .actionSetName = "penguinbox",
        .localizedActionSetName = "PenguinBox",
    };
    if (!check_in(xr_pfn.CreateActionSet(xr->instance, &asci, &s_in.set),
                  "xrCreateActionSet")) {
        return;
    }

    XrActionCreateInfo aci = {
        .type = XR_TYPE_ACTION_CREATE_INFO,
        .actionType = XR_ACTION_TYPE_POSE_INPUT,
        .countSubactionPaths = VR_INPUT_NUM_HANDS,
        .subactionPaths = s_in.hand_path,
    };
    pstrcpy(aci.actionName, sizeof(aci.actionName), "hand_aim");
    pstrcpy(aci.localizedActionName, sizeof(aci.localizedActionName),
            "Hand aim");
    if (!check_in(xr_pfn.CreateAction(s_in.set, &aci, &s_in.aim),
                  "xrCreateAction(aim)")) {
        goto fail;
    }

    aci.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    pstrcpy(aci.actionName, sizeof(aci.actionName), "panel_select");
    pstrcpy(aci.localizedActionName, sizeof(aci.localizedActionName),
            "Panel select");
    if (!check_in(xr_pfn.CreateAction(s_in.set, &aci, &s_in.select),
                  "xrCreateAction(select)")) {
        goto fail;
    }

    if (xr->hand_interaction_supported) {
        aci.actionType = XR_ACTION_TYPE_POSE_INPUT;
        pstrcpy(aci.actionName, sizeof(aci.actionName), "finger_poke");
        pstrcpy(aci.localizedActionName, sizeof(aci.localizedActionName),
                "Finger poke");
        if (!check_in(xr_pfn.CreateAction(s_in.set, &aci, &s_in.poke),
                      "xrCreateAction(poke)")) {
            goto fail;
        }
    }

    create_padxr_set();

    PadxrProfile profiles[PADXR_PROFILE_COUNT];
    size_t nprof = padxr_profiles_to_suggest(s_frame_ext, profiles,
                                             PADXR_PROFILE_COUNT);
    for (size_t i = 0; i < nprof; i++) {
        suggest_profile(profiles[i]);
    }
    if (s_in.poke != XR_NULL_HANDLE) {
        suggest_hand_interaction();
    }

    for (int h = 0; h < VR_INPUT_NUM_HANDS; h++) {
        XrActionSpaceCreateInfo si = {
            .type = XR_TYPE_ACTION_SPACE_CREATE_INFO,
            .action = s_in.aim,
            .subactionPath = s_in.hand_path[h],
            .poseInActionSpace.orientation.w = 1.0f,
        };
        if (!check_in(xr_pfn.CreateActionSpace(xr->session, &si,
                                               &s_in.aim_space[h]),
                      "xrCreateActionSpace")) {
            goto fail;
        }
        if (s_in.poke != XR_NULL_HANDLE) {
            si.action = s_in.poke;
            if (!check_in(xr_pfn.CreateActionSpace(xr->session, &si,
                                                   &s_in.poke_space[h]),
                          "xrCreateActionSpace(poke)")) {
                goto fail;
            }
        }
    }

    XrActionSet sets[2] = { s_in.set, s_in.px_set };
    XrSessionActionSetsAttachInfo at = {
        .type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO,
        .countActionSets = s_in.px_set != XR_NULL_HANDLE ? 2 : 1,
        .actionSets = sets,
    };
    if (!check_in(xr_pfn.AttachSessionActionSets(xr->session, &at),
                  "xrAttachSessionActionSets")) {
        goto fail;
    }

    s_in.ready = true;
    s_in.px_ready = s_in.px_set != XR_NULL_HANDLE;
    for (int h = 0; h < VR_INPUT_NUM_HANDS; h++) {
        s_in.holds_controller[h] = true;
    }
    padxr_event_reset(&s_in.em);
    fprintf(stderr, "xemu-vr: input: action set attached (aim + select%s, "
                    "both hands)%s\n",
            s_in.poke != XR_NULL_HANDLE ? " + finger poke" : "",
            s_in.px_ready ? "; padxr attached (system, menu_select, "
                            "menu_back, menu_navigate, talk)" : "");
    vr_input_on_profile_changed();
    return;

fail:
    vr_input_destroy();
}

void vr_input_destroy(void)
{
    for (int h = 0; h < VR_INPUT_NUM_HANDS; h++) {
        if (s_in.aim_space[h] != XR_NULL_HANDLE && xr_pfn.DestroySpace) {
            xr_pfn.DestroySpace(s_in.aim_space[h]);
        }
        if (s_in.poke_space[h] != XR_NULL_HANDLE && xr_pfn.DestroySpace) {
            xr_pfn.DestroySpace(s_in.poke_space[h]);
        }
    }
    if (s_in.set != XR_NULL_HANDLE && xr_pfn.DestroyActionSet) {
        xr_pfn.DestroyActionSet(s_in.set);
    }
    if (s_in.px_set != XR_NULL_HANDLE && xr_pfn.DestroyActionSet) {
        xr_pfn.DestroyActionSet(s_in.px_set);
    }
    memset(&s_in, 0, sizeof(s_in));
}

static void padxr_frame(bool live)
{
    VRXrSession *xr = &g_xemu_vr.xr;
    const VRCompositor *c = &g_xemu_vr.comp;
    const PadxrPinchParams pp = padxr_pinch_default_params();
    bool system_held = false;

    if (!s_in.px_ready) {
        return;
    }
    for (int h = 0; h < VR_INPUT_NUM_HANDS; h++) {
        PadxrHandActions *o = &s_in.px_hands[h];
        *o = (PadxrHandActions){ 0 };
        if (live) {
            bool *bools[PADXR_NUM_PADXR_ACTIONS] = {
                [PADXR_SYSTEM - PADXR_FIRST_PADXR_ACTION] = &o->system,
                [PADXR_MENU_SELECT - PADXR_FIRST_PADXR_ACTION] =
                    &o->menu_select,
                [PADXR_MENU_BACK - PADXR_FIRST_PADXR_ACTION] = &o->menu_back,
                [PADXR_TALK - PADXR_FIRST_PADXR_ACTION] = &o->talk,
            };
            for (int i = 0; i < PADXR_NUM_PADXR_ACTIONS; i++) {
                XrActionStateGetInfo gi = {
                    .type = XR_TYPE_ACTION_STATE_GET_INFO,
                    .action = s_in.px_action[i],
                    .subactionPath = s_in.hand_path[h],
                };
                if (bools[i] != NULL) {
                    XrActionStateBoolean bs = {
                        .type = XR_TYPE_ACTION_STATE_BOOLEAN,
                    };
                    xr_pfn.GetActionStateBoolean(xr->session, &gi, &bs);
                    *bools[i] = bs.isActive && bs.currentState;
                } else if (s_in.get_vec2 != NULL) {
                    XrActionStateVector2f vs = {
                        .type = XR_TYPE_ACTION_STATE_VECTOR2F,
                    };
                    s_in.get_vec2(xr->session, &gi, &vs);
                    if (vs.isActive) {
                        o->navigate_x = vs.currentState.x;
                        o->navigate_y = vs.currentState.y;
                    }
                }
            }
        }
        system_held |= o->system;

        PadxrHandJoints j = vr_input_joints_take(&s_in.joints[h],
                                                 &s_in.joints_fed[h]);
        PadxrPinchPose head = { .valid = live && c->head_pose_valid };
        head.position[0] = c->head_pose.position.x;
        head.position[1] = c->head_pose.position.y;
        head.position[2] = c->head_pose.position.z;
        head.orientation[0] = c->head_pose.orientation.x;
        head.orientation[1] = c->head_pose.orientation.y;
        head.orientation[2] = c->head_pose.orientation.z;
        head.orientation[3] = c->head_pose.orientation.w;
        PadxrPinchOutput po = padxr_pinch_step(&s_in.pinch[h], &j, &head,
                                               s_in.holds_controller[h], &pp);
        if (po.pressed) {
            fprintf(stderr, "xemu-vr: input: wrist pinch hand %d\n", h);
        }
        system_held |= po.held;
    }

    const bool ui_open = vr_ui_panel_owns_input();
    uint32_t ev = padxr_event_step(&s_in.em, system_held, live, ui_open,
                                   qemu_clock_get_ms(QEMU_CLOCK_REALTIME));
    for (uint32_t bit = 1; bit <= PADXR_EVENT_RECENTRE; bit <<= 1) {
        if (ev & bit) {
            fprintf(stderr, "xemu-vr: input: padxr %s\n",
                    padxr_event_name(bit));
        }
    }
    if (ev & PADXR_EVENT_RECENTRE) {
        xemu_vr_request_recenter();
    }
    if (ev & (PADXR_EVENT_MENU_OPEN | PADXR_EVENT_MENU_CLOSE)) {
        vr_ui_panel_post_events(ev & (PADXR_EVENT_MENU_OPEN |
                                      PADXR_EVENT_MENU_CLOSE));
    }

    if (ui_open) {
        VrUiPanelInput in = { .hand = -1 };
        VrUiPanelPose pose;
        bool have_pose = vr_ui_panel_pose(&pose);
        float best_nav = 0.f;
        for (int h = 0; h < VR_INPUT_NUM_HANDS; h++) {
            const VrHandState *hs = &s_in.hands[h];
            const PadxrHandActions *pa = &s_in.px_hands[h];
            float nav = fabsf(pa->navigate_x) + fabsf(pa->navigate_y);
            if (nav > best_nav) {
                best_nav = nav;
                in.nav_x = pa->navigate_x;
                in.nav_y = pa->navigate_y;
            }
            in.back |= pa->menu_back;
            if (in.pointer_valid || !have_pose || !hs->aim_valid) {
                continue;
            }
            const float o[3] = { hs->aim.position.x, hs->aim.position.y,
                                 hs->aim.position.z };
            const float qx = hs->aim.orientation.x, qy = hs->aim.orientation.y,
                        qz = hs->aim.orientation.z, qw = hs->aim.orientation.w;
            const float d[3] = { -2.f * (qx * qz + qw * qy),
                                 -2.f * (qy * qz - qw * qx),
                                 -(1.f - 2.f * (qx * qx + qy * qy)) };
            float u, v;
            if (vr_ui_panel_ray_hit(&pose, o, d, &u, &v)) {
                in.pointer_valid = true;
                in.hand = h;
                in.u = u;
                in.v = v;
                in.select = pa->menu_select || hs->select;
            }
        }
        vr_ui_panel_post_input(&in);
    }
}

void vr_input_frame(XrTime time)
{
    VRXrSession *xr = &g_xemu_vr.xr;

    if (!s_in.ready) {
        return;
    }
    if (xr->session_state != XR_SESSION_STATE_FOCUSED) {
        for (int h = 0; h < VR_INPUT_NUM_HANDS; h++) {
            s_in.hands[h] = (VrHandState){ 0 };
        }
        padxr_frame(false);
        return;
    }

    XrActiveActionSet active[2] = {
        { .actionSet = s_in.set, .subactionPath = XR_NULL_PATH },
        { .actionSet = s_in.px_set, .subactionPath = XR_NULL_PATH },
    };
    XrActionsSyncInfo si = {
        .type = XR_TYPE_ACTIONS_SYNC_INFO,
        .countActiveActionSets = s_in.px_ready ? 2 : 1,
        .activeActionSets = active,
    };
    XrResult res = xr_pfn.SyncActions(xr->session, &si);
    if (res == XR_SESSION_NOT_FOCUSED || XR_FAILED(res)) {
        for (int h = 0; h < VR_INPUT_NUM_HANDS; h++) {
            s_in.hands[h].select_changed = false;
        }
        padxr_frame(false);
        return;
    }

    for (int h = 0; h < VR_INPUT_NUM_HANDS; h++) {
        VrHandState *out = &s_in.hands[h];

        XrActionStateGetInfo gi = {
            .type = XR_TYPE_ACTION_STATE_GET_INFO,
            .action = s_in.aim,
            .subactionPath = s_in.hand_path[h],
        };
        XrActionStatePose ps = { .type = XR_TYPE_ACTION_STATE_POSE };
        xr_pfn.GetActionStatePose(xr->session, &gi, &ps);

        out->aim_valid = false;
        if (ps.isActive) {
            XrSpaceLocation loc = { .type = XR_TYPE_SPACE_LOCATION };
            if (!XR_FAILED(xr_pfn.LocateSpace(s_in.aim_space[h], xr->space,
                                              time, &loc))) {
                const XrSpaceLocationFlags need =
                    XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
                    XR_SPACE_LOCATION_POSITION_VALID_BIT;
                if ((loc.locationFlags & need) == need) {
                    out->aim = loc.pose;
                    out->aim_valid = true;
                }
            }
        }

        out->poke_valid = false;
        if (s_in.poke != XR_NULL_HANDLE) {
            gi.action = s_in.poke;
            XrActionStatePose pk = { .type = XR_TYPE_ACTION_STATE_POSE };
            xr_pfn.GetActionStatePose(xr->session, &gi, &pk);
            if (pk.isActive) {
                XrSpaceLocation loc = { .type = XR_TYPE_SPACE_LOCATION };
                const XrSpaceLocationFlags need =
                    XR_SPACE_LOCATION_POSITION_VALID_BIT |
                    XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
                if (!XR_FAILED(xr_pfn.LocateSpace(s_in.poke_space[h],
                                                  xr->space, time, &loc)) &&
                    (loc.locationFlags & need) == need) {
                    out->poke = loc.pose;
                    out->poke_valid = true;
                }
            }
        }

        gi.action = s_in.select;
        XrActionStateBoolean bs = { .type = XR_TYPE_ACTION_STATE_BOOLEAN };
        xr_pfn.GetActionStateBoolean(xr->session, &gi, &bs);
        out->select = bs.isActive && bs.currentState;
        out->select_changed = bs.isActive && bs.changedSinceLastSync;
    }
    padxr_frame(true);

    {
        PadxrInput pi;
        memset(&pi, 0, sizeof(pi));
        for (int h = 0; h < VR_INPUT_NUM_HANDS && h < 2; h++) {
            const VrHandState *hs = &s_in.hands[h];
            PadxrHand *ph = &pi.hands[h];
            ph->aim.valid = hs->aim_valid;
            ph->aim.px = hs->aim.position.x;
            ph->aim.py = hs->aim.position.y;
            ph->aim.pz = hs->aim.position.z;
            ph->aim.qx = hs->aim.orientation.x;
            ph->aim.qy = hs->aim.orientation.y;
            ph->aim.qz = hs->aim.orientation.z;
            ph->aim.qw = hs->aim.orientation.w;
            ph->select = hs->select;
            ph->select_changed = hs->select_changed;
        }
        const VRCompositor *vc = &g_xemu_vr.comp;
        pi.head.valid = vc->head_pose_valid;
        pi.head.px = vc->head_pose.position.x;
        pi.head.py = vc->head_pose.position.y;
        pi.head.pz = vc->head_pose.position.z;
        pi.head.qx = vc->head_pose.orientation.x;
        pi.head.qy = vc->head_pose.orientation.y;
        pi.head.qz = vc->head_pose.orientation.z;
        pi.head.qw = vc->head_pose.orientation.w;
        pi.actions_active = true;
        vr_padxr_publish_input(0, &pi);
    }
}

bool vr_input_hand(int hand, VrHandState *out)
{
    if (!s_in.ready || hand < 0 || hand >= VR_INPUT_NUM_HANDS) {
        memset(out, 0, sizeof(*out));
        return false;
    }
    *out = s_in.hands[hand];
    vr_input_gate_hand(vr_ui_panel_owns_input(), &out->aim_valid,
                       &out->poke_valid, &out->select, &out->select_changed,
                       &s_in.sbc_select_reported[hand]);
    return true;
}

bool vr_input_hand_ungated(int hand, VrHandState *out)
{
    if (!s_in.ready || hand < 0 || hand >= VR_INPUT_NUM_HANDS) {
        memset(out, 0, sizeof(*out));
        return false;
    }
    *out = s_in.hands[hand];
    return true;
}

const char *vr_input_hand_profile(int hand)
{
    if (!s_in.ready || hand < 0 || hand >= VR_INPUT_NUM_HANDS ||
        s_in.profile_path[hand][0] == '\0') {
        return NULL;
    }
    return s_in.profile_path[hand];
}

bool vr_input_padxr_hand(int hand, PadxrHandActions *out)
{
    if (!s_in.px_ready || hand < 0 || hand >= VR_INPUT_NUM_HANDS) {
        memset(out, 0, sizeof(*out));
        return false;
    }
    *out = s_in.px_hands[hand];
    return true;
}

void vr_input_set_hand_joints(int hand, const PadxrHandJoints *joints)
{
    if (hand < 0 || hand >= VR_INPUT_NUM_HANDS || joints == NULL) {
        return;
    }
    s_in.joints[hand] = *joints;
    s_in.joints_fed[hand] = true;
}

void vr_input_feed_tracked_hands(const VrHandJoints *hands)
{
    for (int h = 0; h < VR_INPUT_NUM_HANDS; h++) {
        const PadxrHandJoints j = vr_input_joints_from(&hands[h]);
        vr_input_set_hand_joints(h, &j);
    }
}

void vr_input_on_profile_changed(void)
{
    VRXrSession *xr = &g_xemu_vr.xr;
    char path[2][XR_MAX_PATH_LENGTH];
    char line[2 * XR_MAX_PATH_LENGTH + 64];

    if (!s_in.ready || s_in.get_profile == NULL ||
        s_in.path_to_string == NULL) {
        return;
    }
    for (int h = 0; h < VR_INPUT_NUM_HANDS; h++) {
        XrInteractionProfileState st = {
            .type = XR_TYPE_INTERACTION_PROFILE_STATE,
        };
        uint32_t len = 0;
        path[h][0] = '\0';
        if (XR_SUCCEEDED(s_in.get_profile(xr->session, s_in.hand_path[h],
                                          &st)) &&
            st.interactionProfile != XR_NULL_PATH) {
            s_in.path_to_string(xr->instance, st.interactionProfile,
                                sizeof(path[h]), &len, path[h]);
        }
        PadxrProfile p = padxr_find_profile(path[h]);
        pstrcpy(s_in.profile_path[h], sizeof(s_in.profile_path[h]), path[h]);
        s_in.holds_controller[h] = path[h][0] != '\0' &&
                                   p != PADXR_PROFILE_HANDS;
    }
    if (padxr_profile_changed(&s_in.latch, path[0], path[1], line,
                              sizeof(line))) {
        fprintf(stderr, "%s\n", line);
    }
}
