/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_VR_XR_INPUT_H
#define HW_XBOX_NV2A_PGRAPH_VK_VR_XR_INPUT_H

#include <stdbool.h>

#include "vr_xr_loader.h"

#define VR_INPUT_HAND_LEFT  0
#define VR_INPUT_HAND_RIGHT 1
#define VR_INPUT_NUM_HANDS  2

typedef struct VrHandState {
    bool aim_valid;
    XrPosef aim;
    bool select;
    bool select_changed;
    bool poke_valid;
    XrPosef poke;
} VrHandState;

void vr_input_init(void);

void vr_input_destroy(void);

void vr_input_frame(XrTime time);

bool vr_input_hand(int hand, VrHandState *out);

bool vr_input_hand_ungated(int hand, VrHandState *out);

const char *vr_input_hand_profile(int hand);

void vr_input_set_frame_extension(bool enabled);

void vr_input_on_profile_changed(void);

#include "vr_padxr_actions.h"

void vr_input_set_hand_joints(int hand, const PadxrHandJoints *joints);

struct VrHandJoints;
void vr_input_feed_tracked_hands(const struct VrHandJoints *hands);

bool vr_input_padxr_hand(int hand, PadxrHandActions *out);

#endif
