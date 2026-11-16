/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_VR_XR_INPUT_PURE_H
#define HW_XBOX_NV2A_PGRAPH_VK_VR_XR_INPUT_PURE_H

#include <stdbool.h>
#include <stddef.h>

#include <string.h>

#include "vr_hands_geom.h"
#include "vr_padxr_actions.h"

static inline void vr_input_gate_hand(bool ui_owns_input, bool *aim_valid,
                                      bool *poke_valid, bool *select,
                                      bool *select_changed, bool *reported)
{
    if (ui_owns_input) {
        bool was = *reported;
        *aim_valid = false;
#ifndef VR_INPUT_PLANTED_POKE_PASSES
        *poke_valid = false;
#else
        (void)poke_valid;
#endif
        *select = false;
        *select_changed = was;
        *reported = false;
        return;
    }
    *reported = *select;
}

#define VR_INPUT_POKE_ACTION ((PadxrActionId)PADXR_ACTION_COUNT)

static inline size_t vr_input_hands_core_bindings(const PadxrBinding **out)
{
    static const PadxrBinding core[] = {
        { PADXR_CORE_HAND_AIM, PADXR_HAND_LEFT, "/input/aim/pose" },
        { PADXR_CORE_PANEL_SELECT, PADXR_HAND_LEFT, "/input/pinch_ext/value" },
        { VR_INPUT_POKE_ACTION, PADXR_HAND_LEFT, "/input/poke_ext/pose" },
        { PADXR_CORE_HAND_AIM, PADXR_HAND_RIGHT, "/input/aim/pose" },
        { PADXR_CORE_PANEL_SELECT, PADXR_HAND_RIGHT,
          "/input/pinch_ext/value" },
        { VR_INPUT_POKE_ACTION, PADXR_HAND_RIGHT, "/input/poke_ext/pose" },
    };
    *out = core;
    return sizeof(core) / sizeof(core[0]);
}

static inline size_t vr_input_hands_padxr_bindings(bool padxr_set,
                                                   const PadxrBinding **out)
{
    *out = NULL;
#ifdef VR_INPUT_PLANTED_HANDS_SPLIT
    (void)padxr_set;
    return 0;
#else
    return padxr_set ? padxr_default_bindings(PADXR_PROFILE_HANDS, out) : 0;
#endif
}

static inline PadxrHandJoints vr_input_joints_from(const VrHandJoints *h)
{
    PadxrHandJoints j;
    memset(&j, 0, sizeof(j));
#ifdef VR_INPUT_PLANTED_JOINTS_UNFED
    return j;
#endif
#ifndef VR_INPUT_PLANTED_JOINTS_HELD
    if (h->held) {
        return j;
    }
#endif
    if (!h->active) {
        return j;
    }
#ifdef VR_INPUT_PLANTED_JOINTS_TIP
    const VrHandJoint *index = &h->j[XR_HAND_JOINT_INDEX_DISTAL_EXT];
#else
    const VrHandJoint *index = &h->j[XR_HAND_JOINT_INDEX_TIP_EXT];
#endif
    const VrHandJoint *wrist = &h->j[XR_HAND_JOINT_WRIST_EXT];
    const VrHandJoint *thumb = &h->j[XR_HAND_JOINT_THUMB_TIP_EXT];
    j.tracked = true;
    j.wrist.valid = wrist->valid;
    j.wrist.position[0] = wrist->pose.position.x;
    j.wrist.position[1] = wrist->pose.position.y;
    j.wrist.position[2] = wrist->pose.position.z;
    j.wrist.orientation[0] = wrist->pose.orientation.x;
    j.wrist.orientation[1] = wrist->pose.orientation.y;
    j.wrist.orientation[2] = wrist->pose.orientation.z;
    j.wrist.orientation[3] = wrist->pose.orientation.w;
    j.tips_valid = thumb->valid && index->valid;
    j.thumb_tip[0] = thumb->pose.position.x;
    j.thumb_tip[1] = thumb->pose.position.y;
    j.thumb_tip[2] = thumb->pose.position.z;
    j.index_tip[0] = index->pose.position.x;
    j.index_tip[1] = index->pose.position.y;
    j.index_tip[2] = index->pose.position.z;
    return j;
}

static inline PadxrHandJoints vr_input_joints_take(const PadxrHandJoints *slot,
                                                   bool *fed)
{
    PadxrHandJoints j;
    memset(&j, 0, sizeof(j));
    if (*fed) {
        j = *slot;
    }
#ifndef VR_INPUT_PLANTED_JOINTS_STICKY
    *fed = false;
#endif
    return j;
}

#endif
