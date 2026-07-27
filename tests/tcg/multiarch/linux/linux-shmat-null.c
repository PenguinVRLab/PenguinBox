/*
 * Test shmat(NULL).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>

int main(void)
{
    int shmid;
    char *p;
    int err;

    shmid = shmget(IPC_PRIVATE, 1, IPC_CREAT | 0600);
    assert(shmid != -1);
    p = shmat(shmid, NULL, 0);
    assert(p != (void *)-1);
    *p = 42;

    err = shmdt(p);
    assert(err == 0);
    p = shmat(shmid, NULL, 0);
    assert(p != (void *)-1);
    assert(*p == 42);

    err = shmdt(p);
    assert(err == 0);
    err = shmctl(shmid, IPC_RMID, NULL);
    assert(err == 0);

    return EXIT_SUCCESS;
}
