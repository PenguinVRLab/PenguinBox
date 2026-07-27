/*
 * user-internals.h: prototypes etc internal to the linux-user implementation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LINUX_USER_USER_INTERNALS_H
#define LINUX_USER_USER_INTERNALS_H

#include "user/thunk.h"
#include "qemu/log.h"
#include "exec/tb-flush.h"
#include "exec/translation-block.h"

extern char *exec_path;
void init_task_state(TaskState *ts);
void task_settid(TaskState *);
void stop_all_tasks(void);
extern const char *qemu_uname_release;
extern unsigned long mmap_min_addr;

typedef struct IOCTLEntry IOCTLEntry;

typedef abi_long do_ioctl_fn(const IOCTLEntry *ie, uint8_t *buf_temp,
                             int fd, int cmd, abi_long arg);

struct IOCTLEntry {
    int target_cmd;
    unsigned int host_cmd;
    const char *name;
    int access;
    do_ioctl_fn *do_ioctl;
    const argtype arg_type[5];
};

extern IOCTLEntry ioctl_entries[];

#define IOC_R 0x0001
#define IOC_W 0x0002
#define IOC_RW (IOC_R | IOC_W)

int info_is_fdpic(struct image_info *info);

void target_set_brk(abi_ulong new_brk);
void syscall_init(void);
abi_long do_syscall(CPUArchState *cpu_env, int num, abi_long arg1,
                    abi_long arg2, abi_long arg3, abi_long arg4,
                    abi_long arg5, abi_long arg6, abi_long arg7,
                    abi_long arg8);
extern __thread CPUState *thread_cpu;
abi_long get_errno(abi_long ret);
const char *target_strerror(int err);
int get_osversion(void);
void init_qemu_uname_release(void);
void fork_start(void);
void fork_end(pid_t pid);

void probe_guest_base(const char *image_name,
                      abi_ulong loaddr, abi_ulong hiaddr);

int host_to_target_waitstatus(int status);

#ifdef TARGET_I386
void save_v86_state(CPUX86State *env);
void handle_vm86_trap(CPUX86State *env, int trapno);
int do_vm86(CPUX86State *env, long subfunction, abi_ulong v86_addr);
#elif defined(TARGET_SPARC64)
void sparc64_set_context(CPUSPARCState *env);
void sparc64_get_context(CPUSPARCState *env);
#endif

static inline int is_error(abi_long ret)
{
    return (abi_ulong)ret >= (abi_ulong)(-4096);
}

#if (TARGET_ABI_BITS == 32) && !defined(TARGET_ABI_MIPSN32)
static inline uint64_t target_offset64(uint32_t word0, uint32_t word1)
{
#if TARGET_BIG_ENDIAN
    return ((uint64_t)word0 << 32) | word1;
#else
    return ((uint64_t)word1 << 32) | word0;
#endif
}
#else
static inline uint64_t target_offset64(uint64_t word0, uint64_t word1)
{
    return word0;
}
#endif

void print_termios(void *arg);

#ifdef TARGET_ARM
static inline int regpairs_aligned(CPUArchState *cpu_env, int num)
{
    return cpu_env->eabi;
}
#elif defined(TARGET_MIPS) && defined(TARGET_ABI_MIPSO32)
static inline int regpairs_aligned(CPUArchState *cpu_env, int num) { return 1; }
#elif defined(TARGET_PPC) && !defined(TARGET_PPC64)
static inline int regpairs_aligned(CPUArchState *cpu_env, int num) { return 1; }
#elif defined(TARGET_SH4)
static inline int regpairs_aligned(CPUArchState *cpu_env, int num)
{
    switch (num) {
    case TARGET_NR_pread64:
    case TARGET_NR_pwrite64:
        return 1;

    default:
        return 0;
    }
}
#elif defined(TARGET_XTENSA)
static inline int regpairs_aligned(CPUArchState *cpu_env, int num) { return 1; }
#elif defined(TARGET_HEXAGON)
static inline int regpairs_aligned(CPUArchState *cpu_env, int num) { return 1; }
#else
static inline int regpairs_aligned(CPUArchState *cpu_env, int num) { return 0; }
#endif

void preexit_cleanup(CPUArchState *env, int code);

static inline void begin_parallel_context(CPUState *cs)
{
    if (!tcg_cflags_has(cs, CF_PARALLEL)) {
        tb_flush__exclusive_or_serial();
        tcg_cflags_set(cs, CF_PARALLEL);
    }
}

#include "target_cpu.h"
#include "target_structs.h"

#endif
