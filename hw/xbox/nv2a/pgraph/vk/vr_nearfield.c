/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"

#include "ui/xemu-settings.h"
#include "renderer.h"
#include "vr_internal.h"
#include "vr_nearfield.h"
#include "vr_nearfield_math.h"
#include "vr_hands.h"
#include "vr_hands_geom.h"
#include "vr_hands_gltf.h"
#include "vr_hands_trace.h"
#include "vr_xr_input.h"
#include "data/glove-v1.1_left.glb.h"
#include "data/glove-v1.1_right.glb.h"

#define NF_ONE_SECOND_NS 1000000000LL
#define NF_MAX_VERTS   8192u
#define NF_MAX_INDICES 32768u
#define NF_NEAR_M 0.02f
#define NF_FAR_M  10.0f
#define NF_RATE_EVERY 300u

typedef struct NfChain {
    XrSwapchain swapchain;
    XrSwapchainImageVulkan2KHR *images;
    VkImageView *views;
    VkFramebuffer *fbs;
    uint32_t num;
    bool wait_pending;
    uint32_t pending_index;
    bool ever_released;
    VkImage depth;
    VkDeviceMemory depth_mem;
    VkImageView depth_view;
} NfChain;

#define NF_GLOVE_VERTS   (2u * VR_GLTF_MAX_VERTS)
#define NF_GLOVE_INDICES (2u * 3u * VR_GLTF_MAX_TRIS)

typedef struct NfPush {
    float vp[16];
    float colour[4];
    float eye[4];
} NfPush;

static struct {
    bool env_override, env_on;
    bool env_scale_set;
    float env_scale;
    VrHandsFilterCfg filter;
    VrHandsHold hold[2];
    bool env_model_set, env_glove;
    bool glove;
    VrGltfHand gl[2];
    uint32_t gl_first[2];
    bool gl_drawn[2];
    uint32_t gl_hidden[2];
    int64_t skin_sum_us, skin_max_us;
    uint32_t skin_n;
    bool last_wanted;
    bool said_state;
    bool created;
    bool failed;
    uint32_t w, h;
    uint32_t full_w, full_h;
    float scale;
    VkFormat color_fmt, depth_fmt;
    NfChain chain[2];
    VkRenderPass rp;
    VkPipelineLayout layout;
    VkPipeline pipe;
    ShaderModuleInfo *vs, *fs;
    VkCommandPool pool;
    VkCommandBuffer cmd;
    VkFence fence;
    bool fence_submitted;
    VkBuffer vb, ib;
    VkDeviceMemory vb_mem, ib_mem;
    void *vb_map, *ib_map;
    uint32_t draw_indices;
    bool hands_drawn;
    uint32_t frame_index;
    uint32_t frames_with_layer, frames_hands_drawn;
    uint32_t frames_no_hand;
    uint32_t submits_in_window;
    int64_t window_t0;
    int64_t joint_sum_us, joint_max_us;
    uint32_t joint_n;
    uint32_t views_missed;
    uint32_t mesh_overflows;
    bool warned_views, warned_acquire, warned_wait, warned_release,
         warned_fence, warned_submit, warned_overflow;
} s_nf;

static const char *nf_vert_glsl =
    "#version 450\n"
    "layout(location = 0) in vec3 in_pos;\n"
    "layout(location = 1) in vec3 in_nrm;\n"
    "layout(push_constant, std430) uniform PC {\n"
    "    mat4 vp;\n"
    "    vec4 colour;\n"
    "    vec4 eye;\n"
    "} pc;\n"
    "layout(location = 0) out vec3 v_pos;\n"
    "layout(location = 1) out vec3 v_nrm;\n"
    "void main() {\n"
    "    v_pos = in_pos;\n"
    "    v_nrm = in_nrm;\n"
    "    gl_Position = pc.vp * vec4(in_pos, 1.0);\n"
    "}\n";

static const char *nf_frag_glsl =
    "#version 450\n"
    "layout(location = 0) in vec3 v_pos;\n"
    "layout(location = 1) in vec3 v_nrm;\n"
    "layout(push_constant, std430) uniform PC {\n"
    "    mat4 vp;\n"
    "    vec4 colour;\n"
    "    vec4 eye;\n"
    "} pc;\n"
    "layout(location = 0) out vec4 out_col;\n"
    "void main() {\n"
    "    vec3 n = normalize(v_nrm);\n"
    "    vec3 v = normalize(pc.eye.xyz - v_pos);\n"
    "    float ndv = clamp(dot(n, v), 0.0, 1.0);\n"
    "    vec3 light = normalize(vec3(0.3, 1.0, 0.4));\n"
    "    float diff = 0.35 + 0.65 * clamp(dot(n, light), 0.0, 1.0);\n"
    "    float rim = pow(1.0 - ndv, 3.0) * 0.35;\n"
    "    vec3 c = pc.colour.rgb * diff + vec3(rim);\n"
    "    out_col = vec4(c, 1.0);\n"
    "}\n";

static const char *nf_glove_vert_glsl =
    "#version 450\n"
    "layout(location = 0) in vec3 in_pos;\n"
    "layout(location = 1) in vec3 in_nrm;\n"
    "layout(location = 2) in vec3 in_col;\n"
    "layout(push_constant, std430) uniform PC {\n"
    "    mat4 vp;\n"
    "    vec4 colour;\n"
    "    vec4 eye;\n"
    "} pc;\n"
    "layout(location = 0) out vec3 v_pos;\n"
    "layout(location = 1) out vec3 v_nrm;\n"
    "layout(location = 2) out vec3 v_col;\n"
    "void main() {\n"
    "    v_pos = in_pos;\n"
    "    v_nrm = in_nrm;\n"
    "    v_col = in_col;\n"
    "    gl_Position = pc.vp * vec4(in_pos, 1.0);\n"
    "}\n";

