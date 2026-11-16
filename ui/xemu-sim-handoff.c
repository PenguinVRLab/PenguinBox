/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef XEMU_SIM_HANDOFF_STANDALONE
#include "qemu/osdep.h"
#endif
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef _WIN32
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef XEMU_SIM_HANDOFF_STANDALONE
extern char **environ;
#endif
#endif

#include "xemu-sim-dbus.h"
#include "xemu-sim-handoff.h"
#include "xemu-yaml-lite.h"

#define SIM_FLATPAK_ID "com.penguinvrlabs.simconfigurator"

const char *xemu_sim_handoff_outcome_name(SimHandoffOutcome o)
{
    switch (o) {
    case SIM_HANDOFF_SAVED: return "saved";
    case SIM_HANDOFF_UNCHANGED: return "unchanged";
    case SIM_HANDOFF_CANCEL_BOOT: return "cancel_boot";
    case SIM_HANDOFF_BUSY: return "busy";
    case SIM_HANDOFF_TOO_OLD: return "too_old";
    case SIM_HANDOFF_NOT_FOUND: return "not_found";
    case SIM_HANDOFF_CRASH: return "crash";
    case SIM_HANDOFF_SKIPPED: return "skipped";
    }
    return "?";
}

#if defined(__GNUC__)
__attribute__((format(printf, 3, 4)))
#endif
static SimHandoffOutcome set(SimHandoffResult *out, SimHandoffOutcome o, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    out->outcome = o;
    vsnprintf(out->why, sizeof(out->why), fmt, ap);
    va_end(ap);
    return o;
}

static void copy(char *dst, size_t n, const char *src)
{
    snprintf(dst, n, "%s", src ? src : "");
}

void xemu_sim_handoff_strv_free(char **v)
{
    if (v) {
        for (char **p = v; *p; p++) {
            free(*p);
        }
        free(v);
    }
}

static char **strv_push(char **v, size_t *n, const char *s)
{
    char **w = realloc(v, (*n + 2) * sizeof(char *));
    if (!w) {
        abort();
    }
    w[*n] = strdup(s);
    w[++*n] = NULL;
    return w;
}

char **xemu_sim_handoff_split(const char *cmd)
{
    char **v = NULL;
    size_t n = 0, len = strlen(cmd);
    char *word = malloc(len + 1);
    size_t w = 0;
    bool in_word = false;
    char quote = 0;

    for (size_t i = 0; i <= len; i++) {
        char c = cmd[i];
        if (quote) {
            if (c == '\0') {
                free(word);
                xemu_sim_handoff_strv_free(v);
                return NULL;
            }
            if (c == quote) {
                quote = 0;
            } else if (quote == '"' && c == '\\' && (cmd[i + 1] == '"' || cmd[i + 1] == '\\' ||
                                                     cmd[i + 1] == '$' || cmd[i + 1] == '`')) {
                word[w++] = cmd[++i];
            } else {
                word[w++] = c;
            }
            continue;
        }
        if (c == '\0' || c == ' ' || c == '\t' || c == '\n') {
            if (in_word) {
                word[w] = '\0';
                v = strv_push(v, &n, word);
                w = 0;
                in_word = false;
            }
            continue;
        }
        in_word = true;
        if (c == '\'' || c == '"') {
            quote = c;
        } else if (c == '\\' && cmd[i + 1]) {
            word[w++] = cmd[++i];
        } else {
            word[w++] = c;
        }
    }
    free(word);
    if (!v) {
        v = calloc(1, sizeof(char *));
    }
    return v;
}

