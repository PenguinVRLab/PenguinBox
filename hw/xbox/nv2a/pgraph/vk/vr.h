/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_VR_H
#define HW_XBOX_NV2A_PGRAPH_VK_VR_H

#include "hw/xbox/nv2a/pgraph/pgraph.h"
#include <vulkan/vulkan.h>
#include "vr_public.h"

#ifdef __cplusplus
extern "C" {
#endif

bool xemu_vr_wanted(void);
void xemu_vr_begin_bootstrap(PGRAPHState *pg);
bool xemu_vr_bootstrap_active(void);

VkResult xemu_vr_create_instance(const VkInstanceCreateInfo *create_info,
                                 VkInstance *out_instance);
VkPhysicalDevice xemu_vr_get_physical_device(VkInstance instance);
VkResult xemu_vr_create_device(VkPhysicalDevice physical_device,
                               const VkDeviceCreateInfo *create_info,
                               VkDevice *out_device);
void xemu_vr_on_device_created(PGRAPHState *pg);
void xemu_vr_on_device_destroyed(PGRAPHState *pg);

void xemu_vr_frame(PGRAPHState *pg);

#ifdef __cplusplus
}
#endif

#endif
