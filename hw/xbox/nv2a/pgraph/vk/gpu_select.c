/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include "gpu_select.h"

#include <stdio.h>
#include <string.h>

bool gpu_select_uuid_is_zero(const uint8_t uuid[GPU_SELECT_UUID_SIZE])
{
    for (int i = 0; i < GPU_SELECT_UUID_SIZE; i++) {
        if (uuid[i]) {
            return false;
        }
    }
    return true;
}

void gpu_select_uuid_to_hex(const uint8_t uuid[GPU_SELECT_UUID_SIZE],
                            char out[GPU_SELECT_UUID_HEX_SIZE])
{
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < GPU_SELECT_UUID_SIZE; i++) {
        out[i * 2] = digits[uuid[i] >> 4];
        out[i * 2 + 1] = digits[uuid[i] & 15];
    }
    out[GPU_SELECT_UUID_SIZE * 2] = '\0';
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool gpu_select_uuid_from_hex(const char *hex,
                              uint8_t out[GPU_SELECT_UUID_SIZE])
{
    int n = 0;
    if (!hex) {
        return false;
    }
    for (const char *p = hex; *p; p++) {
        if (*p == '-') {
            continue;
        }
        int d = hex_digit(*p);
        if (d < 0 || n >= GPU_SELECT_UUID_SIZE * 2) {
            return false;
        }
        if (n % 2 == 0) {
            out[n / 2] = (uint8_t)(d << 4);
        } else {
            out[n / 2] |= (uint8_t)d;
        }
        n++;
    }
    return n == GPU_SELECT_UUID_SIZE * 2;
}

static int find_by_uuid(const GpuSelectDevice *dev, int n,
                        const uint8_t uuid[GPU_SELECT_UUID_SIZE])
{
    if (gpu_select_uuid_is_zero(uuid)) {
        return -1;
    }
    for (int i = 0; i < n; i++) {
        if (dev[i].usable && dev[i].has_uuid &&
            !memcmp(dev[i].uuid, uuid, GPU_SELECT_UUID_SIZE)) {
            return i;
        }
    }
    return -1;
}

