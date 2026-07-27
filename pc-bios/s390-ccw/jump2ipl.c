/*
 * QEMU s390-ccw firmware - jump to IPL code
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include <string.h>
#include <stdio.h>
#include "s390-ccw.h"
#include "s390-arch.h"

#define KERN_IMAGE_START 0x010000UL
#define RESET_PSW_MASK (PSW_MASK_SHORTPSW | PSW_MASK_64)
#define RESET_PSW ((uint64_t)&jump_to_IPL_addr | RESET_PSW_MASK)

static uint64_t *reset_psw = 0, save_psw, ipl_continue;

void write_reset_psw(uint64_t psw)
{
    *reset_psw = psw;
}

static void jump_to_IPL_addr(void)
{
    __attribute__((noreturn)) void (*ipl)(void) = (void *)ipl_continue;

    write_reset_psw(save_psw);

    ipl();
}

int jump_to_IPL_code(uint64_t address)
{
    write_subsystem_identification();
    write_iplb_location();

    if (iplb.pbt == S390_IPL_TYPE_QEMU_SCSI) {
        iplb.devno = qipl.index;
    }

    if (have_iplb && !set_iplb(&iplb)) {
        panic("Failed to set IPLB");
    }

    if (address) {
        save_psw = *reset_psw;
        write_reset_psw(RESET_PSW);
        ipl_continue = address;
    }
    debug_print_int("set IPL addr to", address ?: *reset_psw & PSW_MASK_SHORT_ADDR);

    printf("\n");

    asm volatile("lghi %%r1,1\n\t"
                 "diag %%r1,%%r1,0x308\n\t"
                 : : : "1", "memory");
    puts("IPL code jump failed");
    return -1;
}

void jump_to_low_kernel(void)
{
    if (!memcmp((char *)S390EP, "S390EP", 6)) {
        jump_to_IPL_code(KERN_IMAGE_START);
    }

    if (*reset_psw & RESET_PSW_MASK) {
        jump_to_IPL_code(0);
    }

    jump_to_IPL_code(KERN_IMAGE_START);
}
