/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2008 Andrzej Zaborowski
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "elf.h"

int arm_arch = __ARM_ARCH;

#ifndef use_idiv_instructions
bool use_idiv_instructions;
#endif
#ifndef use_neon_instructions
bool use_neon_instructions;
#endif

#define TCG_TARGET_STACK_ALIGN          8
#define TCG_TARGET_CALL_STACK_OFFSET    0
#define TCG_TARGET_CALL_ARG_I32         TCG_CALL_ARG_NORMAL
#define TCG_TARGET_CALL_ARG_I64         TCG_CALL_ARG_EVEN
#define TCG_TARGET_CALL_ARG_I128        TCG_CALL_ARG_EVEN
#define TCG_TARGET_CALL_RET_I128        TCG_CALL_RET_BY_REF

#ifdef CONFIG_DEBUG_TCG
static const char * const tcg_target_reg_names[TCG_TARGET_NB_REGS] = {
    "%r0",  "%r1",  "%r2",  "%r3",  "%r4",  "%r5",  "%r6",  "%r7",
    "%r8",  "%r9",  "%r10", "%r11", "%r12", "%sp",  "%r14", "%pc",
    "%q0",  "%q1",  "%q2",  "%q3",  "%q4",  "%q5",  "%q6",  "%q7",
    "%q8",  "%q9",  "%q10", "%q11", "%q12", "%q13", "%q14", "%q15",
};
#endif

static const int tcg_target_reg_alloc_order[] = {
    TCG_REG_R4,
    TCG_REG_R5,
    TCG_REG_R6,
    TCG_REG_R7,
    TCG_REG_R8,
    TCG_REG_R9,
    TCG_REG_R10,
    TCG_REG_R11,
    TCG_REG_R13,
    TCG_REG_R0,
    TCG_REG_R1,
    TCG_REG_R2,
    TCG_REG_R3,
    TCG_REG_R12,
    TCG_REG_R14,

    TCG_REG_Q0,
    TCG_REG_Q1,
    TCG_REG_Q2,
    TCG_REG_Q3,
    TCG_REG_Q8,
    TCG_REG_Q9,
    TCG_REG_Q10,
    TCG_REG_Q11,
    TCG_REG_Q12,
    TCG_REG_Q13,
    TCG_REG_Q14,
    TCG_REG_Q15,
};

static const int tcg_target_call_iarg_regs[4] = {
    TCG_REG_R0, TCG_REG_R1, TCG_REG_R2, TCG_REG_R3
};

static TCGReg tcg_target_call_oarg_reg(TCGCallReturnKind kind, int slot)
{
    tcg_debug_assert(kind == TCG_CALL_RET_NORMAL);
    tcg_debug_assert(slot >= 0 && slot <= 3);
    return TCG_REG_R0 + slot;
}

#define TCG_REG_TMP  TCG_REG_R12
#define TCG_VEC_TMP  TCG_REG_Q15
#define TCG_REG_GUEST_BASE  TCG_REG_R11

typedef enum {
    COND_EQ = 0x0,
    COND_NE = 0x1,
    COND_CS = 0x2,
    COND_CC = 0x3,
    COND_MI = 0x4,
    COND_PL = 0x5,
    COND_VS = 0x6,
    COND_VC = 0x7,
    COND_HI = 0x8,
    COND_LS = 0x9,
    COND_GE = 0xa,
    COND_LT = 0xb,
    COND_GT = 0xc,
    COND_LE = 0xd,
    COND_AL = 0xe,
} ARMCond;

#define TO_CPSR (1 << 20)

#define SHIFT_IMM_LSL(im)	(((im) << 7) | 0x00)
#define SHIFT_IMM_LSR(im)	(((im) << 7) | 0x20)
#define SHIFT_IMM_ASR(im)	(((im) << 7) | 0x40)
#define SHIFT_IMM_ROR(im)	(((im) << 7) | 0x60)
#define SHIFT_REG_LSL(rs)	(((rs) << 8) | 0x10)
#define SHIFT_REG_LSR(rs)	(((rs) << 8) | 0x30)
#define SHIFT_REG_ASR(rs)	(((rs) << 8) | 0x50)
#define SHIFT_REG_ROR(rs)	(((rs) << 8) | 0x70)

typedef enum {
    ARITH_AND = 0x0 << 21,
    ARITH_EOR = 0x1 << 21,
    ARITH_SUB = 0x2 << 21,
    ARITH_RSB = 0x3 << 21,
    ARITH_ADD = 0x4 << 21,
    ARITH_ADC = 0x5 << 21,
    ARITH_SBC = 0x6 << 21,
    ARITH_RSC = 0x7 << 21,
    ARITH_TST = 0x8 << 21 | TO_CPSR,
    ARITH_CMP = 0xa << 21 | TO_CPSR,
    ARITH_CMN = 0xb << 21 | TO_CPSR,
    ARITH_ORR = 0xc << 21,
    ARITH_MOV = 0xd << 21,
    ARITH_BIC = 0xe << 21,
    ARITH_MVN = 0xf << 21,

    INSN_B         = 0x0a000000,

    INSN_CLZ       = 0x016f0f10,
    INSN_RBIT      = 0x06ff0f30,

    INSN_LDMIA     = 0x08b00000,
    INSN_STMDB     = 0x09200000,

    INSN_LDR_IMM   = 0x04100000,
    INSN_LDR_REG   = 0x06100000,
    INSN_STR_IMM   = 0x04000000,
    INSN_STR_REG   = 0x06000000,

    INSN_LDRH_IMM  = 0x005000b0,
    INSN_LDRH_REG  = 0x001000b0,
    INSN_LDRSH_IMM = 0x005000f0,
    INSN_LDRSH_REG = 0x001000f0,
    INSN_STRH_IMM  = 0x004000b0,
    INSN_STRH_REG  = 0x000000b0,

    INSN_LDRB_IMM  = 0x04500000,
    INSN_LDRB_REG  = 0x06500000,
    INSN_LDRSB_IMM = 0x005000d0,
    INSN_LDRSB_REG = 0x001000d0,
    INSN_STRB_IMM  = 0x04400000,
    INSN_STRB_REG  = 0x06400000,

    INSN_LDRD_IMM  = 0x004000d0,
    INSN_LDRD_REG  = 0x000000d0,
    INSN_STRD_IMM  = 0x004000f0,
    INSN_STRD_REG  = 0x000000f0,

    INSN_DMB_ISH   = 0xf57ff05b,
    INSN_DMB_MCR   = 0xee070fba,

    INSN_MSRI_CPSR = 0x0360f000,

    INSN_NOP_v6k   = 0xe320f000,
    INSN_NOP_v4    = (COND_AL << 28) | ARITH_MOV,

    INSN_VADD      = 0xf2000800,
    INSN_VAND      = 0xf2000110,
    INSN_VBIC      = 0xf2100110,
    INSN_VEOR      = 0xf3000110,
    INSN_VORN      = 0xf2300110,
    INSN_VORR      = 0xf2200110,
    INSN_VSUB      = 0xf3000800,
    INSN_VMUL      = 0xf2000910,
    INSN_VQADD     = 0xf2000010,
    INSN_VQADD_U   = 0xf3000010,
    INSN_VQSUB     = 0xf2000210,
    INSN_VQSUB_U   = 0xf3000210,
    INSN_VMAX      = 0xf2000600,
    INSN_VMAX_U    = 0xf3000600,
    INSN_VMIN      = 0xf2000610,
    INSN_VMIN_U    = 0xf3000610,

    INSN_VABS      = 0xf3b10300,
    INSN_VMVN      = 0xf3b00580,
    INSN_VNEG      = 0xf3b10380,

    INSN_VCEQ0     = 0xf3b10100,
    INSN_VCGT0     = 0xf3b10000,
    INSN_VCGE0     = 0xf3b10080,
    INSN_VCLE0     = 0xf3b10180,
    INSN_VCLT0     = 0xf3b10200,

    INSN_VCEQ      = 0xf3000810,
    INSN_VCGE      = 0xf2000310,
    INSN_VCGT      = 0xf2000300,
    INSN_VCGE_U    = 0xf3000310,
    INSN_VCGT_U    = 0xf3000300,

    INSN_VSHLI     = 0xf2800510,
    INSN_VSARI     = 0xf2800010,
    INSN_VSHRI     = 0xf3800010,
    INSN_VSLI      = 0xf3800510,
    INSN_VSHL_S    = 0xf2000400,
    INSN_VSHL_U    = 0xf3000400,

    INSN_VBSL      = 0xf3100110,
    INSN_VBIT      = 0xf3200110,
    INSN_VBIF      = 0xf3300110,

    INSN_VTST      = 0xf2000810,

    INSN_VDUP_G    = 0xee800b10,
    INSN_VDUP_S    = 0xf3b00c00,
    INSN_VLDR_D    = 0xed100b00,
    INSN_VLD1      = 0xf4200000,
    INSN_VLD1R     = 0xf4a00c00,
    INSN_VST1      = 0xf4000000,
    INSN_VMOVI     = 0xf2800010,
} ARMInsn;

#define INSN_NOP   (use_armv7_instructions ? INSN_NOP_v6k : INSN_NOP_v4)

static const uint8_t tcg_cond_to_arm_cond[] = {
    [TCG_COND_EQ] = COND_EQ,
    [TCG_COND_NE] = COND_NE,
    [TCG_COND_LT] = COND_LT,
    [TCG_COND_GE] = COND_GE,
    [TCG_COND_LE] = COND_LE,
    [TCG_COND_GT] = COND_GT,
    [TCG_COND_LTU] = COND_CC,
    [TCG_COND_GEU] = COND_CS,
    [TCG_COND_LEU] = COND_LS,
    [TCG_COND_GTU] = COND_HI,
};

static int encode_imm(uint32_t imm);

#define R_ARM_PC8  11

#define R_ARM_PC11 12

static bool reloc_pc24(tcg_insn_unit *src_rw, const tcg_insn_unit *target)
{
    const tcg_insn_unit *src_rx = tcg_splitwx_to_rx(src_rw);
    ptrdiff_t offset = (tcg_ptr_byte_diff(target, src_rx) - 8) >> 2;

    if (offset == sextract32(offset, 0, 24)) {
        *src_rw = deposit32(*src_rw, 0, 24, offset);
        return true;
    }
    return false;
}

static bool reloc_pc13(tcg_insn_unit *src_rw, const tcg_insn_unit *target)
{
    const tcg_insn_unit *src_rx = tcg_splitwx_to_rx(src_rw);
    ptrdiff_t offset = tcg_ptr_byte_diff(target, src_rx) - 8;

    if (offset >= -0xfff && offset <= 0xfff) {
        tcg_insn_unit insn = *src_rw;
        bool u = (offset >= 0);
        if (!u) {
            offset = -offset;
        }
        insn = deposit32(insn, 23, 1, u);
        insn = deposit32(insn, 0, 12, offset);
        *src_rw = insn;
        return true;
    }
    return false;
}

static bool reloc_pc11(tcg_insn_unit *src_rw, const tcg_insn_unit *target)
{
    const tcg_insn_unit *src_rx = tcg_splitwx_to_rx(src_rw);
    ptrdiff_t offset = (tcg_ptr_byte_diff(target, src_rx) - 8) / 4;

    if (offset >= -0xff && offset <= 0xff) {
        tcg_insn_unit insn = *src_rw;
        bool u = (offset >= 0);
        if (!u) {
            offset = -offset;
        }
        insn = deposit32(insn, 23, 1, u);
        insn = deposit32(insn, 0, 8, offset);
        *src_rw = insn;
        return true;
    }
    return false;
}

static bool reloc_pc8(tcg_insn_unit *src_rw, const tcg_insn_unit *target)
{
    const tcg_insn_unit *src_rx = tcg_splitwx_to_rx(src_rw);
    ptrdiff_t offset = tcg_ptr_byte_diff(target, src_rx) - 8;
    int imm12 = encode_imm(offset);

    if (imm12 >= 0) {
        *src_rw = deposit32(*src_rw, 0, 12, imm12);
        return true;
    }
    return false;
}

static bool patch_reloc(tcg_insn_unit *code_ptr, int type,
                        intptr_t value, intptr_t addend)
{
    tcg_debug_assert(addend == 0);
    switch (type) {
    case R_ARM_PC24:
        return reloc_pc24(code_ptr, (const tcg_insn_unit *)value);
    case R_ARM_PC13:
        return reloc_pc13(code_ptr, (const tcg_insn_unit *)value);
    case R_ARM_PC11:
        return reloc_pc11(code_ptr, (const tcg_insn_unit *)value);
    case R_ARM_PC8:
        return reloc_pc8(code_ptr, (const tcg_insn_unit *)value);
    default:
        g_assert_not_reached();
    }
}

#define TCG_CT_CONST_ARM  0x100
#define TCG_CT_CONST_INV  0x200
#define TCG_CT_CONST_NEG  0x400
#define TCG_CT_CONST_ZERO 0x800
#define TCG_CT_CONST_ORRI 0x1000
#define TCG_CT_CONST_ANDI 0x2000

#define ALL_GENERAL_REGS  0xffffu
#define ALL_VECTOR_REGS   0xffff0000u

#define ALL_QLDST_REGS \
    (ALL_GENERAL_REGS & ~((tcg_use_softmmu ? 0xf : 0) | (1 << TCG_REG_R14)))

static int encode_imm(uint32_t imm)
{
    uint32_t rot, imm8;

    if ((imm & ~0xff) == 0) {
        return imm;
    }

    rot = ctz32(imm) & ~1;
    imm8 = imm >> rot;
    rot = 32 - rot;
    if ((imm8 & ~0xff) == 0) {
        goto found;
    }

    for (rot = 2; rot < 8; rot += 2) {
        imm8 = rol32(imm, rot);
        if ((imm8 & ~0xff) == 0) {
            goto found;
        }
    }
    return -1;

 found:
    return rot << 7 | imm8;
}

static int encode_imm_nofail(uint32_t imm)
{
    int ret = encode_imm(imm);
    tcg_debug_assert(ret >= 0);
    return ret;
}

static bool check_fit_imm(uint32_t imm)
{
    return encode_imm(imm) >= 0;
}

static bool is_shimm16(uint16_t v16, int *cmode, int *imm8)
{
    if (v16 == (v16 & 0xff)) {
        *cmode = 0x8;
        *imm8 = v16 & 0xff;
        return true;
    } else if (v16 == (v16 & 0xff00)) {
        *cmode = 0xa;
        *imm8 = v16 >> 8;
        return true;
    }
    return false;
}

