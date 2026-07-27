/*
 * Copyright (c) 2018 Red Hat, Inc. and/or its affiliates
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef BOOTFILE_H
#define BOOTFILE_H

#define TEST_MEM_PAGE_SIZE 4096

#define X86_TEST_MEM_START (1 * 1024 * 1024)
#define X86_TEST_MEM_END   (100 * 1024 * 1024)

#define S390_TEST_MEM_START (1 * 1024 * 1024)
#define S390_TEST_MEM_END   (100 * 1024 * 1024)

#define PPC_TEST_MEM_START (1 * 1024 * 1024)
#define PPC_TEST_MEM_END   (100 * 1024 * 1024)
#define PPC_H_PUT_TERM_CHAR 0x58

#define ARM_TEST_MEM_START (0x40000000 + 1 * 1024 * 1024)
#define ARM_TEST_MEM_END   (0x40000000 + 100 * 1024 * 1024)
#define ARM_MACH_VIRT_UART 0x09000000
#define ARM_TEST_MAX_KERNEL_SIZE (512 * 1024)

void bootfile_delete(void);
char *bootfile_create(const char *arch, const char *dir, bool suspend_me);
char *bootfile_get(void);

#endif
