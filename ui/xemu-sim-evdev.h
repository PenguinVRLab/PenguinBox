/*
 * PenguinBox — joysticks, wheels, pedals and button boxes read by port path (B-SHIP-1 XB-7)
 *
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 *
 * Plain C: no QEMU, no SDL, no glib. tests/xbox/simevdev runs it whole on a made-up tree.
 *
 * A Sim Configurator device whose `source` is sdl_joystick names ONE physical unit by its `port_path`, udev's
 * ID_PATH ("pci-0000:00:14.0-usb-0:1:1.0"), and its inputs by their Linux evdev codes (the Sim Configurator's
 * contract: an axis's `code` is its ABS_* code, a button's its BTN_* code (EV_KEY), a hat's its ABS_HAT<n>X, the Y
 * being the next code). This file reads that unit straight from evdev, as the Sim Configurator reads it; nothing
 * is translated through SDL's joystick indices.
 *
 * How a unit and its port path are found (the Sim Configurator's own way, line for line; no device is opened):
 *   1. every link in the by-path folder (/dev/input/by-path) whose name ends in "-event-joystick", "-event-kbd",
 *      "-event-mouse" or "-event" (tried in that order) and that points at an "eventN" node: the port path is the
 *      name without that suffix, with udev's USB-revision form "-usbv<N>-" read as "-usb-" (the same unit). The
 *      links are taken in sorted order; the links of one port path are ONE unit with all their nodes; the node
 *      that describes the unit is the one of the first kind in the order above (a gamepad by its joystick node,
 *      never by its motion sensor's "-event" node).
 *   2. an "eventN" in the sysfs input folder (/sys/class/input) that no link names: its port path is the
 *      `E:ID_PATH=` line of udev's record of it (the udev data folder, /run/udev/data, file "c<major:minor>" from
 *      eventN/dev), and its node is eventN in the device folder (/dev/input).
 *   3. the unit's vendor and product (hex) and name: eventN/device/id/vendor, id/product, name of its describing
 *      node. The ABS and key codes a node reports: eventN/device/capabilities/abs and /key (sysfs bitmaps).
 * The four folders are parameters (sim_evdev_set_roots), so a test can point them at a made-up tree.
 *
 * Reading (only on a Sim Configurator boot, at the attach): every node of the unit is opened READ-ONLY and
 * non-blocking, and is NEVER grabbed (no EVIOCGRAB): the desktop and every other reader keep receiving its events.
 * An ABS code is taken only from the node that OWNS it: the first node, the describing one first, that reports it
 * and whose EVIOCGABS answers (another node of the unit, such as a motion sensor, may report the same code number).
 * The current values come from EVIOCGABS (axes) and EVIOCGKEY (buttons) at the open, then from the events; a
 * SYN_DROPPED (the kernel's queue overflowed) re-reads both. The values are the RAW evdev values: an axis as the
 * unit reports it (the merge layer normalises it with the descriptor or the person's calibration), a button 0/1,
 * a hat its two axes (-1/0/+1 each) turned into the merge layer's hat bits. Nothing else is applied here.
 *
 * A unit that goes away while it is read (a read that fails, or ends): the whole unit is "gone" and every input
 * reads as at rest (sim_evdev_fill returns false: the caller feeds the merge layer no values for it). This file's
 * reading, where the contract is silent: one lost node makes the whole unit gone (a unit unplugged loses every
 * node; half a unit is not read); and a gone unit stays gone until the next boot (it is never re-opened by
 * itself: another unit could have taken that port).
 *
 * Builds other than Linux: nothing is read; sim_evdev_check_device refuses, naming the device.
 */
#ifndef XEMU_SIM_EVDEV_H
#define XEMU_SIM_EVDEV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "xemu-sim-profile-reader.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SIM_EVDEV_MAX_NODES 32
#define SIM_EVDEV_PATH      512

#define SIM_EVDEV_KIND_JOYSTICK 0x1
#define SIM_EVDEV_KIND_KEYBOARD 0x2
#define SIM_EVDEV_KIND_MOUSE    0x4
#define SIM_EVDEV_KIND_OTHER    0x8

typedef struct SimEvdevUnit {
    char port_path[256];
    char name[128];
    int vid, pid;
    unsigned kinds;
    int nnodes;
    char node[SIM_EVDEV_MAX_NODES][SIM_EVDEV_PATH];
    char event[SIM_EVDEV_MAX_NODES][32];
    bool too_many;
} SimEvdevUnit;

typedef struct SimEvdevRoots {
    const char *dev_input;
    const char *by_path;
    const char *sys_input;
    const char *udev_data;
} SimEvdevRoots;
void sim_evdev_set_roots(const SimEvdevRoots *roots);

typedef struct SimEvdevIo {
    int (*absinfo)(int fd, const char *node, int code, int32_t info[6]);
    int (*keys)(int fd, const char *node, uint8_t *bits, size_t len);
} SimEvdevIo;
void sim_evdev_set_io(const SimEvdevIo *io);

bool sim_evdev_link_port_path(const char *link, char *port_path, size_t len, unsigned *kind);
int sim_evdev_scan(SimEvdevUnit **units, char *why, size_t whylen);
bool sim_evdev_find(const char *port_path, SimEvdevUnit *out);
bool sim_evdev_node_reports(const SimEvdevUnit *u, int i, int type, int code);

bool sim_evdev_check_device(const SimPortDevice *dev, int port, char *why, size_t whylen);

typedef struct SimEvdevReader SimEvdevReader;

SimEvdevReader *sim_evdev_open(const SimEvdevUnit *u, char *why, size_t whylen);
bool sim_evdev_reader_covers(const SimEvdevReader *r, const SimPortDevice *dev, int port, char *why, size_t whylen);
const char *sim_evdev_reader_skipped(const SimEvdevReader *r);
int sim_evdev_reader_poll(SimEvdevReader *r);
bool sim_evdev_reader_gone(const SimEvdevReader *r);
bool sim_evdev_reader_abs(const SimEvdevReader *r, int code, int *value, int *min, int *max);
int sim_evdev_reader_key(const SimEvdevReader *r, int code);
bool sim_evdev_fill(const SimEvdevReader *r, const SimPortDevice *dev, double *vals);
void sim_evdev_close(SimEvdevReader *r);

#ifdef __cplusplus
}
#endif

#endif
