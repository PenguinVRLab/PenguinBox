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

bool pgraph_vk_query_ring_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        enabled = (getenv("XEMU_QUERY_RING") != NULL &&
                   getenv("XEMU_NO_QUERY_RING") == NULL)
                      ? 1
                      : 0;
        fprintf(stderr,
                "query-ring: deferred-epoch STALLED occlusion-query resolve %s\n",
                enabled ? "ON" : "OFF");
    }
    return enabled;
}

static uint32_t query_ring_env_u32(const char *name, uint32_t def, uint32_t lo,
                                   uint32_t hi)
{
    const char *v = getenv(name);
    if (v == NULL || *v == '\0') {
        return def;
    }
    long n = strtol(v, NULL, 10);
    if (n < (long)lo) {
        n = (long)lo;
    }
    if (n > (long)hi) {
        n = (long)hi;
    }
    return (uint32_t)n;
}

void pgraph_vk_init_reports(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    QSIMPLEQ_INIT(&r->report_queue);
    r->num_queries_in_flight = 0;
    r->queries_submitted = 0;
    r->new_query_needed = false;
    r->query_in_flight = false;
    r->zpass_pixel_count_result = 0;

    r->async_queries_enabled = pgraph_vk_async_queries_enabled();

    r->async_reports_enabled = pgraph_vk_async_reports_enabled();
    r->batch_qreset_enabled = pgraph_vk_batch_qreset_enabled();
    r->report_marker_pending = false;
    r->report_marker_watermark = 0;
    r->report_marker_submit = 0;
    pg->report_marker_pending = false;
    r->query_pool_reset_pending = false;

    r->query_ring_enabled = pgraph_vk_query_ring_enabled();

    if (r->query_ring_enabled &&
        (r->async_reports_enabled || r->batch_qreset_enabled)) {
        r->query_ring_enabled = false;
        fprintf(stderr, "query-ring: force-OFF (superseded by "
                        "async-reports/batch-qreset; stale serve refuted)\n");
    }
    if (r->async_queries_enabled && r->async_reports_enabled) {
        fprintf(stderr, "async-reports: legacy XEMU_ASYNC_QUERIES resolve "
                        "shadowed (marker path takes precedence)\n");
    }

    if (r->query_ring_enabled) {
        r->query_region_size =
            query_ring_env_u32("XEMU_QUERY_RING_SLOTS", 512, 64, 4096);
        r->query_num_regions =
            query_ring_env_u32("XEMU_QUERY_RING_REGIONS", 8, 2, 64);
        r->query_ring_lag = query_ring_env_u32("XEMU_QUERY_RING_LAG", 3, 1,
                                               r->query_num_regions - 1);
    } else if (r->batch_qreset_enabled) {
        r->query_region_size = 1024;
        r->query_num_regions = 2;
        r->query_ring_lag = 0;
    } else {
        r->query_region_size = 1024;
        r->query_num_regions = 1;
        r->query_ring_lag = 0;
    }
    r->query_epoch_seq = 0;
    r->query_epoch_base = 0;
    r->query_region_origin = 0;
    r->max_queries_in_flight =
        (int)(r->query_region_size * r->query_num_regions);

    r->query_pool_reset_pending = r->batch_qreset_enabled;

    r->query_epochs = g_new0(QueryEpoch, r->query_num_regions);

    r->query_index_submit =
        g_malloc_n(r->max_queries_in_flight, sizeof(uint32_t));
    memset(r->query_index_submit, 0xFF,
           r->max_queries_in_flight * sizeof(uint32_t));

    VkQueryPoolCreateInfo pool_create_info = (VkQueryPoolCreateInfo){
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_OCCLUSION,
        .queryCount = r->max_queries_in_flight,
    };
    VK_CHECK(
        vkCreateQueryPool(r->device, &pool_create_info, NULL, &r->query_pool));

    if (r->query_ring_enabled) {
        fprintf(stderr,
                "query-ring: %u regions x %u slots (pool %d), lag %u epochs\n",
                r->query_num_regions, r->query_region_size,
                r->max_queries_in_flight, r->query_ring_lag);
    }
}

void pgraph_vk_finalize_reports(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    pg->report_marker_pending = false;

    QueryReport *report;
    while ((report = QSIMPLEQ_FIRST(&r->report_queue)) != NULL) {
        QSIMPLEQ_REMOVE_HEAD(&r->report_queue, entry);
        g_free(report);
    }

    vkDestroyQueryPool(r->device, r->query_pool, NULL);

    g_free(r->query_index_submit);
    r->query_index_submit = NULL;

    g_free(r->query_epochs);
    r->query_epochs = NULL;
}

