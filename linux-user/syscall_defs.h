/* common syscall defines for all architectures */

/* Note: although the syscall numbers change between architectures,
   most of them stay the same, so we handle it by putting ifdefs if
   necessary */

#ifndef SYSCALL_DEFS_H
#define SYSCALL_DEFS_H

#include "syscall_nr.h"


#define TARGET_SYS_SOCKET       1
#define TARGET_SYS_BIND         2
#define TARGET_SYS_CONNECT      3
#define TARGET_SYS_LISTEN       4
#define TARGET_SYS_ACCEPT       5
#define TARGET_SYS_GETSOCKNAME  6
#define TARGET_SYS_GETPEERNAME  7
#define TARGET_SYS_SOCKETPAIR   8
#define TARGET_SYS_SEND         9
#define TARGET_SYS_RECV         10
#define TARGET_SYS_SENDTO       11
#define TARGET_SYS_RECVFROM     12
#define TARGET_SYS_SHUTDOWN     13
#define TARGET_SYS_SETSOCKOPT   14
#define TARGET_SYS_GETSOCKOPT   15
#define TARGET_SYS_SENDMSG      16
#define TARGET_SYS_RECVMSG      17
#define TARGET_SYS_ACCEPT4      18
#define TARGET_SYS_RECVMMSG     19
#define TARGET_SYS_SENDMMSG     20

#define IPCOP_CALL(VERSION, OP) ((VERSION) << 16 | (OP))
#define IPCOP_semop             1
#define IPCOP_semget            2
#define IPCOP_semctl            3
#define IPCOP_semtimedop        4
#define IPCOP_msgsnd            11
#define IPCOP_msgrcv            12
#define IPCOP_msgget            13
#define IPCOP_msgctl            14
#define IPCOP_shmat             21
#define IPCOP_shmdt             22
#define IPCOP_shmget            23
#define IPCOP_shmctl            24

#define TARGET_SEMOPM     500

#define TARGET_IOC_NRBITS       8
#define TARGET_IOC_TYPEBITS     8

#if (defined(TARGET_I386) && defined(TARGET_ABI32))                     \
    || (defined(TARGET_ARM) && defined(TARGET_ABI32))                   \
    || (defined(TARGET_SPARC) && defined(TARGET_ABI32))                 \
    || defined(TARGET_M68K) || defined(TARGET_SH4)
#define USE_UID16
#define target_id uint16_t
#else
#define target_id abi_uint
#endif

#if defined(TARGET_I386) || defined(TARGET_ARM) || defined(TARGET_SH4)  \
    || defined(TARGET_M68K)                                             \
    || defined(TARGET_S390X) || defined(TARGET_OPENRISC)                \
    || defined(TARGET_RISCV)                                            \
    || defined(TARGET_XTENSA) || defined(TARGET_LOONGARCH64)

#define TARGET_IOC_SIZEBITS     14
#define TARGET_IOC_DIRBITS      2

#define TARGET_IOC_NONE   0U
#define TARGET_IOC_WRITE  1U
#define TARGET_IOC_READ   2U

#elif defined(TARGET_PPC) || defined(TARGET_ALPHA) ||           \
    defined(TARGET_SPARC) || defined(TARGET_MICROBLAZE) ||      \
    defined(TARGET_MIPS)

#define TARGET_IOC_SIZEBITS     13
#define TARGET_IOC_DIRBITS      3

#define TARGET_IOC_NONE   1U
#define TARGET_IOC_READ   2U
#define TARGET_IOC_WRITE  4U

#elif defined(TARGET_HPPA)

#define TARGET_IOC_SIZEBITS  14
#define TARGET_IOC_DIRBITS    2

#define TARGET_IOC_NONE   0U
#define TARGET_IOC_WRITE  2U
#define TARGET_IOC_READ   1U

#elif defined(TARGET_HEXAGON)

#define TARGET_IOC_SIZEBITS     14

#define TARGET_IOC_NONE   0U
#define TARGET_IOC_WRITE  1U
#define TARGET_IOC_READ          2U

#else
#error unsupported CPU
#endif

#define TARGET_IOC_NRMASK       ((1 << TARGET_IOC_NRBITS)-1)
#define TARGET_IOC_TYPEMASK     ((1 << TARGET_IOC_TYPEBITS)-1)
#define TARGET_IOC_SIZEMASK     ((1 << TARGET_IOC_SIZEBITS)-1)
#define TARGET_IOC_DIRMASK      ((1 << TARGET_IOC_DIRBITS)-1)

#define TARGET_IOC_NRSHIFT      0
#define TARGET_IOC_TYPESHIFT    (TARGET_IOC_NRSHIFT+TARGET_IOC_NRBITS)
#define TARGET_IOC_SIZESHIFT    (TARGET_IOC_TYPESHIFT+TARGET_IOC_TYPEBITS)
#define TARGET_IOC_DIRSHIFT     (TARGET_IOC_SIZESHIFT+TARGET_IOC_SIZEBITS)

#define TARGET_IOC(dir,type,nr,size)            \
    (((dir)  << TARGET_IOC_DIRSHIFT) |          \
     ((type) << TARGET_IOC_TYPESHIFT) |         \
     ((nr)   << TARGET_IOC_NRSHIFT) |           \
     ((size) << TARGET_IOC_SIZESHIFT))

#define TARGET_IO(type,nr)              TARGET_IOC(TARGET_IOC_NONE,(type),(nr),0)
#define TARGET_IOR(type,nr,size)        TARGET_IOC(TARGET_IOC_READ,(type),(nr),sizeof(size))
#define TARGET_IOW(type,nr,size)        TARGET_IOC(TARGET_IOC_WRITE,(type),(nr),sizeof(size))
#define TARGET_IOWR(type,nr,size)       TARGET_IOC(TARGET_IOC_READ|TARGET_IOC_WRITE,(type),(nr),sizeof(size))

#define TARGET_IORU(type,nr)    TARGET_IOC(TARGET_IOC_READ,(type),(nr),TARGET_IOC_SIZEMASK)
#define TARGET_IOWU(type,nr)    TARGET_IOC(TARGET_IOC_WRITE,(type),(nr),TARGET_IOC_SIZEMASK)
#define TARGET_IOWRU(type,nr)   TARGET_IOC(TARGET_IOC_READ|TARGET_IOC_WRITE,(type),(nr),TARGET_IOC_SIZEMASK)

struct target_sockaddr {
    abi_ushort sa_family;
    uint8_t sa_data[14];
};

struct target_sockaddr_ll {
    abi_ushort sll_family;
    abi_ushort sll_protocol;
    abi_int    sll_ifindex;
    abi_ushort sll_hatype;
    uint8_t    sll_pkttype;
    uint8_t    sll_halen;
    uint8_t    sll_addr[8];
};

struct target_sockaddr_un {
    abi_ushort su_family;
    uint8_t sun_path[108];
};

struct target_sockaddr_nl {
    abi_ushort nl_family;
    abi_ushort __pad;
    abi_uint nl_pid;
    abi_uint nl_groups;
};

struct target_in_addr {
    abi_uint s_addr;
};

struct target_sockaddr_in {
    abi_ushort sin_family;
    abi_short sin_port;
    struct target_in_addr sin_addr;
    uint8_t __pad[sizeof(struct target_sockaddr) -
                  sizeof(abi_ushort) - sizeof(abi_short) -
                  sizeof(struct target_in_addr)];
};

struct target_sockaddr_in6 {
    abi_ushort sin6_family;
    abi_ushort sin6_port;
    abi_uint sin6_flowinfo;
    struct in6_addr sin6_addr;
    abi_uint sin6_scope_id;
};

struct target_sock_filter {
    abi_ushort code;
    uint8_t jt;
    uint8_t jf;
    abi_uint k;
};

struct target_sock_fprog {
    abi_ushort len;
    abi_ulong filter;
};

struct target_ip_mreq {
    struct target_in_addr imr_multiaddr;
    struct target_in_addr imr_address;
};

struct target_ip_mreqn {
    struct target_in_addr imr_multiaddr;
    struct target_in_addr imr_address;
    abi_long imr_ifindex;
};

struct target_ip_mreq_source {
    abi_uint imr_multiaddr;
    abi_uint imr_interface;
    abi_uint imr_sourceaddr;
};

struct target_linger {
    abi_int l_onoff;
    abi_int l_linger;
};

#if defined(TARGET_SPARC64) && !defined(TARGET_ABI32)
struct target_timeval {
    abi_long tv_sec;
    abi_int tv_usec;
};
#define target__kernel_sock_timeval target_timeval
#else
struct target_timeval {
    abi_long tv_sec;
    abi_long tv_usec;
};

struct target__kernel_sock_timeval {
    abi_llong tv_sec;
    abi_llong tv_usec;
};
#endif

struct target_timespec {
    abi_long tv_sec;
    abi_long tv_nsec;
};

struct target__kernel_timespec {
    abi_llong tv_sec;
    abi_llong tv_nsec;
};

struct target_timezone {
    abi_int tz_minuteswest;
    abi_int tz_dsttime;
};

struct target_itimerval {
    struct target_timeval it_interval;
    struct target_timeval it_value;
};

struct target_itimerspec {
    struct target_timespec it_interval;
    struct target_timespec it_value;
};

struct target__kernel_itimerspec {
    struct target__kernel_timespec it_interval;
    struct target__kernel_timespec it_value;
};

struct target_timex {
    abi_uint modes;
    abi_long offset;
    abi_long freq;
    abi_long maxerror;
    abi_long esterror;
    abi_int status;
    abi_long constant;
    abi_long precision;
    abi_long tolerance;
    struct target_timeval time;
    abi_long tick;
    abi_long ppsfreq;
    abi_long jitter;
    abi_int shift;
    abi_long stabil;
    abi_long jitcnt;
    abi_long calcnt;
    abi_long errcnt;
    abi_long stbcnt;
    abi_int tai;

    abi_int:32; abi_int:32; abi_int:32; abi_int:32;
    abi_int:32; abi_int:32; abi_int:32; abi_int:32;
    abi_int:32; abi_int:32; abi_int:32;
};

struct target__kernel_timex {
    abi_uint modes;
    abi_int: 32;
    abi_llong offset;
    abi_llong freq;
    abi_llong maxerror;
    abi_llong esterror;
    abi_int status;
    abi_int: 32;
    abi_llong constant;
    abi_llong precision;
    abi_llong tolerance;
    struct target__kernel_sock_timeval time;
    abi_llong tick;
    abi_llong ppsfreq;
    abi_llong jitter;
    abi_int shift;
    abi_int: 32;
    abi_llong stabil;
    abi_llong jitcnt;
    abi_llong calcnt;
    abi_llong errcnt;
    abi_llong stbcnt;
    abi_int tai;

    abi_int:32; abi_int:32; abi_int:32; abi_int:32;
    abi_int:32; abi_int:32; abi_int:32; abi_int:32;
    abi_int:32; abi_int:32; abi_int:32;
};

typedef abi_long target_clock_t;

#define TARGET_HZ 100

struct target_tms {
    target_clock_t tms_utime;
    target_clock_t tms_stime;
    target_clock_t tms_cutime;
    target_clock_t tms_cstime;
};

struct target_utimbuf {
    abi_long actime;
    abi_long modtime;
};

struct target_sel_arg_struct {
    abi_long n;
    abi_long inp, outp, exp;
    abi_long tvp;
};

struct target_iovec {
    abi_long iov_base;
    abi_long iov_len;
};

struct target_msghdr {
    abi_long     msg_name;
    abi_int      msg_namelen;
    abi_long     msg_iov;
    abi_long     msg_iovlen;
    abi_long     msg_control;    /* Per protocol magic (eg BSD file descriptor passing) */
    abi_long     msg_controllen;
    abi_uint     msg_flags;
};

struct target_cmsghdr {
    abi_long     cmsg_len;
    abi_int      cmsg_level;
    abi_int      cmsg_type;
};

#define TARGET_CMSG_DATA(cmsg) ((unsigned char *) ((struct target_cmsghdr *) (cmsg) + 1))
#define TARGET_CMSG_NXTHDR(mhdr, cmsg, cmsg_start)      \
    __target_cmsg_nxthdr(mhdr, cmsg, cmsg_start)
#define TARGET_CMSG_ALIGN(len) (((len) + sizeof (abi_long) - 1)         \
                                & (size_t) ~(sizeof (abi_long) - 1))
#define TARGET_CMSG_SPACE(len) (sizeof(struct target_cmsghdr) + \
                                TARGET_CMSG_ALIGN(len))
#define TARGET_CMSG_LEN(len) (sizeof(struct target_cmsghdr) + (len))

static __inline__ struct target_cmsghdr *
__target_cmsg_nxthdr(struct target_msghdr *__mhdr,
                     struct target_cmsghdr *__cmsg,
                     struct target_cmsghdr *__cmsg_start)
{
    struct target_cmsghdr *__ptr;

    __ptr = (struct target_cmsghdr *)((unsigned char *) __cmsg
                                      + TARGET_CMSG_ALIGN (tswapal(__cmsg->cmsg_len)));
    if ((unsigned long)((char *)(__ptr+1) - (char *)__cmsg_start)
        > tswapal(__mhdr->msg_controllen)) {
        return (struct target_cmsghdr *)0;
    }
    return __ptr;
}

struct target_mmsghdr {
    struct target_msghdr msg_hdr;
    abi_uint             msg_len;
};

