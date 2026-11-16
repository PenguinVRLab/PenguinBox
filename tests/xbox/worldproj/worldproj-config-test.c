/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/nv2a_int.h"
#include "../../../hw/xbox/nv2a/pgraph/vr_worldproj.h"
#include "../../../hw/xbox/nv2a/pgraph/vr_worldproj_profiles.h"
#include "fake_host.h"
#include <float.h>
#include <unistd.h>

#define HALO 0x4D530004
#define TESTROW 0x7E570001
#define PGR2 0x4D53004B

static PGRAPHState pg;
static float copy[NV2A_VERTEXSHADER_CONSTANTS][4];
static float mg[4][4];
static int fails, checks;
static FILE *log_file;
static long log_mark;

static void expect(const char *what, bool ok)
{
    checks++;
    fails += !ok;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
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

static int log_count(const char *needle)
{
    static char buf[1 << 16];
    size_t n;
    int k = 0;

    fflush(stderr);
    fseek(log_file, log_mark, SEEK_SET);
    n = fread(buf, 1, sizeof(buf) - 1, log_file);
    buf[n] = 0;
    for (const char *p = strstr(buf, needle); p; p = strstr(p + 1, needle)) {
        k++;
    }
    return k;
}

static void mark(void)
{
    fflush(stderr);
    fseek(log_file, 0, SEEK_END);
    log_mark = ftell(log_file);
}

static int src_n;
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
    (void)fresh;
    memset(out, 0, sizeof(*out));
    out->display_time = 1000 + src_n;
    head(src_n, out->q, out->p);
    out->ref_q[3] = 1;
    out->ref_p[1] = 1.6;
    memcpy(out->tan, TAN, sizeof(TAN));
    return true;
}

static void flip(int next_pose)
{
    src_n = next_pose;
    memset(pg.vsh_constants_dirty, 0, sizeof(pg.vsh_constants_dirty));
    vr_worldproj_flip(&pg, 0x3c0000);
}

static void flips(int n)
{
    for (int i = 0; i < n; i++) {
        flip(1);
    }
}

static bool any_dirty(void)
{
    for (int i = 0; i < NV2A_VERTEXSHADER_CONSTANTS; i++) {
        if (pg.vsh_constants_dirty[i]) {
            return true;
        }
    }
    return false;
}

static void draw(void)
{
    memcpy(copy, pg.vsh_constants, sizeof(copy));
    vr_worldproj_apply(false, copy);
    checks++;
    fails += fake_rows_outside(pg.vsh_constants, copy) != 0;
}

static bool drawn_full(int group, int n, double upm, double sref,
                       const double seat[3])
{
    VrWpDecomp d;
    VrWpView v;
    float w[4][4];
    double q[4], p[3];
    static const double rq[4] = { 0, 0, 0, 1 }, rp[3] = { 0, 1.6, 0 };

    head(n, q, p);
    vr_wp_view_from_xr(rq, rp, q, p, upm, &v);
    v.fov = VR_WP_FOV_TANGENTS;
    v.tan_l = TAN[0];
    v.tan_r = TAN[1];
    v.tan_d = TAN[2];
    v.tan_u = TAN[3];
    if (vr_wp_decompose(mg, &d) != VR_WP_OK) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        v.pos[i] += upm * seat[i];
        if (sref > 0) {
            v.pos[i] *= sref / d.s;
        }
    }
    if (!vr_wp_rewrite(&d, &v, true, w)) {
        return false;
    }
    for (int i = 0; i < 4; i++) {
        double big = fmax(fmax(fabs(w[i][0]), fabs(w[i][1])),
                          fmax(fabs(w[i][2]), fabs(w[i][3])));
        for (int j = 0; j < 4; j++) {
            if (!(fabs(copy[group + i][j] - w[i][j]) <=
                  2.0 * FLT_EPSILON * big)) {
                return false;
            }
        }
    }
    return true;
}

static bool drawn_at(int group, int n, double upm)
{
    static const double none[3];
    return drawn_full(group, n, upm, 0, none);
}

static bool drawn_with(int n, double upm)
{
    return drawn_at(0, n, upm);
}

static bool untouched_at(int group)
{
    return !memcmp(copy[group], mg, sizeof(mg));
}

static bool untouched(void)
{
    return untouched_at(0);
}

