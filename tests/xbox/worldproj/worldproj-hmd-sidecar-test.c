/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/nv2a_int.h"
#include "../../../hw/xbox/nv2a/pgraph/vr_worldproj.h"
#include "pins.h"
#include <float.h>

static PGRAPHState pg;
static float copy[NV2A_VERTEXSHADER_CONSTANTS][4];
static const char *ctl_path;
static int fails, checks;

enum { SA = 0x3c0000, SB = 0x4c0000 };

static void ctl(const char *line)
{
    FILE *f = fopen(ctl_path, "w");
    fprintf(f, "%s\n", line);
    fclose(f);
}

static void expect(const char *what, bool ok)
{
    checks++;
    fails += !ok;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static int src_n, src_calls, src_fresh;
static bool src_ok = true;
static int src_bad;
static const double TAN[4] = { -1.30, 1.39, -1.19, 0.97 };

static void head(int n, double q[4], double p[3])
{
    double h = -3.0 * n * 3.14159265358979323846 / 180.0 / 2.0;
    q[0] = 0;
    q[1] = sin(h);
    q[2] = 0;
    q[3] = cos(h);
    p[0] = 0.01 * n;
    p[1] = 1.6;
    p[2] = 0;
}

static bool source(bool fresh, VrWpHmdPose *out)
{
    src_calls++;
    src_fresh += fresh;
    if (!src_ok) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->display_time = 1000 + src_n;
    head(src_n, out->q, out->p);
    out->ref_q[3] = 1;
    out->ref_p[1] = 1.6;
    memcpy(out->tan, TAN, sizeof(TAN));
    switch (src_bad) {
    case 1: out->q[1] = NAN; break;
    case 2: out->tan[3] = out->tan[2]; break;
    case 3: out->p[2] = NAN; break;
    case 4: out->ref_q[0] = NAN; break;
    case 5: out->ref_p[0] = NAN; break;
    case 6: out->tan[1] = out->tan[0]; break;
    case 7: out->tan[1] = INFINITY; break;
    }
    return true;
}

static void flip(uint64_t key, int next_pose)
{
    src_n = next_pose;
    memset(pg.vsh_constants_dirty, 0, sizeof(pg.vsh_constants_dirty));
    vr_worldproj_flip(&pg, key);
}

static void draw(void)
{
    memcpy(copy, pg.vsh_constants, sizeof(copy));
    vr_worldproj_apply(false, copy);
}

static void upload(const float m[4][4])
{
    memcpy(pg.vsh_constants[0], m, 64);
}

static double want_upm = 1.0 / 3.048;
static bool drawn_with(const float mg[4][4], int n)
{
    VrWpDecomp d;
    VrWpView v;
    float w[4][4];
    double q[4], p[3];
    static const double rq[4] = { 0, 0, 0, 1 }, rp[3] = { 0, 1.6, 0 };

    head(n, q, p);
    vr_wp_view_from_xr(rq, rp, q, p, want_upm, &v);
    v.fov = VR_WP_FOV_TANGENTS;
    v.tan_l = TAN[0];
    v.tan_r = TAN[1];
    v.tan_d = TAN[2];
    v.tan_u = TAN[3];
    if (vr_wp_decompose(mg, &d) != VR_WP_OK || !vr_wp_rewrite(&d, &v, true, w)) {
        return false;
    }
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            double big = fmax(fmax(fabs(w[i][0]), fabs(w[i][1])),
                              fmax(fabs(w[i][2]), fabs(w[i][3])));
            if (!(fabs(copy[i][j] - w[i][j]) <= 2.0 * FLT_EPSILON * big)) {
                return false;
            }
        }
    }
    return true;
}

static int shown(uint64_t key, VrWpStamp *s, VrWpStampSource *src)
{
    vr_worldproj_present(key);
    *src = vr_worldproj_presented(s);
    return *src == VR_WP_STAMP_NONE ? -1 : (int)lround(s->xr_p[0] * 100.0);
}

static void camera(float m[4][4], double a, double b, double near)
{
    const double f = 1000.0, r = f / (f - near), t = -near * f / (f - near);
    float k[4][4] = { { (float)a, 0, 0, 0 }, { 0, (float)b, 0, 0 },
                      { 0, 0, (float)r, (float)t }, { 0, 0, 1, 0 } };
    memcpy(m, k, sizeof(k));
}

