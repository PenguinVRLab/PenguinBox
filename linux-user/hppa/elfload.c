/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"
#include "target_elf.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
    return "hppa";
}

const char *get_elf_platform(CPUState *cs)
{
    return "PARISC";
}

bool init_guest_commpage(void)
{
    if (!reserved_va) {
        void *want, *addr;

        want = g2h_untagged(LO_COMMPAGE);
        addr = mmap(want, TARGET_PAGE_SIZE, PROT_NONE,
                    MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED_NOREPLACE, -1, 0);
        if (addr == MAP_FAILED) {
            perror("Allocating guest commpage");
            exit(EXIT_FAILURE);
        }
        if (addr != want) {
            return false;
        }
    }

    page_set_flags(LO_COMMPAGE, LO_COMMPAGE | ~TARGET_PAGE_MASK,
                   PAGE_EXEC | PAGE_VALID, PAGE_VALID);
    return true;
}
