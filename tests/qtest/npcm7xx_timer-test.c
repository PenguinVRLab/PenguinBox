/*
 * QTest testcase for the Nuvoton NPCM7xx Timer
 *
 * Copyright 2020 Google LLC
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
#include "qemu/timer.h"
#include "libqtest-single.h"

#define TIM_REF_HZ      (25000000)

#define CEN             BIT(30)
#define IE              BIT(29)
#define MODE_ONESHOT    (0 << 27)
#define MODE_PERIODIC   (1 << 27)
#define CRST            BIT(26)
#define CACT            BIT(25)
#define PRESCALE(x)     (x)

#define TISR    0x18
#define WTCR    0x1c
# define WTCLK(x)       ((x) << 10)

#define TCSR_DEFAULT    PRESCALE(5)

typedef struct Timer {
    unsigned int tcsr_offset;
    unsigned int ticr_offset;
    unsigned int tdr_offset;
} Timer;

typedef struct TimerBlock {
    int irq_base;
    uint64_t base_addr;
} TimerBlock;

typedef struct TestData {
    const TimerBlock *tim;
    const Timer *timer;
} TestData;

const TimerBlock timer_block[] = {
    {
        .irq_base   = 32,
        .base_addr  = 0xf0008000,
    },
    {
        .irq_base   = 37,
        .base_addr  = 0xf0009000,
    },
    {
        .irq_base   = 42,
        .base_addr  = 0xf000a000,
    },
};

const Timer timer[] = {
    {
        .tcsr_offset    = 0x00,
        .ticr_offset    = 0x08,
        .tdr_offset     = 0x10,
    }, {
        .tcsr_offset    = 0x04,
        .ticr_offset    = 0x0c,
        .tdr_offset     = 0x14,
    }, {
        .tcsr_offset    = 0x20,
        .ticr_offset    = 0x28,
        .tdr_offset     = 0x30,
    }, {
        .tcsr_offset    = 0x24,
        .ticr_offset    = 0x2c,
        .tdr_offset     = 0x34,
    }, {
        .tcsr_offset    = 0x40,
        .ticr_offset    = 0x48,
        .tdr_offset     = 0x50,
    },
};

static int tim_index(const TimerBlock *tim)
{
    ptrdiff_t diff = tim - timer_block;

    g_assert(diff >= 0 && diff < ARRAY_SIZE(timer_block));

    return diff;
}

static int timer_index(const Timer *t)
{
    ptrdiff_t diff = t - timer;

    g_assert(diff >= 0 && diff < ARRAY_SIZE(timer));

    return diff;
}

static int tim_timer_irq(const TestData *td)
{
    return td->tim->irq_base + timer_index(td->timer);
}

static void tim_write(const TestData *td,
                      unsigned int offset, uint32_t value)
{
    writel(td->tim->base_addr + offset, value);
}

static uint32_t tim_read(const TestData *td, unsigned int offset)
{
    return readl(td->tim->base_addr + offset);
}

static void tim_write_tcsr(const TestData *td, uint32_t value)
{
    tim_write(td, td->timer->tcsr_offset, value);
}

static uint32_t tim_read_tcsr(const TestData *td)
{
    return tim_read(td, td->timer->tcsr_offset);
}

static void tim_write_ticr(const TestData *td, uint32_t value)
{
    tim_write(td, td->timer->ticr_offset, value);
}

static uint32_t tim_read_ticr(const TestData *td)
{
    return tim_read(td, td->timer->ticr_offset);
}

static uint32_t tim_read_tdr(const TestData *td)
{
    return tim_read(td, td->timer->tdr_offset);
}

static int64_t tim_calculate_step(uint32_t count, uint32_t prescale)
{
    return (1000000000LL / TIM_REF_HZ) * count * (prescale + 1);
}

static uint32_t tim_timer_bit(const TestData *td)
{
    return BIT(timer_index(td->timer));
}

static void tim_reset(const TestData *td)
{
    int i, j;

    for (i = 0; i < ARRAY_SIZE(timer_block); i++) {
        for (j = 0; j < ARRAY_SIZE(timer); j++) {
            writel(timer_block[i].base_addr + timer[j].tcsr_offset,
                   CRST | TCSR_DEFAULT);
        }
        writel(timer_block[i].base_addr + TISR, -1);
    }
}

static void test_reset(gconstpointer test_data)
{
    const TestData *td = test_data;

    tim_reset(td);

    g_assert_cmphex(tim_read_tcsr(td), ==, TCSR_DEFAULT);
    g_assert_cmphex(tim_read_ticr(td), ==, 0);
    g_assert_cmphex(tim_read_tdr(td), ==, 0);
    g_assert_cmphex(tim_read(td, TISR), ==, 0);
    g_assert_cmphex(tim_read(td, WTCR), ==, WTCLK(1));
}

static void test_reset_overrides_enable(gconstpointer test_data)
{
    const TestData *td = test_data;

    tim_reset(td);

    tim_write_tcsr(td, CEN | CRST | TCSR_DEFAULT);

    g_assert_cmphex(tim_read_tcsr(td), ==, TCSR_DEFAULT);
    g_assert_cmphex(tim_read_tdr(td), ==, 0);
    g_assert_cmphex(tim_read(td, TISR), ==, 0);
}

static void test_oneshot_enable_then_disable(gconstpointer test_data)
{
    const TestData *td = test_data;

    tim_reset(td);

    tim_write_tcsr(td, CEN | TCSR_DEFAULT);
    tim_write_tcsr(td, TCSR_DEFAULT);

    g_assert_cmphex(tim_read_tcsr(td), ==, TCSR_DEFAULT);
    g_assert_cmphex(tim_read_tdr(td), ==, 0);
    g_assert_cmphex(tim_read(td, TISR), ==, tim_timer_bit(td));
    g_assert_false(qtest_get_irq(global_qtest, tim_timer_irq(td)));
}

static void test_oneshot_ps5(gconstpointer test_data)
{
    const TestData *td = test_data;
    unsigned int count = 256;
    unsigned int ps = 5;

    tim_reset(td);

    tim_write_ticr(td, count);
    tim_write_tcsr(td, CEN | PRESCALE(ps));
    g_assert_cmphex(tim_read_tcsr(td), ==, CEN | CACT | PRESCALE(ps));
    g_assert_cmpuint(tim_read_tdr(td), ==, count);

    clock_step(tim_calculate_step(count, ps) - 1);

    g_assert_cmphex(tim_read_tcsr(td), ==, CEN | CACT | PRESCALE(ps));
    g_assert_cmpuint(tim_read_tdr(td), <, count);
    g_assert_cmphex(tim_read(td, TISR), ==, 0);

    clock_step(1);

    g_assert_cmphex(tim_read_tcsr(td), ==, PRESCALE(ps));
    g_assert_cmpuint(tim_read_tdr(td), ==, count);
    g_assert_cmphex(tim_read(td, TISR), ==, tim_timer_bit(td));
    g_assert_false(qtest_get_irq(global_qtest, tim_timer_irq(td)));

    tim_write(td, TISR, tim_timer_bit(td));
    g_assert_cmphex(tim_read(td, TISR), ==, 0);
    g_assert_false(qtest_get_irq(global_qtest, tim_timer_irq(td)));

    clock_step(2 * tim_calculate_step(count, ps));
    g_assert_cmphex(tim_read(td, TISR), ==, 0);
    g_assert_false(qtest_get_irq(global_qtest, tim_timer_irq(td)));
}

static void test_oneshot_ps0(gconstpointer test_data)
{
    const TestData *td = test_data;
    unsigned int count = 1;
    unsigned int ps = 0;

    tim_reset(td);

    tim_write_ticr(td, count);
    tim_write_tcsr(td, CEN | PRESCALE(ps));
    g_assert_cmphex(tim_read_tcsr(td), ==, CEN | CACT | PRESCALE(ps));
    g_assert_cmpuint(tim_read_tdr(td), ==, count);

    clock_step(tim_calculate_step(count, ps) - 1);

    g_assert_cmphex(tim_read_tcsr(td), ==, CEN | CACT | PRESCALE(ps));
    g_assert_cmpuint(tim_read_tdr(td), <, count);
    g_assert_cmphex(tim_read(td, TISR), ==, 0);

    clock_step(1);

    g_assert_cmphex(tim_read_tcsr(td), ==, PRESCALE(ps));
    g_assert_cmpuint(tim_read_tdr(td), ==, count);
    g_assert_cmphex(tim_read(td, TISR), ==, tim_timer_bit(td));
    g_assert_false(qtest_get_irq(global_qtest, tim_timer_irq(td)));
}

static void test_oneshot_ps255(gconstpointer test_data)
{
    const TestData *td = test_data;
    unsigned int count = (1U << 24) - 1;
    unsigned int ps = 255;

    tim_reset(td);

    tim_write_ticr(td, count);
    tim_write_tcsr(td, CEN | PRESCALE(ps));
    g_assert_cmphex(tim_read_tcsr(td), ==, CEN | CACT | PRESCALE(ps));
    g_assert_cmpuint(tim_read_tdr(td), ==, count);

    clock_step(tim_calculate_step(count, ps) - 1);

    g_assert_cmphex(tim_read_tcsr(td), ==, CEN | CACT | PRESCALE(ps));
    g_assert_cmpuint(tim_read_tdr(td), <, count);
    g_assert_cmphex(tim_read(td, TISR), ==, 0);

    clock_step(1);

    g_assert_cmphex(tim_read_tcsr(td), ==, PRESCALE(ps));
    g_assert_cmpuint(tim_read_tdr(td), ==, count);
    g_assert_cmphex(tim_read(td, TISR), ==, tim_timer_bit(td));
    g_assert_false(qtest_get_irq(global_qtest, tim_timer_irq(td)));
}

static void test_oneshot_interrupt(gconstpointer test_data)
{
    const TestData *td = test_data;
    unsigned int count = 256;
    unsigned int ps = 7;

    tim_reset(td);

    tim_write_ticr(td, count);
    tim_write_tcsr(td, IE | CEN | MODE_ONESHOT | PRESCALE(ps));

    clock_step_next();

    g_assert_cmphex(tim_read(td, TISR), ==, tim_timer_bit(td));
    g_assert_true(qtest_get_irq(global_qtest, tim_timer_irq(td)));
}

static void test_pause_resume(gconstpointer test_data)
{
    const TestData *td = test_data;
    unsigned int count = 256;
    unsigned int ps = 1;

    tim_reset(td);

    tim_write_ticr(td, count);
    tim_write_tcsr(td, IE | CEN | MODE_ONESHOT | PRESCALE(ps));

    clock_step(tim_calculate_step(count / 2, ps));
    tim_write_tcsr(td, IE | MODE_ONESHOT | PRESCALE(ps));
    g_assert_cmpuint(tim_read_tdr(td), ==, count / 2);

    clock_step(2 * tim_calculate_step(count, ps));
    g_assert_cmpuint(tim_read_tdr(td), ==, count / 2);
    g_assert_cmphex(tim_read(td, TISR), ==, 0);
    g_assert_false(qtest_get_irq(global_qtest, tim_timer_irq(td)));

    tim_write_tcsr(td, IE | CEN | MODE_ONESHOT | PRESCALE(ps));
    clock_step(tim_calculate_step(count / 2, ps) - 1);
    g_assert_cmpuint(tim_read_tdr(td), <, count);
    g_assert_cmphex(tim_read(td, TISR), ==, 0);
    g_assert_false(qtest_get_irq(global_qtest, tim_timer_irq(td)));

    clock_step(1);
    g_assert_cmphex(tim_read(td, TISR), ==, tim_timer_bit(td));
    g_assert_true(qtest_get_irq(global_qtest, tim_timer_irq(td)));
}

static void test_prescaler_change(gconstpointer test_data)
{
    const TestData *td = test_data;
    unsigned int count = 256;
    unsigned int ps = 5;

    tim_reset(td);

    tim_write_ticr(td, count);
    tim_write_tcsr(td, CEN | MODE_ONESHOT | PRESCALE(ps));

    clock_step(tim_calculate_step(count / 4, ps));
    g_assert_cmpuint(tim_read_tdr(td), ==, 3 * count / 4);
    ps = 2;
    tim_write_tcsr(td, CEN | MODE_ONESHOT | PRESCALE(ps));
    g_assert_cmpuint(tim_read_tdr(td), ==, 3 * count / 4);

    clock_step(tim_calculate_step(count / 4, ps));
    g_assert_cmpuint(tim_read_tdr(td), ==, count / 2);
    ps = 8;
    tim_write_tcsr(td, CEN | MODE_ONESHOT | PRESCALE(ps));
    g_assert_cmpuint(tim_read_tdr(td), ==, count / 2);

    clock_step(tim_calculate_step(count / 4, ps));
    g_assert_cmpuint(tim_read_tdr(td), ==, count / 4);
    ps = 0;
    tim_write_tcsr(td, CEN | MODE_ONESHOT | PRESCALE(ps));
    g_assert_cmpuint(tim_read_tdr(td), ==, count / 4);

    clock_step(tim_calculate_step(count / 4, ps) - 1);
    g_assert_cmpuint(tim_read_tdr(td), <, count);
    g_assert_cmphex(tim_read(td, TISR), ==, 0);

    clock_step(1);
    g_assert_cmphex(tim_read(td, TISR), ==, tim_timer_bit(td));
}

static void test_periodic_no_interrupt(gconstpointer test_data)
{
    const TestData *td = test_data;
    unsigned int count = 2;
    unsigned int ps = 3;
    int i;

    tim_reset(td);

    tim_write_ticr(td, count);
    tim_write_tcsr(td, CEN | MODE_PERIODIC | PRESCALE(ps));

    for (i = 0; i < 4; i++) {
        clock_step_next();

        g_assert_cmphex(tim_read(td, TISR), ==, tim_timer_bit(td));
        g_assert_false(qtest_get_irq(global_qtest, tim_timer_irq(td)));

        tim_write(td, TISR, tim_timer_bit(td));

        g_assert_cmphex(tim_read(td, TISR), ==, 0);
        g_assert_false(qtest_get_irq(global_qtest, tim_timer_irq(td)));
    }
}

static void test_periodic_interrupt(gconstpointer test_data)
{
    const TestData *td = test_data;
    unsigned int count = 65535;
    unsigned int ps = 2;
    int i;

    tim_reset(td);

    tim_write_ticr(td, count);
    tim_write_tcsr(td, CEN | IE | MODE_PERIODIC | PRESCALE(ps));

    for (i = 0; i < 4; i++) {
        clock_step_next();

        g_assert_cmphex(tim_read(td, TISR), ==, tim_timer_bit(td));
        g_assert_true(qtest_get_irq(global_qtest, tim_timer_irq(td)));

        tim_write(td, TISR, tim_timer_bit(td));

        g_assert_cmphex(tim_read(td, TISR), ==, 0);
        g_assert_false(qtest_get_irq(global_qtest, tim_timer_irq(td)));
    }
}

static void test_disable_on_expiration(gconstpointer test_data)
{
    const TestData *td = test_data;
    unsigned int count = 8;
    unsigned int ps = 255;

    tim_reset(td);

    tim_write_ticr(td, count);
    tim_write_tcsr(td, CEN | MODE_ONESHOT | PRESCALE(ps));

    clock_step(tim_calculate_step(count, ps) - 1);

    tim_write_tcsr(td, MODE_ONESHOT | PRESCALE(ps));
    tim_write_tcsr(td, CEN | MODE_ONESHOT | PRESCALE(ps));
    clock_step(1);
    tim_write_tcsr(td, MODE_ONESHOT | PRESCALE(ps));
    g_assert_cmphex(tim_read(td, TISR), ==, tim_timer_bit(td));
}

static void tim_add_test(const char *name, const TestData *td, GTestDataFunc fn)
{
    g_autofree char *full_name = g_strdup_printf(
        "npcm7xx_timer/tim[%d]/timer[%d]/%s", tim_index(td->tim),
        timer_index(td->timer), name);
    qtest_add_data_func(full_name, td, fn);
}

#define add_test(name, td) tim_add_test(#name, td, test_##name)

int main(int argc, char **argv)
{
    TestData testdata[ARRAY_SIZE(timer_block) * ARRAY_SIZE(timer)];
    int ret;
    int i, j;

    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();

    for (i = 0; i < ARRAY_SIZE(timer_block); i++) {
        for (j = 0; j < ARRAY_SIZE(timer); j++) {
            TestData *td = &testdata[i * ARRAY_SIZE(timer) + j];
            td->tim = &timer_block[i];
            td->timer = &timer[j];

            add_test(reset, td);
            add_test(reset_overrides_enable, td);
            add_test(oneshot_enable_then_disable, td);
            add_test(oneshot_ps5, td);
            add_test(oneshot_ps0, td);
            add_test(oneshot_ps255, td);
            add_test(oneshot_interrupt, td);
            add_test(pause_resume, td);
            add_test(prescaler_change, td);
            add_test(periodic_no_interrupt, td);
            add_test(periodic_interrupt, td);
            add_test(disable_on_expiration, td);
        }
    }

    qtest_start("-machine npcm750-evb");
    qtest_irq_intercept_in(global_qtest, "/machine/soc/a9mpcore/gic");
    ret = g_test_run();
    qtest_end();

    return ret;
}
