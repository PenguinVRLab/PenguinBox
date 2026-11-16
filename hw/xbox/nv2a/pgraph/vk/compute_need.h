/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_COMPUTE_NEED_H
#define HW_XBOX_NV2A_PGRAPH_VK_COMPUTE_NEED_H

#include <stdbool.h>
#include <stdint.h>

typedef struct Nv2aComputeNeed {
    uint64_t dst;
    uint64_t src;
} Nv2aComputeNeed;

static inline uint64_t nv2a_cn_round_up(uint64_t n, uint64_t align)
{
    if (align <= 1) {
        return n;
    }
    return (n + align - 1) / align * align;
}

static inline uint64_t nv2a_cn_max(uint64_t a, uint64_t b)
{
    return a > b ? a : b;
}

static inline uint64_t nv2a_cn_planes_bytes(bool has_stencil, uint64_t pixels,
                                            uint64_t align)
{
#if defined(NV2A_CN_PLANTED_OLD_EXPR)
    (void)align;
    return has_stencil ? 8 * pixels + pixels : 2 * pixels;
#elif defined(NV2A_CN_PLANTED_NO_ALIGN)
    (void)align;
    return has_stencil ? 4 * pixels + pixels : 2 * pixels;
#else
    if (has_stencil) {
        return nv2a_cn_round_up(4 * pixels, align) + pixels;
    }
    return 2 * pixels;
#endif
}

static inline Nv2aComputeNeed nv2a_cn_download(uint64_t guest_pixels,
                                               uint64_t scaled_pixels,
                                               bool downscale, uint64_t align)
{
    Nv2aComputeNeed n = {
        .dst = nv2a_cn_planes_bytes(true, scaled_pixels, align),
#ifdef NV2A_CN_PLANTED_DL_SCALED
        .src = 4 * scaled_pixels + 0 * guest_pixels * downscale,
#else
        .src = 4 * (downscale ? guest_pixels : scaled_pixels),
#endif
    };
    return n;
}

static inline Nv2aComputeNeed nv2a_cn_upload(uint64_t guest_bytes,
                                             uint64_t scaled_pixels,
                                             uint64_t align)
{
    Nv2aComputeNeed n = {
        .dst = guest_bytes,
        .src = nv2a_cn_planes_bytes(true, scaled_pixels, align),
    };
    return n;
}

static inline Nv2aComputeNeed nv2a_cn_zeta_to_texture(bool has_stencil,
                                                      uint64_t scaled_pixels,
                                                      uint64_t texel_size,
                                                      uint64_t align)
{
    uint64_t planes = nv2a_cn_planes_bytes(has_stencil, scaled_pixels, align);
#ifdef NV2A_CN_PLANTED_NO_COPYOUT
    uint64_t copy_out = 0 * texel_size;
#else
    uint64_t copy_out = texel_size * scaled_pixels;
#endif
    Nv2aComputeNeed n;
    if (has_stencil) {
        n.dst = planes;
        n.src = nv2a_cn_max(4 * scaled_pixels, copy_out);
    } else {
        n.dst = nv2a_cn_max(planes, copy_out);
        n.src = 0;
    }
    return n;
}

static inline bool nv2a_cn_fits(Nv2aComputeNeed need, uint64_t dst_size,
                                uint64_t src_size)
{
    return need.dst <= dst_size && need.src <= src_size;
}

static inline uint64_t nv2a_cn_total_max(Nv2aComputeNeed need)
{
    return nv2a_cn_max(need.dst, need.src);
}

#define NV2A_CN_PAIR_CAP 838860800ull
#define NV2A_CN_GROW_STEP (16ull * 1024 * 1024)

static inline uint64_t nv2a_cn_pair_base(unsigned int scale)
{
    uint64_t s = scale;
#ifdef NV2A_CN_PLANTED_BASE_LINEAR
    uint64_t b = 2048ull * 2048ull * 5 * s;
#else
    uint64_t b = 2048ull * 2048ull * 5 * s * s;
#endif
#ifdef NV2A_CN_PLANTED_NO_CAP
    return b;
#else
    return b < NV2A_CN_PAIR_CAP ? b : NV2A_CN_PAIR_CAP;
#endif
}

static inline uint64_t nv2a_cn_grow_bytes(uint64_t need, uint64_t cap)
{
    uint64_t g = nv2a_cn_round_up(need, NV2A_CN_GROW_STEP);
#ifdef NV2A_CN_PLANTED_NO_CAP
    (void)cap;
    return g;
#else
    return g < cap ? g : cap;
#endif
}

#define NV2A_SURFACE_SCALE_MAX 10

static inline unsigned int nv2a_surface_scale_clamp(int configured)
{
#ifdef NV2A_CN_PLANTED_CLAMP_LOW_ONLY
    return configured < 1 ? 1 : (unsigned int)configured;
#else
    if (configured < 1) {
        return 1;
    }
    if (configured > NV2A_SURFACE_SCALE_MAX) {
        return NV2A_SURFACE_SCALE_MAX;
    }
    return (unsigned int)configured;
#endif
}

#endif
