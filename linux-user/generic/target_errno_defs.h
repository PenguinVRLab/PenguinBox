/*
 * Target definitions of errnos. These may be overridden by an
 * architecture specific header if needed.
 *
 * Taken from asm-generic/errno-base.h and asm-generic/errno.h
 */

#ifndef GENERIC_TARGET_ERRNO_DEFS_H
#define GENERIC_TARGET_ERRNO_DEFS_H

#define TARGET_EPERM            1
#define TARGET_ENOENT           2
#define TARGET_ESRCH            3
#define TARGET_EINTR            4
#define TARGET_EIO              5
#define TARGET_ENXIO            6
#define TARGET_E2BIG            7
#define TARGET_ENOEXEC          8
#define TARGET_EBADF            9
#define TARGET_ECHILD          10
#define TARGET_EAGAIN          11
#define TARGET_ENOMEM          12
#define TARGET_EACCES          13
#define TARGET_EFAULT          14
#define TARGET_ENOTBLK         15
#define TARGET_EBUSY           16
#define TARGET_EEXIST          17
#define TARGET_EXDEV           18
#define TARGET_ENODEV          19
#define TARGET_ENOTDIR         20
#define TARGET_EISDIR          21
#define TARGET_EINVAL          22
#define TARGET_ENFILE          23
#define TARGET_EMFILE          24
#define TARGET_ENOTTY          25
#define TARGET_ETXTBSY         26
#define TARGET_EFBIG           27
#define TARGET_ENOSPC          28
#define TARGET_ESPIPE          29
#define TARGET_EROFS           30
#define TARGET_EMLINK          31
#define TARGET_EPIPE           32
#define TARGET_EDOM            33
#define TARGET_ERANGE          34

#define TARGET_EDEADLK         35
#define TARGET_ENAMETOOLONG    36
#define TARGET_ENOLCK          37
#define TARGET_ENOSYS          38
#define TARGET_ENOTEMPTY       39
#define TARGET_ELOOP           40

#define TARGET_ENOMSG          42
#define TARGET_EIDRM           43
#define TARGET_ECHRNG          44
#define TARGET_EL2NSYNC        45
#define TARGET_EL3HLT          46
#define TARGET_EL3RST          47
#define TARGET_ELNRNG          48
#define TARGET_EUNATCH         49
#define TARGET_ENOCSI          50
#define TARGET_EL2HLT          51
#define TARGET_EBADE           52
#define TARGET_EBADR           53
#define TARGET_EXFULL          54
#define TARGET_ENOANO          55
#define TARGET_EBADRQC         56
#define TARGET_EBADSLT         57

#define TARGET_EBFONT          59
#define TARGET_ENOSTR          60
#define TARGET_ENODATA         61
#define TARGET_ETIME           62
#define TARGET_ENOSR           63
#define TARGET_ENONET          64
#define TARGET_ENOPKG          65
#define TARGET_EREMOTE         66
#define TARGET_ENOLINK         67
#define TARGET_EADV            68
#define TARGET_ESRMNT          69
#define TARGET_ECOMM           70
#define TARGET_EPROTO          71
#define TARGET_EMULTIHOP       72
#define TARGET_EDOTDOT         73
#define TARGET_EBADMSG         74
#define TARGET_EOVERFLOW       75
#define TARGET_ENOTUNIQ        76
#define TARGET_EBADFD          77
#define TARGET_EREMCHG         78
#define TARGET_ELIBACC         79
#define TARGET_ELIBBAD         80
#define TARGET_ELIBSCN         81
#define TARGET_ELIBMAX         82
#define TARGET_ELIBEXEC        83
#define TARGET_EILSEQ          84
#define TARGET_ERESTART        85
#define TARGET_ESTRPIPE        86
#define TARGET_EUSERS          87
#define TARGET_ENOTSOCK        88
#define TARGET_EDESTADDRREQ    89
#define TARGET_EMSGSIZE        90
#define TARGET_EPROTOTYPE      91
#define TARGET_ENOPROTOOPT     92
#define TARGET_EPROTONOSUPPORT 93
#define TARGET_ESOCKTNOSUPPORT 94
#define TARGET_EOPNOTSUPP      95
#define TARGET_EPFNOSUPPORT    96
#define TARGET_EAFNOSUPPORT    97
#define TARGET_EADDRINUSE      98
#define TARGET_EADDRNOTAVAIL   99
#define TARGET_ENETDOWN        100
#define TARGET_ENETUNREACH     101
#define TARGET_ENETRESET       102
#define TARGET_ECONNABORTED    103
#define TARGET_ECONNRESET      104
#define TARGET_ENOBUFS         105
#define TARGET_EISCONN         106
#define TARGET_ENOTCONN        107
#define TARGET_ESHUTDOWN       108
#define TARGET_ETOOMANYREFS    109
#define TARGET_ETIMEDOUT       110
#define TARGET_ECONNREFUSED    111
#define TARGET_EHOSTDOWN       112
#define TARGET_EHOSTUNREACH    113
#define TARGET_EALREADY        114
#define TARGET_EINPROGRESS     115
#define TARGET_ESTALE          116
#define TARGET_EUCLEAN         117
#define TARGET_ENOTNAM         118
#define TARGET_ENAVAIL         119
#define TARGET_EISNAM          120
#define TARGET_EREMOTEIO       121
#define TARGET_EDQUOT          122

#define TARGET_ENOMEDIUM       123
#define TARGET_EMEDIUMTYPE     124
#define TARGET_ECANCELED       125
#define TARGET_ENOKEY          126
#define TARGET_EKEYEXPIRED     127
#define TARGET_EKEYREVOKED     128
#define TARGET_EKEYREJECTED    129

#define TARGET_EOWNERDEAD      130
#define TARGET_ENOTRECOVERABLE 131

#define TARGET_ERFKILL         132
#define TARGET_EHWPOISON       133

#endif
