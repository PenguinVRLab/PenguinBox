/*
 * Geforce NV2A PGRAPH Vulkan Renderer
 *
 * Copyright (c) 2024-2025 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "ui/xemu-settings.h"
#include "renderer.h"
#include "vr.h"
#include "gpu_choice.h"
#include "xemu-version.h"

#define VkExtensionPropertiesArray GArray
#define StringArray GArray

static bool enable_validation = false;

static char const *const validation_layers[] = {
    "VK_LAYER_KHRONOS_validation",
};

static char const *const required_device_extensions[] = {
#ifdef WIN32
    VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
#else
    VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
#endif
};

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData)
{
    fprintf(stderr, "[vk] %s\n", pCallbackData->pMessage);

    if ((messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) &&
        (messageSeverity & (VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT))) {
        assert(!g_config.display.vulkan.assert_on_validation_msg);
    }
    return VK_FALSE;
}

static bool check_validation_layer_support(void)
{
    uint32_t num_available_layers;
    vkEnumerateInstanceLayerProperties(&num_available_layers, NULL);

    g_autofree VkLayerProperties *available_layers =
        g_malloc_n(num_available_layers, sizeof(VkLayerProperties));
    vkEnumerateInstanceLayerProperties(&num_available_layers, available_layers);

    for (int i = 0; i < ARRAY_SIZE(validation_layers); i++) {
        bool found = false;
        for (int j = 0; j < num_available_layers; j++) {
            if (!strcmp(validation_layers[i], available_layers[j].layerName)) {
                found = true;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "desired validation layer not found: %s\n",
                    validation_layers[i]);
            return false;
        }
    }

    return true;
}

static VkExtensionPropertiesArray *
get_available_instance_extensions(PGRAPHState *pg)
{
    uint32_t num_extensions = 0;

    VK_CHECK(
        vkEnumerateInstanceExtensionProperties(NULL, &num_extensions, NULL));

    VkExtensionPropertiesArray *extensions = g_array_sized_new(
        FALSE, FALSE, sizeof(VkExtensionProperties), num_extensions);

    g_array_set_size(extensions, num_extensions);
    VK_CHECK(vkEnumerateInstanceExtensionProperties(
        NULL, &num_extensions, (VkExtensionProperties *)extensions->data));

    return extensions;
}

static bool
is_extension_available(VkExtensionPropertiesArray *available_extensions,
                       const char *extension_name)
{
    for (int i = 0; i < available_extensions->len; i++) {
        VkExtensionProperties *e =
            &g_array_index(available_extensions, VkExtensionProperties, i);
        if (!strcmp(e->extensionName, extension_name)) {
            return true;
        }
    }

    return false;
}

static bool
add_extension_if_available(VkExtensionPropertiesArray *available_extensions,
                           StringArray *enabled_extension_names,
                           const char *desired_extension_name)
{
    if (is_extension_available(available_extensions, desired_extension_name)) {
        g_array_append_val(enabled_extension_names, desired_extension_name);
        return true;
    }

    fprintf(stderr, "Warning: extension not available: %s\n",
            desired_extension_name);
    return false;
}

static void
add_optional_instance_extension_names(PGRAPHState *pg,
                                      VkExtensionPropertiesArray *available_extensions,
                                      StringArray *enabled_extension_names)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    r->debug_utils_extension_enabled =
        g_config.display.vulkan.validation_layers &&
        add_extension_if_available(available_extensions, enabled_extension_names,
                                   VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
}

static bool create_instance(PGRAPHState *pg, Error **errp)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    VkResult result;

    result = volkInitialize();
    if (result != VK_SUCCESS) {
        error_setg(errp, "volkInitialize failed");
        return false;
    }

    uint32_t instance_version = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion) {
        vkEnumerateInstanceVersion(&instance_version);
        instance_version = MIN(instance_version, VK_API_VERSION_1_3);
    }
    if (instance_version < VK_API_VERSION_1_1) {
        error_setg(errp, "Vulkan 1.1 or higher is required");
        return false;
    }
    r->vk_api_version = instance_version;

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "xemu",
        .applicationVersion = VK_MAKE_VERSION(
            xemu_version_major, xemu_version_minor, xemu_version_patch),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = r->vk_api_version,
    };

    g_autoptr(VkExtensionPropertiesArray) available_extensions =
        get_available_instance_extensions(pg);

    g_autoptr(StringArray) enabled_extension_names =
        g_array_new(FALSE, FALSE, sizeof(char *));

    add_optional_instance_extension_names(pg, available_extensions,
                                          enabled_extension_names);

    if (enabled_extension_names->len > 0) {
        fprintf(stderr, "Enabled instance extensions:\n");
        for (int i = 0; i < enabled_extension_names->len; i++) {
            fprintf(stderr, "- %s\n",
                    g_array_index(enabled_extension_names, char *, i));
        }
    }

    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = enabled_extension_names->len,
        .ppEnabledExtensionNames =
            &g_array_index(enabled_extension_names, const char *, 0),
    };

    enable_validation = g_config.display.vulkan.validation_layers;

    VkValidationFeatureEnableEXT enables[] = {
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
    };

    VkValidationFeaturesEXT validationFeatures = {
        .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .enabledValidationFeatureCount = ARRAY_SIZE(enables),
        .pEnabledValidationFeatures = enables,
    };

    if (enable_validation) {
        if (check_validation_layer_support()) {
            fprintf(stderr, "Warning: Validation layers enabled. Expect "
                            "performance impact.\n");
            create_info.enabledLayerCount = ARRAY_SIZE(validation_layers);
            create_info.ppEnabledLayerNames = validation_layers;
            create_info.pNext = &validationFeatures;
        } else {
            fprintf(stderr, "Warning: validation layers not available\n");
            enable_validation = false;
        }
    }

    result = xemu_vr_create_instance(&create_info, &r->instance);
    if (result != VK_SUCCESS) {
        error_setg(errp, "Failed to create instance (%d)", result);
        return false;
    }

    volkLoadInstance(r->instance);

    if (r->debug_utils_extension_enabled) {
        VkDebugUtilsMessengerCreateInfoEXT messenger_info = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = debugCallback,
        };
        VK_CHECK(vkCreateDebugUtilsMessengerEXT(r->instance, &messenger_info,
                                                NULL, &r->debug_messenger));
    }

    return true;
}

static bool is_queue_family_indicies_complete(QueueFamilyIndices indices)
{
    return indices.queue_family >= 0;
}

QueueFamilyIndices pgraph_vk_find_queue_families(VkPhysicalDevice device)
{
    QueueFamilyIndices indices = {
        .queue_family = -1,
    };

    uint32_t num_queue_families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &num_queue_families, NULL);

    g_autofree VkQueueFamilyProperties *queue_families =
        g_malloc_n(num_queue_families, sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(device, &num_queue_families,
                                             queue_families);

    for (int i = 0; i < num_queue_families; i++) {
        VkQueueFamilyProperties queueFamily = queue_families[i];
        int required_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        if ((queueFamily.queueFlags & required_flags) == required_flags) {
            indices.queue_family = i;
        }
        if (is_queue_family_indicies_complete(indices)) {
            break;
        }
    }

    return indices;
}

static VkExtensionPropertiesArray *
get_available_device_extensions(VkPhysicalDevice device)
{
    uint32_t num_extensions = 0;

    VK_CHECK(vkEnumerateDeviceExtensionProperties(device, NULL, &num_extensions,
                                                  NULL));

    VkExtensionPropertiesArray *extensions = g_array_sized_new(
        FALSE, FALSE, sizeof(VkExtensionProperties), num_extensions);

    g_array_set_size(extensions, num_extensions);
    VK_CHECK(vkEnumerateDeviceExtensionProperties(
        device, NULL, &num_extensions,
        (VkExtensionProperties *)extensions->data));

    return extensions;
}

static StringArray *get_required_device_extension_names(void)
{
    StringArray *extensions =
        g_array_sized_new(FALSE, FALSE, sizeof(char *),
                          ARRAY_SIZE(required_device_extensions));

    g_array_append_vals(extensions, required_device_extensions,
                        ARRAY_SIZE(required_device_extensions));

    return extensions;
}

static void add_optional_device_extension_names(
    PGRAPHState *pg, VkExtensionPropertiesArray *available_extensions,
    StringArray *enabled_extension_names)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    r->custom_border_color_extension_enabled =
        add_extension_if_available(available_extensions, enabled_extension_names,
                                   VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME);

    r->memory_budget_extension_enabled = add_extension_if_available(
        available_extensions, enabled_extension_names,
        VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
}

static bool check_device_support_required_extensions(VkPhysicalDevice device)
{
    g_autoptr(VkExtensionPropertiesArray) available_extensions =
        get_available_device_extensions(device);

    for (int i = 0; i < ARRAY_SIZE(required_device_extensions); i++) {
        if (!is_extension_available(available_extensions,
                                    required_device_extensions[i])) {
            fprintf(stderr, "required device extension not found: %s\n",
                    required_device_extensions[i]);
            return false;
        }
    }

    return true;
}

static bool is_device_compatible(VkPhysicalDevice device)
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);
    if (props.apiVersion < VK_API_VERSION_1_1) {
        return false;
    }

    QueueFamilyIndices indices = pgraph_vk_find_queue_families(device);

    return is_queue_family_indicies_complete(indices) &&
           check_device_support_required_extensions(device);
}

static void describe_device(VkPhysicalDevice device, GpuSelectDevice *out)
{
    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceMemoryProperties mem;

    memset(out, 0, sizeof(*out));
    vkGetPhysicalDeviceProperties(device, &props);
    g_strlcpy(out->name, props.deviceName, sizeof(out->name));
    out->vendor_id = props.vendorID;
    out->device_id = props.deviceID;
    switch (props.deviceType) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        out->type = GPU_SELECT_TYPE_INTEGRATED;
        break;
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        out->type = GPU_SELECT_TYPE_DISCRETE;
        break;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        out->type = GPU_SELECT_TYPE_VIRTUAL;
        break;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        out->type = GPU_SELECT_TYPE_CPU;
        break;
    default:
        out->type = GPU_SELECT_TYPE_OTHER;
        break;
    }

    vkGetPhysicalDeviceMemoryProperties(device, &mem);
    for (uint32_t i = 0; i < mem.memoryHeapCount; i++) {
        if (mem.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            out->memory_bytes += mem.memoryHeaps[i].size;
        }
    }

    g_autoptr(VkExtensionPropertiesArray) extensions =
        get_available_device_extensions(device);
    bool has_driver = is_extension_available(
        extensions, VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME);
    bool has_pci =
        is_extension_available(extensions, VK_EXT_PCI_BUS_INFO_EXTENSION_NAME);

    out->driver = !has_driver && props.vendorID == 0x10de ?
                      GPU_SELECT_DRIVER_NVIDIA :
                      GPU_SELECT_DRIVER_OTHER;
    if (props.apiVersion >= VK_API_VERSION_1_1) {
        VkPhysicalDeviceIDProperties id = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES,
        };
        VkPhysicalDeviceDriverProperties driver = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES,
        };
        VkPhysicalDevicePCIBusInfoPropertiesEXT pci = {
            .sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT,
        };
        VkPhysicalDeviceProperties2 props2 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            .pNext = &id,
        };
        if (has_driver) {
            driver.pNext = props2.pNext;
            props2.pNext = &driver;
        }
        if (has_pci) {
            pci.pNext = props2.pNext;
            props2.pNext = &pci;
        }
        vkGetPhysicalDeviceProperties2(device, &props2);

        memcpy(out->uuid, id.deviceUUID, GPU_SELECT_UUID_SIZE);
        out->has_uuid = !gpu_select_uuid_is_zero(out->uuid);
        if (has_driver) {
            if (driver.driverID == VK_DRIVER_ID_NVIDIA_PROPRIETARY) {
                out->driver = GPU_SELECT_DRIVER_NVIDIA;
            }
            g_autofree char *text =
                g_strdup_printf("%s %s", driver.driverName, driver.driverInfo);
            g_strlcpy(out->driver_text, g_strstrip(text),
                      sizeof(out->driver_text));
        }
        if (has_pci) {
            out->has_pci = true;
            out->pci_domain = pci.pciDomain;
            out->pci_bus = pci.pciBus;
            out->pci_device = pci.pciDevice;
            out->pci_function = pci.pciFunction;
        }
    }

    out->usable = is_device_compatible(device);
    if (!out->usable) {
        const char *missing = NULL;
        for (int i = 0; i < ARRAY_SIZE(required_device_extensions); i++) {
            if (!is_extension_available(extensions,
                                        required_device_extensions[i])) {
                missing = required_device_extensions[i];
                break;
            }
        }
        if (props.apiVersion < VK_API_VERSION_1_1) {
            g_strlcpy(out->why_not, "Vulkan 1.1 is required",
                      sizeof(out->why_not));
        } else if (missing) {
            g_autofree char *text =
                g_strdup_printf("the extension %s is missing", missing);
            g_strlcpy(out->why_not, text, sizeof(out->why_not));
        } else {
            g_strlcpy(out->why_not,
                      "no queue family with graphics and compute",
                      sizeof(out->why_not));
        }
    }
}

int pgraph_vk_gpu_probe(GpuSelectDevice *out, int max)
{
    if (volkInitialize() != VK_SUCCESS) {
        return 0;
    }
    uint32_t instance_version = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion) {
        vkEnumerateInstanceVersion(&instance_version);
    }
    if (instance_version < VK_API_VERSION_1_1) {
        return 0;
    }

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "xemu",
        .apiVersion = MIN(instance_version, VK_API_VERSION_1_3),
    };
    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
    };
    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&create_info, NULL, &instance) != VK_SUCCESS) {
        return 0;
    }
    volkLoadInstance(instance);

    int count = 0;
    uint32_t num_devices = 0;
    if (vkEnumeratePhysicalDevices(instance, &num_devices, NULL) ==
            VK_SUCCESS &&
        num_devices > 0) {
        g_autofree VkPhysicalDevice *devices =
            g_malloc0_n(num_devices, sizeof(VkPhysicalDevice));
        VkResult result =
            vkEnumeratePhysicalDevices(instance, &num_devices, devices);
        if (result == VK_SUCCESS || result == VK_INCOMPLETE) {
            for (uint32_t i = 0; i < num_devices && count < max; i++) {
                describe_device(devices[i], &out[count++]);
            }
        }
    }
    vkDestroyInstance(instance, NULL);
    return count;
}

static bool select_physical_device(PGRAPHState *pg, Error **errp)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    VkResult result;

    VkPhysicalDevice xr_device = xemu_vr_get_physical_device(r->instance);
    if (xr_device != VK_NULL_HANDLE) {
        if (!is_device_compatible(xr_device)) {
            error_setg(errp, "OpenXR-mandated GPU lacks required Vulkan "
                             "features");
            return false;
        }
        r->physical_device = xr_device;
        vkGetPhysicalDeviceProperties(r->physical_device, &r->device_props);
        r->vk_api_version = MIN(r->vk_api_version, r->device_props.apiVersion);
        fprintf(stderr, "Physical device selected by OpenXR runtime: %s\n",
                r->device_props.deviceName);
        GpuSelectDevice xr_desc;
        describe_device(xr_device, &xr_desc);
        xemu_gpu_note_vulkan_xr(&xr_desc);
        return true;
    }

    uint32_t num_physical_devices = 0;

    result =
        vkEnumeratePhysicalDevices(r->instance, &num_physical_devices, NULL);
    if (result != VK_SUCCESS || num_physical_devices == 0) {
        error_setg(errp, "Failed to find GPUs with Vulkan support");
        return false;
    }

    g_autofree VkPhysicalDevice *devices =
        g_malloc_n(num_physical_devices, sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(r->instance, &num_physical_devices, devices);

    const char *preferred_device = g_config.display.vulkan.preferred_physical_device;

    fprintf(stderr, "Available physical devices:\n");
    for (int i = 0; i < num_physical_devices; i++) {
        vkGetPhysicalDeviceProperties(devices[i], &r->device_props);
        bool is_preferred =
            preferred_device &&
            !strcmp(r->device_props.deviceName, preferred_device);
        fprintf(stderr, "- %s%s\n", r->device_props.deviceName,
                is_preferred ? " *" : "");
    }

    r->physical_device = VK_NULL_HANDLE;

    int n_desc = MIN(num_physical_devices, GPU_SELECT_MAX_DEVICES);
    g_autofree GpuSelectDevice *desc = g_new0(GpuSelectDevice, n_desc);
    g_autofree int *desc_device = g_new0(int, n_desc);
    for (int i = 0; i < n_desc; i++) {
        describe_device(devices[i], &desc[i]);
    }
    n_desc = gpu_select_dedup(desc, n_desc, desc_device);
    uint8_t gl_uuid[GPU_SELECT_UUID_SIZE];
    bool gl_known = xemu_gpu_gl_uuid(gl_uuid);
    GpuSelectChoice choice = xemu_gpu_effective_choice(NULL);
    GpuSelectPick pick =
        gpu_select_pick(desc, n_desc, &choice, gl_known ? gl_uuid : NULL);
    if (pick.index >= 0) {
        r->physical_device = devices[desc_device[pick.index]];
        xemu_gpu_note_vulkan_pick(desc, n_desc, &pick);
    }
    if (r->physical_device == VK_NULL_HANDLE) {
        error_setg(errp, "Failed to find a suitable GPU");
        return false;
    }

    vkGetPhysicalDeviceProperties(r->physical_device, &r->device_props);
    xemu_settings_set_string(&g_config.display.vulkan.preferred_physical_device,
                             r->device_props.deviceName);
    r->vk_api_version = MIN(r->vk_api_version, r->device_props.apiVersion);

    fprintf(stderr,
            "Selected physical device: %s\n"
            "- Vendor: %x, Device: %x\n"
            "- Driver Version: %d.%d.%d\n",
            r->device_props.deviceName,
            r->device_props.vendorID,
            r->device_props.deviceID,
            VK_VERSION_MAJOR(r->device_props.driverVersion),
            VK_VERSION_MINOR(r->device_props.driverVersion),
            VK_VERSION_PATCH(r->device_props.driverVersion));

    return true;
}

static bool create_logical_device(PGRAPHState *pg, Error **errp)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    VkResult result;

    QueueFamilyIndices indices =
        pgraph_vk_find_queue_families(r->physical_device);

    g_autoptr(VkExtensionPropertiesArray) available_extensions =
        get_available_device_extensions(r->physical_device);

    g_autoptr(StringArray) enabled_extension_names =
        get_required_device_extension_names();

    add_optional_device_extension_names(pg, available_extensions,
                                        enabled_extension_names);

    fprintf(stderr, "Enabled device extensions:\n");
    for (int i = 0; i < enabled_extension_names->len; i++) {
        fprintf(stderr, "- %s\n",
                g_array_index(enabled_extension_names, char *, i));
    }

    float queuePriority = 1.0f;

    VkDeviceQueueCreateInfo queue_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = indices.queue_family,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority,
    };

    VkPhysicalDeviceFeatures physical_device_features;
    vkGetPhysicalDeviceFeatures(r->physical_device, &physical_device_features);
    memset(&r->enabled_physical_device_features, 0,
           sizeof(r->enabled_physical_device_features));

    struct {
        const char *name;
        VkBool32 available, *enabled;
        bool required;
    } desired_features[] = {
        // clang-format off
        #define F(n, req) { \
            .name = #n, \
            .available = physical_device_features.n, \
            .enabled = &r->enabled_physical_device_features.n, \
            .required = req, \
        }
        F(depthClamp, true),
        F(fillModeNonSolid, true),
        F(geometryShader, true),
        F(occlusionQueryPrecise, true),
        F(samplerAnisotropy, false),
        F(shaderClipDistance, true),
        F(shaderTessellationAndGeometryPointSize, true),
        F(wideLines, false),
        #undef F
        // clang-format on
    };

    bool all_required_features_available = true;
    for (int i = 0; i < ARRAY_SIZE(desired_features); i++) {
        if (desired_features[i].required &&
            desired_features[i].available != VK_TRUE) {
            fprintf(stderr,
                    "Error: Device does not support required feature %s\n",
                    desired_features[i].name);
            all_required_features_available = false;
        }
        *desired_features[i].enabled = desired_features[i].available;
    }

    if (!all_required_features_available) {
        error_setg(errp, "Device does not support required features");
        return false;
    }

    void *next_struct = NULL;

    VkPhysicalDeviceCustomBorderColorFeaturesEXT custom_border_features;
    if (r->custom_border_color_extension_enabled) {
        custom_border_features = (VkPhysicalDeviceCustomBorderColorFeaturesEXT){
            .sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT,
            .customBorderColors = VK_TRUE,
            .pNext = next_struct,
        };
        next_struct = &custom_border_features;
    }

    VkPhysicalDeviceMultiviewFeatures multiview_features;
    if (pgraph_vk_stereo_requested()) {
        VkPhysicalDeviceMultiviewFeatures query = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES,
        };
        VkPhysicalDeviceFeatures2 features2 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &query,
        };
        vkGetPhysicalDeviceFeatures2(r->physical_device, &features2);
        if (pgraph_vk_stereo_device_features(query.multiview,
                                             query.multiviewGeometryShader)) {
            multiview_features = (VkPhysicalDeviceMultiviewFeatures){
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES,
                .multiview = VK_TRUE,
                .multiviewGeometryShader = VK_TRUE,
                .pNext = next_struct,
            };
            next_struct = &multiview_features;
        }
    }

    VkDeviceCreateInfo device_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_create_info,
        .pEnabledFeatures = &r->enabled_physical_device_features,
        .enabledExtensionCount = enabled_extension_names->len,
        .ppEnabledExtensionNames =
            &g_array_index(enabled_extension_names, const char *, 0),
        .pNext = next_struct,
    };

    if (enable_validation) {
        device_create_info.enabledLayerCount = ARRAY_SIZE(validation_layers);
        device_create_info.ppEnabledLayerNames = validation_layers;
    }

    result = xemu_vr_create_device(r->physical_device, &device_create_info,
                                   &r->device);
    if (result != VK_SUCCESS) {
        error_setg(errp, "Failed to create logical device (%d)", result);
        return false;
    }

    vkGetDeviceQueue(r->device, indices.queue_family, 0, &r->queue);
    return true;
}

uint32_t pgraph_vk_get_memory_type(PGRAPHState *pg, uint32_t type_bits,
                                   VkMemoryPropertyFlags properties)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkPhysicalDeviceMemoryProperties prop;
    vkGetPhysicalDeviceMemoryProperties(r->physical_device, &prop);
    for (uint32_t i = 0; i < prop.memoryTypeCount; i++) {
        if ((prop.memoryTypes[i].propertyFlags & properties) == properties &&
            type_bits & (1 << i)) {
            return i;
        }
    }
    return 0xFFFFFFFF;
}

static bool init_allocator(PGRAPHState *pg, Error **errp)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    VkResult result;

    VmaAllocatorCreateInfo create_info = {
        .flags = (r->memory_budget_extension_enabled ?
                      VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT :
                      0),
        .vulkanApiVersion = r->vk_api_version,
        .instance = r->instance,
        .physicalDevice = r->physical_device,
        .device = r->device,
    };

    VmaVulkanFunctions vulkan_functions;
    VkResult res = vmaImportVulkanFunctionsFromVolk(&create_info, &vulkan_functions);
    if (res != VK_SUCCESS) {
        error_setg(errp, "vmaImportVulkanFunctionsFromVolk failed");
        return false;
    }
    create_info.pVulkanFunctions = &vulkan_functions;

    result = vmaCreateAllocator(&create_info, &r->allocator);
    if (result != VK_SUCCESS) {
        error_setg(errp, "vmaCreateAllocator failed");
        return false;
    }

    return true;
}

static bool check_buffer_memory(PGRAPHState *pg, Error **errp)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    PgraphVkBufferPlan plan;
    const VkPhysicalDeviceMemoryProperties *mp;
    VmaBudget budgets[VK_MAX_MEMORY_HEAPS];
    uint64_t dev_free = 0, host_free = 0;
    bool have_host_heap = false;

    pgraph_vk_buffer_plan(container_of(pg, NV2AState, pgraph), &plan);
    vmaGetMemoryProperties(r->allocator, &mp);
    vmaGetHeapBudgets(r->allocator, budgets);
    for (uint32_t h = 0; h < mp->memoryHeapCount; h++) {
        uint64_t free_h = budgets[h].budget > budgets[h].usage ?
                              budgets[h].budget - budgets[h].usage : 0;
        if (mp->memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            dev_free = MAX(dev_free, free_h);
        } else {
            have_host_heap = true;
            host_free = MAX(host_free, free_h);
        }
    }
    bool unified = !have_host_heap;
    uint64_t need_dev = plan.device_bytes + (unified ? plan.host_bytes : 0);
    bool fits = need_dev <= dev_free;
    bool fits_spilled = !unified &&
                        plan.device_bytes + plan.host_bytes <= dev_free + host_free;

    fprintf(stderr,
            "nv2a/vk: memory check: the working buffers need %" PRIu64
            " MiB device-local and %" PRIu64 " MiB host; %s offers %" PRIu64
            " MiB device-local and %" PRIu64 " MiB host now: %s\n",
            pgraph_vk_mib_up(plan.device_bytes), pgraph_vk_mib_up(plan.host_bytes),
            r->device_props.deviceName, dev_free >> 20, host_free >> 20,
            fits ? "fits" : fits_spilled ? "fits only with part of them in "
                                           "system memory (slower)" : "does not fit");
    if (fits || fits_spilled) {
        return true;
    }
    g_autofree char *heaps = pgraph_vk_describe_heaps(pg);
    error_setg(errp,
               "%s has %" PRIu64 " MiB of device-local and %" PRIu64 " MiB of "
               "host memory free for PenguinBox, and the Vulkan renderer needs "
               "%" PRIu64 " MiB device-local and %" PRIu64 " MiB host at start "
               "(%s). Close other GPU-heavy apps, or use the OpenGL renderer "
               "(display.renderer = 'OPENGL')",
               r->device_props.deviceName, dev_free >> 20, host_free >> 20,
               pgraph_vk_mib_up(need_dev),
               pgraph_vk_mib_up(unified ? 0 : plan.host_bytes), heaps);
    return false;
}

void pgraph_vk_init_instance(PGRAPHState *pg, Error **errp)
{
    xemu_vr_begin_bootstrap(pg);

    if (create_instance(pg, errp) &&
        select_physical_device(pg, errp) &&
        create_logical_device(pg, errp) &&
        init_allocator(pg, errp) &&
        check_buffer_memory(pg, errp)) {
        xemu_vr_on_device_created(pg);
        return;
    }

    pgraph_vk_finalize_instance(pg);

    const char *msg = "Failed to initialize Vulkan renderer";
    if (*errp) {
        error_prepend(errp, "%s: ", msg);
    } else {
        error_setg(errp, "%s", msg);
    }
}

void pgraph_vk_finalize_instance(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    xemu_vr_on_device_destroyed(pg);

    if (r->allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(r->allocator);
        r->allocator = VK_NULL_HANDLE;
    }

    if (r->device != VK_NULL_HANDLE) {
        vkDestroyDevice(r->device, NULL);
        r->device = VK_NULL_HANDLE;
    }

    if (r->debug_messenger != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(r->instance, r->debug_messenger, NULL);
        r->debug_messenger = VK_NULL_HANDLE;
    }

    if (r->instance != VK_NULL_HANDLE) {
        vkDestroyInstance(r->instance, NULL);
        r->instance = VK_NULL_HANDLE;
    }

    volkFinalize();
}