static const char *nf_glove_frag_glsl =
    "#version 450\n"
    "layout(location = 0) in vec3 v_pos;\n"
    "layout(location = 1) in vec3 v_nrm;\n"
    "layout(location = 2) in vec3 v_col;\n"
    "layout(push_constant, std430) uniform PC {\n"
    "    mat4 vp;\n"
    "    vec4 colour;\n"
    "    vec4 eye;\n"
    "} pc;\n"
    "layout(location = 0) out vec4 out_col;\n"
    "void main() {\n"
    "    vec3 n = normalize(v_nrm);\n"
    "    vec3 v = normalize(pc.eye.xyz - v_pos);\n"
    "    if (dot(n, v) < 0.0) n = -n;\n"
    "    float ndv = clamp(dot(n, v), 0.0, 1.0);\n"
    "    vec3 light = normalize(vec3(0.3, 1.0, 0.4));\n"
    "    float diff = 0.35 + 0.65 * clamp(dot(n, light), 0.0, 1.0);\n"
    "    float rim = pow(1.0 - ndv, 3.0) * 0.35;\n"
    "    vec3 c = pc.colour.rgb * v_col * diff + vec3(rim);\n"
    "    out_col = vec4(c, 1.0);\n"
    "}\n";

void vr_nearfield_start(void)
{
    memset(&s_nf, 0, sizeof(s_nf));
    const char *env = getenv("XEMU_VR_HANDS_MODE");
    if (env && env[0]) {
        s_nf.env_override = true;
        s_nf.env_on = strcmp(env, "tracked") == 0;
        fprintf(stderr, "PenguinBox: near-field: XEMU_VR_HANDS_MODE=%s "
                        "overrides vr.hands_mode this run\n", env);
    }
    const char *md = getenv("XEMU_VR_HANDS_MODEL");
    if (md && md[0]) {
        s_nf.env_model_set = true;
        s_nf.env_glove = strcmp(md, "glove") == 0;
        fprintf(stderr, "PenguinBox: near-field: XEMU_VR_HANDS_MODEL=%s "
                        "overrides vr.hands_model this run\n", md);
    }
    const char *sc = getenv("XEMU_VR_HANDS_SCALE");
    if (sc && sc[0]) {
        s_nf.env_scale_set = true;
        s_nf.env_scale = strtof(sc, NULL);
        fprintf(stderr, "PenguinBox: near-field: XEMU_VR_HANDS_SCALE=%s "
                        "overrides vr.hands_scale this run\n", sc);
    }
    const char *rt = getenv("XEMU_VR_HANDS_REQUIRE_TRACKED");
    if (rt && rt[0] && strcmp(rt, "0") != 0) {
        s_nf.filter.require_tracked = true;
        fprintf(stderr, "PenguinBox: near-field: XEMU_VR_HANDS_REQUIRE_TRACKED: "
                        "a joint is drawn only when the runtime TRACKED it "
                        "(an inferred joint is not)\n");
    }
    const char *hm = getenv("XEMU_VR_HANDS_HOLD_MS");
    if (hm && hm[0]) {
        long ms = strtol(hm, NULL, 10);
        ms = ms < 0 ? 0 : ms > 1000 ? 1000 : ms;
        s_nf.filter.hold_ns = ms * 1000000LL;
        fprintf(stderr, "PenguinBox: near-field: XEMU_VR_HANDS_HOLD_MS=%s: a hand "
                        "that loses tracking keeps its last joints for up to "
                        "%ld ms, then disappears\n", hm, ms);
    }
    s_nf.window_t0 = g_get_monotonic_time();
    vr_hands_trace_session_begin();
    if (!vr_nearfield_wanted()) {
        vr_hands_trace_warn_not_started("vr.hands_mode is not tracked, so the near-field layer (which starts the "
                                        "recorder) is off; set vr.hands_mode to tracked or XEMU_VR_HANDS_MODE=tracked");
    }
}

bool vr_nearfield_wanted(void)
{
    if (s_nf.env_override) {
        return s_nf.env_on;
    }
    return g_config.vr.hands_mode == CONFIG_VR_HANDS_MODE_TRACKED;
}

static void nf_fail(const char *what)
{
    fprintf(stderr, "PenguinBox: near-field: %s; the layer stays off this "
                    "session\n", what);
    vr_hands_trace_warn_not_started("the near-field layer failed to create (%s)", what);
    s_nf.failed = true;
}

static bool nf_pick_color_format(void)
{
    const XrSession session = g_xemu_vr.xr.session;
    uint32_t n = 0;
    if (XR_FAILED(xr_pfn.EnumerateSwapchainFormats(session, 0, &n, NULL)) ||
        n == 0) {
        return false;
    }
    g_autofree int64_t *fmts = g_new0(int64_t, n);
    if (XR_FAILED(xr_pfn.EnumerateSwapchainFormats(session, n, &n, fmts))) {
        return false;
    }
    static const VkFormat prefer[] = {
        VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB,
        VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM,
    };
    for (size_t p = 0; p < ARRAY_SIZE(prefer); p++) {
        for (uint32_t i = 0; i < n; i++) {
            if (fmts[i] == (int64_t)prefer[p]) {
                s_nf.color_fmt = prefer[p];
                return true;
            }
        }
    }
    return false;
}

static bool nf_pick_depth_format(PGRAPHVkState *r)
{
    static const VkFormat prefer[] = {
        VK_FORMAT_D32_SFLOAT, VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D16_UNORM,
    };
    for (size_t p = 0; p < ARRAY_SIZE(prefer); p++) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(r->physical_device, prefer[p], &fp);
        if (fp.optimalTilingFeatures &
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            s_nf.depth_fmt = prefer[p];
            return true;
        }
    }
    return false;
}

