#include <stdio.h>

int main(void)
{
    float a, b, c;
    float result;

    b = 10.5;
    c = 1.5;
    result = 9.0;
    __asm
    ("lf.sub.s  %0, %1, %2\n\t"
     : "=r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sub.s error\n");
        return -1;
    }

    return 0;
}
