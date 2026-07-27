/*
 * Declarations for cpu physical memory functions
 *
 * Copyright 2011 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Avi Kivity <avi@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 *
 */

#ifndef SYSTEM_RAMBLOCK_H
#define SYSTEM_RAMBLOCK_H

#include "exec/cpu-common.h"
#include "qemu/rcu.h"
#include "exec/ramlist.h"
#include "system/hostmem.h"

#define TYPE_RAM_BLOCK_ATTRIBUTES "ram-block-attributes"
OBJECT_DECLARE_SIMPLE_TYPE(RamBlockAttributes, RAM_BLOCK_ATTRIBUTES)

struct RAMBlock {
    struct rcu_head rcu;
    struct MemoryRegion *mr;
    uint8_t *host;
    uint8_t *colo_cache;
    ram_addr_t offset;
    ram_addr_t used_length;
    ram_addr_t max_length;
    void (*resized)(const char*, uint64_t length, void *host);
    uint32_t flags;
    char idstr[256];
    QLIST_ENTRY(RAMBlock) next;
    QLIST_HEAD(, RAMBlockNotifier) ramblock_notifiers;
    Error *cpr_blocker;
    int fd;
    uint64_t fd_offset;
    int guest_memfd;
    RamBlockAttributes *attributes;
    size_t page_size;
    unsigned long *bmap;

    unsigned long *file_bmap;
    off_t bitmap_offset;
    uint64_t pages_offset;

    unsigned long *receivedmap;

    unsigned long *clear_bmap;
    uint8_t clear_bmap_shift;

    ram_addr_t postcopy_length;
};

struct RamBlockAttributes {
    Object parent;

    RAMBlock *ram_block;

    unsigned bitmap_size;
    unsigned long *bitmap;

    QLIST_HEAD(, RamDiscardListener) rdl_list;
};

int ram_block_discard_range(RAMBlock *rb, uint64_t offset, size_t length);
int ram_block_discard_guest_memfd_range(RAMBlock *rb, uint64_t offset,
                                        size_t length);

RamBlockAttributes *ram_block_attributes_create(RAMBlock *ram_block);
void ram_block_attributes_destroy(RamBlockAttributes *attr);
int ram_block_attributes_state_change(RamBlockAttributes *attr, uint64_t offset,
                                      uint64_t size, bool to_discard);

bool ram_block_is_pmem(RAMBlock *rb);

static inline bool offset_in_ramblock(RAMBlock *b, ram_addr_t offset)
{
    return b && b->host && (offset < b->used_length);
}

static inline void *ramblock_ptr(RAMBlock *block, ram_addr_t offset)
{
    assert(offset_in_ramblock(block, offset));
    return (char *)block->host + offset;
}

#endif
