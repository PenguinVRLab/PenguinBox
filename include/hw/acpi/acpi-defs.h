/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef QEMU_ACPI_DEFS_H
#define QEMU_ACPI_DEFS_H

enum {
    ACPI_FADT_F_WBINVD,
    ACPI_FADT_F_WBINVD_FLUSH,
    ACPI_FADT_F_PROC_C1,
    ACPI_FADT_F_P_LVL2_UP,
    ACPI_FADT_F_PWR_BUTTON,
    ACPI_FADT_F_SLP_BUTTON,
    ACPI_FADT_F_FIX_RTC,
    ACPI_FADT_F_RTC_S4,
    ACPI_FADT_F_TMR_VAL_EXT,
    ACPI_FADT_F_DCK_CAP,
    ACPI_FADT_F_RESET_REG_SUP,
    ACPI_FADT_F_SEALED_CASE,
    ACPI_FADT_F_HEADLESS,
    ACPI_FADT_F_CPU_SW_SLP,
    ACPI_FADT_F_PCI_EXP_WAK,
    ACPI_FADT_F_USE_PLATFORM_CLOCK,
    ACPI_FADT_F_S4_RTC_STS_VALID,
    ACPI_FADT_F_REMOTE_POWER_ON_CAPABLE,
    ACPI_FADT_F_FORCE_APIC_CLUSTER_MODEL,
    ACPI_FADT_F_FORCE_APIC_PHYSICAL_DESTINATION_MODE,
    ACPI_FADT_F_HW_REDUCED_ACPI,
    ACPI_FADT_F_LOW_POWER_S0_IDLE_CAPABLE,
};

typedef struct AcpiRsdpData {
    char *oem_id;
    uint8_t revision;

    unsigned *rsdt_tbl_offset;
    unsigned *xsdt_tbl_offset;
} AcpiRsdpData;

struct AcpiGenericAddress {
    uint8_t space_id;
    uint8_t bit_width;
    uint8_t bit_offset;
    uint8_t access_width;
    uint64_t address;
};

typedef struct AcpiFadtData {
    struct AcpiGenericAddress pm1a_cnt;
    struct AcpiGenericAddress pm1a_evt;
    struct AcpiGenericAddress pm_tmr;
    struct AcpiGenericAddress gpe0_blk;
    struct AcpiGenericAddress reset_reg;
    struct AcpiGenericAddress sleep_ctl;
    struct AcpiGenericAddress sleep_sts;
    uint8_t reset_val;
    uint8_t  rev;
    uint32_t flags;
    uint32_t smi_cmd;
    uint16_t sci_int;
    uint8_t  int_model;
    uint8_t  acpi_enable_cmd;
    uint8_t  acpi_disable_cmd;
    uint8_t  rtc_century;
    uint16_t plvl2_lat;
    uint16_t plvl3_lat;
    uint16_t arm_boot_arch;
    uint16_t iapc_boot_arch;
    uint8_t minor_ver;

    unsigned *facs_tbl_offset;
    unsigned *dsdt_tbl_offset;
    unsigned *xdsdt_tbl_offset;
} AcpiFadtData;

typedef struct AcpiGas {
    uint8_t id;
    uint8_t width;
    uint8_t offset;
    uint8_t size;
    uint64_t addr;
} AcpiGas;

typedef struct AcpiSpcrData {
    uint8_t interface_type;
    uint8_t reserved[3];
    struct AcpiGas base_addr;
    uint8_t interrupt_type;
    uint8_t pc_interrupt;
    uint32_t interrupt;
    uint8_t baud_rate;
    uint8_t parity;
    uint8_t stop_bits;
    uint8_t flow_control;
    uint8_t terminal_type;
    uint8_t language;
    uint16_t pci_device_id;
    uint16_t pci_vendor_id;
    uint8_t pci_bus;
    uint8_t pci_device;
    uint8_t pci_function;
    uint32_t pci_flags;
    uint8_t pci_segment;
    uint32_t uart_clk_freq;
    uint32_t precise_baudrate;
    uint32_t namespace_string_length;
    uint32_t namespace_string_offset;
    char namespace_string[];
} AcpiSpcrData;

#define ACPI_FADT_ARM_PSCI_COMPLIANT  (1 << 0)
#define ACPI_FADT_ARM_PSCI_USE_HVC    (1 << 1)

#endif
