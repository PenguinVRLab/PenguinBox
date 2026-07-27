#ifndef LINUX_USER_SOCKET_H
#define LINUX_USER_SOCKET_H

#include "sockbits.h"

#ifndef TARGET_ARCH_HAS_SOCKET_TYPES
enum sock_type {
       TARGET_SOCK_STREAM      = 1,
       TARGET_SOCK_DGRAM       = 2,
       TARGET_SOCK_RAW         = 3,
       TARGET_SOCK_RDM         = 4,
       TARGET_SOCK_SEQPACKET   = 5,
       TARGET_SOCK_DCCP        = 6,
       TARGET_SOCK_PACKET      = 10,
};

#define TARGET_SOCK_MAX (TARGET_SOCK_PACKET + 1)
#define TARGET_SOCK_TYPE_MASK    0xf

#define TARGET_SOCK_CLOEXEC    TARGET_O_CLOEXEC
#ifndef TARGET_SOCK_NONBLOCK
#define TARGET_SOCK_NONBLOCK   TARGET_O_NONBLOCK
#endif
#endif

#endif
