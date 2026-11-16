/*
 * PenguinBox — joysticks, wheels, pedals and button boxes read by port path (see xemu-sim-evdev.h)
 *
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xemu-sim-evdev.h"
#include "xemu-sim-merge.h"

#define SIM_EV_KEY 1
#define SIM_EV_ABS 3

#ifdef __linux__

#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/input.h>

static void copy_str(char *dst, size_t len, const char *src)
{
    size_t n = strlen(src);
    if (n >= len) {
        n = len - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static bool input_bound(const SimPortDevice *dev, const SimInput *in)
{
    for (int k = 0; k < dev->nbindings; k++) {
        if (dev->bindings[k].source == in || dev->bindings[k].recentre == in) {
            return true;
        }
    }
    return false;
}

static const char *kind_name(SimInputKind k)
{
    switch (k) {
    case SIM_IN_AXIS: return "axis";
    case SIM_IN_BUTTON: return "button";
    case SIM_IN_HAT: return "hat";
    case SIM_IN_DETENT: return "detent";
    case SIM_IN_RELATIVE: return "relative";
    }
    return "?";
}

static SimEvdevRoots g_roots;
static SimEvdevIo g_io;

void sim_evdev_set_roots(const SimEvdevRoots *roots)
{
    memset(&g_roots, 0, sizeof(g_roots));
    if (roots) {
        g_roots = *roots;
    }
}

static const char *dev_input(void) { return g_roots.dev_input ? g_roots.dev_input : "/dev/input"; }
static const char *by_path(void) { return g_roots.by_path ? g_roots.by_path : "/dev/input/by-path"; }
static const char *sys_input(void) { return g_roots.sys_input ? g_roots.sys_input : "/sys/class/input"; }
static const char *udev_data(void) { return g_roots.udev_data ? g_roots.udev_data : "/run/udev/data"; }

static int real_absinfo(int fd, const char *node, int code, int32_t info[6])
{
    (void)node;
    struct input_absinfo a;
    if (ioctl(fd, EVIOCGABS(code), &a) < 0) {
        return -1;
    }
    info[0] = a.value;
    info[1] = a.minimum;
    info[2] = a.maximum;
    info[3] = a.fuzz;
    info[4] = a.flat;
    info[5] = a.resolution;
    return 0;
}

static int real_keys(int fd, const char *node, uint8_t *bits, size_t len)
{
    (void)node;
    return ioctl(fd, EVIOCGKEY(len), bits) < 0 ? -1 : 0;
}

void sim_evdev_set_io(const SimEvdevIo *io)
{
    memset(&g_io, 0, sizeof(g_io));
    if (io) {
        g_io = *io;
    }
}

static int do_absinfo(int fd, const char *node, int code, int32_t info[6])
{
    return (g_io.absinfo ? g_io.absinfo : real_absinfo)(fd, node, code, info);
}

static int do_keys(int fd, const char *node, uint8_t *bits, size_t len)
{
    return (g_io.keys ? g_io.keys : real_keys)(fd, node, bits, len);
}

static bool join(char *dst, size_t len, const char *a, const char *b, const char *c)
{
    int n = c ? snprintf(dst, len, "%s/%s/%s", a, b, c) : snprintf(dst, len, "%s/%s", a, b);
    return n > 0 && (size_t)n < len;
}

static void read_text(const char *path, char *out, size_t len)
{
    out[0] = '\0';
    FILE *f = fopen(path, "r");
    if (!f) {
        return;
    }
    size_t n = fread(out, 1, len - 1, f);
    fclose(f);
    out[n] = '\0';
    while (n > 0 && isspace((unsigned char)out[n - 1])) {
        out[--n] = '\0';
    }
}

static void sys_read(const char *event, const char *rel, char *out, size_t len)
{
    char p[SIM_EVDEV_PATH];
    out[0] = '\0';
    if (join(p, sizeof(p), sys_input(), event, rel)) {
        read_text(p, out, len);
    }
}

static int hex_id(const char *s)
{
    char *end = NULL;
    long v = s[0] ? strtol(s, &end, 16) : -1;
    return (s[0] && end && *end == '\0' && v >= 0 && v <= 0xffff) ? (int)v : -1;
}

static bool bitmap_has(const char *text, int bit)
{
    const int wbits = (int)(sizeof(unsigned long) * 8);
    int nwords = 0;
    char copy[2048];
    copy_str(copy, sizeof(copy), text);
    char *words[256];
    for (char *save = NULL, *t = strtok_r(copy, " \t\n", &save); t && nwords < 256;
         t = strtok_r(NULL, " \t\n", &save)) {
        words[nwords++] = t;
    }
    int w = bit / wbits;
    if (bit < 0 || w >= nwords) {
        return false;
    }
    unsigned long long v = strtoull(words[nwords - 1 - w], NULL, 16);
    return (v >> (bit % wbits)) & 1;
}

static const struct {
    const char *suffix;
    unsigned kind;
} LINK_KINDS[] = {
    { "-event-joystick", SIM_EVDEV_KIND_JOYSTICK },
    { "-event-kbd", SIM_EVDEV_KIND_KEYBOARD },
    { "-event-mouse", SIM_EVDEV_KIND_MOUSE },
    { "-event", SIM_EVDEV_KIND_OTHER },
};

static int kind_rank(unsigned kind)
{
    for (int i = 0; i < 4; i++) {
        if (LINK_KINDS[i].kind == kind) {
            return i;
        }
    }
    return 4;
}

static void usb_revision_form(const char *in, char *out, size_t len)
{
    size_t o = 0;
    for (const char *p = in; *p && o + 1 < len;) {
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

bool sim_evdev_link_port_path(const char *link, char *port_path, size_t len, unsigned *kind)
{
    size_t ll = strlen(link);
    for (int i = 0; i < 4; i++) {
        size_t sl = strlen(LINK_KINDS[i].suffix);
        if (ll > sl && !strcmp(link + ll - sl, LINK_KINDS[i].suffix)) {
            char stem[512];
            if (ll - sl >= sizeof(stem)) {
                return false;
            }
            memcpy(stem, link, ll - sl);
            stem[ll - sl] = '\0';
            usb_revision_form(stem, port_path, len);
            if (kind) {
                *kind = LINK_KINDS[i].kind;
            }
            return true;
        }
    }
    return false;
}

typedef struct ScanUnit {
    SimEvdevUnit u;
    int rank;
    char describing[32];
} ScanUnit;

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static int cmp_unit(const void *a, const void *b)
{
    return strcmp(((const ScanUnit *)a)->u.port_path, ((const ScanUnit *)b)->u.port_path);
}

static char **list_dir(const char *dir, const char *prefix, int *n)
{
    *n = 0;
    DIR *d = opendir(dir);
    if (!d) {
        return NULL;
    }
    int cap = 16;
    char **names = malloc(sizeof(char *) * cap);
    struct dirent *e;
    while (names && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' || (prefix && strncmp(e->d_name, prefix, strlen(prefix)))) {
            continue;
        }
        if (*n == cap) {
            char **grown = realloc(names, sizeof(char *) * (cap *= 2));
            if (!grown) {
                break;
            }
            names = grown;
        }
        names[(*n)++] = strdup(e->d_name);
    }
    closedir(d);
    if (names) {
        qsort(names, *n, sizeof(char *), cmp_str);
    }
    return names;
}

static void free_list(char **names, int n)
{
    for (int i = 0; i < n; i++) {
        free(names[i]);
    }
    free(names);
}

static ScanUnit *unit_for(ScanUnit **units, int *n, int *cap, const char *port_path)
{
    for (int i = 0; i < *n; i++) {
        if (!strcmp((*units)[i].u.port_path, port_path)) {
            return &(*units)[i];
        }
    }
    if (*n == *cap) {
        ScanUnit *grown = realloc(*units, sizeof(ScanUnit) * (*cap *= 2));
        if (!grown) {
            return NULL;
        }
        *units = grown;
    }
    ScanUnit *s = &(*units)[(*n)++];
    memset(s, 0, sizeof(*s));
    copy_str(s->u.port_path, sizeof(s->u.port_path), port_path);
    s->rank = 99;
    return s;
}

static bool has_node(const SimEvdevUnit *u, const char *node)
{
    for (int i = 0; i < u->nnodes; i++) {
        if (!strcmp(u->node[i], node)) {
            return true;
        }
    }
    return false;
}

static void add_node(SimEvdevUnit *u, const char *node, const char *event)
{
    if (has_node(u, node)) {
        return;
    }
    if (u->nnodes == SIM_EVDEV_MAX_NODES) {
        u->too_many = true;
        return;
    }
    copy_str(u->node[u->nnodes], SIM_EVDEV_PATH, node);
    copy_str(u->event[u->nnodes], sizeof(u->event[0]), event);
    u->nnodes++;
}

int sim_evdev_scan(SimEvdevUnit **out, char *why, size_t whylen)
{
    *out = NULL;
    int n = 0, cap = 8;
    ScanUnit *units = calloc(cap, sizeof(ScanUnit));
    if (!units) {
        snprintf(why, whylen, "out of memory");
        return -1;
    }
    int nlinks = 0;
    char **links = list_dir(by_path(), NULL, &nlinks);
    for (int i = 0; links && i < nlinks; i++) {
        char port_path[256], lp[SIM_EVDEV_PATH], node[PATH_MAX];
        unsigned kind;
        if (!sim_evdev_link_port_path(links[i], port_path, sizeof(port_path), &kind) ||
            !join(lp, sizeof(lp), by_path(), links[i], NULL) || !realpath(lp, node)) {
            continue;
        }
        const char *event = strrchr(node, '/') ? strrchr(node, '/') + 1 : node;
        if (strncmp(event, "event", 5)) {
            continue;
        }
        ScanUnit *s = unit_for(&units, &n, &cap, port_path);
        if (!s) {
            break;
        }
        if (s->u.nnodes == 0 || kind_rank(kind) < s->rank) {
            s->rank = kind_rank(kind);
            copy_str(s->describing, sizeof(s->describing), event);
        }
        s->u.kinds |= kind;
        add_node(&s->u, node, event);
    }
    int nevents = 0;
    char **events = list_dir(sys_input(), "event", &nevents);
    if (!links && !events) {
        snprintf(why, whylen, "could not list input devices (%s, %s): %s", by_path(), sys_input(), strerror(errno));
        free(units);
        return -1;
    }
    for (int i = 0; events && i < nevents; i++) {
        bool seen = false;
        for (int k = 0; k < n && !seen; k++) {
            for (int j = 0; j < units[k].u.nnodes && !seen; j++) {
                seen = !strcmp(units[k].u.event[j], events[i]);
            }
        }
        if (seen) {
            continue;
        }
        char devno[64], rec[PATH_MAX], db[8192], node[SIM_EVDEV_PATH];
        sys_read(events[i], "dev", devno, sizeof(devno));
        if (!devno[0] || snprintf(rec, sizeof(rec), "%s/c%s", udev_data(), devno) >= (int)sizeof(rec)) {
            continue;
        }
        read_text(rec, db, sizeof(db));
        const char *id = NULL;
        for (char *save = NULL, *line = strtok_r(db, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            if (!strncmp(line, "E:ID_PATH=", 10)) {
                id = line + 10;
                break;
            }
        }
        if (!id || !*id || !join(node, sizeof(node), dev_input(), events[i], NULL)) {
            continue;
        }
        ScanUnit *s = unit_for(&units, &n, &cap, id);
        if (!s) {
            break;
        }
        if (s->u.nnodes == 0) {
            copy_str(s->describing, sizeof(s->describing), events[i]);
        }
        add_node(&s->u, node, events[i]);
    }
    free_list(links, nlinks);
    free_list(events, nevents);
    qsort(units, n, sizeof(ScanUnit), cmp_unit);
    SimEvdevUnit *res = calloc(n ? n : 1, sizeof(SimEvdevUnit));
    if (!res) {
        free(units);
        snprintf(why, whylen, "out of memory");
        return -1;
    }
    for (int i = 0; i < n; i++) {
        SimEvdevUnit *u = &res[i];
        const SimEvdevUnit *s = &units[i].u;
        copy_str(u->port_path, sizeof(u->port_path), s->port_path);
        u->kinds = s->kinds ? s->kinds : SIM_EVDEV_KIND_OTHER;
        u->too_many = s->too_many;
        char t[128];
        sys_read(units[i].describing, "device/id/vendor", t, sizeof(t));
        u->vid = hex_id(t);
        sys_read(units[i].describing, "device/id/product", t, sizeof(t));
        u->pid = hex_id(t);
        sys_read(units[i].describing, "device/name", u->name, sizeof(u->name));
        for (int pass = 0; pass < 2; pass++) {
            for (int j = 0; j < s->nnodes; j++) {
                if ((strcmp(s->event[j], units[i].describing) == 0) == (pass == 0)) {
                    add_node(u, s->node[j], s->event[j]);
                }
            }
        }
    }
    free(units);
    *out = res;
    return n;
}

bool sim_evdev_find(const char *port_path, SimEvdevUnit *out)
{
    SimEvdevUnit *units = NULL;
    char why[256];
    int n = sim_evdev_scan(&units, why, sizeof(why));
    bool found = false;
    for (int i = 0; i < n && !found; i++) {
        if (!strcmp(units[i].port_path, port_path)) {
            *out = units[i];
            found = true;
        }
    }
    free(units);
    return found;
}

bool sim_evdev_node_reports(const SimEvdevUnit *u, int i, int type, int code)
{
    if (i < 0 || i >= u->nnodes || code < 0) {
        return false;
    }
    char caps[2048];
    sys_read(u->event[i], type == SIM_EV_ABS ? "device/capabilities/abs" : "device/capabilities/key", caps,
             sizeof(caps));
    return bitmap_has(caps, code);
}

static bool unit_reports(const SimEvdevUnit *u, int type, int code)
{
    for (int i = 0; i < u->nnodes; i++) {
        if (sim_evdev_node_reports(u, i, type, code)) {
            return true;
        }
    }
    return false;
}

static bool device_matches(const SimPortDevice *dev, const SimEvdevUnit *u)
{
    if (!dev->has_match) {
        return true;
    }
    if (u->vid == dev->vid && u->pid == dev->pid) {
        return true;
    }
    for (int i = 0; i < dev->nmatch_also; i++) {
        if (u->vid == dev->match_also[2 * i] && u->pid == dev->match_also[2 * i + 1]) {
            return true;
        }
    }
    return false;
}

static bool check_inputs(const SimPortDevice *dev, int port, char *why, size_t whylen)
{
    for (int i = 0; i < dev->ninputs; i++) {
        const SimInput *in = &dev->inputs[i];
        if (!input_bound(dev, in)) {
            continue;
        }
        if (in->kind != SIM_IN_AXIS && in->kind != SIM_IN_BUTTON && in->kind != SIM_IN_HAT) {
            snprintf(why, whylen, "port %d: %s.%s is a %s input, and PenguinBox reads only axes, buttons and hats "
                     "from a joystick-class device (the device '%s')", port, dev->device_id, in->id,
                     kind_name(in->kind), dev->device_id);
            return false;
        }
        if (in->code == -1) {
            snprintf(why, whylen, "port %d: %s.%s has no evdev code, so PenguinBox cannot read it (the device '%s'; "
                     "capture its code in the Sim Configurator)", port, dev->device_id, in->id, dev->device_id);
            return false;
        }
        const int last = in->kind == SIM_IN_BUTTON ? 0x2ff : in->kind == SIM_IN_HAT ? 0x3e : 0x3f;
        if (in->code < 0 || in->code > last) {
            snprintf(why, whylen, "port %d: %s.%s has the code %d, outside evdev's %s codes 0..%d, so PenguinBox "
                     "cannot read it (the device '%s')", port, dev->device_id, in->id, in->code,
                     in->kind == SIM_IN_BUTTON ? "button" : in->kind == SIM_IN_HAT ? "hat (X; its Y is the next)" :
                     "axis", last, dev->device_id);
            return false;
        }
    }
    return true;
}

static void code_text(const SimInput *in, char *out, size_t len)
{
    if (in->kind == SIM_IN_BUTTON) {
        snprintf(out, len, "the button code %d", in->code);
    } else if (in->kind == SIM_IN_HAT) {
        snprintf(out, len, "the hat codes %d and %d", in->code, in->code + 1);
    } else {
        snprintf(out, len, "the axis code %d", in->code);
    }
}

bool sim_evdev_check_device(const SimPortDevice *dev, int port, char *why, size_t whylen)
{
    if (!dev->port_path || !*dev->port_path) {
        snprintf(why, whylen, "port %d: the device '%s' has no port path, so PenguinBox cannot tell which unit to "
                 "read (assign it to a unit in the Sim Configurator)", port, dev->device_id);
        return false;
    }
    if (!check_inputs(dev, port, why, whylen)) {
        return false;
    }
    SimEvdevUnit u;
    if (!sim_evdev_find(dev->port_path, &u)) {
        snprintf(why, whylen, "port %d: no unit is plugged in at the port path '%s' (the device '%s')", port,
                 dev->port_path, dev->device_id);
        return false;
    }
    if (u.too_many) {
        snprintf(why, whylen, "port %d: the unit at '%s' (%s) has more than %d event nodes, more than PenguinBox "
                 "reads, so it is not read in part (the device '%s')", port, dev->port_path,
                 u.name[0] ? u.name : "no name", SIM_EVDEV_MAX_NODES, dev->device_id);
        return false;
    }
    if (!device_matches(dev, &u)) {
        snprintf(why, whylen, "port %d: the unit at '%s' is %04x:%04x (%s), not the device '%s' (%04x:%04x)", port,
                 dev->port_path, u.vid < 0 ? 0 : u.vid, u.pid < 0 ? 0 : u.pid, u.name[0] ? u.name : "no name",
                 dev->device_id, dev->vid, dev->pid);
        return false;
    }
    if (!dev->has_match && !(u.kinds & SIM_EVDEV_KIND_JOYSTICK)) {
        snprintf(why, whylen, "port %d: the device '%s' names no match, so only a joystick can be read as it, and "
                 "the unit at '%s' (%s) is %s%s%s(give the device a match, or assign a joystick to the port)", port,
                 dev->device_id, dev->port_path, u.name[0] ? u.name : "no name",
                 u.kinds & SIM_EVDEV_KIND_KEYBOARD ? "a keyboard " : "", u.kinds & SIM_EVDEV_KIND_MOUSE ? "a mouse " : "",
                 u.kinds & SIM_EVDEV_KIND_OTHER ? "an other-kind unit " : "");
        return false;
    }
    for (int i = 0; i < dev->ninputs; i++) {
        const SimInput *in = &dev->inputs[i];
        if (!input_bound(dev, in)) {
            continue;
        }
        bool ok = in->kind == SIM_IN_BUTTON ? unit_reports(&u, SIM_EV_KEY, in->code) :
                  in->kind == SIM_IN_HAT ? unit_reports(&u, SIM_EV_ABS, in->code) &&
                                           unit_reports(&u, SIM_EV_ABS, in->code + 1) :
                  unit_reports(&u, SIM_EV_ABS, in->code);
        if (!ok) {
            char c[64];
            code_text(in, c, sizeof(c));
            snprintf(why, whylen, "port %d: the unit at '%s' (%s) does not report %s, which %s.%s names", port,
                     dev->port_path, u.name[0] ? u.name : "no name", c, dev->device_id, in->id);
            return false;
        }
    }
    return true;
}

#define SIM_ABS_CNT 0x40
#define SIM_KEY_CNT 0x300

struct SimEvdevReader {
    SimEvdevUnit unit;
    int fd[SIM_EVDEV_MAX_NODES];
    int8_t owner[SIM_ABS_CNT];
    int32_t abs[SIM_ABS_CNT][3];
    uint8_t keys[SIM_KEY_CNT / 8];
    bool gone;
    char skipped[1024];
};

static void append(char *dst, size_t len, const char *s)
{
    size_t have = strlen(dst);
    if (have + 1 < len) {
        copy_str(dst + have, len - have, s);
    }
}

static void read_keys(SimEvdevReader *r)
{
    memset(r->keys, 0, sizeof(r->keys));
    for (int i = 0; i < r->unit.nnodes; i++) {
        uint8_t bits[SIM_KEY_CNT / 8] = { 0 };
        if (r->fd[i] >= 0 && do_keys(r->fd[i], r->unit.node[i], bits, sizeof(bits)) == 0) {
            for (size_t b = 0; b < sizeof(bits); b++) {
                r->keys[b] |= bits[b];
            }
        }
    }
}

static void reread_abs(SimEvdevReader *r)
{
    for (int c = 0; c < SIM_ABS_CNT; c++) {
        int32_t info[6];
        int i = r->owner[c];
        if (i >= 0 && r->fd[i] >= 0 && do_absinfo(r->fd[i], r->unit.node[i], c, info) == 0) {
            r->abs[c][0] = info[0];
        }
    }
}

SimEvdevReader *sim_evdev_open(const SimEvdevUnit *u, char *why, size_t whylen)
{
    SimEvdevReader *r = calloc(1, sizeof(*r));
    if (!r) {
        snprintf(why, whylen, "out of memory");
        return NULL;
    }
    if (u->too_many) {
        snprintf(why, whylen, "the unit at '%s' has more than %d event nodes, more than PenguinBox reads, so it is "
                 "not read in part", u->port_path, SIM_EVDEV_MAX_NODES);
        free(r);
        return NULL;
    }
    r->unit = *u;
    memset(r->owner, -1, sizeof(r->owner));
    int nopen = 0;
    for (int i = 0; i < SIM_EVDEV_MAX_NODES; i++) {
        r->fd[i] = -1;
    }
    for (int i = 0; i < u->nnodes; i++) {
        int fd = open(u->node[i], O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            char t[SIM_EVDEV_PATH + 160];
            int e = errno;
            snprintf(t, sizeof(t), "%s%s: %s", r->skipped[0] ? "; " : "", u->node[i],
                     (e == EACCES || e == EPERM) ? "no permission to read it (udev gives this user no access to it)" :
                     (e == ENOENT || e == ENODEV || e == ENXIO) ? "no longer plugged in" : strerror(e));
            append(r->skipped, sizeof(r->skipped), t);
            continue;
        }
        r->fd[i] = fd;
        nopen++;
        char caps[2048];
        sys_read(u->event[i], "device/capabilities/abs", caps, sizeof(caps));
        for (int c = 0; c < SIM_ABS_CNT; c++) {
            int32_t info[6];
            if (r->owner[c] >= 0 || !bitmap_has(caps, c) ||
                do_absinfo(fd, u->node[i], c, info) != 0) {
                continue;
            }
            r->owner[c] = (int8_t)i;
            r->abs[c][0] = info[0];
            r->abs[c][1] = info[1];
            r->abs[c][2] = info[2];
        }
    }
    if (!nopen) {
        snprintf(why, whylen, "no node of the unit at '%s' could be opened: %s", u->port_path,
                 r->skipped[0] ? r->skipped : "it has no event node");
        free(r);
        return NULL;
    }
    read_keys(r);
    return r;
}

const char *sim_evdev_reader_skipped(const SimEvdevReader *r)
{
    return r->skipped;
}

static bool reader_has_key(const SimEvdevReader *r, int code)
{
    for (int i = 0; i < r->unit.nnodes; i++) {
        if (r->fd[i] >= 0 && sim_evdev_node_reports(&r->unit, i, SIM_EV_KEY, code)) {
            return true;
        }
    }
    return false;
}

static bool reader_has_abs(const SimEvdevReader *r, int code)
{
    return code >= 0 && code < SIM_ABS_CNT && r->owner[code] >= 0;
}

bool sim_evdev_reader_covers(const SimEvdevReader *r, const SimPortDevice *dev, int port, char *why, size_t whylen)
{
    if (!check_inputs(dev, port, why, whylen)) {
        return false;
    }
    for (int i = 0; i < dev->ninputs; i++) {
        const SimInput *in = &dev->inputs[i];
        if (!input_bound(dev, in)) {
            continue;
        }
        bool ok = in->kind == SIM_IN_BUTTON ? reader_has_key(r, in->code) :
                  in->kind == SIM_IN_HAT ? reader_has_abs(r, in->code) && reader_has_abs(r, in->code + 1) :
                  reader_has_abs(r, in->code);
        if (!ok) {
            char c[64];
            code_text(in, c, sizeof(c));
            snprintf(why, whylen, "port %d: %s of %s.%s cannot be read from the unit at '%s'%s%s", port, c,
                     dev->device_id, in->id, r->unit.port_path, r->skipped[0] ? ": " : "", r->skipped);
            return false;
        }
        if (in->kind == SIM_IN_AXIS) {
            const int32_t *a = r->abs[in->code];
            double flo = in->range.min < in->range.max ? in->range.min : in->range.max;
            double fhi = in->range.min < in->range.max ? in->range.max : in->range.min;
            if (flo != (double)a[1] || fhi != (double)a[2]) {
                snprintf(why, whylen, "port %d: the device file gives %s.%s the range %g..%g, but the unit at '%s' "
                         "reports %d..%d on ABS %d (the device '%s'): the device file's range is not this unit's; "
                         "open the device in the Sim Configurator's Device Studio with it plugged in, then record its "
                         "calibration again", port, dev->device_id, in->id, flo, fhi, r->unit.port_path, (int)a[1],
                         (int)a[2], in->code, dev->device_id);
                return false;
            }
        }
        for (int k = 0; in->kind == SIM_IN_HAT && k < 2; k++) {
            const int32_t *a = r->abs[in->code + k];
            if (a[1] < -1 || a[2] > 1) {
                snprintf(why, whylen, "port %d: the device file declares %s.%s a hat, but the unit at '%s' reports "
                         "%d..%d on its %s code, ABS %d, a wider range than a hat's -1..1 (the device '%s'): "
                         "declare that control as an axis in the Sim Configurator's Device Studio", port,
                         dev->device_id, in->id, r->unit.port_path, (int)a[1], (int)a[2], k ? "Y" : "X",
                         in->code + k, dev->device_id);
                return false;
            }
        }
    }
    return true;
}

static void mark_gone(SimEvdevReader *r)
{
    for (int i = 0; i < r->unit.nnodes; i++) {
        if (r->fd[i] >= 0) {
            close(r->fd[i]);
            r->fd[i] = -1;
        }
    }
    r->gone = true;
}

int sim_evdev_reader_poll(SimEvdevReader *r)
{
    if (r->gone) {
        return -1;
    }
    int applied = 0;
    bool resync = false;
    for (int i = 0; i < r->unit.nnodes && !r->gone; i++) {
        while (r->fd[i] >= 0) {
            struct input_event ev[64];
            ssize_t n = read(r->fd[i], ev, sizeof(ev));
            if (n < 0 && errno == EINTR) {
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break;
            }
            if (n <= 0) {
                mark_gone(r);
                return -1;
            }
            for (size_t k = 0; k < (size_t)n / sizeof(ev[0]); k++) {
                const struct input_event *e = &ev[k];
                if (e->type == EV_ABS && e->code < SIM_ABS_CNT) {
#ifndef SIM_EVDEV_DRILL_ANY_NODE
                    if (r->owner[e->code] == i) {
#else
                    /* the RED drill: an ABS event taken from any node of the unit (a motion sensor's ABS_X) */
                    if (r->owner[e->code] >= 0) {
#endif
                        r->abs[e->code][0] = e->value;
                        applied++;
                    }
                } else if (e->type == EV_KEY && e->code < SIM_KEY_CNT) {
                    if (e->value) {
                        r->keys[e->code / 8] |= (uint8_t)(1u << (e->code % 8));
                    } else {
                        r->keys[e->code / 8] &= (uint8_t)~(1u << (e->code % 8));
                    }
                    applied++;
                } else if (e->type == EV_SYN && e->code == SYN_DROPPED) {
                    resync = true;
                }
            }
        }
    }
    if (resync) {
        reread_abs(r);
        read_keys(r);
    }
    return applied;
}

