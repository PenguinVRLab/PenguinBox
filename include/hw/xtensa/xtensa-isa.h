/* Interface definition for configurable Xtensa ISA support.
 *
 * Copyright (c) 2001-2013 Tensilica Inc.
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

#ifndef HW_XTENSA_XTENSA_ISA_H
#define HW_XTENSA_XTENSA_ISA_H

#ifdef __cplusplus
extern "C" {
#endif

#define XTENSA_ISA_VERSION 7000

typedef struct xtensa_isa_opaque { int unused; } *xtensa_isa;


typedef int xtensa_opcode;
typedef int xtensa_format;
typedef int xtensa_regfile;
typedef int xtensa_state;
typedef int xtensa_sysreg;
typedef int xtensa_interface;
typedef int xtensa_funcUnit;


#define XTENSA_UNDEFINED -1


typedef enum xtensa_isa_status_enum {
    xtensa_isa_ok = 0,
    xtensa_isa_bad_format,
    xtensa_isa_bad_slot,
    xtensa_isa_bad_opcode,
    xtensa_isa_bad_operand,
    xtensa_isa_bad_field,
    xtensa_isa_bad_iclass,
    xtensa_isa_bad_regfile,
    xtensa_isa_bad_sysreg,
    xtensa_isa_bad_state,
    xtensa_isa_bad_interface,
    xtensa_isa_bad_funcUnit,
    xtensa_isa_wrong_slot,
    xtensa_isa_no_field,
    xtensa_isa_out_of_memory,
    xtensa_isa_buffer_overflow,
    xtensa_isa_internal_error,
    xtensa_isa_bad_value
} xtensa_isa_status;

xtensa_isa_status xtensa_isa_errno(xtensa_isa isa);

char *xtensa_isa_error_msg(xtensa_isa isa);



typedef uint32_t xtensa_insnbuf_word;
typedef xtensa_insnbuf_word *xtensa_insnbuf;


int xtensa_insnbuf_size(xtensa_isa isa);


xtensa_insnbuf xtensa_insnbuf_alloc(xtensa_isa isa);


void xtensa_insnbuf_free(xtensa_isa isa, xtensa_insnbuf buf);


int xtensa_insnbuf_to_chars(xtensa_isa isa, const xtensa_insnbuf insn,
                            unsigned char *cp, int num_chars);

void xtensa_insnbuf_from_chars(xtensa_isa isa, xtensa_insnbuf insn,
                               const unsigned char *cp, int num_chars);



xtensa_isa xtensa_isa_init(void *xtensa_modules, xtensa_isa_status *errno_p,
                           char **error_msg_p);


void xtensa_isa_free(xtensa_isa isa);


int xtensa_isa_maxlength(xtensa_isa isa);


int xtensa_isa_length_from_chars(xtensa_isa isa, const unsigned char *cp);


int xtensa_isa_num_pipe_stages(xtensa_isa isa);


int xtensa_isa_num_formats(xtensa_isa isa);

int xtensa_isa_num_opcodes(xtensa_isa isa);

int xtensa_isa_num_regfiles(xtensa_isa isa);

int xtensa_isa_num_states(xtensa_isa isa);

int xtensa_isa_num_sysregs(xtensa_isa isa);

int xtensa_isa_num_interfaces(xtensa_isa isa);

int xtensa_isa_num_funcUnits(xtensa_isa isa);



const char *xtensa_format_name(xtensa_isa isa, xtensa_format fmt);


xtensa_format xtensa_format_lookup(xtensa_isa isa, const char *fmtname);


xtensa_format xtensa_format_decode(xtensa_isa isa, const xtensa_insnbuf insn);


int xtensa_format_encode(xtensa_isa isa, xtensa_format fmt,
                         xtensa_insnbuf insn);


int xtensa_format_length(xtensa_isa isa, xtensa_format fmt);


int xtensa_format_num_slots(xtensa_isa isa, xtensa_format fmt);


xtensa_opcode xtensa_format_slot_nop_opcode(xtensa_isa isa, xtensa_format fmt,
                                            int slot);


int xtensa_format_get_slot(xtensa_isa isa, xtensa_format fmt, int slot,
                           const xtensa_insnbuf insn, xtensa_insnbuf slotbuf);

int xtensa_format_set_slot(xtensa_isa isa, xtensa_format fmt, int slot,
                           xtensa_insnbuf insn, const xtensa_insnbuf slotbuf);



xtensa_opcode xtensa_opcode_lookup(xtensa_isa isa, const char *opname);


xtensa_opcode xtensa_opcode_decode(xtensa_isa isa, xtensa_format fmt, int slot,
                                   const xtensa_insnbuf slotbuf);


int xtensa_opcode_encode(xtensa_isa isa, xtensa_format fmt, int slot,
                         xtensa_insnbuf slotbuf, xtensa_opcode opc);


const char *xtensa_opcode_name(xtensa_isa isa, xtensa_opcode opc);


int xtensa_opcode_is_branch(xtensa_isa isa, xtensa_opcode opc);

int xtensa_opcode_is_jump(xtensa_isa isa, xtensa_opcode opc);

int xtensa_opcode_is_loop(xtensa_isa isa, xtensa_opcode opc);

int xtensa_opcode_is_call(xtensa_isa isa, xtensa_opcode opc);


int xtensa_opcode_num_operands(xtensa_isa isa, xtensa_opcode opc);

int xtensa_opcode_num_stateOperands(xtensa_isa isa, xtensa_opcode opc);

int xtensa_opcode_num_interfaceOperands(xtensa_isa isa, xtensa_opcode opc);


typedef struct xtensa_funcUnit_use_struct {
    xtensa_funcUnit unit;
    int stage;
} xtensa_funcUnit_use;

int xtensa_opcode_num_funcUnit_uses(xtensa_isa isa, xtensa_opcode opc);

xtensa_funcUnit_use *xtensa_opcode_funcUnit_use(xtensa_isa isa,
                                                xtensa_opcode opc, int u);



const char *xtensa_operand_name(xtensa_isa isa, xtensa_opcode opc, int opnd);


/*
 * Some operands are "invisible", i.e., not explicitly specified in
 * assembly language. When assembling an instruction, you need not set
 * the values of invisible operands, since they are either hardwired or
 * derived from other field values. The values of invisible operands
 * can be examined in the same way as other operands, but remember that
 * an invisible operand may get its value from another visible one, so
 * the entire instruction must be available before examining the
 * invisible operand values. This function returns 1 if an operand is
 * visible, 0 if it is invisible, or XTENSA_UNDEFINED on error. Note
 * that whether an operand is visible is orthogonal to whether it is
 * "implicit", i.e., whether it is encoded in a field in the
 * instruction.
 */