static void palette(int slot, int n)
{
    for (int i = 0; i < 64 && slot + 3 * i + 2 < NV2A_VERTEXSHADER_CONSTANTS; i++) {
        double ang = 0.1 * i, sc = i < n ? 1.0 : 1.15;
        float b[3][4] = { { (float)(sc * cos(ang)), (float)(-sc * sin(ang)), 0, 0.1f },
                          { (float)(sc * sin(ang)), (float)(sc * cos(ang)), 0, 0.2f },
                          { 0, 0, (float)(i < n ? 1.0 : 1.15), 0.3f } };
        if (i > n) {
            memset(b, 0, sizeof(b));
        }
        memcpy(pg.vsh_constants[slot + 3 * i], b, sizeof(b));
    }
}

static const Pin *find(const Pin *pins, int n, const char *name)
{
    for (int i = 0; i < n; i++) {
        if (!strcmp(pins[i].name, name)) {
            return &pins[i];
        }
    }
    printf("FAIL no pin named %s\n", name);
    exit(1);
}

int main(int argc, char **argv)
{
    static Pin pins[1024];
    VrWpStamp s;
    VrWpStampSource src;
    bool ok;

    ctl_path = getenv("XEMU_VR_WORLDPROJ");
    if (argc < 2 || !ctl_path) {
        printf("usage: XEMU_VR_WORLDPROJ=<scratch file> %s PINS.txt\n",
               argv[0]);
        return 2;
    }
    int n = pins_load(argv[1], pins, 1024);
    if (n <= 0) {
        printf("FAIL no pins in %s\n", argv[1]);
        return 1;
    }
    const Pin *cam = find(pins, n, "syn1.yaw+5");
    const Pin *scope = find(pins, n, "syn2.yaw+5");

    vr_worldproj_set_pose_source(source);

    ctl("hmd fov tan -1 1 -1 1 cyup 1");
    flip(SA, 1);
    expect("parse: hmd refuses fov tan (drawn must equal declared): off, and "
           "no pose asked for", src_calls == 0);
    ctl("hmd shear 0.1 cyup 1");
    flip(SA, 1);
    expect("parse: hmd refuses a shear", src_calls == 0);

    ctl("hmd cyup 1");
    upload(cam->mg);
    flip(SB, 1);
    expect("fresh: the first hmd flip asks for a re-anchored pose",
           src_calls == 1 && src_fresh == 1);
    draw();
    expect("draw: frame 1's draw uses pose 1, converted, headset frustum",
           drawn_with(cam->mg, 1));

    flip(SA, 2);
    expect("fresh: not again while hmd stays on", src_calls == 2 &&
                                                   src_fresh == 1);
    ok = shown(SA, &s, &src) == 1 && src == VR_WP_STAMP_COPIED &&
         s.declarable && s.reprojected;
    expect("present: SA declares pose 1 (drawn with it), not the latched "
           "pose 2; re-projected", ok);
    ok = fabs(s.tan[0] - TAN[0]) < 1e-15 && fabs(s.tan[3] - TAN[3]) < 1e-15 &&
         s.display_time == 1001;
    expect("present: the declared frustum and time are the ones located",
           ok);
    ok = s.view.fov == VR_WP_FOV_TANGENTS && s.view.tan_l == TAN[0] &&
         s.view.tan_r == TAN[1] && s.view.tan_d == TAN[2] &&
         s.view.tan_u == TAN[3];
    expect("present: the stamp's view drew with the declared frustum", ok);
    {
        double q[4], p[3];
        head(1, q, p);
        ok = true;
        for (int i = 0; i < 4; i++) {
            ok &= s.xr_q[i] == q[i];
        }
        for (int i = 0; i < 3; i++) {
            ok &= s.xr_p[i] == p[i];
        }
    }
    expect("present: the declared orientation and position are pose 1's, "
           "as the runtime gave them", ok);

    draw();
    expect("draw: frame 2 uses pose 2", drawn_with(cam->mg, 2));
    flip(SB, 3);
    ok = shown(SB, &s, &src) == 2 && src == VR_WP_STAMP_COPIED;
    ok &= shown(SA, &s, &src) == 1 && src == VR_WP_STAMP_COPIED;
    expect("present: each surface declares its own frame's pose (SB 2, SA "
           "1)", ok);

    upload(scope->mg);
    draw();
    flip(SA, 4);
    ok = shown(SA, &s, &src) == 3 && src == VR_WP_STAMP_COPIED &&
         !s.reprojected;
    expect("scoped: a frame whose camera is scoped is declared not "
           "re-projected (the screen quad)", ok);

    upload(cam->mg);
    draw();
    upload(scope->mg);
    draw();
    flip(SB, 4);
    ok = shown(SB, &s, &src) == 4 && src == VR_WP_STAMP_COPIED &&
         !s.reprojected;
    expect("scoped: one scoped camera in a frame makes it not re-projected",
           ok);
    upload(cam->mg);
    draw();
    flip(SA, 4);
    ok = shown(SA, &s, &src) == 4 && s.reprojected;
    expect("scoped: the next frame, world only, is re-projected again", ok);

    {
        const uint64_t SHADOW = 0x100000;
        vr_worldproj_draw_target(SHADOW, true);
        upload(scope->mg);
        draw();
        vr_worldproj_draw_target(SB, true);
        upload(cam->mg);
        draw();
        flip(SB, 4);
        ok = shown(SB, &s, &src) == 4 && s.reprojected;
        expect("scoped: a scoped camera drawn into another surface (a shadow "
               "map) leaves the presented frame re-projected", ok);
        vr_worldproj_draw_target(SA, true);
        upload(cam->mg);
        draw();
        upload(scope->mg);
        draw();
        flip(SA, 4);
        ok = shown(SA, &s, &src) == 4 && !s.reprojected;
        expect("scoped: (control) a scoped camera drawn into the presented "
               "surface still makes it not re-projected", ok);
        vr_worldproj_draw_target(0, true);
        upload(cam->mg);
        draw();
        upload(scope->mg);
        draw();
        flip(SB, 4);
        ok = shown(SB, &s, &src) == 4 && !s.reprojected;
        expect("scoped: (control) a scoped draw on an unknown surface still "
               "makes the frame not re-projected", ok);
        vr_worldproj_draw_target(0, false);
        upload(scope->mg);
        draw();
        vr_worldproj_draw_target(SB, true);
        upload(cam->mg);
        draw();
        flip(SB, 4);
        ok = shown(SB, &s, &src) == 4 && s.reprojected;
        expect("scoped: a scoped draw that writes no colour (key 0, a "
               "depth-only shadow pass) leaves the frame re-projected", ok);
        vr_worldproj_draw_target(0, true);
        upload(cam->mg);
    }

    ok = true;
    for (int k = 0; k < 3; k++) {
        draw();
        flip(k % 2 ? SA : SB, 4);
        ok &= shown(k % 2 ? SA : SB, &s, &src) == 4 && s.reprojected &&
              src == VR_WP_STAMP_COPIED;
    }
    expect("still: a head held still stays re-projected (cache hits count)",
           ok);

    upload(cam->mg);
    flip(SB, 5);
    ok = shown(SB, &s, &src) == 4 && src == VR_WP_STAMP_LAST;
    expect("no draw: the surface's old stamp is forgotten; the last "
           "declared one is re-declared", ok);

    src_ok = false;
    draw();
    flip(SA, 6);
    ok = shown(SA, &s, &src) == 5 && s.reprojected;
    expect("lost: frame 5 was drawn with pose 5", ok);
    memcpy(copy, pg.vsh_constants, sizeof(copy));
    draw();
    ok = !memcmp(copy, pg.vsh_constants, sizeof(copy));
    expect("lost: with no pose the draw keeps the game's rows", ok);
    flip(SB, 7);
    ok = shown(SB, &s, &src) == -1 || !s.declarable;
    expect("lost: the frame drawn with no pose is not declarable", ok &&
                                                                   src == VR_WP_STAMP_COPIED);
    src_ok = true;
    int fresh_before = src_fresh;
    flip(SA, 8);
    expect("fresh: hmd coming back after a lost pose re-anchors",
           src_fresh == fresh_before + 1);

    draw();
    ctl("off");
    flip(SB, 9);
    draw();
    flip(SA, 10);
    ok = shown(SA, &s, &src) != -1 && src == VR_WP_STAMP_COPIED &&
         !s.reprojected && !s.declarable;
    expect("off: a frame drawn with the rewrite off is declared flat", ok);

    ctl("pose 5 0 0 0 0 0 fov game cyup 1");
    flip(SB, 11);
    draw();
    flip(SA, 12);
    ok = shown(SA, &s, &src) != -1 && !s.declarable;
    expect("pose mode: a script's pose is drawn but never declared to a "
           "headset", ok);

    for (src_bad = 1; src_bad <= 7; src_bad++) {
        ctl("hmd cyup 1");
        upload(cam->mg);
        flip(SB, 20);
        memcpy(copy, pg.vsh_constants, sizeof(copy));
        draw();
        ok = !memcmp(copy, pg.vsh_constants, sizeof(copy));
        flip(SA, 21);
        ok &= shown(SA, &s, &src) != -1 && !s.declarable;
        static const char *const bad[] = {
            "", "a NaN orientation", "a frustum collapsed up-down",
            "a NaN position", "a NaN reference orientation",
            "a NaN reference position", "a frustum collapsed left-right",
            "an infinite frustum" };
        char what[120];
        snprintf(what, sizeof(what), "bad pose: %s draws flat", bad[src_bad]);
        expect(what, ok);
    }
    src_bad = 0;

    ctl("hmd upm 0 cyup 1");
    upload(cam->mg);
    flip(SB, 30);
    memcpy(copy, pg.vsh_constants, sizeof(copy));
    draw();
    expect("parse: upm 0 is refused (off: the game's rows)",
           !memcmp(copy, pg.vsh_constants, sizeof(copy)));
    ctl("hmd upm 5000 cyup 1");
    flip(SA, 30);
    memcpy(copy, pg.vsh_constants, sizeof(copy));
    draw();
    expect("parse: upm 5000 is refused (off: the game's rows)",
           !memcmp(copy, pg.vsh_constants, sizeof(copy)));
    ctl("hmd upm 1 cyup 1");
    flip(SA, 30);
    flip(SB, 31);
    draw();
    want_upm = 1.0;
    expect("upm: 'upm 1' scales the head's position one to one",
           drawn_with(cam->mg, 31));
    want_upm = 1.0 / 3.048;
    ctl("hmd cyup 1");
    flip(SA, 32);
    draw();
    expect("upm: without the option, 1/3.048 (Halo's 10 ft unit)",
           drawn_with(cam->mg, 32));

    {
        int c0 = src_calls;
        ctl("hmd nearfov 0.04 cyup 1");
        upload(cam->mg);
        flip(SB, 40);
        memcpy(copy, pg.vsh_constants, sizeof(copy));
        draw();
        expect("parse: hmd refuses nearfov (off, no pose asked for)",
               src_calls == c0 &&
                   !memcmp(copy, pg.vsh_constants, sizeof(copy)));
    }

    ctl("hmd cyup 1");
    flip(SA, 41);
    draw();
    flip(SA, 42);
    ok = shown(SA, &s, &src) == 41 && s.reprojected;
    for (int k = 0; k < 2; k++) {
        draw();
        flip(SB, 43 + k);
    }
    ok &= shown(SA, &s, &src) == 41;
    draw();
    flip(SB, 46);
    ok &= shown(SA, &s, &src) == -1 && src == VR_WP_STAMP_NONE;
    expect("stale: a surface's stamp older than 3 flips is shown flat, not "
           "declared", ok);
    ok = shown(SB, &s, &src) != -1 && s.reprojected;
    expect("stale: the surface drawn now is still declared", ok);

    draw();
    flip(SA, 47);
    ok = shown(SA, &s, &src) != -1 && src == VR_WP_STAMP_COPIED;
    vr_worldproj_invalidate();
    ok &= shown(SA, &s, &src) == -1 && src == VR_WP_STAMP_NONE;
    expect("invalidate: the next present declares nothing, not the old "
           "stamp", ok);
    ok = shown(SB, &s, &src) == -1 && src == VR_WP_STAMP_NONE;
    expect("invalidate: no fallback to what was declared before", ok);
    draw();
    flip(SA, 48);
    draw();
    flip(SA, 49);
    ok = shown(SA, &s, &src) == 48 && src == VR_WP_STAMP_COPIED;
    expect("invalidate: stamping resumes with the next whole frame", ok);
    vr_worldproj_invalidate();
    draw();
    flip(SB, 50);
    ok = vr_worldproj_presented(&s) == VR_WP_STAMP_NONE && s.seq == 0;
    ok &= shown(SA, &s, &src) == -1;
    expect("invalidate: taken at the flip as well as at a present (and what "
           "was presented is forgotten at once)", ok);

    {
        float nearcam[4][4], worldcam[4][4];
        camera(nearcam, 1.42815, -1.90420, 0.0234);
        camera(worldcam, 1.42815, -1.90420, 0.0625);
        memset(pg.vsh_constants[60], 0, sizeof(pg.vsh_constants[60]) * 132);
        ctl("pose 0 0 0 0 0 0 arms 8 cyup 1");
        flip(SA, 60);
        upload(nearcam);
        palette(60, 1);
        expect("arms: the near pass with 1 bone (the pistol) is drawn",
               !vr_worldproj_end_skips(&pg, true));
        palette(60, 37);
        expect("arms: the near pass with 37 bones (the arms) is dropped",
               vr_worldproj_end_skips(&pg, true));
        palette(60, 8);
        ok = vr_worldproj_end_skips(&pg, true);
        palette(60, 7);
        ok &= !vr_worldproj_end_skips(&pg, true);
        expect("arms: the threshold: 8 bones dropped, 7 kept (arms 8)", ok);
        palette(60, 37);
        expect("arms: a fixed-function draw is never dropped",
               !vr_worldproj_end_skips(&pg, false));
        upload(worldcam);
        expect("arms: a WORLD draw with 37 bones (near 0.0625) is drawn",
               !vr_worldproj_end_skips(&pg, true));
        upload(nearcam);
        ctl("pose 0 0 0 0 0 0 arms 8 armsnear 0.01 cyup 1");
        flip(SB, 61);
        expect("arms: armsnear 0.01: a near plane of 0.0234 is not the near "
               "pass", !vr_worldproj_end_skips(&pg, true));
        ctl("pose 0 0 0 0 0 0 arms 8 palette 90 cyup 1");
        flip(SA, 62);
        palette(60, 37);
        palette(90, 1);
        ok = !vr_worldproj_end_skips(&pg, true);
        palette(90, 37);
        ok &= vr_worldproj_end_skips(&pg, true);
        expect("arms: palette 90 reads the bones at 90, not 60", ok);
        ctl("pose 0 0 0 0 0 0 cyup 1");
        flip(SB, 63);
        expect("arms: without the option nothing is dropped",
               !vr_worldproj_end_skips(&pg, true));
        ctl("off");
        flip(SA, 64);
        palette(60, 37);
        ctl("pose 0 0 0 0 0 0 arms 8 cyup 1");
        ctl("off");
        flip(SB, 65);
        expect("arms: off drops nothing", !vr_worldproj_end_skips(&pg, true));
        src_ok = false;
        ctl("hmd arms 8 cyup 1");
        flip(SA, 66);
        upload(nearcam);
        palette(60, 37);
        expect("arms: hmd with no pose (drawn flat) drops nothing",
               !vr_worldproj_end_skips(&pg, true));
        src_ok = true;
        memset(pg.vsh_constants[60], 0, sizeof(pg.vsh_constants[60]) * 132);
    }

    {
        float cam2[4][4];
        VrWpDecomp d;
        double gt[4];
        ctl("hmd fov tan -1 1 -1 1 cyup 1");
        int c0 = src_calls;
        flip(SA, 70);
        expect("parse: hmd refuses fov tan (only fov game)", src_calls == c0);
        ctl("hmd fov game cyup 1");
        upload(cam->mg);
        flip(SB, 71);
        expect("parse: hmd fov game asks for a pose", src_calls == c0 + 1);
        draw();
        flip(SA, 72);
        vr_wp_decompose(cam->mg, &d);
        vr_wp_game_tangents(&d, true, gt);
        ok = shown(SA, &s, &src) == 71 && s.reprojected && s.declarable;
        for (int i = 0; i < 4; i++) {
            ok &= fabs(s.tan[i] - gt[i]) < 1e-12;
        }
        expect("fov game: the declared frustum is the camera's own game "
               "frustum, not the headset's", ok);
        ok = s.view.fov == VR_WP_FOV_GAME;
        expect("fov game: the frame was drawn with the game frustum", ok);
        draw();
        flip(SB, 72);
        ok = shown(SB, &s, &src) == 72 && s.reprojected;
        for (int i = 0; i < 4; i++) {
            ok &= fabs(s.tan[i] - gt[i]) < 1e-12;
        }
        expect("fov game: a frame of cache hits is declared too", ok);
        draw();
        camera(cam2, 1.6, -2.1, 0.0625);
        upload(cam2);
        draw();
        upload(cam->mg);
        flip(SA, 73);
        ok = shown(SA, &s, &src) == 72 && !s.reprojected;
        expect("fov game: a frame whose cameras disagree on the frustum is "
               "shown flat", ok);
        draw();
        flip(SB, 74);
        ok = shown(SB, &s, &src) == 73 && s.reprojected;
        expect("fov game: the next single-frustum frame is declared again",
               ok);
    }

    vr_worldproj_set_pose_source(NULL);
    ctl("hmd cyup 1");
    int calls = src_calls;
    flip(SB, 13);
    draw();
    flip(SA, 14);
    ok = src_calls == calls && shown(SA, &s, &src) != -1 && !s.declarable;
    expect("no source registered: hmd draws flat", ok);

    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
