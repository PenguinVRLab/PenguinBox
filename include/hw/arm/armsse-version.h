/*
 * ARM SSE (Subsystems for Embedded): IoTKit, SSE-200
 *
 * Copyright (c) 2020 Linaro Limited
 * Written by Peter Maydell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#ifndef ARMSSE_VERSION_H
#define ARMSSE_VERSION_H


enum {
    ARMSSE_IOTKIT = 0,
    ARMSSE_SSE200 = 200,
    ARMSSE_SSE300 = 300,
};

static inline bool armsse_version_valid(uint32_t sse_version)
{
    switch (sse_version) {
    case ARMSSE_IOTKIT:
    case ARMSSE_SSE200:
    case ARMSSE_SSE300:
        return true;
    default:
        return false;
    }
}

#endif
