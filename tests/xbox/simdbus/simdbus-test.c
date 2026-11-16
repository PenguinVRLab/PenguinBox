/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "xemu-sim-dbus.h"
#include "xemu-sim-handoff.h"

static int fails, checks;
static const char *mode_file;

static void expect(const char *what, int ok)
{
    checks++;
    fails += !ok;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static SimHandoffOutcome run_mode(const char *mode, SimHandoffResult *r)
{
    FILE *f = fopen(mode_file, "w");
    fprintf(f, "%s\n", mode);
    fclose(f);
    return xemu_sim_handoff_run(0x43430002u, "Steel Battalion", NULL, r);
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s MODE_FILE DATA_HOME\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    mode_file = argv[1];
    setenv("XDG_DATA_HOME", argv[2], 1);
    setenv("XEMU_SIM_ROUTE", "dbus", 1);
    unsetenv("XEMU_SIM_CONFIGURATOR_CMD");
    SimHandoffResult r;
    if (getenv("SIMDBUS_NOT_INSTALLED")) {
        expect("no app on the bus is not_found (ServiceUnknown)", run_mode("saved", &r) == SIM_HANDOFF_NOT_FOUND &&
               strstr(r.why, "ServiceUnknown"));
        printf("%d checks, %d failed\n", checks, fails);
        return fails ? 1 : 0;
    }
    if (getenv("SIMDBUS_BROKEN_INSTALL")) {
        expect("a broken install (Spawn.*) is not_found, not busy or too old",
               run_mode("saved", &r) == SIM_HANDOFF_NOT_FOUND && strstr(r.why, "org.freedesktop.DBus.Error.Spawn."));
        printf("     why: %s\n", r.why);
        printf("%d checks, %d failed\n", checks, fails);
        return fails ? 1 : 0;
    }

    expect("the route is built with gio", xemu_sim_dbus_available());
    expect("saved: the bus starts the app, the result decides", run_mode("saved", &r) == SIM_HANDOFF_SAVED &&
           !strcmp(r.ports, "ports") && r.nwritten == 1);
    char args[1200], want[1200];
    snprintf(args, sizeof(args), "%s/sim-configurator/handoff/%s.yaml.args", argv[2], r.request_id);
    FILE *f = fopen(args, "r");
    char got[1200] = "";
    size_t n = f ? fread(got, 1, sizeof(got) - 1, f) : 0;
    got[n] = '\0';
    if (f) {
        fclose(f);
    }
    snprintf(want, sizeof(want), "title 0x43430002\nprofiles %s/sim-configurator/profiles\nresult "
             "%s/sim-configurator/handoff/%s.yaml\nrequest %s\n", argv[2], argv[2], r.request_id, r.request_id);
    expect("the dictionary carried the title, the store's profiles, the result path and the request id",
           !strcmp(got, want));
    if (strcmp(got, want)) {
        printf("     got:\n%s     want:\n%s", got, want);
    }
    expect("unchanged, the app gone before the first look (the result still decides)",
           run_mode("quick", &r) == SIM_HANDOFF_UNCHANGED);
    expect("cancel boot (outcome cancel_boot, ports null)", run_mode("cancel", &r) == SIM_HANDOFF_CANCEL_BOOT &&
           r.ports[0] == '\0');
    expect("Error.Busy is busy", run_mode("busy", &r) == SIM_HANDOFF_BUSY && strstr(r.why, "Busy"));
    expect("Error.Usage is too old", run_mode("usage", &r) == SIM_HANDOFF_TOO_OLD && strstr(r.why, "Usage"));
    expect("the name gone and no result is a crash", run_mode("nofile", &r) == SIM_HANDOFF_CRASH &&
           strstr(r.why, "without writing a result"));
    expect("a result for another request is a crash", run_mode("badid", &r) == SIM_HANDOFF_CRASH);

    f = fopen(mode_file, "w");
    fprintf(f, "slow\n");
    fclose(f);
    int started = xemu_sim_handoff_start(0x43430002u, "Steel Battalion", NULL, &r);
    usleep(1500 * 1000);
    SimHandoffResult r2;
    expect("polling while the app is open is not done", started && !xemu_sim_handoff_poll(&r2));
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    xemu_sim_handoff_cancel();
    int done = 0;
    for (int i = 0; i < 200 && !done; i++) {
        done = xemu_sim_handoff_poll(&r);
        if (!done) {
            usleep(20000);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    expect("Cancel ends the wait within 2 s, as a crash", done && r.outcome == SIM_HANDOFF_CRASH &&
           t1.tv_sec - t0.tv_sec < 2);
    expect("a request while an instance runs is busy", run_mode("saved", &r) == SIM_HANDOFF_BUSY);

    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
