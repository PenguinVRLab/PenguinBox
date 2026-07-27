#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <signal.h>
#include <sys/prctl.h>

#define MTFSF(FLM, FRB) asm volatile ("mtfsf %0, %1" :: "i" (FLM), "f" (FRB))
#define MFFS(FRT) asm("mffs %0" : "=f" (FRT))

#define FPSCR_VE     7
#define FPSCR_VXSOFT 10
#define FPSCR_FI     17

#define FP_VE           (1ull << FPSCR_VE)
#define FP_VXSOFT       (1ull << FPSCR_VXSOFT)
#define FP_FI           (1ull << FPSCR_FI)

void sigfpe_handler(int sig, siginfo_t *si, void *ucontext)
{
    if (si->si_code == FPE_FLTINV) {
        exit(0);
    }
    exit(1);
}

int main(void)
{
    uint64_t fpscr;

    struct sigaction sa = {
        .sa_sigaction = sigfpe_handler,
        .sa_flags = SA_SIGINFO
    };

    prctl(PR_SET_FPEXC, PR_FP_EXC_PRECISE);

    MTFSF(0b11111111, FP_FI);
    MFFS(fpscr);
    assert((fpscr & FP_FI) != 0);

    sigaction(SIGFPE, &sa, NULL);

    MTFSF(0b11111111, FP_VE | FP_VXSOFT);

    return 1;
}