static bool nf_pick_size(void)
{
    VRXrSession *xr = &g_xemu_vr.xr;
    XrViewConfigurationView v[2] = {
        { .type = XR_TYPE_VIEW_CONFIGURATION_VIEW },
        { .type = XR_TYPE_VIEW_CONFIGURATION_VIEW },
    };
    uint32_t n = 0;
    if (xr_pfn.EnumerateViewConfigurationViews == NULL ||
        XR_FAILED(xr_pfn.EnumerateViewConfigurationViews(
            xr->instance, xr->system_id,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &n, v)) ||
        n < 2) {
        return false;
    }
    uint32_t w = MAX(v[0].recommendedImageRectWidth, v[1].recommendedImageRectWidth);
    uint32_t h = MAX(v[0].recommendedImageRectHeight, v[1].recommendedImageRectHeight);
    for (int e = 0; e < 2; e++) {
        w = MIN(w, v[e].maxImageRectWidth);
        h = MIN(h, v[e].maxImageRectHeight);
    }
    if (xr->max_swapchain_w) {
        w = MIN(w, xr->max_swapchain_w);
    }
    if (xr->max_swapchain_h) {
        h = MIN(h, xr->max_swapchain_h);
    }
    if (w == 0 || h == 0) {
        return false;
    }
    s_nf.scale = s_nf.env_scale_set ? s_nf.env_scale : g_config.vr.hands_scale;
    s_nf.full_w = w;
    s_nf.full_h = h;
    s_nf.w = vr_nf_scaled_extent(w, s_nf.scale, VR_NF_SAB_NONE);
    s_nf.h = vr_nf_scaled_extent(h, s_nf.scale, VR_NF_SAB_NONE);
    return true;
}

static bool nf_create_chain(PGRAPHState *pg, int e)
{
    VRCompositor *c = &g_xemu_vr.comp;
    NfChain *ch = &s_nf.chain[e];

    XrSwapchainCreateInfo ci = {
        .type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
        .usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
        .format = (int64_t)s_nf.color_fmt,
        .sampleCount = 1,
        .width = s_nf.w,
        .height = s_nf.h,
        .faceCount = 1,
        .arraySize = 1,
        .mipCount = 1,
    };
    XrResult res = xr_pfn.CreateSwapchain(g_xemu_vr.xr.session, &ci,
                                          &ch->swapchain);
    if (XR_FAILED(res)) {
        ch->swapchain = XR_NULL_HANDLE;
        fprintf(stderr, "PenguinBox: near-field: xrCreateSwapchain (eye %d, "
                        "%ux%u) failed (%d)\n", e, s_nf.w, s_nf.h, (int)res);
        return false;
    }
    uint32_t n = 0;
    if (XR_FAILED(xr_pfn.EnumerateSwapchainImages(ch->swapchain, 0, &n, NULL)) ||
        n == 0) {
        return false;
    }
    ch->images = g_new0(XrSwapchainImageVulkan2KHR, n);
    for (uint32_t i = 0; i < n; i++) {
        ch->images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR;
    }
    if (XR_FAILED(xr_pfn.EnumerateSwapchainImages(
            ch->swapchain, n, &n, (XrSwapchainImageBaseHeader *)ch->images))) {
        return false;
    }
    ch->num = n;

    VkImageCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = s_nf.depth_fmt,
        .extent = { s_nf.w, s_nf.h, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(c->device, &ici, NULL, &ch->depth) != VK_SUCCESS) {
        ch->depth = VK_NULL_HANDLE;
        return false;
    }
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(c->device, ch->depth, &mr);
    uint32_t type = pgraph_vk_get_memory_type(
        pg, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == 0xFFFFFFFF) {
        type = pgraph_vk_get_memory_type(pg, mr.memoryTypeBits, 0);
    }
    if (type == 0xFFFFFFFF) {
        return false;
    }
    VkMemoryAllocateInfo mai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size,
        .memoryTypeIndex = type,
    };
    if (vkAllocateMemory(c->device, &mai, NULL, &ch->depth_mem) != VK_SUCCESS) {
        ch->depth_mem = VK_NULL_HANDLE;
        return false;
    }
    if (vkBindImageMemory(c->device, ch->depth, ch->depth_mem, 0) != VK_SUCCESS) {
        return false;
    }
    VkImageViewCreateInfo dvi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = ch->depth,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = s_nf.depth_fmt,
        .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 },
    };
    if (vkCreateImageView(c->device, &dvi, NULL, &ch->depth_view) != VK_SUCCESS) {
        ch->depth_view = VK_NULL_HANDLE;
        return false;
    }

    ch->views = g_new0(VkImageView, n);
    ch->fbs = g_new0(VkFramebuffer, n);
    for (uint32_t i = 0; i < n; i++) {
        VkImageViewCreateInfo vi = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = ch->images[i].image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = s_nf.color_fmt,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        };
        if (vkCreateImageView(c->device, &vi, NULL, &ch->views[i]) != VK_SUCCESS) {
            ch->views[i] = VK_NULL_HANDLE;
            return false;
        }
        VkImageView atts[2] = { ch->views[i], ch->depth_view };
        VkFramebufferCreateInfo fci = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = s_nf.rp,
            .attachmentCount = 2,
            .pAttachments = atts,
            .width = s_nf.w,
            .height = s_nf.h,
            .layers = 1,
        };
        if (vkCreateFramebuffer(c->device, &fci, NULL, &ch->fbs[i]) != VK_SUCCESS) {
            ch->fbs[i] = VK_NULL_HANDLE;
            return false;
        }
    }
    return true;
}

