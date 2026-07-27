/*
 * QEMU boot sector testing helpers.
 *
 * Copyright (c) 2016 Red Hat Inc.
 *
 * Authors:
 *  Michael S. Tsirkin <mst@redhat.com>
 *  Victor Kaplansky <victork@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TEST_BOOT_SECTOR_H
#define TEST_BOOT_SECTOR_H

#include "libqtest.h"

int boot_sector_init(char *fname);

void boot_sector_test(QTestState *qts);

void boot_sector_cleanup(const char *fname);

#endif
