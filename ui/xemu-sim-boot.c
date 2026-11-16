/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include <SDL3/SDL.h>
#include "xemu-sim-boot.h"
#include "xemu-disc-xbe.h"
#include "xemu-input.h"
#include "xemu-notifications.h"
#include "xemu-settings.h"
#include "xemu-sim-bootplan.h"
#include "xemu-sim-handoff.h"
#include "xemu-sim-port.h"
#include "system/block-backend.h"

void xemu_eject_disc(Error **errp);

bool xemu_sim_boot_live(void)
{
    return true;
}

typedef struct SimSetup {
    SimProfileSet *set;
    SimPort ports[SIM_PROFILE_MAX_PORTS];
    SimBootPlan plan;
    char ports_id[96];
} SimSetup;

static SimSetup g_next;
static SimSetup g_cur;
static bool g_in_force;
static char *g_disc;
static char g_disc_node[64];
static unsigned char g_held_keys[SIM_BOOT_NKEYS];
static bool g_pending;
static char *g_pending_disc;

static char *g_restart_ports;
static char *g_restart_disc;
static char *g_handoff_done;

static void set_str(char **slot, const char *v)
{
    g_free(*slot);
    *slot = v && *v ? g_strdup(v) : NULL;
}

void xemu_sim_boot_export_for_restart(void)
{
    const char *names[3] = { "XEMU_SIM_BOOT_PORTS", "XEMU_SIM_BOOT_DISC", "XEMU_SIM_HANDOFF_DONE" };
    char *vals[3] = { g_restart_ports, g_restart_disc, g_handoff_done };
    for (int i = 0; i < 3; i++) {
        if (vals[i]) {
            g_setenv(names[i], vals[i], true);
        } else {
            g_unsetenv(names[i]);
        }
    }
    fprintf(stderr, "sim-boot: the restart hand-over: ports '%s', disc '%s', hand-off done for '%s'\n",
            g_restart_ports ? g_restart_ports : "", g_restart_disc ? g_restart_disc : "",
            g_handoff_done ? g_handoff_done : "");
}

static void setup_free(SimSetup *s)
{
    if (s->set) {
        sim_profile_set_free(s->set);
    }
    memset(s, 0, sizeof(*s));
}

static char *exe_path(void)
{
    const char *base = SDL_GetBasePath();
    return base ? g_strconcat(base, "qemu-system-i386", NULL) : NULL;
}

bool xemu_sim_boot_prepare(const char *ports_id, char *why, size_t whylen)
{
    setup_free(&g_next);
    char looked[1024];
    char *exe = exe_path();
    char *builtin = sim_bootplan_builtin_dir(exe, looked, sizeof(looked));
    g_free(exe);
    if (!builtin) {
        snprintf(why, whylen, "PenguinBox's own Sim Configurator documents are missing (looked at %s)", looked);
        fprintf(stderr, "sim-boot: %s\n", why);
        return false;
    }
    char *profiles = xemu_sim_handoff_profile_dir();
    g_next.set = sim_profile_set_load(builtin, profiles);
    fprintf(stderr, "sim-boot: loaded the shipped documents (%s) and the store (%s): %d refused\n", builtin,
            profiles ? profiles : "none", sim_profile_set_nrefused(g_next.set));
    for (int i = 0; i < sim_profile_set_nrefused(g_next.set); i++) {
        const SimRefusal *r = sim_profile_set_refusal(g_next.set, i);
        fprintf(stderr, "sim-boot:   refused %s %s: %s\n", r->code, r->where, r->why);
    }
    free(builtin);
    free(profiles);
    bool ok = sim_bootplan_from_set(g_next.set, ports_id, g_next.ports, NULL, &g_next.plan);
    for (int i = 0; i < SIM_PROFILE_MAX_PORTS; i++) {
        if (g_next.plan.line[i][0]) {
            fprintf(stderr, "sim-boot:   %s\n", g_next.plan.line[i]);
        }
    }
    fprintf(stderr, "sim-boot: plan %s: %s\n", ok ? "OK" : "REFUSED", g_next.plan.why);
    if (!ok) {
        snprintf(why, whylen, "%s", g_next.plan.why);
        setup_free(&g_next);
        return false;
    }
    g_strlcpy(g_next.ports_id, ports_id, sizeof(g_next.ports_id));
    return true;
}

