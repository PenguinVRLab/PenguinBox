/*
 * host-signal.h: signal info dependent on the host architecture
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2021 Linaro Limited
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef S390_HOST_SIGNAL_H
#define S390_HOST_SIGNAL_H

typedef ucontext_t host_sigcontext;

static inline uintptr_t host_signal_pc(host_sigcontext *uc)
{
    return uc->uc_mcontext.psw.addr;
}

static inline void host_signal_set_pc(host_sigcontext *uc, uintptr_t pc)
{
    uc->uc_mcontext.psw.addr = pc;
}

static inline void *host_signal_mask(host_sigcontext *uc)
{
    return &uc->uc_sigmask;
}

static inline bool host_signal_write(siginfo_t *info, host_sigcontext *uc)
{
    uint16_t *pinsn = (uint16_t *)host_signal_pc(uc);

    switch (pinsn[0] >> 8) {
    case 0x50:
    case 0x42:
    case 0x40:
    case 0x44:
    case 0xba:
    case 0xbb:
        return true;
    case 0xc4:
        switch (pinsn[0] & 0xf) {
        case 0xf:
        case 0xb:
        case 0x7:
            return true;
        }
        break;
    case 0xc6:
        switch (pinsn[0] & 0xf) {
        case 0x0:
            return true;
        }
        break;
    case 0xc8:
        switch (pinsn[0] & 0xf) {
        case 0x2:
            return true;
        }
        break;
    case 0xe3:
        switch (pinsn[2] & 0xff) {
        case 0x50:
        case 0x24:
        case 0x72:
        case 0x70:
        case 0x8e:
        case 0x3f:
        case 0x3e:
        case 0x2f:
            return true;
        }
        break;
    case 0xe6:
        switch (pinsn[2] & 0xff) {
        case 0x09:
        case 0x0a:
        case 0x0b:
        case 0x0e:
        case 0x0f:
        case 0x3f:
            return true;
        }
        break;
    case 0xe7:
        switch (pinsn[2] & 0xff) {
        case 0x08:
        case 0x09:
        case 0x0a:
        case 0x0b:
        case 0x0e:
        case 0x1a:
        case 0x1b:
        case 0x3e:
        case 0x3f:
            return true;
        }
        break;
    case 0xeb:
        switch (pinsn[2] & 0xff) {
        case 0x14:
        case 0x30:
        case 0x31:
        case 0x3e:
        case 0xe4:
        case 0xe6:
        case 0xe7:
        case 0xe8:
        case 0xea:
        case 0xf4:
        case 0xf6:
        case 0xf7:
        case 0xfa:
        case 0xf8:
            return true;
        }
        break;
    }
    return false;
}

#endif
