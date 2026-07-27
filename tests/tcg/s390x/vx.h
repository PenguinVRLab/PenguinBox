#ifndef QEMU_TESTS_S390X_VX_H
#define QEMU_TESTS_S390X_VX_H

#include <stdint.h>

typedef union S390Vector {
    uint64_t d[2];
    uint32_t w[4];
    uint16_t h[8];
    uint8_t  b[16];
    float    f[4];
    double   fd[2];
    __uint128_t v;
} S390Vector;

#define ES8  0
#define ES16 1
#define ES32 2
#define ES64 3

#endif
