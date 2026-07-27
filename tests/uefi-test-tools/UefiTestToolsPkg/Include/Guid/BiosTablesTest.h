/** @file
  Expose the address(es) of the ACPI RSD PTR table(s) and the SMBIOS entry
  point(s) in a MB-aligned structure to the hypervisor.

  The hypervisor locates the MB-aligned structure based on the signature GUID
  that is at offset 0 in the structure. Once the RSD PTR and SMBIOS anchor
  address(es) are retrieved, the hypervisor may perform various ACPI and SMBIOS
  checks.

  This feature is a development aid, for supporting ACPI and SMBIOS table unit
  tests in hypervisors. Do not enable in production builds.

  Copyright (C) 2019, Red Hat, Inc.

  This program and the accompanying materials are licensed and made available
  under the terms and conditions of the BSD License that accompanies this
  distribution. The full text of the license may be found at
  <http://opensource.org/licenses/bsd-license.php>.

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS, WITHOUT
  WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
**/

#ifndef BIOSTABLESTEST_H
#define BIOSTABLESTEST_H

#include <Uefi/UefiBaseType.h>

#define BIOS_TABLES_TEST_GUID                          \
  {                                                    \
    0x5478594e,                                        \
    0xdfcb,                                            \
    0x425f,                                            \
    { 0x8e, 0x42, 0xc8, 0xaf, 0xf8, 0x8a, 0x88, 0x7a } \
  }

extern EFI_GUID gBiosTablesTestGuid;

#pragma pack (1)
typedef struct {
  EFI_GUID             InverseSignatureGuid;
  EFI_PHYSICAL_ADDRESS Rsdp10;
  EFI_PHYSICAL_ADDRESS Rsdp20;
  EFI_PHYSICAL_ADDRESS Smbios21;
  EFI_PHYSICAL_ADDRESS Smbios30;
} BIOS_TABLES_TEST;
#pragma pack ()

#endif
