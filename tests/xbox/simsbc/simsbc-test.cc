/*
 * PenguinBox — the real Steel Battalion controller as a Sim Configurator source (ui/xemu-sim-sbc-source.h)
 *
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 *
 * simsbc-test DATA DIR WORK [part]
 *   DATA  the shipped documents (data/sim-profiles)    DIR   this folder (its profiles/ store)
 *   WORK  a scratch folder for the made-up USB trees   part  "toggles" runs only part T (see the Makefile)
 *
 * A  the 49 ids: the table against the writer's own tables; the shipped device document checks; every way a
 *    document can disagree is refused by name.
 * B  frames to device inputs, by id: buttons and toggles as levels, the eight axes raw, the tuner and the gear lever
 *    as lever readings (0 between gears as an unlisted reading).
 * C  end to end: synthetic frames through the source, the reader's composed port (all 49 bound one to one), the merge
 *    layer and the Steel Battalion writer, and the EMULATED report built from the writer's fields compared with the
 *    frame's wire bytes (every byte the emulated report carries); the gear sequence 2, 0, 1, 0, 255, 0, 254; the
 *    shipped lever bindings alone.
 * T  the five toggles end to end as levels (switched on, off, on): see the Makefile.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "xemu-sim-merge.h"
#include "xemu-sim-profile-reader.h"
#include "xemu-sim-sbc-source.h"
#include "xemu-sim-target.h"

static int nchecks, nfailed;

static void expect(const std::string &what, bool ok)
{
    nchecks++;
    if (!ok) {
        nfailed++;
    }
    printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
}

static bool has(const char *s, const char *sub)
{
    return s && strstr(s, sub);
}

struct Wire {
    uint64_t buttons = 0;
    uint16_t ax = 0x8000, ay = 0x8000;
    int16_t rot = 0, sx = 0, sy = 0;
    uint16_t lp = 0, mp = 0, rp = 0;
    uint8_t tuner = 0, gear = 255;
};

static void to_raw(const Wire &w, uint8_t *raw)
{
    memset(raw, 0, 26);
    raw[1] = 26;
    for (int i = 0; i < 4; i++) {
        raw[2 + i] = (uint8_t)(w.buttons >> (8 * i));
    }
    raw[6] = (uint8_t)((w.buttons >> 32) & 0x7f);
    raw[8] = (uint8_t)w.ax;
    raw[9] = (uint8_t)(w.ax >> 8);
    raw[10] = (uint8_t)w.ay;
    raw[11] = (uint8_t)(w.ay >> 8);
    const uint16_t s16[] = { (uint16_t)w.rot, (uint16_t)w.sx, (uint16_t)w.sy, w.lp, w.mp, w.rp };
    for (int i = 0; i < 6; i++) {
        raw[12 + 2 * i] = (uint8_t)s16[i];
        raw[13 + 2 * i] = (uint8_t)(s16[i] >> 8);
    }
    raw[24] = w.tuner;
    raw[25] = w.gear;
}

static SbcFrame to_frame(const Wire &w)
{
    SbcFrame f;
    memset(&f, 0, sizeof(f));
    to_raw(w, f.raw);
    f.raw_len = 26;
    f.buttons = w.buttons;
    f.toggle_levels = (uint8_t)((w.buttons >> 32) & 0x7c);
    f.aiming_x = w.ax;
    f.aiming_y = w.ay;
    f.rotation_lever = w.rot;
    f.sight_change_x = w.sx;
    f.sight_change_y = w.sy;
    f.left_pedal = w.lp;
    f.middle_pedal = w.mp;
    f.right_pedal = w.rp;
    f.tuner_dial = w.tuner;
    f.gear_lever = w.gear;
    return f;
}

struct Emu {
    uint64_t buttons = 0, prev = 0;
    int16_t axis[SIM_SBC_NAXES] = { 0 };
    uint8_t gear = 0, tuner = 0, toggles = 0;
};

static void emu_report(const Emu &e, uint8_t *rep)
{
    memset(rep, 0, 26);
    rep[1] = 26;
    uint32_t dw = (uint32_t)(e.buttons & 0xffffffffu);
    memcpy(rep + 2, &dw, 4);
    rep[6] = (uint8_t)(((e.buttons >> 32) & 0x7f & ~0x7cu) | e.toggles);
    rep[9] = (uint8_t)(128 + (e.axis[0] / 256));
    rep[11] = (uint8_t)(128 + (e.axis[1] / 256));
    const int16_t s16[] = { e.axis[2], e.axis[6], e.axis[7], e.axis[3], e.axis[4], e.axis[5] };
    for (int i = 0; i < 6; i++) {
        uint16_t v = (uint16_t)s16[i];
        rep[12 + 2 * i] = (uint8_t)v;
        rep[13 + 2 * i] = (uint8_t)(v >> 8);
    }
    rep[24] = e.tuner;
    rep[25] = e.gear;
}

static std::string report_diff(const uint8_t *emu, const uint8_t *wire)
{
    std::string d;
    for (int i = 0; i < 26; i++) {
        if (i == 7 || i == 8 || i == 10) {
            continue;
        }
        uint8_t w = i == 24 ? (uint8_t)(wire[i] & 0x0f) : wire[i];
        if (emu[i] != w) {
            char b[64];
            snprintf(b, sizeof(b), " byte %d: emulated %02x, wire %02x;", i, emu[i], w);
            d += b;
        }
    }
    return d;
}

struct Rig {
    const SimPort *port = nullptr;
    int dev = -1;
    SimMerge *m = nullptr;
    std::vector<double> vals;
    Emu e;
    uint8_t rep[26];

    bool open(const SimPort *p)
    {
        port = p;
        for (int d = 0; d < p->ndevices; d++) {
            if (sim_sbc_is_controller(&p->devices[d])) {
                dev = d;
            }
        }
        if (dev < 0) {
            return false;
        }
        vals.assign(p->devices[dev].ninputs + 1, 0.0);
        m = sim_merge_new(p);
        return true;
    }
    void feed(const Wire &w)
    {
        SbcFrame f = to_frame(w);
        sim_sbc_source_fill(&port->devices[dev], &f, vals.data());
        std::vector<const double *> feed(port->ndevices, nullptr);
        feed[dev] = vals.data();
        sim_merge_update_dt(m, feed.data(), 0);
        sim_target_write_sbc_levels(m, &e.buttons, &e.prev, e.axis, &e.gear, &e.tuner, &e.toggles,
                                    sim_sbc_toggle_level_mask(port));
        emu_report(e, rep);
    }
    ~Rig()
    {
        sim_merge_free(m);
    }
};

static const SimPortDevice *shipped_device(SimProfileSet *set, SimPort *ports)
{
    if (sim_profile_set_ports(set, "sbc_real", ports) < 1 || !ports[0].ndevices) {
        return nullptr;
    }
    return &ports[0].devices[0];
}

static void part_a(const SimPortDevice *dev)
{
    printf("== A. the 49 ids and the document\n");
    const SimSbcId *ids = sim_sbc_ids();
    int nbutton = 0, ntoggle = 0, naxis = 0, nlever = 0;
    bool unique = true, bits_ok = true, axes_ok = true;
    for (int i = 0; i < SIM_SBC_SOURCE_NIDS; i++) {
        for (int j = 0; j < i; j++) {
            unique = unique && strcmp(ids[i].id, ids[j].id);
        }
        if (ids[i].field == SIM_SBC_F_BUTTON || ids[i].field == SIM_SBC_F_TOGGLE) {
            (ids[i].field == SIM_SBC_F_BUTTON ? nbutton : ntoggle)++;
            bool found = false;
#define CHK_BUTTON(nm, name, bit) \
            if (!strcmp(#nm, ids[i].id)) { found = true; bits_ok = bits_ok && (bit) == ids[i].arg; }
            SIM_SBC_BUTTONS(CHK_BUTTON)
#undef CHK_BUTTON
            bits_ok = bits_ok && found && (ids[i].field == SIM_SBC_F_TOGGLE) == (ids[i].arg >= 34);
        } else if (ids[i].kind == SIM_IN_AXIS) {
            naxis++;
            bool found = false;
#define CHK_AXIS(nm, name, index) found = found || !strcmp(#nm, ids[i].id);
            SIM_SBC_AXES(CHK_AXIS)
#undef CHK_AXIS
            axes_ok = axes_ok && found;
        } else {
            nlever++;
        }
    }
    expect("A1 49 unique ids: 34 buttons, 5 toggles, 8 axes, 2 levers", unique && nbutton == 34 && ntoggle == 5 &&
           naxis == 8 && nlever == 2);
    expect("A1 every button id carries the writer's bit (SIM_SBC_BUTTONS), the toggles bits 34..38", bits_ok);
    expect("A1 every axis id is one of the writer's eight (SIM_SBC_AXES)", axes_ok);

    char why[400] = "";
    expect("A2 the shipped device document steel_battalion_controller checks",
           dev && sim_sbc_is_controller(dev) && sim_sbc_source_check(dev, 1, why, sizeof(why)));
    if (why[0]) {
        printf("     %s\n", why);
    }
    if (!dev) {
        return;
    }
    std::vector<SimInput> in(dev->inputs, dev->inputs + dev->ninputs);
    auto with = [&](std::vector<SimInput> v) {
        SimPortDevice d = *dev;
        d.inputs = v.data();
        d.ninputs = (int)v.size();
        why[0] = '\0';
        bool ok = sim_sbc_source_check(&d, 2, why, sizeof(why));
        printf("     -> %s\n", why);
        return !ok;
    };
    std::vector<SimInput> v = in;
    int gi = -1;
    for (size_t i = 0; i < v.size(); i++) {
        if (!strcmp(v[i].id, "gear_lever")) {
            gi = (int)i;
        }
    }
    v.erase(v.begin() + gi);
    expect("A3 a document without gear_lever: refused naming it", with(v) && has(why, "'gear_lever'") &&
           has(why, "port 2") && has(why, "disagree"));
    v = in;
    SimInput extra = in[0];
    extra.id = "flux_capacitor";
    v.push_back(extra);
    expect("A3 a document with an input the report does not carry: refused naming it", with(v) &&
           has(why, "'flux_capacitor'"));
    v = in;
    for (auto &x : v) {
        if (!strcmp(x.id, "left_pedal")) {
            x.range.max = 1023;
        }
    }
    expect("A3 a pedal with the range 0..1023: refused naming the axis and both ranges", with(v) &&
           has(why, "'left_pedal'") && has(why, "0..1023") && has(why, "0..65535"));
    v = in;
    for (auto &x : v) {
        if (!strcmp(x.id, "eject")) {
            x.kind = SIM_IN_AXIS;
        }
    }
    expect("A3 a button made an axis: refused naming it", with(v) && has(why, "'eject'") && has(why, "axis"));
    v = in;
    static const char kinds[] = "IIIIISI";
    for (auto &x : v) {
        if (!strcmp(x.id, "gear_lever") && x.npositions == 7) {
            x.position_kinds = kinds;
        }
    }
    expect("A3 a gear position that is text: refused naming the lever", with(v) && has(why, "'gear_lever'"));
    SimPortDevice other = *dev;
    other.vid = 0x046d;
    expect("A4 a hid_usb device of another vid:pid is not the controller", !sim_sbc_is_controller(&other));
}

static double value_of(const SimPortDevice *dev, const std::vector<double> &vals, const char *id)
{
    for (int i = 0; i < dev->ninputs; i++) {
        if (!strcmp(dev->inputs[i].id, id)) {
            return vals[i];
        }
    }
    return -999;
}

static const SimInput *input_of(const SimPortDevice *dev, const char *id)
{
    for (int i = 0; i < dev->ninputs; i++) {
        if (!strcmp(dev->inputs[i].id, id)) {
            return &dev->inputs[i];
        }
    }
    return nullptr;
}

static void part_b(const SimPortDevice *dev)
{
    printf("== B. frames to the document's inputs, by id\n");
    std::vector<double> vals(dev->ninputs);
    const SimSbcId *ids = sim_sbc_ids();
    bool buttons_ok = true;
    for (int bit = 0; bit <= 38; bit++) {
        Wire w;
        w.buttons = 1ULL << bit;
        SbcFrame f = to_frame(w);
        sim_sbc_source_fill(dev, &f, vals.data());
        for (int k = 0; k < 39; k++) {
            buttons_ok = buttons_ok && value_of(dev, vals, ids[k].id) == (ids[k].arg == bit ? 1.0 : 0.0);
        }
    }
    expect("B1 each of the 39 report bits alone: exactly its id reads 1, every other 0 (toggles at their level)",
           buttons_ok);
    Wire w;
    w.buttons = 0x7cULL << 32;
    SbcFrame f = to_frame(w);
    f.buttons = 0;
    sim_sbc_source_fill(dev, &f, vals.data());
    expect("B1 a toggle reads toggle_levels (its physical level), not the buttons word",
           value_of(dev, vals, "fuel_flow_rate") == 1 && value_of(dev, vals, "vt_location_measurement") == 1);
    w = Wire();
    w.ax = 65535;
    w.ay = 1;
    w.rot = -32768;
    w.sx = 32767;
    w.sy = -1;
    w.lp = 65535;
    w.mp = 12345;
    w.rp = 1;
    f = to_frame(w);
    sim_sbc_source_fill(dev, &f, vals.data());
    expect("B2 the eight axes are the wire values, raw", value_of(dev, vals, "aiming_x") == 65535 &&
           value_of(dev, vals, "aiming_y") == 1 && value_of(dev, vals, "rotation_lever") == -32768 &&
           value_of(dev, vals, "sight_change_x") == 32767 && value_of(dev, vals, "sight_change_y") == -1 &&
           value_of(dev, vals, "left_pedal") == 65535 && value_of(dev, vals, "middle_pedal") == 12345 &&
           value_of(dev, vals, "right_pedal") == 1);
    const SimInput *gear = input_of(dev, "gear_lever");
    const SimInput *tuner = input_of(dev, "tuner_dial");
    bool levers = gear && tuner;
    const int wires[] = { 254, 255, 1, 2, 3, 4, 5 };
    for (int i = 0; levers && i < 7; i++) {
        w.gear = (uint8_t)wires[i];
        f = to_frame(w);
        sim_sbc_source_fill(dev, &f, vals.data());
        levers = value_of(dev, vals, "gear_lever") == i;
    }
    w.gear = 0;
    f = to_frame(w);
    sim_sbc_source_fill(dev, &f, vals.data());
    expect("B3 the gear lever: R N 1-5 are their positions' indexes", levers);
    expect("B3 the gear lever between gears (0): an unlisted reading 0 (SIM_DETENT_UNLISTED + 0)",
           value_of(dev, vals, "gear_lever") == SIM_DETENT_UNLISTED);
    bool tun = tuner != nullptr;
    for (int t = 0; tun && t < 16; t++) {
        w.tuner = (uint8_t)t;
        f = to_frame(w);
        sim_sbc_source_fill(dev, &f, vals.data());
        tun = value_of(dev, vals, "tuner_dial") == t;
    }
    expect("B3 the tuner dial 0..15: each its position", tun);
}

static void part_c(const SimPort *ports)
{
    printf("== C. end to end: the source, the setup's bindings, the merge layer, the writer, the emulated report\n");
    Rig r;
    expect("C0 port 1 of the ports document sbc_real carries the controller with 49 bindings",
           r.open(&ports[0]) && ports[0].devices[r.dev].nbindings == 49);
    if (r.dev < 0) {
        return;
    }
    const SimSbcId *ids = sim_sbc_ids();
    std::string bad;
    Wire w;
    uint8_t raw[26];
    for (int bit = 0; bit <= 33; bit++) {
        w.buttons = 1ULL << bit;
        r.feed(w);
        to_raw(w, raw);
        std::string d = report_diff(r.rep, raw);
        if (!d.empty()) {
            bad += std::string(" ") + ids[bit].id + ":" + d;
        }
        w.buttons = 0;
        r.feed(w);
        to_raw(w, raw);
        d = report_diff(r.rep, raw);
        if (!d.empty()) {
            bad += std::string(" ") + ids[bit].id + " released:" + d;
        }
    }
    expect("C1 each of the 34 momentary buttons pressed and released: the emulated report equals the wire report" +
           bad, bad.empty());
    bad.clear();
    const struct { uint16_t ax, ay; int16_t rot, sx, sy; uint16_t lp, mp, rp; } axes[] = {
        { 0, 65535, -32768, 32767, -32768, 0, 65535, 1 },
        { 32768, 32768, 0, 0, 0, 0, 0, 0 },
        { 65535, 0, 32767, -32768, 32767, 65535, 0, 65534 },
        { 0x1234, 0xfedc, -1, 1, -12345, 0x8000, 0x7fff, 0x00ff },
        { 0x80ff, 0x7f00, 256, -256, 255, 0xff00, 0x0100, 0x4321 },
    };
    for (const auto &a : axes) {
        w = Wire();
        w.ax = a.ax; w.ay = a.ay; w.rot = a.rot; w.sx = a.sx; w.sy = a.sy; w.lp = a.lp; w.mp = a.mp; w.rp = a.rp;
        r.feed(w);
        to_raw(w, raw);
        bad += report_diff(r.rep, raw);
    }
    expect("C2 the eight axes at their ends, rest and odd values: the emulated report equals the wire report" + bad,
           bad.empty());
    bad.clear();
    w = Wire();
    for (int t = 0; t < 16; t++) {
        w.tuner = (uint8_t)t;
        r.feed(w);
        to_raw(w, raw);
        bad += report_diff(r.rep, raw);
    }
    expect("C3 the tuner dial 0..15: the emulated report's dial byte is the wire's" + bad, bad.empty());
    const int seq[] = { 2, 0, 1, 0, 255, 0, 254 };
    std::string fed, arrived;
    w = Wire();
    for (int g : seq) {
        w.gear = (uint8_t)g;
        r.feed(w);
        fed += " " + std::to_string(g);
        arrived += " " + std::to_string(r.rep[25]);
    }
    printf("     gear fed:%s\n     arrived: %s\n", fed.c_str(), arrived.c_str());
    expect("C4 the gear sequence 2 0 1 0 255 0 254 arrives in the emulated report as 2 0 1 0 255 0 254",
           arrived == " 2 0 1 0 255 0 254");
    w = Wire();
    w.buttons = (1ULL << 0) | (1ULL << 13) | (1ULL << 33);
    w.ax = 0x9a00; w.rot = -20000; w.rp = 40000; w.tuner = 9; w.gear = 3;
    r.feed(w);
    to_raw(w, raw);
    expect("C5 a mixed frame (three buttons, aiming, rotation, a pedal, the dial, gear 3): every carried byte equal" +
           report_diff(r.rep, raw), report_diff(r.rep, raw).empty());

    Rig lv;
    expect("C6 port 2: the shipped lever bindings alone (sbc_levers): 2 bindings", lv.open(&ports[1]) &&
           ports[1].devices[lv.dev].nbindings == 2);
    if (lv.dev >= 0) {
        std::string got;
        w = Wire();
        w.buttons = 1;
        for (int g : seq) {
            w.gear = (uint8_t)g;
            w.tuner = (uint8_t)(g & 15);
            lv.feed(w);
            got += " " + std::to_string(lv.rep[25]) + "/" + std::to_string(lv.rep[24]);
        }
        printf("     gear/tuner arrived:%s\n", got.c_str());
        expect("C6 the levers pass through unchanged (gear 0 included), an unbound button does nothing",
               got == " 2/2 0/0 1/1 0/0 255/15 0/0 254/14" && lv.rep[2] == 0);
    }
}

static void part_t(const SimPort *ports)
{
    printf("== T. the five toggles end to end, switched on, off, on\n");
    Rig r;
    if (!r.open(&ports[0])) {
        expect("T0 the port opens", false);
        return;
    }
    const SimSbcId *ids = sim_sbc_ids();
    for (int k = 34; k <= 38; k++) {
        std::string got;
        Wire w;
        const int levels[] = { 1, 0, 1, 0 };
        for (int lv : levels) {
            w.buttons = lv ? 1ULL << k : 0;
            r.feed(w);
            got += std::to_string((r.rep[6] >> (k - 32)) & 1);
        }
        expect(std::string("T1 ") + ids[k].id + ": the switch 1 0 1 0, the emulated report's bit " + got, got == "1010");
    }
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: simsbc-test DATA DIR WORK [toggles]\n");
        return 2;
    }
    std::string data = argv[1], dir = argv[2], work = argv[3];
    bool toggles = argc > 4 && !strcmp(argv[4], "toggles");
    SimProfileSet *set = sim_profile_set_load(data.c_str(), (dir + "/profiles").c_str());
    for (int i = 0; i < sim_profile_set_nrefused(set); i++) {
        const SimRefusal *rf = sim_profile_set_refusal(set, i);
        printf("     refused %s %s: %s\n", rf->code, rf->where, rf->why);
    }
    expect("0 the reader accepts every document (the shipped set and this test's store)",
           sim_profile_set_nrefused(set) == 0);
    SimPort ports[SIM_PROFILE_MAX_PORTS];
    const SimPortDevice *dev = shipped_device(set, ports);
    expect("0 the ports document sbc_real puts steel_battalion_controller on port 1", dev &&
           !strcmp(dev->device_id, "steel_battalion_controller"));
    if (dev) {
        if (toggles) {
            part_t(ports);
        } else {
            part_a(dev);
            part_b(dev);
            part_c(ports);
            extern void simsbc_more(const std::string &data, const std::string &dir, const std::string &work,
                                    const SimPort *ports);
            simsbc_more(data, dir, work, ports);
        }
    }
    sim_profile_set_free(set);
    printf("%d checks, %d failed\n", nchecks, nfailed);
    return nfailed ? 1 : 0;
}

#include <sys/stat.h>
#include "xemu-sim-bootplan.h"

static void put(const std::string &path, const std::string &text)
{
    FILE *f = fopen(path.c_str(), "w");
    if (f) {
        fputs(text.c_str(), f);
        fclose(f);
    }
}

static void sh(const std::string &cmd)
{
    if (system(cmd.c_str()) != 0) {
        printf("     (command failed: %s)\n", cmd.c_str());
    }
}

static void add_usb(const std::string &root, const std::string &host, const std::string &name, int bus, int devnum,
                    const char *vid = "0a7b", const char *pid = "d000", int mode = 0600)
{
    std::string real = root + "/sys/devices/" + host + "/usb" + std::to_string(bus) + "/" + name;
    sh("mkdir -p '" + real + "' '" + root + "/sys/bus/usb/devices' '" + root + "/dev'");
    put(real + "/idVendor", std::string(vid) + "\n");
    put(real + "/idProduct", std::string(pid) + "\n");
    put(real + "/busnum", std::to_string(bus) + "\n");
    put(real + "/devnum", std::to_string(devnum) + "\n");
    put(real + "/bConfigurationValue", "1\n");
    sh("ln -sfn '" + real + "' '" + root + "/sys/bus/usb/devices/" + name + "'");
    char node[64];
    snprintf(node, sizeof(node), "/dev/%03d", bus);
    sh("mkdir -p '" + root + node + "'");
    snprintf(node + strlen(node), sizeof(node) - strlen(node), "/%03d", devnum);
    put(root + node, "");
    chmod((root + node).c_str(), (mode_t)mode);
}

static SimSbcRoots roots_of(const std::string &root, std::string &a, std::string &b)
{
    a = root + "/sys/bus/usb/devices";
    b = root + "/dev";
    return SimSbcRoots{ a.c_str(), b.c_str() };
}

static void part_p(const std::string &work_in, const SimPort *ports)
{
    printf("== P. where the controller is, and the boot plan's predicate\n");
    sh("rm -rf '" + work_in + "' && mkdir -p '" + work_in + "'");
    char abs_work[PATH_MAX];
    const std::string work = realpath(work_in.c_str(), abs_work) ? abs_work : work_in;
    const SimPortDevice *dev = &ports[0].devices[0];
    char why[600];
    std::string a, b;
    SimSbcFound f;

    std::string none = work + "/none";
    sh("mkdir -p '" + none + "/sys/bus/usb/devices' '" + none + "/dev'");
    add_usb(none, "pci0000:00/0000:00:14.0", "1-4", 1, 3, "046d", "c52b");
    SimSbcRoots r = roots_of(none, a, b);
    sim_sbc_locate(&r, &f);
    expect("P1 no controller plugged in (another USB device is): none found", f.count == 0);
    why[0] = '\0';
    expect("P1 ... the plan refuses: not plugged in, by name", !sim_sbc_plan_check(dev, 1, &r, why, sizeof(why)) &&
           has(why, "not plugged in") && has(why, "'steel_battalion_controller'") && has(why, "port 1"));
    printf("     -> %s\n", why);

    std::string one = work + "/one";
    add_usb(one, "pci0000:00/0000:00:14.0", "1-2", 1, 5);
    add_usb(one, "pci0000:00/0000:00:14.0", "1-4", 1, 3, "046d", "c52b");
    r = roots_of(one, a, b);
    sim_sbc_locate(&r, &f);
    printf("     found %d: %s, port path %s, node %s, may open %d\n", f.count, f.sysname, f.port_path, f.node,
           f.may_open);
    expect("P2 one controller at 1-2: its port path is pci-0000:00:14.0-usb-0:2:1.0 and this user may open it",
           f.count == 1 && !strcmp(f.port_path, "pci-0000:00:14.0-usb-0:2:1.0") && f.may_open);
    why[0] = '\0';
    expect("P2 ... the plan reads it (the setup names that port path)", sim_sbc_plan_check(dev, 1, &r, why,
                                                                                          sizeof(why)));
    if (why[0]) {
        printf("     -> %s\n", why);
    }
    SimPortDevice usbv = *dev;
    usbv.port_path = "pci-0000:00:14.0-usbv2-0:2:1.0";
    expect("P2 ... the setup's port path in udev's USB-revision form (-usbv2-) is the same place",
           sim_sbc_plan_check(&usbv, 1, &r, why, sizeof(why)));
    SimPortDevice nopath = *dev;
    nopath.port_path = nullptr;
    expect("P2 ... a setup that names no port path takes the one controller", sim_sbc_plan_check(&nopath, 1, &r, why,
                                                                                                sizeof(why)));
    SimPortDevice elsewhere = *dev;
    elsewhere.port_path = "pci-0000:00:14.0-usb-0:3:1.0";
    why[0] = '\0';
    expect("P3 the setup names another USB position: refused naming both", !sim_sbc_plan_check(&elsewhere, 1, &r, why,
                                                                                               sizeof(why)) &&
           has(why, "usb-0:3:1.0") && has(why, "usb-0:2:1.0"));
    printf("     -> %s\n", why);

    std::string hub = work + "/hub";
    add_usb(hub, "pci0000:00/0000:00:1d.0/0000:01:00.0", "3-1.4", 3, 9);
    r = roots_of(hub, a, b);
    sim_sbc_locate(&r, &f);
    expect("P2 behind a hub on a PCI bridge: the nearest PCI device and the port chain, " + std::string(f.port_path),
           !strcmp(f.port_path, "pci-0000:01:00.0-usb-0:1.4:1.0"));
    std::string plat = work + "/plat";
    add_usb(plat, "platform/fe9c0000.usb", "2-1", 2, 2);
    r = roots_of(plat, a, b);
    sim_sbc_locate(&r, &f);
    expect("P2 on a platform host controller: " + std::string(f.port_path),
           !strcmp(f.port_path, "platform-fe9c0000.usb-usb-0:1:1.0"));

    std::string two = work + "/two";
    add_usb(two, "pci0000:00/0000:00:14.0", "1-2", 1, 5);
    add_usb(two, "pci0000:00/0000:00:14.0", "1-3", 1, 6);
    r = roots_of(two, a, b);
    why[0] = '\0';
    expect("P4 two controllers plugged in: refused", !sim_sbc_plan_check(dev, 1, &r, why, sizeof(why)) &&
           has(why, "2 Steel Battalion controllers"));
    printf("     -> %s\n", why);

    std::string perm = work + "/perm";
    add_usb(perm, "pci0000:00/0000:00:14.0", "1-2", 1, 5, "0a7b", "d000", 0);
    r = roots_of(perm, a, b);
    why[0] = '\0';
    bool refused = !sim_sbc_plan_check(dev, 1, &r, why, sizeof(why));
    if (geteuid() == 0) {
        printf("     (run as root: access() grants every node, so the permission case cannot be shown here)\n");
    } else {
        expect("P5 the controller's node not readable and writable by this user: refused naming the udev rule",
               refused && has(why, "70-penguinvrlabs-sim-devices.rules") && has(why, "--install-sbc-rules"));
        printf("     -> %s\n", why);
    }

    r = roots_of(one, a, b);
    setenv("XEMU_SBC_PASSTHROUGH", "1", 1);
    why[0] = '\0';
    expect("P6 the developer passthrough switch set: refused naming it", !sim_sbc_plan_check(dev, 1, &r, why,
                                                                                            sizeof(why)) &&
           has(why, "XEMU_SBC_PASSTHROUGH=1"));
    printf("     -> %s\n", why);
    setenv("XEMU_SBC_PASSTHROUGH", "0", 1);
    expect("P6 ... any other value of the switch is not it", sim_sbc_plan_check(dev, 1, &r, why, sizeof(why)));
    unsetenv("XEMU_SBC_PASSTHROUGH");

    SimBootPlan plan;
    SimPort one_port[SIM_PROFILE_MAX_PORTS];
    memcpy(one_port, ports, sizeof(one_port));
    one_port[1] = SimPort();
    one_port[2] = SimPort();
    one_port[3] = SimPort();
    sim_bootplan_set_sbc_roots(a.c_str(), b.c_str());
    expect("P7 the boot plan with the controller plugged in: port 1 attached (made-up folders noted)",
           sim_bootplan_decide("sbc_real", 1, one_port, nullptr, 0, nullptr, &plan) &&
           plan.action[0] == SIM_PLAN_ATTACH && sim_bootplan_sbc_roots_made_up());
    std::string na, nb;
    roots_of(none, na, nb);
    sim_bootplan_set_sbc_roots(na.c_str(), nb.c_str());
    expect("P7 the boot plan with no controller: the boot is refused, naming the port, the profile and the device",
           !sim_bootplan_decide("sbc_real", 1, one_port, nullptr, 0, nullptr, &plan) && has(plan.why, "port 1") &&
           has(plan.why, "sbc_all") && has(plan.why, "not plugged in"));
    printf("     -> %s\n", plan.why);
    SimPortDevice wire = *dev;
    wire.vid = 0x046d;
    SimPort other = one_port[0];
    other.devices = &wire;
    other.ndevices = 1;
    SimPort other_ports[SIM_PROFILE_MAX_PORTS] = { other, SimPort(), SimPort(), SimPort() };
    expect("P8 a hid_usb device that is not the controller: refused by name as before",
           !sim_bootplan_decide("x", 1, other_ports, nullptr, 0, nullptr, &plan) && has(plan.why, "later build") &&
           has(plan.why, "'steel_battalion_controller'"));
    sim_bootplan_set_sbc_roots(nullptr, nullptr);
    expect("P9 the system's folders again: made-up no more", !sim_bootplan_sbc_roots_made_up());
}

struct Sink {
    std::vector<std::vector<uint8_t>> got;
    bool busy = false;
};

static bool sink_send(const uint8_t *r, size_t n, void *ctx)
{
    Sink *k = (Sink *)ctx;
    if (k->busy) {
        return false;
    }
    k->got.emplace_back(r, r + n);
    return true;
}

static std::vector<uint8_t> lights(int light, int level)
{
    std::vector<uint8_t> r(22, 0);
    r[1] = 22;
    int byte = 2 + light / 2;
    r[byte] = (uint8_t)(light % 2 ? level << 4 : level);
    return r;
}

static std::string hex(const std::vector<uint8_t> &v)
{
    std::string s;
    char b[4];
    for (uint8_t x : v) {
        snprintf(b, sizeof(b), "%02x", x);
        s += b;
    }
    return s;
}

static void part_l(const SimPort *ports)
{
    printf("== L. the panel lights: the game's report forwarded unchanged\n");
    SimSbcLights l;
    sim_sbc_lights_init(&l);
    SimSbcLightSlot slot;
    memset(&slot, 0, sizeof(slot));
    uint32_t seen = 0;
    Sink k;
    struct { int64_t t; int light, level; } guest[] = {
        { 0, 0, 15 },
        { 5, 1, 8 },
        { 10, 2, 4 },
        { 25, 2, 4 },
        { 60, 37, 15 },
    };
    std::vector<std::vector<uint8_t>> want;
    for (auto &g : guest) {
        std::vector<uint8_t> r = lights(g.light, g.level);
        sim_sbc_lights_store(&slot, r.data(), r.size());
        for (int64_t t = g.t; t < g.t + 5; t++) {
            sim_sbc_lights_step(&l, &slot, &seen, t, sink_send, &k);
        }
    }
    for (int64_t t = 65; t < 100; t++) {
        sim_sbc_lights_step(&l, &slot, &seen, t, sink_send, &k);
    }
    want = { lights(0, 15), lights(2, 4), lights(37, 15) };
    std::string got_s;
    for (auto &v : k.got) {
        got_s += " " + hex(v).substr(0, 24) + "..";
    }
    printf("     arrived:%s\n", got_s.c_str());
    expect("L1 the reports arrive byte for byte, in order, the newest winning under the bound (3 sends of 5 writes)",
           k.got == want);
    expect("L1 the slot's version moves only when the report differs", slot.version == 4);

    SimSbcLights l2;
    sim_sbc_lights_init(&l2);
    SimSbcLightSlot slot2;
    memset(&slot2, 0, sizeof(slot2));
    uint32_t seen2 = 0;
    Sink k2;
    k2.busy = true;
    std::vector<uint8_t> r = lights(5, 9);
    sim_sbc_lights_store(&slot2, r.data(), r.size());
    sim_sbc_lights_step(&l2, &slot2, &seen2, 0, sink_send, &k2);
    sim_sbc_lights_step(&l2, &slot2, &seen2, 1, sink_send, &k2);
    k2.busy = false;
    sim_sbc_lights_step(&l2, &slot2, &seen2, 2, sink_send, &k2);
    expect("L2 a send refused while the last is in flight is sent once it can be", k2.got.size() == 1 &&
           k2.got[0] == r);

    uint8_t off[SIM_SBC_LIGHTS_MAX];
    size_t n = sim_sbc_lights_off(&l, off);
    std::vector<uint8_t> offv(off, off + n), zero(22, 0);
    zero[1] = 22;
    printf("     off: %s\n", hex(offv).c_str());
    expect("L3 lights out: the game's header and every light 0 (22 bytes)", offv == zero);
    r = lights(3, 3);
    sim_sbc_lights_store(&slot, r.data(), r.size());
    size_t before = k.got.size();
    for (int64_t t = 200; t < 260; t++) {
        sim_sbc_lights_step(&l, &slot, &seen, t, sink_send, &k);
    }
    expect("L3 after lights out nothing more is sent", k.got.size() == before);

    expect("L4 the port that carries the real controller forwards the lights", sim_sbc_port_forwards_lights(&ports[0]));
    SimInput key = SimInput();
    key.id = "key_a";
    key.kind = SIM_IN_BUTTON;
    key.scancode = "30";
    SimPortDevice kbd = SimPortDevice();
    kbd.device_id = "keyboard_104";
    kbd.source = SIM_SRC_KEYBOARD;
    kbd.ninputs = 1;
    kbd.inputs = &key;
    kbd.usable = true;
    SimPort kp = ports[0];
    kp.devices = &kbd;
    kp.ndevices = 1;
    expect("L4 a keyboard setup of the Steel Battalion controller forwards nothing", !sim_sbc_port_forwards_lights(&kp));
    SimPort duke = ports[0];
    duke.target = "xbox_duke";
    expect("L4 a Duke port with the real controller on it forwards nothing", !sim_sbc_port_forwards_lights(&duke));
}

void simsbc_more(const std::string &, const std::string &, const std::string &work, const SimPort *ports)
{
    part_p(work + "/usb", ports);
    part_l(ports);
}
