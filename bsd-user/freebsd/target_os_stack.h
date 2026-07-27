/*
 *  FreeBSD setup_initial_stack() implementation.
 *
 *  Copyright (c) 2013-14 Stacey D. Son
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

#ifndef TARGET_OS_STACK_H
#define TARGET_OS_STACK_H

#include <sys/param.h>
#include "target_arch_sigtramp.h"
#include "qemu/guest-random.h"
#include "user/tswap-target.h"

static inline int setup_initial_stack(struct bsd_binprm *bprm,
        abi_ulong *ret_addr, abi_ulong *stringp)
{
    int i;
    abi_ulong stack_hi_addr;
    size_t execpath_len, stringspace;
    abi_ulong destp, argvp, envp, p;
    struct target_ps_strings ps_strs;
    char canary[sizeof(abi_long) * 8];

    stack_hi_addr = p = target_stkbas + target_stksiz;

    p -= sizeof(struct target_ps_strings);

    p -= TARGET_SZSIGCODE;
    if (setup_sigtramp(p, (unsigned)offsetof(struct target_sigframe, sf_uc),
            TARGET_FREEBSD_NR_sigreturn)) {
        errno = EFAULT;
        return -1;
    }
    if (bprm->fullpath) {
        execpath_len = strlen(bprm->fullpath) + 1;
        p -= roundup(execpath_len, sizeof(abi_ulong));
        if (memcpy_to_target(p, bprm->fullpath, execpath_len)) {
            errno = EFAULT;
            return -1;
        }
    }
    qemu_guest_getrandom_nofail(canary, sizeof(canary));
    p -= roundup(sizeof(canary), sizeof(abi_ulong));
    if (memcpy_to_target(p, canary, sizeof(canary))) {
        errno = EFAULT;
        return -1;
    }
    p -= sizeof(abi_ulong);
    if (put_user_ual(TARGET_PAGE_SIZE, p)) {
        errno = EFAULT;
        return -1;
    }
    p = rounddown(p, TARGET_PAGE_SIZE);

    stringspace = 0;
    for (i = 0; i < bprm->argc; ++i) {
        stringspace += strlen(bprm->argv[i]) + 1;
    }
    for (i = 0; i < bprm->envc; ++i) {
        stringspace += strlen(bprm->envp[i]) + 1;
    }
    if (stringspace > TARGET_ARG_MAX) {
        errno = ENOMEM;
        return -1;
    }
    destp = rounddown(p - stringspace, sizeof(abi_ulong));
    p = argvp = destp - (bprm->argc + bprm->envc + 2) * sizeof(abi_ulong);
    if (stringp) {
        *stringp = destp;
    }
    ps_strs.ps_argvstr = tswapl(argvp);
    ps_strs.ps_nargvstr = tswap32(bprm->argc);
    for (i = 0; i < bprm->argc; ++i) {
        size_t len = strlen(bprm->argv[i]) + 1;

        if (memcpy_to_target(destp, bprm->argv[i], len)) {
            errno = EFAULT;
            return -1;
        }
        if (put_user_ual(destp, argvp)) {
            errno = EFAULT;
            return -1;
        }
        argvp += sizeof(abi_ulong);
        destp += len;
    }
    if (put_user_ual(0, argvp)) {
        errno = EFAULT;
        return -1;
    }
    envp = argvp + sizeof(abi_ulong);
    ps_strs.ps_envstr = tswapl(envp);
    ps_strs.ps_nenvstr = tswap32(bprm->envc);
    for (i = 0; i < bprm->envc; ++i) {
        size_t len = strlen(bprm->envp[i]) + 1;

        if (memcpy_to_target(destp, bprm->envp[i], len)) {
            errno = EFAULT;
            return -1;
        }
        if (put_user_ual(destp, envp)) {
            errno = EFAULT;
            return -1;
        }
        envp += sizeof(abi_ulong);
        destp += len;
    }
    if (put_user_ual(0, envp)) {
        errno = EFAULT;
        return -1;
    }
    if (memcpy_to_target(stack_hi_addr - sizeof(ps_strs), &ps_strs,
                sizeof(ps_strs))) {
        errno = EFAULT;
        return -1;
    }

    if (ret_addr) {
        *ret_addr = p;
    }

    return 0;
 }

#endif
