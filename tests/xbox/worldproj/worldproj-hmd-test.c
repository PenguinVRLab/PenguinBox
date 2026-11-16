/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#include "../../../hw/xbox/nv2a/pgraph/vr_worldproj_stamp.h"
#include <stdio.h>

static int fails, checks;

static void expect(const char *what, bool ok)
{
    checks++;
    fails += !ok;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static const double D2R = 3.14159265358979323846 / 180.0;

static void qaxis(double ax, double ay, double az, double deg, double q[4])
{
    double h = deg * D2R / 2;
    q[0] = ax * sin(h);
    q[1] = ay * sin(h);
    q[2] = az * sin(h);
    q[3] = cos(h);
}

static void qmul(const double a[4], const double b[4], double o[4])
{
    double r[4] = {
        a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
        a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
        a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
        a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2],
    };
    memcpy(o, r, sizeof(r));
}

static void xr_head(double yaw, double pitch, double roll, double q[4])
{
    double qy[4], qp[4], qr[4], t[4];
    qaxis(0, 1, 0, -yaw, qy);
    qaxis(1, 0, 0, pitch, qp);
    qaxis(0, 0, 1, -roll, qr);
    qmul(qy, qp, t);
    qmul(t, qr, q);
}

static double rot_err(const double A[3][3], const double B[3][3])
{
    double e = 0;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            e = fmax(e, fabs(A[i][j] - B[i][j]));
        }
    }
    return e;
}

static void land(const VrWpView *v, const double P[3], double *nx, double *ny)
{
    const double n = 0.0625, f = 1000.0;
    const double r = f / (f - n), t = -n * f / (f - n);
    float Mg[4][4] = { { 1.42815f, 0, 0, 0 },
                       { 0, -1.90420f, 0, 0 },
                       { 0, 0, (float)r, (float)t },
                       { 0, 0, 1, 0 } };
    float out[4][4];
    VrWpDecomp d;
    double c[4];

    if (vr_wp_decompose(Mg, &d) != VR_WP_OK ||
        !vr_wp_rewrite(&d, v, true, out)) {
        *nx = *ny = NAN;
        return;
    }
    for (int i = 0; i < 4; i++) {
        c[i] = out[i][0] * P[0] + out[i][1] * P[1] + out[i][2] * P[2] +
               out[i][3];
    }
    *nx = c[0] / c[3];
    *ny = c[1] / c[3];
}

static void view_tan(VrWpView *v, double l, double r, double d, double u)
{
    v->fov = VR_WP_FOV_TANGENTS;
    v->tan_l = l;
    v->tan_r = r;
    v->tan_d = d;
    v->tan_u = u;
}