static bool nf_create_render_pass(VkDevice dev)
{
    VkAttachmentDescription atts[2] = {
        {
            .format = s_nf.color_fmt,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
        {
            .format = s_nf.depth_fmt,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        },
    };
    VkAttachmentReference color_ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depth_ref = {
        1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };
    VkSubpassDescription sub = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref,
        .pDepthStencilAttachment = &depth_ref,
    };
    VkSubpassDependency dep = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
    };
    VkRenderPassCreateInfo rci = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 2,
        .pAttachments = atts,
        .subpassCount = 1,
        .pSubpasses = &sub,
        .dependencyCount = 1,
        .pDependencies = &dep,
    };
    if (vkCreateRenderPass(dev, &rci, NULL, &s_nf.rp) != VK_SUCCESS) {
        s_nf.rp = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

static bool nf_create_pipeline(PGRAPHVkState *r)
{
    s_nf.vs = pgraph_vk_create_shader_module_from_glsl(
        r, VK_SHADER_STAGE_VERTEX_BIT,
        s_nf.glove ? nf_glove_vert_glsl : nf_vert_glsl);
    s_nf.fs = pgraph_vk_create_shader_module_from_glsl(
        r, VK_SHADER_STAGE_FRAGMENT_BIT,
        s_nf.glove ? nf_glove_frag_glsl : nf_frag_glsl);
    if (s_nf.vs == NULL || s_nf.fs == NULL) {
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = s_nf.vs->module,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = s_nf.fs->module,
            .pName = "main",
        },
    };
    VkVertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(VrNfVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkVertexInputAttributeDescription attrs[2] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
          .offset = offsetof(VrNfVertex, p) },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
          .offset = offsetof(VrNfVertex, n) },
    };
    VkVertexInputBindingDescription glove_binding = {
        .binding = 0,
        .stride = sizeof(VrNfVertexC),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkVertexInputAttributeDescription glove_attrs[3] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
          .offset = offsetof(VrNfVertexC, p) },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
          .offset = offsetof(VrNfVertexC, n) },
        { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
          .offset = offsetof(VrNfVertexC, c) },
    };
    VkPipelineVertexInputStateCreateInfo vin = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = s_nf.glove ? &glove_binding : &binding,
        .vertexAttributeDescriptionCount = s_nf.glove ? 3 : 2,
        .pVertexAttributeDescriptions = s_nf.glove ? glove_attrs : attrs,
    };
    VkPipelineInputAssemblyStateCreateInfo ia = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo vp = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = s_nf.glove ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineDepthStencilStateCreateInfo ds = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
    };
    VkPipelineColorBlendAttachmentState cba = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &cba,
    };
    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dsi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dyn,
    };
    VkPushConstantRange pcr = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(NfPush),
    };
    VkPipelineLayoutCreateInfo pli = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pcr,
    };
    if (vkCreatePipelineLayout(r->device, &pli, NULL, &s_nf.layout) != VK_SUCCESS) {
        s_nf.layout = VK_NULL_HANDLE;
        return false;
    }
    VkGraphicsPipelineCreateInfo gpi = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vin,
        .pInputAssemblyState = &ia,
        .pViewportState = &vp,
        .pRasterizationState = &rs,
        .pMultisampleState = &ms,
        .pDepthStencilState = &ds,
        .pColorBlendState = &cb,
        .pDynamicState = &dsi,
        .layout = s_nf.layout,
        .renderPass = s_nf.rp,
        .subpass = 0,
    };
    if (vkCreateGraphicsPipelines(r->device, r->vk_pipeline_cache, 1, &gpi,
                                  NULL, &s_nf.pipe) != VK_SUCCESS) {
        s_nf.pipe = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

static bool nf_create_buffer(PGRAPHState *pg, VkDeviceSize size,
                             VkBufferUsageFlags usage, VkBuffer *buf,
                             VkDeviceMemory *mem, void **map)
{
    VRCompositor *c = &g_xemu_vr.comp;
    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(c->device, &bci, NULL, buf) != VK_SUCCESS) {
        *buf = VK_NULL_HANDLE;
        return false;
    }
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(c->device, *buf, &mr);
    const uint32_t type = pgraph_vk_get_memory_type(
        pg, mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (type == 0xFFFFFFFF) {
        return false;
    }
    VkMemoryAllocateInfo mai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size,
        .memoryTypeIndex = type,
    };
    if (vkAllocateMemory(c->device, &mai, NULL, mem) != VK_SUCCESS) {
        *mem = VK_NULL_HANDLE;
        return false;
    }
    if (vkBindBufferMemory(c->device, *buf, *mem, 0) != VK_SUCCESS ||
        vkMapMemory(c->device, *mem, 0, size, 0, map) != VK_SUCCESS) {
        *map = NULL;
        return false;
    }
    return true;
}

static void nf_destroy_chain(VkDevice dev, int e)
{
    NfChain *ch = &s_nf.chain[e];
    for (uint32_t i = 0; ch->fbs && i < ch->num; i++) {
        if (ch->fbs[i]) {
            vkDestroyFramebuffer(dev, ch->fbs[i], NULL);
        }
    }
    for (uint32_t i = 0; ch->views && i < ch->num; i++) {
        if (ch->views[i]) {
            vkDestroyImageView(dev, ch->views[i], NULL);
        }
    }
    g_free(ch->fbs);
    g_free(ch->views);
    g_free(ch->images);
    if (ch->depth_view) {
        vkDestroyImageView(dev, ch->depth_view, NULL);
    }
    if (ch->depth) {
        vkDestroyImage(dev, ch->depth, NULL);
    }
    if (ch->depth_mem) {
        vkFreeMemory(dev, ch->depth_mem, NULL);
    }
    if (ch->swapchain != XR_NULL_HANDLE) {
        xr_pfn.DestroySwapchain(ch->swapchain);
    }
    memset(ch, 0, sizeof(*ch));
}

