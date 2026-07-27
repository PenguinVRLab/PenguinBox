#include <assert.h>
#include "pauth.h"

static int x;

int main()
{
    int *p0 = &x, *p1, *p2, *p3;
    unsigned long salt = 0;
    int pac_feature = get_pac_feature();

    if (pac_feature == 0 || pac_feature >= 4) {
        return 0;
    }

    do {
        salt++;
        asm("pacda %0, %1" : "=r"(p1) : "r"(salt), "0"(p0));
    } while (p0 == p1);

    asm("pacda %0, %1" : "=r"(p2) : "r"(salt), "0"(p1));

    asm("autda %0, %1" : "=r"(p3) : "r"(salt), "0"(p2));

    assert(p3 != p0);
    return 0;
}
