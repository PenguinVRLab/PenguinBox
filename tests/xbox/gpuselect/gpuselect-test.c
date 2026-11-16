/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>
#include "gpu_select.h"

static int fails, checks;

static void expect(const char *what, bool ok)
{
    checks++;
    fails += !ok;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static GpuSelectDevice D(const char *name, GpuSelectType type,
                         GpuSelectDriver driver, uint8_t uuid_byte,
                         uint32_t vendor, uint32_t device, int bus)
{
    GpuSelectDevice d;
    memset(&d, 0, sizeof(d));
    snprintf(d.name, sizeof(d.name), "%s", name);
    d.type = type;
    d.driver = driver;
    d.vendor_id = vendor;
    d.device_id = device;
    if (uuid_byte) {
        memset(d.uuid, uuid_byte, sizeof(d.uuid));
        d.uuid[0] = 0x10;
        d.has_uuid = true;
    }
    if (bus >= 0) {
        d.has_pci = true;
        d.pci_bus = (uint32_t)bus;
        d.pci_device = bus == 0 ? 2 : 0;
    }
    d.usable = true;
    return d;
}

#define INTEL "Intel(R) HD Graphics 4600 (HSW GT2)"
#define NVIDIA "NVIDIA GeForce GTX 965M"
#define SOFT "llvmpipe (LLVM 21.1.8, 256 bits)"

static int laptop(GpuSelectDevice *d)
{
    d[0] = D(INTEL, GPU_SELECT_TYPE_INTEGRATED, GPU_SELECT_DRIVER_OTHER, 0xa1,
             0x8086, 0x0416, 0);
    d[1] = D(NVIDIA, GPU_SELECT_TYPE_DISCRETE, GPU_SELECT_DRIVER_NVIDIA, 0xb2,
             0x10de, 0x1427, 1);
    d[2] = D(SOFT, GPU_SELECT_TYPE_CPU, GPU_SELECT_DRIVER_OTHER, 0xc3, 0x10005,
             0, -1);
    return 3;
}

static void hex_of(const GpuSelectDevice *d, char out[GPU_SELECT_UUID_HEX_SIZE])
{
    gpu_select_uuid_to_hex(d->uuid, out);
}

static const GpuSelectEnvOp *op(const GpuSelectEnvOp *ops, int n,
                                const char *name)
{
    for (int i = 0; i < n; i++) {
        if (!strcmp(ops[i].name, name)) {
            return &ops[i];
        }
    }
    return NULL;
}

static bool op_set(const GpuSelectEnvOp *ops, int n, const char *name,
                   const char *value)
{
    const GpuSelectEnvOp *o = op(ops, n, name);
    return o && o->set && !strcmp(o->value, value);
}

static bool op_unset(const GpuSelectEnvOp *ops, int n, const char *name)
{
    const GpuSelectEnvOp *o = op(ops, n, name);
    return o && !o->set;
}

static void test_automatic(void)
{
    GpuSelectDevice d[4];
    int n = laptop(d);
    GpuSelectChoice none = { NULL, NULL, NULL };
    GpuSelectPick p = gpu_select_pick(d, n, &none, NULL);
    expect("automatic: the dedicated GPU, though the integrated one is listed first",
           p.index == 1 && p.wanted == 1);
    expect("automatic: the reason is the class", p.reason == GPU_SELECT_AUTO_CLASS);
    expect("automatic: nothing is missing, OpenGL unknown",
           !p.choice_missing && p.gl == GPU_SELECT_GL_UNKNOWN && !p.followed_gl);

    GpuSelectChoice empty = { "", "", "" };
    p = gpu_select_pick(d, n, &empty, NULL);
    expect("automatic: empty strings are no choice", p.index == 1 && !p.choice_missing);
    p = gpu_select_pick(d, n, NULL, NULL);
    expect("automatic: a NULL choice is no choice", p.index == 1 && !p.choice_missing);

    GpuSelectChoice old = { NULL, NULL, INTEL };
    p = gpu_select_pick(d, n, &old, NULL);
    expect("automatic: the last-used integrated GPU does not beat a dedicated one",
           p.index == 1 && p.reason == GPU_SELECT_AUTO_CLASS);
    GpuSelectChoice old2 = { NULL, NULL, NVIDIA };
    p = gpu_select_pick(d, n, &old2, NULL);
    expect("automatic: the last-used dedicated GPU is the pick and the reason says so",
           p.index == 1 && p.reason == GPU_SELECT_AUTO_LAST_USED);

    d[1].usable = false;
    p = gpu_select_pick(d, n, &none, NULL);
    expect("automatic: integrated before software when the dedicated GPU is unusable",
           p.index == 0);
    d[0].type = GPU_SELECT_TYPE_OTHER;
    d[2].type = GPU_SELECT_TYPE_VIRTUAL;
    p = gpu_select_pick(d, n, &none, NULL);
    expect("automatic: virtual before other", p.index == 2);
    d[2].type = GPU_SELECT_TYPE_CPU;
    p = gpu_select_pick(d, n, &none, NULL);
    expect("automatic: other before software", p.index == 0);
    d[0].type = GPU_SELECT_TYPE_INTEGRATED;
    d[2].type = GPU_SELECT_TYPE_VIRTUAL;
    p = gpu_select_pick(d, n, &none, NULL);
    expect("automatic: integrated before virtual", p.index == 0);

    d[0].usable = false;
    d[2].usable = false;
    p = gpu_select_pick(d, n, &none, NULL);
    expect("no usable device: no pick", p.index == -1 && p.wanted == -1 &&
           p.reason == GPU_SELECT_NONE);
    p = gpu_select_pick(d, 0, &none, NULL);
    expect("an empty list: no pick", p.index == -1 && p.reason == GPU_SELECT_NONE);
}

static void test_two_cards(void)
{
    GpuSelectDevice d[3];
    d[0] = D("AMD Radeon RX 6600 (RADV NAVI23)", GPU_SELECT_TYPE_DISCRETE,
             GPU_SELECT_DRIVER_OTHER, 0x11, 0x1002, 0x73ff, 3);
    d[1] = D("AMD Radeon RX 6600 (RADV NAVI23)", GPU_SELECT_TYPE_DISCRETE,
             GPU_SELECT_DRIVER_OTHER, 0x22, 0x1002, 0x73ff, 4);
    d[2] = D("AMD Radeon Graphics (RADV RENOIR)", GPU_SELECT_TYPE_INTEGRATED,
             GPU_SELECT_DRIVER_OTHER, 0x33, 0x1002, 0x1636, 0);
    char hex[GPU_SELECT_UUID_HEX_SIZE];

    GpuSelectChoice none = { NULL, NULL, NULL };
    GpuSelectPick p = gpu_select_pick(d, 3, &none, NULL);
    expect("two cards of one class: the first listed", p.index == 0 &&
           p.reason == GPU_SELECT_AUTO_CLASS);

    hex_of(&d[1], hex);
    GpuSelectChoice second = { hex, d[1].name, NULL };
    p = gpu_select_pick(d, 3, &second, NULL);
    expect("two identical cards: the UUID picks the second, not the first of that name",
           p.index == 1 && p.reason == GPU_SELECT_CHOSEN_UUID && !p.choice_missing);

    d[1] = D("AMD Radeon RX 7900 XT (RADV NAVI31)", GPU_SELECT_TYPE_DISCRETE,
             GPU_SELECT_DRIVER_OTHER, 0x22, 0x1002, 0x744c, 4);
    GpuSelectChoice last = { NULL, NULL, d[1].name };
    p = gpu_select_pick(d, 3, &last, NULL);
    expect("automatic: the last-used card inside the best class", p.index == 1 &&
           p.reason == GPU_SELECT_AUTO_LAST_USED);
    d[1].usable = false;
    p = gpu_select_pick(d, 3, &last, NULL);
    expect("automatic: an unusable last-used card is not picked", p.index == 0 &&
           p.reason == GPU_SELECT_AUTO_CLASS);
}

static void test_choice(void)
{
    GpuSelectDevice d[4];
    int n = laptop(d);
    char hex[GPU_SELECT_UUID_HEX_SIZE];

    hex_of(&d[0], hex);
    GpuSelectChoice c = { hex, INTEL, NVIDIA };
    GpuSelectPick p = gpu_select_pick(d, n, &c, NULL);
    expect("choice: the chosen integrated GPU, not the default dedicated one",
           p.index == 0 && p.wanted == 0 && p.reason == GPU_SELECT_CHOSEN_UUID);
    expect("choice: found, so not missing", !p.choice_missing);

    GpuSelectChoice renamed = { hex, "an older name of the same device", NULL };
    p = gpu_select_pick(d, n, &renamed, NULL);
    expect("choice: a renamed device is found by its UUID", p.index == 0 &&
           p.reason == GPU_SELECT_CHOSEN_UUID);
    GpuSelectChoice cross = { hex, NVIDIA, NULL };
    p = gpu_select_pick(d, n, &cross, NULL);
    expect("choice: the UUID wins over a name that says another device", p.index == 0);

    GpuSelectChoice moved = { "00112233445566778899aabbccddeeff", INTEL, NULL };
    p = gpu_select_pick(d, n, &moved, NULL);
    expect("choice: no such UUID, the saved name finds the device", p.index == 0 &&
           p.reason == GPU_SELECT_CHOSEN_NAME && !p.choice_missing);
    GpuSelectChoice byname = { "", INTEL, NULL };
    p = gpu_select_pick(d, n, &byname, NULL);
    expect("choice: a name alone is a choice", p.index == 0 &&
           p.reason == GPU_SELECT_CHOSEN_NAME);
    GpuSelectChoice bad = { "not-a-uuid", INTEL, NULL };
    p = gpu_select_pick(d, n, &bad, NULL);
    expect("choice: a malformed UUID falls to the name", p.index == 0 &&
           p.reason == GPU_SELECT_CHOSEN_NAME);

    d[2].has_uuid = false;
    memset(d[2].uuid, 0, sizeof(d[2].uuid));
    GpuSelectChoice zero = { "00000000000000000000000000000000", "", NULL };
    p = gpu_select_pick(d, n, &zero, NULL);
    expect("choice: a zero UUID matches no device", p.index == 1 && p.choice_missing);
}

static void test_missing(void)
{
    GpuSelectDevice d[4];
    int n = laptop(d);

    GpuSelectChoice gone = { "00112233445566778899aabbccddeeff",
                             "NVIDIA GeForce RTX 4090", INTEL };
    GpuSelectPick p = gpu_select_pick(d, n, &gone, NULL);
    expect("missing: the pick falls to the automatic rule", p.index == 1 &&
           p.reason == GPU_SELECT_AUTO_CLASS);
    expect("missing: the result says the choice is missing", p.choice_missing);

    char hex[GPU_SELECT_UUID_HEX_SIZE];
    hex_of(&d[1], hex);
    d[1].usable = false;
    GpuSelectChoice unusable = { hex, NVIDIA, NULL };
    p = gpu_select_pick(d, n, &unusable, NULL);
    expect("missing: a chosen device that is unusable is not picked", p.index == 0);
    expect("missing: and it is reported missing", p.choice_missing);

    GpuSelectChoice none = { NULL, NULL, "a device that is gone" };
    p = gpu_select_pick(d, n, &none, NULL);
    expect("missing: a last-used name that is gone is not a missing choice",
           !p.choice_missing && p.index == 0);
}

static void test_gl(void)
{
    GpuSelectDevice d[4];
    int n = laptop(d);
    GpuSelectChoice none = { NULL, NULL, NULL };

    GpuSelectPick p = gpu_select_pick(d, n, &none, d[1].uuid);
    expect("gl: OpenGL on the pick is a match", p.gl == GPU_SELECT_GL_MATCH &&
           p.index == 1 && !p.followed_gl);

    p = gpu_select_pick(d, n, &none, d[0].uuid);
    expect("gl: OpenGL on another device is reported as a mismatch",
           p.gl == GPU_SELECT_GL_MISMATCH);
    expect("gl: the device OpenGL is on is used, and the result says so",
           p.index == 0 && p.followed_gl);
    expect("gl: what was asked for is kept beside it", p.wanted == 1 &&
           p.reason == GPU_SELECT_AUTO_CLASS);

    d[0].usable = false;
    p = gpu_select_pick(d, n, &none, d[0].uuid);
    expect("gl: a mismatch with an unusable OpenGL device keeps the pick",
           p.gl == GPU_SELECT_GL_MISMATCH && p.index == 1 && !p.followed_gl);
    d[0].usable = true;

    uint8_t other[GPU_SELECT_UUID_SIZE];
    memset(other, 0x77, sizeof(other));
    p = gpu_select_pick(d, n, &none, other);
    expect("gl: a mismatch with an unlisted OpenGL device keeps the pick",
           p.gl == GPU_SELECT_GL_MISMATCH && p.index == 1 && !p.followed_gl);

    uint8_t zero[GPU_SELECT_UUID_SIZE] = { 0 };
    p = gpu_select_pick(d, n, &none, zero);
    expect("gl: a zero OpenGL UUID is unknown, not a mismatch",
           p.gl == GPU_SELECT_GL_UNKNOWN && p.index == 1 && !p.followed_gl);
    d[1].has_uuid = false;
    p = gpu_select_pick(d, n, &none, d[0].uuid);
    expect("gl: a pick with no UUID is unknown, not a mismatch",
           p.gl == GPU_SELECT_GL_UNKNOWN && p.index == 1 && !p.followed_gl);
    d[1].has_uuid = true;

    char hex[GPU_SELECT_UUID_HEX_SIZE];
    hex_of(&d[1], hex);
    GpuSelectChoice c = { hex, NVIDIA, NULL };
    p = gpu_select_pick(d, n, &c, d[0].uuid);
    expect("gl: a chosen device with OpenGL elsewhere is a mismatch too",
           p.gl == GPU_SELECT_GL_MISMATCH && p.wanted == 1 && p.index == 0 &&
           p.reason == GPU_SELECT_CHOSEN_UUID);
}

static void test_env(void)
{
    GpuSelectDevice d[4];
    int n = laptop(d);
    GpuSelectEnvOp ops[GPU_SELECT_MAX_ENV_OPS];

    expect("env: two hardware GPUs on the laptop (the software device not counted)",
           gpu_select_hardware_count(d, n) == 2);

    int k = gpu_select_env_plan(d, n, 1, ops, GPU_SELECT_MAX_ENV_OPS);
    expect("env: NVIDIA: four operations", k == 4);
    expect("env: NVIDIA: the offload variable set",
           op_set(ops, k, "__NV_PRIME_RENDER_OFFLOAD", "1"));
    expect("env: NVIDIA: the GLX vendor set",
           op_set(ops, k, "__GLX_VENDOR_LIBRARY_NAME", "nvidia"));
    expect("env: NVIDIA: DRI_PRIME removed", op_unset(ops, k, "DRI_PRIME"));
    expect("env: NVIDIA: the device filter removed",
           op_unset(ops, k, "__VK_LAYER_NV_optimus"));

    k = gpu_select_env_plan(d, n, 0, ops, GPU_SELECT_MAX_ENV_OPS);
    expect("env: Mesa: four operations", k == 4);
    expect("env: Mesa: the offload variable removed",
           op_unset(ops, k, "__NV_PRIME_RENDER_OFFLOAD"));
    expect("env: Mesa: the GLX vendor removed",
           op_unset(ops, k, "__GLX_VENDOR_LIBRARY_NAME"));
    expect("env: Mesa: DRI_PRIME names the PCI address",
           op_set(ops, k, "DRI_PRIME", "pci-0000_00_02_0"));
    expect("env: Mesa: the device filter removed",
           op_unset(ops, k, "__VK_LAYER_NV_optimus"));

    d[0].pci_domain = 0x1;
    d[0].pci_bus = 0xc1;
    d[0].pci_device = 0x1f;
    d[0].pci_function = 7;
    k = gpu_select_env_plan(d, n, 0, ops, GPU_SELECT_MAX_ENV_OPS);
    expect("env: Mesa: every part of the PCI address, in hex",
           op_set(ops, k, "DRI_PRIME", "pci-0001_c1_1f_7"));
    d[0].has_pci = false;
    k = gpu_select_env_plan(d, n, 0, ops, GPU_SELECT_MAX_ENV_OPS);
    expect("env: Mesa: no PCI address, the vendor and device ids",
           op_set(ops, k, "DRI_PRIME", "8086:0416"));

    expect("env: a software device changes nothing",
           gpu_select_env_plan(d, n, 2, ops, GPU_SELECT_MAX_ENV_OPS) == 0);
    expect("env: no pick changes nothing",
           gpu_select_env_plan(d, n, -1, ops, GPU_SELECT_MAX_ENV_OPS) == 0 &&
           gpu_select_env_plan(d, n, n, ops, GPU_SELECT_MAX_ENV_OPS) == 0);
    expect("env: too little room changes nothing",
           gpu_select_env_plan(d, n, 1, ops, 3) == 0);

    d[1].driver = GPU_SELECT_DRIVER_OTHER;
    k = gpu_select_env_plan(d, n, 1, ops, GPU_SELECT_MAX_ENV_OPS);
    expect("env: an NVIDIA card on Mesa's driver gets DRI_PRIME",
           op_set(ops, k, "DRI_PRIME", "pci-0000_01_00_0") &&
           op_unset(ops, k, "__NV_PRIME_RENDER_OFFLOAD"));
}

static void test_one_gpu(void)
{
    GpuSelectDevice d[2];
    d[0] = D("AMD Custom GPU 0405 (RADV VANGOGH)", GPU_SELECT_TYPE_INTEGRATED,
             GPU_SELECT_DRIVER_OTHER, 0x44, 0x1002, 0x163f, 4);
    d[1] = D(SOFT, GPU_SELECT_TYPE_CPU, GPU_SELECT_DRIVER_OTHER, 0xc3, 0x10005,
             0, -1);
    GpuSelectEnvOp ops[GPU_SELECT_MAX_ENV_OPS];
    GpuSelectChoice old = { NULL, NULL, d[0].name };

    GpuSelectPick p = gpu_select_pick(d, 2, &old, NULL);
    expect("one GPU: it is the pick", p.index == 0 && !p.choice_missing);
    expect("one GPU: one hardware GPU counted", gpu_select_hardware_count(d, 2) == 1);
    expect("one GPU: the environment is not touched",
           gpu_select_env_plan(d, 2, 0, ops, GPU_SELECT_MAX_ENV_OPS) == 0);

    d[0] = D(NVIDIA, GPU_SELECT_TYPE_DISCRETE, GPU_SELECT_DRIVER_NVIDIA, 0xb2,
             0x10de, 0x1427, 1);
    expect("one GPU: an NVIDIA card alone is not touched",
           gpu_select_env_plan(d, 2, 0, ops, GPU_SELECT_MAX_ENV_OPS) == 0);

    d[1] = D("NVIDIA GeForce GTX 965M (NVK)", GPU_SELECT_TYPE_DISCRETE,
             GPU_SELECT_DRIVER_OTHER, 0xb3, 0x10de, 0x1427, 1);
    expect("one GPU: two entries at one PCI address are one GPU",
           gpu_select_hardware_count(d, 2) == 1);
    d[1].pci_function = 1;
    expect("one GPU: another function is another GPU",
           gpu_select_hardware_count(d, 2) == 2);
    d[1].pci_function = 0;
    d[1].has_pci = false;
    expect("one GPU: an entry with no PCI address is counted",
           gpu_select_hardware_count(d, 2) == 2);
}

static void test_vr(void)
{
    GpuSelectDevice d[4];
    int n = laptop(d);
    char hex[GPU_SELECT_UUID_HEX_SIZE];
    bool aside = false;

    hex_of(&d[0], hex);
    GpuSelectChoice igpu = { hex, INTEL, NVIDIA };
    GpuSelectChoice c = gpu_select_choice_for(&igpu, true, &aside);
    GpuSelectPick p = gpu_select_pick(d, n, &c, NULL);
    expect("vr: a saved integrated GPU is set aside for the automatic dedicated GPU",
           p.index == 1 && p.wanted == 1);
    expect("vr: the result says a choice was set aside", aside);
    expect("vr: the last-used name still decides inside the class",
           p.reason == GPU_SELECT_AUTO_LAST_USED);
    expect("vr: a choice set aside is not reported missing", !p.choice_missing);

    aside = true;
    c = gpu_select_choice_for(&igpu, false, &aside);
    p = gpu_select_pick(d, n, &c, NULL);
    expect("vr: without VR the saved choice is the pick", p.index == 0 &&
           p.reason == GPU_SELECT_CHOSEN_UUID);
    expect("vr: without VR nothing is set aside", !aside);

    GpuSelectChoice byname = { "", INTEL, NULL };
    aside = false;
    c = gpu_select_choice_for(&byname, true, &aside);
    p = gpu_select_pick(d, n, &c, NULL);
    expect("vr: a choice by name is set aside", p.index == 1 && aside);

    GpuSelectChoice none = { NULL, NULL, NVIDIA };
    aside = true;
    c = gpu_select_choice_for(&none, true, &aside);
    p = gpu_select_pick(d, n, &c, NULL);
    expect("vr: with no saved choice nothing is set aside", !aside && p.index == 1);
    GpuSelectChoice empty = { "", "", "" };
    aside = true;
    c = gpu_select_choice_for(&empty, true, &aside);
    expect("vr: empty strings are no saved choice", !aside);

    GpuSelectChoice gone = { "00112233445566778899aabbccddeeff",
                             "NVIDIA GeForce RTX 4090", NULL };
    aside = false;
    c = gpu_select_choice_for(&gone, true, &aside);
    p = gpu_select_pick(d, n, &c, NULL);
    expect("vr: a saved device that is gone is set aside, not reported missing",
           aside && !p.choice_missing && p.index == 1);

    c = gpu_select_choice_for(NULL, true, NULL);
    p = gpu_select_pick(d, n, &c, NULL);
    expect("vr: a NULL choice and a NULL flag are accepted", p.index == 1);
}

static void test_vr_gate(void)
{
    GpuSelectDevice d[4];
    laptop(d);
    uint8_t zero[GPU_SELECT_UUID_SIZE] = { 0 };
    uint8_t near[GPU_SELECT_UUID_SIZE];

    expect("vr gate: the runtime on the device OpenGL is on is allowed",
           gpu_select_vr_gate(d[1].uuid, d[1].uuid) == GPU_SELECT_VR_ALLOW);
    expect("vr gate: the runtime on another device is refused",
           gpu_select_vr_gate(d[1].uuid, d[0].uuid) == GPU_SELECT_VR_REFUSE &&
           gpu_select_vr_gate(d[0].uuid, d[1].uuid) == GPU_SELECT_VR_REFUSE);
    memcpy(near, d[1].uuid, sizeof(near));
    near[GPU_SELECT_UUID_SIZE - 1] ^= 1;
    expect("vr gate: a UUID that differs in its last byte is refused",
           gpu_select_vr_gate(d[1].uuid, near) == GPU_SELECT_VR_REFUSE);
    expect("vr gate: an unknown OpenGL device gives no verdict",
           gpu_select_vr_gate(d[1].uuid, NULL) == GPU_SELECT_VR_UNKNOWN &&
           gpu_select_vr_gate(d[1].uuid, zero) == GPU_SELECT_VR_UNKNOWN);
    expect("vr gate: an unknown runtime device gives no verdict",
           gpu_select_vr_gate(NULL, d[1].uuid) == GPU_SELECT_VR_UNKNOWN &&
           gpu_select_vr_gate(zero, d[1].uuid) == GPU_SELECT_VR_UNKNOWN);
    expect("vr gate: two unknown devices are not a match",
           gpu_select_vr_gate(zero, zero) == GPU_SELECT_VR_UNKNOWN &&
           gpu_select_vr_gate(NULL, NULL) == GPU_SELECT_VR_UNKNOWN);
}

static void test_dedup(void)
{
    GpuSelectDevice d[8];
    int map[8];
    laptop(d);
    laptop(d + 3);
    int n = gpu_select_dedup(d, 6, map);
    expect("dedup: six entries for three devices are three", n == 3);
    expect("dedup: the order of the first entries is kept",
           !strcmp(d[0].name, INTEL) && !strcmp(d[1].name, NVIDIA) &&
           !strcmp(d[2].name, SOFT));
    expect("dedup: the map names the entries kept", map[0] == 0 && map[1] == 1 &&
           map[2] == 2);
    GpuSelectChoice none = { NULL, NULL, NULL };
    GpuSelectPick p = gpu_select_pick(d, n, &none, NULL);
    expect("dedup: the pick over the list is the dedicated GPU, once",
           p.index == 1 && gpu_select_hardware_count(d, n) == 2);

    laptop(d);
    laptop(d + 3);
    d[1].usable = false;
    n = gpu_select_dedup(d, 6, map);
    expect("dedup: the usable entry of a device is the one kept",
           n == 3 && d[1].usable && map[1] == 4 && !strcmp(d[1].name, NVIDIA));
    laptop(d);
    laptop(d + 3);
    d[4].usable = false;
    n = gpu_select_dedup(d, 6, map);
    expect("dedup: a usable first entry is not replaced by an unusable second",
           n == 3 && d[1].usable && map[1] == 1);

    laptop(d);
    laptop(d + 3);
    expect("dedup: no map is needed", gpu_select_dedup(d, 6, NULL) == 3);

    laptop(d);
    expect("dedup: three devices stay three", gpu_select_dedup(d, 3, map) == 3 &&
           map[2] == 2);
    expect("dedup: an empty list stays empty", gpu_select_dedup(d, 0, map) == 0);
    laptop(d);
    laptop(d + 3);
    d[2].has_uuid = false;
    d[5].has_uuid = false;
    n = gpu_select_dedup(d, 6, map);
    expect("dedup: entries without a UUID are all kept",
           n == 4 && map[2] == 2 && map[3] == 5 && !strcmp(d[3].name, SOFT));
    laptop(d);
    d[2] = d[1];
    d[2].uuid[5] ^= 0x40;
    expect("dedup: two cards of one name and two UUIDs stay two",
           gpu_select_dedup(d, 3, map) == 3);
    laptop(d);
    laptop(d + 3);
    d[3].uuid[15] ^= 1;
    expect("dedup: a UUID that differs in its last byte is another device",
           gpu_select_dedup(d, 6, map) == 4 && map[3] == 3);
}

static void test_text(void)
{
    uint8_t u[GPU_SELECT_UUID_SIZE], v[GPU_SELECT_UUID_SIZE];
    char hex[GPU_SELECT_UUID_HEX_SIZE];
    for (int i = 0; i < GPU_SELECT_UUID_SIZE; i++) {
        u[i] = (uint8_t)(0x5c + i * 0x1b);
    }
    gpu_select_uuid_to_hex(u, hex);
    expect("uuid: 32 lower-case digits, most significant byte first",
           !strcmp(hex, "5c7792adc8e3fe19344f6a85a0bbd6f1"));
    expect("uuid: back from text", gpu_select_uuid_from_hex(hex, v) &&
           !memcmp(u, v, sizeof(u)));
    expect("uuid: dashes and upper case accepted",
           gpu_select_uuid_from_hex("5C7792AD-C8E3-FE19-344F-6A85A0BBD6F1", v) &&
           !memcmp(u, v, sizeof(u)));
    expect("uuid: too short refused",
           !gpu_select_uuid_from_hex("5c7792adc8e3fe19344f6a85a0bbd6f", v));
    expect("uuid: too long refused",
           !gpu_select_uuid_from_hex("5c7792adc8e3fe19344f6a85a0bbd6f100", v));
    expect("uuid: a letter that is not hex refused",
           !gpu_select_uuid_from_hex("5c7792adc8e3fe19344f6a85a0bbd6fg", v));
    expect("uuid: NULL and empty refused", !gpu_select_uuid_from_hex(NULL, v) &&
           !gpu_select_uuid_from_hex("", v));
    memset(v, 0, sizeof(v));
    expect("uuid: zero is zero", gpu_select_uuid_is_zero(v));
    v[15] = 1;
    expect("uuid: the last byte counts", !gpu_select_uuid_is_zero(v));

    GpuSelectDevice d[4];
    laptop(d);
    char line[256];
    d[1].memory_bytes = 4096ull << 20;
    snprintf(d[1].driver_text, sizeof(d[1].driver_text), "NVIDIA 580.178.04");
    gpu_select_describe(&d[1], line, sizeof(line));
    expect("describe: name, type, memory, driver",
           !strcmp(line, NVIDIA " - dedicated, 4096 MiB, NVIDIA 580.178.04"));
    gpu_select_describe(&d[0], line, sizeof(line));
    expect("describe: no memory figure and no driver text when there is none",
           !strcmp(line, INTEL " - integrated"));
    expect("type: the five names",
           !strcmp(gpu_select_type_text(GPU_SELECT_TYPE_DISCRETE), "dedicated") &&
           !strcmp(gpu_select_type_text(GPU_SELECT_TYPE_INTEGRATED), "integrated") &&
           !strcmp(gpu_select_type_text(GPU_SELECT_TYPE_VIRTUAL), "virtual") &&
           !strcmp(gpu_select_type_text(GPU_SELECT_TYPE_CPU), "software") &&
           !strcmp(gpu_select_type_text(GPU_SELECT_TYPE_OTHER), "other"));
    expect("reason: each reason has its own text",
           strcmp(gpu_select_reason_text(GPU_SELECT_CHOSEN_UUID),
                  gpu_select_reason_text(GPU_SELECT_CHOSEN_NAME)) &&
           strcmp(gpu_select_reason_text(GPU_SELECT_AUTO_CLASS),
                  gpu_select_reason_text(GPU_SELECT_AUTO_LAST_USED)) &&
           strcmp(gpu_select_reason_text(GPU_SELECT_NONE),
                  gpu_select_reason_text(GPU_SELECT_AUTO_CLASS)));
}

int main(void)
{
    test_automatic();
    test_two_cards();
    test_choice();
    test_missing();
    test_gl();
    test_env();
    test_one_gpu();
    test_vr();
    test_vr_gate();
    test_dedup();
    test_text();
    printf("gpuselect: %d of %d checks pass\n", checks - fails, checks);
    return fails ? 1 : 0;
}
