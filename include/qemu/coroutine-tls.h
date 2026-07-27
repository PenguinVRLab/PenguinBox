/*
 * QEMU Thread Local Storage for coroutines
 *
 * Copyright Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 * It is forbidden to access Thread Local Storage in coroutines because
 * compiler optimizations may cause values to be cached across coroutine
 * re-entry. Coroutines can run in more than one thread through the course of
 * their life, leading bugs when stale TLS values from the wrong thread are
 * used as a result of compiler optimization.
 *
 * An example is:
 *
 * ..code-block:: c
 *   :caption: A coroutine that may see the wrong TLS value
 *
 *   static __thread AioContext *current_aio_context;
 *   ...
 *   static void coroutine_fn foo(void)
 *   {
 *       aio_notify(current_aio_context);
 *       qemu_coroutine_yield();
 *       aio_notify(current_aio_context); // <-- may be stale after yielding!
 *   }
 *
 * This header provides macros for safely defining variables in Thread Local
 * Storage:
 *
 * ..code-block:: c
 *   :caption: A coroutine that safely uses TLS
 *
 *   QEMU_DEFINE_STATIC_CO_TLS(AioContext *, current_aio_context)
 *   ...
 *   static void coroutine_fn foo(void)
 *   {
 *       aio_notify(get_current_aio_context());
 *       qemu_coroutine_yield();
 *       aio_notify(get_current_aio_context()); // <-- safe
 *   }
 */

#ifndef QEMU_COROUTINE_TLS_H
#define QEMU_COROUTINE_TLS_H

#define QEMU_DECLARE_CO_TLS(type, var)                                       \
    __attribute__((noinline)) type get_##var(void);                          \
    __attribute__((noinline)) void set_##var(type v);                        \
    __attribute__((noinline)) type *get_ptr_##var(void);

#define QEMU_DEFINE_CO_TLS(type, var)                                        \
    static __thread type co_tls_##var;                                       \
    type get_##var(void) { asm volatile(""); return co_tls_##var; }          \
    void set_##var(type v) { asm volatile(""); co_tls_##var = v; }           \
    type *get_ptr_##var(void)                                                \
    { type *ptr = &co_tls_##var; asm volatile("" : "+rm" (ptr)); return ptr; }

#define QEMU_DEFINE_STATIC_CO_TLS(type, var)                                 \
    static __thread type co_tls_##var;                                       \
    static __attribute__((noinline, unused))                                 \
    type get_##var(void)                                                     \
    { asm volatile(""); return co_tls_##var; }                               \
    static __attribute__((noinline, unused))                                 \
    void set_##var(type v)                                                   \
    { asm volatile(""); co_tls_##var = v; }                                  \
    static __attribute__((noinline, unused))                                 \
    type *get_ptr_##var(void)                                                \
    { type *ptr = &co_tls_##var; asm volatile("" : "+rm" (ptr)); return ptr; }

#endif
