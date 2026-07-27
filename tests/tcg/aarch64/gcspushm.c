/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gcs.h"


#define GCSPUSHM  "sys #3, c7, c7, #0, %[push]"
#define GCSPOPM   "sysl %[pop], #3, c7, c7, #1"

static void test_sigsegv(int sig, siginfo_t *info, void *vuc)
{
    ucontext_t *uc = vuc;
    uint64_t inst_sigsegv;

    __asm__("adr %0, inst_sigsegv" : "=r"(inst_sigsegv));
    assert(uc->uc_mcontext.pc == inst_sigsegv);
    assert(info->si_code == SEGV_CPERR);
    uc->uc_mcontext.pc += 4;
}

static void test_sigill(int sig, siginfo_t *info, void *vuc)
{
    ucontext_t *uc = vuc;
    uint64_t inst_sigill;

    __asm__("adr %0, inst_sigill" : "=r"(inst_sigill));
    assert(uc->uc_mcontext.pc == inst_sigill);
    assert(info->si_code == ILL_ILLOPC);
    uc->uc_mcontext.pc += 4;
}

int main()
{
    struct sigaction sa = { .sa_flags = SA_SIGINFO };
    uint64_t old, new;

    sa.sa_sigaction = test_sigsegv;
    if (sigaction(SIGSEGV, &sa, NULL) < 0) {
        perror("sigaction");
        exit(1);
    }

    sa.sa_sigaction = test_sigill;
    if (sigaction(SIGILL, &sa, NULL) < 0) {
        perror("sigaction");
        exit(1);
    }

    asm volatile("inst_sigill:\t" GCSPUSHM
                 : : [push] "r" (1));

    enable_gcs(PR_SHADOW_STACK_PUSH);

    old = 0xdeadbeeffeedcaec;
    asm volatile(GCSPUSHM "\n\t" GCSPOPM
                 : [pop] "=r" (new)
                 : [push] "r" (old)
                 : "memory");
    assert(old == new);

    asm volatile(GCSPUSHM "\n"
                 "inst_sigsegv:\t" GCSPOPM
                 : [pop] "=r" (new)
                 : [push] "r" (1)
                 : "memory");

    exit(0);
}
