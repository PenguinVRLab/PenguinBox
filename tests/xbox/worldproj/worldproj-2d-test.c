/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/nv2a_int.h"
#include "../../../hw/xbox/nv2a/pgraph/vr_worldproj.h"
#include "fake_host.h"
#include <float.h>
#include <unistd.h>

enum { O = 28 };

static PGRAPHState pg;
static float copy[NV2A_VERTEXSHADER_CONSTANTS][4];
static const char *ctl_path;
static int fails, checks;
static FILE *log_file;
static long log_mark;

static float world[4][4], weapon[4][4], scoped_cam[4][4];
static float hud[4][4], depth2d[4][4], skinned[4][4];

static void expect(const char *what, bool ok)
{
    checks++;
    fails += !ok;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static void ctl(const char *line)
{
    FILE *f = fopen(ctl_path, "w");
    fprintf(f, "%s\n", line);
    fclose(f);
}

static bool log_has(const char *needle)
{
    static char buf[1 << 16];
    size_t n;

    fflush(stderr);
    fseek(log_file, log_mark, SEEK_SET);
    n = fread(buf, 1, sizeof(buf) - 1, log_file);
    buf[n] = 0;
    return strstr(buf, needle) != NULL;
}

static void mark(void)
{
    fflush(stderr);
    fseek(log_file, 0, SEEK_END);
    log_mark = ftell(log_file);
}

static void upload(int slot, const float m[4][4])
{
    memcpy(pg.vsh_constants[slot], m, 64);
}

static void flip(void)
{
    memset(pg.vsh_constants_dirty, 0, sizeof(pg.vsh_constants_dirty));
    vr_worldproj_flip(&pg, 0x3c0000);
}

static void draw(void)
{
    memcpy(copy, pg.vsh_constants, sizeof(copy));
    vr_worldproj_apply(false, copy);
    checks++;
    fails += fake_rows_outside(pg.vsh_constants, copy) != 0;
}

static void end_draw(void)
{
    vr_worldproj_draw_extent(&pg, NULL, 0);
    draw();
}

static void end_draw_quad(float x0, float y0, float x1, float y1)
{
    const float v[16] = { x0, y0, 0, 1, x1, y0, 0, 1, x1, y1, 0, 1, x0, y1, 0, 1 };
    vr_worldproj_draw_extent(&pg, v, 4);
    draw();
}

static bool dirty2d(void)
{
    return pg.vsh_constants_dirty[O] && pg.vsh_constants_dirty[O + 1] &&
           pg.vsh_constants_dirty[O + 2] && pg.vsh_constants_dirty[O + 3];
}

static void camera(float m[4][4], double a, double b, double near,
                   double yaw)
{
    const double f = 1024.0, r = f / (f - near), t = -near * f / (f - near);
    double y = yaw * 3.14159265358979323846 / 180.0;
    double R[3][4] = { { cos(y), 0, sin(y), 3.0 },
                       { 0, 1, 0, -0.6 },
                       { -sin(y), 0, cos(y), 12.0 } };
    for (int j = 0; j < 4; j++) {
        m[0][j] = (float)(a * R[0][j]);
        m[1][j] = (float)(b * R[1][j]);
        m[2][j] = (float)(r * R[2][j] + (j == 3 ? t : 0));
        m[3][j] = (float)R[2][j];
    }
}

static void rows2d(float m[4][4], bool flat)
{
    const float r[4][4] = { { 1 / 320.0f, 0, 0, -1.0016f },
                            { 0, -1 / 240.0f, 0, 1.0021f },
                            { 0, 0, flat ? 0.0f : 1.0f, flat ? 0.5f : 0.0f },
                            { 0, 0, 0, 1 } };
    memcpy(m, r, sizeof(r));
}

static bool handed(const float cam[4][4], const float rows[4][4], double yaw,
                   const double tan4[4], double hud_m, double scale)
{
    VrWpDecomp d;
    VrWpView v;
    float want[4][4];

    vr_wp_view_identity(&v);
    vr_wp_rot_ypr(yaw, 0, 0, v.rot);
    if (tan4) {
        v.fov = VR_WP_FOV_TANGENTS;
        v.tan_l = tan4[0];
        v.tan_r = tan4[1];
        v.tan_d = tan4[2];
        v.tan_u = tan4[3];
    }
    if (vr_wp_decompose(cam, &d) != VR_WP_OK ||
        !vr_wp_rewrite_2d(&d, &v, true, rows, vr_wp_2d_class(rows),
                          hud_m / 3.048 * d.s, scale, want)) {
        return false;
    }
    return !memcmp(copy[O], want, sizeof(want));
}

static bool untouched(const float rows[4][4])
{
    return !memcmp(copy[O], rows, 64);
}

static void frame_with(const float rows[4][4], int world_draws,
                       int weapon_draws)
{
    upload(0, world);
    for (int i = 0; i < world_draws; i++) {
        end_draw();
    }
    upload(0, weapon);
    for (int i = 0; i < weapon_draws; i++) {
        end_draw();
    }
    upload(O, rows);
    end_draw();
}

int main(void)
{
    static const double TAN[4] = { -1.3764, 1.3764, -1.4281, 0.9657 };
    const float vpscl[4] = { 320.0f, -240.0f, 1.0f, 0.0f };

    ctl_path = getenv("XEMU_VR_WORLDPROJ");
    if (!ctl_path) {
        printf("usage: XEMU_VR_WORLDPROJ=<scratch file> worldproj-2d-test\n");
        return 2;
    }
    log_file = tmpfile();
    if (!log_file || dup2(fileno(log_file), 2) < 0) {
        printf("FAIL cannot capture the log\n");
        return 1;
    }
    setvbuf(stderr, NULL, _IONBF, 0);

    camera(world, 1.42815, -1.90420, 0.0625, 30);
    camera(weapon, 1.42815, -1.90420, 0.0234, 30);
    camera(scoped_cam, 3.17159, -4.22879, 0.0625, 30);
    rows2d(hud, true);
    rows2d(depth2d, false);
    {
        const float s[4][4] = { { 2.59577f, 0, -0.43264f, 53.97818f },
                                { 0.02982f, -2.62532f, 0.17891f, 10.78954f },
                                { 0, 0, 0, 0.5f },
                                { 0, 0, 0, 1 } };
        memcpy(skinned, s, sizeof(s));
    }
    memcpy(pg.vsh_constants[58], vpscl, sizeof(vpscl));

    ctl("pose 20 0 0 0 0 0 fov tan -1.3764 1.3764 -1.4281 0.9657");
    flip();
    frame_with(hud, 10, 2);
    expect("off by default: without `ortho` the HUD rows are the game's",
           untouched(hud));

    ctl("pose 20 0 0 0 0 0 fov tan -1.3764 1.3764 -1.4281 0.9657 ortho 28");
    flip();
    frame_with(hud, 10, 2);
    expect("ortho: the HUD draw gets the plane rows (2 m, 0.8) of the MAIN "
           "camera", handed(world, hud, 20, TAN, 2.0, 0.8));
    frame_with(depth2d, 0, 0);
    expect("ortho: a real-depth draw (a glow, a query) gets the world "
           "identity's rows", handed(world, depth2d, 20, TAN, 2.0, 0.8));

    flip();
    frame_with(hud, 2, 10);
    expect("main camera: 10 weapon-pass draws against 2 world draws -> the "
           "weapon camera is the main one this frame",
           handed(weapon, hud, 20, TAN, 2.0, 0.8));
    flip();
    frame_with(hud, 2, 30);
    flip();
    frame_with(hud, 10, 2);
    expect("main camera: chosen per frame (a frame of 30 weapon draws, then "
           "one of 10 world draws: the world camera)",
           handed(world, hud, 20, TAN, 2.0, 0.8));

    flip();
    {
        const float none[4][4] = { { 0 } };
        upload(0, none);
    }
    upload(O, hud);
    end_draw();
    expect("gate: a 2D draw before any camera of the frame was re-projected "
           "is left alone", untouched(hud));
    flip();
    upload(0, scoped_cam);
    for (int i = 0; i < 5; i++) {
        end_draw();
    }
    upload(O, hud);
    end_draw();
    expect("gate: on a scoped frame the HUD is left alone (the frame is "
           "shown flat)", untouched(hud));
    flip();
    upload(0, world);
    for (int i = 0; i < 10; i++) {
        end_draw();
    }
    upload(0, scoped_cam);
    end_draw();
    upload(O, hud);
    end_draw();
    expect("gate: a frame with a re-projected camera AND a scoped one is "
           "shown flat: its HUD is left alone", untouched(hud));

    flip();
    frame_with(skinned, 10, 2);
    expect("Halo's skinned-ortho rows (a rotated 3D camera) are left alone",
           untouched(skinned));

    flip();
    frame_with(depth2d, 10, 0);
    upload(O, hud);
    end_draw_quad(0, 0, 640, 480);
    expect("fullscreen keep: a flat quad over the whole screen is left alone",
           untouched(hud));
    end_draw_quad(300, 220, 340, 260);
    expect("... and the next, smaller quad is on the plane",
           handed(world, hud, 20, TAN, 2.0, 0.8));
    end_draw_quad(0, 200, 640, 260);
    expect("... and a full-width bar that is not full-height is on the plane",
           handed(world, hud, 20, TAN, 2.0, 0.8));
    ctl("pose 20 0 0 0 0 0 fov tan -1.3764 1.3764 -1.4281 0.9657 ortho 28 "
        "fullscreen plane");
    flip();
    frame_with(depth2d, 10, 0);
    upload(O, hud);
    end_draw_quad(0, 0, 640, 480);
    expect("fullscreen plane: the whole-screen quad goes on the plane too",
           handed(world, hud, 20, TAN, 2.0, 0.8));

    ctl("pose 20 0 0 0 0 0 fov tan -1.3764 1.3764 -1.4281 0.9657 ortho 28 "
        "hud 3.5 hudscale 0.6 upm 0.5");
    flip();
    frame_with(hud, 10, 0);
    {
        VrWpDecomp d;
        VrWpView v;
        float want[4][4];
        vr_wp_view_identity(&v);
        vr_wp_rot_ypr(20, 0, 0, v.rot);
        v.fov = VR_WP_FOV_TANGENTS;
        v.tan_l = TAN[0];
        v.tan_r = TAN[1];
        v.tan_d = TAN[2];
        v.tan_u = TAN[3];
        vr_wp_decompose(world, &d);
        vr_wp_rewrite_2d(&d, &v, true, hud, VR_WP_2D_FLAT, 3.5 * 0.5 * d.s,
                         0.6, want);
        expect("hud 3.5, hudscale 0.6, upm 0.5: the plane at 3.5 x 0.5 x s, "
               "scaled 0.6", !memcmp(copy[O], want, sizeof(want)));
    }

    ctl("pose 20 0 0 0 0 0 fov tan -1.3764 1.3764 -1.4281 0.9657 ortho 28 "
        "hudlock head");
    flip();
    frame_with(hud, 10, 0);
    expect("hudlock head: the HUD gets the rows of the head at REST (the "
           "headset frustum, scale 0.8), not of the turned head",
           handed(world, hud, 0, TAN, 2.0, 0.8) &&
           !handed(world, hud, 20, TAN, 2.0, 0.8));
    frame_with(depth2d, 0, 0);
    expect("hudlock head: the glows and queries still follow the turned head",
           handed(world, depth2d, 20, TAN, 2.0, 0.8));
    ctl("pose 20 0 0 0.3 0.1 0 fov tan -1.3764 1.3764 -1.4281 0.9657 "
        "ortho 28 hudlock head");
    flip();
    frame_with(hud, 10, 0);
    expect("hudlock head: a displaced head too -- the HUD keeps the rows of "
           "the head at rest (no position either)",
           handed(world, hud, 0, TAN, 2.0, 0.8));
    ctl("pose 20 0 0 0 0 0 fov tan -1.3764 1.3764 -1.4281 0.9657 ortho 28 "
        "hudlock world");
    flip();
    frame_with(hud, 10, 0);
    expect("hudlock world: the HUD a plane, following the turned head's view",
           handed(world, hud, 20, TAN, 2.0, 0.8));
    mark();
    ctl("pose 0 0 0 0 0 0 ortho 28 hudlock maybe");
    flip();
    expect("parse: hudlock takes head or world", !vr_worldproj_active() &&
           log_has("did not parse"));

    ctl("pose 20 0 0 0 0 0 fov tan -1.3764 1.3764 -1.4281 0.9657");
    flip();
    ctl("pose 20 0 0 0 0 0 fov tan -1.3764 1.3764 -1.4281 0.9657 ortho 28");
    flip();
    expect("dirty: turning the 2D layer on at a flip dirties its rows",
           dirty2d());
    memset(pg.vsh_constants_dirty, 0, sizeof(pg.vsh_constants_dirty));
    upload(O, hud);
    vr_worldproj_draw_extent(&pg, NULL, 0);
    bool d0 = dirty2d();
    memset(pg.vsh_constants_dirty, 0, sizeof(pg.vsh_constants_dirty));
    vr_worldproj_draw_extent(&pg, NULL, 0);
    bool d1 = dirty2d();
    upload(0, world);
    draw();
    memset(pg.vsh_constants_dirty, 0, sizeof(pg.vsh_constants_dirty));
    vr_worldproj_draw_extent(&pg, NULL, 0);
    bool d2 = dirty2d();
    memset(pg.vsh_constants_dirty, 0, sizeof(pg.vsh_constants_dirty));
    {
        const float v[16] = { 0, 0, 0, 1, 640, 0, 0, 1, 640, 480, 0, 1,
                              0, 480, 0, 1 };
        vr_worldproj_draw_extent(&pg, v, 4);
    }
    bool d3 = dirty2d();
    expect("dirty: unchanged state leaves the rows clean; the gate opening "
           "and a whole-screen draw each dirty them", !d1 && d2 && d3);
    (void)d0;

    mark();
    ctl("pose 0 0 0 0 0 0 ortho 2");
    flip();
    expect("parse: ortho over the camera's rows is refused",
           !vr_worldproj_active() && log_has("did not parse"));
    ctl("pose 0 0 0 0 0 0 ortho 56");
    flip();
    expect("parse: ortho over the viewport rows (58, 59) is refused",
           !vr_worldproj_active());
    ctl("pose 0 0 0 0 0 0 ortho 28 hud 0");
    flip();
    expect("parse: hud 0 is refused", !vr_worldproj_active());
    ctl("pose 0 0 0 0 0 0 ortho 28 hudscale 20");
    flip();
    expect("parse: hudscale 20 is refused", !vr_worldproj_active());
    ctl("pose 0 0 0 0 0 0 ortho 28 fullscreen maybe");
    flip();
    expect("parse: fullscreen takes keep or plane", !vr_worldproj_active());
    mark();
    ctl("hmd ortho 28 hud 2 hudscale 0.8 fullscreen keep cyup 1");
    flip();
    expect("parse: hmd takes the 2D options (they do not change the frustum)",
           log_has("hmd, but no headset pose") && !log_has("did not parse"));

    ctl("pose 20 0 0 0 0 0 fov tan -1.3764 1.3764 -1.4281 0.9657 ortho 28");
    flip();
    frame_with(hud, 10, 0);
    frame_with(depth2d, 0, 0);
    frame_with(depth2d, 0, 0);
    upload(O, hud);
    end_draw_quad(0, 0, 640, 480);
    mark();
    flip();
    expect("log: the per-frame line counts 2d flat 10 depth 2 full 1",
           log_has("2d flat 10 depth 2 full 1"));

    ctl("pose 0 0 0 0 0 0 ortho 28");
    flip();
    {
        float weapon10[4][4];
        camera(weapon10, 1.42815, -1.90420, 0.0234, 10);
        upload(0, world);
        for (int i = 0; i < 10; i++) {
            end_draw();
        }
        upload(0, weapon10);
        for (int i = 0; i < 3; i++) {
            end_draw();
        }
        upload(0, world);
    }
    mark();
    flip();
    expect("log: the main camera's world axes -- the test camera turned 30 "
           "degrees: forward (-0.5, 0, 0.866), right (0.866, 0, 0.5)",
           log_has("camera fwd -0.50000 0.00000 0.86603 right 0.86603 "
                   "0.00000 0.50000"));

    {
        const double sq[4][2] = { { 0, 0 }, { 2, 0 }, { 2, 3 }, { 0, 3 } };
        const double mix[5][2] = { { 2, 3 }, { 0, 0 }, { 1, 1 }, { 0, 3 }, { 2, 0 } };
        const double tri[3][2] = { { 0, 0 }, { 4, 0 }, { 0, 2 } };
        const double line[3][2] = { { 0, 0 }, { 1, 1 }, { 2, 2 } };
        const double dent[5][2] = { { 0, 0 }, { 4, 0 }, { 2, 1 }, { 4, 4 }, { 0, 4 } };
        expect("hull: a 2x3 rectangle 6, in any order and with an interior "
               "point; a triangle 4; collinear points 0; a point inside near an "
               "edge does not dent the hull (16)",
               fabs(vr_wp_hull_area(sq, 4) - 6) < 1e-12 &&
               fabs(vr_wp_hull_area(mix, 5) - 6) < 1e-12 &&
               fabs(vr_wp_hull_area(tri, 3) - 4) < 1e-12 &&
               vr_wp_hull_area(line, 3) == 0 &&
               fabs(vr_wp_hull_area(dent, 5) - 16) < 1e-12);
        expect("count: 100 x 4.48 = 448; 400 counted at scale 2 (divisor 4) "
               "= 448; capped at 100; one rounding (2.5 -> 3)",
               vr_wp_scale_count(100, 1, 4.48, 0) == 448 &&
               vr_wp_scale_count(400, 4, 4.48, 0) == 448 &&
               vr_wp_scale_count(100, 1, 4.48, 100) == 100 &&
               vr_wp_scale_count(5, 4, 2.0, 0) == 3);
    }
    {
        double zr, zt;
        VrWpDecomp d;
        vr_wp_decompose(world, &d);
        zr = d.r;
        zt = d.t;
        const float z = (float)(zr + zt / (5.0 * d.s));
        const float q[16] = { 315, 235, z, 1, 325, 235, z, 1,
                              325, 245, z, 1, 315, 245, z, 1 };
        double sc, cap;

        ctl("pose 0 0 0 0 0 0 fov tan -1.3764 1.3764 -1.4281 0.9657 "
            "ortho 28");
        flip();
        upload(0, world);
        end_draw();
        upload(O, depth2d);
        vr_worldproj_report_clear();
        pg.zpass_pixel_count_enable = true;
        vr_worldproj_draw_extent(&pg, q, 4);
        bool got = vr_worldproj_report_take(&sc, &cap);
        expect("qscale: a 10x10 query at rest in the first-light frustum: "
               "scale 4.48 (1 / 0.2232, scope 1.11), cap 100 game pixels",
               got && fabs(sc - 4.4801) < 0.02 && fabs(cap - 100) < 0.01);
        printf("     (scale %.4f cap %.3f)\n", sc, cap);
        expect("qscale: taking the report starts a new one (nothing left)",
               !vr_worldproj_report_take(&sc, &cap) && sc == 1.0 &&
               cap == 0.0);
        vr_worldproj_draw_extent(&pg, q, 4);
        vr_worldproj_report_clear();
        expect("qscale: CLEAR_REPORT_VALUE forgets the measured draws",
               !vr_worldproj_report_take(&sc, &cap));
        pg.zpass_pixel_count_enable = false;
        vr_worldproj_draw_extent(&pg, q, 4);
        expect("qscale: a draw with zpass counting off is not measured",
               !vr_worldproj_report_take(&sc, &cap));
        pg.zpass_pixel_count_enable = true;

        ctl("pose 0 0 0 0 0 0 ortho 28");
        flip();
        upload(0, world);
        end_draw();
        upload(O, depth2d);
        vr_worldproj_draw_extent(&pg, q, 4);
        expect("qscale: the game's own frustum at rest: scale 1",
               vr_worldproj_report_take(&sc, &cap) && fabs(sc - 1) < 1e-6);

        ctl("pose 0 0 0 0 0 0 fov tan -1.3764 1.3764 -1.4281 0.9657 "
            "ortho 28 qscale 1");
        flip();
        upload(0, world);
        end_draw();
        upload(O, depth2d);
        vr_worldproj_draw_extent(&pg, q, 4);
        expect("qscale 1: nothing measured, the count is the game's",
               !vr_worldproj_report_take(&sc, &cap));

        ctl("pose 180 0 0 0 0 0 fov tan -1.3764 1.3764 -1.4281 0.9657 "
            "ortho 28");
        flip();
        upload(0, world);
        end_draw();
        upload(O, depth2d);
        vr_worldproj_draw_extent(&pg, q, 4);
        expect("qscale: a lamp behind the eye is not measured",
               !vr_worldproj_report_take(&sc, &cap));

        ctl("pose 0 0 0 0 0 0 fov tan -1.3764 1.3764 -1.4281 0.9657 "
            "ortho 28");
        flip();
        upload(0, world);
        end_draw();
        upload(0, scoped_cam);
        end_draw();
        upload(O, depth2d);
        vr_worldproj_draw_extent(&pg, q, 4);
        expect("qscale: a scoped frame (a re-projected camera AND a scoped "
               "one) is not measured",
               !vr_worldproj_report_take(&sc, &cap));

        ctl("pose 0 0 0 0 0 0 fov tan -1.3764 1.3764 -1.4281 0.9657 "
            "ortho 28");
        flip();
        upload(0, world);
        end_draw();
        flip();
        upload(O, depth2d);
        vr_worldproj_report_clear();
        vr_worldproj_draw_extent(&pg, q, 4);
        draw();
        bool opened_rewritten = !untouched(depth2d);
        bool opened_measured = vr_worldproj_report_take(&sc, &cap);
        upload(0, world);
        end_draw();
        upload(O, depth2d);
        vr_worldproj_report_clear();
        vr_worldproj_draw_extent(&pg, q, 4);
        draw();
        expect("T3: a query opening the frame is neither re-projected nor "
               "measured; after a world draw it is both",
               !opened_rewritten && !opened_measured &&
               handed(world, depth2d, 0, TAN, 2.0, 0.8) &&
               vr_worldproj_report_take(&sc, &cap) && fabs(sc - 4.4801) < 0.02);
        volatile uint64_t big_acc = 1000000000ull;
        volatile double big_scale = 1e6;
        expect("count: no cap and a huge scale saturate at the report's width "
               "instead of overflowing",
               vr_wp_scale_count(big_acc, 1, big_scale, 0) == UINT32_MAX &&
               vr_wp_scale_count(0, 1, big_scale, 0) == 0);
        pg.zpass_pixel_count_enable = false;
    }

    printf("worldproj-2d-test: %s -- %d checks, %d failed\n",
           fails ? "FAIL" : "PASS", checks, fails);
    return fails != 0;
}
