/*
 * CPU interfaces that are target independent.
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */
#ifndef CPU_COMMON_H
#define CPU_COMMON_H

#include "exec/vaddr.h"
#include "exec/hwaddr.h"
#include "hw/core/cpu.h"
#include "tcg/debug-assert.h"
#include "exec/page-protection.h"

#define EXCP_INTERRUPT  0x10000
#define EXCP_HLT        0x10001
#define EXCP_DEBUG      0x10002
#define EXCP_HALTED     0x10003
#define EXCP_YIELD      0x10004
#define EXCP_ATOMIC     0x10005

void cpu_exec_init_all(void);
void cpu_exec_step_atomic(CPUState *cpu);

#define REAL_HOST_PAGE_ALIGN(addr) ROUND_UP((addr), qemu_real_host_page_size())

extern QemuMutex qemu_cpu_list_lock;
void qemu_init_cpu_list(void);
void cpu_list_lock(void);
void cpu_list_unlock(void);
unsigned int cpu_list_generation_id_get(void);

int cpu_get_free_index(void);

void tcg_iommu_init_notifier_list(CPUState *cpu);
void tcg_iommu_free_notifier_list(CPUState *cpu);

enum device_endian {
    DEVICE_NATIVE_ENDIAN,
    DEVICE_BIG_ENDIAN,
    DEVICE_LITTLE_ENDIAN,
};

#if defined(CONFIG_XEN_BACKEND)
typedef uint64_t ram_addr_t;
#  define RAM_ADDR_MAX UINT64_MAX
#  define RAM_ADDR_FMT "%" PRIx64
#else
typedef uintptr_t ram_addr_t;
#  define RAM_ADDR_MAX UINTPTR_MAX
#  define RAM_ADDR_FMT "%" PRIxPTR
#endif

void qemu_ram_remap(ram_addr_t addr);
ram_addr_t qemu_ram_addr_from_host(void *ptr);
ram_addr_t qemu_ram_addr_from_host_nofail(void *ptr);
RAMBlock *qemu_ram_block_by_name(const char *name);

RAMBlock *qemu_ram_block_from_host(void *ptr, bool round_offset,
                                   ram_addr_t *offset);
ram_addr_t qemu_ram_block_host_offset(RAMBlock *rb, void *host);
void qemu_ram_set_idstr(RAMBlock *block, const char *name, DeviceState *dev);
void qemu_ram_unset_idstr(RAMBlock *block);
const char *qemu_ram_get_idstr(RAMBlock *rb);
void *qemu_ram_get_host_addr(RAMBlock *rb);
ram_addr_t qemu_ram_get_offset(RAMBlock *rb);
ram_addr_t qemu_ram_get_fd_offset(RAMBlock *rb);
ram_addr_t qemu_ram_get_used_length(RAMBlock *rb);
ram_addr_t qemu_ram_get_max_length(RAMBlock *rb);
bool qemu_ram_is_shared(RAMBlock *rb);
bool qemu_ram_is_noreserve(RAMBlock *rb);
bool qemu_ram_is_uf_zeroable(RAMBlock *rb);
void qemu_ram_set_uf_zeroable(RAMBlock *rb);
bool qemu_ram_is_migratable(RAMBlock *rb);
void qemu_ram_set_migratable(RAMBlock *rb);
void qemu_ram_unset_migratable(RAMBlock *rb);
bool qemu_ram_is_named_file(RAMBlock *rb);
int qemu_ram_get_fd(RAMBlock *rb);

size_t qemu_ram_pagesize(RAMBlock *block);
size_t qemu_ram_pagesize_largest(void);

void cpu_address_space_init(CPUState *cpu, int asidx,
                            const char *prefix, MemoryRegion *mr);
void cpu_destroy_address_spaces(CPUState *cpu);

void cpu_physical_memory_read(hwaddr addr, void *buf, hwaddr len);
void cpu_physical_memory_write(hwaddr addr, const void *buf, hwaddr len);
void *cpu_physical_memory_map(hwaddr addr,
                              hwaddr *plen,
                              bool is_write);
void cpu_physical_memory_unmap(void *buffer, hwaddr len,
                               bool is_write, hwaddr access_len);

void qemu_flush_coalesced_mmio_buffer(void);

typedef int (RAMBlockIterFunc)(RAMBlock *rb, void *opaque);

int qemu_ram_foreach_block(RAMBlockIterFunc func, void *opaque);

void list_cpus(void);

#ifdef CONFIG_TCG
#include "qemu/atomic.h"

bool cpu_unwind_state_data(CPUState *cpu, uintptr_t host_pc, uint64_t *data);

bool cpu_restore_state(CPUState *cpu, uintptr_t host_pc);

static inline bool cpu_loop_exit_requested(CPUState *cpu)
{
    return (int32_t)qatomic_read(&cpu->neg.icount_decr.u32) < 0;
}

G_NORETURN void cpu_loop_exit_noexc(CPUState *cpu);
G_NORETURN void cpu_loop_exit_atomic(CPUState *cpu, uintptr_t pc);
G_NORETURN void cpu_loop_exit_restore(CPUState *cpu, uintptr_t pc);
#endif
G_NORETURN void cpu_loop_exit(CPUState *cpu);

int cpu_exec(CPUState *cpu);

static inline ArchCPU *env_archcpu(CPUArchState *env)
{
    return (void *)env - sizeof(CPUState);
}

static inline const CPUState *env_cpu_const(const CPUArchState *env)
{
    return (void *)env - sizeof(CPUState);
}

static inline CPUState *env_cpu(CPUArchState *env)
{
    return (CPUState *)env_cpu_const(env);
}

#endif
