/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VR_PADXR_H
#define HW_XBOX_NV2A_PGRAPH_VR_PADXR_H

#include "vr_padxr_core.h"

#ifdef __cplusplus
extern "C" {
#endif

void vr_padxr_publish_input(int seat, const PadxrInput *in);

bool vr_padxr_input(int seat, PadxrInput *out);

void vr_padxr_merge_gamepad(int port, uint16_t *buttons, int16_t *axis,
                            int n_axis);
void vr_padxr_merge_sbc(int port, uint64_t *buttons, int16_t *axis, int n_axis,
                        uint8_t *gear, uint8_t *tuner, uint8_t *toggles);

uint64_t vr_padxr_transactions(int port);

void vr_padxr_cue(int seat, const PadxrCue *cue);

struct PGRAPHState;
void vr_padxr_frame(struct PGRAPHState *pg);

#ifdef __cplusplus
}
#endif

#endif
