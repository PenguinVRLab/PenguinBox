/*
 * QEMU KVM support
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

/* header to be included in non-KVM-specific code */

#ifndef QEMU_KVM_H
#define QEMU_KVM_H

#include "exec/memattrs.h"
#include "qemu/accel.h"
#include "qom/object.h"

#ifdef COMPILING_PER_TARGET
# ifdef CONFIG_KVM
#  include <linux/kvm.h>
#  define CONFIG_KVM_IS_POSSIBLE
# endif
#else
# define CONFIG_KVM_IS_POSSIBLE
#endif

#ifdef CONFIG_KVM_IS_POSSIBLE

extern bool kvm_allowed;
extern bool kvm_kernel_irqchip;
extern bool kvm_split_irqchip;
extern bool kvm_async_interrupts_allowed;
extern bool kvm_halt_in_kernel_allowed;
extern bool kvm_resamplefds_allowed;
extern bool kvm_msi_via_irqfd_allowed;
extern bool kvm_gsi_routing_allowed;
extern bool kvm_gsi_direct_mapping;
extern bool kvm_readonly_mem_allowed;
extern bool kvm_msi_use_devid;
extern bool kvm_pre_fault_memory_supported;

#define kvm_enabled()           (kvm_allowed)
#define kvm_irqchip_in_kernel() (kvm_kernel_irqchip)

#define kvm_irqchip_is_split() (kvm_split_irqchip)

#define kvm_async_interrupts_enabled() (kvm_async_interrupts_allowed)

#define kvm_halt_in_kernel() (kvm_halt_in_kernel_allowed)

#define kvm_irqfds_enabled() kvm_irqchip_in_kernel()

#define kvm_resamplefds_enabled() (kvm_resamplefds_allowed)

#define kvm_msi_via_irqfd_enabled() (kvm_msi_via_irqfd_allowed)

#define kvm_gsi_routing_enabled() (kvm_gsi_routing_allowed)

#define kvm_gsi_direct_mapping() (kvm_gsi_direct_mapping)

#define kvm_readonly_mem_enabled() (kvm_readonly_mem_allowed)

#define kvm_msi_devid_required() (kvm_msi_use_devid)

#else

#define kvm_enabled()           (0)
#define kvm_irqchip_in_kernel() (false)
#define kvm_irqchip_is_split() (false)
#define kvm_async_interrupts_enabled() (false)
#define kvm_halt_in_kernel() (false)
#define kvm_irqfds_enabled() (false)
#define kvm_resamplefds_enabled() (false)
#define kvm_msi_via_irqfd_enabled() (false)
#define kvm_gsi_routing_allowed() (false)
#define kvm_gsi_direct_mapping() (false)
#define kvm_readonly_mem_enabled() (false)
#define kvm_msi_devid_required() (false)

#endif

struct kvm_run;
struct kvm_irq_routing_entry;

typedef struct KVMCapabilityInfo {
    const char *name;
    int value;
} KVMCapabilityInfo;

#define KVM_CAP_INFO(CAP) { "KVM_CAP_" stringify(CAP), KVM_CAP_##CAP }
#define KVM_CAP_LAST_INFO { NULL, 0 }

struct KVMState;

#define TYPE_KVM_ACCEL ACCEL_CLASS_NAME("kvm")
typedef struct KVMState KVMState;
DECLARE_INSTANCE_CHECKER(KVMState, KVM_STATE,
                         TYPE_KVM_ACCEL)

extern KVMState *kvm_state;
typedef struct Notifier Notifier;

typedef struct KVMRouteChange {
     KVMState *s;
     int changes;
} KVMRouteChange;

unsigned int kvm_get_max_memslots(void);
unsigned int kvm_get_free_memslots(void);
bool kvm_has_sync_mmu(void);
int kvm_has_vcpu_events(void);
int kvm_max_nested_state_length(void);
int kvm_has_gsi_routing(void);
void kvm_close(void);

bool kvm_arm_supports_user_irq(void);


int kvm_on_sigbus_vcpu(CPUState *cpu, int code, void *addr);
int kvm_on_sigbus(int code, void *addr);

int kvm_check_extension(KVMState *s, unsigned int extension);

int kvm_vm_ioctl(KVMState *s, unsigned long type, ...);

void kvm_flush_coalesced_mmio_buffer(void);

#ifdef COMPILING_PER_TARGET
#include "cpu.h"