static void scene(int bones)
{
    const double f = 1000.0, near = 0.0234;
    const double r = f / (f - near), t = -near * f / (f - near);
    float k[4][4] = { { 1.42f, 0, 0, 0 }, { 0, -1.9f, 0, 0 },
                      { 0, 0, (float)r, (float)t }, { 0, 0, 1, 0 } };

    memcpy(mg, k, sizeof(k));
    memcpy(pg.vsh_constants[0], mg, sizeof(mg));
    const float vpscl[4] = { 320.0f, -240.0f, 1.0f, 0.0f };
    memcpy(pg.vsh_constants[58], vpscl, sizeof(vpscl));
    for (int i = 0; i < 40; i++) {
        double ang = 0.1 * i, sc = i < bones ? 1.0 : 1.15;
        float b[3][4] = { { (float)(sc * cos(ang)), (float)(-sc * sin(ang)), 0, 0.1f },
                          { (float)(sc * sin(ang)), (float)(sc * cos(ang)), 0, 0.2f },
                          { 0, 0, (float)(i < bones ? 1.0 : 1.15), 0.3f } };
        if (i > bones) {
            memset(b, 0, sizeof(b));
        }
        memcpy(pg.vsh_constants[60 + 3 * i], b, sizeof(b));
    }
}

static void row_scene(void)
{
    const double f = 1000.0, near = 0.045;
    const double r = f / (f - near), t = -near * f / (f - near);
    float k[4][4] = { { 3.17f, 0, 0, 0 }, { 0, -4.2f, 0, 0 },
                      { 0, 0, (float)r, (float)t }, { 0, 0, 1, 0 } };
    const float vpscl[4] = { 320.0f, 240.0f, 1.0f, 0.0f };

    scene(0);
    memcpy(mg, k, sizeof(k));
    memcpy(pg.vsh_constants[8], mg, sizeof(mg));
    memcpy(pg.vsh_constants[58], vpscl, sizeof(vpscl));
    for (int i = 0; i < 4; i++) {
        double ang = 0.2 * i, sc = i < 3 ? 1.0 : 1.15;
        float b[3][4] = { { (float)(sc * cos(ang)), (float)(-sc * sin(ang)), 0, 0 },
                          { (float)(sc * sin(ang)), (float)(sc * cos(ang)), 0, 0 },
                          { 0, 0, (float)sc, 0 } };
        memcpy(pg.vsh_constants[90 + 3 * i], b, sizeof(b));
    }
}

static void ctl_write(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    fputs(text, f);
    fclose(f);
}

static void table_checks(void)
{
    const VrWpProfile *h = vr_wp_profile_find(HALO);

    expect("table: Halo's row is found by its title id, with the measured "
           "numbers (group 0, scope 2, upm 1/3.048, palette 60, arms 8, "
           "armsnear 0.04, cyup from the viewport)",
           h && h->group == 0 && h->scope_a == 2.0 &&
           h->upm == 1.0 / 3.048 && h->palette == 60 &&
           h->arms_bones == 8 && h->arms_near == 0.04 && h->cyup == -1);
    const VrWpProfile *g = vr_wp_profile_find(PGR2);
    expect("table: PGR2's row: group 114, upm 1, sref 5592.41 (the physical "
           "metre), hidebone 164 (the driver's head), the seat -0.30 0 0.20, "
           "no scope below 100, the 2D layer off",
           g && g->group == 114 && g->upm == 1.0 && g->sref == 5592.41 &&
           g->hide_bone == 164 && g->seat[0] == -0.30 && g->seat[1] == 0.0 &&
           g->seat[2] == 0.20 && g->scope_a == 100.0 && g->cyup == -1 &&
           g->arms_bones == 0 && !g->layer2d && g->requires_mod &&
           !strcmp(g->requires_mod, "pgr2-cockpit"));
    expect("table: no other row requires a mod (Halo's is used as it is)",
           !vr_wp_profile_find(HALO)->requires_mod &&
           !vr_wp_profile_find(HALO)->requires_view);
    expect("table: PGR2's view watch: the camera header (19 words), the index "
           "at 0xA0, 0 = the cockpit",
           g && g->requires_view &&
           !strcmp(g->requires_view->name, "pgr2-cockpit-view") &&
           g->requires_view->n_words == 19 &&
           g->requires_view->words[0] == 3 &&
           g->requires_view->offset == 0xA0 && g->requires_view->equals == 0);
    expect("table: a neighbouring id has no row",
           !vr_wp_profile_find(HALO + 1) && !vr_wp_profile_find(HALO - 1) &&
           !vr_wp_profile_find(0));
    bool rows_ok = true;
    for (int i = 0; i < vr_wp_profile_count; i++) {
        const VrWpProfile *p = &vr_wp_profiles[i];
        rows_ok &= p->name && p->name[0] && p->group >= 0 &&
                   p->group + 4 <= NV2A_VERTEXSHADER_CONSTANTS &&
                   (p->cyup >= 0 || p->group > 58 || p->group + 3 < 58) &&
                   p->scope_a > 0 && p->upm > 0 && p->arms_near > 0 &&
                   p->palette >= 0 &&
                   p->palette + 3 <= NV2A_VERTEXSHADER_CONSTANTS &&
                   vr_wp_profile_find(p->title_id) == p;
    }
    expect("table: every row is in bounds, says cyup if its group covers the "
           "viewport scale, and its id finds it (no duplicate ids)", rows_ok);
}

