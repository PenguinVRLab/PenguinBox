/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VR_WORLDPROJ_MATH_H
#define HW_XBOX_NV2A_PGRAPH_VR_WORLDPROJ_MATH_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef enum VrWpFov {
    VR_WP_FOV_GAME,
    VR_WP_FOV_TANGENTS,
} VrWpFov;

typedef struct VrWpView {
    VrWpFov fov;
    double tan_l, tan_r, tan_d, tan_u;
    double shear_p;
    double rot[3][3];
    double pos[3];
} VrWpView;

typedef struct VrWpDecomp {
    double a, b, p, q, r, t;
    double s;
    double A[4][4];
    double resid;
} VrWpDecomp;

typedef enum VrWpVerdict {
    VR_WP_OK,
    VR_WP_NO_PERSPECTIVE,
    VR_WP_DEGENERATE,
    VR_WP_NOT_A_CAMERA,
} VrWpVerdict;

#define VR_WP_RESID_MAX 1e-3

static inline double vr_wp_dot3(const double *x, const double *y)
{
    return x[0] * y[0] + x[1] * y[1] + x[2] * y[2];
}

static inline void vr_wp_rot_ypr(double yaw, double pitch, double roll,
                                 double R[3][3])
{
    const double d2r = 3.14159265358979323846 / 180.0;
    double cy = cos(yaw * d2r), sy = sin(yaw * d2r);
    double cp = cos(-pitch * d2r), sp = sin(-pitch * d2r);
    double cr = cos(-roll * d2r), sr = sin(-roll * d2r);
    double Ry[3][3] = { { cy, 0, sy }, { 0, 1, 0 }, { -sy, 0, cy } };
    double Rx[3][3] = { { 1, 0, 0 }, { 0, cp, -sp }, { 0, sp, cp } };
    double Rz[3][3] = { { cr, -sr, 0 }, { sr, cr, 0 }, { 0, 0, 1 } };
    double T[3][3];

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            T[i][j] = Ry[i][0] * Rx[0][j] + Ry[i][1] * Rx[1][j] +
                      Ry[i][2] * Rx[2][j];
        }
    }
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            R[i][j] = T[i][0] * Rz[0][j] + T[i][1] * Rz[1][j] +
                      T[i][2] * Rz[2][j];
        }
    }
}

static inline void vr_wp_view_identity(VrWpView *v)
{
    *v = (VrWpView){ .fov = VR_WP_FOV_GAME };
    v->rot[0][0] = v->rot[1][1] = v->rot[2][2] = 1.0;
}

static inline void vr_wp_quat_mat(const double q[4], double R[3][3])
{
    double x = q[0], y = q[1], z = q[2], w = q[3];
    double n = x * x + y * y + z * z + w * w;
    double s = n > 0 ? 2.0 / n : 0;

    R[0][0] = 1 - s * (y * y + z * z);
    R[0][1] = s * (x * y - z * w);
    R[0][2] = s * (x * z + y * w);
    R[1][0] = s * (x * y + z * w);
    R[1][1] = 1 - s * (x * x + z * z);
    R[1][2] = s * (y * z - x * w);
    R[2][0] = s * (x * z - y * w);
    R[2][1] = s * (y * z + x * w);
    R[2][2] = 1 - s * (x * x + y * y);
}

static inline void vr_wp_view_from_xr(const double ref_q[4],
                                      const double ref_p[3],
                                      const double q[4], const double p[3],
                                      double upm, VrWpView *v)
{
    static const double F[3] = { 1.0, 1.0, -1.0 };
    double Rr[3][3], Rh[3][3], d[3];

    vr_wp_quat_mat(ref_q, Rr);
    vr_wp_quat_mat(q, Rh);
    for (int i = 0; i < 3; i++) {
        d[i] = p[i] - ref_p[i];
    }
    for (int i = 0; i < 3; i++) {
        double rel_p = 0;
        for (int j = 0; j < 3; j++) {
            double rel = 0;
            for (int k = 0; k < 3; k++) {
                rel += Rr[k][i] * Rh[k][j];
            }
            v->rot[i][j] = F[i] * F[j] * rel;
            rel_p += Rr[j][i] * d[j];
        }
        v->pos[i] = upm * F[i] * rel_p;
    }
}

