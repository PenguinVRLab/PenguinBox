#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "pauth.h"

#define TESTS 1000

int main()
{
    char base[TESTS];
    int i, count = 0;
    float perc;
    int pac_feature = get_pac_feature();

    if (pac_feature == 0 || pac_feature >= 4) {
        return 0;
    }

    for (i = 0; i < TESTS; i++) {
        uintptr_t in, x, y;

        in = i + (uintptr_t) base;

        asm("mov %0, %[in]\n\t"
            "pacia %0, sp\n\t"
            "eor %0, %0, #4\n\t"
            "mov %1, %0\n\t"
            "autia %1, sp\n\t"
            "xpaci %0\n\t"
            : "=r"(x), "=r"(y)
            : [in] "r" (in)
            : );

        if (x != y) {
            count++;
        }
    }

    perc = (float) count / (float) TESTS;
    printf("Checks Passed: %0.2f%%\n", perc * 100.0);
    assert(perc > 0.95);
    return 0;
}
