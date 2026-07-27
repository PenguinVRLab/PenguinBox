/*
 * Copyright (c) 2026 Patrick Carey
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_XBOX_NV2A_PGRAPH_PHASE_TIMERS_H
#define HW_XBOX_NV2A_PGRAPH_PHASE_TIMERS_H

#include <stdbool.h>
#include <stdint.h>

#include "qemu/atomic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PHASE_WINDOW 60

#define PHASE_HEAVY_MS 40

extern int nv2a_phase_timers_state;
bool nv2a_phase_timers_probe(void);

static inline bool nv2a_phase_timers_on(void)
{
    int s = qatomic_read(&nv2a_phase_timers_state);
    if (s < 0) {
        return nv2a_phase_timers_probe();
    }
    return s == 1;
}

int64_t nv2a_phase_now_ns(void);

void nv2a_phase_add_dl_wait(int64_t start_ns);
void nv2a_phase_add_fifo_wait(int64_t start_ns);

void nv2a_phase_add_pfifo_idle(int64_t start_ns);
void nv2a_phase_add_dl_proc(int64_t start_ns);
void nv2a_phase_add_present(int64_t start_ns);

enum {
    PF_LEAF_TEXHASH = 0,
    PF_LEAF_UPLOAD,
    PF_LEAF_DOWNLOAD,
    PF_LEAF_SUBMIT,
    PF_LEAF_QUERIES,
    PF_LEAF_PRESENT,
    PF_LEAF__COUNT
};

typedef struct NV2APhaseLeaf {
    int bucket;
    int active;
    int64_t start_ns;
} NV2APhaseLeaf;

NV2APhaseLeaf nv2a_phase_leaf_open(int bucket);
void nv2a_phase_leaf_close(NV2APhaseLeaf *l);

#define NV2A_PHASE_LEAF_CAT2(a, b) a##b
#define NV2A_PHASE_LEAF_CAT(a, b) NV2A_PHASE_LEAF_CAT2(a, b)
#define NV2A_PHASE_LEAF(bucket_id)                                       \
    NV2APhaseLeaf NV2A_PHASE_LEAF_CAT(nv2a_phase_leaf_, __LINE__)        \
        __attribute__((cleanup(nv2a_phase_leaf_close))) =               \
        nv2a_phase_timers_on()                                          \
            ? nv2a_phase_leaf_open(bucket_id)                            \
            : (NV2APhaseLeaf){ .bucket = (bucket_id), .active = 0,       \
                               .start_ns = 0 }

int64_t nv2a_phase_method_begin(void);
void nv2a_phase_method_end(int64_t start_ns);

enum {
    PF_MSUB_CONST = 0,
    PF_MSUB_ATTR,
    PF_MSUB_PIPE,
    PF_MSUB_DESC,
    PF_MSUB_DRAW,
    PF_MSUB_SYNCTEX,
    PF_MSUB_RPCHURN,
    PF_MSUB__COUNT
};

typedef struct NV2APhaseMsub {
    int active;
    int saved;
} NV2APhaseMsub;

NV2APhaseMsub nv2a_phase_msub_open(int cat);
void nv2a_phase_msub_close(NV2APhaseMsub *m);
void nv2a_phase_msub_mark(int cat);

NV2APhaseMsub nv2a_phase_msub_redirect_open(int cat);
void nv2a_phase_msub_redirect_close(NV2APhaseMsub *m);

#define NV2A_PHASE_MSUB_SCOPE(cat)                                       \
    NV2APhaseMsub NV2A_PHASE_LEAF_CAT(nv2a_phase_msub_, __LINE__)        \
        __attribute__((cleanup(nv2a_phase_msub_close))) =               \
        nv2a_phase_timers_on()                                          \
            ? nv2a_phase_msub_open(cat)                                  \
            : (NV2APhaseMsub){ .active = 0 }

#define NV2A_PHASE_MSUB(cat)                                              \
    do {                                                                 \
        if (nv2a_phase_timers_on()) {                                    \
            nv2a_phase_msub_mark(cat);                                   \
        }                                                                \
    } while (0)

#define NV2A_PHASE_MSUB_REDIRECT(cat)                                    \
    NV2APhaseMsub NV2A_PHASE_LEAF_CAT(nv2a_phase_msub_rd_, __LINE__)     \
        __attribute__((cleanup(nv2a_phase_msub_redirect_close))) =      \
        nv2a_phase_timers_on()                                          \
            ? nv2a_phase_msub_redirect_open(cat)                         \
            : (NV2APhaseMsub){ .active = 0, .saved = 0 }

enum {
    PF_PSUB_GPUWAIT = 0,
    PF_PSUB_COMPOSITE,
    PF_PSUB_VRSUBMIT,
    PF_PSUB__COUNT
};

void nv2a_phase_add_psub(int which, int64_t start_ns);

void nv2a_phase_flip(void);

#ifdef __cplusplus
}
#endif

#endif
