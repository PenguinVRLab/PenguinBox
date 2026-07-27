/*
 * QEMU KVM support -- ARM specific functions.
 *
 * Copyright (c) 2012 Linaro Limited
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_KVM_ARM_H
#define QEMU_KVM_ARM_H

#include "system/kvm.h"
#include "target/arm/cpu-qom.h"

#define KVM_ARM_VGIC_V2   (1 << 0)
#define KVM_ARM_VGIC_V3   (1 << 1)

void kvm_arm_register_device(MemoryRegion *mr, uint64_t devid, uint64_t group,
                             uint64_t attr, int dev_fd, uint64_t addr_ormask);

bool write_list_to_kvmstate(ARMCPU *cpu, int level);

bool write_kvmstate_to_list(ARMCPU *cpu);

void kvm_arm_cpu_pre_save(ARMCPU *cpu);

bool kvm_arm_cpu_post_load(ARMCPU *cpu);

void kvm_arm_reset_vcpu(ARMCPU *cpu);

struct kvm_vcpu_init;
bool kvm_arm_create_scratch_host_vcpu(int *fdarray,
                                      struct kvm_vcpu_init *init);

void kvm_arm_destroy_scratch_host_vcpu(int *fdarray);

uint32_t kvm_arm_sve_get_vls(ARMCPU *cpu);

void kvm_arm_set_cpu_features_from_host(ARMCPU *cpu);

void kvm_arm_add_vcpu_properties(ARMCPU *cpu);

void kvm_arm_steal_time_finalize(ARMCPU *cpu, Error **errp);

#ifndef CONFIG_USER_ONLY
bool kvm_arm_aarch32_supported(void);

bool kvm_arm_pmu_supported(void);

bool kvm_arm_sve_supported(void);

bool kvm_arm_mte_supported(void);

bool kvm_arm_el2_supported(void);
#else

static inline bool kvm_arm_aarch32_supported(void)
{
    return false;
}

static inline bool kvm_arm_pmu_supported(void)
{
    return false;
}

static inline bool kvm_arm_sve_supported(void)
{
    return false;
}

static inline bool kvm_arm_mte_supported(void)
{
    return false;
}

static inline bool kvm_arm_el2_supported(void)
{
    return false;
}
#endif

int kvm_arm_get_max_vm_ipa_size(MachineState *ms, bool *fixed_ipa);

int kvm_arm_vgic_probe(void);

void kvm_arm_pmu_init(ARMCPU *cpu);
void kvm_arm_pmu_set_irq(ARMCPU *cpu, int irq);

void kvm_arm_pvtime_init(ARMCPU *cpu, uint64_t ipa);

int kvm_arm_set_irq(int cpu, int irqtype, int irq, int level);

void kvm_arm_enable_mte(Object *cpuobj, Error **errp);

void arm_cpu_kvm_set_irq(void *arm_cpu, int irq, int level);

#endif
