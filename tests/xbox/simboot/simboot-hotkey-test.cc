/*
 * PenguinBox — the emulator's hotkeys on keys a Sim Configurator setup binds (ui/xemu-sim-boot.h:
 * sim_boot_bound_keys, sim_boot_hotkey_held), on hand-built ports
 *
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 *
 * The rule: a key a keyboard device on an attached port binds does not also act as an emulator hotkey; a key the
 * setup does not bind keeps it. Evdev codes (as a Sim Configurator document writes them) against SDL scancodes:
 * KEY_F5 63 = SDL F5 62, KEY_F6 64 = 63, KEY_F8 66 = 65, KEY_F10 68 = 67, KEY_F11 87 = 68, KEY_A 30 = 4, KEY_W 17 = 26.
 */
#include <cstdio>
#include <string>

#include "xemu-sim-boot.h"

static int nchecks, nfailed;

static void expect(const std::string &what, bool ok)
{
    nchecks++;
    if (!ok) {
        nfailed++;
    }
    printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
}

enum { SDL_F5 = 62, SDL_F6 = 63, SDL_F7 = 64, SDL_F8 = 65, SDL_F10 = 67, SDL_F11 = 68, SDL_A = 4, SDL_W = 26 };

static SimInput key(const char *id, const char *scancode)
{
    SimInput in = {};
    in.id = id;
    in.kind = SIM_IN_BUTTON;
    in.code = -1;
    in.scancode = scancode;
    return in;
}

static SimBinding bind(const SimInput *source)
{
    SimBinding b = {};
    b.source = source;
    return b;
}

static SimPortDevice device(SimSourceKind src, const SimInput *inputs, int ninputs, const SimBinding *bindings,
                            int nbindings, bool usable = true)
{
    SimPortDevice d = {};
    d.device_id = "dev";
    d.key = "dev";
    d.source = src;
    d.ninputs = ninputs;
    d.inputs = inputs;
    d.nbindings = nbindings;
    d.bindings = bindings;
    d.usable = usable;
    return d;
}

static void clear(SimPort ports[SIM_PROFILE_MAX_PORTS], SimBootPlan *plan)
{
    for (int i = 0; i < SIM_PROFILE_MAX_PORTS; i++) {
        ports[i] = SimPort{};
        ports[i].port = i + 1;
        plan->action[i] = SIM_PLAN_UNCONNECTED;
    }
    plan->ok = true;
    plan->nattach = 0;
}

