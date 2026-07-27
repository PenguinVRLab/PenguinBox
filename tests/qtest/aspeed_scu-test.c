/*
 * QTest testcase for the ASPEED AST2500 and AST2600 SCU.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2025 Tan Siewert
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"

#define AST_SCU_BASE                    0x1E6E2000
#define AST_SCU_PROT_LOCK_STATE         0x0
#define AST_SCU_PROT_LOCK_VALUE         0x2
#define AST_SCU_PROT_UNLOCK_STATE       0x1
#define AST_SCU_PROT_UNLOCK_VALUE       0x1688A8A8

#define AST2500_MACHINE                 "-machine ast2500-evb"
#define AST2500_SCU_PROT_REG            0x00
#define AST2500_SCU_MISC_2_CONTROL_REG  0x4C

#define AST2600_MACHINE                 "-machine ast2600-evb"
#define AST2600_SCU_PROT_REG            0x000
#define AST2600_SCU_PROT_REG2           0x010
#define AST2600_SCU_MISC_2_CONTROL_REG  0x0C4

#define TEST_LOCK_ARBITRARY_VALUE       0xABCDEFAB

static inline void assert_register_eq(QTestState *s,
                                      uint32_t reg,
                                      uint32_t expected)
{
    uint32_t value = qtest_readl(s, reg);
    g_assert_cmphex(value, ==, expected);
}

static inline void assert_register_neq(QTestState *s,
                                       uint32_t reg,
                                       uint32_t not_expected)
{
    uint32_t value = qtest_readl(s, reg);
    g_assert_cmphex(value, !=, not_expected);
}

static void test_protection_register(const char *machine,
                                     const uint32_t regs[],
                                     const int regc)
{
    QTestState *s = qtest_init(machine);

    for (int i = 0; i < regc; i++) {
        uint32_t reg = regs[i];

        qtest_writel(s, reg, AST_SCU_PROT_UNLOCK_VALUE);
        assert_register_eq(s, reg, AST_SCU_PROT_UNLOCK_STATE);

        if (regc > 1 && i == 0) {
            for (int j = 1; j < regc; j++) {
                uint32_t add_reg = regs[j];
                assert_register_eq(s, add_reg, AST_SCU_PROT_UNLOCK_STATE);
            }
        }

        qtest_writel(s, reg, AST_SCU_PROT_LOCK_VALUE);
        assert_register_eq(s, reg, AST_SCU_PROT_LOCK_STATE);

        if (regc > 1 && i == 0) {
            for (int j = 1; j < regc; j++) {
                uint32_t add_reg = regs[j];
                assert_register_eq(s, add_reg, AST_SCU_PROT_LOCK_STATE);
            }
        }
    }

    qtest_quit(s);
}

static void test_2500_protection_register(void)
{
    uint32_t regs[] = { AST_SCU_BASE + AST2500_SCU_PROT_REG };

    test_protection_register(AST2500_MACHINE,
                             regs,
                             ARRAY_SIZE(regs));
}

static void test_2600_protection_register(void)
{
    uint32_t regs[] = { AST_SCU_BASE + AST2600_SCU_PROT_REG,
                        AST_SCU_BASE + AST2600_SCU_PROT_REG2 };

    test_protection_register(AST2600_MACHINE,
                             regs,
                             ARRAY_SIZE(regs));
}

static void test_write_permission_lock_state(const char *machine,
                                             const uint32_t protection_register,
                                             const uint32_t test_register)
{
    QTestState *s = qtest_init(machine);

    qtest_writel(s, protection_register, AST_SCU_PROT_LOCK_VALUE);

    assert_register_eq(s, protection_register, AST_SCU_PROT_LOCK_STATE);

    qtest_writel(s, test_register, TEST_LOCK_ARBITRARY_VALUE);

    assert_register_neq(s, test_register, TEST_LOCK_ARBITRARY_VALUE);

    qtest_writel(s, protection_register, AST_SCU_PROT_UNLOCK_VALUE);
    assert_register_eq(s, protection_register, AST_SCU_PROT_UNLOCK_STATE);

    qtest_writel(s, test_register, TEST_LOCK_ARBITRARY_VALUE);

    assert_register_eq(s, test_register, TEST_LOCK_ARBITRARY_VALUE);

    qtest_quit(s);
}

static void test_2500_write_permission_lock_state(void)
{
    test_write_permission_lock_state(
            AST2500_MACHINE,
            AST_SCU_BASE + AST2500_SCU_PROT_REG,
            AST_SCU_BASE + AST2500_SCU_MISC_2_CONTROL_REG
    );
}

static void test_2600_write_permission_lock_state(void)
{
    test_write_permission_lock_state(
            AST2600_MACHINE,
            AST_SCU_BASE + AST2600_SCU_PROT_REG,
            AST_SCU_BASE + AST2600_SCU_MISC_2_CONTROL_REG
    );
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/ast2500/scu/protection_register",
                   test_2500_protection_register);
    qtest_add_func("/ast2600/scu/protection_register",
                   test_2600_protection_register);

    qtest_add_func("/ast2500/scu/write_permission_lock_state",
                   test_2500_write_permission_lock_state);
    qtest_add_func("/ast2600/scu/write_permission_lock_state",
                   test_2600_write_permission_lock_state);

    return g_test_run();
}
