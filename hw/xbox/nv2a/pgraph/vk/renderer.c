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

#include "hw/xbox/nv2a/nv2a_int.h"
#include "hw/xbox/xbox-game-hacks.h"
#include "renderer.h"
#include "vr.h"
#include "vr_ui_panel.h"
#include "system/runstate.h"
#include "qemu/timer.h"

#include "gloffscreen.h"

#if HAVE_EXTERNAL_MEMORY
static GloContext *g_gl_context;
#endif

static void early_context_init(void)
{
#if HAVE_EXTERNAL_MEMORY
    g_gl_context = glo_context_create();
#endif
}


static void pgraph_vk_init(NV2AState *d, Error **errp)
{
    PGRAPHState *pg = &d->pgraph;

    pg->vk_renderer_state = (PGRAPHVkState *)g_malloc0(sizeof(PGRAPHVkState));

#if HAVE_EXTERNAL_MEMORY
    glo_set_current(g_gl_context);
#endif

    pgraph_vk_debug_init();

    pgraph_vk_latch_surface_scale_factor(pg);

    pgraph_vk_init_instance(pg, errp);
    if (*errp) {
        goto fail_state;
    }

    pgraph_vk_init_command_buffers(pg);
    if (!pgraph_vk_init_buffers(d, errp)) {
        goto fail_command_buffers;
    }
    pgraph_vk_init_surfaces(pg);
    pgraph_vk_init_shaders(pg);
    pgraph_vk_init_pipelines(pg);
    pgraph_vk_init_textures(pg);
    pgraph_vk_init_reports(pg);
    pgraph_vk_init_compute(pg);
    pgraph_vk_init_display(pg);

    pgraph_vk_update_vertex_ram_buffer(&d->pgraph, 0, d->vram_ptr,
                                   memory_region_size(d->vram));

    pgraph_vk_determine_gpu_properties(d);
    return;

fail_command_buffers:
    pgraph_vk_finalize_command_buffers(pg);
    pgraph_vk_finalize_instance(pg);
fail_state:
    g_free(pg->vk_renderer_state);
    pg->vk_renderer_state = NULL;
}

static void pgraph_vk_finalize(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;

    pgraph_vk_finalize_display(pg);
    pgraph_vk_finalize_compute(pg);
    pgraph_vk_finalize_reports(pg);
    pgraph_vk_finalize_textures(pg);
    pgraph_vk_finalize_pipelines(pg);
    pgraph_vk_finalize_shaders(pg);
    pgraph_vk_finalize_surfaces(pg);
    pgraph_vk_finalize_buffers(d);
    pgraph_vk_finalize_command_buffers(pg);
    pgraph_vk_finalize_instance(pg);

    g_free(pg->vk_renderer_state);
    pg->vk_renderer_state = NULL;
}

static void pgraph_vk_flush(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;

    pgraph_vk_finish(pg, VK_FINISH_REASON_FLUSH);
    pgraph_vk_wait_all_present_slots(pg);
    pgraph_vk_surface_flush(d);
    pgraph_vk_mark_textures_possibly_dirty(d, 0, memory_region_size(d->vram),
                                           false);
    pgraph_vk_update_vertex_ram_buffer(&d->pgraph, 0, d->vram_ptr,
                                       memory_region_size(d->vram));
    for (int i = 0; i < 4; i++) {
        pg->texture_dirty[i] = true;
    }
    pgraph_vsh_constants_forget(pg);

    qatomic_set(&d->pgraph.flush_pending, false);
    qemu_event_set(&d->pgraph.flush_complete);
}

static void pgraph_vk_sync(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;

    pgraph_vk_render_display(pg);

    xemu_vr_frame(pg);

    qatomic_set(&d->pgraph.sync_pending, false);
    qemu_event_set(&d->pgraph.sync_complete);
}

static bool s_tick_pending;
static int64_t s_tick_last_post_ms = -1;
static bool s_nosurf_said;
static struct {
    uint32_t n, ended;
    int64_t since_ms;
} s_tick_streak;

static VrUiPauseTickIn pause_tick_state(void)
{
    return (VrUiPauseTickIn){
        .guest_running = runstate_is_running(),
        .guest_stopped = vr_ui_pause_tick_settled_stop(
            runstate_check(RUN_STATE_PAUSED), runstate_check(RUN_STATE_DEBUG),
            runstate_check(RUN_STATE_PRELAUNCH)),
        .session_running = xemu_vr_active(),
        .panel_shown = vr_ui_panel_wanted(),
        .now_ms = qemu_clock_get_ms(QEMU_CLOCK_REALTIME),
    };
}

static void pause_tick_maybe_post(NV2AState *d)
{
    VrUiPauseTickIn in = pause_tick_state();
    if (!s_nosurf_said && in.guest_stopped && in.session_running) {
        s_nosurf_said = true;
        fprintf(stderr, "PenguinBox: pause tick: guest stopped with no display "
                        "surface bound (UI panel %s)\n",
                in.panel_shown ? "shown: ticking" : "closed: no tick");
    }
#ifdef XEMU_VR_PAUSE_TICK_PLANTED_DROP
    (void)d;
    return;
#else
    in.sync_runs = false;
    in.tick_pending = qatomic_read(&s_tick_pending);
    in.last_post_ms = s_tick_last_post_ms;
    if (!vr_ui_pause_tick_post(&in)) {
        return;
    }
    s_tick_last_post_ms = in.now_ms;
    qatomic_set(&s_tick_pending, true);
    pfifo_kick(d);
#endif
}

