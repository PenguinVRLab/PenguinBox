#include <stdint.h>
#include <signal.h>
#include <stdlib.h>
#include <assert.h>
#include "pauth.h"


static void sigill(int sig, siginfo_t *info, void *vuc)
{
    ucontext_t *uc = vuc;
    uint64_t test;

    asm volatile("adr %0, auth2_insn" : "=r"(test));
    assert(test == uc->uc_mcontext.pc);
    exit(0);
}

static int pac_feature;

void do_test(uint64_t value)
{
    uint64_t salt1, salt2;
    uint64_t encode, decode;

    for (salt1 = 1; ; salt1++) {
        asm volatile("pacda %0, %2" : "=r"(encode) : "0"(value), "r"(salt1));
        if (encode != value) {
            break;
        }
    }

    asm volatile("autda %0, %2" : "=r"(decode) : "0"(encode), "r"(salt1));
    assert(decode == value);

    for (salt2 = salt1 + 1; ; salt2++) {
        asm volatile("auth2_insn: autda %0, %2"
                     : "=r"(decode) : "0"(encode), "r"(salt2));
        if (decode != value) {
            break;
        }
    }

    assert(pac_feature < 4);

    assert(((decode ^ value) & 0xff80ffffffffffffull) == 0);

    if (pac_feature < 3) {
        if ((value >> 55) & 1) {
            assert(((decode >> 48) & 0xff) == 0b10111111);
        } else {
            assert(((decode >> 48) & 0xff) == 0b00100000);
        }
    }
}

int main()
{
    static const struct sigaction sa = {
        .sa_sigaction = sigill,
        .sa_flags = SA_SIGINFO
    };

    pac_feature = get_pac_feature();
    assert(pac_feature != 0);

    if (pac_feature >= 4) {
        sigaction(SIGILL, &sa, NULL);
    }

    do_test(0);
    do_test(0xda004acedeadbeefull);
    return 0;
}
