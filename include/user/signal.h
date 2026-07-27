/*
 * Signal-related declarations.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef USER_SIGNAL_H
#define USER_SIGNAL_H

#ifndef CONFIG_USER_ONLY
#error Cannot include this header from system emulation
#endif

int target_to_host_signal(int sig);

extern int host_interrupt_signal;

#endif
