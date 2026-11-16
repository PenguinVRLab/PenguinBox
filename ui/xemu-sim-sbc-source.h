/*
 * PenguinBox — the real Steel Battalion controller as a source of a Sim Configurator port
 *
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 *
 * A Sim Configurator setup may put the real Steel Battalion controller (USB 0a7b:d000) on a console port: its device
 * document has `source: hid_usb`, `match: { vid: 0x0a7b, pid: 0xd000 }` and the controls of the controller's report
 * by id (the shipped document is data/sim-profiles/device_steel_battalion_controller.yaml). The controller has no
 * kernel input node; PenguinBox's host reader (ui/xemu-sbc-device.h) reads it, one owner of the device handle, and
 * publishes each report as an SbcFrame (the 26 wire bytes and their decoded view). This file turns a frame into the
 * values of the device document's inputs, BY ID (a document's `code:` values are the Sim Configurator's own reader's
 * numbering and are never read here), for the merge layer (ui/xemu-sim-merge.h), which applies the setup's bindings.
 *
 * THE MAPPING, each of the 49 ids to the SbcFrame field it is read from (SIM_SBC_SOURCE_IDS below):
 *   - the 34 momentary buttons main_weapon .. sight_change: `buttons` bit 0 .. 33 (the report's bit order, the same
 *     as the emulated controller's SBC_BUTTON_* bits), the LEVEL: 1 held, 0 not;
 *   - the five latching toggles filt_control_system, oxygen_supply_system, fuel_flow_rate, buffer_material,
 *     vt_location_measurement: `toggle_levels` bit 2 .. 6 (report bit 34 .. 38), the switch's TRUE PHYSICAL LEVEL
 *     (1 on), never an edge;
 *   - aiming_x, aiming_y: `aiming_x`, `aiming_y`, the full 16-bit wire value 0 .. 65535 (rest 32768);
 *   - rotation_lever, sight_change_x, sight_change_y: the signed 16-bit wire value -32768 .. 32767;
 *   - left_pedal, middle_pedal, right_pedal: the unsigned 16-bit wire value 0 .. 65535;
 *   - tuner_dial: `tuner_dial` 0 .. 15, as a lever reading (sim_merge_detent_int);
 *   - gear_lever: `gear_lever`, the wire byte itself, as a lever reading: 254 R, 255 N, 1 .. 5, and 0, which the real
 *     lever sends between gears and which is no position, passes on as an unlisted reading (SIM_DETENT_UNLISTED + 0),
 *     so a lever-to-lever binding hands the game the 0 unchanged.
 * A value is the raw reading; nothing is normalised or calibrated here (that is the merge layer's).
 *
 * THE DOCUMENT AGAINST THE DECODER (sim_sbc_source_check): the device document must name exactly these 49 ids, each
 * with the kind above, each axis with the range of its wire field (min and max), each lever with integer positions in
 * its field's range. An id the frame does not carry, a field no input names, another kind or another range is a
 * disagreement between the document and this reader: the boot is refused naming the first, never a control that
 * silently does nothing.
 *
 * WHERE THE CONTROLLER IS (sim_sbc_locate; nothing is opened): the USB devices folder (/sys/bus/usb/devices) is read
 * for every device whose idVendor and idProduct are the controller's. The setup names the controller's place by its
 * port path, udev's ID_PATH of the controller's USB interface 0 ("pci-0000:00:14.0-usb-0:2:1.0"), as the Sim
 * Configurator writes it; it is rebuilt here the way udev's path_id builds it: the nearest PCI device above the
 * controller ("pci-<its address>", or "platform-<name>" for a host controller on the platform bus), then
 * "usb-0:<the port chain>:<configuration>.0". udev's USB-revision form "-usbv<N>-" reads as "-usb-" on both sides.
 * Whether this user may open it: read and write access to its usbfs node (/dev/bus/usb/<bus>/<device>), which the
 * installer's udev rule 70-penguinvrlabs-sim-devices.rules grants. The two folders are parameters, so a test can point
 * them at a made-up tree.
 *
 * The plan's side is in this header as static inline functions, because the boot plan (ui/xemu-sim-bootplan.c) is
 * built alone by its tests with a fixed list of objects and must not need another one; the attach's side is in
 * ui/xemu-sim-sbc-source.c.
 *
 * THE PANEL LIGHTS (SimSbcLights): the game's light report to the emulated controller (the 22-byte output report,
 * hw/xbox/xid-steel-battalion.c) is forwarded to the real controller UNCHANGED, the game's own bytes: only when it
 * differs from the last one sent, never two sends closer than SIM_SBC_LIGHTS_MIN_MS, the newest report winning while
 * one waits. At the end of the setup the lights are put out: the last report's two header bytes and every light
 * nibble 0. Pure C, no clock of its own: the caller passes the time.
 */
