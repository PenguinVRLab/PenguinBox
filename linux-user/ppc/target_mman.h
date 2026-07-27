#ifndef PPC_TARGET_MMAN_H
#define PPC_TARGET_MMAN_H

#define TARGET_MAP_NORESERVE            0x40
#define TARGET_MAP_LOCKED               0x80

#ifdef TARGET_PPC64
#define TASK_UNMAPPED_BASE      0x0000100000000000ull
#else
#define TASK_UNMAPPED_BASE      0x40000000
#endif

#ifdef TARGET_PPC64
#define ELF_ET_DYN_BASE         0x100000000ull
#else
#define ELF_ET_DYN_BASE         0x000400000
#endif

#include "../generic/target_mman.h"

#endif
