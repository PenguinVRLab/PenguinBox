/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_VR_SBC_PANEL_H
#define HW_XBOX_NV2A_PGRAPH_VK_VR_SBC_PANEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VR_SBC_PANEL_W 768
#define VR_SBC_PANEL_H 256

bool vr_sbc_panel_frame(uint8_t *rgba, bool *active);

void vr_sbc_panel_publish(void);

typedef struct VrSbcPanelCell {
    const char *id;
    uint16_t    x, y, w, h;
    uint64_t    button_mask;
    int        *scancode;
} VrSbcPanelCell;

const VrSbcPanelCell *vr_sbc_panel_cells(size_t *count);

void vr_sbc_panel_inject(const char *cell_id, bool pressed);

#ifdef __cplusplus
}
#endif

#endif
