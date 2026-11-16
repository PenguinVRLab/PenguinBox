/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include <dirent.h>

#include "xemu-sim-profile-reader.h"

#ifndef SIM_PROFILE_MAX_FILE_BYTES
#define SIM_PROFILE_MAX_FILE_BYTES (1024 * 1024)
#endif

static int fails, checks;

static void expect(const std::string &what, bool ok)
{
    checks++;
    fails += !ok;
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
}

static std::string slurp(const std::string &p)
{
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void spit(const std::string &p, const std::string &s)
{
    std::ofstream(p, std::ios::binary) << s;
}

static std::vector<std::string> yaml_files(const std::string &dir)
{
    std::vector<std::string> out;
    DIR *d = opendir(dir.c_str());
    while (d) {
        struct dirent *e = readdir(d);
        if (!e) {
            break;
        }
        std::string n = e->d_name;
        if (n.size() > 5 && n.compare(n.size() - 5, 5, ".yaml") == 0) {
            out.push_back(n);
        }
    }
    if (d) {
        closedir(d);
    }
    return out;
}

static std::string top(const std::string &text, const char *key)
{
    std::istringstream in(text);
    std::string line, k = std::string(key) + ":";
    while (std::getline(in, line)) {
        if (line.compare(0, k.size(), k) == 0) {
            std::string v = line.substr(k.size());
            size_t a = v.find_first_not_of(" \""), b = v.find_last_not_of(" \"\r");
            return a == std::string::npos ? "" : v.substr(a, b - a + 1);
        }
    }
    return "";
}

static bool has_refusal(const SimProfileSet *s, const char *code, const char *where_part)
{
    for (int i = 0; i < sim_profile_set_nrefused(s); i++) {
        const SimRefusal *r = sim_profile_set_refusal(s, i);
        if (std::strcmp(r->code, code) == 0 && (!where_part || std::strstr(r->where, where_part))) {
            return true;
        }
    }
    return false;
}

static void print_refusals(const SimProfileSet *s)
{
    for (int i = 0; i < sim_profile_set_nrefused(s); i++) {
        const SimRefusal *r = sim_profile_set_refusal(s, i);
        std::printf("     refused %s %s: %s\n", r->code, r->where, r->why);
    }
}

static const SimPortDevice *dev_of(const SimPort &p, const char *id)
{
    for (int i = 0; i < p.ndevices; i++) {
        if (std::strcmp(p.devices[i].device_id, id) == 0) {
            return &p.devices[i];
        }
    }
    return nullptr;
}

static std::string DATA, GOOD, SCRATCH, BAD;

#ifndef SIMPROFILE_DATA
#define SIMPROFILE_DATA "../../../data/sim-profiles"
#endif

static std::string mutated(const char *name, const char *file, const char *find, const char *repl)
{
    std::string dir = SCRATCH + "/" + name;
    std::string cmd = "rm -rf '" + dir + "' && mkdir -p '" + dir + "' && cp '" + GOOD + "'/*.yaml '" + dir + "'/";
    if (std::system(cmd.c_str()) != 0) {
        expect(std::string(name) + ": scratch copy", false);
    }
    std::string p = dir + "/" + file;
    if (!find) {
        spit(p, repl);
        return dir;
    }
    std::string s = slurp(p);
    size_t at = s.find(find);
    bool once = at != std::string::npos && s.find(find, at + 1) == std::string::npos;
    expect(std::string(name) + ": the mutation changes " + file + " exactly once", once);
    if (once) {
        s.replace(at, std::strlen(find), repl);
        spit(p, s);
    }
    return dir;
}

struct Mut {
    const char *name, *file, *find, *repl, *code, *where;
    std::function<void(const SimProfileSet *)> also;
};

struct Hostile {
    bool exited = false;
    int status = 0;
    double cpu_s = 0;
    long maxrss_kb = 0;
    std::vector<std::string> refusals;
    bool has_dv = false;
};

static Hostile load_capped(const std::string &dir)
{
    Hostile h;
    int fd[2];
    if (pipe(fd) != 0) {
        return h;
    }
    pid_t pid = fork();
    if (pid == 0) {
        close(fd[0]);
        struct rlimit as = { 1UL << 30, 1UL << 30 }, cpu = { 60, 60 };
        setrlimit(RLIMIT_AS, &as);
        setrlimit(RLIMIT_CPU, &cpu);
        alarm(30);
        SimProfileSet *s = sim_profile_set_load(DATA.c_str(), dir.c_str());
        std::string out = sim_profile_set_has(s, "device", "dv") ? "has dv\n" : "no dv\n";
        for (int i = 0; i < sim_profile_set_nrefused(s); i++) {
            const SimRefusal *r = sim_profile_set_refusal(s, i);
            out += std::string(r->code) + "\t" + r->where + "\t" + r->why + "\n";
        }
        if (write(fd[1], out.data(), out.size()) < 0) {
            _exit(3);
        }
        _exit(0);
    }
    close(fd[1]);
    std::string got;
    char buf[4096];
    for (ssize_t n; (n = read(fd[0], buf, sizeof buf)) > 0;) {
        got.append(buf, (size_t)n);
    }
    close(fd[0]);
    struct rusage ru;
    if (pid > 0 && wait4(pid, &h.status, 0, &ru) == pid) {
        h.exited = WIFEXITED(h.status) && WEXITSTATUS(h.status) == 0;
        h.cpu_s = ru.ru_utime.tv_sec + ru.ru_stime.tv_sec + (ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1e6;
        h.maxrss_kb = ru.ru_maxrss;
    }
    std::istringstream in(got);
    std::string line;
    if (std::getline(in, line)) {
        h.has_dv = line == "has dv";
    }
    while (std::getline(in, line)) {
        h.refusals.push_back(line);
    }
    return h;
}

static std::string fresh(const std::string &name)
{
    std::string dir = SCRATCH + "/" + name;
    std::string cmd = "rm -rf '" + dir + "' && mkdir -p '" + dir + "'";
    if (std::system(cmd.c_str()) != 0) {
        expect(name + ": scratch folder", false);
    }
    return dir;
}

static std::string said(const Hostile &h)
{
    std::string s = h.exited ? "exited 0" : WIFSIGNALED(h.status) ? "killed by signal " + std::to_string(WTERMSIG(h.status))
                                                                   : "exit " + std::to_string(WEXITSTATUS(h.status));
    s += ", " + std::to_string(h.cpu_s) + " s cpu, " + std::to_string(h.maxrss_kb / 1024) + " MiB, " +
         std::to_string(h.refusals.size()) + " refusals";
    if (!h.refusals.empty()) {
        s += " (first: " + h.refusals[0].substr(0, 160) + ")";
    }
    return s;
}

static void hostile(const char *name, const std::function<void(const std::string &)> &make, const char *why,
                    size_t nrefusals = 1, bool dv_filed = false, double max_cpu_s = 10)
{
    std::string dir = fresh(std::string("H-") + name);
    make(dir);
    Hostile h = load_capped(dir);
    bool ok = h.exited && h.refusals.size() == nrefusals && h.has_dv == dv_filed && h.cpu_s < max_cpu_s &&
              h.maxrss_kb < 256 * 1024;
    for (const auto &r : h.refusals) {
        ok = ok && r.compare(0, 12, "PARSE_ERROR\t") == 0 && r.find(why) != std::string::npos;
    }
    char cap[32];
    std::snprintf(cap, sizeof cap, "%g", max_cpu_s);
    expect(std::string("H ") + name + ": " + std::to_string(nrefusals) + " PARSE_ERROR refusal(s) naming '" + why +
           "', under " + cap + " s of CPU and 256 MiB: " + said(h), ok);
}

static std::string aliases(int depth)
{
    std::string t = "schema: sim-profile/1\nkind: device\nid: dv\nsource: sdl_joystick\n"
                    "a0: &a0 [x, x, x, x, x, x, x, x, x, x]\n";
    for (int k = 1; k <= depth; k++) {
        t += "a" + std::to_string(k) + ": &a" + std::to_string(k) + " [";
        for (int j = 0; j < 10; j++) {
            t += (j ? ", *a" : "*a") + std::to_string(k - 1);
        }
        t += "]\n";
    }
    return t + "inputs: [ { id: bt, kind: button } ]\n";
}

static const char *I_HDR = "schema: sim-profile/1\n";

static std::string i_world(const std::string &name, std::vector<std::pair<std::string, std::string>> files)
{
    std::string dir = fresh("I-" + name);
    for (const auto &f : files) {
        spit(dir + "/" + f.first, f.second);
    }
    return dir;
}

static std::string i_dev(const std::string &range, const std::string &extra = "")
{
    return std::string(I_HDR) + "kind: device\nid: dv\nsource: sdl_joystick\ninputs:\n  - { id: ax, kind: axis, " +
           range + " }\n  - { id: bt, kind: button" + extra + " }\n";
}

static std::string i_bind(const std::string &entries)
{
    return std::string(I_HDR) + "kind: bindings\nid: bd\ndevice: dv\ntarget: xbox_duke\nbind:\n" + entries;
}

static const std::string I_PROF = std::string("schema: sim-profile/1\nkind: profile\nid: pf\ntarget: xbox_duke\n"
                                              "bindings: { dv: bd }\n");

static std::string i_ports(const std::string &extra = "")
{
    return std::string(I_HDR) + "kind: ports\nid: pp\nports:\n  1: { target: xbox_duke, profile: pf, devices: [ { "
           "device: dv" + extra + " } ] }\n";
}

static void i_refused(const std::string &name, const std::string &dir, const char *code, const char *where,
                      bool port_lost = true)
{
    SimProfileSet *s = sim_profile_set_load(DATA.c_str(), dir.c_str());
    bool hit = has_refusal(s, code, where);
    SimPort p[SIM_PROFILE_MAX_PORTS];
    int on = sim_profile_set_ports(s, "pp", p);
    bool lost = on < 0 || !p[0].profile;
    expect("I " + name + ": refused " + code + " at " + where + (port_lost ? ", port 1 applies no profile" : ""),
           hit && (!port_lost || lost));
    if (!hit || (port_lost && !lost)) {
        print_refusals(s);
    }
    sim_profile_set_free(s);
}

static void part_i()
{
    const std::string ax = "min: 0, rest: 0, max: 1000";
    const std::string trig = "  - { source: ax, target: left_trigger }\n";
    const std::string start = "  - { source: bt, target: start }\n";
    const std::string unit = ", port_path: \"pci-0000:00:14.0-usb-0:1:1.0\"";
    auto base = [&](const std::string &dev, const std::string &bind, const std::string &ports) {
        return std::vector<std::pair<std::string, std::string>>{
            { "device_dv.yaml", dev }, { "bindings_bd.yaml", bind }, { "profile_pf.yaml", I_PROF },
            { "ports_pp.yaml", ports } };
    };
    i_refused("nan-max", i_world("nan-max", base(i_dev("min: -1, rest: -1, max: .nan"), i_bind(trig), i_ports())),
              "DESCRIPTOR_INVALID", "device:dv.inputs[0]");
    {
        auto w = base(i_dev(ax), i_bind(trig), i_ports(unit));
        w.push_back({ "calibration_cc.yaml", std::string(I_HDR) + "kind: calibration\nid: cc\ndevice: dv\nport_path: "
                      "\"pci-0000:00:14.0-usb-0:1:1.0\"\ninputs: { ax: { min: 0, rest: 0, max: .nan } }\n" });
        i_refused("nan-calibration", i_world("nan-calibration", w), "DESCRIPTOR_INVALID", "calibration:cc.inputs.ax",
                  false);
    }
    i_refused("nan-target", i_world("nan-target", {
        { "target_tt.yaml", std::string(I_HDR) + "kind: target\nid: tt\ncontrols:\n"
                            "  - { id: c0, kind: axis, min: 5, rest: 5, max: .nan }\n" } }),
              "DESCRIPTOR_INVALID", "target:tt.controls[0]", false);
    i_refused("inf-max", i_world("inf-max", base(i_dev("min: 0, rest: 0, max: .inf"), i_bind(trig), i_ports())),
              "DESCRIPTOR_INVALID", "device:dv.inputs[0]");
    i_refused("tag-local", i_world("tag-local", base(i_dev("min: 0, rest: 0, max: !foo 1000"), i_bind(trig),
                                                     i_ports())),
              "PARSE_ERROR", "I-tag-local/device_dv.yaml");
    {
        auto w = base(i_dev(ax), i_bind(start), i_ports());
        w[2].second = std::string(I_HDR) + "kind: profile\nid: pf\ntarget: xbox_duke\ngame: { title_id: !!str 5 }\n"
                                           "bindings: { dv: bd }\n";
        i_refused("tag-str", i_world("tag-str", w), "GAME_TITLE_ID", "profile:pf");
    }
    i_refused("tag-float", i_world("tag-float", base(i_dev(ax, ", code: !!float 3"), i_bind(start), i_ports())),
              "CODE_TYPE", "device:dv.inputs[1]");
    {
        auto w = base(i_dev(ax), i_bind(start), i_ports());
        w.push_back({ "device_d2.yaml", std::string(I_HDR) + "kind: device\nid: 2026-11-15\nsource: sdl_joystick\n"
                                                             "inputs: [ { id: a, kind: button } ]\n" });
        std::string dir = i_world("timestamp-id", w);
        SimProfileSet *s = sim_profile_set_load(DATA.c_str(), dir.c_str());
        expect("I timestamp-id: `id: 2026-11-15` (a date to the app) is MISSING_ID, nothing filed under that text",
               has_refusal(s, "MISSING_ID", nullptr) && !sim_profile_set_has(s, "device", "2026-11-15"));
        sim_profile_set_free(s);
    }
    i_refused("timestamp-port-path", i_world("timestamp-port-path", base(i_dev(ax), i_bind(start),
                                                                         i_ports(", port_path: 2026-11-15"))),
              "PORT_PATH_SHAPE", "ports:pp.ports.1.devices[0].port_path");
    i_refused("bigint-port-path", i_world("bigint-port-path", base(i_dev(ax), i_bind(start),
                                                                   i_ports(", port_path: 99999999999999999999"))),
              "PORT_PATH_SHAPE", "ports:pp.ports.1.devices[0].port_path");
    for (const auto &b : std::vector<std::pair<const char *, std::string>>{
             { "not-utf8", i_dev(ax) + "name: \"\xff\xfe\"\n" },
             { "nul-byte", i_dev(ax) + std::string("name: \"a\0b\"\n", 11) },
             { "del-byte", i_dev(ax) + "name: \"a\x7f\"\n" } }) {
        i_refused(b.first, i_world(b.first, base(b.second, i_bind(start), i_ports())), "PARSE_ERROR",
                  (std::string("I-") + b.first + "/device_dv.yaml").c_str());
    }
    {
        std::string dir = i_world("bom", base("\xef\xbb\xbf" + i_dev(ax), i_bind(start), i_ports()));
        SimProfileSet *s = sim_profile_set_load(DATA.c_str(), dir.c_str());
        expect("I bom: a UTF-8 BOM reads (no refusal)", sim_profile_set_nrefused(s) == 0);
        sim_profile_set_free(s);
    }
    {
        std::string dir = i_world("dup-keys", base(i_dev(ax), std::string(I_HDR) + "kind: bindings\nid: bd\n"
                                                   "device: dv\ntarget: xbox_duke\nbind: { bt: start, bt: back }\n",
                                                   i_ports()));
        SimProfileSet *s = sim_profile_set_load(DATA.c_str(), dir.c_str());
        SimPort p[SIM_PROFILE_MAX_PORTS];
        bool ok = sim_profile_set_ports(s, "pp", p) == 1 && p[0].ndevices == 1 && p[0].devices[0].nbindings == 1 &&
                  !std::strcmp(p[0].devices[0].bindings[0].target->id, "back");
        expect("I dup-keys: `bind: { bt: start, bt: back }` is one binding, bt -> back", ok);
        if (!ok) {
            print_refusals(s);
        }
        sim_profile_set_free(s);
    }
    i_refused("int-id-by-text", i_world("int-id-by-text", base(
                  std::string(I_HDR) + "kind: device\nid: dv\nsource: sdl_joystick\ninputs:\n  - { id: 5, kind: button }\n",
                  i_bind("  - { source: \"5\", target: start }\n"), i_ports())),
              "UNKNOWN_SOURCE", "bindings:bd.bind[0]");
    const std::string gate = std::string(I_HDR) + "kind: device\nid: dv\nsource: sdl_joystick\ninputs:\n"
                                                  "  - { id: g, kind: detent, positions: ";
    i_refused("detent-text", i_world("detent-text", base(gate + "[\"1\", \"2\"] }\n",
                                                         i_bind("  - { source: g, target: start, position: 1 }\n"),
                                                         i_ports())),
              "DETENT_POSITION", "bindings:bd.bind[0]");
    {
        std::string dir = i_world("detent-float", base(gate + "[1, 2] }\n",
                                                       i_bind("  - { source: g, target: start, position: 1.0 }\n"),
                                                       i_ports()));
        SimProfileSet *s = sim_profile_set_load(DATA.c_str(), dir.c_str());
        SimPort p[SIM_PROFILE_MAX_PORTS];
        bool ok = sim_profile_set_nrefused(s) == 0 && sim_profile_set_ports(s, "pp", p) == 1 &&
                  p[0].devices[0].nbindings == 1 && !std::strcmp(p[0].devices[0].bindings[0].position, "1");
        expect("I detent-float: position 1.0 against positions [1, 2] is accepted, as the position \"1\"", ok);
        if (!ok) {
            print_refusals(s);
        }
        sim_profile_set_free(s);
    }
    {
        std::string dir = i_world("assign-int", {
            { "device_dv.yaml", i_dev(ax) }, { "bindings_bd.yaml", i_bind(start) },
            { "rig_r.yaml", std::string(I_HDR) + "kind: rig\nid: \"5\"\ngame: { title_id: 1 }\ntarget: xbox_duke\n"
                                                 "devices: [ { profile: dv, bindings: bd } ]\n" },
            { "ports_pp.yaml", std::string(I_HDR) + "kind: ports\nid: pp\nassign: { 1: 5 }\n" } });
        i_refused("assign-int", dir, "UNRESOLVED_REF", "ports:pp.assign.1", false);
    }
    i_refused("devices-null", i_world("devices-null", { { "ports_pp.yaml", std::string(I_HDR) + "kind: ports\nid: pp\n"
                                                         "ports: { 1: { target: xbox_duke, devices: null } }\n" } }),
              "PORT_SHAPE", "ports:pp.ports.1.devices");
    {
        auto w = base(i_dev(ax), i_bind(trig), i_ports(unit));
        w.push_back({ "calibration_cc.yaml", std::string(I_HDR) + "kind: calibration\nid: cc\ndevice: dv\nport_path: "
                      "\"pci-0000:00:14.0-usb-0:1:1.0\"\ninputs: { zz: { min: 0, rest: 0, max: 900 } }\n" });
        i_refused("calibration-unknown-input", i_world("calibration-unknown-input", w), "UNKNOWN_SOURCE",
                  "calibration:cc.inputs.zz", false);
    }
    long long i = 0;
    expect("I scalar: 2026-11-15 and 2026-1-5 1:02:03 are timestamps ('O'), 2026-11-15x a string",
           sim_profile_scalar_kind("2026-11-15", false, nullptr, nullptr) == 'O' &&
           sim_profile_scalar_kind("2026-1-5 1:02:03", false, nullptr, nullptr) == 'O' &&
           sim_profile_scalar_kind("2026-11-15x", false, nullptr, nullptr) == 'S');
    expect("I scalar: 99999999999999999999 is an integer (saturated to 64 bits), not a string",
           sim_profile_scalar_kind("99999999999999999999", false, &i, nullptr) == 'I' && i == LLONG_MAX &&
           sim_profile_scalar_kind("-0x1ffffffffffffffff", false, &i, nullptr) == 'I' && i == LLONG_MIN);
}

static void part_j()
{
    const std::string dev = i_dev("min: 0, rest: 0, max: 1000");
    const std::string start = "  - { source: bt, target: start }\n";
    SimPort p[SIM_PROFILE_MAX_PORTS];
    {
        std::string dir = i_world("first-ports-refused", {
            { "device_dv.yaml", dev }, { "bindings_bd.yaml", i_bind(start) }, { "profile_pf.yaml", I_PROF },
            { "ports_pp.yaml", i_ports() },
            { "ports_aa.yaml", std::string(I_HDR) + "kind: ports\nid: aa\nports: { 1: { target: 5 } }\n" } });
        SimProfileSet *s = sim_profile_set_load(DATA.c_str(), dir.c_str());
        int dflt = sim_profile_set_ports(s, nullptr, p), named = sim_profile_set_ports(s, "pp", p);
        expect("J first-ports-refused: the default (aa, refused) applies nothing (-1); pp by name still composes ("
               + std::to_string(dflt) + ", " + std::to_string(named) + ")", dflt == -1 && named == 1);
        sim_profile_set_free(s);
    }
    {
        std::string dir = i_world("first-ports-ok", {
            { "device_dv.yaml", dev }, { "bindings_bd.yaml", i_bind(start) }, { "profile_pf.yaml", I_PROF },
            { "ports_pp.yaml", i_ports() } });
        SimProfileSet *s = sim_profile_set_load(DATA.c_str(), dir.c_str());
        expect("J first-ports-ok: with one valid ports document the default is it (1 port, profile pf)",
               sim_profile_set_ports(s, nullptr, p) == 1 && p[0].profile && !std::strcmp(p[0].profile, "pf"));
        sim_profile_set_free(s);
    }
    {
        std::string dir = i_world("dup-first-invalid", {
            { "aaa_device.yaml", std::string(I_HDR) + "kind: device\nid: dv\nsource: bogus\n"
                                                      "inputs: [ { id: bt, kind: button } ]\n" },
            { "device_dv.yaml", dev }, { "bindings_bd.yaml", i_bind(start) }, { "profile_pf.yaml", I_PROF },
            { "ports_pp.yaml", i_ports() } });
        SimProfileSet *s = sim_profile_set_load(DATA.c_str(), dir.c_str());
        bool on = sim_profile_set_ports(s, "pp", p) == 1;
        expect("J dup-first-invalid: DUPLICATE_ID for the second dv, dv not filed, port 1 applies no profile",
               has_refusal(s, "DUPLICATE_ID", "device:dv") && has_refusal(s, "SOURCE_UNKNOWN", "device:dv") &&
               !sim_profile_set_has(s, "device", "dv") && on && !p[0].profile);
        sim_profile_set_free(s);
    }
    {
        std::string dir = i_world("int-instance", {
            { "device_dv.yaml", dev }, { "bindings_bd.yaml", i_bind(start) }, { "profile_pf.yaml", I_PROF },
            { "ports_pp.yaml", i_ports(", instance: 5") } });
        SimProfileSet *s = sim_profile_set_load(DATA.c_str(), dir.c_str());
        bool ok = sim_profile_set_ports(s, "pp", p) == 1 && p[0].profile && p[0].ndevices == 1 &&
                  p[0].devices[0].nbindings == 0;
        expect("J int-instance: `instance: 5` leaves the device unbound (the profile still applies)", ok);
        sim_profile_set_free(s);
    }
    {
        std::string dir = i_world("rig-twice", {
            { "device_dv.yaml", dev },
            { "bindings_b1.yaml", std::string(I_HDR) + "kind: bindings\nid: b1\ndevice: dv\ntarget: xbox_duke\n"
                                                       "bind: [ { source: bt, target: a } ]\n" },
            { "bindings_b2.yaml", std::string(I_HDR) + "kind: bindings\nid: b2\ndevice: dv\ntarget: xbox_duke\n"
                                                       "bind: [ { source: bt, target: b } ]\n" },
            { "rig_r.yaml", std::string(I_HDR) + "kind: rig\nid: rr\ngame: { title_id: 1 }\ntarget: xbox_duke\n"
                                                 "devices:\n  - { profile: dv, bindings: b1 }\n"
                                                 "  - { profile: dv, bindings: b2 }\n" },
            { "ports_pp.yaml", std::string(I_HDR) + "kind: ports\nid: pp\nassign: { 1: rr }\n" } });
        SimProfileSet *s = sim_profile_set_load(DATA.c_str(), dir.c_str());
        bool ok = sim_profile_set_ports(s, "pp", p) == 1 && p[0].ndevices == 2 && p[0].devices[0].nbindings == 1 &&
                  p[0].devices[1].nbindings == 1 && !std::strcmp(p[0].devices[0].bindings[0].target->id, "b") &&
                  !std::strcmp(p[0].devices[1].bindings[0].target->id, "b");
        expect("J rig-twice: both copies of dv get the last bindings (b2: bt -> b), as the app composes them", ok);
        sim_profile_set_free(s);
    }
}

static void part_h()
{
    const std::string head = "schema: sim-profile/1\nkind: device\nid: dv\nsource: sdl_joystick\n"
                             "inputs: [ { id: bt, kind: button } ]\n";
    hostile("alias-depth-6", [&](const std::string &d) { spit(d + "/dv.yaml", aliases(6)); }, "nodes");
    hostile("alias-depth-7", [&](const std::string &d) { spit(d + "/dv.yaml", aliases(7)); }, "nodes");
    hostile("nested-seq-20000", [&](const std::string &d) {
        spit(d + "/dv.yaml", head + "x: " + std::string(20000, '[') + std::string(20000, ']') + "\n");
    }, "deeper");
    hostile("nested-map-20000", [&](const std::string &d) {
        std::string t = head + "x: ";
        for (int i = 0; i < 20000; i++) {
            t += "{a: ";
        }
        spit(d + "/dv.yaml", t + "1" + std::string(20000, '}') + "\n");
    }, "deeper");
    for (int levels : { 100000, 200000 }) {
        std::string name = "nested-map-" + std::to_string(levels) + "-fast";
        hostile(name.c_str(), [&](const std::string &d) {
            std::string t = head + "x: ";
            for (int i = 0; i < levels; i++) {
                t += "{a: ";
            }
            spit(d + "/dv.yaml", t + "1" + std::string(levels, '}') + "\n");
        }, "deeper", 1, false, 0.5);
    }
    {
        std::string dir = fresh("H-brackets-quoted");
        spit(dir + "/dv.yaml", head + "name: \"" + std::string(300, '[') + "\"\nlabel: '" + std::string(300, '{') +
                               "''s'\nnote: it's {not} [nested]\n# " + std::string(300, '[') + "\nx: [a, {b: \"]]]\"}]  # {{{\n");
        Hostile h = load_capped(dir);
        expect("H brackets-quoted (300 brackets in a double-quoted, a single-quoted scalar and a comment): filed, no "
               "refusal: " + said(h), h.exited && h.has_dv && h.refusals.empty());
    }
    hostile("nested-seq-500000", [&](const std::string &d) {
        spit(d + "/dv.yaml", head + "x: " + std::string(500000, '[') + std::string(500000, ']') + "\n");
    }, "deeper");
    hostile("too-large", [&](const std::string &d) {
        spit(d + "/dv.yaml", head + "# " + std::string(SIM_PROFILE_MAX_FILE_BYTES, 'x') + "\n");
    }, "larger than");
    hostile("fifo", [&](const std::string &d) {
        expect("H fifo: made", mkfifo((d + "/dv.yaml").c_str(), 0600) == 0);
    }, "not a regular file");
    hostile("symlink-loop-1", [&](const std::string &d) {
        spit(d + "/device_dv.yaml", head);
        expect("H symlink-loop-1: made", symlink(".", (d + "/loop").c_str()) == 0);
    }, "symbolic link", 1, true);
    hostile("symlink-loop-2", [&](const std::string &d) {
        spit(d + "/device_dv.yaml", head);
        expect("H symlink-loop-2: made", symlink(".", (d + "/loopa").c_str()) == 0 &&
                                         symlink(".", (d + "/loopb").c_str()) == 0);
    }, "symbolic link", 2, true);
    {
        std::string dir = fresh("H-alias-depth-3");
        spit(dir + "/dv.yaml", aliases(3));
        Hostile h = load_capped(dir);
        expect("H alias-depth-3 (10,000 leaves, inside the bound): filed, no refusal: " + said(h),
               h.exited && h.has_dv && h.refusals.empty());
        dir = fresh("H-nested-90");
        spit(dir + "/dv.yaml", head + "x: " + std::string(90, '[') + std::string(90, ']') + "\n");
        h = load_capped(dir);
        expect("H nested-90 (inside the depth bound): filed, no refusal: " + said(h),
               h.exited && h.has_dv && h.refusals.empty());
        dir = fresh("H-file-link");
        spit(dir + "/real.txt", head);
        expect("H file-link: made", symlink("real.txt", (dir + "/dv.yaml").c_str()) == 0);
        h = load_capped(dir);
        expect("H file-link (a link to a file is read as the file): filed, no refusal: " + said(h),
               h.exited && h.has_dv && h.refusals.empty());
    }
}

int main(int argc, char **argv)
{
    if (argc == 4) {
        DATA = argv[1], GOOD = argv[2], SCRATCH = argv[3];
        BAD = GOOD + "/../bad";
    } else if (argc == 1) {
        char tmpl[] = "/tmp/simprofile-test-XXXXXX";
        const char *t = mkdtemp(tmpl);
        DATA = SIMPROFILE_DATA, GOOD = "good", SCRATCH = t ? t : "/tmp";
        BAD = "bad";
    } else {
        std::fprintf(stderr, "usage: %s [DATA_DIR GOOD_DIR SCRATCH_DIR]\n", argv[0]);
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    SimProfileSet *a = sim_profile_set_load(DATA.c_str(), nullptr);
    print_refusals(a);
    expect("A: the shipped documents load with no refusal", sim_profile_set_nrefused(a) == 0);
    expect("A: both Xbox targets are filed", sim_profile_set_has(a, "target", "steel_battalion") &&
           sim_profile_set_has(a, "target", "xbox_duke"));
    const SimInput *aim = sim_profile_target_control(a, "steel_battalion", "aiming_x");
    expect("A: steel_battalion aiming_x is a bipolar axis 0..65535 rest 32768",
           aim && aim->kind == SIM_IN_AXIS && aim->bipolar && aim->range.min == 0 && aim->range.rest == 32768 &&
           aim->range.max == 65535);
    const SimInput *pedal = sim_profile_target_control(a, "steel_battalion", "left_pedal");
    expect("A: steel_battalion left_pedal is unipolar", pedal && !pedal->bipolar);
    const SimInput *tuner = sim_profile_target_control(a, "steel_battalion", "tuner_dial");
    expect("A: tuner_dial is a detent with 16 positions", tuner && tuner->kind == SIM_IN_DETENT &&
           tuner->npositions == 16 && std::strcmp(tuner->positions[15], "15") == 0);
    const SimInput *duke_a = sim_profile_target_control(a, "xbox_duke", "a");
    expect("A: xbox_duke a is an analog button 0..255", duke_a && duke_a->kind == SIM_IN_BUTTON && duke_a->analog &&
           duke_a->analog_range.max == 255);
    sim_profile_set_free(a);

    SimProfileSet *b = sim_profile_set_load(DATA.c_str(), GOOD.c_str());
    print_refusals(b);
    expect("B: the good set loads with no refusal", sim_profile_set_nrefused(b) == 0);
    std::vector<std::string> good = yaml_files(GOOD);
    expect("B: 30 good fixtures vendored", good.size() == 30);
    int filed = 0;
    for (const auto &n : good) {
        std::string t = slurp(GOOD + "/" + n), kind = top(t, "kind"), id = top(t, "id");
        bool ok = sim_profile_set_has(b, kind.c_str(), id.c_str());
        filed += ok;
        if (!ok) {
            expect("B: " + n + " filed as " + kind + ":" + id, false);
        }
    }
    expect("B: every good fixture is filed under its kind and id", filed == 30);

    SimPort ports[SIM_PROFILE_MAX_PORTS];
    int on = sim_profile_set_ports(b, nullptr, ports);
    expect("C: three ports on (1, 2, 3), port 4 off", on == 3 && ports[3].target == nullptr);
    expect("C: an unknown ports id is -1", sim_profile_set_ports(b, "nope", ports) == -1 &&
           sim_profile_set_ports(b, "living_room", ports) == 3);
    const SimPort &p1 = ports[0], &p2 = ports[1], &p3 = ports[2];
    expect("C: port 1 is xbox_duke with profile pgr2_racing, 4 devices",
           p1.target && !std::strcmp(p1.target, "xbox_duke") && p1.profile && !std::strcmp(p1.profile, "pgr2_racing") &&
           p1.ndevices == 4 && p1.has_title_id && p1.title_id == 1);
    const SimPortDevice *wheel = dev_of(p1, "wheel");
    expect("C: the wheel: sdl_joystick, its port path, 4 bindings",
           wheel && wheel->source == SIM_SRC_SDL_JOYSTICK && wheel->port_path &&
           !std::strcmp(wheel->port_path, "pci-0000:00:14.0-usb-0:1:1.0") && wheel->nbindings == 4);
    bool hat = false;
    for (int i = 0; wheel && i < wheel->nbindings; i++) {
        const SimBinding &x = wheel->bindings[i];
        hat |= x.source->kind == SIM_IN_HAT && !std::strcmp(x.target->id, "dpad_up") && x.direction &&
               !std::strcmp(x.direction, "up");
    }
    expect("C: the wheel's pov -> dpad_up, direction up", hat);
    const SimPortDevice *ped = dev_of(p1, "racing_pedals");
    bool window = ped && ped->nbindings == 2 && ped->bindings[0].has_window && ped->bindings[0].window_lo == 0.1 &&
                  ped->bindings[0].window_hi == 0.9 && ped->bindings[0].has_curve && ped->bindings[0].curve == 1.0;
    expect("C: racing_pedals accel -> right_trigger with window [0.1, 0.9], curve 1.0", window);
    const SimPortDevice *th = dev_of(p1, "th8a");
    expect("C: the shifter's gate -> black at position r", th && th->nbindings == 1 && th->bindings[0].position &&
           !std::strcmp(th->bindings[0].position, "r"));
    const SimPortDevice *key = dev_of(p1, "sinden_pedal");
    expect("C: the Sinden pedal is a keyboard input with scancode c", key && key->source == SIM_SRC_KEYBOARD &&
           key->ninputs == 1 && key->inputs[0].scancode && !std::strcmp(key->inputs[0].scancode, "c"));
    expect("C: port 2 is steel_battalion, profile sbc_real, title 0x43430002",
           p2.target && !std::strcmp(p2.target, "steel_battalion") && p2.profile && !std::strcmp(p2.profile, "sbc_real") &&
           p2.has_title_id && p2.title_id == 0x43430002u);
    const SimPortDevice *sbc = dev_of(p2, "sbc_wire");
    expect("C: sbc_wire is hid_usb 0a7b:d000 with 3 bindings, aiming bipolar",
           sbc && sbc->source == SIM_SRC_HID_USB && sbc->has_match && sbc->vid == 0x0a7b && sbc->pid == 0xd000 &&
           sbc->nbindings == 3 && sbc->bindings[0].source->bipolar && sbc->bindings[0].target->bipolar);
    expect("C: port 3 is xbox_duke with no profile and no devices",
           p3.target && !std::strcmp(p3.target, "xbox_duke") && !p3.profile && !p3.profile_lost && p3.ndevices == 0);
    sim_profile_set_free(b);

    std::vector<Mut> muts = {
        { "polarity", "bindings_wheel.yaml", "{ source: steering, target: left_stick_x }",
          "{ source: steering, target: left_trigger }", "POLARITY_POLICY_REQUIRED", "bindings:pgr2_wheel",
          [](const SimProfileSet *s) {
              expect("D polarity: the bindings are not filed", !sim_profile_set_has(s, "bindings", "pgr2_wheel"));
              expect("D polarity: the profile that names them is refused (cascade)",
                     has_refusal(s, "UNRESOLVED_REF", "profile:pgr2_racing") &&
                     !sim_profile_set_has(s, "profile", "pgr2_racing"));
              SimPort p[4];
              sim_profile_set_ports(s, nullptr, p);
              bool unbound = p[0].ndevices == 4;
              for (int i = 0; i < p[0].ndevices; i++) {
                  unbound &= p[0].devices[i].nbindings == 0;
              }
              expect("D polarity: port 1 keeps xbox_duke, loses the profile, every device unbound",
                     p[0].target && !p[0].profile && p[0].profile_lost &&
                     !std::strcmp(p[0].profile_lost, "pgr2_racing") && unbound);
              expect("D polarity: port 2 is untouched", p[1].profile && p[1].devices[0].nbindings == 3);
          } },
        { "polarity-declared", "bindings_wheel.yaml", "{ source: steering, target: left_stick_x }",
          "{ source: steering, target: left_trigger, policy: half_positive }", nullptr, nullptr,
          [](const SimProfileSet *s) {
              expect("D polarity-declared: a declared policy is accepted (no refusal)", sim_profile_set_nrefused(s) == 0);
          } },
        { "relative", "bindings_sbc_mouse_aim.yaml",
          "{ source: rel_x, target: aiming_x, response: { mode: integrate, counts_per_travel: 2000, recentre: middle } }",
          "{ source: rel_x, target: aiming_x }", "RELATIVE_NEEDS_INTEGRATE", "bindings:sb_mouse_aim", nullptr },
        { "counts", "bindings_sbc_mouse_aim.yaml", "{ source: rel_y, target: aiming_y, response: { mode: integrate, counts_per_travel: 2000",
          "{ source: rel_y, target: aiming_y, response: { mode: integrate, counts_per_travel: 0", "INTEGRATE_COUNTS",
          "bindings:sb_mouse_aim", nullptr },
        { "recentre", "bindings_sbc_mouse_aim.yaml", "counts_per_travel: 2000, recentre: middle } }\n  - { source: rel_y",
          "counts_per_travel: 2000, recentre: rel_y } }\n  - { source: rel_y", "RECENTRE_UNKNOWN", "bindings:sb_mouse_aim",
          nullptr },
        { "hat", "bindings_wheel.yaml", "direction: up", "direction: north", "HAT_DIRECTION", "bindings:pgr2_wheel", nullptr },
        { "detent", "bindings_shifter.yaml", "position: r", "position: q", "DETENT_POSITION", "bindings:pgr2_shifter", nullptr },
        { "button-axis", "bindings_sinden.yaml", "target: x }", "target: left_stick_x }", "BUTTON_TO_AXIS_VALUE",
          "bindings:pedal_as_x", nullptr },
        { "axis-button", "bindings_racing.yaml", "{ source: brake, target: left_trigger }",
          "{ source: brake, target: dpad_up }", "AXIS_TO_BUTTON_THRESHOLD", "bindings:pgr2_pedals", nullptr },
        { "schema", "device_wheel.yaml", "schema: sim-profile/1", "schema: sim-profile/2", "SCHEMA_UNKNOWN", "device:wheel",
          [](const SimProfileSet *s) {
              expect("D schema: the bindings for the refused device are refused (cascade)",
                     has_refusal(s, "UNRESOLVED_REF", "bindings:pgr2_wheel"));
              SimPort p[4];
              sim_profile_set_ports(s, nullptr, p);
              expect("D schema: port 1 drops the wheel and the profile, keeps its other 3 devices unbound",
                     p[0].target && !p[0].profile && p[0].ndevices == 3 && !dev_of(p[0], "wheel") &&
                     dev_of(p[0], "th8a") && dev_of(p[0], "th8a")->nbindings == 0);
          } },
        { "rest", "device_sbc_wire.yaml", "rest: 0x8000", "rest: 0x18000", "DESCRIPTOR_INVALID", "device:sbc_wire", nullptr },
        { "incomplete", "device_wheel.yaml", "min: -32768, rest: 0, max: 32767", "min: -32768, max: 32767",
          "DESCRIPTOR_INCOMPLETE", "device:wheel", nullptr },
        { "scancode", "device_sinden_pedal.yaml", "scancode: c", "code: 3", "SCANCODE_REQUIRED", "device:sinden_pedal", nullptr },
        { "mouse-code", "device_desk_mouse.yaml", "{ id: left, kind: button, code: 272 }", "{ id: left, kind: button }",
          "CODE_REQUIRED", "device:desk_mouse", nullptr },
        { "kind", "device_shifter.yaml", "kind: device", "kind: gadget", "KIND_UNKNOWN", nullptr, nullptr },
        { "id", "device_shifter.yaml", "id: th8a", "name: th8a", "MISSING_ID", nullptr, nullptr },
        { "match", "device_sbc_wire.yaml", "pid: 0xd000", "pid: 0x1d000", "MATCH_SHAPE", "device:sbc_wire", nullptr },
        { "port-range", "ports.yaml", "  3: { target: xbox_duke }", "  5: { target: xbox_duke }", "PORT_RANGE",
          "ports:living_room",
          [](const SimProfileSet *s) {
              SimPort p[4];
              expect("D port-range: the ports document is refused whole (-1)", sim_profile_set_ports(s, nullptr, p) == -1);
          } },
        { "port-off", "ports.yaml", "  3: { target: xbox_duke }", "  3: { profile: sbc_real }", "PORT_OFF",
          "ports:living_room", nullptr },
        { "port-bindings", "ports.yaml", "- { device: sbc_wire }", "- { device: sbc_wire, bindings: sbc_identity }",
          "PORT_DEVICE_BINDINGS", "ports:living_room", nullptr },
        { "port-profile-target", "ports.yaml", "profile: sbc_real", "profile: pgr2_racing", "PORT_PROFILE_TARGET",
          "ports:living_room.ports.2",
          [](const SimProfileSet *s) {
              SimPort p[4];
              int on = sim_profile_set_ports(s, nullptr, p);
              expect("D port-profile-target: port 2 keeps steel_battalion and its device, no profile, unbound",
                     on == 3 && p[1].target && !p[1].profile && p[1].profile_lost && p[1].ndevices == 1 &&
                     p[1].devices[0].nbindings == 0);
              expect("D port-profile-target: port 1 is untouched", p[0].profile && p[0].ndevices == 4);
          } },
        { "profile-target", "profile_sbc_real.yaml", "sbc_wire: sbc_identity", "sbc_wire: pgr2_wheel",
          "BINDINGS_TARGET_MISMATCH", "profile:sbc_real", nullptr },
        { "title-quoted", "profile_sbc_real.yaml", "title_id: 0x43430002", "title_id: \"0x43430002\"", "GAME_TITLE_ID",
          "profile:sbc_real", nullptr },
        { "source", "bindings_sbc_wire.yaml", "source: main_weapon", "source: fire", "UNKNOWN_SOURCE",
          "bindings:sbc_identity", nullptr },
        { "target-control", "bindings_sbc_wire.yaml", "target: main_weapon", "target: fire", "UNKNOWN_TARGET_CONTROL",
          "bindings:sbc_identity", nullptr },
        { "window", "bindings_racing.yaml", "window: [0.1, 0.9]", "window: [0.9, 0.1]", "RESPONSE_WINDOW",
          "bindings:pgr2_pedals", nullptr },
        { "policy", "bindings_wheel.yaml", "{ source: paddle_up, target: a }", "{ source: paddle_up, target: a, policy: sideways }",
          "POLICY_UNKNOWN", "bindings:pgr2_wheel", nullptr },
        { "split", "bindings_wheel.yaml", "{ source: steering, target: left_stick_x }",
          "{ source: steering, targets: [left_trigger], policy: split }", "SPLIT_SHAPE", "bindings:pgr2_wheel", nullptr },
        { "calibration-path", "calibration_pedals.yaml", "port_path: pci-0000:00:14.0-usb-0:3:1.0", "port_path: \"\"",
          "PORT_PATH_REQUIRED", "calibration:pedals_unit_a", nullptr },
        { "calibration-device", "calibration_pedals.yaml", "device: racing_pedals", "device: nope", "UNRESOLVED_REF",
          "calibration:pedals_unit_a", nullptr },
        { "duplicate", "zz_duplicate.yaml", nullptr,
          "schema: sim-profile/1\nkind: device\nid: wheel\nsource: sdl_joystick\ninputs:\n  - { id: x, kind: button }\n",
          "DUPLICATE_ID", "device:wheel",
          [](const SimProfileSet *s) {
              SimPort p[4];
              sim_profile_set_ports(s, nullptr, p);
              expect("D duplicate: the first wheel stays (4 bindings on port 1)",
                     dev_of(p[0], "wheel") && dev_of(p[0], "wheel")->nbindings == 4);
          } },
        { "calibration-joined", "ports.yaml", "- { device: racing_pedals, position: floor }",
          "- { device: racing_pedals, position: floor, port_path: \"pci-0000:00:14.0-usb-0:3:1.0\" }", nullptr, nullptr,
          [](const SimProfileSet *s) {
              SimPort p[4];
              sim_profile_set_ports(s, nullptr, p);
              const SimPortDevice *d = dev_of(p[0], "racing_pedals");
              expect("D calibration-joined: the unit at that port path gets its calibration (accel -32768/-32700/32100)",
                     sim_profile_set_nrefused(s) == 0 && d && d->ncalibrated == 1 &&
                     !std::strcmp(d->calibrated[0].input->id, "accel") && d->calibrated[0].range.rest == -32700 &&
                     d->calibrated[0].range.max == 32100);
          } },
        { "invert-type", "device_wheel.yaml", "{ id: steering, kind: axis, min: -32768, rest: 0, max: 32767 }", "{ id: steering, kind: axis, min: -32768, rest: 0, max: 32767, invert: 1 }", "INVERT_TYPE", "device:wheel", nullptr },
        { "deadzone", "device_wheel.yaml", "{ id: steering, kind: axis, min: -32768, rest: 0, max: 32767 }", "{ id: steering, kind: axis, min: -32768, rest: 0, max: 32767, deadzone: 1.5 }", "DEADZONE_RANGE", "device:wheel", nullptr },
        { "axis-code", "device_wheel.yaml", "{ id: steering, kind: axis, min: -32768, rest: 0, max: 32767 }", "{ id: steering, kind: axis, min: -32768, rest: 0, max: 32767, code: -1 }", "CODE_TYPE", "device:wheel", nullptr },
        { "hat-code", "device_wheel.yaml", "{ id: pov, kind: hat }", "{ id: pov, kind: hat, code: x }", "CODE_TYPE", "device:wheel", nullptr },
        { "button-range", "device_wheel.yaml", "{ id: paddle_up, kind: button }", "{ id: paddle_up, kind: button, min: 0, rest: 0, max: 1 }", "DESCRIPTOR_ON_BUTTON", "device:wheel", nullptr },
        { "analog-incomplete", "device_wheel.yaml", "{ id: paddle_up, kind: button }", "{ id: paddle_up, kind: button, analog: { min: 0, max: 1 } }", "DESCRIPTOR_INCOMPLETE", "device:wheel", nullptr },
        { "analog-invalid", "device_wheel.yaml", "{ id: paddle_up, kind: button }", "{ id: paddle_up, kind: button, analog: { min: 1, rest: 1, max: 1 } }", "DESCRIPTOR_INVALID", "device:wheel", nullptr },
        { "input-duplicate", "device_wheel.yaml", "{ id: paddle_down, kind: button }", "{ id: paddle_up, kind: button }", "DUPLICATE_ID", "device:wheel", nullptr },
        { "input-noid", "device_wheel.yaml", "{ id: paddle_down, kind: button }", "{ kind: button }", "MISSING_ID", "device:wheel", nullptr },
        { "source-unknown", "device_wheel.yaml", "source: sdl_joystick", "source: usb_thing", "SOURCE_UNKNOWN", "device:wheel", nullptr },
        { "inputs-required", "device_wheel.yaml", "inputs:", "inputs_old:", "INPUTS_REQUIRED", "device:wheel", nullptr },
        { "relative-range", "device_desk_mouse.yaml", "{ id: rel_x, kind: relative, code: 0 }", "{ id: rel_x, kind: relative, code: 0, min: 0, rest: 0, max: 9 }", "DESCRIPTOR_ON_RELATIVE", "device:desk_mouse", nullptr },
        { "mouse-kind", "device_desk_mouse.yaml", "{ id: left, kind: button, code: 272 }", "{ id: left, kind: axis, code: 272, min: 0, rest: 0, max: 1 }", "MOUSE_INPUT_KIND", "device:desk_mouse", nullptr },
        { "key-kind", "device_sinden_pedal.yaml", "{ id: pedal, kind: button, scancode: c }", "{ id: pedal, kind: hat, scancode: c }", "KEYBOARD_NOT_BUTTON", "device:sinden_pedal", nullptr },
        { "key-range", "device_sinden_pedal.yaml", "{ id: pedal, kind: button, scancode: c }", "{ id: pedal, kind: button, scancode: c, analog: { min: 0, rest: 0, max: 1 } }", "DESCRIPTOR_ON_KEY", "device:sinden_pedal", nullptr },
        { "detent-positions", "device_shifter.yaml", "positions: [n, \"1\", \"2\", \"3\", \"4\", \"5\", \"6\", r]", "positions: []", "DETENT_POSITIONS", "device:th8a", nullptr },
        { "catalogue", "device_vr_stick.yaml", "catalogue: Stick", "catalog: Stick", "CATALOGUE_REQUIRED", "device:vr_stick", nullptr },
        { "inputs-on-virtual", "device_vr_stick.yaml", "catalogue: Stick", "catalogue: Stick\ninputs:\n  - { id: a, kind: button }", "INPUTS_ON_VIRTUAL", "device:vr_stick", nullptr },
        { "match-key", "device_sbc_wire.yaml", "match: { vid: 0x0a7b, pid: 0xd000 }", "match: { vid: 0x0a7b, pid: 0xd000, extra: 1 }", "MATCH_SHAPE", "device:sbc_wire", nullptr },
        { "match-also", "device_sbc_wire.yaml", "match: { vid: 0x0a7b, pid: 0xd000 }", "match: { vid: 0x0a7b, pid: 0xd000 }\nmatch_also: [ { vid: 0x0a7b } ]", "MATCH_SHAPE", "device:sbc_wire", nullptr },
        { "bind-required", "bindings_sinden.yaml", "bind:", "bound:", "BIND_REQUIRED", "bindings:pedal_as_x", nullptr },
        { "bind-source", "bindings_sinden.yaml", "{ source: pedal, target: x }", "{ target: x }", "BIND_SOURCE", "bindings:pedal_as_x", nullptr },
        { "bind-target", "bindings_sinden.yaml", "{ source: pedal, target: x }", "{ source: pedal }", "BIND_TARGET", "bindings:pedal_as_x", nullptr },
        { "bind-both", "bindings_sinden.yaml", "{ source: pedal, target: x }", "{ source: pedal, target: x, targets: [x, y] }", "BIND_TARGET", "bindings:pedal_as_x", nullptr },
        { "bind-ref", "bindings_sinden.yaml", "device: sinden_pedal", "device: [sinden_pedal]", "MISSING_REF", "bindings:pedal_as_x", nullptr },
        { "integrate-field", "bindings_sbc_mouse_aim.yaml", "mode: integrate, counts_per_travel: 2000, recentre: middle } }\n  - { source: rel_y", "mode: integrate, counts_per_travel: 2000, recentre: middle, curve: 2 } }\n  - { source: rel_y", "INTEGRATE_FIELD", "bindings:sb_mouse_aim", nullptr },
        { "mode-unknown", "bindings_sbc_mouse_aim.yaml", "mode: integrate, counts_per_travel: 2000, recentre: middle } }\n  - { source: rel_y", "mode: glide, counts_per_travel: 2000, recentre: middle } }\n  - { source: rel_y", "RESPONSE_MODE_UNKNOWN", "bindings:sb_mouse_aim", nullptr },
        { "policy-relative", "bindings_sbc_mouse_aim.yaml", "{ source: rel_x, target: aiming_x,", "{ source: rel_x, target: aiming_x, policy: absolute,", "POLICY_ON_RELATIVE", "bindings:sb_mouse_aim", nullptr },
        { "resp-invert", "bindings_racing.yaml", "curve: 1.0 }", "curve: 1.0, invert: 2 }", "INVERT_TYPE", "bindings:pgr2_pedals", nullptr },
        { "integrate-absolute", "bindings_wheel.yaml", "{ source: steering, target: left_stick_x }", "{ source: steering, target: left_stick_x, response: { mode: integrate, counts_per_travel: 5 } }", "INTEGRATE_ON_ABSOLUTE", "bindings:pgr2_wheel", nullptr },
        { "response-button", "bindings_wheel.yaml", "{ source: paddle_up, target: a }", "{ source: paddle_up, target: a, response: { curve: 2 } }", "RESPONSE_ON_NON_AXIS", "bindings:pgr2_wheel", nullptr },
        { "kind-pair", "bindings_wheel.yaml", "{ source: pov, target: dpad_up, direction: up }", "{ source: pov, target: left_stick_x }", "UNSUPPORTED_KIND_PAIR", "bindings:pgr2_wheel", nullptr },
        { "rig-title", "rig_sbc_real.yaml", "game: { title_id: 0x43430002, name: \"Steel Battalion\" }", "", "GAME_TITLE_ID", "rig:sb_real", nullptr },
        { "rig-devices", "rig_sbc_real.yaml", "  - { profile: sbc_wire, bindings: sbc_identity }", "", "DEVICES_REQUIRED", "rig:sb_real", nullptr },
        { "rig-device", "rig_sbc_real.yaml", "{ profile: sbc_wire, bindings: sbc_identity }", "{ profile: sbc_wire }", "RIG_DEVICE", "rig:sb_real", nullptr },
        { "rig-mismatch", "rig_sbc_real.yaml", "{ profile: sbc_wire, bindings: sbc_identity }", "{ profile: sbc_wire, bindings: sb_mouse_aim }", "BINDINGS_DEVICE_MISMATCH", "rig:sb_real", nullptr },
        { "rig-target", "rig_sbc_real.yaml", "target: steel_battalion", "target: [steel_battalion]", "MISSING_REF", "rig:sb_real", nullptr },
        { "rig-unresolved", "rig_sbc_real.yaml", "{ profile: sbc_wire, bindings: sbc_identity }", "{ profile: sbc_wire, bindings: nope }", "UNRESOLVED_REF", "rig:sb_real", nullptr },
        { "ports-mixed", "ports.yaml", "id: living_room", "id: living_room\nassign: { 1: sb_real }", "PORTS_FORM_MIXED", "ports:living_room", nullptr },
        { "port-shape", "ports.yaml", "  3: { target: xbox_duke }", "  3: [ xbox_duke ]", "PORT_SHAPE", "ports:living_room", nullptr },
        { "port-profile-type", "ports.yaml", "profile: sbc_real", "profile: 7", "PORT_SHAPE", "ports:living_room", nullptr },
        { "port-device", "ports.yaml", "- { device: sbc_wire }", "- { devise: sbc_wire }", "PORT_DEVICE", "ports:living_room", nullptr },
        { "port-path-type", "ports.yaml", "port_path: \"pci-0000:00:14.0-usb-0:1:1.0\"", "port_path: 5", "PORT_PATH_SHAPE", "ports:living_room", nullptr },
        { "port-target-unknown", "ports.yaml", "  3: { target: xbox_duke }", "  3: { target: atari_paddle }", "UNRESOLVED_REF", "ports:living_room.ports.3", nullptr },
        { "profile-map", "profile_sbc_real.yaml", "bindings: { sbc_wire: sbc_identity }", "bindings: [ sbc_identity ]", "BINDINGS_MAP_SHAPE", "profile:sbc_real", nullptr },
        { "profile-target-ref", "profile_sbc_real.yaml", "target: steel_battalion", "target: [steel_battalion]", "MISSING_REF", "profile:sbc_real", nullptr },
        { "profile-target-unknown", "profile_sbc_real.yaml", "target: steel_battalion", "target: atari_paddle", "UNRESOLVED_REF", "profile:sbc_real", nullptr },
        { "calibration-range", "calibration_pedals.yaml", "accel: { min: -32768, rest: -32700, max: 32100 }", "accel: { min: -32768, max: 32100 }", "DESCRIPTOR_INCOMPLETE", "calibration:pedals_unit_a", nullptr },
        { "calibration-invalid", "calibration_pedals.yaml", "accel: { min: -32768, rest: -32700, max: 32100 }", "accel: { min: 5, rest: 9, max: 5 }", "DESCRIPTOR_INVALID", "calibration:pedals_unit_a", nullptr },
        { "calibration-ref", "calibration_pedals.yaml", "device: racing_pedals", "device: [racing_pedals]", "MISSING_REF", "calibration:pedals_unit_a", nullptr },
        { "rtr-ok", "zz_target.yaml", nullptr, "schema: sim-profile/1\nkind: target\nid: rtr_t\ncore: xbox\ncontrols:\n  - { id: lever, kind: axis, min: 0, rest: 50, max: 100, returns_to_rest: false }\n  - { id: stick, kind: axis, min: 0, rest: 50, max: 100, returns_to_rest: true }\n  - { id: plain, kind: axis, min: 0, rest: 50, max: 100 }\n", nullptr, nullptr,
          [](const SimProfileSet *s) {
              const SimInput *a = sim_profile_target_control(s, "rtr_t", "lever"), *b = sim_profile_target_control(s, "rtr_t", "stick"),
                             *c = sim_profile_target_control(s, "rtr_t", "plain");
              expect("D rtr-ok: accepted; false stays put, true and absent spring back", sim_profile_set_nrefused(s) == 0 && a && b && c &&
                     a->stays_put && !b->stays_put && !c->stays_put);
          } },
        { "rtr-on-button", "zz_target.yaml", nullptr, "schema: sim-profile/1\nkind: target\nid: rtr_t\ncore: xbox\ncontrols:\n  - { id: fire, kind: button, returns_to_rest: false }\n", "RETURNS_TO_REST", "target:rtr_t", nullptr },
        { "rtr-not-bool", "zz_target.yaml", nullptr, "schema: sim-profile/1\nkind: target\nid: rtr_t\ncore: xbox\ncontrols:\n  - { id: lever, kind: axis, min: 0, rest: 50, max: 100, returns_to_rest: 0 }\n", "RETURNS_TO_REST", "target:rtr_t", nullptr },
        { "rtr-text", "zz_target.yaml", nullptr, "schema: sim-profile/1\nkind: target\nid: rtr_t\ncore: xbox\ncontrols:\n  - { id: lever, kind: axis, min: 0, rest: 50, max: 100, returns_to_rest: never }\n", "RETURNS_TO_REST", "target:rtr_t", nullptr },
        { "not-a-mapping", "zz_list.yaml", nullptr, "- just\n- a list\n", "NOT_A_MAPPING", "zz_list.yaml", nullptr },
        { "two-documents", "zz_two.yaml", nullptr, "schema: sim-profile/1\nkind: template\nid: a\n---\nschema: sim-profile/1\nkind: template\nid: b\n", "PARSE_ERROR", "zz_two.yaml", nullptr },
        { "assign-required", "zz_old_ports.yaml", nullptr, "schema: sim-profile/1\nkind: ports\nid: aaa_old\nassign: {}\n", "ASSIGN_REQUIRED", "ports:aaa_old", nullptr },
        { "assign-range", "zz_old_ports.yaml", nullptr, "schema: sim-profile/1\nkind: ports\nid: aaa_old\nassign: { 7: sb_real }\n", "PORT_RANGE", "ports:aaa_old", nullptr },
        { "assign-unresolved", "zz_old_ports.yaml", nullptr, "schema: sim-profile/1\nkind: ports\nid: aaa_old\nassign: { 1: nope }\n", "UNRESOLVED_REF", "ports:aaa_old.assign.1", nullptr },
        { "match-also-ok", "device_sbc_wire.yaml", "match: { vid: 0x0a7b, pid: 0xd000 }",
          "match: { vid: 0x0a7b, pid: 0xd000 }\nmatch_also: [ { vid: 0x0a7b, pid: 0xd001, mode: second } ]", nullptr, nullptr,
          [](const SimProfileSet *s) {
              SimPort p[4];
              sim_profile_set_ports(s, nullptr, p);
              const SimPortDevice *d = dev_of(p[1], "sbc_wire");
              expect("D match-also-ok: accepted, the second id carried (0a7b:d001)", sim_profile_set_nrefused(s) == 0 && d &&
                     d->nmatch_also == 1 && d->match_also[0] == 0x0a7b && d->match_also[1] == 0xd001);
          } },
        { "old-ports", "zz_old_ports.yaml", nullptr,
          "schema: sim-profile/1\nkind: ports\nid: aaa_old\nassign: { 1: sb_real, 2: sb_mouse }\n", nullptr, nullptr,
          [](const SimProfileSet *s) {
              SimPort p[4];
              int on = sim_profile_set_ports(s, "aaa_old", p);
              const SimPortDevice *m = dev_of(p[1], "desk_mouse");
              bool integ = m && m->nbindings == 5 && m->bindings[0].integrate && m->bindings[0].counts_per_travel == 2000 &&
                           m->bindings[0].recentre && !std::strcmp(m->bindings[0].recentre->id, "middle");
              expect("D old-ports: the old assign form composes: 2 ports, the rigs' targets, devices and bindings",
                     sim_profile_set_nrefused(s) == 0 && on == 2 && p[0].profile && !std::strcmp(p[0].profile, "sb_real") &&
                     p[0].ndevices == 1 && p[0].devices[0].nbindings == 3 && p[0].has_title_id &&
                     p[0].title_id == 0x43430002u && integ && !p[2].target);
              expect("D old-ports: the first ports document by id is the default", sim_profile_set_ports(s, nullptr, p) == 2);
          } },
        { "window-edges", "bindings_racing.yaml", "window: [0.1, 0.9]", "window: [0, 1]", nullptr, nullptr,
          [](const SimProfileSet *s) { expect("D window-edges: accepted (no refusal)", sim_profile_set_nrefused(s) == 0); } },
        { "resp-dz-at-hi", "bindings_racing.yaml", "window: [0.1, 0.9], curve: 1.0", "window: [0.1, 0.9], curve: 1.0, deadzone: 0.9", "RESPONSE_DEADZONE", "bindings:pgr2_pedals", nullptr },
        { "resp-dz-below-hi", "bindings_racing.yaml", "window: [0.1, 0.9], curve: 1.0", "window: [0.1, 0.9], curve: 1.0, deadzone: 0.89", nullptr, nullptr,
          [](const SimProfileSet *s) { expect("D resp-dz-below-hi: accepted (no refusal)", sim_profile_set_nrefused(s) == 0); } },
        { "resp-dz-one-no-window", "bindings_racing.yaml", "window: [0.1, 0.9], curve: 1.0", "curve: 1.0, deadzone: 1", "RESPONSE_DEADZONE", "bindings:pgr2_pedals", nullptr },
        { "resp-dz-zero-no-window", "bindings_racing.yaml", "window: [0.1, 0.9], curve: 1.0", "curve: 1.0, deadzone: 0", nullptr, nullptr,
          [](const SimProfileSet *s) { expect("D resp-dz-zero-no-window: accepted (no refusal)", sim_profile_set_nrefused(s) == 0); } },
        { "resp-dz-negative", "bindings_racing.yaml", "window: [0.1, 0.9], curve: 1.0", "window: [0.1, 0.9], curve: 1.0, deadzone: -0.1", "RESPONSE_DEADZONE", "bindings:pgr2_pedals", nullptr },
        { "resp-dz-text", "bindings_racing.yaml", "window: [0.1, 0.9], curve: 1.0", "window: [0.1, 0.9], curve: 1.0, deadzone: half", "RESPONSE_DEADZONE", "bindings:pgr2_pedals", nullptr },
        { "resp-sat-zero", "bindings_racing.yaml", "window: [0.1, 0.9], curve: 1.0", "window: [0.1, 0.9], curve: 1.0, saturation: 0", "RESPONSE_SATURATION", "bindings:pgr2_pedals", nullptr },
        { "resp-sat-one", "bindings_racing.yaml", "window: [0.1, 0.9], curve: 1.0", "window: [0.1, 0.9], curve: 1.0, saturation: 1", nullptr, nullptr,
          [](const SimProfileSet *s) { expect("D resp-sat-one: accepted (no refusal)", sim_profile_set_nrefused(s) == 0); } },
        { "resp-sat-over", "bindings_racing.yaml", "window: [0.1, 0.9], curve: 1.0", "window: [0.1, 0.9], curve: 1.0, saturation: 1.5", "RESPONSE_SATURATION", "bindings:pgr2_pedals", nullptr },
        { "resp-sat-text", "bindings_racing.yaml", "window: [0.1, 0.9], curve: 1.0", "window: [0.1, 0.9], curve: 1.0, saturation: full", "RESPONSE_SATURATION", "bindings:pgr2_pedals", nullptr },
        { "resp-curve-zero", "bindings_racing.yaml", "window: [0.1, 0.9], curve: 1.0", "window: [0.1, 0.9], curve: 0", "RESPONSE_CURVE", "bindings:pgr2_pedals", nullptr },
        { "resp-curve-negative", "bindings_racing.yaml", "window: [0.1, 0.9], curve: 1.0", "window: [0.1, 0.9], curve: -2", "RESPONSE_CURVE", "bindings:pgr2_pedals", nullptr },
        { "resp-curve-half", "bindings_racing.yaml", "window: [0.1, 0.9], curve: 1.0", "window: [0.1, 0.9], curve: 0.5", nullptr, nullptr,
          [](const SimProfileSet *s) { expect("D resp-curve-half: accepted (no refusal)", sim_profile_set_nrefused(s) == 0); } },
        { "resp-curve-text", "bindings_racing.yaml", "window: [0.1, 0.9], curve: 1.0", "window: [0.1, 0.9], curve: steep", "RESPONSE_CURVE", "bindings:pgr2_pedals", nullptr },
        { "window-empty", "bindings_racing.yaml", "window: [0.1, 0.9]", "window: [0.5, 0.5]", "RESPONSE_WINDOW", "bindings:pgr2_pedals", nullptr },
        { "window-over", "bindings_racing.yaml", "window: [0.1, 0.9]", "window: [0.1, 1.5]", "RESPONSE_WINDOW", "bindings:pgr2_pedals", nullptr },
        { "window-under", "bindings_racing.yaml", "window: [0.1, 0.9]", "window: [-0.1, 0.9]", "RESPONSE_WINDOW", "bindings:pgr2_pedals", nullptr },
        { "window-three", "bindings_racing.yaml", "window: [0.1, 0.9]", "window: [0.1, 0.5, 0.9]", "RESPONSE_WINDOW", "bindings:pgr2_pedals", nullptr },
        { "split-two", "bindings_wheel.yaml", "{ source: steering, target: left_stick_x }", "{ source: steering, targets: [left_trigger, right_trigger], policy: split }", nullptr, nullptr,
          [](const SimProfileSet *s) { expect("D split-two: accepted (no refusal)", sim_profile_set_nrefused(s) == 0); } },
        { "split-three", "bindings_wheel.yaml", "{ source: steering, target: left_stick_x }", "{ source: steering, targets: [left_trigger, right_trigger, a], policy: split }", "SPLIT_SHAPE", "bindings:pgr2_wheel", nullptr },
        { "split-no-policy", "bindings_wheel.yaml", "{ source: steering, target: left_stick_x }", "{ source: steering, targets: [left_trigger, right_trigger] }", "SPLIT_SHAPE", "bindings:pgr2_wheel", nullptr },
        { "split-one-target", "bindings_wheel.yaml", "{ source: steering, target: left_stick_x }", "{ source: steering, target: left_trigger, policy: split }", "SPLIT_SHAPE", "bindings:pgr2_wheel", nullptr },
        { "counts-fraction", "bindings_sbc_mouse_aim.yaml", "mode: integrate, counts_per_travel: 2000, recentre: middle } }\n  - { source: rel_y", "mode: integrate, counts_per_travel: 0.5, recentre: middle } }\n  - { source: rel_y", nullptr, nullptr,
          [](const SimProfileSet *s) { expect("D counts-fraction: accepted (no refusal)", sim_profile_set_nrefused(s) == 0); } },
        { "counts-negative", "bindings_sbc_mouse_aim.yaml", "mode: integrate, counts_per_travel: 2000, recentre: middle } }\n  - { source: rel_y", "mode: integrate, counts_per_travel: -5, recentre: middle } }\n  - { source: rel_y", "INTEGRATE_COUNTS", "bindings:sb_mouse_aim", nullptr },
        { "port-four", "ports.yaml", "  3: { target: xbox_duke }", "  3: { target: xbox_duke }\n  4: { target: xbox_duke }", nullptr, nullptr,
          [](const SimProfileSet *s) { expect("D port-four: accepted (no refusal)", sim_profile_set_nrefused(s) == 0); } },
        { "port-zero", "ports.yaml", "  3: { target: xbox_duke }", "  0: { target: xbox_duke }", "PORT_RANGE", "ports:living_room", nullptr },
        { "port-text", "ports.yaml", "  3: { target: xbox_duke }", "  three: { target: xbox_duke }", "PORT_RANGE", "ports:living_room", nullptr },
        { "profile-map-key", "profile_sbc_real.yaml", "bindings: { sbc_wire: sbc_identity }", "bindings: { sbc_wire: \"\" }", "BINDINGS_MAP_SHAPE", "profile:sbc_real", nullptr },
        { "rig-device-bindings-type", "rig_sbc_real.yaml", "{ profile: sbc_wire, bindings: sbc_identity }", "{ profile: sbc_wire, bindings: [sbc_identity] }", "RIG_DEVICE", "rig:sb_real", nullptr },
        { "deadzone-one", "device_racing_pedals.yaml", "deadzone: 0.02", "deadzone: 1", "DEADZONE_RANGE", "device:racing_pedals", nullptr },
        { "deadzone-zero", "device_racing_pedals.yaml", "deadzone: 0.02", "deadzone: 0", nullptr, nullptr,
          [](const SimProfileSet *s) { expect("D deadzone-zero: accepted (no refusal)", sim_profile_set_nrefused(s) == 0); } },
        { "deadzone-negative", "device_racing_pedals.yaml", "deadzone: 0.02", "deadzone: -0.1", "DEADZONE_RANGE", "device:racing_pedals", nullptr },
        { "assign-four", "zz_old_ports.yaml", nullptr, "schema: sim-profile/1\nkind: ports\nid: aaa_old\nassign: { 4: sb_real }\n", nullptr, nullptr,
          [](const SimProfileSet *s) { expect("D assign-four: accepted (no refusal)", sim_profile_set_nrefused(s) == 0); } },
        { "assign-zero", "zz_old_ports.yaml", nullptr, "schema: sim-profile/1\nkind: ports\nid: aaa_old\nassign: { 0: sb_real }\n", "PORT_RANGE", "ports:aaa_old", nullptr },
        { "truncated", "zz_truncated.yaml", nullptr, "ports: {1: [unclosed\n", "PARSE_ERROR", "zz_truncated.yaml",
          [](const SimProfileSet *s) {
              SimPort p[4];
              expect("E truncated: the process survived and the rest of the set still composes",
                     sim_profile_set_ports(s, nullptr, p) == 3 && p[1].devices[0].nbindings == 3);
          } },
    };
    for (const Mut &m : muts) {
        std::string dir = mutated(m.name, m.file, m.find, m.repl);
        SimProfileSet *s = sim_profile_set_load(DATA.c_str(), dir.c_str());
        if (m.code) {
            bool hit = has_refusal(s, m.code, m.where);
            expect(std::string("D ") + m.name + ": refused " + m.code + (m.where ? std::string(" at ") + m.where : ""), hit);
            if (!hit) {
                print_refusals(s);
            }
        }
        if (m.also) {
            m.also(s);
        }
        sim_profile_set_free(s);
    }

    {
        std::istringstream man(slurp(BAD + "/MANIFEST.tsv"));
        std::string line;
        int rows = 0, hit = 0;
        std::getline(man, line);
        while (std::getline(man, line)) {
            std::vector<std::string> col;
            std::istringstream ls(line);
            std::string c;
            while (std::getline(ls, c, '\t')) {
                col.push_back(c);
            }
            if (col.size() < 3) {
                continue;
            }
            rows++;
            std::string dir = SCRATCH + "/bad-" + col[0];
            std::string cmd = "rm -rf '" + dir + "' && mkdir -p '" + dir + "' && cp '" + GOOD + "'/*.yaml '" + dir +
                              "'/ && rm '" + dir + "/" + col[1] + "' && cp '" + BAD + "/" + col[0] + "' '" + dir + "'/";
            bool made = std::system(cmd.c_str()) == 0;
            SimProfileSet *s = sim_profile_set_load(DATA.c_str(), dir.c_str());
            bool ok = made && has_refusal(s, col[2].c_str(), nullptr);
            hit += ok;
            if (!ok) {
                expect("G " + col[0] + ": refused " + col[2], false);
                print_refusals(s);
            }
            sim_profile_set_free(s);
        }
        expect("G: the app's 46 bad fixtures, each refused with its code", rows == 46 && hit == rows);
    }

    struct Sc { const char *text; char kind; long long i; double d; } sc[] = {
        { "", 'N', 0LL, 0 },
        { "~", 'N', 0LL, 0 },
        { "null", 'N', 0LL, 0 },
        { "Null", 'N', 0LL, 0 },
        { "NULL", 'N', 0LL, 0 },
        { "nUll", 'S', 0LL, 0 },
        { "true", 'B', 1LL, 0 },
        { "True", 'B', 1LL, 0 },
        { "TRUE", 'B', 1LL, 0 },
        { "tRue", 'S', 0LL, 0 },
        { "yes", 'B', 1LL, 0 },
        { "Yes", 'B', 1LL, 0 },
        { "YES", 'B', 1LL, 0 },
        { "on", 'B', 1LL, 0 },
        { "On", 'B', 1LL, 0 },
        { "ON", 'B', 1LL, 0 },
        { "no", 'B', 0LL, 0 },
        { "No", 'B', 0LL, 0 },
        { "NO", 'B', 0LL, 0 },
        { "off", 'B', 0LL, 0 },
        { "Off", 'B', 0LL, 0 },
        { "OFF", 'B', 0LL, 0 },
        { "false", 'B', 0LL, 0 },
        { "False", 'B', 0LL, 0 },
        { "FALSE", 'B', 0LL, 0 },
        { "n", 'S', 0LL, 0 },
        { "y", 'S', 0LL, 0 },
        { "N", 'S', 0LL, 0 },
        { "Y", 'S', 0LL, 0 },
        { "0", 'I', 0LL, 0 },
        { "1", 'I', 1LL, 0 },
        { "-1", 'I', -1LL, 0 },
        { "+5", 'I', 5LL, 0 },
        { "10", 'I', 10LL, 0 },
        { "012", 'I', 10LL, 0 },
        { "08", 'S', 0LL, 0 },
        { "0o17", 'S', 0LL, 0 },
        { "0b101", 'I', 5LL, 0 },
        { "0b2", 'S', 0LL, 0 },
        { "0x1F", 'I', 31LL, 0 },
        { "0X1f", 'S', 0LL, 0 },
        { "0x", 'S', 0LL, 0 },
        { "0x1g", 'S', 0LL, 0 },
        { "1_000", 'I', 1000LL, 0 },
        { "0x_1F", 'I', 31LL, 0 },
        { "00", 'I', 0LL, 0 },
        { "-0", 'I', 0LL, 0 },
        { "-012", 'I', -10LL, 0 },
        { "-0x10", 'I', -16LL, 0 },
        { "+0x10", 'I', 16LL, 0 },
        { "1.5", 'F', 0LL, 1.5 },
        { ".5", 'F', 0LL, 0.5 },
        { "1.", 'F', 0LL, 1.0 },
        { "-1.5e3", 'S', 0LL, 0 },
        { "1e3", 'S', 0LL, 0 },
        { "1.0e+3", 'F', 0LL, 1000.0 },
        { "1.0e3", 'S', 0LL, 0 },
        { "1.5E-2", 'F', 0LL, 0.015 },
        { ".inf", 'F', 0LL, INFINITY },
        { "-.inf", 'F', 0LL, -INFINITY },
        { "+.inf", 'F', 0LL, INFINITY },
        { ".Inf", 'F', 0LL, INFINITY },
        { ".INF", 'F', 0LL, INFINITY },
        { ".nan", 'F', 0LL, NAN },
        { ".NaN", 'F', 0LL, NAN },
        { "-.nan", 'S', 0LL, 0 },
        { "1.2.3", 'S', 0LL, 0 },
        { "1_0.5", 'F', 0LL, 10.5 },
        { "0x43430002", 'I', 1128464386LL, 0 },
        { "43430002", 'I', 43430002LL, 0 },
        { "9223372036854775807", 'I', 9223372036854775807LL, 0 },
        { "abc", 'S', 0LL, 0 },
        { "1a", 'S', 0LL, 0 },
        { "+", 'S', 0LL, 0 },
        { "0b", 'S', 0LL, 0 },
        { "0o", 'S', 0LL, 0 },
        { "r", 'S', 0LL, 0 },
        { "c", 'S', 0LL, 0 },
        { "pci-0000:00:14.0-usb-0:1:1.0", 'S', 0LL, 0 },
        { "0xd000", 'I', 53248LL, 0 },
        { "0xFFFF", 'I', 65535LL, 0 },
        { "0x0a7b", 'I', 2683LL, 0 },
        { "-32768", 'I', -32768LL, 0 },
        { "32767", 'I', 32767LL, 0 },
        { "0.02", 'F', 0LL, 0.02 },
        { "1.0E+3", 'F', 0LL, 1000.0 },
        { "1e+3", 'S', 0LL, 0 },
        { "1.5e-2", 'F', 0LL, 0.015 },
        { "-1.5e-3", 'F', 0LL, -0.0015 },
        { "0x8000", 'I', 32768LL, 0 },
        { "0x18000", 'I', 98304LL, 0 },
        { "5.", 'F', 0LL, 5.0 },
        { "+1.5", 'F', 0LL, 1.5 },
        { "-.5", 'S', 0LL, 0 },
        { ".", 'S', 0LL, 0 },
        { "1_", 'I', 1LL, 0 },
        { "_1", 'S', 0LL, 0 },
        { "0b_1", 'I', 1LL, 0 },
        { "1__0", 'I', 10LL, 0 },
        { "0x1F_", 'I', 31LL, 0 },
        { "1:30", 'I', 90LL, 0 },
        { "-1:30", 'I', -90LL, 0 },
        { "1:30.5", 'F', 0LL, 90.5 },
        { "190:20:30", 'I', 685230LL, 0 },
    };
    int sc_bad = 0;
    for (const Sc &c : sc) {
        long long i = -99;
        double d = -99;
        char k = sim_profile_scalar_kind(c.text, false, &i, &d);
        bool ok = k == c.kind && ((k != 'I' && k != 'B') || i == c.i) &&
                  (k != 'F' || (std::isnan(c.d) ? std::isnan(d) : d == c.d));
        if (!ok) {
            std::printf("     scalar '%s': reader %c %lld %g, PyYAML %c %lld %g\n", c.text, k, i, d, c.kind, c.i, c.d);
            sc_bad++;
        }
    }
    expect("F: every scalar is typed as PyYAML types it", sc_bad == 0);
    expect("F: a quoted scalar is always a string", sim_profile_scalar_kind("0x43430002", true, nullptr, nullptr) == 'S' &&
           sim_profile_scalar_kind("true", true, nullptr, nullptr) == 'S' &&
           sim_profile_scalar_kind("", true, nullptr, nullptr) == 'S');

    part_h();
    part_i();
    part_j();

    std::printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