void pgraph_vk_clear_report_value(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    QueryReport *report = g_malloc(sizeof(QueryReport));
    report->clear = true;
    report->parameter = 0;
    report->query_count = r->num_queries_in_flight;
    report->enqueue_us =
        r->async_reports_enabled ? g_get_monotonic_time() : 0;
    QSIMPLEQ_INSERT_TAIL(&r->report_queue, report, entry);

    r->new_query_needed = true;
}

void pgraph_vk_get_report(NV2AState *d, uint32_t parameter)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    uint8_t type = GET_MASK(parameter, NV097_GET_REPORT_TYPE);
    assert(type == NV097_GET_REPORT_TYPE_ZPASS_PIXEL_CNT);

    QueryReport *report = g_malloc(sizeof(QueryReport));
    report->clear = false;
    report->parameter = parameter;
    report->query_count = r->num_queries_in_flight;
    report->enqueue_us =
        r->async_reports_enabled ? g_get_monotonic_time() : 0;
    QSIMPLEQ_INSERT_TAIL(&r->report_queue, report, entry);

    r->new_query_needed = true;
}

static uint32_t zpass_result_scaled(PGRAPHState *pg, PGRAPHVkState *r)
{
    const uint32_t divisor =
        (uint32_t)(pg->surface_scale_factor * pg->surface_scale_factor);
    if (r->async_reports_enabled || r->batch_qreset_enabled) {
        return (uint32_t)(((uint64_t)r->zpass_pixel_count_result +
                           divisor / 2) /
                          divisor);
    }
    return r->zpass_pixel_count_result / divisor;
}

static bool submission_retired(PGRAPHVkState *r, uint32_t idx)
{
    if (idx == SURFACE_NO_WRITE) {
        return true;
    }
    struct CbFenceSlot *slot = &r->cb_fence_ring[idx % CB_FENCE_RING_SIZE];
    if (slot->submit_index != idx) {
        return true;
    }
    return vkGetFenceStatus(r->device, slot->fence) == VK_SUCCESS;
}

static void consume_marker_forced(PGRAPHState *pg, PGRAPHVkState *r)
{
    if (r->report_marker_pending) {
        assert(submission_retired(r, r->report_marker_submit));
        r->report_marker_pending = false;
        pg->report_marker_pending = false;
        nv2a_profile_inc_counter(NV2A_PROF_REPORT_FORCE_DRAIN);
    }
}

static void apply_reports_from_range(NV2AState *d, uint32_t src_base,
                                     uint32_t src_count)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    NV2A_VK_DGROUP_BEGIN("Processing queries");

    assert(!r->in_command_buffer);

    const uint32_t consumed = (uint32_t)r->num_queries_in_flight;

    g_autofree uint64_t *query_results = NULL;

    if (r->num_queries_in_flight > 0) {
        query_results = g_malloc0_n(r->num_queries_in_flight,
                                    sizeof(uint64_t));
        uint32_t read = MIN(src_count, (uint32_t)r->num_queries_in_flight);
        if (read > 0) {
            VkResult result;
            do {
                result = vkGetQueryPoolResults(
                    r->device, r->query_pool, src_base, read,
                    read * sizeof(uint64_t), query_results, sizeof(uint64_t),
                    VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
            } while (result == VK_NOT_READY);
        }
    }

    int num_results_counted = 0;

    QueryReport *report;
    while ((report = QSIMPLEQ_FIRST(&r->report_queue)) != NULL) {
        assert(report->query_count >= num_results_counted);
        assert(report->query_count <= r->num_queries_in_flight);

        while (num_results_counted < report->query_count) {
            r->zpass_pixel_count_result +=
                query_results[num_results_counted++];
        }

        if (report->clear) {
            NV2A_VK_DPRINTF("Cleared");
            r->zpass_pixel_count_result = 0;
        } else {
            pgraph_write_zpass_pixel_cnt_report(d, report->parameter,
                                                zpass_result_scaled(pg, r));
        }

        QSIMPLEQ_REMOVE_HEAD(&r->report_queue, entry);
        g_free(report);
    }

    while (num_results_counted < r->num_queries_in_flight) {
        r->zpass_pixel_count_result += query_results[num_results_counted++];
    }

    r->num_queries_in_flight = 0;
    r->queries_submitted = 0;
    r->query_epoch_base = r->batch_qreset_enabled ?
                              r->query_epoch_base + consumed :
                              r->query_region_origin;

    consume_marker_forced(pg, r);
    NV2A_VK_DGROUP_END();
}

