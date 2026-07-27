/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_VR_PUBLIC_H
#define HW_XBOX_NV2A_PGRAPH_VK_VR_PUBLIC_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void xemu_vr_request_recenter(void);

bool xemu_vr_active(void);

void xemu_vr_request_hunt_dump(void);

#ifdef __cplusplus
}
#endif

#endif
