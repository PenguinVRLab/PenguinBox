/*
 * Linux Boot Option ROM for fw_cfg DMA
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (c) 2015-2016 Red Hat Inc.
 *   Authors:
 *     Marc Marí <marc.mari.barcelo@gmail.com>
 *     Richard W.M. Jones <rjones@redhat.com>
 */

asm(
".text\n"
".global _start\n"
"_start:\n"
"   .short 0xaa55\n"
"   .byte 3\n"
"   .byte 0xcb\n"
"   .org 0x18\n"
"   .short 0\n"
"   .short _pnph\n"
"_pnph:\n"
"   .ascii \"$PnP\"\n"
"   .byte 0x01\n"
"   .byte (_pnph_len / 16)\n"
"   .short 0x0000\n"
"   .byte 0x00\n"
"   .byte 0x00\n"
"   .long 0x00000000\n"
"   .short _manufacturer\n"
"   .short _product\n"
"   .long 0x00000000\n"
"   .short 0x0000\n"
"   .short 0x0000\n"
"   .short _bev\n"
"   .short 0x0000\n"
"   .short 0x0000\n"
"   .equ _pnph_len, . - _pnph\n"
"_manufacturer:\n"
"   .asciz \"QEMU\"\n"
"_product:\n"
"   .asciz \"Linux loader DMA\"\n"
"   .align 4, 0\n"
"_bev:\n"
"   cli\n"
"   cld\n"
"   jmp load_kernel\n"
);

#include <stdint.h>
#include "optrom.h"
#include "optrom_fw_cfg.h"

static inline void set_es(void *addr)
{
    uint32_t seg = (uint32_t)addr >> 4;
    asm("movl %0, %%es" : : "r"(seg));
}

static inline uint16_t readw_es(uint16_t offset)
{
    uint16_t val;
    asm(ADDR32 "movw %%es:(%1), %0" : "=r"(val) : "r"((uint32_t)offset));
    barrier();
    return val;
}

static inline uint32_t readl_es(uint16_t offset)
{
    uint32_t val;
    asm(ADDR32 "movl %%es:(%1), %0" : "=r"(val) : "r"((uint32_t)offset));
    barrier();
    return val;
}

static inline void writel_es(uint16_t offset, uint32_t val)
{
    barrier();
    asm(ADDR32 "movl %0, %%es:(%1)" : : "r"(val), "r"((uint32_t)offset));
}

static uint32_t get_e801_addr(void)
{
    uint16_t ax, bx, cx, dx;
    uint32_t ret;

    asm("int $0x15\n"
        : "=a"(ax), "=b"(bx), "=c"(cx), "=d"(dx)
        : "a"(0xe801), "b"(0), "c"(0), "d"(0));

    if (cx == 0 && dx == 0) {
        cx = ax;
        dx = bx;
    }

    if (dx) {
        ret = ((uint32_t)dx + 256 ) << 16;
    } else {
        ret = ((uint32_t)cx + 1024 ) << 10;
    }

    return ret;
}

extern void load_kernel(void) asm("load_kernel");

void load_kernel(void)
{
    void *setup_addr;
    void *initrd_addr;
    void *kernel_addr;
    void *cmdline_addr;
    uint32_t setup_size;
    uint32_t initrd_size;
    uint32_t kernel_size;
    uint32_t cmdline_size;
    uint32_t initrd_end_page, max_allowed_page;
    uint32_t segment_addr, stack_addr;

    bios_cfg_read_entry_dma(&setup_addr, FW_CFG_SETUP_ADDR, 4);
    bios_cfg_read_entry_dma(&setup_size, FW_CFG_SETUP_SIZE, 4);
    bios_cfg_read_entry_dma(setup_addr, FW_CFG_SETUP_DATA, setup_size);

    set_es(setup_addr);

    if (readw_es(0x206) < 0x203) {
        writel_es(0x22c, 0x37ffffff);
    }

    bios_cfg_read_entry_dma(&initrd_addr, FW_CFG_INITRD_ADDR, 4);
    bios_cfg_read_entry_dma(&initrd_size, FW_CFG_INITRD_SIZE, 4);

    initrd_end_page = ((uint32_t)(initrd_addr + initrd_size) & -4096);
    max_allowed_page = (readl_es(0x22c) & -4096);

    if (initrd_end_page != 0 && max_allowed_page != 0 &&
        initrd_end_page != max_allowed_page) {
        initrd_addr = (void *)((get_e801_addr() - initrd_size) & -4096);
        writel_es(0x218, (uint32_t)initrd_addr);

    }

    bios_cfg_read_entry_dma(initrd_addr, FW_CFG_INITRD_DATA, initrd_size);

    bios_cfg_read_entry_dma(&kernel_addr, FW_CFG_KERNEL_ADDR, 4);
    bios_cfg_read_entry_dma(&kernel_size, FW_CFG_KERNEL_SIZE, 4);
    bios_cfg_read_entry_dma(kernel_addr, FW_CFG_KERNEL_DATA, kernel_size);

    bios_cfg_read_entry_dma(&cmdline_addr, FW_CFG_CMDLINE_ADDR, 4);
    bios_cfg_read_entry_dma(&cmdline_size, FW_CFG_CMDLINE_SIZE, 4);
    bios_cfg_read_entry_dma(cmdline_addr, FW_CFG_CMDLINE_DATA, cmdline_size);

    segment_addr = ((uint32_t)setup_addr >> 4);
    stack_addr = (uint32_t)(cmdline_addr - setup_addr - 16);

    asm("movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        "movw %%ax, %%ss\n"
        "movl %%ebx, %%esp\n"
        "addw $0x20, %%ax\n"
        "pushw %%ax\n"
        "pushw $0\n"
        "xor %%ebx, %%ebx\n"
        "xor %%ecx, %%ecx\n"
        "xor %%edx, %%edx\n"
        "xor %%edi, %%edi\n"
        "xor %%ebp, %%ebp\n"
        "lretw\n"
        : : "a"(segment_addr), "b"(stack_addr));
}