bool sim_evdev_reader_gone(const SimEvdevReader *r)
{
    return r->gone;
}

bool sim_evdev_reader_abs(const SimEvdevReader *r, int code, int *value, int *min, int *max)
{
    if (!reader_has_abs(r, code)) {
        return false;
    }
    *value = r->abs[code][0];
    *min = r->abs[code][1];
    *max = r->abs[code][2];
    return true;
}

int sim_evdev_reader_key(const SimEvdevReader *r, int code)
{
    return (code >= 0 && code < SIM_KEY_CNT) ? (r->keys[code / 8] >> (code % 8)) & 1 : 0;
}

void sim_evdev_close(SimEvdevReader *r)
{
    if (r) {
        mark_gone(r);
        free(r);
    }
}

#else

void sim_evdev_set_roots(const SimEvdevRoots *roots) { (void)roots; }
void sim_evdev_set_io(const SimEvdevIo *io) { (void)io; }
bool sim_evdev_link_port_path(const char *link, char *port_path, size_t len, unsigned *kind)
{
    (void)link; (void)port_path; (void)len; (void)kind;
    return false;
}
int sim_evdev_scan(SimEvdevUnit **units, char *why, size_t whylen)
{
    *units = NULL;
    snprintf(why, whylen, "joysticks are read by port path on Linux only");
    return -1;
}
bool sim_evdev_find(const char *port_path, SimEvdevUnit *out) { (void)port_path; (void)out; return false; }
bool sim_evdev_node_reports(const SimEvdevUnit *u, int i, int type, int code)
{
    (void)u; (void)i; (void)type; (void)code;
    return false;
}
bool sim_evdev_check_device(const SimPortDevice *dev, int port, char *why, size_t whylen)
{
    snprintf(why, whylen, "port %d: the device '%s' is read by port path, which this build does on Linux only",
             port, dev->device_id);
    return false;
}
SimEvdevReader *sim_evdev_open(const SimEvdevUnit *u, char *why, size_t whylen)
{
    (void)u;
    snprintf(why, whylen, "joysticks are read by port path on Linux only");
    return NULL;
}
bool sim_evdev_reader_covers(const SimEvdevReader *r, const SimPortDevice *dev, int port, char *why, size_t whylen)
{
    (void)r; (void)dev; (void)port; (void)why; (void)whylen;
    return false;
}
const char *sim_evdev_reader_skipped(const SimEvdevReader *r) { (void)r; return ""; }
int sim_evdev_reader_poll(SimEvdevReader *r) { (void)r; return -1; }
bool sim_evdev_reader_gone(const SimEvdevReader *r) { (void)r; return true; }
bool sim_evdev_reader_abs(const SimEvdevReader *r, int code, int *value, int *min, int *max)
{
    (void)r; (void)code; (void)value; (void)min; (void)max;
    return false;
}
int sim_evdev_reader_key(const SimEvdevReader *r, int code) { (void)r; (void)code; return 0; }
void sim_evdev_close(SimEvdevReader *r) { (void)r; }

