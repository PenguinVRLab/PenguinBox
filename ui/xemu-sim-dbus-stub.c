/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include "xemu-sim-dbus.h"

bool xemu_sim_dbus_available(void)
{
    return false;
}

bool xemu_sim_dbus_start(const char *const *keys, const char *const *values, int n, char *why, size_t whylen)
{
    (void)keys, (void)values, (void)n;
    snprintf(why, whylen, "this build has no D-Bus route (no gio)");
    return false;
}

SimDbusEnd xemu_sim_dbus_poll(char *why, size_t whylen)
{
    if (why && whylen) {
        snprintf(why, whylen, "this build has no D-Bus route (no gio)");
    }
    return SIM_DBUS_FAILED;
}

void xemu_sim_dbus_cancel(void)
{
}

SimDbusEnd xemu_sim_dbus_wait(char *why, size_t whylen)
{
    return xemu_sim_dbus_poll(why, whylen);
}