struct  target_rusage {
    struct target_timeval ru_utime;
    struct target_timeval ru_stime;
    abi_long    ru_maxrss;
    abi_long    ru_ixrss;
    abi_long    ru_idrss;
    abi_long    ru_isrss;
    abi_long    ru_minflt;
    abi_long    ru_majflt;
    abi_long    ru_nswap;
    abi_long    ru_inblock;
    abi_long    ru_oublock;
    abi_long    ru_msgsnd;
    abi_long    ru_msgrcv;
    abi_long    ru_nsignals;
    abi_long    ru_nvcsw;
    abi_long    ru_nivcsw;
};

typedef struct {
    abi_int val[2];
} kernel_fsid_t;

struct target_dirent {
    abi_long        d_ino;
    abi_long        d_off;
    abi_ushort      d_reclen;
    char            d_name[];
};

struct target_dirent64 {
    abi_ullong      d_ino;
    abi_llong       d_off;
    abi_ushort      d_reclen;
    unsigned char   d_type;
    char            d_name[];
};


#define TARGET_SIG_DFL  ((abi_long)0)
#define TARGET_SIG_IGN  ((abi_long)1)
#define TARGET_SIG_ERR  ((abi_long)-1)

#ifdef TARGET_MIPS
#define TARGET_NSIG        128
#else
#define TARGET_NSIG        64
#endif
#define TARGET_NSIG_BPW    TARGET_ABI_BITS
#define TARGET_NSIG_WORDS  (TARGET_NSIG / TARGET_NSIG_BPW)

typedef struct {
    abi_ulong sig[TARGET_NSIG_WORDS];
} target_sigset_t;

#if HOST_BIG_ENDIAN != TARGET_BIG_ENDIAN
static inline void tswap_sigset(target_sigset_t *d, const target_sigset_t *s)
{
    int i;
    for(i = 0;i < TARGET_NSIG_WORDS; i++)
        d->sig[i] = tswapal(s->sig[i]);
}
#else
static inline void tswap_sigset(target_sigset_t *d, const target_sigset_t *s)
{
    *d = *s;
}
#endif

static inline void target_siginitset(target_sigset_t *d, abi_ulong set)
{
    int i;
    d->sig[0] = set;
    for(i = 1;i < TARGET_NSIG_WORDS; i++)
        d->sig[i] = 0;
}

void host_to_target_sigset(target_sigset_t *d, const sigset_t *s);
void target_to_host_sigset(sigset_t *d, const target_sigset_t *s);
void host_to_target_old_sigset(abi_ulong *old_sigset,
                               const sigset_t *sigset);
void target_to_host_old_sigset(sigset_t *sigset,
                               const abi_ulong *old_sigset);
struct target_sigaction;
int do_sigaction(int sig, const struct target_sigaction *act,
                 struct target_sigaction *oact, abi_ulong ka_restorer);

#include "target_signal.h"

#ifdef TARGET_SA_RESTORER
#define TARGET_ARCH_HAS_SA_RESTORER 1
#endif

#if defined(TARGET_ALPHA)
typedef abi_int target_old_sa_flags;
#else
typedef abi_ulong target_old_sa_flags;
#endif

#if defined(TARGET_MIPS)
struct target_sigaction {
    abi_uint        sa_flags;
#if defined(TARGET_ABI_MIPSN32)
    abi_uint        _sa_handler;
#else
    abi_ulong       _sa_handler;
#endif
    target_sigset_t sa_mask;
};
#else
struct target_old_sigaction {
    abi_ulong _sa_handler;
    abi_ulong sa_mask;
    target_old_sa_flags sa_flags;
#ifdef TARGET_ARCH_HAS_SA_RESTORER
    abi_ulong sa_restorer;
#endif
};

struct target_sigaction {
    abi_ulong _sa_handler;
    abi_ulong sa_flags;
#ifdef TARGET_ARCH_HAS_SA_RESTORER
    abi_ulong sa_restorer;
#endif
    target_sigset_t sa_mask;
#ifdef TARGET_ARCH_HAS_KA_RESTORER
    abi_ulong ka_restorer;
#endif
};
#endif

typedef union target_sigval {
    abi_int sival_int;
    abi_ulong sival_ptr;
} target_sigval_t;

#define TARGET_SI_MAX_SIZE      128

#if TARGET_ABI_BITS == 32
#define TARGET_SI_PREAMBLE_SIZE (3 * sizeof(int))
#else
#define TARGET_SI_PREAMBLE_SIZE (4 * sizeof(int))
#endif

#define TARGET_SI_PAD_SIZE ((TARGET_SI_MAX_SIZE - TARGET_SI_PREAMBLE_SIZE) / sizeof(int))

#define QEMU_SI_KILL 0
#define QEMU_SI_TIMER 1
#define QEMU_SI_POLL 2
#define QEMU_SI_FAULT 3
#define QEMU_SI_CHLD 4
#define QEMU_SI_RT 5

typedef struct target_siginfo {
#ifdef TARGET_MIPS
    abi_int si_signo;
    abi_int si_code;
    abi_int si_errno;
#else
    abi_int si_signo;
    abi_int si_errno;
    abi_int si_code;
#endif

    union {
        abi_int _pad[TARGET_SI_PAD_SIZE];

        struct {
            pid_t _pid;
            uid_t _uid;
        } _kill;

        struct {
            abi_uint _timer1;
            abi_uint _timer2;
        } _timer;

        struct {
            pid_t _pid;
            uid_t _uid;
            target_sigval_t _sigval;
        } _rt;

        struct {
            pid_t _pid;
            uid_t _uid;
            abi_int _status;
            target_clock_t _utime;
            target_clock_t _stime;
        } _sigchld;

        struct {
            abi_ulong _addr;
        } _sigfault;

        struct {
            abi_int _band;
            abi_int _fd;
        } _sigpoll;
    } _sifields;
} target_siginfo_t;

#define TARGET_SI_USER          0
#define TARGET_SI_KERNEL        0x80
#define TARGET_SI_QUEUE -1
#define TARGET_SI_TIMER -2
#define TARGET_SI_MESGQ -3
#define TARGET_SI_ASYNCIO       -4
#define TARGET_SI_SIGIO -5

#define TARGET_ILL_ILLOPC       (1)
#define TARGET_ILL_ILLOPN       (2)
#define TARGET_ILL_ILLADR       (3)
#define TARGET_ILL_ILLTRP       (4)
#define TARGET_ILL_PRVOPC       (5)
#define TARGET_ILL_PRVREG       (6)
#define TARGET_ILL_COPROC       (7)
#define TARGET_ILL_BADSTK       (8)

#define TARGET_FPE_INTDIV      (1)
#define TARGET_FPE_INTOVF      (2)
#define TARGET_FPE_FLTDIV      (3)
#define TARGET_FPE_FLTOVF      (4)
#define TARGET_FPE_FLTUND      (5)
#define TARGET_FPE_FLTRES      (6)
#define TARGET_FPE_FLTINV      (7)
#define TARGET_FPE_FLTSUB      (8)
#define TARGET_FPE_FLTUNK      (14)
#define TARGET_FPE_CONDTRAP    (15)

#define TARGET_SEGV_MAPERR     (1)
#define TARGET_SEGV_ACCERR     (2)
#define TARGET_SEGV_BNDERR     (3)

#define TARGET_BUS_ADRALN       (1)
#define TARGET_BUS_ADRERR       (2)
#define TARGET_BUS_OBJERR       (3)
#define TARGET_BUS_MCEERR_AR    (4)
#define TARGET_BUS_MCEERR_AO    (5)

#define TARGET_TRAP_BRKPT       (1)
#define TARGET_TRAP_TRACE       (2)
#define TARGET_TRAP_BRANCH      (3)
#define TARGET_TRAP_HWBKPT      (4)
#define TARGET_TRAP_UNK         (5)

#define TARGET_SYS_SECCOMP       (1)
#define TARGET_SYS_USER_DISPATCH (2)

#define TARGET_EMT_TAGOVF      1

#include "target_resource.h"

struct target_pollfd {
    abi_int fd;
    abi_short events;
    abi_short revents;
};

#define TARGET_KIOCSOUND       0x4B2F
#define TARGET_KDMKTONE        0x4B30
#define TARGET_KDGKBTYPE       0x4b33
#define TARGET_KDSETMODE       0x4b3a
#define TARGET_KDGKBMODE       0x4b44
#define TARGET_KDSKBMODE       0x4b45
#define TARGET_KDGKBENT        0x4B46
#define TARGET_KDGKBSENT       0x4B48
#define TARGET_KDGKBLED        0x4B64
#define TARGET_KDSKBLED        0x4B65
#define TARGET_KDGETLED        0x4B31
#define TARGET_KDSETLED        0x4B32
#define TARGET_KDSIGACCEPT     0x4B4E

struct target_rtc_pll_info {
    abi_int pll_ctrl;
    abi_int pll_value;
    abi_int pll_max;
    abi_int pll_min;
    abi_int pll_posmult;
    abi_int pll_negmult;
    abi_long pll_clock;
};

#define TARGET_RTC_AIE_ON           TARGET_IO('p', 0x01)
#define TARGET_RTC_AIE_OFF          TARGET_IO('p', 0x02)
#define TARGET_RTC_UIE_ON           TARGET_IO('p', 0x03)
#define TARGET_RTC_UIE_OFF          TARGET_IO('p', 0x04)
#define TARGET_RTC_PIE_ON           TARGET_IO('p', 0x05)
#define TARGET_RTC_PIE_OFF          TARGET_IO('p', 0x06)
#define TARGET_RTC_WIE_ON           TARGET_IO('p', 0x0f)
#define TARGET_RTC_WIE_OFF          TARGET_IO('p', 0x10)
#define TARGET_RTC_ALM_READ         TARGET_IOR('p', 0x08, struct rtc_time)
#define TARGET_RTC_ALM_SET          TARGET_IOW('p', 0x07, struct rtc_time)
#define TARGET_RTC_RD_TIME          TARGET_IOR('p', 0x09, struct rtc_time)
#define TARGET_RTC_SET_TIME         TARGET_IOW('p', 0x0a, struct rtc_time)
#define TARGET_RTC_IRQP_READ        TARGET_IOR('p', 0x0b, abi_ulong)
#define TARGET_RTC_IRQP_SET         TARGET_IOW('p', 0x0c, abi_ulong)
#define TARGET_RTC_EPOCH_READ       TARGET_IOR('p', 0x0d, abi_ulong)
#define TARGET_RTC_EPOCH_SET        TARGET_IOW('p', 0x0e, abi_ulong)
#define TARGET_RTC_WKALM_RD         TARGET_IOR('p', 0x10, struct rtc_wkalrm)
#define TARGET_RTC_WKALM_SET        TARGET_IOW('p', 0x0f, struct rtc_wkalrm)
#define TARGET_RTC_PLL_GET          TARGET_IOR('p', 0x11,               \
                                               struct target_rtc_pll_info)
#define TARGET_RTC_PLL_SET          TARGET_IOW('p', 0x12,               \
                                               struct target_rtc_pll_info)
#define TARGET_RTC_VL_READ          TARGET_IOR('p', 0x13, abi_int)
#define TARGET_RTC_VL_CLR           TARGET_IO('p', 0x14)

#if defined(TARGET_ALPHA) || defined(TARGET_MIPS) || defined(TARGET_SH4) || \
    defined(TARGET_XTENSA)
#define TARGET_FIOGETOWN       TARGET_IOR('f', 123, abi_int)
#define TARGET_FIOSETOWN       TARGET_IOW('f', 124, abi_int)
#define TARGET_SIOCATMARK      TARGET_IOR('s', 7, abi_int)
#define TARGET_SIOCSPGRP       TARGET_IOW('s', 8, pid_t)
#define TARGET_SIOCGPGRP       TARGET_IOR('s', 9, pid_t)
#else
#define TARGET_FIOGETOWN       0x8903
#define TARGET_FIOSETOWN       0x8901
#define TARGET_SIOCATMARK      0x8905
#define TARGET_SIOCSPGRP       0x8902
#define TARGET_SIOCGPGRP       0x8904
#endif

#if defined(TARGET_SH4)
#define TARGET_SIOCGSTAMP_OLD   TARGET_IOR('s', 100, struct target_timeval)
#define TARGET_SIOCGSTAMPNS_OLD TARGET_IOR('s', 101, struct target_timespec)
#else
#define TARGET_SIOCGSTAMP_OLD   0x8906
#define TARGET_SIOCGSTAMPNS_OLD 0x8907
#endif

#define TARGET_SIOCGSTAMP_NEW   TARGET_IOR(0x89, 0x06, abi_llong[2])
#define TARGET_SIOCGSTAMPNS_NEW TARGET_IOR(0x89, 0x07, abi_llong[2])

