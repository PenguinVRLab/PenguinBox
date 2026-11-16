/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VR_MODS_GLUE_H
#define HW_XBOX_NV2A_PGRAPH_VR_MODS_GLUE_H

typedef struct NV2AState NV2AState;

void vr_mods_flip(NV2AState *d);

void vr_mods_machine_changed(void);

#endif
