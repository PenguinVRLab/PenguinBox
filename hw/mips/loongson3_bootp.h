/*
 * LEFI (a UEFI-like interface for BIOS-Kernel boot parameters) data structures
 * defined at arch/mips/include/asm/mach-loongson64/boot_param.h in Linux kernel
 *
 * Copyright (c) 2017-2020 Huacai Chen (chenhc@lemote.com)
 * Copyright (c) 2017-2020 Jiaxun Yang <jiaxun.yang@flygoat.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef HW_MIPS_LOONGSON3_BOOTP_H
#define HW_MIPS_LOONGSON3_BOOTP_H

struct efi_memory_map_loongson {
    uint16_t vers;
    uint32_t nr_map;
    uint32_t mem_freq;
    struct mem_map {
        uint32_t node_id;
        uint32_t mem_type;
        uint64_t mem_start;
        uint32_t mem_size;
    } map[128];
} QEMU_PACKED;

enum loongson_cpu_type {
    Legacy_2E = 0x0,
    Legacy_2F = 0x1,
    Legacy_3A = 0x2,
    Legacy_3B = 0x3,
    Legacy_1A = 0x4,
    Legacy_1B = 0x5,
    Legacy_2G = 0x6,
    Legacy_2H = 0x7,
    Loongson_1A = 0x100,
    Loongson_1B = 0x101,
    Loongson_2E = 0x200,
    Loongson_2F = 0x201,
    Loongson_2G = 0x202,
    Loongson_2H = 0x203,
    Loongson_3A = 0x300,
    Loongson_3B = 0x301
};

struct efi_cpuinfo_loongson {
    uint16_t vers;
    uint32_t processor_id;
    uint32_t cputype;
    uint32_t total_node;
    uint16_t cpu_startup_core_id;
    uint16_t reserved_cores_mask;
    uint32_t cpu_clock_freq;
    uint32_t nr_cpus;
    char cpuname[64];
} QEMU_PACKED;

#define MAX_UARTS 64
struct uart_device {
    uint32_t iotype;
    uint32_t uartclk;
    uint32_t int_offset;
    uint64_t uart_base;
} QEMU_PACKED;

#define MAX_SENSORS 64
#define SENSOR_TEMPER  0x00000001
#define SENSOR_VOLTAGE 0x00000002
#define SENSOR_FAN     0x00000004
struct sensor_device {
    char name[32];
    char label[64];
    uint32_t type;
    uint32_t id;
    uint32_t fan_policy;
    uint32_t fan_percent;
    uint64_t base_addr;
} QEMU_PACKED;

struct system_loongson {
    uint16_t vers;
    uint32_t ccnuma_smp;
    uint32_t sing_double_channel;
    uint32_t nr_uarts;
    struct uart_device uarts[MAX_UARTS];
    uint32_t nr_sensors;
    struct sensor_device sensors[MAX_SENSORS];
    char has_ec;
    char ec_name[32];
    uint64_t ec_base_addr;
    char has_tcm;
    char tcm_name[32];
    uint64_t tcm_base_addr;
    uint64_t workarounds;
    uint64_t of_dtb_addr;
} QEMU_PACKED;

struct irq_source_routing_table {
    uint16_t vers;
    uint16_t size;
    uint16_t rtr_bus;
    uint16_t rtr_devfn;
    uint32_t vendor;
    uint32_t device;
    uint32_t PIC_type;
    uint64_t ht_int_bit;
    uint64_t ht_enable;
    uint32_t node_id;
    uint64_t pci_mem_start_addr;
    uint64_t pci_mem_end_addr;
    uint64_t pci_io_start_addr;
    uint64_t pci_io_end_addr;
    uint64_t pci_config_addr;
    uint16_t dma_mask_bits;
    uint16_t dma_noncoherent;
} QEMU_PACKED;

struct interface_info {
    uint16_t vers;
    uint16_t size;
    uint8_t  flag;
    char description[64];
} QEMU_PACKED;

#define MAX_RESOURCE_NUMBER 128
struct resource_loongson {
    uint64_t start;
    uint64_t end;
    char name[64];
    uint32_t flags;
};

struct archdev_data {};

struct board_devices {
    char name[64];
    uint32_t num_resources;
    struct resource_loongson resource[MAX_RESOURCE_NUMBER];
    struct archdev_data archdata;
};

struct loongson_special_attribute {
    uint16_t vers;
    char special_name[64];
    uint32_t loongson_special_type;
    struct resource_loongson resource[MAX_RESOURCE_NUMBER];
};

struct loongson_params {
    uint64_t memory_offset;
    uint64_t cpu_offset;
    uint64_t system_offset;
    uint64_t irq_offset;
    uint64_t interface_offset;
    uint64_t special_offset;
    uint64_t boarddev_table_offset;
};

struct smbios_tables {
    uint16_t vers;
    uint64_t vga_bios;
    struct loongson_params lp;
};

struct efi_reset_system_t {
    uint64_t ResetCold;
    uint64_t ResetWarm;
    uint64_t ResetType;
    uint64_t Shutdown;
    uint64_t DoSuspend;
};

struct efi_loongson {
    uint64_t mps;
    uint64_t acpi;
    uint64_t acpi20;
    struct smbios_tables smbios;
    uint64_t sal_systab;
    uint64_t boot_info;
};

struct boot_params {
    struct efi_loongson efi;
    struct efi_reset_system_t reset_system;
};

#define LOONGSON3_CORE_PER_NODE 4

enum {
    VIRT_LOWMEM,
    VIRT_PM,
    VIRT_FW_CFG,
    VIRT_RTC,
    VIRT_PCIE_PIO,
    VIRT_PCIE_ECAM,
    VIRT_BIOS_ROM,
    VIRT_UART,
    VIRT_LIOINTC,
    VIRT_IPI,
    VIRT_PCIE_MMIO,
    VIRT_HIGHMEM
};

enum {
    LOADER_KERNEL,
    LOADER_INITRD,
    LOADER_CMDLINE
};

enum {
    LOADER_BOOTROM,
    LOADER_PARAM,
};

extern const MemMapEntry virt_memmap[];
void init_loongson_params(struct loongson_params *lp, void *p,
                          uint32_t cpu_count, uint32_t processor_id,
                          uint64_t cpu_freq, uint64_t ram_size);
void init_reset_system(struct efi_reset_system_t *reset);

#endif
