/*
 * P9Array - deep auto free C-array
 *
 * Copyright (c) 2021 Crudebyte
 *
 * Authors:
 *   Christian Schoenebeck <qemu_oss@crudebyte.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef QEMU_P9ARRAY_H
#define QEMU_P9ARRAY_H

#define P9ARRAY_DECLARE_TYPE(scalar_type) \
    typedef struct P9Array##scalar_type { \
        size_t len; \
        scalar_type first[]; \
    } P9Array##scalar_type; \
    \
    void p9array_new_##scalar_type(scalar_type **auto_var, size_t len); \
    void p9array_auto_free_##scalar_type(scalar_type **auto_var); \

#define P9ARRAY_DEFINE_TYPE(scalar_type, scalar_cleanup_func) \
    void p9array_new_##scalar_type(scalar_type **auto_var, size_t len) \
    { \
        p9array_auto_free_##scalar_type(auto_var); \
        P9Array##scalar_type *arr = g_malloc0(sizeof(P9Array##scalar_type) + \
            len * sizeof(scalar_type)); \
        arr->len = len; \
        *auto_var = &arr->first[0]; \
    } \
    \
    void p9array_auto_free_##scalar_type(scalar_type **auto_var) \
    { \
        scalar_type *first = (*auto_var); \
        if (!first) { \
            return; \
        } \
        P9Array##scalar_type *arr = (P9Array##scalar_type *) ( \
            ((char *)first) - offsetof(P9Array##scalar_type, first) \
        ); \
        for (size_t i = 0; i < arr->len; ++i) { \
            scalar_cleanup_func(&arr->first[i]); \
        } \
        g_free(arr); \
    } \

#define P9ARRAY_REF(scalar_type) \
    __attribute((__cleanup__(p9array_auto_free_##scalar_type))) scalar_type*

#define P9ARRAY_NEW(scalar_type, auto_var, len) \
    QEMU_BUILD_BUG_MSG( \
        !__builtin_types_compatible_p(scalar_type, typeof(*auto_var)), \
        "P9Array scalar type mismatch" \
    ); \
    p9array_new_##scalar_type((&auto_var), len)

#endif
