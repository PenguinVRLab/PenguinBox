/*
 *  Semihosting support for systems modeled on the Arm "Angel"
 *  semihosting syscalls design. This includes Arm and RISC-V processors
 *
 *  Copyright (c) 2005, 2007 CodeSourcery.
 *  Copyright (c) 2019 Linaro
 *  Written by Paul Brook.
 *
 *  Copyright © 2020 by Keith Packard <keithp@keithp.com>
 *  Adapted for systems other than ARM, including RISC-V, by Keith Packard
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
 *
 *  ARM Semihosting is documented in:
 *     Semihosting for AArch32 and AArch64 Release 2.0
 *     https://github.com/ARM-software/abi-aa/blob/main/semihosting/semihosting.rst
 *
 *  RISC-V Semihosting is documented in:
 *     RISC-V Semihosting
 *     https://github.com/riscv/riscv-semihosting-spec/blob/main/riscv-semihosting-spec.adoc
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "exec/gdbstub.h"
#include "gdbstub/syscalls.h"
#include "semihosting/semihost.h"
#include "semihosting/console.h"
#include "semihosting/common-semi.h"
#include "semihosting/guestfd.h"
#include "semihosting/syscalls.h"

#ifdef CONFIG_USER_ONLY
#include "qemu.h"

#define COMMON_SEMI_HEAP_SIZE (128 * 1024 * 1024)
#else
#include "qemu/cutils.h"
#include "hw/loader.h"
#include "hw/boards.h"
#endif

#define TARGET_SYS_OPEN        0x01
#define TARGET_SYS_CLOSE       0x02
#define TARGET_SYS_WRITEC      0x03
#define TARGET_SYS_WRITE0      0x04
#define TARGET_SYS_WRITE       0x05
#define TARGET_SYS_READ        0x06
#define TARGET_SYS_READC       0x07
#define TARGET_SYS_ISERROR     0x08
#define TARGET_SYS_ISTTY       0x09
#define TARGET_SYS_SEEK        0x0a
#define TARGET_SYS_FLEN        0x0c
#define TARGET_SYS_TMPNAM      0x0d
#define TARGET_SYS_REMOVE      0x0e
#define TARGET_SYS_RENAME      0x0f
#define TARGET_SYS_CLOCK       0x10
#define TARGET_SYS_TIME        0x11
#define TARGET_SYS_SYSTEM      0x12
#define TARGET_SYS_ERRNO       0x13
#define TARGET_SYS_GET_CMDLINE 0x15
#define TARGET_SYS_HEAPINFO    0x16
#define TARGET_SYS_EXIT        0x18
#define TARGET_SYS_SYNCCACHE   0x19
#define TARGET_SYS_EXIT_EXTENDED 0x20
#define TARGET_SYS_ELAPSED     0x30
#define TARGET_SYS_TICKFREQ    0x31

#define ADP_Stopped_ApplicationExit     (0x20026)

#ifndef O_BINARY
#define O_BINARY 0
#endif

static int gdb_open_modeflags[12] = {
    GDB_O_RDONLY,
    GDB_O_RDONLY,
    GDB_O_RDWR,
    GDB_O_RDWR,
    GDB_O_WRONLY | GDB_O_CREAT | GDB_O_TRUNC,
    GDB_O_WRONLY | GDB_O_CREAT | GDB_O_TRUNC,
    GDB_O_RDWR | GDB_O_CREAT | GDB_O_TRUNC,
    GDB_O_RDWR | GDB_O_CREAT | GDB_O_TRUNC,
    GDB_O_WRONLY | GDB_O_CREAT | GDB_O_APPEND,
    GDB_O_WRONLY | GDB_O_CREAT | GDB_O_APPEND,
    GDB_O_RDWR | GDB_O_CREAT | GDB_O_APPEND,
    GDB_O_RDWR | GDB_O_CREAT | GDB_O_APPEND,
};

static GuestFD console_in_gf;
static GuestFD console_out_gf;

#ifndef CONFIG_USER_ONLY

typedef struct LayoutInfo {
    vaddr rambase;
    size_t ramsize;
    hwaddr heapbase;
    hwaddr heaplimit;
} LayoutInfo;

static bool find_ram_cb(Int128 start, Int128 len, const MemoryRegion *mr,
                        hwaddr offset_in_region, void *opaque)
{
    LayoutInfo *info = (LayoutInfo *) opaque;
    uint64_t size = int128_get64(len);

    if (!mr->ram || mr->readonly) {
        return false;
    }

    if (size > info->ramsize) {
        info->rambase = int128_get64(start);
        info->ramsize = size;
    }

    return false;
}

static LayoutInfo common_semi_find_bases(CPUState *cs)
{
    FlatView *fv;
    LayoutInfo info = { 0, 0, 0, 0 };

    RCU_READ_LOCK_GUARD();

    fv = address_space_to_flatview(cs->as);
    flatview_for_each_range(fv, find_ram_cb, &info);

    if (info.rambase || info.ramsize > 0) {
        RomGap gap = rom_find_largest_gap_between(info.rambase, info.ramsize);
        info.heapbase = gap.base;
        info.heaplimit = gap.base + gap.size;
    }

    return info;
}

#endif

#include "semihosting/common-semi.h"

#define GET_ARG(n) do {                                 \
    if (is_64bit_semihosting(env)) {                    \
        if (get_user_u64(arg ## n, args + (n) * 8)) {   \
            goto do_fault;                              \
        }                                               \
    } else {                                            \
        if (get_user_u32(arg ## n, args + (n) * 4)) {   \
            goto do_fault;                              \
        }                                               \
    }                                                   \
} while (0)

#define SET_ARG(n, val)                                 \
    (is_64bit_semihosting(env) ?                        \
     put_user_u64(val, args + (n) * 8) :                \
     put_user_u32(val, args + (n) * 4))


#ifndef CONFIG_USER_ONLY
static uint64_t syscall_err;

#include "semihosting/uaccess.h"
#endif

static inline uint32_t get_swi_errno(CPUState *cs)
{
#ifdef CONFIG_USER_ONLY
    TaskState *ts = get_task_state(cs);

    return ts->swi_errno;
#else
    return syscall_err;
#endif
}

static void common_semi_cb(CPUState *cs, uint64_t ret, int err)
{
    if (err) {
#ifdef CONFIG_USER_ONLY
        TaskState *ts = get_task_state(cs);
        ts->swi_errno = err;
#else
        syscall_err = err;
#endif
    }
    common_semi_set_ret(cs, ret);
}

static void common_semi_dead_cb(CPUState *cs, uint64_t ret, int err)
{
    common_semi_set_ret(cs, 0xdeadbeef);
}

static void common_semi_rw_cb(CPUState *cs, uint64_t ret, int err)
{
    CPUArchState *env G_GNUC_UNUSED = cpu_env(cs);
    uint64_t args = common_semi_arg(cs, 1);
    uint64_t arg2;
    GET_ARG(2);

    if (err) {
 do_fault:
        ret = 0;
    }
    common_semi_set_ret(cs, arg2 - ret);
}

static void common_semi_istty_cb(CPUState *cs, uint64_t ret, int err)
{
    if (err) {
        ret = (err == ENOTTY ? 0 : -1);
    }
    common_semi_cb(cs, ret, err);
}

static void common_semi_seek_cb(CPUState *cs, uint64_t ret, int err)
{
    if (!err) {
        ret = 0;
    }
    common_semi_cb(cs, ret, err);
}

static uint64_t common_semi_flen_buf(CPUState *cs)
{
    vaddr sp = common_semi_stack_bottom(cs);
    return sp - 64;
}

static void
common_semi_flen_fstat_cb(CPUState *cs, uint64_t ret, int err)
{
    if (!err) {
        uint64_t size;
        if (cpu_memory_rw_debug(cs, common_semi_flen_buf(cs) +
                                offsetof(struct gdb_stat, gdb_st_size),
                                &size, 8, 0)) {
            ret = -1, err = EFAULT;
        } else {
            ret = be64_to_cpu(size);
        }
    }
    common_semi_cb(cs, ret, err);
}

static void
common_semi_readc_cb(CPUState *cs, uint64_t ret, int err)
{
    if (!err) {
        CPUArchState *env G_GNUC_UNUSED = cpu_env(cs);
        uint8_t ch;

        if (get_user_u8(ch, common_semi_stack_bottom(cs) - 1)) {
            ret = -1, err = EFAULT;
        } else {
            ret = ch;
        }
    }
    common_semi_cb(cs, ret, err);
}

#define SHFB_MAGIC_0 0x53
#define SHFB_MAGIC_1 0x48
#define SHFB_MAGIC_2 0x46
#define SHFB_MAGIC_3 0x42

#define SH_EXT_EXIT_EXTENDED (1 << 0)
#define SH_EXT_STDOUT_STDERR (1 << 1)

static const uint8_t featurefile_data[] = {
    SHFB_MAGIC_0,
    SHFB_MAGIC_1,
    SHFB_MAGIC_2,
    SHFB_MAGIC_3,
    SH_EXT_EXIT_EXTENDED | SH_EXT_STDOUT_STDERR,
};

bool semihosting_arm_compatible(void)
{
    return true;
}

void semihosting_arm_compatible_init(void)
{
    if (use_gdb_syscalls()) {
        console_in_gf.type = GuestFDGDB;
        console_in_gf.hostfd = 0;
        console_out_gf.type = GuestFDGDB;
        console_out_gf.hostfd = 2;
    } else {
        console_in_gf.type = GuestFDConsole;
        console_out_gf.type = GuestFDConsole;
    }
}

void do_common_semihosting(CPUState *cs)
{
    CPUArchState *env = cpu_env(cs);
    uint64_t args;
    uint64_t arg0, arg1, arg2, arg3;
    uint64_t ul_ret;
    char * s;
    int nr;
    int64_t elapsed;

    nr = common_semi_arg(cs, 0) & 0xffffffffU;
    args = common_semi_arg(cs, 1);

    switch (nr) {
    case TARGET_SYS_OPEN:
    {
        int ret, err = 0;
        int hostfd;

        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        s = lock_user_string(arg0);
        if (!s) {
            goto do_fault;
        }
        if (arg1 >= 12) {
            unlock_user(s, arg0, 0);
            common_semi_cb(cs, -1, EINVAL);
            break;
        }

        if (strcmp(s, ":tt") == 0) {
            if (arg1 < 4) {
                hostfd = STDIN_FILENO;
            } else if (arg1 < 8) {
                hostfd = STDOUT_FILENO;
            } else {
                hostfd = STDERR_FILENO;
            }
            ret = alloc_guestfd();
            associate_guestfd(ret, hostfd);
        } else if (strcmp(s, ":semihosting-features") == 0) {
            if (arg1 != 0 && arg1 != 1) {
                ret = -1;
                err = EACCES;
            } else {
                ret = alloc_guestfd();
                staticfile_guestfd(ret, featurefile_data,
                                   sizeof(featurefile_data));
            }
        } else {
            unlock_user(s, arg0, 0);
            semihost_sys_open(cs, common_semi_cb, arg0, arg2 + 1,
                              gdb_open_modeflags[arg1], 0644);
            break;
        }
        unlock_user(s, arg0, 0);
        common_semi_cb(cs, ret, err);
        break;
    }

    case TARGET_SYS_CLOSE:
        GET_ARG(0);
        semihost_sys_close(cs, common_semi_cb, arg0);
        break;

    case TARGET_SYS_WRITEC:
        semihost_sys_write_gf(cs, common_semi_dead_cb,
                              &console_out_gf, args, 1);
        break;

    case TARGET_SYS_WRITE0:
        {
            ssize_t len = target_strlen(args);
            if (len < 0) {
                common_semi_dead_cb(cs, -1, EFAULT);
            } else {
                semihost_sys_write_gf(cs, common_semi_dead_cb,
                                      &console_out_gf, args, len);
            }
        }
        break;

    case TARGET_SYS_WRITE:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        semihost_sys_write(cs, common_semi_rw_cb, arg0, arg1, arg2);
        break;

    case TARGET_SYS_READ:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        semihost_sys_read(cs, common_semi_rw_cb, arg0, arg1, arg2);
        break;

    case TARGET_SYS_READC:
        semihost_sys_read_gf(cs, common_semi_readc_cb, &console_in_gf,
                             common_semi_stack_bottom(cs) - 1, 1);
        break;

    case TARGET_SYS_ISERROR:
    {
        GET_ARG(0);
        bool ret = is_64bit_semihosting(env) ?
                   (int64_t)arg0 < 0 : (int32_t)arg0 < 0;
        common_semi_set_ret(cs, ret);
        break;
    }
    case TARGET_SYS_ISTTY:
        GET_ARG(0);
        semihost_sys_isatty(cs, common_semi_istty_cb, arg0);
        break;

    case TARGET_SYS_SEEK:
        GET_ARG(0);
        GET_ARG(1);
        semihost_sys_lseek(cs, common_semi_seek_cb, arg0, arg1, GDB_SEEK_SET);
        break;

    case TARGET_SYS_FLEN:
        GET_ARG(0);
        semihost_sys_flen(cs, common_semi_flen_fstat_cb, common_semi_cb,
                          arg0, common_semi_flen_buf(cs));
        break;

    case TARGET_SYS_TMPNAM:
    {
        int len;
        char *p;

        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        len = asprintf(&s, "%s/qemu-%x%02x", g_get_tmp_dir(),
                       getpid(), (int)arg1 & 0xff);
        if (len < 0) {
            common_semi_set_ret(cs, -1);
            break;
        }

        len++;
        if (len > arg2) {
            free(s);
            common_semi_set_ret(cs, -1);
            break;
        }
        p = lock_user(VERIFY_WRITE, arg0, len, 0);
        if (!p) {
            free(s);
            goto do_fault;
        }
        memcpy(p, s, len);
        unlock_user(p, arg0, len);
        free(s);
        common_semi_set_ret(cs, 0);
        break;
    }

    case TARGET_SYS_REMOVE:
        GET_ARG(0);
        GET_ARG(1);
        semihost_sys_remove(cs, common_semi_cb, arg0, arg1 + 1);
        break;

    case TARGET_SYS_RENAME:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        GET_ARG(3);
        semihost_sys_rename(cs, common_semi_cb, arg0, arg1 + 1, arg2, arg3 + 1);
        break;

    case TARGET_SYS_CLOCK:
        common_semi_set_ret(cs, clock() / (CLOCKS_PER_SEC / 100));
        break;

    case TARGET_SYS_TIME:
        ul_ret = time(NULL);
        common_semi_cb(cs, ul_ret, ul_ret == -1 ? errno : 0);
        break;

    case TARGET_SYS_SYSTEM:
        GET_ARG(0);
        GET_ARG(1);
        semihost_sys_system(cs, common_semi_cb, arg0, arg1 + 1);
        break;

    case TARGET_SYS_ERRNO:
        common_semi_set_ret(cs, get_swi_errno(cs));
        break;

    case TARGET_SYS_GET_CMDLINE:
        {

            char *output_buffer;
            size_t input_size;
            size_t output_size;
            int status = 0;
#if !defined(CONFIG_USER_ONLY)
            const char *cmdline;
#else
            TaskState *ts = get_task_state(cs);
#endif
            GET_ARG(0);
            GET_ARG(1);
            input_size = arg1;
#if !defined(CONFIG_USER_ONLY)
            cmdline = semihosting_get_cmdline();
            if (cmdline == NULL) {
                cmdline = "";
            }
            output_size = strlen(cmdline) + 1;
#else
            unsigned int i;

            output_size = ts->info->env_strings - ts->info->arg_strings;
            if (!output_size) {
                output_size = 1;
            }
#endif

            if (output_size > input_size) {
                common_semi_cb(cs, -1, E2BIG);
                break;
            }

            if (SET_ARG(1, output_size - 1)) {
                goto do_fault;
            }

            output_buffer = lock_user(VERIFY_WRITE, arg0, output_size, 0);
            if (!output_buffer) {
                goto do_fault;
            }

#if !defined(CONFIG_USER_ONLY)
            pstrcpy(output_buffer, output_size, cmdline);
#else
            if (output_size == 1) {
                output_buffer[0] = '\0';
                goto out;
            }

            if (copy_from_user(output_buffer, ts->info->arg_strings,
                               output_size)) {
                unlock_user(output_buffer, arg0, 0);
                goto do_fault;
            }

            for (i = 0; i < output_size - 1; i++) {
                if (output_buffer[i] == 0) {
                    output_buffer[i] = ' ';
                }
            }
        out:
#endif
            unlock_user(output_buffer, arg0, output_size);
            common_semi_cb(cs, status, 0);
        }
        break;

    case TARGET_SYS_HEAPINFO:
        {
            uint64_t retvals[4];
            int i;
#ifdef CONFIG_USER_ONLY
            TaskState *ts = get_task_state(cs);
            static abi_ulong heapbase, heaplimit;
#else
            LayoutInfo info = common_semi_find_bases(cs);
#endif

            GET_ARG(0);

#ifdef CONFIG_USER_ONLY
            if (!heaplimit) {
                heapbase = do_brk(0);
                for (abi_ulong size = COMMON_SEMI_HEAP_SIZE; ; size >>= 1) {
                    abi_ulong limit = heapbase + size;
                    abi_ulong ret = do_brk(limit);
                    if (ret >= limit) {
                        heaplimit = limit;
                        break;
                    }
                }
            }
            retvals[0] = heapbase;
            retvals[1] = heaplimit;
            retvals[2] = ts->info->start_stack;
            retvals[3] = 0;
#else
            retvals[0] = info.heapbase;
            retvals[1] = info.heaplimit;
            retvals[2] = info.heaplimit;
            retvals[3] = info.heapbase;
#endif

            for (i = 0; i < ARRAY_SIZE(retvals); i++) {
                bool fail;

                if (is_64bit_semihosting(env)) {
                    fail = put_user_u64(retvals[i], arg0 + i * 8);
                } else {
                    fail = put_user_u32(retvals[i], arg0 + i * 4);
                }

                if (fail) {
                    goto do_fault;
                }
            }
            common_semi_set_ret(cs, 0);
        }
        break;

    case TARGET_SYS_EXIT:
    case TARGET_SYS_EXIT_EXTENDED:
    {
        uint32_t ret;

        if (nr == TARGET_SYS_EXIT_EXTENDED ||
            common_semi_sys_exit_is_extended(cs)) {
            GET_ARG(0);
            GET_ARG(1);

            if (arg0 == ADP_Stopped_ApplicationExit) {
                ret = arg1;
            } else {
                ret = 1;
            }
        } else {
            ret = (args == ADP_Stopped_ApplicationExit) ? 0 : 1;
        }
        gdb_exit(ret);
        exit(ret);
    }

    case TARGET_SYS_ELAPSED:
        elapsed = get_clock() - clock_start;
        if (is_64bit_semihosting(env)) {
            if (SET_ARG(0, elapsed)) {
                goto do_fault;
            }
        } else {
            if (SET_ARG(0, (uint32_t) elapsed) ||
                SET_ARG(1, (uint32_t) (elapsed >> 32))) {
                goto do_fault;
            }
        }
        common_semi_set_ret(cs, 0);
        break;

    case TARGET_SYS_TICKFREQ:
        common_semi_set_ret(cs, 1000000000);
        break;

    case TARGET_SYS_SYNCCACHE:
        if (common_semi_has_synccache(env)) {
            common_semi_set_ret(cs, 0);
            break;
        }
        /* fall through */
    default:
        fprintf(stderr, "qemu: Unsupported SemiHosting SWI 0x%02x\n", nr);
        cpu_dump_state(cs, stderr, 0);
        abort();

    do_fault:
        common_semi_cb(cs, -1, EFAULT);
        break;
    }
}
