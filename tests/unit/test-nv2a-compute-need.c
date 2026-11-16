/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdio.h>

#include "../../hw/xbox/nv2a/pgraph/vk/compute_need.h"

static int s_fail_count, s_check_failed;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "    %s:%d: CHECK(%s) failed\n", __FILE__,     \
                    __LINE__, #cond);                                      \
            s_check_failed = 1;                                            \
        }                                                                  \
    } while (0)
#define RUN(fn)                                                            \
    do {                                                                   \
        s_check_failed = 0;                                                \
        fn();                                                              \
        printf("%s %s\n", s_check_failed ? "FAIL" : "ok  ", #fn);          \
        s_fail_count += s_check_failed;                                    \
        n_run++;                                                           \
    } while (0)

#define MIB (1024ull * 1024ull)
#define PAIR_BYTES ((1024ull * 10) * (1024ull * 10) * 8)

static void d32s8_texture_is_five_bytes_a_pixel(void)
{
    uint64_t sp = 640ull * 480ull;
    Nv2aComputeNeed n = nv2a_cn_zeta_to_texture(true, sp, 4, 256);
    CHECK(n.dst == 4 * sp + sp);
    CHECK(n.dst == 1536000);
    CHECK(n.dst != 9 * sp);
    CHECK(n.src == 4 * sp);
}

static void stencil_plane_is_aligned(void)
{
    uint64_t sp = 3 * 3;
    CHECK(nv2a_cn_planes_bytes(true, sp, 64) == 64 + 9);
    CHECK(nv2a_cn_planes_bytes(true, sp, 1) == 36 + 9);
    CHECK(nv2a_cn_planes_bytes(true, sp, 0) == 36 + 9);
    CHECK(nv2a_cn_planes_bytes(false, sp, 64) == 18);
    Nv2aComputeNeed n = nv2a_cn_download(sp, sp, false, 64);
    CHECK(n.dst == 73);
}

static void d16_copy_out_term(void)
{
    uint64_t sp = 1280ull * 960ull;
    Nv2aComputeNeed n2 = nv2a_cn_zeta_to_texture(false, sp, 2, 256);
    CHECK(n2.dst == 2 * sp);
    CHECK(n2.src == 0);
    Nv2aComputeNeed n4 = nv2a_cn_zeta_to_texture(false, sp, 4, 256);
    CHECK(n4.dst == 4 * sp);
    CHECK(n4.src == 0);
    Nv2aComputeNeed n0 = nv2a_cn_zeta_to_texture(false, sp, 0, 256);
    CHECK(n0.dst == 2 * sp);
}

static void stencil_copy_out_reads_src(void)
{
    uint64_t sp = 100;
    Nv2aComputeNeed n = nv2a_cn_zeta_to_texture(true, sp, 2, 256);
    CHECK(n.src == 4 * sp);
    n = nv2a_cn_zeta_to_texture(true, sp, 8, 256);
    CHECK(n.src == 8 * sp);
    CHECK(n.dst == 512 + sp);
}

static void download_packs_at_guest_size_when_downscaling(void)
{
    uint64_t gp = 640ull * 480ull, sp = gp * 16;
    Nv2aComputeNeed d = nv2a_cn_download(gp, sp, true, 256);
    CHECK(d.dst == 4 * sp + sp);
    CHECK(d.src == 4 * gp);
    Nv2aComputeNeed s = nv2a_cn_download(gp, sp, false, 256);
    CHECK(s.src == 4 * sp);
}

static void upload_unpacks_at_scaled_size(void)
{
    uint64_t gp = 640ull * 480ull, sp = gp * 4;
    Nv2aComputeNeed u = nv2a_cn_upload(gp * 4, sp, 256);
    CHECK(u.dst == gp * 4);
    CHECK(u.src == 4 * sp + sp);
}

static void fits_is_inclusive_and_per_buffer(void)
{
    Nv2aComputeNeed n = { .dst = 100, .src = 50 };
    CHECK(nv2a_cn_fits(n, 100, 50));
    CHECK(!nv2a_cn_fits(n, 99, 50));
    CHECK(!nv2a_cn_fits(n, 100, 49));
    CHECK(nv2a_cn_total_max(n) == 100);
}

static void review_sizes(void)
{
    uint64_t align = 256;
    uint64_t sp = (640ull * 10) * (480ull * 10);
    Nv2aComputeNeed a = nv2a_cn_download(640ull * 480ull, sp, true, align);
    CHECK(a.dst / MIB == 146);
    CHECK(nv2a_cn_fits(a, PAIR_BYTES, PAIR_BYTES));
    sp = (2048ull * 8) * (2048ull * 8);
    Nv2aComputeNeed b = nv2a_cn_download(2048ull * 2048ull, sp, true, align);
    CHECK(b.dst == 1280 * MIB);
    CHECK(!nv2a_cn_fits(b, PAIR_BYTES, PAIR_BYTES));
    sp = (1920ull * 10) * (1080ull * 10);
    Nv2aComputeNeed c = nv2a_cn_upload(1920ull * 1080ull * 4, sp, align);
    CHECK(c.src / MIB == 988);
    CHECK(!nv2a_cn_fits(c, PAIR_BYTES, PAIR_BYTES));
}

static void scale_clamps_to_one_through_ten(void)
{
    CHECK(nv2a_surface_scale_clamp(99) == 10);
    CHECK(nv2a_surface_scale_clamp(11) == 10);
    CHECK(nv2a_surface_scale_clamp(10) == 10);
    CHECK(nv2a_surface_scale_clamp(4) == 4);
    CHECK(nv2a_surface_scale_clamp(1) == 1);
    CHECK(nv2a_surface_scale_clamp(0) == 1);
    CHECK(nv2a_surface_scale_clamp(-3) == 1);
}

static void pair_base_ladder_and_cap(void)
{
    CHECK(nv2a_cn_pair_base(1) == 20971520ull);
    CHECK(nv2a_cn_pair_base(2) == 83886080ull);
    CHECK(nv2a_cn_pair_base(3) == 188743680ull);
    CHECK(nv2a_cn_pair_base(4) == 335544320ull);
    CHECK(nv2a_cn_pair_base(6) == 754974720ull);
    CHECK(nv2a_cn_pair_base(7) == 838860800ull);
    CHECK(nv2a_cn_pair_base(10) == 838860800ull);
    CHECK(nv2a_cn_pair_base(7) == PAIR_BYTES);
}

static void grow_rounds_to_16_mib_under_the_cap(void)
{
    CHECK(nv2a_cn_grow_bytes(1, NV2A_CN_PAIR_CAP) == 16 * MIB);
    CHECK(nv2a_cn_grow_bytes(16 * MIB, NV2A_CN_PAIR_CAP) == 16 * MIB);
    CHECK(nv2a_cn_grow_bytes(16 * MIB + 1, NV2A_CN_PAIR_CAP) == 32 * MIB);
    CHECK(nv2a_cn_grow_bytes(55296000ull, NV2A_CN_PAIR_CAP) == 64 * MIB);
    CHECK(nv2a_cn_grow_bytes(NV2A_CN_PAIR_CAP, NV2A_CN_PAIR_CAP) == NV2A_CN_PAIR_CAP);
    CHECK(nv2a_cn_grow_bytes(NV2A_CN_PAIR_CAP - 1, NV2A_CN_PAIR_CAP) == NV2A_CN_PAIR_CAP);
    CHECK(nv2a_cn_grow_bytes(3 * MIB, 1 * MIB) == 1 * MIB);
}

int main(void)
{
    int n_run = 0;
    RUN(d32s8_texture_is_five_bytes_a_pixel);
    RUN(stencil_plane_is_aligned);
    RUN(d16_copy_out_term);
    RUN(stencil_copy_out_reads_src);
    RUN(download_packs_at_guest_size_when_downscaling);
    RUN(upload_unpacks_at_scaled_size);
    RUN(fits_is_inclusive_and_per_buffer);
    RUN(review_sizes);
    RUN(scale_clamps_to_one_through_ten);
    RUN(pair_base_ladder_and_cap);
    RUN(grow_rounds_to_16_mib_under_the_cap);
    printf("%d/%d passed\n", n_run - s_fail_count, n_run);
    return s_fail_count ? 1 : 0;
}
