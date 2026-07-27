/* Internal definitions for the Xtensa ISA library.
 *
 * Copyright (c) 2004-2011 Tensilica Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef XTENSA_ISA_INTERNAL_H
#define XTENSA_ISA_INTERNAL_H

#ifndef uint32
#define uint32 uint32_t
#endif

#define XTENSA_OPERAND_IS_REGISTER      0x00000001
#define XTENSA_OPERAND_IS_PCRELATIVE    0x00000002
#define XTENSA_OPERAND_IS_INVISIBLE     0x00000004
#define XTENSA_OPERAND_IS_UNKNOWN       0x00000008

#define XTENSA_OPCODE_IS_BRANCH         0x00000001
#define XTENSA_OPCODE_IS_JUMP           0x00000002
#define XTENSA_OPCODE_IS_LOOP           0x00000004
#define XTENSA_OPCODE_IS_CALL           0x00000008

#define XTENSA_STATE_IS_EXPORTED        0x00000001
#define XTENSA_STATE_IS_SHARED_OR       0x00000002

#define XTENSA_INTERFACE_HAS_SIDE_EFFECT 0x00000001

typedef void (*xtensa_format_encode_fn)(xtensa_insnbuf);
typedef void (*xtensa_get_slot_fn)(const xtensa_insnbuf, xtensa_insnbuf);
typedef void (*xtensa_set_slot_fn)(xtensa_insnbuf, const xtensa_insnbuf);
typedef int (*xtensa_opcode_decode_fn)(const xtensa_insnbuf);
typedef uint32_t (*xtensa_get_field_fn)(const xtensa_insnbuf);
typedef void (*xtensa_set_field_fn)(xtensa_insnbuf, uint32_t);
typedef int (*xtensa_immed_decode_fn)(uint32_t *);
typedef int (*xtensa_immed_encode_fn)(uint32_t *);
typedef int (*xtensa_do_reloc_fn)(uint32_t *, uint32_t);
typedef int (*xtensa_undo_reloc_fn)(uint32_t *, uint32_t);
typedef void (*xtensa_opcode_encode_fn)(xtensa_insnbuf);
typedef int (*xtensa_format_decode_fn)(const xtensa_insnbuf);
typedef int (*xtensa_length_decode_fn)(const unsigned char *);

typedef struct xtensa_format_internal_struct {
    const char *name;
    int length;
    xtensa_format_encode_fn encode_fn;
    int num_slots;
    int *slot_id;
} xtensa_format_internal;

typedef struct xtensa_slot_internal_struct {
    const char *name;
    const char *format;
    int position;
    xtensa_get_slot_fn get_fn;
    xtensa_set_slot_fn set_fn;
    xtensa_get_field_fn *get_field_fns;
    xtensa_set_field_fn *set_field_fns;
    xtensa_opcode_decode_fn opcode_decode_fn;
    const char *nop_name;
} xtensa_slot_internal;

typedef struct xtensa_operand_internal_struct {
    const char *name;
    int field_id;
    xtensa_regfile regfile;
    int num_regs;
    uint32_t flags;
    xtensa_immed_encode_fn encode;
    xtensa_immed_decode_fn decode;
    xtensa_do_reloc_fn do_reloc;
    xtensa_undo_reloc_fn undo_reloc;
} xtensa_operand_internal;

typedef struct xtensa_arg_internal_struct {
    union {
        int operand_id;
        xtensa_state state;
    } u;
    char inout;
} xtensa_arg_internal;

typedef struct xtensa_iclass_internal_struct {
    int num_operands;
    xtensa_arg_internal *operands;

    int num_stateOperands;
    xtensa_arg_internal *stateOperands;

    int num_interfaceOperands;
    xtensa_interface *interfaceOperands;
} xtensa_iclass_internal;

typedef struct xtensa_opcode_internal_struct {
    const char *name;
    int iclass_id;
    uint32_t flags;
    xtensa_opcode_encode_fn *encode_fns;
    int num_funcUnit_uses;
    xtensa_funcUnit_use *funcUnit_uses;
} xtensa_opcode_internal;

typedef struct xtensa_regfile_internal_struct {
    const char *name;
    const char *shortname;
    xtensa_regfile parent;
    int num_bits;
    int num_entries;
} xtensa_regfile_internal;

typedef struct xtensa_interface_internal_struct {
    const char *name;
    int num_bits;
    uint32_t flags;
    int class_id;
    char inout;
} xtensa_interface_internal;

typedef struct xtensa_funcUnit_internal_struct {
    const char *name;
    int num_copies;
} xtensa_funcUnit_internal;

typedef struct xtensa_state_internal_struct {
    const char *name;
    int num_bits;
    uint32_t flags;
} xtensa_state_internal;

typedef struct xtensa_sysreg_internal_struct {
    const char *name;
    int number;
    int is_user;
} xtensa_sysreg_internal;

typedef struct xtensa_lookup_entry_struct {
    const char *key;
    union {
        xtensa_opcode opcode;
        xtensa_sysreg sysreg;
        xtensa_state state;
        xtensa_interface intf;
        xtensa_funcUnit fun;
    } u;
} xtensa_lookup_entry;

typedef struct xtensa_isa_internal_struct {
    int is_big_endian;
    int insn_size;
    int insnbuf_size;

    int num_formats;
    xtensa_format_internal *formats;
    xtensa_format_decode_fn format_decode_fn;
    xtensa_length_decode_fn length_decode_fn;

    int num_slots;
    xtensa_slot_internal *slots;

    int num_fields;

    int num_operands;
    xtensa_operand_internal *operands;

    int num_iclasses;
    xtensa_iclass_internal *iclasses;

    int num_opcodes;
    xtensa_opcode_internal *opcodes;
    xtensa_lookup_entry *opname_lookup_table;

    int num_regfiles;
    xtensa_regfile_internal *regfiles;

    int num_states;
    xtensa_state_internal *states;
    xtensa_lookup_entry *state_lookup_table;

    int num_sysregs;
    xtensa_sysreg_internal *sysregs;
    xtensa_lookup_entry *sysreg_lookup_table;

    int max_sysreg_num[2];
    xtensa_sysreg *sysreg_table[2];

    int num_interfaces;
    xtensa_interface_internal *interfaces;
    xtensa_lookup_entry *interface_lookup_table;

    int num_funcUnits;
    xtensa_funcUnit_internal *funcUnits;
    xtensa_lookup_entry *funcUnit_lookup_table;

    int num_stages;
} xtensa_isa_internal;

int xtensa_isa_name_compare(const void *, const void *);

extern xtensa_isa_status xtisa_errno;
extern char xtisa_error_msg[];

#endif
