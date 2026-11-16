/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#ifndef XEMU_SIM_DBUS_H
#define XEMU_SIM_DBUS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SIM_DBUS_NAME "com.penguinvrlabs.simconfigurator"
#define SIM_DBUS_PATH "/com/penguinvrlabs/simconfigurator"

typedef enum SimDbusEnd {
    SIM_DBUS_RUNNING,
    SIM_DBUS_EXITED,
    SIM_DBUS_USAGE,
    SIM_DBUS_BUSY,
    SIM_DBUS_UNKNOWN,
    SIM_DBUS_FAILED,
    SIM_DBUS_CANCELLED,
} SimDbusEnd;

bool xemu_sim_dbus_available(void);

bool xemu_sim_dbus_start(const char *const *keys, const char *const *values, int n, char *why, size_t whylen);
SimDbusEnd xemu_sim_dbus_poll(char *why, size_t whylen);
void xemu_sim_dbus_cancel(void);
SimDbusEnd xemu_sim_dbus_wait(char *why, size_t whylen);

#ifdef __cplusplus
}
#endif

#endif