#ifndef XEMU_SIM_SBC_SOURCE_H
#define XEMU_SIM_SBC_SOURCE_H

#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "xemu-sbc-device.h"
#include "xemu-sim-profile-reader.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SimSbcField {
    SIM_SBC_F_BUTTON,
    SIM_SBC_F_TOGGLE,
    SIM_SBC_F_AIMING_X,
    SIM_SBC_F_AIMING_Y,
    SIM_SBC_F_ROTATION,
    SIM_SBC_F_SIGHT_X,
    SIM_SBC_F_SIGHT_Y,
    SIM_SBC_F_LEFT_PEDAL,
    SIM_SBC_F_MIDDLE_PEDAL,
    SIM_SBC_F_RIGHT_PEDAL,
    SIM_SBC_F_TUNER,
    SIM_SBC_F_GEAR,
} SimSbcField;

#define SIM_SBC_SOURCE_IDS(M) \
    M(main_weapon, SIM_SBC_F_BUTTON, 0, SIM_IN_BUTTON, 0, 1) \
    M(sub_weapon, SIM_SBC_F_BUTTON, 1, SIM_IN_BUTTON, 0, 1) \
    M(lock_on, SIM_SBC_F_BUTTON, 2, SIM_IN_BUTTON, 0, 1) \
    M(eject, SIM_SBC_F_BUTTON, 3, SIM_IN_BUTTON, 0, 1) \
    M(cockpit_hatch, SIM_SBC_F_BUTTON, 4, SIM_IN_BUTTON, 0, 1) \
    M(ignition, SIM_SBC_F_BUTTON, 5, SIM_IN_BUTTON, 0, 1) \
    M(start, SIM_SBC_F_BUTTON, 6, SIM_IN_BUTTON, 0, 1) \
    M(open_close, SIM_SBC_F_BUTTON, 7, SIM_IN_BUTTON, 0, 1) \
    M(map_zoom_in_out, SIM_SBC_F_BUTTON, 8, SIM_IN_BUTTON, 0, 1) \
    M(mode_select, SIM_SBC_F_BUTTON, 9, SIM_IN_BUTTON, 0, 1) \
    M(sub_monitor_mode_select, SIM_SBC_F_BUTTON, 10, SIM_IN_BUTTON, 0, 1) \
    M(zoom_in, SIM_SBC_F_BUTTON, 11, SIM_IN_BUTTON, 0, 1) \
    M(zoom_out, SIM_SBC_F_BUTTON, 12, SIM_IN_BUTTON, 0, 1) \
    M(fss, SIM_SBC_F_BUTTON, 13, SIM_IN_BUTTON, 0, 1) \
    M(manipulator, SIM_SBC_F_BUTTON, 14, SIM_IN_BUTTON, 0, 1) \
    M(line_color_change, SIM_SBC_F_BUTTON, 15, SIM_IN_BUTTON, 0, 1) \
    M(washing, SIM_SBC_F_BUTTON, 16, SIM_IN_BUTTON, 0, 1) \
    M(extinguisher, SIM_SBC_F_BUTTON, 17, SIM_IN_BUTTON, 0, 1) \
    M(chaff, SIM_SBC_F_BUTTON, 18, SIM_IN_BUTTON, 0, 1) \
    M(tank_detach, SIM_SBC_F_BUTTON, 19, SIM_IN_BUTTON, 0, 1) \
    M(override, SIM_SBC_F_BUTTON, 20, SIM_IN_BUTTON, 0, 1) \
    M(night_scope, SIM_SBC_F_BUTTON, 21, SIM_IN_BUTTON, 0, 1) \
    M(func1, SIM_SBC_F_BUTTON, 22, SIM_IN_BUTTON, 0, 1) \
    M(func2, SIM_SBC_F_BUTTON, 23, SIM_IN_BUTTON, 0, 1) \
    M(func3, SIM_SBC_F_BUTTON, 24, SIM_IN_BUTTON, 0, 1) \
    M(main_weapon_control, SIM_SBC_F_BUTTON, 25, SIM_IN_BUTTON, 0, 1) \
    M(sub_weapon_control, SIM_SBC_F_BUTTON, 26, SIM_IN_BUTTON, 0, 1) \
    M(magazine_change, SIM_SBC_F_BUTTON, 27, SIM_IN_BUTTON, 0, 1) \
    M(com1, SIM_SBC_F_BUTTON, 28, SIM_IN_BUTTON, 0, 1) \
    M(com2, SIM_SBC_F_BUTTON, 29, SIM_IN_BUTTON, 0, 1) \
    M(com3, SIM_SBC_F_BUTTON, 30, SIM_IN_BUTTON, 0, 1) \
    M(com4, SIM_SBC_F_BUTTON, 31, SIM_IN_BUTTON, 0, 1) \
    M(com5, SIM_SBC_F_BUTTON, 32, SIM_IN_BUTTON, 0, 1) \
    M(sight_change, SIM_SBC_F_BUTTON, 33, SIM_IN_BUTTON, 0, 1) \
    M(filt_control_system, SIM_SBC_F_TOGGLE, 34, SIM_IN_BUTTON, 0, 1) \
    M(oxygen_supply_system, SIM_SBC_F_TOGGLE, 35, SIM_IN_BUTTON, 0, 1) \
    M(fuel_flow_rate, SIM_SBC_F_TOGGLE, 36, SIM_IN_BUTTON, 0, 1) \
    M(buffer_material, SIM_SBC_F_TOGGLE, 37, SIM_IN_BUTTON, 0, 1) \
    M(vt_location_measurement, SIM_SBC_F_TOGGLE, 38, SIM_IN_BUTTON, 0, 1) \
    M(aiming_x, SIM_SBC_F_AIMING_X, 0, SIM_IN_AXIS, 0, 65535) \
    M(aiming_y, SIM_SBC_F_AIMING_Y, 0, SIM_IN_AXIS, 0, 65535) \
    M(rotation_lever, SIM_SBC_F_ROTATION, 0, SIM_IN_AXIS, -32768, 32767) \
    M(sight_change_x, SIM_SBC_F_SIGHT_X, 0, SIM_IN_AXIS, -32768, 32767) \
    M(sight_change_y, SIM_SBC_F_SIGHT_Y, 0, SIM_IN_AXIS, -32768, 32767) \
    M(left_pedal, SIM_SBC_F_LEFT_PEDAL, 0, SIM_IN_AXIS, 0, 65535) \
    M(middle_pedal, SIM_SBC_F_MIDDLE_PEDAL, 0, SIM_IN_AXIS, 0, 65535) \
    M(right_pedal, SIM_SBC_F_RIGHT_PEDAL, 0, SIM_IN_AXIS, 0, 65535) \
    M(tuner_dial, SIM_SBC_F_TUNER, 0, SIM_IN_DETENT, 0, 15) \
    M(gear_lever, SIM_SBC_F_GEAR, 0, SIM_IN_DETENT, 0, 255)

