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

struct ControllerState;
union SDL_Event;

bool xemu_sim_port_attach(int index, const SimPort *p, char *why, size_t whylen);
void xemu_sim_port_detach(int index);
void xemu_sim_port_detach_all(void);
bool xemu_sim_port_attached(int index);

void xemu_sim_port_update(struct ControllerState *state);
void xemu_sim_port_process_sdl_event(const union SDL_Event *event);

#ifdef __cplusplus
}
#endif

#endif
