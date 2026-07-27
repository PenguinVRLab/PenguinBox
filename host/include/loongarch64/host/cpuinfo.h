/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Host specific cpu identification for LoongArch
 */

#ifndef HOST_CPUINFO_H
#define HOST_CPUINFO_H

#define CPUINFO_ALWAYS          (1u << 0)
#define CPUINFO_LSX             (1u << 1)
#define CPUINFO_LASX            (1u << 2)

extern unsigned cpuinfo;

unsigned cpuinfo_init(void);

#endif
