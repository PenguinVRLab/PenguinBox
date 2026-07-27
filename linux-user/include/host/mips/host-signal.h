/*
 * host-signal.h: signal info dependent on the host architecture
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2021 Linaro Limited
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef MIPS_HOST_SIGNAL_H
#define MIPS_HOST_SIGNAL_H

typedef ucontext_t host_sigcontext;

static inline uintptr_t host_signal_pc(host_sigcontext *uc)
{
    return uc->uc_mcontext.pc;
}

static inline void host_signal_set_pc(host_sigcontext *uc, uintptr_t pc)
{
    uc->uc_mcontext.pc = pc;
}

static inline void *host_signal_mask(host_sigcontext *uc)
{
    return &uc->uc_sigmask;
}

#if defined(__misp16) || defined(__mips_micromips)
#error "Unsupported encoding"
#endif

static inline bool host_signal_write(siginfo_t *info, host_sigcontext *uc)
{
    uint32_t insn = *(uint32_t *)host_signal_pc(uc);

    switch ((insn >> 26) & 077) {
    case 050:
    case 051:
    case 052:
    case 053:
    case 054:
    case 055:
    case 056:
    case 070:
    case 071:
    case 074:
    case 075:
    case 077:
#if !defined(__mips_isa_rev) || __mips_isa_rev < 6
    case 072:
    case 076:
#endif
        return true;
    case 023:
        switch (insn & 077) {
        case 010:
        case 011:
        case 015:
            return true;
        }
        break;
    }
    return false;
}

#endif
