/*
 * QTest testcase for the SSE timer device
 *
 * Copyright (c) 2021 Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"

#define TIMER_BASE 0x48000000

#define PERIPHNSPPC0 (0x50080000 + 0x70)

#define COUNTER_BASE 0x58100000

#define CNTCR 0
#define CNTSR 0x4
#define CNTCV_LO 0x8
#define CNTCV_HI 0xc
#define CNTSCR 0x10

#define CNTPCT_LO 0
#define CNTPCT_HI 4
#define CNTFRQ 0x10
#define CNTP_CVAL_LO 0x20
#define CNTP_CVAL_HI 0x24
#define CNTP_TVAL 0x28
#define CNTP_CTL 0x2c
#define CNTP_AIVAL_LO 0x40
#define CNTP_AIVAL_HI 0x44
#define CNTP_AIVAL_RELOAD 0x48
#define CNTP_AIVAL_CTL 0x4c

#define FOUR_TICKS 125

static void clock_step_ticks(uint64_t ticks)
{
    assert(!(ticks & 3));
    clock_step(FOUR_TICKS * (ticks >> 2));
}

static void reset_counter_and_timer(void)
{
    writel(COUNTER_BASE + CNTCR, 0);
    writel(TIMER_BASE + CNTP_CTL, 0);
    writel(TIMER_BASE + CNTP_AIVAL_CTL, 0);
    writel(COUNTER_BASE + CNTCV_LO, 0);
    writel(COUNTER_BASE + CNTCV_HI, 0);
}

static void test_counter(void)
{

    reset_counter_and_timer();
    clock_step_ticks(100);
    g_assert_cmpuint(readl(COUNTER_BASE + CNTCV_LO), ==, 0);
    g_assert_cmpuint(readl(COUNTER_BASE + CNTCV_HI), ==, 0);
    writel(COUNTER_BASE + CNTCR, 1);
    clock_step_ticks(100);
    g_assert_cmpuint(readl(COUNTER_BASE + CNTCV_LO), ==, 100);
    g_assert_cmpuint(readl(COUNTER_BASE + CNTCV_HI), ==, 0);
    writel(COUNTER_BASE + CNTCR, 0);
    writel(COUNTER_BASE + CNTSCR, 0x00100000);
    writel(COUNTER_BASE + CNTCR, 5);
    clock_step_ticks(160);
    g_assert_cmpuint(readl(COUNTER_BASE + CNTCV_LO), ==, 110);
    g_assert_cmpuint(readl(COUNTER_BASE + CNTCV_HI), ==, 0);
}

static void test_timer(void)
{

    reset_counter_and_timer();
    writel(PERIPHNSPPC0, 1);
    writel(COUNTER_BASE + CNTCR, 1);

    g_assert_cmpuint(readl(TIMER_BASE + CNTP_CTL), ==, 0);
    g_assert_cmpuint(readl(TIMER_BASE + CNTPCT_LO), ==, 0);
    g_assert_cmpuint(readl(TIMER_BASE + CNTPCT_HI), ==, 0);

    writel(TIMER_BASE + CNTP_CTL, 1);

    clock_step_ticks(100);
    g_assert_cmpuint(readl(TIMER_BASE + CNTPCT_LO), ==, 100);
    g_assert_cmpuint(readl(TIMER_BASE + CNTPCT_HI), ==, 0);

    writel(TIMER_BASE + CNTP_CVAL_LO, 4000);
    writel(TIMER_BASE + CNTP_CVAL_HI, 0);

    g_assert_cmpuint(readl(TIMER_BASE + CNTP_TVAL), ==, 3900);

    clock_step_ticks(3900);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_TVAL), ==, 0);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_CTL), ==, 5);

    writel(TIMER_BASE + CNTP_AIVAL_RELOAD, 200);
    writel(TIMER_BASE + CNTP_AIVAL_CTL, 1);

    g_assert_cmpuint(readl(TIMER_BASE + CNTP_AIVAL_LO), ==, 4200);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_AIVAL_HI), ==, 0);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_CTL), ==, 1);

    clock_step_ticks(100);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_CTL), ==, 1);
    clock_step_ticks(100);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_CTL), ==, 5);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_AIVAL_LO), ==, 4400);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_AIVAL_HI), ==, 0);

    clock_step_ticks(100);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_CTL), ==, 5);
    writel(TIMER_BASE + CNTP_AIVAL_CTL, 1);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_CTL), ==, 1);
    clock_step_ticks(100);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_CTL), ==, 5);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_AIVAL_LO), ==, 4600);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_AIVAL_HI), ==, 0);

    writel(TIMER_BASE + CNTP_AIVAL_CTL, 0);
    clock_step_ticks(0x42ULL << 32);
    g_assert_cmpuint(readl(TIMER_BASE + CNTPCT_LO), ==, 4400);
    g_assert_cmphex(readl(TIMER_BASE + CNTPCT_HI), ==, 0x42);

    writel(TIMER_BASE + CNTP_AIVAL_CTL, 1);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_AIVAL_LO), ==, 4600);
    g_assert_cmphex(readl(TIMER_BASE + CNTP_AIVAL_HI), ==, 0x42);
}

static void test_timer_scale_change(void)
{
    reset_counter_and_timer();
    writel(PERIPHNSPPC0, 1);
    writel(COUNTER_BASE + CNTCR, 1);
    writel(TIMER_BASE + CNTP_CTL, 1);
    writel(TIMER_BASE + CNTP_CVAL_LO, 4000);
    writel(TIMER_BASE + CNTP_CVAL_HI, 0);
    clock_step_ticks(2000);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_CTL), ==, 1);
    writel(COUNTER_BASE + CNTCR, 0);
    writel(COUNTER_BASE + CNTSCR, 0x00100000);
    writel(COUNTER_BASE + CNTCR, 5);
    clock_step_ticks(2000);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_CTL), ==, 1);
    clock_step_ticks(29996);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_CTL), ==, 1);
    clock_step_ticks(4);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_CTL), ==, 5);
}

int main(int argc, char **argv)
{
    int r;

    g_test_init(&argc, &argv, NULL);

    qtest_start("-machine mps3-an547");

    qtest_add_func("/sse-timer/counter", test_counter);
    qtest_add_func("/sse-timer/timer", test_timer);
    qtest_add_func("/sse-timer/timer-scale-change", test_timer_scale_change);

    r = g_test_run();

    qtest_end();

    return r;
}