static void config_mode(void)
{
    vr_worldproj_set_pose_source(source);
    scene(37);
    flips(40);
    draw();
    expect("off: VR off, no rewrite, no title read scheduled, no constant "
           "marked dirty, nothing logged",
           !vr_worldproj_active() && untouched() && fake_requests == 0 &&
           !any_dirty() && !log_has("vr-wp"));
    vr_worldproj_invalidate();
    flips(2);
    expect("off: a reset with VR off schedules no title read",
           fake_requests == 0 && fake_forgets == 0);

    fake_view_projection = true;
    mark();
    flip(1);
    draw();
    expect("u5a: projection, title not known: the screen; a title read asked "
           "for; the log says so",
           !vr_worldproj_active() && untouched() && fake_requests == 1 &&
           log_has("view from config: projection, title not known yet: "
                   "screen"));
    flips(30);
    expect("retry: while not known, asked again every 30 flips",
           fake_requests == 2);

    fake_title_id = 0x12345678;
    mark();
    flip(1);
    draw();
    expect("u5b: a title with no row: the screen, and the log names it",
           !vr_worldproj_active() && untouched() &&
           log_has("no profile for title 12345678: screen"));

    fake_title_id = HALO;
    vr_worldproj_set_pose_source(NULL);
    mark();
    flip(1);
    draw();
    expect("Halo's row with no headset pose: drawn flat, and the log says "
           "why", !vr_worldproj_active() && untouched() &&
           log_has("view from config: projection (profile 4D530004 Halo: "
                   "Combat Evolved)") &&
           log_has("hmd, but no headset pose"));

    vr_worldproj_set_pose_source(source);
    mark();
    flip(4);
    draw();
    expect("u1: Halo's row and a pose: on (hmd), drawn for the pose with "
           "upm 1/3.048 and the headset frustum",
           vr_worldproj_active() && drawn_with(4, 1.0 / 3.048) &&
           log_has("-> on (hmd)"));
    vr_worldproj_draw_relative(true);
    draw();
    vr_worldproj_draw_relative(false);
    expect("u1: Halo's row hides no bone (its hide_bone 0 is none, not slot "
           "0): a relative draw gets the same camera rows",
           drawn_with(4, 1.0 / 3.048));
    expect("u1: the row's arms: a near-pass draw with 37 rigid bones is "
           "dropped", vr_worldproj_end_skips(&pg, true));
    scene(7);
    expect("u1: ... and one with 7 is kept (the row's threshold is 8)",
           !vr_worldproj_end_skips(&pg, true));

    fake_title_id = TESTROW;
    row_scene();
    mark();
    flip(7);
    draw();
    static const double row_seat[3] = { 0.5, -0.25, 1.0 };
    expect("row: the test row's numbers: group 8, scope 5 (a focal term of "
           "3.17 rewritten), cyup 1 over the viewport's sign, upm 1, the eye "
           "on the seat 0.5 -0.25 1.0 over the head, sref 7",
           vr_worldproj_active() && drawn_full(8, 7, 1.0, 7.0, row_seat) &&
           log_has("view from config: projection (profile 7E570001 test "
                   "row)"));
    expect("row: its arms: 3 rigid bones at palette 90, near 0.045 under "
           "armsnear 0.05: dropped", vr_worldproj_end_skips(&pg, true));
    {
        const float bone[3][4] = { { 1, 2, 3, 4 }, { 5, 6, 7, 8 },
                                   { 9, 10, 11, 12 } };
        const float hidden[3][4] = { { 0, 0, 0, 4 }, { 0, 0, 0, 8 },
                                     { 0, 0, 0, 12 } };
        memcpy(pg.vsh_constants[120], bone, sizeof(bone));
        vr_worldproj_draw_relative(true);
        draw();
        bool hid = !memcmp(copy[120], hidden, sizeof(hidden));
        vr_worldproj_draw_relative(false);
        draw();
        expect("row: its hidebone 120: collapsed for a relative draw, kept "
               "for a direct one",
               hid && !memcmp(copy[120], bone, sizeof(bone)));
        memset(pg.vsh_constants[120], 0, sizeof(bone));
    }
    {
        const float hud[4][4] = { { 1 / 320.0f, 0, 0, -1.0016f },
                                  { 0, -1 / 240.0f, 0, 1.0021f },
                                  { 0, 0, 0, 0.5f }, { 0, 0, 0, 1 } };
        VrWpDecomp d;
        VrWpView v;
        float want[4][4];
        double q[4], p[3];
        static const double rq[4] = { 0, 0, 0, 1 }, rp[3] = { 0, 1.6, 0 };
        memcpy(pg.vsh_constants[20], hud, sizeof(hud));
        vr_worldproj_draw_extent(&pg, NULL, 0);
        draw();
        head(7, q, p);
        vr_wp_view_from_xr(rq, rp, q, p, 1.0, &v);
        vr_wp_view_identity(&v);
        (void)rq;
        (void)rp;
        v.fov = VR_WP_FOV_TANGENTS;
        v.tan_l = TAN[0];
        v.tan_r = TAN[1];
        v.tan_d = TAN[2];
        v.tan_u = TAN[3];
        vr_wp_decompose(mg, &d);
        vr_wp_rewrite_2d(&d, &v, true, hud, VR_WP_2D_FLAT, 3.0 * 1.0 * 7.0,
                         0.5, want);
        expect("row: its 2D layer (slot 20, 3 m, scale 0.5, head-locked) is on, "
               "with its numbers", !memcmp(copy[20], want, sizeof(want)));
    }
    fake_title_id = HALO;
    scene(37);
    flip(1);
    {
        const float hud[4][4] = { { 1 / 320.0f, 0, 0, -1.0016f },
                                  { 0, -1 / 240.0f, 0, 1.0021f },
                                  { 0, 0, 0, 0.5f }, { 0, 0, 0, 1 } };
        draw();
        memcpy(pg.vsh_constants[28], hud, sizeof(hud));
        vr_worldproj_draw_extent(&pg, NULL, 0);
        draw();
        expect("Halo's row: the 2D layer stays off (layer2d false)",
               vr_worldproj_active() && !memcmp(copy[28], hud, sizeof(hud)));
    }

    fake_title_id = PGR2;
    fake_mod_armed = false;
    fake_mod_asked[0] = 0;
    mark();
    flip(3);
    draw();
    expect("mod: PGR2's row with its fix not armed: the screen, and the log "
           "names the fix", !vr_worldproj_active() &&
           !strcmp(fake_mod_asked, "pgr2-cockpit") &&
           log_has("profile 4D53004B Project Gotham Racing 2 waits for "
                   "'pgr2-cockpit' (not armed): screen"));
    fake_mod_armed = true;
    fake_view_armed = false;
    mark();
    flip(3);
    expect("view: the fix armed but the cockpit camera not on screen: the "
           "screen, and the log names the view watch",
           !vr_worldproj_active() &&
           log_has("waits for 'pgr2-cockpit-view' (not armed): screen"));
    fake_view_armed = true;
    mark();
    flip(3);
    expect("mod: ... both armed (the cockpit on screen): projection with "
           "PGR2's row", vr_worldproj_active() &&
           log_has("view from config: projection (profile 4D53004B Project "
                   "Gotham Racing 2)"));
    fake_view_armed = false;
    flip(3);
    expect("view: another camera chosen: back to the screen",
           !vr_worldproj_active());
    fake_view_armed = true;
    fake_mod_armed = false;
    flip(3);
    expect("mod: ... disarmed again (a reset, the pattern gone): the screen",
           !vr_worldproj_active());
    fake_title_id = HALO;
    flip(1);

    int before = fake_requests;
    flips(600);
    expect("refresh: once known, the title is read every 300 flips",
           fake_requests - before == 2);

    vr_worldproj_invalidate();
    flip(5);
    draw();
    expect("invalidate: the title is forgotten (a forgetting read asked for) "
           "and the frame is not re-projected meanwhile",
           fake_forgets == 1 && !vr_worldproj_active() && untouched());
    fake_title_id = HALO;
    flip(6);
    draw();
    expect("invalidate: once read again, on", vr_worldproj_active() &&
           drawn_with(6, 1.0 / 3.048));

    fake_view_projection = false;
    before = fake_requests;
    mark();
    flip(7);
    draw();
    expect("u2: the config off: off at the next flip, and the log says so",
           !vr_worldproj_active() && untouched() &&
           log_has("view from config: screen"));
    flips(400);
    expect("u2: and from then on nothing is read or asked for",
           !vr_worldproj_active() && fake_requests == before);

    fake_view_projection = true;
    flip(8);
    expect("the config back on: on again", vr_worldproj_active());
    vr_worldproj_set_view(NULL, false);
    flip(9);
    flips(5);
    draw();
    expect("set_view(off) wins over the config, and keeps winning",
           !vr_worldproj_active() && untouched());
}