#define SIM_SBC_SOURCE_NIDS 49

typedef struct SimSbcId {
    const char *id;
    SimSbcField field;
    int arg;
    SimInputKind kind;
    double min, max;
} SimSbcId;

static inline const SimSbcId *sim_sbc_ids(void)
{
#define SIM_SBC_ID_ROW(id, field, arg, kind, min, max) { #id, field, arg, kind, min, max },
    static const SimSbcId ids[SIM_SBC_SOURCE_NIDS] = { SIM_SBC_SOURCE_IDS(SIM_SBC_ID_ROW) };
#undef SIM_SBC_ID_ROW
    return ids;
}

static inline int sim_sbc_id_index(const char *id)
{
    const SimSbcId *ids = sim_sbc_ids();
    for (int i = 0; id && i < SIM_SBC_SOURCE_NIDS; i++) {
        if (!strcmp(ids[i].id, id)) {
            return i;
        }
    }
    return -1;
}

static inline bool sim_sbc_is_controller(const SimPortDevice *dev)
{
    if (!dev || dev->source != SIM_SRC_HID_USB) {
        return false;
    }
    if (dev->has_match && dev->vid == SBC_USB_VENDOR_ID && dev->pid == SBC_USB_PRODUCT_ID) {
        return true;
    }
    for (int i = 0; dev->match_also && i < dev->nmatch_also; i++) {
        if (dev->match_also[2 * i] == SBC_USB_VENDOR_ID && dev->match_also[2 * i + 1] == SBC_USB_PRODUCT_ID) {
            return true;
        }
    }
    return false;
}