bool xemu_sim_boot_apply(char *why, size_t whylen)
{
    if (!g_next.set) {
        snprintf(why, whylen, "no Sim Configurator setup was prepared");
        return false;
    }
    xemu_sim_boot_end("a new Sim Configurator boot");
    g_cur = g_next;
    memset(&g_next, 0, sizeof(g_next));
    xemu_input_strict_begin();
    for (int i = 0; i < SIM_PROFILE_MAX_PORTS; i++) {
        if (g_cur.plan.action[i] != SIM_PLAN_ATTACH) {
            continue;
        }
        char w[320];
        if (!xemu_sim_port_attach(i, &g_cur.ports[i], w, sizeof(w))) {
            snprintf(why, whylen, "port %d (profile %s): %s", i + 1, g_cur.ports[i].profile, w);
            fprintf(stderr, "sim-boot: %s\n", why);
            xemu_input_strict_end();
            setup_free(&g_cur);
            return false;
        }
    }
    g_in_force = true;
    int nheld = sim_boot_bound_keys(g_cur.ports, &g_cur.plan, g_held_keys);
    fprintf(stderr, "sim-boot: %d keys the setup binds are not emulator hotkeys while it is in force\n", nheld);
    set_str(&g_restart_ports, g_cur.ports_id);
    GString *msg = g_string_new("Sim Configurator setup in force:");
    for (int i = 0; i < SIM_PROFILE_MAX_PORTS; i++) {
        g_string_append_printf(msg, " port %d %s%s", i + 1, g_cur.plan.action[i] == SIM_PLAN_ATTACH ?
                               g_cur.ports[i].profile : "not connected", i < SIM_PROFILE_MAX_PORTS - 1 ? "," : "");
    }
    fprintf(stderr, "sim-boot: %s\n", msg->str);
    xemu_queue_notification(msg->str);
    g_string_free(msg, true);
    return true;
}

static const char *drive_disc(void)
{
    return g_config.sys.files.dvd_path ? g_config.sys.files.dvd_path : "";
}

static bool drive_medium(char *node, size_t nodelen)
{
    BlockBackend *blk = blk_by_name("ide0-cd1");
    BlockDriverState *bs = blk && blk_is_inserted(blk) ? blk_bs(blk) : NULL;
    if (!bs) {
        return false;
    }
    g_strlcpy(node, bdrv_get_node_name(bs), nodelen);
    return true;
}

void xemu_sim_boot_disc_loaded(const char *disc)
{
    if (!g_in_force) {
        return;
    }
    if (!disc || strcmp(drive_disc(), disc)) {
        xemu_sim_boot_end("the disc did not load");
        return;
    }
    g_free(g_disc);
    g_disc = g_strdup(disc);
    if (!drive_medium(g_disc_node, sizeof(g_disc_node))) {
        g_disc_node[0] = '\0';
    }
    set_str(&g_restart_disc, disc);
}

void xemu_sim_boot_end(const char *reason)
{
    if (!g_in_force) {
        return;
    }
    xemu_input_strict_end();
    setup_free(&g_cur);
    g_in_force = false;
    memset(g_held_keys, 0, sizeof(g_held_keys));
    g_free(g_disc);
    g_disc = NULL;
    g_disc_node[0] = '\0';
    set_str(&g_restart_ports, NULL);
    set_str(&g_restart_disc, NULL);
    char *msg = g_strdup_printf("Sim Configurator setup ended (%s): your own controllers are back", reason);
    fprintf(stderr, "sim-boot: %s\n", msg);
    xemu_queue_notification(msg);
    g_free(msg);
}

bool xemu_sim_boot_in_force(void)
{
    return g_in_force;
}

bool xemu_sim_boot_hotkey_held(int sc)
{
    return g_in_force && sim_boot_hotkey_held(g_held_keys, sc);
}

void xemu_sim_boot_poll(void)
{
    if (!g_in_force || !g_disc) {
        return;
    }
    char node[64];
    if (!drive_medium(node, sizeof(node))) {
        xemu_sim_boot_end("the disc was ejected");
    } else if (strcmp(node, g_disc_node)) {
        xemu_sim_boot_end("another disc was loaded");
    }
}

void xemu_sim_boot_input_ready(void)
{
    if (!g_pending) {
        return;
    }
    g_pending = false;
    char why[768];
    if (!xemu_sim_boot_apply(why, sizeof(why))) {
        char *msg = g_strdup_printf("The Sim Configurator setup cannot be applied, so the game is not booted: %s",
                                    why);
        fprintf(stderr, "sim-boot: %s\n", msg);
        xemu_queue_notification(msg);
        g_free(msg);
        xemu_eject_disc(NULL);
    } else {
        fprintf(stderr, "sim-boot: the startup disc's setup applied once the input layer was up\n");
        g_free(g_disc);
        g_disc = g_strdup(g_pending_disc);
        if (!drive_medium(g_disc_node, sizeof(g_disc_node))) {
            g_disc_node[0] = '\0';
        }
        set_str(&g_restart_disc, g_pending_disc);
    }
    g_free(g_pending_disc);
    g_pending_disc = NULL;
}

static char *g_not_installed_label;
static char *g_not_installed_why;