int xtensa_operand_is_visible(xtensa_isa isa, xtensa_opcode opc, int opnd);


char xtensa_operand_inout(xtensa_isa isa, xtensa_opcode opc, int opnd);


int xtensa_operand_get_field(xtensa_isa isa, xtensa_opcode opc, int opnd,
                             xtensa_format fmt, int slot,
                             const xtensa_insnbuf slotbuf, uint32_t *valp);

int xtensa_operand_set_field(xtensa_isa isa, xtensa_opcode opc, int opnd,
                             xtensa_format fmt, int slot,
                             xtensa_insnbuf slotbuf, uint32_t val);


int xtensa_operand_encode(xtensa_isa isa, xtensa_opcode opc, int opnd,
                          uint32_t *valp);

int xtensa_operand_decode(xtensa_isa isa, xtensa_opcode opc, int opnd,
                          uint32_t *valp);


int xtensa_operand_is_register(xtensa_isa isa, xtensa_opcode opc, int opnd);

xtensa_regfile xtensa_operand_regfile(xtensa_isa isa, xtensa_opcode opc,
                                      int opnd);


int xtensa_operand_num_regs(xtensa_isa isa, xtensa_opcode opc, int opnd);


int xtensa_operand_is_known_reg(xtensa_isa isa, xtensa_opcode opc, int opnd);


int xtensa_operand_is_PCrelative(xtensa_isa isa, xtensa_opcode opc, int opnd);


int xtensa_operand_do_reloc(xtensa_isa isa, xtensa_opcode opc, int opnd,
                            uint32_t *valp, uint32_t pc);

int xtensa_operand_undo_reloc(xtensa_isa isa, xtensa_opcode opc, int opnd,
                              uint32_t *valp, uint32_t pc);



xtensa_state xtensa_stateOperand_state(xtensa_isa isa, xtensa_opcode opc,
                                       int stOp);


char xtensa_stateOperand_inout(xtensa_isa isa, xtensa_opcode opc, int stOp);



xtensa_interface xtensa_interfaceOperand_interface(xtensa_isa isa,
                                                   xtensa_opcode opc,
                                                   int ifOp);



xtensa_regfile xtensa_regfile_lookup(xtensa_isa isa, const char *name);

xtensa_regfile xtensa_regfile_lookup_shortname(xtensa_isa isa,
                                               const char *shortname);


const char *xtensa_regfile_name(xtensa_isa isa, xtensa_regfile rf);

const char *xtensa_regfile_shortname(xtensa_isa isa, xtensa_regfile rf);


xtensa_regfile xtensa_regfile_view_parent(xtensa_isa isa, xtensa_regfile rf);


int xtensa_regfile_num_bits(xtensa_isa isa, xtensa_regfile rf);


int xtensa_regfile_num_entries(xtensa_isa isa, xtensa_regfile rf);



xtensa_state xtensa_state_lookup(xtensa_isa isa, const char *name);


const char *xtensa_state_name(xtensa_isa isa, xtensa_state st);


int xtensa_state_num_bits(xtensa_isa isa, xtensa_state st);


int xtensa_state_is_exported(xtensa_isa isa, xtensa_state st);


int xtensa_state_is_shared_or(xtensa_isa isa, xtensa_state st);



xtensa_sysreg xtensa_sysreg_lookup(xtensa_isa isa, int num, int is_user);


xtensa_sysreg xtensa_sysreg_lookup_name(xtensa_isa isa, const char *name);


const char *xtensa_sysreg_name(xtensa_isa isa, xtensa_sysreg sysreg);


int xtensa_sysreg_number(xtensa_isa isa, xtensa_sysreg sysreg);


int xtensa_sysreg_is_user(xtensa_isa isa, xtensa_sysreg sysreg);



xtensa_interface xtensa_interface_lookup(xtensa_isa isa, const char *ifname);


const char *xtensa_interface_name(xtensa_isa isa, xtensa_interface intf);


int xtensa_interface_num_bits(xtensa_isa isa, xtensa_interface intf);


char xtensa_interface_inout(xtensa_isa isa, xtensa_interface intf);


int xtensa_interface_has_side_effect(xtensa_isa isa, xtensa_interface intf);


int xtensa_interface_class_id(xtensa_isa isa, xtensa_interface intf);


xtensa_funcUnit xtensa_funcUnit_lookup(xtensa_isa isa, const char *fname);


const char *xtensa_funcUnit_name(xtensa_isa isa, xtensa_funcUnit fun);


int xtensa_funcUnit_num_copies(xtensa_isa isa, xtensa_funcUnit fun);


#ifdef __cplusplus
}
#endif
#endif