#ifndef _WIN32
static char *find_in_path(const char *prog)
{
    const char *path = getenv("PATH");
    if (!path) {
        return NULL;
    }
    char *copy_ = strdup(path), *save = NULL;
    for (char *dir = strtok_r(copy_, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
        size_t n = strlen(dir) + strlen(prog) + 2;
        char *p = malloc(n);
        snprintf(p, n, "%s/%s", *dir ? dir : ".", prog);
        struct stat st;
        if (stat(p, &st) == 0 && S_ISREG(st.st_mode) && access(p, X_OK) == 0) {
            free(copy_);
            return p;
        }
        free(p);
    }
    free(copy_);
    return NULL;
}

static int run_wait(char *const argv[])
{
    pid_t pid;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    int rc = posix_spawnp(&pid, argv[0], &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) {
        return -1;
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
#endif

char **xemu_sim_handoff_command(const char *configured, char *why, size_t whylen)
{
    const char *env = getenv("XEMU_SIM_CONFIGURATOR_CMD");
    for (int k = 0; k < 2; k++) {
        const char *cmd = k == 0 ? env : configured;
        const char *what = k == 0 ? "XEMU_SIM_CONFIGURATOR_CMD" : "sim.configurator_command";
        if (!cmd || !*cmd) {
            continue;
        }
        char **v = xemu_sim_handoff_split(cmd);
        if (!v || !v[0]) {
            snprintf(why, whylen, "%s does not parse as a command line", what);
            xemu_sim_handoff_strv_free(v);
            return NULL;
        }
        return v;
    }
#ifdef _WIN32
    snprintf(why, whylen, "the Sim Configurator has no Windows build yet; set sim.configurator_command");
    return NULL;
#else
    size_t n = 0;
    char **v = NULL;
    char *path = find_in_path("sim-configurator");
    if (path) {
        v = strv_push(v, &n, path);
        free(path);
        return v;
    }
    char *flatpak = find_in_path("flatpak");
    if (flatpak) {
        char *info[] = { flatpak, (char *)"info", (char *)SIM_FLATPAK_ID, NULL };
        if (run_wait(info) == 0) {
            v = strv_push(v, &n, flatpak);
            v = strv_push(v, &n, "run");
            v = strv_push(v, &n, SIM_FLATPAK_ID);
            free(flatpak);
            return v;
        }
        free(flatpak);
    }
    snprintf(why, whylen, "the Sim Configurator was not found (tried XEMU_SIM_CONFIGURATOR_CMD, "
             "sim.configurator_command, sim-configurator on PATH, flatpak " SIM_FLATPAK_ID ")");
    return NULL;
#endif
}

static char *join(const char *a, const char *b)
{
    size_t n = strlen(a) + strlen(b) + 2;
    char *p = malloc(n);
    snprintf(p, n, "%s/%s", a, b);
    return p;
}

char *xemu_sim_handoff_store_dir(const char *leaf)
{
    const char *xdg = getenv(getenv("FLATPAK_ID") ? "HOST_XDG_DATA_HOME" : "XDG_DATA_HOME");
    char *root;
    if (xdg && xdg[0] == '/') {
        root = join(xdg, "sim-configurator");
    } else {
        const char *home = getenv("HOME");
        root = join(home && *home ? home : ".", ".local/share/sim-configurator");
    }
    if (!leaf || !*leaf) {
        return root;
    }
    char *p = join(root, leaf);
    free(root);
    return p;
}

char *xemu_sim_handoff_profile_dir(void)
{
    return xemu_sim_handoff_store_dir("profiles");
}

SimHandoffOutcome xemu_sim_handoff_judge(bool exited, int exit_status, const char *result_path,
                                         const char *request_id, uint32_t title_id,
                                         SimHandoffResult *out)
{
    out->exit_status = exited ? exit_status : -1;
    out->ports[0] = out->preflight[0] = '\0';
    out->nwritten = 0;
    copy(out->request_id, sizeof(out->request_id), request_id);

    if (exited && exit_status == 4) {
        return set(out, SIM_HANDOFF_BUSY, "the Sim Configurator is already open: close its window first, then boot the game again");
    }
    if (exited && exit_status == 2) {
        return set(out, SIM_HANDOFF_TOO_OLD, "this Sim Configurator does not know the hand-off flags "
                   "(exit 2): update it");
    }
    if (!exited || (exit_status != 0 && exit_status != 3)) {
        return set(out, SIM_HANDOFF_CRASH, exited ? "the Sim Configurator ended with exit %d"
                   : "the Sim Configurator was killed or stopped (signal %d)", exit_status);
    }

    char err[160] = "";
    YlNode *doc = yl_parse_file(result_path, err, sizeof(err));
    if (!doc) {
        return set(out, SIM_HANDOFF_CRASH, "exit %d but no readable result (%s)", exit_status, err);
    }
    SimHandoffOutcome o = SIM_HANDOFF_CRASH;
    const char *schema = yl_str(yl_get(doc, "schema"));
    const char *rid = yl_str(yl_get(doc, "request_id"));
    const char *outcome = yl_str(yl_get(doc, "outcome"));
    long long tid = -1;
    if (!schema || strcmp(schema, "sim-result/1") != 0) {
        set(out, o, "exit %d but the result is not sim-result/1", exit_status);
#ifdef SIM_HANDOFF_DRILL_NO_REQUEST_ID
    } else if (!rid && rid) {
#else
    } else if (!rid || strcmp(rid, request_id) != 0) {
        set(out, o, "exit %d but the result answers another request (%s)", exit_status, rid ? rid : "none");
#endif
    } else if (!yl_int(yl_get(doc, "title_id"), &tid) || tid != (long long)title_id) {
        set(out, o, "exit %d but the result is for another title", exit_status);
    } else if (outcome && strcmp(outcome, "cancel_boot") == 0 && exit_status == 3) {
        o = set(out, SIM_HANDOFF_CANCEL_BOOT, "boot cancelled in the Sim Configurator");
    } else if (outcome && strcmp(outcome, "saved") == 0 && exit_status == 0) {
        o = set(out, SIM_HANDOFF_SAVED, "saved in the Sim Configurator");
    } else if (outcome && strcmp(outcome, "unchanged") == 0 && exit_status == 0) {
        o = set(out, SIM_HANDOFF_UNCHANGED, "no change in the Sim Configurator");
    } else {
        set(out, o, "exit %d disagrees with the result's outcome '%s'", exit_status, outcome ? outcome : "none");
    }
    if (o != SIM_HANDOFF_CRASH) {
        const YlNode *pn = yl_get(doc, "ports");
        const char *ports = yl_str(pn);
        if (ports && pn && !pn->quoted && (!strcmp(ports, "null") || !strcmp(ports, "~"))) {
            ports = "";
        }
        copy(out->ports, sizeof(out->ports), ports);
        copy(out->preflight, sizeof(out->preflight), yl_str(yl_get(doc, "preflight")));
        out->nwritten = (int)yl_len(yl_get(doc, "written"));
    }
    yl_free(doc);
    return o;
}

static struct {
    bool active;
    bool dbus;
#ifndef _WIN32
    pid_t pid;
#endif
    uint32_t title_id;
    char result_path[1024];
    char request_id[48];
    struct timespec term_at;
} g_run;

bool xemu_sim_handoff_active(void)
{
    return g_run.active;
}

#ifndef _WIN32
static void new_request_id(char *out, size_t n)
{
    unsigned char b[16] = { 0 };
    int fd = open("/dev/urandom", O_RDONLY);
    ssize_t got = fd >= 0 ? read(fd, b, sizeof(b)) : -1;
    if (fd >= 0) {
        close(fd);
    }
    if (got != (ssize_t)sizeof(b)) {
        struct timespec t;
        clock_gettime(CLOCK_REALTIME, &t);
        uint64_t x = (uint64_t)t.tv_sec * 1000000007u ^ (uint64_t)t.tv_nsec ^ ((uint64_t)getpid() << 32);
        memcpy(b, &x, sizeof(x));
    }
    size_t k = (size_t)snprintf(out, n, "pb-");
    for (size_t i = 0; i < sizeof(b) && k + 2 < n; i++) {
        k += (size_t)snprintf(out + k, n - k, "%02x", b[i]);
    }
}

static bool make_dirs(const char *dir)
{
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", dir);
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(buf, 0700);
            *p = '/';
        }
    }
    mkdir(buf, 0700);
    struct stat st;
    return stat(buf, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool result_dir(char *out, size_t n, char *why, size_t whylen)
{
    char *dir = xemu_sim_handoff_store_dir("handoff");
    char *profiles = xemu_sim_handoff_profile_dir();
#ifdef SIM_HANDOFF_DRILL_NO_MKDIR
    bool ok = true;
    (void)make_dirs;
#else
    bool ok = make_dirs(dir) && make_dirs(profiles);
#endif
    if (!ok) {
        snprintf(why, whylen, "the Sim Configurator's shared folder could not be made (%s, %s): %s", dir, profiles,
                 strerror(errno));
    }
    snprintf(out, n, "%s", dir);
    free(dir);
    free(profiles);
    return ok;
}
#endif

bool xemu_sim_handoff_start(uint32_t title_id, const char *title_name, const char *configured_cmd,
                            SimHandoffResult *out)
{
    memset(out, 0, sizeof(*out));
    out->exit_status = -1;
    if (getenv("XEMU_NO_SIM_PREFLIGHT")) {
        set(out, SIM_HANDOFF_SKIPPED, "XEMU_NO_SIM_PREFLIGHT is set: the Sim Configurator hand-off is skipped");
        return false;
    }
    if (g_run.active) {
        set(out, SIM_HANDOFF_BUSY, "a Sim Configurator hand-off is already running");
        return false;
    }
#ifdef _WIN32
    (void)title_id, (void)title_name, (void)configured_cmd;
    set(out, SIM_HANDOFF_NOT_FOUND, "the Sim Configurator hand-off is Linux-only for now");
    return false;
#else
    char why[256] = "";
    const char *route = getenv("XEMU_SIM_ROUTE");
    bool cmd_set = (getenv("XEMU_SIM_CONFIGURATOR_CMD") && *getenv("XEMU_SIM_CONFIGURATOR_CMD")) ||
                   (configured_cmd && *configured_cmd);
    if ((route && strcmp(route, "dbus") == 0) || (!route && getenv("FLATPAK_ID") && !cmd_set)) {
        if (!xemu_sim_dbus_available()) {
            set(out, SIM_HANDOFF_NOT_FOUND, "this build has no D-Bus route to the Sim Configurator (no gio)");
            return false;
        }
        char dir[900];
        if (!result_dir(dir, sizeof(dir), why, sizeof(why))) {
            set(out, SIM_HANDOFF_NOT_FOUND, "%s", why);
            return false;
        }
        new_request_id(g_run.request_id, sizeof(g_run.request_id));
        snprintf(g_run.result_path, sizeof(g_run.result_path), "%s/%s.yaml", dir, g_run.request_id);
        unlink(g_run.result_path);
        char *profiles = xemu_sim_handoff_profile_dir();
        char tid[16];
        snprintf(tid, sizeof(tid), "0x%08x", title_id);
        const char *keys[] = { "configure-title", "title-name", "core", "profiles", "result", "request-id" };
        const char *vals[] = { tid, title_name && *title_name ? title_name : tid, "xbox", profiles,
                               g_run.result_path, g_run.request_id };
        bool ok = xemu_sim_dbus_start(keys, vals, 6, why, sizeof(why));
        fprintf(stderr, "sim-handoff: request %s %s over D-Bus (" SIM_DBUS_NAME ".ActivateAction configure-title, "
                "profiles %s, result %s)%s%s\n", g_run.request_id, ok ? "started" : "FAILED", profiles,
                g_run.result_path, ok ? "" : ": ", ok ? "" : why);
        free(profiles);
        if (!ok) {
            set(out, SIM_HANDOFF_NOT_FOUND, "%s", why);
            memset(&g_run, 0, sizeof(g_run));
            return false;
        }
        g_run.active = true;
        g_run.dbus = true;
        g_run.title_id = title_id;
        copy(out->request_id, sizeof(out->request_id), g_run.request_id);
        return true;
    }
    char **argv = xemu_sim_handoff_command(configured_cmd, why, sizeof(why));
    if (!argv) {
        set(out, SIM_HANDOFF_NOT_FOUND, "%s", why);
        return false;
    }
    char dir[900];
    if (!result_dir(dir, sizeof(dir), why, sizeof(why))) {
        xemu_sim_handoff_strv_free(argv);
        set(out, SIM_HANDOFF_NOT_FOUND, "%s", why);
        return false;
    }
    new_request_id(g_run.request_id, sizeof(g_run.request_id));
    snprintf(g_run.result_path, sizeof(g_run.result_path), "%s/%s.yaml", dir, g_run.request_id);
    unlink(g_run.result_path);

    char *profiles = xemu_sim_handoff_profile_dir();
    char tid[16];
    snprintf(tid, sizeof(tid), "0x%08x", title_id);
    size_t n = 0;
    while (argv[n]) {
        n++;
    }
    const char *flags[] = { "--configure-title", tid, "--title-name", title_name && *title_name ? title_name : tid,
                            "--core", "xbox", "--profiles", profiles, "--result", g_run.result_path,
                            "--request-id", g_run.request_id };
    for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); i++) {
        argv = strv_push(argv, &n, flags[i]);
    }
    free(profiles);

    pid_t pid = 0;
    int rc = posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ);
    fprintf(stderr, "sim-handoff: request %s %s (pid %d):", g_run.request_id, rc == 0 ? "started" : "FAILED",
            rc == 0 ? (int)pid : -1);
    for (char **p = argv; *p; p++) {
        fprintf(stderr, " %s", *p);
    }
    fprintf(stderr, "\n");
    if (rc != 0) {
        set(out, SIM_HANDOFF_NOT_FOUND, "the Sim Configurator could not start (%s): %s", argv[0], strerror(rc));
        xemu_sim_handoff_strv_free(argv);
        memset(&g_run, 0, sizeof(g_run));
        return false;
    }
    xemu_sim_handoff_strv_free(argv);
    g_run.active = true;
    g_run.pid = pid;
    g_run.title_id = title_id;
    copy(out->request_id, sizeof(out->request_id), g_run.request_id);
    return true;
#endif
}

