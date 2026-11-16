/*
 * SPDX-License-Identifier: MIT
 */
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "xemu-sim-merge.h"
#include "xemu-sim-target.h"
#include "rapidyaml-0.12.1.hpp"

static int nchecks, nfailed;

static void expect(const std::string &what, bool ok)
{
    nchecks++;
    if (!ok) {
        nfailed++;
    }
    printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
}

static bool near(double a, double b)
{
    return std::fabs(a - b) <= 1e-9 * (1.0 + std::fabs(b));
}

static std::vector<std::vector<std::string>> rows(const std::string &path)
{
    std::vector<std::vector<std::string>> out;
    FILE *f = fopen(path.c_str(), "r");
    if (!f) {
        expect("open " + path, false);
        return out;
    }
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }
        line[strcspn(line, "\r\n")] = 0;
        std::vector<std::string> r;
        for (char *s = line, *t; s; s = t) {
            t = strchr(s, '\t');
            if (t) {
                *t++ = 0;
            }
            r.push_back(s);
        }
        out.push_back(r);
    }
    fclose(f);
    return out;
}

static double D(const std::string &s)
{
    return strtod(s.c_str(), NULL);
}

static std::vector<double> list(const std::string &s)
{
    std::vector<double> v;
    for (size_t i = 0; i <= s.size();) {
        size_t j = s.find(',', i);
        if (j == std::string::npos) {
            j = s.size();
        }
        v.push_back(D(s.substr(i, j - i)));
        i = j + 1;
    }
    return v;
}

static SimPolicy policy(const std::string &n)
{
    return n == "half_positive" ? SIM_POLICY_HALF_POSITIVE : n == "half_negative" ? SIM_POLICY_HALF_NEGATIVE :
           n == "absolute" ? SIM_POLICY_ABSOLUTE : n == "full_range" ? SIM_POLICY_FULL_RANGE :
           n == "split" ? SIM_POLICY_SPLIT : SIM_POLICY_NONE;
}

struct Hand {
    SimInput src{}, tgt{};
    SimBinding b{};
    SimPortDevice dev{};
    SimPort port{};
    void build()
    {
        b.source = &src;
        b.target = &tgt;
        dev.device_id = dev.key = "hand";
        dev.ninputs = 1;
        dev.inputs = &src;
        dev.nbindings = 1;
        dev.bindings = &b;
        dev.usable = true;
        port.port = 1;
        port.target = "hand_target";
        port.ndevices = 1;
        port.devices = &dev;
    }
};

static void part_a(const std::string &dir)
{
    int n = 0, bad = 0;
    for (auto &r : rows(dir + "/app_vectors.tsv")) {
        bool ok = true;
        if (r[0] == "norm") {
            SimRange R{D(r[1]), D(r[2]), D(r[3])};
            ok = near(sim_merge_normalise(R, D(r[4])), D(r[5]));
        } else if (r[0] == "denorm") {
            SimRange R{D(r[1]), D(r[2]), D(r[3])};
            ok = near(sim_merge_denormalise(R, D(r[4])), D(r[5]));
        } else if (r[0] == "eval") {
            Hand h;
            h.src.id = "s";
            h.src.kind = SIM_IN_AXIS;
            h.src.has_range = true;
            h.src.range = SimRange{D(r[1]), D(r[2]), D(r[3])};
            h.b.has_deadzone = D(r[8]) != 0;
            h.b.resp_deadzone = D(r[8]);
            h.tgt.id = "t";
            h.tgt.kind = SIM_IN_AXIS;
            h.tgt.has_range = true;
            h.tgt.range = SimRange{D(r[4]), D(r[5]), D(r[6])};
            h.b.resp_invert = r[7] == "1";
            h.b.has_window = true;
            h.b.window_lo = D(r[9]);
            h.b.window_hi = D(r[10]);
            h.build();
            SimMerge *m = sim_merge_new(&h.port);
            double v = D(r[11]);
            const double *vals[1] = { &v };
            sim_merge_update(m, vals);
            const SimMergeOut *o = sim_merge_find(m, "t");
            double tr = D(r[13]);
            ok = o && near(o->canonical, D(r[12])) && o->raw == sim_merge_round(tr, h.tgt.range.rest);
            sim_merge_free(m);
        } else if (r[0] == "integ") {
            Hand h;
            h.src.id = "rel";
            h.src.kind = SIM_IN_RELATIVE;
            h.tgt.id = "t";
            h.tgt.kind = SIM_IN_AXIS;
            h.tgt.has_range = true;
            h.tgt.range = SimRange{D(r[1]), D(r[2]), D(r[3])};
            h.b.integrate = true;
            h.b.counts_per_travel = D(r[4]);
            h.b.resp_invert = r[5] == "1";
            h.build();
            SimMerge *m = sim_merge_new(&h.port);
            for (double d : list(r[6])) {
                const double *vals[1] = { &d };
                sim_merge_update(m, vals);
            }
            const SimMergeOut *o = sim_merge_find(m, "t");
            double p = D(r[7]);
            ok = o && near(sim_merge_denormalise(h.tgt.range, o->canonical), p) &&
                 o->raw == sim_merge_round(p, h.tgt.range.rest);
            sim_merge_free(m);
        } else {
            continue;
        }
        n++;
        if (!ok) {
            bad++;
            std::string w = "A: " + r[0];
            for (size_t i = 1; i < r.size(); i++) {
                w += " " + r[i];
            }
            expect(w, false);
        }
    }
    expect("A: " + std::to_string(n - bad) + " of " + std::to_string(n) + " rows the app's own code computed agree",
           n > 400 && bad == 0);
}

static void part_b(const std::string &dir)
{
    for (auto &r : rows(dir + "/vectors.tsv")) {
        std::string w = "B: " + r[0];
        for (size_t i = 1; i < r.size(); i++) {
            w += " " + r[i];
        }
        if (r[0] == "policy") {
            expect(w, near(sim_merge_policy(policy(r[1]), r[2] == "1", D(r[3]), atoi(r[4].c_str())), D(r[5])));
        } else if (r[0] == "resp") {
            SimBinding b{};
            b.resp_invert = r[1] == "1";
            b.has_window = true;
            b.window_lo = D(r[2]);
            b.window_hi = D(r[3]);
            b.has_deadzone = D(r[4]) != 0;
            b.resp_deadzone = D(r[4]);
            b.has_saturation = D(r[5]) != 0;
            b.saturation = D(r[5]);
            b.has_curve = D(r[6]) != 0;
            b.curve = D(r[6]);
            expect(w, near(sim_merge_response(&b, D(r[9]), r[7] == "1", D(r[8])), D(r[10])));
        } else if (r[0] == "round") {
            expect(w, sim_merge_round(D(r[1]), D(r[2])) == D(r[3]));
        } else if (r[0] == "chain") {
            SimRange s{D(r[1]), D(r[2]), D(r[3])}, t{D(r[4]), D(r[5]), D(r[6])};
            double c = sim_merge_normalise(s, D(r[8]));
            if (sim_merge_bipolar(s) != sim_merge_bipolar(t)) {
                c = sim_merge_policy(policy(r[7]), sim_merge_bipolar(s), c, 0);
            }
            expect(w, sim_merge_round(sim_merge_denormalise(t, c), t.rest) == D(r[9]));
        } else if (r[0] == "aim") {
            int16_t a = sim_merge_sbc_aiming_axis(D(r[1]));
            uint8_t byte = (uint8_t)(128 + (a / 256));
            expect(w, a == (int16_t)atoi(r[2].c_str()) && byte == atoi(r[3].c_str()));
        } else if (r[0] == "thresh") {
            Hand h;
            double th = D(r[1]);
            h.src.id = "s";
            h.src.kind = SIM_IN_AXIS;
            h.src.has_range = true;
            h.src.range = th >= 0 ? SimRange{0, 0, 1} : SimRange{-1, 0, 1};
            h.tgt.id = "t";
            h.tgt.kind = SIM_IN_BUTTON;
            h.b.has_threshold = true;
            h.b.threshold = th;
            h.b.hysteresis = D(r[2]);
            h.build();
            SimMerge *m = sim_merge_new(&h.port);
            std::vector<double> in = list(r[3]), want = list(r[4]);
            bool ok = in.size() == want.size();
            for (size_t i = 0; ok && i < in.size(); i++) {
                const double *vals[1] = { &in[i] };
                sim_merge_update(m, vals);
                ok = sim_merge_find(m, "t")->pressed == (want[i] != 0);
            }
            expect(w, ok);
            sim_merge_free(m);
        }
    }
}

