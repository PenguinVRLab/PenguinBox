/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#ifndef XEMU_SIM_BOOT_H
#define XEMU_SIM_BOOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xemu-sim-bootplan.h"
#include "xemu-sim-keys.h"

#ifdef __cplusplus
extern "C" {
#endif

bool xemu_sim_boot_live(void);

const char *xemu_sim_startup_disc(const char *dvd_path);

bool xemu_sim_boot_prepare(const char *ports_id, char *why, size_t whylen);
bool xemu_sim_boot_apply(char *why, size_t whylen);
void xemu_sim_boot_disc_loaded(const char *disc);
void xemu_sim_boot_end(const char *reason);
bool xemu_sim_boot_in_force(void);
void xemu_sim_boot_poll(void);
void xemu_sim_boot_input_ready(void);
void xemu_sim_boot_export_for_restart(void);

bool xemu_sim_boot_take_not_installed(char *label, size_t labellen, char *why, size_t whylen);
#define SIM_BOOT_NOT_INSTALLED "Sim Configurator not installed"

static inline bool sim_boot_preflight_refuses(const char *preflight, char *why, size_t whylen)
{
    if (!preflight || !*preflight || !strcmp(preflight, "READY") || !strcmp(preflight, "WARN")) {
        return false;
    }
    if (why && whylen) {
        snprintf(why, whylen, "the Sim Configurator's pre-flight check %s this setup (pre-flight %s): open the Sim "
                 "Configurator and fix what its Pre-flight page shows", strcmp(preflight, "BLOCK") ? "does not pass" :
                 "blocks", preflight);
    }
    return true;
}

#define SIM_BOOT_NKEYS 512

static inline int sim_boot_bound_keys(const SimPort ports[SIM_PROFILE_MAX_PORTS], const SimBootPlan *plan,
                                      unsigned char held[SIM_BOOT_NKEYS])
{
    int n = 0;
    memset(held, 0, SIM_BOOT_NKEYS);
    for (int p = 0; p < SIM_PROFILE_MAX_PORTS; p++) {
        if (plan->action[p] != SIM_PLAN_ATTACH) {
            continue;
        }
        for (int d = 0; d < ports[p].ndevices; d++) {
            const SimPortDevice *dev = &ports[p].devices[d];
            if (!dev->usable) {
                continue;
            }
            for (int b = 0; b < dev->nbindings; b++) {
                const SimInput *in = dev->bindings[b].source;
                int sc = 0;
                if (dev->source == SIM_SRC_KEYBOARD) {
                    char *end = NULL;
                    long ev = (in && in->scancode) ? strtol(in->scancode, &end, 0) : -1;
                    sc = (in && in->scancode && end && end != in->scancode && *end == '\0') ?
                         sim_evdev_to_sdl_scancode((int)ev) : 0;
                } else if (in && in->kind == SIM_IN_BUTTON && in->code > 0) {
                    sc = sim_evdev_to_sdl_scancode(in->code);
                }
                if (sc > 0 && sc < SIM_BOOT_NKEYS && !held[sc]) {
                    held[sc] = 1;
                    n++;
                }
            }
        }
    }
    return n;
}

static inline bool sim_boot_hotkey_held(const unsigned char held[SIM_BOOT_NKEYS], int sc)
{
    return sc > 0 && sc < SIM_BOOT_NKEYS && held[sc];
}

bool xemu_sim_boot_hotkey_held(int sc);

#ifdef __cplusplus
}
#endif

#endif