static void pause_tick_end_streak(const char *why)
{
    if (s_tick_streak.n == 0) {
        return;
    }
    fprintf(stderr, "PenguinBox: pause tick off (%s): n=%u ended=%u\n", why,
            s_tick_streak.n, s_tick_streak.ended);
    s_tick_streak.n = 0;
    s_tick_streak.ended = 0;
}

static void pause_tick_run(NV2AState *d, bool synced)
{
    if (!qatomic_read(&s_tick_pending) ||
        !qatomic_xchg(&s_tick_pending, false)) {
        return;
    }
    VrUiPauseTickIn in = pause_tick_state();
    in.sync_runs = synced;
    if (!vr_ui_pause_tick_run(&in)) {
        pause_tick_end_streak(synced ? "a sync ran" : "state changed");
        return;
    }
    if (s_tick_streak.n == 0) {
        s_tick_streak.since_ms = in.now_ms;
        fprintf(stderr, "PenguinBox: pause tick on (guest stopped, no display "
                        "surface bound, UI panel shown)\n");
    }
    const bool ended = xemu_vr_pause_tick(&d->pgraph);
    s_tick_streak.n++;
    s_tick_streak.ended += ended ? 1 : 0;
    if (!xemu_vr_active()) {
        pause_tick_end_streak("VR session ended");
        return;
    }
    if (s_tick_streak.n % 600 == 0) {
        fprintf(stderr, "PenguinBox: pause tick n=%u ended=%u uploads=%u in "
                        "%lld ms\n", s_tick_streak.n, s_tick_streak.ended,
                vr_ui_panel_uploads(),
                (long long)(in.now_ms - s_tick_streak.since_ms));
    }
}

static void pgraph_vk_process_pending(NV2AState *d)
{
    PGRAPHVkState *r = d->pgraph.vk_renderer_state;

    if (qatomic_read(&r->downloads_pending) ||
        qatomic_read(&r->download_dirty_surfaces_pending) ||
        qatomic_read(&d->pgraph.sync_pending) ||
        qatomic_read(&d->pgraph.flush_pending) ||
        qatomic_read(&s_tick_pending)
    ) {
        qemu_mutex_unlock(&d->pfifo.lock);
        qemu_mutex_lock(&d->pgraph.lock);
        bool synced = false;
        if (qatomic_read(&r->downloads_pending)) {
            pgraph_vk_process_pending_downloads(d);
        }
        if (qatomic_read(&r->download_dirty_surfaces_pending)) {
            pgraph_vk_download_dirty_surfaces(d);
        }
        if (qatomic_read(&d->pgraph.sync_pending)) {
            pgraph_vk_sync(d);
            synced = true;
            pause_tick_end_streak("a sync ran");
        }
        if (qatomic_read(&d->pgraph.flush_pending)) {
            pgraph_vk_flush(d);
        }
        pause_tick_run(d, synced);
        qemu_mutex_unlock(&d->pgraph.lock);
        qemu_mutex_lock(&d->pfifo.lock);
    }
}

static void pgraph_vk_flip_stall(NV2AState *d)
{
    pgraph_vk_stereo_flip_begin(d);

    {
        if (pgraph_vk_async_present_enabled() &&
            !pgraph_vk_batch_qreset_enabled()) {
            PGRAPHState *pg = &d->pgraph;
            PGRAPHVkState *r = pg->vk_renderer_state;
            pgraph_vk_submit_batch(pg, VK_FINISH_REASON_FLIP_STALL);
            vmaSetCurrentFrameIndex(r->allocator, r->submit_count);
            r->allocator_last_submit_index = r->submit_count;
            pgraph_vk_check_memory_budget(pg);
        } else {
            pgraph_vk_finish(&d->pgraph, VK_FINISH_REASON_FLIP_STALL);
        }
    }


    pgraph_vk_refresh_predl_toggle();
    pgraph_vk_refresh_narrowfence_toggle();
    pgraph_vk_refresh_async_present_toggle();

    xbox_game_hacks_frame_tick();


    {
        PGRAPHVkState *r = d->pgraph.vk_renderer_state;
        SurfaceBinding *s;
        QTAILQ_FOREACH(s, &r->surfaces, entry) {
            s->reads_frame = 0;
            s->dl_frame = 0;
        }
    }

    {
        PGRAPHVkState *r = d->pgraph.vk_renderer_state;
        r->exact_dirty_count = 0;
        r->exact_spans_valid = true;
    }

    pgraph_vk_debug_frame_terminator();
}

