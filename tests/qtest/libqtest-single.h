/*
 * QTest - wrappers for test with single QEMU instances
 *
 * Copyright IBM, Corp. 2012
 * Copyright Red Hat, Inc. 2012
 * Copyright SUSE LINUX Products GmbH 2013
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef LIBQTEST_SINGLE_H
#define LIBQTEST_SINGLE_H

#include "libqtest.h"

#ifndef _WIN32
QTestState *global_qtest __attribute__((common, weak));
#else
__declspec(selectany) QTestState *global_qtest;
#endif

static inline QTestState *qtest_start(const char *args)
{
    global_qtest = qtest_init(args);
    return global_qtest;
}

static inline void qtest_end(void)
{
    if (!global_qtest) {
        return;
    }
    qtest_quit(global_qtest);
    global_qtest = NULL;
}

G_GNUC_PRINTF(1, 2)
static inline QDict *qmp(const char *fmt, ...)
{
    va_list ap;
    QDict *response;

    va_start(ap, fmt);
    response = qtest_vqmp(global_qtest, fmt, ap);
    va_end(ap);
    return response;
}

static inline void qmp_eventwait(const char *event)
{
    return qtest_qmp_eventwait(global_qtest, event);
}

static inline bool get_irq(int num)
{
    return qtest_get_irq(global_qtest, num);
}

static inline void outb(uint16_t addr, uint8_t value)
{
    qtest_outb(global_qtest, addr, value);
}

static inline void outw(uint16_t addr, uint16_t value)
{
    qtest_outw(global_qtest, addr, value);
}

static inline void outl(uint16_t addr, uint32_t value)
{
    qtest_outl(global_qtest, addr, value);
}

static inline uint8_t inb(uint16_t addr)
{
    return qtest_inb(global_qtest, addr);
}

static inline uint16_t inw(uint16_t addr)
{
    return qtest_inw(global_qtest, addr);
}

static inline uint32_t inl(uint16_t addr)
{
    return qtest_inl(global_qtest, addr);
}

static inline void writeb(uint64_t addr, uint8_t value)
{
    qtest_writeb(global_qtest, addr, value);
}

static inline void writew(uint64_t addr, uint16_t value)
{
    qtest_writew(global_qtest, addr, value);
}

static inline void writel(uint64_t addr, uint32_t value)
{
    qtest_writel(global_qtest, addr, value);
}

static inline void writeq(uint64_t addr, uint64_t value)
{
    qtest_writeq(global_qtest, addr, value);
}

static inline uint8_t readb(uint64_t addr)
{
    return qtest_readb(global_qtest, addr);
}

static inline uint16_t readw(uint64_t addr)
{
    return qtest_readw(global_qtest, addr);
}

static inline uint32_t readl(uint64_t addr)
{
    return qtest_readl(global_qtest, addr);
}

static inline uint64_t readq(uint64_t addr)
{
    return qtest_readq(global_qtest, addr);
}

static inline void memread(uint64_t addr, void *data, size_t size)
{
    qtest_memread(global_qtest, addr, data, size);
}

static inline void memwrite(uint64_t addr, const void *data, size_t size)
{
    qtest_memwrite(global_qtest, addr, data, size);
}

static inline int64_t clock_step_next(void)
{
    return qtest_clock_step_next(global_qtest);
}

static inline int64_t clock_step(int64_t step)
{
    return qtest_clock_step(global_qtest, step);
}

#endif
