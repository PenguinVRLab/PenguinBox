/*
 * AioContext wait support
 *
 * Copyright (C) 2018 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef QEMU_AIO_WAIT_H
#define QEMU_AIO_WAIT_H

#include "block/aio.h"
#include "qemu/main-loop.h"

typedef struct {
    unsigned num_waiters;
} AioWait;

extern AioWait global_aio_wait;

#define AIO_WAIT_WHILE_INTERNAL(ctx, cond) ({                      \
    bool waited_ = false;                                          \
    AioWait *wait_ = &global_aio_wait;                             \
    AioContext *ctx_ = (ctx);                                      \
     \
    qatomic_inc(&wait_->num_waiters);                              \
                   \
    smp_mb__after_rmw();                                           \
    if (ctx_ && in_aio_context_home_thread(ctx_)) {                \
        while ((cond)) {                                           \
            aio_poll(ctx_, true);                                  \
            waited_ = true;                                        \
        }                                                          \
    } else {                                                       \
        assert(qemu_get_current_aio_context() ==                   \
               qemu_get_aio_context());                            \
        while ((cond)) {                                           \
            aio_poll(qemu_get_aio_context(), true);                \
            waited_ = true;                                        \
        }                                                          \
    }                                                              \
    qatomic_dec(&wait_->num_waiters);                              \
    waited_; })

#define AIO_WAIT_WHILE(ctx, cond)                                  \
    AIO_WAIT_WHILE_INTERNAL(ctx, cond)

#define AIO_WAIT_WHILE_UNLOCKED(ctx, cond)                         \
    AIO_WAIT_WHILE_INTERNAL(ctx, cond)

void aio_wait_kick(void);

void aio_wait_bh_oneshot(AioContext *ctx, QEMUBHFunc *cb, void *opaque);

static inline bool in_aio_context_home_thread(AioContext *ctx)
{
    if (ctx == qemu_get_current_aio_context()) {
        return true;
    }

    if (ctx == qemu_get_aio_context()) {
        return bql_locked();
    } else {
        return false;
    }
}

#endif
