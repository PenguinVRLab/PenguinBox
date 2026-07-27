/*
 * Throttle infrastructure tests
 *
 * Copyright Nodalink, EURL. 2013-2014
 * Copyright Igalia, S.L. 2015
 *
 * Authors:
 *  Benoît Canet     <benoit.canet@nodalink.com>
 *  Alberto Garcia   <berto@igalia.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <math.h>
#include "block/aio.h"
#include "qapi/error.h"
#include "qemu/throttle.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "block/throttle-groups.h"
#include "system/block-backend.h"

static AioContext     *ctx;
static LeakyBucket    bkt;
static ThrottleConfig cfg;
static ThrottleGroupMember tgm;
static ThrottleState  ts;
static ThrottleTimers *tt;

static bool double_cmp(double x, double y)
{
    return fabsl(x - y) < 1e-6;
}

static void test_leak_bucket(void)
{
    throttle_config_init(&cfg);
    bkt = cfg.buckets[THROTTLE_BPS_TOTAL];

    bkt.avg = 150;
    bkt.max = 15;
    bkt.level = 1.5;

    throttle_leak_bucket(&bkt, NANOSECONDS_PER_SECOND / 150);
    g_assert(bkt.avg == 150);
    g_assert(bkt.max == 15);
    g_assert(double_cmp(bkt.level, 0.5));

    throttle_leak_bucket(&bkt, NANOSECONDS_PER_SECOND / 150);
    g_assert(bkt.avg == 150);
    g_assert(bkt.max == 15);
    g_assert(double_cmp(bkt.level, 0));

    throttle_leak_bucket(&bkt, NANOSECONDS_PER_SECOND / 150);
    g_assert(bkt.avg == 150);
    g_assert(bkt.max == 15);
    g_assert(double_cmp(bkt.level, 0));

    bkt.burst_level = 6;
    bkt.max = 250;
    bkt.burst_length = 2;
    throttle_leak_bucket(&bkt, NANOSECONDS_PER_SECOND / 100);
    g_assert(double_cmp(bkt.burst_level, 3.5));

    throttle_leak_bucket(&bkt, NANOSECONDS_PER_SECOND / 100);
    g_assert(double_cmp(bkt.burst_level, 1));

    throttle_leak_bucket(&bkt, NANOSECONDS_PER_SECOND / 100);
    g_assert(double_cmp(bkt.burst_level, 0));

    throttle_leak_bucket(&bkt, NANOSECONDS_PER_SECOND / 100);
    g_assert(double_cmp(bkt.burst_level, 0));
}

static void test_compute_wait(void)
{
    unsigned i;
    int64_t wait;
    int64_t result;

    throttle_config_init(&cfg);
    bkt = cfg.buckets[THROTTLE_BPS_TOTAL];

    bkt.avg = 0;
    bkt.max = 15;
    bkt.level = 1.5;
    wait = throttle_compute_wait(&bkt);
    g_assert(!wait);

    bkt.avg = 150;
    bkt.max = 15;
    bkt.level = 15;
    wait = throttle_compute_wait(&bkt);
    g_assert(!wait);

    bkt.avg = 150;
    bkt.max = 15;
    bkt.level = 9;
    wait = throttle_compute_wait(&bkt);
    g_assert(!wait);

    bkt.avg = 150;
    bkt.max = 15;
    bkt.level = 15.5;
    wait = throttle_compute_wait(&bkt);
    result = (int64_t)  NANOSECONDS_PER_SECOND / 150 / 2;
    g_assert(wait == result);

    bkt.burst_length = 2;
    bkt.level = 0;
    bkt.avg = 10;
    bkt.max = 200;
    for (i = 0; i < 22; i++) {
        double units = bkt.max / 10.0;
        bkt.level += units;
        bkt.burst_level += units;
        throttle_leak_bucket(&bkt, NANOSECONDS_PER_SECOND / 10);
        wait = throttle_compute_wait(&bkt);
        g_assert(double_cmp(bkt.burst_level, 0));
        g_assert(double_cmp(bkt.level, (i + 1) * (bkt.max - bkt.avg) / 10.0));
        result = i < 21 ? 0 : 1.8 * NANOSECONDS_PER_SECOND;
        g_assert(wait == result);
    }
}

static void read_timer_cb(void *opaque)
{
}

static void write_timer_cb(void *opaque)
{
}

static void test_init(void)
{
    int i;

    tt = &tgm.throttle_timers;

    memset(&ts, 1, sizeof(ts));
    memset(tt, 1, sizeof(*tt));

    throttle_init(&ts);
    throttle_timers_init(tt, ctx, QEMU_CLOCK_VIRTUAL,
                         read_timer_cb, write_timer_cb, &ts);

    g_assert(tt->clock_type == QEMU_CLOCK_VIRTUAL);
    g_assert(tt->timers[THROTTLE_READ]);
    g_assert(tt->timers[THROTTLE_WRITE]);

    g_assert(!ts.previous_leak);
    g_assert(!ts.cfg.op_size);
    for (i = 0; i < BUCKETS_COUNT; i++) {
        g_assert(!ts.cfg.buckets[i].avg);
        g_assert(!ts.cfg.buckets[i].max);
        g_assert(!ts.cfg.buckets[i].level);
    }

    throttle_timers_destroy(tt);
}

static void test_init_readonly(void)
{
    int i;

    tt = &tgm.throttle_timers;

    memset(&ts, 1, sizeof(ts));
    memset(tt, 1, sizeof(*tt));

    throttle_init(&ts);
    throttle_timers_init(tt, ctx, QEMU_CLOCK_VIRTUAL,
                         read_timer_cb, NULL, &ts);

    g_assert(tt->clock_type == QEMU_CLOCK_VIRTUAL);
    g_assert(tt->timers[THROTTLE_READ]);
    g_assert(!tt->timers[THROTTLE_WRITE]);

    g_assert(!ts.previous_leak);
    g_assert(!ts.cfg.op_size);
    for (i = 0; i < BUCKETS_COUNT; i++) {
        g_assert(!ts.cfg.buckets[i].avg);
        g_assert(!ts.cfg.buckets[i].max);
        g_assert(!ts.cfg.buckets[i].level);
    }

    throttle_timers_destroy(tt);
}

static void test_init_writeonly(void)
{
    int i;

    tt = &tgm.throttle_timers;

    memset(&ts, 1, sizeof(ts));
    memset(tt, 1, sizeof(*tt));

    throttle_init(&ts);
    throttle_timers_init(tt, ctx, QEMU_CLOCK_VIRTUAL,
                         NULL, write_timer_cb, &ts);

    g_assert(tt->clock_type == QEMU_CLOCK_VIRTUAL);
    g_assert(!tt->timers[THROTTLE_READ]);
    g_assert(tt->timers[THROTTLE_WRITE]);

    g_assert(!ts.previous_leak);
    g_assert(!ts.cfg.op_size);
    for (i = 0; i < BUCKETS_COUNT; i++) {
        g_assert(!ts.cfg.buckets[i].avg);
        g_assert(!ts.cfg.buckets[i].max);
        g_assert(!ts.cfg.buckets[i].level);
    }

    throttle_timers_destroy(tt);
}

static void test_destroy(void)
{
    int i;
    throttle_init(&ts);
    throttle_timers_init(tt, ctx, QEMU_CLOCK_VIRTUAL,
                         read_timer_cb, write_timer_cb, &ts);
    throttle_timers_destroy(tt);
    for (i = 0; i < THROTTLE_MAX; i++) {
        g_assert(!tt->timers[i]);
    }
}

static void test_config_functions(void)
{
    int i;
    ThrottleConfig orig_cfg, final_cfg;

    orig_cfg.buckets[THROTTLE_BPS_TOTAL].avg = 153;
    orig_cfg.buckets[THROTTLE_BPS_READ].avg  = 56;
    orig_cfg.buckets[THROTTLE_BPS_WRITE].avg = 1;

    orig_cfg.buckets[THROTTLE_OPS_TOTAL].avg = 150;
    orig_cfg.buckets[THROTTLE_OPS_READ].avg  = 69;
    orig_cfg.buckets[THROTTLE_OPS_WRITE].avg = 23;

    orig_cfg.buckets[THROTTLE_BPS_TOTAL].max = 0;
    orig_cfg.buckets[THROTTLE_BPS_READ].max  = 56;
    orig_cfg.buckets[THROTTLE_BPS_WRITE].max = 120;

    orig_cfg.buckets[THROTTLE_OPS_TOTAL].max = 150;
    orig_cfg.buckets[THROTTLE_OPS_READ].max  = 400;
    orig_cfg.buckets[THROTTLE_OPS_WRITE].max = 500;

    orig_cfg.buckets[THROTTLE_BPS_TOTAL].level = 45;
    orig_cfg.buckets[THROTTLE_BPS_READ].level  = 65;
    orig_cfg.buckets[THROTTLE_BPS_WRITE].level = 23;

    orig_cfg.buckets[THROTTLE_OPS_TOTAL].level = 1;
    orig_cfg.buckets[THROTTLE_OPS_READ].level  = 90;
    orig_cfg.buckets[THROTTLE_OPS_WRITE].level = 75;

    orig_cfg.op_size = 1;

    throttle_init(&ts);
    throttle_timers_init(tt, ctx, QEMU_CLOCK_VIRTUAL,
                         read_timer_cb, write_timer_cb, &ts);
    g_assert(!ts.previous_leak);
    throttle_config(&ts, QEMU_CLOCK_VIRTUAL, &orig_cfg);

    g_assert(ts.previous_leak);

    throttle_get_config(&ts, &final_cfg);

    throttle_timers_destroy(tt);

    g_assert(final_cfg.buckets[THROTTLE_BPS_TOTAL].avg == 153);
    g_assert(final_cfg.buckets[THROTTLE_BPS_READ].avg  == 56);
    g_assert(final_cfg.buckets[THROTTLE_BPS_WRITE].avg == 1);

    g_assert(final_cfg.buckets[THROTTLE_OPS_TOTAL].avg == 150);
    g_assert(final_cfg.buckets[THROTTLE_OPS_READ].avg  == 69);
    g_assert(final_cfg.buckets[THROTTLE_OPS_WRITE].avg == 23);

    g_assert(final_cfg.buckets[THROTTLE_BPS_TOTAL].max == 0);
    g_assert(final_cfg.buckets[THROTTLE_BPS_READ].max  == 56);
    g_assert(final_cfg.buckets[THROTTLE_BPS_WRITE].max == 120);

    g_assert(final_cfg.buckets[THROTTLE_OPS_TOTAL].max == 150);
    g_assert(final_cfg.buckets[THROTTLE_OPS_READ].max  == 400);
    g_assert(final_cfg.buckets[THROTTLE_OPS_WRITE].max == 500);

    g_assert(final_cfg.op_size == 1);

    for (i = 0; i < BUCKETS_COUNT; i++) {
        g_assert(!final_cfg.buckets[i].level);
    }
}

static void set_cfg_value(bool is_max, int index, int value)
{
    if (is_max) {
        cfg.buckets[index].max = value;
        cfg.buckets[index].avg = MAX(cfg.buckets[index].avg, 1);
    } else {
        cfg.buckets[index].avg = value;
    }
}

static void test_enabled(void)
{
    int i;

    throttle_config_init(&cfg);
    g_assert(!throttle_enabled(&cfg));

    for (i = 0; i < BUCKETS_COUNT; i++) {
        throttle_config_init(&cfg);
        set_cfg_value(false, i, 150);
        g_assert(throttle_is_valid(&cfg, NULL));
        g_assert(throttle_enabled(&cfg));
    }

    for (i = 0; i < BUCKETS_COUNT; i++) {
        throttle_config_init(&cfg);
        set_cfg_value(false, i, -150);
        g_assert(!throttle_is_valid(&cfg, NULL));
    }
}

static void test_conflicts_for_one_set(bool is_max,
                                       int total,
                                       int read,
                                       int write)
{
    throttle_config_init(&cfg);
    g_assert(throttle_is_valid(&cfg, NULL));

    set_cfg_value(is_max, total, 1);
    set_cfg_value(is_max, read,  1);
    g_assert(!throttle_is_valid(&cfg, NULL));

    throttle_config_init(&cfg);
    set_cfg_value(is_max, total, 1);
    set_cfg_value(is_max, write, 1);
    g_assert(!throttle_is_valid(&cfg, NULL));

    throttle_config_init(&cfg);
    set_cfg_value(is_max, total, 1);
    set_cfg_value(is_max, read,  1);
    set_cfg_value(is_max, write, 1);
    g_assert(!throttle_is_valid(&cfg, NULL));

    throttle_config_init(&cfg);
    set_cfg_value(is_max, total, 1);
    g_assert(throttle_is_valid(&cfg, NULL));

    throttle_config_init(&cfg);
    set_cfg_value(is_max, read,  1);
    set_cfg_value(is_max, write, 1);
    g_assert(throttle_is_valid(&cfg, NULL));
}

static void test_conflicting_config(void)
{
    test_conflicts_for_one_set(false,
                               THROTTLE_BPS_TOTAL,
                               THROTTLE_BPS_READ,
                               THROTTLE_BPS_WRITE);

    test_conflicts_for_one_set(false,
                               THROTTLE_OPS_TOTAL,
                               THROTTLE_OPS_READ,
                               THROTTLE_OPS_WRITE);

    test_conflicts_for_one_set(true,
                               THROTTLE_BPS_TOTAL,
                               THROTTLE_BPS_READ,
                               THROTTLE_BPS_WRITE);
    test_conflicts_for_one_set(true,
                               THROTTLE_OPS_TOTAL,
                               THROTTLE_OPS_READ,
                               THROTTLE_OPS_WRITE);
}
static void test_is_valid_for_value(int value, bool should_be_valid)
{
    int is_max, index;
    for (is_max = 0; is_max < 2; is_max++) {
        for (index = 0; index < BUCKETS_COUNT; index++) {
            throttle_config_init(&cfg);
            set_cfg_value(is_max, index, value);
            g_assert(throttle_is_valid(&cfg, NULL) == should_be_valid);
        }
    }
}

static void test_is_valid(void)
{
    test_is_valid_for_value(-1, false);
    test_is_valid_for_value(0, true);
    test_is_valid_for_value(1, true);
}

static void test_ranges(void)
{
    int i;

    for (i = 0; i < BUCKETS_COUNT; i++) {
        LeakyBucket *b = &cfg.buckets[i];
        throttle_config_init(&cfg);

        b->avg = 0;
        g_assert(throttle_is_valid(&cfg, NULL));
        g_assert(!throttle_enabled(&cfg));

        b->avg = 1;
        g_assert(throttle_is_valid(&cfg, NULL));

        b->avg = THROTTLE_VALUE_MAX;
        g_assert(throttle_is_valid(&cfg, NULL));

        b->avg = THROTTLE_VALUE_MAX;
        b->max = THROTTLE_VALUE_MAX;
        g_assert(throttle_is_valid(&cfg, NULL));

        b->avg = THROTTLE_VALUE_MAX + 1;
        g_assert(!throttle_is_valid(&cfg, NULL));

        b->avg = THROTTLE_VALUE_MAX;
        b->max = THROTTLE_VALUE_MAX + 1;
        g_assert(!throttle_is_valid(&cfg, NULL));

        b->avg = 1;
        b->max = 1;
        b->burst_length = 0;
        g_assert(!throttle_is_valid(&cfg, NULL));

        b->avg = 1;
        b->max = 1;
        b->burst_length = 1;
        g_assert(throttle_is_valid(&cfg, NULL));

        b->avg = 1;
        b->max = 1;
        b->burst_length = THROTTLE_VALUE_MAX;
        g_assert(throttle_is_valid(&cfg, NULL));

        b->avg = 1;
        b->max = 1;
        b->burst_length = THROTTLE_VALUE_MAX + 1;
        g_assert(!throttle_is_valid(&cfg, NULL));

        b->avg = 1;
        b->max = 2;
        b->burst_length = THROTTLE_VALUE_MAX / 2;
        g_assert(throttle_is_valid(&cfg, NULL));

        b->avg = 1;
        b->max = 3;
        b->burst_length = THROTTLE_VALUE_MAX / 2;
        g_assert(!throttle_is_valid(&cfg, NULL));

        b->avg = 1;
        b->max = THROTTLE_VALUE_MAX;
        b->burst_length = 1;
        g_assert(throttle_is_valid(&cfg, NULL));

        b->avg = 1;
        b->max = THROTTLE_VALUE_MAX;
        b->burst_length = 2;
        g_assert(!throttle_is_valid(&cfg, NULL));
    }
}

static void test_max_is_missing_limit(void)
{
    int i;

    for (i = 0; i < BUCKETS_COUNT; i++) {
        throttle_config_init(&cfg);
        cfg.buckets[i].max = 100;
        cfg.buckets[i].avg = 0;
        g_assert(!throttle_is_valid(&cfg, NULL));

        cfg.buckets[i].max = 0;
        cfg.buckets[i].avg = 0;
        g_assert(throttle_is_valid(&cfg, NULL));

        cfg.buckets[i].max = 0;
        cfg.buckets[i].avg = 100;
        g_assert(throttle_is_valid(&cfg, NULL));

        cfg.buckets[i].max = 30;
        cfg.buckets[i].avg = 100;
        g_assert(!throttle_is_valid(&cfg, NULL));

        cfg.buckets[i].max = 100;
        cfg.buckets[i].avg = 100;
        g_assert(throttle_is_valid(&cfg, NULL));
    }
}

static void test_iops_size_is_missing_limit(void)
{
    throttle_config_init(&cfg);
    cfg.op_size = 4096;
    g_assert(!throttle_is_valid(&cfg, NULL));
}

static void test_have_timer(void)
{
    memset(&ts, 0, sizeof(ts));
    memset(tt, 0, sizeof(*tt));

    g_assert(!throttle_timers_are_initialized(tt));

    throttle_init(&ts);
    throttle_timers_init(tt, ctx, QEMU_CLOCK_VIRTUAL,
                         read_timer_cb, write_timer_cb, &ts);

    g_assert(throttle_timers_are_initialized(tt));

    throttle_timers_destroy(tt);
}

static void test_detach_attach(void)
{
    memset(&ts, 0, sizeof(ts));
    memset(tt, 0, sizeof(*tt));

    throttle_init(&ts);
    throttle_timers_init(tt, ctx, QEMU_CLOCK_VIRTUAL,
                         read_timer_cb, write_timer_cb, &ts);

    g_assert(throttle_timers_are_initialized(tt));

    throttle_timers_detach_aio_context(tt);
    g_assert(!throttle_timers_are_initialized(tt));

    throttle_timers_attach_aio_context(tt, ctx);
    g_assert(throttle_timers_are_initialized(tt));

    throttle_timers_destroy(tt);
}

static bool do_test_accounting(bool is_ops,
                int size,
                double avg,
                uint64_t op_size,
                double total_result,
                double read_result,
                double write_result)
{
    BucketType to_test[2][3] = { { THROTTLE_BPS_TOTAL,
                                   THROTTLE_BPS_READ,
                                   THROTTLE_BPS_WRITE, },
                                 { THROTTLE_OPS_TOTAL,
                                   THROTTLE_OPS_READ,
                                   THROTTLE_OPS_WRITE, } };
    BucketType index;
    int i;

    throttle_config_init(&cfg);

    for (i = 0; i < 3; i++) {
        index = to_test[is_ops][i];
        cfg.buckets[index].avg = avg;
    }

    cfg.op_size = op_size;

    throttle_init(&ts);
    throttle_timers_init(tt, ctx, QEMU_CLOCK_VIRTUAL,
                         read_timer_cb, write_timer_cb, &ts);
    throttle_config(&ts, QEMU_CLOCK_VIRTUAL, &cfg);

    throttle_account(&ts, THROTTLE_READ, size);
    throttle_account(&ts, THROTTLE_WRITE, size);

    index = to_test[is_ops][0];
    if (!double_cmp(ts.cfg.buckets[index].level, total_result)) {
        return false;
    }

    index = to_test[is_ops][1];
    if (!double_cmp(ts.cfg.buckets[index].level, read_result)) {
        return false;
    }

    index = to_test[is_ops][2];
    if (!double_cmp(ts.cfg.buckets[index].level, write_result)) {
        return false;
    }

    throttle_timers_destroy(tt);

    return true;
}

static void test_accounting(void)
{

    g_assert(do_test_accounting(false,
                                1 * 512,
                                150,
                                0,
                                1024,
                                512,
                                512));

    g_assert(do_test_accounting(false,
                                2 * 512,
                                150,
                                0,
                                2048,
                                1024,
                                1024));

    g_assert(do_test_accounting(false,
                                2 * 512,
                                150,
                                17,
                                2048,
                                1024,
                                1024));


    g_assert(do_test_accounting(true,
                                1 * 512,
                                150,
                                0,
                                2,
                                1,
                                1));

    g_assert(do_test_accounting(true,
                                2 *  512,
                                150,
                                0,
                                2,
                                1,
                                1));

    g_assert(do_test_accounting(true,
                                64 * 512,
                                150,
                                13 * 512,
                                (64.0 * 2) / 13,
                                (64.0 / 13),
                                (64.0 / 13)));

    g_assert(do_test_accounting(true,
                                64 * 512,
                                300,
                                13 * 512,
                                (64.0 * 2) / 13,
                                (64.0 / 13),
                                (64.0 / 13)));
}

static void test_groups(void)
{
    ThrottleConfig cfg1, cfg2;
    BlockBackend *blk1, *blk2, *blk3;
    BlockBackendPublic *blkp1, *blkp2, *blkp3;
    ThrottleGroupMember *tgm1, *tgm2, *tgm3;

    blk1 = blk_new(qemu_get_aio_context(), 0, BLK_PERM_ALL);
    blk2 = blk_new(qemu_get_aio_context(), 0, BLK_PERM_ALL);
    blk3 = blk_new(qemu_get_aio_context(), 0, BLK_PERM_ALL);

    blkp1 = blk_get_public(blk1);
    blkp2 = blk_get_public(blk2);
    blkp3 = blk_get_public(blk3);

    tgm1 = &blkp1->throttle_group_member;
    tgm2 = &blkp2->throttle_group_member;
    tgm3 = &blkp3->throttle_group_member;

    g_assert(tgm1->throttle_state == NULL);
    g_assert(tgm2->throttle_state == NULL);
    g_assert(tgm3->throttle_state == NULL);

    throttle_group_register_tgm(tgm1, "bar", blk_get_aio_context(blk1));
    throttle_group_register_tgm(tgm2, "foo", blk_get_aio_context(blk2));
    throttle_group_register_tgm(tgm3, "bar", blk_get_aio_context(blk3));

    g_assert(tgm1->throttle_state != NULL);
    g_assert(tgm2->throttle_state != NULL);
    g_assert(tgm3->throttle_state != NULL);

    g_assert(!strcmp(throttle_group_get_name(tgm1), "bar"));
    g_assert(!strcmp(throttle_group_get_name(tgm2), "foo"));
    g_assert(tgm1->throttle_state == tgm3->throttle_state);

    throttle_config_init(&cfg1);
    cfg1.buckets[THROTTLE_BPS_READ].avg  = 500000;
    cfg1.buckets[THROTTLE_BPS_WRITE].avg = 285000;
    cfg1.buckets[THROTTLE_OPS_READ].avg  = 20000;
    cfg1.buckets[THROTTLE_OPS_WRITE].avg = 12000;
    throttle_group_config(tgm1, &cfg1);

    throttle_group_get_config(tgm1, &cfg1);
    throttle_group_get_config(tgm3, &cfg2);
    g_assert(!memcmp(&cfg1, &cfg2, sizeof(cfg1)));

    cfg2.buckets[THROTTLE_BPS_READ].avg  = 4547;
    cfg2.buckets[THROTTLE_BPS_WRITE].avg = 1349;
    cfg2.buckets[THROTTLE_OPS_READ].avg  = 123;
    cfg2.buckets[THROTTLE_OPS_WRITE].avg = 86;
    throttle_group_config(tgm3, &cfg1);

    throttle_group_get_config(tgm1, &cfg1);
    throttle_group_get_config(tgm3, &cfg2);
    g_assert(!memcmp(&cfg1, &cfg2, sizeof(cfg1)));

    throttle_group_unregister_tgm(tgm1);
    throttle_group_unregister_tgm(tgm2);
    throttle_group_unregister_tgm(tgm3);

    g_assert(tgm1->throttle_state == NULL);
    g_assert(tgm2->throttle_state == NULL);
    g_assert(tgm3->throttle_state == NULL);
}

int main(int argc, char **argv)
{
    qemu_init_main_loop(&error_fatal);
    ctx = qemu_get_aio_context();
    bdrv_init();
    module_call_init(MODULE_INIT_QOM);

    do {} while (g_main_context_iteration(NULL, false));

    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/throttle/leak_bucket",        test_leak_bucket);
    g_test_add_func("/throttle/compute_wait",       test_compute_wait);
    g_test_add_func("/throttle/init",               test_init);
    g_test_add_func("/throttle/init_readonly",      test_init_readonly);
    g_test_add_func("/throttle/init_writeonly",     test_init_writeonly);
    g_test_add_func("/throttle/destroy",            test_destroy);
    g_test_add_func("/throttle/have_timer",         test_have_timer);
    g_test_add_func("/throttle/detach_attach",      test_detach_attach);
    g_test_add_func("/throttle/config/enabled",     test_enabled);
    g_test_add_func("/throttle/config/conflicting", test_conflicting_config);
    g_test_add_func("/throttle/config/is_valid",    test_is_valid);
    g_test_add_func("/throttle/config/ranges",      test_ranges);
    g_test_add_func("/throttle/config/max",         test_max_is_missing_limit);
    g_test_add_func("/throttle/config/iops_size",
                    test_iops_size_is_missing_limit);
    g_test_add_func("/throttle/config_functions",   test_config_functions);
    g_test_add_func("/throttle/accounting",         test_accounting);
    g_test_add_func("/throttle/groups",             test_groups);
    return g_test_run();
}