static void nf_destroy_all(PGRAPHState *pg)
{
    VRCompositor *c = &g_xemu_vr.comp;
    const VkDevice dev = c->device;
    PGRAPHVkState *r = pg ? pg->vk_renderer_state : NULL;

    if (dev == VK_NULL_HANDLE) {
        return;
    }
    if (s_nf.fence_submitted && s_nf.fence) {
        vkWaitForFences(dev, 1, &s_nf.fence, VK_TRUE, NF_ONE_SECOND_NS);
        s_nf.fence_submitted = false;
    }
    vr_hands_destroy();
    for (int e = 0; e < 2; e++) {
        nf_destroy_chain(dev, e);
    }
    if (s_nf.pipe) {
        vkDestroyPipeline(dev, s_nf.pipe, NULL);
    }
    if (s_nf.layout) {
        vkDestroyPipelineLayout(dev, s_nf.layout, NULL);
    }
    if (r && s_nf.vs) {
        pgraph_vk_destroy_shader_module(r, s_nf.vs);
    }
    if (r && s_nf.fs) {
        pgraph_vk_destroy_shader_module(r, s_nf.fs);
    }
    if (s_nf.rp) {
        vkDestroyRenderPass(dev, s_nf.rp, NULL);
    }
    if (s_nf.vb_map) {
        vkUnmapMemory(dev, s_nf.vb_mem);
    }
    if (s_nf.ib_map) {
        vkUnmapMemory(dev, s_nf.ib_mem);
    }
    if (s_nf.vb) {
        vkDestroyBuffer(dev, s_nf.vb, NULL);
    }
    if (s_nf.ib) {
        vkDestroyBuffer(dev, s_nf.ib, NULL);
    }
    if (s_nf.vb_mem) {
        vkFreeMemory(dev, s_nf.vb_mem, NULL);
    }
    if (s_nf.ib_mem) {
        vkFreeMemory(dev, s_nf.ib_mem, NULL);
    }
    if (s_nf.fence) {
        vkDestroyFence(dev, s_nf.fence, NULL);
    }
    if (s_nf.pool) {
        vkDestroyCommandPool(dev, s_nf.pool, NULL);
    }
    s_nf.pipe = VK_NULL_HANDLE;
    s_nf.layout = VK_NULL_HANDLE;
    s_nf.vs = s_nf.fs = NULL;
    s_nf.rp = VK_NULL_HANDLE;
    s_nf.vb = s_nf.ib = VK_NULL_HANDLE;
    s_nf.vb_mem = s_nf.ib_mem = VK_NULL_HANDLE;
    s_nf.vb_map = s_nf.ib_map = NULL;
    s_nf.fence = VK_NULL_HANDLE;
    s_nf.pool = VK_NULL_HANDLE;
    s_nf.cmd = VK_NULL_HANDLE;
    s_nf.created = false;
    for (int h = 0; h < 2; h++) {
        vr_hands_gltf_free(&s_nf.gl[h]);
    }
    s_nf.glove = false;
}

static void nf_glove_load(void)
{
    s_nf.glove = s_nf.env_model_set ? s_nf.env_glove :
                 g_config.vr.hands_model == CONFIG_VR_HANDS_MODEL_GLOVE;
    if (!s_nf.glove) {
        return;
    }
    static const XrHandEXT side[2] = { XR_HAND_LEFT_EXT, XR_HAND_RIGHT_EXT };
    const unsigned char *data[2] = { glove_v1_1_left_data, glove_v1_1_right_data };
    const unsigned int size[2] = { glove_v1_1_left_size, glove_v1_1_right_size };
    for (int h = 0; h < 2; h++) {
        char why[256];
        if (!vr_hands_gltf_load(data[h], size[h], side[h], VR_GLTF_SAB_NONE,
                                &s_nf.gl[h], why, sizeof(why))) {
            fprintf(stderr, "PenguinBox: near-field: the %s glove was refused "
                            "(%s); drawing the capsules\n",
                    h ? "right" : "left", why);
            vr_hands_gltf_free(&s_nf.gl[0]);
            vr_hands_gltf_free(&s_nf.gl[1]);
            s_nf.glove = false;
            return;
        }
    }
    fprintf(stderr, "PenguinBox: near-field: hands model glove: left %u "
                    "vertices %u triangles, right %u vertices %u triangles, "
                    "skinned on the CPU to the 26 joints (wrist to middle tip "
                    "%.3f m at rest)\n",
            s_nf.gl[0].nv, s_nf.gl[0].ni / 3, s_nf.gl[1].nv, s_nf.gl[1].ni / 3,
            (double)s_nf.gl[1].wrist_to_middle_tip_m);
}

static PGRAPHState *s_nf_pg;