static void conversion(void)
{
    static const double I4[4] = { 0, 0, 0, 1 }, O3[3] = { 0, 0, 0 };
    static const double cases[][3] = {
        { 30, 0, 0 }, { -30, 0, 0 }, { 0, 20, 0 }, { 0, -20, 0 },
        { 0, 0, 15 }, { 0, 0, -15 }, { 35, -20, 12 }, { -120, 45, -30 },
    };
    char what[160];
    VrWpView v;
    double q[4], R[3][3], nx, ny;

    for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
        xr_head(cases[k][0], cases[k][1], cases[k][2], q);
        vr_wp_view_from_xr(I4, O3, q, O3, 1.0, &v);
        vr_wp_rot_ypr(cases[k][0], cases[k][1], cases[k][2], R);
        snprintf(what, sizeof(what), "conversion: runtime head yaw %g pitch "
                 "%g roll %g = vr_wp_rot_ypr of the same (%.1e)",
                 cases[k][0], cases[k][1], cases[k][2], rot_err(v.rot, R));
        expect(what, rot_err(v.rot, R) < 1e-12);
    }

    {
        double qn[4];
        xr_head(35, -20, 12, q);
        for (int i = 0; i < 4; i++) {
            qn[i] = 3.0 * q[i];
        }
        vr_wp_view_from_xr(I4, O3, qn, O3, 1.0, &v);
        vr_wp_rot_ypr(35, -20, 12, R);
        expect("conversion: a quaternion not of unit length is the same "
               "rotation", rot_err(v.rot, R) < 1e-12);
    }

    const double ahead[3] = { 0, 0, 10 };
    xr_head(30, 0, 0, q);
    vr_wp_view_from_xr(I4, O3, q, O3, 1.0, &v);
    view_tan(&v, -1, 1, -1, 1);
    land(&v, ahead, &nx, &ny);
    snprintf(what, sizeof(what), "screen: head 30 deg right: a point straight "
             "ahead lands left at tan(-30) (%.6f, %.6f)", nx, ny);
    expect(what, fabs(nx - tan(-30 * D2R)) < 1e-5 && fabs(ny) < 1e-5);

    xr_head(0, 20, 0, q);
    vr_wp_view_from_xr(I4, O3, q, O3, 1.0, &v);
    view_tan(&v, -1, 1, -1, 1);
    land(&v, ahead, &nx, &ny);
    snprintf(what, sizeof(what), "screen: head 20 deg up: it lands low at "
             "tan(-20) (%.6f, %.6f)", nx, ny);
    expect(what, fabs(nx) < 1e-5 && fabs(ny - tan(-20 * D2R)) < 1e-5);

    xr_head(0, 0, 90, q);
    vr_wp_view_from_xr(I4, O3, q, O3, 1.0, &v);
    view_tan(&v, -1, 1, -1, 1);
    {
        const double right[3] = { 1, 0, 10 };
        land(&v, right, &nx, &ny);
    }
    snprintf(what, sizeof(what), "screen: head rolled 90 deg clockwise: a "
             "point right of centre lands above it (%.6f, %.6f)", nx, ny);
    expect(what, fabs(nx) < 1e-5 && fabs(ny - 0.1) < 1e-5);

    {
        const double p[3] = { 1.0, 0.5, 0 };
        vr_wp_view_from_xr(I4, O3, I4, p, 1.0, &v);
        view_tan(&v, -1, 1, -1, 1);
        land(&v, ahead, &nx, &ny);
    }
    snprintf(what, sizeof(what), "screen: head 1 m right and 0.5 m up: the "
             "point lands left and low (%.6f, %.6f)", nx, ny);
    expect(what, fabs(nx + 0.1) < 1e-5 && fabs(ny + 0.05) < 1e-5);

    {
        const double p[3] = { 0, 0, -1.0 };
        const double off[3] = { 1, 0, 10 };
        vr_wp_view_from_xr(I4, O3, I4, p, 1.0, &v);
        view_tan(&v, -1, 1, -1, 1);
        land(&v, off, &nx, &ny);
    }
    snprintf(what, sizeof(what), "screen: head 1 m forward: a point 1 right "
             "at 10 lands at 1/9 (%.6f)", nx);
    expect(what, fabs(nx - 1.0 / 9.0) < 1e-5);

    {
        const double p[3] = { 3.048, 0, 0 };
        vr_wp_view_from_xr(I4, O3, I4, p, 1.0 / 3.048, &v);
    }
    snprintf(what, sizeof(what), "units: 3.048 m right at upm 1/3.048 is one "
             "game unit (%.9f)", v.pos[0]);
    expect(what, fabs(v.pos[0] - 1.0) < 1e-12 && fabs(v.pos[1]) < 1e-12 &&
                     fabs(v.pos[2]) < 1e-12);

    {
        double rq[4], rp[3] = { 0.3, 1.6, -0.2 }, hp[3], dq[4], d[3];
        xr_head(90, 0, 0, rq);
        vr_wp_view_from_xr(rq, rp, rq, rp, 1.0, &v);
        vr_wp_rot_ypr(0, 0, 0, R);
        expect("reference: head = reference is the identity pose",
               rot_err(v.rot, R) < 1e-12 && fabs(v.pos[0]) < 1e-12 &&
                   fabs(v.pos[2]) < 1e-12);
        xr_head(10, 0, 0, dq);
        qmul(rq, dq, q);
        d[0] = 0.5;
        d[1] = 0;
        d[2] = 0;
        vr_wp_quat_mat(rq, R);
        for (int i = 0; i < 3; i++) {
            hp[i] = rp[i] + R[i][0] * d[0] + R[i][1] * d[1] + R[i][2] * d[2];
        }
        vr_wp_view_from_xr(rq, rp, q, hp, 1.0, &v);
        vr_wp_rot_ypr(10, 0, 0, R);
        snprintf(what, sizeof(what), "reference: 10 more right and 0.5 m to "
                 "the reference's right (pos %.6f %.6f %.6f)", v.pos[0],
                 v.pos[1], v.pos[2]);
        expect(what, rot_err(v.rot, R) < 1e-12 &&
                         fabs(v.pos[0] - 0.5) < 1e-12 &&
                         fabs(v.pos[1]) < 1e-12 && fabs(v.pos[2]) < 1e-12);
    }

    vr_wp_view_from_xr(I4, O3, I4, O3, 1.0, &v);
    view_tan(&v, -1.39, 1.39, -1.19, 0.97);
    land(&v, ahead, &nx, &ny);
    snprintf(what, sizeof(what), "frustum: tangents l -1.39 r 1.39 d -1.19 u "
             "0.97: the axis lands at y = (1.19 - 0.97) / 2.16 (%.6f)", ny);
    expect(what, fabs(nx) < 1e-5 && fabs(ny - 0.22 / 2.16) < 1e-5);
}

