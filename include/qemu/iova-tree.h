/*
 * An very simplified iova tree implementation based on GTree.
 *
 * Copyright 2018 Red Hat, Inc.
 *
 * Authors:
 *  Peter Xu <peterx@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 */
#ifndef IOVA_TREE_H
#define IOVA_TREE_H

#include "system/memory.h"
#include "exec/hwaddr.h"

#define  IOVA_OK           (0)
#define  IOVA_ERR_INVALID  (-1)
#define  IOVA_ERR_OVERLAP  (-2)
#define  IOVA_ERR_NOMEM    (-3)

typedef struct IOVATree IOVATree;
typedef struct DMAMap {
    hwaddr iova;
    hwaddr translated_addr;
    hwaddr size;
    IOMMUAccessFlags perm;
} QEMU_PACKED DMAMap;
typedef gboolean (*iova_tree_iterator)(DMAMap *map);

IOVATree *gpa_tree_new(void);

int gpa_tree_insert(IOVATree *tree, const DMAMap *map);

IOVATree *iova_tree_new(void);

int iova_tree_insert(IOVATree *tree, const DMAMap *map);

void iova_tree_remove(IOVATree *tree, DMAMap map);

const DMAMap *iova_tree_find(const IOVATree *tree, const DMAMap *map);

const DMAMap *iova_tree_find_iova(const IOVATree *tree, const DMAMap *map);

int iova_tree_alloc_map(IOVATree *tree, DMAMap *map, hwaddr iova_begin,
                        hwaddr iova_end);

void iova_tree_destroy(IOVATree *tree);

#endif
