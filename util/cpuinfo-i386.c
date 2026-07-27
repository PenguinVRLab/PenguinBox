/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Host specific cpu identification for x86.
 */

#include "qemu/osdep.h"
#include "host/cpuinfo.h"
#ifdef CONFIG_CPUID_H
# include "qemu/cpuid.h"
#endif

unsigned cpuinfo;

unsigned __attribute__((constructor)) cpuinfo_init(void)
{
    unsigned info = cpuinfo;

    if (info) {
        return info;
    }

#ifdef CONFIG_CPUID_H
    unsigned max, a, b, c, d, b7 = 0, c7 = 0;

    max = __get_cpuid_max(0, 0);

    if (max >= 7) {
        __cpuid_count(7, 0, a, b7, c7, d);
        info |= (b7 & bit_BMI ? CPUINFO_BMI1 : 0);
        info |= (b7 & bit_BMI2 ? CPUINFO_BMI2 : 0);
    }

    if (max >= 1) {
        __cpuid(1, a, b, c, d);

        info |= (d & bit_SSE2 ? CPUINFO_SSE2 : 0);
        info |= (c & bit_OSXSAVE ? CPUINFO_OSXSAVE : 0);
        info |= (c & bit_MOVBE ? CPUINFO_MOVBE : 0);
        info |= (c & bit_POPCNT ? CPUINFO_POPCNT : 0);
        info |= (c & bit_PCLMUL ? CPUINFO_PCLMUL : 0);

        info |= ((c & bit_AES) && (c & bit_SSSE3) ? CPUINFO_AES : 0);

        if ((c & bit_AVX) && (c & bit_OSXSAVE)) {
            unsigned bv = xgetbv_low(0);

            if ((bv & 6) == 6) {
                info |= CPUINFO_AVX1;
                info |= (b7 & bit_AVX2 ? CPUINFO_AVX2 : 0);
                info |= (c7 & bit_GFNI ? CPUINFO_GFNI : 0);

                if ((bv & 0xe0) == 0xe0) {
                    info |= (b7 & bit_AVX512F ? CPUINFO_AVX512F : 0);
                    info |= (b7 & bit_AVX512VL ? CPUINFO_AVX512VL : 0);
                    info |= (b7 & bit_AVX512BW ? CPUINFO_AVX512BW : 0);
                    info |= (b7 & bit_AVX512DQ ? CPUINFO_AVX512DQ : 0);
                    info |= (c7 & bit_AVX512VBMI2 ? CPUINFO_AVX512VBMI2 : 0);
                }

                __cpuid(0, a, b, c, d);
                if (c == signature_INTEL_ecx) {
                    info |= CPUINFO_ATOMIC_VMOVDQA;
                } else if (c == signature_AMD_ecx) {
                    info |= CPUINFO_ATOMIC_VMOVDQA | CPUINFO_ATOMIC_VMOVDQU;
                }
            }
        }
    }

    max = __get_cpuid_max(0x8000000, 0);
    if (max >= 1) {
        __cpuid(0x80000001, a, b, c, d);
        info |= (c & bit_LZCNT ? CPUINFO_LZCNT : 0);
    }
#endif

    info |= CPUINFO_ALWAYS;
    cpuinfo = info;
    return info;
}
