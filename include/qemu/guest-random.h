/*
 * QEMU guest-visible random functions
 *
 * Copyright 2019 Linaro, Ltd.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#ifndef QEMU_GUEST_RANDOM_H
#define QEMU_GUEST_RANDOM_H

int qemu_guest_random_seed_main(const char *seedstr, Error **errp);

uint64_t qemu_guest_random_seed_thread_part1(void);

void qemu_guest_random_seed_thread_part2(uint64_t seed);

int qemu_guest_getrandom(void *buf, size_t len, Error **errp);

void qemu_guest_getrandom_nofail(void *buf, size_t len);

#endif
