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
static uint32_t guest[NV2A_VERTEXSHADER_CONSTANTS][4];
static float copy[NV2A_VERTEXSHADER_CONSTANTS][4];
static int fails, checks;

static void guest_upload(int group, const float m[4][4])
{
    memcpy(pg.vsh_constants[group], m, 64);
    memcpy(guest[group], m, 64);
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
}

static void expect(const char *what, bool ok)
{
    checks++;
    ok = ok && !memcmp(pg.vsh_constants, guest, sizeof(guest));
    fails += !ok;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static bool copy_is(const double me[16])
{
    for (int i = 0; i < 4; i++) {
        double big = 0;
        for (int j = 0; j < 4; j++) {
            big = fmax(big, fabs(me[i * 4 + j]));
        }
        for (int j = 0; j < 4; j++) {
            if (!(fabs((double)copy[i][j] - me[i * 4 + j]) <=
                  2.0 * FLT_EPSILON * fmax(big, 1e-30))) {
                return false;
            }
        }
    }
    return true;
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
    if (argc < 2 || getenv("XEMU_VR_WORLDPROJ")) {
        printf("usage: %s PINS.txt   (with XEMU_VR_WORLDPROJ unset)\n", argv[0]);
        return 2;
    }
    int n = pins_load(argv[1], pins, 1024);
    const Pin *yaw = find(pins, n, "syn1.yaw+5");
    const Pin *pitch = find(pins, n, "syn1.pitch+5");
    const Pin *other = find(pins, n, "syn0.yaw+5");
    VrWpView vy = yaw->view, vp = pitch->view;
    vr_wp_rot_ypr(yaw->ypr[0], yaw->ypr[1], yaw->ypr[2], vy.rot);
    vr_wp_rot_ypr(pitch->ypr[0], pitch->ypr[1], pitch->ypr[2], vp.rot);

    guest_upload(0, yaw->mg);
    flip();
    draw();
    expect("nothing set: the guest's rows", !memcmp(copy, guest, 64));
    expect("nothing set: a flip marks nothing",
           !pg.vsh_constants_dirty[0] && !pg.vsh_constants_dirty[3]);

    vr_worldproj_set_view(&vy, true);
    draw();
    expect("set_view takes effect at the FLIP, not before",
           !memcmp(copy, guest, 64));
    flip();
    expect("the flip after set_view marks the group dirty",
           pg.vsh_constants_dirty[0] && pg.vsh_constants_dirty[3]);
    draw();
    expect("on: rewritten rows", copy_is(yaw->me));

    vr_worldproj_set_view(&vp, true);
    flip();
    draw();
    expect("a new pose each frame: rows for the new pose", copy_is(pitch->me));

    vr_worldproj_set_view(&vp, false);
    flip();
    guest_upload(0, other->mg);
    draw();
    expect("set_view(a view, OFF): the guest's rows, whatever the view",
           !vr_worldproj_active() && !memcmp(copy, guest, 64));
    vr_worldproj_set_view(&vy, true);
    flip();
    draw();
    expect("on again, the camera changed while off: rewritten from the rows "
           "the file holds now", copy_is(other->me));

    guest_upload(0, yaw->mg);
    draw();
    expect("the constant file replaced under the module (a loaded save "
           "state): the next draw follows it", copy_is(yaw->me));

    {
        VrWpStamp st;
        flip();
        draw();
        flip();
        vr_worldproj_present(0x3c0000);
        bool before = vr_worldproj_presented(&st) == VR_WP_STAMP_COPIED &&
                      st.reprojected;
        vr_worldproj_set_view(NULL, false);
        flip();
        draw();
        flip();
        draw();
        flip();
        vr_worldproj_present(0x3c0000);
        VrWpStampSource src = vr_worldproj_presented(&st);
        expect("set_view(OFF): a flat frame is never presented with a "
               "re-projected stamp (review F1)",
               before && !(src != VR_WP_STAMP_NONE && st.reprojected));
    }

    printf("worldproj-setview-test: %s -- %d checks, %d failed\n",
           fails ? "FAIL" : "PASS", checks, fails);
    return fails != 0;
}
