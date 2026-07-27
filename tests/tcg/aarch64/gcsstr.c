/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gcs.h"

void __attribute__((noinline))
test_gcsstr(void)
{
    register uint64_t *ptr __asm__("x0") = gcspr();
    __asm__("inst_gcsstr: .inst 0xd91f1c01" : : "r"(--ptr));
}

static void test_sigsegv(int sig, siginfo_t *info, void *vuc)
{
    ucontext_t *uc = vuc;
    uint64_t inst_gcsstr;

    __asm__("adr %0, inst_gcsstr" : "=r"(inst_gcsstr));
    assert(uc->uc_mcontext.pc == inst_gcsstr);
    assert(info->si_code == SEGV_CPERR);
    exit(0);
}

int main()
{
    struct sigaction sa = {
        .sa_sigaction = test_sigsegv,
        .sa_flags = SA_SIGINFO,
    };

    enable_gcs(PR_SHADOW_STACK_WRITE);
    test_gcsstr();

    enable_gcs(0);
    if (sigaction(SIGSEGV, &sa, NULL) < 0) {
        perror("sigaction");
        exit(1);
    }
    test_gcsstr();
    abort();
}
