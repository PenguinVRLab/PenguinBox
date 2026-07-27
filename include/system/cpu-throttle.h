/*
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 */

#ifndef SYSTEM_CPU_THROTTLE_H
#define SYSTEM_CPU_THROTTLE_H

#include "qemu/timer.h"

void cpu_throttle_init(void);

void cpu_throttle_set(int new_throttle_pct);

void cpu_throttle_stop(void);

bool cpu_throttle_active(void);

int cpu_throttle_get_percentage(void);

void cpu_throttle_dirty_sync_timer_tick(void *opaque);

void cpu_throttle_dirty_sync_timer(bool enable);

#endif
