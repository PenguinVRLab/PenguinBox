#include <stdio.h>

int main(void)
{
    float a, b;
    float res2;

    a = 1.5;
    b = 2.5;
    res2 = 4.0;
    __asm
    ("lf.add.s  %0, %0, %1\n\t"
     : "+r"(a)
     : "r"(b)
    );
    if (a != res2) {
        printf("lf.add.s error, %f\n", a);
        return -1;
    }

    return 0;
}
