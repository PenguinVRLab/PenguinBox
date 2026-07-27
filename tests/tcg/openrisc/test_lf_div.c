#include <stdio.h>

int main(void)
{
    float a, b, c;
    float result;

    b = 1.5;
    c = 0.5;
    result = 3.0;
    __asm
    ("lf.div.s    %0, %1, %2\n\t"
     : "=r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.div.s error\n");
        return -1;
    }

    return 0;
}
