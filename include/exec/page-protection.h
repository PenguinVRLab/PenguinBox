/*
 * QEMU page protection definitions.
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */
#ifndef EXEC_PAGE_PROT_COMMON_H
#define EXEC_PAGE_PROT_COMMON_H

#define PAGE_READ      0x0001
#define PAGE_WRITE     0x0002
#define PAGE_EXEC      0x0004
#define PAGE_RWX       (PAGE_READ | PAGE_WRITE | PAGE_EXEC)
#define PAGE_VALID     0x0008
#define PAGE_WRITE_ORG 0x0010
#define PAGE_WRITE_INV 0x0020
#define PAGE_PASSTHROUGH 0x40
#define PAGE_ANON      0x0080
#define PAGE_DONTDUMP  0x0100
#define PAGE_TARGET_1  0x0200
#define PAGE_TARGET_2  0x0400

#endif
