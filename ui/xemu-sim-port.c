/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#include "qemu/osdep.h"
#include "qemu/compiler.h"
#include "qemu/timer.h"

#include <SDL3/SDL.h>

#include "xemu-input.h"
#include "xemu-sbc-device.h"
#include "xemu-sim-bootplan.h"
#include "xemu-sim-evdev.h"
#include "xemu-sim-keys.h"
#include "xemu-sim-merge.h"
#include "xemu-sim-port.h"
#include "xemu-sim-sbc-source.h"
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
    const double **feed;
    SimEvdevReader **reader;
    bool *gone_said;
    int sbc_dev;
    bool sbc_gone;
    bool lights_on;
    SimSbcLights lights;
    uint32_t lights_seen;
    int **code;
    double rel_x, rel_y, wheel, hwheel;
    int64_t last_us;
} SimPortRun;

static SimPortRun g_sim_ports[4];

static int g_sbc_port = -1;

static SimSbcLightSlot g_light_slot[4];

void xemu_sim_port_sbc_output(int index, const void *report, size_t len)
{
    if (index >= 0 && index < 4) {
        sim_sbc_lights_store(&g_light_slot[index], report, len);
    }
}

static void sbc_give_back(SimPortRun *r)
{
    if (r->sbc_dev >= 0 && g_sbc_port == (int)(r - g_sim_ports)) {
        uint8_t off[SIM_SBC_LIGHTS_MAX];
        size_t n = 0;
        if (r->lights_on && !r->sbc_gone) {
            n = sim_sbc_lights_off(&r->lights, off);
            fprintf(stderr, "sim-port: port %d: the Steel Battalion controller's lights put out\n",
                    (int)(r - g_sim_ports) + 1);
        }
        sbc_device_serve_end(n ? off : NULL, n);
        g_sbc_port = -1;
    }
    r->sbc_dev = -1;
    r->lights_on = false;
}

static bool sbc_send_lights(const uint8_t *report, size_t len, void *ctx)
{
    (void)ctx;
    return sbc_device_send_output(report, len);
}

static const char *sbc_advice(SbcDeviceStatus st)
{
    switch (st) {
    case SBC_DEV_ABSENT:
        return "the Steel Battalion controller is not plugged in (or went away while it was opened): plug it in and "
               "boot again";
    case SBC_DEV_NO_PERMISSION:
        return "this user may not open the Steel Battalion controller: install the udev rule "
               "70-penguinvrlabs-sim-devices.rules (run PenguinBox once with --install-sbc-rules), then unplug the "
               "controller and plug it in again";
    case SBC_DEV_NO_DRIVER:
        return "the Steel Battalion controller is not bound to a WinUSB driver: bind WinUSB to it with Zadig, then "
               "unplug it and plug it in again";
    case SBC_DEV_BUSY:
        return "another program holds the Steel Battalion controller (the Sim Configurator still open, a second "
               "PenguinBox, or USB passthrough): close it and boot again";
    default:
        return "PenguinBox could not open the Steel Battalion controller";
    }
}

static bool open_sbc(SimPortRun *r, int d, int port, char *why, size_t whylen)
{
    const SimPortDevice *dev = &r->port->devices[d];
    if (r->sbc_dev >= 0) {
        snprintf(why, whylen, "port %d: the setup puts the Steel Battalion controller on this port twice (the devices "
                 "'%s' and '%s'); one controller is one device", port, r->port->devices[r->sbc_dev].device_id,
                 dev->device_id);
        return false;
    }
    if (g_sbc_port >= 0) {
        snprintf(why, whylen, "port %d: the Steel Battalion controller already drives port %d; one controller drives "
                 "one console port: take it off one of them in the Sim Configurator", port, g_sbc_port + 1);
        return false;
    }
    char detail[256];
    SbcDeviceStatus st = sbc_device_serve_begin(detail, sizeof(detail));
    if (st != SBC_DEV_READY) {
        snprintf(why, whylen, "port %d: %s (%s)", port, sbc_advice(st), detail);
        return false;
    }
    r->sbc_dev = d;
    g_sbc_port = port - 1;
    if (sbc_device_synthetic_enabled() && !sim_bootplan_sbc_roots_made_up()) {
        snprintf(why, whylen, "port %d: the Steel Battalion diagnostics panel's synthetic or replay source is on, so "
                 "the frames would not be the controller's: turn it off in the panel and boot again", port);
        sbc_give_back(r);
        return false;
    }
    fprintf(stderr, "sim-port: port %d: the device '%s' is the Steel Battalion controller%s%s, served by the host "
            "reader%s; its %d inputs:\n", port, dev->device_id, dev->port_path ? " at " : "",
            dev->port_path ? dev->port_path : "", sbc_device_synthetic_enabled() ?
            " from a SYNTHETIC or REPLAY source (made-up device folders: a test)" : "", dev->ninputs);
    sim_sbc_source_log(dev, port, stderr);
    r->lights_on = sim_sbc_port_forwards_lights(r->port);
    sim_sbc_lights_init(&r->lights);
    r->lights_seen = 0;
    fprintf(stderr, "sim-port: port %d: the game's light report %s\n", port, r->lights_on ?
            "is forwarded to the controller unchanged" : "stays with the emulated controller (not a Steel Battalion "
            "port)");
    return true;
}