#ifdef TARGET_KVM_HAVE_GUEST_DEBUG
int kvm_update_guest_debug(CPUState *cpu, unsigned long reinject_trap);
#else
static inline int kvm_update_guest_debug(CPUState *cpu, unsigned long reinject_trap)
{
    return -EINVAL;
}
#endif

int kvm_ioctl(KVMState *s, unsigned long type, ...);

int kvm_vcpu_ioctl(CPUState *cpu, unsigned long type, ...);

int kvm_device_ioctl(int fd, unsigned long type, ...);

int kvm_vm_check_attr(KVMState *s, uint32_t group, uint64_t attr);

int kvm_device_check_attr(int fd, uint32_t group, uint64_t attr);

int kvm_device_access(int fd, int group, uint64_t attr,
                      void *val, bool write, Error **errp);

int kvm_create_device(KVMState *s, uint64_t type, bool test);

bool kvm_device_supported(int vmfd, uint64_t type);

int kvm_create_and_park_vcpu(CPUState *cpu);

extern const KVMCapabilityInfo kvm_arch_required_capabilities[];

void kvm_arch_accel_class_init(ObjectClass *oc);

void kvm_arch_pre_run(CPUState *cpu, struct kvm_run *run);
MemTxAttrs kvm_arch_post_run(CPUState *cpu, struct kvm_run *run);

int kvm_arch_handle_exit(CPUState *cpu, struct kvm_run *run);

int kvm_arch_process_async_events(CPUState *cpu);

int kvm_arch_get_registers(CPUState *cpu, Error **errp);

typedef enum kvm_put_state {
    KVM_PUT_RUNTIME_STATE = 1,
    KVM_PUT_RESET_STATE = 2,
    KVM_PUT_FULL_STATE = 3,
} KvmPutState;

int kvm_arch_put_registers(CPUState *cpu, KvmPutState level, Error **errp);

int kvm_arch_get_default_type(MachineState *ms);

int kvm_arch_init(MachineState *ms, KVMState *s);

int kvm_arch_pre_create_vcpu(CPUState *cpu, Error **errp);
int kvm_arch_init_vcpu(CPUState *cpu);
int kvm_arch_destroy_vcpu(CPUState *cpu);

#ifdef TARGET_KVM_HAVE_RESET_PARKED_VCPU
void kvm_arch_reset_parked_vcpu(unsigned long vcpu_id, int kvm_fd);
#else
static inline void kvm_arch_reset_parked_vcpu(unsigned long vcpu_id, int kvm_fd)
{
}
#endif

bool kvm_vcpu_id_is_valid(int vcpu_id);

unsigned long kvm_arch_vcpu_id(CPUState *cpu);

void kvm_arch_on_sigbus_vcpu(CPUState *cpu, int code, void *addr);

void kvm_arch_init_irq_routing(KVMState *s);

int kvm_arch_fixup_msi_route(struct kvm_irq_routing_entry *route,
                             uint64_t address, uint32_t data, PCIDevice *dev);

int kvm_arch_add_msi_route_post(struct kvm_irq_routing_entry *route,
                                int vector, PCIDevice *dev);
int kvm_arch_release_virq_post(int virq);

int kvm_arch_msi_data_to_gsi(uint32_t data);

int kvm_set_irq(KVMState *s, int irq, int level);
int kvm_irqchip_send_msi(KVMState *s, MSIMessage msg);

void kvm_irqchip_add_irq_route(KVMState *s, int gsi, int irqchip, int pin);

void kvm_irqchip_add_change_notifier(Notifier *n);
void kvm_irqchip_remove_change_notifier(Notifier *n);
void kvm_irqchip_change_notify(void);

struct kvm_guest_debug;
struct kvm_debug_exit_arch;

struct kvm_sw_breakpoint {
    vaddr pc;
    vaddr saved_insn;
    int use_count;
    QTAILQ_ENTRY(kvm_sw_breakpoint) entry;
};

struct kvm_sw_breakpoint *kvm_find_sw_breakpoint(CPUState *cpu,
                                                 vaddr pc);

int kvm_sw_breakpoints_active(CPUState *cpu);

int kvm_arch_insert_sw_breakpoint(CPUState *cpu,
                                  struct kvm_sw_breakpoint *bp);
int kvm_arch_remove_sw_breakpoint(CPUState *cpu,
                                  struct kvm_sw_breakpoint *bp);
int kvm_arch_insert_hw_breakpoint(vaddr addr, vaddr len, int type);
int kvm_arch_remove_hw_breakpoint(vaddr addr, vaddr len, int type);
void kvm_arch_remove_all_hw_breakpoints(void);

