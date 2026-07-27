/*
 * Simple interface for 128-bit atomic operations.
 *
 * Copyright (C) 2018 Linaro, Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * See docs/devel/atomics.rst for discussion about the guarantees each
 * atomic primitive is meant to provide.
 */

#ifndef QEMU_ATOMIC128_H
#define QEMU_ATOMIC128_H

#include "qemu/atomic.h"
#include "qemu/int128.h"

#if defined(CONFIG_ATOMIC128_OPT)
# if !defined(__OPTIMIZE__)
#  define ATTRIBUTE_ATOMIC128_OPT  __attribute__((optimize("O1")))
# endif
# define CONFIG_ATOMIC128
#endif
#ifndef ATTRIBUTE_ATOMIC128_OPT
# define ATTRIBUTE_ATOMIC128_OPT
#endif

#include "host/atomic128-cas.h.inc"
#include "host/atomic128-ldst.h.inc"

#endif