bool xemu_sim_port_attached(int index)
{
    return index >= 0 && index < 4 && g_sim_ports[index].on;
}

QEMU_BUILD_BUG_ON(SDL_BUTTON_LEFT != 1 || SDL_BUTTON_MIDDLE != 2 || SDL_BUTTON_RIGHT != 3 || SDL_BUTTON_X1 != 4 ||
                  SDL_BUTTON_X2 != 5);
static int mouse_button(int code)
{
    return sim_port_mouse_button(code);
}

static void free_run(SimPortRun *r)
{
    sbc_give_back(r);
    if (r->port) {
        for (int d = 0; d < r->port->ndevices; d++) {
            g_free(r->vals ? r->vals[d] : NULL);
            g_free(r->code ? r->code[d] : NULL);
            if (r->reader && r->reader[d]) {
                sim_evdev_close(r->reader[d]);
            }
        }
    }
    g_free(r->vals);
    g_free(r->code);
    g_free(r->feed);
    g_free(r->reader);
    g_free(r->gone_said);
    sim_merge_free(r->merge);
    if (r->state) {
        g_free((char *)r->state->name);
        g_free(r->state);
    }
    memset(r, 0, sizeof(*r));
}

static bool open_unit(SimPortRun *r, int d, int port, char *why, size_t whylen)
{
    const SimPortDevice *dev = &r->port->devices[d];
    SimEvdevUnit u;
    if (!sim_evdev_find(dev->port_path, &u)) {
        snprintf(why, whylen, "port %d: no unit is plugged in at the port path '%s' (the device '%s')", port,
                 dev->port_path, dev->device_id);
        return false;
    }
    r->reader[d] = sim_evdev_open(&u, why, whylen);
    if (!r->reader[d]) {
        return false;
    }
    if (!sim_evdev_reader_covers(r->reader[d], dev, port, why, whylen)) {
        return false;
    }
    const char *skipped = sim_evdev_reader_skipped(r->reader[d]);
    fprintf(stderr, "sim-port: port %d: the device '%s' reads the unit at %s (%04x:%04x, %s, %d node%s)%s%s\n", port,
            dev->device_id, u.port_path, u.vid & 0xffff, u.pid & 0xffff, u.name[0] ? u.name : "no name", u.nnodes,
            u.nnodes == 1 ? "" : "s", skipped[0] ? "; not opened: " : "", skipped);
    for (int i = 0; i < dev->ninputs; i++) {
        const SimInput *in = &dev->inputs[i];
        int v, lo, hi;
        bool bound = false;
        for (int k = 0; k < dev->nbindings && !bound; k++) {
            bound = dev->bindings[k].source == in;
        }
        if (!bound || in->kind != SIM_IN_AXIS || !sim_evdev_reader_abs(r->reader[d], in->code, &v, &lo, &hi)) {
            continue;
        }
        SimRange use = in->range;
        const char *from = "its descriptor";
        for (int c = 0; c < dev->ncalibrated; c++) {
            if (dev->calibrated[c].input == in) {
                use = dev->calibrated[c].range;
                from = "the calibration";
            }
        }
        fprintf(stderr, "sim-port: port %d:   %s.%s is ABS %d: the unit reports %d..%d (now %d); read as %g/%g/%g "
                "(min/rest/max, %s)\n", port, dev->device_id, in->id, in->code, lo, hi, v, use.min, use.rest,
                use.max, from);
    }
    return true;
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
    r->feed = g_new0(const double *, p->ndevices ? p->ndevices : 1);
    r->reader = g_new0(SimEvdevReader *, p->ndevices ? p->ndevices : 1);
    r->gone_said = g_new0(bool, p->ndevices ? p->ndevices : 1);
    r->sbc_dev = -1;
    for (int d = 0; d < p->ndevices; d++) {
        const SimPortDevice *dev = &p->devices[d];
        r->vals[d] = g_new0(double, dev->ninputs ? dev->ninputs : 1);
        r->code[d] = g_new0(int, dev->ninputs ? dev->ninputs : 1);
        if (!dev->usable) {
            continue;
        }
        if (!sim_bootplan_build_can_read(dev, index + 1, why, whylen)) {
            free_run(r);
            return false;
        }
        if (dev->source == SIM_SRC_SDL_JOYSTICK || dev->source == SIM_SRC_SDL_GAMEPAD) {
            if (!open_unit(r, d, index + 1, why, whylen)) {
                free_run(r);
                return false;
            }
            continue;
        }
        if (dev->source == SIM_SRC_HID_USB) {
            if (!open_sbc(r, d, index + 1, why, whylen)) {
                free_run(r);
                return false;
            }
            continue;
        }
        for (int i = 0; i < dev->ninputs; i++) {
            const SimInput *in = &dev->inputs[i];
            if (dev->source == SIM_SRC_KEYBOARD) {
                char *end = NULL;
                long ev = in->scancode ? strtol(in->scancode, &end, 0) : -1;
                int sc = (in->scancode && end && *end == '\0') ? sim_evdev_to_sdl_scancode((int)ev) : 0;
                if (!sc && sim_bootplan_input_bound(dev, in)) {
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
    for (int d = 0; d < p->ndevices; d++) {
        r->feed[d] = r->vals[d];
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
        if (d == r->sbc_dev) {
            SbcDeviceState st;
            sbc_device_get_state(&st);
            if (!r->sbc_gone && st.status != SBC_DEV_READY) {
                r->sbc_gone = true;
                if (r->lights_on) {
                    uint8_t off[SIM_SBC_LIGHTS_MAX];
                    sim_sbc_lights_off(&r->lights, off);
                    sbc_device_forget_output();
                }
                fprintf(stderr, "sim-port: port %d: the Steel Battalion controller (the device '%s') went away (%s): "
                        "its inputs read as at rest until the next boot\n", (int)(r - g_sim_ports) + 1,
                        dev->device_id, st.status_detail);
            }
            if (r->sbc_gone || st.frames_received == 0) {
                r->feed[d] = NULL;
            } else {
                sim_sbc_source_fill(dev, &st.frame, r->vals[d]);
                r->feed[d] = r->vals[d];
            }
            if (r->lights_on && !r->sbc_gone) {
                sim_sbc_lights_step(&r->lights, &g_light_slot[r - g_sim_ports], &r->lights_seen,
                                    qemu_clock_get_ms(QEMU_CLOCK_REALTIME), sbc_send_lights, NULL);
            }
            continue;
        }
        if (r->reader[d]) {
            sim_evdev_reader_poll(r->reader[d]);
            if (sim_evdev_fill(r->reader[d], dev, r->vals[d])) {
                r->feed[d] = r->vals[d];
            } else {
                r->feed[d] = NULL;
                if (!r->gone_said[d]) {
                    r->gone_said[d] = true;
                    fprintf(stderr, "sim-port: port %d: the unit at %s (the device '%s') went away: its inputs read "
                            "as at rest until the next boot\n", (int)(r - g_sim_ports) + 1, dev->port_path,
                            dev->device_id);
                }
            }
            continue;
        }
        for (int i = 0; i < dev->ninputs; i++) {
            const SimInput *in = &dev->inputs[i];
            int c = r->code[d][i];
            double v = 0;
            if (dev->source == SIM_SRC_KEYBOARD) {
                v = (c && kbd[c]) ? 1.0 : 0.0;
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
    sim_merge_update_dt(r->merge, (const double *const *)r->feed, dt_ms);
    if (r->kind == SIM_PORT_SBC) {
        sim_target_write_sbc_levels(r->merge, &state->sbc.buttons, &state->sbc.previousButtons, state->sbc.axis,
                                    &state->sbc.gearLever, &state->sbc.tunerDial, &state->sbc.toggleSwitches,
                                    r->sbc_dev >= 0 ? sim_sbc_toggle_level_mask(r->port) : 0);
    } else {
        sim_target_write_duke(r->merge, &state->gp.buttons, state->gp.axis);
    }
}
