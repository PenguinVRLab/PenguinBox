/*
 * Copyright (C) 2026 Patrick Carey
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "hw/boards.h"
#include "hw/ide/atapi-read-patch.h"
#include "xemu-machine-profile.h"
#include "xemu-sim-boot.h"
#include "xemu-yaml-lite.h"
#include "xemu-settings.h"
#include "xemu-notifications.h"
#include "system/runstate.h"
#include "block/block-global-state.h"
#include "qemu/job.h"

#ifdef _WIN32
#include <process.h>
#endif

static int launch_argc;
static char **launch_argv;
static int running_ram_mb;
static XemuMachineProfileState launch_state;
static QEMUTimer *watch_timer;
static char *watch_path;
static bool watch_first = true;

static const char *store_dir(void)
{
    static char *dir;
    if (!dir) {
        dir = g_path_get_dirname(xemu_settings_get_path());
    }
    return dir;
}

const char *xemu_machine_profile_store_path(void)
{
    static char *p;
    if (!p) {
        p = g_build_filename(store_dir(), "console-upgrades.toml", NULL);
    }
    return p;
}

const char *xemu_machine_profile_mods_dir(void)
{
    static char *p;
    if (!p) {
        p = g_build_filename(store_dir(), "mod-profiles", NULL);
    }
    return p;
}

void xemu_machine_profile_note_argv(int argc, char **argv)
{
    if (launch_argv) {
        return;
    }
    launch_argv = g_new0(char *, argc + 3);
    for (int i = 0; i < argc; i++) {
        if (!argv[i]) {
            continue;
        }
        if (i > 0 && !strcmp(argv[i], "-dvd_path")) {
            i++;
            continue;
        }
        launch_argv[launch_argc++] = g_strdup(argv[i]);
    }
}

static bool is_sha256(const char *s)
{
    if (!s || strlen(s) != 64) {
        return false;
    }
    for (int i = 0; i < 64; i++) {
        if (!g_ascii_isxdigit(s[i])) {
            return false;
        }
    }
    return true;
}

static int variant_hash_match(const YlNode *v, const char *sha, const char *file,
                              bool *keyed)
{
    const YlNode *h = yl_get(v, "xbe_sha256");
    if (!h || h->type == YL_NULL) {
        return -1;
    }
    *keyed = true;
    size_t n = h->type == YL_SEQ ? yl_len(h) : 1;
    for (size_t i = 0; i < n; i++) {
        const char *x = yl_str(h->type == YL_SEQ ? yl_at(h, i) : h);
        if (!is_sha256(x)) {
            fprintf(stderr, "mod-profile: %s line %d: xbe_sha256 '%s' is not 64 hex digits: "
                    "the variant never matches\n", file, h->line, x ? x : "?");
            continue;
        }
        if (sha && !g_ascii_strcasecmp(x, sha)) {
            return 1;
        }
    }
    return 0;
}

static const char *const reserved_machine_keys[] = { "cpu", "disk" };

static int machine_field(const YlNode *n, const char *file, XemuMpModKind *kind,
                         bool *clear)
{
    static const char *const sects[] = { "requires", "recommends" };
    for (int k = 0; k < 2; k++) {
        const YlNode *c = yl_get(yl_get(yl_get(n, sects[k]), "machine"),
                                 "xbe_clear_devkit_memory_limit");
        if (c && yl_bool(c) == 1) {
            *clear = true;
            if (*kind == XEMU_MP_MOD_NONE) {
                *kind = k == 0 ? XEMU_MP_MOD_REQUIRES : XEMU_MP_MOD_RECOMMENDS;
            }
        } else if (c) {
            fprintf(stderr, "mod-profile: %s line %d: xbe_clear_devkit_memory_limit must be "
                    "true or false\n", file, c->line);
        }
    }
    for (int k = 0; k < 2; k++) {
        const YlNode *m = yl_get(yl_get(n, sects[k]), "machine");
        if (!m) {
            continue;
        }
        for (size_t r = 0; r < G_N_ELEMENTS(reserved_machine_keys); r++) {
            if (yl_get(m, reserved_machine_keys[r])) {
                fprintf(stderr, "mod-profile: %s: %s.machine.%s is not built yet: ignored\n",
                        file, sects[k], reserved_machine_keys[r]);
            }
        }
        const YlNode *ram = yl_get(m, "ram_mb");
        if (!ram) {
            continue;
        }
        long long v;
        if (!yl_int(ram, &v) || (v != 64 && v != 128)) {
            fprintf(stderr, "mod-profile: %s line %d: machine.ram_mb must be 64 or 128: ignored\n",
                    file, ram->line);
            continue;
        }
        *kind = k == 0 ? XEMU_MP_MOD_REQUIRES : XEMU_MP_MOD_RECOMMENDS;
        return (int)v;
    }
    return 0;
}

static gint path_cmp(gconstpointer a, gconstpointer b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

int xemu_mp_mods_find(const char *mods_dir, const char *title_id,
                      const char *xbe_sha256, XemuMpModMachine *out)
{
    memset(out, 0, sizeof(*out));
    GDir *d = mods_dir ? g_dir_open(mods_dir, 0, NULL) : NULL;
    if (!d) {
        return 0;
    }
    GPtrArray *files = g_ptr_array_new_with_free_func(g_free);
    const char *e;
    while ((e = g_dir_read_name(d))) {
        if (g_str_has_suffix(e, ".yaml") || g_str_has_suffix(e, ".yml")) {
            g_ptr_array_add(files, g_build_filename(mods_dir, e, NULL));
        }
    }
    g_dir_close(d);
    g_ptr_array_sort(files, path_cmp);

    int result = 0;
    for (guint f = 0; f < files->len && !result; f++) {
        const char *file = g_ptr_array_index(files, f);
        char err[256];
        YlNode *root = yl_parse_file(file, err, sizeof(err));
        if (!root) {
            fprintf(stderr, "mod-profile: %s: %s: skipped\n", file, err);
            continue;
        }
        if (root->type != YL_MAP || yl_bool(yl_get(root, "enabled")) != 1) {
            yl_free(root);
            continue;
        }
        const YlNode *title = NULL;
        for (size_t i = 0; i < yl_len(root); i++) {
            const char *k = yl_key_at(root, i);
            if (strlen(k) == 8 && !g_ascii_strcasecmp(k, title_id)) {
                title = yl_at(root, i);
                break;
            }
        }
        const YlNode *variants = title && title->type == YL_SEQ ? title
                                 : yl_get(title, "variants");
        const YlNode *hit = NULL, *base = NULL;
        bool keyed = false;
        for (size_t i = 0; i < yl_len(variants) && variants->type == YL_SEQ; i++) {
            const YlNode *v = yl_at(variants, i);
            int m = variant_hash_match(v, xbe_sha256, file, &keyed);
            if (m == 1) {
                hit = v;
                break;
            }
            if (m == -1 && !base) {
                base = v;
            }
        }
        if (title && !hit && keyed && !xbe_sha256) {
            yl_free(root);
            result = 2;
            break;
        }
        if (!hit) {
            hit = base;
        }
        XemuMpModKind kind = XEMU_MP_MOD_NONE;
        bool clear = false;
        int mb = hit ? machine_field(hit, file, &kind, &clear) : 0;
        if (!mb && !clear && title && title->type == YL_MAP && (hit || !yl_len(variants))) {
            mb = machine_field(title, file, &kind, &clear);
        }
        if (mb || clear) {
            const char *name = yl_str(yl_get(root, "name"));
            char *stem = g_path_get_basename(file);
            out->kind = kind;
            out->ram_mb = mb;
            out->clear_devkit_limit = clear;
            snprintf(out->mod_name, sizeof(out->mod_name), "%s", name ? name : stem);
            const char *vn = hit ? yl_str(yl_get(hit, "name")) : NULL;
            snprintf(out->variant, sizeof(out->variant), "%s", vn ? vn : "base");
            snprintf(out->file, sizeof(out->file), "%s", file);
            g_free(stem);
            result = 1;
        }
        yl_free(root);
    }
    g_ptr_array_free(files, TRUE);
    return result;
}

static struct {
    bool armed;
    int64_t lba;
    uint32_t flags_off;
    uint32_t cert_tid_off;
    uint32_t title_id;
    char title[9];
    char mod[96];
    unsigned patched;
    unsigned refused;
} flag_patch;

static void flag_patch_hook(int64_t lba, uint8_t *data, int nb_sectors)
{
    if (!flag_patch.armed || flag_patch.lba < lba || flag_patch.lba >= lba + nb_sectors) {
        return;
    }
    uint8_t *sec = data + (flag_patch.lba - lba) * 2048;
    uint32_t tid = flag_patch.cert_tid_off ? (uint32_t)sec[flag_patch.cert_tid_off] |
                   ((uint32_t)sec[flag_patch.cert_tid_off + 1] << 8) |
                   ((uint32_t)sec[flag_patch.cert_tid_off + 2] << 16) |
                   ((uint32_t)sec[flag_patch.cert_tid_off + 3] << 24) : flag_patch.title_id;
    if (memcmp(sec, "XBEH", 4) || tid != flag_patch.title_id) {
        if (!flag_patch.refused++) {
            fprintf(stderr, "mod: %s: the sector at LBA %" PRId64 " is not %s's XBE header: "
                    "not patched\n", flag_patch.mod, flag_patch.lba, flag_patch.title);
        }
        return;
    }
    uint8_t before = sec[flag_patch.flags_off];
    sec[flag_patch.flags_off] = before & ~(uint8_t)XEMU_XBE_INIT_LIMIT_DEVKIT_MEMORY;
    if (!flag_patch.patched++) {
        fprintf(stderr, "mod: %s: XINIT_LIMIT_DEVKIT_MEMORY cleared (title %s; init flags "
                "byte 0x%02x -> 0x%02x as read from the disc)\n", flag_patch.mod,
                flag_patch.title, before, sec[flag_patch.flags_off]);
    }
}

static void flag_patch_disarm(const char *why)
{
    if (flag_patch.armed) {
        flag_patch.armed = false;
        fprintf(stderr, "mod: %s: flag clear disarmed (%s; %u header read(s) patched)\n",
                flag_patch.mod, why, flag_patch.patched);
    }
}

static void flag_patch_arm(const char *dvd_path, const XemuMachineProfileState *st)
{
    (void)dvd_path;
    if (!st->disc_known || !st->mod.clear_devkit_limit) {
        return;
    }
    if (!(st->xbe.init_flags & XEMU_XBE_INIT_LIMIT_DEVKIT_MEMORY)) {
        fprintf(stderr, "mod: %s: %s's XBE does not set XINIT_LIMIT_DEVKIT_MEMORY "
                "(init flags 0x%x): nothing to clear\n", st->mod.mod_name, st->title_id,
                st->xbe.init_flags);
        return;
    }
    if (st->effective_ram_mb != 128) {
        fprintf(stderr, "mod: %s: XINIT_LIMIT_DEVKIT_MEMORY clear waits for 128 MB (this "
                "session runs %d MB): not applied\n", st->mod.mod_name, st->effective_ram_mb);
        return;
    }
    if (st->xbe.xbe_offset % 2048) {
        fprintf(stderr, "mod: %s: default.xbe is not sector aligned: not applied\n",
                st->mod.mod_name);
        return;
    }
    memset(&flag_patch, 0, sizeof(flag_patch));
    flag_patch.lba = (int64_t)(st->xbe.xbe_offset / 2048);
    flag_patch.flags_off = 0x124;
    flag_patch.title_id = st->xbe.title_id;
    if (st->xbe.cert_offset && st->xbe.cert_offset + 0x0C <= 2048) {
        flag_patch.cert_tid_off = st->xbe.cert_offset + 0x08;
    }
    snprintf(flag_patch.title, sizeof(flag_patch.title), "%s", st->title_id);
    snprintf(flag_patch.mod, sizeof(flag_patch.mod), "%s", st->mod.mod_name);
    flag_patch.armed = true;
    ide_atapi_read_patch = flag_patch_hook;
    fprintf(stderr, "mod: %s: XINIT_LIMIT_DEVKIT_MEMORY will be cleared as %s's XBE header "
            "is read (LBA %" PRId64 ", init flags 0x%x -> 0x%x; the disc is not modified)\n",
            st->mod.mod_name, st->title_id, flag_patch.lba, st->xbe.init_flags,
            st->xbe.init_flags & ~XEMU_XBE_INIT_LIMIT_DEVKIT_MEMORY);
}

static int global_mb(void)
{
    return ((int)g_config.sys.mem_limit + 1) * 64;
}

static void xbe_sha256(const char *dvd_path, const XemuDiscXbeInfo *x, char out[65])
{
    out[0] = 0;
    FILE *f = fopen(dvd_path, "rb");
    if (!f) {
        return;
    }
    GChecksum *c = g_checksum_new(G_CHECKSUM_SHA256);
    uint8_t buf[65536];
    uint64_t left = x->xbe_size;
    bool ok = fseeko(f, (off_t)x->xbe_offset, SEEK_SET) == 0;
    while (ok && left) {
        size_t n = left < sizeof(buf) ? (size_t)left : sizeof(buf);
        ok = fread(buf, 1, n, f) == n;
        if (ok) {
            g_checksum_update(c, buf, n);
            left -= n;
        }
    }
    if (ok) {
        snprintf(out, 65, "%s", g_checksum_get_string(c));
    }
    g_checksum_free(c);
    fclose(f);
}

static void evaluate(const char *dvd_path, int gmb, XemuMachineProfileState *st)
{
    memset(st, 0, sizeof(*st));
    st->global_ram_mb = gmb;
    st->effective_ram_mb = gmb;
    if (!dvd_path || !dvd_path[0]) {
        snprintf(st->disc_error, sizeof(st->disc_error), "no disc");
        return;
    }
    if (xemu_disc_read_xbe_info(dvd_path, &st->xbe, st->disc_error,
                                sizeof(st->disc_error))) {
        return;
    }
    st->disc_known = true;
    xemu_disc_title_id_str(st->xbe.title_id, st->title_id);

    int mb = 0;
    xemu_mp_store_get_ram(xemu_machine_profile_store_path(), st->title_id, &mb,
                          st->store_note, sizeof(st->store_note));
    st->user_ram_mb = mb;
    if (mb) {
        st->effective_ram_mb = mb;
        st->from_profile = true;
    }

    int r = xemu_mp_mods_find(xemu_machine_profile_mods_dir(), st->title_id, NULL, &st->mod);
    if (r == 2) {
        char sha[65];
        xbe_sha256(dvd_path, &st->xbe, sha);
        xemu_mp_mods_find(xemu_machine_profile_mods_dir(), st->title_id,
                          sha[0] ? sha : "-", &st->mod);
    }
}

void xemu_machine_profile_evaluate(const char *dvd_path, XemuMachineProfileState *out)
{
    evaluate(dvd_path, global_mb(), out);
}

static void print_state(const XemuMachineProfileState *st, const char *when)
{
    if (st->from_profile) {
        fprintf(stderr, "system-memory: %d MB (per-game profile %s)%s\n",
                st->effective_ram_mb, st->title_id, when);
    } else if (st->disc_known) {
        fprintf(stderr, "system-memory: %d MB (global setting; title %s has no per-game choice)%s\n",
                st->effective_ram_mb, st->title_id, when);
    } else {
        fprintf(stderr, "system-memory: %d MB (global setting; disc title not read: %s)%s\n",
                st->effective_ram_mb, st->disc_error, when);
    }
    if (st->store_note[0]) {
        fprintf(stderr, "system-memory: %s\n", st->store_note);
    }
    if (st->disc_known && st->effective_ram_mb > 64 &&
        (st->xbe.init_flags & XEMU_XBE_INIT_LIMIT_DEVKIT_MEMORY)) {
        fprintf(stderr, "system-memory: note: %s's XBE sets the devkit 64 MB limit flag "
                "(init flags 0x%x); a debug kernel keeps such a title in the low 64 MB\n",
                st->title_id, st->xbe.init_flags);
    }
    if (st->mod.kind != XEMU_MP_MOD_NONE && st->mod.ram_mb &&
        st->mod.ram_mb != st->effective_ram_mb) {
        fprintf(stderr, "system-memory: mod profile \"%s\" (%s) %s %d MB for %s; not applied "
                "(the player chooses: Machine > System > This game)\n",
                st->mod.mod_name, st->mod.variant,
                st->mod.kind == XEMU_MP_MOD_REQUIRES ? "requires" : "recommends",
                st->mod.ram_mb, st->title_id);
    }
}

int xemu_machine_profile_launch_ram_mb(const char *dvd_path, int gmb)
{
    evaluate(dvd_path, gmb, &launch_state);
    running_ram_mb = launch_state.effective_ram_mb;
    print_state(&launch_state, " at launch");
    flag_patch_arm(dvd_path, &launch_state);
    g_free(watch_path);
    watch_path = g_strdup(dvd_path ? dvd_path : "");
    return running_ram_mb;
}

int xemu_machine_profile_running_ram_mb(void)
{
    return running_ram_mb;
}

static void watch_tick(void *opaque)
{
    const char *p = g_config.sys.files.dvd_path ? g_config.sys.files.dvd_path : "";
    bool first = watch_first;
    watch_first = false;
    if (first || strcmp(p, watch_path ? watch_path : "")) {
        XemuMachineProfileState st;
        if (first) {
            st = launch_state;
        } else {
            g_free(watch_path);
            watch_path = g_strdup(p);
            flag_patch_disarm("the disc changed");
            evaluate(p, global_mb(), &st);
            if (st.disc_known) {
                print_state(&st, " (disc loaded)");
            }
        }
        char *msg = NULL;
        if (st.disc_known && st.effective_ram_mb != running_ram_mb) {
            msg = g_strdup_printf("%s is set to %d MB of system memory; this session "
                                  "runs %d MB. Restart to apply (Machine > System).",
                                  st.xbe.title_name[0] ? st.xbe.title_name : st.title_id,
                                  st.effective_ram_mb, running_ram_mb);
        } else if (st.disc_known && st.from_profile && first) {
            msg = g_strdup_printf("System memory: %d MB (this game's profile)",
                                  st.effective_ram_mb);
        } else if (st.disc_known && st.mod.kind == XEMU_MP_MOD_REQUIRES &&
                   st.mod.ram_mb && st.mod.ram_mb != running_ram_mb) {
            msg = g_strdup_printf("Mod \"%s\" requires %d MB of system memory for this "
                                  "game. See Machine > System.", st.mod.mod_name,
                                  st.mod.ram_mb);
        }
        if (msg) {
            xemu_queue_notification(msg);
            g_free(msg);
        }
    }
    timer_mod(watch_timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + 1000);
}


void xemu_machine_profile_start(void)
{
    if (watch_timer) {
        return;
    }
    if (current_machine && current_machine->ram_size) {
        int mb = (int)(current_machine->ram_size >> 20);
        if (mb != running_ram_mb) {
            fprintf(stderr, "system-memory: %d MB (set on the command line; the profile asked %d MB)\n",
                    mb, running_ram_mb);
        }
        running_ram_mb = mb;
    }
    watch_timer = timer_new_ms(QEMU_CLOCK_REALTIME, watch_tick, NULL);
    timer_mod(watch_timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + 3000);
}

int xemu_machine_profile_set_choice(const XemuMachineProfileState *st, int ram_mb,
                                    char *err, size_t errlen)
{
    if (!st->disc_known) {
        snprintf(err, errlen, "no game disc read");
        return -1;
    }
    char sha[65] = "";
    const char *p = g_config.sys.files.dvd_path;
    if (ram_mb && p && p[0]) {
        xbe_sha256(p, &st->xbe, sha);
    }
    int r = xemu_mp_store_set_ram(xemu_machine_profile_store_path(), st->title_id,
                                  st->xbe.title_name, sha, ram_mb, err, errlen);
    if (!r) {
        fprintf(stderr, "system-memory: per-game choice for %s set to %s (%s)\n",
                st->title_id, ram_mb ? (ram_mb == 128 ? "128 MB" : "64 MB") : "use global",
                xemu_machine_profile_store_path());
    }
    return r;
}

static int stop_guest_and_close_disks(char *err, size_t errlen)
{
    int r = vm_shutdown();
    if (r) {
        snprintf(err, errlen, "the hard disk could not be written (%s): PenguinBox did not restart; "
                 "quit it from the menu", strerror(r < 0 ? -r : EIO));
        fprintf(stderr, "system-memory: %s\n", err);
        return -1;
    }
    bdrv_drain_all_begin();
    job_cancel_sync_all();
    bdrv_close_all();
    fprintf(stderr, "system-memory: guest stopped, disks flushed and closed\n");
    return 0;
}

int xemu_machine_profile_restart(char *err, size_t errlen)
{
    if (!launch_argv || !launch_argc) {
        snprintf(err, errlen, "launch arguments unknown");
        return -1;
    }
    char **args = g_new0(char *, launch_argc + 3);
    int n = 0;
    args[n++] = launch_argv[0];
    args[n++] = (char *)"-config_path";
    args[n++] = (char *)xemu_settings_get_path();
    for (int i = 1; i < launch_argc; i++) {
        args[n++] = launch_argv[i];
    }
    args[n] = NULL;
    xemu_settings_save();
    if (stop_guest_and_close_disks(err, errlen)) {
        g_free(args);
        return -1;
    }
    xemu_sim_boot_export_for_restart();
    fprintf(stderr, "system-memory: restarting PenguinBox to apply the system memory size\n");
    fflush(NULL);
#if defined(_WIN32)
    _execv(args[0], (const char *const *)args);
#elif defined(__linux__)
    execv("/proc/self/exe", args);
    execv(args[0], args);
#else
    execv(args[0], args);
#endif
    snprintf(err, errlen, "restart failed (%s): close and start PenguinBox again", strerror(errno));
    g_free(args);
    return -1;
}
