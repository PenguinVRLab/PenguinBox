/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Verify that a single TB spin-loop is properly invalidated,
 * releasing the thread from the spin-loop.
 */

#include <assert.h>
#include <sys/mman.h>
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <sched.h>


#ifdef __x86_64__
#define READY   0x000047c6
#define LOOP    0xfceb9090
#define RETURN  0x909090c3
#define NOP     0x90909090
#elif defined(__aarch64__)
#define READY   0x3900001f
#define LOOP    0x14000000
#define RETURN  0xd65f03c0
#define NOP     0xd503201f
#elif defined(__riscv)
#define READY   0x00050023
#define LOOP    0x0000006f
#define RETURN  0x00008067
#define NOP     0x00000013
#endif


int main()
{
#ifdef READY
    int tmp;
    pthread_t thread_id;
    bool hold = true;
    uint32_t *buf;

    buf = mmap(NULL, 3 * sizeof(uint32_t),
               PROT_READ | PROT_WRITE | PROT_EXEC,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(buf != MAP_FAILED);

    buf[0] = READY;
    buf[1] = LOOP;
    buf[2] = RETURN;

    alarm(2);

    tmp = pthread_create(&thread_id, NULL, (void *(*)(void *))buf, &hold);
    assert(tmp == 0);

    while (hold) {
        sched_yield();
    }

    buf[1] = NOP;
    __builtin___clear_cache(&buf[1], &buf[2]);

    tmp = pthread_join(thread_id, NULL);
    assert(tmp == 0);
#endif
    return 0;
}
