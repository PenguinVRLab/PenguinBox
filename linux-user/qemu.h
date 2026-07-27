#ifndef QEMU_H
#define QEMU_H

#include "cpu.h"
#include "accel/tcg/cpu-ldst.h"

#include "user/abitypes.h"
#include "user/page-protection.h"

#include "syscall_defs.h"
#include "target_syscall.h"
#include "accel/tcg/vcpu-state.h"

#define SIGSET_T_SIZE (_NSIG / 8)

struct image_info {
        abi_ulong       load_bias;
        abi_ulong       load_addr;
        abi_ulong       start_code;
        abi_ulong       end_code;
        abi_ulong       start_data;
        abi_ulong       end_data;
        abi_ulong       brk;
        abi_ulong       start_stack;
        abi_ulong       stack_limit;
        abi_ulong       vdso;
        abi_ulong       entry;
        abi_ulong       code_offset;
        abi_ulong       data_offset;
        abi_ulong       saved_auxv;
        abi_ulong       auxv_len;
        abi_ulong       argc;
        abi_ulong       argv;
        abi_ulong       envc;
        abi_ulong       envp;
        abi_ulong       file_string;
        uint32_t        elf_flags;
        int             personality;
        bool            exec_stack;

        abi_ulong       arg_strings;
        abi_ulong       env_strings;

        abi_ulong       loadmap_addr;
        uint16_t        nsegs;
        void            *loadsegs;
        abi_ulong       pt_dynamic_addr;
        abi_ulong       interpreter_loadmap_addr;
        abi_ulong       interpreter_pt_dynamic_addr;
        struct image_info *other_info;

        uint32_t        note_flags;

#ifdef TARGET_MIPS
        int             fp_abi;
        int             interp_fp_abi;
#endif
};

#ifdef TARGET_I386
struct vm86_saved_state {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t eflags;
    uint32_t eip;
    uint16_t cs, ss, ds, es, fs, gs;
};
#endif

#if defined(TARGET_ARM) && defined(TARGET_ABI32)
#include "nwfpe/fpa11.h"
#endif

struct emulated_sigtable {
    int pending;
    target_siginfo_t info;
};

struct TaskState {
    pid_t ts_tid;
#ifdef TARGET_ARM
# ifdef TARGET_ABI32
    FPA11 fpa;
# endif
#endif
#if defined(TARGET_ARM) || defined(TARGET_RISCV)
    int swi_errno;
#endif
#if defined(TARGET_I386) && !defined(TARGET_X86_64)
    abi_ulong target_v86;
    struct vm86_saved_state vm86_saved_regs;
    struct target_vm86plus_struct vm86plus;
    uint32_t v86flags;
    uint32_t v86mask;
#endif
#if defined(TARGET_I386)
    target_ulong orig_ax;
#endif
    abi_ulong child_tidptr;
#ifdef TARGET_M68K
    abi_ulong tp_value;
#endif
#if defined(TARGET_AARCH64)
    vaddr gcs_base;
    abi_ulong gcs_size;
    abi_ulong gcs_el0_locked;
#endif
    int used;
    struct image_info *info;
    struct linux_binprm *bprm;

    struct emulated_sigtable sync_signal;
    struct emulated_sigtable sigtab[TARGET_NSIG];
    sigset_t signal_mask;
    sigset_t sigsuspend_mask;
    int in_sigsuspend;

    int signal_pending;

    struct target_sigaltstack sigaltstack_used;

    vaddr sys_dispatch;
    vaddr sys_dispatch_selector;
    abi_ulong sys_dispatch_len;

    uint64_t start_boottime;
};

abi_long do_brk(abi_ulong new_brk);
int do_guest_openat(CPUArchState *cpu_env, int dirfd, const char *pathname,
                    int flags, mode_t mode, bool safe);
ssize_t do_guest_readlink(const char *pathname, char *buf, size_t bufsiz);

#define VERIFY_NONE  0
#define VERIFY_READ  PAGE_READ
#define VERIFY_WRITE (PAGE_READ | PAGE_WRITE)

static inline bool access_ok_untagged(int type, abi_ulong addr, abi_ulong size)
{
    if (size == 0
        ? !guest_addr_valid_untagged(addr)
        : !guest_range_valid_untagged(addr, size)) {
        return false;
    }
    return page_check_range((target_ulong)addr, size, type);
}

static inline bool access_ok(CPUState *cpu, int type,
                             abi_ulong addr, abi_ulong size)
{
    return access_ok_untagged(type, cpu_untagged_addr(cpu, addr), size);
}

#if defined(__clang__) && __has_warning("-Waddress-of-packed-member")
#define PRAGMA_DISABLE_PACKED_WARNING                                   \
    _Pragma("GCC diagnostic push");                                     \
    _Pragma("GCC diagnostic ignored \"-Waddress-of-packed-member\"")

#define PRAGMA_REENABLE_PACKED_WARNING          \
    _Pragma("GCC diagnostic pop")

#else
#define PRAGMA_DISABLE_PACKED_WARNING
#define PRAGMA_REENABLE_PACKED_WARNING
#endif

