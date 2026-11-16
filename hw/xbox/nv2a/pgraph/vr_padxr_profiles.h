/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VR_PADXR_PROFILES_H
#define HW_XBOX_NV2A_PGRAPH_VR_PADXR_PROFILES_H

#include "vr_padxr_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PadxrMemberEntry {
    const PadxrMemberDesc *desc;
    bool built;
} PadxrMemberEntry;

const PadxrMemberEntry *padxr_member_table(size_t *count);
const PadxrMemberEntry *padxr_member_find(const char *id);

typedef enum {
    PADXR_CFG_NONE,
    PADXR_CFG_SBC_PANEL,
    PADXR_CFG_GLOVES,
} PadxrCfgGate;

#define PADXR_ROW_MEMBERS 4

typedef struct PadxrProfileRow {
    uint32_t title_id;
    const char *title;
    struct { const char *id; uint8_t cfg; } members[PADXR_ROW_MEMBERS];
} PadxrProfileRow;

const PadxrProfileRow *padxr_profile_for_title(int64_t title_id);

#ifdef __cplusplus
}
#endif

#endif