static void pgraph_vk_pre_savevm_trigger(NV2AState *d)
{
    qatomic_set(&d->pgraph.vk_renderer_state->download_dirty_surfaces_pending, true);
    qemu_event_reset(&d->pgraph.vk_renderer_state->dirty_surfaces_download_complete);
}

static void pgraph_vk_pre_savevm_wait(NV2AState *d)
{
    qemu_event_wait(&d->pgraph.vk_renderer_state->dirty_surfaces_download_complete);
}

static void pgraph_vk_pre_shutdown_trigger(NV2AState *d)
{
}

static void pgraph_vk_pre_shutdown_wait(NV2AState *d)
{
}

static int pgraph_vk_get_framebuffer_surface(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    qemu_mutex_lock(&d->pfifo.lock);

    VGADisplayParams vga_display_params;
    d->vga.get_params(&d->vga, &vga_display_params);

    SurfaceBinding *surface = pgraph_vk_surface_get_within(
        d, d->pcrtc.start + vga_display_params.line_offset);
    if (surface == NULL || !surface->color) {
        pause_tick_maybe_post(d);
        qemu_mutex_unlock(&d->pfifo.lock);
        return 0;
    }

    assert(surface->color);
    s_nosurf_said = false;

    surface->frame_time = pg->frame_time;

#if HAVE_EXTERNAL_MEMORY
    qemu_event_reset(&d->pgraph.sync_complete);
    qatomic_set(&pg->sync_pending, true);
    pfifo_kick(d);
    qemu_mutex_unlock(&d->pfifo.lock);
    qemu_event_wait(&d->pgraph.sync_complete);

    if (r->display.present_published_valid) {
        if (vkGetFenceStatus(r->device, r->display.present_published_fence) !=
            VK_SUCCESS) {
            nv2a_profile_inc_counter(NV2A_PROF_ASYNC_PRESENT_GATE_WAIT);
            VK_CHECK(vkWaitForFences(r->device, 1,
                                     &r->display.present_published_fence,
                                     VK_TRUE, UINT64_MAX));
        }
        return r->display.present_published_gl_id;
    }
    return r->display.gl_texture_id;
#else
    qemu_mutex_unlock(&d->pfifo.lock);
    pgraph_vk_wait_for_surface_download(surface);
    return 0;
#endif
}

static PGRAPHRenderer pgraph_vk_renderer = {
    .type = CONFIG_DISPLAY_RENDERER_VULKAN,
    .name = "Vulkan",
    .ops = {
        .init = pgraph_vk_init,
        .early_context_init = early_context_init,
        .finalize = pgraph_vk_finalize,
        .clear_report_value = pgraph_vk_clear_report_value,
        .clear_surface = pgraph_vk_clear_surface,
        .draw_begin = pgraph_vk_draw_begin,
        .draw_end = pgraph_vk_draw_end,
        .flip_stall = pgraph_vk_flip_stall,
        .flush_draw = pgraph_vk_flush_draw,
        .get_report = pgraph_vk_get_report,
        .image_blit = pgraph_vk_image_blit,
        .pre_savevm_trigger = pgraph_vk_pre_savevm_trigger,
        .pre_savevm_wait = pgraph_vk_pre_savevm_wait,
        .pre_shutdown_trigger = pgraph_vk_pre_shutdown_trigger,
        .pre_shutdown_wait = pgraph_vk_pre_shutdown_wait,
        .process_pending = pgraph_vk_process_pending,
        .process_pending_reports = pgraph_vk_process_pending_reports,
        .surface_update = pgraph_vk_surface_update,
        .get_surface_scale_factor = pgraph_vk_get_surface_scale_factor,
        .get_framebuffer_surface = pgraph_vk_get_framebuffer_surface,
        .get_gpu_properties = pgraph_vk_get_gpu_properties,
    }
};

static void __attribute__((constructor)) register_renderer(void)
{
    pgraph_renderer_register(&pgraph_vk_renderer);
}

void pgraph_vk_check_memory_budget(PGRAPHState *pg)
{
#if 0
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkPhysicalDeviceMemoryProperties const *props;
    vmaGetMemoryProperties(r->allocator, &props);

    g_autofree VmaBudget *budgets = g_malloc_n(props->memoryHeapCount, sizeof(VmaBudget));
    vmaGetHeapBudgets(r->allocator, budgets);

    const float budget_threshold = 0.8;
    bool near_budget = false;

    for (int i = 0; i < props->memoryHeapCount; i++) {
        VmaBudget *b = &budgets[i];
        float use_to_budget_ratio =
            (double)b->statistics.allocationBytes / (double)b->budget;
        NV2A_VK_DPRINTF("Heap %d: used %lu/%lu MiB (%.2f%%)", i,
                        b->statistics.allocationBytes / (1024 * 1024),
                        b->budget / (1024 * 1024), use_to_budget_ratio * 100);
        near_budget |= use_to_budget_ratio > budget_threshold;
    }

    if (near_budget) {
        pgraph_vk_trim_texture_cache(pg);
    }
#endif

#if 0
    char *s;
    vmaBuildStatsString(r->allocator, &s, VK_TRUE);
    puts(s);
    vmaFreeStatsString(r->allocator, s);
#endif
}