static inline uint8_t sim_sbc_toggle_level_mask(const SimPort *p)
{
    const SimSbcId *ids = sim_sbc_ids();
    uint8_t mask = 0;
    for (int d = 0; p && d < p->ndevices; d++) {
        const SimPortDevice *dev = &p->devices[d];
        if (!sim_sbc_is_controller(dev)) {
            continue;
        }
        for (int k = 0; k < dev->nbindings; k++) {
            const SimBinding *b = &dev->bindings[k];
            int s = b->source ? sim_sbc_id_index(b->source->id) : -1;
            int t = b->target && !b->target2 ? sim_sbc_id_index(b->target->id) : -1;
            if (s >= 0 && t >= 0 && ids[s].field == SIM_SBC_F_TOGGLE && ids[t].field == SIM_SBC_F_TOGGLE) {
                mask |= (uint8_t)(1u << (ids[t].arg - 32));
            }
        }
    }
    return mask;
}

static inline const char *sim_sbc_kind_name(SimInputKind k)
{
    return k == SIM_IN_AXIS ? "axis" : k == SIM_IN_BUTTON ? "button" : k == SIM_IN_HAT ? "hat" :
           k == SIM_IN_DETENT ? "detent" : "relative";
}

static inline bool sim_sbc_source_check(const SimPortDevice *dev, int port, char *why, size_t whylen)
{
    const SimSbcId *ids = sim_sbc_ids();
    for (int k = 0; k < SIM_SBC_SOURCE_NIDS; k++) {
        const SimSbcId *t = &ids[k];
        const SimInput *in = NULL;
        for (int i = 0; i < dev->ninputs && !in; i++) {
            if (dev->inputs[i].id && !strcmp(dev->inputs[i].id, t->id)) {
                in = &dev->inputs[i];
            }
        }
        if (!in) {
            snprintf(why, whylen, "port %d: the device document '%s' has no input '%s', which the Steel Battalion "
                     "controller's report carries (the document and PenguinBox's reader of the controller disagree)",
                     port, dev->device_id, t->id);
            return false;
        }
        if (in->kind != t->kind) {
            snprintf(why, whylen, "port %d: the device document '%s' makes '%s' of the kind %s; the controller's "
                     "report carries it as a %s (the document and PenguinBox's reader of the controller disagree)", port,
                     dev->device_id, t->id, sim_sbc_kind_name(in->kind), sim_sbc_kind_name(t->kind));
            return false;
        }
        if (t->kind == SIM_IN_AXIS && (!in->has_range || in->range.min != t->min || in->range.max != t->max)) {
            snprintf(why, whylen, "port %d: the device document '%s' gives the axis '%s' the range %g..%g; the "
                     "controller's report carries %g..%g (the document and PenguinBox's reader of the controller "
                     "disagree)", port, dev->device_id, t->id, in->has_range ? in->range.min : 0.0,
                     in->has_range ? in->range.max : 0.0, t->min, t->max);
            return false;
        }
        if (t->kind == SIM_IN_DETENT) {
            for (int p = 0; p < in->npositions; p++) {
                bool is_int = in->position_kinds && in->position_kinds[p] == 'I' && in->position_ints;
                if (!is_int || in->position_ints[p] < t->min || in->position_ints[p] > t->max) {
                    snprintf(why, whylen, "port %d: the device document '%s' gives the lever '%s' the position '%s'; "
                             "the controller's report carries an integer %g..%g (the document and PenguinBox's "
                             "reader of the controller disagree)", port, dev->device_id, t->id,
                             in->positions && in->positions[p] ? in->positions[p] : "?", t->min, t->max);
                    return false;
                }
            }
        }
    }
    for (int i = 0; i < dev->ninputs; i++) {
        if (sim_sbc_id_index(dev->inputs[i].id) < 0) {
            snprintf(why, whylen, "port %d: the device document '%s' names the input '%s', which the Steel Battalion "
                     "controller's report does not carry (the document and PenguinBox's reader of the controller "
                     "disagree)", port, dev->device_id, dev->inputs[i].id ? dev->inputs[i].id : "(no id)");
            return false;
        }
    }
    return true;
}

typedef struct SimSbcRoots {
    const char *sys_usb;
    const char *dev_usb;
} SimSbcRoots;

typedef struct SimSbcFound {
    int count;
    char sysname[256];
    char port_path[256];
    char node[PATH_MAX + 32];
    bool may_open;
} SimSbcFound;