bool xemu_sim_boot_take_not_installed(char *label, size_t labellen, char *why, size_t whylen)
{
    if (!g_not_installed_label) {
        return false;
    }
    g_strlcpy(label, g_not_installed_label, labellen);
    g_strlcpy(why, g_not_installed_why ? g_not_installed_why : "", whylen);
    g_free(g_not_installed_label);
    g_free(g_not_installed_why);
    g_not_installed_label = NULL;
    g_not_installed_why = NULL;
    return true;
}

static void startup_pending(const char *dvd_path)
{
    g_pending = true;
    g_free(g_pending_disc);
    g_pending_disc = g_strdup(dvd_path);
    fprintf(stderr, "sim-boot: the setup is planned; it is applied when the input layer is up\n");
}

const char *xemu_sim_startup_disc(const char *dvd_path)
{
    static bool loaded;
    if (!loaded) {
        loaded = true;
        set_str(&g_restart_ports, getenv("XEMU_SIM_BOOT_PORTS"));
        set_str(&g_restart_disc, getenv("XEMU_SIM_BOOT_DISC"));
        set_str(&g_handoff_done, getenv("XEMU_SIM_HANDOFF_DONE"));
    }
    if (g_restart_ports && g_restart_disc) {
        char why[768];
        const char *rports = g_restart_ports;
        dvd_path = g_restart_disc;
        fprintf(stderr, "sim-boot: a restart of the Sim Configurator boot of %s: the ports document '%s' again\n",
                dvd_path, rports);
        if (!xemu_sim_boot_prepare(rports, why, sizeof(why))) {
            char *msg = g_strdup_printf("The Sim Configurator setup cannot be applied, so the game is not booted: "
                                        "%s", why);
            xemu_queue_notification(msg);
            g_free(msg);
            set_str(&g_restart_ports, NULL);
            set_str(&g_restart_disc, NULL);
            return "";
        }
        startup_pending(dvd_path);
        return dvd_path;
    }
    set_str(&g_restart_ports, NULL);
    set_str(&g_restart_disc, NULL);
    if (!dvd_path || !*dvd_path) {
        return dvd_path;
    }
    if (g_handoff_done && !strcmp(g_handoff_done, dvd_path)) {
        return dvd_path;
    }
    set_str(&g_handoff_done, NULL);
    XemuDiscXbeInfo info;
    char err[160], tid[9];
    if (xemu_disc_read_xbe_info(dvd_path, &info, err, sizeof(err)) != 0) {
        return dvd_path;
    }
    xemu_disc_title_id_str(info.title_id, tid);
    if (!xemu_settings_sim_always_open(tid)) {
        return dvd_path;
    }
    fprintf(stderr, "sim-boot: the startup disc's game %s (%s) is flagged \"Always open in Sim Configurator\"\n",
            tid, info.title_name);
    SimHandoffResult r;
    SimHandoffOutcome o = xemu_sim_handoff_run(info.title_id, info.title_name, g_config.sim.configurator_command, &r);
    char *msg = NULL;
    const char *disc = dvd_path;
    switch (o) {
    case SIM_HANDOFF_CANCEL_BOOT:
        msg = g_strdup_printf("Boot cancelled in the Sim Configurator: starting without the disc");
        disc = "";
        break;
    case SIM_HANDOFF_BUSY:
        msg = g_strdup_printf("Sim Configurator: %s; starting without the disc", r.why);
        disc = "";
        break;
    case SIM_HANDOFF_NOT_FOUND:
        fprintf(stderr, "sim-boot: " SIM_BOOT_NOT_INSTALLED " (%s): %s is not booted\n", r.why,
                info.title_name[0] ? info.title_name : tid);
        g_free(g_not_installed_label);
        g_free(g_not_installed_why);
        g_not_installed_label = g_strdup(info.title_name[0] ? info.title_name : tid);
        g_not_installed_why = g_strdup(r.why);
        disc = "";
        break;
    case SIM_HANDOFF_SAVED:
    case SIM_HANDOFF_UNCHANGED: {
        char why[768];
        fprintf(stderr, "sim-boot: %s; the ports document '%s'\n", r.why, r.ports);
        if (sim_boot_preflight_refuses(r.preflight, why, sizeof(why)) ||
            !xemu_sim_boot_prepare(r.ports, why, sizeof(why))) {
            msg = g_strdup_printf("The Sim Configurator setup cannot be applied, so %s is not booted: %s",
                                  info.title_name[0] ? info.title_name : tid, why);
            disc = "";
        } else {
            startup_pending(dvd_path);
        }
        break;
    }
    default:
        msg = g_strdup_printf("Sim Configurator: %s; booting as usual", r.why);
        set_str(&g_handoff_done, dvd_path);
        break;
    }
    if (msg) {
        fprintf(stderr, "sim-boot: %s\n", msg);
        xemu_queue_notification(msg);
        g_free(msg);
    }
    return disc;
}
