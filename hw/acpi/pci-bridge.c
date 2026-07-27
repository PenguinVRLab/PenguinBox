/*
 * QEMU ACPI PCI bridge
 *
 * Copyright (c) 2023 Red Hat, Inc.
 *
 * Author:
 *   Igor Mammedov <imammedo@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/acpi/pci.h"
#include "hw/pci/pci_bridge.h"
#include "hw/acpi/pcihp.h"

void build_pci_bridge_aml(AcpiDevAmlIf *adev, Aml *scope)
{
    PCIBridge *br = PCI_BRIDGE(adev);

    if (!DEVICE(br)->hotplugged) {
        PCIBus *sec_bus = pci_bridge_get_sec_bus(br);

        build_append_pci_bus_devices(scope, sec_bus);

        if (object_property_find(OBJECT(sec_bus), ACPI_PCIHP_PROP_BSEL)) {
            build_append_pcihp_slots(scope, sec_bus);
        }
    }
}

Aml *build_pci_bridge_edsm(void)
{
    Aml *method, *ifctx;
    Aml *zero = aml_int(0);
    Aml *func = aml_arg(2);
    Aml *ret = aml_local(0);
    Aml *aidx = aml_local(1);
    Aml *params = aml_arg(4);

    method = aml_method("EDSM", 5, AML_SERIALIZED);

    ifctx = aml_if(aml_equal(func, zero));
    {
        const uint8_t caps = 1 | BIT(7);
        build_append_pci_dsm_func0_common(ifctx, ret);
        aml_append(ifctx, aml_store(aml_int(caps), aml_index(ret, zero)));
        aml_append(ifctx, aml_return(ret));
    }
    aml_append(method, ifctx);

    ifctx = aml_if(aml_equal(func, aml_int(7)));
    {
       Aml *pkg = aml_package(2);
       aml_append(pkg, zero);
       aml_append(pkg, aml_string("%s", ""));
       aml_append(ifctx, aml_store(pkg, ret));

       aml_append(ifctx,
           aml_store(aml_derefof(aml_index(params, aml_int(0))), aidx));
       aml_append(ifctx, aml_store(aidx, aml_index(ret, zero)));
       aml_append(ifctx, aml_return(ret));
    }
    aml_append(method, ifctx);

    return method;
}

