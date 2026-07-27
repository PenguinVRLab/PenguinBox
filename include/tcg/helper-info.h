/*
 * TCG Helper Information Structure
 *
 * Copyright (c) 2023 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TCG_HELPER_INFO_H
#define TCG_HELPER_INFO_H

#ifdef CONFIG_TCG_INTERPRETER
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#include <ffi.h>
#pragma GCC diagnostic pop
#endif
#include "tcg-target-reg-bits.h"

#define MAX_CALL_IARGS  7

typedef enum {
    TCG_CALL_RET_NORMAL,
    TCG_CALL_RET_BY_REF,
    TCG_CALL_RET_BY_VEC,
} TCGCallReturnKind;

typedef enum {
    TCG_CALL_ARG_NORMAL,
    TCG_CALL_ARG_EVEN,
    TCG_CALL_ARG_EXTEND,
    TCG_CALL_ARG_EXTEND_U,
    TCG_CALL_ARG_EXTEND_S,
    TCG_CALL_ARG_BY_REF,
    TCG_CALL_ARG_BY_REF_N,
} TCGCallArgumentKind;

typedef struct TCGCallArgumentLoc {
    TCGCallArgumentKind kind    : 8;
    unsigned arg_slot           : 8;
    unsigned ref_slot           : 8;
    unsigned arg_idx            : 4;
    unsigned tmp_subindex       : 2;
} TCGCallArgumentLoc;

struct TCGHelperInfo {
    void *func;
    const char *name;

#ifdef CONFIG_TCG_INTERPRETER
    ffi_cif *cif;
#else
    uintptr_t init;
#endif

    unsigned typemask           : 32;
    unsigned flags              : 8;
    unsigned nr_in              : 8;
    unsigned nr_out             : 8;
    TCGCallReturnKind out_kind  : 8;

    TCGCallArgumentLoc in[MAX_CALL_IARGS * (128 / TCG_TARGET_REG_BITS)];
};

#endif
