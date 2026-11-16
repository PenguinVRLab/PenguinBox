/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../hw/xbox/nv2a/pgraph/vr_worldproj_math.h"
#include "pins.h"

static const char *verdict_name(VrWpVerdict v)
{
    switch (v) {
    case VR_WP_OK: return "ok";
    case VR_WP_NO_PERSPECTIVE: return "no_perspective";
    case VR_WP_DEGENERATE: return "degenerate";
    default: return "not_a_camera";
    }
}

static const char *cls_name(VrWp2d c)
{
    return c == VR_WP_2D_DEPTH ? "depth" : c == VR_WP_2D_FLAT ? "flat" : "none";
}

static int compare(const Pin *p, float out[4][4], double *worst)
{
    int bad = 0;
    for (int i = 0; i < 4; i++) {
        double big = 0;
        for (int j = 0; j < 4; j++) {
            big = fmax(big, fabs(p->me[i * 4 + j]));
        }
        double tol = 2.0 * FLT_EPSILON * fmax(big, 1e-30);
        for (int j = 0; j < 4; j++) {
            double e = fabs((double)out[i][j] - p->me[i * 4 + j]);
            *worst = fmax(*worst, big > 0 ? e / big : 0);
            if (!(e <= tol)) {
                if (!bad) {
                    printf("FAIL %s: row %d col %d is %.9g, the pin says "
                           "%.9g\n", p->name, i, j, out[i][j],
                           p->me[i * 4 + j]);
                }
                bad = 1;
            }
        }
    }
    return bad;
}

static int check_2d(const Pin *p, double *worst)
{
    VrWpDecomp d;
    VrWpView v = p->view;
    VrWp2d cls = vr_wp_2d_class(p->mo);
    float out[4][4];

    if (strcmp(cls_name(cls), p->cls)) {
        printf("FAIL %s: 2D class %s, the pin says %s\n", p->name,
               cls_name(cls), p->cls);
        return 1;
    }
    if (cls == VR_WP_2D_NONE) {
        return 0;
    }
    if (vr_wp_decompose(p->mg, &d) != VR_WP_OK) {
        printf("FAIL %s: the camera does not decompose\n", p->name);
        return 1;
    }
    vr_wp_rot_ypr(p->ypr[0], p->ypr[1], p->ypr[2], v.rot);
    if (!vr_wp_rewrite_2d(&d, &v, p->clip_y_up, p->mo, cls, p->depth,
                          p->scale, out)) {
        printf("FAIL %s: the 2D result is not finite\n", p->name);
        return 1;
    }
    return compare(p, out, worst);
}

static int check(const Pin *p, double *worst)
{
    VrWpDecomp d;
    VrWpView v = p->view;
    VrWpVerdict got = vr_wp_decompose(p->mg, &d);

    if (strcmp(verdict_name(got), p->verdict)) {
        printf("FAIL %s: verdict %s, the pin says %s\n", p->name,
               verdict_name(got), p->verdict);
        return 1;
    }
    if (got != VR_WP_OK) {
        return 0;
    }
    vr_wp_rot_ypr(p->ypr[0], p->ypr[1], p->ypr[2], v.rot);

    float out[4][4];
    if (!vr_wp_rewrite(&d, &v, p->clip_y_up, out)) {
        printf("FAIL %s: the result is not finite\n", p->name);
        return 1;
    }
    return compare(p, out, worst);
}

static int run_file(const char *path, int *npins, double *worst)
{
    static Pin pins[1024];
    int n = pins_load(path, pins, 1024), fails = 0;

    if (n < 0) {
        printf("FAIL cannot read %s\n", path);
        return 1;
    }
    for (int i = 0; i < n; i++) {
        if (pins[i].have_mo) {
            if (strcmp(pins[i].cls, "none") && !pins[i].have_me) {
                printf("FAIL %s: a 2D pin with no expected rows\n",
                       pins[i].name);
                fails++;
            } else {
                fails += check_2d(&pins[i], worst);
            }
        } else if (!strcmp(pins[i].verdict, "ok") && !pins[i].have_me) {
            printf("FAIL %s: an ok pin with no expected rows\n", pins[i].name);
            fails++;
        } else {
            fails += check(&pins[i], worst);
        }
    }
    *npins += n;
    return fails;
}

int main(int argc, char **argv)
{
    int fails = 0, npins = 0;
    double worst = 0;

    if (argc < 2) {
        printf("usage: %s PINS.txt [PINS.txt ...]\n", argv[0]);
        return 2;
    }
    for (int i = 1; i < argc; i++) {
        fails += run_file(argv[i], &npins, &worst);
    }
    if (!npins) {
        printf("FAIL no pins read\n");
        return 1;
    }
    printf("worldproj-math-test: %s -- %d pins, %d failed, worst relative "
           "difference %.2e\n", fails ? "FAIL" : "PASS", npins, fails, worst);
    return fails != 0;
}
