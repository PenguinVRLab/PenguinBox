/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "vr_stereo_promo.h"

void vr_sp_init(VrStereoPromo *p, uint32_t decay)
{
    memset(p, 0, sizeof(*p));
    p->decay = decay ? decay : VR_SP_DECAY_DEFAULT;
}

static void note_edge(VrStereoPromo *p, const VrSpSurf *tgt,
                      const VrSpSurf *src)
{
    for (int i = 0; i < p->n_edge; i++) {
        if (p->edge[i].tgt.addr == tgt->addr &&
            p->edge[i].src.addr == src->addr &&
            p->edge[i].src.color == src->color &&
            p->edge[i].tgt.width == tgt->width &&
            p->edge[i].tgt.height == tgt->height &&
            p->edge[i].src.width == src->width &&
            p->edge[i].src.height == src->height) {
            return;
        }
    }
    if (p->n_edge == VR_SP_MAX_EDGES) {
        p->overflow++;
        return;
    }
    p->edge[p->n_edge].tgt = *tgt;
    p->edge[p->n_edge].src = *src;
    p->n_edge++;
}

static void note_pair(VrStereoPromo *p, uint64_t c, uint64_t z)
{
    for (int i = 0; i < p->n_pair; i++) {
        if (p->pair[i].c == c && p->pair[i].z == z) {
            return;
        }
    }
    if (p->n_pair == VR_SP_MAX_PAIRS) {
        p->overflow++;
        return;
    }
    p->pair[p->n_pair].c = c;
    p->pair[p->n_pair].z = z;
    p->n_pair++;
}

void vr_sp_note_draw(VrStereoPromo *p, const VrSpSurf *color,
                     const VrSpSurf *zeta, const VrSpSurf *src, int n)
{
    if (!color) {
        return;
    }
    if (zeta) {
        note_pair(p, color->addr, zeta->addr);
    }
    for (int i = 0; i < n; i++) {
        note_edge(p, color, &src[i]);
    }
}

typedef struct Qual {
    uint64_t addr;
    bool color;
    VrSpWhy why;
} Qual;

static bool qual_has(const Qual *q, int n, uint64_t addr, bool color)
{
    for (int i = 0; i < n; i++) {
        if (q[i].addr == addr && q[i].color == color) {
            return true;
        }
    }
    return false;
}

#define QMAX (VR_SP_MAX_SEEDS + VR_SP_MAX_EDGES + VR_SP_MAX_PAIRS)

static VrSpEntry *entry_get(VrStereoPromo *p, uint64_t addr, bool color,
                            bool add)
{
    for (int i = 0; i < p->n; i++) {
        if (p->e[i].addr == addr && p->e[i].color == color) {
            return &p->e[i];
        }
    }
    if (!add) {
        return NULL;
    }
    VrSpEntry *slot = NULL;
    if (p->n < VR_SP_MAX_ENTRIES) {
        slot = &p->e[p->n++];
    } else {
        for (int i = 0; i < p->n; i++) {
            if (!p->e[i].promoted && (!slot || p->e[i].idle > slot->idle)) {
                slot = &p->e[i];
            }
        }
        if (!slot) {
            p->overflow++;
            return NULL;
        }
    }
    memset(slot, 0, sizeof(*slot));
    slot->addr = addr;
    slot->color = color;
    return slot;
}

int vr_sp_flip(VrStereoPromo *p, const uint64_t *seeds, int n_seeds,
               VrSpChange *out, int max_out)
{
    Qual q[QMAX];
    int nq = 0;
    int nout = 0;

    if (n_seeds > VR_SP_MAX_SEEDS) {
        n_seeds = VR_SP_MAX_SEEDS;
    }
    for (int i = 0; i < n_seeds; i++) {
        if (!qual_has(q, nq, seeds[i], true)) {
            q[nq++] = (Qual){ seeds[i], true, VR_SP_SEED };
        }
    }

    bool grew = true;
    while (grew) {
        grew = false;
        for (int i = 0; i < p->n_edge; i++) {
            const VrSpSurf *t = &p->edge[i].tgt, *s = &p->edge[i].src;
            if (!s->color || !qual_has(q, nq, t->addr, true) ||
                qual_has(q, nq, s->addr, true)) {
                continue;
            }
            if (s->width >= t->width && s->height >= t->height) {
                q[nq++] = (Qual){ s->addr, true, VR_SP_SAMPLED };
                grew = true;
            }
        }
    }

    int nc = nq;
    for (int i = 0; i < p->n_pair; i++) {
        if (qual_has(q, nc, p->pair[i].c, true) &&
            !qual_has(q, nq, p->pair[i].z, false)) {
            q[nq++] = (Qual){ p->pair[i].z, false, VR_SP_ZETA };
        }
    }

    for (int i = 0; i < p->n; i++) {
        if (p->e[i].idle < UINT32_MAX) {
            p->e[i].idle++;
        }
    }
    for (int i = 0; i < nq; i++) {
        VrSpEntry *e = entry_get(p, q[i].addr, q[i].color, true);
        if (!e) {
            continue;
        }
        e->idle = 0;
        if (!e->promoted) {
            e->promoted = true;
            if (nout < max_out) {
                out[nout] = (VrSpChange){ e->addr, e->color, true, q[i].why };
            }
            nout++;
        }
    }
    for (int i = 0; i < p->n; i++) {
        VrSpEntry *e = &p->e[i];
        if (e->promoted && e->idle >= p->decay) {
            e->promoted = false;
            if (nout < max_out) {
                out[nout] = (VrSpChange){ e->addr, e->color, false, 0 };
            }
            nout++;
        }
    }

    p->n_edge = 0;
    p->n_pair = 0;
    return nout;
}

bool vr_sp_promoted(const VrStereoPromo *p, uint64_t addr, bool color)
{
    for (int i = 0; i < p->n; i++) {
        if (p->e[i].addr == addr && p->e[i].color == color) {
            return p->e[i].promoted;
        }
    }
    return false;
}

int vr_sp_list(const VrStereoPromo *p, VrSpEntry *out, int max_out)
{
    int k = 0;
    for (int i = 0; i < p->n; i++) {
        if (p->e[i].promoted) {
            if (k < max_out) {
                out[k] = p->e[i];
            }
            k++;
        }
    }
    return k;
}

int vr_sp_seeds(uint64_t flip_key, uint64_t presented, uint64_t seeds[2])
{
    int n = 0;

    if (flip_key) {
        seeds[n++] = flip_key;
    }
    if (presented && presented != flip_key) {
        seeds[n++] = presented;
    }
    return n;
}

void vr_sp_fold_pairs(const uint8_t *slot_views, uint32_t base,
                      uint64_t *results, uint32_t n,
                      void (*pair_cb)(uint64_t a, uint64_t b))
{
    if (!slot_views) {
        return;
    }
    for (uint32_t i = 0; i + 1 < n; i++) {
        if (slot_views[base + i] == 2) {
            uint64_t a = results[i], b = results[i + 1];
            if (pair_cb) {
                pair_cb(a, b);
            }
            results[i] = (a + b + 1) / 2;
            results[i + 1] = 0;
            i++;
        }
    }
}
