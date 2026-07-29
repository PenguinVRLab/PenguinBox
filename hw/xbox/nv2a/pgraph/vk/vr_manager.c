/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "ui/xemu-settings.h"
#include "renderer.h"
#include "vr.h"
#include "vr_internal.h"

XemuVRState g_xemu_vr;

bool xemu_vr_wanted(void)
{
    return g_config.vr.enable &&
           g_config.display.renderer == CONFIG_DISPLAY_RENDERER_VULKAN;
}

void vr_abort_bootstrap(void)
{
    vr_compositor_stop();
    vr_camera_shutdown();
    vr_xr_destroy_session();
    vr_xr_destroy_instance();
    g_xemu_vr.bootstrap_active = false;
    g_xemu_vr.session_running = false;
}

void xemu_vr_begin_bootstrap(PGRAPHState *pg)
{
    if (!xemu_vr_wanted()) {
        return;
    }

    fprintf(stderr, "xemu-vr: VR enabled — bootstrapping OpenXR (Phase A)\n");

    if (!vr_xr_loader_init() ||
        !vr_xr_create_instance_and_system() ||
        !vr_xr_query_graphics_requirements()) {
        vr_abort_bootstrap();
        return;
    }

    g_xemu_vr.bootstrap_active = true;
}

bool xemu_vr_bootstrap_active(void)
{
    return g_xemu_vr.bootstrap_active;
}

VkResult xemu_vr_create_instance(const VkInstanceCreateInfo *create_info,
                                 VkInstance *out_instance)
{
    if (g_xemu_vr.bootstrap_active) {
        VkResult res = vr_xr_create_vulkan_instance(create_info,
                                                    out_instance);
        if (res == VK_SUCCESS) {
            return VK_SUCCESS;
        }
        fprintf(stderr, "xemu-vr: instance creation through XR failed — "
                        "aborting VR, falling back to plain Vulkan.\n");
        vr_abort_bootstrap();
    }
    return vkCreateInstance(create_info, NULL, out_instance);
}

VkPhysicalDevice xemu_vr_get_physical_device(VkInstance instance)
{
    if (!g_xemu_vr.bootstrap_active) {
        return VK_NULL_HANDLE;
    }

    VkPhysicalDevice pd = vr_xr_get_vulkan_physical_device(instance);
    if (pd == VK_NULL_HANDLE) {
        fprintf(stderr, "xemu-vr: OpenXR did not yield a physical device — "
                        "aborting VR, using xemu's own selection.\n");
        vr_abort_bootstrap();
    }
    return pd;
}

VkResult xemu_vr_create_device(VkPhysicalDevice physical_device,
                               const VkDeviceCreateInfo *create_info,
                               VkDevice *out_device)
{
    if (g_xemu_vr.bootstrap_active) {
        VkResult res = vr_xr_create_vulkan_device(physical_device,
                                                  create_info, out_device);
        if (res == VK_SUCCESS) {
            return VK_SUCCESS;
        }
        fprintf(stderr, "xemu-vr: device creation through XR failed — "
                        "aborting VR, falling back to plain Vulkan.\n");
        vr_abort_bootstrap();
    }
    return vkCreateDevice(physical_device, create_info, NULL, out_device);
}

void xemu_vr_on_device_created(PGRAPHState *pg)
{
    if (!g_xemu_vr.bootstrap_active) {
        return;
    }

    if (!vr_xr_create_session(pg)) {
        fprintf(stderr, "xemu-vr: session creation failed — aborting VR, "
                        "running flat.\n");
        vr_abort_bootstrap();
        return;
    }

    if (!vr_compositor_start(pg)) {
        fprintf(stderr, "xemu-vr: compositor start failed — aborting VR, "
                        "running flat.\n");
        vr_abort_bootstrap();
        return;
    }

    g_xemu_vr.session_running = true;
    fprintf(stderr, "xemu-vr: VR active — headset presentation begins when "
                    "the runtime reports the session READY.\n");
}

void xemu_vr_on_device_destroyed(PGRAPHState *pg)
{
    vr_abort_bootstrap();
}

void xemu_vr_request_recenter(void)
{
    qatomic_set(&g_xemu_vr.comp.recenter_requested, true);
}

void xemu_vr_request_hunt_dump(void)
{
    vr_camera_request_hunt_dump();
}

bool xemu_vr_active(void)
{
    return g_xemu_vr.session_running;
}

void xemu_vr_frame(PGRAPHState *pg)
{
    vr_camera_hunt_poll(pg);

    if (!g_xemu_vr.session_running) {
        return;
    }

    vr_xr_pump_events();
    if (g_xemu_vr.xr.lost) {
        fprintf(stderr, "xemu-vr: session/instance lost — shutting VR down; "
                        "flat rendering continues.\n");
        vr_abort_bootstrap();
        return;
    }

    vr_compositor_frame(pg);
}