enum { STICK_X, STICK_Y, TRIG, PEDAL_DESC, BTN_A, BTN_B, POV, GATE, BTN_C, NPAD };
enum { REL_X, MIDDLE, NMOUSE };

struct Want {
    const char *control;
    double raw;
};

static void frame(SimMerge *m, const char *name, const double *pad, const double *mouse,
                  const std::vector<Want> &want, int pad_dev, int mouse_dev)
{
    const double *vals[2] = { nullptr, nullptr };
    vals[pad_dev] = pad;
    vals[mouse_dev] = mouse;
    sim_merge_update(m, vals);
    for (const Want &w : want) {
        const SimMergeOut *o = sim_merge_find(m, w.control);
        char buf[200];
        snprintf(buf, sizeof(buf), "C: %s: %s = %.17g (got %.17g)", name, w.control, w.raw, o ? o->raw : NAN);
        expect(buf, o && o->raw == w.raw);
    }
}

static void part_c(const std::string &data, const std::string &dir)
{
    SimProfileSet *set = sim_profile_set_load(data.c_str(), (dir + "/profiles").c_str());
    for (int i = 0; i < sim_profile_set_nrefused(set); i++) {
        const SimRefusal *r = sim_profile_set_refusal(set, i);
        printf("     refused %s %s: %s\n", r->code, r->where, r->why);
    }
    expect("C: the reader accepts every fixture document", sim_profile_set_nrefused(set) == 0);
    SimPort ports[SIM_PROFILE_MAX_PORTS];
    int on = sim_profile_set_ports(set, "e2e", ports);
    expect("C: ports e2e: two ports on", on == 2);
    if (on != 2 || !ports[0].profile) {
        sim_profile_set_free(set);
        return;
    }
    int pad_dev = -1, mouse_dev = -1;
    for (int d = 0; d < ports[0].ndevices; d++) {
        if (!strcmp(ports[0].devices[d].device_id, "e2e_pad")) {
            pad_dev = d;
        } else if (!strcmp(ports[0].devices[d].device_id, "e2e_mouse")) {
            mouse_dev = d;
        }
    }
    expect("C: port 1 has the pad and the mouse", pad_dev >= 0 && mouse_dev >= 0);
    if (pad_dev < 0 || mouse_dev < 0) {
        sim_profile_set_free(set);
        return;
    }
    SimMerge *m = sim_merge_new(&ports[0]);
    expect("C: port 1 drives 12 controls", sim_merge_noutputs(m) == 12);

    frame(m, "frame 0 (both devices absent)", nullptr, nullptr,
          { { "rotation_lever", 0 }, { "sight_change_y", 0 }, { "left_pedal", 0 }, { "middle_pedal", 0 },
            { "right_pedal", 0 }, { "aiming_y", 32768 }, { "sight_change_x", 0 }, { "aiming_x", 32768 }, { "main_weapon", 0 },
            { "gear_up", 0 }, { "gear_down", 0 }, { "lock_on", 0 } }, pad_dev, mouse_dev);
    double p1[NPAD] = { 16384, 16384, 500, 0, 1, 1, SIM_HAT_UP, 2 }, m1[NMOUSE] = { 100, 0 };
    frame(m, "frame 1", p1, m1,
          { { "rotation_lever", 16384 }, { "sight_change_y", -16385 }, { "left_pedal", 32768 },
            { "middle_pedal", 32769 }, { "right_pedal", 65535 }, { "sight_change_x", -16384 }, { "aiming_y", 32768 },
            { "aiming_x", 36045 },
            { "main_weapon", 1 }, { "gear_up", 1 }, { "gear_down", 1 }, { "lock_on", 1 } }, pad_dev, mouse_dev);
    double p2[NPAD] = { -32768, 0, 470, 1023, 0, 0, SIM_HAT_DOWN, 0 }, m2[NMOUSE] = { 2000, 0 };
    frame(m, "frame 2", p2, m2,
          { { "rotation_lever", -32768 }, { "sight_change_y", 0 }, { "left_pedal", 29696 }, { "middle_pedal", 0 },
            { "right_pedal", 0 }, { "sight_change_x", 0 }, { "aiming_y", 32768 }, { "aiming_x", 65535 }, { "main_weapon", 0 },
            { "gear_up", 0 }, { "gear_down", 0 }, { "lock_on", 1 } }, pad_dev, mouse_dev);
    double p3[NPAD] = { 0, 0, 410, 1023, 0, 0, 0, 1 }, m3[NMOUSE] = { -1, 0 };
    frame(m, "frame 3", p3, m3, { { "lock_on", 0 }, { "aiming_x", 65502 }, { "gear_down", 0 } }, pad_dev, mouse_dev);
    double p4[NPAD] = { 0, 0, 0, 1023, 0, 0, 0, 0 }, m4[NMOUSE] = { 0, 1 };
    frame(m, "frame 4 (recentre held)", p4, m4, { { "aiming_x", 32768 }, { "main_weapon", 1 }, { "left_pedal", 0 } },
          pad_dev, mouse_dev);
    double m5[NMOUSE] = { 50, 0 };
    frame(m, "frame 5 (the pad unplugged)", nullptr, m5,
          { { "aiming_x", 34406 }, { "rotation_lever", 0 }, { "lock_on", 0 }, { "main_weapon", 0 } }, pad_dev,
          mouse_dev);
    sim_merge_reset(m);
    double m6[NMOUSE] = { 0, 0 };
    frame(m, "frame 6 (after a reset)", nullptr, m6, { { "aiming_x", 32768 } }, pad_dev, mouse_dev);
    {
        double held[NPAD] = { 0, 0, 0, 1023, 0, 0, 0, 0, 1 }, idle[NPAD] = { 0, 0, 0, 1023, 0, 0, 0, 0, 0 };
        const double *v[2] = { nullptr, nullptr };
        v[mouse_dev] = m6;
        v[pad_dev] = held;
        sim_merge_update_dt(m, v, 0);
        sim_merge_update_dt(m, v, 100);
        const SimMergeOut *ay = sim_merge_find(m, "aiming_y");
        expect("C: a rate key held 100 ms moves aiming_y 32768 -> 36044.75 (36045)", ay && ay->raw == 36045);
        v[pad_dev] = idle;
        sim_merge_update_dt(m, v, 50);
        expect("C: the 50 ms before the release is read still count (37683), then it stays", ay->raw == 37683);
        sim_merge_update_dt(m, v, 500);
        expect("C: released, 500 ms later: still 37683 (the lever stays put)", ay->raw == 37683);
    }
    sim_merge_free(m);

    SimMerge *none = sim_merge_new(&ports[1]);
    expect("C: port 2 (no profile) drives nothing", sim_merge_noutputs(none) == 0);
    sim_merge_update(none, nullptr);
    sim_merge_free(none);
    sim_profile_set_free(set);
}

struct Multi {
    SimInput in[4]{};
    SimInput tgt[4]{};
    SimBinding b[4]{};
    SimPortDevice dev{};
    SimPort port{};
    int nin = 0, nb = 0;
    SimInput *input(const char *id, SimInputKind k)
    {
        SimInput *x = &in[nin++];
        x->id = id;
        x->kind = k;
        return x;
    }
    SimInput *target(int i, const char *id, SimInputKind k)
    {
        tgt[i].id = id;
        tgt[i].kind = k;
        return &tgt[i];
    }
    SimBinding *bind(SimInput *s, SimInput *t)
    {
        SimBinding *x = &b[nb++];
        x->source = s;
        x->target = t;
        return x;
    }
    SimMerge *build()
    {
        dev.device_id = dev.key = "multi";
        dev.ninputs = nin;
        dev.inputs = in;
        dev.nbindings = nb;
        dev.bindings = b;
        dev.usable = true;
        port.port = 1;
        port.target = "multi_target";
        port.ndevices = 1;
        port.devices = &dev;
        return sim_merge_new(&port);
    }
};

static const SimMergeOut *step(SimMerge *m, std::vector<double> v, const char *id)
{
    const double *vals[1] = { v.data() };
    sim_merge_update(m, vals);
    return sim_merge_find(m, id);
}

