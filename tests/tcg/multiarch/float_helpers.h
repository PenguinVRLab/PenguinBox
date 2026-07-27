/*
 * Common Float Helpers
 *
 * Copyright (c) 2019, 2024 Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <inttypes.h>

#ifndef FE_OVERFLOW
#define FE_OVERFLOW 0
#endif
#ifndef FE_UNDERFLOW
#define FE_UNDERFLOW 0
#endif
#ifndef FE_DIVBYZERO
#define FE_DIVBYZERO 0
#endif
#ifndef FE_INEXACT
#define FE_INEXACT 0
#endif
#ifndef FE_INVALID
#define FE_INVALID 0
#endif

int get_num_f16(void);
int get_num_f32(void);
int get_num_f64(void);

uint16_t get_f16(int i);
float    get_f32(int i);
double   get_f64(int i);

char * fmt_f16(uint16_t);
char * fmt_f32(float);
char * fmt_f64(double);
char * fmt_flags(void);
