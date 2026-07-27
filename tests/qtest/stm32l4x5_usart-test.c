/*
 * QTest testcase for STML4X5_USART
 *
 * Copyright (c) 2023 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "hw/misc/stm32l4x5_rcc_internals.h"
#include "hw/registerfields.h"
#include "stm32l4x5.h"

#define RCC_BASE_ADDR 0x40021000
#define USART1_BASE_ADDR 0x40013800

REG32(CR1, 0x00)
    FIELD(CR1, M1, 28, 1)
    FIELD(CR1, OVER8, 15, 1)
    FIELD(CR1, M0, 12, 1)
    FIELD(CR1, PCE, 10, 1)
    FIELD(CR1, TXEIE, 7, 1)
    FIELD(CR1, RXNEIE, 5, 1)
    FIELD(CR1, TE, 3, 1)
    FIELD(CR1, RE, 2, 1)
    FIELD(CR1, UE, 0, 1)
REG32(CR2, 0x04)
REG32(CR3, 0x08)
    FIELD(CR3, OVRDIS, 12, 1)
REG32(BRR, 0x0C)
REG32(GTPR, 0x10)
REG32(RTOR, 0x14)
REG32(RQR, 0x18)
REG32(ISR, 0x1C)
    FIELD(ISR, REACK, 22, 1)
    FIELD(ISR, TEACK, 21, 1)
    FIELD(ISR, TXE, 7, 1)
    FIELD(ISR, RXNE, 5, 1)
    FIELD(ISR, ORE, 3, 1)
REG32(ICR, 0x20)
REG32(RDR, 0x24)
REG32(TDR, 0x28)

#define NVIC_ISPR1 0XE000E204
#define NVIC_ICPR1 0xE000E284
#define USART1_IRQ 37

static bool check_nvic_pending(QTestState *qts, unsigned int n)
{
    assert(n > 32);
    n -= 32;
    return qtest_readl(qts, NVIC_ISPR1) & (1 << n);
}

static bool clear_nvic_pending(QTestState *qts, unsigned int n)
{
    assert(n > 32);
    n -= 32;
    qtest_writel(qts, NVIC_ICPR1, (1 << n));
    return true;
}

static bool usart_wait_for_flag(QTestState *qts, uint32_t event_addr,
                                uint32_t flag)
{
    while (true) {
        if ((qtest_readl(qts, event_addr) & flag)) {
            return true;
        }
        g_usleep(1000);
    }

    return false;
}

static void usart_receive_string(QTestState *qts, int sock_fd, const char *in,
                                 char *out)
{
    int i, in_len = strlen(in);

    g_assert_true(send(sock_fd, in, in_len, 0) == in_len);
    for (i = 0; i < in_len; i++) {
        g_assert_true(usart_wait_for_flag(qts,
            USART1_BASE_ADDR + A_ISR, R_ISR_RXNE_MASK));
        out[i] = qtest_readl(qts, USART1_BASE_ADDR + A_RDR);
    }
    out[i] = '\0';
}

static void usart_send_string(QTestState *qts, const char *in)
{
    int i, in_len = strlen(in);

    for (i = 0; i < in_len; i++) {
        qtest_writel(qts, USART1_BASE_ADDR + A_TDR, in[i]);
        g_assert_true(usart_wait_for_flag(qts,
            USART1_BASE_ADDR + A_ISR, R_ISR_TXE_MASK));
    }
}

static void init_clocks(QTestState *qts)
{
    uint32_t value;

    qtest_writel(qts, (RCC_BASE_ADDR + A_CR), R_CR_MSION_MASK);

    qtest_writel(qts, (RCC_BASE_ADDR + A_CFGR), 0);

    qtest_writel(qts, (RCC_BASE_ADDR + A_PLLCFGR), R_PLLCFGR_PLLREN_MASK |
            (40 << R_PLLCFGR_PLLN_SHIFT) |
            (0b01 << R_PLLCFGR_PLLSRC_SHIFT));

    value = qtest_readl(qts, (RCC_BASE_ADDR + A_CR));
    qtest_writel(qts, (RCC_BASE_ADDR + A_CR), value | R_CR_PLLON_MASK);

    qtest_writel(qts, (RCC_BASE_ADDR + A_CFGR), 0);

    qtest_writel(qts, (RCC_BASE_ADDR + A_CCIPR), 0);

    value = qtest_readl(qts, (RCC_BASE_ADDR + A_CFGR));
    qtest_writel(qts, (RCC_BASE_ADDR + A_CFGR), (value & ~R_CFGR_SW_MASK) |
        (0b11 << R_CFGR_SW_SHIFT));

    qtest_writel(qts, (RCC_BASE_ADDR + A_APB2ENR), R_APB2ENR_SYSCFGEN_MASK);

    qtest_writel(qts, (RCC_BASE_ADDR + A_AHB2ENR), R_AHB2ENR_GPIOBEN_MASK);

    qtest_writel(qts, (RCC_BASE_ADDR + A_APB2ENR),
                 R_APB2ENR_SYSCFGEN_MASK | R_APB2ENR_USART1EN_MASK);

    qtest_writel(qts, (RCC_BASE_ADDR + A_CCIPR), 0);

    qtest_writel(qts, (RCC_BASE_ADDR + A_APB2RSTR), 1 << 14);
    qtest_writel(qts, (RCC_BASE_ADDR + A_APB2RSTR), 0);
}

static void init_uart(QTestState *qts)
{
    uint32_t cr1;

    init_clocks(qts);

    qtest_writel(qts, (USART1_BASE_ADDR + A_BRR), 0x2B7);

    cr1 = qtest_readl(qts, (USART1_BASE_ADDR + A_CR1));
    cr1 &= ~(R_CR1_M1_MASK | R_CR1_M0_MASK | R_CR1_OVER8_MASK | R_CR1_PCE_MASK);
    qtest_writel(qts, (USART1_BASE_ADDR + A_CR1), cr1);

    qtest_writel(qts, (USART1_BASE_ADDR + A_CR1),
        cr1 | R_CR1_UE_MASK | R_CR1_RE_MASK | R_CR1_TE_MASK);
}

static void test_write_read(void)
{
    QTestState *qts = qtest_init("-M b-l475e-iot01a");

    qtest_writel(qts, USART1_BASE_ADDR + A_TDR, 0xFFFFFFFF);
    const uint32_t tdr = qtest_readl(qts, USART1_BASE_ADDR + A_TDR);
    g_assert_cmpuint(tdr, ==, 0x000001FF);

    qtest_quit(qts);
}

static void test_receive_char(void)
{
    int sock_fd;
    uint32_t cr1;
    QTestState *qts = qtest_init_with_serial("-M b-l475e-iot01a", &sock_fd);

    init_uart(qts);

    g_assert_true(send(sock_fd, "a", 1, 0) == 1);
    usart_wait_for_flag(qts, USART1_BASE_ADDR + A_ISR, R_ISR_RXNE_MASK);
    g_assert_cmphex(qtest_readl(qts, USART1_BASE_ADDR + A_RDR), ==, 'a');
    g_assert_false(check_nvic_pending(qts, USART1_IRQ));

    cr1 = qtest_readl(qts, (USART1_BASE_ADDR + A_CR1));
    cr1 |= R_CR1_RXNEIE_MASK;
    qtest_writel(qts, USART1_BASE_ADDR + A_CR1, cr1);
    g_assert_true(send(sock_fd, "b", 1, 0) == 1);
    usart_wait_for_flag(qts, USART1_BASE_ADDR + A_ISR, R_ISR_RXNE_MASK);
    g_assert_cmphex(qtest_readl(qts, USART1_BASE_ADDR + A_RDR), ==, 'b');
    g_assert_true(check_nvic_pending(qts, USART1_IRQ));
    clear_nvic_pending(qts, USART1_IRQ);

    close(sock_fd);

    qtest_quit(qts);
}

static void test_send_char(void)
{
    int sock_fd;
    char s[1];
    uint32_t cr1;
    QTestState *qts = qtest_init_with_serial("-M b-l475e-iot01a", &sock_fd);

    init_uart(qts);

    qtest_writel(qts, USART1_BASE_ADDR + A_TDR, 'c');
    g_assert_true(recv(sock_fd, s, 1, 0) == 1);
    g_assert_cmphex(s[0], ==, 'c');
    g_assert_false(check_nvic_pending(qts, USART1_IRQ));

    cr1 = qtest_readl(qts, (USART1_BASE_ADDR + A_CR1));
    cr1 |= R_CR1_TXEIE_MASK;
    qtest_writel(qts, USART1_BASE_ADDR + A_CR1, cr1);
    qtest_writel(qts, USART1_BASE_ADDR + A_TDR, 'd');
    g_assert_true(recv(sock_fd, s, 1, 0) == 1);
    g_assert_cmphex(s[0], ==, 'd');
    g_assert_true(check_nvic_pending(qts, USART1_IRQ));
    clear_nvic_pending(qts, USART1_IRQ);

    close(sock_fd);

    qtest_quit(qts);
}

static void test_receive_str(void)
{
    int sock_fd;
    char s[10];
    QTestState *qts = qtest_init_with_serial("-M b-l475e-iot01a", &sock_fd);

    init_uart(qts);

    usart_receive_string(qts, sock_fd, "hello", s);
    g_assert_true(memcmp(s, "hello", 5) == 0);

    close(sock_fd);

    qtest_quit(qts);
}

static void test_send_str(void)
{
    int sock_fd;
    char s[10];
    QTestState *qts = qtest_init_with_serial("-M b-l475e-iot01a", &sock_fd);

    init_uart(qts);

    usart_send_string(qts, "world");
    g_assert_true(recv(sock_fd, s, 10, 0) == 5);
    g_assert_true(memcmp(s, "world", 5) == 0);

    close(sock_fd);

    qtest_quit(qts);
}

static void test_ack(void)
{
    uint32_t cr1;
    uint32_t isr;
    QTestState *qts = qtest_init("-M b-l475e-iot01a");

    init_uart(qts);

    cr1 = qtest_readl(qts, (USART1_BASE_ADDR + A_CR1));

    qtest_writel(qts, (USART1_BASE_ADDR + A_CR1),
        cr1 & ~(R_CR1_RE_MASK | R_CR1_TE_MASK));

    isr = qtest_readl(qts, (USART1_BASE_ADDR + A_ISR));
    g_assert_false(isr & R_ISR_TEACK_MASK);
    g_assert_false(isr & R_ISR_REACK_MASK);

    qtest_writel(qts, (USART1_BASE_ADDR + A_CR1),
        cr1 | (R_CR1_RE_MASK | R_CR1_TE_MASK));

    isr = qtest_readl(qts, (USART1_BASE_ADDR + A_ISR));
    g_assert_true(isr & R_ISR_TEACK_MASK);
    g_assert_true(isr & R_ISR_REACK_MASK);

    qtest_quit(qts);
}

static void check_clock(QTestState *qts, const char *path, uint32_t rcc_reg,
                        uint32_t reg_offset)
{
    g_assert_cmpuint(get_clock_period(qts, path), ==, 0);
    qtest_writel(qts, rcc_reg, qtest_readl(qts, rcc_reg) | (0x1 << reg_offset));
    g_assert_cmpuint(get_clock_period(qts, path), ==, SYSCLK_PERIOD);
}

static void test_clock_enable(void)
{
    QTestState *qts = qtest_init("-M b-l475e-iot01a");

    check_clock(qts, "machine/soc/usart[0]/clk", RCC_APB2ENR, 14);
    check_clock(qts, "machine/soc/usart[1]/clk", RCC_APB1ENR1, 17);
    check_clock(qts, "machine/soc/usart[2]/clk", RCC_APB1ENR1, 18);
    check_clock(qts, "machine/soc/uart[0]/clk", RCC_APB1ENR1, 19);
    check_clock(qts, "machine/soc/uart[1]/clk", RCC_APB1ENR1, 20);
    check_clock(qts, "machine/soc/lpuart1/clk", RCC_APB1ENR2, 0);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();

    qtest_add_func("stm32l4x5/usart/write_read", test_write_read);
    qtest_add_func("stm32l4x5/usart/receive_char", test_receive_char);
    qtest_add_func("stm32l4x5/usart/send_char", test_send_char);
    qtest_add_func("stm32l4x5/usart/receive_str", test_receive_str);
    qtest_add_func("stm32l4x5/usart/send_str", test_send_str);
    qtest_add_func("stm32l4x5/usart/ack", test_ack);
    qtest_add_func("stm32l4x5/usart/clock_enable", test_clock_enable);
    return g_test_run();
}