static inline bool sim_sbc_read_line(const char *dir, const char *name, const char *file, char *out, size_t len)
{
    char path[PATH_MAX];
    out[0] = '\0';
    if (snprintf(path, sizeof(path), "%s/%s/%s", dir, name, file) >= (int)sizeof(path)) {
        return false;
    }
    FILE *f = fopen(path, "r");
    if (!f) {
        return false;
    }
    bool ok = fgets(out, (int)len, f) != NULL;
    fclose(f);
    size_t n = strlen(out);
    while (n && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' ')) {
        out[--n] = '\0';
    }
    return ok;
}

static inline void sim_sbc_usb_form(const char *in, char *out, size_t len)
{
    size_t o = 0;
    for (const char *p = in; p && *p && o + 1 < len;) {
        if (!strncmp(p, "-usbv", 5) && isdigit((unsigned char)p[5])) {
            const char *q = p + 5;
            while (isdigit((unsigned char)*q)) {
                q++;
            }
            if (*q == '-' && o + 5 < len) {
                memcpy(out + o, "-usb-", 5);
                o += 5;
                p = q + 1;
                continue;
            }
        }
        out[o++] = *p++;
    }
    out[o] = '\0';
}

static inline bool sim_sbc_is_pci(const char *s, size_t n)
{
    static const char shape[] = "xxxx:xx:xx.x";
    if (n != sizeof(shape) - 1) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (shape[i] == 'x' ? !isxdigit((unsigned char)s[i]) : s[i] != shape[i]) {
            return false;
        }
    }
    return true;
}

static inline void sim_sbc_port_path(const char *real, const char *sysname, const char *config, char *out,
                                     size_t len)
{
    out[0] = '\0';
    const char *dash = strchr(sysname, '-');
    if (!dash) {
        return;
    }
    char prefix[128] = "";
    const char *p = real;
    const char *platform_next = NULL;
    while (p && *p) {
        while (*p == '/') {
            p++;
        }
        const char *e = strchr(p, '/');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (sim_sbc_is_pci(p, n)) {
            snprintf(prefix, sizeof(prefix), "pci-%.*s", (int)n, p);
        } else if (n == 8 && !strncmp(p, "platform", 8) && e) {
            platform_next = e + 1;
        } else if (!prefix[0] && platform_next == p && n) {
            snprintf(prefix, sizeof(prefix), "platform-%.*s", (int)n, p);
        }
        if (n == strlen(sysname) && !strncmp(p, sysname, n)) {
            break;
        }
        p = e;
    }
    if (prefix[0]) {
        snprintf(out, len, "%s-usb-0:%s:%s.0", prefix, dash + 1, config[0] ? config : "1");
    }
}

static inline void sim_sbc_locate(const SimSbcRoots *roots, SimSbcFound *out)
{
#ifndef __linux__
    (void)roots;
    memset(out, 0, sizeof(*out));
    out->count = -1;
#else
    const char *sys = roots && roots->sys_usb ? roots->sys_usb : "/sys/bus/usb/devices";
    const char *dev = roots && roots->dev_usb ? roots->dev_usb : "/dev/bus/usb";
    memset(out, 0, sizeof(*out));
    DIR *d = opendir(sys);
    if (!d) {
        return;
    }
    struct dirent *e;
    while ((e = readdir(d))) {
        const char *name = e->d_name;
        char vid[16], pid[16];
        if (name[0] == '.' || strchr(name, ':') || !strchr(name, '-') ||
            !sim_sbc_read_line(sys, name, "idVendor", vid, sizeof(vid)) ||
            !sim_sbc_read_line(sys, name, "idProduct", pid, sizeof(pid)) ||
            strtol(vid, NULL, 16) != SBC_USB_VENDOR_ID || strtol(pid, NULL, 16) != SBC_USB_PRODUCT_ID) {
            continue;
        }
        out->count++;
        if (out->count > 1 && strcmp(name, out->sysname) > 0) {
            continue;
        }
        snprintf(out->sysname, sizeof(out->sysname), "%s", name);
        char cfg[16], bus[16], num[16], link[PATH_MAX], real[PATH_MAX];
        sim_sbc_read_line(sys, name, "bConfigurationValue", cfg, sizeof(cfg));
        sim_sbc_read_line(sys, name, "busnum", bus, sizeof(bus));
        sim_sbc_read_line(sys, name, "devnum", num, sizeof(num));
        out->port_path[0] = '\0';
        if (snprintf(link, sizeof(link), "%s/%s", sys, name) < (int)sizeof(link) && realpath(link, real)) {
            sim_sbc_port_path(real, name, cfg, out->port_path, sizeof(out->port_path));
        }
        snprintf(out->node, sizeof(out->node), "%s/%03ld/%03ld", dev, strtol(bus, NULL, 10), strtol(num, NULL, 10));
        out->may_open = access(out->node, R_OK | W_OK) == 0;
    }
    closedir(d);
#endif
}

