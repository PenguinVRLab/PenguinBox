/*
 * tb-flush prototype for use by the rest of the system.
 *
 * Copyright (c) 2022 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef _TB_FLUSH_H_
#define _TB_FLUSH_H_

void tb_flush__exclusive_or_serial(void);

void queue_tb_flush(CPUState *cs);

void tcg_flush_jmp_cache(CPUState *cs);

#endif
