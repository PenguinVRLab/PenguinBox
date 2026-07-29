/*
 * Renderer-fallback status query — deliberately dependency-free.
 *
 * nv2a.h is NOT standalone-includable from UI-layer code: its other
 * declarations (nv2a_init, etc.) require PCIBus/MemoryRegion from QEMU core
 * headers that UI .cc files don't pull in. This header exists so
 * ui/xui/{main-menu,debug}.cc can query the fallback state without dragging
 * that in. See hw/xbox/nv2a/pgraph/pgraph.c for the implementation.
 */

#ifndef HW_NV2A_RENDERER_STATUS_H
#define HW_NV2A_RENDERER_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

/* True for the rest of this session after the configured renderer failed to
 * initialize and we fell back (H-3: this is the "VK failed, silently landed
 * on GL" hazard made loud). reason() is a one-line, already-formatted,
 * user-facing explanation — empty string when not active. */
bool nv2a_get_renderer_fallback_active(void);
const char *nv2a_get_renderer_fallback_reason(void);

#ifdef __cplusplus
}
#endif

#endif
