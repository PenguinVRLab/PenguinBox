/*
 * Copyright (C) 2026 Patrick Carey
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef XEMU_DISC_XBE_H
#define XEMU_DISC_XBE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XEMU_XBE_INIT_LIMIT_DEVKIT_MEMORY 0x00000004u

typedef struct XemuDiscXbeInfo {
    uint64_t partition_base;
    uint64_t xbe_offset;
    uint32_t xbe_size;
    uint32_t cert_offset;
    uint32_t title_id;
    uint32_t init_flags;
    uint32_t game_region;
    uint32_t version;
    char title_name[121];
} XemuDiscXbeInfo;

typedef int (*XemuDiscReadFn)(void *opaque, uint64_t offset, void *buf,
                              size_t len);

int xemu_disc_read_xbe_info_fn(XemuDiscReadFn read_fn, void *opaque,
                               uint64_t image_size, XemuDiscXbeInfo *out,
                               char *err, size_t errlen);

int xemu_disc_read_xbe_info(const char *image_path, XemuDiscXbeInfo *out,
                            char *err, size_t errlen);

void xemu_disc_title_id_str(uint32_t title_id, char out[9]);

#ifdef __cplusplus
}
#endif

#endif
