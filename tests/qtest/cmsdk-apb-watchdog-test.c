/*
 * QTest testcase for the CMSDK APB watchdog device
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
#include "exec/hwaddr.h"
#include "qemu/bitops.h"
#include "libqtest-single.h"

#define WDOG_BASE 0x40000000
#define WDOG_BASE_MPS2 0x40008000

#define WDOGLOAD 0
#define WDOGVALUE 4
#define WDOGCONTROL 8
#define WDOGINTCLR 0xc
#define WDOGRIS 0x10
#define WDOGMIS 0x14
#define WDOGLOCK 0xc00

#define SSYS_BASE 0x400fe000
#define RCC 0x60
#define SYSDIV_SHIFT 23
#define SYSDIV_LENGTH 4

#define WDOGLOAD_DEFAULT 0xFFFFFFFF
#define WDOGVALUE_DEFAULT 0xFFFFFFFF

typedef struct CMSDKAPBWatchdogTestArgs {
    int64_t tick;
    hwaddr wdog_base;
    const char *machine;
} CMSDKAPBWatchdogTestArgs;

enum {
    MACHINE_LM3S811EVB,
    MACHINE_MPS2_AN385,
};

static const CMSDKAPBWatchdogTestArgs machine_info[] = {
    [MACHINE_LM3S811EVB] = {
        .tick = 80,
        .wdog_base = WDOG_BASE,
        .machine = "lm3s811evb",
    },
    [MACHINE_MPS2_AN385] = {
        .tick = 40,
        .wdog_base = WDOG_BASE_MPS2,
        .machine = "mps2-an385",
    },
};

static void system_reset(QTestState *qtest)
{
    QDict *resp;

    resp = qtest_qmp(qtest, "{'execute': 'system_reset'}");
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);
    qtest_qmp_eventwait(qtest, "RESET");
}

static void test_watchdog(const void *ptr)
{
    const CMSDKAPBWatchdogTestArgs *args = ptr;
    hwaddr wdog_base = args->wdog_base;
    int64_t tick = args->tick;
    g_autofree gchar *cmdline = g_strdup_printf("-machine %s", args->machine);
    qtest_start(cmdline);

    g_assert_cmpuint(readl(wdog_base + WDOGRIS), ==, 0);

    writel(wdog_base + WDOGCONTROL, 1);
    writel(wdog_base + WDOGLOAD, 1000);

    clock_step(500 * tick + 1);
    g_assert_cmpuint(readl(wdog_base + WDOGRIS), ==, 0);
    g_assert_cmpuint(readl(wdog_base + WDOGVALUE), ==, 500);

    clock_step(500 * tick);
    g_assert_cmpuint(readl(wdog_base + WDOGRIS), ==, 1);
    g_assert_cmpuint(readl(wdog_base + WDOGVALUE), ==, 0);

    clock_step(tick);
    g_assert_cmpuint(readl(wdog_base + WDOGVALUE), ==, 1000);

    clock_step(500 * tick);
    g_assert_cmpuint(readl(wdog_base + WDOGVALUE), ==, 500);
    g_assert_cmpuint(readl(wdog_base + WDOGRIS), ==, 1);
    writel(wdog_base + WDOGINTCLR, 0);
    g_assert_cmpuint(readl(wdog_base + WDOGVALUE), ==, 1000);
    g_assert_cmpuint(readl(wdog_base + WDOGRIS), ==, 0);

    qtest_end();
}

static void test_clock_change(const void *ptr)
{
    uint32_t rcc;
    const CMSDKAPBWatchdogTestArgs *args = ptr;
    g_autofree gchar *cmdline = g_strdup_printf("-machine %s", args->machine);
    qtest_start(cmdline);

    g_assert_cmpuint(readl(WDOG_BASE + WDOGRIS), ==, 0);

    writel(WDOG_BASE + WDOGCONTROL, 1);
    writel(WDOG_BASE + WDOGLOAD, 1000);

    clock_step(80 * 500 + 1);
    g_assert_cmpuint(readl(WDOG_BASE + WDOGRIS), ==, 0);
    g_assert_cmpuint(readl(WDOG_BASE + WDOGVALUE), ==, 500);

    rcc = readl(SSYS_BASE + RCC);
    g_assert_cmphex(extract32(rcc, SYSDIV_SHIFT, SYSDIV_LENGTH), ==, 0xf);
    rcc = deposit32(rcc, SYSDIV_SHIFT, SYSDIV_LENGTH, 7);
    writel(SSYS_BASE + RCC, rcc);

    clock_step(40 * 500);
    g_assert_cmpuint(readl(WDOG_BASE + WDOGRIS), ==, 1);

    g_assert_cmpuint(readl(WDOG_BASE + WDOGVALUE), ==, 0);

    clock_step(41);
    g_assert_cmpuint(readl(WDOG_BASE + WDOGVALUE), ==, 1000);

    clock_step(40 * 500);
    g_assert_cmpuint(readl(WDOG_BASE + WDOGVALUE), ==, 500);
    g_assert_cmpuint(readl(WDOG_BASE + WDOGRIS), ==, 1);
    writel(WDOG_BASE + WDOGINTCLR, 0);
    g_assert_cmpuint(readl(WDOG_BASE + WDOGVALUE), ==, 1000);
    g_assert_cmpuint(readl(WDOG_BASE + WDOGRIS), ==, 0);

    qtest_end();
}

static void test_watchdog_reset(const void *ptr)
{
    const CMSDKAPBWatchdogTestArgs *args = ptr;
    hwaddr wdog_base = args->wdog_base;
    int64_t tick = args->tick;
    g_autofree gchar *cmdline = g_strdup_printf("-machine %s", args->machine);
    qtest_start(cmdline);
    g_assert_cmpuint(readl(wdog_base + WDOGRIS), ==, 0);

    g_assert_cmphex(readl(wdog_base + WDOGLOAD), ==, WDOGLOAD_DEFAULT);
    g_assert_cmphex(readl(wdog_base + WDOGVALUE), ==, WDOGVALUE_DEFAULT);

    g_assert_cmphex(readl(wdog_base + WDOGCONTROL), ==, 0);

    clock_step(15 * tick + 1);
    g_assert_cmphex(readl(wdog_base + WDOGLOAD), ==, WDOGLOAD_DEFAULT);
    g_assert_cmphex(readl(wdog_base + WDOGVALUE), ==, WDOGVALUE_DEFAULT);

    writel(wdog_base + WDOGLOAD, 3000);
    writel(wdog_base + WDOGCONTROL, 1);

    clock_step(1000 * tick + 1);
    g_assert_cmpuint(readl(wdog_base + WDOGLOAD), ==, 3000);
    g_assert_cmpuint(readl(wdog_base + WDOGVALUE), ==, 2000);

    system_reset(global_qtest);

    g_assert_cmphex(readl(wdog_base + WDOGLOAD), ==, WDOGLOAD_DEFAULT);
    g_assert_cmphex(readl(wdog_base + WDOGVALUE), ==, WDOGVALUE_DEFAULT);

    clock_step(1000 * tick + 1);
    g_assert_cmphex(readl(wdog_base + WDOGLOAD), ==, WDOGLOAD_DEFAULT);
    g_assert_cmphex(readl(wdog_base + WDOGVALUE), ==, WDOGVALUE_DEFAULT);

    qtest_end();
}

static void test_watchdog_inten(const void *ptr)
{
    const CMSDKAPBWatchdogTestArgs *args = ptr;
    hwaddr wdog_base = args->wdog_base;
    int64_t tick = args->tick;
    g_autofree gchar *cmdline = g_strdup_printf("-machine %s", args->machine);
    qtest_start(cmdline);
    g_assert_cmpuint(readl(wdog_base + WDOGRIS), ==, 0);

    g_assert_cmphex(readl(wdog_base + WDOGLOAD), ==, WDOGLOAD_DEFAULT);
    g_assert_cmphex(readl(wdog_base + WDOGVALUE), ==, WDOGVALUE_DEFAULT);

    writel(wdog_base + WDOGLOAD, 4000);
    g_assert_cmpuint(readl(wdog_base + WDOGLOAD), ==, 4000);
    g_assert_cmpuint(readl(wdog_base + WDOGVALUE), ==, 4000);
    clock_step(500 * tick + 1);
    g_assert_cmpuint(readl(wdog_base + WDOGLOAD), ==, 4000);
    g_assert_cmpuint(readl(wdog_base + WDOGVALUE), ==, 4000);

    writel(wdog_base + WDOGCONTROL, 1);
    clock_step(500 * tick + 1);
    g_assert_cmpuint(readl(wdog_base + WDOGLOAD), ==, 4000);
    g_assert_cmpuint(readl(wdog_base + WDOGVALUE), ==, 3500);

    writel(wdog_base + WDOGCONTROL, 0);
    clock_step(100 * tick);
    g_assert_cmpuint(readl(wdog_base + WDOGLOAD), ==, 4000);
    g_assert_cmpuint(readl(wdog_base + WDOGVALUE), ==, 3500);

    writel(wdog_base + WDOGCONTROL, 1);
    g_assert_cmpuint(readl(wdog_base + WDOGLOAD), ==, 4000);
    g_assert_cmpuint(readl(wdog_base + WDOGVALUE), ==, 4000);

    clock_step(50 * tick + 1);
    g_assert_cmpuint(readl(wdog_base + WDOGLOAD), ==, 4000);
    g_assert_cmpuint(readl(wdog_base + WDOGVALUE), ==, 3950);

    writel(wdog_base + WDOGLOAD, 5000);
    g_assert_cmpuint(readl(wdog_base + WDOGLOAD), ==, 5000);
    g_assert_cmpuint(readl(wdog_base + WDOGVALUE), ==, 5000);
    clock_step(4999 * tick + 1);
    g_assert_cmpuint(readl(wdog_base + WDOGLOAD), ==, 5000);
    g_assert_cmpuint(readl(wdog_base + WDOGVALUE), ==, 1);
    g_assert_cmpuint(readl(wdog_base + WDOGRIS), ==, 0);

    writel(wdog_base + WDOGCONTROL, 0);
    clock_step(10 * tick);
    g_assert_cmpuint(readl(wdog_base + WDOGLOAD), ==, 5000);
    g_assert_cmpuint(readl(wdog_base + WDOGVALUE), ==, 1);
    g_assert_cmpuint(readl(wdog_base + WDOGRIS), ==, 0);

    qtest_end();
}

static void test_watchdog_inten_luminary(const void *ptr)
{
    const CMSDKAPBWatchdogTestArgs *args = ptr;
    hwaddr wdog_base = args->wdog_base;
    int64_t tick = args->tick;
    g_autofree gchar *cmdline = g_strdup_printf("-machine %s", args->machine);
    qtest_start(cmdline);
    g_assert_cmpuint(readl(wdog_base + WDOGRIS), ==, 0);

    g_assert_cmphex(readl(wdog_base + WDOGLOAD), ==, WDOGLOAD_DEFAULT);
    g_assert_cmphex(readl(wdog_base + WDOGVALUE), ==, WDOGVALUE_DEFAULT);

    writel(wdog_base + WDOGLOAD, 4000);
    g_assert_cmpuint(readl(wdog_base + WDOGLOAD), ==, 4000);
    g_assert_cmpuint(readl(wdog_base + WDOGVALUE), ==, 4000);
    clock_step(500 * tick + 1);
    g_assert_cmpuint(readl(wdog_base + WDOGLOAD), ==, 4000);
    g_assert_cmpuint(readl(wdog_base + WDOGVALUE), ==, 4000);

    writel(wdog_base + WDOGCONTROL, 1);
    clock_step(500 * tick + 1);
    g_assert_cmpuint(readl(wdog_base + WDOGLOAD), ==, 4000);
    g_assert_cmpuint(readl(wdog_base + WDOGVALUE), ==, 3500);

    writel(wdog_base + WDOGCONTROL, 0);
    clock_step(100 * tick);
    g_assert_cmpuint(readl(wdog_base + WDOGLOAD), ==, 4000);
    g_assert_cmpuint(readl(wdog_base + WDOGVALUE), ==, 3400);
    g_assert_cmphex(readl(wdog_base + WDOGCONTROL), ==, 0x1);

    system_reset(global_qtest);

    g_assert_cmphex(readl(wdog_base + WDOGLOAD), ==, WDOGLOAD_DEFAULT);
    g_assert_cmphex(readl(wdog_base + WDOGVALUE), ==, WDOGVALUE_DEFAULT);
    g_assert_cmphex(readl(wdog_base + WDOGCONTROL), ==, 0);

    clock_step(1000 * tick + 1);
    g_assert_cmphex(readl(wdog_base + WDOGLOAD), ==, WDOGLOAD_DEFAULT);
    g_assert_cmphex(readl(wdog_base + WDOGVALUE), ==, WDOGVALUE_DEFAULT);

    qtest_end();
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();

    if (qtest_has_machine(machine_info[MACHINE_LM3S811EVB].machine)) {
        qtest_add_data_func("/cmsdk-apb-watchdog/watchdog",
                            &machine_info[MACHINE_LM3S811EVB], test_watchdog);
        qtest_add_data_func("/cmsdk-apb-watchdog/watchdog_clock_change",
                            &machine_info[MACHINE_LM3S811EVB],
                            test_clock_change);
        qtest_add_data_func("/cmsdk-apb-watchdog/watchdog_reset",
                            &machine_info[MACHINE_LM3S811EVB],
                            test_watchdog_reset);
        qtest_add_data_func("/cmsdk-apb-watchdog/watchdog_inten_luminary",
                            &machine_info[MACHINE_LM3S811EVB],
                            test_watchdog_inten_luminary);
    }
    if (qtest_has_machine(machine_info[MACHINE_MPS2_AN385].machine)) {
        qtest_add_data_func("/cmsdk-apb-watchdog/watchdog_mps2",
                            &machine_info[MACHINE_MPS2_AN385], test_watchdog);
        qtest_add_data_func("/cmsdk-apb-watchdog/watchdog_reset_mps2",
                            &machine_info[MACHINE_MPS2_AN385],
                            test_watchdog_reset);
        qtest_add_data_func("/cmsdk-apb-watchdog/watchdog_inten",
                            &machine_info[MACHINE_MPS2_AN385],
                            test_watchdog_inten);
    }

    return g_test_run();
}
