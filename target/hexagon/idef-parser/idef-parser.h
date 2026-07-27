/*
 *  Copyright(c) 2019-2022 rev.ng Labs Srl. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef IDEF_PARSER_H
#define IDEF_PARSER_H

#include <inttypes.h>
#include <stdio.h>
#include <stdbool.h>
#include <glib.h>

#define EMIT(c, ...)                                                           \
    do {                                                                       \
        g_string_append_printf((c)->out_str, __VA_ARGS__);                     \
    } while (0)

#define EMIT_SIG(c, ...)                                                       \
    do {                                                                       \
        g_string_append_printf((c)->signature_str, __VA_ARGS__);               \
    } while (0)

#define EMIT_HEAD(c, ...)                                                      \
    do {                                                                       \
        g_string_append_printf((c)->header_str, __VA_ARGS__);                  \
    } while (0)

typedef enum { GENERAL_PURPOSE, CONTROL, MODIFIER, DOTNEW } HexRegType;

typedef enum { UNKNOWN_SIGNEDNESS, SIGNED, UNSIGNED } HexSignedness;

typedef struct HexReg {
    uint8_t id;
    HexRegType type;
    unsigned bit_width;
} HexReg;

typedef struct HexTmp {
    unsigned index;
} HexTmp;

enum ImmUnionTag {
    I,
    VARIABLE,
    VALUE,
    QEMU_TMP,
    IMM_PC,
    IMM_CONSTEXT,
};

typedef struct HexImm {
    union {
        char id;
        uint64_t value;
        uint64_t index;
    };
    enum ImmUnionTag type;
} HexImm;

typedef struct HexPred {
    char id;
} HexPred;

typedef struct HexSat {
    HexSignedness signedness;
} HexSat;

typedef struct HexCast {
    unsigned bit_width;
    HexSignedness signedness;
} HexCast;

typedef struct HexExtract {
    unsigned bit_width;
    unsigned storage_bit_width;
    HexSignedness signedness;
} HexExtract;

typedef struct HexMpy {
    unsigned first_bit_width;
    unsigned second_bit_width;
    HexSignedness first_signedness;
    HexSignedness second_signedness;
} HexMpy;

typedef struct HexVar {
    GString *name;
} HexVar;

typedef struct Var {
    GString *name;
    uint8_t bit_width;
    HexSignedness signedness;
} Var;

typedef enum RvalueUnionTag {
    REGISTER, REGISTER_ARG, TEMP, IMMEDIATE, PREDICATE, VARID
} RvalueUnionTag;

typedef struct HexValue {
    union {
        HexReg reg;
        HexTmp tmp;
        HexImm imm;
        HexPred pred;
        HexVar var;
    };
    RvalueUnionTag type;
    unsigned bit_width;
    HexSignedness signedness;
    bool is_dotnew;
} HexValue;

typedef enum TernaryState { IN_LEFT, IN_RIGHT } TernaryState;

typedef struct Ternary {
    TernaryState state;
    HexValue cond;
} Ternary;

typedef enum OpType {
    ADD_OP, SUB_OP, MUL_OP, ASL_OP, ASR_OP, LSR_OP, ANDB_OP, ORB_OP,
    XORB_OP, ANDL_OP, MINI_OP, MAXI_OP
} OpType;

typedef struct Inst {
    GString *name;
    char *code_begin;
    char *code_end;
    unsigned tmp_count;
    unsigned qemu_tmp_count;
    unsigned if_count;
    unsigned error_count;
    GArray *allocated;
    GArray *init_list;
    GArray *strings;
} Inst;

typedef struct Context {
    void *scanner;
    char *input_buffer;
    GString *out_str;
    GString *signature_str;
    GString *header_str;
    FILE *defines_file;
    FILE *output_file;
    FILE *enabled_file;
    GArray *ternary;
    unsigned total_insn;
    unsigned implemented_insn;
    Inst inst;
} Context;

#endif
