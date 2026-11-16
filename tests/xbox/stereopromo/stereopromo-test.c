/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>
#include "vr_stereo_promo.h"

static int fails, checks;

static void expect(const char *what, bool ok)
{
    checks++;
    fails += !ok;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static VrSpSurf S(uint64_t a, uint32_t w, uint32_t h, bool color)
{
    return (VrSpSurf){ a, w, h, color };
}

#define FRONT 0x3b58000
#define SCENE 0x3c84000
#define SCENEZ 0x38e0000
#define CUBE0 0x35d7000
#define CUBEZ 0x3b38000
#define LOOKUP 0x37f0000
#define SHADOWZ 0x33d7000

static void pgr2_frame(VrStereoPromo *p)
{
    VrSpSurf front = S(FRONT, 640, 480, true);
    VrSpSurf scene = S(SCENE, 1280, 480, true);
    VrSpSurf scenez = S(SCENEZ, 1280, 480, false);
    VrSpSurf srcs[3];

    srcs[0] = scene;
    srcs[1] = front;
    vr_sp_note_draw(p, &front, NULL, srcs, 2);
    for (int f = 0; f < 6; f++) {
        VrSpSurf face = S(CUBE0 + 0x20000 * f, 256, 256, true);
        VrSpSurf cz = S(CUBEZ, 256, 256, false);
        vr_sp_note_draw(p, &face, &cz, NULL, 0);
    }
    VrSpSurf lookup = S(LOOKUP, 256, 256, true);
    vr_sp_note_draw(p, &lookup, NULL, NULL, 0);
    VrSpSurf shz = S(SHADOWZ, 1024, 1024, false);
    vr_sp_note_draw(p, NULL, &shz, NULL, 0);
    srcs[0] = lookup;
    srcs[1] = shz;
    srcs[2] = S(CUBE0, 256, 256, true);
    for (int i = 0; i < 50; i++) {
        vr_sp_note_draw(p, &scene, &scenez, srcs, 3);
    }
}

#define BB_A 0x22c4000
#define BB_B 0x23f0000
#define BBZ 0x2198000
#define GLOW1 0x212f000
#define GLOW2 0x2127000

static void halo_frame(VrStereoPromo *p, uint64_t drawn)
{
    VrSpSurf bb = S(drawn, 640, 480, true);
    VrSpSurf z = S(BBZ, 640, 480, false);
    VrSpSurf g1 = S(GLOW1, 128, 128, true), g2 = S(GLOW2, 128, 128, true);
    vr_sp_note_draw(p, &g1, NULL, &g2, 1);
    vr_sp_note_draw(p, &g2, NULL, &g1, 1);
    VrSpSurf srcs[2] = { g1, g2 };
    vr_sp_note_draw(p, &bb, &z, srcs, 2);
}

static int count_why(const VrSpChange *c, int n, VrSpWhy why, bool promoted)
{
    int k = 0;
    for (int i = 0; i < n; i++) {
        k += c[i].promoted == promoted && (!promoted || c[i].why == why);
    }
    return k;
}

static void test_pgr2(void)
{
    VrStereoPromo p;
    VrSpChange ch[16];
    uint64_t seeds[1] = { FRONT };

    vr_sp_init(&p, 60);
    pgr2_frame(&p);
    int n = vr_sp_flip(&p, seeds, 1, ch, 16);
    expect("pgr2: three promotions at the first flip", n == 3);
    expect("pgr2: the front buffer, as a seed",
           vr_sp_promoted(&p, FRONT, true) && count_why(ch, n, VR_SP_SEED, true) == 1);
    expect("pgr2: the scene, by sampling (no warm-up)",
           vr_sp_promoted(&p, SCENE, true) && count_why(ch, n, VR_SP_SAMPLED, true) == 1);
    expect("pgr2: the scene's zeta, alongside",
           vr_sp_promoted(&p, SCENEZ, false) && count_why(ch, n, VR_SP_ZETA, true) == 1);
    bool cube = false;
    for (int f = 0; f < 6; f++) {
        cube |= vr_sp_promoted(&p, CUBE0 + 0x20000 * f, true);
    }
    expect("pgr2: no cube face", !cube);
    expect("pgr2: not the cube zeta", !vr_sp_promoted(&p, CUBEZ, false));
    expect("pgr2: not the lookup", !vr_sp_promoted(&p, LOOKUP, true));
    expect("pgr2: not the shadow zeta (as zeta or as colour)",
           !vr_sp_promoted(&p, SHADOWZ, false) && !vr_sp_promoted(&p, SHADOWZ, true));
    expect("pgr2: the scene address is not promoted as a zeta",
           !vr_sp_promoted(&p, SCENE, false));

    int later = 0;
    for (int i = 0; i < 600; i++) {
        pgr2_frame(&p);
        later += vr_sp_flip(&p, seeds, 1, ch, 16);
    }
    expect("pgr2: stable over 600 flips (no transition)", later == 0);
    VrSpEntry list[8];
    expect("pgr2: the list has the three", vr_sp_list(&p, list, 8) == 3);
    expect("pgr2: no observation dropped", p.overflow == 0);
}

static void test_halo(void)
{
    VrStereoPromo p;
    VrSpChange ch[16];
    int n, later = 0;

    vr_sp_init(&p, 60);
    halo_frame(&p, BB_B);
    uint64_t s1[2] = { BB_B, BB_A };
    n = vr_sp_flip(&p, s1, 2, ch, 16);
    expect("halo: both back buffers seeded at the first flip (one presented, not drawn)",
           vr_sp_promoted(&p, BB_A, true) && vr_sp_promoted(&p, BB_B, true));
    expect("halo: their zeta", vr_sp_promoted(&p, BBZ, false));
    expect("halo: three promotions", n == 3);
    expect("halo: not the glow pair",
           !vr_sp_promoted(&p, GLOW1, true) && !vr_sp_promoted(&p, GLOW2, true));
    for (int i = 0; i < 600; i++) {
        uint64_t drawn = (i & 1) ? BB_B : BB_A;
        uint64_t s[2] = { drawn, (i & 1) ? BB_A : BB_B };
        halo_frame(&p, drawn);
        later += vr_sp_flip(&p, s, 2, ch, 16);
    }
    expect("halo: stable over 600 alternating flips", later == 0);
}

static void test_transitive(void)
{
    VrStereoPromo p;
    VrSpChange ch[8];
    VrSpSurf front = S(0x100000, 640, 480, true);
    VrSpSurf post = S(0x200000, 640, 480, true);
    VrSpSurf scene = S(0x300000, 1280, 960, true);
    uint64_t seeds[1] = { front.addr };

    vr_sp_init(&p, 60);
    vr_sp_note_draw(&p, &post, NULL, &scene, 1);
    vr_sp_note_draw(&p, &front, NULL, &post, 1);
    vr_sp_flip(&p, seeds, 1, ch, 8);
    expect("transitive: the post-process target (one hop)", vr_sp_promoted(&p, post.addr, true));
    expect("transitive: the scene behind it (two hops, edge listed first)",
           vr_sp_promoted(&p, scene.addr, true));
}

static void test_cover(void)
{
    VrStereoPromo p;
    VrSpChange ch[8];
    VrSpSurf t = S(0x100000, 640, 480, true);
    VrSpSurf same = S(0x200000, 640, 480, true);
    VrSpSurf narrow = S(0x300000, 639, 960, true);
    VrSpSurf shallow = S(0x400000, 1280, 479, true);
    VrSpSurf zeta = S(0x500000, 1024, 1024, false);
    VrSpSurf srcs[4] = { same, narrow, shallow, zeta };
    uint64_t seeds[1] = { t.addr };

    vr_sp_init(&p, 60);
    vr_sp_note_draw(&p, &t, NULL, srcs, 4);
    vr_sp_flip(&p, seeds, 1, ch, 8);
    expect("cover: an equal-size source covers", vr_sp_promoted(&p, same.addr, true));
    expect("cover: one pixel narrower does not", !vr_sp_promoted(&p, narrow.addr, true));
    expect("cover: one row shorter does not", !vr_sp_promoted(&p, shallow.addr, true));
    expect("cover: a zeta source never qualifies by sampling",
           !vr_sp_promoted(&p, zeta.addr, false) && !vr_sp_promoted(&p, zeta.addr, true));
}

static void test_unqualified_target(void)
{
    VrStereoPromo p;
    VrSpChange ch[8];
    VrSpSurf rtt = S(0x100000, 256, 256, true);
    VrSpSurf big = S(0x200000, 1024, 1024, true);
    VrSpSurf z = S(0x300000, 256, 256, false);
    VrSpSurf shown = S(0x400000, 640, 480, true);
    uint64_t seeds[1] = { shown.addr };

    vr_sp_init(&p, 60);
    vr_sp_note_draw(&p, &rtt, &z, &big, 1);
    vr_sp_note_draw(&p, &shown, NULL, NULL, 0);
    vr_sp_flip(&p, seeds, 1, ch, 8);
    expect("unqualified: a source sampled into an unshown target stays mono",
           !vr_sp_promoted(&p, big.addr, true));
    expect("unqualified: that target's zeta stays mono", !vr_sp_promoted(&p, z.addr, false));
    expect("unqualified: the unshown target itself stays mono", !vr_sp_promoted(&p, rtt.addr, true));
    VrSpSurf zz = S(0x500000, 640, 480, false);
    vr_sp_note_draw(&p, NULL, &zz, &big, 1);
    vr_sp_flip(&p, seeds, 1, ch, 8);
    expect("unqualified: a draw with no colour target promotes nothing",
           !vr_sp_promoted(&p, zz.addr, false) && !vr_sp_promoted(&p, big.addr, true));
}

static void test_decay(void)
{
    VrStereoPromo p;
    VrSpChange ch[8];
    uint64_t seeds[1] = { 0x100000 };
    int n = 0;

    vr_sp_init(&p, 60);
    vr_sp_flip(&p, seeds, 1, ch, 8);
    for (int i = 0; i < 59; i++) {
        n += vr_sp_flip(&p, NULL, 0, ch, 8);
    }
    expect("decay: still promoted after 59 flips without qualifying",
           vr_sp_promoted(&p, 0x100000, true) && n == 0);
    n = vr_sp_flip(&p, NULL, 0, ch, 8);
    expect("decay: demoted at the 60th", !vr_sp_promoted(&p, 0x100000, true));
    expect("decay: one demote transition, named",
           n == 1 && !ch[0].promoted && ch[0].addr == 0x100000 && ch[0].color);

    vr_sp_init(&p, 60);
    vr_sp_flip(&p, seeds, 1, ch, 8);
    for (int i = 0; i < 59; i++) {
        vr_sp_flip(&p, NULL, 0, ch, 8);
    }
    vr_sp_flip(&p, seeds, 1, ch, 8);
    for (int i = 0; i < 59; i++) {
        vr_sp_flip(&p, NULL, 0, ch, 8);
    }
    expect("decay: re-qualifying resets the count", vr_sp_promoted(&p, 0x100000, true));
    n = vr_sp_flip(&p, seeds, 1, ch, 8);
    expect("decay: re-seeding a promoted surface is not a new transition", n == 0);

    vr_sp_init(&p, 5);
    vr_sp_flip(&p, seeds, 1, ch, 8);
    for (int i = 0; i < 5; i++) {
        vr_sp_flip(&p, NULL, 0, ch, 8);
    }
    expect("decay: the configured decay is used (5)", !vr_sp_promoted(&p, 0x100000, true));
    vr_sp_init(&p, 0);
    expect("decay: 0 means the default 60", p.decay == VR_SP_DECAY_DEFAULT);
}

static void test_cleared(void)
{
    VrStereoPromo p;
    VrSpChange ch[8];
    VrSpSurf front = S(0x100000, 640, 480, true);
    VrSpSurf scene = S(0x200000, 1280, 480, true);
    VrSpSurf z = S(0x300000, 1280, 480, false);
    uint64_t seeds[1] = { front.addr };

    VrSpSurf fz = S(0x400000, 640, 480, false);
    vr_sp_init(&p, 60);
    vr_sp_note_draw(&p, &front, &fz, &scene, 1);
    vr_sp_note_draw(&p, &scene, &z, NULL, 0);
    vr_sp_flip(&p, seeds, 1, ch, 8);
    expect("cleared: the seed's zeta promoted", vr_sp_promoted(&p, fz.addr, false));
    expect("cleared: the scene and its zeta promoted",
           vr_sp_promoted(&p, scene.addr, true) && vr_sp_promoted(&p, z.addr, false));
    for (int i = 0; i < 60; i++) {
        vr_sp_note_draw(&p, &front, NULL, NULL, 0);
        vr_sp_flip(&p, seeds, 1, ch, 8);
    }
    expect("cleared: last frame's edge does not keep the scene",
           !vr_sp_promoted(&p, scene.addr, true));
    expect("cleared: last frame's pair does not keep the zeta",
           !vr_sp_promoted(&p, z.addr, false));
    expect("cleared: the seed stays", vr_sp_promoted(&p, front.addr, true));
    expect("cleared: the seed's zeta, no longer bound beside it, is demoted",
           !vr_sp_promoted(&p, fz.addr, false));
}

static void test_kind(void)
{
    VrStereoPromo p;
    VrSpChange ch[8];
    VrSpSurf c = S(0x0, 640, 480, true);
    VrSpSurf z = S(0x0, 640, 480, false);
    uint64_t seeds[1] = { 0x0 };

    vr_sp_init(&p, 60);
    vr_sp_note_draw(&p, &c, &z, NULL, 0);
    int n = vr_sp_flip(&p, seeds, 1, ch, 8);
    expect("kind: one address as colour and as zeta: two entries, two promotions",
           n == 2 && vr_sp_promoted(&p, 0x0, true) && vr_sp_promoted(&p, 0x0, false));
}

static void test_caps(void)
{
    VrStereoPromo p;
    VrSpChange ch[2];
    uint64_t seeds[4] = { 1, 2, 3, 4 };

    vr_sp_init(&p, 60);
    int n = vr_sp_flip(&p, seeds, 4, ch, 2);
    expect("caps: the count of transitions beyond max_out", n == 4);
    expect("caps: the first two written", ch[0].addr == 1 && ch[1].addr == 2);

    vr_sp_init(&p, 60);
    VrSpSurf t = S(0x10, 64, 64, true);
    for (int i = 0; i < 300; i++) {
        VrSpSurf s = S(0x1000 + i, 8, 8, true);
        vr_sp_note_draw(&p, &t, NULL, &s, 1);
    }
    expect("caps: edges past the table are counted, not written",
           p.n_edge == VR_SP_MAX_EDGES && p.overflow == 300 - VR_SP_MAX_EDGES);
    for (int i = 0; i < 300; i++) {
        vr_sp_note_draw(&p, &t, NULL, &(VrSpSurf){ 0x1000, 8, 8, true }, 1);
    }
    expect("caps: a repeated edge is kept once", p.overflow == 300 - VR_SP_MAX_EDGES);

    vr_sp_init(&p, 1000);
    for (uint64_t a = 0; a < VR_SP_MAX_ENTRIES; a++) {
        uint64_t s[1] = { 0x100 + a };
        vr_sp_flip(&p, s, 1, ch, 2);
    }
    uint64_t s65[1] = { 0x9999 };
    vr_sp_flip(&p, s65, 1, ch, 2);
    expect("caps: a full table of promoted entries refuses a new one, counted",
           !vr_sp_promoted(&p, 0x9999, true) && p.overflow == 1);
    for (int i = 0; i < 1000; i++) {
        vr_sp_flip(&p, NULL, 0, ch, 2);
    }
    vr_sp_flip(&p, s65, 1, ch, 2);
    expect("caps: an unpromoted entry's slot is reused", vr_sp_promoted(&p, 0x9999, true));
}

static void test_pairkey(void)
{
    VrStereoPromo p;
    VrSpChange ch[8];
    VrSpSurf rt = { 0x100000, 640, 480, true };
    VrSpSurf scn = { 0x200000, 640, 480, true };
    VrSpSurf z = { 0x300000, 640, 480, false };
    VrSpSurf z2 = { 0x400000, 640, 480, false };
    uint64_t seed = scn.addr;

    vr_sp_init(&p, 60);
    vr_sp_note_draw(&p, &rt, &z, NULL, 0);
    vr_sp_note_draw(&p, &scn, &z, NULL, 0);
    vr_sp_flip(&p, &seed, 1, ch, 8);
    expect("pairkey: a zeta shared with an unqualified target drawn first is "
           "promoted beside the seeded scene; the unqualified target is not",
           vr_sp_promoted(&p, z.addr, false) &&
           vr_sp_promoted(&p, scn.addr, true) &&
           !vr_sp_promoted(&p, rt.addr, true));
    vr_sp_init(&p, 60);
    vr_sp_note_draw(&p, &scn, &z, NULL, 0);
    vr_sp_note_draw(&p, &scn, &z2, NULL, 0);
    vr_sp_flip(&p, &seed, 1, ch, 8);
    expect("pairkey: both zetas of one qualified colour target are promoted",
           vr_sp_promoted(&p, z.addr, false) &&
           vr_sp_promoted(&p, z2.addr, false));
}

static void test_seeds(void)
{
    uint64_t s[2] = { 7, 7 };

    expect("seeds: flip key and a different presented surface: both",
           vr_sp_seeds(0x3b58000, 0x22c4000, s) == 2 && s[0] == 0x3b58000 &&
           s[1] == 0x22c4000);
    expect("seeds: the same surface flipped and presented: once",
           vr_sp_seeds(0x3b58000, 0x3b58000, s) == 1 && s[0] == 0x3b58000);
    s[0] = s[1] = 7;
    expect("seeds: a zero flip key seeds nothing; the presented surface still "
           "does", vr_sp_seeds(0, 0x3b58000, s) == 1 && s[0] == 0x3b58000);
    expect("seeds: neither known: none", vr_sp_seeds(0, 0, s) == 0);
}

static int fold_seen;
static void fold_cb(uint64_t a, uint64_t b)
{
    (void)a;
    (void)b;
    fold_seen++;
}

static void test_fold(void)
{
    uint8_t views[32] = { 0 };
    uint64_t r[7] = { 5, 40, 42, 83, 0, 9, 13 };
    views[10] = 1;
    views[11] = 2;
    views[13] = 2;
    views[15] = 1;
    views[16] = 2;
    fold_seen = 0;
    vr_sp_fold_pairs(views, 10, r, 7, fold_cb);
    expect("fold: a mono slot is left as it is", r[0] == 5 && r[5] == 9);
    expect("fold: per-view counts (40, 42): the rounded mean 41, the second "
           "zeroed", r[1] == 41 && r[2] == 0);
    expect("fold: the whole count in the first slot (83, 0): 42 (one view)",
           r[3] == 42 && r[4] == 0);
    expect("fold: a pair cut off at the end of the range is not folded",
           r[6] == 13);
    expect("fold: the callback sees each raw pair once", fold_seen == 2);
    uint64_t q[2] = { 4, 6 };
    vr_sp_fold_pairs(NULL, 0, q, 2, fold_cb);
    expect("fold: no view table (stereo off): nothing changes",
           q[0] == 4 && q[1] == 6);
}

int main(void)
{
    test_pgr2();
    test_halo();
    test_transitive();
    test_cover();
    test_unqualified_target();
    test_decay();
    test_cleared();
    test_kind();
    test_caps();
    test_pairkey();
    test_seeds();
    test_fold();
    printf("stereopromo: %d of %d checks pass\n", checks - fails, checks);
    return fails ? 1 : 0;
}
