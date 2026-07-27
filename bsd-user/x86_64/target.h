/*
 * Intel general target stuff that's common to all x86_64 details
 *
 * Copyright (c) 2022 M. Warner Losh <imp@bsdimp.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TARGET_H
#define TARGET_H

static inline bool regpairs_aligned(void *cpu_env)
{
    return false;
}

#endif