static void part_d_hostile_values()
{
    const double nan = NAN, inf = INFINITY;
    {
        Multi u;
        SimInput *h = u.input("pov", SIM_IN_HAT);
        SimInput *g = u.input("gate", SIM_IN_DETENT);
        SimInput *a = u.input("x", SIM_IN_AXIS);
        SimInput *k = u.input("k", SIM_IN_BUTTON);
        static const char *const pos[] = { "n", "r" };
        g->npositions = 2;
        g->positions = pos;
        a->has_range = true;
        a->range = SimRange{-100, 0, 100};
        u.bind(h, u.target(0, "up", SIM_IN_BUTTON))->direction = "up";
        u.bind(g, u.target(1, "rev", SIM_IN_BUTTON))->position = "r";
        SimInput *t = u.target(2, "t", SIM_IN_AXIS);
        t->has_range = true;
        t->range = SimRange{-1000, 0, 1000};
        u.bind(a, t);
        u.bind(k, u.target(3, "fire", SIM_IN_BUTTON));
        SimMerge *m = u.build();
        auto none = [&](const char *what) {
            expect(std::string("D hostile: ") + what + ": nothing pressed, the axis at rest",
                   !sim_merge_find(m, "up")->pressed && !sim_merge_find(m, "rev")->pressed &&
                   sim_merge_find(m, "t")->raw == 0 && !sim_merge_find(m, "fire")->pressed);
        };
        step(m, { nan, nan, nan, nan }, "t");
        none("NaN on the hat, detent, axis and button");
        step(m, { inf, inf, inf, inf }, "t");
        none("+inf on each");
        step(m, { -inf, -inf, -inf, -inf }, "t");
        none("-inf on each");
        step(m, { -1, 5e9, nan, 0 }, "t");
        none("hat -1, detent 5e9");
        step(m, { 1e10, -1e20, nan, 0 }, "t");
        none("hat 1e10, detent -1e20");
        step(m, { 16, -1, nan, 0 }, "t");
        none("hat 16 (past the SDL bits), detent -1");
        step(m, { 1.5, 1.0, 1e300, 1 }, "t");
        expect("D hostile: hat 1.5 (up), detent 1.0 (r), axis 1e300 (full), button 1: all pressed, the axis 1000",
               sim_merge_find(m, "up")->pressed && sim_merge_find(m, "rev")->pressed &&
               sim_merge_find(m, "t")->raw == 1000 && sim_merge_find(m, "fire")->pressed);
        sim_merge_free(m);
    }
    {
        Multi u;
        SimInput *rel = u.input("rx", SIM_IN_RELATIVE);
        SimInput *t = u.target(0, "aim", SIM_IN_AXIS);
        t->has_range = true;
        t->range = SimRange{0, 500, 1000};
        SimBinding *b = u.bind(rel, t);
        b->integrate = true;
        b->counts_per_travel = 100;
        SimMerge *m = u.build();
        const SimMergeOut *o = step(m, { 10 }, "aim");
        bool ok = o->raw == 600;
        for (double v : { nan, inf, -inf }) {
            ok = ok && step(m, { v }, "aim")->raw == 600;
        }
        expect("D hostile: an integrated position does not move on NaN or +-inf counts (it stays 600)", ok);
        expect("D hostile: -1e308 counts clamp the position to its low end (0)", step(m, { -1e308 }, "aim")->raw == 0);
        expect("D hostile: the next motion moves it again (+10 -> 100)", step(m, { 10 }, "aim")->raw == 100);
        sim_merge_free(m);
    }
    {
        Multi u;
        SimInput *a = u.input("pedal", SIM_IN_AXIS);
        a->has_range = true;
        a->range = SimRange{0, 0, 1000};
        a->deadzone = 0.9;
        SimInput *t = u.target(0, "trig", SIM_IN_AXIS), *t2 = u.target(1, "trig2", SIM_IN_AXIS);
        t->has_range = t2->has_range = true;
        t->range = t2->range = SimRange{0, 0, 255};
        SimBinding *b = u.bind(a, t);
        b->has_window = true;
        b->window_lo = 0;
        b->window_hi = 0.5;
        b = u.bind(a, t2);
        b->has_window = true;
        b->window_lo = 0;
        b->window_hi = 0.5;
        b->has_deadzone = true;
        b->resp_deadzone = 0.2;
        SimMerge *m = u.build();
        const SimMergeOut *o = step(m, { 400 }, "trig");
        expect("D deadzone: raw 400 of 1000, window [0, 0.5], descriptor deadzone 0.9 (not read): 204, as the app",
               o->raw == 204);
        expect("D deadzone: the same with the binding's deadzone 0.2: (0.4 - 0.2) / 0.3 of 255 -> 170",
               sim_merge_find(m, "trig2")->raw == 170);
        sim_merge_free(m);
    }
    expect("D hostile: the SBC aiming axis of a NaN wire value is its rest (0); +-inf clamp to the ends",
           sim_merge_sbc_aiming_axis(NAN) == 0 && sim_merge_sbc_aiming_axis(INFINITY) == 32512 &&
           sim_merge_sbc_aiming_axis(-INFINITY) == -32768);
}

static void part_d_levers()
{
    static const char *const pos[] = { "254", "255", "1", "2" };
    static const char kinds[] = "IIII";
    static const long long ints[] = { 254, 255, 1, 2 };
    auto lever = [&](SimInput *x) {
        x->npositions = 4;
        x->positions = pos;
        x->position_kinds = kinds;
        x->position_ints = ints;
    };
    {
        Multi u;
        SimInput *g = u.input("gear", SIM_IN_DETENT), *g2 = u.input("gear2", SIM_IN_DETENT);
        lever(g);
        lever(g2);
        SimInput *t = u.target(0, "gear_lever", SIM_IN_DETENT);
        lever(t);
        u.bind(g, t);
        u.bind(g2, t);
        SimMerge *m = u.build();
        const SimMergeOut *o = step(m, { -1, sim_merge_detent_int(g2, 255) }, "gear_lever");
        expect("D lever: the first binding has no reading, the second's 255 passes (position 1)",
               o->has_reading && o->raw == 255 && o->position == 1 && !o->pressed);
        o = step(m, { sim_merge_detent_int(g, 0), sim_merge_detent_int(g2, 1) }, "gear_lever");
        expect("D lever: both read; the first binding's unlisted 0 wins (raw 0, position -1)",
               o->has_reading && o->raw == 0 && o->position == -1);
        bool none = true;
        for (double h : { (double)NAN, (double)INFINITY, SIM_DETENT_UNLISTED + 0.5, 1e300, -SIM_DETENT_UNLISTED, 2.5,
                          4.0, SIM_DETENT_UNLISTED * 2 }) {
            o = step(m, { h, h }, "gear_lever");
            none = none && !o->has_reading && o->raw == 0 && o->position == -1;
        }
        expect("D lever: NaN, inf, 2^40 + 0.5, 1e300, -2^40, 2.5, 4 (past the positions), 2^41: no reading", none);
        const double *absent[1] = { nullptr };
        sim_merge_update(m, absent);
        o = sim_merge_find(m, "gear_lever");
        expect("D lever: the device absent: no reading", !o->has_reading);
        o = step(m, { SIM_DETENT_UNLISTED + 2, -1 }, "gear_lever");
        expect("D lever: a listed position written as unlisted (2^40 + 2) is that position (3, raw 2)",
               o->has_reading && o->position == 3 && o->raw == 2);
        expect("D lever: detent_int: 2 is position 3, 0 is 2^40 + 0, -5 is 2^40 - 5, 2^39 cannot be carried (-1)",
               sim_merge_detent_int(g, 2) == 3 && sim_merge_detent_int(g, 0) == SIM_DETENT_UNLISTED &&
               sim_merge_detent_int(g, -5) == SIM_DETENT_UNLISTED - 5 &&
               sim_merge_detent_int(g, 549755813888LL) == -1 && sim_merge_detent_int(g, -549755813888LL) == -1);
        expect("D lever: detent_text: \"1\" on an integer lever is no position (-1)", sim_merge_detent_text(g, "1") == -1);
        sim_merge_free(m);
    }
    {
        static const char one_int[] = "1", one_text[] = "1";
        static const char *const pos2[] = { one_int, one_text };
        static const char kinds2[] = "IS";
        static const long long ints2[] = { 1, 0 };
        Multi u;
        SimInput *g = u.input("g", SIM_IN_DETENT);
        g->npositions = 2;
        g->positions = pos2;
        g->position_kinds = kinds2;
        g->position_ints = ints2;
        u.bind(g, u.target(0, "b", SIM_IN_BUTTON))->position = pos2[1];
        SimMerge *m = u.build();
        bool by_int = step(m, { sim_merge_detent_int(g, 1) }, "b")->pressed;
        bool by_text = step(m, { sim_merge_detent_text(g, "1") }, "b")->pressed;
        expect("D lever: [1, \"1\"], bound on \"1\": the integer 1 releases, the text \"1\" presses", !by_int && by_text);
        sim_merge_free(m);
    }
    expect("D hat: sim_merge_hat_value: (0,0) 0; (-1,-1) left+up; (5,-5) right+up; (NaN,3) down; (inf,0) centred",
           sim_merge_hat_value(0, 0) == 0 && sim_merge_hat_value(-1, -1) == (SIM_HAT_LEFT | SIM_HAT_UP) &&
           sim_merge_hat_value(5, -5) == (SIM_HAT_RIGHT | SIM_HAT_UP) && sim_merge_hat_value(NAN, 3) == SIM_HAT_DOWN &&
           sim_merge_hat_value(INFINITY, 0) == 0);
}