#define TARGET_SIOCADDRT       0x890B
#define TARGET_SIOCDELRT       0x890C
#define TARGET_SIOCGIFNAME     0x8910
#define TARGET_SIOCSIFLINK     0x8911
#define TARGET_SIOCGIFCONF     0x8912
#define TARGET_SIOCGIFFLAGS    0x8913
#define TARGET_SIOCSIFFLAGS    0x8914
#define TARGET_SIOCGIFADDR     0x8915
#define TARGET_SIOCSIFADDR     0x8916
#define TARGET_SIOCGIFDSTADDR  0x8917
#define TARGET_SIOCSIFDSTADDR  0x8918
#define TARGET_SIOCGIFBRDADDR  0x8919
#define TARGET_SIOCSIFBRDADDR  0x891a
#define TARGET_SIOCGIFNETMASK  0x891b
#define TARGET_SIOCSIFNETMASK  0x891c
#define TARGET_SIOCGIFMETRIC   0x891d
#define TARGET_SIOCSIFMETRIC   0x891e
#define TARGET_SIOCGIFMEM      0x891f          /* get memory address (BSD)     */
#define TARGET_SIOCSIFMEM      0x8920          /* set memory address (BSD)     */
#define TARGET_SIOCGIFMTU      0x8921
#define TARGET_SIOCSIFMTU      0x8922
#define TARGET_SIOCSIFHWADDR   0x8924
#define TARGET_SIOCGIFENCAP    0x8925
#define TARGET_SIOCSIFENCAP    0x8926
#define TARGET_SIOCGIFHWADDR   0x8927
#define TARGET_SIOCGIFSLAVE    0x8929
#define TARGET_SIOCSIFSLAVE    0x8930
#define TARGET_SIOCADDMULTI    0x8931
#define TARGET_SIOCDELMULTI    0x8932
#define TARGET_SIOCGIFINDEX    0x8933
#define TARGET_SIOCSIFPFLAGS   0x8934
#define TARGET_SIOCGIFPFLAGS   0x8935

#define TARGET_SIOCGIFBR       0x8940
#define TARGET_SIOCSIFBR       0x8941

#define TARGET_SIOCGIFTXQLEN   0x8942
#define TARGET_SIOCSIFTXQLEN   0x8943

#define TARGET_OLD_SIOCDARP    0x8950
#define TARGET_OLD_SIOCGARP    0x8951
#define TARGET_OLD_SIOCSARP    0x8952
#define TARGET_SIOCDARP        0x8953
#define TARGET_SIOCGARP        0x8954
#define TARGET_SIOCSARP        0x8955

#define TARGET_SIOCDRARP       0x8960
#define TARGET_SIOCGRARP       0x8961
#define TARGET_SIOCSRARP       0x8962

#define TARGET_SIOCGIFMAP      0x8970
#define TARGET_SIOCSIFMAP      0x8971

#define TARGET_SIOCADDDLCI     0x8980
#define TARGET_SIOCDELDLCI     0x8981

#define TARGET_SIOCGIWNAME     0x8B01

#define TARGET_TUNSETDEBUG        TARGET_IOW('T', 201, abi_int)
#define TARGET_TUNSETIFF          TARGET_IOW('T', 202, abi_int)
#define TARGET_TUNSETPERSIST      TARGET_IOW('T', 203, abi_int)
#define TARGET_TUNSETOWNER        TARGET_IOW('T', 204, abi_int)
#define TARGET_TUNSETLINK         TARGET_IOW('T', 205, abi_int)
#define TARGET_TUNSETGROUP        TARGET_IOW('T', 206, abi_int)
#define TARGET_TUNGETFEATURES     TARGET_IOR('T', 207, abi_uint)
#define TARGET_TUNSETOFFLOAD      TARGET_IOW('T', 208, abi_uint)
#define TARGET_TUNSETTXFILTER     TARGET_IOW('T', 209, abi_uint)
#define TARGET_TUNGETIFF          TARGET_IOR('T', 210, abi_uint)
#define TARGET_TUNGETSNDBUF       TARGET_IOR('T', 211, abi_int)
#define TARGET_TUNSETSNDBUF       TARGET_IOW('T', 212, abi_int)
#define TARGET_TUNGETVNETHDRSZ    TARGET_IOR('T', 215, abi_int)
#define TARGET_TUNSETVNETHDRSZ    TARGET_IOW('T', 216, abi_int)
#define TARGET_TUNSETQUEUE        TARGET_IOW('T', 217, abi_int)
#define TARGET_TUNSETIFINDEX      TARGET_IOW('T', 218, abi_uint)
#define TARGET_TUNSETVNETLE       TARGET_IOW('T', 220, abi_int)
#define TARGET_TUNGETVNETLE       TARGET_IOR('T', 221, abi_int)
#define TARGET_TUNSETVNETBE       TARGET_IOW('T', 222, abi_int)
#define TARGET_TUNGETVNETBE       TARGET_IOR('T', 223, abi_int)
#define TARGET_TUNSETSTEERINGEBPF TARGET_IOR('T', 224, abi_int)
#define TARGET_TUNSETFILTEREBPF   TARGET_IOR('T', 225, abi_int)
#define TARGET_TUNSETCARRIER      TARGET_IOW('T', 226, abi_int)
#define TARGET_TUNGETDEVNETNS     TARGET_IO('T', 227)

#define TARGET_RNDGETENTCNT    TARGET_IOR('R', 0x00, abi_int)
#define TARGET_RNDADDTOENTCNT  TARGET_IOW('R', 0x01, abi_int)
#define TARGET_RNDZAPENTCNT    TARGET_IO('R', 0x04)
#define TARGET_RNDCLEARPOOL    TARGET_IO('R', 0x06)
#define TARGET_RNDRESEEDCRNG   TARGET_IO('R', 0x07)

#define TARGET_BLKROSET   TARGET_IO(0x12,93)
#define TARGET_BLKROGET   TARGET_IO(0x12,94)
#define TARGET_BLKRRPART  TARGET_IO(0x12,95)
#define TARGET_BLKGETSIZE TARGET_IO(0x12,96)
#define TARGET_BLKFLSBUF  TARGET_IO(0x12,97)
#define TARGET_BLKRASET   TARGET_IO(0x12,98)
#define TARGET_BLKRAGET   TARGET_IO(0x12,99)
#define TARGET_BLKFRASET  TARGET_IO(0x12,100)
#define TARGET_BLKFRAGET  TARGET_IO(0x12,101)
#define TARGET_BLKSECTSET TARGET_IO(0x12,102)
#define TARGET_BLKSECTGET TARGET_IO(0x12,103)
#define TARGET_BLKSSZGET  TARGET_IO(0x12,104)
#define TARGET_BLKPG      TARGET_IO(0x12,105)
#define TARGET_BLKBSZGET  TARGET_IOR(0x12, 112, abi_ulong)
#define TARGET_BLKBSZSET  TARGET_IOW(0x12, 113, abi_ulong)
#define TARGET_BLKGETSIZE64 TARGET_IOR(0x12,114,abi_ulong)

#define TARGET_BLKDISCARD TARGET_IO(0x12, 119)
#define TARGET_BLKIOMIN TARGET_IO(0x12, 120)
#define TARGET_BLKIOOPT TARGET_IO(0x12, 121)
#define TARGET_BLKALIGNOFF TARGET_IO(0x12, 122)
#define TARGET_BLKPBSZGET TARGET_IO(0x12, 123)
#define TARGET_BLKDISCARDZEROES TARGET_IO(0x12, 124)
#define TARGET_BLKSECDISCARD TARGET_IO(0x12, 125)
#define TARGET_BLKROTATIONAL TARGET_IO(0x12, 126)
#define TARGET_BLKZEROOUT TARGET_IO(0x12, 127)

#define TARGET_FDMSGON        TARGET_IO(2, 0x45)
#define TARGET_FDMSGOFF       TARGET_IO(2, 0x46)
#define TARGET_FDFMTBEG       TARGET_IO(2, 0x47)
#define TARGET_FDFMTTRK      TARGET_IOW(2, 0x48, struct format_descr)
#define TARGET_FDFMTEND       TARGET_IO(2, 0x49)
#define TARGET_FDSETEMSGTRESH TARGET_IO(2, 0x4a)
#define TARGET_FDFLUSH        TARGET_IO(2, 0x4b)
#define TARGET_FDSETMAXERRS  TARGET_IOW(2, 0x4c, struct floppy_max_errors)
#define TARGET_FDGETMAXERRS  TARGET_IOR(2, 0x0e, struct floppy_max_errors)
#define TARGET_FDRESET        TARGET_IO(2, 0x54)
#define TARGET_FDRAWCMD       TARGET_IO(2, 0x58)
#define TARGET_FDTWADDLE      TARGET_IO(2, 0x59)
#define TARGET_FDEJECT        TARGET_IO(2, 0x5a)

#define TARGET_FIBMAP     TARGET_IO(0x00,1)
#define TARGET_FIGETBSZ   TARGET_IO(0x00,2)

#define TARGET_FICLONE    TARGET_IOW(0x94, 9, abi_int)
#define TARGET_FICLONERANGE TARGET_IOW(0x94, 13, struct file_clone_range)

#define TARGET_FIFREEZE    TARGET_IOWR('X', 119, abi_int)
#define TARGET_FITHAW    TARGET_IOWR('X', 120, abi_int)
#define TARGET_FITRIM    TARGET_IOWR('X', 121, struct fstrim_range)

#define TARGET_FS_IOC_GETFLAGS TARGET_IOR('f', 1, abi_long)
#define TARGET_FS_IOC_SETFLAGS TARGET_IOW('f', 2, abi_long)
#define TARGET_FS_IOC_GETVERSION TARGET_IOR('v', 1, abi_long)
#define TARGET_FS_IOC_SETVERSION TARGET_IOW('v', 2, abi_long)
#define TARGET_FS_IOC_FIEMAP TARGET_IOWR('f',11,struct fiemap)
#define TARGET_FS_IOC32_GETFLAGS TARGET_IOR('f', 1, abi_int)
#define TARGET_FS_IOC32_SETFLAGS TARGET_IOW('f', 2, abi_int)
#define TARGET_FS_IOC32_GETVERSION TARGET_IOR('v', 1, abi_int)
#define TARGET_FS_IOC32_SETVERSION TARGET_IOW('v', 2, abi_int)

#ifdef HAVE_BTRFS_H
#define TARGET_BTRFS_IOC_SNAP_CREATE            TARGET_IOWU(BTRFS_IOCTL_MAGIC, 1)
#define TARGET_BTRFS_IOC_SCAN_DEV               TARGET_IOWU(BTRFS_IOCTL_MAGIC, 4)
#define TARGET_BTRFS_IOC_FORGET_DEV             TARGET_IOWU(BTRFS_IOCTL_MAGIC, 5)
#define TARGET_BTRFS_IOC_ADD_DEV                TARGET_IOWU(BTRFS_IOCTL_MAGIC, 10)
#define TARGET_BTRFS_IOC_RM_DEV                 TARGET_IOWU(BTRFS_IOCTL_MAGIC, 11)
#define TARGET_BTRFS_IOC_SUBVOL_CREATE          TARGET_IOWU(BTRFS_IOCTL_MAGIC, 14)
#define TARGET_BTRFS_IOC_SNAP_DESTROY           TARGET_IOWU(BTRFS_IOCTL_MAGIC, 15)
#define TARGET_BTRFS_IOC_INO_LOOKUP             TARGET_IOWRU(BTRFS_IOCTL_MAGIC, 18)
#define TARGET_BTRFS_IOC_DEFAULT_SUBVOL         TARGET_IOW(BTRFS_IOCTL_MAGIC, 19, \
                                                           abi_ullong)
#define TARGET_BTRFS_IOC_SUBVOL_GETFLAGS        TARGET_IOR(BTRFS_IOCTL_MAGIC, 25, \
                                                           abi_ullong)
#define TARGET_BTRFS_IOC_SUBVOL_SETFLAGS        TARGET_IOW(BTRFS_IOCTL_MAGIC, 26, \
                                                           abi_ullong)
#define TARGET_BTRFS_IOC_SCRUB                  TARGET_IOWRU(BTRFS_IOCTL_MAGIC, 27)
#define TARGET_BTRFS_IOC_SCRUB_CANCEL           TARGET_IO(BTRFS_IOCTL_MAGIC, 28)
#define TARGET_BTRFS_IOC_SCRUB_PROGRESS         TARGET_IOWRU(BTRFS_IOCTL_MAGIC, 29)
#define TARGET_BTRFS_IOC_DEV_INFO               TARGET_IOWRU(BTRFS_IOCTL_MAGIC, 30)
#define TARGET_BTRFS_IOC_INO_PATHS              TARGET_IOWRU(BTRFS_IOCTL_MAGIC, 35)
#define TARGET_BTRFS_IOC_LOGICAL_INO            TARGET_IOWRU(BTRFS_IOCTL_MAGIC, 36)
#define TARGET_BTRFS_IOC_QUOTA_CTL              TARGET_IOWRU(BTRFS_IOCTL_MAGIC, 40)
#define TARGET_BTRFS_IOC_QGROUP_ASSIGN          TARGET_IOWU(BTRFS_IOCTL_MAGIC, 41)
#define TARGET_BTRFS_IOC_QGROUP_CREATE          TARGET_IOWU(BTRFS_IOCTL_MAGIC, 42)
#define TARGET_BTRFS_IOC_QGROUP_LIMIT           TARGET_IORU(BTRFS_IOCTL_MAGIC, 43)
#define TARGET_BTRFS_IOC_QUOTA_RESCAN           TARGET_IOWU(BTRFS_IOCTL_MAGIC, 44)
#define TARGET_BTRFS_IOC_QUOTA_RESCAN_STATUS    TARGET_IORU(BTRFS_IOCTL_MAGIC, 45)
#define TARGET_BTRFS_IOC_QUOTA_RESCAN_WAIT      TARGET_IO(BTRFS_IOCTL_MAGIC, 46)
#define TARGET_BTRFS_IOC_GET_DEV_STATS          TARGET_IOWRU(BTRFS_IOCTL_MAGIC, 52)
#define TARGET_BTRFS_IOC_GET_FEATURES           TARGET_IORU(BTRFS_IOCTL_MAGIC, 57)
#define TARGET_BTRFS_IOC_SET_FEATURES           TARGET_IOWU(BTRFS_IOCTL_MAGIC, 57)
#define TARGET_BTRFS_IOC_GET_SUPPORTED_FEATURES TARGET_IORU(BTRFS_IOCTL_MAGIC, 57)
#define TARGET_BTRFS_IOC_LOGICAL_INO_V2         TARGET_IOWRU(BTRFS_IOCTL_MAGIC, 59)
#define TARGET_BTRFS_IOC_GET_SUBVOL_INFO        TARGET_IORU(BTRFS_IOCTL_MAGIC, 60)
#define TARGET_BTRFS_IOC_GET_SUBVOL_ROOTREF     TARGET_IOWRU(BTRFS_IOCTL_MAGIC, 61)
#define TARGET_BTRFS_IOC_INO_LOOKUP_USER        TARGET_IOWRU(BTRFS_IOCTL_MAGIC, 62)
#endif

