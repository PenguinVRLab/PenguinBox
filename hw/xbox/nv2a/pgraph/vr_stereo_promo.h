/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VR_STEREO_PROMO_H
#define HW_XBOX_NV2A_PGRAPH_VR_STEREO_PROMO_H

#include <stdbool.h>
#include <stdint.h>

#define VR_SP_MAX_ENTRIES 64
#define VR_SP_MAX_EDGES 128
#define VR_SP_MAX_PAIRS 64
#define VR_SP_MAX_SEEDS 4
#define VR_SP_DECAY_DEFAULT 60

typedef struct VrSpSurf {
    uint64_t addr;
    uint32_t width, height;
    bool color;
} VrSpSurf;

typedef enum VrSpWhy {
    VR_SP_SEED = 1,
    VR_SP_SAMPLED = 2,
    VR_SP_ZETA = 3,
} VrSpWhy;

typedef struct VrSpChange {
    uint64_t addr;
    bool color;
    bool promoted;
    VrSpWhy why;
} VrSpChange;

typedef struct VrSpEntry {
    uint64_t addr;
    bool color;
    bool promoted;
    uint32_t idle;
} VrSpEntry;

typedef struct VrStereoPromo {
    uint32_t decay;
    VrSpEntry e[VR_SP_MAX_ENTRIES];
    int n;
    struct {
        VrSpSurf tgt, src;
    } edge[VR_SP_MAX_EDGES];
    int n_edge;
    struct {
        uint64_t c, z;
    } pair[VR_SP_MAX_PAIRS];
    int n_pair;
    unsigned int overflow;
} VrStereoPromo;

void vr_sp_init(VrStereoPromo *p, uint32_t decay);

void vr_sp_note_draw(VrStereoPromo *p, const VrSpSurf *color,
                     const VrSpSurf *zeta, const VrSpSurf *src, int n);

int vr_sp_flip(VrStereoPromo *p, const uint64_t *seeds, int n_seeds,
               VrSpChange *out, int max_out);

bool vr_sp_promoted(const VrStereoPromo *p, uint64_t addr, bool color);

int vr_sp_list(const VrStereoPromo *p, VrSpEntry *out, int max_out);

int vr_sp_seeds(uint64_t flip_key, uint64_t presented, uint64_t seeds[2]);

void vr_sp_fold_pairs(const uint8_t *slot_views, uint32_t base,
                      uint64_t *results, uint32_t n,
                      void (*pair_cb)(uint64_t a, uint64_t b));

#endif
