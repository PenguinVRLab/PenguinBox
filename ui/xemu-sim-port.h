/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#ifndef XEMU_SIM_PORT_H
#define XEMU_SIM_PORT_H

#include <stdbool.h>
#include <stddef.h>

#include "xemu-sim-profile-reader.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline int sim_port_mouse_button(int code)
{
    switch (code) {
    case 272: return 1;
    case 273: return 3;
    case 274: return 2;
    case 275: return 4;
    case 276: return 5;
    default: return 0;
    }
}

static inline bool sim_port_mouse_motion(int code)
{
    return code == 0 || code == 1 || code == 6 || code == 8;
}

struct ControllerState;
union SDL_Event;

bool xemu_sim_port_attach(int index, const SimPort *p, char *why, size_t whylen);
void xemu_sim_port_detach(int index);
void xemu_sim_port_detach_all(void);
bool xemu_sim_port_attached(int index);

void xemu_sim_port_sbc_output(int index, const void *report, size_t len);

void xemu_sim_port_update(struct ControllerState *state);
void xemu_sim_port_process_sdl_event(const union SDL_Event *event);

#ifdef __cplusplus
}
#endif

#endif
