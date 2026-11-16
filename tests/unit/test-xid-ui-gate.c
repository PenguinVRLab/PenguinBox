/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../hw/xbox/xid-ui-gate.h"

static int g_fail, g_n;
#define CHECK(c) do { if (!(c)) { printf("# %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define RUN(t) do { int f0 = g_fail; t(); printf("%sok %d - %s\n", g_fail == f0 ? "" : "not ", ++g_n, #t); } while (0)

typedef struct Report { uint16_t buttons; int16_t lx, ly, rx, ry; uint8_t lt, rt; } Report;

static void poll(Report *guest, const Report *live, bool test_mode, bool vr_ui)
{
    switch (xid_ui_gate(test_mode, vr_ui)) {
    case XID_UI_GATE_LIVE:    *guest = *live; break;
    case XID_UI_GATE_HOLD:    break;
    case XID_UI_GATE_NEUTRAL: memset(guest, 0, sizeof(*guest)); break;
    }
}

static const Report k_held = { 0x0001, 12000, -9000, 0, 30000, 255, 0 };

static void no_owner_is_live(void)
{
    Report g = { 0 };
    poll(&g, &k_held, false, false);
    CHECK(memcmp(&g, &k_held, sizeof(g)) == 0);
}

static void xemu_ui_keeps_last_report(void)
{
    Report g = k_held, idle = { 0 };
    poll(&g, &idle, true, false);
    CHECK(memcmp(&g, &k_held, sizeof(g)) == 0);
}

static void panel_open_releases_a_held_button(void)
{
    Report g = { 0 };
    poll(&g, &k_held, false, false);
    poll(&g, &k_held, true, true);
    CHECK(g.buttons == 0);
    CHECK(g.lx == 0 && g.ly == 0 && g.rx == 0 && g.ry == 0);
    CHECK(g.lt == 0 && g.rt == 0);
}

static void panel_open_without_test_mode_is_neutral(void)
{
    CHECK(xid_ui_gate(false, true) == XID_UI_GATE_NEUTRAL);
}

static void panel_close_goes_live_again(void)
{
    Report g = { 0 };
    poll(&g, &k_held, true, true);
    poll(&g, &k_held, false, false);
    CHECK(memcmp(&g, &k_held, sizeof(g)) == 0);
}

int main(void)
{
    RUN(no_owner_is_live);
    RUN(xemu_ui_keeps_last_report);
    RUN(panel_open_releases_a_held_button);
    RUN(panel_open_without_test_mode_is_neutral);
    RUN(panel_close_goes_live_again);
    printf("1..%d\n", g_n);
    return g_fail ? 1 : 0;
}