static void part_d()
{
    part_d_levers();
    {
        Multi u;
        SimInput *a = u.input("trig", SIM_IN_AXIS);
        a->has_range = true;
        a->range = SimRange{0, 0, 1000};
        SimInput *t = u.target(0, "a", SIM_IN_BUTTON);
        t->analog = true;
        t->analog_range = SimRange{0, 0, 255};
        u.bind(a, t);
        SimMerge *m = u.build();
        const SimMergeOut *o = step(m, { 0 }, "a");
        expect("D: analog button at rest: not pressed, raw 0", o && !o->pressed && o->raw == 0);
        o = step(m, { 500 }, "a");
        expect("D: analog button at half: pressed, raw 128 (127.5 away from rest)", o && o->pressed && o->raw == 128);
        expect("D: sim_merge_output(0) is it, (1) and (-1) are NULL",
               sim_merge_output(m, 0) == o && !sim_merge_output(m, 1) && !sim_merge_output(m, -1) && o->bound);
        sim_merge_free(m);
    }
    {
        Multi u;
        SimInput *a = u.input("x", SIM_IN_AXIS);
        a->has_range = true;
        a->range = SimRange{-100, 0, 100};
        SimInput *l = u.target(0, "l", SIM_IN_AXIS), *r = u.target(1, "r", SIM_IN_AXIS);
        l->has_range = r->has_range = true;
        l->range = r->range = SimRange{0, 0, 1000};
        SimBinding *b = u.bind(a, l);
        b->target2 = r;
        b->policy = SIM_POLICY_SPLIT;
        SimMerge *m = u.build();
        const SimMergeOut *ol = step(m, { 40 }, "l"), *or_ = sim_merge_find(m, "r");
        expect("D: split +40 of 100: first 400, second 0", ol && or_ && ol->raw == 400 && or_->raw == 0);
        ol = step(m, { -25 }, "l");
        or_ = sim_merge_find(m, "r");
        expect("D: split -25 of 100: first 0, second 250", ol->raw == 0 && or_->raw == 250);
        sim_merge_free(m);
    }
    {
        Multi u;
        SimInput *h = u.input("pov", SIM_IN_HAT);
        SimInput *g = u.input("gate", SIM_IN_DETENT);
        static const char *const pos[] = { "n", "r" };
        g->npositions = 2;
        g->positions = pos;
        const char *dirs[] = { "up", "right", "down", "left" };
        for (int i = 0; i < 3; i++) {
            u.bind(h, u.target(i, dirs[i + 1], SIM_IN_BUTTON))->direction = dirs[i + 1];
        }
        u.bind(g, u.target(3, "rev", SIM_IN_BUTTON))->position = "r";
        SimMerge *m = u.build();
        step(m, { SIM_HAT_RIGHT | SIM_HAT_DOWN, 1 }, "rev");
        expect("D: hat right+down presses right and down, not left; detent r presses",
               sim_merge_find(m, "right")->pressed && sim_merge_find(m, "down")->pressed &&
               !sim_merge_find(m, "left")->pressed && sim_merge_find(m, "rev")->pressed);
        step(m, { SIM_HAT_LEFT, 2 }, "rev");
        expect("D: hat left presses left only; detent index 2 (past the end) presses nothing",
               sim_merge_find(m, "left")->pressed && !sim_merge_find(m, "right")->pressed &&
               !sim_merge_find(m, "rev")->pressed);
        step(m, { 0, -1 }, "rev");
        expect("D: detent -1 presses nothing", !sim_merge_find(m, "rev")->pressed);
        sim_merge_free(m);
    }
    {
        Multi u;
        SimInput *p = u.input("pedal", SIM_IN_AXIS);
        p->has_range = true;
        p->range = SimRange{0, 0, 100};
        p->invert = true;
        SimInput *k = u.input("press", SIM_IN_BUTTON);
        k->analog = true;
        k->analog_range = SimRange{0, 0, 255};
        SimInput *b1 = u.input("k1", SIM_IN_BUTTON), *b2 = u.input("k2", SIM_IN_BUTTON);
        SimInput *t = u.target(0, "t", SIM_IN_AXIS);
        t->has_range = true;
        t->range = SimRange{0, 0, 1000};
        u.bind(p, t);
        u.bind(k, u.target(1, "fire", SIM_IN_BUTTON));
        SimInput *ax = u.target(2, "ax", SIM_IN_AXIS);
        ax->has_range = true;
        ax->range = SimRange{-1000, 0, 1000};
        u.bind(b1, ax)->has_value = true;
        u.b[2].value = 0.5;
        u.bind(b2, ax)->has_value = true;
        u.b[3].value = -0.5;
        SimMerge *m = u.build();
        const SimMergeOut *o = step(m, { 25, 0, 1, 1 }, "t");
        expect("D: unipolar descriptor invert: 25 of 100 reads 0.75 -> 750", o->raw == 750);
        expect("D: an analog source button at rest does not press", !sim_merge_find(m, "fire")->pressed);
        expect("D: two keys pressed in the same update, values +0.5 and -0.5: the later binding counts as pressed "
               "later and drives (-500)", sim_merge_find(m, "ax")->raw == -500);
        step(m, { 25, 3, 0, 1 }, "t");
        expect("D: the analog button above rest presses; the second key alone gives -500",
               sim_merge_find(m, "fire")->pressed && sim_merge_find(m, "ax")->raw == -500);
        sim_merge_free(m);
    }
    {
        Multi u;
        SimInput *a = u.input("a", SIM_IN_AXIS);
        a->has_range = true;
        a->range = SimRange{0, 0, 1};
        SimBinding *b = u.bind(a, u.target(0, "btn", SIM_IN_BUTTON));
        b->has_threshold = true;
        b->threshold = 0.5;
        b->hysteresis = 0.25;
        SimMerge *m = u.build();
        bool p1 = step(m, { 0.5 }, "btn")->pressed, p2 = step(m, { 0.25 }, "btn")->pressed;
        bool p3 = step(m, { 0.2 }, "btn")->pressed;
        expect("D: threshold 0.5 hysteresis 0.25: 0.5 presses, 0.25 holds, 0.2 releases", p1 && p2 && !p3);
        step(m, { 0.5 }, "btn");
        sim_merge_reset(m);
        expect("D: after a reset, 0.3 (between release and press) is not pressed", !step(m, { 0.3 }, "btn")->pressed);
        sim_merge_free(m);
    }
    {
        Multi u;
        SimInput *mid = u.input("mid", SIM_IN_BUTTON);
        SimInput *rel = u.input("rel", SIM_IN_RELATIVE);
        SimInput *g = u.input("gate", SIM_IN_DETENT);
        SimInput *a = u.input("a", SIM_IN_AXIS);
        static const char *const pos[] = { "n", "r" };
        g->npositions = 2;
        g->positions = pos;
        a->has_range = true;
        a->range = SimRange{-1, 0, 1};
        SimInput *t = u.target(0, "aim", SIM_IN_AXIS);
        t->has_range = true;
        t->range = SimRange{0, 500, 1000};
        SimBinding *b = u.bind(rel, t);
        b->integrate = true;
        b->counts_per_travel = 100;
        b->recentre = mid;
        u.bind(g, u.target(1, "neutral", SIM_IN_BUTTON))->position = "n";
        b = u.bind(a, u.target(2, "neg", SIM_IN_BUTTON));
        b->has_threshold = true;
        b->threshold = -0.5;
        b->hysteresis = 0.25;
        SimMerge *m = u.build();
        expect("D: sim_merge_find of a control nobody drives is NULL", !sim_merge_find(m, "nobody"));
        const SimMergeOut *o = step(m, { 0, 10, 0, -0.5 }, "aim");
        expect("D: +10 of 100 counts: 600; detent position 0 (n) presses; -0.5 presses the negative threshold",
               o->raw == 600 && sim_merge_find(m, "neutral")->pressed && sim_merge_find(m, "neg")->pressed);
        step(m, { 1, 0, 1, -0.25 }, "aim");
        expect("D: the recentre button at input 0 recentres (500); -0.25 (= threshold + hysteresis) still holds",
               o->raw == 500 && !sim_merge_find(m, "neutral")->pressed && sim_merge_find(m, "neg")->pressed);
        step(m, { 0, 0, 1, -0.2 }, "aim");
        expect("D: -0.2 releases it", !sim_merge_find(m, "neg")->pressed);
        sim_merge_free(m);
    }
    {
        SimMerge *m = sim_merge_new(nullptr);
        expect("D: sim_merge_new(NULL) has no outputs", sim_merge_noutputs(m) == 0);
        sim_merge_free(m);
    }
    part_d_hostile_values();
}