#ifndef _WIN32
static bool finish(int status, bool reaped, SimHandoffResult *out)
{
    bool exited = reaped && WIFEXITED(status);
    int code = exited ? WEXITSTATUS(status) : (reaped && WIFSIGNALED(status) ? WTERMSIG(status) : -1);
    if (!reaped) {
        YlNode *doc = yl_parse_file(g_run.result_path, NULL, 0);
        const char *oc = doc ? yl_str(yl_get(doc, "outcome")) : NULL;
        exited = doc != NULL;
        code = oc && strcmp(oc, "cancel_boot") == 0 ? 3 : 0;
        yl_free(doc);
    }
    if (!reaped && !exited && !g_run.term_at.tv_sec) {
        set(out, SIM_HANDOFF_CRASH, "the Sim Configurator ended without writing a result");
        out->exit_status = -1;
        copy(out->request_id, sizeof(out->request_id), g_run.request_id);
    } else if (g_run.term_at.tv_sec && !(reaped && exited)) {
        set(out, SIM_HANDOFF_CRASH, "the Sim Configurator was closed from PenguinBox (Cancel)");
        out->exit_status = exited ? code : -1;
        copy(out->request_id, sizeof(out->request_id), g_run.request_id);
    } else {
        xemu_sim_handoff_judge(exited, code, g_run.result_path, g_run.request_id, g_run.title_id, out);
    }
    fprintf(stderr, "sim-handoff: request %s %s (exit %d): %s; ports '%s', preflight '%s', %d file(s) written\n",
            g_run.request_id, xemu_sim_handoff_outcome_name(out->outcome), out->exit_status, out->why, out->ports,
            out->preflight, out->nwritten);
    unlink(g_run.result_path);
    memset(&g_run, 0, sizeof(g_run));
    return true;
}
#endif

