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
#include "compute_need.h"

#include <inttypes.h>

static VkResult create_buffer(PGRAPHState *pg, StorageBuffer *buffer)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkBufferCreateInfo buffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = buffer->buffer_size,
        .usage = buffer->usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkResult vk_result = vmaCreateBuffer(r->allocator, &buffer_create_info,
                                         &buffer->alloc_info, &buffer->buffer,
                                         &buffer->allocation, NULL);
    if (vk_result != VK_SUCCESS) {
        int idx = (int)(buffer - r->storage_buffers);
        if (idx < 0 || idx >= BUFFER_COUNT) {
            idx = -1;
        }
        fprintf(stderr,
                "nv2a/vk: FATAL: storage buffer %d allocation failed "
                "(size=%" PRIu64 " MiB, VkResult=%d)\n",
                idx, (uint64_t)(buffer->buffer_size >> 20), (int)vk_result);
        const VkPhysicalDeviceMemoryProperties *mp;
        vmaGetMemoryProperties(r->allocator, &mp);
        for (uint32_t h = 0; h < mp->memoryHeapCount; h++) {
            fprintf(stderr, "nv2a/vk:   heap %u: %" PRIu64 " MiB%s\n", h,
                    (uint64_t)(mp->memoryHeaps[h].size >> 20),
                    (mp->memoryHeaps[h].flags &
                     VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ?
                        " (device-local)" : "");
        }
        PgraphVkBufferPlan plan;
        pgraph_vk_buffer_plan(container_of(pg, NV2AState, pgraph), &plan);
        fprintf(stderr,
                "nv2a/vk: the Vulkan renderer allocates %" PRIu64 " MiB of "
                "device-local and %" PRIu64 " MiB of host working buffers at "
                "start. Close other GPU-heavy apps, or set "
                "display.renderer = 'OPENGL' in xemu.toml.\n",
                pgraph_vk_mib_up(plan.device_bytes),
                pgraph_vk_mib_up(plan.host_bytes));
    }
    return vk_result;
}

