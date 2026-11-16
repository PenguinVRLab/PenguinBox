/*
 * SPDX-License-Identifier: MIT
 *
 * tests/xbox/simboot: the plan of a Sim Configurator boot (ui/xemu-sim-bootplan.{c,h}).
 *   A. hand-built ports: every way a port is attached, left unconnected, or fails the boot; one failing port fails
 *      the whole boot (nothing is attached)
 *   B. real documents loaded by the reader (ui/xemu-sim-profile-reader.cc) over the shipped data: the fixtures of
 *      tests/xbox/simprofile/good and tests/xbox/simmerge/profiles as they are, and a copy of good/ with a few
 *      documents this test writes (a keyboard-and-mouse setup that boots, and the documents that must not)
 *   C. where the shipped documents are found: an installed layout, a developer build, neither
 *   D. joystick-class devices by port path (ui/xemu-sim-evdev.h), against a made-up input tree this test writes
 *      (two identical sticks; no device is opened by the plan): hand-built ports, then the shipped T.16000M through
 *      the reader. Parts A to C run with the input folders pointed at that tree too, so this machine's own devices
 *      never decide a check.
 *   usage: simboot-test DATA_DIR TESTS_DIR WORK_DIR     exit 0 = every check passed
 *     DATA_DIR  data/sim-profiles; TESTS_DIR  tests/xbox; WORK_DIR  a scratch folder this test may fill
 *   RED drill: make drill (SIM_BOOTPLAN_DRILL_LET_THROUGH: a failing port is let through) must fail
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "xemu-sim-bootplan.h"
#include "xemu-sim-evdev.h"

namespace fs = std::filesystem;

static int nchecks, nfailed;

static void expect(const std::string &what, bool ok)
{
    nchecks++;
    if (!ok) {
        nfailed++;
    }
    printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
}

static bool has(const char *s, const char *needle)
{
    return s && strstr(s, needle) != nullptr;
}

static void show(const SimBootPlan &p)
{
    printf("     plan: %s; why: %s\n", p.ok ? "OK" : "FAIL", p.why);
    for (int i = 0; i < SIM_PROFILE_MAX_PORTS; i++) {
        if (p.line[i][0]) {
            printf("       %s\n", p.line[i]);
        }
    }
}

static bool none_attached(const SimBootPlan &p)
{
    for (int i = 0; i < SIM_PROFILE_MAX_PORTS; i++) {
        if (p.action[i] != SIM_PLAN_UNCONNECTED) {
            return false;
        }
    }
    return p.nattach == 0;
}

static SimInput key_input(const char *id, const char *scancode)
{
    SimInput in{};
    in.id = id;
    in.kind = SIM_IN_BUTTON;
    in.code = -1;
    in.scancode = scancode;
    return in;
}

static SimPortDevice device(const char *id, SimSourceKind src, int ninputs, const SimInput *inputs)
{
    SimPortDevice d{};
    d.device_id = id;
    d.key = id;
    d.source = src;
    d.ninputs = ninputs;
    d.inputs = inputs;
    d.usable = src != SIM_SRC_VR_VIRTUAL;
    return d;
}

static SimPort port(int n, const char *target, const char *profile, int ndev, const SimPortDevice *devs)
{
    SimPort p{};
    p.port = n;
    p.target = target;
    p.profile = profile;
    p.ndevices = ndev;
    p.devices = devs;
    return p;
}

static void blank(SimPort ports[4])
{
    for (int i = 0; i < 4; i++) {
        ports[i] = SimPort{};
        ports[i].port = i + 1;
    }
}

static int count_on(const SimPort ports[4])
{
    int on = 0;
    for (int i = 0; i < 4; i++) {
        on += ports[i].target != nullptr;
    }
    return on;
}

static bool accept_all(const SimPortDevice *, int, char *, size_t)
{
    return true;
}

static void part_a()
{
    printf("== A. hand-built ports\n");
    SimBootPlan p;
    SimPort ports[4];
    const SimPortDevice mouse = device("a_mouse", SIM_SRC_MOUSE, 0, nullptr);
    const SimInput keys_ok[] = { key_input("key_a", "30"), key_input("key_f14", "184") };
    const SimInput keys_bad[] = { key_input("key_a", "30"), key_input("key_odd", "240") };
    const SimInput keys_none[] = { key_input("key_named", nullptr) };
    const SimInput keys_text[] = { key_input("key_word", "KEY_A") };
    const SimPortDevice kbd = device("a_keys", SIM_SRC_KEYBOARD, 2, keys_ok);
    SimBinding bind_odd{}, bind_named{}, bind_word{};
    bind_odd.source = &keys_bad[1];
    bind_named.source = &keys_none[0];
    bind_word.source = &keys_text[0];
    SimPortDevice kbd_bad = device("a_keys_bad", SIM_SRC_KEYBOARD, 2, keys_bad);
    SimPortDevice kbd_none = device("a_keys_none", SIM_SRC_KEYBOARD, 1, keys_none);
    SimPortDevice kbd_text = device("a_keys_text", SIM_SRC_KEYBOARD, 1, keys_text);
    kbd_bad.nbindings = kbd_none.nbindings = kbd_text.nbindings = 1;
    kbd_bad.bindings = &bind_odd;
    kbd_none.bindings = &bind_named;
    kbd_text.bindings = &bind_word;
    const SimPortDevice stick = device("a_stick", SIM_SRC_SDL_JOYSTICK, 0, nullptr);
    const SimPortDevice pad = device("a_pad", SIM_SRC_SDL_GAMEPAD, 0, nullptr);
    const SimPortDevice hid = device("a_sbc", SIM_SRC_HID_USB, 0, nullptr);
    const SimPortDevice vr = device("a_vr", SIM_SRC_VR_VIRTUAL, 0, nullptr);
    const SimPortDevice mouse_kbd[] = { mouse, kbd };
    const SimPortDevice mouse_stick[] = { mouse, stick };
    const SimPortDevice mouse_vr[] = { mouse, vr };

    expect("A1 no ports id (the app wrote ports: null): FAIL", !sim_bootplan_decide(nullptr, 1, ports, nullptr, 0,
                                                                                   nullptr, &p) &&
           has(p.why, "no ports document") && none_attached(p));
    expect("A1 an empty ports id: FAIL", !sim_bootplan_decide("", 1, ports, nullptr, 0, nullptr, &p) &&
           has(p.why, "no ports document"));

    blank(ports);
    expect("A2 a ports id the store does not have: FAIL, named",
           !sim_bootplan_decide("away", -1, ports, nullptr, 0, nullptr, &p) && has(p.why, "'away'") &&
           has(p.why, "not in the profile store"));
    {
        SimRefusal r{ "SCHEMA_UNKNOWN", "ports:away", "schema sim-profile/9" };
        const SimRefusal *rs[] = { &r };
        expect("A2 a refused ports document: FAIL with the refusal",
               !sim_bootplan_decide("away", -1, ports, rs, 1, nullptr, &p) && has(p.why, "SCHEMA_UNKNOWN") &&
               has(p.why, "refused"));
        SimRefusal r2{ "SCHEMA_UNKNOWN", "ports:awayer", "another document" };
        const SimRefusal *rs2[] = { &r2 };
        expect("A2 a refusal of another document is not taken for it",
               !sim_bootplan_decide("away", -1, ports, rs2, 1, nullptr, &p) && !has(p.why, "SCHEMA_UNKNOWN"));
    }

    blank(ports);
    expect("A3 a ports document with no port on: FAIL", !sim_bootplan_decide("x", 0, ports, nullptr, 0, nullptr, &p) &&
           has(p.why, "no console port on"));

    blank(ports);
    ports[0] = port(1, "steel_battalion", "kbm", 2, mouse_kbd);
    ports[1] = port(2, "xbox_duke", nullptr, 0, nullptr);
    bool ok = sim_bootplan_decide("x", count_on(ports), ports, nullptr, 0, nullptr, &p);
    show(p);
    expect("A4 port 1 with a keyboard-and-mouse profile is attached", ok && p.action[0] == SIM_PLAN_ATTACH);
    expect("A4 port 2 on with no profile: nothing connected (this lane's reading)", ok &&
           p.action[1] == SIM_PLAN_UNCONNECTED && has(p.line[1], "no profile"));
    expect("A4 ports 3 and 4 not in the setup: nothing connected", p.action[2] == SIM_PLAN_UNCONNECTED &&
           p.action[3] == SIM_PLAN_UNCONNECTED && p.nattach == 1);

    blank(ports);
    ports[0] = port(1, "steel_battalion", nullptr, 1, &mouse);
    ports[0].profile_lost = "gone";
    ok = sim_bootplan_decide("x", count_on(ports), ports, nullptr, 0, nullptr, &p);
    expect("A5 a refused profile (profile_lost): FAIL naming the port and the profile",
           !ok && has(p.why, "port 1") && has(p.why, "gone") && none_attached(p));
    {
        SimRefusal r{ "UNRESOLVED_REF", "ports:x.ports.1.profile", "no profile 'gone' (absent, or refused)" };
        SimRefusal decoy{ "UNRESOLVED_REF", "ports:x.ports.10", "port 10 is not port 1" };
        const SimRefusal *rs[] = { &decoy, &r };
        ok = sim_bootplan_decide("x", count_on(ports), ports, rs, 2, nullptr, &p);
        expect("A5 ... with the reader's refusal as the reason", !ok && has(p.why, "UNRESOLVED_REF") &&
               has(p.why, "no profile 'gone'") && !has(p.why, "port 10"));
    }

    blank(ports);
    ports[0] = port(1, "steel_battalion", "kbm", 2, mouse_kbd);
    ports[1] = port(2, "steel_battalion", nullptr, 1, &mouse);
    ports[1].profile_lost = "half";
    ok = sim_bootplan_decide("x", count_on(ports), ports, nullptr, 0, nullptr, &p);
    show(p);
    expect("A6 one good port and one refused: the WHOLE boot fails, nothing is attached (never half a setup)",
           !ok && none_attached(p) && has(p.why, "port 2") && !has(p.why, "port 1"));

    blank(ports);
    ports[0] = port(1, "xbox_duke", "stick", 2, mouse_stick);
    ok = sim_bootplan_decide("x", count_on(ports), ports, nullptr, 0, nullptr, &p);
    show(p);
    expect("A7 a joystick this build cannot read: FAIL naming the port, the profile and the device",
           !ok && has(p.why, "port 1") && has(p.why, "stick") && has(p.why, "'a_stick'") && none_attached(p));
    expect("A7 the device predicate is the one passed (accept-all: attached)",
           sim_bootplan_decide("x", count_on(ports), ports, nullptr, 0, accept_all, &p) &&
           p.action[0] == SIM_PLAN_ATTACH);
    ports[0] = port(1, "xbox_duke", "pad", 1, &pad);
    expect("A7 a gamepad: FAIL", !sim_bootplan_decide("x", 1, ports, nullptr, 0, nullptr, &p) &&
           has(p.why, "'a_pad'"));
    ports[0] = port(1, "steel_battalion", "real", 1, &hid);
    expect("A7 the real Steel Battalion controller (hid_usb): FAIL",
           !sim_bootplan_decide("x", 1, ports, nullptr, 0, nullptr, &p) && has(p.why, "'a_sbc'"));

    ports[0] = port(1, "steel_battalion", "keys", 1, &kbd_bad);
    expect("A8 a key the key table lacks (scancode 240): FAIL naming the key",
           !sim_bootplan_decide("x", 1, ports, nullptr, 0, nullptr, &p) && has(p.why, "key_odd") &&
           has(p.why, "240"));
    ports[0] = port(1, "steel_battalion", "keys", 1, &kbd_none);
    expect("A8 a key with no scancode: FAIL", !sim_bootplan_decide("x", 1, ports, nullptr, 0, nullptr, &p) &&
           has(p.why, "key_named"));
    ports[0] = port(1, "steel_battalion", "keys", 1, &kbd_text);
    expect("A8 a scancode written as a name: FAIL (the key table is by evdev number)",
           !sim_bootplan_decide("x", 1, ports, nullptr, 0, nullptr, &p) && has(p.why, "KEY_A"));
    ports[0] = port(1, "steel_battalion", "keys", 1, &kbd);
    expect("A8 known keys (30, 184): attached", sim_bootplan_decide("x", 1, ports, nullptr, 0, nullptr, &p) &&
           p.action[0] == SIM_PLAN_ATTACH);

    ports[0] = port(1, "cyberpuck", "puck", 1, &mouse);
    expect("A9 a target PenguinBox cannot emulate: FAIL", !sim_bootplan_decide("x", 1, ports, nullptr, 0, nullptr,
                                                                              &p) && has(p.why, "cyberpuck"));
    ports[0] = port(1, "cyberpuck", nullptr, 0, nullptr);
    expect("A9 ... even with no profile on it", !sim_bootplan_decide("x", 1, ports, nullptr, 0, nullptr, &p) &&
           has(p.why, "cyberpuck"));

    blank(ports);
    ports[0] = port(1, "steel_battalion", "kbm", 2, mouse_kbd);
    {
        SimRefusal r{ "UNRESOLVED_REF", "ports:x.ports.3.target", "no target 'nope': the port is off" };
        const SimRefusal *rs[] = { &r };
        ok = sim_bootplan_decide("x", count_on(ports), ports, rs, 1, nullptr, &p);
        expect("A10 a named port the reader turned off (its target refused): FAIL with the refusal",
               !ok && has(p.why, "port 3") && has(p.why, "no target 'nope'") && none_attached(p));
        SimRefusal ra{ "UNRESOLVED_REF", "ports:x.assign.4", "no rig 'gone' (absent, or refused)" };
        const SimRefusal *ras[] = { &ra };
        ok = sim_bootplan_decide("x", count_on(ports), ports, ras, 1, nullptr, &p);
        expect("A10 ... the old assign: form too", !ok && has(p.why, "port 4") && has(p.why, "no rig 'gone'"));
    }

    blank(ports);
    ports[0] = port(1, "steel_battalion", nullptr, 0, nullptr);
    ports[2] = port(3, "xbox_duke", nullptr, 0, nullptr);
    ok = sim_bootplan_decide("x", count_on(ports), ports, nullptr, 0, nullptr, &p);
    expect("A11 every named port on but none with a profile: FAIL (no controller would be connected)",
           !ok && has(p.why, "no controller would be connected") && none_attached(p));

    blank(ports);
    ports[0] = port(1, "steel_battalion", "vr", 2, mouse_vr);
    expect("A12 a VR virtual device beside a mouse: attached (the VR layer's device passes)",
           sim_bootplan_decide("x", 1, ports, nullptr, 0, nullptr, &p) && p.action[0] == SIM_PLAN_ATTACH);
}

static void write_file(const fs::path &f, const char *text)
{
    std::ofstream o(f);
    o << text;
}

static const char *const RUNTIME_DOCS[][2] = {
    { "zz_boot_profile_kbm.yaml",
      "schema: sim-profile/1\nkind: profile\nid: boot_kbm\nname: \"boot plan: keyboard and mouse\"\n"
      "target: steel_battalion\ngame: { title_id: 0x43430002, name: \"Steel Battalion\" }\n"
      "bindings: { keyboard_104: sbc_keys, desk_mouse: sb_mouse_aim }\n" },
    { "zz_boot_ports_ok.yaml",
      "schema: sim-profile/1\nkind: ports\nid: boot_ok\nports:\n"
      "  1:\n    target: steel_battalion\n    profile: boot_kbm\n"
      "    devices:\n      - { device: keyboard_104 }\n      - { device: desk_mouse }\n"
      "  2: { target: xbox_duke }\n" },
    { "zz_boot_ports_lost.yaml",
      "schema: sim-profile/1\nkind: ports\nid: boot_lost\nports:\n"
      "  1:\n    target: steel_battalion\n    profile: boot_kbm\n"
      "    devices:\n      - { device: keyboard_104 }\n      - { device: desk_mouse }\n"
      "  2:\n    target: steel_battalion\n    profile: no_such_profile\n"
      "    devices:\n      - { device: desk_mouse }\n" },
    { "zz_boot_ports_badtarget.yaml",
      "schema: sim-profile/1\nkind: ports\nid: boot_badtarget\nports:\n"
      "  1:\n    target: steel_battalion\n    profile: boot_kbm\n"
      "    devices:\n      - { device: keyboard_104 }\n      - { device: desk_mouse }\n"
      "  3: { target: no_such_target }\n" },
    { "zz_boot_device_badkeys.yaml",
      "schema: sim-profile/1\nkind: device\nid: boot_bad_keys\nsource: keyboard\ninputs:\n"
      "  - { id: k_known, kind: button, scancode: 30 }\n  - { id: k_unknown, kind: button, scancode: 240 }\n" },
    { "zz_boot_bindings_badkeys.yaml",
      "schema: sim-profile/1\nkind: bindings\nid: boot_bad_keys_sb\ndevice: boot_bad_keys\ntarget: steel_battalion\n"
      "bind:\n  - { source: k_known, target: main_weapon }\n  - { source: k_unknown, target: sub_weapon }\n" },
    { "zz_boot_profile_badkeys.yaml",
      "schema: sim-profile/1\nkind: profile\nid: boot_badkey\nname: \"boot plan: a key PenguinBox lacks\"\n"
      "target: steel_battalion\nbindings: { boot_bad_keys: boot_bad_keys_sb }\n" },
    { "zz_boot_ports_badkeys.yaml",
      "schema: sim-profile/1\nkind: ports\nid: boot_badkeys\nports:\n"
      "  1:\n    target: steel_battalion\n    profile: boot_badkey\n    devices:\n      - { device: boot_bad_keys }\n" },
    { "zz_boot_ports_assign.yaml",
      "schema: sim-profile/1\nkind: ports\nid: boot_assign\nassign: { 1: sb_mouse }\n" },
    { "zz_boot_ports_puck.yaml",
      "schema: sim-profile/1\nkind: ports\nid: boot_puck\nports:\n"
      "  1:\n    target: steel_battalion\n    profile: boot_kbm\n"
      "    devices:\n      - { device: keyboard_104 }\n      - { device: desk_mouse }\n"
      "  2: { target: cyberpuck }\n" },
};

static void print_refusals(const SimProfileSet *set)
{
    for (int i = 0; i < sim_profile_set_nrefused(set); i++) {
        const SimRefusal *r = sim_profile_set_refusal(set, i);
        printf("     refused: %s %s: %s\n", r->code, r->where, r->why);
    }
}

static void part_b(const std::string &data, const std::string &tests, const std::string &work)
{
    printf("== B. real documents through the reader\n");
    SimPort ports[4];
    SimBootPlan p;

    SimProfileSet *set = sim_profile_set_load(data.c_str(), (tests + "/simprofile/good").c_str());
    bool ok = sim_bootplan_from_set(set, "living_room", ports, nullptr, &p);
    show(p);
    expect("B1 good/ports.yaml living_room: FAIL (a wheel and the real controller cannot be read yet)", !ok &&
           none_attached(p));
    expect("B1 ... port 1 named with its profile and its device", has(p.line[0], "port 1 (profile pgr2_racing)") &&
           has(p.line[0], "'wheel'"));
    expect("B1 ... port 2 named with its profile and the real controller", has(p.line[1], "sbc_real") &&
           has(p.line[1], "'sbc_wire'") && p.nfailed == 2);
    expect("B1 ... port 3 is on with no profile: nothing connected", has(p.line[2], "no profile"));
    sim_profile_set_free(set);

    set = sim_profile_set_load(data.c_str(), (tests + "/simmerge/profiles").c_str());
    ok = sim_bootplan_from_set(set, "e2e", ports, nullptr, &p);
    show(p);
    expect("B2 simmerge e2e: FAIL on port 1's joystick (e2e_pad), nothing attached", !ok &&
           has(p.why, "port 1 (profile e2e_sb)") && has(p.why, "'e2e_pad'") && none_attached(p));
    expect("B2 ... the device predicate accepting all: port 1 attached, port 2 (no profile) not",
           sim_bootplan_from_set(set, "e2e", ports, accept_all, &p) && p.action[0] == SIM_PLAN_ATTACH &&
           p.action[1] == SIM_PLAN_UNCONNECTED && p.nattach == 1);
    sim_profile_set_free(set);

    fs::path dir = fs::path(work) / "profiles";
    fs::remove_all(dir);
    fs::create_directories(dir);
    for (const auto &e : fs::directory_iterator(tests + "/simprofile/good")) {
        if (e.path().extension() == ".yaml") {
            fs::copy_file(e.path(), dir / e.path().filename());
        }
    }
    for (const auto &d : RUNTIME_DOCS) {
        write_file(dir / d[0], d[1]);
    }
    set = sim_profile_set_load(data.c_str(), dir.c_str());
    print_refusals(set);

    ok = sim_bootplan_from_set(set, "boot_ok", ports, nullptr, &p);
    show(p);
    expect("B3 boot_ok (the shipped keyboard_104 + good/'s desk_mouse, one profile): OK", ok);
    expect("B3 ... port 1 attached, with both devices and the profile", p.action[0] == SIM_PLAN_ATTACH &&
           ports[0].ndevices == 2 && ports[0].profile && !strcmp(ports[0].profile, "boot_kbm"));
    expect("B3 ... port 2 (on, no profile) and ports 3, 4: nothing connected", p.action[1] == SIM_PLAN_UNCONNECTED &&
           p.action[2] == SIM_PLAN_UNCONNECTED && p.action[3] == SIM_PLAN_UNCONNECTED && p.nattach == 1);
    expect("B3 ... the keyboard's bindings composed (sbc_keys: 5 binds)", ports[0].devices[0].nbindings == 5 &&
           ports[0].devices[0].source == SIM_SRC_KEYBOARD);

    ok = sim_bootplan_from_set(set, "boot_lost", ports, nullptr, &p);
    show(p);
    expect("B4 boot_lost: port 2's profile does not exist: FAIL with the reader's refusal, nothing attached",
           !ok && has(p.why, "port 2 (profile no_such_profile)") && has(p.why, "UNRESOLVED_REF") && none_attached(p));

    ok = sim_bootplan_from_set(set, "boot_badtarget", ports, nullptr, &p);
    show(p);
    expect("B5 boot_badtarget: port 3's target is unknown (the reader turns it off): FAIL naming port 3",
           !ok && has(p.why, "port 3") && has(p.why, "no_such_target") && none_attached(p));

    ok = sim_bootplan_from_set(set, "boot_badkeys", ports, nullptr, &p);
    show(p);
    expect("B6 boot_badkeys: a keyboard whose key 240 PenguinBox does not know: FAIL naming the key",
           !ok && has(p.why, "k_unknown") && has(p.why, "240"));

    ok = sim_bootplan_from_set(set, "no_such_ports", ports, nullptr, &p);
    expect("B7 a ports id the store does not hold: FAIL", !ok && has(p.why, "'no_such_ports'"));
    ok = sim_bootplan_from_set(set, "", ports, nullptr, &p);
    expect("B7 the hand-off's empty ports (ports: null): FAIL, never the first document by id",
           !ok && has(p.why, "no ports document"));

    ok = sim_bootplan_from_set(set, "boot_assign", ports, nullptr, &p);
    show(p);
    expect("B8 the old assign: form (rig sb_mouse, a mouse): OK, port 1 attached", ok &&
           p.action[0] == SIM_PLAN_ATTACH && p.nattach == 1);

    ok = sim_bootplan_from_set(set, "boot_puck", ports, nullptr, &p);
    show(p);
    expect("B9 boot_puck: port 2 is a cyberpuck (a shipped target PenguinBox does not emulate): FAIL",
           !ok && has(p.why, "port 2") && has(p.why, "cyberpuck") && none_attached(p));
    sim_profile_set_free(set);
}

static void part_c(const std::string &data, const std::string &work)
{
    printf("== C. the shipped documents' folder\n");
    fs::path root = fs::path(work) / "layout";
    fs::remove_all(root);
    fs::create_directories(root / "inst/usr/bin");
    fs::create_directories(root / "inst/usr/share/penguinbox/sim-profiles");
    fs::copy_file(fs::path(data) / "target_steel_battalion.yaml",
                  root / "inst/usr/share/penguinbox/sim-profiles/target_steel_battalion.yaml");
    fs::create_directories(root / "dev/build");
    fs::create_directories(root / "dev/data/sim-profiles");
    fs::copy_file(fs::path(data) / "target_steel_battalion.yaml",
                  root / "dev/data/sim-profiles/target_steel_battalion.yaml");
    fs::create_directories(root / "none/bin");
    fs::create_directories(root / "none/share/penguinbox/sim-profiles");

    char looked[1024];
    std::string exe = (root / "inst/usr/bin/qemu-system-i386").string();
    char *d = sim_bootplan_builtin_dir(exe.c_str(), looked, sizeof(looked));
    printf("     installed: %s\n", d ? d : looked);
    expect("C1 an installed layout: <exe>/../share/penguinbox/sim-profiles",
           d && fs::equivalent(d, root / "inst/usr/share/penguinbox/sim-profiles"));
    free(d);

    exe = (root / "dev/build/qemu-system-i386").string();
    d = sim_bootplan_builtin_dir(exe.c_str(), looked, sizeof(looked));
    printf("     developer: %s\n", d ? d : looked);
    expect("C2 a developer build: <exe>/../data/sim-profiles", d && fs::equivalent(d, root / "dev/data/sim-profiles"));
    free(d);

    exe = (root / "none/bin/qemu-system-i386").string();
    d = sim_bootplan_builtin_dir(exe.c_str(), looked, sizeof(looked));
    printf("     neither: looked at %s\n", looked);
    expect("C3 neither (an empty folder does not count): NULL, and every path looked at is named",
           !d && has(looked, "none/bin/../share/penguinbox/sim-profiles") && has(looked, "none/bin/../data/sim-profiles"));
    free(d);
    expect("C4 no executable path: NULL", !sim_bootplan_builtin_dir("", looked, sizeof(looked)));
}

#define STICK_1 "pci-0000:00:14.0-usb-0:7:1.0"
#define STICK_2 "pci-0000:00:14.0-usb-0:8:1.0"
#define NO_UNIT "pci-0000:00:14.0-usb-0:9:1.0"

static void input_tree(const fs::path &t)
{
    fs::remove_all(t);
    fs::create_directories(t / "dev/by-path");
    fs::create_directories(t / "udev");
    const char *const units[][2] = { { STICK_1, "event1" }, { STICK_2, "event2" } };
    for (const auto &u : units) {
        write_file(t / "dev" / u[1], "");
        fs::create_symlink(std::string("../") + u[1], t / "dev/by-path" / (std::string(u[0]) + "-event-joystick"));
        fs::path d = t / "sys" / u[1] / "device";
        fs::create_directories(d / "id");
        fs::create_directories(d / "capabilities");
        write_file(d / "id/vendor", "044f\n");
        write_file(d / "id/product", "b10a\n");
        write_file(d / "name", "Thrustmaster T.16000M\n");
        write_file(d / "capabilities/abs", "30063\n");
        write_file(d / "capabilities/key", "fff00000000 0 0 0 0\n");
    }
}

static SimInput stick_axis(const char *id, int code)
{
    SimInput in{};
    in.id = id;
    in.kind = SIM_IN_AXIS;
    in.has_range = true;
    in.range = SimRange{ -32768, 0, 32767 };
    in.bipolar = true;
    in.code = code;
    return in;
}

static void part_d_hand()
{
    printf("== D. joystick-class devices by port path (hand-built ports)\n");
    SimBootPlan p;
    SimPort ports[4];
    const SimInput in[] = { stick_axis("stick_x", 0), stick_axis("steer", -1), stick_axis("unbound", -1) };
    SimBinding b[2]{};
    b[0].source = &in[0];
    b[1].source = &in[1];
    SimPortDevice stick = device("a_t16", SIM_SRC_SDL_JOYSTICK, 3, in);
    stick.port_path = STICK_1;
    stick.nbindings = 1;
    stick.bindings = b;
    stick.has_match = true;
    stick.vid = 0x044f;
    stick.pid = 0xb10a;

    blank(ports);
    ports[0] = port(1, "xbox_duke", "stick", 1, &stick);
    bool ok = sim_bootplan_decide("x", 1, ports, nullptr, 0, nullptr, &p);
    show(p);
    expect("D1 a stick plugged in at its port path, its bound axis's code reported: attached",
           ok && p.action[0] == SIM_PLAN_ATTACH && p.nattach == 1);

    SimPortDevice gone = stick;
    gone.port_path = NO_UNIT;
    ports[0] = port(1, "xbox_duke", "stick", 1, &gone);
    ok = sim_bootplan_decide("x", 1, ports, nullptr, 0, nullptr, &p);
    show(p);
    expect("D2 no unit plugged in at its port path: FAIL naming the port and the port path, nothing attached",
           !ok && has(p.why, "port 1") && has(p.why, NO_UNIT) && none_attached(p));

    SimPortDevice nocode = stick;
    nocode.nbindings = 2;
    ports[0] = port(1, "xbox_duke", "stick", 1, &nocode);
    ok = sim_bootplan_decide("x", 1, ports, nullptr, 0, nullptr, &p);
    show(p);
    expect("D3 a bound input with no evdev code: FAIL as 'a_t16.steer has no evdev code'",
           !ok && has(p.why, "a_t16.steer has no evdev code") && none_attached(p));

    SimPortDevice other = stick;
    other.vid = 0x046d;
    other.pid = 0xc266;
    ports[0] = port(1, "xbox_duke", "stick", 1, &other);
    expect("D4 the unit at the port path is another model: FAIL naming both IDs",
           !sim_bootplan_decide("x", 1, ports, nullptr, 0, nullptr, &p) && has(p.why, "044f:b10a") &&
           has(p.why, "046d:c266"));

    SimPortDevice second = stick;
    second.port_path = STICK_2;
    const SimPortDevice mouse = device("a_mouse", SIM_SRC_MOUSE, 0, nullptr);
    const SimPortDevice two[] = { mouse, second };
    ports[0] = port(1, "xbox_duke", "stick", 1, &stick);
    ports[1] = port(2, "xbox_duke", "stick2", 2, two);
    ok = sim_bootplan_decide("x", 2, ports, nullptr, 0, nullptr, &p);
    show(p);
    expect("D5 two identical sticks at two port paths on two ports (one beside a mouse): both attached",
           ok && p.action[0] == SIM_PLAN_ATTACH && p.action[1] == SIM_PLAN_ATTACH && p.nattach == 2);

    SimPortDevice gpad = stick;
    gpad.device_id = "a_gpad";
    gpad.key = "a_gpad";
    gpad.source = SIM_SRC_SDL_GAMEPAD;
    blank(ports);
    ports[0] = port(1, "xbox_duke", "gpad", 1, &gpad);
    ok = sim_bootplan_decide("x", 1, ports, nullptr, 0, nullptr, &p);
    show(p);
    expect("D10 an sdl_gamepad device, the same stick at its port path: attached, as a joystick",
           ok && p.action[0] == SIM_PLAN_ATTACH && p.nattach == 1);
    SimPortDevice gnopath = gpad;
    gnopath.port_path = nullptr;
    ports[0] = port(1, "xbox_duke", "gpad", 1, &gnopath);
    expect("D11 an sdl_gamepad device with no port path: FAIL as a joystick's ('no port path')",
           !sim_bootplan_decide("x", 1, ports, nullptr, 0, nullptr, &p) && has(p.why, "'a_gpad' has no port path"));
    SimPortDevice gnocode = gpad;
    gnocode.nbindings = 2;
    ports[0] = port(1, "xbox_duke", "gpad", 1, &gnocode);
    expect("D12 an sdl_gamepad device with a bound input with no code: FAIL as 'a_gpad.steer has no evdev code'",
           !sim_bootplan_decide("x", 1, ports, nullptr, 0, nullptr, &p) && has(p.why, "a_gpad.steer has no evdev code"));
    SimPortDevice gunplugged = gpad;
    gunplugged.port_path = NO_UNIT;
    ports[0] = port(1, "xbox_duke", "gpad", 1, &gunplugged);
    expect("D13 an sdl_gamepad device whose unit is not plugged in: FAIL naming the port path",
           !sim_bootplan_decide("x", 1, ports, nullptr, 0, nullptr, &p) && has(p.why, NO_UNIT));
}

static const char *const STICK_DOCS[][2] = {
    { "zz_stick_bindings.yaml",
      "schema: sim-profile/1\nkind: bindings\nid: boot_t16_duke\ndevice: t16000m\ntarget: xbox_duke\n"
      "bind:\n  - { source: stick_x, target: left_stick_x }\n  - { source: stick_y, target: left_stick_y }\n" },
    { "zz_stick_profile.yaml",
      "schema: sim-profile/1\nkind: profile\nid: boot_t16\nname: \"boot plan: a T.16000M on the Duke\"\n"
      "target: xbox_duke\nbindings: { t16000m: boot_t16_duke }\n" },
    { "zz_stick_bindings_trigger.yaml",
      "schema: sim-profile/1\nkind: bindings\nid: boot_t16_duke_trigger\ndevice: t16000m\ntarget: xbox_duke\n"
      "bind:\n  - { source: stick_x, target: left_stick_x }\n  - { source: trigger, target: a }\n" },
    { "zz_stick_profile_trigger.yaml",
      "schema: sim-profile/1\nkind: profile\nid: boot_t16_trigger\nname: \"boot plan: the stick's trigger\"\n"
      "target: xbox_duke\nbindings: { t16000m: boot_t16_duke_trigger }\n" },
    { "zz_stick_ports_ok.yaml",
      "schema: sim-profile/1\nkind: ports\nid: boot_t16_ok\nports:\n"
      "  1:\n    target: xbox_duke\n    profile: boot_t16\n"
      "    devices:\n      - { device: t16000m, port_path: \"" STICK_1 "\" }\n"
      "  2:\n    target: xbox_duke\n    profile: boot_t16\n"
      "    devices:\n      - { device: t16000m, port_path: \"" STICK_2 "\" }\n" },
    { "zz_stick_ports_unplugged.yaml",
      "schema: sim-profile/1\nkind: ports\nid: boot_t16_unplugged\nports:\n"
      "  1:\n    target: xbox_duke\n    profile: boot_t16\n"
      "    devices:\n      - { device: t16000m, port_path: \"" STICK_1 "\" }\n"
      "  2:\n    target: xbox_duke\n    profile: boot_t16\n"
      "    devices:\n      - { device: t16000m, port_path: \"" NO_UNIT "\" }\n" },
    { "zz_stick_ports_trigger.yaml",
      "schema: sim-profile/1\nkind: ports\nid: boot_t16_trigger\nports:\n"
      "  1:\n    target: xbox_duke\n    profile: boot_t16_trigger\n"
      "    devices:\n      - { device: t16000m, port_path: \"" STICK_1 "\" }\n" },
};

static void part_d_docs(const std::string &data, const std::string &work)
{
    printf("== D. joystick-class devices by port path (the shipped T.16000M through the reader)\n");
    SimPort ports[4];
    SimBootPlan p;
    fs::path dir = fs::path(work) / "stick-profiles";
    fs::remove_all(dir);
    fs::create_directories(dir);
    for (const auto &d : STICK_DOCS) {
        write_file(dir / d[0], d[1]);
    }
    SimProfileSet *set = sim_profile_set_load(data.c_str(), dir.c_str());
    print_refusals(set);
    expect("D6 the test's stick documents load with no refusal", sim_profile_set_nrefused(set) == 0);

    bool ok = sim_bootplan_from_set(set, "boot_t16_ok", ports, nullptr, &p);
    show(p);
    expect("D7 the shipped T.16000M (stick_x, stick_y carry codes) at two port paths, both plugged in: OK, both "
           "ports attached", ok && p.action[0] == SIM_PLAN_ATTACH && p.action[1] == SIM_PLAN_ATTACH &&
           p.nattach == 2 && ports[0].devices[0].port_path && !strcmp(ports[0].devices[0].port_path, STICK_1));

    ok = sim_bootplan_from_set(set, "boot_t16_unplugged", ports, nullptr, &p);
    show(p);
    expect("D8 port 2's unit is not plugged in at boot: FAIL naming port 2 and its port path, nothing attached",
           !ok && has(p.why, "port 2") && has(p.why, NO_UNIT) && !has(p.why, "port 1") && none_attached(p));

    ok = sim_bootplan_from_set(set, "boot_t16_trigger", ports, nullptr, &p);
    show(p);
    expect("D9 a binding on the shipped trigger, which carries its code now: OK, port 1 attached",
           ok && p.action[0] == SIM_PLAN_ATTACH && p.nattach == 1);
    sim_profile_set_free(set);
}

static const char *const E_DOCS[][2] = {
    { "e_device_stick.yaml",
      "schema: sim-profile/1\nkind: device\nid: e_stick\nsource: sdl_joystick\nmatch: { vid: 0x044f, pid: 0xb10a }\n"
      "inputs:\n  - { id: x, kind: axis, code: 0, min: -32768, rest: 0, max: 32767 }\n"
      "  - { id: fire, kind: button, code: 288 }\n" },
    { "e1_bindings.yaml",
      "schema: sim-profile/1\nkind: bindings\nid: e1_bind\ndevice: e_stick\ntarget: xbox_duke\n"
      "bind:\n  - { source: x, target: left_trigger }\n" },
    { "e1_profile.yaml",
      "schema: sim-profile/1\nkind: profile\nid: e1_prof\nname: \"E1\"\ntarget: xbox_duke\nbindings: { e_stick: e1_bind }\n" },
    { "e1_ports.yaml",
      "schema: sim-profile/1\nkind: ports\nid: e1_ports\nports:\n  1:\n    target: xbox_duke\n    profile: e1_prof\n"
      "    devices:\n      - { device: e_stick, port_path: \"" STICK_1 "\" }\n" },
    { "e3_device_kb.yaml",
      "schema: sim-profile/1\nkind: device\nid: e_kb\nsource: keyboard\ninputs:\n"
      "  - { id: k_a, kind: button, scancode: 30 }\n  - { id: k_f14, kind: button, scancode: 184 }\n"
      "  - { id: k_odd, kind: button, scancode: 600 }\n" },
    { "e3_device_kb2.yaml",
      "schema: sim-profile/1\nkind: device\nid: e_kb2\nsource: keyboard\ninputs:\n"
      "  - { id: k_a, kind: button, scancode: 30 }\n" },
    { "e3_bindings.yaml",
      "schema: sim-profile/1\nkind: bindings\nid: e3_kb_f14\ndevice: e_kb\ntarget: xbox_duke\n"
      "bind:\n  - { source: k_f14, target: dpad_up }\n" },
    { "e3_bindings_a.yaml",
      "schema: sim-profile/1\nkind: bindings\nid: e3_kb_a\ndevice: e_kb\ntarget: xbox_duke\n"
      "bind:\n  - { source: k_a, target: dpad_left }\n" },
    { "e3_bindings_kb2.yaml",
      "schema: sim-profile/1\nkind: bindings\nid: e3_kb2_a\ndevice: e_kb2\ntarget: xbox_duke\n"
      "bind:\n  - { source: k_a, target: dpad_right }\n" },
    { "e3_bindings_box1.yaml",
      "schema: sim-profile/1\nkind: bindings\nid: e3_box_1\ndevice: diy_button_box_6\ntarget: xbox_duke\n"
      "bind:\n  - { source: btn_1, target: dpad_down }\n" },
    { "e3_bindings_box2.yaml",
      "schema: sim-profile/1\nkind: bindings\nid: e3_box_2\ndevice: diy_button_box_6\ntarget: xbox_duke\n"
      "bind:\n  - { source: btn_2, target: dpad_down }\n" },
    { "e3_profiles.yaml",
      "schema: sim-profile/1\nkind: profile\nid: e3_boxkb\nname: \"E3\"\ntarget: xbox_duke\n"
      "bindings: { e_kb: e3_kb_f14, diy_button_box_6: e3_box_1 }\n" },
    { "e3_profiles2.yaml",
      "schema: sim-profile/1\nkind: profile\nid: e3_boxkb_ok\nname: \"E3\"\ntarget: xbox_duke\n"
      "bindings: { e_kb: e3_kb_f14, diy_button_box_6: e3_box_2 }\n" },
    { "e3_profiles3.yaml",
      "schema: sim-profile/1\nkind: profile\nid: e3_twokb\nname: \"E3\"\ntarget: xbox_duke\n"
      "bindings: { e_kb: e3_kb_a, e_kb2: e3_kb2_a }\n" },
    { "e3_profiles4.yaml",
      "schema: sim-profile/1\nkind: profile\nid: e3_kb_only\nname: \"E3\"\ntarget: xbox_duke\nbindings: { e_kb: e3_kb_a }\n" },
    { "e3_profiles5.yaml",
      "schema: sim-profile/1\nkind: profile\nid: e3_kb2_only\nname: \"E3\"\ntarget: xbox_duke\nbindings: { e_kb2: e3_kb2_a }\n" },
    { "e3_ports_boxkb.yaml",
      "schema: sim-profile/1\nkind: ports\nid: e3_boxkb\nports:\n  1:\n    target: xbox_duke\n    profile: e3_boxkb\n"
      "    devices:\n      - { device: e_kb }\n      - { device: diy_button_box_6, port_path: \"" STICK_2 "\" }\n" },
    { "e3_ports_boxkb_ok.yaml",
      "schema: sim-profile/1\nkind: ports\nid: e3_boxkb_ok\nports:\n  1:\n    target: xbox_duke\n    profile: e3_boxkb_ok\n"
      "    devices:\n      - { device: e_kb }\n      - { device: diy_button_box_6, port_path: \"" STICK_2 "\" }\n" },
    { "e3_ports_twokb.yaml",
      "schema: sim-profile/1\nkind: ports\nid: e3_twokb\nports:\n  1:\n    target: xbox_duke\n    profile: e3_twokb\n"
      "    devices:\n      - { device: e_kb, port_path: \"" STICK_1 "\" }\n"
      "      - { device: e_kb2, port_path: \"" STICK_2 "\" }\n" },
    { "e3_ports_kb_odd.yaml",
      "schema: sim-profile/1\nkind: ports\nid: e3_kb_odd\nports:\n  1:\n    target: xbox_duke\n    profile: e3_kb_only\n"
      "    devices:\n      - { device: e_kb }\n" },
    { "e3_ports_crossport.yaml",
      "schema: sim-profile/1\nkind: ports\nid: e3_crossport\nports:\n"
      "  1:\n    target: xbox_duke\n    profile: e3_kb_only\n    devices:\n      - { device: e_kb }\n"
      "  2:\n    target: xbox_duke\n    profile: e3_kb2_only\n    devices:\n      - { device: e_kb2 }\n" },
    { "e3_ports_sharedkb.yaml",
      "schema: sim-profile/1\nkind: ports\nid: e3_sharedkb\nports:\n"
      "  1:\n    target: xbox_duke\n    profile: e3_kb_only\n    devices:\n      - { device: e_kb }\n"
      "  2:\n    target: xbox_duke\n    profile: e3_kb_only\n    devices:\n      - { device: e_kb }\n" },
    { "e4_device_mouse.yaml",
      "schema: sim-profile/1\nkind: device\nid: e_mouse\nsource: mouse\ninputs:\n"
      "  - { id: rel_z, kind: relative, code: 2 }\n  - { id: b_task, kind: button, code: 279 }\n" },
    { "e4_bindings_ok.yaml",
      "schema: sim-profile/1\nkind: bindings\nid: e4_ok\ndevice: mouse\ntarget: steel_battalion\nbind:\n"
      "  - { source: left, target: main_weapon }\n  - { source: side_forward, target: sub_weapon }\n"
      "  - { source: rel_x, target: aiming_x, response: { mode: integrate, counts_per_travel: 1000 } }\n"
      "  - { source: wheel, target: aiming_y, response: { mode: integrate, counts_per_travel: 1000 } }\n" },
    { "e4_bindings_edge.yaml",
      "schema: sim-profile/1\nkind: bindings\nid: e4_edge\ndevice: mouse\ntarget: steel_battalion\nbind:\n"
      "  - { source: left, target: main_weapon }\n  - { source: side_right, target: sub_weapon }\n" },
    { "e4_bindings_relz.yaml",
      "schema: sim-profile/1\nkind: bindings\nid: e4_relz\ndevice: e_mouse\ntarget: steel_battalion\nbind:\n"
      "  - { source: rel_z, target: aiming_x, response: { mode: integrate, counts_per_travel: 100 } }\n" },
    { "e4_profiles.yaml",
      "schema: sim-profile/1\nkind: profile\nid: e4_ok\nname: \"E4\"\ntarget: steel_battalion\nbindings: { mouse: e4_ok }\n" },
    { "e4_profiles2.yaml",
      "schema: sim-profile/1\nkind: profile\nid: e4_edge\nname: \"E4\"\ntarget: steel_battalion\nbindings: { mouse: e4_edge }\n" },
    { "e4_profiles3.yaml",
      "schema: sim-profile/1\nkind: profile\nid: e4_relz\nname: \"E4\"\ntarget: steel_battalion\nbindings: { e_mouse: e4_relz }\n" },
    { "e4_ports.yaml",
      "schema: sim-profile/1\nkind: ports\nid: e4_ok\nports:\n  1:\n    target: steel_battalion\n    profile: e4_ok\n"
      "    devices:\n      - { device: mouse }\n" },
    { "e4_ports2.yaml",
      "schema: sim-profile/1\nkind: ports\nid: e4_edge\nports:\n  1:\n    target: steel_battalion\n    profile: e4_edge\n"
      "    devices:\n      - { device: mouse }\n" },
    { "e4_ports3.yaml",
      "schema: sim-profile/1\nkind: ports\nid: e4_relz\nports:\n  1:\n    target: steel_battalion\n    profile: e4_relz\n"
      "    devices:\n      - { device: e_mouse }\n" },
    { "e5_bindings.yaml",
      "schema: sim-profile/1\nkind: bindings\nid: e5_pedals\ndevice: e_stick\ntarget: steel_battalion\nbind:\n"
      "  - { source: x, targets: [left_pedal, right_pedal], policy: split }\n" },
    { "e5_bindings2.yaml",
      "schema: sim-profile/1\nkind: bindings\nid: e5_button\ndevice: e_stick\ntarget: steel_battalion\nbind:\n"
      "  - { source: fire, targets: [main_weapon, sub_weapon], policy: split }\n" },
    { "e5_bindings3.yaml",
      "schema: sim-profile/1\nkind: bindings\nid: e5_lever\ndevice: e_stick\ntarget: steel_battalion\nbind:\n"
      "  - { source: x, targets: [gear_lever, tuner_dial], policy: split }\n" },
    { "e5_profiles.yaml",
      "schema: sim-profile/1\nkind: profile\nid: e5_pedals\nname: \"E5\"\ntarget: steel_battalion\nbindings: { e_stick: e5_pedals }\n" },
    { "e5_profiles2.yaml",
      "schema: sim-profile/1\nkind: profile\nid: e5_button\nname: \"E5\"\ntarget: steel_battalion\nbindings: { e_stick: e5_button }\n" },
    { "e5_profiles3.yaml",
      "schema: sim-profile/1\nkind: profile\nid: e5_lever\nname: \"E5\"\ntarget: steel_battalion\nbindings: { e_stick: e5_lever }\n" },
    { "e5_ports.yaml",
      "schema: sim-profile/1\nkind: ports\nid: e5_pedals\nports:\n  1:\n    target: steel_battalion\n    profile: e5_pedals\n"
      "    devices:\n      - { device: e_stick, port_path: \"" STICK_1 "\" }\n" },
    { "e5_ports2.yaml",
      "schema: sim-profile/1\nkind: ports\nid: e5_button\nports:\n  1:\n    target: steel_battalion\n    profile: e5_button\n"
      "    devices:\n      - { device: e_stick, port_path: \"" STICK_1 "\" }\n" },
    { "e6_bindings.yaml",
      "schema: sim-profile/1\nkind: bindings\nid: e6_level\ndevice: e_stick\ntarget: xbox_duke\nbind:\n"
      "  - { source: x, target: a, policy: absolute }\n" },
    { "e6_bindings2.yaml",
      "schema: sim-profile/1\nkind: bindings\nid: e6_threshold\ndevice: e_stick\ntarget: xbox_duke\nbind:\n"
      "  - { source: x, target: a, threshold: 0.5, hysteresis: 0.1 }\n  - { source: fire, target: b }\n" },
    { "e6_profiles.yaml",
      "schema: sim-profile/1\nkind: profile\nid: e6_level\nname: \"E6\"\ntarget: xbox_duke\nbindings: { e_stick: e6_level }\n" },
    { "e6_profiles2.yaml",
      "schema: sim-profile/1\nkind: profile\nid: e6_threshold\nname: \"E6\"\ntarget: xbox_duke\nbindings: { e_stick: e6_threshold }\n" },
    { "e6_ports.yaml",
      "schema: sim-profile/1\nkind: ports\nid: e6_level\nports:\n  1:\n    target: xbox_duke\n    profile: e6_level\n"
      "    devices:\n      - { device: e_stick, port_path: \"" STICK_1 "\" }\n" },
    { "e6_ports2.yaml",
      "schema: sim-profile/1\nkind: ports\nid: e6_threshold\nports:\n  1:\n    target: xbox_duke\n    profile: e6_threshold\n"
      "    devices:\n      - { device: e_stick, port_path: \"" STICK_1 "\" }\n" },
    { "e7_bindings.yaml",
      "schema: sim-profile/1\nkind: bindings\nid: e7_bind\ndevice: e_stick\ntarget: xbox_duke\nbind:\n"
      "  - { source: x, target: left_stick_x }\n" },
    { "e7_bindings2.yaml",
      "schema: sim-profile/1\nkind: bindings\nid: e7_bind2\ndevice: e_stick\ntarget: xbox_duke\nbind:\n"
      "  - { source: x, target: right_stick_x }\n" },
    { "e7_profiles.yaml",
      "schema: sim-profile/1\nkind: profile\nid: e7_twin\nname: \"E7\"\ntarget: xbox_duke\nbindings: { e_stick: e7_bind }\n" },
    { "e7_profiles2.yaml",
      "schema: sim-profile/1\nkind: profile\nid: e7_pair\nname: \"E7\"\ntarget: xbox_duke\n"
      "bindings: { left: e7_bind, right: e7_bind2 }\n" },
    { "e7_ports.yaml",
      "schema: sim-profile/1\nkind: ports\nid: e7_twin\nports:\n  1:\n    target: xbox_duke\n    profile: e7_twin\n"
      "    devices:\n      - { device: e_stick, port_path: \"" STICK_1 "\" }\n"
      "      - { device: e_stick, port_path: \"" STICK_2 "\" }\n" },
    { "e7_ports2.yaml",
      "schema: sim-profile/1\nkind: ports\nid: e7_pair\nports:\n  1:\n    target: xbox_duke\n    profile: e7_pair\n"
      "    devices:\n      - { device: e_stick, instance: left, port_path: \"" STICK_1 "\" }\n"
      "      - { device: e_stick, instance: right, port_path: \"" STICK_2 "\" }\n" },
    { "e5_ports3.yaml",
      "schema: sim-profile/1\nkind: ports\nid: e5_lever\nports:\n  1:\n    target: steel_battalion\n    profile: e5_lever\n"
      "    devices:\n      - { device: e_stick, port_path: \"" STICK_1 "\" }\n" },
};

static SimProfileSet *e_load(const std::string &data, const std::string &work)
{
    fs::path dir = fs::path(work) / "e-profiles";
    fs::remove_all(dir);
    fs::create_directories(dir);
    for (const auto &d : E_DOCS) {
        write_file(dir / d[0], d[1]);
    }
    return sim_profile_set_load(data.c_str(), dir.c_str());
}

static void part_e(const std::string &data, const std::string &work)
{
    printf("== E. the setups a boot refuses by name\n");
    SimPort ports[4];
    SimBootPlan p;
    SimProfileSet *set = e_load(data, work);
    print_refusals(set);

    bool ok = sim_bootplan_from_set(set, "e1_ports", ports, nullptr, &p);
    show(p);
    expect("E1 a profile whose bindings the reader refused: FAIL with the ROOT refusal (its code, its text, the "
           "document), not only the lost profile",
           !ok && has(p.why, "POLARITY_POLICY_REQUIRED") && has(p.why, "needs a declared policy") &&
           has(p.why, "bindings 'e1_bind'") && has(p.why, "port 1 (profile e1_prof)") && none_attached(p));

    ok = sim_bootplan_from_set(set, "e3_boxkb", ports, nullptr, &p);
    show(p);
    expect("E3 a button box beside a keyboard, both binding scancode 184: FAIL naming both devices, both keys and "
           "that PenguinBox reads them as one keyboard", !ok && has(p.why, "'e_kb'") && has(p.why, "'k_f14'") &&
           has(p.why, "'diy_button_box_6'") && has(p.why, "'btn_1'") && has(p.why, "184") &&
           has(p.why, "one keyboard") && none_attached(p));
    ok = sim_bootplan_from_set(set, "e3_boxkb_ok", ports, nullptr, &p);
    show(p);
    expect("E3 ... the box's other key (185), which the keyboard does not bind: OK", ok && p.action[0] == SIM_PLAN_ATTACH);
    ok = sim_bootplan_from_set(set, "e3_twokb", ports, nullptr, &p);
    show(p);
    expect("E3 two keyboards told apart by port path, both binding key A (30): FAIL naming both",
           !ok && has(p.why, "'e_kb'") && has(p.why, "'e_kb2'") && has(p.why, "30") && none_attached(p));
    ok = sim_bootplan_from_set(set, "e3_crossport", ports, nullptr, &p);
    show(p);
    expect("E3 ... the two keyboards on two driven ports (key A on each): FAIL naming port 2 and port 1",
           !ok && has(p.why, "port 2 (profile e3_kb2_only)") && has(p.why, "port 1") && none_attached(p));
    ok = sim_bootplan_from_set(set, "e3_sharedkb", ports, nullptr, &p);
    show(p);
    expect("E3 ... ONE keyboard (the same device) on two ports, key A on each: OK (one unit, as the app reads it)",
           ok && p.nattach == 2);
    ok = sim_bootplan_from_set(set, "e3_kb_odd", ports, nullptr, &p);
    show(p);
    expect("E3 a keyboard whose UNBOUND key (scancode 600) PenguinBox does not know: OK (only bound keys are checked)",
           ok && p.action[0] == SIM_PLAN_ATTACH);

    ok = sim_bootplan_from_set(set, "e4_ok", ports, nullptr, &p);
    show(p);
    expect("E4 the shipped mouse's left and front thumb buttons, its motion and its wheel: OK",
           ok && p.action[0] == SIM_PLAN_ATTACH);
    ok = sim_bootplan_from_set(set, "e4_edge", ports, nullptr, &p);
    show(p);
    expect("E4 the shipped mouse's right-edge button (BTN_FORWARD 277), which the mouse source does not read: FAIL "
           "naming the mouse, the input and the code", !ok && has(p.why, "'mouse'") && has(p.why, "'side_right'") &&
           has(p.why, "277") && none_attached(p));
    ok = sim_bootplan_from_set(set, "e4_relz", ports, nullptr, &p);
    show(p);
    expect("E4 a mouse motion REL_Z (2) on the aiming lever: FAIL naming it",
           !ok && has(p.why, "'e_mouse'") && has(p.why, "'rel_z'") && has(p.why, "motion code 2") &&
           none_attached(p));
    {
        const SimInput in[] = { SimInput{}, SimInput{} };
        SimInput odd = in[0], xmove = in[1];
        odd.id = "b_task";
        odd.kind = SIM_IN_BUTTON;
        odd.code = 279;
        xmove.id = "rel_x";
        xmove.kind = SIM_IN_RELATIVE;
        xmove.code = 0;
        const SimInput both[] = { odd, xmove };
        SimBinding b{};
        b.source = &both[1];
        SimPortDevice m = device("e_mouse2", SIM_SRC_MOUSE, 2, both);
        m.nbindings = 1;
        m.bindings = &b;
        blank(ports);
        ports[0] = port(1, "steel_battalion", "hand", 1, &m);
        expect("E4 ... an UNBOUND button the source does not read (BTN_TASK 279): OK",
               sim_bootplan_decide("x", 1, ports, nullptr, 0, nullptr, &p) && p.action[0] == SIM_PLAN_ATTACH);
    }

    ok = sim_bootplan_from_set(set, "e5_pedals", ports, nullptr, &p);
    show(p);
    expect("E5 a stick axis split onto two pedals (the split PenguinBox computes): OK", ok && p.action[0] == SIM_PLAN_ATTACH);
    ok = sim_bootplan_from_set(set, "e5_button", ports, nullptr, &p);
    show(p);
    expect("E5 a button split onto two buttons: FAIL naming the device, the input and both controls",
           !ok && has(p.why, "'e_stick'") && has(p.why, "'fire'") && has(p.why, "'main_weapon'") &&
           has(p.why, "'sub_weapon'") && none_attached(p));
    ok = sim_bootplan_from_set(set, "e5_lever", ports, nullptr, &p);
    show(p);
    expect("E5 an axis split onto the gear lever and the tuner dial: FAIL naming them",
           !ok && has(p.why, "'gear_lever'") && has(p.why, "'tuner_dial'") && none_attached(p));

    ok = sim_bootplan_from_set(set, "e6_level", ports, nullptr, &p);
    show(p);
    expect("E6 a stick axis on the Duke's pressure-sensitive A with no threshold: FAIL naming the axis and the button",
           !ok && has(p.why, "'e_stick'") && has(p.why, "'x'") && has(p.why, "'a'") && has(p.why, "threshold") &&
           none_attached(p));
    ok = sim_bootplan_from_set(set, "e6_threshold", ports, nullptr, &p);
    show(p);
    expect("E6 ... the axis with a threshold, and a button on B: OK", ok && p.action[0] == SIM_PLAN_ATTACH);

    ok = sim_bootplan_from_set(set, "e7_twin", ports, nullptr, &p);
    show(p);
    expect("E7 the same stick model twice on one port with no instance: FAIL naming the device",
           !ok && has(p.why, "'e_stick'") && has(p.why, "twice") && none_attached(p));
    ok = sim_bootplan_from_set(set, "e7_pair", ports, nullptr, &p);
    show(p);
    expect("E7 ... each with its own instance: OK", ok && p.action[0] == SIM_PLAN_ATTACH);
    sim_profile_set_free(set);
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: simboot-test DATA_DIR TESTS_DIR WORK_DIR\n");
        return 2;
    }
    static std::string tdev, tby, tsys, tudev;
    fs::path tree = fs::path(argv[3]) / "input-tree";
    input_tree(tree);
    tdev = (tree / "dev").string();
    tby = (tree / "dev/by-path").string();
    tsys = (tree / "sys").string();
    tudev = (tree / "udev").string();
    SimEvdevRoots roots = { tdev.c_str(), tby.c_str(), tsys.c_str(), tudev.c_str() };
    sim_evdev_set_roots(&roots);
    part_a();
    part_b(argv[1], argv[2], argv[3]);
    part_c(argv[1], argv[3]);
    part_d_hand();
    part_d_docs(argv[1], argv[3]);
    part_e(argv[1], argv[3]);
    printf("%d checks, %d failed\n", nchecks, nfailed);
    return nfailed ? 1 : 0;
}
