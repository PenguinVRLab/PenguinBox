/*
 * QEMU texture swizzling routines
 *
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2013 espes
 * Copyright (c) 2007-2010 The Nouveau Project.
 * Copyright (c) 2025 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#include "swizzle.h"

static void generate_swizzle_masks(unsigned int width,
                                   unsigned int height,
                                   unsigned int depth,
                                   uint32_t* mask_x,
                                   uint32_t* mask_y,
                                   uint32_t* mask_z)
{
    uint32_t x = 0, y = 0, z = 0;
    uint32_t bit = 1;
    uint32_t mask_bit = 1;
    bool done;
    do {
        done = true;
        if (bit < width) { x |= mask_bit; mask_bit <<= 1; done = false; }
        if (bit < height) { y |= mask_bit; mask_bit <<= 1; done = false; }
        if (bit < depth) { z |= mask_bit; mask_bit <<= 1; done = false; }
        bit <<= 1;
    } while(!done);
    assert((x ^ y ^ z) == (mask_bit - 1));
    *mask_x = x;
    *mask_y = y;
    *mask_z = z;
}

static inline void swizzle_box_internal(
    const uint8_t *src_buf,
    unsigned int width,
    unsigned int height,
    unsigned int depth,
    uint8_t *dst_buf,
    unsigned int row_pitch,
    unsigned int slice_pitch,
    unsigned int bytes_per_pixel)
{
    uint32_t mask_x, mask_y, mask_z;
    generate_swizzle_masks(width, height, depth, &mask_x, &mask_y, &mask_z);

    int x, y, z;
    int off_z = 0;
    for (z = 0; z < depth; z++) {
        int off_y = 0;
        for (y = 0; y < height; y++) {
            int off_x = 0;
            const uint8_t *src_tmp = src_buf + y * row_pitch;
            uint8_t *dst_tmp = dst_buf + (off_y + off_z) * bytes_per_pixel;
            for (x = 0; x < width; x++) {
                const uint8_t *src = src_tmp + x * bytes_per_pixel;
                uint8_t *dst = dst_tmp + off_x * bytes_per_pixel;
                memcpy(dst, src, bytes_per_pixel);

                off_x = (off_x - mask_x) & mask_x;
            }
            off_y = (off_y - mask_y) & mask_y;
        }
        src_buf += slice_pitch;
        off_z = (off_z - mask_z) & mask_z;
    }
}

static inline void unswizzle_box_internal(
    const uint8_t *src_buf,
    unsigned int width,
    unsigned int height,
    unsigned int depth,
    uint8_t *dst_buf,
    unsigned int row_pitch,
    unsigned int slice_pitch,
    unsigned int bytes_per_pixel)
{
    uint32_t mask_x, mask_y, mask_z;
    generate_swizzle_masks(width, height, depth, &mask_x, &mask_y, &mask_z);

    int x, y, z;
    int off_z = 0;
    for (z = 0; z < depth; z++) {
        int off_y = 0;
        for (y = 0; y < height; y++) {
            int off_x = 0;
            const uint8_t *src_tmp = src_buf + (off_y + off_z) * bytes_per_pixel;
            uint8_t *dst_tmp = dst_buf + y * row_pitch;
            for (x = 0; x < width; x++) {
                const uint8_t *src = src_tmp + off_x * bytes_per_pixel;
                uint8_t *dst = dst_tmp + x * bytes_per_pixel;
                memcpy(dst, src, bytes_per_pixel);

                off_x = (off_x - mask_x) & mask_x;
            }
            off_y = (off_y - mask_y) & mask_y;
        }
        dst_buf += slice_pitch;
        off_z = (off_z - mask_z) & mask_z;
    }
}

         \
#define C(m, bpp)                                                   \
    m##_internal(src_buf, width, height, depth, dst_buf, row_pitch, \
                 slice_pitch, bpp)
#define MULTIVERSION(m)                                                     \
    void m(const uint8_t *src_buf, unsigned int width, unsigned int height, \
           unsigned int depth, uint8_t *dst_buf, unsigned int row_pitch,    \
           unsigned int slice_pitch, unsigned int bytes_per_pixel)          \
    {                                                                       \
        switch (bytes_per_pixel) {                                          \
        case 1:                                                             \
            C(m, 1);                                                        \
            break;                                                          \
        case 2:                                                             \
            C(m, 2);                                                        \
            break;                                                          \
        case 3:                                                             \
            C(m, 3);                                                        \
            break;                                                          \
        case 4:                                                             \
            C(m, 4);                                                        \
            break;                                                          \
        default:                                                            \
            C(m, bytes_per_pixel);                                          \
        }                                                                   \
    }

MULTIVERSION(swizzle_box)
MULTIVERSION(unswizzle_box)

#undef C
#undef MULTIVERSION
