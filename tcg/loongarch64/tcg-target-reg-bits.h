/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific register size
 * Copyright (c) 2021 WANG Xuerui <git@xen0n.name>
 */

#ifndef TCG_TARGET_REG_BITS_H
#define TCG_TARGET_REG_BITS_H

#if defined(__loongarch64)
# define TCG_TARGET_REG_BITS 64
#else
# error unsupported LoongArch register size
#endif

#endif
