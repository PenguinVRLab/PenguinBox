/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"

#include "xemu-version.h"

#include <math.h>

#include "vr_internal.h"
#include "vr_hands.h"
#include "vr_hands_trace.h"
#include "vr_public.h"
#include "vr_xr_input.h"

#define HAND_INTERACTION_PROFILE "/interaction_profiles/ext/hand_interaction_ext"

static struct {
    bool ready;
    bool warned;
    XrHandTrackerEXT tracker[2];
} s_h;

bool vr_hands_init(void)
{
    VRXrSession *xr = &g_xemu_vr.xr;

    if (s_h.ready) {
        return true;
    }
    if (!xr->hand_tracking_supported || xr->session == XR_NULL_HANDLE ||
        xr_pfn.CreateHandTrackerEXT == NULL) {
        if (!s_h.warned) {
            s_h.warned = true;
            fprintf(stderr, "PenguinBox: hands: XR_EXT_hand_tracking is not "
                            "available this session; no joints.\n");
        }
        vr_hands_trace_warn_not_started("XR_EXT_hand_tracking is not enabled this session");
        return false;
    }
    static const XrHandEXT which[2] = { XR_HAND_LEFT_EXT, XR_HAND_RIGHT_EXT };
    for (int h = 0; h < 2; h++) {
        XrHandTrackerCreateInfoEXT ci = {
            .type = XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT,
            .hand = which[h],
            .handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT,
        };
        XrResult res = xr_pfn.CreateHandTrackerEXT(xr->session, &ci,
                                                   &s_h.tracker[h]);
        if (XR_FAILED(res)) {
            s_h.tracker[h] = XR_NULL_HANDLE;
            if (!s_h.warned) {
                s_h.warned = true;
                fprintf(stderr, "PenguinBox: hands: xrCreateHandTrackerEXT "
                                "failed (%d); no joints.\n", (int)res);
            }
            vr_hands_trace_warn_not_started("xrCreateHandTrackerEXT failed (result %d)", (int)res);
            vr_hands_destroy();
            return false;
        }
    }
    s_h.ready = true;
    if (getenv("XEMU_VR_HANDS_TRACE") != NULL) {
        char version[32];
        snprintf(version, sizeof(version), "%u.%u.%u", XR_VERSION_MAJOR(xr->runtime_version),
                 XR_VERSION_MINOR(xr->runtime_version), XR_VERSION_PATCH(xr->runtime_version));
        const VrHandsTraceHeader hdr = {
            .core = "xemu_vr",
            .build = xemu_commit,
            .runtime_name = xr->runtime_name[0] ? xr->runtime_name : NULL,
            .runtime_version = xr->runtime_version ? version : NULL,
            .system = xr->system_name[0] ? xr->system_name : NULL,
            .extensions = xr->enabled_ext,
            .n_extensions = (int)xr->enabled_ext_count,
            .t_us_at_start = g_get_monotonic_time(),
            .utc_us_at_start = g_get_real_time(),
        };
        if (!vr_hands_trace_start_from_env(&hdr)) {
            vr_hands_trace_warn_not_started("the trace file could not be opened or its script could not be read "
                                            "(see the line above)");
        }
    }
    fprintf(stderr, "PenguinBox: hands: two hand trackers created (%u joints "
                    "each, located in LOCAL at the display time)\n",
            (unsigned)XR_HAND_JOINT_COUNT_EXT);
    return true;
}

void vr_hands_destroy(void)
{
    for (int h = 0; h < 2; h++) {
        if (s_h.tracker[h] != XR_NULL_HANDLE && xr_pfn.DestroyHandTrackerEXT) {
            xr_pfn.DestroyHandTrackerEXT(s_h.tracker[h]);
        }
        s_h.tracker[h] = XR_NULL_HANDLE;
    }
    s_h.ready = false;
    vr_hands_trace_stop();
}

bool vr_hands_ready(void)
{
    return s_h.ready;
}

static void trace_frame(const VrHandJoints out[2])
{
    const VRXrSession *xr = &g_xemu_vr.xr;
    const VRCompositor *c = &g_xemu_vr.comp;
    VrHandsTraceFrame fx = {
        .t_us = g_get_monotonic_time(),
        .utc_us = g_get_real_time(),
        .space = "LOCAL",
        .head_valid = c->head_pose_valid,
        .head = c->head_pose,
        .session = vr_hands_trace_session_name((int)xr->session_state),
    };
    for (int h = 0; h < 2; h++) {
        fx.profile[h] = vr_input_hand_profile(h);
        VrHandState st;
        if (xr->hand_interaction_supported && fx.profile[h] &&
            !strcmp(fx.profile[h], HAND_INTERACTION_PROFILE) &&
            vr_input_hand_ungated(h, &st)) {
            fx.hi[h] = (VrHandsTraceInteraction){
                .present = true,
                .pinch = st.select ? 1.f : 0.f,
                .grasp = NAN,
                .pinch_ready = -1,
                .grasp_ready = -1,
                .aim_valid = st.aim_valid,
                .poke_valid = st.poke_valid,
            };
        }
    }
    vr_hands_trace_frame(out, &fx);
}

void vr_hands_locate(XrTime time, VrHandJoints out[2], int64_t *cost_us)
{
    VRXrSession *xr = &g_xemu_vr.xr;
    const int64_t t0 = g_get_monotonic_time();

    memset(out, 0, 2 * sizeof(VrHandJoints));
    if (!s_h.ready) {
        *cost_us = 0;
        return;
    }
    for (int h = 0; h < 2; h++) {
        XrHandJointLocationEXT loc[XR_HAND_JOINT_COUNT_EXT];
        XrHandJointLocationsEXT locs = {
            .type = XR_TYPE_HAND_JOINT_LOCATIONS_EXT,
            .jointCount = XR_HAND_JOINT_COUNT_EXT,
            .jointLocations = loc,
        };
        XrHandJointsLocateInfoEXT li = {
            .type = XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT,
            .baseSpace = xr->space,
            .time = time,
        };
        if (XR_FAILED(xr_pfn.LocateHandJointsEXT(s_h.tracker[h], &li, &locs)) ||
            !locs.isActive) {
            continue;
        }
        out[h].active = true;
        const XrSpaceLocationFlags need =
            XR_SPACE_LOCATION_POSITION_VALID_BIT |
            XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        const XrSpaceLocationFlags seen =
            XR_SPACE_LOCATION_POSITION_TRACKED_BIT |
            XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
        for (uint32_t k = 0; k < XR_HAND_JOINT_COUNT_EXT; k++) {
            out[h].j[k].valid = (loc[k].locationFlags & need) == need;
            out[h].j[k].tracked = (loc[k].locationFlags & seen) == seen;
            out[h].j[k].flags = loc[k].locationFlags;
            out[h].j[k].pose = loc[k].pose;
            out[h].j[k].radius = loc[k].radius;
        }
    }
    *cost_us = g_get_monotonic_time() - t0;
    if (vr_hands_trace_active()) {
        trace_frame(out);
    }
}

void xemu_vr_hands_trace_mark(void)
{
    vr_hands_trace_mark();
}
