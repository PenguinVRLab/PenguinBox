/*
 * Virtual Machine coreinfo device
 *
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * Authors: Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "system/reset.h"
#include "hw/nvram/fw_cfg.h"
#include "migration/vmstate.h"
#include "hw/misc/vmcoreinfo.h"

static void fw_cfg_vmci_write(void *opaque, off_t offset, size_t len)
{
    VMCoreInfoState *s = opaque;

    s->has_vmcoreinfo = offset == 0 && len == sizeof(s->vmcoreinfo)
        && s->vmcoreinfo.guest_format != FW_CFG_VMCOREINFO_FORMAT_NONE;
}

static void vmcoreinfo_reset_hold(Object *obj, ResetType type)
{
    VMCoreInfoState *s = VMCOREINFO(obj);

    s->has_vmcoreinfo = false;
    memset(&s->vmcoreinfo, 0, sizeof(s->vmcoreinfo));
    s->vmcoreinfo.host_format = cpu_to_le16(FW_CFG_VMCOREINFO_FORMAT_ELF);
}

static void vmcoreinfo_realize(DeviceState *dev, Error **errp)
{
    VMCoreInfoState *s = VMCOREINFO(dev);
    FWCfgState *fw_cfg = fw_cfg_find();
    static VMCoreInfoState * volatile vmcoreinfo_state G_GNUC_UNUSED;

    if (!vmcoreinfo_find()) {
        error_setg(errp, "at most one %s device is permitted",
                   TYPE_VMCOREINFO);
        return;
    }

    if (!fw_cfg || !fw_cfg->dma_enabled) {
        error_setg(errp, "%s device requires fw_cfg with DMA",
                   TYPE_VMCOREINFO);
        return;
    }

    fw_cfg_add_file_callback(fw_cfg, FW_CFG_VMCOREINFO_FILENAME,
                             NULL, fw_cfg_vmci_write, s,
                             &s->vmcoreinfo, sizeof(s->vmcoreinfo), false);

    qemu_register_resettable(OBJECT(s));
    vmcoreinfo_state = s;
}

static const VMStateDescription vmstate_vmcoreinfo = {
    .name = "vmcoreinfo",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_BOOL(has_vmcoreinfo, VMCoreInfoState),
        VMSTATE_UINT16(vmcoreinfo.host_format, VMCoreInfoState),
        VMSTATE_UINT16(vmcoreinfo.guest_format, VMCoreInfoState),
        VMSTATE_UINT32(vmcoreinfo.size, VMCoreInfoState),
        VMSTATE_UINT64(vmcoreinfo.paddr, VMCoreInfoState),
        VMSTATE_END_OF_LIST()
    },
};

static void vmcoreinfo_device_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_vmcoreinfo;
    dc->realize = vmcoreinfo_realize;
    dc->hotpluggable = false;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    rc->phases.hold = vmcoreinfo_reset_hold;
}

static const TypeInfo vmcoreinfo_types[] = {
    {
        .name           = TYPE_VMCOREINFO,
        .parent         = TYPE_DEVICE,
        .instance_size  = sizeof(VMCoreInfoState),
        .class_init     = vmcoreinfo_device_class_init,
    }
};

DEFINE_TYPES(vmcoreinfo_types)
