/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_VR_HANDS_TRACE_H
#define HW_XBOX_NV2A_PGRAPH_VK_VR_HANDS_TRACE_H

#include <stdbool.h>
#include <stdint.h>

#include "vr_hands_geom.h"

typedef enum VrHandsTraceSabotage {
    VR_HANDS_TRACE_SAB_NONE = 0,
    VR_HANDS_TRACE_SAB_SWAP,
    VR_HANDS_TRACE_SAB_NORADIUS,
    VR_HANDS_TRACE_SAB_HEADSPACE,
    VR_HANDS_TRACE_SAB_NOADVANCE,
    VR_HANDS_TRACE_SAB_DROPNOHAND,
    VR_HANDS_TRACE_SAB_NOUTC,
    VR_HANDS_TRACE_SAB_DROPMARKER,
    VR_HANDS_TRACE_SAB_NOWARN,
    VR_HANDS_TRACE_SAB_NOSKIP,
    VR_HANDS_TRACE_SAB_REOPEN,
    VR_HANDS_TRACE_SAB_RAWFLOAT,
} VrHandsTraceSabotage;

typedef struct VrHandsTraceHeader {
    const char *core;
    const char *build;
    const char *runtime_name;
    const char *runtime_version;
    const char *system;
    const char *const *extensions;
    int n_extensions;
    int64_t t_us_at_start;
    int64_t utc_us_at_start;
} VrHandsTraceHeader;

typedef struct VrHandsTraceInteraction {
    bool present;
    float pinch, grasp;
    int8_t pinch_ready, grasp_ready, aim_valid, poke_valid;
} VrHandsTraceInteraction;

typedef struct VrHandsTraceFrame {
    int64_t t_us;
    int64_t utc_us;
    const char *space;
    bool head_valid;
    XrPosef head;
    const char *session;
    const char *profile[2];
    VrHandsTraceInteraction hi[2];
} VrHandsTraceFrame;

bool vr_hands_trace_start_from_env(const VrHandsTraceHeader *hdr);
bool vr_hands_trace_start(const char *path, double seconds, const char *pose, const char *script,
                          const VrHandsTraceHeader *hdr, VrHandsTraceSabotage sab);
bool vr_hands_trace_active(void);
int vr_hands_trace_frame(const VrHandJoints hands[2], const VrHandsTraceFrame *fx);
int vr_hands_trace_skipped(const char *reason, const VrHandsTraceFrame *fx);
void vr_hands_trace_stop(void);

bool vr_hands_trace_requested(void);
void vr_hands_trace_session_begin(void);
bool vr_hands_trace_warn_not_started(const char *reason_fmt, ...) __attribute__((format(printf, 1, 2)));
void vr_hands_trace_warn_seed_fault(VrHandsTraceSabotage sab);

void vr_hands_trace_mark(void);

const char *vr_hands_trace_session_name(int xr_session_state);

int vr_hands_trace_segment_count(void);

#endif
