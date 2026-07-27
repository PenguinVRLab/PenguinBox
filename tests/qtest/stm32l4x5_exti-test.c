/*
 * QTest testcase for STM32L4x5_EXTI
 *
 * Copyright (c) 2023 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"

#define EXTI_BASE_ADDR 0x40010400
#define EXTI_IMR1 0x00
#define EXTI_EMR1 0x04
#define EXTI_RTSR1 0x08
#define EXTI_FTSR1 0x0C
#define EXTI_SWIER1 0x10
#define EXTI_PR1 0x14
#define EXTI_IMR2 0x20
#define EXTI_EMR2 0x24
#define EXTI_RTSR2 0x28
#define EXTI_FTSR2 0x2C
#define EXTI_SWIER2 0x30
#define EXTI_PR2 0x34

#define NVIC_ISER 0xE000E100
#define NVIC_ISPR 0xE000E200
#define NVIC_ICPR 0xE000E280

#define EXTI0_IRQ 6
#define EXTI1_IRQ 7
#define EXTI5_9_IRQ 23
#define EXTI35_IRQ 1

static void enable_nvic_irq(unsigned int n)
{
    writel(NVIC_ISER, 1 << n);
}

static void unpend_nvic_irq(unsigned int n)
{
    writel(NVIC_ICPR, 1 << n);
}

static bool check_nvic_pending(unsigned int n)
{
    return readl(NVIC_ISPR) & (1 << n);
}

static void exti_writel(unsigned int offset, uint32_t value)
{
    writel(EXTI_BASE_ADDR + offset, value);
}

static uint32_t exti_readl(unsigned int offset)
{
    return readl(EXTI_BASE_ADDR + offset);
}

static void exti_set_irq(int num, int level)
{
   qtest_set_irq_in(global_qtest, "/machine/soc/exti", NULL,
                    num, level);
}

static void test_reg_write_read(void)
{

    exti_writel(EXTI_IMR1, 0xFFFFFFFF);
    g_assert_cmphex(exti_readl(EXTI_IMR1), ==, 0xFFFFFFFF);
    exti_writel(EXTI_IMR1, 0x00000000);
    g_assert_cmphex(exti_readl(EXTI_IMR1), ==, 0x00000000);

    exti_writel(EXTI_EMR1, 0xFFFFFFFF);
    g_assert_cmphex(exti_readl(EXTI_EMR1), ==, 0xFFFFFFFF);
    exti_writel(EXTI_EMR1, 0x00000000);
    g_assert_cmphex(exti_readl(EXTI_EMR1), ==, 0x00000000);

    exti_writel(EXTI_RTSR1, 0xFFFFFFFF);
    g_assert_cmphex(exti_readl(EXTI_RTSR1), ==, 0x007DFFFF);
    exti_writel(EXTI_RTSR1, 0x00000000);
    g_assert_cmphex(exti_readl(EXTI_RTSR1), ==, 0x00000000);

    exti_writel(EXTI_FTSR1, 0xFFFFFFFF);
    g_assert_cmphex(exti_readl(EXTI_FTSR1), ==, 0x007DFFFF);
    exti_writel(EXTI_FTSR1, 0x00000000);
    g_assert_cmphex(exti_readl(EXTI_FTSR1), ==, 0x00000000);

    exti_writel(EXTI_IMR2, 0xFFFFFFFF);
    g_assert_cmphex(exti_readl(EXTI_IMR2), ==, 0x000000FF);
    exti_writel(EXTI_IMR2, 0x00000000);
    g_assert_cmphex(exti_readl(EXTI_IMR2), ==, 0x00000000);

    exti_writel(EXTI_EMR2, 0xFFFFFFFF);
    g_assert_cmphex(exti_readl(EXTI_EMR2), ==, 0x000000FF);
    exti_writel(EXTI_EMR2, 0x00000000);
    g_assert_cmphex(exti_readl(EXTI_EMR2), ==, 0x00000000);

    exti_writel(EXTI_RTSR2, 0xFFFFFFFF);
    g_assert_cmphex(exti_readl(EXTI_RTSR2), ==, 0x00000078);
    exti_writel(EXTI_RTSR2, 0x00000000);
    g_assert_cmphex(exti_readl(EXTI_RTSR2), ==, 0x00000000);

    exti_writel(EXTI_FTSR2, 0xFFFFFFFF);
    g_assert_cmphex(exti_readl(EXTI_FTSR2), ==, 0x00000078);
    exti_writel(EXTI_FTSR2, 0x00000000);
    g_assert_cmphex(exti_readl(EXTI_FTSR2), ==, 0x00000000);
}

static void test_direct_lines_write(void)
{

    exti_writel(EXTI_RTSR1, 0xFF820000);
    g_assert_cmphex(exti_readl(EXTI_RTSR1), ==, 0x00000000);

    exti_writel(EXTI_FTSR1, 0xFF820000);
    g_assert_cmphex(exti_readl(EXTI_FTSR1), ==, 0x00000000);

    exti_writel(EXTI_SWIER1, 0xFF820000);
    g_assert_cmphex(exti_readl(EXTI_SWIER1), ==, 0x00000000);

    exti_writel(EXTI_PR1, 0xFF820000);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);

    exti_writel(EXTI_RTSR2, 0x00000087);
    g_assert_cmphex(exti_readl(EXTI_RTSR2), ==, 0x00000000);

    exti_writel(EXTI_FTSR2, 0x00000087);
    g_assert_cmphex(exti_readl(EXTI_FTSR2), ==, 0x00000000);

    exti_writel(EXTI_SWIER2, 0x00000087);
    g_assert_cmphex(exti_readl(EXTI_SWIER2), ==, 0x00000000);

    exti_writel(EXTI_PR2, 0x00000087);
    g_assert_cmphex(exti_readl(EXTI_PR2), ==, 0x00000000);
}

static void test_reserved_bits_write(void)
{

    exti_writel(EXTI_IMR2, 0xFFFFFF00);
    g_assert_cmphex(exti_readl(EXTI_IMR2), ==, 0x00000000);

    exti_writel(EXTI_EMR2, 0xFFFFFF00);
    g_assert_cmphex(exti_readl(EXTI_EMR2), ==, 0x00000000);

    exti_writel(EXTI_RTSR2, 0xFFFFFF00);
    g_assert_cmphex(exti_readl(EXTI_RTSR2), ==, 0x00000000);

    exti_writel(EXTI_FTSR2, 0xFFFFFF00);
    g_assert_cmphex(exti_readl(EXTI_FTSR2), ==, 0x00000000);

    exti_writel(EXTI_SWIER2, 0xFFFFFF00);
    g_assert_cmphex(exti_readl(EXTI_SWIER2), ==, 0x00000000);

    exti_writel(EXTI_PR2, 0xFFFFFF00);
    g_assert_cmphex(exti_readl(EXTI_PR2), ==, 0x00000000);
}

static void test_software_interrupt(void)
{

    enable_nvic_irq(EXTI0_IRQ);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    g_assert_false(check_nvic_pending(EXTI0_IRQ));

    exti_writel(EXTI_IMR1, 0x00000001);
    exti_writel(EXTI_SWIER1, 0x00000000);
    exti_writel(EXTI_SWIER1, 0x00000001);

    g_assert_cmphex(exti_readl(EXTI_SWIER1), ==, 0x00000001);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000001);
    g_assert_true(check_nvic_pending(EXTI0_IRQ));

    exti_writel(EXTI_PR1, 0x00000001);

    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    g_assert_cmphex(exti_readl(EXTI_SWIER1), ==, 0x00000000);
    g_assert_true(check_nvic_pending(EXTI0_IRQ));

    enable_nvic_irq(EXTI35_IRQ);
    g_assert_cmphex(exti_readl(EXTI_PR2), ==, 0x00000000);
    g_assert_false(check_nvic_pending(EXTI35_IRQ));

    exti_writel(EXTI_IMR2, 0x00000008);
    exti_writel(EXTI_SWIER2, 0x00000000);
    exti_writel(EXTI_SWIER2, 0x00000008);

    g_assert_cmphex(exti_readl(EXTI_SWIER2), ==, 0x00000008);
    g_assert_cmphex(exti_readl(EXTI_PR2), ==, 0x00000008);
    g_assert_true(check_nvic_pending(EXTI35_IRQ));

    exti_writel(EXTI_PR2, 0x00000008);

    g_assert_cmphex(exti_readl(EXTI_PR2), ==, 0x00000000);
    g_assert_cmphex(exti_readl(EXTI_SWIER2), ==, 0x00000000);
    g_assert_true(check_nvic_pending(EXTI35_IRQ));

    unpend_nvic_irq(EXTI0_IRQ);
    g_assert_false(check_nvic_pending(EXTI0_IRQ));
    unpend_nvic_irq(EXTI35_IRQ);
    g_assert_false(check_nvic_pending(EXTI35_IRQ));
}

static void test_edge_selector(void)
{
    enable_nvic_irq(EXTI0_IRQ);

    exti_set_irq(0, 1);
    exti_writel(EXTI_IMR1, 0x00000001);
    exti_writel(EXTI_RTSR1, 0x00000001);
    exti_writel(EXTI_FTSR1, 0x00000000);

    exti_set_irq(0, 0);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    g_assert_false(check_nvic_pending(EXTI0_IRQ));

    exti_set_irq(0, 1);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000001);
    g_assert_true(check_nvic_pending(EXTI0_IRQ));

    exti_writel(EXTI_PR1, 0x00000001);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    unpend_nvic_irq(EXTI0_IRQ);
    g_assert_false(check_nvic_pending(EXTI0_IRQ));

    exti_set_irq(0, 0);
    exti_writel(EXTI_IMR1, 0x00000001);
    exti_writel(EXTI_RTSR1, 0x00000000);
    exti_writel(EXTI_FTSR1, 0x00000001);

    exti_set_irq(0, 1);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    g_assert_false(check_nvic_pending(EXTI0_IRQ));

    exti_set_irq(0, 0);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000001);
    g_assert_true(check_nvic_pending(EXTI0_IRQ));

    exti_writel(EXTI_PR1, 0x00000001);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    unpend_nvic_irq(EXTI0_IRQ);
    g_assert_false(check_nvic_pending(EXTI0_IRQ));

    exti_writel(EXTI_IMR1, 0x00000001);
    exti_writel(EXTI_RTSR1, 0x00000001);
    exti_writel(EXTI_FTSR1, 0x00000001);

    exti_set_irq(0, 1);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000001);
    g_assert_true(check_nvic_pending(EXTI0_IRQ));

    exti_writel(EXTI_PR1, 0x00000001);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    unpend_nvic_irq(EXTI0_IRQ);
    g_assert_false(check_nvic_pending(EXTI0_IRQ));

    exti_set_irq(0, 0);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000001);
    g_assert_true(check_nvic_pending(EXTI0_IRQ));

    exti_writel(EXTI_PR1, 0x00000001);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    unpend_nvic_irq(EXTI0_IRQ);
    g_assert_false(check_nvic_pending(EXTI0_IRQ));

    exti_writel(EXTI_IMR1, 0x00000001);
    exti_writel(EXTI_RTSR1, 0x00000000);
    exti_writel(EXTI_FTSR1, 0x00000000);

    exti_set_irq(0, 1);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    g_assert_false(check_nvic_pending(EXTI0_IRQ));

    exti_set_irq(0, 0);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    g_assert_false(check_nvic_pending(EXTI0_IRQ));
}

static void test_no_software_interrupt(void)
{

    enable_nvic_irq(EXTI0_IRQ);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    g_assert_false(check_nvic_pending(EXTI0_IRQ));

    exti_writel(EXTI_IMR1, 0x00000000);
    exti_writel(EXTI_SWIER1, 0x00000000);
    exti_writel(EXTI_SWIER1, 0x00000001);

    g_assert_cmphex(exti_readl(EXTI_SWIER1), ==, 0x00000001);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    g_assert_false(check_nvic_pending(EXTI0_IRQ));

    exti_writel(EXTI_IMR1, 0x00000001);

    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    g_assert_false(check_nvic_pending(EXTI0_IRQ));

    enable_nvic_irq(EXTI35_IRQ);
    g_assert_cmphex(exti_readl(EXTI_PR2), ==, 0x00000000);
    g_assert_false(check_nvic_pending(EXTI35_IRQ));

    exti_writel(EXTI_IMR2, 0x00000000);
    exti_writel(EXTI_SWIER2, 0x00000000);
    exti_writel(EXTI_SWIER2, 0x00000008);

    g_assert_cmphex(exti_readl(EXTI_SWIER2), ==, 0x00000008);
    g_assert_cmphex(exti_readl(EXTI_PR2), ==, 0x00000000);
    g_assert_false(check_nvic_pending(EXTI35_IRQ));

    exti_writel(EXTI_IMR2, 0x00000008);

    g_assert_cmphex(exti_readl(EXTI_PR2), ==, 0x00000000);
    g_assert_false(check_nvic_pending(EXTI35_IRQ));
}

static void test_masked_interrupt(void)
{

    enable_nvic_irq(EXTI1_IRQ);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    g_assert_false(check_nvic_pending(EXTI1_IRQ));

    exti_writel(EXTI_IMR1, 0x00000000);

    exti_writel(EXTI_RTSR1, 0x00000002);

    exti_set_irq(1, 1);

    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    g_assert_false(check_nvic_pending(EXTI1_IRQ));

    exti_writel(EXTI_IMR1, 0x00000002);

    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    g_assert_false(check_nvic_pending(EXTI1_IRQ));

    exti_set_irq(1, 0);
}

static void test_interrupt(void)
{

    enable_nvic_irq(EXTI1_IRQ);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    g_assert_false(check_nvic_pending(EXTI1_IRQ));

    exti_writel(EXTI_IMR1, 0x00000002);

    exti_writel(EXTI_RTSR1, 0x00000002);

    exti_set_irq(1, 1);

    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000002);
    g_assert_true(check_nvic_pending(EXTI1_IRQ));

    exti_writel(EXTI_PR1, 0x00000002);

    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    g_assert_true(check_nvic_pending(EXTI1_IRQ));

    unpend_nvic_irq(EXTI1_IRQ);
    g_assert_false(check_nvic_pending(EXTI1_IRQ));

    exti_set_irq(1, 0);
}

static void test_orred_interrupts(void)
{
    enable_nvic_irq(EXTI5_9_IRQ);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    g_assert_false(check_nvic_pending(EXTI5_9_IRQ));

    exti_writel(EXTI_IMR1, (0x1F << 5));

    exti_writel(EXTI_RTSR1, (0x1F << 5));

    for (unsigned i = 5; i < 10; i++) {
        exti_set_irq(i, 1);
        g_assert_cmphex(exti_readl(EXTI_PR1), ==, 1 << i);
        g_assert_true(check_nvic_pending(EXTI5_9_IRQ));

        exti_writel(EXTI_PR1, 1 << i);
        g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
        g_assert_true(check_nvic_pending(EXTI5_9_IRQ));

        unpend_nvic_irq(EXTI5_9_IRQ);
        g_assert_false(check_nvic_pending(EXTI5_9_IRQ));

        exti_set_irq(i, 0);
    }
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();
    qtest_add_func("stm32l4x5/exti/direct_lines", test_direct_lines_write);
    qtest_add_func("stm32l4x5/exti/reserved_bits", test_reserved_bits_write);
    qtest_add_func("stm32l4x5/exti/reg_write_read", test_reg_write_read);
    qtest_add_func("stm32l4x5/exti/no_software_interrupt",
                   test_no_software_interrupt);
    qtest_add_func("stm32l4x5/exti/software_interrupt",
                   test_software_interrupt);
    qtest_add_func("stm32l4x5/exti/masked_interrupt", test_masked_interrupt);
    qtest_add_func("stm32l4x5/exti/interrupt", test_interrupt);
    qtest_add_func("stm32l4x5/exti/test_edge_selector", test_edge_selector);
    qtest_add_func("stm32l4x5/exti/test_orred_interrupts",
                   test_orred_interrupts);

    qtest_start("-machine b-l475e-iot01a");
    ret = g_test_run();
    qtest_end();

    return ret;
}
