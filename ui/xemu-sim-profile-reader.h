/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#ifndef XEMU_SIM_PROFILE_READER_H
#define XEMU_SIM_PROFILE_READER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SIM_PROFILE_MAX_PORTS 4

#define SIM_PROFILE_MAX_FILE_BYTES (1024 * 1024)
#define SIM_PROFILE_MAX_DEPTH 100
#define SIM_PROFILE_MAX_NODES 100000

typedef enum SimSourceKind {
    SIM_SRC_SDL_JOYSTICK,
    SIM_SRC_SDL_GAMEPAD,
    SIM_SRC_KEYBOARD,
    SIM_SRC_MOUSE,
    SIM_SRC_HID_USB,
    SIM_SRC_VR_VIRTUAL,
} SimSourceKind;

typedef enum SimInputKind {
    SIM_IN_AXIS,
    SIM_IN_BUTTON,
    SIM_IN_HAT,
    SIM_IN_DETENT,
    SIM_IN_RELATIVE,
} SimInputKind;

typedef struct SimRange {
    double min, rest, max;
} SimRange;

typedef struct SimInput {
    const char *id;
    SimInputKind kind;
    bool has_range;
    SimRange range;
    bool bipolar;
    bool invert;
    double deadzone;
    bool analog;
    SimRange analog_range;
    int code;
    const char *scancode;
    int npositions;
    const char *const *positions;
    bool stays_put;
} SimInput;

typedef enum SimPolicy {
    SIM_POLICY_NONE,
    SIM_POLICY_HALF_POSITIVE,
    SIM_POLICY_HALF_NEGATIVE,
    SIM_POLICY_ABSOLUTE,
    SIM_POLICY_FULL_RANGE,
    SIM_POLICY_SPLIT,
} SimPolicy;

typedef struct SimBinding {
    const SimInput *source;
    const SimInput *target;
    const SimInput *target2;
    SimPolicy policy;
    bool has_threshold;
    double threshold, hysteresis;
    bool has_value;
    double value;
    bool has_ramp;
    double press_ms, release_ms;
    bool has_rate;
    double rate;
    const char *position;
    const char *direction;
    bool integrate;
    double counts_per_travel;
    const SimInput *recentre;
    bool resp_invert;
    bool has_window;
    double window_lo, window_hi;
    bool has_curve, has_deadzone, has_saturation;
    double curve, resp_deadzone, saturation;
} SimBinding;

typedef struct SimCalibrated {
    const SimInput *input;
    SimRange range;
} SimCalibrated;

typedef struct SimPortDevice {
    const char *device_id;
    const char *key;
    const char *port_path;
    SimSourceKind source;
    bool has_match;
    uint16_t vid, pid;
    int nmatch_also;
    const uint16_t *match_also;
    int ninputs;
    const SimInput *inputs;
    int nbindings;
    const SimBinding *bindings;
    int ncalibrated;
    const SimCalibrated *calibrated;
    bool usable;
} SimPortDevice;

typedef struct SimPort {
    int port;
    const char *target;
    const char *profile;
    const char *profile_lost;
    bool has_title_id;
    uint32_t title_id;
    int ndevices;
    const SimPortDevice *devices;
} SimPort;

typedef struct SimRefusal {
    const char *code;
    const char *where;
    const char *why;
} SimRefusal;

typedef struct SimProfileSet SimProfileSet;

SimProfileSet *sim_profile_set_load(const char *builtin_dir, const char *profile_dir);
void sim_profile_set_free(SimProfileSet *set);

int sim_profile_set_nrefused(const SimProfileSet *set);
const SimRefusal *sim_profile_set_refusal(const SimProfileSet *set, int i);

int sim_profile_set_count(const SimProfileSet *set, const char *kind);
bool sim_profile_set_has(const SimProfileSet *set, const char *kind, const char *id);

const SimInput *sim_profile_target_control(const SimProfileSet *set, const char *target,
                                           const char *control);

int sim_profile_set_ports(const SimProfileSet *set, const char *ports_id,
                          SimPort out[SIM_PROFILE_MAX_PORTS]);

char sim_profile_scalar_kind(const char *text, bool quoted, long long *i, double *d);

#ifdef __cplusplus
}
#endif

#endif
