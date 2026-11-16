/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_GPU_CHOICE_H
#define HW_XBOX_NV2A_PGRAPH_VK_GPU_CHOICE_H

#include "gpu_select.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct XemuGpuStatus {
    int n;
    GpuSelectDevice dev[GPU_SELECT_MAX_DEVICES];
    GpuSelectPick start;
    bool vr;
    bool choice_set_aside;
    bool gl_known;
    uint8_t gl_uuid[GPU_SELECT_UUID_SIZE];
    char gl_renderer[160];
    bool vulkan_known;
    char vulkan_name[256];
    char notice_missing[400];
    char notice_gl[900];
    char notice_vr[900];
} XemuGpuStatus;

void xemu_gpu_start(void);

void xemu_gpu_note_gl(void);

GpuSelectChoice xemu_gpu_effective_choice(bool *set_aside);

bool xemu_gpu_gl_uuid(uint8_t out[GPU_SELECT_UUID_SIZE]);

void xemu_gpu_note_vulkan_pick(const GpuSelectDevice *dev, int n,
                               const GpuSelectPick *pick);
void xemu_gpu_note_vulkan_xr(const GpuSelectDevice *dev);

void xemu_gpu_note_vr_refused(const char *runtime_name);

void xemu_gpu_get_status(XemuGpuStatus *out);

int pgraph_vk_gpu_probe(GpuSelectDevice *out, int max);

#ifdef __cplusplus
}
#endif

#endif
