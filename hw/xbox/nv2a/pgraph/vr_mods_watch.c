/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <string.h>
#include "vr_mods_watch.h"

static char *dup(const char *s)
{
    size_t n = strlen(s) + 1;
    return memcpy(malloc(n), s, n);
}

int vr_mods_add_watches(VrMod **mods, int n)
{
    for (int i = 0; i < vr_wp_profile_count; i++) {
        const VrWpProfile *p = &vr_wp_profiles[i];
        const VrWpWatch *w = p->requires_view;
        if (!w || w->n_words <= 0 || w->n_words * 4 > VR_MOD_FIND_MAX) {
            continue;
        }
        *mods = realloc(*mods, (n + 1) * sizeof(VrMod));
        VrMod *m = &(*mods)[n++];
        memset(m, 0, sizeof(*m));
        m->name = dup(w->name);
        m->slug = dup(w->name);
        m->version = dup("profile");
        m->source = dup("vr_worldproj_profiles.c");
        m->title_id = p->title_id;
        m->enabled = true;
        m->has_find = true;
        m->find.len = w->n_words * 4;
        m->find.align = 4;
        for (int k = 0; k < w->n_words; k++) {
            memcpy(m->find.bytes + 4 * k, &w->words[k], 4);
        }
        memset(m->find.mask, 0xFF, m->find.len);
        m->n_guards = 1;
        m->guards = calloc(1, sizeof(VrModGuard));
        m->guards[0] = (VrModGuard){ w->offset, w->equals, 4 };
    }
    return n;
}
