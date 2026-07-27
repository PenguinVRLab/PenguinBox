#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
    int *ptr = (void *)0xdeadbeef;

    if (argc == 2 && strcmp(argv[1], "-s") == 0) {
        printf("%d\n", *ptr);
    }
}
