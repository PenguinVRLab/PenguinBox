/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdio.h>

#include "../../hw/xbox/nv2a/pgraph/vk/vr_ui_panel.h"

static int s_fail_count, s_check_failed;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "    %s:%d: CHECK(%s) failed\n", __FILE__,     \
                    __LINE__, #cond);                                      \
            s_check_failed = 1;                                            \
        }                                                                  \
    } while (0)
#define RUN(fn)                                                            \
    do {                                                                   \
        s_check_failed = 0;                                                \
        fn();                                                              \
        printf("%s %s\n", s_check_failed ? "FAIL" : "ok  ", #fn);          \
        s_fail_count += s_check_failed;                                    \
        n_run++;                                                           \
    } while (0)

static VrUiPauseTickIn due(void)
{
    return (VrUiPauseTickIn){
        .guest_running = false,
        .guest_stopped = true,
        .session_running = true,
        .panel_shown = true,
        .sync_runs = false,
        .tick_pending = false,
        .now_ms = 1000,
        .last_post_ms = -1,
    };
}

static void paused_without_surface_ticks(void)
{
    VrUiPauseTickIn in = due();
    CHECK(vr_ui_pause_tick_post(&in));
    CHECK(vr_ui_pause_tick_run(&in));
}

static void running_guest_never_ticks(void)
{
    VrUiPauseTickIn in = due();
    in.guest_running = true;
    in.guest_stopped = false;
    CHECK(!vr_ui_pause_tick_post(&in));
    CHECK(!vr_ui_pause_tick_run(&in));
    in.guest_stopped = true;
    CHECK(!vr_ui_pause_tick_post(&in));
    CHECK(!vr_ui_pause_tick_run(&in));
}

static void reset_while_paused_ticks(void)
{
    CHECK(vr_ui_pause_tick_settled_stop(false, false, true));
    CHECK(vr_ui_pause_tick_settled_stop(true, false, false));
    CHECK(vr_ui_pause_tick_settled_stop(false, true, false));
    CHECK(!vr_ui_pause_tick_settled_stop(false, false, false));
    VrUiPauseTickIn in = due();
    in.guest_stopped = vr_ui_pause_tick_settled_stop(false, false, true);
    CHECK(vr_ui_pause_tick_post(&in));
    CHECK(vr_ui_pause_tick_run(&in));
}

static void transitional_stop_never_ticks(void)
{
    VrUiPauseTickIn in = due();
    in.guest_stopped = false;
    CHECK(!vr_ui_pause_tick_post(&in));
    CHECK(!vr_ui_pause_tick_run(&in));
}

static void no_session_never_ticks(void)
{
    VrUiPauseTickIn in = due();
    in.session_running = false;
    CHECK(!vr_ui_pause_tick_post(&in));
    CHECK(!vr_ui_pause_tick_run(&in));
}

static void closed_panel_never_ticks(void)
{
    VrUiPauseTickIn in = due();
    in.panel_shown = false;
    CHECK(!vr_ui_pause_tick_post(&in));
    CHECK(!vr_ui_pause_tick_run(&in));
}

static void sync_covers_it(void)
{
    VrUiPauseTickIn in = due();
    in.sync_runs = true;
    CHECK(!vr_ui_pause_tick_post(&in));
    CHECK(!vr_ui_pause_tick_run(&in));
}

static void pending_tick_coalesces(void)
{
    VrUiPauseTickIn in = due();
    in.tick_pending = true;
    CHECK(!vr_ui_pause_tick_post(&in));
}

static void rate_is_bounded(void)
{
    VrUiPauseTickIn in = due();
    in.last_post_ms = in.now_ms - (VR_UI_PAUSE_TICK_MIN_MS - 1);
    CHECK(!vr_ui_pause_tick_post(&in));
    in.last_post_ms = in.now_ms - VR_UI_PAUSE_TICK_MIN_MS;
    CHECK(vr_ui_pause_tick_post(&in));
    in.last_post_ms = -1;
    CHECK(vr_ui_pause_tick_post(&in));
    in.now_ms = 0;
    CHECK(vr_ui_pause_tick_post(&in));
}

static void run_rechecks_after_post(void)
{
    VrUiPauseTickIn in = due();
    CHECK(vr_ui_pause_tick_post(&in));
    in.guest_running = true;
    in.guest_stopped = false;
    CHECK(!vr_ui_pause_tick_run(&in));
    in = due();
    in.session_running = false;
    CHECK(!vr_ui_pause_tick_run(&in));
    in = due();
    in.panel_shown = false;
    CHECK(!vr_ui_pause_tick_run(&in));
}

static void simulate(int loop_hz, int *posts_running, int *posts_paused)
{
    int64_t last_post = -1;
    bool pending = false;
    *posts_running = *posts_paused = 0;
    for (int i = 0; i < 2 * loop_hz; i++) {
        const int64_t now = (int64_t)i * 1000 / loop_hz;
        const bool paused = i >= loop_hz;
        VrUiPauseTickIn in = {
            .guest_running = !paused,
            .guest_stopped = paused,
            .session_running = true,
            .panel_shown = true,
            .sync_runs = !paused,
            .tick_pending = pending,
            .now_ms = now,
            .last_post_ms = last_post,
        };
        if (vr_ui_pause_tick_post(&in)) {
            pending = true;
            last_post = now;
            if (paused) {
                (*posts_paused)++;
            } else {
                (*posts_running)++;
            }
        }
        in.sync_runs = !paused;
        if (pending) {
            pending = false;
            (void)vr_ui_pause_tick_run(&in);
        }
    }
}

static void loop_vsync_60(void)
{
    int run, pause;
    simulate(60, &run, &pause);
    CHECK(run == 0);
    CHECK(pause == 60);
}

static void loop_uncapped_1000(void)
{
    int run, pause;
    simulate(1000, &run, &pause);
    CHECK(run == 0);
    CHECK(pause > 0);
    CHECK(pause <= 1000 / VR_UI_PAUSE_TICK_MIN_MS);
}

int main(void)
{
    int n_run = 0;
    RUN(paused_without_surface_ticks);
    RUN(running_guest_never_ticks);
    RUN(reset_while_paused_ticks);
    RUN(transitional_stop_never_ticks);
    RUN(no_session_never_ticks);
    RUN(closed_panel_never_ticks);
    RUN(sync_covers_it);
    RUN(pending_tick_coalesces);
    RUN(rate_is_bounded);
    RUN(run_rechecks_after_post);
    RUN(loop_vsync_60);
    RUN(loop_uncapped_1000);
    printf("%d/%d passed\n", n_run - s_fail_count, n_run);
    return s_fail_count ? 1 : 0;
}
