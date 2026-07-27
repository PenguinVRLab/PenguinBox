/*
 * Debug information support.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TCG_DEBUGINFO_H
#define TCG_DEBUGINFO_H

#include "qemu/bitops.h"

struct debuginfo_query {
    uint64_t address;
    int flags;
    const char *symbol;
    uint64_t offset;
    const char *file;
    int line;
};

#define DEBUGINFO_SYMBOL BIT(1)
#define DEBUGINFO_LINE   BIT(2)

#if defined(CONFIG_TCG) && defined(CONFIG_LIBDW)
void debuginfo_report_elf(const char *name, int fd, uint64_t bias);

void debuginfo_lock(void);

void debuginfo_query(struct debuginfo_query *q, size_t n);

void debuginfo_unlock(void);
#else
static inline void debuginfo_report_elf(const char *image_name, int image_fd,
                                        uint64_t load_bias)
{
}

static inline void debuginfo_lock(void)
{
}

static inline void debuginfo_query(struct debuginfo_query *q, size_t n)
{
}

static inline void debuginfo_unlock(void)
{
}
#endif

#endif