static bool nf_ensure(PGRAPHState *pg)
{
    VRCompositor *c = &g_xemu_vr.comp;
    PGRAPHVkState *r = pg->vk_renderer_state;

    if (s_nf.created) {
        return true;
    }
    if (s_nf.failed) {
        return false;
    }
    s_nf_pg = pg;
    if (!nf_pick_color_format()) {
        nf_fail("no usable colour swapchain format (R8G8B8A8/B8G8R8A8 SRGB "
                "or UNORM) in the runtime's list");
        return false;
    }
    if (!nf_pick_depth_format(r)) {
        nf_fail("no depth format with DEPTH_STENCIL_ATTACHMENT support");
        return false;
    }
    if (!nf_pick_size()) {
        nf_fail("xrEnumerateViewConfigurationViews gave no usable size");
        return false;
    }
    nf_glove_load();
    if (!nf_create_render_pass(c->device)) {
        nf_fail("vkCreateRenderPass failed");
        goto fail;
    }
    if (!nf_create_pipeline(r)) {
        nf_fail("pipeline creation failed");
        goto fail;
    }
    for (int e = 0; e < 2; e++) {
        if (!nf_create_chain(pg, e)) {
            nf_fail("swapchain, depth image or framebuffer creation failed");
            goto fail;
        }
    }
    if (!nf_create_buffer(pg, s_nf.glove ?
                              (VkDeviceSize)NF_GLOVE_VERTS * sizeof(VrNfVertexC) :
                              (VkDeviceSize)NF_MAX_VERTS * sizeof(VrNfVertex),
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &s_nf.vb,
                          &s_nf.vb_mem, &s_nf.vb_map) ||
        !nf_create_buffer(pg, (VkDeviceSize)(s_nf.glove ? NF_GLOVE_INDICES :
                                                          NF_MAX_INDICES) *
                                  sizeof(uint32_t),
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &s_nf.ib,
                          &s_nf.ib_mem, &s_nf.ib_map)) {
        nf_fail("vertex/index buffer creation failed");
        goto fail;
    }
    if (s_nf.glove) {
        uint32_t *ib = s_nf.ib_map;
        s_nf.gl_first[0] = 0;
        s_nf.gl_first[1] = s_nf.gl[0].ni;
        memcpy(ib, s_nf.gl[0].idx, s_nf.gl[0].ni * sizeof(uint32_t));
        for (uint32_t i = 0; i < s_nf.gl[1].ni; i++) {
            ib[s_nf.gl[0].ni + i] = s_nf.gl[1].idx[i] + s_nf.gl[0].nv;
        }
    }
    VkCommandPoolCreateInfo pci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = c->queue_family,
    };
    if (vkCreateCommandPool(c->device, &pci, NULL, &s_nf.pool) != VK_SUCCESS) {
        s_nf.pool = VK_NULL_HANDLE;
        nf_fail("vkCreateCommandPool failed");
        goto fail;
    }
    VkCommandBufferAllocateInfo cbai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = s_nf.pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (vkAllocateCommandBuffers(c->device, &cbai, &s_nf.cmd) != VK_SUCCESS) {
        s_nf.cmd = VK_NULL_HANDLE;
        nf_fail("vkAllocateCommandBuffers failed");
        goto fail;
    }
    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    if (vkCreateFence(c->device, &fci, NULL, &s_nf.fence) != VK_SUCCESS) {
        s_nf.fence = VK_NULL_HANDLE;
        nf_fail("vkCreateFence failed");
        goto fail;
    }
    s_nf.created = true;
    fprintf(stderr, "PenguinBox: near-field: layer created: %ux%u per eye "
                    "(hands_scale %.2f of %ux%u), "
                    "colour format %d, depth format %d, %u+%u images, "
                    "premultiplied SOURCE_ALPHA, LOCAL space\n",
            s_nf.w, s_nf.h, (double)s_nf.scale, s_nf.full_w, s_nf.full_h,
            (int)s_nf.color_fmt, (int)s_nf.depth_fmt,
            s_nf.chain[0].num, s_nf.chain[1].num);
    vr_hands_init();
    return true;

fail:
    nf_destroy_all(pg);
    s_nf.failed = true;
    return false;
}

static int nf_acquire(NfChain *ch)
{
    uint32_t index = 0;
    if (ch->wait_pending) {
        index = ch->pending_index;
    } else {
        XrSwapchainImageAcquireInfo ai = {
            .type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO,
        };
        XrResult res = xr_pfn.AcquireSwapchainImage(ch->swapchain, &ai, &index);
        if (XR_FAILED(res)) {
            if (!s_nf.warned_acquire) {
                s_nf.warned_acquire = true;
                fprintf(stderr, "PenguinBox: near-field: xrAcquireSwapchainImage "
                                "failed (%d)\n", (int)res);
            }
            return -1;
        }
        ch->pending_index = index;
        ch->wait_pending = true;
    }
    XrSwapchainImageWaitInfo wi = {
        .type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO,
        .timeout = NF_ONE_SECOND_NS,
    };
    XrResult res = xr_pfn.WaitSwapchainImage(ch->swapchain, &wi);
    if (XR_FAILED(res) || res == XR_TIMEOUT_EXPIRED) {
        if (!s_nf.warned_wait) {
            s_nf.warned_wait = true;
            fprintf(stderr, "PenguinBox: near-field: xrWaitSwapchainImage "
                            "%s (%d)\n",
                    res == XR_TIMEOUT_EXPIRED ? "timed out" : "failed", (int)res);
        }
        return -1;
    }
    ch->wait_pending = false;
    return (int)index;
}

static void nf_release(NfChain *ch)
{
    XrSwapchainImageReleaseInfo ri = {
        .type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO,
    };
    if (XR_FAILED(xr_pfn.ReleaseSwapchainImage(ch->swapchain, &ri))) {
        if (!s_nf.warned_release) {
            s_nf.warned_release = true;
            fprintf(stderr, "PenguinBox: near-field: xrReleaseSwapchainImage "
                            "failed\n");
        }
        return;
    }
    ch->ever_released = true;
}

static void nf_record_eye(VkCommandBuffer cmd, int e, uint32_t index,
                          const XrView *view)
{
    NfChain *ch = &s_nf.chain[e];
    VkClearValue clears[2] = {
        { .color = { { 0.0f, 0.0f, 0.0f, 0.0f } } },
        { .depthStencil = { 1.0f, 0 } },
    };
    VkRenderPassBeginInfo rbi = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = s_nf.rp,
        .framebuffer = ch->fbs[index],
        .renderArea = { { 0, 0 }, { s_nf.w, s_nf.h } },
        .clearValueCount = 2,
        .pClearValues = clears,
    };
    vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    if (s_nf.draw_indices > 0) {
        VkViewport vp = { 0.0f, 0.0f, (float)s_nf.w, (float)s_nf.h, 0.0f, 1.0f };
        VkRect2D sc = { { 0, 0 }, { s_nf.w, s_nf.h } };
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_nf.pipe);
        VkDeviceSize zero = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &s_nf.vb, &zero);
        vkCmdBindIndexBuffer(cmd, s_nf.ib, 0, VK_INDEX_TYPE_UINT32);

        NfPush pc;
        float v[16], p[16];
        vr_nf_view_matrix(&view->pose, v);
        vr_nf_projection(&view->fov, NF_NEAR_M, NF_FAR_M, VR_NF_SAB_NONE, p);
        vr_nf_mat_mul(p, v, pc.vp);
        pc.colour[0] = s_nf.glove ? 1.0f : 0.16f;
        pc.colour[1] = s_nf.glove ? 1.0f : 0.16f;
        pc.colour[2] = s_nf.glove ? 1.0f : 0.18f;
        pc.colour[3] = 1.0f;
        pc.eye[0] = view->pose.position.x;
        pc.eye[1] = view->pose.position.y;
        pc.eye[2] = view->pose.position.z;
        pc.eye[3] = 1.0f;
        vkCmdPushConstants(cmd, s_nf.layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pc), &pc);
        if (s_nf.glove) {
            for (int h = 0; h < 2; h++) {
                if (s_nf.gl_drawn[h]) {
                    vkCmdDrawIndexed(cmd, s_nf.gl[h].ni, 1, s_nf.gl_first[h], 0, 0);
                }
            }
        } else {
            vkCmdDrawIndexed(cmd, s_nf.draw_indices, 1, 0, 0, 0);
        }
    }
    vkCmdEndRenderPass(cmd);
}