static void ctl_mode(const char *path)
{
    fake_view_projection = true;
    fake_title_id = HALO;
    vr_worldproj_set_pose_source(source);
    scene(37);

    flip(2);
    draw();
    expect("u4: no complete line yet: the config is the previous view (on)",
           vr_worldproj_active() && drawn_with(2, 1.0 / 3.048));
    ctl_write(path, "off");
    flip(3);
    draw();
    expect("u4: a torn line before any complete one: still the config's",
           vr_worldproj_active() && drawn_with(3, 1.0 / 3.048));

    mark();
    ctl_write(path, "off\n");
    flip(4);
    draw();
    expect("u3: the file's `off` over the config: off, and the log says the "
           "file overrides", !vr_worldproj_active() && untouched() &&
           log_has("the control file overrides the config"));
    ctl_write(path, "hmd");
    flip(5);
    flips(3);
    draw();
    expect("u3: a torn line after a complete one keeps the FILE's view (off), "
           "not the config's", !vr_worldproj_active() && untouched());
    ctl_write(path, "hmd upm 1\n");
    flip(6);
    draw();
    expect("the file's own line: hmd with its own numbers (upm 1), not the "
           "row's", vr_worldproj_active() && drawn_with(6, 1.0));
    {
        static const double seat[3] = { -0.30, 0.0, 0.20 };
        float seated[4][4];
        ctl_write(path, "pose 0 0 0 0 0 0 upm 1 seat -0.30 0 0.20\n");
        flip(6);
        draw();
        memcpy(seated, copy[0], sizeof(seated));
        ctl_write(path, "pose 0 0 0 -0.30 0 0.20 upm 1\n");
        flip(6);
        draw();
        expect("seat: on a pose line it is the pose's position moved by the "
               "seat (the same rows as the pose there), and it moves the eye",
               vr_worldproj_active() &&
               !memcmp(seated, copy[0], sizeof(seated)) && !untouched());
        ctl_write(path, "hmd upm 1 seat -0.30 0 0.20\n");
        flip(6);
        draw();
        expect("seat: on an hmd line, added to each head pose",
               vr_worldproj_active() && drawn_full(0, 6, 1.0, 0, seat));
        ctl_write(path, "hmd upm 1 seat 11 0 0\n");
        flip(6);
        draw();
        expect("seat: a seat beyond 10 m does not parse (the view off)",
               !vr_worldproj_active() && untouched());
    }

    float m[4][4];
    ctl_write(path, "hmd fov game\n");
    flip(7);
    mark();
    for (int i = 0; i < 8; i++) {
        memcpy(m, mg, sizeof(m));
        m[0][0] = i % 2 ? nextafterf(mg[0][0], 2.0f) : mg[0][0];
        memcpy(pg.vsh_constants[0], m, sizeof(m));
        draw();
        flip(8 + i);
    }
    expect("log: a frustum that differs only in its last bits is logged once",
           log_count("game frustum (drawn and declared)") == 1);
    memcpy(m, mg, sizeof(m));
    m[0][0] = 1.6f;
    memcpy(pg.vsh_constants[0], m, sizeof(m));
    draw();
    flip(20);
    expect("log: a frustum that really changed is logged again",
           log_count("game frustum (drawn and declared)") == 2);
}

int main(int argc, char **argv)
{
    const char *path = getenv("XEMU_VR_WORLDPROJ");

    if (argc < 2 || (strcmp(argv[1], "config") && strcmp(argv[1], "ctl")) ||
        (!strcmp(argv[1], "config") && path) ||
        (!strcmp(argv[1], "ctl") && !path)) {
        printf("usage: %s config   (XEMU_VR_WORLDPROJ unset)\n"
               "       XEMU_VR_WORLDPROJ=<empty file> %s ctl\n",
               argv[0], argv[0]);
        return 2;
    }
    log_file = tmpfile();
    if (!log_file || dup2(fileno(log_file), 2) < 0) {
        printf("FAIL cannot capture the log\n");
        return 1;
    }
    setvbuf(stderr, NULL, _IONBF, 0);

    table_checks();
    if (!strcmp(argv[1], "config")) {
        config_mode();
    } else {
        ctl_mode(path);
    }
    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