void kvm_arch_update_guest_debug(CPUState *cpu, struct kvm_guest_debug *dbg);

bool kvm_arch_stop_on_emulation_error(CPUState *cpu);

int kvm_vm_check_extension(KVMState *s, unsigned int extension);

#define kvm_vm_enable_cap(s, capability, cap_flags, ...)             \
    ({                                                               \
        struct kvm_enable_cap cap = {                                \
            .cap = capability,                                       \
            .flags = cap_flags,                                      \
        };                                                           \
        uint64_t args_tmp[] = { __VA_ARGS__ };                       \
        size_t n = MIN(ARRAY_SIZE(args_tmp), ARRAY_SIZE(cap.args));  \
        memcpy(cap.args, args_tmp, n * sizeof(cap.args[0]));         \
        kvm_vm_ioctl(s, KVM_ENABLE_CAP, &cap);                       \
    })

#define kvm_vcpu_enable_cap(cpu, capability, cap_flags, ...)         \
    ({                                                               \
        struct kvm_enable_cap cap = {                                \
            .cap = capability,                                       \
            .flags = cap_flags,                                      \
        };                                                           \
        uint64_t args_tmp[] = { __VA_ARGS__ };                       \
        size_t n = MIN(ARRAY_SIZE(args_tmp), ARRAY_SIZE(cap.args));  \
        memcpy(cap.args, args_tmp, n * sizeof(cap.args[0]));         \
        kvm_vcpu_ioctl(cpu, KVM_ENABLE_CAP, &cap);                   \
    })

void kvm_set_sigmask_len(KVMState *s, unsigned int sigmask_len);

int kvm_physical_memory_addr_from_host(KVMState *s, void *ram_addr,
                                       hwaddr *phys_addr);

#endif

void kvm_cpu_synchronize_state(CPUState *cpu);

void kvm_init_cpu_signals(CPUState *cpu);

int kvm_irqchip_add_msi_route(KVMRouteChange *c, int vector, PCIDevice *dev);
int kvm_irqchip_update_msi_route(KVMState *s, int virq, MSIMessage msg,
                                 PCIDevice *dev);
void kvm_irqchip_commit_routes(KVMState *s);

static inline KVMRouteChange kvm_irqchip_begin_route_changes(KVMState *s)
{
    return (KVMRouteChange) { .s = s, .changes = 0 };
}

static inline void kvm_irqchip_commit_route_changes(KVMRouteChange *c)
{
    if (c->changes) {
        kvm_irqchip_commit_routes(c->s);
        c->changes = 0;
    }
}

int kvm_irqchip_get_virq(KVMState *s);
void kvm_irqchip_release_virq(KVMState *s, int virq);

void kvm_add_routing_entry(KVMState *s,
                           struct kvm_irq_routing_entry *entry);

int kvm_irqchip_add_irqfd_notifier_gsi(KVMState *s, EventNotifier *n,
                                       EventNotifier *rn, int virq);
int kvm_irqchip_remove_irqfd_notifier_gsi(KVMState *s, EventNotifier *n,
                                          int virq);
int kvm_irqchip_add_irqfd_notifier(KVMState *s, EventNotifier *n,
                                   EventNotifier *rn, qemu_irq irq);
int kvm_irqchip_remove_irqfd_notifier(KVMState *s, EventNotifier *n,
                                      qemu_irq irq);
void kvm_irqchip_set_qemuirq_gsi(KVMState *s, qemu_irq irq, int gsi);
void kvm_init_irq_routing(KVMState *s);

bool kvm_kernel_irqchip_allowed(void);
bool kvm_kernel_irqchip_required(void);
bool kvm_kernel_irqchip_split(void);

int kvm_arch_irqchip_create(KVMState *s);

int kvm_set_one_reg(CPUState *cs, uint64_t id, void *source);

int kvm_get_one_reg(CPUState *cs, uint64_t id, void *target);

void kvm_resample_fd_notify(int gsi);

bool kvm_dirty_ring_enabled(void);

uint32_t kvm_dirty_ring_size(void);

void kvm_mark_guest_state_protected(void);

bool kvm_hwpoisoned_mem(void);

int kvm_create_guest_memfd(uint64_t size, uint64_t flags, Error **errp);

int kvm_set_memory_attributes_private(hwaddr start, uint64_t size);
int kvm_set_memory_attributes_shared(hwaddr start, uint64_t size);

int kvm_convert_memory(hwaddr start, hwaddr size, bool to_private);

#endif