#define TARGET_USBDEVFS_CONTROL TARGET_IOWRU('U', 0)
#define TARGET_USBDEVFS_BULK TARGET_IOWRU('U', 2)
#define TARGET_USBDEVFS_RESETEP TARGET_IORU('U', 3)
#define TARGET_USBDEVFS_SETINTERFACE TARGET_IORU('U', 4)
#define TARGET_USBDEVFS_SETCONFIGURATION TARGET_IORU('U',  5)
#define TARGET_USBDEVFS_GETDRIVER TARGET_IOWU('U', 8)
#define TARGET_USBDEVFS_SUBMITURB TARGET_IORU('U', 10)
#define TARGET_USBDEVFS_DISCARDURB TARGET_IO('U', 11)
#define TARGET_USBDEVFS_REAPURB TARGET_IOWU('U', 12)
#define TARGET_USBDEVFS_REAPURBNDELAY TARGET_IOWU('U', 13)
#define TARGET_USBDEVFS_DISCSIGNAL TARGET_IORU('U', 14)
#define TARGET_USBDEVFS_CLAIMINTERFACE TARGET_IORU('U', 15)
#define TARGET_USBDEVFS_RELEASEINTERFACE TARGET_IORU('U', 16)
#define TARGET_USBDEVFS_CONNECTINFO TARGET_IOWU('U', 17)
#define TARGET_USBDEVFS_IOCTL TARGET_IOWRU('U', 18)
#define TARGET_USBDEVFS_HUB_PORTINFO TARGET_IORU('U', 19)
#define TARGET_USBDEVFS_RESET TARGET_IO('U', 20)
#define TARGET_USBDEVFS_CLEAR_HALT TARGET_IORU('U', 21)
#define TARGET_USBDEVFS_DISCONNECT TARGET_IO('U', 22)
#define TARGET_USBDEVFS_CONNECT TARGET_IO('U', 23)
#define TARGET_USBDEVFS_CLAIM_PORT TARGET_IORU('U', 24)
#define TARGET_USBDEVFS_RELEASE_PORT TARGET_IORU('U', 25)
#define TARGET_USBDEVFS_GET_CAPABILITIES TARGET_IORU('U', 26)
#define TARGET_USBDEVFS_DISCONNECT_CLAIM TARGET_IORU('U', 27)
#define TARGET_USBDEVFS_DROP_PRIVILEGES TARGET_IOWU('U', 30)
#define TARGET_USBDEVFS_GET_SPEED TARGET_IO('U', 31)

#define TARGET_CDROMPAUSE               0x5301
#define TARGET_CDROMRESUME              0x5302
#define TARGET_CDROMPLAYMSF             0x5303
#define TARGET_CDROMPLAYTRKIND          0x5304
#define TARGET_CDROMREADTOCHDR          0x5305
#define TARGET_CDROMREADTOCENTRY        0x5306
#define TARGET_CDROMSTOP                0x5307
#define TARGET_CDROMSTART               0x5308
#define TARGET_CDROMEJECT               0x5309
#define TARGET_CDROMVOLCTRL             0x530a
#define TARGET_CDROMSUBCHNL             0x530b
#define TARGET_CDROMREADMODE2           0x530c
#define TARGET_CDROMREADMODE1           0x530d
#define TARGET_CDROMREADAUDIO           0x530e
#define TARGET_CDROMEJECT_SW            0x530f
#define TARGET_CDROMMULTISESSION        0x5310
#define TARGET_CDROM_GET_MCN            0x5311
#define TARGET_CDROM_GET_UPC            TARGET_CDROM_GET_MCN
#define TARGET_CDROMRESET               0x5312
#define TARGET_CDROMVOLREAD             0x5313
#define TARGET_CDROMREADRAW             0x5314
#define TARGET_CDROMREADCOOKED          0x5315
#define TARGET_CDROMSEEK                0x5316

#define TARGET_CDROMPLAYBLK             0x5317

#define TARGET_CDROMREADALL             0x5318

#define TARGET_CDROMGETSPINDOWN        0x531d
#define TARGET_CDROMSETSPINDOWN        0x531e

#define TARGET_CDROMCLOSETRAY           0x5319
#define TARGET_CDROM_SET_OPTIONS        0x5320
#define TARGET_CDROM_CLEAR_OPTIONS      0x5321
#define TARGET_CDROM_SELECT_SPEED       0x5322
#define TARGET_CDROM_SELECT_DISC        0x5323
#define TARGET_CDROM_MEDIA_CHANGED      0x5325
#define TARGET_CDROM_DRIVE_STATUS       0x5326
#define TARGET_CDROM_DISC_STATUS        0x5327
#define TARGET_CDROM_CHANGER_NSLOTS    0x5328
#define TARGET_CDROM_LOCKDOOR           0x5329
#define TARGET_CDROM_DEBUG              0x5330
#define TARGET_CDROM_GET_CAPABILITY     0x5331

#define TARGET_CDROMAUDIOBUFSIZ        0x5382

#define TARGET_DVD_READ_STRUCT          0x5390
#define TARGET_DVD_WRITE_STRUCT 0x5391
#define TARGET_DVD_AUTH         0x5392

#define TARGET_CDROM_SEND_PACKET        0x5393
#define TARGET_CDROM_NEXT_WRITABLE      0x5394
#define TARGET_CDROM_LAST_WRITTEN       0x5395

#define TARGET_HDIO_GETGEO            0x0301
#define TARGET_HDIO_GET_UNMASKINTR    0x0302
#define TARGET_HDIO_GET_MULTCOUNT     0x0304
#define TARGET_HDIO_GET_KEEPSETTINGS  0x0308
#define TARGET_HDIO_GET_32BIT         0x0309
#define TARGET_HDIO_GET_NOWERR        0x030a
#define TARGET_HDIO_GET_DMA           0x030b
#define TARGET_HDIO_GET_IDENTITY      0x030d
#define TARGET_HDIO_DRIVE_CMD         0x031f

#define TARGET_HDIO_SET_MULTCOUNT     0x0321
#define TARGET_HDIO_SET_UNMASKINTR    0x0322
#define TARGET_HDIO_SET_KEEPSETTINGS  0x0323
#define TARGET_HDIO_SET_32BIT         0x0324
#define TARGET_HDIO_SET_NOWERR        0x0325
#define TARGET_HDIO_SET_DMA           0x0326
#define TARGET_HDIO_SET_PIO_MODE      0x0327

#define TARGET_LOOP_SET_FD            0x4C00
#define TARGET_LOOP_CLR_FD            0x4C01
#define TARGET_LOOP_SET_STATUS        0x4C02
#define TARGET_LOOP_GET_STATUS        0x4C03
#define TARGET_LOOP_SET_STATUS64      0x4C04
#define TARGET_LOOP_GET_STATUS64      0x4C05
#define TARGET_LOOP_CHANGE_FD         0x4C06
#define TARGET_LOOP_SET_CAPACITY      0x4C07
#define TARGET_LOOP_SET_DIRECT_IO     0x4C08
#define TARGET_LOOP_SET_BLOCK_SIZE    0x4C09
#define TARGET_LOOP_CONFIGURE         0x4C0A

#define TARGET_LOOP_CTL_ADD           0x4C80
#define TARGET_LOOP_CTL_REMOVE        0x4C81
#define TARGET_LOOP_CTL_GET_FREE      0x4C82

#define TARGET_FBIOGET_VSCREENINFO    0x4600
#define TARGET_FBIOPUT_VSCREENINFO    0x4601
#define TARGET_FBIOGET_FSCREENINFO    0x4602
#define TARGET_FBIOGETCMAP            0x4604
#define TARGET_FBIOPUTCMAP            0x4605
#define TARGET_FBIOPAN_DISPLAY        0x4606
#define TARGET_FBIOGET_CON2FBMAP      0x460F
#define TARGET_FBIOPUT_CON2FBMAP      0x4610

#define TARGET_VT_OPENQRY             0x5600
#define TARGET_VT_GETSTATE            0x5603
#define TARGET_VT_ACTIVATE            0x5606
#define TARGET_VT_WAITACTIVE          0x5607
#define TARGET_VT_LOCKSWITCH          0x560b
#define TARGET_VT_UNLOCKSWITCH        0x560c
#define TARGET_VT_GETMODE             0x5601
#define TARGET_VT_SETMODE             0x5602
#define TARGET_VT_RELDISP             0x5605
#define TARGET_VT_DISALLOCATE         0x5608

#define TARGET_DM_VERSION             TARGET_IOWRU(0xfd, 0x00)
#define TARGET_DM_REMOVE_ALL          TARGET_IOWRU(0xfd, 0x01)
#define TARGET_DM_LIST_DEVICES        TARGET_IOWRU(0xfd, 0x02)
#define TARGET_DM_DEV_CREATE          TARGET_IOWRU(0xfd, 0x03)
#define TARGET_DM_DEV_REMOVE          TARGET_IOWRU(0xfd, 0x04)
#define TARGET_DM_DEV_RENAME          TARGET_IOWRU(0xfd, 0x05)
#define TARGET_DM_DEV_SUSPEND         TARGET_IOWRU(0xfd, 0x06)
#define TARGET_DM_DEV_STATUS          TARGET_IOWRU(0xfd, 0x07)
#define TARGET_DM_DEV_WAIT            TARGET_IOWRU(0xfd, 0x08)
#define TARGET_DM_TABLE_LOAD          TARGET_IOWRU(0xfd, 0x09)
#define TARGET_DM_TABLE_CLEAR         TARGET_IOWRU(0xfd, 0x0a)
#define TARGET_DM_TABLE_DEPS          TARGET_IOWRU(0xfd, 0x0b)
#define TARGET_DM_TABLE_STATUS        TARGET_IOWRU(0xfd, 0x0c)
#define TARGET_DM_LIST_VERSIONS       TARGET_IOWRU(0xfd, 0x0d)
#define TARGET_DM_TARGET_MSG          TARGET_IOWRU(0xfd, 0x0e)
#define TARGET_DM_DEV_SET_GEOMETRY    TARGET_IOWRU(0xfd, 0x0f)

#define TARGET_DRM_IOCTL_VERSION      TARGET_IOWRU('d', 0x00)

#define TARGET_DRM_IOCTL_I915_GETPARAM              TARGET_IOWRU('d', 0x46)

#define TARGET_NCC 8
struct target_termio {
    abi_ushort c_iflag;
    abi_ushort c_oflag;
    abi_ushort c_cflag;
    abi_ushort c_lflag;
    unsigned char c_line;
    unsigned char c_cc[TARGET_NCC];
};

struct target_winsize {
    abi_ushort ws_row;
    abi_ushort ws_col;
    abi_ushort ws_xpixel;
    abi_ushort ws_ypixel;
};

#include "termbits.h"

#include "target_mman.h"

#if (defined(TARGET_I386) && defined(TARGET_ABI32))     \
    || (defined(TARGET_ARM) && defined(TARGET_ABI32))
#define TARGET_STAT_HAVE_NSEC
struct target_stat {
    abi_ushort st_dev;
    abi_ushort __pad1;
    abi_ulong st_ino;
    abi_ushort st_mode;
    abi_ushort st_nlink;
    abi_ushort st_uid;
    abi_ushort st_gid;
    abi_ushort st_rdev;
    abi_ushort __pad2;
    abi_ulong  st_size;
    abi_ulong  st_blksize;
    abi_ulong  st_blocks;
    abi_ulong  target_st_atime;
    abi_ulong  target_st_atime_nsec;
    abi_ulong  target_st_mtime;
    abi_ulong  target_st_mtime_nsec;
    abi_ulong  target_st_ctime;
    abi_ulong  target_st_ctime_nsec;
    abi_ulong  __unused4;
    abi_ulong  __unused5;
};

#define TARGET_HAS_STRUCT_STAT64
struct target_stat64 {
    abi_ushort      st_dev;
    unsigned char   __pad0[10];

#define TARGET_STAT64_HAS_BROKEN_ST_INO 1
    abi_ulong       __st_ino;

    abi_uint        st_mode;
    abi_uint        st_nlink;

    abi_ulong       st_uid;
    abi_ulong       st_gid;

    abi_ushort      st_rdev;
    unsigned char   __pad3[10];

    abi_llong       st_size;
    abi_ulong       st_blksize;

    abi_ulong       st_blocks;
    abi_ulong       __pad4;

    abi_ulong       target_st_atime;
    abi_ulong       target_st_atime_nsec;

    abi_ulong       target_st_mtime;
    abi_ulong       target_st_mtime_nsec;

    abi_ulong       target_st_ctime;
    abi_ulong       target_st_ctime_nsec;

    abi_ullong      st_ino;
} QEMU_PACKED;

