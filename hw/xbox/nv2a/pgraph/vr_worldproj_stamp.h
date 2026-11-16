/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VR_WORLDPROJ_STAMP_H
#define HW_XBOX_NV2A_PGRAPH_VR_WORLDPROJ_STAMP_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "vr_worldproj_math.h"

typedef struct VrWpStamp {
    uint64_t seq;
    int64_t display_time;
    bool declarable;
    bool reprojected;
    VrWpView view;
    double xr_q[4];
    double xr_p[3];
    double tan[4];
} VrWpStamp;

typedef enum VrWpStampSource {
    VR_WP_STAMP_NONE,
    VR_WP_STAMP_COPIED,
    VR_WP_STAMP_LAST,
} VrWpStampSource;

#define VR_WP_STAMP_SLOTS 8

typedef struct VrWpStampMachine {
    struct {
        bool used;
        uint64_t key;
        VrWpStamp stamp;
    } slot[VR_WP_STAMP_SLOTS];
    VrWpStamp latch, last_declared;
    uint64_t seq;
    bool latch_open;
} VrWpStampMachine;

static inline bool vr_wp_stamp_latch(VrWpStampMachine *m, const VrWpStamp *s)
{
    if (m->latch_open) {
        return false;
    }
    m->latch = *s;
    m->latch.seq = ++m->seq;
    m->latch_open = true;
    return true;
}

static inline void vr_wp_stamp_forget(VrWpStampMachine *m, uint64_t key)
{
    for (int i = 0; i < VR_WP_STAMP_SLOTS; i++) {
        if (m->slot[i].used && m->slot[i].key == key) {
            memset(&m->slot[i], 0, sizeof(m->slot[i]));
        }
    }
}

static inline bool vr_wp_stamp_close(VrWpStampMachine *m, uint64_t key,
                                     bool drew, bool reprojected)
{
    int target = -1;

    if (!m->latch_open || !drew) {
        m->latch_open = false;
        vr_wp_stamp_forget(m, key);
        return false;
    }
    m->latch_open = false;
    m->latch.reprojected = reprojected;
    for (int i = 0; i < VR_WP_STAMP_SLOTS; i++) {
        if (m->slot[i].used && m->slot[i].key == key) {
            target = i;
            break;
        }
        if (!m->slot[i].used && target < 0) {
            target = i;
        }
    }
    if (target < 0) {
        target = 0;
        for (int i = 1; i < VR_WP_STAMP_SLOTS; i++) {
            if (m->slot[i].stamp.seq < m->slot[target].stamp.seq) {
                target = i;
            }
        }
    }
    m->slot[target].used = true;
    m->slot[target].key = key;
    m->slot[target].stamp = m->latch;
    return true;
}

static inline VrWpStampSource vr_wp_stamp_present(VrWpStampMachine *m,
                                                  uint64_t key, VrWpStamp *out)
{
    for (int i = 0; i < VR_WP_STAMP_SLOTS; i++) {
        if (m->slot[i].used && m->slot[i].key == key) {
            *out = m->slot[i].stamp;
            m->last_declared = *out;
            return VR_WP_STAMP_COPIED;
        }
    }
    if (m->last_declared.seq) {
        *out = m->last_declared;
        return VR_WP_STAMP_LAST;
    }
    memset(out, 0, sizeof(*out));
    return VR_WP_STAMP_NONE;
}

static inline bool vr_wp_stamp_is_projection(VrWpStampSource src,
                                             const VrWpStamp *s)
{
    return src != VR_WP_STAMP_NONE && s->declarable && s->reprojected;
}

static inline bool vr_wp_stamp_fresh(const VrWpStampMachine *m,
                                     const VrWpStamp *s, uint64_t max_age)
{
    return s->seq != 0 && m->seq - s->seq <= max_age;
}

static inline void vr_wp_stamp_invalidate(VrWpStampMachine *m)
{
    uint64_t seq = m->seq;

    memset(m, 0, sizeof(*m));
    m->seq = seq;
}

#endif
