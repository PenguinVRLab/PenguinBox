/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "qemu/thread.h"

#include "renderer.h"
#include "vr_internal.h"
#include "vr_padxr_actions.h"
#include "vr_public.h"
#include "vr_ui_panel.h"

#define UI_BYTES ((size_t)VR_UI_PANEL_W * VR_UI_PANEL_H * 4)

static QemuMutex s_lock;
static struct {
    uint8_t *pixels;
    uint32_t seq;
} s_pub;
static VrUiPanelInput s_input;
static bool s_input_any;

static bool s_open;
static uint32_t s_events;
static int s_env_test = -1;
static const char *s_env_dump;
static int s_dumped;

static void __attribute__((__constructor__)) vr_ui_panel_init_lock(void)
{
    qemu_mutex_init(&s_lock);
}

static bool env_test(void)
{
    if (s_env_test < 0) {
        const char *e = getenv("XEMU_VR_UIPANEL_TEST");
        s_env_dump = getenv("XEMU_VR_UIPANEL_DUMP");
        s_env_test = (e && *e && strcmp(e, "0") != 0) ? 1 : 0;
    }
    return s_env_test == 1;
}

bool vr_ui_panel_wanted(void)
{
    return xemu_vr_active() && (qatomic_read(&s_open) || env_test());
}

void vr_ui_panel_publish(const uint8_t *rgba, uint32_t w, uint32_t h)
{
    if (rgba == NULL || w != VR_UI_PANEL_W || h != VR_UI_PANEL_H) {
        return;
    }
    env_test();
    qemu_mutex_lock(&s_lock);
    if (s_pub.pixels == NULL) {
        s_pub.pixels = g_malloc(UI_BYTES);
    }
#ifdef XEMU_VR_UIPANEL_PLANTED_NODRAW
    memset(s_pub.pixels, 0, UI_BYTES);
#else
    memcpy(s_pub.pixels, rgba, UI_BYTES);
#endif
    s_pub.seq++;
    uint32_t seq = s_pub.seq;
    if (s_env_dump && *s_env_dump && s_dumped < 8) {
        g_autofree char *path =
            g_strdup_printf("%s/uipanel-%u.rgba", s_env_dump, seq);
        FILE *f = fopen(path, "wb");
        if (f) {
            fwrite(s_pub.pixels, 1, UI_BYTES, f);
            fclose(f);
            s_dumped++;
            fprintf(stderr, "PenguinBox: UI panel dump %s (%ux%u RGBA)\n",
                    path, (unsigned)w, (unsigned)h);
        }
    }
    qemu_mutex_unlock(&s_lock);
}

void vr_ui_panel_set_open(bool open)
{
    if (qatomic_xchg(&s_open, open) != open) {
        fprintf(stderr, "PenguinBox: UI panel %s\n",
                open ? "open (owns input)" : "closed");
    }
}

uint32_t vr_ui_panel_take_events(void)
{
    return qatomic_xchg(&s_events, 0);
}

bool vr_ui_panel_take_input(VrUiPanelInput *out)
{
    qemu_mutex_lock(&s_lock);
    bool any = s_input_any;
    *out = s_input;
    qemu_mutex_unlock(&s_lock);
    return any;
}

void vr_ui_panel_post_events(uint32_t events)
{
    qatomic_or(&s_events, events);
}

void vr_ui_panel_post_input(const VrUiPanelInput *in)
{
    qemu_mutex_lock(&s_lock);
    uint32_t seq = s_input.seq + 1;
    s_input = *in;
    s_input.seq = seq;
    s_input_any = true;
    qemu_mutex_unlock(&s_lock);
}

bool vr_ui_panel_owns_input(void)
{
    return qatomic_read(&s_open);
}

static struct {
    VREyeChain chain;
    VkBuffer staging;
    VkDeviceMemory staging_mem;
    void *mapped;
    VkCommandBuffer cmd;
    VkFence fence;
    bool fence_submitted;
    bool failed_once;
    bool warned;
    uint32_t uploaded_seq;
    bool upload_owed;
    bool test_drawn;
    bool placed;
    bool was_shown;
    VrUiPanelPose pose;
    VrUiComeback comeback;
    uint32_t uploads;
} s_ui;