int main()
{
    printf("== E. hotkeys on keys the setup binds\n");
    unsigned char held[SIM_BOOT_NKEYS];
    SimPort ports[SIM_PROFILE_MAX_PORTS];
    SimBootPlan plan = {};

    const SimInput kin[] = { key("key_f5", "63"), key("key_w", "17"), key("key_a", "0x1e"), key("key_f6", "64") };
    const SimBinding kb[] = { bind(&kin[0]), bind(&kin[1]), bind(&kin[2]) };
    const SimPortDevice kbd = device(SIM_SRC_KEYBOARD, kin, 4, kb, 3);
    const SimInput k2in[] = { key("key_f8", "66") };
    const SimBinding k2b[] = { bind(&k2in[0]) };
    const SimPortDevice kbd2 = device(SIM_SRC_KEYBOARD, k2in, 1, k2b, 1);
    SimInput min[] = { key("btn_left", "65") };
    const SimBinding mb[] = { bind(&min[0]) };
    const SimPortDevice mouse = device(SIM_SRC_MOUSE, min, 1, mb, 1);
    const SimInput vin[] = { key("key_f10", "68") };
    const SimBinding vb[] = { bind(&vin[0]) };
    const SimPortDevice vr = device(SIM_SRC_KEYBOARD, vin, 1, vb, 1, false);
    const SimInput nin[] = { key("key_f11", "KEY_F11"), key("key_x", nullptr) };
    const SimBinding nb[] = { bind(&nin[0]), bind(&nin[1]) };
    const SimPortDevice named = device(SIM_SRC_KEYBOARD, nin, 2, nb, 2);
    const SimPortDevice p3[] = { mouse, vr, named };

    clear(ports, &plan);
    ports[0].target = "steel_battalion";
    ports[0].profile = "sbc";
    ports[0].ndevices = 1;
    ports[0].devices = &kbd;
    ports[1].target = "xbox_duke";
    ports[1].profile = "duke";
    ports[1].ndevices = 1;
    ports[1].devices = &kbd2;
    ports[2].target = "xbox_duke";
    ports[2].profile = "other";
    ports[2].ndevices = 3;
    ports[2].devices = p3;
    plan.action[0] = SIM_PLAN_ATTACH;
    plan.action[2] = SIM_PLAN_ATTACH;
    plan.nattach = 2;
    memset(held, 0xff, sizeof(held));
    int n = sim_boot_bound_keys(ports, &plan, held);
    printf("     bound keys: %d\n", n);
    expect("E1 F5, bound on an attached port: its hotkey stands down", sim_boot_hotkey_held(held, SDL_F5));
    expect("E2 W and A (a hex code) bound there: marked", sim_boot_hotkey_held(held, SDL_W) &&
           sim_boot_hotkey_held(held, SDL_A));
    expect("E3 F6, declared by the device but bound by nothing: keeps its hotkey", !sim_boot_hotkey_held(held, SDL_F6));
    expect("E4 F8, bound only on a port the plan does not attach: keeps its hotkey", !sim_boot_hotkey_held(held, SDL_F8));
    expect("E5 a mouse button's number (65, KEY_F7 as a key) is not a key", !sim_boot_hotkey_held(held, SDL_F7));
    expect("E6 F10 on a VR virtual (unusable) device: keeps its hotkey", !sim_boot_hotkey_held(held, SDL_F10));
    expect("E7 a key named, not numbered, and a binding with no scancode: nothing marked",
           !sim_boot_hotkey_held(held, SDL_F11));
    expect("E8 the count is the three keys of port 1, the cleared table holds nothing else", n == 3 && [&] {
        int m = 0;
        for (int i = 0; i < SIM_BOOT_NKEYS; i++) {
            m += held[i] != 0;
        }
        return m == 3;
    }());
    expect("E9 out of range: 0, -1 and SIM_BOOT_NKEYS are never held", !sim_boot_hotkey_held(held, 0) &&
           !sim_boot_hotkey_held(held, -1) && !sim_boot_hotkey_held(held, SIM_BOOT_NKEYS));

    plan.action[1] = SIM_PLAN_ATTACH;
    n = sim_boot_bound_keys(ports, &plan, held);
    expect("E10 port 2 attached: F8 stands down, 4 keys", sim_boot_hotkey_held(held, SDL_F8) && n == 4);

    clear(ports, &plan);
    memset(held, 1, sizeof(held));
    n = sim_boot_bound_keys(ports, &plan, held);
    expect("E11 no attached port: no key held", n == 0 && !sim_boot_hotkey_held(held, SDL_F5));

    clear(ports, &plan);
    const SimBinding twice[] = { bind(&kin[0]), bind(&kin[0]) };
    const SimPortDevice kt = device(SIM_SRC_KEYBOARD, kin, 4, twice, 2);
    ports[0].ndevices = 1;
    ports[0].devices = &kt;
    ports[3].ndevices = 1;
    ports[3].devices = &kt;
    plan.action[0] = SIM_PLAN_ATTACH;
    plan.action[3] = SIM_PLAN_ATTACH;
    n = sim_boot_bound_keys(ports, &plan, held);
    expect("E12 F5 bound twice on two ports: held, counted once", n == 1 && sim_boot_hotkey_held(held, SDL_F5));

    clear(ports, &plan);
    SimInput jb = key("btn_f6", nullptr);
    jb.code = 64;
    SimInput jaxis = key("abs_y", nullptr);
    jaxis.kind = SIM_IN_AXIS;
    jaxis.code = 1;
    SimInput gsouth = key("btn_south", nullptr);
    gsouth.code = 0x130;
    const SimInput jin[] = { jb, jaxis };
    const SimBinding jbind[] = { bind(&jin[0]), bind(&jin[1]) };
    const SimInput gin[] = { gsouth };
    const SimBinding gbind[] = { bind(&gin[0]) };
    const SimPortDevice joy[] = { device(SIM_SRC_SDL_JOYSTICK, jin, 2, jbind, 2),
                                  device(SIM_SRC_SDL_GAMEPAD, gin, 1, gbind, 1) };
    ports[0].ndevices = 2;
    ports[0].devices = joy;
    plan.action[0] = SIM_PLAN_ATTACH;
    n = sim_boot_bound_keys(ports, &plan, held);
    expect("E13 a joystick-declared unit's button on KEY_F6: its hotkey stands down", sim_boot_hotkey_held(held, SDL_F6));
    expect("E14 its axis on ABS_Y (KEY_ESC's number): not a key", !sim_boot_hotkey_held(held, 41));
    expect("E15 a gamepad's BTN_SOUTH: not a key; one key in all", n == 1);

    printf("%d checks, %d failed\n", nchecks, nfailed);
    return nfailed ? 1 : 0;
}