#ifndef _WIN32
static bool dbus_end(SimDbusEnd e, const char *why, SimHandoffResult *out)
{
    switch (e) {
    case SIM_DBUS_RUNNING:
        return false;
    case SIM_DBUS_EXITED:
        return finish(0, false, out);
    case SIM_DBUS_CANCELLED:
        g_run.term_at.tv_sec = 1;
        return finish(0, false, out);
    default:
        break;
    }
    memset(out, 0, sizeof(*out));
    out->exit_status = -1;
    copy(out->request_id, sizeof(out->request_id), g_run.request_id);
    if (e == SIM_DBUS_USAGE) {
        set(out, SIM_HANDOFF_TOO_OLD, "this Sim Configurator refused the request (%s): update it", why);
    } else if (e == SIM_DBUS_BUSY) {
        set(out, SIM_HANDOFF_BUSY, "the Sim Configurator is already open: close its window first, then boot the game again (%s)", why);
    } else if (e == SIM_DBUS_UNKNOWN) {
        set(out, SIM_HANDOFF_NOT_FOUND, "the Sim Configurator is not installed, or its install is broken (%s)", why);
    } else {
        set(out, SIM_HANDOFF_CRASH, "the Sim Configurator hand-off over D-Bus failed: %s", why);
    }
    fprintf(stderr, "sim-handoff: request %s %s over D-Bus: %s\n", g_run.request_id,
            xemu_sim_handoff_outcome_name(out->outcome), out->why);
    unlink(g_run.result_path);
    memset(&g_run, 0, sizeof(g_run));
    return true;
}
#endif

