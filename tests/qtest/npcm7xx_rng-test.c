/*
 * QTest testcase for the Nuvoton NPCM7xx Random Number Generator
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

#include <math.h>

#include "libqtest-single.h"
#include "qemu/bitops.h"
#include "qemu/cutils.h"

#define RNG_BASE_ADDR   0xf000b000

#define RNGCS   0x00
# define DVALID     BIT(1)
# define RNGE       BIT(0)
#define RNGD    0x04
#define RNGMODE 0x08
# define ROSEL_NORMAL   (2)

#define TEST_INPUT_BITS  (128)

static void dump_buf_if_failed(const uint8_t *buf, size_t size)
{
    if (g_test_failed()) {
        qemu_hexdump(stderr, "", buf, size);
    }
}

static void rng_writeb(unsigned int offset, uint8_t value)
{
    writeb(RNG_BASE_ADDR + offset, value);
}

static uint8_t rng_readb(unsigned int offset)
{
    return readb(RNG_BASE_ADDR + offset);
}

static void rng_reset(void)
{
    rng_writeb(RNGCS, 0);
    rng_writeb(RNGMODE, ROSEL_NORMAL);
}

static void rng_reset_enable(void)
{
    rng_reset();
    rng_writeb(RNGCS, RNGE);
}

static bool rng_wait_ready(void)
{
    int retries = 10;

    while (retries-- > 0) {
        if (rng_readb(RNGCS) & DVALID) {
            return true;
        }
    }

    return false;
}

static double calc_monobit_p(const uint8_t *buf, unsigned int len)
{
    unsigned int i;
    double s_obs;
    int sn = 0;

    for (i = 0; i < len; i++) {
        sn += 2 * ctpop8(buf[i]) - 8;
    }

    s_obs = abs(sn) / sqrt(len * BITS_PER_BYTE);

    return erfc(s_obs / sqrt(2));
}

static double calc_runs_p(const unsigned long *buf, unsigned int nr_bits)
{
    unsigned int j;
    unsigned int k;
    int nr_ones = 0;
    int vn_obs = 0;
    double pi;

    g_assert(nr_bits % BITS_PER_LONG == 0);

    for (j = 0; j < nr_bits / BITS_PER_LONG; j++) {
        nr_ones += __builtin_popcountl(buf[j]);
    }
    pi = (double)nr_ones / nr_bits;

    for (k = 0; k < nr_bits - 1; k++) {
        vn_obs += (test_bit(k, buf) ^ test_bit(k + 1, buf));
    }
    vn_obs += 1;

    return erfc(fabs(vn_obs - 2 * nr_bits * pi * (1.0 - pi))
                / (2 * sqrt(2 * nr_bits) * pi * (1.0 - pi)));
}

static void test_enable_disable(void)
{
    rng_reset();
    g_assert_cmphex(rng_readb(RNGCS), ==, 0);
    g_assert_cmphex(rng_readb(RNGD), ==, 0);

    rng_writeb(RNGCS, RNGE);
    g_assert_true(rng_wait_ready());
    g_assert_cmphex(rng_readb(RNGCS), ==, DVALID | RNGE);

    rng_writeb(RNGCS, 0);
    g_assert_cmphex(rng_readb(RNGCS), ==, 0);
    g_assert_cmphex(rng_readb(RNGD), ==, 0);
}

static void test_rosel(void)
{
    rng_reset_enable();
    g_assert_true(rng_wait_ready());
    rng_writeb(RNGMODE, 0);
    g_assert_false(rng_wait_ready());
    rng_writeb(RNGMODE, ROSEL_NORMAL);
    g_assert_true(rng_wait_ready());
    rng_writeb(RNGMODE, 0);
    g_assert_false(rng_wait_ready());
}

static void test_continuous_monobit(void)
{
    uint8_t buf[TEST_INPUT_BITS / BITS_PER_BYTE];
    unsigned int i;

    rng_reset_enable();
    for (i = 0; i < sizeof(buf); i++) {
        g_assert_true(rng_wait_ready());
        buf[i] = rng_readb(RNGD);
    }

    g_assert_cmpfloat(calc_monobit_p(buf, sizeof(buf)), >, 0.01);
    dump_buf_if_failed(buf, sizeof(buf));
}

static void test_continuous_runs(void)
{
    union {
        unsigned long l[TEST_INPUT_BITS / BITS_PER_LONG];
        uint8_t c[TEST_INPUT_BITS / BITS_PER_BYTE];
    } buf;
    unsigned int i;

    rng_reset_enable();
    for (i = 0; i < sizeof(buf); i++) {
        g_assert_true(rng_wait_ready());
        buf.c[i] = rng_readb(RNGD);
    }

    g_assert_cmpfloat(calc_runs_p(buf.l, sizeof(buf) * BITS_PER_BYTE), >, 0.01);
    dump_buf_if_failed(buf.c, sizeof(buf));
}

static void test_first_byte_monobit(void)
{
    uint8_t buf[TEST_INPUT_BITS / BITS_PER_BYTE];
    unsigned int i;

    rng_reset();
    for (i = 0; i < sizeof(buf); i++) {
        rng_writeb(RNGCS, RNGE);
        g_assert_true(rng_wait_ready());
        buf[i] = rng_readb(RNGD);
        rng_writeb(RNGCS, 0);
    }

    g_assert_cmpfloat(calc_monobit_p(buf, sizeof(buf)), >, 0.01);
    dump_buf_if_failed(buf, sizeof(buf));
}

static void test_first_byte_runs(void)
{
    union {
        unsigned long l[TEST_INPUT_BITS / BITS_PER_LONG];
        uint8_t c[TEST_INPUT_BITS / BITS_PER_BYTE];
    } buf;
    unsigned int i;

    rng_reset();
    for (i = 0; i < sizeof(buf); i++) {
        rng_writeb(RNGCS, RNGE);
        g_assert_true(rng_wait_ready());
        buf.c[i] = rng_readb(RNGD);
        rng_writeb(RNGCS, 0);
    }

    g_assert_cmpfloat(calc_runs_p(buf.l, sizeof(buf) * BITS_PER_BYTE), >, 0.01);
    dump_buf_if_failed(buf.c, sizeof(buf));
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();

    qtest_add_func("npcm7xx_rng/enable_disable", test_enable_disable);
    qtest_add_func("npcm7xx_rng/rosel", test_rosel);
    if (getenv("QEMU_TEST_FLAKY_RNG_TESTS")) {
        qtest_add_func("npcm7xx_rng/continuous/monobit", test_continuous_monobit);
        qtest_add_func("npcm7xx_rng/continuous/runs", test_continuous_runs);
        qtest_add_func("npcm7xx_rng/first_byte/monobit", test_first_byte_monobit);
        qtest_add_func("npcm7xx_rng/first_byte/runs", test_first_byte_runs);
    }

    qtest_start("-machine npcm750-evb");
    ret = g_test_run();
    qtest_end();

    return ret;
}
