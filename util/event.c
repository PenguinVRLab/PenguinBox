/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu/thread.h"

#define EV_SET         0
#define EV_FREE        1
#define EV_BUSY       -1

void qemu_event_init(QemuEvent *ev, bool init)
{
#ifndef HAVE_FUTEX
    pthread_mutex_init(&ev->lock, NULL);
    pthread_cond_init(&ev->cond, NULL);
#endif

    ev->value = (init ? EV_SET : EV_FREE);
    ev->initialized = true;
}

void qemu_event_destroy(QemuEvent *ev)
{
    assert(ev->initialized);
    ev->initialized = false;
#ifndef HAVE_FUTEX
    pthread_mutex_destroy(&ev->lock);
    pthread_cond_destroy(&ev->cond);
#endif
}

void qemu_event_set(QemuEvent *ev)
{
    assert(ev->initialized);

#ifdef HAVE_FUTEX
    smp_mb();
    if (qatomic_read(&ev->value) != EV_SET) {
        int old = qatomic_xchg(&ev->value, EV_SET);

        smp_mb__after_rmw();
        if (old == EV_BUSY) {
            qemu_futex_wake_all(ev);
        }
    }
#else
    pthread_mutex_lock(&ev->lock);
    qatomic_store_release(&ev->value, EV_SET);
    pthread_cond_broadcast(&ev->cond);
    pthread_mutex_unlock(&ev->lock);
#endif
}

void qemu_event_reset(QemuEvent *ev)
{
    assert(ev->initialized);

#ifdef HAVE_FUTEX
    qatomic_or(&ev->value, EV_FREE);

    smp_mb__after_rmw();
#else
    qatomic_set(&ev->value, qatomic_load_acquire(&ev->value) | EV_FREE);
#endif
}

void qemu_event_wait(QemuEvent *ev)
{
    assert(ev->initialized);

#ifdef HAVE_FUTEX
    while (true) {
        unsigned value = qatomic_load_acquire(&ev->value);
        if (value == EV_SET) {
            break;
        }

        if (value == EV_FREE) {
            if (qatomic_cmpxchg(&ev->value, EV_FREE, EV_BUSY) == EV_SET) {
                break;
            }
        }

        qemu_futex_wait(ev, EV_BUSY);
    }
#else
    pthread_mutex_lock(&ev->lock);
    while (qatomic_read(&ev->value) != EV_SET) {
        pthread_cond_wait(&ev->cond, &ev->lock);
    }
    pthread_mutex_unlock(&ev->lock);
#endif
}