#ifdef TARGET_ARM
#define TARGET_HAS_STRUCT_STAT64
struct target_eabi_stat64 {
    abi_ullong   st_dev;
    abi_uint     __pad1;
    abi_ulong    __st_ino;
    abi_uint     st_mode;
    abi_uint     st_nlink;

    abi_ulong    st_uid;
    abi_ulong    st_gid;

    abi_ullong   st_rdev;
    abi_uint     __pad2[2];

    abi_llong       st_size;
    abi_ulong    st_blksize;
    abi_uint     __pad3;
    abi_ullong   st_blocks;

    abi_ulong    target_st_atime;
    abi_ulong    target_st_atime_nsec;

    abi_ulong    target_st_mtime;
    abi_ulong    target_st_mtime_nsec;

    abi_ulong    target_st_ctime;
    abi_ulong    target_st_ctime_nsec;

    abi_ullong   st_ino;
} QEMU_PACKED;
#endif

#elif defined(TARGET_SPARC64) && !defined(TARGET_ABI32)
struct target_stat {
    abi_uint        st_dev;
    abi_ulong       st_ino;
    abi_uint        st_mode;
    abi_uint        st_nlink;
    abi_uint        st_uid;
    abi_uint        st_gid;
    abi_uint        st_rdev;
    abi_long        st_size;
    abi_long        target_st_atime;
    abi_long        target_st_mtime;
    abi_long        target_st_ctime;
    abi_long        st_blksize;
    abi_long        st_blocks;
    abi_ulong       __unused4[2];
};

#define TARGET_HAS_STRUCT_STAT64
struct target_stat64 {
    unsigned char   __pad0[6];
    abi_ushort      st_dev;

    abi_ullong      st_ino;
    abi_ullong      st_nlink;

    abi_uint        st_mode;

    abi_uint        st_uid;
    abi_uint        st_gid;

    unsigned char   __pad2[6];
    abi_ushort      st_rdev;

    abi_llong       st_size;
    abi_llong       st_blksize;

    unsigned char   __pad4[4];
    abi_uint        st_blocks;

    abi_ulong       target_st_atime;
    abi_ulong       target_st_atime_nsec;

    abi_ulong       target_st_mtime;
    abi_ulong       target_st_mtime_nsec;

    abi_ulong       target_st_ctime;
    abi_ulong       target_st_ctime_nsec;

    abi_ulong       __unused4[3];
};

#elif defined(TARGET_SPARC)

#define TARGET_STAT_HAVE_NSEC
struct target_stat {
    abi_ushort      st_dev;
    abi_ulong       st_ino;
    abi_ushort      st_mode;
    abi_short       st_nlink;
    abi_ushort      st_uid;
    abi_ushort      st_gid;
    abi_ushort      st_rdev;
    abi_long        st_size;
    abi_long        target_st_atime;
    abi_ulong       target_st_atime_nsec;
    abi_long        target_st_mtime;
    abi_ulong       target_st_mtime_nsec;
    abi_long        target_st_ctime;
    abi_ulong       target_st_ctime_nsec;
    abi_long        st_blksize;
    abi_long        st_blocks;
    abi_ulong       __unused1[2];
};

#define TARGET_HAS_STRUCT_STAT64
struct target_stat64 {
    unsigned char   __pad0[6];
    abi_ushort      st_dev;

    abi_ullong      st_ino;

    abi_uint        st_mode;
    abi_uint        st_nlink;

    abi_uint        st_uid;
    abi_uint        st_gid;

    unsigned char   __pad2[6];
    abi_ushort      st_rdev;

    unsigned char   __pad3[8];

    abi_llong       st_size;
    abi_uint        st_blksize;

    unsigned char   __pad4[8];
    abi_uint        st_blocks;

    abi_uint        target_st_atime;
    abi_uint        target_st_atime_nsec;

    abi_uint        target_st_mtime;
    abi_uint        target_st_mtime_nsec;

    abi_uint        target_st_ctime;
    abi_uint        target_st_ctime_nsec;

    abi_uint        __unused1;
    abi_uint        __unused2;
};

#elif defined(TARGET_PPC)

#define TARGET_STAT_HAVE_NSEC
struct target_stat {
    abi_ulong st_dev;
    abi_ulong st_ino;
#if defined(TARGET_PPC64)
    abi_ulong st_nlink;
    abi_uint  st_mode;
#else
    abi_uint  st_mode;
    abi_ushort st_nlink;
#endif
    abi_uint   st_uid;
    abi_uint   st_gid;
    abi_ulong  st_rdev;
    abi_ulong  st_size;
    abi_ulong  st_blksize;
    abi_ulong  st_blocks;
    abi_ulong  target_st_atime;
    abi_ulong  target_st_atime_nsec;
    abi_ulong  target_st_mtime;
    abi_ulong  target_st_mtime_nsec;
    abi_ulong  target_st_ctime;
    abi_ulong  target_st_ctime_nsec;
    abi_ulong  __unused4;
    abi_ulong  __unused5;
#if defined(TARGET_PPC64)
    abi_ulong  __unused6;
#endif
};

#if !defined(TARGET_PPC64)
#define TARGET_HAS_STRUCT_STAT64
struct QEMU_PACKED target_stat64 {
    abi_ullong st_dev;
    abi_ullong st_ino;
    abi_uint st_mode;
    abi_uint st_nlink;
    abi_uint st_uid;
    abi_uint st_gid;
    abi_ullong st_rdev;
    abi_ullong __pad0;
    abi_llong      st_size;
    abi_int        st_blksize;
    abi_uint       __pad1;
    abi_llong      st_blocks;
    abi_int        target_st_atime;
    abi_uint       target_st_atime_nsec;
    abi_int        target_st_mtime;
    abi_uint       target_st_mtime_nsec;
    abi_int        target_st_ctime;
    abi_uint       target_st_ctime_nsec;
    abi_uint       __unused4;
    abi_uint       __unused5;
};
#endif

#elif defined(TARGET_MICROBLAZE)

#define TARGET_STAT_HAVE_NSEC
struct target_stat {
    abi_ulong st_dev;
    abi_ulong st_ino;
    abi_uint st_mode;
    abi_ushort st_nlink;
    abi_uint st_uid;
    abi_uint st_gid;
    abi_ulong  st_rdev;
    abi_ulong  st_size;
    abi_ulong  st_blksize;
    abi_ulong  st_blocks;
    abi_ulong  target_st_atime;
    abi_ulong  target_st_atime_nsec;
    abi_ulong  target_st_mtime;
    abi_ulong  target_st_mtime_nsec;
    abi_ulong  target_st_ctime;
    abi_ulong  target_st_ctime_nsec;
    abi_ulong  __unused4;
    abi_ulong  __unused5;
};

#define TARGET_HAS_STRUCT_STAT64
struct QEMU_PACKED target_stat64 {
    abi_ullong st_dev;
#define TARGET_STAT64_HAS_BROKEN_ST_INO 1
    abi_uint pad0;
    abi_uint __st_ino;

    abi_uint st_mode;
    abi_uint st_nlink;
    abi_uint st_uid;
    abi_uint st_gid;
    abi_ullong st_rdev;
    abi_ullong __pad1;

    abi_llong st_size;
    abi_int  st_blksize;
    abi_uint __pad2;
    abi_llong st_blocks;

    abi_int    target_st_atime;
    abi_uint   target_st_atime_nsec;
    abi_int    target_st_mtime;
    abi_uint   target_st_mtime_nsec;
    abi_int    target_st_ctime;
    abi_uint   target_st_ctime_nsec;
    abi_ullong st_ino;
};

#elif defined(TARGET_M68K)

struct target_stat {
    abi_ushort st_dev;
    abi_ushort __pad1;
    abi_ulong  st_ino;
    abi_ushort st_mode;
    abi_ushort st_nlink;
    abi_ushort st_uid;
    abi_ushort st_gid;
    abi_ushort st_rdev;
    abi_ushort __pad2;
    abi_ulong  st_size;
    abi_ulong  st_blksize;
    abi_ulong  st_blocks;
    abi_ulong  target_st_atime;
    abi_ulong  __unused1;
    abi_ulong  target_st_mtime;
    abi_ulong  __unused2;
    abi_ulong  target_st_ctime;
    abi_ulong  __unused3;
    abi_ulong  __unused4;
    abi_ulong  __unused5;
};

#define TARGET_HAS_STRUCT_STAT64
struct target_stat64 {
    abi_ullong      st_dev;
    unsigned char   __pad1[2];

#define TARGET_STAT64_HAS_BROKEN_ST_INO 1
    abi_ulong       __st_ino;

    abi_uint    st_mode;
    abi_uint    st_nlink;

    abi_ulong       st_uid;
    abi_ulong       st_gid;

    abi_ullong      st_rdev;
    unsigned char   __pad3[2];

    abi_llong       st_size;
    abi_ulong       st_blksize;

    abi_ulong       __pad4;
    abi_ulong       st_blocks;

    abi_ulong       target_st_atime;
    abi_ulong       target_st_atime_nsec;

    abi_ulong       target_st_mtime;
    abi_ulong       target_st_mtime_nsec;

    abi_ulong       target_st_ctime;
    abi_ulong       target_st_ctime_nsec;

    abi_ullong      st_ino;
} QEMU_PACKED;

#elif defined(TARGET_ABI_MIPSN64)

#define TARGET_STAT_HAVE_NSEC
struct target_stat {
    abi_uint                st_dev;
    abi_uint                st_pad0[3];

    abi_ulong               st_ino;

    abi_uint                st_mode;
    abi_uint                st_nlink;

    abi_int                 st_uid;
    abi_int                 st_gid;

    abi_uint                st_rdev;
    abi_uint                st_pad1[3];

    abi_ulong               st_size;

    abi_uint                target_st_atime;
    abi_uint                target_st_atime_nsec;

    abi_uint                target_st_mtime;
    abi_uint                target_st_mtime_nsec;

    abi_uint                target_st_ctime;
    abi_uint                target_st_ctime_nsec;

    abi_uint                st_blksize;
    abi_uint                st_pad2;

    abi_ulong               st_blocks;
};

#elif defined(TARGET_ABI_MIPSN32)

#define TARGET_STAT_HAVE_NSEC
struct target_stat {
    abi_ulong    st_dev;
    abi_ulong    st_pad0[3];
    abi_ullong   st_ino;
    abi_uint     st_mode;
    abi_uint     st_nlink;
    abi_int      st_uid;
    abi_int      st_gid;
    abi_ulong    st_rdev;
    abi_ulong    st_pad1[3];
    abi_llong    st_size;
    abi_long     target_st_atime;
    abi_ulong    target_st_atime_nsec;
    abi_long     target_st_mtime;
    abi_ulong    target_st_mtime_nsec;
    abi_long     target_st_ctime;
    abi_ulong    target_st_ctime_nsec;
    abi_ulong    st_blksize;
    abi_ulong    st_pad2;
    abi_llong    st_blocks;
};

#elif defined(TARGET_ABI_MIPSO32)

#define TARGET_STAT_HAVE_NSEC
struct target_stat {
    abi_uint        st_dev;
    abi_long        st_pad1[3];
    abi_ulong       st_ino;
    abi_uint        st_mode;
    abi_uint        st_nlink;
    abi_int         st_uid;
    abi_int         st_gid;
    abi_uint        st_rdev;
    abi_long        st_pad2[2];
    abi_long        st_size;
    abi_long        st_pad3;
    abi_long                target_st_atime;
    abi_long                target_st_atime_nsec;
    abi_long                target_st_mtime;
    abi_long                target_st_mtime_nsec;
    abi_long                target_st_ctime;
    abi_long                target_st_ctime_nsec;
    abi_long                st_blksize;
    abi_long                st_blocks;
    abi_long                st_pad4[14];
};

#define TARGET_HAS_STRUCT_STAT64
struct target_stat64 {
    abi_ulong       st_dev;
    abi_ulong       st_pad0[3];

    abi_ullong      st_ino;

    abi_uint        st_mode;
    abi_uint        st_nlink;

    abi_int         st_uid;
    abi_int         st_gid;

    abi_ulong       st_rdev;
    abi_ulong       st_pad1[3];

    abi_llong       st_size;

    abi_long        target_st_atime;
    abi_ulong       target_st_atime_nsec;

    abi_long        target_st_mtime;
    abi_ulong       target_st_mtime_nsec;

    abi_long        target_st_ctime;
    abi_ulong       target_st_ctime_nsec;

    abi_ulong       st_blksize;
    abi_ulong       st_pad2;

    abi_llong       st_blocks;
};

#elif defined(TARGET_ALPHA)

struct target_stat {
    abi_uint     st_dev;
    abi_uint     st_ino;
    abi_uint     st_mode;
    abi_uint     st_nlink;
    abi_uint     st_uid;
    abi_uint     st_gid;
    abi_uint     st_rdev;
    abi_long     st_size;
    abi_ulong    target_st_atime;
    abi_ulong    target_st_mtime;
    abi_ulong    target_st_ctime;
    abi_uint     st_blksize;
    abi_uint     st_blocks;
    abi_uint     st_flags;
    abi_uint     st_gen;
};

#define TARGET_HAS_STRUCT_STAT64
struct target_stat64 {
    abi_ulong    st_dev;
    abi_ulong    st_ino;
    abi_ulong    st_rdev;
    abi_long     st_size;
    abi_ulong    st_blocks;

    abi_uint     st_mode;
    abi_uint     st_uid;
    abi_uint     st_gid;
    abi_uint     st_blksize;
    abi_uint     st_nlink;
    abi_uint     __pad0;

