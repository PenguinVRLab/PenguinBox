/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vr_mods.h"

#define VR_MODS_SEARCH_PERIOD 60

typedef struct VrModState {
    bool bound;
    bool anchored;
    uint32_t anchor;
    int throttle;
    bool refused;
    bool armed;
    bool armed_said;
    bool waiting_said;
    bool ambiguous_said;
} VrModState;

static VrMod *mods;
static VrModState *state;
static int n_mods;
static int64_t bound_title = -1, bound_version = -1;
static int forget_pending;

void vr_mods_free(VrMod *m)
{
    free(m->name);
    free(m->slug);
    free(m->version);
    free(m->source);
    free(m->xbe_versions);
    free(m->guards);
    free(m->words);
    memset(m, 0, sizeof(*m));
}

void vr_mods_set_catalogue(VrMod *m, int n)
{
    for (int i = 0; i < n_mods; i++) {
        vr_mods_free(&mods[i]);
    }
    free(mods);
    free(state);
    mods = m;
    n_mods = n;
    state = n > 0 ? calloc(n, sizeof(*state)) : NULL;
    bound_title = bound_version = -1;
}

void vr_mods_forget(void)
{
    __atomic_store_n(&forget_pending, 1, __ATOMIC_RELEASE);
}

static bool selected(const VrMod *m, int64_t title, int64_t version)
{
    if (!m->enabled || title < 0 || m->title_id != (uint32_t)title) {
        return false;
    }
    if (m->n_xbe_versions == 0) {
        return true;
    }
    for (int i = 0; i < m->n_xbe_versions; i++) {
        if (version >= 0 && m->xbe_versions[i] == (uint32_t)version) {
            return true;
        }
    }
    return false;
}

static void bind(int64_t title, int64_t version)
{
    int n = 0;

    for (int i = 0; i < n_mods; i++) {
        state[i] = (VrModState){ .bound = selected(&mods[i], title, version) };
        n += state[i].bound;
    }
    if (title >= 0 && n > 0) {
        fprintf(stderr, "vr-mods: title %08X version %08llX%s: %d fix(es) "
                "bound\n", (unsigned)title,
                (unsigned long long)(version < 0 ? 0 : version),
                version < 0 ? " (not known)" : "", n);
    }
    bound_title = title;
    bound_version = version;
}

static uint32_t rd(const uint8_t *ram, uint32_t at, int width)
{
    uint32_t v = 0;
    memcpy(&v, ram + at, width);
    return v;
}

static bool inside(uint64_t ram_size, uint64_t at, int width)
{
    return at + (uint64_t)width <= ram_size;
}

static bool match_at(const VrMod *m, const uint8_t *ram, uint64_t off,
                     bool ours)
{
    const VrModFind *f = &m->find;

    for (int i = 0; i < f->len; i++) {
        if (!f->mask[i] || ram[off + i] == f->bytes[i]) {
            continue;
        }
        bool alt = false;
        for (int w = 0; ours && w < m->n_words && !alt; w++) {
            int64_t k = (int64_t)i - (int64_t)m->words[w].at;
            if (k >= 0 && k < 4) {
                alt = ram[off + i] == ((m->words[w].value >> (8 * k)) & 0xFF);
            }
        }
        if (!alt) {
            return false;
        }
    }
    return true;
}

static int search(const VrMod *m, const uint8_t *ram, uint64_t ram_size,
                  uint32_t *at)
{
    const VrModFind *f = &m->find;
    uint64_t lo = f->lo, hi = f->hi ? f->hi : ram_size;
    int found = 0;
    bool first_fixed = f->mask[0];

    for (int w = 0; w < m->n_words; w++) {
        if (m->words[w].at == 0) {
            first_fixed = false;
        }
    }

    if (hi > ram_size) {
        hi = ram_size;
    }
    if (f->len <= 0 || hi < (uint64_t)f->len || lo > hi - f->len) {
        return 0;
    }
    lo = (lo + f->align - 1) / f->align * f->align;
    for (uint64_t off = lo; off <= hi - f->len; off += f->align) {
        if (first_fixed) {
            const uint8_t *p = memchr(ram + off, f->bytes[0],
                                      hi - f->len - off + 1);
            if (!p) {
                break;
            }
            off = (uint64_t)(p - ram);
            if (off % f->align) {
                off = off / f->align * f->align;
                continue;
            }
        }
        if (match_at(m, ram, off, true)) {
            if (found++ == 0) {
                *at = (uint32_t)off;
            } else {
                return 2;
            }
        }
    }
    return found;
}