static inline void vr_wp_fov_union(const double e0[4], const double e1[4],
                                   double tan_out[4])
{
    tan_out[0] = fmin(tan(e0[0]), tan(e1[0]));
    tan_out[1] = fmax(tan(e0[1]), tan(e1[1]));
    tan_out[2] = fmin(tan(e0[3]), tan(e1[3]));
    tan_out[3] = fmax(tan(e0[2]), tan(e1[2]));
}

static inline void vr_wp_fov_angles(const double t[4], double xr_fov[4])
{
    xr_fov[0] = atan(t[0]);
    xr_fov[1] = atan(t[1]);
    xr_fov[2] = atan(t[3]);
    xr_fov[3] = atan(t[2]);
}

static inline void vr_wp_game_tangents(const VrWpDecomp *d, bool clip_y_up,
                                       double t[4])
{
    double cy = clip_y_up ? 1.0 : -1.0;
    double ab = fabs(d->b);

    t[0] = (-1.0 - d->p) / d->a;
    t[1] = (1.0 - d->p) / d->a;
    t[2] = (-1.0 - cy * d->q) / ab;
    t[3] = (1.0 - cy * d->q) / ab;
}

static inline VrWpVerdict vr_wp_decompose(const float m[4][4], VrWpDecomp *d)
{
    double M[4][4];

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            M[i][j] = m[i][j];
            if (!isfinite(M[i][j])) {
                return VR_WP_DEGENERATE;
            }
        }
    }
    const double *f = M[3];
    double s2 = vr_wp_dot3(f, f);
    double scale2 = 0;
    for (int i = 0; i < 4; i++) {
        scale2 += vr_wp_dot3(M[i], M[i]);
    }
    if (!(s2 > 1e-12 * scale2) || !(s2 > 0)) {
        return VR_WP_NO_PERSPECTIVE;
    }
    d->s = sqrt(s2);
    d->p = vr_wp_dot3(M[0], f) / s2;
    d->q = vr_wp_dot3(M[1], f) / s2;
    d->r = vr_wp_dot3(M[2], f) / s2;
    d->t = M[2][3] - d->r * M[3][3];

    double x[3], y[3], zres[3];
    for (int j = 0; j < 3; j++) {
        x[j] = M[0][j] - d->p * f[j];
        y[j] = M[1][j] - d->q * f[j];
        zres[j] = M[2][j] - d->r * f[j];
    }
    d->a = sqrt(vr_wp_dot3(x, x)) / d->s;
    d->b = sqrt(vr_wp_dot3(y, y)) / d->s;
    if (!(d->a > 1e-6) || !(d->b > 1e-6) ||
        !(fabs(d->t) > 1e-6 * (fabs(M[2][3]) + fabs(d->r * M[3][3])))) {
        return VR_WP_DEGENERATE;
    }

    double c[3] = { x[1] * y[2] - x[2] * y[1], x[2] * y[0] - x[0] * y[2],
                    x[0] * y[1] - x[1] * y[0] };
    if (vr_wp_dot3(c, f) < 0) {
        d->b = -d->b;
    }

    for (int j = 0; j < 4; j++) {
        d->A[0][j] = (M[0][j] - d->p * M[3][j]) / d->a;
        d->A[1][j] = (M[1][j] - d->q * M[3][j]) / d->b;
        d->A[2][j] = M[3][j];
        d->A[3][j] = (M[2][j] - d->r * M[3][j]) / d->t;
    }

    double r_xy = fabs(vr_wp_dot3(d->A[0], d->A[1])) / s2;
    double m2 = vr_wp_dot3(M[2], M[2]);
    double r_z = m2 > 0 ? sqrt(vr_wp_dot3(zres, zres) / m2) : 0;
    d->resid = r_xy > r_z ? r_xy : r_z;
    if (!(d->resid < VR_WP_RESID_MAX)) {
        return VR_WP_NOT_A_CAMERA;
    }
    return VR_WP_OK;
}

