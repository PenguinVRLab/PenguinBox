/*
 * Hosted file support for semihosting syscalls.
 *
 * Copyright (c) 2005, 2007 CodeSourcery.
 * Copyright (c) 2019 Linaro
 * Copyright © 2020 by Keith Packard <keithp@keithp.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "gdbstub/syscalls.h"
#include "semihosting/semihost.h"
#include "semihosting/guestfd.h"

static GArray *guestfd_array;

void qemu_semihosting_guestfd_init(void)
{
    guestfd_array = g_array_new(FALSE, TRUE, sizeof(GuestFD));

    if (semihosting_arm_compatible()) {
        semihosting_arm_compatible_init();
        return;
    }

    guestfd_array = g_array_set_size(guestfd_array, 3);
#ifndef CONFIG_USER_ONLY
    if (!use_gdb_syscalls()) {
        GuestFD *gf = &g_array_index(guestfd_array, GuestFD, 0);
        gf[0].type = GuestFDConsole;
        gf[1].type = GuestFDConsole;
        gf[2].type = GuestFDConsole;
        return;
    }
#endif
    associate_guestfd(0, 0);
    associate_guestfd(1, 1);
    associate_guestfd(2, 2);
}

int alloc_guestfd(void)
{
    guint i;

    for (i = 1; i < guestfd_array->len; i++) {
        GuestFD *gf = &g_array_index(guestfd_array, GuestFD, i);

        if (gf->type == GuestFDUnused) {
            return i;
        }
    }

    g_array_set_size(guestfd_array, i + 1);
    return i;
}

static void do_dealloc_guestfd(GuestFD *gf)
{
    gf->type = GuestFDUnused;
}

static GuestFD *do_get_guestfd(int guestfd)
{
    if (guestfd < 0 || guestfd >= guestfd_array->len) {
        return NULL;
    }

    return &g_array_index(guestfd_array, GuestFD, guestfd);
}

GuestFD *get_guestfd(int guestfd)
{
    GuestFD *gf = do_get_guestfd(guestfd);

    if (!gf || gf->type == GuestFDUnused) {
        return NULL;
    }
    return gf;
}

void associate_guestfd(int guestfd, int hostfd)
{
    GuestFD *gf = do_get_guestfd(guestfd);

    assert(gf);
    gf->type = use_gdb_syscalls() ? GuestFDGDB : GuestFDHost;
    gf->hostfd = hostfd;
}

void staticfile_guestfd(int guestfd, const uint8_t *data, size_t len)
{
    GuestFD *gf = do_get_guestfd(guestfd);

    assert(gf);
    gf->type = GuestFDStatic;
    gf->staticfile.data = data;
    gf->staticfile.len = len;
    gf->staticfile.off = 0;
}

void dealloc_guestfd(int guestfd)
{
    GuestFD *gf = do_get_guestfd(guestfd);

    assert(gf);
    do_dealloc_guestfd(gf);
}
