/*
 * QTest testcase for the CMSDK APB dualtimer device
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

#define TIMER_BASE 0x40002000

#define TIMER1LOAD 0
#define TIMER1VALUE 4
#define TIMER1CONTROL 8
#define TIMER1INTCLR 0xc
#define TIMER1RIS 0x10
#define TIMER1MIS 0x14
#define TIMER1BGLOAD 0x18

#define TIMER2LOAD 0x20
#define TIMER2VALUE 0x24
#define TIMER2CONTROL 0x28
#define TIMER2INTCLR 0x2c
#define TIMER2RIS 0x30
#define TIMER2MIS 0x34
#define TIMER2BGLOAD 0x38

#define CTRL_ENABLE (1 << 7)
#define CTRL_PERIODIC (1 << 6)
#define CTRL_INTEN (1 << 5)
#define CTRL_PRESCALE_1 (0 << 2)
#define CTRL_PRESCALE_16 (1 << 2)
#define CTRL_PRESCALE_256 (2 << 2)
#define CTRL_32BIT (1 << 1)
#define CTRL_ONESHOT (1 << 0)

static void test_dualtimer(void)
{
    g_assert_true(readl(TIMER_BASE + TIMER1RIS) == 0);

    writel(TIMER_BASE + TIMER1LOAD, 1000);
    writel(TIMER_BASE + TIMER1CONTROL, CTRL_ENABLE | CTRL_INTEN);

    clock_step(500 * 40 + 1);
    g_assert_cmpuint(readl(TIMER_BASE + TIMER1RIS), ==, 0);
    g_assert_cmpuint(readl(TIMER_BASE + TIMER1VALUE), ==, 500);

    clock_step(500 * 40);
    g_assert_cmpuint(readl(TIMER_BASE + TIMER1RIS), ==, 1);
    g_assert_cmpuint(readl(TIMER_BASE + TIMER1VALUE), ==, 0);

    clock_step(40);
    g_assert_cmphex(readl(TIMER_BASE + TIMER1VALUE), ==, 0xffff);

    writel(TIMER_BASE + TIMER1INTCLR, 1);
    g_assert_cmpuint(readl(TIMER_BASE + TIMER1RIS), ==, 0);

    writel(TIMER_BASE + TIMER1CONTROL, 0);
}

static void test_prescale(void)
{
    g_assert_true(readl(TIMER_BASE + TIMER2RIS) == 0);

    writel(TIMER_BASE + TIMER2LOAD, 1000);
    writel(TIMER_BASE + TIMER2CONTROL,
           CTRL_ENABLE | CTRL_INTEN | CTRL_PERIODIC | CTRL_PRESCALE_256);

    clock_step(40 * 256 * 501);
    g_assert_cmpuint(readl(TIMER_BASE + TIMER2RIS), ==, 0);
    g_assert_cmpuint(readl(TIMER_BASE + TIMER2VALUE), ==, 500);

    clock_step(40 * 256 * 500);
    g_assert_cmpuint(readl(TIMER_BASE + TIMER2RIS), ==, 1);
    g_assert_cmpuint(readl(TIMER_BASE + TIMER2VALUE), ==, 0);

    clock_step(40 * 256);
    g_assert_cmpuint(readl(TIMER_BASE + TIMER2VALUE), ==, 1000);

    writel(TIMER_BASE + TIMER2INTCLR, 1);
    g_assert_cmpuint(readl(TIMER_BASE + TIMER2RIS), ==, 0);

    writel(TIMER_BASE + TIMER2CONTROL, 0);
}

int main(int argc, char **argv)
{
    int r;

    g_test_init(&argc, &argv, NULL);

    qtest_start("-machine mps2-an385");

    qtest_add_func("/cmsdk-apb-dualtimer/dualtimer", test_dualtimer);
    qtest_add_func("/cmsdk-apb-dualtimer/prescale", test_prescale);

    r = g_test_run();

    qtest_end();

    return r;
}
