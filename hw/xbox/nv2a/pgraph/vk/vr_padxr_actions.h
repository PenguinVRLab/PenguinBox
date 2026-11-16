/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_VR_PADXR_ACTIONS_H
#define HW_XBOX_NV2A_PGRAPH_VK_VR_PADXR_ACTIONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PADXR_ACTION_SET_NAME      "padxr"
#define PADXR_ACTION_SET_LOCALIZED "PadXR"
#define PADXR_ACTION_SET_PRIORITY  0u

#define PADXR_HAND_LEFT  0
#define PADXR_HAND_RIGHT 1
#define PADXR_NUM_HANDS  2

typedef enum PadxrKind {
    PADXR_KIND_BOOL,
    PADXR_KIND_FLOAT,
    PADXR_KIND_VEC2,
    PADXR_KIND_POSE,
    PADXR_KIND_HAPTIC,
} PadxrKind;

typedef enum PadxrActionId {
    PADXR_CORE_HAND_AIM = 0,
    PADXR_CORE_PANEL_SELECT,
    PADXR_CORE_COUNT,
    PADXR_SYSTEM = PADXR_CORE_COUNT,
    PADXR_MENU_SELECT,
    PADXR_MENU_BACK,
    PADXR_MENU_NAVIGATE,
    PADXR_TALK,
    PADXR_ACTION_COUNT
} PadxrActionId;

#define PADXR_FIRST_PADXR_ACTION PADXR_SYSTEM
#define PADXR_NUM_PADXR_ACTIONS (PADXR_ACTION_COUNT - PADXR_FIRST_PADXR_ACTION)

typedef struct PadxrActionDef {
    const char *name;
    const char *localized;
    const char *abstract_name;
    PadxrKind kind;
    bool padxr_set;
    bool reserved;
} PadxrActionDef;

const PadxrActionDef *padxr_action(PadxrActionId id);
PadxrActionId padxr_find_action(const char *name);

typedef enum PadxrProfile {
    PADXR_PROFILE_SIMPLE = 0,
    PADXR_PROFILE_TOUCH,
    PADXR_PROFILE_INDEX,
    PADXR_PROFILE_VIVE,
    PADXR_PROFILE_HANDS,
    PADXR_PROFILE_FRAME,
    PADXR_PROFILE_COUNT
} PadxrProfile;

#define PADXR_FRAME_EXTENSION_NAME "XR_VALVE_frame_controller_interaction"

const char *padxr_profile_path(PadxrProfile p);
PadxrProfile padxr_find_profile(const char *path);

size_t padxr_profiles_to_suggest(bool frame_extension_enabled,
                                 PadxrProfile *out, size_t cap);

typedef struct PadxrBinding {
    PadxrActionId action;
    uint8_t hand;
    const char *subpath;
} PadxrBinding;

#define PADXR_MAX_BINDINGS 16

size_t padxr_core_bindings(PadxrProfile p, const PadxrBinding **out);

size_t padxr_default_bindings(PadxrProfile p, const PadxrBinding **out);

size_t padxr_merge_suggestions(const PadxrBinding *core, size_t ncore,
                               const PadxrBinding *padxr, size_t npadxr,
                               PadxrBinding *out, size_t cap);

typedef enum PadxrSuggestOutcome {
    PADXR_SUGGEST_MERGED,
    PADXR_SUGGEST_CORE_ONLY,
    PADXR_SUGGEST_REFUSED,
} PadxrSuggestOutcome;

typedef bool (*PadxrSubmitFn)(void *ctx, PadxrProfile p,
                              const PadxrBinding *list, size_t n);

typedef struct PadxrSuggestResult {
    PadxrSuggestOutcome outcome;
    size_t count;
    char log[160];
} PadxrSuggestResult;

PadxrSuggestResult padxr_suggest_with_retry(PadxrProfile p,
                                            const PadxrBinding *core,
                                            size_t ncore,
                                            const PadxrBinding *padxr,
                                            size_t npadxr,
                                            PadxrSubmitFn submit, void *ctx);

bool padxr_path_allowed(PadxrProfile p, int hand, const char *subpath,
                        PadxrKind action_kind);

typedef struct PadxrHandActions {
    bool system;
    bool menu_select;
    bool menu_back;
    bool talk;
    float navigate_x;
    float navigate_y;
} PadxrHandActions;

#define PADXR_RECENTRE_HOLD_MS 800

enum {
    PADXR_EVENT_NONE = 0,
    PADXR_EVENT_MENU_OPEN = 1u << 0,
    PADXR_EVENT_MENU_CLOSE = 1u << 1,
    PADXR_EVENT_RECENTRE = 1u << 2,
};

const char *padxr_event_name(uint32_t one_event);

typedef enum PadxrPhase {
    PADXR_PHASE_WAIT_RELEASE = 0,
    PADXR_PHASE_IDLE,
    PADXR_PHASE_PRESSED,
    PADXR_PHASE_LONG_FIRED,
} PadxrPhase;

typedef struct PadxrEventMachine {
    PadxrPhase phase;
    int64_t press_ms;
} PadxrEventMachine;

uint32_t padxr_event_step(PadxrEventMachine *m, bool system_held,
                          bool input_live, bool ui_open, int64_t now_ms);
void padxr_event_reset(PadxrEventMachine *m);

typedef struct PadxrProfileLatch {
    bool any;
    char left[128];
    char right[128];
} PadxrProfileLatch;

bool padxr_profile_changed(PadxrProfileLatch *l, const char *left,
                           const char *right, char *line, size_t cap);

#define PADXR_PINCH_PALM_TO_HEAD_MAX_DEG 45.0f
#define PADXR_PINCH_GAZE_MAX_DEG         35.0f
#define PADXR_PINCH_CLOSE_M              0.020f
#define PADXR_PINCH_OPEN_M               0.035f

typedef struct PadxrPinchPose {
    float position[3];
    float orientation[4];
    bool valid;
} PadxrPinchPose;

typedef struct PadxrHandJoints {
    PadxrPinchPose wrist;
    float thumb_tip[3];
    float index_tip[3];
    bool tips_valid;
    bool tracked;
} PadxrHandJoints;

typedef struct PadxrPinchParams {
    float palm_to_head_max_deg;
    bool require_gaze;
    float gaze_max_deg;
    float pinch_close_m;
    float pinch_open_m;
} PadxrPinchParams;

PadxrPinchParams padxr_pinch_default_params(void);

typedef struct PadxrPinchOutput {
    bool pressed;
    bool held;
    bool facing;
    bool pinched;
} PadxrPinchOutput;

typedef struct PadxrPinchDetector {
    bool initialised;
    bool pinched;
    bool held;
} PadxrPinchDetector;

bool padxr_pinch_is_facing(const PadxrHandJoints *hand, const PadxrPinchPose *head,
                           const PadxrPinchParams *params);
PadxrPinchOutput padxr_pinch_step(PadxrPinchDetector *d,
                                  const PadxrHandJoints *hand,
                                  const PadxrPinchPose *head,
                                  bool holds_controller,
                                  const PadxrPinchParams *params);
void padxr_pinch_reset(PadxrPinchDetector *d);

#ifdef __cplusplus
}
#endif

#endif