static bool is_shimm32(uint32_t v32, int *cmode, int *imm8)
{
    if (v32 == (v32 & 0xff)) {
        *cmode = 0x0;
        *imm8 = v32 & 0xff;
        return true;
    } else if (v32 == (v32 & 0xff00)) {
        *cmode = 0x2;
        *imm8 = (v32 >> 8) & 0xff;
        return true;
    } else if (v32 == (v32 & 0xff0000)) {
        *cmode = 0x4;
        *imm8 = (v32 >> 16) & 0xff;
        return true;
    } else if (v32 == (v32 & 0xff000000)) {
        *cmode = 0x6;
        *imm8 = v32 >> 24;
        return true;
    }
    return false;
}

static bool is_soimm32(uint32_t v32, int *cmode, int *imm8)
{
    if ((v32 & 0xffff00ff) == 0xff) {
        *cmode = 0xc;
        *imm8 = (v32 >> 8) & 0xff;
        return true;
    } else if ((v32 & 0xff00ffff) == 0xffff) {
        *cmode = 0xd;
        *imm8 = (v32 >> 16) & 0xff;
        return true;
    }
    return false;
}

static int is_shimm32_pair(uint32_t v32, int *cmode, int *imm8)
{
    int i;

    for (i = 6; i > 0; i -= 2) {
        uint32_t tmp = v32 & ~(0xffu << (i * 4));
        if (is_shimm32(tmp, cmode, imm8) ||
            is_soimm32(tmp, cmode, imm8)) {
            break;
        }
    }
    return i;
}

static bool is_shimm1632(uint32_t v32, int *cmode, int *imm8)
{
    if (v32 == deposit32(v32, 16, 16, v32)) {
        return is_shimm16(v32, cmode, imm8);
    } else {
        return is_shimm32(v32, cmode, imm8);
    }
}

static bool tcg_target_const_match(int64_t val, int ct,
                                   TCGType type, TCGCond cond, int vece)
{
    if (ct & TCG_CT_CONST) {
        return 1;
    } else if ((ct & TCG_CT_CONST_ARM) && check_fit_imm(val)) {
        return 1;
    } else if ((ct & TCG_CT_CONST_INV) && check_fit_imm(~val)) {
        return 1;
    } else if ((ct & TCG_CT_CONST_NEG) && check_fit_imm(-val)) {
        return 1;
    } else if ((ct & TCG_CT_CONST_ZERO) && val == 0) {
        return 1;
    }

    switch (ct & (TCG_CT_CONST_ORRI | TCG_CT_CONST_ANDI)) {
    case 0:
        break;
    case TCG_CT_CONST_ANDI:
        val = ~val;
        /* fallthru */
    case TCG_CT_CONST_ORRI:
        if (val == deposit64(val, 32, 32, val)) {
            int cmode, imm8;
            return is_shimm1632(val, &cmode, &imm8);
        }
        break;
    default:
        g_assert_not_reached();
    }

    return 0;
}

static void tcg_out_b_imm(TCGContext *s, ARMCond cond, int32_t offset)
{
    tcg_out32(s, (cond << 28) | INSN_B |
                    (((offset - 8) >> 2) & 0x00ffffff));
}

static void tcg_out_bl_imm(TCGContext *s, ARMCond cond, int32_t offset)
{
    tcg_out32(s, (cond << 28) | 0x0b000000 |
                    (((offset - 8) >> 2) & 0x00ffffff));
}

static void tcg_out_blx_reg(TCGContext *s, ARMCond cond, TCGReg rn)
{
    tcg_out32(s, (cond << 28) | 0x012fff30 | rn);
}

static void tcg_out_blx_imm(TCGContext *s, int32_t offset)
{
    tcg_out32(s, 0xfa000000 | ((offset & 2) << 23) |
                (((offset - 8) >> 2) & 0x00ffffff));
}

static void tcg_out_dat_reg(TCGContext *s, ARMCond cond, ARMInsn opc,
                            TCGReg rd, TCGReg rn, TCGReg rm, int shift)
{
    tcg_out32(s, (cond << 28) | (0 << 25) | opc |
                    (rn << 16) | (rd << 12) | shift | rm);
}

static void tcg_out_mov_reg(TCGContext *s, ARMCond cond, TCGReg rd, TCGReg rm)
{
    if (rd != rm) {
        tcg_out_dat_reg(s, cond, ARITH_MOV, rd, 0, rm, SHIFT_IMM_LSL(0));
    }
}

static void tcg_out_bx_reg(TCGContext *s, ARMCond cond, TCGReg rn)
{
    tcg_out32(s, (cond << 28) | 0x012fff10 | rn);
}

static void tcg_out_b_reg(TCGContext *s, ARMCond cond, TCGReg rn)
{
    tcg_out_bx_reg(s, cond, rn);
}

static void tcg_out_dat_imm(TCGContext *s, ARMCond cond, ARMInsn opc,
                            TCGReg rd, TCGReg rn, int im)
{
    tcg_out32(s, (cond << 28) | (1 << 25) | opc |
                    (rn << 16) | (rd << 12) | im);
}

static void tcg_out_ldstm(TCGContext *s, ARMCond cond, ARMInsn opc,
                          TCGReg rn, uint16_t mask)
{
    tcg_out32(s, (cond << 28) | opc | (rn << 16) | mask);
}

static void tcg_out_memop_r(TCGContext *s, ARMCond cond, ARMInsn opc, TCGReg rt,
                            TCGReg rn, TCGReg rm, bool u, bool p, bool w)
{
    tcg_out32(s, (cond << 28) | opc | (u << 23) | (p << 24)
              | (w << 21) | (rn << 16) | (rt << 12) | rm);
}

static void tcg_out_memop_8(TCGContext *s, ARMCond cond, ARMInsn opc, TCGReg rt,
                            TCGReg rn, int imm8, bool p, bool w)
{
    bool u = 1;
    if (imm8 < 0) {
        imm8 = -imm8;
        u = 0;
    }
    tcg_out32(s, (cond << 28) | opc | (u << 23) | (p << 24) | (w << 21) |
              (rn << 16) | (rt << 12) | ((imm8 & 0xf0) << 4) | (imm8 & 0xf));
}

static void tcg_out_memop_12(TCGContext *s, ARMCond cond, ARMInsn opc,
                             TCGReg rt, TCGReg rn, int imm12, bool p, bool w)
{
    bool u = 1;
    if (imm12 < 0) {
        imm12 = -imm12;
        u = 0;
    }
    tcg_out32(s, (cond << 28) | opc | (u << 23) | (p << 24) | (w << 21) |
              (rn << 16) | (rt << 12) | imm12);
}

static void tcg_out_ld32_12(TCGContext *s, ARMCond cond, TCGReg rt,
                            TCGReg rn, int imm12)
{
    tcg_out_memop_12(s, cond, INSN_LDR_IMM, rt, rn, imm12, 1, 0);
}

static void tcg_out_st32_12(TCGContext *s, ARMCond cond, TCGReg rt,
                            TCGReg rn, int imm12)
{
    tcg_out_memop_12(s, cond, INSN_STR_IMM, rt, rn, imm12, 1, 0);
}

static void tcg_out_ld32_r(TCGContext *s, ARMCond cond, TCGReg rt,
                           TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_LDR_REG, rt, rn, rm, 1, 1, 0);
}

static void tcg_out_st32_r(TCGContext *s, ARMCond cond, TCGReg rt,
                           TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_STR_REG, rt, rn, rm, 1, 1, 0);
}

static void tcg_out_ldrd_8(TCGContext *s, ARMCond cond, TCGReg rt,
                           TCGReg rn, int imm8)
{
    tcg_out_memop_8(s, cond, INSN_LDRD_IMM, rt, rn, imm8, 1, 0);
}

static void tcg_out_ldrd_r(TCGContext *s, ARMCond cond, TCGReg rt,
                           TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_LDRD_REG, rt, rn, rm, 1, 1, 0);
}

static void tcg_out_strd_8(TCGContext *s, ARMCond cond, TCGReg rt,
                           TCGReg rn, int imm8)
{
    tcg_out_memop_8(s, cond, INSN_STRD_IMM, rt, rn, imm8, 1, 0);
}

static void tcg_out_strd_r(TCGContext *s, ARMCond cond, TCGReg rt,
                           TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_STRD_REG, rt, rn, rm, 1, 1, 0);
}

static void tcg_out_ld32_rwb(TCGContext *s, ARMCond cond, TCGReg rt,
                             TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_LDR_REG, rt, rn, rm, 1, 1, 1);
}

static void tcg_out_st32_rwb(TCGContext *s, ARMCond cond, TCGReg rt,
                             TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_STR_REG, rt, rn, rm, 1, 1, 1);
}

static void tcg_out_ld16u_8(TCGContext *s, ARMCond cond, TCGReg rt,
                            TCGReg rn, int imm8)
{
    tcg_out_memop_8(s, cond, INSN_LDRH_IMM, rt, rn, imm8, 1, 0);
}

static void tcg_out_st16_8(TCGContext *s, ARMCond cond, TCGReg rt,
                           TCGReg rn, int imm8)
{
    tcg_out_memop_8(s, cond, INSN_STRH_IMM, rt, rn, imm8, 1, 0);
}

static void tcg_out_ld16u_r(TCGContext *s, ARMCond cond, TCGReg rt,
                            TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_LDRH_REG, rt, rn, rm, 1, 1, 0);
}

static void tcg_out_st16_r(TCGContext *s, ARMCond cond, TCGReg rt,
                           TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_STRH_REG, rt, rn, rm, 1, 1, 0);
}

static void tcg_out_ld16s_8(TCGContext *s, ARMCond cond, TCGReg rt,
                            TCGReg rn, int imm8)
{
    tcg_out_memop_8(s, cond, INSN_LDRSH_IMM, rt, rn, imm8, 1, 0);
}

static void tcg_out_ld16s_r(TCGContext *s, ARMCond cond, TCGReg rt,
                            TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_LDRSH_REG, rt, rn, rm, 1, 1, 0);
}

static void tcg_out_ld8_12(TCGContext *s, ARMCond cond, TCGReg rt,
                           TCGReg rn, int imm12)
{
    tcg_out_memop_12(s, cond, INSN_LDRB_IMM, rt, rn, imm12, 1, 0);
}

static void tcg_out_st8_12(TCGContext *s, ARMCond cond, TCGReg rt,
                           TCGReg rn, int imm12)
{
    tcg_out_memop_12(s, cond, INSN_STRB_IMM, rt, rn, imm12, 1, 0);
}

static void tcg_out_ld8_r(TCGContext *s, ARMCond cond, TCGReg rt,
                          TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_LDRB_REG, rt, rn, rm, 1, 1, 0);
}

static void tcg_out_st8_r(TCGContext *s, ARMCond cond, TCGReg rt,
                          TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_STRB_REG, rt, rn, rm, 1, 1, 0);
}

static void tcg_out_ld8s_8(TCGContext *s, ARMCond cond, TCGReg rt,
                           TCGReg rn, int imm8)
{
    tcg_out_memop_8(s, cond, INSN_LDRSB_IMM, rt, rn, imm8, 1, 0);
}

static void tcg_out_ld8s_r(TCGContext *s, ARMCond cond, TCGReg rt,
                           TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_LDRSB_REG, rt, rn, rm, 1, 1, 0);
}

static void tcg_out_movi_pool(TCGContext *s, ARMCond cond,
                              TCGReg rd, uint32_t arg)
{
    new_pool_label(s, arg, R_ARM_PC13, s->code_ptr, 0);
    tcg_out_ld32_12(s, cond, rd, TCG_REG_PC, 0);
}

static void tcg_out_movi32(TCGContext *s, ARMCond cond,
                           TCGReg rd, uint32_t arg)
{
    int imm12, diff, opc, sh1, sh2;
    uint32_t tt0, tt1, tt2;

    imm12 = encode_imm(arg);
    if (imm12 >= 0) {
        tcg_out_dat_imm(s, cond, ARITH_MOV, rd, 0, imm12);
        return;
    }
    imm12 = encode_imm(~arg);
    if (imm12 >= 0) {
        tcg_out_dat_imm(s, cond, ARITH_MVN, rd, 0, imm12);
        return;
    }

    diff = tcg_pcrel_diff(s, (void *)arg) - 8;
    if (diff >= 0) {
        imm12 = encode_imm(diff);
        if (imm12 >= 0) {
            tcg_out_dat_imm(s, cond, ARITH_ADD, rd, TCG_REG_PC, imm12);
            return;
        }
    } else {
        imm12 = encode_imm(-diff);
        if (imm12 >= 0) {
            tcg_out_dat_imm(s, cond, ARITH_SUB, rd, TCG_REG_PC, imm12);
            return;
        }
    }

    if (use_armv7_instructions) {
        tcg_out32(s, (cond << 28) | 0x03000000 | (rd << 12)
                  | ((arg << 4) & 0x000f0000) | (arg & 0xfff));
        if (arg & 0xffff0000) {
            tcg_out32(s, (cond << 28) | 0x03400000 | (rd << 12)
                      | ((arg >> 12) & 0x000f0000) | ((arg >> 16) & 0xfff));
        }
        return;
    }

    tt0 = arg;
    opc = ARITH_MOV;
    if (ctpop32(arg) > 16) {
        tt0 = ~arg;
        opc = ARITH_MVN;
    }
    sh1 = ctz32(tt0) & ~1;
    tt1 = tt0 & ~(0xff << sh1);
    sh2 = ctz32(tt1) & ~1;
    tt2 = tt1 & ~(0xff << sh2);
    if (tt2 == 0) {
        int rot;

        rot = ((32 - sh1) << 7) & 0xf00;
        tcg_out_dat_imm(s, cond, opc, rd,  0, ((tt0 >> sh1) & 0xff) | rot);
        rot = ((32 - sh2) << 7) & 0xf00;
        tcg_out_dat_imm(s, cond, ARITH_EOR, rd, rd,
                        ((tt0 >> sh2) & 0xff) | rot);
        return;
    }

    tcg_out_movi_pool(s, cond, rd, arg);
}

static void tcg_out_dat_rI(TCGContext *s, ARMCond cond, ARMInsn opc,
                           TCGReg dst, TCGReg lhs, TCGArg rhs, int rhs_is_const)
{
    if (rhs_is_const) {
        tcg_out_dat_imm(s, cond, opc, dst, lhs, encode_imm_nofail(rhs));
    } else {
        tcg_out_dat_reg(s, cond, opc, dst, lhs, rhs, SHIFT_IMM_LSL(0));
    }
}

static void tcg_out_dat_IK(TCGContext *s, ARMCond cond, ARMInsn opc,
                            ARMInsn opinv, TCGReg dst, TCGReg lhs, TCGArg rhs)
{
    int imm12 = encode_imm(rhs);
    if (imm12 < 0) {
        imm12 = encode_imm_nofail(~rhs);
        opc = opinv;
    }
    tcg_out_dat_imm(s, cond, opc, dst, lhs, imm12);
}

