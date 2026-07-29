#ifndef HW_NV2A_RENDERER_STATUS_H
#define HW_NV2A_RENDERER_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

bool nv2a_get_renderer_fallback_active(void);
const char *nv2a_get_renderer_fallback_reason(void);

#ifdef __cplusplus
}
#endif

#endif
