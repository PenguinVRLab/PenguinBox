/*
 * Copyright (C) 2026 Patrick Carey
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef XEMU_YAML_LITE_H
#define XEMU_YAML_LITE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum YlType { YL_NULL, YL_SCALAR, YL_MAP, YL_SEQ } YlType;

typedef struct YlNode {
    YlType type;
    int line;
    char *str;
    bool quoted;
    size_t n, cap;
    char **keys;
    struct YlNode **items;
} YlNode;

YlNode *yl_parse(const char *text, size_t len, char *err, size_t errlen);
YlNode *yl_parse_file(const char *path, char *err, size_t errlen);
void yl_free(YlNode *n);

const YlNode *yl_get(const YlNode *map, const char *key);
size_t yl_len(const YlNode *n);
const YlNode *yl_at(const YlNode *seq, size_t i);
const char *yl_key_at(const YlNode *map, size_t i);
const char *yl_str(const YlNode *n);
bool yl_int(const YlNode *n, long long *out);
int yl_bool(const YlNode *n);

#ifdef __cplusplus
}
#endif

#endif
