/*
 * Accelerator interface, specializes CPUClass
 * This header is used only by target-specific code.
 *
 * Copyright 2021 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef ACCEL_CPU_TARGET_H
#define ACCEL_CPU_TARGET_H

#include "qom/object.h"
#include "accel/accel-cpu.h"
#include "cpu.h"

#define TYPE_ACCEL_CPU "accel-" CPU_RESOLVING_TYPE
#define ACCEL_CPU_NAME(name) (name "-" TYPE_ACCEL_CPU)
DECLARE_CLASS_CHECKERS(AccelCPUClass, ACCEL_CPU, TYPE_ACCEL_CPU)

#endif
