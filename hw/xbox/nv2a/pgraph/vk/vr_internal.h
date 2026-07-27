/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_VR_INTERNAL_H
#define HW_XBOX_NV2A_PGRAPH_VK_VR_INTERNAL_H

#include "hw/xbox/nv2a/pgraph/pgraph.h"
#include "qemu/thread.h"
#include "vr_xr_loader.h"

typedef struct VRXrSession {
    XrInstance instance;
    XrSystemId system_id;
    XrSession session;
    XrSpace space;
    XrSessionState session_state;
    bool session_running;
    bool lost;
    bool cylinder_supported;
    XrVersion vk_min_version;
    XrVersion vk_max_version;
} VRXrSession;

#define VR_COMP_NUM_CMD_BUFFERS 2

typedef struct VREyeChain {
    XrSwapchain swapchain;
    XrSwapchainImageVulkan2KHR *images;
    uint32_t num_images;
    bool ever_released;
    bool wait_pending;
    uint32_t pending_index;
} VREyeChain;

typedef struct VRCompositor {
    bool initialized;

    VkDevice device;
    VkQueue queue;
    uint32_t queue_family;

    QemuThread pacer;
    bool pacer_started;
    bool pacer_exit;
    bool pacer_done;
    XrFrameState pending_frame_state;
    bool has_pending_frame;

    bool begin_owed;
    XrTime begin_owed_display_time;

    VREyeChain chains[2];
    uint32_t swapchain_width;
    uint32_t swapchain_height;

    VkImageCopy *flip_regions;
    uint32_t flip_regions_count;

    XrSpace view_space;
    bool head_pose_valid;
    XrPosef head_pose;

    bool recenter_requested;
    XrPosef recenter_pose;

    VkCommandPool cmd_pool;
    VkCommandBuffer cmd_buffers[VR_COMP_NUM_CMD_BUFFERS];
    VkFence fences[VR_COMP_NUM_CMD_BUFFERS];
    bool fence_submitted[VR_COMP_NUM_CMD_BUFFERS];
    uint32_t next_cmd_buffer;

    VREyeChain panel_chain;
    VkBuffer panel_staging_buffer;
    VkDeviceMemory panel_staging_memory;
    void *panel_staging_mapped;
    VkCommandBuffer panel_cmd;
    VkFence panel_fence;
    bool panel_fence_submitted;
    bool panel_dirty;

    bool warned_pacer;
    bool warned_beginframe;
    bool warned_endframe;
    bool warned_acquire;
    bool warned_wait_timeout;
    bool warned_wait_fail;
    bool warned_release;
    bool warned_fence_timeout;
    bool warned_swapchain;
    bool warned_panel;
    bool warned_panel_acquire;
    bool warned_panel_wait;
    bool warned_panel_release;
    bool warned_panel_upload;
} VRCompositor;

typedef struct VRCamera {
    bool hunt_requested;
    uint32_t hunt_count;

    char *yaw_addr_str, *pitch_addr_str, *gate_addr_str;
    uint32_t yaw_addr, pitch_addr, gate_addr;
    bool yaw_addr_valid, pitch_addr_valid, gate_addr_valid;

    char *base_mode_str, *base_ptr_str, *base_aob_str;
    int base_mode;
    uint32_t base_ptr; bool base_ptr_valid;
    uint8_t aob_bytes[64], aob_mask[64]; int aob_len;
    bool aob_resolved; uint32_t aob_base; int aob_throttle;

    bool gate_was_on;

    bool base_captured;
    float yaw_base, pitch_base;
    float head_ref_yaw, head_ref_pitch;

    float last_written_yaw, last_written_pitch;
} VRCamera;

typedef struct XemuVRState {
    bool bootstrap_active;
    bool session_running;

    VRXrSession xr;

    VRCompositor comp;

    VRCamera cam;
} XemuVRState;

extern XemuVRState g_xemu_vr;

void vr_abort_bootstrap(void);

bool vr_xr_create_instance_and_system(void);
bool vr_xr_query_graphics_requirements(void);
void vr_xr_destroy_instance(void);
VkResult vr_xr_create_vulkan_instance(const VkInstanceCreateInfo *create_info,
                                      VkInstance *out_instance);
VkPhysicalDevice vr_xr_get_vulkan_physical_device(VkInstance instance);
VkResult vr_xr_create_vulkan_device(VkPhysicalDevice physical_device,
                                    const VkDeviceCreateInfo *create_info,
                                    VkDevice *out_device);
bool vr_xr_create_session(PGRAPHState *pg);
void vr_xr_destroy_session(void);
void vr_xr_pump_events(void);
bool vr_xr_session_running(void);

bool vr_compositor_start(PGRAPHState *pg);
void vr_compositor_stop(void);
void vr_compositor_frame(PGRAPHState *pg);

void vr_camera_frame(PGRAPHState *pg);
void vr_camera_hunt_poll(PGRAPHState *pg);
void vr_camera_reset(void);
void vr_camera_request_hunt_dump(void);
void vr_camera_shutdown(void);

#endif