static void tcg_out_dat_rIK(TCGContext *s, ARMCond cond, ARMInsn opc,
                            ARMInsn opinv, TCGReg dst, TCGReg lhs, TCGArg rhs,
                            bool rhs_is_const)
{
    if (rhs_is_const) {
        tcg_out_dat_IK(s, cond, opc, opinv, dst, lhs, rhs);
    } else {
        tcg_out_dat_reg(s, cond, opc, dst, lhs, rhs, SHIFT_IMM_LSL(0));
    }
}

static void tcg_out_dat_IN(TCGContext *s, ARMCond cond, ARMInsn opc,
                           ARMInsn opneg, TCGReg dst, TCGReg lhs, TCGArg rhs)
{
    int imm12 = encode_imm(rhs);
    if (imm12 < 0) {
        imm12 = encode_imm_nofail(-rhs);
        opc = opneg;
    }
    tcg_out_dat_imm(s, cond, opc, dst, lhs, imm12);
}

static void tcg_out_dat_rIN(TCGContext *s, ARMCond cond, ARMInsn opc,
                            ARMInsn opneg, TCGReg dst, TCGReg lhs, TCGArg rhs,
                            bool rhs_is_const)
{
    if (rhs_is_const) {
        tcg_out_dat_IN(s, cond, opc, opneg, dst, lhs, rhs);
    } else {
        tcg_out_dat_reg(s, cond, opc, dst, lhs, rhs, SHIFT_IMM_LSL(0));
    }
}

static void tcg_out_ext8s(TCGContext *s, TCGType t, TCGReg rd, TCGReg rn)
{
    tcg_out32(s, 0x06af0070 | (COND_AL << 28) | (rd << 12) | rn);
}

static void tcg_out_ext8u(TCGContext *s, TCGReg rd, TCGReg rn)
{
    tcg_out_dat_imm(s, COND_AL, ARITH_AND, rd, rn, 0xff);
}

static void tcg_out_ext16s(TCGContext *s, TCGType t, TCGReg rd, TCGReg rn)
{
    tcg_out32(s, 0x06bf0070 | (COND_AL << 28) | (rd << 12) | rn);
}

static void tcg_out_ext16u(TCGContext *s, TCGReg rd, TCGReg rn)
{
    tcg_out32(s, 0x06ff0070 | (COND_AL << 28) | (rd << 12) | rn);
}

static void tcg_out_ext32s(TCGContext *s, TCGReg rd, TCGReg rn)
{
    g_assert_not_reached();
}

static void tcg_out_ext32u(TCGContext *s, TCGReg rd, TCGReg rn)
{
    g_assert_not_reached();
}

static void tcg_out_exts_i32_i64(TCGContext *s, TCGReg rd, TCGReg rn)
{
    g_assert_not_reached();
}

static void tcg_out_extu_i32_i64(TCGContext *s, TCGReg rd, TCGReg rn)
{
    g_assert_not_reached();
}

static void tcg_out_extrl_i64_i32(TCGContext *s, TCGReg rd, TCGReg rn)
{
    g_assert_not_reached();
}

static void tgen_deposit(TCGContext *s, TCGType type, TCGReg a0, TCGReg a1,
                         TCGReg a2, unsigned ofs, unsigned len)
{
    tcg_debug_assert(a0 == a1);
    tcg_out32(s, 0x07c00010 | (COND_AL << 28) | (a0 << 12) | a2
              | (ofs << 7) | ((ofs + len - 1) << 16));
}

static void tgen_depositi(TCGContext *s, TCGType type, TCGReg a0, TCGReg a1,
                          tcg_target_long a2, unsigned ofs, unsigned len)
{
    tgen_deposit(s, type, a0, a1, 15, ofs, len);
}

static const TCGOutOpDeposit outop_deposit = {
    .base.static_constraint = C_O1_I2(r, 0, rZ),
    .out_rrr = tgen_deposit,
    .out_rri = tgen_depositi,
};

static void tgen_extract(TCGContext *s, TCGType type, TCGReg rd, TCGReg rn,
                         unsigned ofs, unsigned len)
{
    if (ofs == 0 && len <= 8) {
        tcg_out_dat_imm(s, COND_AL, ARITH_AND, rd, rn,
                        encode_imm_nofail((1 << len) - 1));
        return;
    }

    if (use_armv7_instructions) {
        tcg_out32(s, 0x07e00050 | (COND_AL << 28) | (rd << 12) | rn
                  | (ofs << 7) | ((len - 1) << 16));
        return;
    }

    assert(ofs % 8 == 0);
    switch (len) {
    case 8:
        tcg_out32(s, 0x06ef0070 | (COND_AL << 28) |
                  (rd << 12) | (ofs << 7) | rn);
        break;
    case 16:
        tcg_out32(s, 0x06ff0070 | (COND_AL << 28) |
                  (rd << 12) | (ofs << 7) | rn);
        break;
    default:
        g_assert_not_reached();
    }
}

static const TCGOutOpExtract outop_extract = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_extract,
};

static void tgen_sextract(TCGContext *s, TCGType type, TCGReg rd, TCGReg rn,
                          unsigned ofs, unsigned len)
{
    if (use_armv7_instructions) {
        tcg_out32(s, 0x07a00050 | (COND_AL << 28) | (rd << 12) | rn
                  | (ofs << 7) | ((len - 1) << 16));
        return;
    }

    assert(ofs % 8 == 0);
    switch (len) {
    case 8:
        tcg_out32(s, 0x06af0070 | (COND_AL << 28) |
                  (rd << 12) | (ofs << 7) | rn);
        break;
    case 16:
        tcg_out32(s, 0x06bf0070 | (COND_AL << 28) |
                  (rd << 12) | (ofs << 7) | rn);
        break;
    default:
        g_assert_not_reached();
    }
}

static const TCGOutOpExtract outop_sextract = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_sextract,
};


static void tcg_out_ld32u(TCGContext *s, ARMCond cond,
                          TCGReg rd, TCGReg rn, int32_t offset)
{
    if (offset > 0xfff || offset < -0xfff) {
        tcg_out_movi32(s, cond, TCG_REG_TMP, offset);
        tcg_out_ld32_r(s, cond, rd, rn, TCG_REG_TMP);
    } else
        tcg_out_ld32_12(s, cond, rd, rn, offset);
}

static void tcg_out_st32(TCGContext *s, ARMCond cond,
                         TCGReg rd, TCGReg rn, int32_t offset)
{
    if (offset > 0xfff || offset < -0xfff) {
        tcg_out_movi32(s, cond, TCG_REG_TMP, offset);
        tcg_out_st32_r(s, cond, rd, rn, TCG_REG_TMP);
    } else
        tcg_out_st32_12(s, cond, rd, rn, offset);
}

static void tcg_out_goto(TCGContext *s, ARMCond cond, const tcg_insn_unit *addr)
{
    intptr_t addri = (intptr_t)addr;
    ptrdiff_t disp = tcg_pcrel_diff(s, addr);
    bool arm_mode = !(addri & 1);

    if (arm_mode && disp - 8 < 0x01fffffd && disp - 8 > -0x01fffffd) {
        tcg_out_b_imm(s, cond, disp);
        return;
    }

    tcg_out_movi_pool(s, cond, TCG_REG_PC, addri);
}

static void tcg_out_call_int(TCGContext *s, const tcg_insn_unit *addr)
{
    intptr_t addri = (intptr_t)addr;
    ptrdiff_t disp = tcg_pcrel_diff(s, addr);
    bool arm_mode = !(addri & 1);

    if (disp - 8 < 0x02000000 && disp - 8 >= -0x02000000) {
        if (arm_mode) {
            tcg_out_bl_imm(s, COND_AL, disp);
        } else {
            tcg_out_blx_imm(s, disp);
        }
        return;
    }

    tcg_out_movi32(s, COND_AL, TCG_REG_TMP, addri);
    tcg_out_blx_reg(s, COND_AL, TCG_REG_TMP);
}

static void tcg_out_call(TCGContext *s, const tcg_insn_unit *addr,
                         const TCGHelperInfo *info)
{
    tcg_out_call_int(s, addr);
}

static void tcg_out_goto_label(TCGContext *s, ARMCond cond, TCGLabel *l)
{
    if (l->has_value) {
        tcg_out_goto(s, cond, l->u.value_ptr);
    } else {
        tcg_out_reloc(s, s->code_ptr, R_ARM_PC24, l, 0);
        tcg_out_b_imm(s, cond, 0);
    }
}

static void tcg_out_br(TCGContext *s, TCGLabel *l)
{
    tcg_out_goto_label(s, COND_AL, l);
}

static void tcg_out_mb(TCGContext *s, unsigned a0)
{
    if (use_armv7_instructions) {
        tcg_out32(s, INSN_DMB_ISH);
    } else {
        tcg_out32(s, INSN_DMB_MCR);
    }
}

static TCGCond tgen_cmp(TCGContext *s, TCGCond cond, TCGReg a, TCGReg b)
{
    if (is_tst_cond(cond)) {
        tcg_out_dat_reg(s, COND_AL, ARITH_TST, 0, a, b, SHIFT_IMM_LSL(0));
        return tcg_tst_eqne_cond(cond);
    }
    tcg_out_dat_reg(s, COND_AL, ARITH_CMP, 0, a, b, SHIFT_IMM_LSL(0));
    return cond;
}

static TCGCond tgen_cmpi(TCGContext *s, TCGCond cond, TCGReg a, TCGArg b)
{
    int imm12;

    if (!is_tst_cond(cond)) {
        tcg_out_dat_IN(s, COND_AL, ARITH_CMP, ARITH_CMN, 0, a, b);
        return cond;
    }

    imm12 = encode_imm(b);
    if (imm12 >= 0) {
        tcg_out_dat_imm(s, COND_AL, ARITH_TST, 0, a, imm12);
    } else {
        tcg_out_movi32(s, COND_AL, TCG_REG_TMP, b);
        tcg_out_dat_reg(s, COND_AL, ARITH_TST, 0,
                        a, TCG_REG_TMP, SHIFT_IMM_LSL(0));
    }
    return tcg_tst_eqne_cond(cond);
}

static TCGCond tcg_out_cmp(TCGContext *s, TCGCond cond, TCGReg a,
                           TCGArg b, int b_const)
{
    if (b_const) {
        return tgen_cmpi(s, cond, a, b);
    } else {
        return tgen_cmp(s, cond, a, b);
    }
}

static TCGCond tcg_out_cmp2(TCGContext *s, TCGCond cond, TCGReg al, TCGReg ah,
                            TCGArg bl, bool const_bl, TCGArg bh, bool const_bh)
{
    switch (cond) {
    case TCG_COND_EQ:
    case TCG_COND_NE:
    case TCG_COND_LTU:
    case TCG_COND_LEU:
    case TCG_COND_GTU:
    case TCG_COND_GEU:
        tcg_out_dat_rI(s, COND_AL, ARITH_CMP, 0, ah, bh, const_bh);
        tcg_out_dat_rI(s, COND_EQ, ARITH_CMP, 0, al, bl, const_bl);
        return cond;

    case TCG_COND_TSTEQ:
    case TCG_COND_TSTNE:
        tcg_out_dat_rI(s, COND_AL, ARITH_TST, 0, ah, bh, const_bh);
        tcg_out_dat_rI(s, COND_EQ, ARITH_TST, 0, al, bl, const_bl);
        return tcg_tst_eqne_cond(cond);

    case TCG_COND_LT:
    case TCG_COND_GE:
        tcg_out_dat_rI(s, COND_AL, ARITH_CMP, 0, al, bl, const_bl);
        tcg_out_dat_rI(s, COND_AL, ARITH_SBC | TO_CPSR,
                       TCG_REG_TMP, ah, bh, const_bh);
        return cond;

    case TCG_COND_LE:
    case TCG_COND_GT:
        tcg_out_dat_rI(s, COND_AL, ARITH_RSB | TO_CPSR,
                       TCG_REG_TMP, al, bl, const_bl);
        tcg_out_dat_rI(s, COND_AL, ARITH_RSC | TO_CPSR,
                       TCG_REG_TMP, ah, bh, const_bh);
        return tcg_swap_cond(cond);

    default:
        g_assert_not_reached();
    }
}

static uint32_t encode_vd(TCGReg rd)
{
    tcg_debug_assert(rd >= TCG_REG_Q0);
    return (extract32(rd, 3, 1) << 22) | (extract32(rd, 0, 3) << 13);
}

static uint32_t encode_vn(TCGReg rn)
{
    tcg_debug_assert(rn >= TCG_REG_Q0);
    return (extract32(rn, 3, 1) << 7) | (extract32(rn, 0, 3) << 17);
}

static uint32_t encode_vm(TCGReg rm)
{
    tcg_debug_assert(rm >= TCG_REG_Q0);
    return (extract32(rm, 3, 1) << 5) | (extract32(rm, 0, 3) << 1);
}

static void tcg_out_vreg2(TCGContext *s, ARMInsn insn, int q, int vece,
                          TCGReg d, TCGReg m)
{
    tcg_out32(s, insn | (vece << 18) | (q << 6) |
              encode_vd(d) | encode_vm(m));
}

static void tcg_out_vreg3(TCGContext *s, ARMInsn insn, int q, int vece,
                          TCGReg d, TCGReg n, TCGReg m)
{
    tcg_out32(s, insn | (vece << 20) | (q << 6) |
              encode_vd(d) | encode_vn(n) | encode_vm(m));
}

static void tcg_out_vmovi(TCGContext *s, TCGReg rd,
                          int q, int op, int cmode, uint8_t imm8)
{
    tcg_out32(s, INSN_VMOVI | encode_vd(rd) | (q << 6) | (op << 5)
              | (cmode << 8) | extract32(imm8, 0, 4)
              | (extract32(imm8, 4, 3) << 16)
              | (extract32(imm8, 7, 1) << 24));
}

static void tcg_out_vshifti(TCGContext *s, ARMInsn insn, int q,
                            TCGReg rd, TCGReg rm, int l_imm6)
{
    tcg_out32(s, insn | (q << 6) | encode_vd(rd) | encode_vm(rm) |
              (extract32(l_imm6, 6, 1) << 7) |
              (extract32(l_imm6, 0, 6) << 16));
}

static void tcg_out_vldst(TCGContext *s, ARMInsn insn,
                          TCGReg rd, TCGReg rn, int offset)
{
    if (offset != 0) {
        if (check_fit_imm(offset) || check_fit_imm(-offset)) {
            tcg_out_dat_rIN(s, COND_AL, ARITH_ADD, ARITH_SUB,
                            TCG_REG_TMP, rn, offset, true);
        } else {
            tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_TMP, offset);
            tcg_out_dat_reg(s, COND_AL, ARITH_ADD,
                            TCG_REG_TMP, TCG_REG_TMP, rn, 0);
        }
        rn = TCG_REG_TMP;
    }
    tcg_out32(s, insn | (rn << 16) | encode_vd(rd) | 0xf);
}

