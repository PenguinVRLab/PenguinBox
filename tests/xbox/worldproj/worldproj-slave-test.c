/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/nv2a_int.h"
#include "../../../hw/xbox/nv2a/pgraph/vr_worldproj.h"
#include "fake_host.h"
#include <float.h>

static PGRAPHState pg;
static float copy[NV2A_VERTEXSHADER_CONSTANTS][4];
static const char *ctl_path;
static int fails, checks;

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

static void halo_camera(float m[4][4], double yaw_deg, double pitch_deg)
{
    const double y = yaw_deg * M_PI / 180, p = pitch_deg * M_PI / 180;
    const double f[3] = { cos(p) * cos(y), cos(p) * sin(y), sin(p) };
    const double r[3] = { sin(y), -cos(y), 0 };
    const double u[3] = { r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2],
                          r[0] * f[1] - r[1] * f[0] };
    const double e[3] = { -20.98, -5.12, 0.62 };
    const double n = 0.0625, far = 1024.0, rr = far / (far - n),
                 tt = -n * far / (far - n);
    const double *axes[3] = { r, u, f };
    double V[3][4];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            V[i][j] = axes[i][j];
        }
        V[i][3] = -(axes[i][0] * e[0] + axes[i][1] * e[1] + axes[i][2] * e[2]);
    }
    for (int j = 0; j < 4; j++) {
        m[0][j] = (float)(1.42815 * V[0][j]);
        m[1][j] = (float)(1.90420 * V[1][j]);
        m[2][j] = (float)(rr * V[2][j] + (j == 3 ? tt : 0));
        m[3][j] = (float)V[2][j];
    }
}

static void frame(const char *line, bool slave, double by, double bp,
                  const float m[4][4], float out[4][4])
{
    ctl(line);
    vr_worldproj_set_slave(slave, by * M_PI / 180,
                           isnan(bp) ? NAN : bp * M_PI / 180);
    memset(pg.vsh_constants_dirty, 0, sizeof(pg.vsh_constants_dirty));
    vr_worldproj_flip(&pg, 0x3c0000);
    memcpy(pg.vsh_constants[0], m, 64);
    memcpy(copy, pg.vsh_constants, sizeof(copy));
    vr_worldproj_apply(false, copy);
    memcpy(out, copy[0], 64);
}

static bool still_head(bool fresh, VrWpHmdPose *o)
{
    (void)fresh;
    memset(o, 0, sizeof(*o));
    o->q[3] = 1;
    o->ref_q[3] = 1;
    o->tan[0] = -1.3764;
    o->tan[1] = 1.3764;
    o->tan[2] = -1.4281;
    o->tan[3] = 0.9657;
    return true;
}

static double rows_diff(const float a[4][4], const float b[4][4])
{
    double worst = 0;
    for (int i = 0; i < 4; i++) {
        double big = 0;
        for (int j = 0; j < 4; j++) {
            big = fmax(big, fmax(fabs(a[i][j]), fabs(b[i][j])));
        }
        for (int j = 0; j < 4; j++) {
            worst = fmax(worst, fabs(a[i][j] - b[i][j]) / fmax(big, 1e-30));
        }
    }
    return worst;
}

int main(void)
{
    const double by = 82.56, bp = 14.0;
    const char *pose = "pose 25 10 5 0.02 0.01 -0.01 cyup 1";
    float cam[4][4], a[4][4], b[4][4], c[4][4], dd[4][4], e[4][4], f[4][4];
    const float vpscl[4] = { 320.0f, -240.0f, 1.0f, 0.0f };

    ctl_path = getenv("XEMU_VR_WORLDPROJ");
    if (!ctl_path) {
        printf("usage: XEMU_VR_WORLDPROJ=<scratch file> worldproj-slave-test\n");
        return 2;
    }
    memcpy(pg.vsh_constants[58], vpscl, sizeof(vpscl));

    halo_camera(cam, by, bp);
    frame(pose, false, 0, 0, cam, a);
    halo_camera(cam, by - 25, bp + 10);
    frame(pose, true, by, bp, cam, b);
    expect("(b) the game carries the head's yaw and pitch: with the residual "
           "the eye sees what (a) sees", rows_diff(a, b) < 2e-5);
    printf("     (a vs b %.2e)\n", rows_diff(a, b));
    halo_camera(cam, by - 15, bp);
    frame(pose, true, by, bp, cam, c);
    expect("(c) another split (latency): the same rows", rows_diff(a, c) < 2e-5);
    halo_camera(cam, by - 25, bp + 10);
    frame(pose, false, 0, 0, cam, dd);
    expect("(d) NEGATIVE: the residual off on a turned camera turns twice "
           "(differs from (a))", rows_diff(a, dd) > 1e-2);
    halo_camera(cam, by - 25, bp);
    frame(pose, true, by, NAN, cam, e);
    expect("(e) yaw-only lever: the body's pitch measured from the camera: "
           "the same rows", rows_diff(a, e) < 2e-5);
    halo_camera(cam, by - 25, bp + 10);
    ctl(pose);
    vr_worldproj_set_slave(true, by * M_PI / 180, bp * M_PI / 180);
    vr_worldproj_flip(&pg, 0x3c0000);
    vr_worldproj_set_slave(false, 0, 0);
    memcpy(pg.vsh_constants[0], cam, 64);
    memcpy(copy, pg.vsh_constants, sizeof(copy));
    vr_worldproj_apply(false, copy);
    memcpy(f, copy[0], 64);
    expect("(f) the body's angles are latched at the flip", rows_diff(a, f) < 2e-5);

    ctl(pose);
    vr_worldproj_set_slave(true, by * M_PI / 180, bp * M_PI / 180);
    vr_worldproj_flip(&pg, 0x3c0000);
    vr_worldproj_set_slave(true, (by + 5) * M_PI / 180, bp * M_PI / 180);
    memset(pg.vsh_constants_dirty, 0, sizeof(pg.vsh_constants_dirty));
    vr_worldproj_flip(&pg, 0x3c0000);
    expect("a changed body report marks the camera rows dirty",
           pg.vsh_constants_dirty[0] && pg.vsh_constants_dirty[3]);

    {
        double y, p;
        ctl("pose 25 10 5 0 0 0 cyup 1");
        vr_worldproj_flip(&pg, 0x3c0000);
        bool got = vr_worldproj_script_head(&y, &p);
        expect("D3: the pose line's head: 25 right -> -25 deg (the driver's + "
               "left), 10 up -> +10 deg",
               got && fabs(y + 25 * M_PI / 180) < 1e-12 &&
               fabs(p - 10 * M_PI / 180) < 1e-12);
        vr_worldproj_set_pose_source(still_head);
        ctl("hmd cyup 1");
        vr_worldproj_flip(&pg, 0x3c0000);
        bool hmd_on = vr_worldproj_active();
        bool hmd = vr_worldproj_script_head(&y, &p);
        vr_worldproj_set_pose_source(NULL);
        expect("D3: (hmd is on in this check)", hmd_on);
        ctl("off");
        vr_worldproj_flip(&pg, 0x3c0000);
        bool off = vr_worldproj_script_head(&y, &p);
        expect("D3: no scripted head under hmd (the headset is the head) or "
               "off", !hmd && !off);
    }

    printf("worldproj-slave-test: %s -- %d checks, %d failed\n",
           fails ? "FAIL" : "PASS", checks, fails);
    return fails != 0;
}