#endif

bool sim_evdev_fill(const SimEvdevReader *r, const SimPortDevice *dev, double *vals)
{
    if (!r || sim_evdev_reader_gone(r)) {
        return false;
    }
    for (int i = 0; i < dev->ninputs; i++) {
        const SimInput *in = &dev->inputs[i];
        int v = 0, lo = 0, hi = 0;
        double out = in->kind == SIM_IN_AXIS ? in->range.rest : in->kind == SIM_IN_DETENT ? -1.0 : 0.0;
        if (in->code >= 0) {
            if (in->kind == SIM_IN_AXIS && sim_evdev_reader_abs(r, in->code, &v, &lo, &hi)) {
                out = v;
            } else if (in->kind == SIM_IN_BUTTON) {
                out = sim_evdev_reader_key(r, in->code) ? 1.0 : 0.0;
            } else if (in->kind == SIM_IN_HAT) {
                int x = 0, y = 0;
                unsigned bits = 0;
                if (sim_evdev_reader_abs(r, in->code, &x, &lo, &hi) &&
                    sim_evdev_reader_abs(r, in->code + 1, &y, &lo, &hi)) {
                    bits = (x < 0 ? SIM_HAT_LEFT : x > 0 ? SIM_HAT_RIGHT : 0u) |
                           (y < 0 ? SIM_HAT_UP : y > 0 ? SIM_HAT_DOWN : 0u);
                }
                out = bits;
            }
        }
        vals[i] = out;
    }
    return true;
}
