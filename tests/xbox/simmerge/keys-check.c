/*
 * SPDX-License-Identifier: MIT
 */
#include <linux/input-event-codes.h>
#include <SDL3/SDL_scancode.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xemu-sim-keys.h"

int main(int argc, char **argv)
{
    int bad = 0, n = 0;
    static int seen_ev[1024], seen_sdl[1024];
#define CHECK(ev, sdl, kname, sname) \
    n++; \
    if ((ev) != (kname) || (sdl) != (sname)) { \
        printf("FAIL %s=%d (table %d), %s=%d (table %d)\n", #kname, (int)(kname), ev, #sname, (int)(sname), sdl); \
        bad++; \
    } \
    if (seen_ev[ev]++ || seen_sdl[sdl]++) { \
        printf("FAIL %s or %s twice\n", #kname, #sname); \
        bad++; \
    }
    SIM_EVDEV_KEYS(CHECK)
#undef CHECK
    printf("%s %d pairs agree with both headers\n", bad ? "FAIL" : "ok  ", n);

    int rc = bad;
    for (int a = 1; a < argc; a += 2) {
    int want = a + 1 < argc ? atoi(argv[a + 1]) : -1;
    int keys = 0, missing = 0;
    FILE *f = fopen(argv[a], "r");
    char line[512];
    while (f && fgets(line, sizeof(line), f)) {
        const char *p = strstr(line, "scancode: ");
        if (p && line[0] != '#') {
            int code = atoi(p + 10);
            keys++;
            if (!sim_evdev_to_sdl_scancode(code)) {
                printf("FAIL %s scancode %d has no SDL scancode\n", argv[a], code);
                missing++;
            }
        }
    }
    if (f) {
        fclose(f);
    }
    printf("%s %d of %d keys of %s map (%d expected)\n", missing || keys != want ? "FAIL" : "ok  ", keys - missing,
           keys, argv[a], want);
    rc = rc || missing || keys != want;
    }
    return rc;
}
