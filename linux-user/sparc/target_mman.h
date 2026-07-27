#ifndef SPARC_TARGET_MMAN_H
#define SPARC_TARGET_MMAN_H

#define TARGET_MAP_NORESERVE           0x40
#define TARGET_MAP_LOCKED              0x100
#define TARGET_MAP_GROWSDOWN           0x0200

#ifdef TARGET_ABI32
#define TASK_UNMAPPED_BASE      0x70000000
#else
#define TASK_UNMAPPED_BASE      (1ull << (TARGET_VIRT_ADDR_SPACE_BITS - 2))
#endif

#ifdef TARGET_ABI32
#define ELF_ET_DYN_BASE         0x78000000
#else
#define ELF_ET_DYN_BASE         0x0000010000000000ull
#endif

#include "../generic/target_mman.h"

#endif
