/*
 * QTest testcase for VM Generation ID
 *
 * Copyright (c) 2016 Red Hat, Inc.
 * Copyright (c) 2017 Skyport Systems
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/bitmap.h"
#include "qemu/uuid.h"
#include "hw/acpi/acpi-defs.h"
#include "boot-sector.h"
#include "acpi-utils.h"
#include "libqtest.h"
#include "qobject/qdict.h"

#define VGID_GUID "324e6eaf-d1d1-4bf6-bf41-b9bb6c91fb87"
#define VMGENID_GUID_OFFSET 40
#define RSDP_ADDR_INVALID 0x100000

static uint32_t acpi_find_vgia(QTestState *qts)
{
    uint32_t rsdp_offset;
    uint32_t guid_offset = 0;
    uint8_t rsdp_table[36 ];
    uint32_t rsdt_len, table_length;
    uint8_t *rsdt, *ent;

    boot_sector_test(qts);

    rsdp_offset = acpi_find_rsdp_address(qts);

    g_assert_cmphex(rsdp_offset, <, RSDP_ADDR_INVALID);


    acpi_fetch_rsdp_table(qts, rsdp_offset, rsdp_table);
    acpi_fetch_table(qts, &rsdt, &rsdt_len, &rsdp_table[16 ],
                     4, "RSDT", true);

    ACPI_FOREACH_RSDT_ENTRY(rsdt, rsdt_len, ent, 4 ) {
        uint8_t *table_aml;

        acpi_fetch_table(qts, &table_aml, &table_length, ent, 4, NULL, true);
        if (!memcmp(table_aml + 16 , "VMGENID", 7)) {
            uint32_t vgia_val;
            uint8_t *aml = &table_aml[36 ];
            g_assert(aml[0 ] == 0x08);
            g_assert(memcmp(&aml[1 ], "VGIA", 4) == 0);
            g_assert(aml[5 ] == 0x0C );
            memcpy(&vgia_val, &aml[6 ], 4);

            guid_offset = le32_to_cpu(vgia_val) + VMGENID_GUID_OFFSET;
            g_free(table_aml);
            break;
        }
        g_free(table_aml);
    }
    g_free(rsdt);
    return guid_offset;
}

static void read_guid_from_memory(QTestState *qts, QemuUUID *guid)
{
    uint32_t vmgenid_addr;
    int i;

    vmgenid_addr = acpi_find_vgia(qts);
    g_assert(vmgenid_addr);

    for (i = 0; i < 16; i++) {
        guid->data[i] = qtest_readb(qts, vmgenid_addr + i);
    }
    *guid = qemu_uuid_bswap(*guid);
}

static void read_guid_from_monitor(QTestState *qts, QemuUUID *guid)
{
    QDict *rsp, *rsp_ret;
    const char *guid_str;

    rsp = qtest_qmp(qts, "{ 'execute': 'query-vm-generation-id' }");
    if (qdict_haskey(rsp, "return")) {
        rsp_ret = qdict_get_qdict(rsp, "return");
        g_assert(qdict_haskey(rsp_ret, "guid"));
        guid_str = qdict_get_str(rsp_ret, "guid");
        g_assert(qemu_uuid_parse(guid_str, guid) == 0);
    }
    qobject_unref(rsp);
}

static char disk[] = "tests/vmgenid-test-disk-XXXXXX";

#define GUID_CMD(guid)                          \
    "-accel kvm -accel tcg "                    \
    "-device vmgenid,id=testvgid,guid=%s "      \
    "-drive id=hd0,if=none,file=%s,format=raw " \
    "-device ide-hd,drive=hd0 ", guid, disk

static void vmgenid_set_guid_test(void)
{
    QemuUUID expected, measured;
    QTestState *qts;

    g_assert(qemu_uuid_parse(VGID_GUID, &expected) == 0);

    qts = qtest_initf(GUID_CMD(VGID_GUID));

    read_guid_from_memory(qts, &measured);
    g_assert(memcmp(measured.data, expected.data, sizeof(measured.data)) == 0);

    qtest_quit(qts);
}

static void vmgenid_set_guid_auto_test(void)
{
    QemuUUID measured;
    QTestState *qts;

    qts = qtest_initf(GUID_CMD("auto"));

    read_guid_from_memory(qts, &measured);

    g_assert(!qemu_uuid_is_null(&measured));

    qtest_quit(qts);
}

static void vmgenid_query_monitor_test(void)
{
    QemuUUID expected, measured;
    QTestState *qts;

    g_assert(qemu_uuid_parse(VGID_GUID, &expected) == 0);

    qts = qtest_initf(GUID_CMD(VGID_GUID));

    read_guid_from_monitor(qts, &measured);
    g_assert(memcmp(measured.data, expected.data, sizeof(measured.data)) == 0);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    if (!qtest_has_accel("tcg") && !qtest_has_accel("kvm")) {
        g_test_skip("No KVM or TCG accelerator available");
        return 0;
    }

    ret = boot_sector_init(disk);
    if (ret) {
        return ret;
    }

    qtest_add_func("/vmgenid/vmgenid/set-guid",
                   vmgenid_set_guid_test);
    qtest_add_func("/vmgenid/vmgenid/set-guid-auto",
                   vmgenid_set_guid_auto_test);
    qtest_add_func("/vmgenid/vmgenid/query-monitor",
                   vmgenid_query_monitor_test);
    ret = g_test_run();
    boot_sector_cleanup(disk);

    return ret;
}