static void draw_test_pattern(uint8_t *px)
{
    for (uint32_t y = 0; y < VR_UI_PANEL_H; y++) {
        for (uint32_t x = 0; x < VR_UI_PANEL_W; x++) {
            uint8_t *p = px + ((size_t)y * VR_UI_PANEL_W + x) * 4;
            bool border = x < 8 || y < 8 || x >= VR_UI_PANEL_W - 8 ||
                          y >= VR_UI_PANEL_H - 8;
            bool cross = (x >= VR_UI_PANEL_W / 2 - 2 &&
                          x < VR_UI_PANEL_W / 2 + 2) ||
                         (y >= VR_UI_PANEL_H / 2 - 2 &&
                          y < VR_UI_PANEL_H / 2 + 2);
            bool dark = ((x / 64) + (y / 64)) & 1;
            p[0] = border || cross ? 255 : (dark ? 20 : 40);
            p[1] = border || cross ? 255 : (dark ? 60 : 110);
            p[2] = border || cross ? 255 : (dark ? 140 : 200);
            p[3] = border || cross ? 255 : 200;
        }
    }
}

static void ui_barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from,
                       VkImageLayout to, VkAccessFlags src_a,
                       VkAccessFlags dst_a, VkPipelineStageFlags src_s,
                       VkPipelineStageFlags dst_s)
{
    VkImageMemoryBarrier b = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = src_a,
        .dstAccessMask = dst_a,
        .oldLayout = from,
        .newLayout = to,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(cmd, src_s, dst_s, 0, 0, NULL, 0, NULL, 1, &b);
}

void vr_ui_panel_destroy(VRCompositor *c)
{
    if (s_ui.fence != VK_NULL_HANDLE && s_ui.fence_submitted) {
        if (vkWaitForFences(c->device, 1, &s_ui.fence, VK_TRUE,
                            1000000000ull) == VK_TIMEOUT) {
            fprintf(stderr, "PenguinBox: UI panel copy fence still busy "
                            "after 1s during teardown.\n");
        }
    }
    if (s_ui.chain.swapchain != XR_NULL_HANDLE) {
        xr_pfn.DestroySwapchain(s_ui.chain.swapchain);
    }
    g_free(s_ui.chain.images);
    if (s_ui.mapped != NULL) {
        vkUnmapMemory(c->device, s_ui.staging_mem);
    }
    if (s_ui.staging != VK_NULL_HANDLE) {
        vkDestroyBuffer(c->device, s_ui.staging, NULL);
    }
    if (s_ui.staging_mem != VK_NULL_HANDLE) {
        vkFreeMemory(c->device, s_ui.staging_mem, NULL);
    }
    if (s_ui.cmd != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(c->device, c->cmd_pool, 1, &s_ui.cmd);
    }
    if (s_ui.fence != VK_NULL_HANDLE) {
        vkDestroyFence(c->device, s_ui.fence, NULL);
    }
    memset(&s_ui, 0, sizeof(s_ui));
}

static bool ui_fail(VRCompositor *c, const char *what)
{
    if (!s_ui.warned) {
        s_ui.warned = true;
        fprintf(stderr, "PenguinBox: UI panel %s failed; the panel stays "
                        "hidden this session.\n", what);
    }
    vr_ui_panel_destroy(c);
    s_ui.failed_once = true;
    s_ui.warned = true;
    return false;
}