static void game_tangents(void)
{
    static const double pq[][2] = { { 0, 0 }, { 0.12, -0.07 }, { -0.3, 0.2 } };
    char what[200];

    for (int up = 0; up < 2; up++) {
        for (size_t k = 0; k < sizeof(pq) / sizeof(pq[0]); k++) {
            const double a = 1.42815, b = up ? -1.90420 : 1.90420;
            const double p = pq[k][0], q = pq[k][1];
            const double n = 0.0625, f = 1000.0;
            const double r = f / (f - n), t = -n * f / (f - n);
            float Mg[4][4] = { { (float)a, 0, (float)p, 0 },
                               { 0, (float)b, (float)q, 0 },
                               { 0, 0, (float)r, (float)t },
                               { 0, 0, 1, 0 } };
            VrWpDecomp d;
            double tn[4], worst = 0;
            bool ok = vr_wp_decompose(Mg, &d) == VR_WP_OK;
            bool cyup = up;
            vr_wp_game_tangents(&d, cyup, tn);
            double s_up = (d.b < 0 ? -1 : 1) * (cyup ? 1 : -1);
            const double X[2] = { tn[0], tn[1] }, Y[2] = { tn[2], tn[3] };
            for (int i = 0; i < 2; i++) {
                for (int j = 0; j < 2; j++) {
                    double v[3] = { X[i] * 10, s_up * Y[j] * 10, 10 };
                    double cx = Mg[0][0] * v[0] + Mg[0][2] * v[2];
                    double cyv = Mg[1][1] * v[1] + Mg[1][2] * v[2];
                    double nx = cx / v[2], ny = cyv / v[2] * (cyup ? 1 : -1);
                    worst = fmax(worst, fabs(nx - (i ? 1 : -1)));
                    worst = fmax(worst, fabs(ny - (j ? 1 : -1)));
                }
            }
            snprintf(what, sizeof(what), "game frustum: p %.2f q %.2f clip-y-%s:"
                     " the tangent corners land on the image corners (%.1e)",
                     p, q, up ? "up" : "down", worst);
            expect(what, ok && worst < 1e-6 && tn[0] < tn[1] && tn[2] < tn[3]);
        }
    }
}

static void frustum(void)
{
    const double eL[4] = { -45 * D2R, 30 * D2R, 40 * D2R, -50 * D2R };
    const double eR[4] = { -30 * D2R, 45 * D2R, 42 * D2R, -48 * D2R };
    double t[4], a[4];
    char what[200];

    vr_wp_fov_union(eL, eR, t);
    snprintf(what, sizeof(what), "frustum: the union of two eyes is the "
             "widest each way (l %.4f r %.4f d %.4f u %.4f)", t[0], t[1], t[2],
             t[3]);
    expect(what, fabs(t[0] - tan(-45 * D2R)) < 1e-12 &&
                     fabs(t[1] - tan(45 * D2R)) < 1e-12 &&
                     fabs(t[2] - tan(-50 * D2R)) < 1e-12 &&
                     fabs(t[3] - tan(42 * D2R)) < 1e-12);
    vr_wp_fov_union(eR, eL, a);
    expect("frustum: the union does not depend on which eye comes first",
           !memcmp(a, t, sizeof(a)));
    vr_wp_fov_angles(t, a);
    snprintf(what, sizeof(what), "frustum: back to OpenXR's order left right "
             "UP DOWN (%.2f %.2f %.2f %.2f deg)", a[0] / D2R, a[1] / D2R,
             a[2] / D2R, a[3] / D2R);
    expect(what, fabs(a[0] + 45 * D2R) < 1e-12 && fabs(a[1] - 45 * D2R) < 1e-12 &&
                     fabs(a[2] - 42 * D2R) < 1e-12 &&
                     fabs(a[3] + 50 * D2R) < 1e-12);
}

