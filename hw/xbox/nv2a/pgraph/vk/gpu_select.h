/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_GPU_SELECT_H
#define HW_XBOX_NV2A_PGRAPH_VK_GPU_SELECT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GPU_SELECT_UUID_SIZE 16
#define GPU_SELECT_UUID_HEX_SIZE 33
#define GPU_SELECT_MAX_DEVICES 16
#define GPU_SELECT_MAX_ENV_OPS 4

typedef enum GpuSelectType {
    GPU_SELECT_TYPE_OTHER = 0,
    GPU_SELECT_TYPE_INTEGRATED = 1,
    GPU_SELECT_TYPE_DISCRETE = 2,
    GPU_SELECT_TYPE_VIRTUAL = 3,
    GPU_SELECT_TYPE_CPU = 4,
} GpuSelectType;

typedef enum GpuSelectDriver {
    GPU_SELECT_DRIVER_OTHER = 0,
    GPU_SELECT_DRIVER_NVIDIA = 1,
} GpuSelectDriver;

typedef struct GpuSelectDevice {
    char name[256];
    uint8_t uuid[GPU_SELECT_UUID_SIZE];
    bool has_uuid;
    uint32_t vendor_id;
    uint32_t device_id;
    GpuSelectType type;
    GpuSelectDriver driver;
    char driver_text[160];
    uint64_t memory_bytes;
    bool has_pci;
    uint32_t pci_domain;
    uint32_t pci_bus;
    uint32_t pci_device;
    uint32_t pci_function;
    bool usable;
    char why_not[128];
} GpuSelectDevice;

typedef struct GpuSelectChoice {
    const char *uuid_hex;
    const char *name;
    const char *last_name;
} GpuSelectChoice;

typedef enum GpuSelectReason {
    GPU_SELECT_NONE = 0,
    GPU_SELECT_CHOSEN_UUID,
    GPU_SELECT_CHOSEN_NAME,
    GPU_SELECT_AUTO_CLASS,
    GPU_SELECT_AUTO_LAST_USED,
} GpuSelectReason;

typedef enum GpuSelectGl {
    GPU_SELECT_GL_UNKNOWN = 0,
    GPU_SELECT_GL_MATCH,
    GPU_SELECT_GL_MISMATCH,
} GpuSelectGl;

typedef struct GpuSelectPick {
    int index;
    int wanted;
    GpuSelectReason reason;
    bool choice_missing;
    GpuSelectGl gl;
    bool followed_gl;
} GpuSelectPick;

typedef enum GpuSelectVrGate {
    GPU_SELECT_VR_UNKNOWN = 0,
    GPU_SELECT_VR_ALLOW,
    GPU_SELECT_VR_REFUSE,
} GpuSelectVrGate;

typedef struct GpuSelectEnvOp {
    const char *name;
    bool set;
    char value[48];
} GpuSelectEnvOp;

bool gpu_select_uuid_is_zero(const uint8_t uuid[GPU_SELECT_UUID_SIZE]);
void gpu_select_uuid_to_hex(const uint8_t uuid[GPU_SELECT_UUID_SIZE],
                            char out[GPU_SELECT_UUID_HEX_SIZE]);
bool gpu_select_uuid_from_hex(const char *hex,
                              uint8_t out[GPU_SELECT_UUID_SIZE]);

GpuSelectChoice gpu_select_choice_for(const GpuSelectChoice *saved, bool vr,
                                      bool *set_aside);

GpuSelectVrGate gpu_select_vr_gate(const uint8_t *runtime_uuid,
                                   const uint8_t *gl_uuid);

GpuSelectPick gpu_select_pick(const GpuSelectDevice *dev, int n,
                              const GpuSelectChoice *choice,
                              const uint8_t *gl_uuid);

int gpu_select_dedup(GpuSelectDevice *dev, int n, int *map);

int gpu_select_hardware_count(const GpuSelectDevice *dev, int n);

int gpu_select_env_plan(const GpuSelectDevice *dev, int n, int pick,
                        GpuSelectEnvOp *ops, int max_ops);

const char *gpu_select_type_text(GpuSelectType type);
const char *gpu_select_reason_text(GpuSelectReason reason);
void gpu_select_describe(const GpuSelectDevice *dev, char *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif
