/*
 * Get user helper pc for memory unwinding.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef ACCEL_TCG_HELPER_RETADDR_H
#define ACCEL_TCG_HELPER_RETADDR_H

#ifdef CONFIG_USER_ONLY
extern __thread uintptr_t helper_retaddr;

static inline void set_helper_retaddr(uintptr_t ra)
{
    helper_retaddr = ra;
    signal_barrier();
}

static inline void clear_helper_retaddr(void)
{
    signal_barrier();
    helper_retaddr = 0;
}
#else
#define set_helper_retaddr(ra)   do { } while (0)
#define clear_helper_retaddr()   do { } while (0)
#endif

#endif
