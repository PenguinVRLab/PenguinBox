#include <stdint.h>
#include <minilib.h>

int main()
{

    static const uint64_t d[5] = {
        0xfb623599da6e8127ull,
        0x477d469dec0b8762ull,
        0x84be85ce9804e94bull,
        0xec2802d4e0a488e9ull,
        0xc003b93999b33765ull & 0xffffffff00000000ull
    };
    uint64_t r;

    asm("msr apgakeyhi_el1, %[w0]\n\t"
        "msr apgakeylo_el1, %[k0]\n\t"
        "pacga %[r], %[P], %[T]"
        : [r] "=r"(r)
        : [P] "r" (d[0]),
          [T] "r" (d[1]),
          [w0] "r" (d[2]),
          [k0] "r" (d[3]));

    if (r == d[4]) {
        ml_printf("OK\n");
        return 0;
    } else {
        ml_printf("FAIL: %lx != %lx\n", r, d[4]);
        return 1;
    }
}
