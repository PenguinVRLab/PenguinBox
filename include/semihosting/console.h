/*
 * Semihosting Console
 *
 * Copyright (c) 2019 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SEMIHOST_CONSOLE_H
#define SEMIHOST_CONSOLE_H

int qemu_semihosting_console_read(CPUState *cs, void *buf, int len);

int qemu_semihosting_console_write(void *buf, int len);

void qemu_semihosting_console_block_until_ready(CPUState *cs);

bool qemu_semihosting_console_ready(void);

#endif
