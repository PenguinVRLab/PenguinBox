/*
 * PCA9552 I2C LED blinker registers
 *
 * Copyright (c) 2017-2018, IBM Corporation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later. See the COPYING file in the top-level directory.
 */
#ifndef PCA9552_REGS_H
#define PCA9552_REGS_H

#define PCA9552_INPUT0   0
#define PCA9552_INPUT1   1
#define PCA9552_PSC0     2
#define PCA9552_PWM0     3
#define PCA9552_PSC1     4
#define PCA9552_PWM1     5
#define PCA9552_LS0      6
#define PCA9552_LS1      7
#define PCA9552_LS2      8
#define PCA9552_LS3      9

#define PCA9552_AUTOINC  (1 << 4)

#endif
