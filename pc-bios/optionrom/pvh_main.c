/*
 * PVH Option ROM for fw_cfg DMA
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
 * Copyright (c) 2019 Red Hat Inc.
 *   Authors:
 *     Stefano Garzarella <sgarzare@redhat.com>
 */

asm (".code32");

#include <stddef.h>
#include <stdint.h>
#include "optrom.h"
#include "optrom_fw_cfg.h"
#include "../../include/hw/xen/start_info.h"

#define RSDP_SIGNATURE          0x2052545020445352LL
#define RSDP_AREA_ADDR          0x000E0000
#define RSDP_AREA_SIZE          0x00020000
#define EBDA_BASE_ADDR          0x0000040E
#define EBDA_SIZE               1024

#define E820_MAXENTRIES         128
#define CMDLINE_BUFSIZE         4096

struct pvh_e820_table {
    uint32_t entries;
    uint32_t reserved;
    struct hvm_memmap_table_entry table[E820_MAXENTRIES];
};

struct pvh_e820_table pvh_e820 asm("pvh_e820") __attribute__ ((aligned));

static struct hvm_start_info start_info;
static struct hvm_modlist_entry ramdisk_mod;
static uint8_t cmdline_buffer[CMDLINE_BUFSIZE];


static uintptr_t search_rsdp(uint32_t start_addr, uint32_t end_addr)
{
    uint64_t *rsdp_p;

    for (rsdp_p = (uint64_t *)start_addr; rsdp_p < (uint64_t *)end_addr;
         rsdp_p += 2) {
        if (*rsdp_p == RSDP_SIGNATURE) {
            return (uintptr_t)rsdp_p;
        }
    }

    return 0;
}

extern void pvh_load_kernel(void) asm("pvh_load_kernel");

void pvh_load_kernel(void)
{
    void *cmdline_addr = &cmdline_buffer;
    void *kernel_entry, *initrd_addr;
    uint32_t cmdline_size, initrd_size, fw_cfg_version = bios_cfg_version();

    start_info.magic = XEN_HVM_START_MAGIC_VALUE;
    start_info.version = 1;

    start_info.memmap_entries = pvh_e820.entries;
    start_info.memmap_paddr = (uintptr_t)pvh_e820.table;

    start_info.rsdp_paddr = search_rsdp(RSDP_AREA_ADDR,
                                        RSDP_AREA_ADDR + RSDP_AREA_SIZE);

    if (!start_info.rsdp_paddr) {
        uint32_t ebda_paddr = ((uint32_t)*((uint16_t *)EBDA_BASE_ADDR)) << 4;
        if (ebda_paddr > 0x400) {
            uint32_t *ebda = (uint32_t *)ebda_paddr;

            start_info.rsdp_paddr = search_rsdp(*ebda, *ebda + EBDA_SIZE);
        }
    }

    bios_cfg_read_entry(&cmdline_size, FW_CFG_CMDLINE_SIZE, 4, fw_cfg_version);
    bios_cfg_read_entry(cmdline_addr, FW_CFG_CMDLINE_DATA, cmdline_size,
                        fw_cfg_version);
    start_info.cmdline_paddr = (uintptr_t)cmdline_addr;

    bios_cfg_read_entry(&initrd_size, FW_CFG_INITRD_SIZE, 4, fw_cfg_version);
    if (initrd_size) {
        bios_cfg_read_entry(&initrd_addr, FW_CFG_INITRD_ADDR, 4,
                            fw_cfg_version);
        bios_cfg_read_entry(initrd_addr, FW_CFG_INITRD_DATA, initrd_size,
                            fw_cfg_version);

        ramdisk_mod.paddr = (uintptr_t)initrd_addr;
        ramdisk_mod.size = initrd_size;

        start_info.modlist_paddr = (uintptr_t)&ramdisk_mod;
        start_info.nr_modules = 1;
    }

    bios_cfg_read_entry(&kernel_entry, FW_CFG_KERNEL_ENTRY, 4, fw_cfg_version);

    asm volatile("jmp *%1" : : "b"(&start_info), "c"(kernel_entry));
}
