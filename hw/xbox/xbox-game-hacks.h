/*
 * Copyright (c) 2026 Patrick Carey
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#ifndef HW_XBOX_GAME_HACKS_H
#define HW_XBOX_GAME_HACKS_H

#include "qemu/typedefs.h"

void xbox_game_hacks_frame_tick(void);

void xbox_game_hacks_maybe_skip_scanner(CPUState *cs);

#endif
