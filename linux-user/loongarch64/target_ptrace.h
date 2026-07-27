/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef LOONGARCH64_TARGET_PTRACE_H
#define LOONGARCH64_TARGET_PTRACE_H

struct target_user_pt_regs {
    abi_ulong regs[32];
    abi_ulong orig_a0;
    abi_ulong csr_era;
    abi_ulong csr_badv;
    abi_ulong reserved[10];
};

#endif
