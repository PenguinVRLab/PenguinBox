/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/nv2a_int.h"
#include "../../../hw/xbox/nv2a/pgraph/vr_worldproj.h"
#include "pins.h"
#include "fake_host.h"
#include <float.h>

static PGRAPHState pg;
static uint32_t guest[NV2A_VERTEXSHADER_CONSTANTS][4];
static float copy[NV2A_VERTEXSHADER_CONSTANTS][4];
static const char *ctl_path;
static int fails, checks;

static void ctl_raw(const char *text)
{
    FILE *f = fopen(ctl_path, "w");
    fputs(text, f);
    fclose(f);
}

static void ctl(const char *line)
{
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s\n", line);
    ctl_raw(buf);
}

static void ctl_pin(const Pin *p, const char *extra)
{
    char line[768], fov[160];
    if (!strncmp(p->fov_text, "shear ", 6)) {
        snprintf(fov, sizeof(fov), "%s", p->fov_text);
    } else {
        snprintf(fov, sizeof(fov), "fov %s", p->fov_text);
    }
    snprintf(line, sizeof(line), "pose %.17g %.17g %.17g %.17g %.17g %.17g "
             "%s cyup %d %s", p->ypr[0], p->ypr[1], p->ypr[2],
             p->view.pos[0], p->view.pos[1], p->view.pos[2], fov,
             p->clip_y_up, extra);
    ctl(line);
}

static void guest_row(int slot, const float row[4])
{
    memcpy(pg.vsh_constants[slot], row, 16);
    memcpy(guest[slot], row, 16);
}

static void guest_upload(int group, const float m[4][4])
{
    for (int i = 0; i < 4; i++) {
        guest_row(group + i, m[i]);
    }
}

static void flip(void)
{
    memset(pg.vsh_constants_dirty, 0, sizeof(pg.vsh_constants_dirty));
    vr_worldproj_flip(&pg, 0x3c0000);
}

static void draw(bool fixed_function)
{
    memcpy(copy, pg.vsh_constants, sizeof(copy));
    vr_worldproj_apply(fixed_function, copy);
    checks++;
    fails += fake_rows_outside(pg.vsh_constants, copy) != 0;
}

static void expect(const char *what, bool ok)
{
    checks++;
    ok = ok && !memcmp(pg.vsh_constants, guest, sizeof(guest));
    if (!ok) {
        fails++;
    }
    printf("%s %s%s\n", ok ? "ok  " : "FAIL", what,
           memcmp(pg.vsh_constants, guest, sizeof(guest))
               ? "  [THE CONSTANT FILE WAS WRITTEN]" : "");
}

static bool copy_is(int group, const double me[16])
{
    for (int i = 0; i < 4; i++) {
        double big = 0;
        for (int j = 0; j < 4; j++) {
            big = fmax(big, fabs(me[i * 4 + j]));
        }
        for (int j = 0; j < 4; j++) {
            if (!(fabs((double)copy[group + i][j] - me[i * 4 + j]) <=
                  2.0 * FLT_EPSILON * fmax(big, 1e-30))) {
                return false;
            }
        }
    }
    return true;
}

static bool copy_is_guest(int group)
{
    return !memcmp(copy[group], guest[group], 16 * sizeof(float));
}

static bool copy_finite(void)
{
    for (int i = 0; i < NV2A_VERTEXSHADER_CONSTANTS; i++) {
        for (int j = 0; j < 4; j++) {
            if (!isfinite(copy[i][j])) {
                return false;
            }
        }
    }
    return true;
}

