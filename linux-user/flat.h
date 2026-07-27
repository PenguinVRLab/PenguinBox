/*
 * Copyright (C) 2002-2003  David McCullough <davidm@snapgear.com>
 * Copyright (C) 1998       Kenneth Albanowski <kjahds@kjahds.com>
 *                          The Silver Hammer Group, Ltd.
 *
 * This file provides the definitions and structures needed to
 * support uClinux flat-format executables.
 */

#ifndef LINUX_USER_FLAT_H
#define LINUX_USER_FLAT_H

#define	FLAT_VERSION			0x00000004L

#define	MAX_SHARED_LIBS			(1)

struct flat_hdr {
	char magic[4];
	abi_ulong rev;
	abi_ulong entry;
	abi_ulong data_start;
	abi_ulong data_end;
	abi_ulong bss_end;

	abi_ulong stack_size;
	abi_ulong reloc_start;
	abi_ulong reloc_count;
	abi_ulong flags;
	abi_ulong build_date;
	abi_ulong filler[5];
};

#define FLAT_FLAG_RAM    0x0001
#define FLAT_FLAG_GOTPIC 0x0002
#define FLAT_FLAG_GZIP   0x0004
#define FLAT_FLAG_GZDATA 0x0008
#define FLAT_FLAG_KTRACE 0x0010


#define	OLD_FLAT_VERSION			0x00000002L
#define OLD_FLAT_RELOC_TYPE_TEXT	0
#define OLD_FLAT_RELOC_TYPE_DATA	1
#define OLD_FLAT_RELOC_TYPE_BSS		2

#   	define OLD_FLAT_FLAG_RAM    0x1

#endif