#define __put_user_e(x, hptr, e)                                            \
    do {                                                                    \
        PRAGMA_DISABLE_PACKED_WARNING;                                      \
        (__builtin_choose_expr(sizeof(*(hptr)) == 1, stb_p,                 \
        __builtin_choose_expr(sizeof(*(hptr)) == 2, stw_##e##_p,            \
        __builtin_choose_expr(sizeof(*(hptr)) == 4, stl_##e##_p,            \
        __builtin_choose_expr(sizeof(*(hptr)) == 8, stq_##e##_p, abort))))  \
            ((hptr), (x)), (void)0);                                        \
        PRAGMA_REENABLE_PACKED_WARNING;                                     \
    } while (0)

#define __get_user_e(x, hptr, e)                                            \
    do {                                                                    \
        PRAGMA_DISABLE_PACKED_WARNING;                                      \
        ((x) = (typeof(*hptr))(                                             \
        __builtin_choose_expr(sizeof(*(hptr)) == 1, ldub_p,                 \
        __builtin_choose_expr(sizeof(*(hptr)) == 2, lduw_##e##_p,           \
        __builtin_choose_expr(sizeof(*(hptr)) == 4, ldl_##e##_p,            \
        __builtin_choose_expr(sizeof(*(hptr)) == 8, ldq_##e##_p, abort))))  \
            (hptr)), (void)0);                                              \
        PRAGMA_REENABLE_PACKED_WARNING;                                     \
    } while (0)


#if TARGET_BIG_ENDIAN
# define __put_user(x, hptr)  __put_user_e(x, hptr, be)
# define __get_user(x, hptr)  __get_user_e(x, hptr, be)
#else
# define __put_user(x, hptr)  __put_user_e(x, hptr, le)
# define __get_user(x, hptr)  __get_user_e(x, hptr, le)
#endif

#define put_user(x, gaddr, target_type)					\
({									\
    abi_ulong __gaddr = (gaddr);					\
    target_type *__hptr;						\
    abi_long __ret = 0;							\
    if ((__hptr = lock_user(VERIFY_WRITE, __gaddr, sizeof(target_type), 0))) { \
        __put_user((x), __hptr);				\
        unlock_user(__hptr, __gaddr, sizeof(target_type));		\
    } else								\
        __ret = -TARGET_EFAULT;						\
    __ret;								\
})

#define get_user(x, gaddr, target_type)					\
({									\
    abi_ulong __gaddr = (gaddr);					\
    target_type *__hptr;						\
    abi_long __ret = 0;							\
    if ((__hptr = lock_user(VERIFY_READ, __gaddr, sizeof(target_type), 1))) { \
        __get_user((x), __hptr);				\
        unlock_user(__hptr, __gaddr, 0);				\
    } else {								\
						\
        (x) = 0;							\
        __ret = -TARGET_EFAULT;						\
    }									\
    __ret;								\
})

#define put_user_ual(x, gaddr) put_user((x), (gaddr), abi_ulong)
#define put_user_sal(x, gaddr) put_user((x), (gaddr), abi_long)
#define put_user_u64(x, gaddr) put_user((x), (gaddr), uint64_t)
#define put_user_s64(x, gaddr) put_user((x), (gaddr), int64_t)
#define put_user_u32(x, gaddr) put_user((x), (gaddr), uint32_t)
#define put_user_s32(x, gaddr) put_user((x), (gaddr), int32_t)
#define put_user_u16(x, gaddr) put_user((x), (gaddr), uint16_t)
#define put_user_s16(x, gaddr) put_user((x), (gaddr), int16_t)
#define put_user_u8(x, gaddr)  put_user((x), (gaddr), uint8_t)
#define put_user_s8(x, gaddr)  put_user((x), (gaddr), int8_t)

#define get_user_ual(x, gaddr) get_user((x), (gaddr), abi_ulong)
#define get_user_sal(x, gaddr) get_user((x), (gaddr), abi_long)
#define get_user_u64(x, gaddr) get_user((x), (gaddr), uint64_t)
#define get_user_s64(x, gaddr) get_user((x), (gaddr), int64_t)
#define get_user_u32(x, gaddr) get_user((x), (gaddr), uint32_t)
#define get_user_s32(x, gaddr) get_user((x), (gaddr), int32_t)
#define get_user_u16(x, gaddr) get_user((x), (gaddr), uint16_t)
#define get_user_s16(x, gaddr) get_user((x), (gaddr), int16_t)
#define get_user_u8(x, gaddr)  get_user((x), (gaddr), uint8_t)
#define get_user_s8(x, gaddr)  get_user((x), (gaddr), int8_t)

int copy_from_user(void *hptr, abi_ulong gaddr, ssize_t len);
int copy_to_user(abi_ulong gaddr, void *hptr, ssize_t len);

int copy_struct_from_user(void *dst, size_t ksize, abi_ptr src, size_t usize);

void *lock_user(int type, abi_ulong guest_addr, ssize_t len, bool copy);

#ifndef CONFIG_DEBUG_REMAP
static inline void unlock_user(void *host_ptr, abi_ulong guest_addr,
                               ssize_t len)
{
}
#else
void unlock_user(void *host_ptr, abi_ulong guest_addr, ssize_t len);
#endif

ssize_t target_strlen(abi_ulong gaddr);

void *lock_user_string(abi_ulong guest_addr);

#define lock_user_struct(type, host_ptr, guest_addr, copy)	\
    (host_ptr = lock_user(type, guest_addr, sizeof(*host_ptr), copy))
#define unlock_user_struct(host_ptr, guest_addr, copy)		\
    unlock_user(host_ptr, guest_addr, (copy) ? sizeof(*host_ptr) : 0)

CPUArchState *cpu_copy(CPUArchState *env);

void init_main_thread(CPUState *cs, struct image_info *info);

#endif
