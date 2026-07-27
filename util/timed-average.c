/*
 * QEMU timed average computation
 *
 * Copyright (C) Nodalink, EURL. 2014
 * Copyright (C) Igalia, S.L. 2015
 *
 * Authors:
 *   Benoît Canet <benoit.canet@nodalink.com>
 *   Alberto Garcia <berto@igalia.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"

#include "qemu/timed-average.h"

static void update_expiration(TimedAverageWindow *w, int64_t now,
                              int64_t period)
{
    int64_t elapsed = (now - w->expiration) % period;
    int64_t remaining = period - elapsed;
    w->expiration = now + remaining;
}

static void window_reset(TimedAverageWindow *w)
{
    w->min = UINT64_MAX;
    w->max = 0;
    w->sum = 0;
    w->count = 0;
}

static TimedAverageWindow *current_window(TimedAverage *ta)
{
     return &ta->windows[ta->current];
}

void timed_average_init(TimedAverage *ta, QEMUClockType clock_type,
                        uint64_t period)
{
    int64_t now = qemu_clock_get_ns(clock_type);

    ta->period = (uint64_t) period * 4 / 3;
    ta->clock_type = clock_type;
    ta->current = 0;

    window_reset(&ta->windows[0]);
    window_reset(&ta->windows[1]);

    ta->windows[0].expiration = now + ta->period / 2;
    ta->windows[1].expiration = now + ta->period;
}

static void check_expirations(TimedAverage *ta, uint64_t *elapsed)
{
    int64_t now = qemu_clock_get_ns(ta->clock_type);
    int i;

    assert(ta->period != 0);

    for (i = 0; i < 2; i++) {
        TimedAverageWindow *w = &ta->windows[i];
        if (w->expiration <= now) {
            window_reset(w);
            update_expiration(w, now, ta->period);
        }
    }

    if (ta->windows[0].expiration < ta->windows[1].expiration) {
        ta->current = 0;
    } else {
        ta->current = 1;
    }

    if (elapsed) {
        int64_t remaining = ta->windows[ta->current].expiration - now;
        *elapsed = ta->period - remaining;
    }
}

void timed_average_account(TimedAverage *ta, uint64_t value)
{
    int i;
    check_expirations(ta, NULL);

    for (i = 0; i < 2; i++) {
        TimedAverageWindow *w = &ta->windows[i];

        w->sum += value;
        w->count++;

        if (value < w->min) {
            w->min = value;
        }

        if (value > w->max) {
            w->max = value;
        }
    }
}

uint64_t timed_average_min(TimedAverage *ta)
{
    TimedAverageWindow *w;
    check_expirations(ta, NULL);
    w = current_window(ta);
    return w->min < UINT64_MAX ? w->min : 0;
}

uint64_t timed_average_avg(TimedAverage *ta)
{
    TimedAverageWindow *w;
    check_expirations(ta, NULL);
    w = current_window(ta);
    return w->count > 0 ? w->sum / w->count : 0;
}

uint64_t timed_average_max(TimedAverage *ta)
{
    check_expirations(ta, NULL);
    return current_window(ta)->max;
}

uint64_t timed_average_sum(TimedAverage *ta, uint64_t *elapsed)
{
    TimedAverageWindow *w;
    check_expirations(ta, elapsed);
    w = current_window(ta);
    return w->sum;
}
