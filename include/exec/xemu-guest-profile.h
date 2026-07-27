/*
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef EXEC_XEMU_GUEST_PROFILE_H
#define EXEC_XEMU_GUEST_PROFILE_H

#ifdef XBOX

extern bool xemu_gp_enabled;

void xemu_guest_profile_tb(uint32_t eip);

void xemu_guest_profile_flip(void);

#endif

#endif