static void nf_trace_skip(const char *reason)
{
    const VRXrSession *xr = &g_xemu_vr.xr;
    const VrHandsTraceFrame fx = {
        .t_us = g_get_monotonic_time(),
        .utc_us = g_get_real_time(),
        .space = "LOCAL",
        .session = vr_hands_trace_session_name((int)xr->session_state),
    };
    vr_hands_trace_skipped(reason, &fx);
}

static bool nf_frame(PGRAPHState *pg, XrTime display_time, bool should_render,
                     XrCompositionLayerProjection *layer,
                     XrCompositionLayerProjectionView views[2])
{
    VRCompositor *c = &g_xemu_vr.comp;

    bool wanted = vr_nearfield_wanted();
    bool say = wanted;
    if (say != s_nf.last_wanted || !s_nf.said_state) {
        s_nf.last_wanted = say;
        s_nf.said_state = true;
        fprintf(stderr, "PenguinBox: near-field: hands %s%s\n",
                say ? "tracked" : "off",
                say ? (g_xemu_vr.xr.hand_tracking_supported ?
                              " (XR_EXT_hand_tracking)" :
                              " requested, but XR_EXT_hand_tracking is not "
                              "available: the layer is drawn empty") :
                         "");
    }
    if (!wanted || !should_render) {
        if (vr_hands_trace_active()) {
            nf_trace_skip(!wanted ? "layer_off" : "not_rendered");
        }
        return false;
    }
    if (!nf_ensure(pg)) {
        if (vr_hands_trace_active()) {
            nf_trace_skip("layer_not_ready");
        }
        return false;
    }

    XrView xv[2];
    if (!vr_compositor_views_at(display_time, xv)) {
        s_nf.views_missed++;
        if (!s_nf.warned_views) {
            s_nf.warned_views = true;
            fprintf(stderr, "PenguinBox: near-field: no located views for this "
                            "display time; the layer is omitted on such "
                            "frames (counted)\n");
        }
        if (vr_hands_trace_active()) {
            nf_trace_skip("views_missed");
        }
        return false;
    }

    VrHandJoints hands[2];
    int64_t cost_us = 0;
    vr_hands_locate(display_time, hands, &cost_us);
    vr_input_feed_tracked_hands(hands);
    for (int h = 0; h < 2; h++) {
        vr_hands_filter(&hands[h], &s_nf.hold[h], display_time, &s_nf.filter,
                        VR_HANDS_SAB_NONE);
    }
    if (vr_hands_ready()) {
        s_nf.joint_sum_us += cost_us;
        s_nf.joint_max_us = MAX(s_nf.joint_max_us, cost_us);
        s_nf.joint_n++;
    }
    bool needed;
    if (s_nf.glove) {
        needed = false;
        for (int h = 0; h < 2; h++) {
            s_nf.gl_drawn[h] = vr_hands_gltf_drawable(&s_nf.gl[h], &hands[h],
                                                      VR_GLTF_SAB_NONE);
            needed |= s_nf.gl_drawn[h];
            if (hands[h].active && !s_nf.gl_drawn[h]) {
                s_nf.gl_hidden[h]++;
            }
        }
    } else {
        needed = vr_hands_layer_needed(hands, VR_HANDS_SAB_NONE);
    }
    if (!needed) {
        s_nf.hands_drawn = false;
        s_nf.draw_indices = 0;
        s_nf.frames_no_hand++;
        if (s_nf.frames_no_hand % NF_RATE_EVERY == 0) {
            fprintf(stderr, "PenguinBox: near-field: no hand to draw on %u "
                            "frames so far: no layer on them (%u frames with "
                            "a layer)\n",
                    s_nf.frames_no_hand, s_nf.frames_with_layer);
        }
        return false;
    }

    if (s_nf.fence_submitted) {
        VkResult wr = vkWaitForFences(c->device, 1, &s_nf.fence, VK_TRUE,
                                      NF_ONE_SECOND_NS);
        if (wr != VK_SUCCESS) {
            if (!s_nf.warned_fence) {
                s_nf.warned_fence = true;
                fprintf(stderr, "PenguinBox: near-field: fence wait returned "
                                "%d; skipping this frame\n", (int)wr);
            }
            return false;
        }
        vkResetFences(c->device, 1, &s_nf.fence);
        s_nf.fence_submitted = false;
    }


    VrNfMesh mesh = {
        .v = s_nf.vb_map,
        .i = s_nf.ib_map,
        .vcap = NF_MAX_VERTS,
        .icap = NF_MAX_INDICES,
    };
    uint32_t capsules = 0;
    if (s_nf.glove) {
        const int64_t t0 = g_get_monotonic_time();
        VrNfVertexC *vb = s_nf.vb_map;
        s_nf.draw_indices = 0;
        for (int h = 0; h < 2; h++) {
            if (s_nf.gl_drawn[h] &&
                vr_hands_gltf_skin(&s_nf.gl[h], &hands[h], VR_GLTF_SAB_NONE,
                                   vb + (h ? s_nf.gl[0].nv : 0)) == s_nf.gl[h].nv) {
                s_nf.draw_indices += s_nf.gl[h].ni;
            } else {
                s_nf.gl_drawn[h] = false;
            }
        }
        const int64_t us = g_get_monotonic_time() - t0;
        s_nf.skin_sum_us += us;
        s_nf.skin_max_us = MAX(s_nf.skin_max_us, us);
        s_nf.skin_n++;
        s_nf.hands_drawn = s_nf.draw_indices > 0;
    } else {
        for (int h = 0; h < 2; h++) {
            capsules += vr_hands_mesh(&mesh, &hands[h], VR_HANDS_SAB_NONE);
        }
        s_nf.draw_indices = mesh.overflow ? 0 : mesh.ni;
        s_nf.hands_drawn = capsules > 0 && s_nf.draw_indices > 0;
    }
    if (mesh.overflow) {
        s_nf.mesh_overflows++;
        if (!s_nf.warned_overflow) {
            s_nf.warned_overflow = true;
            fprintf(stderr, "PenguinBox: near-field: hand mesh overflow (%u "
                            "capsules, over %u vertices or %u indices): no "
                            "hands are drawn on such frames (counted)\n",
                    capsules, mesh.vcap, mesh.icap);
        }
    }

    int idx[2];
    idx[0] = nf_acquire(&s_nf.chain[0]);
    idx[1] = idx[0] >= 0 ? nf_acquire(&s_nf.chain[1]) : -1;
    if (idx[0] < 0 || idx[1] < 0) {
        if (idx[0] >= 0) {
            nf_release(&s_nf.chain[0]);
        }
        return false;
    }

    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    bool submitted = false;
    if (vkBeginCommandBuffer(s_nf.cmd, &bi) == VK_SUCCESS) {
        for (int e = 0; e < 2; e++) {
            nf_record_eye(s_nf.cmd, e, (uint32_t)idx[e], &xv[e]);
        }
        if (vkEndCommandBuffer(s_nf.cmd) == VK_SUCCESS) {
            VkSubmitInfo si = {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .commandBufferCount = 1,
                .pCommandBuffers = &s_nf.cmd,
            };
            if (vkQueueSubmit(c->queue, 1, &si, s_nf.fence) == VK_SUCCESS) {
                s_nf.fence_submitted = true;
                submitted = true;
            }
        }
    }
    if (!submitted && !s_nf.warned_submit) {
        s_nf.warned_submit = true;
        fprintf(stderr, "PenguinBox: near-field: command buffer record/submit "
                        "failed; the layer shows its last image\n");
    }
    nf_release(&s_nf.chain[0]);
    nf_release(&s_nf.chain[1]);
    if (!s_nf.chain[0].ever_released || !s_nf.chain[1].ever_released) {
        return false;
    }

    for (int e = 0; e < 2; e++) {
        views[e] = (XrCompositionLayerProjectionView){
            .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
            .pose = xv[e].pose,
            .fov = xv[e].fov,
            .subImage = {
                .swapchain = s_nf.chain[e].swapchain,
                .imageRect = { { 0, 0 }, { (int32_t)s_nf.w, (int32_t)s_nf.h } },
                .imageArrayIndex = 0,
            },
        };
    }
    *layer = (XrCompositionLayerProjection){
        .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
        .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
        .space = g_xemu_vr.xr.space,
        .viewCount = 2,
        .views = views,
    };
    return true;
}

