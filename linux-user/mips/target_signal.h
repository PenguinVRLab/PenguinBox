#ifndef MIPS_TARGET_SIGNAL_H
#define MIPS_TARGET_SIGNAL_H

#define TARGET_SIGHUP            1
#define TARGET_SIGINT            2
#define TARGET_SIGQUIT           3
#define TARGET_SIGILL            4
#define TARGET_SIGTRAP           5
#define TARGET_SIGIOT            6      /* IOT trap (4.2 BSD).  */
#define TARGET_SIGABRT           TARGET_SIGIOT
#define TARGET_SIGEMT            7
#define TARGET_SIGSTKFLT         7
#define TARGET_SIGFPE            8
#define TARGET_SIGKILL           9
#define TARGET_SIGBUS           10      /* BUS error (4.2 BSD).  */
#define TARGET_SIGSEGV          11
#define TARGET_SIGSYS           12
#define TARGET_SIGPIPE          13
#define TARGET_SIGALRM          14
#define TARGET_SIGTERM          15
#define TARGET_SIGUSR1          16
#define TARGET_SIGUSR2          17
#define TARGET_SIGCHLD          18
#define TARGET_SIGCLD           TARGET_SIGCHLD
#define TARGET_SIGPWR           19
#define TARGET_SIGWINCH 20      /* Window size change (4.3 BSD, Sun).  */
#define TARGET_SIGURG           21      /* Urgent condition on socket (4.2 BSD).  */
#define TARGET_SIGIO            22      /* I/O now possible (4.2 BSD).  */
#define TARGET_SIGPOLL          TARGET_SIGIO
#define TARGET_SIGSTOP          23
#define TARGET_SIGTSTP          24
#define TARGET_SIGCONT          25
#define TARGET_SIGTTIN          26
#define TARGET_SIGTTOU          27
#define TARGET_SIGVTALRM        28      /* Virtual alarm clock (4.2 BSD).  */
#define TARGET_SIGPROF          29      /* Profiling alarm clock (4.2 BSD).  */
#define TARGET_SIGXCPU          30      /* CPU limit exceeded (4.2 BSD).  */
#define TARGET_SIGXFSZ          31      /* File size limit exceeded (4.2 BSD).  */
#define TARGET_SIGRTMIN         32

#define TARGET_SIG_BLOCK        1
#define TARGET_SIG_UNBLOCK      2
#define TARGET_SIG_SETMASK      3

typedef struct target_sigaltstack {
    abi_ulong ss_sp;
    abi_ulong ss_size;
    abi_int ss_flags;
} target_stack_t;


#define TARGET_SS_ONSTACK     1
#define TARGET_SS_DISABLE     2

#define TARGET_SA_NOCLDSTOP     0x00000001
#define TARGET_SA_NOCLDWAIT     0x00010000
#define TARGET_SA_SIGINFO       0x00000008
#define TARGET_SA_ONSTACK       0x08000000
#define TARGET_SA_NODEFER       0x40000000
#define TARGET_SA_RESTART       0x10000000
#define TARGET_SA_RESETHAND     0x80000000

#define TARGET_MINSIGSTKSZ    2048

#if defined(TARGET_ABI_MIPSO32)
#define TARGET_ARCH_HAS_SETUP_FRAME
#endif
#define TARGET_ARCH_HAS_SIGTRAMP_PAGE 1

#define TARGET_SS_AUTODISARM (1U << 31)
#define TARGET_SS_FLAG_BITS  TARGET_SS_AUTODISARM

#endif
