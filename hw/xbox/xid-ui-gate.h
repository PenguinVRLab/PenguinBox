/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_XBOX_XID_UI_GATE_H
#define HW_XBOX_XID_UI_GATE_H

#include <stdbool.h>

typedef enum XidUiGate {
    XID_UI_GATE_LIVE,
    XID_UI_GATE_HOLD,
    XID_UI_GATE_NEUTRAL,
} XidUiGate;

static inline XidUiGate xid_ui_gate(bool test_mode, bool vr_ui_owns_input)
{
#ifndef XID_UI_GATE_PLANTED_FREEZE
    if (vr_ui_owns_input) {
        return XID_UI_GATE_NEUTRAL;
    }
#else
    (void)vr_ui_owns_input;
#endif
    return test_mode ? XID_UI_GATE_HOLD : XID_UI_GATE_LIVE;
}

#endif
