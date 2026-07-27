/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
    static char buf[32];
    int err;

    if (eflags == 0x04 ||
        eflags == 0x05 ||
        eflags == 0x60 ||
        eflags == 0x61 ||
        eflags == 0x62 ||
        eflags == 0x65 ||
        eflags == 0x66 ||
        eflags == 0x67 ||
        eflags == 0x8067 ||
        eflags == 0x68 ||
        eflags == 0x69 ||
        eflags == 0x71 ||
        eflags == 0x8071 ||
        eflags == 0x73
       ) {
        return "v73";
    }

    err = snprintf(buf, sizeof(buf), "unknown (0x%x)", eflags);
    return err >= 0 && err < sizeof(buf) ? buf : "unknown";
}
