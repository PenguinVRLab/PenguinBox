/*
 * QEMU Confidential Guest support
 *   This interface describes the common pieces between various
 *   schemes for protecting guest memory or other state against a
 *   compromised hypervisor.  This includes memory encryption (AMD's
 *   SEV and Intel's MKTME) or special protection modes (PEF on POWER,
 *   or PV on s390x).
 *
 * Copyright Red Hat.
 *
 * Authors:
 *  David Gibson <david@gibson.dropbear.id.au>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 *
 */
#ifndef QEMU_CONFIDENTIAL_GUEST_SUPPORT_H
#define QEMU_CONFIDENTIAL_GUEST_SUPPORT_H

#include "qom/object.h"
#include "exec/hwaddr.h"

#define TYPE_CONFIDENTIAL_GUEST_SUPPORT "confidential-guest-support"
OBJECT_DECLARE_TYPE(ConfidentialGuestSupport,
                    ConfidentialGuestSupportClass,
                    CONFIDENTIAL_GUEST_SUPPORT)


typedef enum ConfidentialGuestPlatformType {
    CGS_PLATFORM_SEV,
    CGS_PLATFORM_SEV_ES,
    CGS_PLATFORM_SEV_SNP,
} ConfidentialGuestPlatformType;

typedef enum ConfidentialGuestMemoryType {
    CGS_MEM_RAM,
    CGS_MEM_RESERVED,
    CGS_MEM_ACPI,
    CGS_MEM_NVS,
    CGS_MEM_UNUSABLE,
} ConfidentialGuestMemoryType;

typedef struct ConfidentialGuestMemoryMapEntry {
    uint64_t gpa;
    uint64_t size;
    ConfidentialGuestMemoryType type;
} ConfidentialGuestMemoryMapEntry;

typedef enum ConfidentialGuestPageType {
    CGS_PAGE_TYPE_NORMAL,
    CGS_PAGE_TYPE_VMSA,
    CGS_PAGE_TYPE_ZERO,
    CGS_PAGE_TYPE_UNMEASURED,
    CGS_PAGE_TYPE_SECRETS,
    CGS_PAGE_TYPE_CPUID,
    CGS_PAGE_TYPE_REQUIRED_MEMORY,
} ConfidentialGuestPageType;

typedef enum ConfidentialGuestPolicyType {
    GUEST_POLICY_SEV,
} ConfidentialGuestPolicyType;

struct ConfidentialGuestSupport {
    Object parent;

    bool require_guest_memfd;

    bool ready;
};

typedef struct ConfidentialGuestSupportClass {
    ObjectClass parent;

    int (*kvm_init)(ConfidentialGuestSupport *cgs, Error **errp);
    int (*kvm_reset)(ConfidentialGuestSupport *cgs, Error **errp);

    bool (*check_support)(ConfidentialGuestPlatformType platform,
                         uint16_t platform_version, uint8_t highest_vtl,
                         uint64_t shared_gpa_boundary);

    int (*set_guest_state)(hwaddr gpa, uint8_t *ptr, uint64_t len,
                           ConfidentialGuestPageType memory_type,
                           uint16_t cpu_index, Error **errp);

    int (*set_guest_policy)(ConfidentialGuestPolicyType policy_type,
                            uint64_t policy,
                            void *policy_data1, uint32_t policy_data1_size,
                            void *policy_data2, uint32_t policy_data2_size,
                            Error **errp);

    int (*get_mem_map_entry)(int index, ConfidentialGuestMemoryMapEntry *entry,
                             Error **errp);
} ConfidentialGuestSupportClass;

static inline int confidential_guest_kvm_init(ConfidentialGuestSupport *cgs,
                                              Error **errp)
{
    ConfidentialGuestSupportClass *klass;

    klass = CONFIDENTIAL_GUEST_SUPPORT_GET_CLASS(cgs);
    if (klass->kvm_init) {
        return klass->kvm_init(cgs, errp);
    }

    return 0;
}

static inline int confidential_guest_kvm_reset(ConfidentialGuestSupport *cgs,
                                               Error **errp)
{
    ConfidentialGuestSupportClass *klass;

    klass = CONFIDENTIAL_GUEST_SUPPORT_GET_CLASS(cgs);
    if (klass->kvm_reset) {
        return klass->kvm_reset(cgs, errp);
    }

    return 0;
}

#endif
