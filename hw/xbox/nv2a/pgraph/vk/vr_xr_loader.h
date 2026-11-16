/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_VR_XR_LOADER_H
#define HW_XBOX_NV2A_PGRAPH_VK_VR_XR_LOADER_H

#include <vulkan/vulkan.h>

#include "openxr/openxr.h"
#define XR_USE_GRAPHICS_API_VULKAN
#include "openxr/openxr_platform.h"

typedef struct XemuXrPfns {
    PFN_xrGetInstanceProcAddr GetInstanceProcAddr;

    PFN_xrEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProperties;
    PFN_xrCreateInstance CreateInstance;

    PFN_xrDestroyInstance DestroyInstance;
    PFN_xrGetInstanceProperties GetInstanceProperties;
    PFN_xrGetSystem GetSystem;
    PFN_xrGetSystemProperties GetSystemProperties;
    PFN_xrEnumerateEnvironmentBlendModes EnumerateEnvironmentBlendModes;
    PFN_xrResultToString ResultToString;
    PFN_xrPollEvent PollEvent;
    PFN_xrCreateSession CreateSession;
    PFN_xrDestroySession DestroySession;
    PFN_xrBeginSession BeginSession;
    PFN_xrEndSession EndSession;
    PFN_xrCreateReferenceSpace CreateReferenceSpace;
    PFN_xrDestroySpace DestroySpace;
    PFN_xrLocateSpace LocateSpace;
    PFN_xrLocateViews LocateViews;
    PFN_xrEnumerateViewConfigurationViews EnumerateViewConfigurationViews;
    PFN_xrCreateSwapchain CreateSwapchain;
    PFN_xrDestroySwapchain DestroySwapchain;
    PFN_xrEnumerateSwapchainFormats EnumerateSwapchainFormats;
    PFN_xrEnumerateSwapchainImages EnumerateSwapchainImages;
    PFN_xrAcquireSwapchainImage AcquireSwapchainImage;
    PFN_xrWaitSwapchainImage WaitSwapchainImage;
    PFN_xrReleaseSwapchainImage ReleaseSwapchainImage;
    PFN_xrWaitFrame WaitFrame;
    PFN_xrBeginFrame BeginFrame;
    PFN_xrEndFrame EndFrame;

    PFN_xrStringToPath StringToPath;
    PFN_xrCreateActionSet CreateActionSet;
    PFN_xrDestroyActionSet DestroyActionSet;
    PFN_xrCreateAction CreateAction;
    PFN_xrDestroyAction DestroyAction;
    PFN_xrSuggestInteractionProfileBindings SuggestInteractionProfileBindings;
    PFN_xrAttachSessionActionSets AttachSessionActionSets;
    PFN_xrCreateActionSpace CreateActionSpace;
    PFN_xrSyncActions SyncActions;
    PFN_xrGetActionStatePose GetActionStatePose;
    PFN_xrGetActionStateBoolean GetActionStateBoolean;

    PFN_xrGetVulkanGraphicsRequirements2KHR GetVulkanGraphicsRequirements2KHR;
    PFN_xrCreateVulkanInstanceKHR CreateVulkanInstanceKHR;
    PFN_xrGetVulkanGraphicsDevice2KHR GetVulkanGraphicsDevice2KHR;
    PFN_xrCreateVulkanDeviceKHR CreateVulkanDeviceKHR;

    PFN_xrCreateHandTrackerEXT CreateHandTrackerEXT;
    PFN_xrDestroyHandTrackerEXT DestroyHandTrackerEXT;
    PFN_xrLocateHandJointsEXT LocateHandJointsEXT;
} XemuXrPfns;

extern XemuXrPfns xr_pfn;

bool vr_xr_loader_init(void);
bool vr_xr_load_instance_pfns(XrInstance instance);
void vr_xr_clear_instance_pfns(void);

#endif
