/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "xemu-sim-handoff.h"

static int fails, checks;

static void expect(const char *what, int ok)
{
    checks++;
    fails += !ok;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static char mark[512], cmd[1024];

static char *slurp(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (!f) {
        return NULL;
    }
    static char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static SimHandoffOutcome run_mode(const char *mode, SimHandoffResult *r)
{
    setenv("STANDIN_MODE", mode, 1);
    unlink(mark);
    return xemu_sim_handoff_run(0x43430002u, "Steel Battalion", NULL, r);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s STANDIN_PY\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    char tmpl[] = "/tmp/simhandoff-test-XXXXXX";
    const char *dir = mkdtemp(tmpl);
    if (!dir) {
        perror("mkdtemp");
        return 2;
    }
    setenv("XDG_DATA_HOME", dir, 1);
    unsetenv("FLATPAK_ID");
    unsetenv("XEMU_NO_SIM_PREFLIGHT");
    snprintf(mark, sizeof(mark), "%s/standin.args", dir);
    setenv("STANDIN_MARK", mark, 1);
    snprintf(cmd, sizeof(cmd), "python3 '%s'", argv[1]);
    setenv("XEMU_SIM_CONFIGURATOR_CMD", cmd, 1);
    SimHandoffResult r;

    expect("A: saved", run_mode("saved", &r) == SIM_HANDOFF_SAVED);
    const char *args = slurp(mark);
    char want[1024];
    snprintf(want, sizeof(want), "--configure-title\n0x43430002\n--title-name\nSteel Battalion\n--core\nxbox\n"
             "--profiles\n%s/sim-configurator/profiles\n--result\n%s/sim-configurator/handoff/%s.yaml\n"
             "--request-id\n%s\n", dir, dir, r.request_id, r.request_id);
    expect("A: the app got the contract's flags, in order: the shared store's profiles, the result inside it", args &&
           strcmp(args, want) == 0);
    if (args && strcmp(args, want) != 0) {
        printf("     got:\n%s     want:\n%s", args, want);
    }
    expect("A: the request id is fresh and marked (pb- and 32 hex digits)",
           strncmp(r.request_id, "pb-", 3) == 0 && strlen(r.request_id) == 35);
    expect("A: the saved result's fields: ports 'ports', preflight READY, 2 files written",
           !strcmp(r.ports, "ports") && !strcmp(r.preflight, "READY") && r.nwritten == 2 && r.exit_status == 0);
    char used[512];
    snprintf(used, sizeof(used), "%s/sim-configurator/handoff/%s.yaml", dir, r.request_id);
    expect("A: the result file is removed after reading", access(used, F_OK) != 0);
    char first[48];
    snprintf(first, sizeof(first), "%s", r.request_id);
    run_mode("saved", &r);
    expect("A: a second hand-off has another request id", strcmp(first, r.request_id) != 0);

    expect("B: unchanged (exit 0)", run_mode("unchanged", &r) == SIM_HANDOFF_UNCHANGED && r.nwritten == 0);
    expect("B: cancel boot (exit 3)", run_mode("cancel", &r) == SIM_HANDOFF_CANCEL_BOOT && r.exit_status == 3);
    expect("B: cancel's `ports: null` reads as no ports document", r.ports[0] == '\0');
    expect("B: busy (exit 4)", run_mode("busy", &r) == SIM_HANDOFF_BUSY);
    expect("B: too old (exit 2)", run_mode("usage", &r) == SIM_HANDOFF_TOO_OLD);
    expect("B: a crash (a signal) is a crash", run_mode("crash", &r) == SIM_HANDOFF_CRASH && r.exit_status == -1);
    expect("B: exit 0 with no result is a crash", run_mode("nofile", &r) == SIM_HANDOFF_CRASH);
    expect("B: a result for another request is a crash", run_mode("badid", &r) == SIM_HANDOFF_CRASH);
    expect("B: a result for another title is a crash", run_mode("wrongtitle", &r) == SIM_HANDOFF_CRASH);
    expect("B: outcome saved with exit 3 is a crash", run_mode("disagree", &r) == SIM_HANDOFF_CRASH);

    setenv("STANDIN_MODE", "saved", 1);
    int started = xemu_sim_handoff_start(0x43430002u, "Steel Battalion", NULL, &r);
    expect("C: the background form starts", started && xemu_sim_handoff_active());
    SimHandoffResult r2;
    expect("C: a second start while one runs is refused", !xemu_sim_handoff_start(1, "x", NULL, &r2) &&
           r2.outcome == SIM_HANDOFF_BUSY);
    int done = 0;
    for (int i = 0; i < 300 && !done; i++) {
        done = xemu_sim_handoff_poll(&r);
        if (!done) {
            usleep(20000);
        }
    }
    expect("C: polling ends with saved", done && r.outcome == SIM_HANDOFF_SAVED && !xemu_sim_handoff_active());
    char readyf[600];
    snprintf(readyf, sizeof(readyf), "%s/standin.ready", dir);
    setenv("STANDIN_READY", readyf, 1);
    unlink(readyf);
    setenv("STANDIN_MODE", "slow", 1);
    started = xemu_sim_handoff_start(0x43430002u, "Steel Battalion", NULL, &r);
    for (int i = 0; i < 3000 && access(readyf, F_OK) != 0; i++) {
        usleep(10000);
    }
    expect("C: the slow stand-in is ready before the Cancel", access(readyf, F_OK) == 0);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    xemu_sim_handoff_cancel();
    done = 0;
    for (int i = 0; i < 400 && !done; i++) {
        done = xemu_sim_handoff_poll(&r);
        if (!done) {
            usleep(20000);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    expect("C: Cancel from PenguinBox ends a slow app within 5 s as Cancel boot (it writes cancel_boot, exits 3: the "
           "owner's \"write it as a cancel boot\")",
           started && done && r.outcome == SIM_HANDOFF_CANCEL_BOOT && r.exit_status == 3 && t1.tv_sec - t0.tv_sec < 5);
    unlink(readyf);
    setenv("STANDIN_MODE", "stubborn", 1);
    started = xemu_sim_handoff_start(0x43430002u, "Steel Battalion", NULL, &r);
    for (int i = 0; i < 3000 && access(readyf, F_OK) != 0; i++) {
        usleep(10000);
    }
    expect("C: the stubborn stand-in is ready before the Cancel", access(readyf, F_OK) == 0);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    xemu_sim_handoff_cancel();
    done = 0;
    for (int i = 0; i < 400 && !done; i++) {
        done = xemu_sim_handoff_poll(&r);
        if (!done) {
            usleep(20000);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    expect("C: an app that ignores the signal is killed after 3 s and is a crash (closed from PenguinBox)",
           started && done && r.outcome == SIM_HANDOFF_CRASH && strstr(r.why, "closed from PenguinBox") &&
           t1.tv_sec - t0.tv_sec < 6);

    char deep[700];
    snprintf(deep, sizeof(deep), "%s/fresh/data", dir);
    setenv("XDG_DATA_HOME", deep, 1);
    expect("C: a fresh XDG_DATA_HOME with no folders: saved (handoff/ and profiles/ made, parents too)",
           run_mode("saved", &r) == SIM_HANDOFF_SAVED);
    snprintf(deep, sizeof(deep), "%s/fresh/data/sim-configurator/profiles", dir);
    struct stat st;
    expect("C: ... the profiles folder exists", stat(deep, &st) == 0 && S_ISDIR(st.st_mode));
    char blocker[600];
    snprintf(blocker, sizeof(blocker), "%s/blocker", dir);
    FILE *bf = fopen(blocker, "w");
    if (bf) {
        fclose(bf);
    }
    snprintf(deep, sizeof(deep), "%s/x", blocker);
    setenv("XDG_DATA_HOME", deep, 1);
    unlink(mark);
    expect("C: a store that cannot be made is not_found, says so, and the app is never started",
           run_mode("saved", &r) == SIM_HANDOFF_NOT_FOUND && strstr(r.why, "could not be made") &&
           access(mark, F_OK) != 0);
    setenv("XDG_DATA_HOME", dir, 1);

    setenv("XEMU_NO_SIM_PREFLIGHT", "1", 1);
    unlink(mark);
    expect("D: the kill switch skips the hand-off", run_mode("saved", &r) == SIM_HANDOFF_SKIPPED);
    expect("D: ... and the stand-in never started (no mark file)", access(mark, F_OK) != 0);
    unsetenv("XEMU_NO_SIM_PREFLIGHT");
    unsetenv("XEMU_SIM_CONFIGURATOR_CMD");
    char *path = getenv("PATH") ? strdup(getenv("PATH")) : NULL;
    setenv("PATH", dir, 1);
    expect("D: nothing to run is not_found", run_mode("saved", &r) == SIM_HANDOFF_NOT_FOUND &&
           strstr(r.why, "not found") != NULL);
    if (path) {
        setenv("PATH", path, 1);
        free(path);
    }
    setenv("XEMU_SIM_CONFIGURATOR_CMD", "/nonexistent/sim-configurator", 1);
    expect("D: a command that cannot start is not_found", run_mode("saved", &r) == SIM_HANDOFF_NOT_FOUND);
    setenv("XEMU_SIM_ROUTE", "dbus", 1);
    expect("D: the D-Bus route in a build without it is not_found", run_mode("saved", &r) == SIM_HANDOFF_NOT_FOUND &&
           strstr(r.why, "D-Bus") != NULL);
    unsetenv("XEMU_SIM_ROUTE");
    setenv("XEMU_SIM_CONFIGURATOR_CMD", "python3 'unclosed", 1);
    expect("D: a command line with an open quote is not_found", run_mode("saved", &r) == SIM_HANDOFF_NOT_FOUND);

    char **v = xemu_sim_handoff_split("flatpak run 'a b' \"c \\\"d\\\"\" e\\ f");
    expect("E: the splitter keeps quoted words and escapes", v && v[0] && !strcmp(v[0], "flatpak") &&
           v[1] && !strcmp(v[1], "run") && v[2] && !strcmp(v[2], "a b") && v[3] && !strcmp(v[3], "c \"d\"") &&
           v[4] && !strcmp(v[4], "e f") && !v[5]);
    xemu_sim_handoff_strv_free(v);
    setenv("HOME", "/home/u", 1);
    setenv("XDG_DATA_HOME", "/xdg", 1);
    char *p1 = xemu_sim_handoff_profile_dir();
    setenv("XDG_DATA_HOME", "rel/data", 1);
    char *p2 = xemu_sim_handoff_profile_dir();
    unsetenv("XDG_DATA_HOME");
    char *p3 = xemu_sim_handoff_profile_dir();
    setenv("FLATPAK_ID", "app.xemu.xemu", 1);
    setenv("XDG_DATA_HOME", "/home/u/.var/app/app.xemu.xemu/data", 1);
    setenv("HOST_XDG_DATA_HOME", "/host/data", 1);
    char *p4 = xemu_sim_handoff_profile_dir();
    unsetenv("HOST_XDG_DATA_HOME");
    char *p5 = xemu_sim_handoff_profile_dir();
    char *h = xemu_sim_handoff_store_dir("handoff");
    expect("E: the store follows the app's rule: an absolute XDG_DATA_HOME", !strcmp(p1, "/xdg/sim-configurator/profiles"));
    expect("E: ... a relative XDG_DATA_HOME is ignored (the XDG spec): ~/.local/share",
           !strcmp(p2, "/home/u/.local/share/sim-configurator/profiles"));
    expect("E: ... none set: ~/.local/share", !strcmp(p3, "/home/u/.local/share/sim-configurator/profiles"));
    expect("E: inside a PenguinBox flatpak, the host's data folder, never the sandbox's",
           !strcmp(p4, "/host/data/sim-configurator/profiles") &&
           !strcmp(p5, "/home/u/.local/share/sim-configurator/profiles"));
    expect("E: the hand-off folder is inside the same store", !strcmp(h, "/home/u/.local/share/sim-configurator/handoff"));
    free(p1); free(p2); free(p3); free(p4); free(p5); free(h);

    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
