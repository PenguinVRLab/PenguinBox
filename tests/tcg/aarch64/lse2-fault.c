#include <sys/mman.h>
#include <sys/shm.h>
#include <unistd.h>
#include <stdio.h>

int main()
{
    int psize = getpagesize();
    int id;
    void *p;

    id = shmget(IPC_PRIVATE, 2 * psize, IPC_CREAT | 0600);
    if (id < 0) {
        perror("shmget");
        return 2;
    }
    p = shmat(id, NULL, 0);
    if (p == MAP_FAILED) {
        perror("shmat");
        return 2;
    }

    if (mprotect(p + psize, psize, PROT_NONE) < 0) {
        perror("mprotect");
        return 2;
    }

    return *(int *)(p + psize - 6);
}