static bool guards_hold(const VrMod *m, const uint8_t *ram, uint64_t ram_size,
                        uint64_t base)
{
    for (int g = 0; g < m->n_guards; g++) {
        const VrModGuard *gd = &m->guards[g];
        uint64_t at = base + gd->at;
        if (!inside(ram_size, at, gd->width) ||
            rd(ram, (uint32_t)at, gd->width) != gd->equals) {
            return false;
        }
    }
    return true;
}

static void one(int i, uint8_t *ram, uint64_t ram_size)
{
    const VrMod *m = &mods[i];
    VrModState *s = &state[i];
    uint64_t base = 0;

    s->armed = false;
    if (m->has_find) {
        if (s->refused) {
            return;
        }
        if (s->anchored && !(inside(ram_size, s->anchor, m->find.len) &&
                             match_at(m, ram, s->anchor, true))) {
            fprintf(stderr, "vr-mods: '%s': the pattern no longer holds at "
                    "0x%08X: searching again\n", m->name, s->anchor);
            s->anchored = false;
            s->throttle = 0;
            return;
        }
        if (!s->anchored) {
            if (s->throttle > 0) {
                s->throttle--;
                return;
            }
            uint32_t at = 0;
            int k = search(m, ram, ram_size, &at);
            if (k == 2 && m->n_words == 0) {
                if (!s->ambiguous_said) {
                    fprintf(stderr, "vr-mods: '%s': the pattern matches more "
                            "than once (a watch: searching again every %d "
                            "frames)\n", m->name, VR_MODS_SEARCH_PERIOD);
                    s->ambiguous_said = true;
                }
                s->throttle = VR_MODS_SEARCH_PERIOD;
                return;
            }
            if (k == 2) {
                s->refused = true;
                fprintf(stderr, "vr-mods: '%s' (%s): the pattern matches more "
                        "than once: refused for this boot\n", m->name,
                        m->source);
                return;
            }
            if (k == 0) {
                if (!s->waiting_said) {
                    fprintf(stderr, "vr-mods: '%s': the pattern is not in RAM "
                            "yet: searching again every %d frames\n", m->name,
                            VR_MODS_SEARCH_PERIOD);
                    s->waiting_said = true;
                }
                s->throttle = VR_MODS_SEARCH_PERIOD;
                return;
            }
            s->anchored = true;
            s->anchor = at;
            fprintf(stderr, "vr-mods: '%s': found at 0x%08X\n", m->name, at);
        }
        base = s->anchor;
    }
    if (!guards_hold(m, ram, ram_size, base)) {
        return;
    }
    for (int w = 0; w < m->n_words; w++) {
        const VrModWord *wd = &m->words[w];
        uint64_t at = base + wd->at;
        if (!inside(ram_size, at, 4)) {
            return;
        }
        uint32_t now = rd(ram, (uint32_t)at, 4);
        if (m->has_find && now != wd->original && now != wd->value) {
            return;
        }
    }
    for (int w = 0; w < m->n_words; w++) {
        const VrModWord *wd = &m->words[w];
        uint32_t at = (uint32_t)(base + wd->at);
        if (rd(ram, at, 4) != wd->value) {
            memcpy(ram + at, &wd->value, 4);
        }
    }
    s->armed = true;
    if (!s->armed_said) {
        if (m->n_words == 0) {
            fprintf(stderr, "vr-mods: '%s' holds (a watch: nothing written)\n",
                    m->name);
        } else {
            fprintf(stderr, "vr-mods: '%s' armed: guards hold, %d word(s) kept "
                    "written%s\n", m->name, m->n_words,
                    m->has_find ? " at the anchor" : "");
        }
        s->armed_said = true;
    }
}

void vr_mods_frame(uint8_t *ram, uint64_t ram_size, int64_t title_id,
                   int64_t xbe_version)
{
    if (__atomic_exchange_n(&forget_pending, 0, __ATOMIC_ACQ_REL)) {
        bound_title = bound_version = -1;
        for (int i = 0; i < n_mods; i++) {
            state[i] = (VrModState){ 0 };
        }
    }
    if (n_mods == 0) {
        return;
    }
    if (title_id != bound_title || xbe_version != bound_version) {
        bind(title_id, xbe_version);
    }
    if (title_id < 0) {
        return;
    }
    for (int i = 0; i < n_mods; i++) {
        if (state[i].bound) {
            one(i, ram, ram_size);
        }
    }
}

bool vr_mods_armed(const char *slug)
{
    for (int i = 0; i < n_mods; i++) {
        if (state[i].bound && state[i].armed && mods[i].slug &&
            !strcmp(mods[i].slug, slug)) {
            return true;
        }
    }
    return false;
}