static bool ensure_chain(VRCompositor *c, PGRAPHState *pg)
{
    if (s_ui.chain.swapchain != XR_NULL_HANDLE) {
        return true;
    }
    if (s_ui.failed_once) {
        return false;
    }
    XrSwapchainCreateInfo ci = {
        .type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
        .usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                      XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT,
        .format = (int64_t)VK_FORMAT_R8G8B8A8_SRGB,
        .sampleCount = 1,
        .width = VR_UI_PANEL_W,
        .height = VR_UI_PANEL_H,
        .faceCount = 1,
        .arraySize = 1,
        .mipCount = 1,
    };
    if (XR_FAILED(xr_pfn.CreateSwapchain(g_xemu_vr.xr.session, &ci,
                                         &s_ui.chain.swapchain))) {
        s_ui.chain.swapchain = XR_NULL_HANDLE;
        return ui_fail(c, "xrCreateSwapchain");
    }
    uint32_t count = 0;
    if (XR_FAILED(xr_pfn.EnumerateSwapchainImages(s_ui.chain.swapchain, 0,
                                                  &count, NULL)) ||
        count == 0) {
        return ui_fail(c, "xrEnumerateSwapchainImages");
    }
    s_ui.chain.images = g_new0(XrSwapchainImageVulkan2KHR, count);
    for (uint32_t i = 0; i < count; i++) {
        s_ui.chain.images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR;
    }
    if (XR_FAILED(xr_pfn.EnumerateSwapchainImages(
            s_ui.chain.swapchain, count, &count,
            (XrSwapchainImageBaseHeader *)s_ui.chain.images))) {
        return ui_fail(c, "xrEnumerateSwapchainImages");
    }
    s_ui.chain.num_images = count;

    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = UI_BYTES,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(c->device, &bci, NULL, &s_ui.staging) != VK_SUCCESS) {
        s_ui.staging = VK_NULL_HANDLE;
        return ui_fail(c, "staging vkCreateBuffer");
    }
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(c->device, s_ui.staging, &mr);
    uint32_t mt = pgraph_vk_get_memory_type(
        pg, mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == 0xFFFFFFFF) {
        return ui_fail(c, "staging memory type");
    }
    VkMemoryAllocateInfo mai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size,
        .memoryTypeIndex = mt,
    };
    if (vkAllocateMemory(c->device, &mai, NULL, &s_ui.staging_mem) !=
        VK_SUCCESS) {
        s_ui.staging_mem = VK_NULL_HANDLE;
        return ui_fail(c, "staging vkAllocateMemory");
    }
    if (vkBindBufferMemory(c->device, s_ui.staging, s_ui.staging_mem, 0) !=
            VK_SUCCESS ||
        vkMapMemory(c->device, s_ui.staging_mem, 0, UI_BYTES, 0,
                    &s_ui.mapped) != VK_SUCCESS) {
        s_ui.mapped = NULL;
        return ui_fail(c, "staging bind/map");
    }
    memset(s_ui.mapped, 0, UI_BYTES);

    VkCommandBufferAllocateInfo cbai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = c->cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (vkAllocateCommandBuffers(c->device, &cbai, &s_ui.cmd) != VK_SUCCESS) {
        s_ui.cmd = VK_NULL_HANDLE;
        return ui_fail(c, "vkAllocateCommandBuffers");
    }
    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    if (vkCreateFence(c->device, &fci, NULL, &s_ui.fence) != VK_SUCCESS) {
        s_ui.fence = VK_NULL_HANDLE;
        return ui_fail(c, "vkCreateFence");
    }
    fprintf(stderr, "PenguinBox: UI panel swapchain created: %ux%u, %u images "
                    "(VK_FORMAT_R8G8B8A8_SRGB).\n",
            (unsigned)VR_UI_PANEL_W, (unsigned)VR_UI_PANEL_H, count);
    return true;
}

