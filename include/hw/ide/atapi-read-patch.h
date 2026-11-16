/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_IDE_ATAPI_READ_PATCH_H
#define HW_IDE_ATAPI_READ_PATCH_H

#include <stdint.h>

extern void (*ide_atapi_read_patch)(int64_t lba, uint8_t *data, int nb_sectors);

#endif
