/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#ifndef TESTS_XBOX_WORLDPROJ_PINS_H
#define TESTS_XBOX_WORLDPROJ_PINS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Pin {
    char name[96];
    float mg[4][4];
    double me[16];
    bool have_me;
    VrWpView view;
    double ypr[3];
    bool clip_y_up;
    char verdict[32];
    char fov_text[128];
    bool have_mo;
    float mo[4][4];
    char cls[16];
    double depth, scale;
} Pin;

static inline int pins_load(const char *path, Pin *pins, int max)
{
    FILE *f = fopen(path, "r");
    char line[2048];
    Pin p;
    int n = 0, open_pin = 0;

    if (!f) {
        return -1;
    }
    while (fgets(line, sizeof(line), f)) {
        char *s = line;
        if (!strncmp(s, "pin ", 4)) {
            memset(&p, 0, sizeof(p));
            vr_wp_view_identity(&p.view);
            p.clip_y_up = true;
            sscanf(s + 4, "%95s", p.name);
            open_pin = 1;
        } else if (!open_pin) {
            continue;
        } else if (!strncmp(s, "mg ", 3)) {
            s += 3;
            for (int i = 0; i < 16; i++) {
                uint32_t w = (uint32_t)strtoul(s, &s, 16);
                memcpy(&p.mg[i / 4][i % 4], &w, 4);
            }
        } else if (!strncmp(s, "mo ", 3)) {
            s += 3;
            for (int i = 0; i < 16; i++) {
                uint32_t w = (uint32_t)strtoul(s, &s, 16);
                memcpy(&p.mo[i / 4][i % 4], &w, 4);
            }
            p.have_mo = true;
        } else if (!strncmp(s, "cls ", 4)) {
            sscanf(s + 4, "%15s", p.cls);
        } else if (!strncmp(s, "plane ", 6)) {
            sscanf(s + 6, "%lf %lf", &p.depth, &p.scale);
        } else if (!strncmp(s, "me ", 3)) {
            s += 3;
            for (int i = 0; i < 16; i++) {
                p.me[i] = strtod(s, &s);
            }
            p.have_me = true;
        } else if (!strncmp(s, "fov ", 4)) {
            size_t len = strcspn(s + 4, "\r\n");
            snprintf(p.fov_text, sizeof(p.fov_text), "%.*s", (int)len, s + 4);
            if (!strncmp(s, "fov tan ", 8)) {
                p.view.fov = VR_WP_FOV_TANGENTS;
                sscanf(s + 8, "%lf %lf %lf %lf", &p.view.tan_l, &p.view.tan_r,
                       &p.view.tan_d, &p.view.tan_u);
            } else if (!strncmp(s, "fov shear ", 10)) {
                sscanf(s + 10, "%lf", &p.view.shear_p);
            }
        } else if (!strncmp(s, "ypr ", 4)) {
            sscanf(s + 4, "%lf %lf %lf", &p.ypr[0], &p.ypr[1], &p.ypr[2]);
        } else if (!strncmp(s, "pos ", 4)) {
            sscanf(s + 4, "%lf %lf %lf", &p.view.pos[0], &p.view.pos[1],
                   &p.view.pos[2]);
        } else if (!strncmp(s, "cyup ", 5)) {
            p.clip_y_up = atoi(s + 5) != 0;
        } else if (!strncmp(s, "verdict ", 8)) {
            sscanf(s + 8, "%31s", p.verdict);
        } else if (!strncmp(s, "end", 3)) {
            if (n < max) {
                pins[n++] = p;
            }
            open_pin = 0;
        }
    }
    fclose(f);
    return n;
}

#endif