bool vr_nearfield_frame(PGRAPHState *pg, XrTime display_time, bool should_render,
                        XrCompositionLayerProjection *layer,
                        XrCompositionLayerProjectionView views[2])
{
    return nf_frame(pg, display_time, should_render, layer, views);
}

void vr_nearfield_submitted(uint32_t layer_count)
{
    s_nf.frames_with_layer++;
    s_nf.submits_in_window++;
    if (s_nf.hands_drawn) {
        s_nf.frames_hands_drawn++;
    }
    if (s_nf.submits_in_window >= NF_RATE_EVERY) {
        const int64_t now = g_get_monotonic_time();
        const double secs = (double)(now - s_nf.window_t0) / 1e6;
        fprintf(stderr, "PenguinBox: near-field: layers=%u; %u submits in %.1f s "
                        "(%.1f/s); hands frames=%u drawn=%u no-hand=%u; joints "
                        "mean %lld max %lld us over %u; views missed %u\n",
                layer_count, s_nf.submits_in_window, secs,
                secs > 0 ? s_nf.submits_in_window / secs : 0.0,
                s_nf.frames_with_layer, s_nf.frames_hands_drawn,
                s_nf.frames_no_hand,
                (long long)(s_nf.joint_n ? s_nf.joint_sum_us / s_nf.joint_n : 0),
                (long long)s_nf.joint_max_us, s_nf.joint_n, s_nf.views_missed);
        if (s_nf.glove) {
            fprintf(stderr, "PenguinBox: near-field: glove skin mean %lld max "
                            "%lld us over %u frames; located but not drawable "
                            "(a weighted joint invalid): left %u right %u\n",
                    (long long)(s_nf.skin_n ? s_nf.skin_sum_us / s_nf.skin_n : 0),
                    (long long)s_nf.skin_max_us, s_nf.skin_n,
                    s_nf.gl_hidden[0], s_nf.gl_hidden[1]);
            s_nf.skin_sum_us = 0;
            s_nf.skin_max_us = 0;
            s_nf.skin_n = 0;
        }
        s_nf.submits_in_window = 0;
        s_nf.window_t0 = now;
        s_nf.joint_sum_us = 0;
        s_nf.joint_max_us = 0;
        s_nf.joint_n = 0;
    }
}

void vr_nearfield_stop(void)
{
    if (s_nf.created) {
        nf_destroy_all(s_nf_pg);
        fprintf(stderr, "PenguinBox: near-field: layer destroyed after %u frames "
                        "(%u with hands drawn; %u with no hand, no layer; "
                        "%u mesh overflows)\n",
                s_nf.frames_with_layer, s_nf.frames_hands_drawn,
                s_nf.frames_no_hand, s_nf.mesh_overflows);
    }
    vr_hands_destroy();
    s_nf.created = false;
}