bool xemu_sim_handoff_poll(SimHandoffResult *out)
{
    if (!g_run.active) {
        return false;
    }
#ifdef _WIN32
    (void)out;
    return false;
#else
    if (g_run.dbus) {
        char why[320] = "";
        return dbus_end(xemu_sim_dbus_poll(why, sizeof(why)), why, out);
    }
    int status = 0;
    pid_t r = waitpid(g_run.pid, &status, WNOHANG);
    if (r == 0) {
        if (g_run.term_at.tv_sec) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (now.tv_sec - g_run.term_at.tv_sec >= 3) {
                kill(g_run.pid, SIGKILL);
            }
        }
        return false;
    }
    return finish(status, r == g_run.pid, out);
#endif
}

void xemu_sim_handoff_cancel(void)
{
#ifndef _WIN32
    if (g_run.active && g_run.dbus) {
        xemu_sim_dbus_cancel();
        return;
    }
    if (g_run.active && !g_run.term_at.tv_sec) {
        kill(g_run.pid, SIGTERM);
        clock_gettime(CLOCK_MONOTONIC, &g_run.term_at);
        if (!g_run.term_at.tv_sec) {
            g_run.term_at.tv_sec = 1;
        }
    }
#endif
}

SimHandoffOutcome xemu_sim_handoff_run(uint32_t title_id, const char *title_name,
                                       const char *configured_cmd,
                                       SimHandoffResult *out)
{
    if (!xemu_sim_handoff_start(title_id, title_name, configured_cmd, out)) {
        fprintf(stderr, "sim-handoff: %s: %s\n", xemu_sim_handoff_outcome_name(out->outcome), out->why);
        return out->outcome;
    }
#ifndef _WIN32
    if (g_run.dbus) {
        char why[320] = "";
        dbus_end(xemu_sim_dbus_wait(why, sizeof(why)), why, out);
        return out->outcome;
    }
    int status = 0;
    pid_t r;
    do {
        r = waitpid(g_run.pid, &status, 0);
    } while (r < 0 && errno == EINTR);
    finish(status, r == g_run.pid, out);
#endif
    return out->outcome;
}
