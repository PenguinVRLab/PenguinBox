/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "hw/xbox/nv2a/nv2a_int.h"
#include "ui/xemu-settings.h"
#include "vr_mods.h"
#include "vr_mods_glue.h"
#include "vr_mods_watch.h"
#include "vr_worldproj_host.h"

#define VR_MODS_TITLE_RETRY 30
#define VR_MODS_TITLE_REFRESH 300

static bool mods_loaded;
static unsigned int title_age;
static int machine_changed;

static bool mods_enabled(void)
{
    static int env = -1;

    if (env < 0) {
        const char *e = getenv("XEMU_VR_MODS");
        env = !(e && (!strcmp(e, "0") || !g_ascii_strcasecmp(e, "off") ||
                      !g_ascii_strcasecmp(e, "false")));
        if (!env) {
            fprintf(stderr, "vr-mods: off (XEMU_VR_MODS=%s)\n", e);
        }
    }
    return env;
}

static char *shipped_dir(void)
{
    const char *e = getenv("XEMU_VR_MODS_DIR");

    if (e && *e) {
        return g_strdup(e);
    }
#ifdef __linux__
    g_autofree char *exe = g_file_read_link("/proc/self/exe", NULL);
    if (exe) {
        g_autofree char *dir = g_path_get_dirname(exe);
        return g_build_filename(dir, "vr-mods", NULL);
    }
#endif
    return NULL;
}

static void load(void)
{
    g_autofree char *user = g_build_filename(xemu_settings_get_base_path(),
                                             "vrmods", NULL);
    g_autofree char *ship = shipped_dir();
    VrMod *mods = NULL;
    int n = vr_mods_load(user, ship, &mods);

    n = vr_mods_add_watches(&mods, n);
    vr_mods_set_catalogue(mods, n);
    mods_loaded = true;
}

void vr_mods_flip(NV2AState *d)
{
    if (!mods_enabled()) {
        return;
    }
    if (!mods_loaded) {
        load();
    }
    if (__atomic_exchange_n(&machine_changed, 0, __ATOMIC_ACQ_REL)) {
        vr_mods_forget();
        vr_wp_host_title_request(true);
        title_age = 1;
    }
    int64_t id = vr_wp_host_title_id();
    unsigned int period = id < 0 ? VR_MODS_TITLE_RETRY : VR_MODS_TITLE_REFRESH;
    if (title_age == 0) {
        vr_wp_host_title_request(false);
    }
    title_age = (title_age + 1) % period;
    vr_mods_frame(d->vram_ptr, memory_region_size(d->vram), id,
                  vr_wp_host_title_version());
}

void vr_mods_machine_changed(void)
{
    __atomic_store_n(&machine_changed, 1, __ATOMIC_RELEASE);
}