enum { XA = 0x300000, XB = 0x400000, XC = 0x500000 };

static VrWpStamp pose(int frame)
{
    VrWpStamp s;
    memset(&s, 0, sizeof(s));
    s.display_time = frame * 16683333LL;
    s.declarable = true;
    s.xr_q[3] = 1;
    s.xr_p[0] = frame;
    return s;
}

static int declared(VrWpStampMachine *m, uint64_t key, VrWpStampSource *src)
{
    VrWpStamp s;
    VrWpStampSource got = vr_wp_stamp_present(m, key, &s);
    if (src) {
        *src = got;
    }
    return got == VR_WP_STAMP_NONE ? -1 : (int)s.xr_p[0];
}

static uint64_t buffer(int n)
{
    return n % 2 ? XB : XA;
}

static void stamps(void)
{
    VrWpStampMachine m;
    VrWpStampSource src;
    VrWpStamp s;
    bool ok;

    memset(&m, 0, sizeof(m));
    ok = true;
    for (int n = 1; n <= 120; n++) {
        VrWpStamp p = pose(n);
        ok &= vr_wp_stamp_latch(&m, &p);
        ok &= vr_wp_stamp_close(&m, buffer(n), true, true);
        ok &= declared(&m, buffer(n), &src) == n && src == VR_WP_STAMP_COPIED;
    }
    expect("stamps: 60 a second, each present declares its own frame", ok);

    memset(&m, 0, sizeof(m));
    ok = true;
    for (int n = 1; n <= 60; n++) {
        VrWpStamp p = pose(n);
        vr_wp_stamp_latch(&m, &p);
        vr_wp_stamp_close(&m, buffer(n), true, true);
        ok &= declared(&m, buffer(n), NULL) == n;
        ok &= declared(&m, buffer(n), &src) == n && src == VR_WP_STAMP_COPIED;
    }
    expect("stamps: 30 a second, the duplicate present declares the same "
           "stamp", ok);

    memset(&m, 0, sizeof(m));
    {
        VrWpStamp p1 = pose(1), p2 = pose(2);
        vr_wp_stamp_latch(&m, &p1);
        vr_wp_stamp_close(&m, XA, true, true);
        vr_wp_stamp_latch(&m, &p2);
        ok = m.latch_open && (int)m.latch.xr_p[0] == 2;
        ok &= declared(&m, XA, &src) == 1 && src == VR_WP_STAMP_COPIED;
        ok &= m.latch_open;
        vr_wp_stamp_close(&m, XB, true, true);
        ok &= declared(&m, XB, NULL) == 2;
    }
    expect("stamps: a present after the next latch declares the stamp, not "
           "the live latch", ok);

    memset(&m, 0, sizeof(m));
    ok = true;
    {
        VrWpStamp p1 = pose(1);
        vr_wp_stamp_latch(&m, &p1);
        vr_wp_stamp_close(&m, buffer(1), true, true);
        for (int n = 2; n <= 100; n++) {
            VrWpStamp p = pose(n);
            vr_wp_stamp_latch(&m, &p);
            ok &= declared(&m, buffer(n - 1), NULL) == n - 1;
            ok &= m.latch.seq != m.last_declared.seq;
            ok &= declared(&m, buffer(n - 1), NULL) == n - 1;
            vr_wp_stamp_close(&m, buffer(n), true, true);
        }
    }
    expect("stamps: a steady interleave never declares the live latch", ok);

    memset(&m, 0, sizeof(m));
    {
        VrWpStamp p1 = pose(1), p2 = pose(2);
        for (int n = 1; n <= 2; n++) {
            VrWpStamp p = n == 1 ? p1 : p2;
            vr_wp_stamp_latch(&m, &p);
            vr_wp_stamp_close(&m, n == 1 ? XA : XB, true, true);
            declared(&m, n == 1 ? XA : XB, NULL);
        }
        VrWpStamp p3 = pose(3);
        vr_wp_stamp_latch(&m, &p3);
        ok = !vr_wp_stamp_close(&m, XA, false, true);
        ok &= declared(&m, XA, &src) == 2 && src == VR_WP_STAMP_LAST;
    }
    expect("stamps: a draw-less frame forgets the key and re-declares the "
           "last stamp, not the stale one", ok);

    memset(&m, 0, sizeof(m));
    {
        VrWpStamp p1 = pose(1);
        vr_wp_stamp_latch(&m, &p1);
        vr_wp_stamp_close(&m, XA, true, true);
        declared(&m, XA, NULL);
        ok = !vr_wp_stamp_close(&m, XA, true, true);
        ok &= declared(&m, XA, &src) == 1 && src == VR_WP_STAMP_LAST;
    }
    expect("stamps: a close with no open latch writes nothing", ok);

    memset(&m, 0, sizeof(m));
    {
        VrWpStamp p1 = pose(1), p2 = pose(2);
        ok = vr_wp_stamp_latch(&m, &p1);
        ok &= !vr_wp_stamp_latch(&m, &p2) && (int)m.latch.xr_p[0] == 1;
        vr_wp_stamp_close(&m, XA, true, true);
        ok &= declared(&m, XA, NULL) == 1;
    }
    expect("stamps: a second latch before the close does not re-latch", ok);

    memset(&m, 0, sizeof(m));
    ok = !vr_wp_stamp_close(&m, XA, true, true);
    ok &= declared(&m, XA, &src) == -1 && src == VR_WP_STAMP_NONE;
    memset(&s, 0xff, sizeof(s));
    ok &= vr_wp_stamp_present(&m, XA, &s) == VR_WP_STAMP_NONE && s.seq == 0 &&
          !s.declarable && !s.reprojected;
    expect("stamps: nothing ever stamped declares nothing, and hands back an "
           "empty stamp", ok);

    memset(&m, 0, sizeof(m));
    {
        VrWpStamp p1 = pose(1);
        vr_wp_stamp_latch(&m, &p1);
        vr_wp_stamp_close(&m, XA, true, true);
        ok = declared(&m, 0, &src) == -1 && src == VR_WP_STAMP_NONE;
        declared(&m, XA, NULL);
        ok &= declared(&m, 0, &src) == 1 && src == VR_WP_STAMP_LAST;
    }
    expect("stamps: key 0 (an unknown surface) never matches an empty slot",
           ok);

    memset(&m, 0, sizeof(m));
    {
        VrWpStamp p1 = pose(1), p2 = pose(2);
        vr_wp_stamp_latch(&m, &p1);
        vr_wp_stamp_close(&m, XA, false, true);
        ok = !m.latch_open && vr_wp_stamp_latch(&m, &p2) &&
             (int)m.latch.xr_p[0] == 2 && m.latch.seq == 2;
    }
    expect("stamps: after a draw-less close the next frame latches its own "
           "pose", ok);

    memset(&m, 0, sizeof(m));
    {
        VrWpStamp p1 = pose(1), p2 = pose(2);
        vr_wp_stamp_latch(&m, &p1);
        vr_wp_stamp_close(&m, XA, true, true);
        vr_wp_stamp_latch(&m, &p2);
        vr_wp_stamp_close(&m, XA, true, true);
        ok = declared(&m, XA, &src) == 2 && src == VR_WP_STAMP_COPIED;
        vr_wp_stamp_latch(&m, &p1);
        vr_wp_stamp_close(&m, XB, true, true);
        ok &= declared(&m, XB, NULL) == 1 && declared(&m, XA, NULL) == 2;
    }
    expect("stamps: a surface drawn again holds its NEWEST frame; keys stay "
           "apart", ok);

    {
        VrWpStamp t = pose(1);
        t.reprojected = true;
        ok = vr_wp_stamp_is_projection(VR_WP_STAMP_COPIED, &t) &&
             vr_wp_stamp_is_projection(VR_WP_STAMP_LAST, &t) &&
             !vr_wp_stamp_is_projection(VR_WP_STAMP_NONE, &t);
        t.declarable = false;
        ok &= !vr_wp_stamp_is_projection(VR_WP_STAMP_COPIED, &t);
        t.declarable = true;
        t.reprojected = false;
        ok &= !vr_wp_stamp_is_projection(VR_WP_STAMP_COPIED, &t);
    }
    expect("layer: projection only for a declarable, re-projected stamp that "
           "exists", ok);

    memset(&m, 0, sizeof(m));
    {
        VrWpStamp p1 = pose(1);
        vr_wp_stamp_latch(&m, &p1);
        vr_wp_stamp_close(&m, XA, true, false);
        vr_wp_stamp_present(&m, XA, &s);
        ok = s.declarable && !s.reprojected;
        VrWpStamp p2 = pose(2);
        vr_wp_stamp_latch(&m, &p2);
        vr_wp_stamp_close(&m, XB, true, true);
        vr_wp_stamp_present(&m, XB, &s);
        ok &= s.reprojected;
    }
    expect("stamps: whether the frame was re-projected is what its close "
           "said", ok);

    memset(&m, 0, sizeof(m));
    for (int n = 1; n <= VR_WP_STAMP_SLOTS + 1; n++) {
        VrWpStamp p = pose(n);
        vr_wp_stamp_latch(&m, &p);
        vr_wp_stamp_close(&m, 0x100000u * n, true, true);
    }
    ok = declared(&m, 0x100000u * 2, &src) == 2 && src == VR_WP_STAMP_COPIED;
    ok &= declared(&m, 0x100000u * (VR_WP_STAMP_SLOTS + 1), NULL) ==
          VR_WP_STAMP_SLOTS + 1;
    ok &= declared(&m, 0x100000u * 1, &src) == VR_WP_STAMP_SLOTS + 1 &&
          src == VR_WP_STAMP_LAST;
    expect("stamps: a full table evicts the OLDEST stamp", ok);

    memset(&m, 0, sizeof(m));
    for (int n = 1; n <= VR_WP_STAMP_SLOTS; n++) {
        VrWpStamp p = pose(n);
        vr_wp_stamp_latch(&m, &p);
        vr_wp_stamp_close(&m, 0x100000u * n, true, true);
    }
    {
        VrWpStamp p = pose(20), q = pose(21);
        vr_wp_stamp_latch(&m, &p);
        vr_wp_stamp_close(&m, 0x100000u * 1, true, true);
        vr_wp_stamp_latch(&m, &q);
        vr_wp_stamp_close(&m, 0x900000u, true, true);
    }
    ok = declared(&m, 0x100000u * 1, &src) == 20 && src == VR_WP_STAMP_COPIED;
    ok &= declared(&m, 0x100000u * 2, &src) == 20 && src == VR_WP_STAMP_LAST;
    ok &= declared(&m, 0x900000u, NULL) == 21;
    expect("stamps: the evicted stamp is the oldest wherever it sits", ok);

    memset(&m, 0, sizeof(m));
    {
        VrWpStamp p1 = pose(1);
        vr_wp_stamp_latch(&m, &p1);
        vr_wp_stamp_close(&m, XA, true, true);
        declared(&m, XA, NULL);
        vr_wp_stamp_invalidate(&m);
        ok = declared(&m, XA, &src) == -1 && src == VR_WP_STAMP_NONE;
        VrWpStamp p2 = pose(2);
        vr_wp_stamp_latch(&m, &p2);
        ok &= m.latch.seq == 2;
        vr_wp_stamp_close(&m, XC, true, true);
        ok &= declared(&m, XC, NULL) == 2;
    }
    expect("stamps: invalidate forgets every stamp; the sequence goes on",
           ok);

    memset(&m, 0, sizeof(m));
    {
        VrWpStamp p1 = pose(1), q;
        vr_wp_stamp_latch(&m, &p1);
        vr_wp_stamp_close(&m, XA, true, true);
        vr_wp_stamp_present(&m, XA, &q);
        ok = vr_wp_stamp_fresh(&m, &q, 3);
        for (int n = 2; n <= 4; n++) {
            VrWpStamp p = pose(n);
            vr_wp_stamp_latch(&m, &p);
            vr_wp_stamp_close(&m, XB, true, true);
        }
        ok &= vr_wp_stamp_fresh(&m, &q, 3);
        VrWpStamp p5 = pose(5);
        vr_wp_stamp_latch(&m, &p5);
        vr_wp_stamp_close(&m, XB, true, true);
        ok &= !vr_wp_stamp_fresh(&m, &q, 3);
        memset(&q, 0, sizeof(q));
        ok &= !vr_wp_stamp_fresh(&m, &q, 3);
        memset(&m, 0, sizeof(m));
        VrWpStamp p6 = pose(6);
        vr_wp_stamp_latch(&m, &p6);
        ok &= !vr_wp_stamp_fresh(&m, &q, 3);
    }
    expect("stamps: a stamp is fresh up to max_age flips, then stale; an "
           "empty one never", ok);
}

int main(void)
{
    conversion();
    game_tangents();
    frustum();
    stamps();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
