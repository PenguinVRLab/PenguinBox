/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#ifndef XEMU_SIM_BOOT_H
#define XEMU_SIM_BOOT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool xemu_sim_boot_live(void);

char *xemu_sim_boot_refusal(const char *label);

const char *xemu_sim_startup_disc(const char *dvd_path);

#ifdef __cplusplus
}
#endif

#endif
