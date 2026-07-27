/*
 * host-signal.h: signal info dependent on the host architecture
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2021 Linaro Limited
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef SPARC64_HOST_SIGNAL_H
#define SPARC64_HOST_SIGNAL_H

typedef struct sigcontext host_sigcontext;

static inline uintptr_t host_signal_pc(host_sigcontext *sc)
{
    return sc->sigc_regs.tpc;
}

static inline void host_signal_set_pc(host_sigcontext *sc, uintptr_t pc)
{
    sc->sigc_regs.tpc = pc;
    sc->sigc_regs.tnpc = pc + 4;
}

static inline void *host_signal_mask(host_sigcontext *sc)
{
    return &sc->sigc_mask;
}

static inline bool host_signal_write(siginfo_t *info, host_sigcontext *uc)
{
    uint32_t insn = *(uint32_t *)host_signal_pc(uc);

    if ((insn >> 30) == 3) {
        switch ((insn >> 19) & 0x3f) {
        case 0x05:
        case 0x15:
        case 0x06:
        case 0x16:
        case 0x04:
        case 0x14:
        case 0x07:
        case 0x17:
        case 0x0e:
        case 0x1e:
        case 0x24:
        case 0x34:
        case 0x27:
        case 0x37:
        case 0x26:
        case 0x36:
        case 0x25:
        case 0x3c:
        case 0x3e:
            return true;
        }
    }
    return false;
}

#endif