    abi_ulong    target_st_atime;
    abi_ulong    target_st_atime_nsec;
    abi_ulong    target_st_mtime;
    abi_ulong    target_st_mtime_nsec;
    abi_ulong    target_st_ctime;
    abi_ulong    target_st_ctime_nsec;
    abi_long     __unused[3];
};

#elif defined(TARGET_SH4)

#define TARGET_STAT_HAVE_NSEC
struct target_stat {
    abi_ulong  st_dev;
    abi_ulong  st_ino;
    abi_ushort st_mode;
    abi_ushort st_nlink;
    abi_ushort st_uid;
    abi_ushort st_gid;
    abi_ulong  st_rdev;
    abi_ulong  st_size;
    abi_ulong  st_blksize;
    abi_ulong  st_blocks;
    abi_ulong  target_st_atime;
    abi_ulong  target_st_atime_nsec;
    abi_ulong  target_st_mtime;
    abi_ulong  target_st_mtime_nsec;
    abi_ulong  target_st_ctime;
    abi_ulong  target_st_ctime_nsec;
    abi_ulong  __unused4;
    abi_ulong  __unused5;
};

#define TARGET_HAS_STRUCT_STAT64
struct QEMU_PACKED target_stat64 {
    abi_ullong      st_dev;
    unsigned char   __pad0[4];

#define TARGET_STAT64_HAS_BROKEN_ST_INO 1
    abi_ulong       __st_ino;

    abi_uint        st_mode;
    abi_uint        st_nlink;

    abi_ulong       st_uid;
    abi_ulong       st_gid;

    abi_ullong      st_rdev;
    unsigned char   __pad3[4];

    abi_llong       st_size;
    abi_ulong       st_blksize;

    abi_ullong      st_blocks;

    abi_ulong       target_st_atime;
    abi_ulong       target_st_atime_nsec;

    abi_ulong       target_st_mtime;
    abi_ulong       target_st_mtime_nsec;

    abi_ulong       target_st_ctime;
    abi_ulong       target_st_ctime_nsec;

    abi_ullong      st_ino;
};

#elif defined(TARGET_I386) && !defined(TARGET_ABI32)
#define TARGET_STAT_HAVE_NSEC
struct target_stat {
    abi_ulong       st_dev;
    abi_ulong       st_ino;
    abi_ulong       st_nlink;

    abi_uint        st_mode;
    abi_uint        st_uid;
    abi_uint        st_gid;
    abi_uint        __pad0;
    abi_ulong       st_rdev;
    abi_long        st_size;
    abi_long        st_blksize;
    abi_long        st_blocks;

    abi_ulong       target_st_atime;
    abi_ulong       target_st_atime_nsec;
    abi_ulong       target_st_mtime;
    abi_ulong       target_st_mtime_nsec;
    abi_ulong       target_st_ctime;
    abi_ulong       target_st_ctime_nsec;

    abi_long        __unused[3];
};
#elif defined(TARGET_S390X)
struct target_stat {
    abi_ulong  st_dev;
    abi_ulong  st_ino;
    abi_ulong  st_nlink;
    abi_uint   st_mode;
    abi_uint   st_uid;
    abi_uint   st_gid;
    abi_uint   __pad1;
    abi_ulong  st_rdev;
    abi_ulong  st_size;
    abi_ulong  target_st_atime;
    abi_ulong  target_st_atime_nsec;
    abi_ulong  target_st_mtime;
    abi_ulong  target_st_mtime_nsec;
    abi_ulong  target_st_ctime;
    abi_ulong  target_st_ctime_nsec;
    abi_ulong  st_blksize;
    abi_long       st_blocks;
    abi_ulong  __unused[3];
};
#elif defined(TARGET_AARCH64)
#define TARGET_STAT_HAVE_NSEC
struct target_stat {
    abi_ulong  st_dev;
    abi_ulong  st_ino;
    abi_uint   st_mode;
    abi_uint   st_nlink;
    abi_uint   st_uid;
    abi_uint   st_gid;
    abi_ulong  st_rdev;
    abi_ulong  _pad1;
    abi_long  st_size;
    abi_int    st_blksize;
    abi_int    __pad2;
    abi_long   st_blocks;
    abi_long  target_st_atime;
    abi_ulong  target_st_atime_nsec;
    abi_long  target_st_mtime;
    abi_ulong  target_st_mtime_nsec;
    abi_long  target_st_ctime;
    abi_ulong  target_st_ctime_nsec;
    abi_uint __unused[2];
};
#elif defined(TARGET_XTENSA)
#define TARGET_STAT_HAVE_NSEC
struct target_stat {
    abi_ulong       st_dev;
    abi_ulong       st_ino;
    abi_uint        st_mode;
    abi_uint        st_nlink;
    abi_uint        st_uid;
    abi_uint        st_gid;
    abi_ulong       st_rdev;
    abi_long        st_size;
    abi_ulong       st_blksize;
    abi_ulong       st_blocks;
    abi_ulong       target_st_atime;
    abi_ulong       target_st_atime_nsec;
    abi_ulong       target_st_mtime;
    abi_ulong       target_st_mtime_nsec;
    abi_ulong       target_st_ctime;
    abi_ulong       target_st_ctime_nsec;
    abi_ulong       __unused4;
    abi_ulong       __unused5;
};

#define TARGET_HAS_STRUCT_STAT64
struct target_stat64  {
    abi_ullong st_dev;
    abi_ullong st_ino;
    abi_uint  st_mode;
    abi_uint  st_nlink;
    abi_uint  st_uid;
    abi_uint  st_gid;
    abi_ullong st_rdev;
    abi_llong st_size;
    abi_ulong st_blksize;
    abi_ulong __unused2;
    abi_ullong st_blocks;
    abi_ulong target_st_atime;
    abi_ulong target_st_atime_nsec;
    abi_ulong target_st_mtime;
    abi_ulong target_st_mtime_nsec;
    abi_ulong target_st_ctime;
    abi_ulong target_st_ctime_nsec;
    abi_ulong __unused4;
    abi_ulong __unused5;
};

#elif defined(TARGET_OPENRISC) \
    || defined(TARGET_RISCV) || defined(TARGET_HEXAGON) || defined(TARGET_LOONGARCH)

#define TARGET_STAT_HAVE_NSEC
struct target_stat {
    abi_ulong st_dev;
    abi_ulong st_ino;
    abi_uint st_mode;
    abi_uint st_nlink;
    abi_uint st_uid;
    abi_uint st_gid;
    abi_ulong st_rdev;
    abi_ulong __pad1;
    abi_long st_size;
    abi_int st_blksize;
    abi_int __pad2;
    abi_long st_blocks;
    abi_long target_st_atime;
    abi_ulong target_st_atime_nsec;
    abi_long target_st_mtime;
    abi_ulong target_st_mtime_nsec;
    abi_long target_st_ctime;
    abi_ulong target_st_ctime_nsec;
    abi_uint __unused4;
    abi_uint __unused5;
};

#if !defined(TARGET_RISCV64)
#define TARGET_HAS_STRUCT_STAT64
struct target_stat64 {
    abi_ullong st_dev;
    abi_ullong st_ino;
    abi_uint st_mode;
    abi_uint st_nlink;
    abi_uint st_uid;
    abi_uint st_gid;
    abi_ullong st_rdev;
    abi_ullong __pad1;
    abi_llong st_size;
    abi_int st_blksize;
    abi_int __pad2;
    abi_llong st_blocks;
    abi_int target_st_atime;
    abi_uint target_st_atime_nsec;
    abi_int target_st_mtime;
    abi_uint target_st_mtime_nsec;
    abi_int target_st_ctime;
    abi_uint target_st_ctime_nsec;
    abi_uint __unused4;
    abi_uint __unused5;
};
#endif

#elif defined(TARGET_HPPA)

#define TARGET_STAT_HAVE_NSEC
struct target_stat {
    abi_uint   st_dev;
    abi_uint   st_ino;
    abi_ushort st_mode;
    abi_ushort st_nlink;
    abi_ushort _res1;
    abi_ushort _res2;
    abi_uint   st_rdev;
    abi_int    st_size;
    abi_int    target_st_atime;
    abi_uint   target_st_atime_nsec;
    abi_int    target_st_mtime;
    abi_uint   target_st_mtime_nsec;
    abi_int    target_st_ctime;
    abi_uint   target_st_ctime_nsec;
    abi_int    st_blksize;
    abi_int    st_blocks;
    abi_uint   _unused1;
    abi_uint   _unused2;
    abi_uint   _unused3;
    abi_uint   _unused4;
    abi_ushort _unused5;
    abi_short  st_fstype;
    abi_uint   st_realdev;
    abi_ushort st_basemode;
    abi_ushort _unused6;
    abi_uint   st_uid;
    abi_uint   st_gid;
    abi_uint   _unused7[3];
};

#define TARGET_HAS_STRUCT_STAT64
struct target_stat64 {
    abi_ullong st_dev;
    abi_uint   _pad1;
    abi_uint   _res1;
    abi_uint   st_mode;
    abi_uint   st_nlink;
    abi_uint   st_uid;
    abi_uint   st_gid;
    abi_ullong st_rdev;
    abi_uint   _pad2;
    abi_llong  st_size;
    abi_int    st_blksize;
    abi_llong  st_blocks;
    abi_int    target_st_atime;
    abi_uint   target_st_atime_nsec;
    abi_int    target_st_mtime;
    abi_uint   target_st_mtime_nsec;
    abi_int    target_st_ctime;
    abi_uint   target_st_ctime_nsec;
    abi_ullong st_ino;
};
#else
#error unsupported CPU
#endif

typedef struct {
    abi_int val[2];
} target_fsid_t;

#ifdef TARGET_MIPS
#ifdef TARGET_ABI_MIPSN32
struct target_statfs {
    abi_int                 f_type;
    abi_int                 f_bsize;
    abi_int                 f_frsize;
    abi_int                 f_blocks;
    abi_int                 f_bfree;
    abi_int                 f_files;
    abi_int                 f_ffree;
    abi_int                 f_bavail;

    target_fsid_t           f_fsid;
    abi_int                 f_namelen;
    abi_int                 f_flags;
    abi_int                 f_spare[5];
};
#else
struct target_statfs {
    abi_long                f_type;
    abi_long                f_bsize;
    abi_long                f_frsize;
    abi_long                f_blocks;
    abi_long                f_bfree;
    abi_long                f_files;
    abi_long                f_ffree;
    abi_long                f_bavail;

    target_fsid_t           f_fsid;
    abi_long                f_namelen;
    abi_long                f_flags;
    abi_long                f_spare[5];
};
#endif

struct target_statfs64 {
    abi_uint        f_type;
    abi_uint        f_bsize;
    abi_uint        f_frsize;
    abi_uint        __pad;
    abi_ullong      f_blocks;
    abi_ullong      f_bfree;
    abi_ullong      f_files;
    abi_ullong      f_ffree;
    abi_ullong      f_bavail;
    target_fsid_t   f_fsid;
    abi_uint        f_namelen;
    abi_uint        f_flags;
    abi_uint        f_spare[5];
};
#elif (defined(TARGET_PPC64) || defined(TARGET_X86_64) ||       \
       defined(TARGET_SPARC64) || defined(TARGET_AARCH64) ||    \
       defined(TARGET_RISCV) || defined(TARGET_LOONGARCH64)) && \
    !defined(TARGET_ABI32)
struct target_statfs {
    abi_long f_type;
    abi_long f_bsize;
    abi_long f_blocks;
    abi_long f_bfree;
    abi_long f_bavail;
    abi_long f_files;
    abi_long f_ffree;
    target_fsid_t f_fsid;
    abi_long f_namelen;
    abi_long f_frsize;
    abi_long f_flags;
    abi_long f_spare[4];
};

struct target_statfs64 {
    abi_long f_type;
    abi_long f_bsize;
    abi_long f_blocks;
    abi_long f_bfree;
    abi_long f_bavail;
    abi_long f_files;
    abi_long f_ffree;
    target_fsid_t f_fsid;
    abi_long f_namelen;
    abi_long f_frsize;
    abi_long f_flags;
    abi_long f_spare[4];
};
#elif defined(TARGET_S390X)
struct target_statfs {
    abi_int  f_type;
    abi_int  f_bsize;
    abi_long f_blocks;
    abi_long f_bfree;
    abi_long f_bavail;
    abi_long f_files;
    abi_long f_ffree;
    kernel_fsid_t f_fsid;
    abi_int  f_namelen;
    abi_int  f_frsize;
    abi_int  f_flags;
    abi_int  f_spare[4];

};

struct target_statfs64 {
    abi_int  f_type;
    abi_int  f_bsize;
    abi_long f_blocks;
    abi_long f_bfree;
    abi_long f_bavail;
    abi_long f_files;
    abi_long f_ffree;
    kernel_fsid_t f_fsid;
    abi_int  f_namelen;
    abi_int  f_frsize;
    abi_int  f_flags;
    abi_int  f_spare[4];
};
#else
struct target_statfs {
    abi_uint f_type;
    abi_uint f_bsize;
    abi_uint f_blocks;
    abi_uint f_bfree;
    abi_uint f_bavail;
    abi_uint f_files;
    abi_uint f_ffree;
    target_fsid_t f_fsid;
    abi_uint f_namelen;
    abi_uint f_frsize;
    abi_uint f_flags;
    abi_uint f_spare[4];
};

