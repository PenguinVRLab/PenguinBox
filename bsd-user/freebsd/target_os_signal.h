#ifndef TARGET_OS_SIGNAL_H
#define TARGET_OS_SIGNAL_H

#include "target_os_siginfo.h"
#include "target_arch_signal.h"

abi_long setup_sigframe_arch(CPUArchState *env, abi_ulong frame_addr,
                             struct target_sigframe *frame, int flags);

#define TARGET_SIGHUP  1
#define TARGET_SIGINT  2
#define TARGET_SIGQUIT 3
#define TARGET_SIGILL  4
#define TARGET_SIGTRAP 5
#define TARGET_SIGABRT 6
#define TARGET_SIGIOT  SIGABRT
#define TARGET_SIGEMT  7
#define TARGET_SIGFPE  8
#define TARGET_SIGKILL 9
#define TARGET_SIGBUS  10
#define TARGET_SIGSEGV 11
#define TARGET_SIGSYS  12
#define TARGET_SIGPIPE 13
#define TARGET_SIGALRM 14
#define TARGET_SIGTERM 15
#define TARGET_SIGURG  16
#define TARGET_SIGSTOP 17
#define TARGET_SIGTSTP 18
#define TARGET_SIGCONT 19
#define TARGET_SIGCHLD 20
#define TARGET_SIGTTIN 21
#define TARGET_SIGTTOU 22
#define TARGET_SIGIO   23
#define TARGET_SIGXCPU 24
#define TARGET_SIGXFSZ 25
#define TARGET_SIGVTALRM 26
#define TARGET_SIGPROF 27
#define TARGET_SIGWINCH 28
#define TARGET_SIGINFO  29
#define TARGET_SIGUSR1 30
#define TARGET_SIGUSR2 31
#define TARGET_SIGTHR 32
#define TARGET_SIGLWP SIGTHR
#define TARGET_SIGLIBRT 33
#define TARGET_SIGRTMIN 65
#define TARGET_SIGRTMAX 126

#define TARGET_SIG_DFL      ((abi_long)0)
#define TARGET_SIG_IGN      ((abi_long)1)
#define TARGET_SIG_ERR      ((abi_long)-1)

#define TARGET_SA_ONSTACK   0x0001
#define TARGET_SA_RESTART   0x0002
#define TARGET_SA_RESETHAND 0x0004
#define TARGET_SA_NODEFER   0x0010
#define TARGET_SA_NOCLDWAIT 0x0020
#define TARGET_SA_USERTRAMP 0x0100
#define TARGET_SA_NOCLDSTOP 0x0008
#define TARGET_SA_SIGINFO   0x0040

#define TARGET_SIG_BLOCK        1
#define TARGET_SIG_UNBLOCK      2
#define TARGET_SIG_SETMASK      3

#define TARGET_BADSIG           SIG_ERR

#define TARGET_SS_ONSTACK 0x0001
#define TARGET_SS_DISABLE 0x0004

#endif
