/*
 * PenguinBox — the plan of a Sim Configurator boot (B-SHIP-1 S-55)
 *
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 *
 * Pure C: no QEMU, no SDL, no glib. tests/xbox/simboot runs it whole.
 *
 * After the Sim Configurator hand-off ends "saved" or "unchanged", PenguinBox
 * loads the saved ports document and decides, per console port (1..4), one of:
 *
 *   ATTACH       the port names a profile this build can serve: it is driven
 *                by that profile (ui/xemu-sim-port.h);
 *   UNCONNECTED  the setup does not name the port, the port is off, or the
 *                port is on with no profile: nothing is connected to it;
 *
 * or it FAILS the whole boot, naming the port, the profile and the reason:
 *
 *   - no ports document: the hand-off saved none (`ports: null`), or the one
 *     it names is not in the store, or the store refused it;
 *   - a ports document with no port on;
 *   - a port whose profile was refused (`profile_lost`), with the refusal;
 *   - a port the document names whose target was refused (it reads as off),
 *     with the refusal;
 *     (each with the ROOT refusal: a refusal for a missing document is
 *     followed to the refusal of that document, as long as there is one, so
 *     the line names the rule that was broken, its code and the document and
 *     place where it was, never only "the profile was refused");
 *   - a target PenguinBox cannot emulate;
 *   - a device this build cannot read (the predicate below: keyboard, mouse
 *     and joystick-class (sdl_joystick, and sdl_gamepad read alike) sources, every key of a keyboard a
 *     binding uses in the key table; a joystick-class device by its port path: every input a
 *     binding uses an axis, button or hat with an evdev code, and the unit
 *     plugged in now, its vid:pid the device's, reporting those codes, as
 *     ui/xemu-sim-evdev.h checks it);
 *   - a setup this build would accept and then get wrong (each named for a
 *     tester, with what to change; the checks run whatever the device
 *     predicate): the same device model twice on one port with no
 *     `instance`; a binding on a mouse code the port's mouse source does not
 *     read (ui/xemu-sim-port.h); a split other than an axis across two axes;
 *     an axis on a pressure-sensitive button with no threshold (the report
 *     carries no level yet); two keyboard-class devices on the driven ports
 *     that bind one key (every keyboard is read as one);
 *   - no port at all would be attached.
 *
 * One failing port fails the whole boot: a tester is never handed half a setup.
 *
 * This lane's readings, where the owner's rulings are silent (each is the
 * safer one):
 *   - a port that is on with no profile is left UNCONNECTED (the app allows a
 *     port with no profile; nothing is connected rather than a controller that
 *     does nothing, or PenguinBox's own mapping);
 *   - an empty ports id from the hand-off FAILS: the first ports document by
 *     id (the reader's NULL) is never taken in its place;
 *   - a setup in which no port would be attached FAILS (a boot with no working
 *     controller), even when every port it names is on;
 *   - a device on a port is checked even when the profile binds none of its
 *     inputs (the device sits on the console port, so it must be readable).
 *
 * Strict input (the owner's S-55): on a Sim Configurator boot PenguinBox
 * honours ONLY these console ports. The input layer's side is in
 * ui/xemu-input.h (xemu_input_strict_begin / _end).
 */
#ifndef XEMU_SIM_BOOTPLAN_H
#define XEMU_SIM_BOOTPLAN_H

#include <stdbool.h>
#include <stddef.h>

#include "xemu-sim-profile-reader.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SimPlanAction {
    SIM_PLAN_UNCONNECTED,
    SIM_PLAN_ATTACH,
} SimPlanAction;

typedef struct SimBootPlan {
    bool ok;
    SimPlanAction action[SIM_PROFILE_MAX_PORTS];
    int nattach;
    int nfailed;
    char line[SIM_PROFILE_MAX_PORTS][512];
    char why[1024];
} SimBootPlan;

typedef bool (*SimDeviceReadable)(const SimPortDevice *dev, int port, char *why, size_t whylen);

bool sim_bootplan_input_bound(const SimPortDevice *dev, const SimInput *in);

bool sim_bootplan_build_can_read(const SimPortDevice *dev, int port, char *why, size_t whylen);
void sim_bootplan_set_sbc_roots(const char *sys_usb, const char *dev_usb);
bool sim_bootplan_sbc_roots_made_up(void);

bool sim_bootplan_target_served(const char *target);

bool sim_bootplan_decide(const char *ports_id, int on, const SimPort ports[SIM_PROFILE_MAX_PORTS],
                         const SimRefusal *const *refusals, int nrefusals, SimDeviceReadable readable,
                         SimBootPlan *out);

bool sim_bootplan_from_set(const SimProfileSet *set, const char *ports_id, SimPort ports[SIM_PROFILE_MAX_PORTS],
                           SimDeviceReadable readable, SimBootPlan *out);

char *sim_bootplan_builtin_dir(const char *exe_path, char *looked, size_t lookedlen);

#ifdef __cplusplus
}
#endif

#endif
