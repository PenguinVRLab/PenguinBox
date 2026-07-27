/*
 * host-signal.h: signal info dependent on the host architecture
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2021 Linaro Limited
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef AARCH64_HOST_SIGNAL_H
#define AARCH64_HOST_SIGNAL_H

typedef ucontext_t host_sigcontext;

#ifndef ESR_MAGIC
#define ESR_MAGIC 0x45535201
struct esr_context {
    struct _aarch64_ctx head;
    uint64_t esr;
};
#endif

static inline struct _aarch64_ctx *first_ctx(host_sigcontext *uc)
{
    return (struct _aarch64_ctx *)&uc->uc_mcontext.__reserved;
}

static inline struct _aarch64_ctx *next_ctx(struct _aarch64_ctx *hdr)
{
    return (struct _aarch64_ctx *)((char *)hdr + hdr->size);
}

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

static inline bool host_signal_write(siginfo_t *info, host_sigcontext *uc)
{
    struct _aarch64_ctx *hdr;
    uint32_t insn;

    for (hdr = first_ctx(uc); hdr->magic; hdr = next_ctx(hdr)) {
        if (hdr->magic == ESR_MAGIC) {
            struct esr_context const *ec = (struct esr_context const *)hdr;
            uint64_t esr = ec->esr;

            return extract32(esr, 27, 5) == 0x12 && extract32(esr, 6, 1) == 1;
        }
    }

    insn = *(uint32_t *)host_signal_pc(uc);

    return (insn & 0xbfff0000) == 0x0c000000
        || (insn & 0xbfe00000) == 0x0c800000
        || (insn & 0xbfdf0000) == 0x0d000000
        || (insn & 0xbfc00000) == 0x0d800000
        || (insn & 0x3f400000) == 0x08000000
        || (insn & 0x3bc00000) == 0x39000000
        || (insn & 0x3fc00000) == 0x3d800000
        || (insn & 0x3bc00000) == 0x38000000
        || (insn & 0x3fe00000) == 0x3c800000
        || (insn & 0x3a400000) == 0x28000000;
}

#endif