static inline bool sim_sbc_plan_check(const SimPortDevice *dev, int port, const SimSbcRoots *roots, char *why,
                                      size_t whylen)
{
    const char *pt = getenv("XEMU_SBC_PASSTHROUGH");
    if (pt && !strcmp(pt, "1")) {
        snprintf(why, whylen, "port %d: the device '%s' is the real Steel Battalion controller, and the developer "
                 "switch XEMU_SBC_PASSTHROUGH=1 hands that controller to the game raw; both cannot own it: unset the "
                 "switch to boot this setup", port, dev->device_id);
        return false;
    }
    if (!sim_sbc_source_check(dev, port, why, whylen)) {
        return false;
    }
    SimSbcFound f;
    sim_sbc_locate(roots, &f);
    if (f.count < 0) {
        return true;
    }
    if (f.count == 0) {
        snprintf(why, whylen, "port %d: the Steel Battalion controller (the device '%s') is not plugged in: plug it "
                 "in and boot again", port, dev->device_id);
        return false;
    }
    if (f.count > 1) {
        snprintf(why, whylen, "port %d: %d Steel Battalion controllers are plugged in, and PenguinBox reads one: "
                 "unplug the others and boot again", port, f.count);
        return false;
    }
    if (dev->port_path && dev->port_path[0]) {
        char want[256], have[256];
        sim_sbc_usb_form(dev->port_path, want, sizeof(want));
        sim_sbc_usb_form(f.port_path, have, sizeof(have));
        if (strcmp(want, have)) {
            snprintf(why, whylen, "port %d: the setup puts the Steel Battalion controller (the device '%s') at the "
                     "USB port '%s', but it is plugged in at '%s': plug it into the port the setup names, or choose "
                     "it again in the Sim Configurator", port, dev->device_id, dev->port_path,
                     f.port_path[0] ? f.port_path : "a port whose path cannot be read");
            return false;
        }
    }
    if (!f.may_open) {
        snprintf(why, whylen, "port %d: the Steel Battalion controller is plugged in, but this user may not open it "
                 "(%.200s): install the udev rule 70-penguinvrlabs-sim-devices.rules (run PenguinBox once with "
                 "--install-sbc-rules), then unplug the controller and plug it in again", port, f.node);
        return false;
    }
    return true;
}

double sim_sbc_source_value(int row, const SimInput *in, const SbcFrame *f);
void sim_sbc_source_fill(const SimPortDevice *dev, const SbcFrame *f, double *vals);
void sim_sbc_source_log(const SimPortDevice *dev, int port, FILE *to);

#define SIM_SBC_LIGHTS_MAX    32
#define SIM_SBC_LIGHTS_MIN_MS 20

typedef struct SimSbcLights {
    uint8_t want[SIM_SBC_LIGHTS_MAX];
    size_t want_len;
    uint8_t sent[SIM_SBC_LIGHTS_MAX];
    size_t sent_len;
    bool have_want, have_sent;
    int64_t sent_ms;
    bool stopped;
} SimSbcLights;

void sim_sbc_lights_init(SimSbcLights *l);
void sim_sbc_lights_offer(SimSbcLights *l, const uint8_t *report, size_t len);
size_t sim_sbc_lights_due(const SimSbcLights *l, int64_t now_ms, uint8_t *out);
void sim_sbc_lights_sent(SimSbcLights *l, int64_t now_ms);
size_t sim_sbc_lights_off(SimSbcLights *l, uint8_t *out);

bool sim_sbc_port_forwards_lights(const SimPort *p);

typedef struct SimSbcLightSlot {
    uint8_t report[SIM_SBC_LIGHTS_MAX];
    size_t len;
    uint32_t version;
} SimSbcLightSlot;

void sim_sbc_lights_store(SimSbcLightSlot *slot, const void *report, size_t len);
void sim_sbc_lights_step(SimSbcLights *l, const SimSbcLightSlot *slot, uint32_t *seen, int64_t now_ms,
                         bool (*send)(const uint8_t *report, size_t len, void *ctx), void *ctx);

#ifdef __cplusplus
}
#endif

#endif
