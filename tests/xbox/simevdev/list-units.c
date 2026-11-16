/*
 * SPDX-License-Identifier: MIT
 *
 * tests/xbox/simevdev/list-units: the input units on THIS machine as ui/xemu-sim-evdev.c finds them, with their
 * port paths, ids, nodes and codes. Not shipped; a positive control for the evdev source on real hardware.
 * It opens each unit's nodes READ-ONLY through sim_evdev_open (never grabbed: no EVIOCGRAB), prints each owned ABS
 * code's value and range (EVIOCGABS) and the buttons each node reports, and closes them.
 *   usage: list-units
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>

#include "xemu-sim-evdev.h"

int main(void)
{
    SimEvdevUnit *u = NULL;
    char why[1024];
    int n = sim_evdev_scan(&u, why, sizeof(why));
    if (n < 0) {
        printf("cannot list: %s\n", why);
        return 1;
    }
    printf("%d units\n", n);
    for (int i = 0; i < n; i++) {
        printf("unit %s  %04x:%04x  '%s'  kinds%s%s%s%s\n", u[i].port_path, u[i].vid & 0xffff, u[i].pid & 0xffff,
               u[i].name, u[i].kinds & SIM_EVDEV_KIND_JOYSTICK ? " joystick" : "",
               u[i].kinds & SIM_EVDEV_KIND_KEYBOARD ? " keyboard" : "",
               u[i].kinds & SIM_EVDEV_KIND_MOUSE ? " mouse" : "", u[i].kinds & SIM_EVDEV_KIND_OTHER ? " other" : "");
        for (int j = 0; j < u[i].nnodes; j++) {
            int nkeys = 0, first = -1, last = -1;
            printf("  node %s  abs:", u[i].node[j]);
            for (int c = 0; c < 0x40; c++) {
                if (sim_evdev_node_reports(&u[i], j, 3, c)) {
                    printf(" %d", c);
                }
            }
            for (int c = 0; c < 0x300; c++) {
                if (sim_evdev_node_reports(&u[i], j, 1, c)) {
                    nkeys++;
                    first = first < 0 ? c : first;
                    last = c;
                }
            }
            printf("  keys: %d", nkeys);
            if (nkeys) {
                printf(" (%d..%d)", first, last);
            }
            printf("\n");
        }
        SimEvdevReader *r = sim_evdev_open(&u[i], why, sizeof(why));
        if (!r) {
            printf("  open: %s\n", why);
            continue;
        }
        if (sim_evdev_reader_skipped(r)[0]) {
            printf("  not opened: %s\n", sim_evdev_reader_skipped(r));
        }
        for (int c = 0; c < 0x40; c++) {
            int v, lo, hi;
            if (sim_evdev_reader_abs(r, c, &v, &lo, &hi)) {
                printf("  abs %d: value %d range %d..%d\n", c, v, lo, hi);
            }
        }
        sim_evdev_close(r);
    }
    free(u);
    return 0;
}
