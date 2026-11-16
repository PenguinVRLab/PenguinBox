/*
 * PenguinBox — the plan of a Sim Configurator boot (see xemu-sim-bootplan.h)
 *
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "xemu-sim-bootplan.h"
#include "xemu-sim-evdev.h"
#include "xemu-sim-keys.h"
#include "xemu-sim-port.h"
#include "xemu-sim-sbc-source.h"

bool sim_bootplan_target_served(const char *target)
{
    return target && (!strcmp(target, "steel_battalion") || !strcmp(target, "xbox_duke"));
}

static SimSbcRoots g_sbc_roots;

void sim_bootplan_set_sbc_roots(const char *sys_usb, const char *dev_usb)
{
    g_sbc_roots.sys_usb = sys_usb;
    g_sbc_roots.dev_usb = dev_usb;
}

bool sim_bootplan_sbc_roots_made_up(void)
{
    return g_sbc_roots.sys_usb || g_sbc_roots.dev_usb;
}

bool sim_bootplan_input_bound(const SimPortDevice *dev, const SimInput *in)
{
    for (int k = 0; k < dev->nbindings; k++) {
        if (dev->bindings[k].source == in || dev->bindings[k].recentre == in) {
            return true;
        }
    }
    return false;
}

bool sim_bootplan_build_can_read(const SimPortDevice *dev, int port, char *why, size_t whylen)
{
    if (!dev->usable) {
        return true;
    }
    if (dev->source == SIM_SRC_MOUSE) {
        return true;
    }
    if (dev->source == SIM_SRC_SDL_JOYSTICK || dev->source == SIM_SRC_SDL_GAMEPAD) {
        return sim_evdev_check_device(dev, port, why, whylen);
    }
    if (dev->source == SIM_SRC_HID_USB && sim_sbc_is_controller(dev)) {
        return sim_sbc_plan_check(dev, port, &g_sbc_roots, why, whylen);
    }
    if (dev->source != SIM_SRC_KEYBOARD) {
        snprintf(why, whylen, "port %d: the device '%s' is read by a later build of PenguinBox (keyboards, mice, "
                 "joystick-class devices by port path and the real Steel Battalion controller are read today)", port,
                 dev->device_id);
        return false;
    }
    for (int i = 0; i < dev->ninputs; i++) {
        const SimInput *in = &dev->inputs[i];
        if (!sim_bootplan_input_bound(dev, in)) {
            continue;
        }
        char *end = NULL;
        long ev = in->scancode ? strtol(in->scancode, &end, 0) : -1;
        if (!(in->scancode && end && *end == '\0' && end != in->scancode && sim_evdev_to_sdl_scancode((int)ev))) {
            snprintf(why, whylen, "port %d: the key '%s' of '%s' (scancode %s) is not one PenguinBox knows", port,
                     in->id, dev->device_id, in->scancode ? in->scancode : "none");
            return false;
        }
    }
    return true;
}

static const SimRefusal *find_refusal(const SimRefusal *const *refusals, int n, const char *prefix)
{
    size_t len = strlen(prefix);
    for (int i = 0; i < n; i++) {
        const SimRefusal *r = refusals[i];
        if (r && r->where && !strncmp(r->where, prefix, len) && (r->where[len] == '\0' || r->where[len] == '.')) {
            return r;
        }
    }
    return NULL;
}

static const SimRefusal *port_refusal(const SimRefusal *const *refusals, int n, const char *ports_id, int port)
{
    char at[160];
    snprintf(at, sizeof(at), "ports:%s.ports.%d", ports_id, port);
    const SimRefusal *r = find_refusal(refusals, n, at);
    if (!r) {
        snprintf(at, sizeof(at), "ports:%s.assign.%d", ports_id, port);
        r = find_refusal(refusals, n, at);
    }
    return r;
}

static bool missing_document(const char *why, char *kind, size_t kindlen, char *id, size_t idlen)
{
    if (!why || strncmp(why, "no ", 3)) {
        return false;
    }
    const char *k = why + 3;
    const char *sp = strchr(k, ' ');
    if (!sp || sp == k || (size_t)(sp - k) >= kindlen || sp[1] != '\'') {
        return false;
    }
    const char *s = sp + 2;
    const char *e = strchr(s, '\'');
    if (!e || e == s || (size_t)(e - s) >= idlen) {
        return false;
    }
    snprintf(kind, kindlen, "%.*s", (int)(sp - k), k);
    snprintf(id, idlen, "%.*s", (int)(e - s), s);
    return true;
}

static void root_refusal(const SimRefusal *const *refusals, int n, const SimRefusal *r, const char *kind,
                         const char *id, char *out, size_t outlen)
{
    char k[32], i[160], nk[32], ni[160], at[200];
    snprintf(k, sizeof(k), "%s", kind);
    snprintf(i, sizeof(i), "%s", id);
    for (int hop = 0; hop < 8 && r->code && !strcmp(r->code, "UNRESOLVED_REF"); hop++) {
        if (!missing_document(r->why, nk, sizeof(nk), ni, sizeof(ni))) {
            break;
        }
        snprintf(at, sizeof(at), "%s:%s", nk, ni);
        const SimRefusal *deeper = find_refusal(refusals, n, at);
        if (!deeper || deeper == r) {
            break;
        }
        r = deeper;
        snprintf(k, sizeof(k), "%s", nk);
        snprintf(i, sizeof(i), "%s", ni);
    }
    const char *place = "";
    size_t plen = strlen(k) + 1 + strlen(i);
    if (r->where && strlen(r->where) >= plen && r->where[strlen(k)] == ':') {
        place = r->where + plen;
        place += *place == '.';
    }
    snprintf(out, outlen, "the %s '%s'%s%s: %s [%s]", strcmp(k, "ports") ? k : "ports document", i,
             *place ? ", at " : "", place, r->why ? r->why : "", r->code ? r->code : "");
}

static void append(char *dst, size_t len, const char *s)
{
    size_t have = strlen(dst);
    if (have + 1 < len) {
        snprintf(dst + have, len - have, "%s", s);
    }
}

static bool port_driven(const SimPort *p)
{
    return p->target && sim_bootplan_target_served(p->target) && p->profile;
}

static bool same_unit(const SimPortDevice *a, const SimPortDevice *b)
{
    bool paths = a->port_path && b->port_path ? !strcmp(a->port_path, b->port_path) : a->port_path == b->port_path;
    return !strcmp(a->device_id, b->device_id) && a->key && b->key && !strcmp(a->key, b->key) && paths;
}

static long key_number(const SimInput *in)
{
    char *end = NULL;
    long v = in->scancode ? strtol(in->scancode, &end, 0) : -1;
    return in->scancode && end && end != in->scancode && *end == '\0' ? v : -1;
}

static bool keyboards_collide(const SimPort ports[SIM_PROFILE_MAX_PORTS], int pi, char *why, size_t whylen)
{
    const SimPort *p = &ports[pi];
    for (int d = 0; d < p->ndevices; d++) {
        const SimPortDevice *a = &p->devices[d];
        if (a->source != SIM_SRC_KEYBOARD) {
            continue;
        }
        for (int qi = 0; qi <= pi; qi++) {
            const SimPort *q = &ports[qi];
            if (qi < pi && !port_driven(q)) {
                continue;
            }
            for (int e = 0; e < (qi == pi ? d : q->ndevices); e++) {
                const SimPortDevice *b = &q->devices[e];
                if (b->source != SIM_SRC_KEYBOARD || same_unit(a, b)) {
                    continue;
                }
                for (int i = 0; i < a->ninputs; i++) {
                    long ka = key_number(&a->inputs[i]);
                    if (ka < 0 || !sim_bootplan_input_bound(a, &a->inputs[i])) {
                        continue;
                    }
                    for (int j = 0; j < b->ninputs; j++) {
                        if (key_number(&b->inputs[j]) != ka || !sim_bootplan_input_bound(b, &b->inputs[j])) {
                            continue;
                        }
                        snprintf(why, whylen, "the keyboard '%s' (its key '%s', on port %d) and the keyboard '%s' "
                                 "(its key '%s', on port %d) both bind the key with scancode %ld. PenguinBox reads "
                                 "every keyboard and keyboard-style button box as one keyboard for now, so one press "
                                 "would work both: bind a different key on one of them in the Sim Configurator",
                                 b->device_id, b->inputs[j].id, qi + 1, a->device_id, a->inputs[i].id, pi + 1, ka);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

static bool mouse_unread(const SimPort *p, char *why, size_t whylen)
{
    for (int d = 0; d < p->ndevices; d++) {
        const SimPortDevice *dev = &p->devices[d];
        if (dev->source != SIM_SRC_MOUSE) {
            continue;
        }
        for (int i = 0; i < dev->ninputs; i++) {
            const SimInput *in = &dev->inputs[i];
            bool motion = in->kind == SIM_IN_RELATIVE;
            if (!sim_bootplan_input_bound(dev, in) ||
                (motion ? sim_port_mouse_motion(in->code) : sim_port_mouse_button(in->code) != 0)) {
                continue;
            }
            snprintf(why, whylen, "the mouse '%s' input '%s' (%s code %d) is not one this build of PenguinBox reads "
                     "(it reads the left, right and middle buttons, the two thumb buttons, moving the mouse, the wheel and the "
                     "sideways wheel), "
                     "so that binding would do nothing: bind another mouse input in the Sim Configurator",
                     dev->device_id, in->id, motion ? "motion" : "button", in->code);
            return true;
        }
    }
    return false;
}

static const char *input_kind_word(SimInputKind k)
{
    switch (k) {
    case SIM_IN_AXIS: return "axis";
    case SIM_IN_BUTTON: return "button";
    case SIM_IN_HAT: return "hat";
    case SIM_IN_DETENT: return "lever";
    case SIM_IN_RELATIVE: return "motion";
    }
    return "input";
}

static bool split_uncomputed(const SimPort *p, char *why, size_t whylen)
{
    for (int d = 0; d < p->ndevices; d++) {
        const SimPortDevice *dev = &p->devices[d];
        for (int k = 0; k < dev->nbindings; k++) {
            const SimBinding *b = &dev->bindings[k];
            if ((b->policy != SIM_POLICY_SPLIT && !b->target2) || !b->source || !b->target) {
                continue;
            }
            if (b->source->kind == SIM_IN_AXIS && b->target->kind == SIM_IN_AXIS && b->target2 &&
                b->target2->kind == SIM_IN_AXIS) {
                continue;
            }
            snprintf(why, whylen, "the device '%s' splits its %s '%s' across '%s' and '%s'. PenguinBox splits only an "
                     "axis (a stick, a wheel or a pedal) across two axes (two triggers, two pedals or two stick "
                     "directions): bind each of the two controls on its own in the Sim Configurator", dev->device_id,
                     input_kind_word(b->source->kind), b->source->id, b->target->id,
                     b->target2 ? b->target2->id : "nothing");
            return true;
        }
    }
    return false;
}

static bool analog_level_unsent(const SimPort *p, char *why, size_t whylen)
{
    for (int d = 0; d < p->ndevices; d++) {
        const SimPortDevice *dev = &p->devices[d];
        for (int k = 0; k < dev->nbindings; k++) {
            const SimBinding *b = &dev->bindings[k];
            if (!b->source || !b->target || b->source->kind != SIM_IN_AXIS || b->target->kind != SIM_IN_BUTTON ||
                !b->target->analog ||
                b->has_threshold) {
                continue;
            }
            snprintf(why, whylen, "the device '%s' drives the pressure-sensitive button '%s' from its axis '%s' with "
                     "no threshold. PenguinBox cannot send how hard a button is pressed yet, so the game would see "
                     "'%s' fully pressed whenever the axis moves (for some settings even at rest): give the binding a "
                     "threshold in the Sim Configurator, or bind a button to '%s'",
                     dev->device_id, b->target->id, b->source->id, b->target->id, b->target->id);
            return true;
        }
    }
    return false;
}

static bool twin_unlabelled(const SimPort *p, char *why, size_t whylen)
{
    for (int d = 0; d < p->ndevices; d++) {
        const SimPortDevice *a = &p->devices[d];
        for (int e = 0; e < d; e++) {
            const SimPortDevice *b = &p->devices[e];
            if (strcmp(a->device_id, b->device_id) || !a->key || !b->key || strcmp(a->key, a->device_id) ||
                strcmp(b->key, b->device_id)) {
                continue;
            }
            snprintf(why, whylen, "the device '%s' is on this port twice with no instance name, so PenguinBox cannot "
                     "tell which unit its bindings are for (both would drive them): give each one its own instance "
                     "name in the Sim Configurator, or remove one", a->device_id);
            return true;
        }
    }
    return false;
}

static bool sim_bootplan_setup_ok(const SimPort ports[SIM_PROFILE_MAX_PORTS], int pi, char *why, size_t whylen)
{
    if (twin_unlabelled(&ports[pi], why, whylen)) {
        return false;
    }
    if (mouse_unread(&ports[pi], why, whylen)) {
        return false;
    }
    if (split_uncomputed(&ports[pi], why, whylen)) {
        return false;
    }
    if (analog_level_unsent(&ports[pi], why, whylen)) {
        return false;
    }
    if (keyboards_collide(ports, pi, why, whylen)) {
        return false;
    }
    return true;
}

bool sim_bootplan_decide(const char *ports_id, int on, const SimPort ports[SIM_PROFILE_MAX_PORTS],
                         const SimRefusal *const *refusals, int nrefusals, SimDeviceReadable readable,
                         SimBootPlan *out)
{
    memset(out, 0, sizeof(*out));
    if (!readable) {
        readable = sim_bootplan_build_can_read;
    }
    if (!ports_id || !*ports_id) {
        snprintf(out->why, sizeof(out->why), "the Sim Configurator saved no ports document (ports: null), so there "
                 "is no setup to apply");
        return false;
    }
    if (on < 0) {
        char at[160];
        snprintf(at, sizeof(at), "ports:%s", ports_id);
        const SimRefusal *r = find_refusal(refusals, nrefusals, at);
        if (r) {
            snprintf(out->why, sizeof(out->why), "the ports document '%s' was refused: %s %s: %s", ports_id, r->code,
                     r->where, r->why);
        } else {
            snprintf(out->why, sizeof(out->why), "the ports document '%s' is not in the profile store", ports_id);
        }
        return false;
    }
    if (on == 0) {
        snprintf(out->why, sizeof(out->why), "the ports document '%s' has no console port on", ports_id);
        return false;
    }

    for (int i = 0; i < SIM_PROFILE_MAX_PORTS; i++) {
        const SimPort *p = &ports[i];
        const int n = i + 1;
        char *line = out->line[i];
        const size_t ll = sizeof(out->line[i]);
        bool fail = false;
        out->action[i] = SIM_PLAN_UNCONNECTED;

        if (!p->target) {
            const SimRefusal *r = port_refusal(refusals, nrefusals, ports_id, n);
            if (r) {
                char root[400];
                root_refusal(refusals, nrefusals, r, "ports", ports_id, root, sizeof(root));
                snprintf(line, ll, "port %d: refused: %s; fix it in the Sim Configurator and save the setup again", n,
                         root);
                fail = true;
            } else {
                snprintf(line, ll, "port %d: not in the setup, or off: nothing connected", n);
            }
        } else if (!sim_bootplan_target_served(p->target)) {
            snprintf(line, ll, "port %d (profile %s): PenguinBox cannot emulate the target '%s'", n,
                     p->profile ? p->profile : p->profile_lost ? p->profile_lost : "none", p->target);
            fail = true;
        } else if (p->profile_lost) {
            const SimRefusal *r = port_refusal(refusals, nrefusals, ports_id, n);
            const char *rkind = "ports", *rid = ports_id;
            if (!r) {
                char at[160];
                snprintf(at, sizeof(at), "profile:%s", p->profile_lost);
                r = find_refusal(refusals, nrefusals, at);
                rkind = "profile";
                rid = p->profile_lost;
            }
            if (r) {
                char root[400];
                root_refusal(refusals, nrefusals, r, rkind, rid, root, sizeof(root));
                snprintf(line, ll, "port %d (profile %s): the profile was refused: %s; fix it in the Sim Configurator "
                         "and save the setup again", n, p->profile_lost, root);
            } else {
                snprintf(line, ll, "port %d (profile %s): the profile was refused", n, p->profile_lost);
            }
            fail = true;
        } else if (!p->profile) {
            snprintf(line, ll, "port %d (%s): on with no profile: nothing connected", n, p->target);
        } else {
            char dwhy[220] = "";
            for (int d = 0; d < p->ndevices && !fail; d++) {
                if (!readable(&p->devices[d], n, dwhy, sizeof(dwhy))) {
                    snprintf(line, ll, "port %d (profile %s): %s", n, p->profile, dwhy);
                    fail = true;
                }
            }
            char swhy[448];
            if (!fail && !sim_bootplan_setup_ok(ports, i, swhy, sizeof(swhy))) {
                snprintf(line, ll, "port %d (profile %s): %s", n, p->profile, swhy);
                fail = true;
            }
            if (!fail) {
                out->action[i] = SIM_PLAN_ATTACH;
                out->nattach++;
                snprintf(line, ll, "port %d: the profile '%s' (%s, %d devices)", n, p->profile, p->target,
                         p->ndevices);
            }
        }
#ifdef SIM_BOOTPLAN_DRILL_LET_THROUGH
        if (fail) {
            fail = false;
            if (p->target) {
                out->action[i] = SIM_PLAN_ATTACH;
                out->nattach++;
            }
        }
#endif
        if (fail) {
            out->nfailed++;
            append(out->why, sizeof(out->why), out->nfailed > 1 ? "; " : "");
            append(out->why, sizeof(out->why), line);
        }
    }
    if (out->nfailed) {
        for (int i = 0; i < SIM_PROFILE_MAX_PORTS; i++) {
            out->action[i] = SIM_PLAN_UNCONNECTED;
        }
        out->nattach = 0;
        return false;
    }
    if (out->nattach == 0) {
        snprintf(out->why, sizeof(out->why), "no console port of the ports document '%s' has a profile, so no "
                 "controller would be connected", ports_id);
        return false;
    }
    snprintf(out->why, sizeof(out->why), "the ports document '%s': %d console port%s driven by the Sim Configurator",
             ports_id, out->nattach, out->nattach == 1 ? "" : "s");
    out->ok = true;
    return true;
}

bool sim_bootplan_from_set(const SimProfileSet *set, const char *ports_id, SimPort ports[SIM_PROFILE_MAX_PORTS],
                           SimDeviceReadable readable, SimBootPlan *out)
{
    memset(ports, 0, sizeof(SimPort) * SIM_PROFILE_MAX_PORTS);
    int on = (ports_id && *ports_id) ? sim_profile_set_ports(set, ports_id, ports) : -1;
    int n = sim_profile_set_nrefused(set);
    const SimRefusal **refs = n > 0 ? calloc((size_t)n, sizeof(*refs)) : NULL;
    for (int i = 0; refs && i < n; i++) {
        refs[i] = sim_profile_set_refusal(set, i);
    }
    bool ok = sim_bootplan_decide(ports_id, on, ports, refs, refs ? n : 0, readable, out);
    free(refs);
    return ok;
}

static bool has_marker(const char *dir)
{
    char f[4096 + 64];
    struct stat st;
    int n = snprintf(f, sizeof(f), "%s/target_steel_battalion.yaml", dir);
    return n > 0 && (size_t)n < sizeof(f) && stat(f, &st) == 0 && S_ISREG(st.st_mode);
}

char *sim_bootplan_builtin_dir(const char *exe_path, char *looked, size_t lookedlen)
{
    if (looked && lookedlen) {
        looked[0] = '\0';
    }
    if (!exe_path || !*exe_path) {
        if (looked && lookedlen) {
            snprintf(looked, lookedlen, "(the executable's path is unknown)");
        }
        return NULL;
    }
    const char *slash = strrchr(exe_path, '/');
    const char *base = slash ? exe_path : ".";
    int blen = slash ? (int)(slash - exe_path) : 1;
    static const char *const rel[] = { "/../share/penguinbox/sim-profiles", "/../data/sim-profiles" };
    for (size_t i = 0; i < sizeof(rel) / sizeof(rel[0]); i++) {
        char dir[4096];
        snprintf(dir, sizeof(dir), "%.*s%s", blen, base, rel[i]);
        if (has_marker(dir)) {
            return strdup(dir);
        }
        if (looked && lookedlen) {
            append(looked, lookedlen, i ? ", " : "");
            append(looked, lookedlen, dir);
        }
    }
    return NULL;
}