typedef struct {
    ARMCond cond;
    TCGReg base;
    int index;
    bool index_scratch;
    TCGAtomAlign aa;
} HostAddress;

bool tcg_target_has_memory_bswap(MemOp memop)
{
    return false;
}

static TCGReg ldst_ra_gen(TCGContext *s, const TCGLabelQemuLdst *l, int arg)
{
    return TCG_REG_R14;
}

static const TCGLdstHelperParam ldst_helper_param = {
    .ra_gen = ldst_ra_gen,
    .ntmp = 1,
    .tmp = { TCG_REG_TMP },
};

static bool tcg_out_qemu_ld_slow_path(TCGContext *s, TCGLabelQemuLdst *lb)
{
    MemOp opc = get_memop(lb->oi);

    if (!reloc_pc24(lb->label_ptr[0], tcg_splitwx_to_rx(s->code_ptr))) {
        return false;
    }

    tcg_out_ld_helper_args(s, lb, &ldst_helper_param);
    tcg_out_call_int(s, qemu_ld_helpers[opc & MO_SIZE]);
    tcg_out_ld_helper_ret(s, lb, false, &ldst_helper_param);

    tcg_out_goto(s, COND_AL, lb->raddr);
    return true;
}

static bool tcg_out_qemu_st_slow_path(TCGContext *s, TCGLabelQemuLdst *lb)
{
    MemOp opc = get_memop(lb->oi);

    if (!reloc_pc24(lb->label_ptr[0], tcg_splitwx_to_rx(s->code_ptr))) {
        return false;
    }

    tcg_out_st_helper_args(s, lb, &ldst_helper_param);

    tcg_out_goto(s, COND_AL, qemu_st_helpers[opc & MO_SIZE]);
    return true;
}

#define MIN_TLB_MASK_TABLE_OFS  -256

static TCGLabelQemuLdst *prepare_host_addr(TCGContext *s, HostAddress *h,
                                           TCGReg addr, MemOpIdx oi, bool is_ld)
{
    TCGLabelQemuLdst *ldst = NULL;
    MemOp opc = get_memop(oi);
    unsigned a_mask;

    if (tcg_use_softmmu) {
        *h = (HostAddress){
            .cond = COND_AL,
            .base = addr,
            .index = TCG_REG_R1,
            .index_scratch = true,
        };
    } else {
        *h = (HostAddress){
            .cond = COND_AL,
            .base = addr,
            .index = guest_base ? TCG_REG_GUEST_BASE : -1,
            .index_scratch = false,
        };
    }

    h->aa = atom_and_align_for_opc(s, opc, MO_ATOM_IFALIGN, false);
    a_mask = (1 << h->aa.align) - 1;

    if (tcg_use_softmmu) {
        int mem_index = get_mmuidx(oi);
        int cmp_off = is_ld ? offsetof(CPUTLBEntry, addr_read)
                            : offsetof(CPUTLBEntry, addr_write);
        int fast_off = tlb_mask_table_ofs(s, mem_index);
        unsigned s_mask = (1 << (opc & MO_SIZE)) - 1;
        TCGReg t_addr;

        ldst = new_ldst_label(s);
        ldst->is_ld = is_ld;
        ldst->oi = oi;
        ldst->addr_reg = addr;

        QEMU_BUILD_BUG_ON(offsetof(CPUTLBDescFast, mask) != 0);
        QEMU_BUILD_BUG_ON(offsetof(CPUTLBDescFast, table) != 4);
        tcg_out_ldrd_8(s, COND_AL, TCG_REG_R0, TCG_AREG0, fast_off);

        tcg_out_dat_reg(s, COND_AL, ARITH_AND, TCG_REG_R0, TCG_REG_R0, addr,
                        SHIFT_IMM_LSR(TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS));

        if (cmp_off == 0) {
            tcg_out_ld32_rwb(s, COND_AL, TCG_REG_R2, TCG_REG_R1, TCG_REG_R0);
        } else {
            tcg_out_dat_reg(s, COND_AL, ARITH_ADD,
                            TCG_REG_R1, TCG_REG_R1, TCG_REG_R0, 0);
            tcg_out_ld32_12(s, COND_AL, TCG_REG_R2, TCG_REG_R1, cmp_off);
        }

        tcg_out_ld32_12(s, COND_AL, TCG_REG_R1, TCG_REG_R1,
                        offsetof(CPUTLBEntry, addend));

        t_addr = addr;
        if (a_mask < s_mask) {
            t_addr = TCG_REG_R0;
            tcg_out_dat_imm(s, COND_AL, ARITH_ADD, t_addr,
                            addr, s_mask - a_mask);
        }
        if (use_armv7_instructions && TARGET_PAGE_BITS <= 16) {
            tcg_out_movi32(s, COND_AL, TCG_REG_TMP, ~(TARGET_PAGE_MASK | a_mask));
            tcg_out_dat_reg(s, COND_AL, ARITH_BIC, TCG_REG_TMP,
                            t_addr, TCG_REG_TMP, 0);
            tcg_out_dat_reg(s, COND_AL, ARITH_CMP, 0,
                            TCG_REG_R2, TCG_REG_TMP, 0);
        } else {
            if (a_mask) {
                tcg_debug_assert(a_mask <= 0xff);
                tcg_out_dat_imm(s, COND_AL, ARITH_TST, 0, addr, a_mask);
            }
            tcg_out_dat_reg(s, COND_AL, ARITH_MOV, TCG_REG_TMP, 0, t_addr,
                            SHIFT_IMM_LSR(TARGET_PAGE_BITS));
            tcg_out_dat_reg(s, (a_mask ? COND_EQ : COND_AL), ARITH_CMP,
                            0, TCG_REG_R2, TCG_REG_TMP,
                            SHIFT_IMM_LSL(TARGET_PAGE_BITS));
        }
    } else if (a_mask) {
        ldst = new_ldst_label(s);
        ldst->is_ld = is_ld;
        ldst->oi = oi;
        ldst->addr_reg = addr;

        tcg_debug_assert(a_mask <= 0xff);
        tcg_out_dat_imm(s, COND_AL, ARITH_TST, 0, addr, a_mask);
    }

    return ldst;
}

static void tcg_out_qemu_ld_direct(TCGContext *s, MemOp opc, TCGReg datalo,
                                   TCGReg datahi, HostAddress h)
{
    TCGReg base;

    tcg_debug_assert((opc & MO_BSWAP) == 0);

    switch (opc & MO_SSIZE) {
    case MO_UB:
        if (h.index < 0) {
            tcg_out_ld8_12(s, h.cond, datalo, h.base, 0);
        } else {
            tcg_out_ld8_r(s, h.cond, datalo, h.base, h.index);
        }
        break;
    case MO_SB:
        if (h.index < 0) {
            tcg_out_ld8s_8(s, h.cond, datalo, h.base, 0);
        } else {
            tcg_out_ld8s_r(s, h.cond, datalo, h.base, h.index);
        }
        break;
    case MO_UW:
        if (h.index < 0) {
            tcg_out_ld16u_8(s, h.cond, datalo, h.base, 0);
        } else {
            tcg_out_ld16u_r(s, h.cond, datalo, h.base, h.index);
        }
        break;
    case MO_SW:
        if (h.index < 0) {
            tcg_out_ld16s_8(s, h.cond, datalo, h.base, 0);
        } else {
            tcg_out_ld16s_r(s, h.cond, datalo, h.base, h.index);
        }
        break;
    case MO_UL:
        if (h.index < 0) {
            tcg_out_ld32_12(s, h.cond, datalo, h.base, 0);
        } else {
            tcg_out_ld32_r(s, h.cond, datalo, h.base, h.index);
        }
        break;
    case MO_UQ:
        tcg_debug_assert((datalo & 1) == 0);
        tcg_debug_assert(datahi == datalo + 1);
        if (memop_alignment_bits(opc) >= MO_64) {
            if (h.index < 0) {
                tcg_out_ldrd_8(s, h.cond, datalo, h.base, 0);
                break;
            }
            if ((h.index & ~1) != datalo) {
                tcg_out_ldrd_r(s, h.cond, datalo, h.base, h.index);
                break;
            }
            if ((h.base & ~1) != datalo) {
                tcg_out_ldrd_r(s, h.cond, datalo, h.index, h.base);
                break;
            }
        }
        if (h.index < 0) {
            base = h.base;
            if (datalo == h.base) {
                tcg_out_mov_reg(s, h.cond, TCG_REG_TMP, base);
                base = TCG_REG_TMP;
            }
        } else if (h.index_scratch) {
            tcg_out_ld32_rwb(s, h.cond, datalo, h.index, h.base);
            tcg_out_ld32_12(s, h.cond, datahi, h.index, 4);
            break;
        } else {
            tcg_out_dat_reg(s, h.cond, ARITH_ADD, TCG_REG_TMP,
                            h.base, h.index, SHIFT_IMM_LSL(0));
            base = TCG_REG_TMP;
        }
        tcg_out_ld32_12(s, h.cond, datalo, base, 0);
        tcg_out_ld32_12(s, h.cond, datahi, base, 4);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tgen_qemu_ld(TCGContext *s, TCGType type, TCGReg data,
                         TCGReg addr, MemOpIdx oi)
{
    MemOp opc = get_memop(oi);
    TCGLabelQemuLdst *ldst;
    HostAddress h;

    ldst = prepare_host_addr(s, &h, addr, oi, true);
    if (ldst) {
        ldst->type = type;
        ldst->datalo_reg = data;
        ldst->datahi_reg = -1;

        ldst->label_ptr[0] = s->code_ptr;
        tcg_out_bl_imm(s, COND_NE, 0);
    }

    tcg_out_qemu_ld_direct(s, opc, data, -1, h);

    if (ldst) {
        ldst->raddr = tcg_splitwx_to_rx(s->code_ptr);
    }
}

static const TCGOutOpQemuLdSt outop_qemu_ld = {
    .base.static_constraint = C_O1_I1(r, q),
    .out = tgen_qemu_ld,
};

static void tgen_qemu_ld2(TCGContext *s, TCGType type, TCGReg datalo,
                          TCGReg datahi, TCGReg addr, MemOpIdx oi)
{
    MemOp opc = get_memop(oi);
    TCGLabelQemuLdst *ldst;
    HostAddress h;

    ldst = prepare_host_addr(s, &h, addr, oi, true);
    if (ldst) {
        ldst->type = type;
        ldst->datalo_reg = datalo;
        ldst->datahi_reg = datahi;

        ldst->label_ptr[0] = s->code_ptr;
        tcg_out_bl_imm(s, COND_NE, 0);
    }

    tcg_out_qemu_ld_direct(s, opc, datalo, datahi, h);

    if (ldst) {
        ldst->raddr = tcg_splitwx_to_rx(s->code_ptr);
    }
}

static const TCGOutOpQemuLdSt2 outop_qemu_ld2 = {
    .base.static_constraint = C_O2_I1(e, p, q),
    .out = tgen_qemu_ld2,
};

static void tcg_out_qemu_st_direct(TCGContext *s, MemOp opc, TCGReg datalo,
                                   TCGReg datahi, HostAddress h)
{
    tcg_debug_assert((opc & MO_BSWAP) == 0);

    switch (opc & MO_SIZE) {
    case MO_8:
        if (h.index < 0) {
            tcg_out_st8_12(s, h.cond, datalo, h.base, 0);
        } else {
            tcg_out_st8_r(s, h.cond, datalo, h.base, h.index);
        }
        break;
    case MO_16:
        if (h.index < 0) {
            tcg_out_st16_8(s, h.cond, datalo, h.base, 0);
        } else {
            tcg_out_st16_r(s, h.cond, datalo, h.base, h.index);
        }
        break;
    case MO_32:
        if (h.index < 0) {
            tcg_out_st32_12(s, h.cond, datalo, h.base, 0);
        } else {
            tcg_out_st32_r(s, h.cond, datalo, h.base, h.index);
        }
        break;
    case MO_64:
        tcg_debug_assert((datalo & 1) == 0);
        tcg_debug_assert(datahi == datalo + 1);
        if (memop_alignment_bits(opc) >= MO_64) {
            if (h.index < 0) {
                tcg_out_strd_8(s, h.cond, datalo, h.base, 0);
            } else {
                tcg_out_strd_r(s, h.cond, datalo, h.base, h.index);
            }
        } else if (h.index < 0) {
            tcg_out_st32_12(s, h.cond, datalo, h.base, 0);
            tcg_out_st32_12(s, h.cond, datahi, h.base, 4);
        } else if (h.index_scratch) {
            tcg_out_st32_rwb(s, h.cond, datalo, h.index, h.base);
            tcg_out_st32_12(s, h.cond, datahi, h.index, 4);
        } else {
            tcg_out_dat_reg(s, h.cond, ARITH_ADD, TCG_REG_TMP,
                            h.base, h.index, SHIFT_IMM_LSL(0));
            tcg_out_st32_12(s, h.cond, datalo, TCG_REG_TMP, 0);
            tcg_out_st32_12(s, h.cond, datahi, TCG_REG_TMP, 4);
        }
        break;
    default:
        g_assert_not_reached();
    }
}

static void tgen_qemu_st(TCGContext *s, TCGType type, TCGReg data,
                         TCGReg addr, MemOpIdx oi)
{
    MemOp opc = get_memop(oi);
    TCGLabelQemuLdst *ldst;
    HostAddress h;

    ldst = prepare_host_addr(s, &h, addr, oi, false);
    if (ldst) {
        ldst->type = type;
        ldst->datalo_reg = data;
        ldst->datahi_reg = -1;

        h.cond = COND_EQ;
        tcg_out_qemu_st_direct(s, opc, data, -1, h);

        ldst->label_ptr[0] = s->code_ptr;
        tcg_out_bl_imm(s, COND_NE, 0);
        ldst->raddr = tcg_splitwx_to_rx(s->code_ptr);
    } else {
        tcg_out_qemu_st_direct(s, opc, data, -1, h);
    }
}

static const TCGOutOpQemuLdSt outop_qemu_st = {
    .base.static_constraint = C_O0_I2(q, q),
    .out = tgen_qemu_st,
};

static void tgen_qemu_st2(TCGContext *s, TCGType type, TCGReg datalo,
                          TCGReg datahi, TCGReg addr, MemOpIdx oi)
{
    MemOp opc = get_memop(oi);
    TCGLabelQemuLdst *ldst;
    HostAddress h;

    ldst = prepare_host_addr(s, &h, addr, oi, false);
    if (ldst) {
        ldst->type = type;
        ldst->datalo_reg = datalo;
        ldst->datahi_reg = datahi;

        h.cond = COND_EQ;
        tcg_out_qemu_st_direct(s, opc, datalo, datahi, h);

        ldst->label_ptr[0] = s->code_ptr;
        tcg_out_bl_imm(s, COND_NE, 0);
        ldst->raddr = tcg_splitwx_to_rx(s->code_ptr);
    } else {
        tcg_out_qemu_st_direct(s, opc, datalo, datahi, h);
    }
}

static const TCGOutOpQemuLdSt2 outop_qemu_st2 = {
    .base.static_constraint = C_O0_I3(Q, p, q),
    .out = tgen_qemu_st2,
};

static void tcg_out_epilogue(TCGContext *s);

static void tcg_out_exit_tb(TCGContext *s, uintptr_t arg)
{
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R0, arg);
    tcg_out_epilogue(s);
}

static void tcg_out_goto_tb(TCGContext *s, int which)
{
    uintptr_t i_addr;
    intptr_t i_disp;

    set_jmp_insn_offset(s, which);
    tcg_out32(s, INSN_NOP);

    i_addr = get_jmp_target_addr(s, which);
    i_disp = tcg_pcrel_diff(s, (void *)i_addr) - 8;
    tcg_debug_assert(i_disp < 0);
    if (i_disp >= -0xfff) {
        tcg_out_ld32_12(s, COND_AL, TCG_REG_PC, TCG_REG_PC, i_disp);
    } else {
        int h = -i_disp;
        int l = -(h & 0xfff);

        h = encode_imm_nofail(h + l);
        tcg_out_dat_imm(s, COND_AL, ARITH_SUB, TCG_REG_R0, TCG_REG_PC, h);
        tcg_out_ld32_12(s, COND_AL, TCG_REG_PC, TCG_REG_R0, l);
    }
    set_jmp_reset_offset(s, which);
}

static void tcg_out_goto_ptr(TCGContext *s, TCGReg a0)
{
    tcg_out_b_reg(s, COND_AL, a0);
}

void tb_target_set_jmp_target(const TranslationBlock *tb, int n,
                              uintptr_t jmp_rx, uintptr_t jmp_rw)
{
    uintptr_t addr = tb->jmp_target_addr[n];
    ptrdiff_t offset = addr - (jmp_rx + 8);
    tcg_insn_unit insn;

    if (offset == sextract64(offset, 0, 26)) {
        insn = deposit32((COND_AL << 28) | INSN_B, 0, 24, offset >> 2);
    } else {
        insn = INSN_NOP;
    }

    qatomic_set((uint32_t *)jmp_rw, insn);
    flush_idcache_range(jmp_rx, jmp_rw, 4);
}


static void tgen_add(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_dat_reg(s, COND_AL, ARITH_ADD, a0, a1, a2, SHIFT_IMM_LSL(0));
}

static void tgen_addi(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tcg_out_dat_IN(s, COND_AL, ARITH_ADD, ARITH_SUB, a0, a1, a2);
}

static const TCGOutOpBinary outop_add = {
    .base.static_constraint = C_O1_I2(r, r, rIN),
    .out_rrr = tgen_add,
    .out_rri = tgen_addi,
};

static void tgen_addco(TCGContext *s, TCGType type,
                       TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_dat_reg(s, COND_AL, ARITH_ADD | TO_CPSR,
                    a0, a1, a2, SHIFT_IMM_LSL(0));
}

static void tgen_addco_imm(TCGContext *s, TCGType type,
                           TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tcg_out_dat_IN(s, COND_AL, ARITH_ADD | TO_CPSR, ARITH_SUB | TO_CPSR,
                   a0, a1, a2);
}

static const TCGOutOpBinary outop_addco = {
    .base.static_constraint = C_O1_I2(r, r, rIN),
    .out_rrr = tgen_addco,
    .out_rri = tgen_addco_imm,
};

static void tgen_addci(TCGContext *s, TCGType type,
                       TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_dat_reg(s, COND_AL, ARITH_ADC, a0, a1, a2, SHIFT_IMM_LSL(0));
}

static void tgen_addci_imm(TCGContext *s, TCGType type,
                           TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tcg_out_dat_IK(s, COND_AL, ARITH_ADC, ARITH_SBC, a0, a1, a2);
}

static const TCGOutOpAddSubCarry outop_addci = {
    .base.static_constraint = C_O1_I2(r, r, rIK),
    .out_rrr = tgen_addci,
    .out_rri = tgen_addci_imm,
};

static void tgen_addcio(TCGContext *s, TCGType type,
                        TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_dat_reg(s, COND_AL, ARITH_ADC | TO_CPSR,
                    a0, a1, a2, SHIFT_IMM_LSL(0));
}

static void tgen_addcio_imm(TCGContext *s, TCGType type,
                            TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tcg_out_dat_IK(s, COND_AL, ARITH_ADC | TO_CPSR, ARITH_SBC | TO_CPSR,
                   a0, a1, a2);
}

static const TCGOutOpBinary outop_addcio = {
    .base.static_constraint = C_O1_I2(r, r, rIK),
    .out_rrr = tgen_addcio,
    .out_rri = tgen_addcio_imm,
};

static void tcg_out_movi_apsr_c(TCGContext *s, bool c)
{
    int imm12 = encode_imm_nofail(c << 29);
    tcg_out32(s, (COND_AL << 28) | INSN_MSRI_CPSR | 0x80000 | imm12);
}

static void tcg_out_set_carry(TCGContext *s)
{
    tcg_out_movi_apsr_c(s, 1);
}

static void tgen_and(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_dat_reg(s, COND_AL, ARITH_AND, a0, a1, a2, SHIFT_IMM_LSL(0));
}

static void tgen_andi(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tcg_out_dat_IK(s, COND_AL, ARITH_AND, ARITH_BIC, a0, a1, a2);
}

static const TCGOutOpBinary outop_and = {
    .base.static_constraint = C_O1_I2(r, r, rIK),
    .out_rrr = tgen_and,
    .out_rri = tgen_andi,
};

static void tgen_andc(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_dat_reg(s, COND_AL, ARITH_BIC, a0, a1, a2, SHIFT_IMM_LSL(0));
}

static const TCGOutOpBinary outop_andc = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_andc,
};

