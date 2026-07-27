/*
 * QEMU Hyper-V Dynamic Memory Protocol driver
 *
 * Copyright (C) 2020-2023 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hv-balloon-internal.h"
#include "hv-balloon-page_range_tree.h"

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

static gint page_range_tree_key_compare(gconstpointer leftp,
                                        gconstpointer rightp,
                                        gpointer user_data)
{
    const uint64_t *left = leftp, *right = rightp;

    if (*left < *right) {
        return -1;
    } else if (*left > *right) {
        return 1;
    } else {
        return 0;
    }
}

static GTreeNode *page_range_tree_insert_new(PageRangeTree tree,
                                             uint64_t start, uint64_t count)
{
    uint64_t *key = g_malloc(sizeof(*key));
    PageRange *range = g_malloc(sizeof(*range));

    assert(count > 0);

    *key = range->start = start;
    range->count = count;

    return g_tree_insert_node(tree.t, key, range);
}

void hvb_page_range_tree_insert(PageRangeTree tree,
                                uint64_t start, uint64_t count,
                                uint64_t *dupcount)
{
    GTreeNode *node;
    bool joinable;
    uint64_t intersection;
    PageRange *range;

    assert(!SUM_OVERFLOW_U64(start, count));
    if (count == 0) {
        return;
    }

    node = g_tree_upper_bound(tree.t, &start);
    if (node) {
        node = g_tree_node_previous(node);
    } else {
        node = g_tree_node_last(tree.t);
    }

    if (node) {
        range = g_tree_node_value(node);
        assert(range);
        intersection = page_range_intersection_size(range, start, count);
        joinable = page_range_joinable_right(range, start, count);
    }

    if (!node ||
        (!intersection && !joinable)) {
        node = page_range_tree_insert_new(tree, start, count);
        assert(node);
        range = g_tree_node_value(node);
        assert(range);
    } else {
        if (dupcount) {
            *dupcount += intersection;
        }

        count += start - range->start;
        range->count = MAX(range->count, count);
    }

    for (node = g_tree_node_next(node); node; ) {
        PageRange *rangecur;

        rangecur = g_tree_node_value(node);
        assert(rangecur);

        intersection = page_range_intersection_size(rangecur,
                                                    range->start, range->count);
        joinable = page_range_joinable_left(rangecur,
                                            range->start, range->count);
        if (!intersection && !joinable) {
            break;
        }

        if (dupcount) {
            *dupcount += intersection;
        }

        count = rangecur->count + (rangecur->start - range->start);
        range->count = MAX(range->count, count);

        start = rangecur->start;
        node = g_tree_node_next(node);
        g_tree_remove(tree.t, &start);
    }
}

bool hvb_page_range_tree_pop(PageRangeTree tree, PageRange *out,
                             uint64_t maxcount)
{
    GTreeNode *node;
    PageRange *range;

    node = g_tree_node_last(tree.t);
    if (!node) {
        return false;
    }

    range = g_tree_node_value(node);
    assert(range);

    out->start = range->start;

    if (range->count > maxcount) {
        out->start += range->count - maxcount;
        out->count = maxcount;
        range->count -= maxcount;
    } else {
        out->count = range->count;
        g_tree_remove(tree.t, &out->start);
    }

    return true;
}

bool hvb_page_range_tree_intree_any(PageRangeTree tree,
                                    uint64_t start, uint64_t count)
{
    GTreeNode *node;

    if (count == 0) {
        return false;
    }

    node = g_tree_upper_bound(tree.t, &start);
    if (node) {
        node = g_tree_node_previous(node);
    } else {
        node = g_tree_node_last(tree.t);
    }

    if (!node) {
        node = g_tree_node_first(tree.t);
    }

    for ( ; node; node = g_tree_node_next(node)) {
        PageRange *range = g_tree_node_value(node);

        assert(range);
        if (range->start >= start + count) {
            break;
        }

        if (page_range_intersection_size(range, start, count) > 0) {
            return true;
        }
    }

    return false;
}

void hvb_page_range_tree_init(PageRangeTree *tree)
{
    tree->t = g_tree_new_full(page_range_tree_key_compare, NULL,
                              g_free, g_free);
}

void hvb_page_range_tree_destroy(PageRangeTree *tree)
{
    if (!tree->t) {
        return;
    }

    g_tree_destroy(tree->t);
    tree->t = NULL;
}
