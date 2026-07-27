/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2019 Western Digital Corporation or its affiliates.
 *
 * Based on include/sbi/{fw_dynamic.h,sbi_scratch.h} from the OpenSBI project.
 */

#ifndef RISCV_BOOT_OPENSBI_H
#define RISCV_BOOT_OPENSBI_H

#include "exec/cpu-defs.h"

#define FW_DYNAMIC_INFO_MAGIC_VALUE     0x4942534f

#define FW_DYNAMIC_INFO_VERSION         0x2

#define FW_DYNAMIC_INFO_NEXT_MODE_U     0x0
#define FW_DYNAMIC_INFO_NEXT_MODE_S     0x1
#define FW_DYNAMIC_INFO_NEXT_MODE_M     0x3

enum sbi_scratch_options {
    SBI_SCRATCH_NO_BOOT_PRINTS = (1 << 0),
    SBI_SCRATCH_DEBUG_PRINTS = (1 << 1),
};

struct fw_dynamic_info64 {
    int64_t magic;
    int64_t version;
    int64_t next_addr;
    int64_t next_mode;
    int64_t options;
    int64_t boot_hart;
};

struct fw_dynamic_info32 {
    int32_t magic;
    int32_t version;
    int32_t next_addr;
    int32_t next_mode;
    int32_t options;
    int32_t boot_hart;
};
#endif