static void tgen_clz(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_dat_imm(s, COND_AL, ARITH_CMP, 0, a1, 0);
    tcg_out_dat_reg(s, COND_NE, INSN_CLZ, a0, 0, a1, 0);
    tcg_out_mov_reg(s, COND_EQ, a0, a2);
}

static void tgen_clzi(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    if (a2 == 32) {
        tcg_out_dat_reg(s, COND_AL, INSN_CLZ, a0, 0, a1, 0);
    } else {
        tcg_out_dat_imm(s, COND_AL, ARITH_CMP, 0, a1, 0);
        tcg_out_dat_reg(s, COND_NE, INSN_CLZ, a0, 0, a1, 0);
        tcg_out_movi32(s, COND_EQ, a0, a2);
    }
}

static const TCGOutOpBinary outop_clz = {
    .base.static_constraint = C_O1_I2(r, r, rIK),
    .out_rrr = tgen_clz,
    .out_rri = tgen_clzi,
};

static const TCGOutOpUnary outop_ctpop = {
    .base.static_constraint = C_NotImplemented,
};

static void tgen_ctz(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_dat_reg(s, COND_AL, INSN_RBIT, TCG_REG_TMP, 0, a1, 0);
    tgen_clz(s, TCG_TYPE_I32, a0, TCG_REG_TMP, a2);
}

static void tgen_ctzi(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tcg_out_dat_reg(s, COND_AL, INSN_RBIT, TCG_REG_TMP, 0, a1, 0);
    tgen_clzi(s, TCG_TYPE_I32, a0, TCG_REG_TMP, a2);
}

static TCGConstraintSetIndex cset_ctz(TCGType type, unsigned flags)
{
    return use_armv7_instructions ? C_O1_I2(r, r, rIK) : C_NotImplemented;
}

static const TCGOutOpBinary outop_ctz = {
    .base.static_constraint = C_Dynamic,
    .base.dynamic_constraint = cset_ctz,
    .out_rrr = tgen_ctz,
    .out_rri = tgen_ctzi,
};

static TCGConstraintSetIndex cset_idiv(TCGType type, unsigned flags)
{
    return use_idiv_instructions ? C_O1_I2(r, r, r) : C_NotImplemented;
}

static void tgen_divs(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out32(s, 0x0710f010 | (COND_AL << 28) | (a0 << 16) | a1 | (a2 << 8));
}

static const TCGOutOpBinary outop_divs = {
    .base.static_constraint = C_Dynamic,
    .base.dynamic_constraint = cset_idiv,
    .out_rrr = tgen_divs,
};

static const TCGOutOpDivRem outop_divs2 = {
    .base.static_constraint = C_NotImplemented,
};

static void tgen_divu(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out32(s, 0x0730f010 | (COND_AL << 28) | (a0 << 16) | a1 | (a2 << 8));
}

static const TCGOutOpBinary outop_divu = {
    .base.static_constraint = C_Dynamic,
    .base.dynamic_constraint = cset_idiv,
    .out_rrr = tgen_divu,
};

static const TCGOutOpDivRem outop_divu2 = {
    .base.static_constraint = C_NotImplemented,
};

static const TCGOutOpBinary outop_eqv = {
    .base.static_constraint = C_NotImplemented,
};

static void tgen_mul(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out32(s, (COND_AL << 28) | 0x90 | (a0 << 16) | (a1 << 8) | a2);
}

static const TCGOutOpBinary outop_mul = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_mul,
};

static void tgen_muls2(TCGContext *s, TCGType type,
                       TCGReg rd0, TCGReg rd1, TCGReg rn, TCGReg rm)
{
    tcg_out32(s, (COND_AL << 28) | 0x00c00090 |
              (rd1 << 16) | (rd0 << 12) | (rm << 8) | rn);
}

static const TCGOutOpMul2 outop_muls2 = {
    .base.static_constraint = C_O2_I2(r, r, r, r),
    .out_rrrr = tgen_muls2,
};

static const TCGOutOpBinary outop_mulsh = {
    .base.static_constraint = C_NotImplemented,
};

static void tgen_mulu2(TCGContext *s, TCGType type,
                       TCGReg rd0, TCGReg rd1, TCGReg rn, TCGReg rm)
{
    tcg_out32(s, (COND_AL << 28) | 0x00800090 |
              (rd1 << 16) | (rd0 << 12) | (rm << 8) | rn);
}

static const TCGOutOpMul2 outop_mulu2 = {
    .base.static_constraint = C_O2_I2(r, r, r, r),
    .out_rrrr = tgen_mulu2,
};

static const TCGOutOpBinary outop_muluh = {
    .base.static_constraint = C_NotImplemented,
};

static const TCGOutOpBinary outop_nand = {
    .base.static_constraint = C_NotImplemented,
};

static const TCGOutOpBinary outop_nor = {
    .base.static_constraint = C_NotImplemented,
};

static void tgen_or(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_dat_reg(s, COND_AL, ARITH_ORR, a0, a1, a2, SHIFT_IMM_LSL(0));
}

static void tgen_ori(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tcg_out_dat_imm(s, COND_AL, ARITH_ORR, a0, a1, encode_imm_nofail(a2));
}

static const TCGOutOpBinary outop_or = {
    .base.static_constraint = C_O1_I2(r, r, rI),
    .out_rrr = tgen_or,
    .out_rri = tgen_ori,
};

static const TCGOutOpBinary outop_orc = {
    .base.static_constraint = C_NotImplemented,
};

static const TCGOutOpBinary outop_rems = {
    .base.static_constraint = C_NotImplemented,
};

static const TCGOutOpBinary outop_remu = {
    .base.static_constraint = C_NotImplemented,
};

static const TCGOutOpBinary outop_rotl = {
    .base.static_constraint = C_NotImplemented,
};

static void tgen_rotr(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_dat_reg(s, COND_AL, ARITH_MOV, a0, 0, a1, SHIFT_REG_ROR(a2));
}

static void tgen_rotri(TCGContext *s, TCGType type,
                       TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tcg_out_dat_reg(s, COND_AL, ARITH_MOV, a0, 0, a1, SHIFT_IMM_ROR(a2 & 0x1f));
}

static const TCGOutOpBinary outop_rotr = {
    .base.static_constraint = C_O1_I2(r, r, ri),
    .out_rrr = tgen_rotr,
    .out_rri = tgen_rotri,
};

static void tgen_sar(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_dat_reg(s, COND_AL, ARITH_MOV, a0, 0, a1, SHIFT_REG_ASR(a2));
}

static void tgen_sari(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tcg_out_dat_reg(s, COND_AL, ARITH_MOV, a0, 0, a1,
                    SHIFT_IMM_ASR(a2 & 0x1f));
}

static const TCGOutOpBinary outop_sar = {
    .base.static_constraint = C_O1_I2(r, r, ri),
    .out_rrr = tgen_sar,
    .out_rri = tgen_sari,
};

static void tgen_shl(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_dat_reg(s, COND_AL, ARITH_MOV, a0, 0, a1, SHIFT_REG_LSL(a2));
}

static void tgen_shli(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tcg_out_dat_reg(s, COND_AL, ARITH_MOV, a0, 0, a1,
                    SHIFT_IMM_LSL(a2 & 0x1f));
}

static const TCGOutOpBinary outop_shl = {
    .base.static_constraint = C_O1_I2(r, r, ri),
    .out_rrr = tgen_shl,
    .out_rri = tgen_shli,
};

static void tgen_shr(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_dat_reg(s, COND_AL, ARITH_MOV, a0, 0, a1, SHIFT_REG_LSR(a2));
}

static void tgen_shri(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tcg_out_dat_reg(s, COND_AL, ARITH_MOV, a0, 0, a1,
                    SHIFT_IMM_LSR(a2 & 0x1f));
}

static const TCGOutOpBinary outop_shr = {
    .base.static_constraint = C_O1_I2(r, r, ri),
    .out_rrr = tgen_shr,
    .out_rri = tgen_shri,
};

static void tgen_sub(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_dat_reg(s, COND_AL, ARITH_SUB, a0, a1, a2, SHIFT_IMM_LSL(0));
}

static void tgen_subfi(TCGContext *s, TCGType type,
                       TCGReg a0, tcg_target_long a1, TCGReg a2)
{
    tcg_out_dat_imm(s, COND_AL, ARITH_RSB, a0, a2, encode_imm_nofail(a1));
}

static const TCGOutOpSubtract outop_sub = {
    .base.static_constraint = C_O1_I2(r, rI, r),
    .out_rrr = tgen_sub,
    .out_rir = tgen_subfi,
};

static void tgen_subbo_rrr(TCGContext *s, TCGType type,
                           TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_dat_reg(s, COND_AL, ARITH_SUB | TO_CPSR,
                    a0, a1, a2, SHIFT_IMM_LSL(0));
}

