/*
 * Geforce NV2A PGRAPH Vulkan Renderer
 *
 * Copyright (c) 2024 Matt Borgerson
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

#include "renderer.h"

static void create_buffer(PGRAPHState *pg, StorageBuffer *buffer)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkBufferCreateInfo buffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = buffer->buffer_size,
        .usage = buffer->usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_CHECK(vmaCreateBuffer(r->allocator, &buffer_create_info,
                             &buffer->alloc_info, &buffer->buffer,
                             &buffer->allocation, NULL));
}

static void destroy_buffer(PGRAPHState *pg, StorageBuffer *buffer)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    vmaDestroyBuffer(r->allocator, buffer->buffer, buffer->allocation);
    buffer->buffer = VK_NULL_HANDLE;
    buffer->allocation = VK_NULL_HANDLE;
}

static bool resolve_inbatch_uploads_toggle(void)
{
    static int resolved = -1;
    if (resolved < 0) {
        if (getenv("XEMU_NO_INBATCH_UPLOADS")) {
            resolved = 0;
        } else {
            resolved = getenv("XEMU_INBATCH_UPLOADS") ? 1 : 0;
        }
        fprintf(stderr, "inbatch-uploads: %s\n", resolved ? "ON" : "OFF");
    }
    return resolved == 1;
}

void pgraph_vk_staging_ring_reset(PGRAPHVkState *r)
{
    r->staging_ring.head = 0;
    r->staging_ring.tail = 0;
    r->staging_ring.pending_first = 0;
    r->staging_ring.pending_count = 0;
}

static void staging_ring_reclaim(PGRAPHVkState *r)
{
    while (r->staging_ring.pending_count > 0) {
        struct StagingRingRegion *reg =
            &r->staging_ring.pending[r->staging_ring.pending_first];
        uint32_t idx = reg->submit_index;

        if (idx != SURFACE_NO_WRITE) {
            if (idx >= r->submit_count) {
                break;
            }
            struct CbFenceSlot *slot =
                &r->cb_fence_ring[idx % CB_FENCE_RING_SIZE];
            if (slot->submit_index == idx &&
                vkGetFenceStatus(r->device, slot->fence) != VK_SUCCESS) {
                break;
            }
        }

        r->staging_ring.tail = reg->end;
        r->staging_ring.pending_first =
            (r->staging_ring.pending_first + 1) % STAGING_RING_MAX_PENDING;
        r->staging_ring.pending_count--;
    }
    if (r->staging_ring.pending_count == 0) {
        r->staging_ring.head = 0;
        r->staging_ring.tail = 0;
        r->staging_ring.pending_first = 0;
    }
}

bool pgraph_vk_staging_ring_alloc(PGRAPHState *pg, VkDeviceSize size,
                                  VkDeviceSize *offset,
                                  uint32_t consumer_submit_index)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    assert(r->inbatch_uploads);
    assert(consumer_submit_index == SURFACE_NO_WRITE ||
           (r->in_command_buffer &&
            consumer_submit_index == r->submit_count));

    staging_ring_reclaim(r);

    uint64_t cap = r->staging_ring.capacity;
    uint64_t need = ROUND_UP((uint64_t)size, STAGING_RING_ALIGN);
    if (need == 0 || need > cap) {
        return false;
    }

    uint64_t begin = r->staging_ring.head;
    uint64_t head = begin;
    uint64_t phys = head % cap;
    if (phys + need > cap) {
        head += cap - phys;
    }
    if (head + need - r->staging_ring.tail > cap) {
        return false;
    }
    uint64_t end = head + need;

    struct StagingRingRegion *last = NULL;
    if (r->staging_ring.pending_count > 0) {
        int last_i = (r->staging_ring.pending_first +
                      r->staging_ring.pending_count - 1) %
                     STAGING_RING_MAX_PENDING;
        last = &r->staging_ring.pending[last_i];
    }
    if (last && last->submit_index == consumer_submit_index &&
        last->end == begin) {
        last->end = end;
    } else {
        if (r->staging_ring.pending_count >= STAGING_RING_MAX_PENDING) {
            return false;
        }
        int slot_i = (r->staging_ring.pending_first +
                      r->staging_ring.pending_count) %
                     STAGING_RING_MAX_PENDING;
        r->staging_ring.pending[slot_i] = (struct StagingRingRegion){
            .begin = begin,
            .end = end,
            .submit_index = consumer_submit_index,
        };
        r->staging_ring.pending_count++;
    }

    r->staging_ring.head = end;
    *offset = STAGING_RING_RESERVE + (head % cap);
    return true;
}

void pgraph_vk_init_buffers(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    VmaAllocationCreateInfo host_alloc_create_info = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                 VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
    };
    VmaAllocationCreateInfo device_alloc_create_info = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT
    };

    r->storage_buffers[BUFFER_STAGING_DST] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .buffer_size = 4096 * 4096 * 4,
    };

    r->storage_buffers[BUFFER_STAGING_SRC] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .buffer_size = r->storage_buffers[BUFFER_STAGING_DST].buffer_size,
    };

    r->storage_buffers[BUFFER_COMPUTE_DST] = (StorageBuffer){
        .alloc_info = device_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .buffer_size = (1024 * 10) * (1024 * 10) * 8,
    };

    r->storage_buffers[BUFFER_COMPUTE_SRC] = (StorageBuffer){
        .alloc_info = device_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .buffer_size = r->storage_buffers[BUFFER_COMPUTE_DST].buffer_size,
    };

    r->storage_buffers[BUFFER_INDEX] = (StorageBuffer){
        .alloc_info = device_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        .buffer_size = sizeof(pg->inline_elements) * 100,
    };

    r->storage_buffers[BUFFER_INDEX_STAGING] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .buffer_size = r->storage_buffers[BUFFER_INDEX].buffer_size,
    };

    r->storage_buffers[BUFFER_VERTEX_RAM] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .buffer_size = memory_region_size(d->vram),
    };

    r->bitmap_size = memory_region_size(d->vram) / 4096;
    r->uploaded_bitmap = bitmap_new(r->bitmap_size);
    bitmap_clear(r->uploaded_bitmap, 0, r->bitmap_size);

    r->storage_buffers[BUFFER_VERTEX_INLINE] = (StorageBuffer){
        .alloc_info = device_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .buffer_size = NV2A_VERTEXSHADER_ATTRIBUTES * NV2A_MAX_BATCH_LENGTH *
                       4 * sizeof(float) * 10,
    };

    r->storage_buffers[BUFFER_VERTEX_INLINE_STAGING] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .buffer_size = r->storage_buffers[BUFFER_VERTEX_INLINE].buffer_size,
    };

    r->storage_buffers[BUFFER_UNIFORM] = (StorageBuffer){
        .alloc_info = device_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .buffer_size = 8 * 1024 * 1024,
    };

    r->storage_buffers[BUFFER_UNIFORM_STAGING] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .buffer_size = r->storage_buffers[BUFFER_UNIFORM].buffer_size,
    };

    for (int i = 0; i < BUFFER_COUNT; i++) {
        create_buffer(pg, &r->storage_buffers[i]);
    }

    int buffers_to_map[] = { BUFFER_VERTEX_RAM,
                             BUFFER_INDEX_STAGING,
                             BUFFER_VERTEX_INLINE_STAGING,
                             BUFFER_UNIFORM_STAGING };

    for (int i = 0; i < ARRAY_SIZE(buffers_to_map); i++) {
        VK_CHECK(vmaMapMemory(
            r->allocator, r->storage_buffers[buffers_to_map[i]].allocation,
            (void **)&r->storage_buffers[buffers_to_map[i]].mapped));
    }

    r->inbatch_uploads = resolve_inbatch_uploads_toggle();
    assert(r->storage_buffers[BUFFER_STAGING_SRC].buffer_size >
           STAGING_RING_RESERVE);
    r->staging_ring.capacity =
        r->storage_buffers[BUFFER_STAGING_SRC].buffer_size -
        STAGING_RING_RESERVE;
    pgraph_vk_staging_ring_reset(r);
}

void pgraph_vk_finalize_buffers(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    for (int i = 0; i < BUFFER_COUNT; i++) {
        if (r->storage_buffers[i].mapped) {
            vmaUnmapMemory(r->allocator, r->storage_buffers[i].allocation);
        }
        destroy_buffer(pg, &r->storage_buffers[i]);
    }

    g_free(r->uploaded_bitmap);
    r->uploaded_bitmap = NULL;
}

bool pgraph_vk_buffer_has_space_for(PGRAPHState *pg, int index,
                                    VkDeviceSize size,
                                    VkDeviceAddress alignment)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    StorageBuffer *b = &r->storage_buffers[index];
    return (ROUND_UP(b->buffer_offset, alignment) + size) <= b->buffer_size;
}

VkDeviceSize pgraph_vk_append_to_buffer(PGRAPHState *pg, int index, void **data,
                                        VkDeviceSize *sizes, size_t count,
                                        VkDeviceAddress alignment)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkDeviceSize total_size = 0;
    for (int i = 0; i < count; i++) {
        total_size += sizes[i];
    }
    assert(pgraph_vk_buffer_has_space_for(pg, index, total_size, alignment));

    StorageBuffer *b = &r->storage_buffers[index];
    VkDeviceSize starting_offset = ROUND_UP(b->buffer_offset, alignment);

    assert(b->mapped);

    for (int i = 0; i < count; i++) {
        b->buffer_offset = ROUND_UP(b->buffer_offset, alignment);
        memcpy(b->mapped + b->buffer_offset, data[i], sizes[i]);
        b->buffer_offset += sizes[i];
    }

    return starting_offset;
}