static std::vector<std::string> doc_ids(const std::string &path)
{
    std::vector<std::string> ids;
    FILE *f = fopen(path.c_str(), "r");
    char line[512];
    while (f && fgets(line, sizeof(line), f)) {
        const char *p = strstr(line, "- { id: ");
        if (p && line[0] != '#') {
            p += 8;
            ids.push_back(std::string(p, strcspn(p, ", }")));
        }
    }
    if (f) {
        fclose(f);
    }
    return ids;
}

static void part_e(const std::string &data)
{
    SimProfileSet *set = sim_profile_set_load(data.c_str(), nullptr);
    std::vector<std::string> map_sbc, map_duke;
#define ID(id, name, ...) map_sbc.push_back(#id);
    SIM_SBC_BUTTONS(ID) SIM_SBC_AXES(ID)
#undef ID
#define ID(id, name, ...) map_duke.push_back(#id);
    SIM_DUKE_BUTTONS(ID) SIM_DUKE_AXES(ID)
#undef ID
    bool ok = true;
    for (auto &id : map_sbc) {
        ok = ok && sim_profile_target_control(set, "steel_battalion", id.c_str());
    }
    std::vector<std::string> d = doc_ids(data + "/target_steel_battalion.yaml");
    int unmapped = 0;
    for (auto &id : d) {
        bool found = false;
        for (auto &m : map_sbc) {
            found = found || m == id;
        }
        unmapped += !found && id != "gear_lever" && id != "tuner_dial";
    }
    expect("E: every SBC map id is a control of steel_battalion, and every control but the two detents is mapped (" +
           std::to_string(d.size()) + " controls)", ok && unmapped == 0 && d.size() == 53);
    ok = true;
    for (auto &id : map_duke) {
        ok = ok && sim_profile_target_control(set, "xbox_duke", id.c_str());
    }
    d = doc_ids(data + "/target_xbox_duke.yaml");
    expect("E: the Duke map and target_xbox_duke.yaml name the same 20 controls", ok && d.size() == 20 &&
           map_duke.size() == 20);
    sim_profile_set_free(set);

    {
        int badtok = 0, n = 0;
        auto upper = [](const char *id) {
            std::string u(id);
            for (char &ch : u) {
                ch = (char)toupper((unsigned char)ch);
            }
            return u;
        };
#define TOK(id, name, ...) n++; badtok += upper(#id) != #name;
        SIM_SBC_BUTTONS(TOK) SIM_DUKE_BUTTONS(TOK)
#undef TOK
        expect("E: every button's header-name token is its id upper-cased (" + std::to_string(n) + " buttons)",
               badtok == 0 && n == 57);
    }

    Multi u;
    const char *ids[4] = { "gear_up", "gear_down", "tuner_left", "filt_control_system" };
    for (int i = 0; i < 4; i++) {
        u.bind(u.input(ids[i], SIM_IN_BUTTON), u.target(i, ids[i], SIM_IN_BUTTON));
    }
    SimMerge *m = u.build();
    uint64_t btn = 0, prev = 0;
    int16_t ax[SIM_SBC_NAXES];
    uint8_t gear = 0, tuner = 0, tog = 0;
    auto press = [&](std::vector<double> v) {
        const double *vals[1] = { v.data() };
        sim_merge_update(m, vals);
        sim_target_write_sbc(m, &btn, &prev, ax, &gear, &tuner, &tog);
    };
    press({ 0, 0, 0, 0 });
    expect("E: the gear lever starts at 255 (neutral)", gear == 255);
    press({ 1, 0, 0, 0 });
    press({ 1, 0, 0, 0 });
    expect("E: gear_up held two updates steps once: 255 -> 1", gear == 1);
    for (int i = 0; i < 6; i++) {
        press({ 0, 0, 0, 0 });
        press({ 1, 0, 0, 0 });
    }
    expect("E: six more presses stop at 5", gear == 5);
    for (int i = 0; i < 7; i++) {
        press({ 0, 0, 0, 0 });
        press({ 0, 1, 0, 0 });
    }
    expect("E: seven gear_down presses: 5 4 3 2 1 255 254", gear == 254);
    press({ 0, 0, 0, 0 });
    press({ 0, 1, 0, 0 });
    expect("E: and it stays at 254", gear == 254);
    press({ 0, 0, 1, 0 });
    expect("E: tuner_left from 0 wraps to 15", tuner == 15);
    press({ 0, 0, 0, 1 });
    expect("E: a press of filt_control_system latches its toggle (0x04) and its bit 34 is set while held",
           tog == 0x04 && (btn >> 34 & 1));
    press({ 0, 0, 0, 1 });
    press({ 0, 0, 0, 0 });
    expect("E: held, then released: still latched", tog == 0x04 && !(btn >> 34 & 1));
    press({ 0, 0, 0, 1 });
    expect("E: a second press unlatches it", tog == 0);
    sim_merge_free(m);

    {
        static const char *const gpos[] = { "254", "255", "1", "2", "3", "4", "5" };
        static const char gkinds[] = "IIIIIII";
        static const long long gints[] = { 254, 255, 1, 2, 3, 4, 5 };
        static const char *const tpos[] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12",
                                            "13", "14", "15" };
        static const char tkinds[] = "IIIIIIIIIIIIIIII";
        static const long long tints[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
        auto lever = [](SimInput *x, int n, const char *const *p, const char *k, const long long *v) {
            x->npositions = n;
            x->positions = p;
            x->position_kinds = k;
            x->position_ints = v;
        };
        Multi l;
        SimInput *gs = l.input("gear_src", SIM_IN_DETENT), *ts = l.input("tuner_src", SIM_IN_DETENT);
        SimInput *up = l.input("up_key", SIM_IN_BUTTON);
        lever(gs, 7, gpos, gkinds, gints);
        lever(ts, 16, tpos, tkinds, tints);
        SimInput *gt = l.target(0, "gear_lever", SIM_IN_DETENT), *tt = l.target(1, "tuner_dial", SIM_IN_DETENT);
        lever(gt, 7, gpos, gkinds, gints);
        lever(tt, 16, tpos, tkinds, tints);
        l.bind(gs, gt);
        l.bind(ts, tt);
        l.bind(up, l.target(2, "gear_up", SIM_IN_BUTTON));
        SimMerge *lm = l.build();
        uint64_t lb = 0, lp = 0;
        uint8_t lg = 0, lt = 0, ltog = 0;
        auto write = [&](double g, double t, double k) {
            std::vector<double> v = { g, t, k };
            const double *vals[1] = { v.data() };
            sim_merge_update(lm, vals);
            sim_target_write_sbc(lm, &lb, &lp, ax, &lg, &lt, &ltog);
        };
        std::vector<int> seen;
        for (long long r : { 2LL, 0LL, 1LL, 0LL, 255LL, 0LL, 254LL }) {
            write(sim_merge_detent_int(gs, r), sim_merge_detent_int(ts, 7), 0);
            seen.push_back(lg);
        }
        expect("E: a lever on the gear lever: readings 2 0 1 0 255 0 254 reach the byte unchanged (0 stays 0); the "
               "tuner dial reads 7", seen == std::vector<int>({ 2, 0, 1, 0, 255, 0, 254 }) && lt == 7);
        write(sim_merge_detent_int(gs, 3), sim_merge_detent_int(ts, 7), 1);
        expect("E: while the lever reads 3, a gear_up press does not step it", lg == 3);
        lg = 0;
        write(-1, -1, 0);
        expect("E: the levers report nothing (-1): a gear lever at 0 becomes 255, the dial keeps 7", lg == 255 && lt == 7);
        write(-1, -1, 1);
        expect("E: ... and gear_up steps it again: 255 -> 1", lg == 1);
        lg = 0;
        const double *absent[1] = { nullptr };
        sim_merge_update(lm, absent);
        sim_target_write_sbc(lm, &lb, &lp, ax, &lg, &lt, &ltog);
        expect("E: the device absent: a gear lever at 0 becomes 255 (as before)", lg == 255);
        sim_merge_free(lm);

        static const char *const npos[] = { "n", "r" };
        Multi x;
        SimInput *xs = x.input("g", SIM_IN_DETENT), *xt = x.target(0, "gear_lever", SIM_IN_DETENT);
        lever(xs, 2, npos, "SS", nullptr);
        lever(xt, 2, npos, "SS", nullptr);
        x.bind(xs, xt);
        SimMerge *xm = x.build();
        double r = sim_merge_detent_text(xs, "n");
        const double *rv[1] = { &r };
        sim_merge_update(xm, rv);
        lg = 0;
        sim_target_write_sbc(xm, &lb, &lp, ax, &lg, &lt, &ltog);
        expect("E: a text reading on a gear lever control is no byte: 0 becomes 255 (as before)",
               sim_merge_find(xm, "gear_lever")->has_reading && lg == 255);
        sim_merge_free(xm);
    }

    Multi a;
    SimInput *sx = a.input("sx", SIM_IN_AXIS);
    sx->has_range = true;
    sx->range = SimRange{-32768, 0, 32767};
    const char *axs[4] = { "aiming_x", "left_pedal", "rotation_lever", "aiming_y" };
    SimRange tr[4] = { { 0, 32768, 65535 }, { 0, 0, 65535 }, { -32768, 0, 32767 }, { 0, 32768, 65535 } };
    for (int i = 0; i < 3; i++) {
        SimInput *t = a.target(i, axs[i], SIM_IN_AXIS);
        t->has_range = true;
        t->range = tr[i];
        SimBinding *b = a.bind(sx, t);
        if (i == 1) {
            b->policy = SIM_POLICY_FULL_RANGE;
        }
    }
    m = a.build();
    double v = -255.0 * 32768.0 / 32768.0;
    const double *vals[1] = { &v };
    sim_merge_update(m, vals);
    sim_target_write_sbc(m, &btn, &prev, ax, &gear, &tuner, &tog);
    expect("E: aiming wire 0x7F01 -> axis -256, report byte 127 (today's truncation gives 128)",
           ax[0] == -256 && (uint8_t)(128 + ax[0] / 256) == 127);
    expect("E: an undriven aiming_y stays 0 (centre)", ax[1] == 0);
    v = 32767;
    sim_merge_update(m, vals);
    sim_target_write_sbc(m, &btn, &prev, ax, &gear, &tuner, &tog);
    expect("E: full_range +1 onto a pedal: 65535 bit-cast to -1 (D3: the report reads 0xFFFF)",
           ax[3] == -1 && (uint16_t)ax[3] == 0xFFFF);
    expect("E: rotation 32767 copied; aiming full right gives byte 255", ax[2] == 32767 &&
           (uint8_t)(128 + ax[0] / 256) == 255);
    sim_merge_free(m);

    Multi k;
    SimInput *tg = k.input("tg", SIM_IN_AXIS), *st = k.input("st", SIM_IN_AXIS), *bt = k.input("bt", SIM_IN_BUTTON);
    tg->has_range = st->has_range = true;
    tg->range = SimRange{0, 0, 255};
    st->range = SimRange{-32768, 0, 32767};
    SimInput *rt = k.target(0, "right_trigger", SIM_IN_AXIS), *ly = k.target(1, "left_stick_y", SIM_IN_AXIS);
    rt->has_range = ly->has_range = true;
    rt->range = SimRange{0, 0, 255};
    ly->range = SimRange{-32768, 0, 32767};
    k.bind(tg, rt);
    k.bind(st, ly);
    k.bind(bt, k.target(2, "black", SIM_IN_BUTTON));
    m = k.build();
    bool all = true;
    for (int t = 0; t <= 255; t++) {
        std::vector<double> in = { (double)t, -12345, t == 255 ? 1.0 : 0.0 };
        const double *vv[1] = { in.data() };
        sim_merge_update(m, vv);
        uint16_t gb;
        int16_t ga[SIM_DUKE_NAXES];
        sim_target_write_duke(m, &gb, ga);
        all = all && (ga[1] >> 7) == t && ga[3] == -12345 && ga[0] == 0 && ((gb >> 11) & 1) == (t == 255);
    }
    expect("E: Duke: every trigger value 0..255 comes back from axis >> 7; the stick is copied; black sets bit 11", all);
    sim_merge_free(m);
}

