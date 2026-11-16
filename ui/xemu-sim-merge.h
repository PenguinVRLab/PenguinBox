/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#ifndef XEMU_SIM_MERGE_H
#define XEMU_SIM_MERGE_H

#include <stdbool.h>
#include <stdint.h>

#include "xemu-sim-profile-reader.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SIM_HAT_UP    0x01
#define SIM_HAT_RIGHT 0x02
#define SIM_HAT_DOWN  0x04
#define SIM_HAT_LEFT  0x08

bool   sim_merge_bipolar(SimRange r);
double sim_merge_normalise(SimRange r, double v);
double sim_merge_denormalise(SimRange r, double c);
double sim_merge_response(const SimBinding *b, double c, bool bipolar, double dev_deadzone);
double sim_merge_policy(SimPolicy p, bool from_bipolar, double c, int which);
double sim_merge_round(double v, double rest);
int16_t sim_merge_sbc_aiming_axis(double wire);

#define SIM_DETENT_UNLISTED 1099511627776.0
double sim_merge_detent_int(const SimInput *lever, long long reading);
double sim_merge_detent_text(const SimInput *lever, const char *reading);
double sim_merge_hat_value(double x, double y);

typedef struct SimMergeOut {
    const SimInput *control;
    bool bound;
    double canonical;
    double raw;
    bool pressed;
    bool has_reading;
    int position;
} SimMergeOut;

typedef struct SimMerge SimMerge;

SimMerge *sim_merge_new(const SimPort *port);
void sim_merge_free(SimMerge *m);

void sim_merge_update(SimMerge *m, const double *const *values);
void sim_merge_update_dt(SimMerge *m, const double *const *values, double dt_ms);

int sim_merge_noutputs(const SimMerge *m);
const SimMergeOut *sim_merge_output(const SimMerge *m, int i);
const SimMergeOut *sim_merge_find(const SimMerge *m, const char *control_id);

void sim_merge_reset(SimMerge *m);

#ifdef __cplusplus
}
#endif

#endif
