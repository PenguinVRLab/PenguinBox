/* Support for generating ACPI tables and passing them to Guests
 *
 * Copyright (C) 2008-2010  Kevin O'Connor <kevin@koconnor.net>
 * Copyright (C) 2006 Fabrice Bellard
 * Copyright (C) 2013 Red Hat Inc
 *
 * Author: Michael S. Tsirkin <mst@redhat.com>
 *
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

#include "qemu/osdep.h"
#include "qapi/error.h"

#include "system/memory.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/utils.h"
#include "target/i386/cpu.h"

#include "acpi-build.h"
#include "acpi-common.h"

void pc_madt_cpu_entry(int uid, const CPUArchIdList *apic_ids,
                       GArray *entry, bool force_enabled)
{
    uint32_t apic_id = apic_ids->cpus[uid].arch_id;
    uint32_t flags = apic_ids->cpus[uid].cpu != NULL || force_enabled ?
                     1 : 0;

    if (apic_id < 255) {
        build_append_int_noprefix(entry, 0, 1);
        build_append_int_noprefix(entry, 8, 1);
        build_append_int_noprefix(entry, uid, 1);
        build_append_int_noprefix(entry, apic_id, 1);
        build_append_int_noprefix(entry, flags, 4);
    } else {
        build_append_int_noprefix(entry, 9, 1);
        build_append_int_noprefix(entry, 16, 1);
        build_append_int_noprefix(entry, 0, 2);
        build_append_int_noprefix(entry, apic_id, 4);
        build_append_int_noprefix(entry, flags, 4);
        build_append_int_noprefix(entry, uid, 4);
    }
}

static void build_ioapic(GArray *entry, uint8_t id, uint32_t addr, uint32_t irq)
{
    build_append_int_noprefix(entry, 1, 1);
    build_append_int_noprefix(entry, 12, 1);
    build_append_int_noprefix(entry, id, 1);
    build_append_int_noprefix(entry, 0, 1);
    build_append_int_noprefix(entry, addr, 4);
    build_append_int_noprefix(entry, irq, 4);
}

static void
build_xrupt_override(GArray *entry, uint8_t src, uint32_t gsi, uint16_t flags)
{
    build_append_int_noprefix(entry, 2, 1);
    build_append_int_noprefix(entry, 10, 1);
    build_append_int_noprefix(entry, 0, 1);
    build_append_int_noprefix(entry, src, 1);
    build_append_int_noprefix(entry, gsi, 4);
    build_append_int_noprefix(entry, flags, 2);
}

void acpi_build_madt(GArray *table_data, BIOSLinker *linker,
                     X86MachineState *x86ms,
                     const char *oem_id, const char *oem_table_id)
{
    int i;
    bool x2apic_mode = false;
    MachineClass *mc = MACHINE_GET_CLASS(x86ms);
    X86MachineClass *x86mc = X86_MACHINE_GET_CLASS(x86ms);
    const CPUArchIdList *apic_ids = mc->possible_cpu_arch_ids(MACHINE(x86ms));
    AcpiTable table = { .sig = "APIC", .rev = 3, .oem_id = oem_id,
                        .oem_table_id = oem_table_id };

    acpi_table_begin(&table, table_data);
    build_append_int_noprefix(table_data, APIC_DEFAULT_ADDRESS, 4);
    build_append_int_noprefix(table_data,
                              x86ms->pic != ON_OFF_AUTO_OFF ? 1 : 0 , 4);

    for (i = 0; i < apic_ids->len; i++) {
        pc_madt_cpu_entry(i, apic_ids, table_data, false);
        if (apic_ids->cpus[i].arch_id > 254) {
            x2apic_mode = true;
        }
    }

    build_ioapic(table_data, ACPI_BUILD_IOAPIC_ID, IO_APIC_DEFAULT_ADDRESS, 0);
    if (x86ms->ioapic2) {
        build_ioapic(table_data, ACPI_BUILD_IOAPIC_ID + 1,
                     IO_APIC_SECONDARY_ADDRESS, IO_APIC_SECONDARY_IRQBASE);
    }

    if (x86mc->apic_xrupt_override) {
        build_xrupt_override(table_data, 0, 2,
            0 );
    }

    for (i = 1; i < 16; i++) {
        if (!(x86ms->pci_irq_mask & (1 << i))) {
            continue;
        }
        build_xrupt_override(table_data, i, i,
            0xd );
    }

    if (x2apic_mode) {
        build_append_int_noprefix(table_data, 0xA, 1);
        build_append_int_noprefix(table_data, 12, 1);
        build_append_int_noprefix(table_data, 0, 2);
        build_append_int_noprefix(table_data, 0xFFFFFFFF ,
                                  4);
        build_append_int_noprefix(table_data, 1 , 1);
        build_append_int_noprefix(table_data, 0, 3);
    } else {
        build_append_int_noprefix(table_data, 4, 1);
        build_append_int_noprefix(table_data, 6, 1);
        build_append_int_noprefix(table_data, 0xFF , 1);
        build_append_int_noprefix(table_data, 0, 2);
        build_append_int_noprefix(table_data, 1 , 1);
    }

    acpi_table_end(linker, &table);
}

