/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _LINUX_SYSINFO_H
#define _LINUX_SYSINFO_H

#include "standard-headers/linux/types.h"

#define SI_LOAD_SHIFT	16
struct sysinfo {
	long uptime;
	unsigned long loads[3];
	unsigned long totalram;
	unsigned long freeram;
	unsigned long sharedram;
	unsigned long bufferram;
	unsigned long totalswap;
	unsigned long freeswap;
	uint16_t procs;
	uint16_t pad;
	unsigned long totalhigh;
	unsigned long freehigh;
	uint32_t mem_unit;
	char _f[20-2*sizeof(unsigned long)-sizeof(uint32_t)];
};

#endif