static void tgen_subbo_rri(TCGContext *s, TCGType type,
                           TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tcg_out_dat_IN(s, COND_AL, ARITH_SUB | TO_CPSR, ARITH_ADD | TO_CPSR,
                   a0, a1, a2);
}

static void tgen_subbo_rir(TCGContext *s, TCGType type,
                           TCGReg a0, tcg_target_long a1, TCGReg a2)
{
    tcg_out_dat_imm(s, COND_AL, ARITH_RSB | TO_CPSR,
                    a0, a2, encode_imm_nofail(a1));
}

static void tgen_subbo_rii(TCGContext *s, TCGType type,
                           TCGReg a0, tcg_target_long a1, tcg_target_long a2)
{
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_TMP, a2);
    tgen_subbo_rir(s, TCG_TYPE_I32, a0, a1, TCG_REG_TMP);
}

static const TCGOutOpAddSubCarry outop_subbo = {
    .base.static_constraint = C_O1_I2(r, rI, rIN),
    .out_rrr = tgen_subbo_rrr,
    .out_rri = tgen_subbo_rri,
    .out_rir = tgen_subbo_rir,
    .out_rii = tgen_subbo_rii,
};

static void tgen_subbi_rrr(TCGContext *s, TCGType type,
                           TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_dat_reg(s, COND_AL, ARITH_SBC,
                    a0, a1, a2, SHIFT_IMM_LSL(0));
}

static void tgen_subbi_rri(TCGContext *s, TCGType type,
                           TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tcg_out_dat_IK(s, COND_AL, ARITH_SBC, ARITH_ADC, a0, a1, a2);
}

static void tgen_subbi_rir(TCGContext *s, TCGType type,
                           TCGReg a0, tcg_target_long a1, TCGReg a2)
{
    tcg_out_dat_imm(s, COND_AL, ARITH_RSC, a0, a2, encode_imm_nofail(a1));
}

static void tgen_subbi_rii(TCGContext *s, TCGType type,
                           TCGReg a0, tcg_target_long a1, tcg_target_long a2)
{
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_TMP, a2);
    tgen_subbi_rir(s, TCG_TYPE_I32, a0, a1, TCG_REG_TMP);
}

static const TCGOutOpAddSubCarry outop_subbi = {
    .base.static_constraint = C_O1_I2(r, rI, rIK),
    .out_rrr = tgen_subbi_rrr,
    .out_rri = tgen_subbi_rri,
    .out_rir = tgen_subbi_rir,
    .out_rii = tgen_subbi_rii,
};

static void tgen_subbio_rrr(TCGContext *s, TCGType type,
                            TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_dat_reg(s, COND_AL, ARITH_SBC | TO_CPSR,
                    a0, a1, a2, SHIFT_IMM_LSL(0));
}

static void tgen_subbio_rri(TCGContext *s, TCGType type,
                            TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tcg_out_dat_IK(s, COND_AL, ARITH_SBC | TO_CPSR, ARITH_ADC | TO_CPSR,
                   a0, a1, a2);
}

static void tgen_subbio_rir(TCGContext *s, TCGType type,
                            TCGReg a0, tcg_target_long a1, TCGReg a2)
{
    tcg_out_dat_imm(s, COND_AL, ARITH_RSC | TO_CPSR,
                    a0, a2, encode_imm_nofail(a1));
}

static void tgen_subbio_rii(TCGContext *s, TCGType type,
                            TCGReg a0, tcg_target_long a1, tcg_target_long a2)
{
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_TMP, a2);
    tgen_subbio_rir(s, TCG_TYPE_I32, a0, a1, TCG_REG_TMP);
}

static const TCGOutOpAddSubCarry outop_subbio = {
    .base.static_constraint = C_O1_I2(r, rI, rIK),
    .out_rrr = tgen_subbio_rrr,
    .out_rri = tgen_subbio_rri,
    .out_rir = tgen_subbio_rir,
    .out_rii = tgen_subbio_rii,
};

static void tcg_out_set_borrow(TCGContext *s)
{
    tcg_out_movi_apsr_c(s, 0);
}

static void tgen_xor(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_dat_reg(s, COND_AL, ARITH_EOR, a0, a1, a2, SHIFT_IMM_LSL(0));
}

static void tgen_xori(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tcg_out_dat_imm(s, COND_AL, ARITH_EOR, a0, a1, encode_imm_nofail(a2));
}

static const TCGOutOpBinary outop_xor = {
    .base.static_constraint = C_O1_I2(r, r, rI),
    .out_rrr = tgen_xor,
    .out_rri = tgen_xori,
};

static void tgen_bswap16(TCGContext *s, TCGType type,
                         TCGReg rd, TCGReg rn, unsigned flags)
{
    if (flags & TCG_BSWAP_OS) {
        tcg_out32(s, 0x06ff0fb0 | (COND_AL << 28) | (rd << 12) | rn);
        return;
    }

    tcg_out32(s, 0x06bf0fb0 | (COND_AL << 28) | (rd << 12) | rn);
    if ((flags & (TCG_BSWAP_IZ | TCG_BSWAP_OZ)) == TCG_BSWAP_OZ) {
        tcg_out_ext16u(s, rd, rd);
    }
}

static const TCGOutOpBswap outop_bswap16 = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_bswap16,
};

static void tgen_bswap32(TCGContext *s, TCGType type,
                         TCGReg rd, TCGReg rn, unsigned flags)
{
    tcg_out32(s, 0x06bf0f30 | (COND_AL << 28) | (rd << 12) | rn);
}

static const TCGOutOpBswap outop_bswap32 = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_bswap32,
};

static const TCGOutOpUnary outop_bswap64 = {
    .base.static_constraint = C_NotImplemented,
};

static void tgen_neg(TCGContext *s, TCGType type, TCGReg a0, TCGReg a1)
{
    tgen_subfi(s, type, a0, 0, a1);
}

static const TCGOutOpUnary outop_neg = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_neg,
};

static void tgen_not(TCGContext *s, TCGType type, TCGReg a0, TCGReg a1)
{
    tcg_out_dat_reg(s, COND_AL, ARITH_MVN, a0, 0, a1, SHIFT_IMM_LSL(0));
}

static const TCGOutOpUnary outop_not = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_not,
};

static void tgen_brcond(TCGContext *s, TCGType type, TCGCond cond,
                        TCGReg a0, TCGReg a1, TCGLabel *l)
{
    cond = tgen_cmp(s, cond, a0, a1);
    tcg_out_goto_label(s, tcg_cond_to_arm_cond[cond], l);
}

static void tgen_brcondi(TCGContext *s, TCGType type, TCGCond cond,
                         TCGReg a0, tcg_target_long a1, TCGLabel *l)
{
    cond = tgen_cmpi(s, cond, a0, a1);
    tcg_out_goto_label(s, tcg_cond_to_arm_cond[cond], l);
}

static const TCGOutOpBrcond outop_brcond = {
    .base.static_constraint = C_O0_I2(r, rIN),
    .out_rr = tgen_brcond,
    .out_ri = tgen_brcondi,
};

static void finish_setcond(TCGContext *s, TCGCond cond, TCGReg ret, bool neg)
{
    tcg_out_movi32(s, tcg_cond_to_arm_cond[tcg_invert_cond(cond)], ret, 0);
    tcg_out_movi32(s, tcg_cond_to_arm_cond[cond], ret, neg ? -1 : 1);
}

static void tgen_setcond(TCGContext *s, TCGType type, TCGCond cond,
                         TCGReg a0, TCGReg a1, TCGReg a2)
{
    cond = tgen_cmp(s, cond, a1, a2);
    finish_setcond(s, cond, a0, false);
}

static void tgen_setcondi(TCGContext *s, TCGType type, TCGCond cond,
                          TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    cond = tgen_cmpi(s, cond, a1, a2);
    finish_setcond(s, cond, a0, false);
}

static const TCGOutOpSetcond outop_setcond = {
    .base.static_constraint = C_O1_I2(r, r, rIN),
    .out_rrr = tgen_setcond,
    .out_rri = tgen_setcondi,
};

static void tgen_negsetcond(TCGContext *s, TCGType type, TCGCond cond,
                            TCGReg a0, TCGReg a1, TCGReg a2)
{
    cond = tgen_cmp(s, cond, a1, a2);
    finish_setcond(s, cond, a0, true);
}

static void tgen_negsetcondi(TCGContext *s, TCGType type, TCGCond cond,
                             TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    cond = tgen_cmpi(s, cond, a1, a2);
    finish_setcond(s, cond, a0, true);
}

static const TCGOutOpSetcond outop_negsetcond = {
    .base.static_constraint = C_O1_I2(r, r, rIN),
    .out_rrr = tgen_negsetcond,
    .out_rri = tgen_negsetcondi,
};

static void tgen_movcond(TCGContext *s, TCGType type, TCGCond cond,
                         TCGReg ret, TCGReg c1, TCGArg c2, bool const_c2,
                         TCGArg vt, bool const_vt, TCGArg vf, bool consf_vf)
{
    cond = tcg_out_cmp(s, cond, c1, c2, const_c2);
    tcg_out_dat_rIK(s, tcg_cond_to_arm_cond[cond], ARITH_MOV, ARITH_MVN,
                    ret, 0, vt, const_vt);
}

static const TCGOutOpMovcond outop_movcond = {
    .base.static_constraint = C_O1_I4(r, r, rIN, rIK, 0),
    .out = tgen_movcond,
};

static void tgen_brcond2(TCGContext *s, TCGCond cond, TCGReg al, TCGReg ah,
                         TCGArg bl, bool const_bl, TCGArg bh, bool const_bh,
                         TCGLabel *l)
{
    cond = tcg_out_cmp2(s, cond, al, ah, bl, const_bl, bh, const_bh);
    tcg_out_goto_label(s, tcg_cond_to_arm_cond[cond], l);
}

static const TCGOutOpBrcond2 outop_brcond2 = {
    .base.static_constraint = C_O0_I4(r, r, rI, rI),
    .out = tgen_brcond2,
};

static void tgen_setcond2(TCGContext *s, TCGCond cond, TCGReg ret,
                          TCGReg al, TCGReg ah,
                          TCGArg bl, bool const_bl,
                          TCGArg bh, bool const_bh)
{
    cond = tcg_out_cmp2(s, cond, al, ah, bl, const_bl, bh, const_bh);
    finish_setcond(s, cond, ret, false);
}

static const TCGOutOpSetcond2 outop_setcond2 = {
    .base.static_constraint = C_O1_I4(r, r, r, rI, rI),
    .out = tgen_setcond2,
};

static void tgen_extract2(TCGContext *s, TCGType type, TCGReg a0,
                          TCGReg a1, TCGReg a2, unsigned shr)
{
    tgen_shli(s, TCG_TYPE_I32, TCG_REG_TMP, a2, 32 - shr);
    tcg_out_dat_reg(s, COND_AL, ARITH_ORR, a0, TCG_REG_TMP,
                    a1, SHIFT_IMM_LSR(shr));
}

static const TCGOutOpExtract2 outop_extract2 = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_extract2,
};

static void tgen_ld8u(TCGContext *s, TCGType type, TCGReg rd,
                      TCGReg rn, ptrdiff_t offset)
{
    if (offset > 0xfff || offset < -0xfff) {
        tcg_out_movi32(s, COND_AL, TCG_REG_TMP, offset);
        tcg_out_ld8_r(s, COND_AL, rd, rn, TCG_REG_TMP);
    } else {
        tcg_out_ld8_12(s, COND_AL, rd, rn, offset);
    }
}

static const TCGOutOpLoad outop_ld8u = {
    .base.static_constraint = C_O1_I1(r, r),
    .out = tgen_ld8u,
};

static void tgen_ld8s(TCGContext *s, TCGType type, TCGReg rd,
                      TCGReg rn, ptrdiff_t offset)
{
    if (offset > 0xff || offset < -0xff) {
        tcg_out_movi32(s, COND_AL, TCG_REG_TMP, offset);
        tcg_out_ld8s_r(s, COND_AL, rd, rn, TCG_REG_TMP);
    } else {
        tcg_out_ld8s_8(s, COND_AL, rd, rn, offset);
    }
}

static const TCGOutOpLoad outop_ld8s = {
    .base.static_constraint = C_O1_I1(r, r),
    .out = tgen_ld8s,
};

static void tgen_ld16u(TCGContext *s, TCGType type, TCGReg rd,
                       TCGReg rn, ptrdiff_t offset)
{
    if (offset > 0xff || offset < -0xff) {
        tcg_out_movi32(s, COND_AL, TCG_REG_TMP, offset);
        tcg_out_ld16u_r(s, COND_AL, rd, rn, TCG_REG_TMP);
    } else {
        tcg_out_ld16u_8(s, COND_AL, rd, rn, offset);
    }
}

static const TCGOutOpLoad outop_ld16u = {
    .base.static_constraint = C_O1_I1(r, r),
    .out = tgen_ld16u,
};

static void tgen_ld16s(TCGContext *s, TCGType type, TCGReg rd,
                       TCGReg rn, ptrdiff_t offset)
{
    if (offset > 0xff || offset < -0xff) {
        tcg_out_movi32(s, COND_AL, TCG_REG_TMP, offset);
        tcg_out_ld16s_r(s, COND_AL, rd, rn, TCG_REG_TMP);
    } else {
        tcg_out_ld16s_8(s, COND_AL, rd, rn, offset);
    }
}

static const TCGOutOpLoad outop_ld16s = {
    .base.static_constraint = C_O1_I1(r, r),
    .out = tgen_ld16s,
};

static void tgen_st8(TCGContext *s, TCGType type, TCGReg rd,
                     TCGReg rn, ptrdiff_t offset)
{
    if (offset > 0xfff || offset < -0xfff) {
        tcg_out_movi32(s, COND_AL, TCG_REG_TMP, offset);
        tcg_out_st8_r(s, COND_AL, rd, rn, TCG_REG_TMP);
    } else {
        tcg_out_st8_12(s, COND_AL, rd, rn, offset);
    }
}

static const TCGOutOpStore outop_st8 = {
    .base.static_constraint = C_O0_I2(r, r),
    .out_r = tgen_st8,
};

static void tgen_st16(TCGContext *s, TCGType type, TCGReg rd,
                      TCGReg rn, ptrdiff_t offset)
{
    if (offset > 0xff || offset < -0xff) {
        tcg_out_movi32(s, COND_AL, TCG_REG_TMP, offset);
        tcg_out_st16_r(s, COND_AL, rd, rn, TCG_REG_TMP);
    } else {
        tcg_out_st16_8(s, COND_AL, rd, rn, offset);
    }
}

static const TCGOutOpStore outop_st16 = {
    .base.static_constraint = C_O0_I2(r, r),
    .out_r = tgen_st16,
};

static const TCGOutOpStore outop_st = {
    .base.static_constraint = C_O0_I2(r, r),
    .out_r = tcg_out_st,
};

