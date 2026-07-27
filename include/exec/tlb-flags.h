/*
 * TLB flags definition
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef TLB_FLAGS_H
#define TLB_FLAGS_H

#ifdef CONFIG_USER_ONLY

#define TLB_INVALID_MASK     (1 << 0)
#define TLB_MMIO             (1 << 1)
#define TLB_WATCHPOINT       0

#else

#define TLB_BSWAP            (1 << 0)
#define TLB_WATCHPOINT       (1 << 1)
#define TLB_CHECK_ALIGNED    (1 << 2)
#define TLB_DISCARD_WRITE    (1 << 3)
#define TLB_MMIO             (1 << 4)

#define TLB_SLOW_FLAGS_MASK \
    (TLB_BSWAP | TLB_WATCHPOINT | TLB_CHECK_ALIGNED | \
     TLB_DISCARD_WRITE | TLB_MMIO)

#define TLB_INVALID_MASK     (1 << 6)
#define TLB_NOTDIRTY         (1 << 7)
#define TLB_FORCE_SLOW       (1 << 8)

#define TLB_FLAGS_MASK \
    (TLB_INVALID_MASK | TLB_NOTDIRTY | TLB_FORCE_SLOW)

QEMU_BUILD_BUG_ON(TLB_FLAGS_MASK & TLB_SLOW_FLAGS_MASK);

#endif

#endif
