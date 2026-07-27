/*
 * Page cache for QEMU
 * The cache is base on a hash of the page address
 *
 * Copyright 2012 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Orit Wasserman  <owasserm@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef PAGE_CACHE_H
#define PAGE_CACHE_H

typedef struct PageCache PageCache;

PageCache *cache_init(uint64_t cache_size, size_t page_size, Error **errp);
void cache_fini(PageCache *cache);

bool cache_is_cached(const PageCache *cache, uint64_t addr,
                     uint64_t current_age);

uint8_t *get_cached_data(const PageCache *cache, uint64_t addr);

int cache_insert(PageCache *cache, uint64_t addr, const uint8_t *pdata,
                 uint64_t current_age);

#endif
