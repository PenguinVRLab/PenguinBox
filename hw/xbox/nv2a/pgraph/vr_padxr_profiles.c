/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include "vr_padxr_profiles.h"

#include <string.h>

static const PadxrMemberDesc k_sbc_panel = {
    .id = "sbcPanel", .kind = "panel",
    .primitives = PADXR_PRIM_R1 | PADXR_PRIM_R2 | PADXR_PRIM_R4 |
                  PADXR_PRIM_R6 | PADXR_PRIM_R7 | PADXR_PRIM_R10,
    .needs = { PADXR_NEED_CONTROLLERS | PADXR_NEED_HANDS },
    .res = { [PADXR_RES_HAPTICS] = PADXR_CLAIM_SHARED },
    .layers = 1,
#ifdef PADXR_PLANTED_MANIFEST_DRIFT
    .family = PADXR_FAM_PAD, .route = PADXR_ROUTE_ADAPTER,
#else
    .family = PADXR_FAM_PAD, .route = PADXR_ROUTE_MERGE,
#endif
    .out_kind = PADXR_OUT_PRESS,
    .online = PADXR_ONLINE_ANY, .clock = PADXR_CLOCK_WALL,
    .session_gates = PADXR_SG_ALL,
    .own_pad = PADXR_PAD_SBC, .own_buttons = 0,
};

static const PadxrMemberDesc k_haptic_gloves = {
    .id = "hapticGloves", .kind = "haptics-backend",
    .primitives = PADXR_PRIM_R9R | PADXR_PRIM_R10 | PADXR_PRIM_R11,
    .needs = { PADXR_NEED_HAPTICS },
    .res = { [PADXR_RES_HAPTICS] = PADXR_CLAIM_SHARED },
    .family = PADXR_FAM_RF, .route = PADXR_ROUTE_NONE,
    .out_kind = PADXR_OUT_DISPLAY,
    .online = PADXR_ONLINE_ANY, .clock = PADXR_CLOCK_WALL,
    .session_gates = PADXR_SG_ALL,
};

static const PadxrMemberEntry k_members[] = {
    { &k_haptic_gloves, false },
    { &k_sbc_panel, false },
};

const PadxrMemberEntry *padxr_member_table(size_t *count)
{
    if (count) {
        *count = sizeof(k_members) / sizeof(k_members[0]);
    }
    return k_members;
}

const PadxrMemberEntry *padxr_member_find(const char *id)
{
    size_t n = 0;
    const PadxrMemberEntry *t = padxr_member_table(&n);
    for (size_t i = 0; id && i < n; i++) {
        if (strcmp(t[i].desc->id, id) == 0) {
            return &t[i];
        }
    }
    return NULL;
}

static const PadxrProfileRow k_rows[] = {
    { 0x43430002u, "Steel Battalion",
      { { "sbcPanel", PADXR_CFG_SBC_PANEL },
        { "hapticGloves", PADXR_CFG_GLOVES } } },
    { 0x43430009u, "Steel Battalion: Line of Contact",
      { { "sbcPanel", PADXR_CFG_SBC_PANEL },
        { "hapticGloves", PADXR_CFG_GLOVES } } },
};

const PadxrProfileRow *padxr_profile_for_title(int64_t title_id)
{
#ifdef PADXR_PLANTED_UNCONDITIONAL_ROW
    return &k_rows[0];
#endif
    if (title_id < 0) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(k_rows) / sizeof(k_rows[0]); i++) {
        if (k_rows[i].title_id == (uint32_t)title_id) {
            return &k_rows[i];
        }
    }
    return NULL;
}