static std::string yval(ryml::ConstNodeRef n)
{
    return std::string(n.val().str, n.val().len);
}

static double ynum(ryml::ConstNodeRef n)
{
    return strtod(yval(n).c_str(), nullptr);
}

static bool yhas(ryml::ConstNodeRef n, const char *k)
{
    return n.is_map() && n.has_child(ryml::to_csubstr(k));
}

static SimRange yrange(ryml::ConstNodeRef n)
{
    return SimRange{ ynum(n["min"]), ynum(n["rest"]), ynum(n["max"]) };
}

struct Typed {
    char k;
    long long i;
    std::string s;
    bool operator==(const Typed &o) const { return k == o.k && (k == 'I' ? i == o.i : s == o.s); }
};

static Typed ytyped(ryml::ConstNodeRef n)
{
    Typed t{ 0, 0, yval(n) };
    t.k = sim_profile_scalar_kind(t.s.c_str(), n.is_val_quoted(), &t.i, nullptr);
    return t;
}

static double lever_value(const SimInput *lever, const Typed &reading)
{
    return reading.k == 'I' ? sim_merge_detent_int(lever, reading.i) :
           reading.k == 'S' ? sim_merge_detent_text(lever, reading.s.c_str()) : -1.0;
}

static double hat_value(double x, double y)
{
    return sim_merge_hat_value(x, y);
}

static bool lever_out_built()
{
    return true;
}

static bool lever_out_matches(const SimMergeOut *o, const Typed &want, std::string *why)
{
    bool ok = false;
    std::string got = " no reading";
    if (o && o->has_reading) {
        const SimInput *c = o->control;
        bool listed = o->position >= 0 && o->position < c->npositions;
        char kind = listed ? (c->position_kinds ? c->position_kinds[o->position] : 'S') : 'I';
        if (kind == 'I') {
            ok = want.k == 'I' && o->raw == (double)want.i && (!listed || c->position_ints[o->position] == want.i);
            got = " integer " + std::to_string((long long)o->raw);
        } else {
            ok = want.k == 'S' && want.s == c->positions[o->position];
            got = std::string(" text ") + c->positions[o->position];
        }
    }
    if (!ok) {
        *why += " reading " + want.s + ":" + got;
    }
    return ok;
}