void pgraph_vk_process_pending_reports_internal(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    apply_reports_from_range(d, r->query_epoch_base,
                             (uint32_t)r->num_queries_in_flight);
}

static void apply_reports_prefix(NV2AState *d, uint32_t take)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    NV2A_VK_DGROUP_BEGIN("Prefix-applying %u query slots", take);

    assert(r->async_reports_enabled);
    assert(take <= (uint32_t)r->num_queries_in_flight);
    assert(take <= r->queries_submitted);

    g_autofree uint64_t *query_results = NULL;
    if (take > 0) {
        query_results = g_malloc0_n(take, sizeof(uint64_t));
        VkResult result;
        do {
            result = vkGetQueryPoolResults(
                r->device, r->query_pool, r->query_epoch_base, take,
                take * sizeof(uint64_t), query_results, sizeof(uint64_t),
                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        } while (result == VK_NOT_READY);
    }

    uint32_t num_results_counted = 0;

    QueryReport *report;
    while ((report = QSIMPLEQ_FIRST(&r->report_queue)) != NULL &&
           (uint32_t)report->query_count <= take) {
        assert((uint32_t)report->query_count >= num_results_counted);

        while (num_results_counted < (uint32_t)report->query_count) {
            r->zpass_pixel_count_result +=
                query_results[num_results_counted++];
        }

        if (report->clear) {
            NV2A_VK_DPRINTF("Cleared");
            r->zpass_pixel_count_result = 0;
        } else {
            pgraph_write_zpass_pixel_cnt_report(d, report->parameter,
                                                zpass_result_scaled(pg, r));
        }

        QSIMPLEQ_REMOVE_HEAD(&r->report_queue, entry);
        g_free(report);
    }

    while (num_results_counted < take) {
        r->zpass_pixel_count_result += query_results[num_results_counted++];
    }

    QSIMPLEQ_FOREACH (report, &r->report_queue, entry) {
        assert((uint32_t)report->query_count > take);
        report->query_count -= take;
    }
    r->query_epoch_base += take;
    r->num_queries_in_flight -= (int)take;
    r->queries_submitted -= take;

    NV2A_VK_DGROUP_END();
}

void pgraph_vk_swap_query_region(PGRAPHVkState *r)
{
    assert(r->batch_qreset_enabled);
    assert(r->num_queries_in_flight == 0);
    assert(r->queries_submitted == 0);
    assert(!r->report_marker_pending);
    assert(QSIMPLEQ_EMPTY(&r->report_queue));
    assert(r->query_num_regions == 2);

    r->query_region_origin =
        (r->query_region_origin == 0) ? r->query_region_size : 0;
    r->query_epoch_base = r->query_region_origin;
    r->query_pool_reset_pending = true;
}

static void arm_report_marker(PGRAPHState *pg, PGRAPHVkState *r)
{
    assert(r->async_reports_enabled);
    assert(r->submit_count > 0);
    r->report_marker_watermark = r->queries_submitted;
    r->report_marker_submit = r->submit_count - 1;
    r->report_marker_pending = true;
    pg->report_marker_pending = true;
}

bool pgraph_vk_async_queries_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        enabled = (getenv("XEMU_ASYNC_QUERIES") != NULL &&
                   getenv("XEMU_NO_ASYNC_QUERIES") == NULL)
                      ? 1
                      : 0;
        fprintf(stderr, "async-queries: STALLED occlusion-query resolve %s\n",
                enabled ? "ON" : "OFF");
    }
    return enabled;
}

bool pgraph_vk_async_reports_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        enabled = (getenv("XEMU_ASYNC_REPORTS") != NULL &&
                   getenv("XEMU_NO_ASYNC_REPORTS") == NULL)
                      ? 1
                      : 0;
        fprintf(stderr,
                "async-reports: submit-then-poll STALLED report delivery %s\n",
                enabled ? "ON" : "OFF");
    }
    return enabled;
}

bool pgraph_vk_batch_qreset_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        enabled = (getenv("XEMU_BATCH_QRESET") != NULL &&
                   getenv("XEMU_NO_BATCH_QRESET") == NULL)
                      ? 1
                      : 0;
        fprintf(stderr,
                "batch-qreset: batched query reset + in-pass queries %s\n",
                enabled ? "ON" : "OFF");
    }
    return enabled;
}

static void advance_query_epoch(PGRAPHVkState *r)
{
    r->query_epoch_seq++;
    r->query_epoch_base =
        (r->query_epoch_seq % r->query_num_regions) * r->query_region_size;
    r->query_region_origin = r->query_epoch_base;
}