static inline bool vr_wp_rewrite_a(const VrWpDecomp *d, const VrWpView *v,
                                   bool clip_y_up, const double A[4][4],
                                   float out[4][4])
{
    double Av[4][4];
    double R[3][3];
    double T[3];
    double tv[3];
    double cy = clip_y_up ? 1.0 : -1.0;
    double bs = d->b < 0 ? -1.0 : 1.0;
    double f[3] = { 1.0, cy * bs, 1.0 };

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            R[i][j] = f[i] * f[j] * v->rot[i][j];
        }
        T[i] = d->s * f[i] * v->pos[i];
    }
    for (int i = 0; i < 3; i++) {
        tv[i] = R[0][i] * T[0] + R[1][i] * T[1] + R[2][i] * T[2];
    }
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            Av[i][j] = R[0][i] * A[0][j] + R[1][i] * A[1][j] +
                       R[2][i] * A[2][j] - tv[i] * A[3][j];
        }
    }
    for (int j = 0; j < 4; j++) {
        Av[3][j] = A[3][j];
    }

    double a, b, p, q;
    if (v->fov == VR_WP_FOV_TANGENTS) {
        a = 2.0 / (v->tan_r - v->tan_l);
        p = -(v->tan_r + v->tan_l) / (v->tan_r - v->tan_l);
        b = bs * 2.0 / (v->tan_u - v->tan_d);
        q = -cy * (v->tan_u + v->tan_d) / (v->tan_u - v->tan_d);
    } else {
        a = d->a;
        b = d->b;
        p = d->p + v->shear_p;
        q = d->q;
    }
    for (int j = 0; j < 4; j++) {
        out[0][j] = (float)(a * Av[0][j] + p * Av[2][j]);
        out[1][j] = (float)(b * Av[1][j] + q * Av[2][j]);
        out[2][j] = (float)(d->r * Av[2][j] + d->t * Av[3][j]);
        out[3][j] = (float)Av[2][j];
    }
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (!isfinite(out[i][j])) {
                return false;
            }
        }
    }
    return true;
}

static inline bool vr_wp_rewrite(const VrWpDecomp *d, const VrWpView *v,
                                 bool clip_y_up, float out[4][4])
{
    return vr_wp_rewrite_a(d, v, clip_y_up, d->A, out);
}

typedef enum VrWp2d { VR_WP_2D_NONE, VR_WP_2D_DEPTH, VR_WP_2D_FLAT } VrWp2d;

static inline VrWp2d vr_wp_2d_class(const float m[4][4])
{
    if (!(m[3][0] == 0 && m[3][1] == 0 && m[3][2] == 0 && m[3][3] == 1)) {
        return VR_WP_2D_NONE;
    }
    if (!(m[0][1] == 0 && m[0][2] == 0 && m[0][0] != 0) ||
        !(m[1][0] == 0 && m[1][2] == 0 && m[1][1] != 0)) {
        return VR_WP_2D_NONE;
    }
    if (!(m[2][0] == 0 && m[2][1] == 0)) {
        return VR_WP_2D_NONE;
    }
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (!isfinite(m[i][j])) {
                return VR_WP_2D_NONE;
            }
        }
    }
    return m[2][2] != 0 ? VR_WP_2D_DEPTH : VR_WP_2D_FLAT;
}

static inline bool vr_wp_rewrite_2d(const VrWpDecomp *d, const VrWpView *v,
                                    bool clip_y_up, const float m[4][4],
                                    VrWp2d cls, double depth, double scale,
                                    float out[4][4])
{
    double G[4][4], A[4][4];

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            G[i][j] = m[i][j];
        }
    }
    if (cls == VR_WP_2D_FLAT) {
        double zc = d->r + d->t / depth;
        for (int j = 0; j < 4; j++) {
            G[0][j] = scale * G[0][j] + (1.0 - scale) * d->p * G[3][j];
            G[1][j] = scale * G[1][j] + (1.0 - scale) * d->q * G[3][j];
            G[2][j] = zc * G[3][j];
        }
    }
    for (int j = 0; j < 4; j++) {
        A[0][j] = (G[0][j] - d->p * G[3][j]) / d->a;
        A[1][j] = (G[1][j] - d->q * G[3][j]) / d->b;
        A[2][j] = G[3][j];
        A[3][j] = (G[2][j] - d->r * G[3][j]) / d->t;
    }
    return vr_wp_rewrite_a(d, v, clip_y_up, A, out);
}

