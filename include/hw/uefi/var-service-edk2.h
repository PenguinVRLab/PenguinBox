/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * uefi-vars device - structs and defines from edk2
 *
 * Note: The edk2 UINTN type has been mapped to uint64_t,
 *       so the structs are compatible with 64bit edk2 builds.
 */
#ifndef QEMU_UEFI_VAR_SERVICE_EDK2_H
#define QEMU_UEFI_VAR_SERVICE_EDK2_H

#include "qemu/uuid.h"

#define MAX_BIT                   0x8000000000000000ULL
#define ENCODE_ERROR(StatusCode)  (MAX_BIT | (StatusCode))
#define EFI_SUCCESS               0
#define EFI_INVALID_PARAMETER     ENCODE_ERROR(2)
#define EFI_UNSUPPORTED           ENCODE_ERROR(3)
#define EFI_BAD_BUFFER_SIZE       ENCODE_ERROR(4)
#define EFI_BUFFER_TOO_SMALL      ENCODE_ERROR(5)
#define EFI_WRITE_PROTECTED       ENCODE_ERROR(8)
#define EFI_OUT_OF_RESOURCES      ENCODE_ERROR(9)
#define EFI_NOT_FOUND             ENCODE_ERROR(14)
#define EFI_ACCESS_DENIED         ENCODE_ERROR(15)
#define EFI_ALREADY_STARTED       ENCODE_ERROR(20)
#define EFI_SECURITY_VIOLATION    ENCODE_ERROR(26)

#define EFI_VARIABLE_NON_VOLATILE                           0x01
#define EFI_VARIABLE_BOOTSERVICE_ACCESS                     0x02
#define EFI_VARIABLE_RUNTIME_ACCESS                         0x04
#define EFI_VARIABLE_HARDWARE_ERROR_RECORD                  0x08
#define EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS             0x10
#define EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS  0x20
#define EFI_VARIABLE_APPEND_WRITE                           0x40

#define SECURE_BOOT_ENABLE         1
#define SECURE_BOOT_DISABLE        0

#define SECURE_BOOT_MODE_ENABLE    1
#define SECURE_BOOT_MODE_DISABLE   0

#define CUSTOM_SECURE_BOOT_MODE    1
#define STANDARD_SECURE_BOOT_MODE  0

#define SETUP_MODE                 1
#define USER_MODE                  0

typedef uint64_t efi_status;
typedef struct mm_header mm_header;

struct mm_header {
    QemuUUID  guid;
    uint64_t  length;
};

#define SMM_VARIABLE_FUNCTION_GET_VARIABLE            1
#define SMM_VARIABLE_FUNCTION_GET_NEXT_VARIABLE_NAME  2
#define SMM_VARIABLE_FUNCTION_SET_VARIABLE            3
#define SMM_VARIABLE_FUNCTION_QUERY_VARIABLE_INFO     4
#define SMM_VARIABLE_FUNCTION_READY_TO_BOOT           5
#define SMM_VARIABLE_FUNCTION_EXIT_BOOT_SERVICE       6
#define SMM_VARIABLE_FUNCTION_LOCK_VARIABLE           8
#define SMM_VARIABLE_FUNCTION_GET_PAYLOAD_SIZE       11

typedef struct mm_variable mm_variable;
typedef struct mm_variable_access mm_variable_access;
typedef struct mm_next_variable mm_next_variable;
typedef struct mm_next_variable mm_lock_variable;
typedef struct mm_variable_info mm_variable_info;
typedef struct mm_get_payload_size mm_get_payload_size;

struct mm_variable {
    uint64_t  function;
    uint64_t  status;
};

struct QEMU_PACKED mm_variable_access {
    QemuUUID  guid;
    uint64_t  data_size;
    uint64_t  name_size;
    uint32_t  attributes;
};

struct mm_next_variable {
    QemuUUID  guid;
    uint64_t  name_size;
};

struct QEMU_PACKED mm_variable_info {
    uint64_t max_storage_size;
    uint64_t free_storage_size;
    uint64_t max_variable_size;
    uint32_t attributes;
};

struct mm_get_payload_size {
    uint64_t  payload_size;
};

#define VAR_CHECK_POLICY_COMMAND_DISABLE     0x01
#define VAR_CHECK_POLICY_COMMAND_IS_ENABLED  0x02
#define VAR_CHECK_POLICY_COMMAND_REGISTER    0x03
#define VAR_CHECK_POLICY_COMMAND_DUMP        0x04
#define VAR_CHECK_POLICY_COMMAND_LOCK        0x05

typedef struct mm_check_policy mm_check_policy;
typedef struct mm_check_policy_is_enabled mm_check_policy_is_enabled;
typedef struct mm_check_policy_dump_params mm_check_policy_dump_params;

struct QEMU_PACKED mm_check_policy {
    uint32_t  signature;
    uint32_t  revision;
    uint32_t  command;
    uint64_t  result;
};

struct QEMU_PACKED mm_check_policy_is_enabled {
    uint8_t   state;
};

struct QEMU_PACKED mm_check_policy_dump_params {
    uint32_t  page_requested;
    uint32_t  total_size;
    uint32_t  page_size;
    uint8_t   has_more;
};

#define VARIABLE_POLICY_ENTRY_REVISION  0x00010000

#define VARIABLE_POLICY_TYPE_NO_LOCK            0
#define VARIABLE_POLICY_TYPE_LOCK_NOW           1
#define VARIABLE_POLICY_TYPE_LOCK_ON_CREATE     2
#define VARIABLE_POLICY_TYPE_LOCK_ON_VAR_STATE  3

typedef struct variable_policy_entry variable_policy_entry;
typedef struct variable_lock_on_var_state variable_lock_on_var_state;

struct variable_policy_entry {
    uint32_t      version;
    uint16_t      size;
    uint16_t      offset_to_name;
    QemuUUID      namespace;
    uint32_t      min_size;
    uint32_t      max_size;
    uint32_t      attributes_must_have;
    uint32_t      attributes_cant_have;
    uint8_t       lock_policy_type;
    uint8_t       padding[3];
};

struct variable_lock_on_var_state {
    QemuUUID      namespace;
    uint8_t       value;
    uint8_t       padding;
};

#define WIN_CERT_TYPE_EFI_GUID  0x0EF1

typedef struct efi_time efi_time;
typedef struct efi_siglist efi_siglist;
typedef struct variable_auth_2 variable_auth_2;

struct efi_time {
    uint16_t  year;
    uint8_t   month;
    uint8_t   day;
    uint8_t   hour;
    uint8_t   minute;
    uint8_t   second;
    uint8_t   pad1;
    uint32_t  nanosecond;
    int16_t   timezone;
    uint8_t   daylight;
    uint8_t   pad2;
};

struct efi_siglist {
    QemuUUID  guid_type;
    uint32_t  siglist_size;
    uint32_t  header_size;
    uint32_t  sig_size;
};

struct variable_auth_2 {
    struct efi_time timestamp;

    uint32_t  hdr_length;
    uint16_t  hdr_revision;
    uint16_t  hdr_cert_type;
    QemuUUID  guid_cert_type;
    uint8_t   cert_data[];
};

#endif
