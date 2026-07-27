/*
 * Accelerator blocking API, to prevent new ioctls from starting and wait the
 * running ones finish.
 * This mechanism differs from pause/resume_all_vcpus() in that it does not
 * release the BQL.
 *
 *  Copyright (c) 2022 Red Hat Inc.
 *
 * Author: Emanuele Giuseppe Esposito       <eesposit@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef ACCEL_BLOCKER_H
#define ACCEL_BLOCKER_H

#include "system/cpus.h"

void accel_blocker_init(void);

void accel_ioctl_begin(void);
void accel_ioctl_end(void);
void accel_cpu_ioctl_begin(CPUState *cpu);
void accel_cpu_ioctl_end(CPUState *cpu);

void accel_ioctl_inhibit_begin(void);

void accel_ioctl_inhibit_end(void);

#endif