static void serve_reports_ring(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    uint32_t seq = r->query_epoch_seq;
    uint32_t base = r->query_epoch_base;
    uint32_t count = (uint32_t)r->num_queries_in_flight;

    QueryEpoch *src = NULL;
    if (seq >= r->query_ring_lag) {
        uint32_t src_seq = seq - r->query_ring_lag;
        QueryEpoch *cand = &r->query_epochs[src_seq % r->query_num_regions];
        if (cand->valid && cand->seq == src_seq) {
            src = cand;
        }
    }

    if (src == NULL) {
        pgraph_vk_finish(pg, VK_FINISH_REASON_STALLED);
        QueryEpoch *cur = &r->query_epochs[seq % r->query_num_regions];
        cur->seq = seq;
        cur->base = base;
        cur->count = count;
        cur->last_submit = r->submit_count - 1;
        cur->valid = true;
        advance_query_epoch(r);
        return;
    }

    pgraph_vk_submit_batch(pg, VK_FINISH_REASON_QUERY_ASYNC);
    QueryEpoch *cur = &r->query_epochs[seq % r->query_num_regions];
    cur->seq = seq;
    cur->base = base;
    cur->count = count;
    cur->last_submit = r->submit_count - 1;
    cur->valid = true;

    pgraph_vk_wait_for_submission(r, src->last_submit);

    unsigned int served = 0;
    QueryReport *rep;
    QSIMPLEQ_FOREACH (rep, &r->report_queue, entry) {
        if (!rep->clear) {
            served++;
        }
    }

    apply_reports_from_range(d, src->base, src->count);

    for (unsigned int i = 0; i < served; i++) {
        nv2a_profile_inc_counter(NV2A_PROF_QUERY_RING_SERVED);
    }

    advance_query_epoch(r);
}

#define REPORT_AGEOUT_US 300

void pgraph_vk_process_pending_reports(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    uint32_t *dma_get = &d->pfifo.regs[NV_PFIFO_CACHE1_DMA_GET];
    uint32_t *dma_put = &d->pfifo.regs[NV_PFIFO_CACHE1_DMA_PUT];

    if (r->async_reports_enabled) {
        if (pg->force_report_drain &&
            (r->report_marker_pending || !QSIMPLEQ_EMPTY(&r->report_queue))) {
            pgraph_vk_finish(pg, VK_FINISH_REASON_FLUSH);
            return;
        }

        if (r->report_marker_pending &&
            submission_retired(r, r->report_marker_submit)) {
            uint32_t take = r->report_marker_watermark;
            r->report_marker_pending = false;
            pg->report_marker_pending = false;
            apply_reports_prefix(d, take);
            nv2a_profile_inc_counter(NV2A_PROF_REPORT_ASYNC_RETIRE);
        }

        if (*dma_get == *dma_put && r->in_command_buffer &&
            !QSIMPLEQ_EMPTY(&r->report_queue)) {
            if (r->num_queries_in_flight == 0) {
                apply_reports_prefix(d, 0);
            } else {
                pgraph_vk_submit_batch(pg, VK_FINISH_REASON_QUERY_ASYNC);
                arm_report_marker(pg, r);
            }
        } else if (!r->report_marker_pending &&
                   !QSIMPLEQ_EMPTY(&r->report_queue)) {
            QueryReport *head = QSIMPLEQ_FIRST(&r->report_queue);
            if (g_get_monotonic_time() - head->enqueue_us > REPORT_AGEOUT_US) {
                if ((uint32_t)head->query_count > r->queries_submitted) {
                    assert(r->in_command_buffer);
                    pgraph_vk_submit_batch(pg, VK_FINISH_REASON_QUERY_ASYNC);
                    arm_report_marker(pg, r);
                } else if (r->queries_submitted > 0) {
                    arm_report_marker(pg, r);
                } else {
                    apply_reports_prefix(d, 0);
                }
                nv2a_profile_inc_counter(NV2A_PROF_REPORT_AGEOUT_SUBMIT);
            }
        }
        return;
    }

    if (*dma_get == *dma_put && r->in_command_buffer &&
        !QSIMPLEQ_EMPTY(&r->report_queue)) {
        if (r->query_ring_enabled) {
            serve_reports_ring(d);
        } else if (r->async_queries_enabled) {
            pgraph_vk_submit_batch(pg, VK_FINISH_REASON_QUERY_ASYNC);
            pgraph_vk_wait_for_submission(r, r->submit_count - 1);
            pgraph_vk_process_pending_reports_internal(d);
        } else {
            pgraph_vk_finish(pg, VK_FINISH_REASON_STALLED);
        }
    }
}