char *pgraph_vk_describe_heaps(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    const VkPhysicalDeviceMemoryProperties *mp;
    VmaBudget budgets[VK_MAX_MEMORY_HEAPS];
    GString *s = g_string_new("");

    vmaGetMemoryProperties(r->allocator, &mp);
    vmaGetHeapBudgets(r->allocator, budgets);
    for (uint32_t h = 0; h < mp->memoryHeapCount; h++) {
        g_string_append_printf(
            s, "%sheap %u: %" PRIu64 " MiB%s, budget %" PRIu64 " MiB, in use %" PRIu64 " MiB",
            h ? "; " : "", h, (uint64_t)(mp->memoryHeaps[h].size >> 20),
            (mp->memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? " device-local" : "",
            (uint64_t)(budgets[h].budget >> 20), (uint64_t)(budgets[h].usage >> 20));
    }
    return g_string_free(s, false);
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

static VkResult compute_pair_create(PGRAPHVkState *r, StorageBuffer *b)
{
    VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = b->buffer_size,
        .usage = b->usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    return vmaCreateBuffer(r->allocator, &info, &b->alloc_info, &b->buffer, &b->allocation, NULL);
}

static bool compute_pair_wants(PGRAPHVkState *r, const uint64_t need[2], uint64_t want[2])
{
    static const int idx[2] = { BUFFER_COMPUTE_DST, BUFFER_COMPUTE_SRC };

    for (int i = 0; i < 2; i++) {
        want[i] = 0;
        if (need[i] > r->compute_pair.cap) {
            return false;
        }
        if (need[i] > r->storage_buffers[idx[i]].buffer_size) {
            want[i] = nv2a_cn_grow_bytes(need[i], r->compute_pair.cap);
            if (r->compute_pair.failed_bytes && want[i] >= r->compute_pair.failed_bytes) {
                return false;
            }
        }
    }
    return true;
}

static bool compute_pair_holds(PGRAPHVkState *r, const uint64_t need[2])
{
    return need[0] <= r->storage_buffers[BUFFER_COMPUTE_DST].buffer_size &&
           need[1] <= r->storage_buffers[BUFFER_COMPUTE_SRC].buffer_size;
}

bool pgraph_vk_compute_pair_make_room(PGRAPHState *pg, uint64_t need_dst, uint64_t need_src, const char *user)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    const uint64_t need[2] = { need_dst, need_src };
    uint64_t want[2];

    if (compute_pair_holds(r, need)) {
        return true;
    }
    if (!compute_pair_wants(r, need, want)) {
        if (!r->compute_pair.over_cap_logged) {
            r->compute_pair.over_cap_logged = true;
            fprintf(stderr, "nv2a/vk: compute pair: a depth/stencil %s needs %" PRIu64 " + %" PRIu64 " bytes, above "
                    "the cap of %" PRIu64 " bytes a buffer or a size that failed to allocate (%" PRIu64 "), at scale "
                    "%ux: it is skipped; later ones are not logged\n", user, need[0], need[1], r->compute_pair.cap,
                    r->compute_pair.failed_bytes, r->compute_pair.scale);
        }
        return false;
    }
    r->compute_pair.want[0] = MAX(r->compute_pair.want[0], want[0]);
    r->compute_pair.want[1] = MAX(r->compute_pair.want[1], want[1]);
    pgraph_vk_finish(pg, VK_FINISH_REASON_NEED_BUFFER_SPACE);
    return compute_pair_holds(r, need);
}

bool pgraph_vk_compute_pair_fits_or_request(PGRAPHVkState *r, uint64_t need_dst, uint64_t need_src)
{
    const uint64_t need[2] = { need_dst, need_src };
    uint64_t want[2];

    if (compute_pair_holds(r, need)) {
        return true;
    }
    if (compute_pair_wants(r, need, want)) {
        r->compute_pair.want[0] = MAX(r->compute_pair.want[0], want[0]);
        r->compute_pair.want[1] = MAX(r->compute_pair.want[1], want[1]);
    }
    return false;
}

void pgraph_vk_compute_pair_grow(PGRAPHVkState *r)
{
    static const int idx[2] = { BUFFER_COMPUTE_DST, BUFFER_COMPUTE_SRC };
    static const char *const name[2] = { "compute destination", "compute source" };

    if (!r->compute_pair.want[0] && !r->compute_pair.want[1]) {
        return;
    }
    assert(!r->in_command_buffer);
    assert(!r->in_aux_command_buffer);
    for (int i = 0; i < CB_FENCE_RING_SIZE; i++) {
        assert(r->cb_fence_ring[i].submit_index == SURFACE_NO_WRITE ||
               vkGetFenceStatus(r->device, r->cb_fence_ring[i].fence) == VK_SUCCESS);
    }
    for (int i = 0; i < 2; i++) {
        StorageBuffer *b = &r->storage_buffers[idx[i]];
        uint64_t want = r->compute_pair.want[i];
        r->compute_pair.want[i] = 0;
        if (want <= b->buffer_size) {
            continue;
        }
        uint64_t old = b->buffer_size;
        vmaDestroyBuffer(r->allocator, b->buffer, b->allocation);
        b->buffer = VK_NULL_HANDLE;
        b->allocation = VK_NULL_HANDLE;
        b->buffer_size = want;
        VkResult res = compute_pair_create(r, b);
        if (res == VK_SUCCESS) {
            r->compute_pair.grows++;
            fprintf(stderr, "nv2a/vk: compute pair: the %s buffer grew from %" PRIu64 " to %" PRIu64 " bytes at "
                    "scale %ux (grow %u)\n", name[i], old, want, r->compute_pair.scale, r->compute_pair.grows);
            continue;
        }
        r->compute_pair.failed_bytes = r->compute_pair.failed_bytes ? MIN(r->compute_pair.failed_bytes, want) : want;
        fprintf(stderr, "nv2a/vk: compute pair: the %s buffer could not grow from %" PRIu64 " to %" PRIu64 " bytes "
                "(VkResult %d): a conversion that needs it is skipped\n", name[i], old, want, (int)res);
        b->buffer_size = old;
        res = vmaCreateBuffer(r->allocator,
                              &(VkBufferCreateInfo){ .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                                     .size = old, .usage = b->usage,
                                                     .sharingMode = VK_SHARING_MODE_EXCLUSIVE },
                              &b->alloc_info, &b->buffer, &b->allocation, NULL);
        if (res != VK_SUCCESS) {
            fprintf(stderr, "nv2a/vk: FATAL: the %s buffer could not be re-created at its previous %" PRIu64
                    " bytes (VkResult %d)\n", name[i], old, (int)res);
            abort();
        }
    }
}

void pgraph_vk_buffer_plan(NV2AState *d, PgraphVkBufferPlan *plan)
{
    PGRAPHState *pg = &d->pgraph;
    static const char *const names[BUFFER_COUNT] = {
        [BUFFER_STAGING_DST] = "staging (download)",
        [BUFFER_STAGING_SRC] = "staging (upload)",
        [BUFFER_COMPUTE_DST] = "compute destination",
        [BUFFER_COMPUTE_SRC] = "compute source",
        [BUFFER_INDEX] = "index",
        [BUFFER_INDEX_STAGING] = "index staging",
        [BUFFER_VERTEX_RAM] = "vertex RAM mirror",
        [BUFFER_VERTEX_INLINE] = "inline vertex",
        [BUFFER_VERTEX_INLINE_STAGING] = "inline vertex staging",
        [BUFFER_UNIFORM] = "uniform",
        [BUFFER_UNIFORM_STAGING] = "uniform staging",
    };
    const VkDeviceSize staging = 4096 * 4096 * 4;
    VkDeviceSize compute = nv2a_cn_pair_base(pg->surface_scale_factor);
    const VkDeviceSize index = sizeof(pg->inline_elements) * 10;
    const VkDeviceSize vertex_inline = (VkDeviceSize)NV2A_VERTEXSHADER_ATTRIBUTES *
                                       NV2A_MAX_BATCH_LENGTH * 4 * sizeof(float);
    const VkDeviceSize uniform = 8 * 1024 * 1024;
    const VkDeviceSize sizes[BUFFER_COUNT] = {
        [BUFFER_STAGING_DST] = staging,
        [BUFFER_STAGING_SRC] = staging,
        [BUFFER_COMPUTE_DST] = compute,
        [BUFFER_COMPUTE_SRC] = compute,
        [BUFFER_INDEX] = index,
        [BUFFER_INDEX_STAGING] = index,
        [BUFFER_VERTEX_RAM] = memory_region_size(d->vram),
        [BUFFER_VERTEX_INLINE] = vertex_inline,
        [BUFFER_VERTEX_INLINE_STAGING] = vertex_inline,
        [BUFFER_UNIFORM] = uniform,
        [BUFFER_UNIFORM_STAGING] = uniform,
    };
    static const bool device_local[BUFFER_COUNT] = {
        [BUFFER_COMPUTE_DST] = true,
        [BUFFER_COMPUTE_SRC] = true,
        [BUFFER_INDEX] = true,
        [BUFFER_VERTEX_INLINE] = true,
        [BUFFER_UNIFORM] = true,
    };

    plan->device_bytes = plan->host_bytes = 0;
    for (int i = 0; i < BUFFER_COUNT; i++) {
        plan->size[i] = sizes[i];
        plan->device_local[i] = device_local[i];
        plan->name[i] = names[i];
        if (device_local[i]) {
            plan->device_bytes += sizes[i];
        } else {
            plan->host_bytes += sizes[i];
        }
    }
}

bool pgraph_vk_init_buffers(NV2AState *d, Error **errp)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;
    PgraphVkBufferPlan plan;

    pgraph_vk_buffer_plan(d, &plan);
    r->compute_pair = (typeof(r->compute_pair)){
        .cap = NV2A_CN_PAIR_CAP,
        .scale = pg->surface_scale_factor,
    };
    fprintf(stderr, "nv2a/vk: working buffers at scale %ux: %" PRIu64 " B device-local, %" PRIu64 " B host (compute "
            "pair %" PRIu64 " B a buffer, cap %" PRIu64 "; inline vertex %" PRIu64 " B; index %" PRIu64 " B)\n",
            pg->surface_scale_factor, plan.device_bytes, plan.host_bytes, (uint64_t)plan.size[BUFFER_COMPUTE_DST],
            (uint64_t)NV2A_CN_PAIR_CAP, (uint64_t)plan.size[BUFFER_VERTEX_INLINE], (uint64_t)plan.size[BUFFER_INDEX]);

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
        .buffer_size = plan.size[BUFFER_STAGING_DST],
    };

    r->storage_buffers[BUFFER_STAGING_SRC] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .buffer_size = plan.size[BUFFER_STAGING_SRC],
    };

    r->storage_buffers[BUFFER_COMPUTE_DST] = (StorageBuffer){
        .alloc_info = device_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .buffer_size = plan.size[BUFFER_COMPUTE_DST],
    };

    r->storage_buffers[BUFFER_COMPUTE_SRC] = (StorageBuffer){
        .alloc_info = device_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .buffer_size = plan.size[BUFFER_COMPUTE_SRC],
    };

    r->storage_buffers[BUFFER_INDEX] = (StorageBuffer){
        .alloc_info = device_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        .buffer_size = plan.size[BUFFER_INDEX],
    };

    r->storage_buffers[BUFFER_INDEX_STAGING] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .buffer_size = plan.size[BUFFER_INDEX_STAGING],
    };

    r->storage_buffers[BUFFER_VERTEX_RAM] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .buffer_size = plan.size[BUFFER_VERTEX_RAM],
    };

    r->bitmap_size = memory_region_size(d->vram) / 4096;
    r->uploaded_bitmap = bitmap_new(r->bitmap_size);
    bitmap_clear(r->uploaded_bitmap, 0, r->bitmap_size);
    r->mirror_stale_bitmap = bitmap_new(r->bitmap_size);
    bitmap_clear(r->mirror_stale_bitmap, 0, r->bitmap_size);

    r->storage_buffers[BUFFER_VERTEX_INLINE] = (StorageBuffer){
        .alloc_info = device_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .buffer_size = plan.size[BUFFER_VERTEX_INLINE],
    };

    r->storage_buffers[BUFFER_VERTEX_INLINE_STAGING] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .buffer_size = plan.size[BUFFER_VERTEX_INLINE_STAGING],
    };

    r->storage_buffers[BUFFER_UNIFORM] = (StorageBuffer){
        .alloc_info = device_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .buffer_size = plan.size[BUFFER_UNIFORM],
    };

    r->storage_buffers[BUFFER_UNIFORM_STAGING] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .buffer_size = plan.size[BUFFER_UNIFORM_STAGING],
    };

    for (int i = 0; i < BUFFER_COUNT; i++) {
        assert((r->storage_buffers[i].alloc_info.usage ==
                VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE) == plan.device_local[i]);
        VkResult res = create_buffer(pg, &r->storage_buffers[i]);
        if (res != VK_SUCCESS) {
            g_autofree char *heaps = pgraph_vk_describe_heaps(pg);
            error_setg(errp,
                       "the %s buffer (%" PRIu64 " MiB, %s) could not be "
                       "allocated (VkResult %d). At start the Vulkan renderer "
                       "needs %" PRIu64 " MiB of device-local and %" PRIu64
                       " MiB of host memory; %s",
                       plan.name[i], pgraph_vk_mib_up(plan.size[i]),
                       plan.device_local[i] ? "device-local" : "host", (int)res,
                       pgraph_vk_mib_up(plan.device_bytes),
                       pgraph_vk_mib_up(plan.host_bytes), heaps);
            pgraph_vk_finalize_buffers(d);
            return false;
        }
    }

    int buffers_to_map[] = { BUFFER_VERTEX_RAM,
                             BUFFER_INDEX_STAGING,
                             BUFFER_VERTEX_INLINE_STAGING,
                             BUFFER_UNIFORM_STAGING };

    for (int i = 0; i < ARRAY_SIZE(buffers_to_map); i++) {
        VkResult res = vmaMapMemory(
            r->allocator, r->storage_buffers[buffers_to_map[i]].allocation,
            (void **)&r->storage_buffers[buffers_to_map[i]].mapped);
        if (res != VK_SUCCESS) {
            error_setg(errp, "the %s buffer could not be mapped (VkResult %d)",
                       plan.name[buffers_to_map[i]], (int)res);
            pgraph_vk_finalize_buffers(d);
            return false;
        }
    }

    r->inbatch_uploads = resolve_inbatch_uploads_toggle();
    r->vtx_war_fix = pgraph_vk_vtx_war_fix_enabled();
    assert(r->storage_buffers[BUFFER_STAGING_SRC].buffer_size >
           STAGING_RING_RESERVE);
    r->staging_ring.capacity =
        r->storage_buffers[BUFFER_STAGING_SRC].buffer_size -
        STAGING_RING_RESERVE;
    pgraph_vk_staging_ring_reset(r);
    return true;
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
    g_free(r->mirror_stale_bitmap);
    r->mirror_stale_bitmap = NULL;
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
