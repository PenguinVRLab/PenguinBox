/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#include "qemu/osdep.h"
#include "qemu/compiler.h"
#include "qemu/timer.h"

#include <SDL3/SDL.h>

#include "xemu-input.h"
#include "xemu-sim-keys.h"
#include "xemu-sim-merge.h"
#include "xemu-sim-port.h"
#include "xemu-sim-target.h"

#define SIM_CHECK_SBC_BUTTON(id, name, bit) \
    QEMU_BUILD_BUG_ON((1ULL << (bit)) != (uint64_t)SBC_BUTTON_##name);
SIM_SBC_BUTTONS(SIM_CHECK_SBC_BUTTON)
#define SIM_CHECK_SBC_AXIS(id, name, index) QEMU_BUILD_BUG_ON((index) != SBC_AXIS_##name);
SIM_SBC_AXES(SIM_CHECK_SBC_AXIS)
QEMU_BUILD_BUG_ON(SIM_SBC_NAXES != SBC_AXIS__COUNT);
#define SIM_CHECK_DUKE_BUTTON(id, name, bit) QEMU_BUILD_BUG_ON((1u << (bit)) != CONTROLLER_BUTTON_##name);
SIM_DUKE_BUTTONS(SIM_CHECK_DUKE_BUTTON)
#define SIM_CHECK_DUKE_AXIS(id, name, index, trigger) QEMU_BUILD_BUG_ON((index) != CONTROLLER_AXIS_##name);
SIM_DUKE_AXES(SIM_CHECK_DUKE_AXIS)
QEMU_BUILD_BUG_ON(SIM_DUKE_NAXES != CONTROLLER_AXIS__COUNT);

typedef enum { SIM_PORT_SBC, SIM_PORT_DUKE } SimPortKind;

typedef struct SimPortRun {
    bool on;
    SimPortKind kind;
    const SimPort *port;
    SimMerge *merge;
    ControllerState *state;
    ControllerState *prev;
    const char *prev_driver;
    double **vals;
    int **code;
    double rel_x, rel_y, wheel, hwheel;
    int64_t last_us;
} SimPortRun;

static SimPortRun g_sim_ports[4];

bool xemu_sim_port_attached(int index)
{
    return index >= 0 && index < 4 && g_sim_ports[index].on;
}

static int mouse_button(int code)
{
    switch (code) {
    case 272: return SDL_BUTTON_LEFT;
    case 273: return SDL_BUTTON_RIGHT;
    case 274: return SDL_BUTTON_MIDDLE;
    case 275: return SDL_BUTTON_X1;
    case 276: return SDL_BUTTON_X2;
    default: return 0;
    }
}

static void free_run(SimPortRun *r)
{
    if (r->port) {
        for (int d = 0; d < r->port->ndevices; d++) {
            g_free(r->vals ? r->vals[d] : NULL);
            g_free(r->code ? r->code[d] : NULL);
        }
    }
    g_free(r->vals);
    g_free(r->code);
    sim_merge_free(r->merge);
    if (r->state) {
        g_free((char *)r->state->name);
        g_free(r->state);
    }
    memset(r, 0, sizeof(*r));
}

bool xemu_sim_port_attach(int index, const SimPort *p, char *why, size_t whylen)
{
    if (index < 0 || index >= 4 || !p || !p->target) {
        snprintf(why, whylen, "port %d has no Sim Configurator target", index + 1);
        return false;
    }
    if (!p->profile) {
        snprintf(why, whylen, "port %d has no usable profile%s%s", index + 1, p->profile_lost ? ": refused, " : "",
                 p->profile_lost ? p->profile_lost : "");
        return false;
    }
    SimPortKind kind;
    const char *driver;
    if (!strcmp(p->target, "steel_battalion")) {
        kind = SIM_PORT_SBC;
        driver = DRIVER_STEEL_BATTALION;
    } else if (!strcmp(p->target, "xbox_duke")) {
        kind = SIM_PORT_DUKE;
        driver = DRIVER_DUKE;
    } else {
        snprintf(why, whylen, "port %d: PenguinBox cannot emulate the target '%s'", index + 1, p->target);
        return false;
    }
    xemu_sim_port_detach(index);
    SimPortRun *r = &g_sim_ports[index];
    r->port = p;
    r->kind = kind;
    r->vals = g_new0(double *, p->ndevices ? p->ndevices : 1);
    r->code = g_new0(int *, p->ndevices ? p->ndevices : 1);
    for (int d = 0; d < p->ndevices; d++) {
        const SimPortDevice *dev = &p->devices[d];
        r->vals[d] = g_new0(double, dev->ninputs ? dev->ninputs : 1);
        r->code[d] = g_new0(int, dev->ninputs ? dev->ninputs : 1);
        if (!dev->usable) {
            continue;
        }
        if (dev->source != SIM_SRC_KEYBOARD && dev->source != SIM_SRC_MOUSE) {
            snprintf(why, whylen, "port %d: the device '%s' is read by a later build (joysticks, wheels and pedals: "
                     "XB-7; the real Steel Battalion controller: route B)", index + 1, dev->device_id);
            free_run(r);
            return false;
        }
        for (int i = 0; i < dev->ninputs; i++) {
            const SimInput *in = &dev->inputs[i];
            if (dev->source == SIM_SRC_KEYBOARD) {
                char *end = NULL;
                long ev = in->scancode ? strtol(in->scancode, &end, 0) : -1;
                int sc = (in->scancode && end && *end == '\0') ? sim_evdev_to_sdl_scancode((int)ev) : 0;
                if (!sc) {
                    snprintf(why, whylen, "port %d: the key '%s' of '%s' (scancode %s) is not one PenguinBox knows",
                             index + 1, in->id, dev->device_id, in->scancode ? in->scancode : "none");
                    free_run(r);
                    return false;
                }
                r->code[d][i] = sc;
            } else {
                r->code[d][i] = in->kind == SIM_IN_RELATIVE ? in->code : mouse_button(in->code);
            }
        }
    }
    r->merge = sim_merge_new(p);
    r->state = g_new0(ControllerState, 1);
    r->state->type = INPUT_DEVICE_SIM_PORT;
    r->state->name = g_strdup_printf("Sim Configurator: %s", p->profile);
    r->state->bound = -1;
    r->prev = bound_controllers[index];
    r->prev_driver = bound_drivers[index];
    bound_drivers[index] = driver;
    xemu_input_bind(index, r->state, 0);
    if (bound_controllers[index] != r->state) {
        snprintf(why, whylen, "port %d could not be bound", index + 1);
        bound_drivers[index] = r->prev_driver;
        free_run(r);
        return false;
    }
    r->on = true;
    fprintf(stderr, "sim-port: port %d is driven by the Sim Configurator profile '%s' (target %s, %d devices)\n",
            index + 1, p->profile, p->target, p->ndevices);
    return true;
}

static bool still_available(ControllerState *s)
{
    ControllerState *iter;
    QTAILQ_FOREACH(iter, &available_controllers, entry) {
        if (iter == s) {
            return true;
        }
    }
    return false;
}

void xemu_sim_port_detach(int index)
{
    if (!xemu_sim_port_attached(index)) {
        return;
    }
    SimPortRun *r = &g_sim_ports[index];
    if (bound_controllers[index] == r->state) {
        xemu_input_bind(index, NULL, 0);
        bound_drivers[index] = r->prev_driver;
        if (r->prev && still_available(r->prev) && r->prev->bound < 0) {
            xemu_input_bind(index, r->prev, 0);
        }
    }
    fprintf(stderr, "sim-port: port %d given back\n", index + 1);
    free_run(r);
}

void xemu_sim_port_detach_all(void)
{
    for (int i = 0; i < 4; i++) {
        xemu_sim_port_detach(i);
    }
}

void xemu_sim_port_process_sdl_event(const SDL_Event *event)
{
    for (int i = 0; i < 4; i++) {
        SimPortRun *r = &g_sim_ports[i];
        if (!r->on) {
            continue;
        }
        if (event->type == SDL_EVENT_MOUSE_MOTION) {
            r->rel_x += event->motion.xrel;
            r->rel_y += event->motion.yrel;
        } else if (event->type == SDL_EVENT_MOUSE_WHEEL) {
            r->wheel += event->wheel.y;
            r->hwheel += event->wheel.x;
        }
    }
}

void xemu_sim_port_update(ControllerState *state)
{
    SimPortRun *r = NULL;
    for (int i = 0; i < 4; i++) {
        if (g_sim_ports[i].on && g_sim_ports[i].state == state) {
            r = &g_sim_ports[i];
        }
    }
    if (!r) {
        return;
    }
    const bool *kbd = SDL_GetKeyboardState(NULL);
    extern bool xemu_sbc_overlay_wants_mouse(void);
    uint32_t mb = xemu_sbc_overlay_wants_mouse() ? 0 : SDL_GetMouseState(NULL, NULL);
    const SimPort *p = r->port;
    for (int d = 0; d < p->ndevices; d++) {
        const SimPortDevice *dev = &p->devices[d];
        if (!dev->usable) {
            continue;
        }
        for (int i = 0; i < dev->ninputs; i++) {
            const SimInput *in = &dev->inputs[i];
            int c = r->code[d][i];
            double v = 0;
            if (dev->source == SIM_SRC_KEYBOARD) {
                v = kbd[c] ? 1.0 : 0.0;
            } else if (in->kind == SIM_IN_RELATIVE) {
                v = c == 0 ? r->rel_x : c == 1 ? r->rel_y : c == 8 ? r->wheel : c == 6 ? r->hwheel : 0.0;
            } else if (c) {
                v = (mb & SDL_BUTTON_MASK(c)) ? 1.0 : 0.0;
            }
            r->vals[d][i] = v;
        }
    }
    r->rel_x = r->rel_y = r->wheel = r->hwheel = 0;
    int64_t now_us = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
    double dt_ms = r->last_us && now_us > r->last_us ? (double)(now_us - r->last_us) / 1000.0 : 0.0;
    r->last_us = now_us;
    sim_merge_update_dt(r->merge, (const double *const *)r->vals, dt_ms);
    if (r->kind == SIM_PORT_SBC) {
        sim_target_write_sbc(r->merge, &state->sbc.buttons, &state->sbc.previousButtons, state->sbc.axis,
                             &state->sbc.gearLever, &state->sbc.tunerDial, &state->sbc.toggleSwitches);
    } else {
        sim_target_write_duke(r->merge, &state->gp.buttons, state->gp.axis);
    }
}
