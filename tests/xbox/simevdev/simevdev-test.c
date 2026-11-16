/*
 * SPDX-License-Identifier: MIT
 *
 * tests/xbox/simevdev: joysticks, wheels and pedals read by port path (ui/xemu-sim-evdev.{c,h}), on a made-up tree:
 * the by-path links, the sysfs input folder and udev's records are files this test writes; the event nodes are
 * FIFOs it writes `struct input_event`s into; EVIOCGABS and EVIOCGKEY are answered from files beside them.
 *   A. a link name's port path
 *   B. the units: two identical units at two port paths, a unit with two nodes (its motion sensor's link sorts
 *      first), a unit udev alone names, links that are not event nodes
 *   C. the plan's check (no device opened): port path, codes, kinds, the unit present, its vid:pid, its codes
 *   D. reading: the node that owns an ABS code, the two identical units apart, the values for the merge layer
 *      (raw axes, buttons 0/1, a hat's bits from its two axes), SYN_DROPPED, a denied node, a node gone before the
 *      open, a node that vanishes while read
 *   usage: simevdev-test WORK_DIR     exit 0 = every check passed
 *   RED drill: make drill (SIM_EVDEV_DRILL_ANY_NODE: an ABS event taken from any node of the unit) must fail
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/input.h>

#include "xemu-sim-evdev.h"
#include "xemu-sim-merge.h"

static int nchecks, nfailed;
static char W[1024];

static void expect(const char *what, int ok)
{
    nchecks++;
    if (!ok) {
        nfailed++;
    }
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static int has(const char *s, const char *needle)
{
    return s && strstr(s, needle) != NULL;
}

static void path(char *out, size_t len, const char *rel)
{
    snprintf(out, len, "%s/%s", W, rel);
}

static void write_file(const char *rel, const char *text)
{
    char p[2048], dir[2048];
    path(p, sizeof(p), rel);
    snprintf(dir, sizeof(dir), "%s", p);
    for (char *s = dir + strlen(W) + 1; (s = strchr(s, '/')) != NULL; s++) {
        *s = '\0';
        mkdir(dir, 0755);
        *s = '/';
    }
    FILE *f = fopen(p, "w");
    if (!f) {
        fprintf(stderr, "cannot write %s: %s\n", p, strerror(errno));
        exit(2);
    }
    fputs(text, f);
    fclose(f);
}

static void sys_event(const char *ev, const char *devno, const char *vid, const char *pid, const char *name,
                      const char *abs, const char *key)
{
    char rel[256];
    snprintf(rel, sizeof(rel), "sys/%s/dev", ev);
    write_file(rel, devno);
    snprintf(rel, sizeof(rel), "sys/%s/device/id/vendor", ev);
    write_file(rel, vid);
    snprintf(rel, sizeof(rel), "sys/%s/device/id/product", ev);
    write_file(rel, pid);
    snprintf(rel, sizeof(rel), "sys/%s/device/name", ev);
    write_file(rel, name);
    snprintf(rel, sizeof(rel), "sys/%s/device/capabilities/abs", ev);
    write_file(rel, abs);
    snprintf(rel, sizeof(rel), "sys/%s/device/capabilities/key", ev);
    write_file(rel, key);
}

static int writer[64];

static void node(int n, mode_t mode)
{
    char p[2048], rel[64];
    snprintf(rel, sizeof(rel), "dev/event%d", n);
    path(p, sizeof(p), rel);
    unlink(p);
    if (mkfifo(p, 0600) != 0) {
        fprintf(stderr, "mkfifo %s: %s\n", p, strerror(errno));
        exit(2);
    }
    writer[n] = open(p, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    chmod(p, mode);
}

static void link_to(const char *name, const char *target)
{
    char p[2048];
    snprintf(p, sizeof(p), "%s/dev/by-path/%s", W, name);
    if (symlink(target, p) != 0) {
        fprintf(stderr, "symlink %s: %s\n", p, strerror(errno));
        exit(2);
    }
}

static void send_ev(int n, int type, int code, int value)
{
    struct input_event e;
    memset(&e, 0, sizeof(e));
    e.type = (unsigned short)type;
    e.code = (unsigned short)code;
    e.value = value;
    if (write(writer[n], &e, sizeof(e)) != (ssize_t)sizeof(e)) {
        fprintf(stderr, "write event%d: %s\n", n, strerror(errno));
        exit(2);
    }
}

static const char *base(const char *node)
{
    const char *s = strrchr(node, '/');
    return s ? s + 1 : node;
}

static int fake_absinfo(int fd, const char *nodep, int code, int32_t info[6])
{
    (void)fd;
    char p[2048];
    snprintf(p, sizeof(p), "%s/absinfo/%s", W, base(nodep));
    FILE *f = fopen(p, "r");
    if (!f) {
        return -1;
    }
    int c, v, lo, hi, found = -1;
    while (fscanf(f, "%d %d %d %d", &c, &v, &lo, &hi) == 4) {
        if (c == code) {
            info[0] = v;
            info[1] = lo;
            info[2] = hi;
            info[3] = info[4] = info[5] = 0;
            found = 0;
        }
    }
    fclose(f);
    return found;
}

static int fake_keys(int fd, const char *nodep, uint8_t *bits, size_t len)
{
    (void)fd;
    char p[2048];
    snprintf(p, sizeof(p), "%s/keys/%s", W, base(nodep));
    memset(bits, 0, len);
    FILE *f = fopen(p, "r");
    if (!f) {
        return 0;
    }
    int c;
    while (fscanf(f, "%d", &c) == 1) {
        if (c >= 0 && (size_t)c / 8 < len) {
            bits[c / 8] |= (uint8_t)(1u << (c % 8));
        }
    }
    fclose(f);
    return 0;
}

#define STICK_A "pci-0000:00:14.0-usb-0:1:1.0"
#define STICK_B "pci-0000:00:14.0-usb-0:2:1.0"
#define PAD_C   "pci-0000:00:14.0-usb-0:3:1.0"
#define WHEEL_D "pci-0000:00:14.0-usb-0:4:1.0"
#define UDEV_E  "pci-0000:00:1d.0-usb-0:1:1.0"
#define SHUT_G  "pci-0000:00:14.0-usb-0:5:1.0"

#define ABS_STICK "30063"
#define KEY_STICK "fff00000000 0 0 0 0"

static void build_tree(void)
{
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "%s/dev/by-path", W);
    write_file("dev/.keep", "");
    mkdir(cmd, 0755);
    for (int i = 0; i < 64; i++) {
        writer[i] = -1;
    }
    node(1, 0600);
    node(2, 0600);
    link_to(STICK_A "-event-joystick", "../event1");
    link_to(STICK_B "-event-joystick", "../event2");
    sys_event("event1", "13:65", "044f", "b10a", "Thrustmaster T.16000M", ABS_STICK, KEY_STICK);
    sys_event("event2", "13:66", "044f", "b10a", "Thrustmaster T.16000M", ABS_STICK, KEY_STICK);
    write_file("absinfo/event1", "0 8192 0 16383\n1 8192 0 16383\n5 128 0 255\n6 255 0 255\n16 0 -1 1\n17 0 -1 1\n");
    write_file("absinfo/event2", "0 100 0 16383\n1 200 0 16383\n5 128 0 255\n6 0 0 255\n16 0 -1 1\n17 0 -1 1\n");
    write_file("keys/event1", "289\n");
    node(3, 0600);
    node(4, 0600);
    link_to(PAD_C "-event", "../event4");
    link_to("pci-0000:00:14.0-usbv2-0:3:1.0-event-joystick", "../event3");
    sys_event("event3", "13:67", "054c", "09cc", "Wireless Controller", "3003f", "7fff000000000000 0 0 0 0");
    sys_event("event4", "13:68", "054c", "09cc", "Wireless Controller Motion Sensors", "3f", "0");
    write_file("absinfo/event3", "0 128 0 255\n1 128 0 255\n2 0 0 255\n3 128 0 255\n4 128 0 255\n5 0 0 255\n"
               "16 0 -1 1\n17 0 -1 1\n");
    write_file("absinfo/event4", "0 -300 -32768 32767\n1 0 -32768 32767\n2 0 -32768 32767\n3 0 -2000 2000\n"
               "4 0 -2000 2000\n5 0 -2000 2000\n");
    node(5, 0600);
    node(7, 0000);
    link_to(WHEEL_D "-event-joystick", "../event5");
    link_to(WHEEL_D "-event", "../event7");
    sys_event("event5", "13:69", "046d", "c266", "Logitech G923", "27", "1000000000000 0 0 0 0");
    sys_event("event7", "13:71", "046d", "c266", "Logitech G923 extra", "8", "0");
    write_file("absinfo/event5", "0 32768 0 65535\n1 255 0 255\n2 255 0 255\n5 255 0 255\n");
    write_file("absinfo/event7", "3 0 0 255\n");
    node(6, 0600);
    sys_event("event6", "13:70", "06a3", "0763", "Saitek Pro Flight Rudder Pedals", "7", "0");
    write_file("udev/c13:70", "S:input/by-id/x\nE:ID_INPUT=1\nE:ID_PATH=" UDEV_E "\nE:ID_PATH_TAG=x\n");
    write_file("absinfo/event6", "0 0 0 511\n1 0 0 511\n2 256 0 511\n");
    node(8, 0000);
    link_to(SHUT_G "-event-joystick", "../event8");
    sys_event("event8", "13:72", "044f", "b10a", "Thrustmaster T.16000M", ABS_STICK, KEY_STICK);
    write_file("dev/js0", "");
    link_to(STICK_A "-joystick", "../js0");
    link_to("-event-joystick", "../event1");
    write_file("sys/input9/name", "not an event node");
    sys_event("event9", "13:73", "1234", "5678", "orphan", "0", "0");
}

static SimInput axis_in(const char *id, int code, double lo, double rest, double hi)
{
    SimInput in;
    memset(&in, 0, sizeof(in));
    in.id = id;
    in.kind = SIM_IN_AXIS;
    in.has_range = true;
    in.range.min = lo;
    in.range.rest = rest;
    in.range.max = hi;
    in.code = code;
    return in;
}

static SimInput other_in(const char *id, SimInputKind kind, int code)
{
    SimInput in;
    memset(&in, 0, sizeof(in));
    in.id = id;
    in.kind = kind;
    in.code = code;
    return in;
}

static SimPortDevice dev_of(const char *id, const char *port_path, int ninputs, const SimInput *inputs,
                            int nbind, const SimBinding *binds)
{
    SimPortDevice d;
    memset(&d, 0, sizeof(d));
    d.device_id = id;
    d.key = id;
    d.port_path = port_path;
    d.source = SIM_SRC_SDL_JOYSTICK;
    d.ninputs = ninputs;
    d.inputs = inputs;
    d.nbindings = nbind;
    d.bindings = binds;
    d.usable = true;
    return d;
}

static void part_a(void)
{
    printf("== A. a link name's port path\n");
    char pp[256];
    unsigned kind = 0;
    expect("A1 '<ID_PATH>-event-joystick': the ID_PATH, a joystick",
           sim_evdev_link_port_path(STICK_A "-event-joystick", pp, sizeof(pp), &kind) && !strcmp(pp, STICK_A) &&
           kind == SIM_EVDEV_KIND_JOYSTICK);
    expect("A2 udev's '-usbv2-' form reads as '-usb-' (the same unit)",
           sim_evdev_link_port_path("pci-0000:00:14.0-usbv2-0:3:1.0-event", pp, sizeof(pp), &kind) &&
           !strcmp(pp, PAD_C) && kind == SIM_EVDEV_KIND_OTHER);
    expect("A3 '-event-kbd' and '-event-mouse' are their kinds",
           sim_evdev_link_port_path("platform-i8042-serio-0-event-kbd", pp, sizeof(pp), &kind) &&
           kind == SIM_EVDEV_KIND_KEYBOARD && !strcmp(pp, "platform-i8042-serio-0") &&
           sim_evdev_link_port_path("pci-x-usb-0:9:1.0-event-mouse", pp, sizeof(pp), &kind) &&
           kind == SIM_EVDEV_KIND_MOUSE);
    expect("A4 '-joystick' (the old js interface) and a bare suffix are not event nodes' links",
           !sim_evdev_link_port_path(STICK_A "-joystick", pp, sizeof(pp), &kind) &&
           !sim_evdev_link_port_path("-event-joystick", pp, sizeof(pp), &kind));
    expect("A5 '-usbv' with no digits, or no '-' after them, is left as written",
           sim_evdev_link_port_path("pci-0-usbvx-1-event", pp, sizeof(pp), &kind) && !strcmp(pp, "pci-0-usbvx-1") &&
           sim_evdev_link_port_path("pci-0-usbv2", pp, sizeof(pp), &kind) == false);
}

static const SimEvdevUnit *unit_at(const SimEvdevUnit *u, int n, const char *pp)
{
    for (int i = 0; i < n; i++) {
        if (!strcmp(u[i].port_path, pp)) {
            return &u[i];
        }
    }
    return NULL;
}

static void part_b(void)
{
    printf("== B. the units\n");
    SimEvdevUnit *u = NULL;
    char why[256] = "";
    int n = sim_evdev_scan(&u, why, sizeof(why));
    for (int i = 0; i < n; i++) {
        printf("     %s %04x:%04x '%s' kinds %x:", u[i].port_path, u[i].vid, u[i].pid, u[i].name, u[i].kinds);
        for (int j = 0; j < u[i].nnodes; j++) {
            printf(" %s", u[i].event[j]);
        }
        printf("\n");
    }
    expect("B1 six units, sorted by port path", n == 6 && !strcmp(u[0].port_path, STICK_A) &&
           !strcmp(u[1].port_path, STICK_B) && !strcmp(u[5].port_path, UDEV_E));
    const SimEvdevUnit *a = unit_at(u, n, STICK_A), *b = unit_at(u, n, STICK_B);
    expect("B2 two identical units (044f:b10a) at two port paths, each its own node",
           a && b && a->vid == 0x044f && a->pid == 0xb10a && b->vid == 0x044f && b->pid == 0xb10a &&
           a->nnodes == 1 && b->nnodes == 1 && !strcmp(a->event[0], "event1") && !strcmp(b->event[0], "event2"));
    const SimEvdevUnit *c = unit_at(u, n, PAD_C);
    expect("B3 a unit with two nodes (one by the '-usbv2-' link): one unit, the joystick node first though its "
           "link sorts second", c && c->nnodes == 2 && !strcmp(c->event[0], "event3") &&
           !strcmp(c->event[1], "event4") && c->kinds == (SIM_EVDEV_KIND_JOYSTICK | SIM_EVDEV_KIND_OTHER));
    expect("B4 ... named by its describing node", c && !strcmp(c->name, "Wireless Controller"));
    const SimEvdevUnit *e = unit_at(u, n, UDEV_E);
    char want[2048];
    snprintf(want, sizeof(want), "%s/dev/event6", W);
    expect("B5 a unit no link names: udev's ID_PATH, its node in the device folder", e && e->nnodes == 1 &&
           !strcmp(e->node[0], want) && e->kinds == SIM_EVDEV_KIND_OTHER && e->vid == 0x06a3);
    expect("B6 the old js link, a bare suffix and an eventN udev does not name make no unit",
           !unit_at(u, n, "") && n == 6);
    free(u);
    SimEvdevUnit f;
    expect("B7 find by port path: the second of two identical units is its own node",
           sim_evdev_find(STICK_B, &f) && !strcmp(f.event[0], "event2"));
    expect("B8 find: a port path with no unit", !sim_evdev_find("pci-0000:00:14.0-usb-0:9:1.0", &f));
    if (sim_evdev_find(PAD_C, &f)) {
        expect("B9 ... read from sysfs", sim_evdev_node_reports(&f, 0, EV_ABS, 0) &&
               sim_evdev_node_reports(&f, 1, EV_ABS, 0) && sim_evdev_node_reports(&f, 0, EV_ABS, 16) &&
               !sim_evdev_node_reports(&f, 1, EV_ABS, 16) && sim_evdev_node_reports(&f, 0, EV_KEY, 304));
    }
}

static void part_c(void)
{
    printf("== C. the plan's check (no device opened)\n");
    char why[512];
    SimInput in[] = {
        axis_in("stick_x", 0, 0, 8192, 16383),
        axis_in("throttle", 6, 255, 255, 0),
        other_in("trigger", SIM_IN_BUTTON, 288),
        other_in("hat", SIM_IN_HAT, 16),
        other_in("unbound_no_code", SIM_IN_BUTTON, -1),
        other_in("gate", SIM_IN_DETENT, -1),
        other_in("no_code", SIM_IN_BUTTON, -1),
        other_in("rx", SIM_IN_AXIS, 3),
        other_in("btn_far", SIM_IN_BUTTON, 704),
    };
    SimBinding b[4];
    memset(b, 0, sizeof(b));
    b[0].source = &in[0];
    b[1].source = &in[1];
    b[2].source = &in[2];
    b[3].source = &in[3];
    SimPortDevice d = dev_of("t16000m", STICK_A, 9, in, 4, b);
    d.has_match = true;
    d.vid = 0x044f;
    d.pid = 0xb10a;
    int ok = sim_evdev_check_device(&d, 1, why, sizeof(why));
    printf("     %s\n", ok ? "(readable)" : why);
    expect("C1 bound axis, button and hat, all with codes the unit reports, vid:pid its match: readable", ok);

    SimPortDevice nopath = d;
    nopath.port_path = NULL;
    expect("C2 no port path: refused, naming the port and the device",
           !sim_evdev_check_device(&nopath, 2, why, sizeof(why)) && has(why, "port 2") && has(why, "'t16000m'") &&
           has(why, "no port path"));
    printf("     %s\n", why);

    SimBinding b2[5];
    memcpy(b2, b, sizeof(b));
    b2[4].source = &in[6];
    SimPortDevice nocode = dev_of("t16000m", STICK_A, 9, in, 5, b2);
    expect("C3 a bound input with no code: refused as 'device.input has no evdev code'",
           !sim_evdev_check_device(&nocode, 1, why, sizeof(why)) && has(why, "t16000m.no_code has no evdev code") &&
           has(why, "port 1"));
    printf("     %s\n", why);
    expect("C4 an input with no code that no binding uses does not refuse (C1 has one)", ok);

    b2[4].source = &in[5];
    SimPortDevice det = dev_of("t16000m", STICK_A, 9, in, 5, b2);
    expect("C5 a bound detent: refused (this build reads axes, buttons and hats from a joystick)",
           !sim_evdev_check_device(&det, 1, why, sizeof(why)) && has(why, "t16000m.gate is a detent"));
    b2[4].source = NULL;
    b2[4].recentre = &in[6];
    SimPortDevice rec = dev_of("t16000m", STICK_A, 9, in, 5, b2);
    expect("C6 a recentre button a binding uses counts as used (no code: refused)",
           !sim_evdev_check_device(&rec, 1, why, sizeof(why)) && has(why, "no_code"));

    SimPortDevice absent = d;
    absent.port_path = "pci-0000:00:14.0-usb-0:9:1.0";
    expect("C7 no unit at the port path: refused, naming the port path",
           !sim_evdev_check_device(&absent, 3, why, sizeof(why)) && has(why, "port 3") &&
           has(why, "no unit is plugged in at the port path 'pci-0000:00:14.0-usb-0:9:1.0'"));
    printf("     %s\n", why);

    SimPortDevice other = d;
    other.vid = 0x046d;
    other.pid = 0xc266;
    expect("C8 the unit there is another model: refused, naming both IDs",
           !sim_evdev_check_device(&other, 1, why, sizeof(why)) && has(why, "044f:b10a") && has(why, "046d:c266"));
    printf("     %s\n", why);
    static const uint16_t also[] = { 0x046d, 0xc267, 0x044f, 0xb10a };
    other.nmatch_also = 2;
    other.match_also = also;
    expect("C9 ... but a match_also that is the unit's: readable", sim_evdev_check_device(&other, 1, why, sizeof(why)));
    SimPortDevice nomatch = d;
    nomatch.has_match = false;
    expect("C10 a device with no match: the port path alone", sim_evdev_check_device(&nomatch, 1, why, sizeof(why)));

    SimBinding b3[1];
    memset(b3, 0, sizeof(b3));
    b3[0].source = &in[7];
    SimPortDevice rx = dev_of("t16000m", STICK_A, 9, in, 1, b3);
    expect("C11 an axis code the unit does not report: refused, naming the code and the input",
           !sim_evdev_check_device(&rx, 1, why, sizeof(why)) && has(why, "the axis code 3") && has(why, "t16000m.rx"));
    printf("     %s\n", why);
    b3[0].source = &in[8];
    expect("C12 a button code the unit does not report: refused", !sim_evdev_check_device(&rx, 1, why, sizeof(why)) &&
           has(why, "the button code 704"));
    SimInput hat1 = other_in("hat1", SIM_IN_HAT, 17);
    SimBinding bh;
    memset(&bh, 0, sizeof(bh));
    bh.source = &hat1;
    SimPortDevice h = dev_of("t16000m", STICK_A, 1, &hat1, 1, &bh);
    expect("C13 a hat needs its X code and the next (its Y): 17 and 18, the unit lacks 18: refused",
           !sim_evdev_check_device(&h, 1, why, sizeof(why)) && has(why, "the hat codes 17 and 18"));
    SimInput rel = other_in("wheel", SIM_IN_RELATIVE, 8);
    bh.source = &rel;
    SimPortDevice r = dev_of("t16000m", STICK_A, 1, &rel, 1, &bh);
    expect("C14 a bound relative input on a joystick: refused", !sim_evdev_check_device(&r, 1, why, sizeof(why)) &&
           has(why, "is a relative input"));
}

static SimEvdevReader *open_at(const char *pp, char *why, size_t len)
{
    SimEvdevUnit u;
    if (!sim_evdev_find(pp, &u)) {
        snprintf(why, len, "no unit at %s", pp);
        return NULL;
    }
    return sim_evdev_open(&u, why, len);
}

static void part_d(void)
{
    printf("== D. reading\n");
    char why[1024];
    int v, lo, hi;

    SimEvdevReader *c = open_at(PAD_C, why, sizeof(why));
    expect("D1 the pad opens (both nodes)", c && !sim_evdev_reader_skipped(c)[0]);
    if (!c) {
        printf("     %s\n", why);
        return;
    }
    expect("D2 ABS_X's range and value from its owner, the joystick node (0..255 at 128, not the sensor's)",
           sim_evdev_reader_abs(c, ABS_X, &v, &lo, &hi) && v == 128 && lo == 0 && hi == 255);
    send_ev(4, EV_ABS, ABS_X, -9000);
    send_ev(4, EV_SYN, SYN_REPORT, 0);
    send_ev(3, EV_ABS, ABS_Y, 40);
    send_ev(3, EV_SYN, SYN_REPORT, 0);
    int got = sim_evdev_reader_poll(c);
    expect("D3 the sensor's ABS_X is not the stick's: ABS_X stays 128; the stick's ABS_Y moves to 40",
           got >= 1 && sim_evdev_reader_abs(c, ABS_X, &v, &lo, &hi) && v == 128 &&
           sim_evdev_reader_abs(c, ABS_Y, &v, &lo, &hi) && v == 40);
    send_ev(3, EV_ABS, ABS_X, 7);
    send_ev(3, EV_SYN, SYN_REPORT, 0);
    sim_evdev_reader_poll(c);
    expect("D4 the stick's own ABS_X moves", sim_evdev_reader_abs(c, ABS_X, &v, &lo, &hi) && v == 7);
    sim_evdev_close(c);

    SimEvdevReader *a = open_at(STICK_A, why, sizeof(why));
    SimEvdevReader *b = open_at(STICK_B, why, sizeof(why));
    expect("D5 both identical sticks open", a && b);
    if (!a || !b) {
        return;
    }
    expect("D6 each with its own value at the open (A 8192, B 100) and A's held thumb button",
           sim_evdev_reader_abs(a, ABS_X, &v, &lo, &hi) && v == 8192 && sim_evdev_reader_abs(b, ABS_X, &v, &lo, &hi) &&
           v == 100 && sim_evdev_reader_key(a, BTN_THUMB) == 1 && sim_evdev_reader_key(b, BTN_THUMB) == 0);
    send_ev(2, EV_ABS, ABS_X, 16000);
    send_ev(2, EV_KEY, BTN_TRIGGER, 1);
    send_ev(2, EV_SYN, SYN_REPORT, 0);
    sim_evdev_reader_poll(a);
    sim_evdev_reader_poll(b);
    expect("D7 B's stick moved and its trigger pressed; A unchanged",
           sim_evdev_reader_abs(b, ABS_X, &v, &lo, &hi) && v == 16000 && sim_evdev_reader_key(b, BTN_TRIGGER) == 1 &&
           sim_evdev_reader_abs(a, ABS_X, &v, &lo, &hi) && v == 8192 && sim_evdev_reader_key(a, BTN_TRIGGER) == 0);

    SimInput in[] = {
        axis_in("stick_x", ABS_X, 0, 8192, 16383),
        axis_in("throttle", ABS_THROTTLE, 255, 255, 0),
        other_in("trigger", SIM_IN_BUTTON, BTN_TRIGGER),
        other_in("hat", SIM_IN_HAT, ABS_HAT0X),
        axis_in("no_code", -1, -1, 0, 1),
        other_in("gate", SIM_IN_DETENT, -1),
        other_in("thumb", SIM_IN_BUTTON, BTN_THUMB),
    };
    SimPortDevice d = dev_of("t16000m", STICK_B, 7, in, 0, NULL);
    send_ev(2, EV_ABS, ABS_HAT0X, -1);
    send_ev(2, EV_ABS, ABS_HAT0Y, 1);
    send_ev(2, EV_ABS, ABS_THROTTLE, 77);
    send_ev(2, EV_KEY, BTN_THUMB, 2);
    send_ev(2, EV_SYN, SYN_REPORT, 0);
    sim_evdev_reader_poll(b);
    double vals[7];
    int filled = sim_evdev_fill(b, &d, vals);
    printf("     B: %g %g %g %g %g %g %g\n", vals[0], vals[1], vals[2], vals[3], vals[4], vals[5], vals[6]);
    expect("D8 the values: raw axes (16000, 77), buttons 0/1 (1, 1), the hat LEFT|DOWN from its two axes, an input "
           "with no code at rest, a detent -1", filled && vals[0] == 16000 && vals[1] == 77 && vals[2] == 1 &&
           vals[3] == (SIM_HAT_LEFT | SIM_HAT_DOWN) && vals[4] == 0 && vals[5] == -1 && vals[6] == 1);
    send_ev(2, EV_ABS, ABS_HAT0X, 1);
    send_ev(2, EV_ABS, ABS_HAT0Y, -1);
    send_ev(2, EV_KEY, BTN_TRIGGER, 0);
    send_ev(2, EV_SYN, SYN_REPORT, 0);
    sim_evdev_reader_poll(b);
    sim_evdev_fill(b, &d, vals);
    expect("D9 the hat RIGHT|UP, the trigger released", vals[3] == (SIM_HAT_RIGHT | SIM_HAT_UP) && vals[2] == 0);

    write_file("absinfo/event2", "0 4321 0 16383\n1 200 0 16383\n5 128 0 255\n6 0 0 255\n16 0 -1 1\n17 0 -1 1\n");
    write_file("keys/event2", "288\n");
    send_ev(2, EV_SYN, SYN_DROPPED, 0);
    sim_evdev_reader_poll(b);
    expect("D10 after SYN_DROPPED the axes and buttons are read again (ABS_X 4321, the trigger held)",
           sim_evdev_reader_abs(b, ABS_X, &v, &lo, &hi) && v == 4321 && sim_evdev_reader_key(b, BTN_TRIGGER) == 1);

    close(writer[2]);
    writer[2] = -1;
    int p1 = sim_evdev_reader_poll(b);
    int p2 = sim_evdev_reader_poll(b);
    expect("D11 a node that vanishes while read: the unit is gone (poll -1, twice, no crash)",
           p1 == -1 && p2 == -1 && sim_evdev_reader_gone(b));
    vals[0] = 12345;
    expect("D12 ... and fill says so (the caller feeds the merge layer no values: every input at rest)",
           !sim_evdev_fill(b, &d, vals) && vals[0] == 12345);
    expect("D13 the other identical stick is still read", !sim_evdev_reader_gone(a) && sim_evdev_reader_poll(a) >= 0);
    sim_evdev_close(b);
    sim_evdev_close(a);

    SimEvdevReader *w = open_at(WHEEL_D, why, sizeof(why));
    expect("D14 the wheel opens though its second node is denied, and says which", w &&
           has(sim_evdev_reader_skipped(w), "event7") && has(sim_evdev_reader_skipped(w), "no permission"));
    if (w) {
        printf("     skipped: %s\n", sim_evdev_reader_skipped(w));
        SimInput win[] = { axis_in("steering", ABS_X, 0, 32768, 65535), axis_in("extra", ABS_RX, 0, 0, 255) };
        SimBinding wb[2];
        memset(wb, 0, sizeof(wb));
        wb[0].source = &win[0];
        SimPortDevice wd = dev_of("g923", WHEEL_D, 2, win, 1, wb);
        expect("D15 ... it covers the steering (on the node it could open)",
               sim_evdev_reader_covers(w, &wd, 1, why, sizeof(why)));
        wb[1].source = &win[1];
        wd.nbindings = 2;
        expect("D16 ... not ABS_RX, which only the denied node reports: refused, naming the node and why",
               !sim_evdev_reader_covers(w, &wd, 1, why, sizeof(why)) && has(why, "the axis code 3") &&
               has(why, "event7: no permission"));
        printf("     %s\n", why);
        sim_evdev_close(w);
    }
    SimEvdevReader *g = open_at(SHUT_G, why, sizeof(why));
    expect("D17 a unit whose only node is denied: no reader, the node named", !g && has(why, "event8") &&
           has(why, "no permission"));
    printf("     %s\n", why);
    sim_evdev_close(g);

    SimEvdevUnit e;
    int found = sim_evdev_find(UDEV_E, &e);
    char p[2048];
    path(p, sizeof(p), "dev/event6");
    unlink(p);
    SimEvdevReader *er = found ? sim_evdev_open(&e, why, sizeof(why)) : NULL;
    expect("D18 a node unplugged since the listing: no reader, 'no longer plugged in'", found && !er &&
           has(why, "no longer plugged in"));
    printf("     %s\n", why);
    sim_evdev_close(er);
}

static SimPortDevice bound_dev(const char *id, const char *pp, int n, const SimInput *in, SimBinding *b, int nb)
{
    memset(b, 0, sizeof(SimBinding) * (size_t)nb);
    for (int i = 0; i < nb; i++) {
        b[i].source = &in[i];
    }
    return dev_of(id, pp, n, in, nb, b);
}

static void part_e(void)
{
    printf("== E. a bound axis's range: the unit's against the device file's\n");
    char why[1024];
    SimEvdevReader *a = open_at(STICK_A, why, sizeof(why));
    if (!a) {
        expect("E0 the stick opens", 0);
        return;
    }
    SimBinding b[2];
    SimInput same[] = { axis_in("stick_x", ABS_X, 0, 8192, 16383), axis_in("throttle", ABS_THROTTLE, 255, 255, 0) };
    SimPortDevice d = bound_dev("t16000m", STICK_A, 2, same, b, 2);
    expect("E1 the file's range is the unit's (0..16383; and 255..0 written high to low on a 0..255 unit): accepted",
           sim_evdev_reader_covers(a, &d, 1, why, sizeof(why)));
    SimInput lo1[] = { axis_in("stick_x", ABS_X, 1, 8192, 16383) };
    d = bound_dev("t16000m", STICK_A, 1, lo1, b, 1);
    int r1 = !sim_evdev_reader_covers(a, &d, 1, why, sizeof(why));
    printf("     %s\n", why);
    expect("E2 the file's min one above the unit's: refused, naming the port, the device, the input and both ranges",
           r1 && has(why, "port 1") && has(why, "'t16000m'") && has(why, "t16000m.stick_x") && has(why, "1..16383") &&
           has(why, "0..16383") && has(why, "Device Studio"));
    SimInput hi1[] = { axis_in("stick_x", ABS_X, 0, 8192, 16382) };
    d = bound_dev("t16000m", STICK_A, 1, hi1, b, 1);
    expect("E3 the file's max one below the unit's: refused, naming both ranges",
           !sim_evdev_reader_covers(a, &d, 1, why, sizeof(why)) && has(why, "0..16382") && has(why, "0..16383"));
    SimInput shipped[] = { axis_in("stick_x", ABS_X, -32768, 0, 32767), axis_in("stick_y", ABS_Y, -32768, 0, 32767) };
    d = bound_dev("t16000m", STICK_A, 2, shipped, b, 1);
    int r4 = !sim_evdev_reader_covers(a, &d, 1, why, sizeof(why));
    printf("     the shipped T.16000M file on its unit: %s\n", why);
    expect("E4 today's shipped T.16000M file (-32768..32767) on its unit (0..16383): refused",
           r4 && has(why, "-32768..32767") && has(why, "0..16383") && has(why, "t16000m.stick_x"));
    SimInput unb[] = { axis_in("stick_x", ABS_X, 0, 8192, 16383), axis_in("stick_y", ABS_Y, -32768, 0, 32767) };
    d = bound_dev("t16000m", STICK_A, 2, unb, b, 1);
    expect("E5 a mismatching axis no binding reads does not refuse (the bound one matches)",
           sim_evdev_reader_covers(a, &d, 1, why, sizeof(why)));
    SimCalibrated cal = { &shipped[0], { 0, 8192, 16383 } };
    d = bound_dev("t16000m", STICK_A, 2, shipped, b, 1);
    d.ncalibrated = 1;
    d.calibrated = &cal;
    expect("E6 a calibration matching the unit does not exempt the file's range: still refused",
           !sim_evdev_reader_covers(a, &d, 1, why, sizeof(why)) && has(why, "-32768..32767"));
    sim_evdev_close(a);
}

#define PAD_H "pci-0000:00:14.0-usb-0:6:1.0"

static void part_f(void)
{
    printf("== F. a declared hat: its X and Y codes must report -1..1\n");
    char why[1024];
    node(10, 0600);
    link_to(PAD_H "-event-joystick", "../event10");
    sys_event("event10", "13:74", "045e", "028e", "a pad with triggers on hat codes", "f0000", "0");
    write_file("absinfo/event10", "16 0 -1 1\n17 0 0 255\n18 0 0 255\n19 0 -1 1\n");
    SimBinding b[1];
    SimInput hat_ok[] = { other_in("hat", SIM_IN_HAT, ABS_HAT0X) };
    SimEvdevReader *a = open_at(STICK_A, why, sizeof(why));
    SimPortDevice d = bound_dev("t16000m", STICK_A, 1, hat_ok, b, 1);
    expect("F1 a hat whose two codes report -1..1: accepted", a && sim_evdev_reader_covers(a, &d, 1, why, sizeof(why)));
    sim_evdev_close(a);
    SimEvdevReader *h = open_at(PAD_H, why, sizeof(why));
    if (!h) {
        expect("F0 the pad opens", 0);
        return;
    }
    SimInput hat_x[] = { other_in("hat1", SIM_IN_HAT, ABS_HAT1X) };
    d = bound_dev("pad", PAD_H, 1, hat_x, b, 1);
    int r2 = !sim_evdev_reader_covers(h, &d, 2, why, sizeof(why));
    printf("     %s\n", why);
    expect("F2 a hat whose X code (18) reports 0..255: refused, naming the port, the device, the input, the code "
           "and the range", r2 && has(why, "port 2") && has(why, "'pad'") && has(why, "pad.hat1") &&
           has(why, "ABS 18") && has(why, "0..255"));
    SimInput hat_y[] = { other_in("hat0", SIM_IN_HAT, ABS_HAT0X) };
    d = bound_dev("pad", PAD_H, 1, hat_y, b, 1);
    int r3 = !sim_evdev_reader_covers(h, &d, 2, why, sizeof(why));
    printf("     %s\n", why);
    expect("F3 a hat whose Y code (17) reports 0..255: refused, naming code 17",
           r3 && has(why, "pad.hat0") && has(why, "ABS 17") && has(why, "0..255"));
    SimInput trig[] = { axis_in("trigger", ABS_HAT1X, 0, 0, 255) };
    d = bound_dev("pad", PAD_H, 1, trig, b, 1);
    expect("F4 an axis declared on a hat code (18, 0..255): accepted, read as an axis",
           sim_evdev_reader_covers(h, &d, 2, why, sizeof(why)));
    sim_evdev_close(h);
}

#define KBD_K "pci-0000:00:14.0-usb-0:7:1.0"

static void part_g(void)
{
    printf("== G. a device with no match reads only a joystick-kind unit\n");
    char why[1024];
    node(11, 0600);
    link_to(KBD_K "-event-kbd", "../event11");
    sys_event("event11", "13:75", "2341", "8036", "a keyboard-kind unit with an axis", "1", "0");
    SimBinding b[1];
    SimInput ax[] = { axis_in("x", ABS_X, 0, 0, 255) };
    SimPortDevice d = bound_dev("my_box", KBD_K, 1, ax, b, 1);
    int r1 = !sim_evdev_check_device(&d, 1, why, sizeof(why));
    printf("     %s\n", why);
    expect("G1 no match, the unit at the port path is keyboard-kind only: refused, naming the port, the device and "
           "the unit's kind", r1 && has(why, "port 1") && has(why, "'my_box'") && has(why, "keyboard"));
    d.has_match = true;
    d.vid = 0x2341;
    d.pid = 0x8036;
    expect("G2 the same unit named by match: readable (match decides, as in the app)",
           sim_evdev_check_device(&d, 1, why, sizeof(why)));
    SimInput sx[] = { axis_in("x", ABS_X, 0, 8192, 16383) };
    d = bound_dev("my_stick", STICK_A, 1, sx, b, 1);
    expect("G3 no match on a joystick-kind unit: readable", sim_evdev_check_device(&d, 1, why, sizeof(why)));
    SimInput cx[] = { axis_in("x", ABS_X, 0, 128, 255) };
    d = bound_dev("my_pad", PAD_C, 1, cx, b, 1);
    expect("G4 no match on a unit with a joystick node beside another kind: readable",
           sim_evdev_check_device(&d, 1, why, sizeof(why)));
}

#define NINE_N "pci-0000:00:14.0-usb-0:8:1.0"
#define MANY_M "pci-0000:00:14.0-usb-0:9:2.0"

static void many_nodes(const char *pp, int first, int n, const char *abs)
{
    char ev[32], devno[32], rec[64], text[256], lk[256];
    for (int i = 0; i < n; i++) {
        int k = first + i;
        node(k, 0600);
        snprintf(ev, sizeof(ev), "event%d", k);
        snprintf(devno, sizeof(devno), "13:%d", 100 + k);
        sys_event(ev, devno, "1234", "abcd", "a unit of many nodes", i == n - 1 ? abs : "0", "0");
        if (i == 0) {
            snprintf(lk, sizeof(lk), "%s-event-joystick", pp);
            snprintf(text, sizeof(text), "../%s", ev);
            link_to(lk, text);
        } else {
            snprintf(rec, sizeof(rec), "udev/c%s", devno);
            snprintf(text, sizeof(text), "E:ID_INPUT=1\nE:ID_PATH=%s\n", pp);
            write_file(rec, text);
        }
    }
}

static void part_h(void)
{
    printf("== H. a unit with many event nodes\n");
    char why[1024];
    many_nodes(NINE_N, 20, 9, "4");
    write_file("absinfo/event28", "2 77 0 255\n");
    SimBinding b[1];
    SimInput z[] = { axis_in("z", ABS_Z, 0, 0, 255) };
    SimPortDevice d = bound_dev("nine", NINE_N, 1, z, b, 1);
    d.has_match = true;
    d.vid = 0x1234;
    d.pid = 0xabcd;
    int ok1 = sim_evdev_check_device(&d, 1, why, sizeof(why));
    if (!ok1) {
        printf("     %s\n", why);
    }
    expect("H1 a unit of nine nodes whose axis is on the ninth: readable", ok1);
    SimEvdevReader *r = open_at(NINE_N, why, sizeof(why));
    int v = 0, lo, hi;
    expect("H2 ... opened, the ninth node owns ABS_Z (77 in 0..255)", r && sim_evdev_reader_covers(r, &d, 1, why,
           sizeof(why)) && sim_evdev_reader_abs(r, ABS_Z, &v, &lo, &hi) && v == 77 && hi == 255);
    sim_evdev_close(r);
    many_nodes(MANY_M, 30, 33, "4");
    write_file("absinfo/event62", "2 0 0 255\n");
    d.port_path = MANY_M;
    int r3 = !sim_evdev_check_device(&d, 1, why, sizeof(why));
    printf("     %s\n", why);
    expect("H3 a unit of 33 nodes, more than this build reads: refused loudly, naming the unit",
           r3 && has(why, MANY_M) && has(why, "event nodes"));
    SimEvdevUnit u;
    SimEvdevReader *m = sim_evdev_find(MANY_M, &u) ? sim_evdev_open(&u, why, sizeof(why)) : NULL;
    expect("H4 ... and the reader will not open part of it", !m && has(why, "event nodes"));
    sim_evdev_close(m);
}

static void part_i(void)
{
    printf("== I. codes outside evdev's tables (run under UBSan by make check)\n");
    char why[1024];
    SimBinding b[1];
    SimInput hat[] = { other_in("hat", SIM_IN_HAT, 2147483647) };
    SimPortDevice d = bound_dev("t16000m", STICK_A, 1, hat, b, 1);
    int r1 = !sim_evdev_check_device(&d, 1, why, sizeof(why));
    printf("     %s\n", why);
    expect("I1 a bound hat whose code is 2147483647: refused, naming the input and the code",
           r1 && has(why, "t16000m.hat") && has(why, "2147483647") && has(why, "outside"));
    SimEvdevReader *a = open_at(STICK_A, why, sizeof(why));
    expect("I2 ... and the reader refuses it too", a && !sim_evdev_reader_covers(a, &d, 1, why, sizeof(why)) &&
           has(why, "outside"));
    sim_evdev_close(a);
    SimInput hat63[] = { other_in("hat", SIM_IN_HAT, 63) };
    d = bound_dev("t16000m", STICK_A, 1, hat63, b, 1);
    expect("I3 a hat on code 63 (its Y would be 64, past ABS_MAX): refused",
           !sim_evdev_check_device(&d, 1, why, sizeof(why)) && has(why, "outside"));
    SimInput ax64[] = { axis_in("x", 64, 0, 0, 255) };
    d = bound_dev("t16000m", STICK_A, 1, ax64, b, 1);
    expect("I4 an axis on code 64 (past ABS_MAX 63): refused", !sim_evdev_check_device(&d, 1, why, sizeof(why)) &&
           has(why, "outside") && has(why, "64"));
    SimInput btn[] = { other_in("b", SIM_IN_BUTTON, 768) };
    d = bound_dev("t16000m", STICK_A, 1, btn, b, 1);
    expect("I5 a button on code 768 (past KEY_MAX 767): refused", !sim_evdev_check_device(&d, 1, why, sizeof(why)) &&
           has(why, "outside") && has(why, "768"));
    SimInput neg[] = { other_in("b", SIM_IN_BUTTON, -5) };
    d = bound_dev("t16000m", STICK_A, 1, neg, b, 1);
    expect("I6 a negative code other than 'none' (-5): refused", !sim_evdev_check_device(&d, 1, why, sizeof(why)) &&
           has(why, "outside") && has(why, "-5"));
}

static void part_j(void)
{
    printf("== J. sdl_gamepad, read as sdl_joystick\n");
    char why[1024];
    SimBinding b[1];
    SimInput x[] = { axis_in("stick_x", ABS_X, 0, 8192, 16383) };
    SimPortDevice d = bound_dev("a_gpad", STICK_A, 1, x, b, 1);
    d.source = SIM_SRC_SDL_GAMEPAD;
    d.has_match = true;
    d.vid = 0x044f;
    d.pid = 0xb10a;
    expect("J1 an sdl_gamepad device on the stick: readable", sim_evdev_check_device(&d, 1, why, sizeof(why)));
    SimEvdevReader *a = open_at(STICK_A, why, sizeof(why));
    expect("J2 ... and its reader covers it", a && sim_evdev_reader_covers(a, &d, 1, why, sizeof(why)));
    SimPortDevice np = d;
    np.port_path = NULL;
    expect("J3 no port path: refused as a joystick", !sim_evdev_check_device(&np, 1, why, sizeof(why)) &&
           has(why, "'a_gpad' has no port path"));
    SimInput nc[] = { other_in("trigger", SIM_IN_BUTTON, -1) };
    SimPortDevice dn = bound_dev("a_gpad", STICK_A, 1, nc, b, 1);
    dn.source = SIM_SRC_SDL_GAMEPAD;
    expect("J4 a bound input with no code: refused as a joystick", !sim_evdev_check_device(&dn, 1, why, sizeof(why)) &&
           has(why, "a_gpad.trigger has no evdev code"));
    SimInput wide[] = { axis_in("stick_x", ABS_X, -32768, 0, 32767) };
    SimPortDevice dw = bound_dev("a_gpad", STICK_A, 1, wide, b, 1);
    dw.source = SIM_SRC_SDL_GAMEPAD;
    expect("J5 a range mismatch: refused as a joystick", a && !sim_evdev_reader_covers(a, &dw, 1, why, sizeof(why)) &&
           has(why, "-32768..32767") && has(why, "0..16383"));
    sim_evdev_close(a);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: simevdev-test WORK_DIR\n");
        return 2;
    }
    if (geteuid() == 0) {
        fprintf(stderr, "simevdev-test: run it as a user, not root (root reads a denied node)\n");
        return 2;
    }
    char cmd[2048];
    snprintf(W, sizeof(W), "%s/tree", argv[1]);
    mkdir(argv[1], 0755);
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", W);
    if (system(cmd) != 0) {
        fprintf(stderr, "cannot clear %s\n", W);
        return 2;
    }
    mkdir(W, 0755);
    build_tree();
    char dev[2048], byp[2048], sys[2048], udev[2048];
    path(dev, sizeof(dev), "dev");
    path(byp, sizeof(byp), "dev/by-path");
    path(sys, sizeof(sys), "sys");
    path(udev, sizeof(udev), "udev");
    SimEvdevRoots roots = { dev, byp, sys, udev };
    sim_evdev_set_roots(&roots);
    SimEvdevIo io = { fake_absinfo, fake_keys };
    sim_evdev_set_io(&io);
    part_a();
    part_b();
    part_c();
    part_d();
    part_e();
    part_f();
    part_g();
    part_h();
    part_i();
    part_j();
    printf("%d checks, %d failed\n", nchecks, nfailed);
    return nfailed ? 1 : 0;
}
