/*
 * Interface for configuring and controlling the state of tracing events.
 *
 * Copyright (C) 2012-2016 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TRACE__EVENT_INTERNAL_H
#define TRACE__EVENT_INTERNAL_H

#define TRACE_VCPU_EVENT_NONE ((uint32_t)-1)

typedef struct TraceEvent {
    uint32_t id;
    const char * name;
    const bool sstate;
    uint16_t *dstate;
} TraceEvent;

void trace_event_set_state_dynamic_init(TraceEvent *ev, bool state);

#endif