static TCGConstraintSetIndex
tcg_target_op_def(TCGOpcode op, TCGType type, unsigned flags)
{
    switch (op) {
    case INDEX_op_st_vec:
        return C_O0_I2(w, r);
    case INDEX_op_ld_vec:
    case INDEX_op_dupm_vec:
        return C_O1_I1(w, r);
    case INDEX_op_dup_vec:
        return C_O1_I1(w, wr);
    case INDEX_op_abs_vec:
    case INDEX_op_neg_vec:
    case INDEX_op_not_vec:
    case INDEX_op_shli_vec:
    case INDEX_op_shri_vec:
    case INDEX_op_sari_vec:
        return C_O1_I1(w, w);
    case INDEX_op_dup2_vec:
    case INDEX_op_add_vec:
    case INDEX_op_mul_vec:
    case INDEX_op_smax_vec:
    case INDEX_op_smin_vec:
    case INDEX_op_ssadd_vec:
    case INDEX_op_sssub_vec:
    case INDEX_op_sub_vec:
    case INDEX_op_umax_vec:
    case INDEX_op_umin_vec:
    case INDEX_op_usadd_vec:
    case INDEX_op_ussub_vec:
    case INDEX_op_xor_vec:
    case INDEX_op_arm_sshl_vec:
    case INDEX_op_arm_ushl_vec:
        return C_O1_I2(w, w, w);
    case INDEX_op_arm_sli_vec:
        return C_O1_I2(w, 0, w);
    case INDEX_op_or_vec:
    case INDEX_op_andc_vec:
        return C_O1_I2(w, w, wO);
    case INDEX_op_and_vec:
    case INDEX_op_orc_vec:
        return C_O1_I2(w, w, wV);
    case INDEX_op_cmp_vec:
        return C_O1_I2(w, w, wZ);
    case INDEX_op_bitsel_vec:
        return C_O1_I3(w, w, w, w);
    default:
        return C_NotImplemented;
    }
}

static void tcg_target_init(TCGContext *s)
{
#if !defined(use_idiv_instructions) || !defined(use_neon_instructions)
    {
        unsigned long hwcap = qemu_getauxval(AT_HWCAP);
#ifndef use_idiv_instructions
        use_idiv_instructions = (hwcap & HWCAP_ARM_IDIVA) != 0;
#endif
#ifndef use_neon_instructions
        use_neon_instructions = (hwcap & HWCAP_ARM_NEON) != 0;
#endif
    }
#endif

    if (__ARM_ARCH < 7) {
        const char *pl = (const char *)qemu_getauxval(AT_PLATFORM);
        if (pl != NULL && pl[0] == 'v' && pl[1] >= '4' && pl[1] <= '9') {
            arm_arch = pl[1] - '0';
        }

        if (arm_arch < 6) {
            error_report("TCG: ARMv%d is unsupported; exiting", arm_arch);
            exit(EXIT_FAILURE);
        }
    }

    tcg_target_available_regs[TCG_TYPE_I32] = ALL_GENERAL_REGS;

    tcg_target_call_clobber_regs = 0;
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R0);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R1);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R2);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R3);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R12);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R14);

    if (use_neon_instructions) {
        tcg_target_available_regs[TCG_TYPE_V64]  = ALL_VECTOR_REGS;
        tcg_target_available_regs[TCG_TYPE_V128] = ALL_VECTOR_REGS;

        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_Q0);
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_Q1);
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_Q2);
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_Q3);
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_Q8);
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_Q9);
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_Q10);
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_Q11);
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_Q12);
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_Q13);
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_Q14);
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_Q15);
    }

    s->reserved_regs = 0;
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_CALL_STACK);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_TMP);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_PC);
    tcg_regset_set_reg(s->reserved_regs, TCG_VEC_TMP);
}

static void tcg_out_ld(TCGContext *s, TCGType type, TCGReg arg,
                       TCGReg arg1, intptr_t arg2)
{
    switch (type) {
    case TCG_TYPE_I32:
        tcg_out_ld32u(s, COND_AL, arg, arg1, arg2);
        return;
    case TCG_TYPE_V64:
        tcg_out_vldst(s, INSN_VLD1 | 0x7d0, arg, arg1, arg2);
        return;
    case TCG_TYPE_V128:
        tcg_out_vldst(s, INSN_VLD1 | 0xad0, arg, arg1, arg2);
        return;
    default:
        g_assert_not_reached();
    }
}

static void tcg_out_st(TCGContext *s, TCGType type, TCGReg arg,
                       TCGReg arg1, intptr_t arg2)
{
    switch (type) {
    case TCG_TYPE_I32:
        tcg_out_st32(s, COND_AL, arg, arg1, arg2);
        return;
    case TCG_TYPE_V64:
        tcg_out_vldst(s, INSN_VST1 | 0x7d0, arg, arg1, arg2);
        return;
    case TCG_TYPE_V128:
        tcg_out_vldst(s, INSN_VST1 | 0xad0, arg, arg1, arg2);
        return;
    default:
        g_assert_not_reached();
    }
}

static bool tcg_out_sti(TCGContext *s, TCGType type, TCGArg val,
                        TCGReg base, intptr_t ofs)
{
    return false;
}

static bool tcg_out_mov(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg)
{
    if (ret == arg) {
        return true;
    }
    switch (type) {
    case TCG_TYPE_I32:
        if (ret < TCG_REG_Q0 && arg < TCG_REG_Q0) {
            tcg_out_mov_reg(s, COND_AL, ret, arg);
            return true;
        }
        return false;

    case TCG_TYPE_V64:
    case TCG_TYPE_V128:
        tcg_out_vreg3(s, INSN_VORR, type - TCG_TYPE_V64, 0, ret, arg, arg);
        return true;

    default:
        g_assert_not_reached();
    }
}

static void tcg_out_movi(TCGContext *s, TCGType type,
                         TCGReg ret, tcg_target_long arg)
{
    tcg_debug_assert(type == TCG_TYPE_I32);
    tcg_debug_assert(ret < TCG_REG_Q0);
    tcg_out_movi32(s, COND_AL, ret, arg);
}

static bool tcg_out_xchg(TCGContext *s, TCGType type, TCGReg r1, TCGReg r2)
{
    return false;
}

static void tcg_out_addi_ptr(TCGContext *s, TCGReg rd, TCGReg rs,
                             tcg_target_long imm)
{
    int enc, opc = ARITH_ADD;

    if (imm < 0) {
        imm = -imm;
        opc = ARITH_SUB;
    }
    enc = encode_imm(imm);
    if (enc >= 0) {
        tcg_out_dat_imm(s, COND_AL, opc, rd, rs, enc);
    } else {
        tcg_out_movi32(s, COND_AL, TCG_REG_TMP, imm);
        tcg_out_dat_reg(s, COND_AL, opc, rd, rs,
                        TCG_REG_TMP, SHIFT_IMM_LSL(0));
    }
}

static void tcg_out_dup2_vec(TCGContext *s, TCGReg rd, TCGReg rl, TCGReg rh)
{
    tcg_out_vreg3(s, INSN_VORR | (1 << 12), 0, 0, rd, rh, rh);
    tcg_out_mov(s, TCG_TYPE_V64, rd, rl);
}

static bool tcg_out_dup_vec(TCGContext *s, TCGType type, unsigned vece,
                            TCGReg rd, TCGReg rs)
{
    int q = type - TCG_TYPE_V64;

    if (vece == MO_64) {
        if (type == TCG_TYPE_V128) {
            tcg_out_dup2_vec(s, rd, rs, rs);
        } else {
            tcg_out_mov(s, TCG_TYPE_V64, rd, rs);
        }
    } else if (rs < TCG_REG_Q0) {
        int b = (vece == MO_8);
        int e = (vece == MO_16);
        tcg_out32(s, INSN_VDUP_G | (b << 22) | (q << 21) | (e << 5) |
                  encode_vn(rd) | (rs << 12));
    } else {
        int imm4 = 1 << vece;
        tcg_out32(s, INSN_VDUP_S | (imm4 << 16) | (q << 6) |
                  encode_vd(rd) | encode_vm(rs));
    }
    return true;
}

static bool tcg_out_dupm_vec(TCGContext *s, TCGType type, unsigned vece,
                             TCGReg rd, TCGReg base, intptr_t offset)
{
    if (vece == MO_64) {
        tcg_out_ld(s, TCG_TYPE_V64, rd, base, offset);
        if (type == TCG_TYPE_V128) {
            tcg_out_dup2_vec(s, rd, rd, rd);
        }
    } else {
        int q = type - TCG_TYPE_V64;
        tcg_out_vldst(s, INSN_VLD1R | (vece << 6) | (q << 5),
                      rd, base, offset);
    }
    return true;
}

static void tcg_out_dupi_vec(TCGContext *s, TCGType type, unsigned vece,
                             TCGReg rd, int64_t v64)
{
    int q = type - TCG_TYPE_V64;
    int cmode, imm8, i;

    if (vece == MO_8) {
        tcg_out_vmovi(s, rd, q, 0, 0xe, v64);
        return;
    }

    for (i = imm8 = 0; i < 8; i++) {
        uint8_t byte = v64 >> (i * 8);
        if (byte == 0xff) {
            imm8 |= 1 << i;
        } else if (byte != 0) {
            goto fail_bytes;
        }
    }
    tcg_out_vmovi(s, rd, q, 1, 0xe, imm8);
    return;
 fail_bytes:

    if (vece == MO_16) {
        uint16_t v16 = v64;

        if (is_shimm16(v16, &cmode, &imm8)) {
            tcg_out_vmovi(s, rd, q, 0, cmode, imm8);
            return;
        }
        if (is_shimm16(~v16, &cmode, &imm8)) {
            tcg_out_vmovi(s, rd, q, 1, cmode, imm8);
            return;
        }

        tcg_out_vmovi(s, rd, q, 0, 0x8, v16 & 0xff);
        tcg_out_vmovi(s, rd, q, 0, 0xb, v16 >> 8);
        return;
    }

    if (vece == MO_32) {
        uint32_t v32 = v64;

        if (is_shimm32(v32, &cmode, &imm8) ||
            is_soimm32(v32, &cmode, &imm8)) {
            tcg_out_vmovi(s, rd, q, 0, cmode, imm8);
            return;
        }
        if (is_shimm32(~v32, &cmode, &imm8) ||
            is_soimm32(~v32, &cmode, &imm8)) {
            tcg_out_vmovi(s, rd, q, 1, cmode, imm8);
            return;
        }

        i = is_shimm32_pair(v32, &cmode, &imm8);
        if (i) {
            tcg_out_vmovi(s, rd, q, 0, cmode, imm8);
            tcg_out_vmovi(s, rd, q, 0, i | 1, extract32(v32, i * 4, 8));
            return;
        }
        i = is_shimm32_pair(~v32, &cmode, &imm8);
        if (i) {
            tcg_out_vmovi(s, rd, q, 1, cmode, imm8);
            tcg_out_vmovi(s, rd, q, 1, i | 1, extract32(~v32, i * 4, 8));
            return;
        }
    }

    if (!q || vece == MO_64) {
        new_pool_l2(s, R_ARM_PC11, s->code_ptr, 0, v64, v64 >> 32);
        tcg_out32(s, INSN_VLDR_D | encode_vd(rd) | (0xf << 16));
        if (q) {
            tcg_out_dup2_vec(s, rd, rd, rd);
        }
    } else {
        new_pool_label(s, (uint32_t)v64, R_ARM_PC8, s->code_ptr, 0);
        tcg_out_dat_imm(s, COND_AL, ARITH_ADD, TCG_REG_TMP, TCG_REG_PC, 0);
        tcg_out_dupm_vec(s, type, MO_32, rd, TCG_REG_TMP, 0);
    }
}

static const ARMInsn vec_cmp_insn[16] = {
    [TCG_COND_EQ] = INSN_VCEQ,
    [TCG_COND_GT] = INSN_VCGT,
    [TCG_COND_GE] = INSN_VCGE,
    [TCG_COND_GTU] = INSN_VCGT_U,
    [TCG_COND_GEU] = INSN_VCGE_U,
};

static const ARMInsn vec_cmp0_insn[16] = {
    [TCG_COND_EQ] = INSN_VCEQ0,
    [TCG_COND_GT] = INSN_VCGT0,
    [TCG_COND_GE] = INSN_VCGE0,
    [TCG_COND_LT] = INSN_VCLT0,
    [TCG_COND_LE] = INSN_VCLE0,
};

