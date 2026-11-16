/* SPDX-License-Identifier: MIT */
#ifndef WORLDPROJ_TEST_STUB_NV2A_INT_H
#define WORLDPROJ_TEST_STUB_NV2A_INT_H
#define NV2A_VERTEXSHADER_CONSTANTS 192
typedef struct PGRAPHState {
    uint32_t vsh_constants[NV2A_VERTEXSHADER_CONSTANTS][4];
    bool vsh_constants_dirty[NV2A_VERTEXSHADER_CONSTANTS];
    bool zpass_pixel_count_enable;
} PGRAPHState;

static inline void pgraph_vsh_constant_dirty(PGRAPHState *pg, unsigned int row)
{
    pg->vsh_constants_dirty[row] = true;
}
#endif
