#ifndef AARCH64_TARGET_MMAN_H
#define AARCH64_TARGET_MMAN_H

#define TARGET_PROT_BTI         0x10
#define TARGET_PROT_MTE         0x20

#define TASK_UNMAPPED_BASE      (1ull << (48 - 2))

#define ELF_ET_DYN_BASE         TARGET_PAGE_ALIGN((1ull << 48) / 3 * 2)

#include "../generic/target_mman.h"

#endif