static int find_by_name(const GpuSelectDevice *dev, int n, const char *name)
{
    if (!name || !name[0]) {
        return -1;
    }
    for (int i = 0; i < n; i++) {
        if (dev[i].usable && !strcmp(dev[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static int class_rank(GpuSelectType type)
{
    switch (type) {
    case GPU_SELECT_TYPE_DISCRETE:
        return 4;
    case GPU_SELECT_TYPE_INTEGRATED:
        return 3;
    case GPU_SELECT_TYPE_VIRTUAL:
        return 2;
    case GPU_SELECT_TYPE_CPU:
        return 0;
    case GPU_SELECT_TYPE_OTHER:
    default:
        return 1;
    }
}

static int pick_automatic(const GpuSelectDevice *dev, int n,
                          const char *last_name, GpuSelectReason *reason)
{
    int best = -1;
    for (int i = 0; i < n; i++) {
        if (!dev[i].usable) {
            continue;
        }
        if (best < 0 || class_rank(dev[i].type) > class_rank(dev[best].type)) {
            best = i;
        }
    }
    if (best < 0) {
        *reason = GPU_SELECT_NONE;
        return -1;
    }
    *reason = GPU_SELECT_AUTO_CLASS;
    if (last_name && last_name[0]) {
        for (int i = 0; i < n; i++) {
            if (dev[i].usable &&
                class_rank(dev[i].type) == class_rank(dev[best].type) &&
                !strcmp(dev[i].name, last_name)) {
                *reason = GPU_SELECT_AUTO_LAST_USED;
                return i;
            }
        }
    }
    return best;
}

GpuSelectChoice gpu_select_choice_for(const GpuSelectChoice *saved, bool vr,
                                      bool *set_aside)
{
    GpuSelectChoice c = { NULL, NULL, NULL };
    bool aside = false;
    if (saved) {
        c = *saved;
    }
    if (vr) {
        aside = (c.uuid_hex && c.uuid_hex[0]) || (c.name && c.name[0]);
        c.uuid_hex = NULL;
        c.name = NULL;
    }
    if (set_aside) {
        *set_aside = aside;
    }
    return c;
}

GpuSelectVrGate gpu_select_vr_gate(const uint8_t *runtime_uuid,
                                   const uint8_t *gl_uuid)
{
    if (!runtime_uuid || !gl_uuid) {
        return GPU_SELECT_VR_UNKNOWN;
    }
    if (gpu_select_uuid_is_zero(runtime_uuid) ||
        gpu_select_uuid_is_zero(gl_uuid)) {
        return GPU_SELECT_VR_UNKNOWN;
    }
    return memcmp(runtime_uuid, gl_uuid, GPU_SELECT_UUID_SIZE) ?
               GPU_SELECT_VR_REFUSE :
               GPU_SELECT_VR_ALLOW;
}

GpuSelectPick gpu_select_pick(const GpuSelectDevice *dev, int n,
                              const GpuSelectChoice *choice,
                              const uint8_t *gl_uuid)
{
    GpuSelectPick p = {
        .index = -1,
        .wanted = -1,
        .reason = GPU_SELECT_NONE,
        .choice_missing = false,
        .gl = GPU_SELECT_GL_UNKNOWN,
        .followed_gl = false,
    };
    const char *uuid_hex = choice ? choice->uuid_hex : NULL;
    const char *name = choice ? choice->name : NULL;
    const char *last_name = choice ? choice->last_name : NULL;
    bool have_choice = (uuid_hex && uuid_hex[0]) || (name && name[0]);

    if (have_choice) {
        uint8_t uuid[GPU_SELECT_UUID_SIZE];
        if (gpu_select_uuid_from_hex(uuid_hex, uuid)) {
            p.wanted = find_by_uuid(dev, n, uuid);
            if (p.wanted >= 0) {
                p.reason = GPU_SELECT_CHOSEN_UUID;
            }
        }
        if (p.wanted < 0) {
            p.wanted = find_by_name(dev, n, name);
            if (p.wanted >= 0) {
                p.reason = GPU_SELECT_CHOSEN_NAME;
            }
        }
        if (p.wanted < 0) {
            p.choice_missing = true;
        }
    }

    if (p.wanted < 0) {
        p.wanted = pick_automatic(dev, n, last_name, &p.reason);
    }
    p.index = p.wanted;
    if (p.wanted < 0) {
        return p;
    }

    if (gl_uuid && !gpu_select_uuid_is_zero(gl_uuid) && dev[p.wanted].has_uuid) {
        if (!memcmp(dev[p.wanted].uuid, gl_uuid, GPU_SELECT_UUID_SIZE)) {
            p.gl = GPU_SELECT_GL_MATCH;
        } else {
            p.gl = GPU_SELECT_GL_MISMATCH;
            int on_gl = find_by_uuid(dev, n, gl_uuid);
            if (on_gl >= 0) {
                p.index = on_gl;
                p.followed_gl = true;
            }
        }
    }
    return p;
}

int gpu_select_dedup(GpuSelectDevice *dev, int n, int *map)
{
    int out = 0;
    for (int i = 0; i < n; i++) {
        int same = -1;
        for (int j = 0; j < out && same < 0 && dev[i].has_uuid; j++) {
            if (dev[j].has_uuid &&
                !memcmp(dev[j].uuid, dev[i].uuid, GPU_SELECT_UUID_SIZE)) {
                same = j;
            }
        }
        if (same >= 0) {
            if (!dev[same].usable && dev[i].usable) {
                dev[same] = dev[i];
                if (map) {
                    map[same] = i;
                }
            }
            continue;
        }
        if (out != i) {
            dev[out] = dev[i];
        }
        if (map) {
            map[out] = i;
        }
        out++;
    }
    return out;
}

int gpu_select_hardware_count(const GpuSelectDevice *dev, int n)
{
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (dev[i].type == GPU_SELECT_TYPE_CPU) {
            continue;
        }
        bool seen = false;
        for (int j = 0; j < i && !seen; j++) {
            seen = dev[j].type != GPU_SELECT_TYPE_CPU && dev[i].has_pci &&
                   dev[j].has_pci && dev[i].pci_domain == dev[j].pci_domain &&
                   dev[i].pci_bus == dev[j].pci_bus &&
                   dev[i].pci_device == dev[j].pci_device &&
                   dev[i].pci_function == dev[j].pci_function;
        }
        if (!seen) {
            count++;
        }
    }
    return count;
}

static void env_op(GpuSelectEnvOp *op, const char *name, const char *value)
{
    op->name = name;
    op->set = value != NULL;
    snprintf(op->value, sizeof(op->value), "%s", value ? value : "");
}

int gpu_select_env_plan(const GpuSelectDevice *dev, int n, int pick,
                        GpuSelectEnvOp *ops, int max_ops)
{
    if (pick < 0 || pick >= n || max_ops < GPU_SELECT_MAX_ENV_OPS) {
        return 0;
    }
    if (gpu_select_hardware_count(dev, n) < 2) {
        return 0;
    }
    const GpuSelectDevice *d = &dev[pick];
    if (d->type == GPU_SELECT_TYPE_CPU) {
        return 0;
    }
    if (d->driver == GPU_SELECT_DRIVER_NVIDIA) {
        env_op(&ops[0], "__NV_PRIME_RENDER_OFFLOAD", "1");
        env_op(&ops[1], "__GLX_VENDOR_LIBRARY_NAME", "nvidia");
        env_op(&ops[2], "DRI_PRIME", NULL);
    } else {
        char prime[48];
        if (d->has_pci) {
            snprintf(prime, sizeof(prime), "pci-%04x_%02x_%02x_%x",
                     (unsigned)d->pci_domain, (unsigned)d->pci_bus,
                     (unsigned)d->pci_device, (unsigned)d->pci_function);
        } else {
            snprintf(prime, sizeof(prime), "%04x:%04x", (unsigned)d->vendor_id,
                     (unsigned)d->device_id);
        }
        env_op(&ops[0], "__NV_PRIME_RENDER_OFFLOAD", NULL);
        env_op(&ops[1], "__GLX_VENDOR_LIBRARY_NAME", NULL);
        env_op(&ops[2], "DRI_PRIME", prime);
    }
    env_op(&ops[3], "__VK_LAYER_NV_optimus", NULL);
    return 4;
}

const char *gpu_select_type_text(GpuSelectType type)
{
    switch (type) {
    case GPU_SELECT_TYPE_DISCRETE:
        return "dedicated";
    case GPU_SELECT_TYPE_INTEGRATED:
        return "integrated";
    case GPU_SELECT_TYPE_VIRTUAL:
        return "virtual";
    case GPU_SELECT_TYPE_CPU:
        return "software";
    case GPU_SELECT_TYPE_OTHER:
    default:
        return "other";
    }
}

const char *gpu_select_reason_text(GpuSelectReason reason)
{
    switch (reason) {
    case GPU_SELECT_CHOSEN_UUID:
        return "the player's choice";
    case GPU_SELECT_CHOSEN_NAME:
        return "the player's choice, found by name (its UUID changed)";
    case GPU_SELECT_AUTO_CLASS:
        return "automatic: the best class of GPU present";
    case GPU_SELECT_AUTO_LAST_USED:
        return "automatic: the best class of GPU present, used last";
    case GPU_SELECT_NONE:
    default:
        return "no usable GPU";
    }
}

void gpu_select_describe(const GpuSelectDevice *dev, char *out, size_t size)
{
    char memory[32] = "";
    if (dev->memory_bytes) {
        snprintf(memory, sizeof(memory), ", %llu MiB",
                 (unsigned long long)(dev->memory_bytes >> 20));
    }
    snprintf(out, size, "%s - %s%s%s%s", dev->name,
             gpu_select_type_text(dev->type), memory,
             dev->driver_text[0] ? ", " : "", dev->driver_text);
}
