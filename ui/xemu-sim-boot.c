/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "xemu-sim-boot.h"
#include "xemu-disc-xbe.h"
#include "xemu-notifications.h"
#include "xemu-settings.h"
#include "xemu-sim-handoff.h"

bool xemu_sim_boot_live(void)
{
    return false;
}

char *xemu_sim_boot_refusal(const char *label)
{
    return g_strdup_printf("%s is set to \"Always open in Sim Configurator\", and PenguinBox cannot apply a Sim "
                           "Configurator setup yet, so it is not booted. To boot it as usual, untick \"Always "
                           "open in Sim Configurator\" in the game's right-click menu.", label);
}

const char *xemu_sim_startup_disc(const char *dvd_path)
{
    if (!dvd_path || !*dvd_path) {
        return dvd_path;
    }
    if (getenv("XEMU_SIM_HANDOFF_DONE")) {
        return dvd_path;
    }
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
    if (!xemu_sim_boot_live()) {
        char *msg = xemu_sim_boot_refusal(info.title_name[0] ? info.title_name : tid);
        fprintf(stderr, "sim-boot: refused: %s\n", msg);
        xemu_queue_notification(msg);
        g_free(msg);
        return "";
    }
    SimHandoffResult r;
    SimHandoffOutcome o = xemu_sim_handoff_run(info.title_id, info.title_name, g_config.sim.configurator_command, &r);
    g_setenv("XEMU_SIM_HANDOFF_DONE", r.request_id[0] ? r.request_id : "1", true);
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
    case SIM_HANDOFF_SAVED:
    case SIM_HANDOFF_UNCHANGED:
        fprintf(stderr, "sim-boot: %s; the saved profile is not applied yet (the merge layer is not built): "
                "booting as usual\n", r.why);
        break;
    default:
        msg = g_strdup_printf("Sim Configurator: %s; booting as usual", r.why);
        break;
    }
    if (msg) {
        fprintf(stderr, "sim-boot: %s\n", msg);
        xemu_queue_notification(msg);
        g_free(msg);
    }
    return disc;
}
