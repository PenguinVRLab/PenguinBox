/*
 * Copyright (c) 2026 Patrick Carey
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#if defined(__x86_64__) || defined(__i386__)

#if defined(__AVX512F__)
# define XCF_NEED_AVX512 1
#else
# define XCF_NEED_AVX512 0
#endif
#if defined(__AVX2__)
# define XCF_NEED_AVX2 1
#else
# define XCF_NEED_AVX2 0
#endif
#if defined(__AVX__)
# define XCF_NEED_AVX 1
#else
# define XCF_NEED_AVX 0
#endif
#if defined(__FMA__)
# define XCF_NEED_FMA 1
#else
# define XCF_NEED_FMA 0
#endif
#if defined(__F16C__)
# define XCF_NEED_F16C 1
#else
# define XCF_NEED_F16C 0
#endif
#if defined(__BMI__)
# define XCF_NEED_BMI1 1
#else
# define XCF_NEED_BMI1 0
#endif
#if defined(__BMI2__)
# define XCF_NEED_BMI2 1
#else
# define XCF_NEED_BMI2 0
#endif
#if defined(__LZCNT__)
# define XCF_NEED_LZCNT 1
#else
# define XCF_NEED_LZCNT 0
#endif
#if defined(__MOVBE__)
# define XCF_NEED_MOVBE 1
#else
# define XCF_NEED_MOVBE 0
#endif
#if defined(__POPCNT__)
# define XCF_NEED_POPCNT 1
#else
# define XCF_NEED_POPCNT 0
#endif
#if defined(__SSE4_2__)
# define XCF_NEED_SSE42 1
#else
# define XCF_NEED_SSE42 0
#endif

#define XCF_NEED_ANY (XCF_NEED_AVX512 | XCF_NEED_AVX2 | XCF_NEED_AVX | \
                      XCF_NEED_FMA | XCF_NEED_F16C | XCF_NEED_BMI1 | \
                      XCF_NEED_BMI2 | XCF_NEED_LZCNT | XCF_NEED_MOVBE | \
                      XCF_NEED_POPCNT | XCF_NEED_SSE42)

#if XCF_NEED_ANY

#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC target("no-avx,no-avx2,no-fma,no-f16c,no-bmi,no-bmi2,no-lzcnt," \
                    "no-movbe,no-popcnt,no-sse4.2,no-sse4.1,no-ssse3,no-sse3")
#elif defined(__clang__)
# error "the CPU-floor check is only proven baseline under GCC; check its code before building it with clang"
#endif

#include <cpuid.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
# include <windows.h>
#else
# include <unistd.h>
#endif

static unsigned long long xcf_xgetbv0(void)
{
    unsigned int lo, hi;
    __asm__ volatile(".byte 0x0f, 0x01, 0xd0" : "=a"(lo), "=d"(hi) : "c"(0));
    return ((unsigned long long)hi << 32) | lo;
}

static void xcf_add(char *buf, size_t len, const char *name)
{
    size_t n = strlen(buf);
    if (n + strlen(name) + 2 < len) {
        if (n) {
            buf[n++] = ' ';
        }
        strcpy(buf + n, name);
    }
}

void xemu_cpu_floor_check(void);

__attribute__((constructor(101), used, noinline))
void xemu_cpu_floor_check(void)
{
    unsigned int a, b, c, d;
    unsigned int c1 = 0, b7 = 0, c81 = 0;
    int os_ymm = 0, os_zmm = 0;
    char missing[256] = "";
    char msg[1024];
    int n;

    if (getenv("XEMU_CPU_FLOOR_SELFTEST_FAIL")) {
        goto report;
    }
    if (__get_cpuid(1, &a, &b, &c, &d)) {
        c1 = c;
    }
    if (__get_cpuid_count(7, 0, &a, &b, &c, &d)) {
        b7 = b;
    }
    if (__get_cpuid(0x80000001, &a, &b, &c, &d)) {
        c81 = c;
    }
    if (c1 & bit_OSXSAVE) {
        unsigned long long xcr0 = xcf_xgetbv0();
        os_ymm = (xcr0 & 0x6) == 0x6;
        os_zmm = (xcr0 & 0xe6) == 0xe6;
    }

    if (XCF_NEED_SSE42 && !(c1 & bit_SSE4_2)) xcf_add(missing, sizeof(missing), "SSE4.2");
    if (XCF_NEED_POPCNT && !(c1 & bit_POPCNT)) xcf_add(missing, sizeof(missing), "POPCNT");
    if (XCF_NEED_AVX && !((c1 & bit_AVX) && os_ymm)) xcf_add(missing, sizeof(missing), "AVX");
    if (XCF_NEED_AVX2 && !((b7 & bit_AVX2) && os_ymm)) xcf_add(missing, sizeof(missing), "AVX2");
    if (XCF_NEED_FMA && !((c1 & bit_FMA) && os_ymm)) xcf_add(missing, sizeof(missing), "FMA");
    if (XCF_NEED_F16C && !((c1 & bit_F16C) && os_ymm)) xcf_add(missing, sizeof(missing), "F16C");
    if (XCF_NEED_BMI1 && !(b7 & bit_BMI)) xcf_add(missing, sizeof(missing), "BMI1");
    if (XCF_NEED_BMI2 && !(b7 & bit_BMI2)) xcf_add(missing, sizeof(missing), "BMI2");
    if (XCF_NEED_LZCNT && !(c81 & bit_LZCNT)) xcf_add(missing, sizeof(missing), "LZCNT");
    if (XCF_NEED_MOVBE && !(c1 & bit_MOVBE)) xcf_add(missing, sizeof(missing), "MOVBE");
    if (XCF_NEED_AVX512 && !((b7 & bit_AVX512F) && os_zmm)) xcf_add(missing, sizeof(missing), "AVX-512");

    if (!missing[0]) {
        return;
    }

report:
    if (!missing[0]) {
        strcpy(missing, "(test hook: all required features treated as missing)");
    }
    n = snprintf(msg, sizeof(msg),
        "PenguinBox cannot run on this CPU: %s\n"
        "Missing: %s\n"
        "%s\n",
        XCF_NEED_AVX2 ? "this CPU lacks AVX2/FMA and the other x86-64-v3 "
                        "instructions this build was compiled for."
                      : "this CPU lacks instructions this build was "
                        "compiled for.",
        missing,
        XCF_NEED_AVX512 ? "This build needs a CPU with AVX-512 (x86-64-v4)."
        : XCF_NEED_AVX2 ? "This build needs an x86-64-v3 CPU: Intel Haswell "
                          "(2013) or AMD Excavator (2015) / Zen or newer."
        : "This build needs an x86-64-v2 CPU (SSE4.2, POPCNT).");
    if (n < 0) {
        n = 0;
    }
#ifdef _WIN32
    {
        HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
        DWORD w;
        if (err && err != INVALID_HANDLE_VALUE) {
            WriteFile(err, msg, (DWORD)strlen(msg), &w, NULL);
        }
        MessageBoxA(NULL, msg, "PenguinBox: unsupported CPU",
                    MB_OK | MB_ICONERROR);
        ExitProcess(3);
    }
#else
    if (write(STDERR_FILENO, msg, strlen(msg)) < 0) {
    }
    _exit(3);
#endif
}

#endif
#endif