static bool upload(VRCompositor *c)
{
    VREyeChain *ch = &s_ui.chain;
    uint32_t index = 0;

    if (ch->wait_pending) {
        index = ch->pending_index;
    } else {
        XrSwapchainImageAcquireInfo ai = {
            .type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO,
        };
        if (XR_FAILED(xr_pfn.AcquireSwapchainImage(ch->swapchain, &ai,
                                                   &index))) {
            return false;
        }
        ch->pending_index = index;
        ch->wait_pending = true;
    }
    XrSwapchainImageWaitInfo wi = {
        .type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO,
        .timeout = 1000000000ll,
    };
    XrResult res = xr_pfn.WaitSwapchainImage(ch->swapchain, &wi);
    if (res == XR_TIMEOUT_EXPIRED || XR_FAILED(res)) {
        return false;
    }
    ch->wait_pending = false;

    VkImage dst = ch->images[index].image;
    VkCommandBuffer cmd = s_ui.cmd;
    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    bool copied = false;
    if (vkBeginCommandBuffer(cmd, &bi) == VK_SUCCESS) {
        VkBufferMemoryBarrier hb = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = s_ui.staging,
            .size = VK_WHOLE_SIZE,
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1,
                             &hb, 0, NULL);
        ui_barrier(cmd, dst, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                       VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
                   VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy region = {
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageExtent = { VR_UI_PANEL_W, VR_UI_PANEL_H, 1 },
        };
        vkCmdCopyBufferToImage(cmd, s_ui.staging, dst,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &region);
        ui_barrier(cmd, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        if (vkEndCommandBuffer(cmd) == VK_SUCCESS) {
            VkSubmitInfo si = {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .commandBufferCount = 1,
                .pCommandBuffers = &cmd,
            };
            if (vkQueueSubmit(c->queue, 1, &si, s_ui.fence) == VK_SUCCESS) {
                s_ui.fence_submitted = true;
                copied = true;
            }
        }
    }
    XrSwapchainImageReleaseInfo ri = {
        .type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO,
    };
    if (XR_FAILED(xr_pfn.ReleaseSwapchainImage(ch->swapchain, &ri))) {
        return false;
    }
    if (copied) {
        ch->ever_released = true;
    }
    return copied;
}

static bool shown(void)
{
    return qatomic_read(&s_open) || env_test();
}

void vr_ui_panel_frame(VRCompositor *c, PGRAPHState *pg, int64_t now_ms)
{
    const bool show = shown();

    if (show && c->head_pose_valid) {
        const float hp[3] = { c->head_pose.position.x, c->head_pose.position.y,
                              c->head_pose.position.z };
        const float hq[4] = { c->head_pose.orientation.x,
                              c->head_pose.orientation.y,
                              c->head_pose.orientation.z,
                              c->head_pose.orientation.w };
        if (!s_ui.was_shown || !s_ui.placed) {
            s_ui.pose = vr_ui_panel_place(hp, hq);
            s_ui.placed = true;
            s_ui.comeback = (VrUiComeback){ 0 };
        } else if (vr_ui_panel_comeback_step(&s_ui.comeback,
                                             vr_ui_panel_yaw_of(hq),
                                             s_ui.pose.yaw, now_ms)) {
            s_ui.pose = vr_ui_panel_place(hp, hq);
        }
    }
    s_ui.was_shown = show;
    if (!show) {
        return;
    }
    if (!ensure_chain(c, pg)) {
        return;
    }

    if (s_ui.fence_submitted) {
        if (vkWaitForFences(c->device, 1, &s_ui.fence, VK_TRUE, 0) !=
            VK_SUCCESS) {
            return;
        }
        vkResetFences(c->device, 1, &s_ui.fence);
        s_ui.fence_submitted = false;
    }

    if (env_test() && !s_ui.test_drawn) {
        draw_test_pattern(s_ui.mapped);
        s_ui.test_drawn = true;
        s_ui.upload_owed = true;
    }
    qemu_mutex_lock(&s_lock);
    if (s_pub.pixels != NULL && s_pub.seq != s_ui.uploaded_seq) {
        memcpy(s_ui.mapped, s_pub.pixels, UI_BYTES);
        s_ui.uploaded_seq = s_pub.seq;
        s_ui.upload_owed = true;
    }
    qemu_mutex_unlock(&s_lock);

    if (s_ui.upload_owed || s_ui.chain.wait_pending) {
        if (upload(c)) {
            s_ui.upload_owed = false;
            s_ui.uploads++;
        }
    }
}

uint32_t vr_ui_panel_uploads(void)
{
    return s_ui.uploads;
}

bool vr_ui_panel_pose(VrUiPanelPose *out)
{
    if (!s_ui.placed || !shown()) {
        return false;
    }
    *out = s_ui.pose;
    return true;
}

bool vr_ui_panel_layer(VRCompositor *c, XrCompositionLayerQuad *quad)
{
    (void)c;
#ifdef XEMU_VR_UIPANEL_PLANTED_NOLAYER
    (void)quad;
    return false;
#else
    if (!shown() || !s_ui.placed || s_ui.chain.swapchain == XR_NULL_HANDLE ||
        !s_ui.chain.ever_released) {
        return false;
    }
    float q[4];
    vr_ui_panel_orientation(&s_ui.pose, q);
    *quad = (XrCompositionLayerQuad){
        .type = XR_TYPE_COMPOSITION_LAYER_QUAD,
        .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                      XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT,
        .space = g_xemu_vr.xr.space,
        .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
        .subImage = {
            .swapchain = s_ui.chain.swapchain,
            .imageRect = { { 0, 0 }, { VR_UI_PANEL_W, VR_UI_PANEL_H } },
            .imageArrayIndex = 0,
        },
        .pose = {
            .orientation = { q[0], q[1], q[2], q[3] },
            .position = { s_ui.pose.pos[0], s_ui.pose.pos[1],
                          s_ui.pose.pos[2] },
        },
        .size = { VR_UI_PANEL_WIDTH_M, VR_UI_PANEL_HEIGHT_M },
    };
    return true;
#endif
}