static void part_f(const std::string &dir)
{
    std::string path = dir + "/shared/vectors.yaml";
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) {
        expect("F: open " + path, false);
        return;
    }
    std::string text;
    char buf[8192];
    size_t got;
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0) {
        text.append(buf, got);
    }
    fclose(f);
    ryml::Tree tree = ryml::parse_in_arena(ryml::to_csubstr(path), ryml::csubstr(text.data(), text.size()));
    ryml::ConstNodeRef vectors = tree.crootref()["vectors"];
    static const char *const known[] = { "id", "rule", "kind", "source", "target", "targets", "binding", "raw",
                                         "expect", "calibration", "source_invert", "value", "rounded", "wire",
                                         "axis", "byte", "steps", "returns_to_rest", "bindings", "raws", "rates",
                                         "integrates", "positions", "readings" };
    static const char *const known_binding[] = { "policy", "invert", "window", "deadzone", "saturation", "curve",
                                                 "counts_per_travel", "threshold", "hysteresis", "position",
                                                 "direction" };
    const bool table = getenv("SIMMERGE_VECTOR_TABLE") != nullptr;
    int n = 0, bad = 0;
    for (ryml::ConstNodeRef v : vectors.children()) {
        std::string id = yval(v["id"]), kind = yval(v["kind"]);
        bool ok = true, not_built = false;
        std::string why;
        for (ryml::ConstNodeRef k : v.children()) {
            std::string key(k.key().str, k.key().len);
            bool seen = false;
            for (const char *kn : known) {
                seen = seen || key == kn;
            }
            if (!seen) {
                ok = false;
                why += " unknown field " + key;
            }
        }
        if (yhas(v, "binding")) {
            for (ryml::ConstNodeRef k : v["binding"].children()) {
                std::string key(k.key().str, k.key().len);
                bool seen = false;
                for (const char *kn : known_binding) {
                    seen = seen || key == kn;
                }
                if (!seen) {
                    ok = false;
                    why += " unknown binding field " + key;
                }
            }
        }
        if (ok && (kind == "axis" || kind == "split" || kind == "integrate")) {
            Multi u;
            ryml::ConstNodeRef b = v["binding"];
            SimInput *s = u.input("s", kind == "integrate" ? SIM_IN_RELATIVE : SIM_IN_AXIS);
            SimInput *rc = kind == "integrate" ? u.input("recentre", SIM_IN_BUTTON) : nullptr;
            SimCalibrated cal;
            if (kind != "integrate") {
                s->has_range = true;
                s->range = yrange(v["source"]);
                s->invert = yhas(v, "source_invert") && yval(v["source_invert"]) == "true";
                if (yhas(v, "calibration")) {
                    cal.input = s;
                    cal.range = yrange(v["calibration"]);
                    u.dev.ncalibrated = 1;
                    u.dev.calibrated = &cal;
                }
            }
            SimInput *t1 = u.target(0, "t1", SIM_IN_AXIS), *t2 = nullptr;
            t1->has_range = true;
            t1->range = yrange(kind == "split" ? v["targets"][0] : v["target"]);
            SimBinding *x = u.bind(s, t1);
            if (kind == "split") {
                t2 = u.target(1, "t2", SIM_IN_AXIS);
                t2->has_range = true;
                t2->range = yrange(v["targets"][1]);
                x->target2 = t2;
            }
            if (yhas(b, "policy")) {
                x->policy = policy(yval(b["policy"]));
            }
            x->resp_invert = yhas(b, "invert") && yval(b["invert"]) == "true";
            if (yhas(b, "window")) {
                x->has_window = true;
                x->window_lo = ynum(b["window"][0]);
                x->window_hi = ynum(b["window"][1]);
            }
            if (yhas(b, "deadzone")) {
                x->has_deadzone = true;
                x->resp_deadzone = ynum(b["deadzone"]);
            }
            if (yhas(b, "saturation")) {
                x->has_saturation = true;
                x->saturation = ynum(b["saturation"]);
            }
            if (yhas(b, "curve")) {
                x->has_curve = true;
                x->curve = ynum(b["curve"]);
            }
            if (kind == "integrate") {
                x->integrate = true;
                x->counts_per_travel = ynum(b["counts_per_travel"]);
                x->recentre = rc;
            }
            SimMerge *m = u.build();
            ryml::ConstNodeRef e = v["expect"];
            if (kind == "integrate") {
                int i = 0;
                for (ryml::ConstNodeRef st : v["steps"].children()) {
                    std::string sv = yval(st);
                    std::vector<double> in = { sv == "recentre" ? 0.0 : strtod(sv.c_str(), nullptr),
                                               sv == "recentre" ? 1.0 : 0.0 };
                    const SimMergeOut *o = step(m, in, "t1");
                    double p = sim_merge_denormalise(t1->range, o->canonical);
                    if (std::fabs(p - ynum(e["positions"][i])) > 1e-6 || o->raw != ynum(e["rounded"][i])) {
                        ok = false;
                        why += " step " + std::to_string(i) + " position " + std::to_string(p);
                    }
                    i++;
                }
            } else {
                const SimMergeOut *o1 = step(m, { ynum(v["raw"]) }, "t1");
                double v1 = sim_merge_denormalise(t1->range, o1->canonical);
                if (kind == "axis") {
                    ok = std::fabs(v1 - ynum(e["value"])) <= 1e-6 && o1->raw == ynum(e["rounded"]);
                    why = " value " + std::to_string(v1) + " rounded " + std::to_string((long)o1->raw);
                } else {
                    const SimMergeOut *o2 = sim_merge_find(m, "t2");
                    double v2 = sim_merge_denormalise(t2->range, o2->canonical);
                    ok = std::fabs(v1 - ynum(e["value"][0])) <= 1e-6 && std::fabs(v2 - ynum(e["value"][1])) <= 1e-6 &&
                         o1->raw == ynum(e["rounded"][0]) && o2->raw == ynum(e["rounded"][1]);
                    why = " values " + std::to_string(v1) + " " + std::to_string(v2);
                }
            }
            sim_merge_free(m);
        } else if (ok && kind == "button_axis") {
            Multi u;
            SimInput *t1 = u.target(0, "t1", SIM_IN_AXIS);
            t1->has_range = true;
            t1->range = yrange(v["target"]);
            t1->stays_put = yhas(v, "returns_to_rest") && yval(v["returns_to_rest"]) == "false";
            std::vector<std::string> names;
            for (ryml::ConstNodeRef k : v["bindings"].children()) {
                names.push_back(std::string(k.key().str, k.key().len));
            }
            std::vector<std::string> order = names;
            static std::string keep[4];
            for (size_t i = 0; ok && i < order.size() && i < 4; i++) {
                keep[i] = order[i];
                ryml::ConstNodeRef b = v["bindings"][ryml::to_csubstr(order[i])];
                for (ryml::ConstNodeRef k : b.children()) {
                    std::string key(k.key().str, k.key().len);
                    if (key != "value" && key != "ramp" && key != "rate") {
                        ok = false;
                        why += " unknown key field " + key;
                    }
                }
                SimBinding *x = u.bind(u.input(keep[i].c_str(), SIM_IN_BUTTON), t1);
                if (yhas(b, "value")) {
                    x->has_value = true;
                    x->value = ynum(b["value"]);
                }
                if (yhas(b, "ramp")) {
                    x->has_ramp = true;
                    x->press_ms = yhas(b["ramp"], "press_ms") ? ynum(b["ramp"]["press_ms"]) : 0.0;
                    x->release_ms = yhas(b["ramp"], "release_ms") ? ynum(b["ramp"]["release_ms"]) : 0.0;
                }
                if (yhas(b, "rate")) {
                    x->has_rate = true;
                    x->rate = ynum(b["rate"]);
                }
            }
            if (ok) {
                SimMerge *m = u.build();
                ryml::ConstNodeRef e = v["expect"];
                int i = 0;
                for (ryml::ConstNodeRef st : v["steps"].children()) {
                    std::vector<double> in(order.size(), 0.0);
                    for (ryml::ConstNodeRef h : st["held"].children()) {
                        for (size_t q = 0; q < order.size(); q++) {
                            if (order[q] == yval(h)) {
                                in[q] = 1.0;
                            }
                        }
                    }
                    const double *vals[1] = { in.data() };
                    sim_merge_update_dt(m, vals, ynum(st["dt_ms"]));
                    const SimMergeOut *o = sim_merge_find(m, "t1");
                    double val = sim_merge_denormalise(t1->range, o->canonical);
                    if (std::fabs(val - ynum(e["values"][i])) > 1e-6 || o->raw != ynum(e["rounded"][i])) {
                        ok = false;
                        why += " step " + std::to_string(i) + " value " + std::to_string(val) + " rounded " +
                               std::to_string((long)o->raw);
                    }
                    i++;
                }
                sim_merge_free(m);
            }
        } else if (ok && kind == "stay_put") {
            Multi u;
            SimInput *t1 = u.target(0, "t1", SIM_IN_AXIS);
            t1->has_range = true;
            t1->range = yrange(v["target"]);
            t1->stays_put = true;
            static std::string nm[4];
            std::vector<std::string> names;
            auto input_of = [&](const std::string &n, SimInputKind k) -> SimInput * {
                for (size_t q = 0; q < names.size(); q++) {
                    if (names[q] == n) {
                        return &u.in[q];
                    }
                }
                if (names.size() >= 4) {
                    return nullptr;
                }
                nm[names.size()] = n;
                names.push_back(n);
                return u.input(nm[names.size() - 1].c_str(), k);
            };
            struct Wire { SimBinding *b; std::string recentre; };
            std::vector<Wire> wires;
            for (ryml::ConstNodeRef g : v["integrates"].children()) {
                for (ryml::ConstNodeRef k : g.children()) {
                    std::string key(k.key().str, k.key().len);
                    if (key != "name" && key != "counts_per_travel" && key != "invert" && key != "source_invert" &&
                        key != "recentre") {
                        ok = false;
                        why += " unknown integrate field " + key;
                    }
                }
                SimInput *src = input_of(yval(g["name"]), SIM_IN_RELATIVE);
                if (!src) {
                    ok = false;
                    why += " more inputs than this test wires";
                    break;
                }
                src->invert = yhas(g, "source_invert") && yval(g["source_invert"]) == "true";
                SimBinding *x = u.bind(src, t1);
                x->integrate = true;
                x->counts_per_travel = ynum(g["counts_per_travel"]);
                x->resp_invert = yhas(g, "invert") && yval(g["invert"]) == "true";
                wires.push_back({ x, yhas(g, "recentre") ? yval(g["recentre"]) : std::string() });
            }
            for (ryml::ConstNodeRef r : v["rates"].children()) {
                SimInput *src = ok ? input_of(std::string(r.key().str, r.key().len), SIM_IN_BUTTON) : nullptr;
                if (!src || u.nb >= 4) {
                    ok = false;
                    why += " more inputs or bindings than this test wires";
                    break;
                }
                SimBinding *x = u.bind(src, t1);
                x->has_rate = true;
                x->rate = ynum(r["rate"]);
            }
            for (Wire &w : wires) {
                if (!w.recentre.empty()) {
                    SimInput *rc = ok ? input_of(w.recentre, SIM_IN_BUTTON) : nullptr;
                    if (!rc) {
                        ok = false;
                        why += " no room for the recentre button";
                    }
                    w.b->recentre = rc;
                }
            }
            if (ok) {
                SimMerge *m = u.build();
                ryml::ConstNodeRef e = v["expect"];
                int i = 0;
                for (ryml::ConstNodeRef st : v["steps"].children()) {
                    std::vector<double> in(names.size(), 0.0);
                    for (ryml::ConstNodeRef h : st["held"].children()) {
                        for (size_t q = 0; q < names.size(); q++) {
                            if (names[q] == yval(h)) {
                                in[q] = 1.0;
                            }
                        }
                    }
                    if (yhas(st, "counts")) {
                        for (ryml::ConstNodeRef c : st["counts"].children()) {
                            for (size_t q = 0; q < names.size(); q++) {
                                if (names[q] == std::string(c.key().str, c.key().len)) {
                                    in[q] = ynum(c);
                                }
                            }
                        }
                    }
                    const double *vals[1] = { in.data() };
                    sim_merge_update_dt(m, vals, ynum(st["dt_ms"]));
                    const SimMergeOut *o = sim_merge_find(m, "t1");
                    double val = sim_merge_denormalise(t1->range, o->canonical);
                    if (std::fabs(val - ynum(e["values"][i])) > 1e-6 || o->raw != ynum(e["rounded"][i])) {
                        ok = false;
                        why += " step " + std::to_string(i) + " value " + std::to_string(val) + " rounded " +
                               std::to_string((long)o->raw);
                    }
                    i++;
                }
                sim_merge_free(m);
            }
        } else if (ok && kind == "axis_button") {
            Multi u;
            ryml::ConstNodeRef b = v["binding"];
            SimInput *s = u.input("s", SIM_IN_AXIS);
            s->has_range = true;
            s->range = yrange(v["source"]);
            SimBinding *x = u.bind(s, u.target(0, "t1", SIM_IN_BUTTON));
            x->has_threshold = true;
            x->threshold = ynum(b["threshold"]);
            x->hysteresis = ynum(b["hysteresis"]);
            x->resp_invert = yhas(b, "invert") && yval(b["invert"]) == "true";
            s->invert = yhas(v, "source_invert") && yval(v["source_invert"]) == "true";
            SimMerge *m = u.build();
            int i = 0;
            for (ryml::ConstNodeRef r : v["raws"].children()) {
                bool pressed = step(m, { ynum(r) }, "t1")->pressed;
                if (pressed != (yval(v["expect"]["pressed"][i]) == "true")) {
                    ok = false;
                    why += " raw " + yval(r) + (pressed ? " pressed" : " released");
                }
                i++;
            }
            sim_merge_free(m);
        } else if (ok && (kind == "detent_button" || kind == "detent_detent")) {
            Multi u;
            static std::vector<std::string> ptext;
            ptext.clear();
            std::vector<Typed> ptyped;
            for (ryml::ConstNodeRef q : v["positions"].children()) {
                ptext.push_back(yval(q));
                ptyped.push_back(ytyped(q));
            }
            static std::vector<const char *> pp;
            pp.clear();
            for (const std::string &t : ptext) {
                pp.push_back(t.c_str());
            }
            static std::string pkinds;
            static std::vector<long long> pints;
            pkinds.clear();
            pints.clear();
            for (const Typed &q : ptyped) {
                pkinds.push_back(q.k == 'I' ? 'I' : 'S');
                pints.push_back(q.k == 'I' ? q.i : 0);
            }
            SimInput *s = u.input("s", SIM_IN_DETENT);
            s->npositions = (int)pp.size();
            s->positions = pp.data();
            s->position_kinds = pkinds.c_str();
            s->position_ints = pints.data();
            SimInput *t1 = u.target(0, "t1", kind == "detent_button" ? SIM_IN_BUTTON : SIM_IN_DETENT);
            if (kind == "detent_detent") {
                t1->npositions = s->npositions;
                t1->positions = s->positions;
                t1->position_kinds = s->position_kinds;
                t1->position_ints = s->position_ints;
            }
            SimBinding *x = u.bind(s, t1);
            if (kind == "detent_button") {
                Typed want = ytyped(v["binding"]["position"]);
                int at = -1;
                for (size_t q = 0; q < ptyped.size() && at < 0; q++) {
                    at = ptyped[q] == want ? (int)q : -1;
                }
                x->position = at >= 0 ? s->positions[at] : nullptr;
            }
            SimMerge *m = u.build();
            int i = 0;
            for (ryml::ConstNodeRef r : v["readings"].children()) {
                Typed rd = ytyped(r);
                double val = lever_value(s, rd);
                const SimMergeOut *o = step(m, { val }, "t1");
                if (kind == "detent_button") {
                    bool pressed = o && o->pressed;
                    if (pressed != (yval(v["expect"]["pressed"][i]) == "true")) {
                        ok = false;
                        why += " reading " + yval(r) + (pressed ? " pressed" : " released");
                    }
                } else if (!lever_out_matches(o, ytyped(v["expect"]["values"][i]), &why)) {
                    ok = false;
                    not_built = not_built || !lever_out_built();
                }
                i++;
            }
            sim_merge_free(m);
        } else if (ok && kind == "hat_button") {
            Multi u;
            SimInput *s = u.input("s", SIM_IN_HAT);
            SimBinding *x = u.bind(s, u.target(0, "t1", SIM_IN_BUTTON));
            static std::string dir;
            dir = yval(v["binding"]["direction"]);
            x->direction = dir.c_str();
            SimMerge *m = u.build();
            int i = 0;
            for (ryml::ConstNodeRef r : v["readings"].children()) {
                double bits = hat_value(ynum(r[0]), ynum(r[1]));
                const SimMergeOut *o = step(m, { bits }, "t1");
                bool pressed = o && o->pressed;
                if (pressed != (yval(v["expect"]["pressed"][i]) == "true")) {
                    ok = false;
                    why += " reading [" + yval(r[0]) + ", " + yval(r[1]) + "]" + (pressed ? " pressed" : " released");
                }
                i++;
            }
            sim_merge_free(m);
        } else if (ok && kind == "round") {
            double r = sim_merge_round(ynum(v["value"]), ynum(v["target"]["rest"]));
            ok = r == ynum(v["rounded"]);
            why = " rounded " + std::to_string((long)r);
        } else if (ok && kind == "sbc_byte") {
            int16_t a = sim_merge_sbc_aiming_axis(ynum(v["wire"]));
            int byte = (uint8_t)(128 + (a / 256));
            ok = byte == (int)ynum(v["byte"]);
            why = " byte " + std::to_string(byte);
        } else if (ok) {
            ok = false;
            why = " unknown kind " + kind;
        }
        n++;
        if (table) {
            printf("VEC %s %s%s\n", id.c_str(), ok ? "AGREES" : not_built ? "NOT-BUILT" : "DISAGREES",
                   ok ? "" : why.c_str());
        }
        if (!ok) {
            bad++;
            expect("F: " + id + why, false);
        }
    }
    expect("F: " + std::to_string(n - bad) + " of " + std::to_string(n) + " shared vectors agree", n >= 91 && bad == 0);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: simmerge-test DATA_DIR TEST_DIR\n");
        return 2;
    }
    part_a(argv[2]);
    part_b(argv[2]);
    part_c(argv[1], argv[2]);
    part_d();
    part_e(argv[1]);
    part_f(argv[2]);
    printf("%d checks, %d failed\n", nchecks, nfailed);
    return nfailed ? 1 : 0;
}
