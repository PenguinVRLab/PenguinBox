/*
 * Geforce NV2A PGRAPH Vulkan Renderer
 *
 * Copyright (c) 2024-2025 Matt Borgerson
 *
 * Based on GL implementation:
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2018-2024 Matt Borgerson
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

#include "hw/xbox/nv2a/nv2a_int.h"
#include "hw/xbox/nv2a/pgraph/swizzle.h"
#include "qemu/compiler.h"
#include "ui/xemu-settings.h"
#include "hw/xbox/nv2a/pgraph/phase_timers.h"
#include "renderer.h"

const int num_invalid_surfaces_to_keep = 10;
const int max_surface_frame_time_delta = 5;

void pgraph_vk_set_surface_scale_factor(NV2AState *d, unsigned int scale)
{
    g_config.display.quality.surface_scale = scale < 1 ? 1 : scale;

    qemu_mutex_lock(&d->pfifo.lock);
    qatomic_set(&d->pfifo.halt, true);
    qemu_mutex_unlock(&d->pfifo.lock);

    qemu_mutex_lock(&d->pgraph.lock);
    qemu_event_reset(&d->pgraph.vk_renderer_state->dirty_surfaces_download_complete);
    qatomic_set(&d->pgraph.vk_renderer_state->download_dirty_surfaces_pending, true);
    qemu_mutex_unlock(&d->pgraph.lock);
    qemu_mutex_lock(&d->pfifo.lock);
    pfifo_kick(d);
    qemu_mutex_unlock(&d->pfifo.lock);
    qemu_event_wait(&d->pgraph.vk_renderer_state->dirty_surfaces_download_complete);

    qemu_mutex_lock(&d->pgraph.lock);
    qemu_event_reset(&d->pgraph.flush_complete);
    qatomic_set(&d->pgraph.flush_pending, true);
    qemu_mutex_unlock(&d->pgraph.lock);
    qemu_mutex_lock(&d->pfifo.lock);
    pfifo_kick(d);
    qemu_mutex_unlock(&d->pfifo.lock);
    qemu_event_wait(&d->pgraph.flush_complete);

    qemu_mutex_lock(&d->pfifo.lock);
    qatomic_set(&d->pfifo.halt, false);
    pfifo_kick(d);
    qemu_mutex_unlock(&d->pfifo.lock);
}

unsigned int pgraph_vk_get_surface_scale_factor(NV2AState *d)
{
    return d->pgraph.surface_scale_factor;
}

void pgraph_vk_reload_surface_scale_factor(PGRAPHState *pg)
{
    int factor = g_config.display.quality.surface_scale;
    pg->surface_scale_factor = MAX(factor, 1);
}

static void get_surface_dimensions(PGRAPHState const *pg, unsigned int *width,
                                   unsigned int *height)
{
    bool swizzle = (pg->surface_type == NV097_SET_SURFACE_FORMAT_TYPE_SWIZZLE);
    if (swizzle) {
        *width = 1 << pg->surface_shape.log_width;
        *height = 1 << pg->surface_shape.log_height;
    } else {
        *width = pg->surface_shape.clip_width;
        *height = pg->surface_shape.clip_height;
    }
}

static bool framebuffer_dirty(PGRAPHState const *pg)
{
    bool shape_changed = memcmp(&pg->surface_shape, &pg->last_surface_shape,
                                sizeof(SurfaceShape)) != 0;
    if (!shape_changed || (!pg->surface_shape.color_format
            && !pg->surface_shape.zeta_format)) {
        return false;
    }
    return true;
}

static void memcpy_image(void *dst, void const *src, int dst_stride,
                         int src_stride, int height)
{
    if (dst_stride == src_stride) {
        memcpy(dst, src, dst_stride * height);
        return;
    }

    uint8_t *dst_ptr = (uint8_t *)dst;
    uint8_t const *src_ptr = (uint8_t *)src;

    size_t copy_stride = MIN(src_stride, dst_stride);

    for (int i = 0; i < height; i++) {
        memcpy(dst_ptr, src_ptr, copy_stride);
        dst_ptr += dst_stride;
        src_ptr += src_stride;
    }
}

static bool check_surface_overlaps_range(const SurfaceBinding *surface,
                                         hwaddr range_start, hwaddr range_len)
{
    hwaddr surface_end = surface->vram_addr + surface->size;
    hwaddr range_end = range_start + range_len;
    return !(surface->vram_addr >= range_end || range_start >= surface_end);
}

void pgraph_vk_surface_transition(PGRAPHState *pg, VkCommandBuffer cmd,
                                  SurfaceBinding *surface, VkImageLayout to)
{
    if (surface->image_layout == to) {
        return;
    }
    pgraph_vk_transition_image_layout(pg, cmd, surface->image,
                                      surface->host_fmt.vk_format,
                                      surface->image_layout, to);
    surface->image_layout = to;
}

VkImageLayout pgraph_vk_surface_rest_layout(const SurfaceBinding *surface)
{
    return surface->color ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL :
                            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
}

int pgraph_vk_download_surfaces_in_range_if_dirty(PGRAPHState *pg,
                                                  hwaddr start, hwaddr size)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    SurfaceBinding *surface;
    int downloaded = 0;

    QTAILQ_FOREACH(surface, &r->surfaces, entry) {
        if (check_surface_overlaps_range(surface, start, size)) {
            if (surface->draw_dirty) {
                downloaded++;
            }
            pgraph_vk_surface_download_if_dirty(
                container_of(pg, NV2AState, pgraph), surface);
        }
    }
    return downloaded;
}

static void download_surface_to_buffer(NV2AState *d, SurfaceBinding *surface,
                                       uint8_t *pixels)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    if (!surface->width || !surface->height) {
        return;
    }

    NV2A_PHASE_LEAF(PF_LEAF_DOWNLOAD);

    nv2a_profile_inc_counter(NV2A_PROF_SURF_DOWNLOAD);

    bool use_compute_to_convert_depth_stencil_format =
        surface->host_fmt.vk_format == VK_FORMAT_D24_UNORM_S8_UINT ||
        surface->host_fmt.vk_format == VK_FORMAT_D32_SFLOAT_S8_UINT;

    bool no_conversion_necessary =
        surface->color || use_compute_to_convert_depth_stencil_format ||
        surface->host_fmt.vk_format == VK_FORMAT_D16_UNORM;

    assert(no_conversion_necessary);

    bool compute_needs_finish = (use_compute_to_convert_depth_stencil_format &&
                                 pgraph_vk_compute_needs_finish(r));

    if (r->in_command_buffer &&
        surface->draw_time >= r->command_buffer_start_time) {
        if (pgraph_vk_narrowfence_enabled() && !compute_needs_finish) {
            pgraph_vk_submit_batch(pg, VK_FINISH_REASON_SURFACE_DOWN);
        } else {
            pgraph_vk_finish(pg, VK_FINISH_REASON_SURFACE_DOWN);
        }
    } else if (compute_needs_finish) {
        pgraph_vk_finish(pg, VK_FINISH_REASON_NEED_BUFFER_SPACE);
    }
    pgraph_vk_wait_for_surface_write(pg, surface);

    bool downscale = (pg->surface_scale_factor != 1);

    trace_nv2a_pgraph_surface_download(
        surface->color ? "COLOR" : "ZETA",
        surface->swizzle ? "sz" : "lin", surface->vram_addr,
        surface->width, surface->height, surface->pitch,
        surface->fmt.bytes_per_pixel);

    uint8_t *gl_read_buf = pixels;

    uint8_t *swizzle_buf = pixels;
    if (surface->swizzle) {
        assert(pg->surface_scale_factor == 1 || downscale);
        swizzle_buf = (uint8_t *)g_malloc(surface->size);
        gl_read_buf = swizzle_buf;
    }

    unsigned int scaled_width = surface->width,
                 scaled_height = surface->height;
    pgraph_apply_scaling_factor(pg, &scaled_width, &scaled_height);

    VkCommandBuffer cmd = pgraph_vk_begin_single_time_commands(pg);
    pgraph_vk_begin_debug_marker(r, cmd, RGBA_RED, __func__);

    pgraph_vk_surface_transition(pg, cmd, surface,
                                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    int num_copy_regions = 1;
    VkBufferImageCopy copy_regions[2];
    copy_regions[0] = (VkBufferImageCopy){
        .imageSubresource.aspectMask = surface->color ?
                                           VK_IMAGE_ASPECT_COLOR_BIT :
                                           VK_IMAGE_ASPECT_DEPTH_BIT,
        .imageSubresource.layerCount = 1,
    };

    VkImage surface_image_loc;
    if (downscale && !use_compute_to_convert_depth_stencil_format) {
        copy_regions[0].imageExtent =
            (VkExtent3D){ surface->width, surface->height, 1 };

        if (surface->image_scratch_current_layout !=
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            pgraph_vk_transition_image_layout(
                pg, cmd, surface->image_scratch, surface->host_fmt.vk_format,
                surface->image_scratch_current_layout,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            surface->image_scratch_current_layout =
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        }

        VkImageBlit blit_region = {
            .srcSubresource.aspectMask = surface->host_fmt.aspect,
            .srcSubresource.mipLevel = 0,
            .srcSubresource.baseArrayLayer = 0,
            .srcSubresource.layerCount = 1,
            .srcOffsets[0] = (VkOffset3D){0, 0, 0},
            .srcOffsets[1] = (VkOffset3D){scaled_width, scaled_height, 1},

            .dstSubresource.aspectMask = surface->host_fmt.aspect,
            .dstSubresource.mipLevel = 0,
            .dstSubresource.baseArrayLayer = 0,
            .dstSubresource.layerCount = 1,
            .dstOffsets[0] = (VkOffset3D){0, 0, 0},
            .dstOffsets[1] = (VkOffset3D){surface->width, surface->height, 1},
        };

        vkCmdBlitImage(cmd, surface->image,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       surface->image_scratch,
                       surface->image_scratch_current_layout, 1, &blit_region,
                       VK_FILTER_NEAREST);

        pgraph_vk_transition_image_layout(pg, cmd, surface->image_scratch,
                                          surface->host_fmt.vk_format,
                                          surface->image_scratch_current_layout,
                                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        surface->image_scratch_current_layout =
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        surface_image_loc = surface->image_scratch;
    } else {
        copy_regions[0].imageExtent =
            (VkExtent3D){ scaled_width, scaled_height, 1 };
        surface_image_loc = surface->image;
    }

    if (surface->host_fmt.aspect & VK_IMAGE_ASPECT_STENCIL_BIT) {
        size_t depth_size = scaled_width * scaled_height * 4;
        copy_regions[num_copy_regions++] = (VkBufferImageCopy){
            .bufferOffset = ROUND_UP(
                depth_size,
                r->device_props.limits.minStorageBufferOffsetAlignment),
            .imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
            .imageSubresource.layerCount = 1,
            .imageExtent = (VkExtent3D){ scaled_width, scaled_height, 1 },
        };
    }

    size_t downloaded_image_size = surface->host_fmt.host_bytes_per_pixel *
                                   surface->width * surface->height;
    assert((downloaded_image_size) <=
           r->storage_buffers[BUFFER_STAGING_DST].buffer_size);

    int copy_buffer_idx = use_compute_to_convert_depth_stencil_format ?
                             BUFFER_COMPUTE_DST :
                             BUFFER_STAGING_DST;
    VkBuffer copy_buffer = r->storage_buffers[copy_buffer_idx].buffer;

    {
        VkBufferMemoryBarrier pre_copy_dst_barrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = copy_buffer,
            .size = VK_WHOLE_SIZE
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1,
                             &pre_copy_dst_barrier, 0, NULL);
    }
    vkCmdCopyImageToBuffer(cmd, surface_image_loc,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, copy_buffer,
                           num_copy_regions, copy_regions);

    pgraph_vk_surface_transition(pg, cmd, surface,
                                 pgraph_vk_surface_rest_layout(surface));

    if (use_compute_to_convert_depth_stencil_format) {
        size_t bytes_per_pixel = 4;
        size_t packed_size =
            downscale ? (surface->width * surface->height * bytes_per_pixel) :
                        (scaled_width * scaled_height * bytes_per_pixel);

        VkBufferMemoryBarrier pre_compute_src_barrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = copy_buffer,
            .size = VK_WHOLE_SIZE
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL,
                             1, &pre_compute_src_barrier, 0, NULL);

        VkBuffer pack_buffer = r->storage_buffers[BUFFER_COMPUTE_SRC].buffer;

        VkBufferMemoryBarrier pre_compute_dst_barrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = pack_buffer,
            .size = VK_WHOLE_SIZE
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL,
                             1, &pre_compute_dst_barrier, 0, NULL);

        pgraph_vk_pack_depth_stencil(pg, surface, cmd, copy_buffer, pack_buffer,
                                     downscale);

        VkBufferMemoryBarrier post_compute_src_barrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = copy_buffer,
            .size = VK_WHOLE_SIZE
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1,
                             &post_compute_src_barrier, 0, NULL);

        VkBufferMemoryBarrier post_compute_dst_barrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = pack_buffer,
            .size = packed_size
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1,
                             &post_compute_dst_barrier, 0, NULL);

        copy_buffer = r->storage_buffers[BUFFER_STAGING_DST].buffer;

        VkBufferMemoryBarrier pre_copy_dst_barrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = copy_buffer,
            .size = VK_WHOLE_SIZE
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1,
                             &pre_copy_dst_barrier, 0, NULL);

        VkBufferCopy buffer_copy_region = {
            .size = packed_size,
        };
        vkCmdCopyBuffer(cmd, pack_buffer, copy_buffer, 1, &buffer_copy_region);

        VkBufferMemoryBarrier post_copy_src_barrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = pack_buffer,
            .size = VK_WHOLE_SIZE
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1,
                             &post_copy_src_barrier, 0, NULL);
    }

    VkBufferMemoryBarrier post_copy_dst_barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = copy_buffer,
        .size = VK_WHOLE_SIZE
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1,
                         &post_copy_dst_barrier, 0, NULL);

    nv2a_profile_inc_counter(NV2A_PROF_QUEUE_SUBMIT_1);
    pgraph_vk_end_debug_marker(r, cmd);
    pgraph_vk_end_single_time_commands(pg, cmd);

    void *mapped_memory_ptr = NULL;
    VK_CHECK(vmaMapMemory(r->allocator,
                          r->storage_buffers[BUFFER_STAGING_DST].allocation,
                          &mapped_memory_ptr));

    vmaInvalidateAllocation(r->allocator,
                            r->storage_buffers[BUFFER_STAGING_DST].allocation,
                            0, VK_WHOLE_SIZE);

    memcpy_image(gl_read_buf, mapped_memory_ptr, surface->pitch,
                 surface->width * surface->fmt.bytes_per_pixel,
                 surface->height);

    vmaUnmapMemory(r->allocator,
                   r->storage_buffers[BUFFER_STAGING_DST].allocation);

    if (surface->swizzle) {
        swizzle_rect(swizzle_buf, surface->width, surface->height, pixels,
                     surface->pitch, surface->fmt.bytes_per_pixel);
        nv2a_profile_inc_counter(NV2A_PROF_SURF_SWIZZLE);
        g_free(swizzle_buf);
    }
}

void pgraph_vk_wait_for_surface_write(PGRAPHState *pg, SurfaceBinding *surface)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    if (r->in_command_buffer &&
        surface->draw_time >= r->command_buffer_start_time) {
        pgraph_vk_finish(pg, VK_FINISH_REASON_SURFACE_DOWN);
    }

    uint32_t idx = surface->last_write_submit;
    if (idx == SURFACE_NO_WRITE) {
        return;
    }
    struct CbFenceSlot *slot = &r->cb_fence_ring[idx % CB_FENCE_RING_SIZE];
    if (slot->submit_index != idx) {
        return;
    }
    if (vkGetFenceStatus(r->device, slot->fence) == VK_SUCCESS) {
        return;
    }
    VK_CHECK(vkWaitForFences(r->device, 1, &slot->fence, VK_TRUE, UINT64_MAX));
}

static void download_surface(NV2AState *d, SurfaceBinding *surface, bool force)
{
    if (!(surface->download_pending || force) || !surface->width ||
        !surface->height) {
        return;
    }

    download_surface_to_buffer(d, surface, d->vram_ptr + surface->vram_addr);
    surface->dl_frame++;

    {
        static int dump_mode = -1;
        static hwaddr dump_addr;
        if (dump_mode < 0) {
            const char *e = getenv("XEMU_SURF_DUMP");
            if (!e) { dump_mode = 0; }
            else if (!strcmp(e, "auto")) { dump_mode = 1; }
            else { dump_mode = 2; dump_addr = (hwaddr)strtoull(e, NULL, 16); }
        }
        bool hit = surface->color &&
            ((dump_mode == 2 && surface->vram_addr == dump_addr) ||
             (dump_mode == 1 && surface->width <= 512 &&
              surface->height <= 512));
        if (hit) {
            char path[128];
            snprintf(path, sizeof(path),
                     "/tmp/surf-dump-%08" HWADDR_PRIx "-%ux%u.ppm",
                     surface->vram_addr, surface->width, surface->height);
            FILE *f = fopen(path, "wb");
            if (f) {
                unsigned w = surface->width, h = surface->height, bpp =
                    surface->fmt.bytes_per_pixel;
                const uint8_t *base = d->vram_ptr + surface->vram_addr;
                fprintf(f, "P6\n%u %u\n255\n", w, h);
                for (unsigned y = 0; y < h; y++) {
                    for (unsigned x = 0; x < w; x++) {
                        const uint8_t *px = base + y * surface->pitch + x * bpp;
                        uint8_t rgb[3] = { bpp >= 3 ? px[2] : px[0],
                                           bpp >= 2 ? px[1] : px[0],
                                           px[0] };
                        fwrite(rgb, 1, 3, f);
                    }
                }
                fclose(f);
            }
        }
    }

    memory_region_set_client_dirty(d->vram, surface->vram_addr,
                                   surface->pitch * surface->height,
                                   DIRTY_MEMORY_VGA);

    if (!pgraph_vk_note_exact_dirty(d, surface->vram_addr,
                                    surface->pitch * surface->height)) {
        memory_region_set_client_dirty(d->vram, surface->vram_addr,
                                       surface->pitch * surface->height,
                                       DIRTY_MEMORY_NV2A_TEX);
    }

    surface->download_pending = false;
    surface->draw_dirty = false;
    mem_access_callback_set_read_quiesced(surface->access_cb, true);
}

void pgraph_vk_wait_for_surface_download(SurfaceBinding *surface)
{
    NV2AState *d = g_nv2a;

    if (qatomic_read(&surface->draw_dirty)) {
        qemu_mutex_lock(&d->pfifo.lock);
        qemu_event_reset(&d->pgraph.vk_renderer_state->downloads_complete);
        qatomic_set(&surface->download_pending, true);
        qatomic_set(&d->pgraph.vk_renderer_state->downloads_pending, true);
        pfifo_kick(d);
        qemu_mutex_unlock(&d->pfifo.lock);
        bool pt = nv2a_phase_timers_on();
        int64_t pt_t0 = pt ? nv2a_phase_now_ns() : 0;
        qemu_event_wait(&d->pgraph.vk_renderer_state->downloads_complete);
        if (pt) {
            nv2a_phase_add_dl_wait(pt_t0);
        }
    }
}

void pgraph_vk_process_pending_downloads(NV2AState *d)
{
    PGRAPHVkState *r = d->pgraph.vk_renderer_state;
    SurfaceBinding *surface;

    bool pt = nv2a_phase_timers_on();
    int64_t pt_t0 = pt ? nv2a_phase_now_ns() : 0;

    QTAILQ_FOREACH(surface, &r->surfaces, entry) {
        download_surface(d, surface, false);
    }

    if (pt) {
        nv2a_phase_add_dl_proc(pt_t0);
    }

    qatomic_set(&r->downloads_pending, false);
    qemu_event_set(&r->downloads_complete);
}

void pgraph_vk_download_dirty_surfaces(NV2AState *d)
{
    PGRAPHVkState *r = d->pgraph.vk_renderer_state;

    if (r->async_reports_enabled &&
        (r->report_marker_pending || !QSIMPLEQ_EMPTY(&r->report_queue))) {
        pgraph_vk_finish(&d->pgraph, VK_FINISH_REASON_FLUSH);
    }

    SurfaceBinding *surface;
    QTAILQ_FOREACH(surface, &r->surfaces, entry) {
        pgraph_vk_surface_download_if_dirty(d, surface);
    }

    qatomic_set(&r->download_dirty_surfaces_pending, false);
    qemu_event_set(&r->dirty_surfaces_download_complete);
}

static void wait_for_surface_downloads(NV2AState *d)
{
    bool is_main_thread = qemu_in_main_thread();
    if (is_main_thread) {
        bql_unlock();
    }

    PGRAPHVkState *r = d->pgraph.vk_renderer_state;
    qemu_mutex_lock(&d->pfifo.lock);
    qemu_event_reset(&r->downloads_complete);
    qatomic_set(&r->downloads_pending, true);
    pfifo_kick(d);
    qemu_mutex_unlock(&d->pfifo.lock);
    bool pt = nv2a_phase_timers_on();
    int64_t pt_t0 = pt ? nv2a_phase_now_ns() : 0;
    qemu_event_wait(&r->downloads_complete);
    if (pt) {
        nv2a_phase_add_dl_wait(pt_t0);
    }

    if (is_main_thread) {
        bql_lock();
    }
}

static void surface_access_callback(void *opaque, MemoryRegion *mr, hwaddr addr,
                                    hwaddr len, bool write)
{
    NV2AState *d = (NV2AState *)opaque;

    if (write) {
        nv2a_profile_inc_counter(len > 8 ? NV2A_PROF_SURF_DMA_WRITE
                                         : NV2A_PROF_SURF_CPU_WRITE);
    } else {
        nv2a_profile_inc_counter(NV2A_PROF_SURF_CPU_READ);
    }

    static int access_log_budget = -1;
    if (access_log_budget < 0) {
        access_log_budget = getenv("XEMU_SURF_ACCESS_LOG") ? 200 : 0;
    }

    qemu_mutex_lock(&d->pgraph.lock);

    PGRAPHVkState *r = d->pgraph.vk_renderer_state;
    bool wait_for_downloads = false;

    SurfaceBinding *surface;
    QTAILQ_FOREACH(surface, &r->surfaces, entry) {
        if (!check_surface_overlaps_range(surface, addr, len)) {
            continue;
        }

        hwaddr offset = addr - surface->vram_addr;

        if (access_log_budget > 0) {
            access_log_budget--;
            fprintf(stderr,
                    "surf-access: %s addr=%08" HWADDR_PRIx
                    " off=%06" HWADDR_PRIx " len=%" HWADDR_PRIu
                    " surf=%08" HWADDR_PRIx " %ux%u %s draw_dirty=%d"
                    " upload_pending=%d\n",
                    write ? "W" : "R", addr, offset, len,
                    surface->vram_addr, surface->width, surface->height,
                    surface->color ? "color" : "zeta",
                    surface->draw_dirty, surface->upload_pending);
            fprintf(stderr, "surf-access:   bound_color=%d bound_zeta=%d\n",
                    surface == r->color_binding, surface == r->zeta_binding);
        }

        if (write) {
            trace_nv2a_pgraph_surface_cpu_write(surface->vram_addr, offset);
        } else {
            trace_nv2a_pgraph_surface_cpu_read(surface->vram_addr, offset);
        }

        if (!write) {
            surface->cpu_read_frame = g_nv2a_stats.frame_count + 1;
            surface->reads_frame++;
        }

        if (surface->draw_dirty) {
            surface->download_pending = true;
            wait_for_downloads = true;
        }

        if (write) {
            surface->upload_pending = true;
        }
    }

    qemu_mutex_unlock(&d->pgraph.lock);

    if (wait_for_downloads) {
        nv2a_profile_inc_counter(NV2A_PROF_SURF_CPU_DL_WAIT);
        wait_for_surface_downloads(d);
    }
}

static void register_cpu_access_callback(NV2AState *d, SurfaceBinding *surface)
{
    if (tcg_enabled()) {
        if (surface->width && surface->height) {
            surface->access_cb = mem_access_callback_insert(
                qemu_get_cpu(0), d->vram, surface->vram_addr, surface->size,
                &surface_access_callback, d);
            mem_access_callback_set_read_quiesced(surface->access_cb, true);
        } else {
            surface->access_cb = NULL;
        }
    }
}

static void unregister_cpu_access_callback(NV2AState *d,
                                           SurfaceBinding const *surface)
{
    if (tcg_enabled()) {
        mem_access_callback_remove_by_ref(qemu_get_cpu(0), surface->access_cb);
    }
}

static void bind_surface(PGRAPHVkState *r, SurfaceBinding *surface)
{
    if (surface->color) {
        r->color_binding = surface;
    } else {
        r->zeta_binding = surface;
    }

    r->framebuffer_dirty = true;
}

static bool g_predl_enabled = true;

void pgraph_vk_refresh_predl_toggle(void)
{
    static const char *watch;
    static int forced_off = -1;
    if (forced_off < 0) {
        forced_off = getenv("XEMU_NO_PREDICTIVE_READBACK") ? 1 : 0;
        watch = getenv("XEMU_PREDL_TOGGLE_FILE");
        if (!watch) {
            watch = "/tmp/predl-off";
        }
    }
    bool now_on = !forced_off && (access(watch, F_OK) != 0);
    static int last = -1;
    if ((int)now_on != last) {
        fprintf(stderr, "predl: predictive readback %s\n",
                now_on ? "ON" : "OFF");
        last = now_on;
    }
    g_predl_enabled = now_on;
}

static bool g_narrowfence_enabled = false;

void pgraph_vk_refresh_narrowfence_toggle(void)
{
    static const char *watch;
    static int forced = -1;
    if (forced < 0) {
        if (getenv("XEMU_NO_NARROW_FENCE")) {
            forced = 0;
        } else if (getenv("XEMU_NARROW_FENCE")) {
            forced = 1;
        } else {
            forced = 2;
            watch = getenv("XEMU_NARROWFENCE_TOGGLE_FILE");
            if (!watch) {
                watch = "/tmp/narrowfence-on";
            }
        }
    }
    bool now_on = (forced == 1) ||
                  (forced == 2 && access(watch, F_OK) == 0);
    static int last = -1;
    if ((int)now_on != last) {
        fprintf(stderr, "narrowfence: async early-submit %s\n",
                now_on ? "ON" : "OFF");
        last = now_on;
    }
    g_narrowfence_enabled = now_on;
}

bool pgraph_vk_narrowfence_enabled(void)
{
    return g_narrowfence_enabled;
}

static void predictive_readback_on_unbind(NV2AState *d, SurfaceBinding *s)
{
    unsigned int now = g_nv2a_stats.frame_count + 1;

    if (g_narrowfence_enabled) {
        PGRAPHState *pg = &d->pgraph;
        PGRAPHVkState *r = pg->vk_renderer_state;
        if (r->in_command_buffer &&
            s->draw_time >= r->command_buffer_start_time &&
            s->draw_dirty && s->cpu_read_frame != 0 &&
            (now - s->cpu_read_frame) < 60 && s->predl_frame != now) {
            s->predl_frame = now;
            nv2a_profile_inc_counter(NV2A_PROF_SURF_PREDL);
            pgraph_vk_submit_batch(pg, VK_FINISH_REASON_SURFACE_DOWN);
        }
        return;
    }

    if (g_predl_enabled && s->draw_dirty && s->cpu_read_frame != 0 &&
        (now - s->cpu_read_frame) < 60 && s->predl_frame != now) {
        s->predl_frame = now;
        nv2a_profile_inc_counter(NV2A_PROF_SURF_PREDL);
        pgraph_vk_surface_download_if_dirty(d, s);
    }
}

static void unbind_surface(NV2AState *d, bool color)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    if (color) {
        if (r->color_binding) {
            predictive_readback_on_unbind(d, r->color_binding);
            r->color_binding = NULL;
            r->framebuffer_dirty = true;
        }
    } else {
        if (r->zeta_binding) {
            predictive_readback_on_unbind(d, r->zeta_binding);
            r->zeta_binding = NULL;
            r->framebuffer_dirty = true;
        }
    }
}

static void invalidate_surface(NV2AState *d, SurfaceBinding *surface)
{
    PGRAPHVkState *r = d->pgraph.vk_renderer_state;

    trace_nv2a_pgraph_surface_invalidated(surface->vram_addr);

    if (pgraph_vk_narrowfence_enabled()) {
        if (r->in_command_buffer &&
            (surface->draw_time >= r->command_buffer_start_time ||
             surface->last_use_submit == r->submit_count)) {
            pgraph_vk_submit_batch(&d->pgraph, VK_FINISH_REASON_SURFACE_DOWN);
        }
        pgraph_vk_wait_for_submission(r, surface->last_write_submit);
        pgraph_vk_wait_for_submission(r, surface->last_use_submit);
    } else {
        pgraph_vk_finish(&d->pgraph, VK_FINISH_REASON_SURFACE_DOWN);
    }

    pgraph_vk_wait_all_present_slots(&d->pgraph);

    assert((!r->in_command_buffer ||
            surface->draw_time < r->command_buffer_start_time) &&
           "Surface evicted while in use!");

    if (surface == r->color_binding) {
        assert(d->pgraph.surface_color.buffer_dirty);
        unbind_surface(d, true);
    }
    if (surface == r->zeta_binding) {
        assert(d->pgraph.surface_zeta.buffer_dirty);
        unbind_surface(d, false);
    }

    unregister_cpu_access_callback(d, surface);

    r->surface_generation++;

    QTAILQ_REMOVE(&r->surfaces, surface, entry);
    QTAILQ_INSERT_HEAD(&r->invalid_surfaces, surface, entry);
}

static bool check_surfaces_overlap(const SurfaceBinding *surface,
                                   const SurfaceBinding *other_surface)
{
    return check_surface_overlaps_range(surface, other_surface->vram_addr,
                                        other_surface->size);
}

static bool alias_surfaces_enabled(void)
{
    static int state = -1;
    if (state < 0) {
        if (getenv("XEMU_NO_ALIAS_SURFACES")) {
            state = 0;
        } else {
            state = getenv("XEMU_ALIAS_SURFACES") ? 1 : 0;
            if (state == 1) {
                fprintf(stderr,
                        "nv2a: XEMU_ALIAS_SURFACES on — small surface creates "
                        "keep large overlapped parents resident (no "
                        "evict+download cascade)\n");
            }
        }
    }
    return state == 1;
}

static void invalidate_overlapping_surfaces(NV2AState *d,
                                            SurfaceBinding const *surface)
{
    PGRAPHVkState *r = d->pgraph.vk_renderer_state;

    SurfaceBinding *other_surface, *next_surface;
    QTAILQ_FOREACH_SAFE (other_surface, &r->surfaces, entry, next_surface) {
        if (check_surfaces_overlap(surface, other_surface)) {
            if (alias_surfaces_enabled() &&
                (uint64_t)surface->size * 4 <= other_surface->size &&
                !other_surface->upload_pending) {
                nv2a_profile_inc_counter(NV2A_PROF_ALIAS_KEPT);
                continue;
            }
            trace_nv2a_pgraph_surface_evict_overlapping(
                other_surface->vram_addr, other_surface->width,
                other_surface->height, other_surface->pitch);
            pgraph_vk_surface_download_if_dirty(d, other_surface);
            invalidate_surface(d, other_surface);
        }
    }
}

static void surface_put(NV2AState *d, SurfaceBinding *surface)
{
    PGRAPHVkState *r = d->pgraph.vk_renderer_state;

    assert(alias_surfaces_enabled() ||
           pgraph_vk_surface_get(d, surface->vram_addr) == NULL);

    invalidate_overlapping_surfaces(d, surface);
    register_cpu_access_callback(d, surface);

    QTAILQ_INSERT_HEAD(&r->surfaces, surface, entry);
}

bool pgraph_vk_alias_surfaces_enabled(void)
{
    return alias_surfaces_enabled();
}

SurfaceBinding *pgraph_vk_surface_get(NV2AState *d, hwaddr addr)
{
    PGRAPHVkState *r = d->pgraph.vk_renderer_state;

    SurfaceBinding *surface;
    QTAILQ_FOREACH (surface, &r->surfaces, entry) {
        if (surface->vram_addr == addr) {
            return surface;
        }
    }

    return NULL;
}

SurfaceBinding *pgraph_vk_surface_get_within(NV2AState *d, hwaddr addr)
{
    PGRAPHVkState *r = d->pgraph.vk_renderer_state;

    SurfaceBinding *surface;
    QTAILQ_FOREACH (surface, &r->surfaces, entry) {
        if (addr >= surface->vram_addr &&
            addr < (surface->vram_addr + surface->size)) {
            return surface;
        }
    }

    return NULL;
}

static void set_surface_label(PGRAPHState *pg, SurfaceBinding const *surface)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    g_autofree gchar *label = g_strdup_printf(
        "Surface %" HWADDR_PRIx "h fmt:%s,%02xh %dx%d aa:%d",
        surface->vram_addr, surface->color ? "Color" : "Zeta",
        surface->color ? surface->shape.color_format :
                         surface->shape.zeta_format,
        surface->width, surface->height, pg->surface_shape.anti_aliasing);

    VkDebugUtilsObjectNameInfoEXT name_info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = VK_OBJECT_TYPE_IMAGE,
        .objectHandle = (uint64_t)surface->image,
        .pObjectName = label,
    };

    if (r->debug_utils_extension_enabled) {
        vkSetDebugUtilsObjectNameEXT(r->device, &name_info);
    }
    vmaSetAllocationName(r->allocator, surface->allocation, label);

    if (surface->image_scratch) {
        g_autofree gchar *label_scratch =
            g_strdup_printf("%s (scratch)", label);
        name_info.objectHandle = (uint64_t)surface->image_scratch;
        name_info.pObjectName = label_scratch;
        if (r->debug_utils_extension_enabled) {
            vkSetDebugUtilsObjectNameEXT(r->device, &name_info);
        }
        vmaSetAllocationName(r->allocator, surface->allocation_scratch,
                             label_scratch);
    }
}

static void create_surface_image(PGRAPHState *pg, SurfaceBinding *surface)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    unsigned int width = surface->width ? surface->width : 1;
    unsigned int height = surface->height ? surface->height : 1;
    pgraph_apply_scaling_factor(pg, &width, &height);

    assert(!surface->image);
    assert(!surface->image_scratch);

    NV2A_VK_DPRINTF(
        "Creating new surface image width=%d height=%d @ %08" HWADDR_PRIx,
        width, height, surface->vram_addr);

    VkImageCreateInfo image_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .extent.width = width,
        .extent.height = height,
        .extent.depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .format = surface->host_fmt.vk_format,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT | surface->host_fmt.usage,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo alloc_create_info = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    VK_CHECK(vmaCreateImage(r->allocator, &image_create_info,
                            &alloc_create_info, &surface->image,
                            &surface->allocation, NULL));

    VK_CHECK(vmaCreateImage(r->allocator, &image_create_info,
                            &alloc_create_info, &surface->image_scratch,
                            &surface->allocation_scratch, NULL));
    surface->image_scratch_current_layout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImageViewCreateInfo image_view_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = surface->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = surface->host_fmt.vk_format,
        .subresourceRange.aspectMask = surface->host_fmt.aspect,
        .subresourceRange.levelCount = 1,
        .subresourceRange.layerCount = 1,
    };
    VK_CHECK(vkCreateImageView(r->device, &image_view_create_info, NULL,
                               &surface->image_view));

    VkCommandBuffer cmd = pgraph_vk_begin_single_time_commands(pg);
    pgraph_vk_begin_debug_marker(r, cmd, RGBA_RED, __func__);

    surface->image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    pgraph_vk_surface_transition(pg, cmd, surface,
                                 pgraph_vk_surface_rest_layout(surface));

    nv2a_profile_inc_counter(NV2A_PROF_QUEUE_SUBMIT_3);
    pgraph_vk_end_debug_marker(r, cmd);
    pgraph_vk_end_single_time_commands(pg, cmd);
    nv2a_profile_inc_counter(NV2A_PROF_SURF_CREATE);
}

static void migrate_surface_image(SurfaceBinding *dst, SurfaceBinding *src)
{
    dst->image = src->image;
    dst->image_view = src->image_view;
    dst->allocation = src->allocation;
    dst->image_layout = src->image_layout;
    dst->image_scratch = src->image_scratch;
    dst->image_scratch_current_layout = src->image_scratch_current_layout;
    dst->allocation_scratch = src->allocation_scratch;

    src->image = VK_NULL_HANDLE;
    src->image_view = VK_NULL_HANDLE;
    src->allocation = VK_NULL_HANDLE;
    src->image_scratch = VK_NULL_HANDLE;
    src->image_scratch_current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    src->allocation_scratch = VK_NULL_HANDLE;
}

static void destroy_surface_image(PGRAPHVkState *r, SurfaceBinding *surface)
{
    vkDestroyImageView(r->device, surface->image_view, NULL);
    surface->image_view = VK_NULL_HANDLE;

    vmaDestroyImage(r->allocator, surface->image, surface->allocation);
    surface->image = VK_NULL_HANDLE;
    surface->allocation = VK_NULL_HANDLE;

    vmaDestroyImage(r->allocator, surface->image_scratch,
                    surface->allocation_scratch);
    surface->image_scratch = VK_NULL_HANDLE;
    surface->allocation_scratch = VK_NULL_HANDLE;
}

static bool check_invalid_surface_is_compatibile(SurfaceBinding *surface,
                                                 SurfaceBinding *target)
{
    return surface->host_fmt.vk_format == target->host_fmt.vk_format &&
           surface->width == target->width &&
           surface->height == target->height &&
           surface->host_fmt.usage == target->host_fmt.usage;
}

static SurfaceBinding *
get_any_compatible_invalid_surface(PGRAPHVkState *r, SurfaceBinding *target)
{
    SurfaceBinding *surface, *next;
    QTAILQ_FOREACH_SAFE(surface, &r->invalid_surfaces, entry, next) {
        if (check_invalid_surface_is_compatibile(surface, target)) {
            QTAILQ_REMOVE(&r->invalid_surfaces, surface, entry);
            return surface;
        }
    }

    return NULL;
}

static void prune_invalid_surfaces(PGRAPHVkState *r, int keep)
{
    int num_surfaces = 0;

    SurfaceBinding *surface, *next;
    QTAILQ_FOREACH_SAFE(surface, &r->invalid_surfaces, entry, next) {
        num_surfaces += 1;
        if (num_surfaces > keep) {
            QTAILQ_REMOVE(&r->invalid_surfaces, surface, entry);
            destroy_surface_image(r, surface);
            g_free(surface);
        }
    }
}

static void expire_old_surfaces(NV2AState *d)
{
    PGRAPHVkState *r = d->pgraph.vk_renderer_state;

    SurfaceBinding *s, *next;
    QTAILQ_FOREACH_SAFE(s, &r->surfaces, entry, next) {
        int last_used = d->pgraph.frame_time - s->frame_time;
        if (last_used >= max_surface_frame_time_delta) {
            trace_nv2a_pgraph_surface_evict_reason("old", s->vram_addr);
            pgraph_vk_surface_download_if_dirty(d, s);
            invalidate_surface(d, s);
        }
    }
}

static bool check_surface_compatibility(SurfaceBinding const *s1,
                                        SurfaceBinding const *s2, bool strict)
{
    bool format_compatible =
        (s1->color == s2->color) &&
        (s1->host_fmt.vk_format == s2->host_fmt.vk_format) &&
        (s1->pitch == s2->pitch);
    if (!format_compatible) {
        return false;
    }

    if (!strict) {
        return (s1->width >= s2->width) && (s1->height >= s2->height);
    } else {
        return (s1->width == s2->width) && (s1->height == s2->height);
    }
}

void pgraph_vk_surface_download_if_dirty(NV2AState *d, SurfaceBinding *surface)
{
    if (surface->draw_dirty) {
        download_surface(d, surface, true);
    }
}

void pgraph_vk_upload_surface_data(NV2AState *d, SurfaceBinding *surface,
                                   bool force)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    if (!(surface->upload_pending || force)) {
        return;
    }

    NV2A_PHASE_LEAF(PF_LEAF_UPLOAD);

    pgraph_vk_wait_for_submission(r, surface->last_write_submit);
    pgraph_vk_wait_for_submission(r, surface->last_use_submit);

    nv2a_profile_inc_counter(NV2A_PROF_SURF_UPLOAD);

    bool inbatch = r->inbatch_uploads;
    if (inbatch) {
        bool upload_needs_compute =
            surface->host_fmt.vk_format == VK_FORMAT_D24_UNORM_S8_UINT ||
            surface->host_fmt.vk_format == VK_FORMAT_D32_SFLOAT_S8_UINT;
        if (upload_needs_compute && pgraph_vk_compute_needs_finish(r)) {
            pgraph_vk_finish(pg, VK_FINISH_REASON_NEED_BUFFER_SPACE);
        } else {
            pgraph_vk_submit_batch(pg, VK_FINISH_REASON_SURFACE_CREATE);
        }
    } else {
        pgraph_vk_finish(pg, VK_FINISH_REASON_SURFACE_CREATE);
    }

    trace_nv2a_pgraph_surface_upload(
                 surface->color ? "COLOR" : "ZETA",
                 surface->swizzle ? "sz" : "lin", surface->vram_addr,
                 surface->width, surface->height, surface->pitch,
                 surface->fmt.bytes_per_pixel);

    surface->upload_pending = false;
    surface->draw_time = pg->draw_time;

    if (!surface->width || !surface->height) {
        surface->initialized = true;
        return;
    }

    uint8_t *data = d->vram_ptr;
    uint8_t *buf = data + surface->vram_addr;

    g_autofree uint8_t *swizzle_buf = NULL;
    uint8_t *gl_read_buf = NULL;

    if (surface->swizzle) {
        swizzle_buf = (uint8_t*)g_malloc(surface->size);
        gl_read_buf = swizzle_buf;
        unswizzle_rect(data + surface->vram_addr,
                       surface->width, surface->height,
                       swizzle_buf,
                       surface->pitch,
                       surface->fmt.bytes_per_pixel);
        nv2a_profile_inc_counter(NV2A_PROF_SURF_SWIZZLE);
    } else {
        gl_read_buf = buf;
    }

    StorageBuffer *copy_buffer = &r->storage_buffers[BUFFER_STAGING_SRC];
    size_t uploaded_image_size = surface->height * surface->width *
                                 surface->fmt.bytes_per_pixel;
    assert(uploaded_image_size <= copy_buffer->buffer_size);

    VkDeviceSize staging_base = 0;
    if (inbatch) {
        pgraph_vk_begin_nondraw_commands(pg);
        if (!pgraph_vk_staging_ring_alloc(pg, uploaded_image_size,
                                          &staging_base, r->submit_count)) {
            nv2a_profile_inc_counter(NV2A_PROF_UPLOAD_STAGING_WAIT);
            pgraph_vk_finish(pg, VK_FINISH_REASON_NEED_BUFFER_SPACE);
            pgraph_vk_staging_ring_reset(r);
            inbatch = false;
            staging_base = 0;
        }
    }

    void *mapped_memory_ptr = NULL;
    VK_CHECK(vmaMapMemory(r->allocator, copy_buffer->allocation,
                          &mapped_memory_ptr));

    bool use_compute_to_convert_depth_stencil_format =
        surface->host_fmt.vk_format == VK_FORMAT_D24_UNORM_S8_UINT ||
        surface->host_fmt.vk_format == VK_FORMAT_D32_SFLOAT_S8_UINT;

    bool no_conversion_necessary =
        surface->color || surface->host_fmt.vk_format == VK_FORMAT_D16_UNORM ||
        use_compute_to_convert_depth_stencil_format;
    assert(no_conversion_necessary);

    memcpy_image((uint8_t *)mapped_memory_ptr + staging_base, gl_read_buf,
                 surface->width * surface->fmt.bytes_per_pixel, surface->pitch,
                 surface->height);

    vmaFlushAllocation(r->allocator, copy_buffer->allocation, staging_base,
                       inbatch ? uploaded_image_size : VK_WHOLE_SIZE);
    vmaUnmapMemory(r->allocator, copy_buffer->allocation);

    VkCommandBuffer cmd;
    if (inbatch) {
        nv2a_profile_inc_counter(NV2A_PROF_UPLOAD_INBATCH);
        cmd = pgraph_vk_begin_nondraw_commands(pg);
    } else {
        cmd = pgraph_vk_begin_single_time_commands(pg);
    }
    pgraph_vk_begin_debug_marker(r, cmd, RGBA_RED, __func__);

    VkBufferMemoryBarrier host_barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = copy_buffer->buffer,
        .size = VK_WHOLE_SIZE
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1,
                         &host_barrier, 0, NULL);

    VkBufferImageCopy regions[2];
    int num_regions = 0;

    regions[num_regions++] = (VkBufferImageCopy){
        .bufferOffset = staging_base,
        .imageSubresource.aspectMask = surface->color ?
                                           VK_IMAGE_ASPECT_COLOR_BIT :
                                           VK_IMAGE_ASPECT_DEPTH_BIT,
        .imageSubresource.layerCount = 1,
        .imageExtent = (VkExtent3D){ surface->width, surface->height, 1 },
    };

    if (surface->host_fmt.aspect & VK_IMAGE_ASPECT_STENCIL_BIT) {
        regions[num_regions++] = (VkBufferImageCopy){
            .imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
            .imageSubresource.layerCount = 1,
            .imageExtent = (VkExtent3D){ surface->width, surface->height, 1 },
        };
    }


    unsigned int scaled_width = surface->width, scaled_height = surface->height;
    pgraph_apply_scaling_factor(pg, &scaled_width, &scaled_height);

    if (use_compute_to_convert_depth_stencil_format) {

        size_t packed_size = uploaded_image_size;
        VkBufferCopy buffer_copy_region = {
            .srcOffset = staging_base,
            .size = packed_size,
        };
        vkCmdCopyBuffer(cmd, copy_buffer->buffer,
                        r->storage_buffers[BUFFER_COMPUTE_DST].buffer, 1,
                        &buffer_copy_region);

        size_t num_pixels = scaled_width * scaled_height;
        size_t unpacked_depth_image_size = num_pixels * 4;
        size_t unpacked_stencil_image_size = num_pixels;
        size_t unpacked_size =
            unpacked_depth_image_size + unpacked_stencil_image_size;

        VkBufferMemoryBarrier post_copy_src_barrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = copy_buffer->buffer,
            .size = VK_WHOLE_SIZE
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1,
                             &post_copy_src_barrier, 0, NULL);

        VkBufferMemoryBarrier pre_unpack_src_barrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = r->storage_buffers[BUFFER_COMPUTE_DST].buffer,
            .size = VK_WHOLE_SIZE
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL,
                             1, &pre_unpack_src_barrier, 0, NULL);

        StorageBuffer *unpack_buffer = &r->storage_buffers[BUFFER_COMPUTE_SRC];

        VkBufferMemoryBarrier pre_unpack_dst_barrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = unpack_buffer->buffer,
            .size = unpacked_size
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 1,
                             &pre_unpack_dst_barrier, 0, NULL);

        pgraph_vk_unpack_depth_stencil(
            pg, surface, cmd, r->storage_buffers[BUFFER_COMPUTE_DST].buffer,
            unpack_buffer->buffer);

        VkBufferMemoryBarrier post_unpack_src_barrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = r->storage_buffers[BUFFER_COMPUTE_DST].buffer,
            .size = VK_WHOLE_SIZE
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1,
                             &post_unpack_src_barrier, 0, NULL);

        VkBufferMemoryBarrier post_unpack_dst_barrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = unpack_buffer->buffer,
            .size = unpacked_size
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1,
                             &post_unpack_dst_barrier, 0, NULL);

        regions[0].imageExtent = (VkExtent3D){ scaled_width, scaled_height, 1 };
        regions[0].bufferOffset = 0;
        regions[1].imageExtent = regions[0].imageExtent;
        regions[1].bufferOffset =
            ROUND_UP(unpacked_depth_image_size,
                     r->device_props.limits.minStorageBufferOffsetAlignment);

        copy_buffer = unpack_buffer;
    }

    if (surface->image_scratch_current_layout !=
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        pgraph_vk_transition_image_layout(pg, cmd, surface->image_scratch,
                                          surface->host_fmt.vk_format,
                                          surface->image_scratch_current_layout,
                                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        surface->image_scratch_current_layout =
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }

    vkCmdCopyBufferToImage(cmd, copy_buffer->buffer, surface->image_scratch,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, num_regions,
                           regions);

    VkBufferMemoryBarrier post_copy_src_buffer_barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = copy_buffer->buffer,
        .size = VK_WHOLE_SIZE
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1,
                         &post_copy_src_buffer_barrier, 0, NULL);

    pgraph_vk_transition_image_layout(pg, cmd, surface->image_scratch,
                                      surface->host_fmt.vk_format,
                                      surface->image_scratch_current_layout,
                                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    surface->image_scratch_current_layout =
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    pgraph_vk_surface_transition(pg, cmd, surface,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    bool upscale = pg->surface_scale_factor > 1 &&
                   !use_compute_to_convert_depth_stencil_format;

    if (upscale) {
        VkImageBlit blitRegion = {
            .srcSubresource.aspectMask = surface->host_fmt.aspect,
            .srcSubresource.mipLevel = 0,
            .srcSubresource.baseArrayLayer = 0,
            .srcSubresource.layerCount = 1,
            .srcOffsets[0] = (VkOffset3D){0, 0, 0},
            .srcOffsets[1] = (VkOffset3D){surface->width, surface->height, 1},

            .dstSubresource.aspectMask = surface->host_fmt.aspect,
            .dstSubresource.mipLevel = 0,
            .dstSubresource.baseArrayLayer = 0,
            .dstSubresource.layerCount = 1,
            .dstOffsets[0] = (VkOffset3D){0, 0, 0},
            .dstOffsets[1] = (VkOffset3D){scaled_width, scaled_height, 1},
        };

        vkCmdBlitImage(cmd, surface->image_scratch,
                       surface->image_scratch_current_layout, surface->image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blitRegion,
                       VK_FILTER_NEAREST);
    } else {

        for (int i = 0; i < num_regions; i++) {
            VkImageAspectFlags aspect = regions[i].imageSubresource.aspectMask;
            VkImageCopy copy_region = {
                .srcSubresource.aspectMask = aspect,
                .srcSubresource.layerCount = 1,
                .dstSubresource.aspectMask = aspect,
                .dstSubresource.layerCount = 1,
                .extent = regions[i].imageExtent,
            };
            vkCmdCopyImage(cmd, surface->image_scratch,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, surface->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &copy_region);
        }
    }

    pgraph_vk_surface_transition(pg, cmd, surface,
                                 pgraph_vk_surface_rest_layout(surface));

    if (!inbatch) {
        nv2a_profile_inc_counter(NV2A_PROF_QUEUE_SUBMIT_2);
    }
    pgraph_vk_end_debug_marker(r, cmd);
    if (inbatch) {
        pgraph_vk_end_nondraw_commands(pg, cmd);
        surface->last_write_submit = r->submit_count;
        pgraph_vk_submit_batch(pg, VK_FINISH_REASON_SURFACE_CREATE);
    } else {
        pgraph_vk_end_single_time_commands(pg, cmd);
    }

    surface->initialized = true;
}

static void compare_surfaces(SurfaceBinding const *a, SurfaceBinding const *b)
{
    #define DO_CMP(fld) \
        if (a->fld != b->fld) \
            trace_nv2a_pgraph_surface_compare_mismatch( \
                #fld, (long int)a->fld, (long int)b->fld);
    DO_CMP(shape.clip_x)
    DO_CMP(shape.clip_width)
    DO_CMP(shape.clip_y)
    DO_CMP(shape.clip_height)
    DO_CMP(fmt.bytes_per_pixel)
    DO_CMP(host_fmt.vk_format)
    DO_CMP(color)
    DO_CMP(swizzle)
    DO_CMP(vram_addr)
    DO_CMP(width)
    DO_CMP(height)
    DO_CMP(pitch)
    DO_CMP(size)
    DO_CMP(dma_addr)
    DO_CMP(dma_len)
    DO_CMP(frame_time)
    DO_CMP(draw_time)
    #undef DO_CMP
}

static void populate_surface_binding_target_sized(NV2AState *d, bool color,
                                                  unsigned int width,
                                                  unsigned int height,
                                                  SurfaceBinding *target)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    Surface *surface;
    hwaddr dma_address;
    BasicSurfaceFormatInfo fmt;
    SurfaceFormatInfo host_fmt;

    if (color) {
        surface = &pg->surface_color;
        dma_address = pg->dma_color;
        assert(pg->surface_shape.color_format != 0);
        assert(pg->surface_shape.color_format <
               ARRAY_SIZE(kelvin_surface_color_format_vk_map));
        fmt = kelvin_surface_color_format_map[pg->surface_shape.color_format];
        host_fmt = kelvin_surface_color_format_vk_map[pg->surface_shape.color_format];
        if (host_fmt.host_bytes_per_pixel == 0) {
            fprintf(stderr, "nv2a: unimplemented color surface format 0x%x\n",
                    pg->surface_shape.color_format);
            abort();
        }
    } else {
        surface = &pg->surface_zeta;
        dma_address = pg->dma_zeta;
        assert(pg->surface_shape.zeta_format != 0);
        assert(pg->surface_shape.zeta_format <
               ARRAY_SIZE(r->kelvin_surface_zeta_vk_map));
        fmt = kelvin_surface_zeta_format_map[pg->surface_shape.zeta_format];
        host_fmt = r->kelvin_surface_zeta_vk_map[pg->surface_shape.zeta_format];
    }

    DMAObject dma = nv_dma_load(d, dma_address);
    assert(dma.dma_class == NV_DMA_IN_MEMORY_CLASS);
    assert(surface->offset <= dma.limit);
    assert(surface->offset + surface->pitch * height <= dma.limit + 1);
    assert(surface->pitch % fmt.bytes_per_pixel == 0);
    assert((dma.address & ~0x07FFFFFF) == 0);

    target->shape = (color || !r->color_binding) ? pg->surface_shape :
                                                   r->color_binding->shape;
    target->fmt = fmt;
    target->host_fmt = host_fmt;
    target->color = color;
    target->swizzle =
        (pg->surface_type == NV097_SET_SURFACE_FORMAT_TYPE_SWIZZLE);
    target->vram_addr = dma.address + surface->offset;
    target->width = width;
    target->height = height;
    target->pitch = surface->pitch;
    target->size = height * MAX(surface->pitch, width * fmt.bytes_per_pixel);
    target->upload_pending = true;
    target->download_pending = false;
    target->draw_dirty = false;
    target->cpu_read_frame = 0;
    target->predl_frame = 0;
    target->reads_frame = 0;
    target->dl_frame = 0;
    target->last_write_submit = SURFACE_NO_WRITE;
    target->last_use_submit = SURFACE_NO_WRITE;
    target->dma_addr = dma.address;
    target->dma_len = dma.limit;
    target->frame_time = pg->frame_time;
    target->draw_time = pg->draw_time;
    target->cleared = false;

    target->initialized = false;
}

static void populate_surface_binding_target(NV2AState *d, bool color,
                                            SurfaceBinding *target)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    unsigned int width, height;

    if (color || !r->color_binding) {
        get_surface_dimensions(pg, &width, &height);
        pgraph_apply_anti_aliasing_factor(pg, &width, &height);

        if (pg->surface_type != NV097_SET_SURFACE_FORMAT_TYPE_SWIZZLE) {
            width += pg->surface_shape.clip_x;
            height += pg->surface_shape.clip_y;
        }
    } else {
        width = r->color_binding->width;
        height = r->color_binding->height;
    }

    populate_surface_binding_target_sized(d, color, width, height, target);
}

static void update_surface_part(NV2AState *d, bool upload, bool color)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    SurfaceBinding target;
    memset(&target, 0, sizeof(target));
    populate_surface_binding_target(d, color, &target);

    Surface *pg_surface = color ? &pg->surface_color : &pg->surface_zeta;

    bool mem_dirty = !tcg_enabled() && memory_region_test_and_clear_dirty(
                                           d->vram, target.vram_addr,
                                           target.size, DIRTY_MEMORY_NV2A);

    SurfaceBinding *current_binding = color ? r->color_binding
                                            : r->zeta_binding;

    if (!current_binding ||
        (upload && (pg_surface->buffer_dirty || mem_dirty))) {
        pgraph_vk_ensure_not_in_render_pass(pg);

        unbind_surface(d, color);

        SurfaceBinding *surface = pgraph_vk_surface_get(d, target.vram_addr);
        if (alias_surfaces_enabled() && surface != NULL &&
            !check_surface_compatibility(surface, &target, false)) {
            SurfaceBinding *deeper;
            QTAILQ_FOREACH(deeper, &r->surfaces, entry) {
                if (deeper != surface &&
                    deeper->vram_addr == target.vram_addr &&
                    check_surface_compatibility(deeper, &target, false)) {
                    surface = deeper;
                    break;
                }
            }
        }
        if (surface != NULL) {
            SurfaceBinding *other = (color ? r->zeta_binding
                                           : r->color_binding);
            if (surface == other) {
                NV2A_UNIMPLEMENTED("Same color & zeta surface offset");
                unbind_surface(d, !color);
            }
        }

        trace_nv2a_pgraph_surface_target(
            color ? "COLOR" : "ZETA", target.vram_addr,
            target.swizzle ? "sz" : "ln",
            pg->surface_shape.anti_aliasing,
            pg->surface_shape.clip_x,
            pg->surface_shape.clip_width, pg->surface_shape.clip_y,
            pg->surface_shape.clip_height);

        bool should_create = true;

        if (surface != NULL) {
            bool is_compatible =
                check_surface_compatibility(surface, &target, false);

            void (*trace_fn)(uint32_t addr, uint32_t width, uint32_t height,
                             const char *layout, uint32_t anti_aliasing,
                             uint32_t clip_x, uint32_t clip_width,
                             uint32_t clip_y, uint32_t clip_height,
                             uint32_t pitch) =
                surface->color ? trace_nv2a_pgraph_surface_match_color :
                               trace_nv2a_pgraph_surface_match_zeta;

            trace_fn(surface->vram_addr, surface->width, surface->height,
                     surface->swizzle ? "sz" : "ln", surface->shape.anti_aliasing,
                     surface->shape.clip_x, surface->shape.clip_width,
                     surface->shape.clip_y, surface->shape.clip_height,
                     surface->pitch);

            assert(!(target.swizzle && pg->clearing));

#if 0
            if (surface->swizzle != target.swizzle) {
                is_compatible &= (pg->clearing || surface->cleared) &&
                    check_surface_compatibility(surface, &target, true);
                if (is_compatible) {
                    trace_nv2a_pgraph_surface_migrate_type(
                        target.swizzle ? "swizzled" : "linear");
                }
            }
#endif

            if (is_compatible && color &&
                !check_surface_compatibility(surface, &target, true)) {
                SurfaceBinding zeta_entry;
                populate_surface_binding_target_sized(
                    d, !color, surface->width, surface->height, &zeta_entry);
                hwaddr color_end = surface->vram_addr + surface->size;
                hwaddr zeta_end = zeta_entry.vram_addr + zeta_entry.size;
                is_compatible &= surface->vram_addr >= zeta_end ||
                                 zeta_entry.vram_addr >= color_end;
            }

            if (is_compatible && !color && r->color_binding) {
                is_compatible &= (surface->width == r->color_binding->width) &&
                                 (surface->height == r->color_binding->height);
            }

            if (is_compatible) {
                pg->surface_binding_dim.width = surface->width;
                pg->surface_binding_dim.clip_x = surface->shape.clip_x;
                pg->surface_binding_dim.clip_width = surface->shape.clip_width;
                pg->surface_binding_dim.height = surface->height;
                pg->surface_binding_dim.clip_y = surface->shape.clip_y;
                pg->surface_binding_dim.clip_height = surface->shape.clip_height;
                surface->upload_pending |= mem_dirty;
                pg->surface_zeta.buffer_dirty |= color;
                should_create = false;
            } else if (alias_surfaces_enabled() &&
                       (uint64_t)target.size * 4 <= surface->size &&
                       !surface->upload_pending) {
                nv2a_profile_inc_counter(NV2A_PROF_ALIAS_KEPT);
            } else {
                trace_nv2a_pgraph_surface_evict_reason(
                    "incompatible", surface->vram_addr);
                compare_surfaces(surface, &target);
                pgraph_vk_surface_download_if_dirty(d, surface);
                invalidate_surface(d, surface);
            }
        }

        if (should_create) {
            surface = get_any_compatible_invalid_surface(r, &target);
            if (surface) {
                migrate_surface_image(&target, surface);
            } else {
                surface = g_malloc(sizeof(SurfaceBinding));
                create_surface_image(pg, &target);
            }

            *surface = target;
            set_surface_label(pg, surface);
            surface_put(d, surface);

            pg->surface_binding_dim.width = target.width;
            pg->surface_binding_dim.clip_x = target.shape.clip_x;
            pg->surface_binding_dim.clip_width = target.shape.clip_width;
            pg->surface_binding_dim.height = target.height;
            pg->surface_binding_dim.clip_y = target.shape.clip_y;
            pg->surface_binding_dim.clip_height = target.shape.clip_height;

            if (color && r->zeta_binding &&
                (r->zeta_binding->width != target.width ||
                 r->zeta_binding->height != target.height)) {
                pg->surface_zeta.buffer_dirty = true;
            }
        }

        void (*trace_fn)(uint32_t addr, uint32_t width, uint32_t height,
                         const char *layout, uint32_t anti_aliasing,
                         uint32_t clip_x, uint32_t clip_width, uint32_t clip_y,
                         uint32_t clip_height, uint32_t pitch) =
            color ? (should_create ? trace_nv2a_pgraph_surface_create_color :
                                     trace_nv2a_pgraph_surface_hit_color) :
                    (should_create ? trace_nv2a_pgraph_surface_create_zeta :
                                     trace_nv2a_pgraph_surface_hit_zeta);
        trace_fn(surface->vram_addr, surface->width, surface->height,
                 surface->swizzle ? "sz" : "ln", surface->shape.anti_aliasing,
                 surface->shape.clip_x, surface->shape.clip_width,
                 surface->shape.clip_y, surface->shape.clip_height, surface->pitch);

        bind_surface(r, surface);
        pg_surface->buffer_dirty = false;
    }

    if (!upload && pg_surface->draw_dirty) {
        if (!tcg_enabled()) {
            download_surface(d, color ? r->color_binding : r->zeta_binding,
                             true);
        }

        pg_surface->write_enabled_cache = false;
        pg_surface->draw_dirty = false;
    }
}

void pgraph_vk_surface_update(NV2AState *d, bool upload, bool color_write,
                              bool zeta_write)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    pg->surface_shape.z_format =
        GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER),
                 NV_PGRAPH_SETUPRASTER_Z_FORMAT);

    color_write = color_write &&
            (pg->clearing || pgraph_color_write_enabled(pg));
    zeta_write = zeta_write && (pg->clearing || pgraph_zeta_write_enabled(pg));

    if (upload) {
        bool fb_dirty = framebuffer_dirty(pg);
        if (fb_dirty) {
            memcpy(&pg->last_surface_shape, &pg->surface_shape,
                   sizeof(SurfaceShape));
            pg->surface_color.buffer_dirty = true;
            pg->surface_zeta.buffer_dirty = true;
        }

        if (pg->surface_color.buffer_dirty) {
            unbind_surface(d, true);
        }

        if (color_write) {
            update_surface_part(d, true, true);
        }

        if (pg->surface_zeta.buffer_dirty) {
            unbind_surface(d, false);
        }

        if (zeta_write) {
            update_surface_part(d, true, false);
        }
    } else {
        if ((color_write || pg->surface_color.write_enabled_cache)
            && pg->surface_color.draw_dirty) {
            update_surface_part(d, false, true);
        }
        if ((zeta_write || pg->surface_zeta.write_enabled_cache)
            && pg->surface_zeta.draw_dirty) {
            update_surface_part(d, false, false);
        }
    }

    if (upload) {
        pg->draw_time++;
    }

    bool swizzle = (pg->surface_type == NV097_SET_SURFACE_FORMAT_TYPE_SWIZZLE);

    if (r->color_binding) {
        r->color_binding->frame_time = pg->frame_time;
        if (upload) {
            pgraph_vk_upload_surface_data(d, r->color_binding, false);
            r->color_binding->draw_time = pg->draw_time;
            r->color_binding->swizzle = swizzle;
        }
    }

    if (r->zeta_binding) {
        r->zeta_binding->frame_time = pg->frame_time;
        if (upload) {
            pgraph_vk_upload_surface_data(d, r->zeta_binding, false);
            r->zeta_binding->draw_time = pg->draw_time;
            r->zeta_binding->swizzle = swizzle;
        }
    }

    if (r->color_binding && r->zeta_binding) {
        assert(r->color_binding->width == r->zeta_binding->width);
        assert(r->color_binding->height == r->zeta_binding->height);
    }

    expire_old_surfaces(d);
    prune_invalid_surfaces(r, num_invalid_surfaces_to_keep);
}

static bool check_format_and_usage_supported(PGRAPHVkState *r, VkFormat format,
                                             VkImageUsageFlags usage)
{
    VkPhysicalDeviceImageFormatInfo2 pdif2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
        .format = format,
        .type = VK_IMAGE_TYPE_2D,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
    };
    VkImageFormatProperties2 props = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
    };
    VkResult result = vkGetPhysicalDeviceImageFormatProperties2(
        r->physical_device, &pdif2, &props);
    return result == VK_SUCCESS;
}

static bool check_surface_internal_formats_supported(
    PGRAPHVkState *r, const SurfaceFormatInfo *fmts, size_t count)
{
    bool all_supported = true;
    for (int i = 0; i < count; i++) {
        const SurfaceFormatInfo *f = &fmts[i];
        if (f->host_bytes_per_pixel) {
            all_supported &=
                check_format_and_usage_supported(r, f->vk_format, f->usage);
        }
    }
    return all_supported;
}

void pgraph_vk_init_surfaces(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    bool color_formats_supported = check_surface_internal_formats_supported(
        r, kelvin_surface_color_format_vk_map,
        ARRAY_SIZE(kelvin_surface_color_format_vk_map));
    assert(color_formats_supported);

    r->kelvin_surface_zeta_vk_map[NV097_SET_SURFACE_FORMAT_ZETA_Z16] = zeta_d16;
    if (check_surface_internal_formats_supported(r, &zeta_d24_unorm_s8_uint,
                                                 1)) {
        r->kelvin_surface_zeta_vk_map[NV097_SET_SURFACE_FORMAT_ZETA_Z24S8] =
            zeta_d24_unorm_s8_uint;
    } else if (check_surface_internal_formats_supported(
                   r, &zeta_d32_sfloat_s8_uint, 1)) {
        r->kelvin_surface_zeta_vk_map[NV097_SET_SURFACE_FORMAT_ZETA_Z24S8] =
            zeta_d32_sfloat_s8_uint;
    } else {
        assert(!"No suitable depth-stencil format supported");
    }

    QTAILQ_INIT(&r->surfaces);
    QTAILQ_INIT(&r->invalid_surfaces);

    r->downloads_pending = false;
    qemu_event_init(&r->downloads_complete, false);
    qemu_event_init(&r->dirty_surfaces_download_complete, false);

    r->color_binding = NULL;
    r->zeta_binding = NULL;
    r->framebuffer_dirty = true;

    pgraph_vk_reload_surface_scale_factor(pg);
}

void pgraph_vk_finalize_surfaces(PGRAPHState *pg)
{
    pgraph_vk_surface_flush(container_of(pg, NV2AState, pgraph));
}

void pgraph_vk_surface_flush(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    pg->surface_color.draw_dirty = false;
    pg->surface_zeta.draw_dirty = false;
    memset(&pg->last_surface_shape, 0, sizeof(pg->last_surface_shape));
    unbind_surface(d, true);
    unbind_surface(d, false);

    SurfaceBinding *s, *next;
    QTAILQ_FOREACH_SAFE(s, &r->surfaces, entry, next) {
        pgraph_vk_surface_download_if_dirty(d, s);
        invalidate_surface(d, s);
    }
    prune_invalid_surfaces(r, 0);

    pgraph_vk_reload_surface_scale_factor(pg);
}