struct target_statfs64 {
    abi_uint f_type;
    abi_uint f_bsize;
    abi_ullong f_blocks;
    abi_ullong f_bfree;
    abi_ullong f_bavail;
    abi_ullong f_files;
    abi_ullong f_ffree;
    target_fsid_t f_fsid;
    abi_uint f_namelen;
    abi_uint f_frsize;
    abi_uint f_flags;
    abi_uint f_spare[4];
};
#endif

#define TARGET_F_LINUX_SPECIFIC_BASE 1024
#define TARGET_F_SETLEASE            (TARGET_F_LINUX_SPECIFIC_BASE + 0)
#define TARGET_F_GETLEASE            (TARGET_F_LINUX_SPECIFIC_BASE + 1)
#define TARGET_F_DUPFD_CLOEXEC       (TARGET_F_LINUX_SPECIFIC_BASE + 6)
#define TARGET_F_NOTIFY              (TARGET_F_LINUX_SPECIFIC_BASE + 2)
#define TARGET_F_SETPIPE_SZ          (TARGET_F_LINUX_SPECIFIC_BASE + 7)
#define TARGET_F_GETPIPE_SZ          (TARGET_F_LINUX_SPECIFIC_BASE + 8)
#define TARGET_F_ADD_SEALS           (TARGET_F_LINUX_SPECIFIC_BASE + 9)
#define TARGET_F_GET_SEALS           (TARGET_F_LINUX_SPECIFIC_BASE + 10)

#include "target_fcntl.h"

#define TARGET_SNDCTL_COPR_HALT           TARGET_IOWR('C',  7, abi_int);
#define TARGET_SNDCTL_COPR_LOAD           0xcfb04301
#define TARGET_SNDCTL_COPR_RCODE          0xc0144303
#define TARGET_SNDCTL_COPR_RCVMSG         0x8fa44309
#define TARGET_SNDCTL_COPR_RDATA          0xc0144302
#define TARGET_SNDCTL_COPR_RESET          0x00004300
#define TARGET_SNDCTL_COPR_RUN            0xc0144306
#define TARGET_SNDCTL_COPR_SENDMSG        0xcfa44308
#define TARGET_SNDCTL_COPR_WCODE          0x40144305
#define TARGET_SNDCTL_COPR_WDATA          0x40144304
#define TARGET_SNDCTL_DSP_RESET           TARGET_IO('P', 0)
#define TARGET_SNDCTL_DSP_SYNC            TARGET_IO('P', 1)
#define TARGET_SNDCTL_DSP_SPEED           TARGET_IOWR('P', 2, abi_int)
#define TARGET_SNDCTL_DSP_STEREO          TARGET_IOWR('P', 3, abi_int)
#define TARGET_SNDCTL_DSP_GETBLKSIZE      TARGET_IOWR('P', 4, abi_int)
#define TARGET_SNDCTL_DSP_SETFMT          TARGET_IOWR('P', 5, abi_int)
#define TARGET_SNDCTL_DSP_CHANNELS        TARGET_IOWR('P', 6, abi_int)
#define TARGET_SOUND_PCM_WRITE_FILTER     TARGET_IOWR('P', 7, abi_int)
#define TARGET_SNDCTL_DSP_POST            TARGET_IO('P', 8)
#define TARGET_SNDCTL_DSP_SUBDIVIDE       TARGET_IOWR('P', 9, abi_int)
#define TARGET_SNDCTL_DSP_SETFRAGMENT     TARGET_IOWR('P',10, abi_int)
#define TARGET_SNDCTL_DSP_GETFMTS         TARGET_IOR('P', 11, abi_int)
#define TARGET_SNDCTL_DSP_GETOSPACE       TARGET_IORU('P',12)
#define TARGET_SNDCTL_DSP_GETISPACE       TARGET_IORU('P',13)
#define TARGET_SNDCTL_DSP_GETCAPS         TARGET_IOR('P', 15, abi_int)
#define TARGET_SNDCTL_DSP_GETTRIGGER      TARGET_IOR('P',16, abi_int)
#define TARGET_SNDCTL_DSP_GETIPTR         TARGET_IORU('P',17)
#define TARGET_SNDCTL_DSP_GETOPTR         TARGET_IORU('P',18)
#define TARGET_SNDCTL_DSP_MAPINBUF        TARGET_IORU('P', 19)
#define TARGET_SNDCTL_DSP_MAPOUTBUF       TARGET_IORU('P', 20)
#define TARGET_SNDCTL_DSP_NONBLOCK        0x0000500e
#define TARGET_SNDCTL_DSP_SAMPLESIZE      0xc0045005
#define TARGET_SNDCTL_DSP_SETDUPLEX       0x00005016
#define TARGET_SNDCTL_DSP_SETSYNCRO       0x00005015
#define TARGET_SNDCTL_DSP_SETTRIGGER      0x40045010
#define TARGET_SNDCTL_FM_4OP_ENABLE       0x4004510f
#define TARGET_SNDCTL_FM_LOAD_INSTR       0x40285107
#define TARGET_SNDCTL_MIDI_INFO           0xc074510c
#define TARGET_SNDCTL_MIDI_MPUCMD         0xc0216d02
#define TARGET_SNDCTL_MIDI_MPUMODE        0xc0046d01
#define TARGET_SNDCTL_MIDI_PRETIME        0xc0046d00
#define TARGET_SNDCTL_PMGR_ACCESS         0xcfb85110
#define TARGET_SNDCTL_PMGR_IFACE          0xcfb85001
#define TARGET_SNDCTL_SEQ_CTRLRATE        0xc0045103
#define TARGET_SNDCTL_SEQ_GETINCOUNT      0x80045105
#define TARGET_SNDCTL_SEQ_GETOUTCOUNT     0x80045104
#define TARGET_SNDCTL_SEQ_NRMIDIS         0x8004510b
#define TARGET_SNDCTL_SEQ_NRSYNTHS        0x8004510a
#define TARGET_SNDCTL_SEQ_OUTOFBAND       0x40085112
#define TARGET_SNDCTL_SEQ_PANIC           0x00005111
#define TARGET_SNDCTL_SEQ_PERCMODE        0x40045106
#define TARGET_SNDCTL_SEQ_RESET           0x00005100
#define TARGET_SNDCTL_SEQ_RESETSAMPLES    0x40045109
#define TARGET_SNDCTL_SEQ_SYNC            0x00005101
#define TARGET_SNDCTL_SEQ_TESTMIDI        0x40045108
#define TARGET_SNDCTL_SEQ_THRESHOLD       0x4004510d
#define TARGET_SNDCTL_SEQ_TRESHOLD        0x4004510d
#define TARGET_SNDCTL_SYNTH_INFO          0xc08c5102
#define TARGET_SNDCTL_SYNTH_MEMAVL        0xc004510e
#define TARGET_SNDCTL_TMR_CONTINUE        0x00005404
#define TARGET_SNDCTL_TMR_METRONOME       0x40045407
#define TARGET_SNDCTL_TMR_SELECT          0x40045408
#define TARGET_SNDCTL_TMR_SOURCE          0xc0045406
#define TARGET_SNDCTL_TMR_START           0x00005402
#define TARGET_SNDCTL_TMR_STOP            0x00005403
#define TARGET_SNDCTL_TMR_TEMPO           0xc0045405
#define TARGET_SNDCTL_TMR_TIMEBASE        0xc0045401
#define TARGET_SOUND_PCM_READ_RATE        0x80045002
#define TARGET_SOUND_PCM_READ_CHANNELS    0x80045006
#define TARGET_SOUND_PCM_READ_BITS        0x80045005
#define TARGET_SOUND_PCM_READ_FILTER      0x80045007
#define TARGET_SOUND_MIXER_INFO           TARGET_IOR ('M', 101, mixer_info)
#define TARGET_SOUND_MIXER_ACCESS         0xc0804d66
#define TARGET_SOUND_MIXER_PRIVATE1       TARGET_IOWR('M', 111, abi_int)
#define TARGET_SOUND_MIXER_PRIVATE2       TARGET_IOWR('M', 112, abi_int)
#define TARGET_SOUND_MIXER_PRIVATE3       TARGET_IOWR('M', 113, abi_int)
#define TARGET_SOUND_MIXER_PRIVATE4       TARGET_IOWR('M', 114, abi_int)
#define TARGET_SOUND_MIXER_PRIVATE5       TARGET_IOWR('M', 115, abi_int)

#define TARGET_MIXER_READ(dev)  TARGET_IOR('M', dev, abi_int)

#define TARGET_SOUND_MIXER_READ_VOLUME          TARGET_MIXER_READ(SOUND_MIXER_VOLUME)
#define TARGET_SOUND_MIXER_READ_BASS            TARGET_MIXER_READ(SOUND_MIXER_BASS)
#define TARGET_SOUND_MIXER_READ_TREBLE          TARGET_MIXER_READ(SOUND_MIXER_TREBLE)
#define TARGET_SOUND_MIXER_READ_SYNTH           TARGET_MIXER_READ(SOUND_MIXER_SYNTH)
#define TARGET_SOUND_MIXER_READ_PCM             TARGET_MIXER_READ(SOUND_MIXER_PCM)
#define TARGET_SOUND_MIXER_READ_SPEAKER         TARGET_MIXER_READ(SOUND_MIXER_SPEAKER)
#define TARGET_SOUND_MIXER_READ_LINE            TARGET_MIXER_READ(SOUND_MIXER_LINE)
#define TARGET_SOUND_MIXER_READ_MIC             TARGET_MIXER_READ(SOUND_MIXER_MIC)
#define TARGET_SOUND_MIXER_READ_CD              TARGET_MIXER_READ(SOUND_MIXER_CD)
#define TARGET_SOUND_MIXER_READ_IMIX            TARGET_MIXER_READ(SOUND_MIXER_IMIX)
#define TARGET_SOUND_MIXER_READ_ALTPCM          TARGET_MIXER_READ(SOUND_MIXER_ALTPCM)
#define TARGET_SOUND_MIXER_READ_RECLEV          TARGET_MIXER_READ(SOUND_MIXER_RECLEV)
#define TARGET_SOUND_MIXER_READ_IGAIN           TARGET_MIXER_READ(SOUND_MIXER_IGAIN)
#define TARGET_SOUND_MIXER_READ_OGAIN           TARGET_MIXER_READ(SOUND_MIXER_OGAIN)
#define TARGET_SOUND_MIXER_READ_LINE1           TARGET_MIXER_READ(SOUND_MIXER_LINE1)
#define TARGET_SOUND_MIXER_READ_LINE2           TARGET_MIXER_READ(SOUND_MIXER_LINE2)
#define TARGET_SOUND_MIXER_READ_LINE3           TARGET_MIXER_READ(SOUND_MIXER_LINE3)

#define TARGET_SOUND_MIXER_READ_MUTE            TARGET_MIXER_READ(SOUND_MIXER_MUTE)
#define TARGET_SOUND_MIXER_READ_ENHANCE         TARGET_MIXER_READ(SOUND_MIXER_ENHANCE)
#define TARGET_SOUND_MIXER_READ_LOUD            TARGET_MIXER_READ(SOUND_MIXER_LOUD)

#define TARGET_SOUND_MIXER_READ_RECSRC          TARGET_MIXER_READ(SOUND_MIXER_RECSRC)
#define TARGET_SOUND_MIXER_READ_DEVMASK         TARGET_MIXER_READ(SOUND_MIXER_DEVMASK)
#define TARGET_SOUND_MIXER_READ_RECMASK         TARGET_MIXER_READ(SOUND_MIXER_RECMASK)
#define TARGET_SOUND_MIXER_READ_STEREODEVS      TARGET_MIXER_READ(SOUND_MIXER_STEREODEVS)
#define TARGET_SOUND_MIXER_READ_CAPS            TARGET_MIXER_READ(SOUND_MIXER_CAPS)

#define TARGET_MIXER_WRITE(dev)         TARGET_IOWR('M', dev, abi_int)

#define TARGET_SOUND_MIXER_WRITE_VOLUME TARGET_MIXER_WRITE(SOUND_MIXER_VOLUME)
#define TARGET_SOUND_MIXER_WRITE_BASS           TARGET_MIXER_WRITE(SOUND_MIXER_BASS)
#define TARGET_SOUND_MIXER_WRITE_TREBLE TARGET_MIXER_WRITE(SOUND_MIXER_TREBLE)
#define TARGET_SOUND_MIXER_WRITE_SYNTH          TARGET_MIXER_WRITE(SOUND_MIXER_SYNTH)
#define TARGET_SOUND_MIXER_WRITE_PCM            TARGET_MIXER_WRITE(SOUND_MIXER_PCM)
#define TARGET_SOUND_MIXER_WRITE_SPEAKER        TARGET_MIXER_WRITE(SOUND_MIXER_SPEAKER)
#define TARGET_SOUND_MIXER_WRITE_LINE           TARGET_MIXER_WRITE(SOUND_MIXER_LINE)
#define TARGET_SOUND_MIXER_WRITE_MIC            TARGET_MIXER_WRITE(SOUND_MIXER_MIC)
#define TARGET_SOUND_MIXER_WRITE_CD             TARGET_MIXER_WRITE(SOUND_MIXER_CD)
#define TARGET_SOUND_MIXER_WRITE_IMIX           TARGET_MIXER_WRITE(SOUND_MIXER_IMIX)
#define TARGET_SOUND_MIXER_WRITE_ALTPCM TARGET_MIXER_WRITE(SOUND_MIXER_ALTPCM)
#define TARGET_SOUND_MIXER_WRITE_RECLEV TARGET_MIXER_WRITE(SOUND_MIXER_RECLEV)
#define TARGET_SOUND_MIXER_WRITE_IGAIN          TARGET_MIXER_WRITE(SOUND_MIXER_IGAIN)
#define TARGET_SOUND_MIXER_WRITE_OGAIN          TARGET_MIXER_WRITE(SOUND_MIXER_OGAIN)
#define TARGET_SOUND_MIXER_WRITE_LINE1          TARGET_MIXER_WRITE(SOUND_MIXER_LINE1)
#define TARGET_SOUND_MIXER_WRITE_LINE2          TARGET_MIXER_WRITE(SOUND_MIXER_LINE2)
#define TARGET_SOUND_MIXER_WRITE_LINE3          TARGET_MIXER_WRITE(SOUND_MIXER_LINE3)

