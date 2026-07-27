/* SPDX-License-Identifier: GPL-2.0-or-later */
/* See https://gitlab.com/qemu-project/qemu/-/issues/1648 */

#include <signal.h>

__attribute__((noinline))
void bar(void)
{
}

__attribute__((noinline, ms_abi))
void foo(void)
{
    bar();
}

void sighandler(int num)
{
    foo();
}

int main(void)
{
    signal(SIGUSR1, sighandler);
    raise(SIGUSR1);
    return 0;
}
