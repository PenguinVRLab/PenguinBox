/*
 * QTest testcase for the CMSDK APB timer device
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

#define TIMER_BASE 0x40000000

#define CTRL 0
#define VALUE 4
#define RELOAD 8
#define INTSTATUS 0xc

static void test_timer(void)
{
    g_assert_true(readl(TIMER_BASE + INTSTATUS) == 0);

    writel(TIMER_BASE + RELOAD, 1000);
    writel(TIMER_BASE + CTRL, 9);

    clock_step(40 * 500 + 1);
    g_assert_cmpuint(readl(TIMER_BASE + INTSTATUS), ==, 0);
    g_assert_cmpuint(readl(TIMER_BASE + VALUE), ==, 500);

    clock_step(40 * 500);
    g_assert_cmpuint(readl(TIMER_BASE + INTSTATUS), ==, 1);
    g_assert_cmpuint(readl(TIMER_BASE + VALUE), ==, 0);

    clock_step(40);
    g_assert_cmpuint(readl(TIMER_BASE + VALUE), ==, 1000);

    writel(TIMER_BASE + INTSTATUS, 0);
    g_assert_cmpuint(readl(TIMER_BASE + INTSTATUS), ==, 1);
    writel(TIMER_BASE + INTSTATUS, 1);
    g_assert_cmpuint(readl(TIMER_BASE + INTSTATUS), ==, 0);

    writel(TIMER_BASE + CTRL, 0);
}

int main(int argc, char **argv)
{
    int r;

    g_test_init(&argc, &argv, NULL);

    qtest_start("-machine mps2-an385");

    qtest_add_func("/cmsdk-apb-timer/timer", test_timer);

    r = g_test_run();

    qtest_end();

    return r;
}
