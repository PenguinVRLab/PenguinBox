/*
 * QEMU throttling infrastructure
 *
 * Copyright (C) Nodalink, EURL. 2013-2014
 * Copyright (C) Igalia, S.L. 2015-2016
 *
 * Authors:
 *   Benoît Canet <benoit.canet@nodalink.com>
 *   Alberto Garcia <berto@igalia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef THROTTLE_H
#define THROTTLE_H

#include "qapi/qapi-types-block-core.h"
#include "qemu/timer.h"

#define THROTTLE_VALUE_MAX 1000000000000000LL

typedef enum {
    THROTTLE_BPS_TOTAL,
    THROTTLE_BPS_READ,
    THROTTLE_BPS_WRITE,
    THROTTLE_OPS_TOTAL,
    THROTTLE_OPS_READ,
    THROTTLE_OPS_WRITE,
    BUCKETS_COUNT,
} BucketType;

typedef struct LeakyBucket {
    uint64_t avg;
    uint64_t max;
    double  level;
    double  burst_level;
    uint64_t burst_length;
} LeakyBucket;

typedef struct ThrottleConfig {
    LeakyBucket buckets[BUCKETS_COUNT];
    uint64_t op_size;
} ThrottleConfig;

typedef struct ThrottleState {
    ThrottleConfig cfg;
    int64_t previous_leak;
} ThrottleState;

typedef enum {
    THROTTLE_READ = 0,
    THROTTLE_WRITE,
    THROTTLE_MAX
} ThrottleDirection;

typedef struct ThrottleTimers {
    QEMUTimer *timers[THROTTLE_MAX];
    QEMUClockType clock_type;

    QEMUTimerCB *timer_cb[THROTTLE_MAX];
    void *timer_opaque;
} ThrottleTimers;

void throttle_leak_bucket(LeakyBucket *bkt, int64_t delta);

int64_t throttle_compute_wait(LeakyBucket *bkt);

void throttle_init(ThrottleState *ts);

void throttle_timers_init(ThrottleTimers *tt,
                          AioContext *aio_context,
                          QEMUClockType clock_type,
                          QEMUTimerCB *read_timer_cb,
                          QEMUTimerCB *write_timer_cb,
                          void *timer_opaque);

void throttle_timers_destroy(ThrottleTimers *tt);

void throttle_timers_detach_aio_context(ThrottleTimers *tt);

void throttle_timers_attach_aio_context(ThrottleTimers *tt,
                                        AioContext *new_context);

bool throttle_timers_are_initialized(ThrottleTimers *tt);

bool throttle_enabled(ThrottleConfig *cfg);

bool throttle_is_valid(ThrottleConfig *cfg, Error **errp);

void throttle_config(ThrottleState *ts,
                     QEMUClockType clock_type,
                     ThrottleConfig *cfg);

void throttle_get_config(ThrottleState *ts, ThrottleConfig *cfg);

void throttle_config_init(ThrottleConfig *cfg);

bool throttle_schedule_timer(ThrottleState *ts,
                             ThrottleTimers *tt,
                             ThrottleDirection direction);

void throttle_account(ThrottleState *ts, ThrottleDirection direction,
                      uint64_t size);
void throttle_limits_to_config(ThrottleLimits *arg, ThrottleConfig *cfg,
                               Error **errp);
void throttle_config_to_limits(ThrottleConfig *cfg, ThrottleLimits *var);

#endif
