/*
 * vhost software live migration iova tree
 *
 * SPDX-FileCopyrightText: Red Hat, Inc. 2021
 * SPDX-FileContributor: Author: Eugenio Pérez <eperezma@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/iova-tree.h"
#include "vhost-iova-tree.h"

#define iova_min_addr qemu_real_host_page_size()

struct VhostIOVATree {
    uint64_t iova_first;

    uint64_t iova_last;

    IOVATree *iova_taddr_map;

    IOVATree *iova_map;

    IOVATree *gpa_iova_map;
};

VhostIOVATree *vhost_iova_tree_new(hwaddr iova_first, hwaddr iova_last)
{
    VhostIOVATree *tree = g_new(VhostIOVATree, 1);

    tree->iova_first = MAX(iova_first, iova_min_addr);
    tree->iova_last = iova_last;

    tree->iova_taddr_map = iova_tree_new();
    tree->iova_map = iova_tree_new();
    tree->gpa_iova_map = gpa_tree_new();
    return tree;
}

void vhost_iova_tree_delete(VhostIOVATree *iova_tree)
{
    iova_tree_destroy(iova_tree->iova_taddr_map);
    iova_tree_destroy(iova_tree->iova_map);
    iova_tree_destroy(iova_tree->gpa_iova_map);
    g_free(iova_tree);
}

const DMAMap *vhost_iova_tree_find_iova(const VhostIOVATree *tree,
                                        const DMAMap *map)
{
    return iova_tree_find_iova(tree->iova_taddr_map, map);
}

int vhost_iova_tree_map_alloc(VhostIOVATree *tree, DMAMap *map, hwaddr taddr)
{
    int ret;

    hwaddr iova_first = tree->iova_first ?: qemu_real_host_page_size();

    if (taddr + map->size < taddr || map->perm == IOMMU_NONE) {
        return IOVA_ERR_INVALID;
    }

    ret = iova_tree_alloc_map(tree->iova_map, map, iova_first, tree->iova_last);
    if (unlikely(ret != IOVA_OK)) {
        return ret;
    }

    map->translated_addr = taddr;
    return iova_tree_insert(tree->iova_taddr_map, map);
}

void vhost_iova_tree_remove(VhostIOVATree *iova_tree, DMAMap map)
{
    iova_tree_remove(iova_tree->iova_taddr_map, map);
    iova_tree_remove(iova_tree->iova_map, map);
}

const DMAMap *vhost_iova_tree_find_gpa(const VhostIOVATree *tree,
                                       const DMAMap *map)
{
    return iova_tree_find_iova(tree->gpa_iova_map, map);
}

int vhost_iova_tree_map_alloc_gpa(VhostIOVATree *tree, DMAMap *map, hwaddr taddr)
{
    int ret;

    hwaddr iova_first = tree->iova_first ?: qemu_real_host_page_size();

    if (taddr + map->size < taddr || map->perm == IOMMU_NONE) {
        return IOVA_ERR_INVALID;
    }

    ret = iova_tree_alloc_map(tree->iova_map, map, iova_first, tree->iova_last);
    if (unlikely(ret != IOVA_OK)) {
        return ret;
    }

    map->translated_addr = taddr;
    return gpa_tree_insert(tree->gpa_iova_map, map);
}

void vhost_iova_tree_remove_gpa(VhostIOVATree *iova_tree, DMAMap map)
{
    iova_tree_remove(iova_tree->gpa_iova_map, map);
    iova_tree_remove(iova_tree->iova_map, map);
}
