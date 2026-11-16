/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/nv2a_int.h"
#include "../../../hw/xbox/nv2a/pgraph/vr_worldproj.h"
#include "../../../hw/xbox/nv2a/pgraph/vr_worldproj_host.h"
#include "fake_host.h"

bool fake_view_projection;
int64_t fake_title_id = -1;
int fake_requests, fake_forgets;
bool fake_mod_armed;
bool fake_view_armed;
char fake_mod_asked[64];

bool vr_wp_host_view_projection(void)
{
    return fake_view_projection;
}

void vr_wp_host_title_request(bool forget)
{
    fake_requests++;
    if (forget) {
        fake_forgets++;
        fake_title_id = -1;
    }
}

int64_t vr_wp_host_title_id(void)
{
    return fake_title_id;
}

bool vr_wp_host_mod_armed(const char *slug)
{
    if (!strcmp(slug, "pgr2-cockpit-view")) {
        return fake_view_armed;
    }
    snprintf(fake_mod_asked, sizeof(fake_mod_asked), "%s", slug);
    return fake_mod_armed;
}

int fake_rows_outside(const uint32_t file[][4], const float copy[][4])
{
    int rows[VR_WORLDPROJ_MAX_ROWS];
    int n = vr_worldproj_renderer_rows(rows), bad = 0;

    for (int r = 0; r < NV2A_VERTEXSHADER_CONSTANTS; r++) {
        bool named = false;

        if (!memcmp(file[r], copy[r], sizeof(copy[r]))) {
            continue;
        }
        for (int k = 0; k < n; k++) {
            named = named || rows[k] == r;
        }
        if (!named) {
            printf("FAIL the rewrite changed row %d, which "
                   "vr_worldproj_renderer_rows does not name (%d named)\n",
                   r, n);
            bad++;
        }
    }
    return bad;
}