static bool dirty(int group)
{
    return pg.vsh_constants_dirty[group] && pg.vsh_constants_dirty[group + 1] &&
           pg.vsh_constants_dirty[group + 2] && pg.vsh_constants_dirty[group + 3];
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

static void want(const float m[4][4], const Pin *p, double me[16])
{
    VrWpDecomp d = { 0 };
    VrWpView v = p->view;
    float w[4][4] = { { 0 } };
    vr_wp_rot_ypr(p->ypr[0], p->ypr[1], p->ypr[2], v.rot);
    if (vr_wp_decompose(m, &d) == VR_WP_OK) {
        vr_wp_rewrite(&d, &v, p->clip_y_up, w);
    }
    for (int i = 0; i < 16; i++) {
        me[i] = w[i / 4][i % 4];
    }
}

int main(int argc, char **argv)
{
    static Pin pins[1024];
    char line[1024];
    double me[16];
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
    const Pin *yaw = find(pins, n, "syn1.yaw+5");
    const Pin *pitch = find(pins, n, "syn1.pitch+5");
    const Pin *shear = find(pins, n, "syn1.shear+0.25");
    const Pin *scoped = find(pins, n, "syn2.yaw+5");
    const Pin *other = find(pins, n, "syn0.yaw+5");
    const Pin *ortho = find(pins, n, "neg.ortho");
    const Pin *nan_ = find(pins, n, "edge.nan");
    const Pin *up0 = find(pins, n, "syn4.pitch+5");
    float vp_up[4] = { 320, -240, 16777215, 0 };
    float vp_down[4] = { 320, 240, 16777215, 0 };

    draw(false);
    expect("a fresh constant file, nothing configured: the copy is the file, "
           "and finite", copy_is_guest(0) && copy_finite());
    ctl("off");
    flip();
    guest_upload(0, yaw->mg);
    draw(false);
    expect("off: a draw is handed the guest's rows", copy_is_guest(0));
    expect("off at a flip, nothing changed: no row is marked dirty",
           !any_dirty());

    ctl_pin(yaw, "");
    flip();
    expect("on at a flip: the group is marked dirty, so the renderer takes "
           "its copy again", dirty(0));
    draw(false);
    expect("on, no new upload: the draw is handed the rewritten rows",
           copy_is(0, yaw->me));

    ctl_pin(pitch, "");
    flip();
    expect("only the pose changed at a flip: the group is marked dirty",
           dirty(0));
    draw(false);
    expect("only the pose changed: the draw is handed rows for the new pose",
           copy_is(0, pitch->me) && !copy_is(0, yaw->me));
    flip();
    expect("a flip with nothing changed marks nothing", !any_dirty());
    ctl_pin(yaw, "");
    flip();

    guest_upload(0, other->mg);
    draw(false);
    expect("a second camera written: rewritten from ITS rows",
           copy_is(0, other->me));
    guest_upload(0, yaw->mg);
    draw(false);
    expect("the first camera again", copy_is(0, yaw->me));
    {
        float m[4][4];
        memcpy(m, yaw->mg, sizeof(m));
        for (int j = 0; j < 4; j++) {
            m[2][j] = yaw->mg[2][j] * 0.5f + yaw->mg[3][j] * 0.25f;
        }
        guest_row(2, m[2]);
        want(m, yaw, me);
        draw(false);
        expect("a depth row written alone: the draw's rows use it",
               copy_is(0, me) && !copy_is(0, yaw->me));
        guest_upload(0, yaw->mg);
    }

    draw(false);
    guest_upload(0, ortho->mg);
    draw(false);
    expect("an orthographic matrix after a camera: the guest's rows, not the "
           "camera's rewrite", copy_is_guest(0));
    guest_upload(0, yaw->mg);
    draw(false);
    guest_upload(0, scoped->mg);
    draw(false);
    expect("a scoped view after a camera: the guest's rows",
           copy_is_guest(0));
    guest_upload(0, nan_->mg);
    draw(false);
    expect("a matrix holding NaN: the guest's rows", copy_is_guest(0));
    guest_upload(0, yaw->mg);
    draw(true);
    expect("a fixed-function draw: the guest's rows", copy_is_guest(0));
    {
        VrWpDecomp d;
        vr_wp_decompose(scoped->mg, &d);
        snprintf(line, sizeof(line), "scope %.17g", d.a);
        ctl_pin(scoped, line);
        flip();
        guest_upload(0, scoped->mg);
        draw(false);
        expect("a focal term EQUAL to the scope limit is rewritten (the limit "
               "is 'above')", copy_is(0, scoped->me));
        snprintf(line, sizeof(line), "scope %.17g", d.a * (1 - 1e-9));
        ctl_pin(scoped, line);
        flip();
        draw(false);
        expect("a limit a hair below the focal term: the guest's rows",
               copy_is_guest(0));
    }

    guest_upload(0, yaw->mg);
    guest_upload(139, other->mg);
    ctl_pin(yaw, "group 139");
    flip();
    expect("the group moved: both the old and the new group are marked dirty",
           dirty(0) && dirty(139));
    draw(false);
    want(other->mg, yaw, me);
    expect("the group moved, nothing uploaded: the NEW group is rewritten from "
           "the rows it holds, the old group is the guest's",
           copy_is(139, me) && copy_is_guest(0));
    {
        float r[4] = { 1.5f, -2.5f, 3.5f, 4.5f };
        guest_row(28, r);
        draw(false);
        expect("rows outside the group pass through, the two next to it "
               "included", copy_is_guest(28) && copy_is_guest(58) &&
               !memcmp(copy[138], guest[138], 16) &&
               !memcmp(copy[143], guest[143], 16));
    }

    {
        const Pin *near_id = find(pins, n, "syn3.identity");
        const Pin *far_hs = find(pins, n, "syn1.headset-fov");
        const Pin *near_sh = find(pins, n, "syn3.shear+0.25");
        guest_row(58, vp_up);
        ctl_pin(far_hs, "nearfov 0.04");
        flip();
        guest_upload(0, near_id->mg);
        draw(false);
        expect("nearfov: a nearer near plane keeps the game's frustum",
               copy_is(0, near_id->me));
        guest_upload(0, far_hs->mg);
        draw(false);
        expect("nearfov: the main pass still gets the view's frustum",
               copy_is(0, far_hs->me));
        ctl_pin(near_sh, "nearfov 0.04");
        flip();
        guest_upload(0, near_id->mg);
        draw(false);
        expect("nearfov with a shear: the near pass gets NO shear",
               copy_is(0, near_id->me) && !copy_is(0, near_sh->me));
        ctl_pin(near_sh, "nearfov 0.005");
        flip();
        draw(false);
        expect("nearfov below the pass's near plane: the shear applies",
               copy_is(0, near_sh->me));
    }

    {
        const Pin *px = find(pins, n, "syn1.x+0.5");
        const Pin *py = find(pins, n, "syn1.y+0.5");
        const Pin *pz = find(pins, n, "syn1.z+0.5");
        guest_upload(0, px->mg);
        ctl_pin(px, "");
        flip();
        draw(false);
        expect("the head 0.5 to the right", copy_is(0, px->me));
        ctl_pin(py, "");
        flip();
        expect("only the POSITION changed at a flip: the group is marked "
               "dirty", dirty(0));
        draw(false);
        expect("the head 0.5 up", copy_is(0, py->me) && !copy_is(0, pz->me));
        ctl_pin(pz, "");
        flip();
        draw(false);
        expect("the head 0.5 forward", copy_is(0, pz->me) &&
               !copy_is(0, py->me));
    }

    {
        const Pin *big = find(pins, n, "edge.rows1e+20");
        float m[4][4];
        for (int i = 0; i < 16; i++) {
            m[i / 4][i % 4] = big->mg[i / 4][i % 4] * 1e15f;
        }
        guest_upload(0, m);
        ctl("pose 5 0 0 0 0 0 fov game");
        flip();
        draw(false);
        expect("a camera with huge rows, the head turned: rewritten, finite",
               vr_worldproj_active() && copy_finite() && !copy_is_guest(0));
        ctl("pose 5 0 0 9000 9000 9000 fov game");
        flip();
        draw(false);
        expect("the same camera, the head far enough that a row overflows "
               "float32: the guest's rows", vr_worldproj_active() &&
               copy_is_guest(0) && copy_finite());
    }

    ctl_pin(shear, "");
    flip();
    guest_upload(0, shear->mg);
    draw(false);
    expect("shear through the control file", copy_is(0, shear->me));

    ctl("pose 0 5 0 0 0 0 fov game");
    flip();
    guest_upload(0, up0->mg);
    guest_row(58, vp_down);
    draw(false);
    expect("viewport y scale positive: read as clip +y DOWN",
           copy_is(0, up0->me));
    guest_row(58, vp_up);
    draw(false);
    expect("the viewport's sign changes with NO flip and NO upload: the next "
           "draw's rows follow it", !copy_is(0, up0->me) && !copy_is_guest(0));
    ctl("pose 0 5 0 0 0 0 fov game group 55");
    flip();
    draw(false);
    expect("a group covering the viewport row, no cyup given: refused (off)",
           !vr_worldproj_active() && copy_is_guest(55) && copy_is_guest(0));
    ctl("pose 0 5 0 0 0 0 fov game group 55 cyup 1");
    flip();
    expect("the same group with cyup given: accepted", vr_worldproj_active());
    ctl("pose 0 5 0 0 0 0 fov game group 189");
    flip();
    guest_upload(0, yaw->mg);
    draw(false);
    expect("a group past the end of the constant file: refused (off)",
           !vr_worldproj_active() && copy_is_guest(0) && copy_is_guest(188));

    {
        static const char *const bad[] = {
            "pose 5 0 0 0 0", "pose 5 0 0 0 0 0 shear nan",
            "pose 5 0 0 0 0 0 shear inf", "pose nan 0 0 0 0 0",
            "pose 5 0 0 1e300 0 0", "pose 5 0 0 0 0 0 fov tan -1 inf -1 1",
            "pose 5 0 0 0 0 0 fov tan 1 -1 -1 1",
            "pose 5 0 0 0 0 0 fov tan -1 1 1 -1",
            "pose 5 0 0 0 0 0 fov tan -1 1 -1", "pose 5 0 0 0 0 0 group 12abc",
            "pose 5 0 0 0 0 0 group 1.5", "pose 5 0 0 0 0 0 scope 0",
            "pose 5 0 0 0 0 0 nearfov -1", "pose 5 0 0 0 0 0 cyup 2",
            "pose 5 0 0 0 0 0 fov", "pose 5x 0 0 0 0 0", "off now",
            "pose 5 0 0 0 0 0 bogus 1",
        };
        guest_upload(0, yaw->mg);
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
            ctl_pin(yaw, "");
            flip();
            draw(false);
            bool was_on = copy_is(0, yaw->me);
            ctl(bad[i]);
            flip();
            draw(false);
            snprintf(line, sizeof(line), "refused: '%s'", bad[i]);
            expect(line, was_on && !vr_worldproj_active() && copy_is_guest(0));
        }
    }

    ctl_pin(yaw, "");
    flip();
    draw(false);
    ctl_raw("");
    flip();
    draw(false);
    expect("an EMPTY control file: the previous view is kept",
           copy_is(0, yaw->me));
    ctl_raw("pose 10 0 0 0 0 1");
    flip();
    draw(false);
    expect("a line with no newline at its end: the previous view is kept",
           copy_is(0, yaw->me));
    {
        char big[900];
        memset(big, ' ', sizeof(big));
        snprintf(big + 700, sizeof(big) - 700, "off\n");
        memcpy(big, "pose 5 0 0 0 0 0", 16);
        ctl_raw(big);
        flip();
        draw(false);
        expect("a line longer than the buffer: the previous view is kept",
               copy_is(0, yaw->me));
    }

    ctl("off");
    flip();
    expect("off at a flip: the group is marked dirty", dirty(0));
    draw(false);
    expect("off: the draw is handed the guest's rows", copy_is_guest(0));

    printf("worldproj-sidecar-test: %s -- %d checks, %d failed\n",
           fails ? "FAIL" : "PASS", checks, fails);
    return fails != 0;
}
