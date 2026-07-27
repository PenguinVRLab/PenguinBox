/*
 * Arm SSE Subsystem System Counter
 *
 * Copyright (c) 2020 Linaro Limited
 * Written by Peter Maydell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

/*
 * This is a model of the "System counter" which is documented in
 * the Arm SSE-123 Example Subsystem Technical Reference Manual:
 * https://developer.arm.com/documentation/101370/latest/
 *
 * QEMU interface:
 *  + Clock input "CLK": clock
 *  + sysbus MMIO region 0: the control register frame
 *  + sysbus MMIO region 1: the status register frame
 *
 * Consumers of the system counter's timestamp, such as the SSE
 * System Timer device, can also use the APIs sse_counter_for_timestamp(),
 * sse_counter_tick_to_time() and sse_counter_register_consumer() to
 * interact with an instance of the System Counter. Generally the
 * consumer device should have a QOM link property which the board
 * code can set to the appropriate instance of the system counter.
 */

#ifndef SSE_COUNTER_H
#define SSE_COUNTER_H

#include "hw/sysbus.h"
#include "qom/object.h"
#include "qemu/notify.h"

#define TYPE_SSE_COUNTER "sse-counter"
OBJECT_DECLARE_SIMPLE_TYPE(SSECounter, SSE_COUNTER)

struct SSECounter {
    SysBusDevice parent_obj;

    MemoryRegion control_mr;
    MemoryRegion status_mr;
    Clock *clk;
    NotifierList notifier_list;

    uint32_t cntcr;
    uint32_t cntscr0;

    uint64_t ns_then;
    uint64_t ticks_then;
};

uint64_t sse_counter_for_timestamp(SSECounter *counter, uint64_t ns);

uint64_t sse_counter_tick_to_time(SSECounter *counter, uint64_t tick);

void sse_counter_register_consumer(SSECounter *counter, Notifier *notifier);

#endif
