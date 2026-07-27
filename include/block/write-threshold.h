/*
 * QEMU System Emulator block write threshold notification
 *
 * Copyright Red Hat, Inc. 2014
 *
 * Authors:
 *  Francesco Romani <fromani@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#ifndef BLOCK_WRITE_THRESHOLD_H
#define BLOCK_WRITE_THRESHOLD_H

void bdrv_write_threshold_set(BlockDriverState *bs, uint64_t threshold_bytes);

uint64_t bdrv_write_threshold_get(const BlockDriverState *bs);

void bdrv_write_threshold_check_write(BlockDriverState *bs, int64_t offset,
                                      int64_t bytes);

#endif
