#ifndef ACPI_VMGENID_H
#define ACPI_VMGENID_H

#include "hw/acpi/bios-linker-loader.h"
#include "hw/qdev-core.h"
#include "qemu/uuid.h"
#include "qom/object.h"

#define TYPE_VMGENID           "vmgenid"
#define VMGENID_GUID             "guid"
#define VMGENID_GUID_FW_CFG_FILE      "etc/vmgenid_guid"
#define VMGENID_ADDR_FW_CFG_FILE      "etc/vmgenid_addr"

#define VMGENID_FW_CFG_SIZE      4096
#define VMGENID_GUID_OFFSET      40

OBJECT_DECLARE_SIMPLE_TYPE(VmGenIdState, VMGENID)

struct VmGenIdState {
    DeviceState parent_obj;
    QemuUUID guid;
    uint8_t vmgenid_addr_le[8];
};

static inline Object *find_vmgenid_dev(void)
{
    return object_resolve_path_type("", TYPE_VMGENID, NULL);
}

void vmgenid_build_acpi(VmGenIdState *vms, GArray *table_data, GArray *guid,
                        BIOSLinker *linker, const char *oem_id);
void vmgenid_add_fw_cfg(VmGenIdState *vms, FWCfgState *s, GArray *guid);

#endif