#define TARGET_SOUND_MIXER_WRITE_MUTE           TARGET_MIXER_WRITE(SOUND_MIXER_MUTE)
#define TARGET_SOUND_MIXER_WRITE_ENHANCE        TARGET_MIXER_WRITE(SOUND_MIXER_ENHANCE)
#define TARGET_SOUND_MIXER_WRITE_LOUD           TARGET_MIXER_WRITE(SOUND_MIXER_LOUD)

#define TARGET_SOUND_MIXER_WRITE_RECSRC TARGET_MIXER_WRITE(SOUND_MIXER_RECSRC)

struct target_snd_timer_id {
    abi_int dev_class;
    abi_int dev_sclass;
    abi_int card;
    abi_int device;
    abi_int subdevice;
};

struct target_snd_timer_ginfo {
    struct target_snd_timer_id tid;
    abi_uint flags;
    abi_int card;
    unsigned char id[64];
    unsigned char name[80];
    abi_ulong reserved0;
    abi_ulong resolution;
    abi_ulong resolution_min;
    abi_ulong resolution_max;
    abi_uint clients;
    unsigned char reserved[32];
};

struct target_snd_timer_gparams {
    struct target_snd_timer_id tid;
    abi_ulong period_num;
    abi_ulong period_den;
    unsigned char reserved[32];
};

struct target_snd_timer_gstatus {
    struct target_snd_timer_id tid;
    abi_ulong resolution;
    abi_ulong resolution_num;
    abi_ulong resolution_den;
    unsigned char reserved[32];
};

struct target_snd_timer_select {
    struct target_snd_timer_id id;
    unsigned char reserved[32];
};

struct target_snd_timer_info {
    abi_uint flags;
    abi_int card;
    unsigned char id[64];
    unsigned char name[80];
    abi_ulong reserved0;
    abi_ulong resolution;
    unsigned char reserved[64];
};

struct target_snd_timer_status {
    struct target_timespec tstamp;
    abi_uint resolution;
    abi_uint lost;
    abi_uint overrun;
    abi_uint queue;
    unsigned char reserved[64];
};

#define TARGET_SNDRV_TIMER_IOCTL_PVERSION     TARGET_IOR('T', 0x00, abi_int)
#define TARGET_SNDRV_TIMER_IOCTL_NEXT_DEVICE  TARGET_IOWR('T', 0x01,    \
                                                          struct snd_timer_id)
#define TARGET_SNDRV_TIMER_IOCTL_GINFO        TARGET_IOWR('T', 0x03,    \
                                                          struct target_snd_timer_ginfo)
#define TARGET_SNDRV_TIMER_IOCTL_GPARAMS      TARGET_IOW('T', 0x04,     \
                                                         struct target_snd_timer_gparams)
#define TARGET_SNDRV_TIMER_IOCTL_GSTATUS      TARGET_IOWR('T', 0x05,    \
                                                          struct target_snd_timer_gstatus)
#define TARGET_SNDRV_TIMER_IOCTL_SELECT       TARGET_IOW('T', 0x10,     \
                                                         struct target_snd_timer_select)
#define TARGET_SNDRV_TIMER_IOCTL_INFO         TARGET_IOR('T', 0x11,     \
                                                         struct target_snd_timer_info)
#define TARGET_SNDRV_TIMER_IOCTL_PARAMS       TARGET_IOW('T', 0x12,     \
                                                         struct snd_timer_params)
#define TARGET_SNDRV_TIMER_IOCTL_STATUS       TARGET_IOR('T', 0x14,     \
                                                         struct target_snd_timer_status)
#define TARGET_SNDRV_TIMER_IOCTL_START        TARGET_IO('T', 0xa0)
#define TARGET_SNDRV_TIMER_IOCTL_STOP         TARGET_IO('T', 0xa1)
#define TARGET_SNDRV_TIMER_IOCTL_CONTINUE     TARGET_IO('T', 0xa2)
#define TARGET_SNDRV_TIMER_IOCTL_PAUSE        TARGET_IO('T', 0xa3)

#define TARGET_VFAT_IOCTL_READDIR_BOTH    TARGET_IORU('r', 1)
#define TARGET_VFAT_IOCTL_READDIR_SHORT   TARGET_IORU('r', 2)

struct target_mtop {
    abi_short mt_op;
    abi_int mt_count;
};

#if defined(TARGET_SPARC) || defined(TARGET_MIPS)
typedef abi_long target_kernel_daddr_t;
#else
typedef abi_int target_kernel_daddr_t;
#endif

struct target_mtget {
    abi_long mt_type;
    abi_long mt_resid;
    abi_long mt_dsreg;
    abi_long mt_gstat;
    abi_long mt_erreg;
    target_kernel_daddr_t mt_fileno;
    target_kernel_daddr_t mt_blkno;
};

struct target_mtpos {
    abi_long mt_blkno;
};

#define TARGET_MTIOCTOP        TARGET_IOW('m', 1, struct target_mtop)
#define TARGET_MTIOCGET        TARGET_IOR('m', 2, struct target_mtget)
#define TARGET_MTIOCPOS        TARGET_IOR('m', 3, struct target_mtpos)

#define TARGET_KCOV_ENABLE     TARGET_IO('c', 100)
#define TARGET_KCOV_DISABLE    TARGET_IO('c', 101)
#define TARGET_KCOV_INIT_TRACE TARGET_IOR('c', 1, abi_ulong)

struct target_sysinfo {
    abi_long uptime;
    abi_ulong loads[3];
    abi_ulong totalram;
    abi_ulong freeram;
    abi_ulong sharedram;
    abi_ulong bufferram;
    abi_ulong totalswap;
    abi_ulong freeswap;
    abi_ushort procs;
    abi_ushort pad;
    abi_ulong totalhigh;
    abi_ulong freehigh;
    abi_uint mem_unit;
    char _f[20-2*sizeof(abi_long)-sizeof(int)];
};

struct linux_dirent {
    long            d_ino;
    unsigned long   d_off;
    unsigned short  d_reclen;
    char            d_name[];
};

struct linux_dirent64 {
    uint64_t        d_ino;
    int64_t         d_off;
    unsigned short  d_reclen;
    unsigned char   d_type;
    char            d_name[];
};

struct target_mq_attr {
    abi_long mq_flags;
    abi_long mq_maxmsg;
    abi_long mq_msgsize;
    abi_long mq_curmsgs;
};

struct target_drm_version {
    abi_int version_major;
    abi_int version_minor;
    abi_int version_patchlevel;
    abi_ulong name_len;
    abi_ulong name;
    abi_ulong date_len;
    abi_ulong date;
    abi_ulong desc_len;
    abi_ulong desc;
};

struct target_drm_i915_getparam {
    abi_int param;
    abi_ulong value;
};

#include "socket.h"

#include "target_errno_defs.h"

#define FUTEX_WAIT              0
#define FUTEX_WAKE              1
#define FUTEX_FD                2
#define FUTEX_REQUEUE           3
#define FUTEX_CMP_REQUEUE       4
#define FUTEX_WAKE_OP           5
#define FUTEX_LOCK_PI           6
#define FUTEX_UNLOCK_PI         7
#define FUTEX_TRYLOCK_PI        8
#define FUTEX_WAIT_BITSET       9
#define FUTEX_WAKE_BITSET       10
#define FUTEX_WAIT_REQUEUE_PI   11
#define FUTEX_CMP_REQUEUE_PI    12
#define FUTEX_LOCK_PI2          13

#define FUTEX_PRIVATE_FLAG      128
#define FUTEX_CLOCK_REALTIME    256
#define FUTEX_CMD_MASK          ~(FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME)

#ifdef CONFIG_EPOLL
#if defined(TARGET_X86_64)
#define TARGET_EPOLL_PACKED QEMU_PACKED
#else
#define TARGET_EPOLL_PACKED
#endif

typedef union target_epoll_data {
    abi_ulong ptr;
    abi_int fd;
    abi_uint u32;
    abi_ullong u64;
} target_epoll_data_t;

struct target_epoll_event {
    abi_uint events;
    target_epoll_data_t data;
} TARGET_EPOLL_PACKED;

#define TARGET_EP_MAX_EVENTS (INT_MAX / sizeof(struct target_epoll_event))

#endif

struct target_ucred {
    abi_uint pid;
    abi_uint uid;
    abi_uint gid;
};

struct target_in_pktinfo {
    abi_int               ipi_ifindex;
    struct target_in_addr ipi_spec_dst;
    struct target_in_addr ipi_addr;
};

typedef abi_int target_timer_t;

#define TARGET_SIGEV_MAX_SIZE 64

#ifdef TARGET_MIPS
#define TARGET_SIGEV_PREAMBLE_SIZE (sizeof(abi_int) * 2 + sizeof(abi_long))
#else
#define TARGET_SIGEV_PREAMBLE_SIZE (sizeof(abi_int) * 2         \
                                    + sizeof(target_sigval_t))
#endif

#define TARGET_SIGEV_PAD_SIZE ((TARGET_SIGEV_MAX_SIZE           \
                                - TARGET_SIGEV_PREAMBLE_SIZE)   \
                               / sizeof(abi_int))

struct target_sigevent {
    target_sigval_t sigev_value;
    abi_int sigev_signo;
    abi_int sigev_notify;
    union {
        abi_int _pad[TARGET_SIGEV_PAD_SIZE];
        abi_int _tid;

        struct {
            abi_ulong _function;
            abi_ulong _attribute;
        } _sigev_thread;
    } _sigev_un;
};

struct target_user_cap_header {
    abi_uint version;
    abi_int  pid;
};

struct target_user_cap_data {
    abi_uint effective;
    abi_uint permitted;
    abi_uint inheritable;
};

#define TARGET_SYSLOG_ACTION_CLOSE          0
#define TARGET_SYSLOG_ACTION_OPEN           1
#define TARGET_SYSLOG_ACTION_READ           2
#define TARGET_SYSLOG_ACTION_READ_ALL       3
#define TARGET_SYSLOG_ACTION_READ_CLEAR     4
#define TARGET_SYSLOG_ACTION_CLEAR          5
#define TARGET_SYSLOG_ACTION_CONSOLE_OFF    6
#define TARGET_SYSLOG_ACTION_CONSOLE_ON     7
#define TARGET_SYSLOG_ACTION_CONSOLE_LEVEL  8
#define TARGET_SYSLOG_ACTION_SIZE_UNREAD    9
#define TARGET_SYSLOG_ACTION_SIZE_BUFFER   10

struct target_statx_timestamp {
    abi_llong tv_sec;
    abi_uint tv_nsec;
    abi_int __reserved;
};

struct target_statx {
    abi_uint stx_mask;
    abi_uint stx_blksize;
    abi_ullong stx_attributes;
    abi_uint stx_nlink;
    abi_uint stx_uid;
    abi_uint stx_gid;
    uint16_t stx_mode;
    uint16_t __spare0[1];
    abi_ullong stx_ino;
    abi_ullong stx_size;
    abi_ullong stx_blocks;
    abi_ullong stx_attributes_mask;
    struct target_statx_timestamp  stx_atime;
    struct target_statx_timestamp  stx_btime;
    struct target_statx_timestamp  stx_ctime;
    struct target_statx_timestamp  stx_mtime;
    abi_uint stx_rdev_major;
    abi_uint stx_rdev_minor;
    abi_uint stx_dev_major;
    abi_uint stx_dev_minor;
    abi_ullong __spare2[14];
};

struct target_sched_attr {
    abi_uint size;
    abi_uint sched_policy;
    abi_ullong sched_flags;
    abi_int sched_nice;
    abi_uint sched_priority;
    abi_ullong sched_runtime;
    abi_ullong sched_deadline;
    abi_ullong sched_period;
    abi_uint sched_util_min;
    abi_uint sched_util_max;
};

struct target_sched_param {
    abi_int sched_priority;
};

struct open_how_ver0 {
    uint64_t flags;
    uint64_t mode;
    uint64_t resolve;
};
struct target_open_how_ver0 {
    abi_ullong flags;
    abi_ullong mode;
    abi_ullong resolve;
};
#ifndef RESOLVE_NO_MAGICLINKS
#define RESOLVE_NO_MAGICLINKS   0x02
#endif
#ifndef RESOLVE_NO_SYMLINKS
#define RESOLVE_NO_SYMLINKS     0x04
#endif

#if (defined(TARGET_I386) && defined(TARGET_ABI32)) || \
    (defined(TARGET_ARM) && defined(TARGET_ABI32)) || \
    defined(TARGET_M68K) || defined(TARGET_MICROBLAZE) || \
    defined(TARGET_S390X)
#define TARGET_ARCH_WANT_SYS_OLD_MMAP
#endif

#endif