static void tcg_out_vec_op(TCGContext *s, TCGOpcode opc,
                           unsigned vecl, unsigned vece,
                           const TCGArg args[TCG_MAX_OP_ARGS],
                           const int const_args[TCG_MAX_OP_ARGS])
{
    TCGType type = vecl + TCG_TYPE_V64;
    unsigned q = vecl;
    TCGArg a0, a1, a2, a3;
    int cmode, imm8;

    a0 = args[0];
    a1 = args[1];
    a2 = args[2];

    switch (opc) {
    case INDEX_op_ld_vec:
        tcg_out_ld(s, type, a0, a1, a2);
        return;
    case INDEX_op_st_vec:
        tcg_out_st(s, type, a0, a1, a2);
        return;
    case INDEX_op_dupm_vec:
        tcg_out_dupm_vec(s, type, vece, a0, a1, a2);
        return;
    case INDEX_op_dup2_vec:
        tcg_out_dup2_vec(s, a0, a1, a2);
        return;
    case INDEX_op_abs_vec:
        tcg_out_vreg2(s, INSN_VABS, q, vece, a0, a1);
        return;
    case INDEX_op_neg_vec:
        tcg_out_vreg2(s, INSN_VNEG, q, vece, a0, a1);
        return;
    case INDEX_op_not_vec:
        tcg_out_vreg2(s, INSN_VMVN, q, 0, a0, a1);
        return;
    case INDEX_op_add_vec:
        tcg_out_vreg3(s, INSN_VADD, q, vece, a0, a1, a2);
        return;
    case INDEX_op_mul_vec:
        tcg_out_vreg3(s, INSN_VMUL, q, vece, a0, a1, a2);
        return;
    case INDEX_op_smax_vec:
        tcg_out_vreg3(s, INSN_VMAX, q, vece, a0, a1, a2);
        return;
    case INDEX_op_smin_vec:
        tcg_out_vreg3(s, INSN_VMIN, q, vece, a0, a1, a2);
        return;
    case INDEX_op_sub_vec:
        tcg_out_vreg3(s, INSN_VSUB, q, vece, a0, a1, a2);
        return;
    case INDEX_op_ssadd_vec:
        tcg_out_vreg3(s, INSN_VQADD, q, vece, a0, a1, a2);
        return;
    case INDEX_op_sssub_vec:
        tcg_out_vreg3(s, INSN_VQSUB, q, vece, a0, a1, a2);
        return;
    case INDEX_op_umax_vec:
        tcg_out_vreg3(s, INSN_VMAX_U, q, vece, a0, a1, a2);
        return;
    case INDEX_op_umin_vec:
        tcg_out_vreg3(s, INSN_VMIN_U, q, vece, a0, a1, a2);
        return;
    case INDEX_op_usadd_vec:
        tcg_out_vreg3(s, INSN_VQADD_U, q, vece, a0, a1, a2);
        return;
    case INDEX_op_ussub_vec:
        tcg_out_vreg3(s, INSN_VQSUB_U, q, vece, a0, a1, a2);
        return;
    case INDEX_op_xor_vec:
        tcg_out_vreg3(s, INSN_VEOR, q, 0, a0, a1, a2);
        return;
    case INDEX_op_arm_sshl_vec:
        tcg_out_vreg3(s, INSN_VSHL_S, q, vece, a0, a2, a1);
        return;
    case INDEX_op_arm_ushl_vec:
        tcg_out_vreg3(s, INSN_VSHL_U, q, vece, a0, a2, a1);
        return;
    case INDEX_op_shli_vec:
        tcg_out_vshifti(s, INSN_VSHLI, q, a0, a1, a2 + (8 << vece));
        return;
    case INDEX_op_shri_vec:
        tcg_out_vshifti(s, INSN_VSHRI, q, a0, a1, (16 << vece) - a2);
        return;
    case INDEX_op_sari_vec:
        tcg_out_vshifti(s, INSN_VSARI, q, a0, a1, (16 << vece) - a2);
        return;
    case INDEX_op_arm_sli_vec:
        tcg_out_vshifti(s, INSN_VSLI, q, a0, a2, args[3] + (8 << vece));
        return;

    case INDEX_op_andc_vec:
        if (!const_args[2]) {
            tcg_out_vreg3(s, INSN_VBIC, q, 0, a0, a1, a2);
            return;
        }
        a2 = ~a2;
        /* fall through */
    case INDEX_op_and_vec:
        if (const_args[2]) {
            is_shimm1632(~a2, &cmode, &imm8);
            if (a0 == a1) {
                tcg_out_vmovi(s, a0, q, 1, cmode | 1, imm8);
                return;
            }
            tcg_out_vmovi(s, a0, q, 1, cmode, imm8);
            a2 = a0;
        }
        tcg_out_vreg3(s, INSN_VAND, q, 0, a0, a1, a2);
        return;

    case INDEX_op_orc_vec:
        if (!const_args[2]) {
            tcg_out_vreg3(s, INSN_VORN, q, 0, a0, a1, a2);
            return;
        }
        a2 = ~a2;
        /* fall through */
    case INDEX_op_or_vec:
        if (const_args[2]) {
            is_shimm1632(a2, &cmode, &imm8);
            if (a0 == a1) {
                tcg_out_vmovi(s, a0, q, 0, cmode | 1, imm8);
                return;
            }
            tcg_out_vmovi(s, a0, q, 0, cmode, imm8);
            a2 = a0;
        }
        tcg_out_vreg3(s, INSN_VORR, q, 0, a0, a1, a2);
        return;

    case INDEX_op_cmp_vec:
        {
            TCGCond cond = args[3];
            ARMInsn insn;

            switch (cond) {
            case TCG_COND_NE:
                if (const_args[2]) {
                    tcg_out_vreg3(s, INSN_VTST, q, vece, a0, a1, a1);
                } else {
                    tcg_out_vreg3(s, INSN_VCEQ, q, vece, a0, a1, a2);
                    tcg_out_vreg2(s, INSN_VMVN, q, 0, a0, a0);
                }
                break;

            case TCG_COND_TSTNE:
            case TCG_COND_TSTEQ:
                if (const_args[2]) {
                    tcg_out_dupi_vec(s, type, MO_8, a0,
                                     -(cond == TCG_COND_TSTEQ));
                    break;
                }
                tcg_out_vreg3(s, INSN_VTST, q, vece, a0, a1, a2);
                if (cond == TCG_COND_TSTEQ) {
                    tcg_out_vreg2(s, INSN_VMVN, q, 0, a0, a0);
                }
                break;

            default:
                if (const_args[2]) {
                    insn = vec_cmp0_insn[cond];
                    if (insn) {
                        tcg_out_vreg2(s, insn, q, vece, a0, a1);
                        return;
                    }
                    tcg_out_dupi_vec(s, type, MO_8, TCG_VEC_TMP, 0);
                    a2 = TCG_VEC_TMP;
                }
                insn = vec_cmp_insn[cond];
                if (insn == 0) {
                    TCGArg t;
                    t = a1, a1 = a2, a2 = t;
                    cond = tcg_swap_cond(cond);
                    insn = vec_cmp_insn[cond];
                    tcg_debug_assert(insn != 0);
                }
                tcg_out_vreg3(s, insn, q, vece, a0, a1, a2);
                break;
            }
        }
        return;

    case INDEX_op_bitsel_vec:
        a3 = args[3];
        if (a0 == a3) {
            tcg_out_vreg3(s, INSN_VBIT, q, 0, a0, a2, a1);
        } else if (a0 == a2) {
            tcg_out_vreg3(s, INSN_VBIF, q, 0, a0, a3, a1);
        } else {
            tcg_out_mov(s, type, a0, a1);
            tcg_out_vreg3(s, INSN_VBSL, q, 0, a0, a2, a3);
        }
        return;

    case INDEX_op_mov_vec:
    case INDEX_op_dup_vec:
    default:
        g_assert_not_reached();
    }
}

int tcg_can_emit_vec_op(TCGOpcode opc, TCGType type, unsigned vece)
{
    switch (opc) {
    case INDEX_op_add_vec:
    case INDEX_op_sub_vec:
    case INDEX_op_and_vec:
    case INDEX_op_andc_vec:
    case INDEX_op_or_vec:
    case INDEX_op_orc_vec:
    case INDEX_op_xor_vec:
    case INDEX_op_not_vec:
    case INDEX_op_shli_vec:
    case INDEX_op_shri_vec:
    case INDEX_op_sari_vec:
    case INDEX_op_ssadd_vec:
    case INDEX_op_sssub_vec:
    case INDEX_op_usadd_vec:
    case INDEX_op_ussub_vec:
    case INDEX_op_bitsel_vec:
        return 1;
    case INDEX_op_abs_vec:
    case INDEX_op_cmp_vec:
    case INDEX_op_mul_vec:
    case INDEX_op_neg_vec:
    case INDEX_op_smax_vec:
    case INDEX_op_smin_vec:
    case INDEX_op_umax_vec:
    case INDEX_op_umin_vec:
        return vece < MO_64;
    case INDEX_op_shlv_vec:
    case INDEX_op_shrv_vec:
    case INDEX_op_sarv_vec:
    case INDEX_op_rotli_vec:
    case INDEX_op_rotlv_vec:
    case INDEX_op_rotrv_vec:
        return -1;
    default:
        return 0;
    }
}

void tcg_expand_vec_op(TCGOpcode opc, TCGType type, unsigned vece,
                       TCGArg a0, ...)
{
    va_list va;
    TCGv_vec v0, v1, v2, t1, t2, c1;
    TCGArg a2;

    va_start(va, a0);
    v0 = temp_tcgv_vec(arg_temp(a0));
    v1 = temp_tcgv_vec(arg_temp(va_arg(va, TCGArg)));
    a2 = va_arg(va, TCGArg);
    va_end(va);

    switch (opc) {
    case INDEX_op_shlv_vec:
        v2 = temp_tcgv_vec(arg_temp(a2));
        vec_gen_3(INDEX_op_arm_ushl_vec, type, vece, tcgv_vec_arg(v0),
                  tcgv_vec_arg(v1), tcgv_vec_arg(v2));
        break;

    case INDEX_op_shrv_vec:
    case INDEX_op_sarv_vec:
        v2 = temp_tcgv_vec(arg_temp(a2));
        t1 = tcg_temp_new_vec(type);
        tcg_gen_neg_vec(vece, t1, v2);
        if (opc == INDEX_op_shrv_vec) {
            opc = INDEX_op_arm_ushl_vec;
        } else {
            opc = INDEX_op_arm_sshl_vec;
        }
        vec_gen_3(opc, type, vece, tcgv_vec_arg(v0),
                  tcgv_vec_arg(v1), tcgv_vec_arg(t1));
        tcg_temp_free_vec(t1);
        break;

    case INDEX_op_rotli_vec:
        t1 = tcg_temp_new_vec(type);
        tcg_gen_shri_vec(vece, t1, v1, -a2 & ((8 << vece) - 1));
        vec_gen_4(INDEX_op_arm_sli_vec, type, vece,
                  tcgv_vec_arg(v0), tcgv_vec_arg(t1), tcgv_vec_arg(v1), a2);
        tcg_temp_free_vec(t1);
        break;

    case INDEX_op_rotlv_vec:
        v2 = temp_tcgv_vec(arg_temp(a2));
        t1 = tcg_temp_new_vec(type);
        c1 = tcg_constant_vec(type, vece, 8 << vece);
        tcg_gen_sub_vec(vece, t1, v2, c1);
        vec_gen_3(INDEX_op_arm_ushl_vec, type, vece, tcgv_vec_arg(t1),
                  tcgv_vec_arg(v1), tcgv_vec_arg(t1));
        vec_gen_3(INDEX_op_arm_ushl_vec, type, vece, tcgv_vec_arg(v0),
                  tcgv_vec_arg(v1), tcgv_vec_arg(v2));
        tcg_gen_or_vec(vece, v0, v0, t1);
        tcg_temp_free_vec(t1);
        break;

    case INDEX_op_rotrv_vec:
        v2 = temp_tcgv_vec(arg_temp(a2));
        t1 = tcg_temp_new_vec(type);
        t2 = tcg_temp_new_vec(type);
        c1 = tcg_constant_vec(type, vece, 8 << vece);
        tcg_gen_neg_vec(vece, t1, v2);
        tcg_gen_sub_vec(vece, t2, c1, v2);
        vec_gen_3(INDEX_op_arm_ushl_vec, type, vece, tcgv_vec_arg(t1),
                  tcgv_vec_arg(v1), tcgv_vec_arg(t1));
        vec_gen_3(INDEX_op_arm_ushl_vec, type, vece, tcgv_vec_arg(t2),
                  tcgv_vec_arg(v1), tcgv_vec_arg(t2));
        tcg_gen_or_vec(vece, v0, t1, t2);
        tcg_temp_free_vec(t1);
        tcg_temp_free_vec(t2);
        break;

    default:
        g_assert_not_reached();
    }
}

static void tcg_out_nop_fill(tcg_insn_unit *p, int count)
{
    int i;
    for (i = 0; i < count; ++i) {
        p[i] = INSN_NOP;
    }
}

#define PUSH_SIZE  ((11 - 4 + 1 + 1) * sizeof(tcg_target_long))

#define FRAME_SIZE \
    ((PUSH_SIZE \
      + TCG_STATIC_CALL_ARGS_SIZE \
      + CPU_TEMP_BUF_NLONGS * sizeof(long) \
      + TCG_TARGET_STACK_ALIGN - 1) \
     & -TCG_TARGET_STACK_ALIGN)

#define STACK_ADDEND  (FRAME_SIZE - PUSH_SIZE)

static void tcg_target_qemu_prologue(TCGContext *s)
{
    tcg_out_ldstm(s, COND_AL, INSN_STMDB, TCG_REG_CALL_STACK,
                  (1 << TCG_REG_R4) | (1 << TCG_REG_R5) | (1 << TCG_REG_R6) |
                  (1 << TCG_REG_R7) | (1 << TCG_REG_R8) | (1 << TCG_REG_R9) |
                  (1 << TCG_REG_R10) | (1 << TCG_REG_R11) | (1 << TCG_REG_R14));

    tcg_out_dat_rI(s, COND_AL, ARITH_SUB, TCG_REG_CALL_STACK,
                   TCG_REG_CALL_STACK, STACK_ADDEND, 1);
    tcg_set_frame(s, TCG_REG_CALL_STACK, TCG_STATIC_CALL_ARGS_SIZE,
                  CPU_TEMP_BUF_NLONGS * sizeof(long));

    tcg_out_mov(s, TCG_TYPE_PTR, TCG_AREG0, tcg_target_call_iarg_regs[0]);

    if (!tcg_use_softmmu && guest_base) {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_GUEST_BASE, guest_base);
        tcg_regset_set_reg(s->reserved_regs, TCG_REG_GUEST_BASE);
    }

    tcg_out_b_reg(s, COND_AL, tcg_target_call_iarg_regs[1]);

    tcg_code_gen_epilogue = tcg_splitwx_to_rx(s->code_ptr);
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R0, 0);
    tcg_out_epilogue(s);
}

static void tcg_out_epilogue(TCGContext *s)
{
    tcg_out_dat_rI(s, COND_AL, ARITH_ADD, TCG_REG_CALL_STACK,
                   TCG_REG_CALL_STACK, STACK_ADDEND, 1);

    tcg_out_ldstm(s, COND_AL, INSN_LDMIA, TCG_REG_CALL_STACK,
                  (1 << TCG_REG_R4) | (1 << TCG_REG_R5) | (1 << TCG_REG_R6) |
                  (1 << TCG_REG_R7) | (1 << TCG_REG_R8) | (1 << TCG_REG_R9) |
                  (1 << TCG_REG_R10) | (1 << TCG_REG_R11) | (1 << TCG_REG_PC));
}

static void tcg_out_tb_start(TCGContext *s)
{
}

typedef struct {
    DebugFrameHeader h;
    uint8_t fde_def_cfa[4];
    uint8_t fde_reg_ofs[18];
} DebugFrame;

#define ELF_HOST_MACHINE EM_ARM

QEMU_BUILD_BUG_ON(FRAME_SIZE >= (1 << 14));

static const DebugFrame debug_frame = {
    .h.cie.len = sizeof(DebugFrameCIE)-4,
    .h.cie.id = -1,
    .h.cie.version = 1,
    .h.cie.code_align = 1,
    .h.cie.data_align = 0x7c,
    .h.cie.return_column = 14,

    .h.fde.len = sizeof(DebugFrame) - offsetof(DebugFrame, h.fde.cie_offset),

    .fde_def_cfa = {
        12, 13,
        (FRAME_SIZE & 0x7f) | 0x80,
        (FRAME_SIZE >> 7)
    },
    .fde_reg_ofs = {
        0x8e, 1,
        0x8b, 2,
        0x8a, 3,
        0x89, 4,
        0x88, 5,
        0x87, 6,
        0x86, 7,
        0x85, 8,
        0x84, 9,
    }
};

void tcg_register_jit(const void *buf, size_t buf_size)
{
    tcg_register_jit_int(buf, buf_size, &debug_frame, sizeof(debug_frame));
}
