/*
 * AVR loader helpers
 *
 * Copyright (c) 2019-2020 Philippe Mathieu-Daudé
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_AVR_BOOT_H
#define HW_AVR_BOOT_H

#include "hw/boards.h"
#include "cpu.h"

bool avr_load_firmware(AVRCPU *cpu, MachineState *ms,
                       MemoryRegion *mr, const char *firmware);

#endif
