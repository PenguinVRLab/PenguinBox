/*
 * host-signal.h: signal info dependent on the host architecture
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2021 WANG Xuerui <git@xen0n.name>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef LOONGARCH64_HOST_SIGNAL_H
#define LOONGARCH64_HOST_SIGNAL_H

typedef ucontext_t host_sigcontext;

static inline uintptr_t host_signal_pc(host_sigcontext *uc)
{
    return uc->uc_mcontext.__pc;
}

static inline void host_signal_set_pc(host_sigcontext *uc, uintptr_t pc)
{
    uc->uc_mcontext.__pc = pc;
}

static inline void *host_signal_mask(host_sigcontext *uc)
{
    return &uc->uc_sigmask;
}

static inline bool host_signal_write(siginfo_t *info, host_sigcontext *uc)
{
    const uint32_t *pinsn = (const uint32_t *)host_signal_pc(uc);
    uint32_t insn = pinsn[0];

    switch ((insn >> 26) & 0b111111) {
    case 0b001000:
        switch ((insn >> 24) & 0b11) {
        case 0b01:
        case 0b11:
            return true;
        }
        break;
    case 0b001001:
        switch ((insn >> 24) & 0b11) {
        case 0b01:
        case 0b11:
            return true;
        }
        break;
    case 0b001010:
        switch ((insn >> 22) & 0b1111) {
        case 0b0100:
        case 0b0101:
        case 0b0110:
        case 0b0111:
        case 0b1101:
        case 0b1111:
            return true;
        }
        break;
    case 0b001110:
        switch ((insn >> 15) & 0b11111111111) {
        case 0b00000100000:
        case 0b00000101000:
        case 0b00000110000:
        case 0b00000111000:
        case 0b00001110000:
        case 0b00001111000:
        case 0b00011101100:
        case 0b00011101101:
        case 0b00011101110:
        case 0b00011101111:
        case 0b00011111000:
        case 0b00011111001:
        case 0b00011111010:
        case 0b00011111011:
        case 0b00011111100:
        case 0b00011111101:
        case 0b00011111110:
        case 0b00011111111:
        case 0b00011000000 ... 0b00011100011:
            return true;
        }
        break;
    }

    return false;
}

#endif