static inline void vr_wp_axes_from_rows(const double r[3], const double f[3],
                                        double ax[3][3])
{
    double nr = sqrt(vr_wp_dot3(r, r)), nf = sqrt(vr_wp_dot3(f, f));
    double R[3] = { r[0] / nr, r[1] / nr, r[2] / nr };
    double F[3] = { f[0] / nf, f[1] / nf, f[2] / nf };
    double U[3] = { R[1] * F[2] - R[2] * F[1], R[2] * F[0] - R[0] * F[2],
                    R[0] * F[1] - R[1] * F[0] };
    for (int i = 0; i < 3; i++) {
        ax[i][0] = R[i];
        ax[i][1] = U[i];
        ax[i][2] = F[i];
    }
}

static inline void vr_wp_slave_view(const VrWpDecomp *d, const VrWpView *v,
                                    double body_yaw, double body_pitch,
                                    VrWpView *out)
{
    double C[3][3], B[3][3], E[3][3], M[3][3];

    vr_wp_axes_from_rows(d->A[0], d->A[2], C);
    if (!isfinite(body_pitch)) {
        body_pitch = asin(fmax(-1.0, fmin(1.0, C[2][2])));
    }
    double cy = cos(body_yaw), sy = sin(body_yaw);
    double cp = cos(body_pitch), sp = sin(body_pitch);
    double bf[3] = { cp * cy, cp * sy, sp }, br[3] = { sy, -cy, 0.0 };

    vr_wp_axes_from_rows(br, bf, B);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            E[i][j] = B[i][0] * v->rot[0][j] + B[i][1] * v->rot[1][j] +
                      B[i][2] * v->rot[2][j];
        }
    }
    *out = *v;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            out->rot[i][j] = C[0][i] * E[0][j] + C[1][i] * E[1][j] +
                             C[2][i] * E[2][j];
            M[i][j] = C[0][i] * B[0][j] + C[1][i] * B[1][j] +
                      C[2][i] * B[2][j];
        }
    }
    for (int i = 0; i < 3; i++) {
        out->pos[i] = M[i][0] * v->pos[0] + M[i][1] * v->pos[1] +
                      M[i][2] * v->pos[2];
    }
}

static inline double vr_wp_hull_area(const double (*pt)[2], int n)
{
    double s[64][2], h[130][2], area = 0;
    int k = 0;

    if (n < 3 || n > 64) {
        return 0;
    }
    memcpy(s, pt, sizeof(double) * 2 * n);
    for (int i = 1; i < n; i++) {
        double x = s[i][0], y = s[i][1];
        int j = i - 1;
        while (j >= 0 && (s[j][0] > x || (s[j][0] == x && s[j][1] > y))) {
            s[j + 1][0] = s[j][0];
            s[j + 1][1] = s[j][1];
            j--;
        }
        s[j + 1][0] = x;
        s[j + 1][1] = y;
    }
    for (int pass = 0; pass < 2; pass++) {
        int start = k;
        for (int c = 0; c < n; c++) {
            int i = pass ? n - 1 - c : c;
            while (k >= start + 2 &&
                   (h[k - 1][0] - h[k - 2][0]) * (s[i][1] - h[k - 2][1]) -
                   (h[k - 1][1] - h[k - 2][1]) * (s[i][0] - h[k - 2][0]) <= 0) {
                k--;
            }
            h[k][0] = s[i][0];
            h[k][1] = s[i][1];
            k++;
        }
        k--;
    }
    for (int i = 0; i < k; i++) {
        int j = (i + 1) % k;
        area += h[i][0] * h[j][1] - h[j][0] * h[i][1];
    }
    return fabs(area) / 2;
}

static inline uint32_t vr_wp_scale_count(uint64_t acc, uint32_t divisor,
                                         double scale, double cap)
{
    double v = (double)acc * scale / (double)divisor;
    if (cap > 0 && v > cap) {
        v = cap;
    }
    if (!(v < 4294967295.0)) {
        return v > 0 ? UINT32_MAX : 0;
    }
    return v > 0 ? (uint32_t)(v + 0.5) : 0;
}

#endif
