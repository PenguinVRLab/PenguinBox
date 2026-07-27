/*
 *  Ingenic XBurst Media eXtension Unit (MXU) translation routines.
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
 *  Copyright (c) 2006 Marius Groeger (FPU operations)
 *  Copyright (c) 2006 Thiemo Seufer (MIPS32R2 support)
 *  Copyright (c) 2009 CodeSourcery (MIPS16 and microMIPS support)
 *  Copyright (c) 2012 Jia Liu & Dongxue Zhang (MIPS ASE DSP support)
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Datasheet:
 *
 *   "XBurst® Instruction Set Architecture MIPS eXtension/enhanced Unit
 *   Programming Manual", Ingenic Semiconductor Co, Ltd., revision June 2, 2017
 */

#include "qemu/osdep.h"
#include "translate.h"

enum {
    OPC_MXU_S32MADD  = 0x00,
    OPC_MXU_S32MADDU = 0x01,
    OPC_MXU__POOL00  = 0x03,
    OPC_MXU_S32MSUB  = 0x04,
    OPC_MXU_S32MSUBU = 0x05,
    OPC_MXU__POOL01  = 0x06,
    OPC_MXU__POOL02  = 0x07,
    OPC_MXU_D16MUL   = 0x08,
    OPC_MXU__POOL03  = 0x09,
    OPC_MXU_D16MAC   = 0x0A,
    OPC_MXU_D16MACF  = 0x0B,
    OPC_MXU_D16MADL  = 0x0C,
    OPC_MXU_S16MAD   = 0x0D,
    OPC_MXU_Q16ADD   = 0x0E,
    OPC_MXU_D16MACE  = 0x0F,
    OPC_MXU__POOL04  = 0x10,
    OPC_MXU__POOL05  = 0x11,
    OPC_MXU__POOL06  = 0x12,
    OPC_MXU__POOL07  = 0x13,
    OPC_MXU__POOL08  = 0x14,
    OPC_MXU__POOL09  = 0x15,
    OPC_MXU__POOL10  = 0x16,
    OPC_MXU__POOL11  = 0x17,
    OPC_MXU_D32ADD   = 0x18,
    OPC_MXU__POOL12  = 0x19,
    OPC_MXU__POOL13  = 0x1B,
    OPC_MXU__POOL14  = 0x1C,
    OPC_MXU_Q8ACCE   = 0x1D,
    OPC_MXU_S8LDD    = 0x22,
    OPC_MXU_S8STD    = 0x23,
    OPC_MXU_S8LDI    = 0x24,
    OPC_MXU_S8SDI    = 0x25,
    OPC_MXU__POOL15  = 0x26,
    OPC_MXU__POOL16  = 0x27,
    OPC_MXU__POOL17  = 0x28,
    OPC_MXU_S16LDD   = 0x2A,
    OPC_MXU_S16STD   = 0x2B,
    OPC_MXU_S16LDI   = 0x2C,
    OPC_MXU_S16SDI   = 0x2D,
    OPC_MXU_S32M2I   = 0x2E,
    OPC_MXU_S32I2M   = 0x2F,
    OPC_MXU_D32SLL   = 0x30,
    OPC_MXU_D32SLR   = 0x31,
    OPC_MXU_D32SARL  = 0x32,
    OPC_MXU_D32SAR   = 0x33,
    OPC_MXU_Q16SLL   = 0x34,
    OPC_MXU_Q16SLR   = 0x35,
    OPC_MXU__POOL18  = 0x36,
    OPC_MXU_Q16SAR   = 0x37,
    OPC_MXU__POOL19  = 0x38,
    OPC_MXU__POOL20  = 0x39,
    OPC_MXU__POOL21  = 0x3A,
    OPC_MXU_Q16SCOP  = 0x3B,
    OPC_MXU_Q8MADL   = 0x3C,
    OPC_MXU_S32SFL   = 0x3D,
    OPC_MXU_Q8SAD    = 0x3E,
};


enum {
    OPC_MXU_S32MAX   = 0x00,
    OPC_MXU_S32MIN   = 0x01,
    OPC_MXU_D16MAX   = 0x02,
    OPC_MXU_D16MIN   = 0x03,
    OPC_MXU_Q8MAX    = 0x04,
    OPC_MXU_Q8MIN    = 0x05,
    OPC_MXU_Q8SLT    = 0x06,
    OPC_MXU_Q8SLTU   = 0x07,
};

enum {
    OPC_MXU_S32SLT   = 0x00,
    OPC_MXU_D16SLT   = 0x01,
    OPC_MXU_D16AVG   = 0x02,
    OPC_MXU_D16AVGR  = 0x03,
    OPC_MXU_Q8AVG    = 0x04,
    OPC_MXU_Q8AVGR   = 0x05,
    OPC_MXU_Q8ADD    = 0x07,
};

enum {
    OPC_MXU_S32CPS   = 0x00,
    OPC_MXU_D16CPS   = 0x02,
    OPC_MXU_Q8ABD    = 0x04,
    OPC_MXU_Q16SAT   = 0x06,
};

enum {
    OPC_MXU_D16MULF  = 0x00,
    OPC_MXU_D16MULE  = 0x01,
};

enum {
    OPC_MXU_S32LDST  = 0x00,
    OPC_MXU_S32LDSTR = 0x01,
};

enum {
    OPC_MXU_D32ACC    = 0x00,
    OPC_MXU_D32ACCM   = 0x01,
    OPC_MXU_D32ASUM   = 0x02,
};

enum {
    OPC_MXU_Q16ACC    = 0x00,
    OPC_MXU_Q16ACCM   = 0x01,
    OPC_MXU_D16ASUM   = 0x02,
};

enum {
    OPC_MXU_Q8ADDE    = 0x00,
    OPC_MXU_D8SUM     = 0x01,
    OPC_MXU_D8SUMC    = 0x02,
};

enum {
    OPC_MXU_S32MUL    = 0x00,
    OPC_MXU_S32MULU   = 0x01,
    OPC_MXU_S32EXTR   = 0x02,
    OPC_MXU_S32EXTRV  = 0x03,
};

enum {
    OPC_MXU_D32SARW  = 0x00,
    OPC_MXU_S32ALN   = 0x01,
    OPC_MXU_S32ALNI  = 0x02,
    OPC_MXU_S32LUI   = 0x03,
    OPC_MXU_S32NOR   = 0x04,
    OPC_MXU_S32AND   = 0x05,
    OPC_MXU_S32OR    = 0x06,
    OPC_MXU_S32XOR   = 0x07,
};

enum {
    OPC_MXU_LXB      = 0x00,
    OPC_MXU_LXH      = 0x01,
    OPC_MXU_LXW      = 0x03,
    OPC_MXU_LXBU     = 0x04,
    OPC_MXU_LXHU     = 0x05,
};

enum {
    OPC_MXU_D32SLLV  = 0x00,
    OPC_MXU_D32SLRV  = 0x01,
    OPC_MXU_D32SARV  = 0x03,
    OPC_MXU_Q16SLLV  = 0x04,
    OPC_MXU_Q16SLRV  = 0x05,
    OPC_MXU_Q16SARV  = 0x07,
};

enum {
    OPC_MXU_Q8MUL    = 0x00,
    OPC_MXU_Q8MULSU  = 0x02,
};

enum {
    OPC_MXU_Q8MOVZ   = 0x00,
    OPC_MXU_Q8MOVN   = 0x01,
    OPC_MXU_D16MOVZ  = 0x02,
    OPC_MXU_D16MOVN  = 0x03,
    OPC_MXU_S32MOVZ  = 0x04,
    OPC_MXU_S32MOVN  = 0x05,
};

enum {
    OPC_MXU_Q8MAC    = 0x00,
    OPC_MXU_Q8MACSU  = 0x02,
};


#define MXU_APTN1_A    0
#define MXU_APTN1_S    1

#define MXU_APTN2_AA    0
#define MXU_APTN2_AS    1
#define MXU_APTN2_SA    2
#define MXU_APTN2_SS    3

#define MXU_EPTN2_AA    0
#define MXU_EPTN2_AS    1
#define MXU_EPTN2_SA    2
#define MXU_EPTN2_SS    3

#define MXU_OPTN2_PTN0  0
#define MXU_OPTN2_PTN1  1
#define MXU_OPTN2_PTN2  2
#define MXU_OPTN2_PTN3  3
#define MXU_OPTN2_WW    0
#define MXU_OPTN2_LW    1
#define MXU_OPTN2_HW    2
#define MXU_OPTN2_XW    3

#define MXU_OPTN3_PTN0  0
#define MXU_OPTN3_PTN1  1
#define MXU_OPTN3_PTN2  2
#define MXU_OPTN3_PTN3  3
#define MXU_OPTN3_PTN4  4
#define MXU_OPTN3_PTN5  5
#define MXU_OPTN3_PTN6  6
#define MXU_OPTN3_PTN7  7

static TCGv mxu_gpr[NUMBER_OF_MXU_REGISTERS - 1];
static TCGv mxu_CR;

static const char mxuregnames[NUMBER_OF_MXU_REGISTERS][4] = {
    "XR1",  "XR2",  "XR3",  "XR4",  "XR5",  "XR6",  "XR7",  "XR8",
    "XR9",  "XR10", "XR11", "XR12", "XR13", "XR14", "XR15", "XCR",
};

void mxu_translate_init(void)
{
    for (unsigned i = 0; i < NUMBER_OF_MXU_REGISTERS - 1; i++) {
        mxu_gpr[i] = tcg_global_mem_new(tcg_env,
                                        offsetof(CPUMIPSState, active_tc.mxu_gpr[i]),
                                        mxuregnames[i]);
    }

    mxu_CR = tcg_global_mem_new(tcg_env,
                                offsetof(CPUMIPSState, active_tc.mxu_cr),
                                mxuregnames[NUMBER_OF_MXU_REGISTERS - 1]);
}

static inline void gen_load_mxu_gpr(TCGv t, unsigned int reg)
{
    if (reg == 0) {
        tcg_gen_movi_tl(t, 0);
    } else if (reg <= 15) {
        tcg_gen_mov_tl(t, mxu_gpr[reg - 1]);
    }
}

static inline void gen_store_mxu_gpr(TCGv t, unsigned int reg)
{
    if (reg > 0 && reg <= 15) {
        tcg_gen_mov_tl(mxu_gpr[reg - 1], t);
    }
}

static inline void gen_extract_mxu_gpr(TCGv t, unsigned int reg,
                                       unsigned int ofs, unsigned int len)
{
    if (reg == 0) {
        tcg_gen_movi_tl(t, 0);
    } else if (reg <= 15) {
        tcg_gen_extract_tl(t, mxu_gpr[reg - 1], ofs, len);
    }
}

static inline void gen_load_mxu_cr(TCGv t)
{
    tcg_gen_mov_tl(t, mxu_CR);
}

static inline void gen_store_mxu_cr(TCGv t)
{
    tcg_gen_mov_tl(mxu_CR, t);
}

static void gen_mxu_s32i2m(DisasContext *ctx)
{
    TCGv t0;
    uint32_t XRa, Rb;

    t0 = tcg_temp_new();

    XRa = extract32(ctx->opcode, 6, 5);
    Rb = extract32(ctx->opcode, 16, 5);

    gen_load_gpr(t0, Rb);
    if (XRa <= 15) {
        gen_store_mxu_gpr(t0, XRa);
    } else if (XRa == 16) {
        gen_store_mxu_cr(t0);
    }
}

static void gen_mxu_s32m2i(DisasContext *ctx)
{
    TCGv t0;
    uint32_t XRa, Rb;

    t0 = tcg_temp_new();

    XRa = extract32(ctx->opcode, 6, 5);
    Rb = extract32(ctx->opcode, 16, 5);

    if (XRa <= 15) {
        gen_load_mxu_gpr(t0, XRa);
    } else if (XRa == 16) {
        gen_load_mxu_cr(t0);
    }

    gen_store_gpr(t0, Rb);
}

static void gen_mxu_s8ldd(DisasContext *ctx, bool postmodify)
{
    TCGv t0, t1;
    uint32_t XRa, Rb, s8, optn3;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    XRa = extract32(ctx->opcode, 6, 4);
    s8 = extract32(ctx->opcode, 10, 8);
    optn3 = extract32(ctx->opcode, 18, 3);
    Rb = extract32(ctx->opcode, 21, 5);

    gen_load_gpr(t0, Rb);
    tcg_gen_addi_tl(t0, t0, (int8_t)s8);
    if (postmodify) {
        gen_store_gpr(t0, Rb);
    }

    switch (optn3) {
    case MXU_OPTN3_PTN0:
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_UB);
        gen_load_mxu_gpr(t0, XRa);
        tcg_gen_deposit_tl(t0, t0, t1, 0, 8);
        break;
    case MXU_OPTN3_PTN1:
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_UB);
        gen_load_mxu_gpr(t0, XRa);
        tcg_gen_deposit_tl(t0, t0, t1, 8, 8);
        break;
    case MXU_OPTN3_PTN2:
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_UB);
        gen_load_mxu_gpr(t0, XRa);
        tcg_gen_deposit_tl(t0, t0, t1, 16, 8);
        break;
    case MXU_OPTN3_PTN3:
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_UB);
        gen_load_mxu_gpr(t0, XRa);
        tcg_gen_deposit_tl(t0, t0, t1, 24, 8);
        break;
    case MXU_OPTN3_PTN4:
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_UB);
        tcg_gen_deposit_tl(t0, t1, t1, 16, 16);
        break;
    case MXU_OPTN3_PTN5:
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_UB);
        tcg_gen_shli_tl(t1, t1, 8);
        tcg_gen_deposit_tl(t0, t1, t1, 16, 16);
        break;
    case MXU_OPTN3_PTN6:
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_SB);
        tcg_gen_mov_tl(t0, t1);
        tcg_gen_andi_tl(t0, t0, 0xFF00FFFF);
        tcg_gen_shli_tl(t1, t1, 16);
        tcg_gen_or_tl(t0, t0, t1);
        break;
    case MXU_OPTN3_PTN7:
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_UB);
        tcg_gen_deposit_tl(t1, t1, t1, 8, 8);
        tcg_gen_deposit_tl(t0, t1, t1, 16, 16);
        break;
    }

    gen_store_mxu_gpr(t0, XRa);
}

static void gen_mxu_s8std(DisasContext *ctx, bool postmodify)
{
    TCGv t0, t1;
    uint32_t XRa, Rb, s8, optn3;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    XRa = extract32(ctx->opcode, 6, 4);
    s8 = extract32(ctx->opcode, 10, 8);
    optn3 = extract32(ctx->opcode, 18, 3);
    Rb = extract32(ctx->opcode, 21, 5);

    if (optn3 > 3) {
        return;
    }

    gen_load_gpr(t0, Rb);
    tcg_gen_addi_tl(t0, t0, (int8_t)s8);
    if (postmodify) {
        gen_store_gpr(t0, Rb);
    }
    gen_load_mxu_gpr(t1, XRa);

    switch (optn3) {
    case MXU_OPTN3_PTN0:
        tcg_gen_extract_tl(t1, t1, 0, 8);
        break;
    case MXU_OPTN3_PTN1:
        tcg_gen_extract_tl(t1, t1, 8, 8);
        break;
    case MXU_OPTN3_PTN2:
        tcg_gen_extract_tl(t1, t1, 16, 8);
        break;
    case MXU_OPTN3_PTN3:
        tcg_gen_extract_tl(t1, t1, 24, 8);
        break;
    }

    tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, MO_UB);
}

static void gen_mxu_s16ldd(DisasContext *ctx, bool postmodify)
{
    TCGv t0, t1;
    uint32_t XRa, Rb, optn2;
    int32_t s10;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    XRa   = extract32(ctx->opcode,   6, 4);
    s10   = sextract32(ctx->opcode, 10, 9) * 2;
    optn2 = extract32(ctx->opcode,  19, 2);
    Rb    = extract32(ctx->opcode,  21, 5);

    gen_load_gpr(t0, Rb);
    tcg_gen_addi_tl(t0, t0, s10);
    if (postmodify) {
        gen_store_gpr(t0, Rb);
    }

    switch (optn2) {
    case MXU_OPTN2_PTN0:
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_UW);
        gen_load_mxu_gpr(t0, XRa);
        tcg_gen_deposit_tl(t0, t0, t1, 0, 16);
        break;
    case MXU_OPTN2_PTN1:
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_UW);
        gen_load_mxu_gpr(t0, XRa);
        tcg_gen_deposit_tl(t0, t0, t1, 16, 16);
        break;
    case MXU_OPTN2_PTN2:
        tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx, MO_SW);
        break;
    case MXU_OPTN2_PTN3:
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_UW);
        tcg_gen_deposit_tl(t0, t1, t1,  0, 16);
        tcg_gen_deposit_tl(t0, t1, t1, 16, 16);
        break;
    }

    gen_store_mxu_gpr(t0, XRa);
}

static void gen_mxu_s16std(DisasContext *ctx, bool postmodify)
{
    TCGv t0, t1;
    uint32_t XRa, Rb, optn2;
    int32_t s10;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    XRa = extract32(ctx->opcode, 6, 4);
    s10 = sextract32(ctx->opcode, 10, 9) * 2;
    optn2 = extract32(ctx->opcode, 19, 2);
    Rb = extract32(ctx->opcode, 21, 5);

    if (optn2 > 1) {
        return;
    }

    gen_load_gpr(t0, Rb);
    tcg_gen_addi_tl(t0, t0, s10);
    if (postmodify) {
        gen_store_gpr(t0, Rb);
    }
    gen_load_mxu_gpr(t1, XRa);

    switch (optn2) {
    case MXU_OPTN2_PTN0:
        tcg_gen_extract_tl(t1, t1, 0, 16);
        break;
    case MXU_OPTN2_PTN1:
        tcg_gen_extract_tl(t1, t1, 16, 16);
        break;
    }

    tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, MO_UW);
}

static void gen_mxu_s32mul(DisasContext *ctx, bool mulu)
{
    TCGv t0, t1;
    uint32_t XRa, XRd, rs, rt;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    XRa = extract32(ctx->opcode,  6, 4);
    XRd = extract32(ctx->opcode, 10, 4);
    rs  = extract32(ctx->opcode, 16, 5);
    rt  = extract32(ctx->opcode, 21, 5);

    if (unlikely(rs == 0 || rt == 0)) {
        tcg_gen_movi_tl(t0, 0);
        tcg_gen_movi_tl(t1, 0);
    } else {
        gen_load_gpr(t0, rs);
        gen_load_gpr(t1, rt);

        if (mulu) {
            tcg_gen_mulu2_tl(t0, t1, t0, t1);
        } else {
            tcg_gen_muls2_tl(t0, t1, t0, t1);
        }
    }
    tcg_gen_mov_tl(cpu_HI[0], t1);
    tcg_gen_mov_tl(cpu_LO[0], t0);
    gen_store_mxu_gpr(t1, XRa);
    gen_store_mxu_gpr(t0, XRd);
}

static void gen_mxu_d16mul(DisasContext *ctx, bool fractional,
                           bool packed_result)
{
    TCGv t0, t1, t2, t3;
    uint32_t XRa, XRb, XRc, XRd, optn2;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    t2 = tcg_temp_new();
    t3 = tcg_temp_new();

    XRa = extract32(ctx->opcode, 6, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRc = extract32(ctx->opcode, 14, 4);
    XRd = extract32(ctx->opcode, 18, 4);
    optn2 = extract32(ctx->opcode, 22, 2);

    gen_load_mxu_gpr(t1, XRb);
    tcg_gen_sextract_tl(t0, t1, 0, 16);
    tcg_gen_sextract_tl(t1, t1, 16, 16);
    gen_load_mxu_gpr(t3, XRc);
    tcg_gen_sextract_tl(t2, t3, 0, 16);
    tcg_gen_sextract_tl(t3, t3, 16, 16);

    switch (optn2) {
    case MXU_OPTN2_WW:
        tcg_gen_mul_tl(t3, t1, t3);
        tcg_gen_mul_tl(t2, t0, t2);
        break;
    case MXU_OPTN2_LW:
        tcg_gen_mul_tl(t3, t0, t3);
        tcg_gen_mul_tl(t2, t0, t2);
        break;
    case MXU_OPTN2_HW:
        tcg_gen_mul_tl(t3, t1, t3);
        tcg_gen_mul_tl(t2, t1, t2);
        break;
    case MXU_OPTN2_XW:
        tcg_gen_mul_tl(t3, t0, t3);
        tcg_gen_mul_tl(t2, t1, t2);
        break;
    }
    if (fractional) {
        TCGLabel *l_done = gen_new_label();
        TCGv rounding = tcg_temp_new();

        tcg_gen_shli_tl(t3, t3, 1);
        tcg_gen_shli_tl(t2, t2, 1);
        tcg_gen_andi_tl(rounding, mxu_CR, 0x2);
        tcg_gen_brcondi_tl(TCG_COND_EQ, rounding, 0, l_done);
        if (packed_result) {
            TCGLabel *l_apply_bias_l = gen_new_label();
            TCGLabel *l_apply_bias_r = gen_new_label();
            TCGLabel *l_half_done = gen_new_label();
            TCGv bias = tcg_temp_new();

            tcg_gen_andi_tl(bias, mxu_CR, 0x4);
            tcg_gen_brcondi_tl(TCG_COND_NE, bias, 0, l_apply_bias_l);
            tcg_gen_andi_tl(t0, t3, 0x1ffff);
            tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0x8000, l_half_done);
            gen_set_label(l_apply_bias_l);
            tcg_gen_addi_tl(t3, t3, 0x8000);
            gen_set_label(l_half_done);
            tcg_gen_brcondi_tl(TCG_COND_NE, bias, 0, l_apply_bias_r);
            tcg_gen_andi_tl(t0, t2, 0x1ffff);
            tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0x8000, l_done);
            gen_set_label(l_apply_bias_r);
            tcg_gen_addi_tl(t2, t2, 0x8000);
        } else {
            tcg_gen_addi_tl(t3, t3, 0x8000);
            tcg_gen_addi_tl(t2, t2, 0x8000);
        }
        gen_set_label(l_done);
    }
    if (!packed_result) {
        gen_store_mxu_gpr(t3, XRa);
        gen_store_mxu_gpr(t2, XRd);
    } else {
        tcg_gen_andi_tl(t3, t3, 0xffff0000);
        tcg_gen_shri_tl(t2, t2, 16);
        tcg_gen_or_tl(t3, t3, t2);
        gen_store_mxu_gpr(t3, XRa);
    }
}

static void gen_mxu_d16mac(DisasContext *ctx, bool fractional,
                           bool packed_result)
{
    TCGv t0, t1, t2, t3;
    uint32_t XRa, XRb, XRc, XRd, optn2, aptn2;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    t2 = tcg_temp_new();
    t3 = tcg_temp_new();

    XRa = extract32(ctx->opcode, 6, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRc = extract32(ctx->opcode, 14, 4);
    XRd = extract32(ctx->opcode, 18, 4);
    optn2 = extract32(ctx->opcode, 22, 2);
    aptn2 = extract32(ctx->opcode, 24, 2);

    gen_load_mxu_gpr(t1, XRb);
    tcg_gen_sextract_tl(t0, t1, 0, 16);
    tcg_gen_sextract_tl(t1, t1, 16, 16);

    gen_load_mxu_gpr(t3, XRc);
    tcg_gen_sextract_tl(t2, t3, 0, 16);
    tcg_gen_sextract_tl(t3, t3, 16, 16);

    switch (optn2) {
    case MXU_OPTN2_WW:
        tcg_gen_mul_tl(t3, t1, t3);
        tcg_gen_mul_tl(t2, t0, t2);
        break;
    case MXU_OPTN2_LW:
        tcg_gen_mul_tl(t3, t0, t3);
        tcg_gen_mul_tl(t2, t0, t2);
        break;
    case MXU_OPTN2_HW:
        tcg_gen_mul_tl(t3, t1, t3);
        tcg_gen_mul_tl(t2, t1, t2);
        break;
    case MXU_OPTN2_XW:
        tcg_gen_mul_tl(t3, t0, t3);
        tcg_gen_mul_tl(t2, t1, t2);
        break;
    }

    if (fractional) {
        tcg_gen_shli_tl(t3, t3, 1);
        tcg_gen_shli_tl(t2, t2, 1);
    }
    gen_load_mxu_gpr(t0, XRa);
    gen_load_mxu_gpr(t1, XRd);

    switch (aptn2) {
    case MXU_APTN2_AA:
        tcg_gen_add_tl(t3, t0, t3);
        tcg_gen_add_tl(t2, t1, t2);
        break;
    case MXU_APTN2_AS:
        tcg_gen_add_tl(t3, t0, t3);
        tcg_gen_sub_tl(t2, t1, t2);
        break;
    case MXU_APTN2_SA:
        tcg_gen_sub_tl(t3, t0, t3);
        tcg_gen_add_tl(t2, t1, t2);
        break;
    case MXU_APTN2_SS:
        tcg_gen_sub_tl(t3, t0, t3);
        tcg_gen_sub_tl(t2, t1, t2);
        break;
    }

    if (fractional) {
        TCGLabel *l_done = gen_new_label();
        TCGv rounding = tcg_temp_new();

        tcg_gen_andi_tl(rounding, mxu_CR, 0x2);
        tcg_gen_brcondi_tl(TCG_COND_EQ, rounding, 0, l_done);
        if (packed_result) {
            TCGLabel *l_apply_bias_l = gen_new_label();
            TCGLabel *l_apply_bias_r = gen_new_label();
            TCGLabel *l_half_done = gen_new_label();
            TCGv bias = tcg_temp_new();

            tcg_gen_andi_tl(bias, mxu_CR, 0x4);
            tcg_gen_brcondi_tl(TCG_COND_NE, bias, 0, l_apply_bias_l);
            tcg_gen_andi_tl(t0, t3, 0x1ffff);
            tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0x8000, l_half_done);
            gen_set_label(l_apply_bias_l);
            tcg_gen_addi_tl(t3, t3, 0x8000);
            gen_set_label(l_half_done);
            tcg_gen_brcondi_tl(TCG_COND_NE, bias, 0, l_apply_bias_r);
            tcg_gen_andi_tl(t0, t2, 0x1ffff);
            tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0x8000, l_done);
            gen_set_label(l_apply_bias_r);
            tcg_gen_addi_tl(t2, t2, 0x8000);
        } else {
            tcg_gen_addi_tl(t3, t3, 0x8000);
            tcg_gen_addi_tl(t2, t2, 0x8000);
        }
        gen_set_label(l_done);
    }

    if (!packed_result) {
        gen_store_mxu_gpr(t3, XRa);
        gen_store_mxu_gpr(t2, XRd);
    } else {
        tcg_gen_andi_tl(t3, t3, 0xffff0000);
        tcg_gen_shri_tl(t2, t2, 16);
        tcg_gen_or_tl(t3, t3, t2);
        gen_store_mxu_gpr(t3, XRa);
    }
}

static void gen_mxu_d16madl(DisasContext *ctx)
{
    TCGv t0, t1, t2, t3;
    uint32_t XRa, XRb, XRc, XRd, optn2, aptn2;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    t2 = tcg_temp_new();
    t3 = tcg_temp_new();

    XRa = extract32(ctx->opcode, 6, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRc = extract32(ctx->opcode, 14, 4);
    XRd = extract32(ctx->opcode, 18, 4);
    optn2 = extract32(ctx->opcode, 22, 2);
    aptn2 = extract32(ctx->opcode, 24, 2);

    gen_load_mxu_gpr(t1, XRb);
    tcg_gen_sextract_tl(t0, t1,  0, 16);
    tcg_gen_sextract_tl(t1, t1, 16, 16);

    gen_load_mxu_gpr(t3, XRc);
    tcg_gen_sextract_tl(t2, t3,  0, 16);
    tcg_gen_sextract_tl(t3, t3, 16, 16);

    switch (optn2) {
    case MXU_OPTN2_WW:
        tcg_gen_mul_tl(t3, t1, t3);
        tcg_gen_mul_tl(t2, t0, t2);
        break;
    case MXU_OPTN2_LW:
        tcg_gen_mul_tl(t3, t0, t3);
        tcg_gen_mul_tl(t2, t0, t2);
        break;
    case MXU_OPTN2_HW:
        tcg_gen_mul_tl(t3, t1, t3);
        tcg_gen_mul_tl(t2, t1, t2);
        break;
    case MXU_OPTN2_XW:
        tcg_gen_mul_tl(t3, t0, t3);
        tcg_gen_mul_tl(t2, t1, t2);
        break;
    }
    tcg_gen_extract_tl(t2, t2, 0, 16);
    tcg_gen_extract_tl(t3, t3, 0, 16);

    gen_load_mxu_gpr(t1, XRa);
    tcg_gen_extract_tl(t0, t1,  0, 16);
    tcg_gen_extract_tl(t1, t1, 16, 16);

    switch (aptn2) {
    case MXU_APTN2_AA:
        tcg_gen_add_tl(t3, t1, t3);
        tcg_gen_add_tl(t2, t0, t2);
        break;
    case MXU_APTN2_AS:
        tcg_gen_add_tl(t3, t1, t3);
        tcg_gen_sub_tl(t2, t0, t2);
        break;
    case MXU_APTN2_SA:
        tcg_gen_sub_tl(t3, t1, t3);
        tcg_gen_add_tl(t2, t0, t2);
        break;
    case MXU_APTN2_SS:
        tcg_gen_sub_tl(t3, t1, t3);
        tcg_gen_sub_tl(t2, t0, t2);
        break;
    }

    tcg_gen_andi_tl(t2, t2, 0xffff);
    tcg_gen_shli_tl(t3, t3, 16);
    tcg_gen_or_tl(mxu_gpr[XRd - 1], t3, t2);
}

static void gen_mxu_s16mad(DisasContext *ctx)
{
    TCGv t0, t1;
    uint32_t XRa, XRb, XRc, XRd, optn2, aptn1, pad;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    XRa = extract32(ctx->opcode, 6, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRc = extract32(ctx->opcode, 14, 4);
    XRd = extract32(ctx->opcode, 18, 4);
    optn2 = extract32(ctx->opcode, 22, 2);
    aptn1 = extract32(ctx->opcode, 24, 1);
    pad = extract32(ctx->opcode, 25, 1);

    if (pad) {
    }

    gen_load_mxu_gpr(t0, XRb);
    gen_load_mxu_gpr(t1, XRc);

    switch (optn2) {
    case MXU_OPTN2_WW:
        tcg_gen_sextract_tl(t0, t0, 16, 16);
        tcg_gen_sextract_tl(t1, t1, 16, 16);
        break;
    case MXU_OPTN2_LW:
        tcg_gen_sextract_tl(t0, t0,  0, 16);
        tcg_gen_sextract_tl(t1, t1,  0, 16);
        break;
    case MXU_OPTN2_HW:
        tcg_gen_sextract_tl(t0, t0, 16, 16);
        tcg_gen_sextract_tl(t1, t1,  0, 16);
        break;
    case MXU_OPTN2_XW:
        tcg_gen_sextract_tl(t0, t0,  0, 16);
        tcg_gen_sextract_tl(t1, t1, 16, 16);
        break;
    }
    tcg_gen_mul_tl(t0, t0, t1);

    gen_load_mxu_gpr(t1, XRa);

    switch (aptn1) {
    case MXU_APTN1_A:
        tcg_gen_add_tl(t1, t1, t0);
        break;
    case MXU_APTN1_S:
        tcg_gen_sub_tl(t1, t1, t0);
        break;
    }

    gen_store_mxu_gpr(t1, XRd);
}

static void gen_mxu_q8mul_mac(DisasContext *ctx, bool su, bool mac)
{
    TCGv t0, t1, t2, t3, t4, t5, t6, t7;
    uint32_t XRa, XRb, XRc, XRd, aptn2;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    t2 = tcg_temp_new();
    t3 = tcg_temp_new();
    t4 = tcg_temp_new();
    t5 = tcg_temp_new();
    t6 = tcg_temp_new();
    t7 = tcg_temp_new();

    XRa = extract32(ctx->opcode, 6, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRc = extract32(ctx->opcode, 14, 4);
    XRd = extract32(ctx->opcode, 18, 4);
    aptn2 = extract32(ctx->opcode, 24, 2);

    gen_load_mxu_gpr(t3, XRb);
    gen_load_mxu_gpr(t7, XRc);

    if (su) {
        tcg_gen_sextract_tl(t0, t3,  0, 8);
        tcg_gen_sextract_tl(t1, t3,  8, 8);
        tcg_gen_sextract_tl(t2, t3, 16, 8);
        tcg_gen_sextract_tl(t3, t3, 24, 8);
    } else {
        tcg_gen_extract_tl(t0, t3,  0, 8);
        tcg_gen_extract_tl(t1, t3,  8, 8);
        tcg_gen_extract_tl(t2, t3, 16, 8);
        tcg_gen_extract_tl(t3, t3, 24, 8);
    }

    tcg_gen_extract_tl(t4, t7,  0, 8);
    tcg_gen_extract_tl(t5, t7,  8, 8);
    tcg_gen_extract_tl(t6, t7, 16, 8);
    tcg_gen_extract_tl(t7, t7, 24, 8);

    tcg_gen_mul_tl(t0, t0, t4);
    tcg_gen_mul_tl(t1, t1, t5);
    tcg_gen_mul_tl(t2, t2, t6);
    tcg_gen_mul_tl(t3, t3, t7);

    if (mac) {
        gen_load_mxu_gpr(t4, XRd);
        gen_load_mxu_gpr(t5, XRa);
        tcg_gen_extract_tl(t6, t4,  0, 16);
        tcg_gen_extract_tl(t7, t4, 16, 16);
        if (aptn2 & 1) {
            tcg_gen_sub_tl(t0, t6, t0);
            tcg_gen_sub_tl(t1, t7, t1);
        } else {
            tcg_gen_add_tl(t0, t6, t0);
            tcg_gen_add_tl(t1, t7, t1);
        }
        tcg_gen_extract_tl(t6, t5,  0, 16);
        tcg_gen_extract_tl(t7, t5, 16, 16);
        if (aptn2 & 2) {
            tcg_gen_sub_tl(t2, t6, t2);
            tcg_gen_sub_tl(t3, t7, t3);
        } else {
            tcg_gen_add_tl(t2, t6, t2);
            tcg_gen_add_tl(t3, t7, t3);
        }
    }

    tcg_gen_deposit_tl(t0, t0, t1, 16, 16);
    tcg_gen_deposit_tl(t1, t2, t3, 16, 16);

    gen_store_mxu_gpr(t0, XRd);
    gen_store_mxu_gpr(t1, XRa);
}

static void gen_mxu_q8madl(DisasContext *ctx)
{
    TCGv t0, t1, t2, t3, t4, t5, t6, t7;
    uint32_t XRa, XRb, XRc, XRd, aptn2;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    t2 = tcg_temp_new();
    t3 = tcg_temp_new();
    t4 = tcg_temp_new();
    t5 = tcg_temp_new();
    t6 = tcg_temp_new();
    t7 = tcg_temp_new();

    XRa = extract32(ctx->opcode, 6, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRc = extract32(ctx->opcode, 14, 4);
    XRd = extract32(ctx->opcode, 18, 4);
    aptn2 = extract32(ctx->opcode, 24, 2);

    gen_load_mxu_gpr(t3, XRb);
    gen_load_mxu_gpr(t7, XRc);

    tcg_gen_extract_tl(t0, t3,  0, 8);
    tcg_gen_extract_tl(t1, t3,  8, 8);
    tcg_gen_extract_tl(t2, t3, 16, 8);
    tcg_gen_extract_tl(t3, t3, 24, 8);

    tcg_gen_extract_tl(t4, t7,  0, 8);
    tcg_gen_extract_tl(t5, t7,  8, 8);
    tcg_gen_extract_tl(t6, t7, 16, 8);
    tcg_gen_extract_tl(t7, t7, 24, 8);

    tcg_gen_mul_tl(t0, t0, t4);
    tcg_gen_mul_tl(t1, t1, t5);
    tcg_gen_mul_tl(t2, t2, t6);
    tcg_gen_mul_tl(t3, t3, t7);

    gen_load_mxu_gpr(t4, XRa);
    tcg_gen_extract_tl(t6, t4, 0, 8);
    tcg_gen_extract_tl(t7, t4, 8, 8);
    if (aptn2 & 1) {
        tcg_gen_sub_tl(t0, t6, t0);
        tcg_gen_sub_tl(t1, t7, t1);
    } else {
        tcg_gen_add_tl(t0, t6, t0);
        tcg_gen_add_tl(t1, t7, t1);
    }
    tcg_gen_extract_tl(t6, t4, 16, 8);
    tcg_gen_extract_tl(t7, t4, 24, 8);
    if (aptn2 & 2) {
        tcg_gen_sub_tl(t2, t6, t2);
        tcg_gen_sub_tl(t3, t7, t3);
    } else {
        tcg_gen_add_tl(t2, t6, t2);
        tcg_gen_add_tl(t3, t7, t3);
    }

    tcg_gen_andi_tl(t5, t0, 0xff);
    tcg_gen_deposit_tl(t5, t5, t1,  8, 8);
    tcg_gen_deposit_tl(t5, t5, t2, 16, 8);
    tcg_gen_deposit_tl(t5, t5, t3, 24, 8);

    gen_store_mxu_gpr(t5, XRd);
}

static void gen_mxu_s32ldxx(DisasContext *ctx, bool reversed, bool postinc)
{
    TCGv t0, t1;
    uint32_t XRa, Rb, s12;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    XRa = extract32(ctx->opcode, 6, 4);
    s12 = sextract32(ctx->opcode, 10, 10);
    Rb = extract32(ctx->opcode, 21, 5);

    gen_load_gpr(t0, Rb);
    tcg_gen_movi_tl(t1, s12 * 4);
    tcg_gen_add_tl(t0, t0, t1);

    tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx,
                       MO_SL | mo_endian_rev(ctx, reversed) |
                        ctx->default_tcg_memop_mask);
    gen_store_mxu_gpr(t1, XRa);

    if (postinc) {
        gen_store_gpr(t0, Rb);
    }
}

static void gen_mxu_s32stxx(DisasContext *ctx, bool reversed, bool postinc)
{
    TCGv t0, t1;
    uint32_t XRa, Rb, s12;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    XRa = extract32(ctx->opcode, 6, 4);
    s12 = sextract32(ctx->opcode, 10, 10);
    Rb = extract32(ctx->opcode, 21, 5);

    gen_load_gpr(t0, Rb);
    tcg_gen_movi_tl(t1, s12 * 4);
    tcg_gen_add_tl(t0, t0, t1);

    gen_load_mxu_gpr(t1, XRa);
    tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx,
                       MO_SL | mo_endian_rev(ctx, reversed) |
                        ctx->default_tcg_memop_mask);

    if (postinc) {
        gen_store_gpr(t0, Rb);
    }
}

static void gen_mxu_s32ldxvx(DisasContext *ctx, bool reversed,
                             bool postinc, uint32_t strd2)
{
    TCGv t0, t1;
    uint32_t XRa, Rb, Rc;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    XRa = extract32(ctx->opcode, 6, 4);
    Rc = extract32(ctx->opcode, 16, 5);
    Rb = extract32(ctx->opcode, 21, 5);

    gen_load_gpr(t0, Rb);
    gen_load_gpr(t1, Rc);
    tcg_gen_shli_tl(t1, t1, strd2);
    tcg_gen_add_tl(t0, t0, t1);

    tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx,
                       MO_SL | mo_endian_rev(ctx, reversed) |
                        ctx->default_tcg_memop_mask);
    gen_store_mxu_gpr(t1, XRa);

    if (postinc) {
        gen_store_gpr(t0, Rb);
    }
}

static void gen_mxu_lxx(DisasContext *ctx, uint32_t strd2, MemOp mop)
{
    TCGv t0, t1;
    uint32_t Ra, Rb, Rc;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    Ra = extract32(ctx->opcode, 11, 5);
    Rc = extract32(ctx->opcode, 16, 5);
    Rb = extract32(ctx->opcode, 21, 5);

    gen_load_gpr(t0, Rb);
    gen_load_gpr(t1, Rc);
    tcg_gen_shli_tl(t1, t1, strd2);
    tcg_gen_add_tl(t0, t0, t1);

    tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, mop | ctx->default_tcg_memop_mask);
    gen_store_gpr(t1, Ra);
}

static void gen_mxu_s32stxvx(DisasContext *ctx, bool reversed,
                             bool postinc, uint32_t strd2)
{
    TCGv t0, t1;
    uint32_t XRa, Rb, Rc;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    XRa = extract32(ctx->opcode, 6, 4);
    Rc = extract32(ctx->opcode, 16, 5);
    Rb = extract32(ctx->opcode, 21, 5);

    gen_load_gpr(t0, Rb);
    gen_load_gpr(t1, Rc);
    tcg_gen_shli_tl(t1, t1, strd2);
    tcg_gen_add_tl(t0, t0, t1);

    gen_load_mxu_gpr(t1, XRa);
    tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx,
                       MO_SL | mo_endian_rev(ctx, reversed) |
                        ctx->default_tcg_memop_mask);

    if (postinc) {
        gen_store_gpr(t0, Rb);
    }
}

static void gen_mxu_S32NOR(DisasContext *ctx)
{
    uint32_t pad, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
    } else if (unlikely(XRa == 0)) {
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        tcg_gen_movi_i32(mxu_gpr[XRa - 1], 0xFFFFFFFF);
    } else if (unlikely(XRb == 0)) {
        tcg_gen_not_i32(mxu_gpr[XRa - 1], mxu_gpr[XRc - 1]);
    } else if (unlikely(XRc == 0)) {
        tcg_gen_not_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
    } else if (unlikely(XRb == XRc)) {
        tcg_gen_not_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
    } else {
        tcg_gen_nor_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1], mxu_gpr[XRc - 1]);
    }
}

static void gen_mxu_S32AND(DisasContext *ctx)
{
    uint32_t pad, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
    } else if (unlikely(XRa == 0)) {
    } else if (unlikely((XRb == 0) || (XRc == 0))) {
        tcg_gen_movi_i32(mxu_gpr[XRa - 1], 0);
    } else if (unlikely(XRb == XRc)) {
        tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
    } else {
        tcg_gen_and_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1], mxu_gpr[XRc - 1]);
    }
}

static void gen_mxu_S32OR(DisasContext *ctx)
{
    uint32_t pad, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
    } else if (unlikely(XRa == 0)) {
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        tcg_gen_movi_i32(mxu_gpr[XRa - 1], 0);
    } else if (unlikely(XRb == 0)) {
        tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRc - 1]);
    } else if (unlikely(XRc == 0)) {
        tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
    } else if (unlikely(XRb == XRc)) {
        tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
    } else {
        tcg_gen_or_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1], mxu_gpr[XRc - 1]);
    }
}

static void gen_mxu_S32XOR(DisasContext *ctx)
{
    uint32_t pad, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
    } else if (unlikely(XRa == 0)) {
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        tcg_gen_movi_i32(mxu_gpr[XRa - 1], 0);
    } else if (unlikely(XRb == 0)) {
        tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRc - 1]);
    } else if (unlikely(XRc == 0)) {
        tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
    } else if (unlikely(XRb == XRc)) {
        tcg_gen_movi_i32(mxu_gpr[XRa - 1], 0);
    } else {
        tcg_gen_xor_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1], mxu_gpr[XRc - 1]);
    }
}

static void gen_mxu_d32sxx(DisasContext *ctx, bool right, bool arithmetic)
{
    uint32_t XRa, XRb, XRc, XRd, sft4;

    XRa  = extract32(ctx->opcode,  6, 4);
    XRb  = extract32(ctx->opcode, 10, 4);
    XRc  = extract32(ctx->opcode, 14, 4);
    XRd  = extract32(ctx->opcode, 18, 4);
    sft4 = extract32(ctx->opcode, 22, 4);

    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();

    gen_load_mxu_gpr(t0, XRb);
    gen_load_mxu_gpr(t1, XRc);

    if (right) {
        if (arithmetic) {
            tcg_gen_sari_tl(t0, t0, sft4);
            tcg_gen_sari_tl(t1, t1, sft4);
        } else {
            tcg_gen_shri_tl(t0, t0, sft4);
            tcg_gen_shri_tl(t1, t1, sft4);
        }
    } else {
        tcg_gen_shli_tl(t0, t0, sft4);
        tcg_gen_shli_tl(t1, t1, sft4);
    }
    gen_store_mxu_gpr(t0, XRa);
    gen_store_mxu_gpr(t1, XRd);
}

static void gen_mxu_d32sxxv(DisasContext *ctx, bool right, bool arithmetic)
{
    uint32_t XRa, XRd, rs;

    XRa = extract32(ctx->opcode, 10, 4);
    XRd = extract32(ctx->opcode, 14, 4);
    rs  = extract32(ctx->opcode, 21, 5);

    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();

    gen_load_mxu_gpr(t0, XRa);
    gen_load_mxu_gpr(t1, XRd);
    gen_load_gpr(t2, rs);
    tcg_gen_andi_tl(t2, t2, 0x0f);

    if (right) {
        if (arithmetic) {
            tcg_gen_sar_tl(t0, t0, t2);
            tcg_gen_sar_tl(t1, t1, t2);
        } else {
            tcg_gen_shr_tl(t0, t0, t2);
            tcg_gen_shr_tl(t1, t1, t2);
        }
    } else {
        tcg_gen_shl_tl(t0, t0, t2);
        tcg_gen_shl_tl(t1, t1, t2);
    }
    gen_store_mxu_gpr(t0, XRa);
    gen_store_mxu_gpr(t1, XRd);
}

static void gen_mxu_d32sarl(DisasContext *ctx, bool sarw)
{
    uint32_t XRa, XRb, XRc, rb;

    XRa = extract32(ctx->opcode,  6, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRc = extract32(ctx->opcode, 14, 4);
    rb  = extract32(ctx->opcode, 21, 5);

    if (unlikely(XRa == 0)) {
    } else {
        TCGv t0 = tcg_temp_new();
        TCGv t1 = tcg_temp_new();
        TCGv t2 = tcg_temp_new();

        if (!sarw) {
            tcg_gen_movi_tl(t2, rb >> 1);
        } else {
            gen_load_gpr(t2, rb);
            tcg_gen_andi_tl(t2, t2, 0x0f);
        }
        gen_load_mxu_gpr(t0, XRb);
        gen_load_mxu_gpr(t1, XRc);
        tcg_gen_sar_tl(t0, t0, t2);
        tcg_gen_sar_tl(t1, t1, t2);
        tcg_gen_extract_tl(t2, t1, 0, 16);
        tcg_gen_deposit_tl(t2, t2, t0, 16, 16);
        gen_store_mxu_gpr(t2, XRa);
    }
}

static void gen_mxu_q16sxx(DisasContext *ctx, bool right, bool arithmetic)
{
    uint32_t XRa, XRb, XRc, XRd, sft4;

    XRa  = extract32(ctx->opcode,  6, 4);
    XRb  = extract32(ctx->opcode, 10, 4);
    XRc  = extract32(ctx->opcode, 14, 4);
    XRd  = extract32(ctx->opcode, 18, 4);
    sft4 = extract32(ctx->opcode, 22, 4);

    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    TCGv t3 = tcg_temp_new();

    gen_load_mxu_gpr(t0, XRb);
    gen_load_mxu_gpr(t2, XRc);

    if (arithmetic) {
        tcg_gen_sextract_tl(t1, t0, 16, 16);
        tcg_gen_sextract_tl(t0, t0,  0, 16);
        tcg_gen_sextract_tl(t3, t2, 16, 16);
        tcg_gen_sextract_tl(t2, t2,  0, 16);
    } else {
        tcg_gen_extract_tl(t1, t0, 16, 16);
        tcg_gen_extract_tl(t0, t0,  0, 16);
        tcg_gen_extract_tl(t3, t2, 16, 16);
        tcg_gen_extract_tl(t2, t2,  0, 16);
    }

    if (right) {
        if (arithmetic) {
            tcg_gen_sari_tl(t0, t0, sft4);
            tcg_gen_sari_tl(t1, t1, sft4);
            tcg_gen_sari_tl(t2, t2, sft4);
            tcg_gen_sari_tl(t3, t3, sft4);
        } else {
            tcg_gen_shri_tl(t0, t0, sft4);
            tcg_gen_shri_tl(t1, t1, sft4);
            tcg_gen_shri_tl(t2, t2, sft4);
            tcg_gen_shri_tl(t3, t3, sft4);
        }
    } else {
        tcg_gen_shli_tl(t0, t0, sft4);
        tcg_gen_shli_tl(t1, t1, sft4);
        tcg_gen_shli_tl(t2, t2, sft4);
        tcg_gen_shli_tl(t3, t3, sft4);
    }
    tcg_gen_deposit_tl(t0, t0, t1, 16, 16);
    tcg_gen_deposit_tl(t2, t2, t3, 16, 16);

    gen_store_mxu_gpr(t0, XRa);
    gen_store_mxu_gpr(t2, XRd);
}

static void gen_mxu_q16sxxv(DisasContext *ctx, bool right, bool arithmetic)
{
    uint32_t XRa, XRd, rs;

    XRa = extract32(ctx->opcode, 10, 4);
    XRd = extract32(ctx->opcode, 14, 4);
    rs  = extract32(ctx->opcode, 21, 5);

    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    TCGv t3 = tcg_temp_new();
    TCGv t5 = tcg_temp_new();

    gen_load_mxu_gpr(t0, XRa);
    gen_load_mxu_gpr(t2, XRd);
    gen_load_gpr(t5, rs);
    tcg_gen_andi_tl(t5, t5, 0x0f);


    if (arithmetic) {
        tcg_gen_sextract_tl(t1, t0, 16, 16);
        tcg_gen_sextract_tl(t0, t0,  0, 16);
        tcg_gen_sextract_tl(t3, t2, 16, 16);
        tcg_gen_sextract_tl(t2, t2,  0, 16);
    } else {
        tcg_gen_extract_tl(t1, t0, 16, 16);
        tcg_gen_extract_tl(t0, t0,  0, 16);
        tcg_gen_extract_tl(t3, t2, 16, 16);
        tcg_gen_extract_tl(t2, t2,  0, 16);
    }

    if (right) {
        if (arithmetic) {
            tcg_gen_sar_tl(t0, t0, t5);
            tcg_gen_sar_tl(t1, t1, t5);
            tcg_gen_sar_tl(t2, t2, t5);
            tcg_gen_sar_tl(t3, t3, t5);
        } else {
            tcg_gen_shr_tl(t0, t0, t5);
            tcg_gen_shr_tl(t1, t1, t5);
            tcg_gen_shr_tl(t2, t2, t5);
            tcg_gen_shr_tl(t3, t3, t5);
        }
    } else {
        tcg_gen_shl_tl(t0, t0, t5);
        tcg_gen_shl_tl(t1, t1, t5);
        tcg_gen_shl_tl(t2, t2, t5);
        tcg_gen_shl_tl(t3, t3, t5);
    }
    tcg_gen_deposit_tl(t0, t0, t1, 16, 16);
    tcg_gen_deposit_tl(t2, t2, t3, 16, 16);

    gen_store_mxu_gpr(t0, XRa);
    gen_store_mxu_gpr(t2, XRd);
}

static void gen_mxu_S32MAX_S32MIN(DisasContext *ctx)
{
    uint32_t pad, opc, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    opc = extract32(ctx->opcode, 18, 3);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
    } else if (unlikely(XRa == 0)) {
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        tcg_gen_movi_i32(mxu_gpr[XRa - 1], 0);
    } else if (unlikely((XRb == 0) || (XRc == 0))) {
        uint32_t XRx = XRb ? XRb : XRc;
        if (opc == OPC_MXU_S32MAX) {
            tcg_gen_smax_i32(mxu_gpr[XRa - 1], mxu_gpr[XRx - 1], 0);
        } else {
            tcg_gen_smin_i32(mxu_gpr[XRa - 1], mxu_gpr[XRx - 1], 0);
        }
    } else if (unlikely(XRb == XRc)) {
        tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
    } else {
        if (opc == OPC_MXU_S32MAX) {
            tcg_gen_smax_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1],
                                               mxu_gpr[XRc - 1]);
        } else {
            tcg_gen_smin_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1],
                                               mxu_gpr[XRc - 1]);
        }
    }
}

static void gen_mxu_D16MAX_D16MIN(DisasContext *ctx)
{
    uint32_t pad, opc, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    opc = extract32(ctx->opcode, 18, 3);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
    } else if (unlikely(XRa == 0)) {
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        tcg_gen_movi_i32(mxu_gpr[XRa - 1], 0);
    } else if (unlikely((XRb == 0) || (XRc == 0))) {
        uint32_t XRx = XRb ? XRb : XRc;
        TCGv_i32 t0 = tcg_temp_new();
        TCGv_i32 t1 = tcg_constant_i32(0);
        TCGv_i32 t2 = tcg_temp_new();

        tcg_gen_andi_i32(t0, mxu_gpr[XRx - 1], 0xFFFF0000);
        if (opc == OPC_MXU_D16MAX) {
            tcg_gen_smax_i32(t2, t0, t1);
        } else {
            tcg_gen_smin_i32(t2, t0, t1);
        }

        tcg_gen_andi_i32(t0, mxu_gpr[XRx - 1], 0x0000FFFF);
        tcg_gen_shli_i32(t0, t0, 16);
        if (opc == OPC_MXU_D16MAX) {
            tcg_gen_smax_i32(t0, t0, t1);
        } else {
            tcg_gen_smin_i32(t0, t0, t1);
        }
        tcg_gen_shri_i32(t0, t0, 16);
        tcg_gen_or_i32(mxu_gpr[XRa - 1], t2, t0);
    } else if (unlikely(XRb == XRc)) {
        tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
    } else {
        TCGv_i32 t0 = tcg_temp_new();
        TCGv_i32 t1 = tcg_temp_new();
        TCGv_i32 t2 = tcg_temp_new();

        tcg_gen_andi_i32(t0, mxu_gpr[XRb - 1], 0xFFFF0000);
        tcg_gen_andi_i32(t1, mxu_gpr[XRc - 1], 0xFFFF0000);
        if (opc == OPC_MXU_D16MAX) {
            tcg_gen_smax_i32(t2, t0, t1);
        } else {
            tcg_gen_smin_i32(t2, t0, t1);
        }

        tcg_gen_andi_i32(t0, mxu_gpr[XRb - 1], 0x0000FFFF);
        tcg_gen_andi_i32(t1, mxu_gpr[XRc - 1], 0x0000FFFF);
        tcg_gen_shli_i32(t0, t0, 16);
        tcg_gen_shli_i32(t1, t1, 16);
        if (opc == OPC_MXU_D16MAX) {
            tcg_gen_smax_i32(t0, t0, t1);
        } else {
            tcg_gen_smin_i32(t0, t0, t1);
        }
        tcg_gen_shri_i32(t0, t0, 16);
        tcg_gen_or_i32(mxu_gpr[XRa - 1], t2, t0);
    }
}

static void gen_mxu_Q8MAX_Q8MIN(DisasContext *ctx)
{
    uint32_t pad, opc, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    opc = extract32(ctx->opcode, 18, 3);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
    } else if (unlikely(XRa == 0)) {
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        tcg_gen_movi_i32(mxu_gpr[XRa - 1], 0);
    } else if (unlikely((XRb == 0) || (XRc == 0))) {
        uint32_t XRx = XRb ? XRb : XRc;
        TCGv_i32 t0 = tcg_temp_new();
        TCGv_i32 t1 = tcg_constant_i32(0);
        TCGv_i32 t2 = tcg_temp_new();
        int32_t i;

        tcg_gen_andi_i32(t0, mxu_gpr[XRx - 1], 0xFF000000);
        if (opc == OPC_MXU_Q8MAX) {
            tcg_gen_smax_i32(t2, t0, t1);
        } else {
            tcg_gen_smin_i32(t2, t0, t1);
        }

        for (i = 2; i >= 0; i--) {
            tcg_gen_andi_i32(t0, mxu_gpr[XRx - 1], 0xFF << (8 * i));
            tcg_gen_shli_i32(t0, t0, 8 * (3 - i));
            if (opc == OPC_MXU_Q8MAX) {
                tcg_gen_smax_i32(t0, t0, t1);
            } else {
                tcg_gen_smin_i32(t0, t0, t1);
            }
            tcg_gen_shri_i32(t0, t0, 8 * (3 - i));
            tcg_gen_or_i32(t2, t2, t0);
        }
        gen_store_mxu_gpr(t2, XRa);
    } else if (unlikely(XRb == XRc)) {
        tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
    } else {
        TCGv_i32 t0 = tcg_temp_new();
        TCGv_i32 t1 = tcg_temp_new();
        TCGv_i32 t2 = tcg_temp_new();
        int32_t i;

        tcg_gen_andi_i32(t0, mxu_gpr[XRb - 1], 0xFF000000);
        tcg_gen_andi_i32(t1, mxu_gpr[XRc - 1], 0xFF000000);
        if (opc == OPC_MXU_Q8MAX) {
            tcg_gen_smax_i32(t2, t0, t1);
        } else {
            tcg_gen_smin_i32(t2, t0, t1);
        }

        for (i = 2; i >= 0; i--) {
            tcg_gen_andi_i32(t0, mxu_gpr[XRb - 1], 0xFF << (8 * i));
            tcg_gen_andi_i32(t1, mxu_gpr[XRc - 1], 0xFF << (8 * i));
            tcg_gen_shli_i32(t0, t0, 8 * (3 - i));
            tcg_gen_shli_i32(t1, t1, 8 * (3 - i));
            if (opc == OPC_MXU_Q8MAX) {
                tcg_gen_smax_i32(t0, t0, t1);
            } else {
                tcg_gen_smin_i32(t0, t0, t1);
            }
            tcg_gen_shri_i32(t0, t0, 8 * (3 - i));
            tcg_gen_or_i32(t2, t2, t0);
        }
        gen_store_mxu_gpr(t2, XRa);
    }
}

static void gen_mxu_q8slt(DisasContext *ctx, bool sltu)
{
    uint32_t pad, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
    } else if (unlikely(XRa == 0)) {
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        tcg_gen_movi_tl(mxu_gpr[XRa - 1], 0);
    } else if (unlikely(XRb == XRc)) {
        tcg_gen_movi_tl(mxu_gpr[XRa - 1], 0);
    } else {
        TCGv t0 = tcg_temp_new();
        TCGv t1 = tcg_temp_new();
        TCGv t2 = tcg_temp_new();
        TCGv t3 = tcg_temp_new();
        TCGv t4 = tcg_temp_new();

        gen_load_mxu_gpr(t3, XRb);
        gen_load_mxu_gpr(t4, XRc);
        tcg_gen_movi_tl(t2, 0);

        for (int i = 0; i < 4; i++) {
            if (sltu) {
                tcg_gen_extract_tl(t0, t3, 8 * i, 8);
                tcg_gen_extract_tl(t1, t4, 8 * i, 8);
            } else {
                tcg_gen_sextract_tl(t0, t3, 8 * i, 8);
                tcg_gen_sextract_tl(t1, t4, 8 * i, 8);
            }
            tcg_gen_setcond_tl(TCG_COND_LT, t0, t0, t1);
            tcg_gen_deposit_tl(t2, t2, t0, 8 * i, 8);
        }
        gen_store_mxu_gpr(t2, XRa);
    }
}

static void gen_mxu_S32SLT(DisasContext *ctx)
{
    uint32_t pad, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
    } else if (unlikely(XRa == 0)) {
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        tcg_gen_movi_tl(mxu_gpr[XRa - 1], 0);
    } else if (unlikely(XRb == XRc)) {
        tcg_gen_movi_tl(mxu_gpr[XRa - 1], 0);
    } else {
        TCGv t0 = tcg_temp_new();
        TCGv t1 = tcg_temp_new();

        gen_load_mxu_gpr(t0, XRb);
        gen_load_mxu_gpr(t1, XRc);
        tcg_gen_setcond_tl(TCG_COND_LT, mxu_gpr[XRa - 1], t0, t1);
    }
}

static void gen_mxu_D16SLT(DisasContext *ctx)
{
    uint32_t pad, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
    } else if (unlikely(XRa == 0)) {
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        tcg_gen_movi_tl(mxu_gpr[XRa - 1], 0);
    } else if (unlikely(XRb == XRc)) {
        tcg_gen_movi_tl(mxu_gpr[XRa - 1], 0);
    } else {
        TCGv t0 = tcg_temp_new();
        TCGv t1 = tcg_temp_new();
        TCGv t2 = tcg_temp_new();
        TCGv t3 = tcg_temp_new();
        TCGv t4 = tcg_temp_new();

        gen_load_mxu_gpr(t3, XRb);
        gen_load_mxu_gpr(t4, XRc);
        tcg_gen_sextract_tl(t0, t3, 16, 16);
        tcg_gen_sextract_tl(t1, t4, 16, 16);
        tcg_gen_setcond_tl(TCG_COND_LT, t0, t0, t1);
        tcg_gen_shli_tl(t2, t0, 16);
        tcg_gen_sextract_tl(t0, t3,  0, 16);
        tcg_gen_sextract_tl(t1, t4,  0, 16);
        tcg_gen_setcond_tl(TCG_COND_LT, t0, t0, t1);
        tcg_gen_or_tl(mxu_gpr[XRa - 1], t2, t0);
    }
}

static void gen_mxu_d16avg(DisasContext *ctx, bool round45)
{
    uint32_t pad, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
    } else if (unlikely(XRa == 0)) {
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        tcg_gen_movi_tl(mxu_gpr[XRa - 1], 0);
    } else if (unlikely(XRb == XRc)) {
        tcg_gen_mov_tl(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
    } else {
        TCGv t0 = tcg_temp_new();
        TCGv t1 = tcg_temp_new();
        TCGv t2 = tcg_temp_new();
        TCGv t3 = tcg_temp_new();
        TCGv t4 = tcg_temp_new();

        gen_load_mxu_gpr(t3, XRb);
        gen_load_mxu_gpr(t4, XRc);
        tcg_gen_sextract_tl(t0, t3, 16, 16);
        tcg_gen_sextract_tl(t1, t4, 16, 16);
        tcg_gen_add_tl(t0, t0, t1);
        if (round45) {
            tcg_gen_addi_tl(t0, t0, 1);
        }
        tcg_gen_shli_tl(t2, t0, 15);
        tcg_gen_andi_tl(t2, t2, 0xffff0000);
        tcg_gen_sextract_tl(t0, t3,  0, 16);
        tcg_gen_sextract_tl(t1, t4,  0, 16);
        tcg_gen_add_tl(t0, t0, t1);
        if (round45) {
            tcg_gen_addi_tl(t0, t0, 1);
        }
        tcg_gen_shri_tl(t0, t0, 1);
        tcg_gen_deposit_tl(t2, t2, t0, 0, 16);
        gen_store_mxu_gpr(t2, XRa);
    }
}

static void gen_mxu_q8avg(DisasContext *ctx, bool round45)
{
    uint32_t pad, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
    } else if (unlikely(XRa == 0)) {
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        tcg_gen_movi_tl(mxu_gpr[XRa - 1], 0);
    } else if (unlikely(XRb == XRc)) {
        tcg_gen_mov_tl(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
    } else {
        TCGv t0 = tcg_temp_new();
        TCGv t1 = tcg_temp_new();
        TCGv t2 = tcg_temp_new();
        TCGv t3 = tcg_temp_new();
        TCGv t4 = tcg_temp_new();

        gen_load_mxu_gpr(t3, XRb);
        gen_load_mxu_gpr(t4, XRc);
        tcg_gen_movi_tl(t2, 0);

        for (int i = 0; i < 4; i++) {
            tcg_gen_extract_tl(t0, t3, 8 * i, 8);
            tcg_gen_extract_tl(t1, t4, 8 * i, 8);
            tcg_gen_add_tl(t0, t0, t1);
            if (round45) {
                tcg_gen_addi_tl(t0, t0, 1);
            }
            tcg_gen_shri_tl(t0, t0, 1);
            tcg_gen_deposit_tl(t2, t2, t0, 8 * i, 8);
        }
        gen_store_mxu_gpr(t2, XRa);
    }
}

static void gen_mxu_q8movzn(DisasContext *ctx, TCGCond cond)
{
    uint32_t XRc, XRb, XRa;

    XRa = extract32(ctx->opcode,  6, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRc = extract32(ctx->opcode, 14, 4);

    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    TCGv t3 = tcg_temp_new();
    TCGLabel *l_quarterdone = gen_new_label();
    TCGLabel *l_halfdone = gen_new_label();
    TCGLabel *l_quarterrest = gen_new_label();
    TCGLabel *l_done = gen_new_label();

    gen_load_mxu_gpr(t0, XRc);
    gen_load_mxu_gpr(t1, XRb);
    gen_load_mxu_gpr(t2, XRa);

    tcg_gen_extract_tl(t3, t1, 24, 8);
    tcg_gen_brcondi_tl(cond, t3, 0, l_quarterdone);
    tcg_gen_extract_tl(t3, t0, 24, 8);
    tcg_gen_deposit_tl(t2, t2, t3, 24, 8);

    gen_set_label(l_quarterdone);
    tcg_gen_extract_tl(t3, t1, 16, 8);
    tcg_gen_brcondi_tl(cond, t3, 0, l_halfdone);
    tcg_gen_extract_tl(t3, t0, 16, 8);
    tcg_gen_deposit_tl(t2, t2, t3, 16, 8);

    gen_set_label(l_halfdone);
    tcg_gen_extract_tl(t3, t1, 8, 8);
    tcg_gen_brcondi_tl(cond, t3, 0, l_quarterrest);
    tcg_gen_extract_tl(t3, t0, 8, 8);
    tcg_gen_deposit_tl(t2, t2, t3, 8, 8);

    gen_set_label(l_quarterrest);
    tcg_gen_extract_tl(t3, t1, 0, 8);
    tcg_gen_brcondi_tl(cond, t3, 0, l_done);
    tcg_gen_extract_tl(t3, t0, 0, 8);
    tcg_gen_deposit_tl(t2, t2, t3, 0, 8);

    gen_set_label(l_done);
    gen_store_mxu_gpr(t2, XRa);
}

static void gen_mxu_d16movzn(DisasContext *ctx, TCGCond cond)
{
    uint32_t XRc, XRb, XRa;

    XRa = extract32(ctx->opcode,  6, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRc = extract32(ctx->opcode, 14, 4);

    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    TCGv t3 = tcg_temp_new();
    TCGLabel *l_halfdone = gen_new_label();
    TCGLabel *l_done = gen_new_label();

    gen_load_mxu_gpr(t0, XRc);
    gen_load_mxu_gpr(t1, XRb);
    gen_load_mxu_gpr(t2, XRa);

    tcg_gen_extract_tl(t3, t1, 16, 16);
    tcg_gen_brcondi_tl(cond, t3, 0, l_halfdone);
    tcg_gen_extract_tl(t3, t0, 16, 16);
    tcg_gen_deposit_tl(t2, t2, t3, 16, 16);

    gen_set_label(l_halfdone);
    tcg_gen_extract_tl(t3, t1, 0, 16);
    tcg_gen_brcondi_tl(cond, t3, 0, l_done);
    tcg_gen_extract_tl(t3, t0, 0, 16);
    tcg_gen_deposit_tl(t2, t2, t3, 0, 16);

    gen_set_label(l_done);
    gen_store_mxu_gpr(t2, XRa);
}

static void gen_mxu_s32movzn(DisasContext *ctx, TCGCond cond)
{
    uint32_t XRc, XRb, XRa;

    XRa = extract32(ctx->opcode,  6, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRc = extract32(ctx->opcode, 14, 4);

    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGLabel *l_done = gen_new_label();

    gen_load_mxu_gpr(t0, XRc);
    gen_load_mxu_gpr(t1, XRb);

    tcg_gen_brcondi_tl(cond, t1, 0, l_done);
    gen_store_mxu_gpr(t0, XRa);
    gen_set_label(l_done);
}

static void gen_mxu_S32CPS(DisasContext *ctx)
{
    uint32_t pad, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
    } else if (unlikely(XRa == 0)) {
    } else if (unlikely(XRb == 0)) {
        tcg_gen_movi_tl(mxu_gpr[XRa - 1], 0);
    } else if (unlikely(XRc == 0)) {
        tcg_gen_mov_tl(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
    } else {
        TCGv t0 = tcg_temp_new();
        TCGLabel *l_not_less = gen_new_label();
        TCGLabel *l_done = gen_new_label();

        tcg_gen_brcondi_tl(TCG_COND_GE, mxu_gpr[XRc - 1], 0, l_not_less);
        tcg_gen_neg_tl(t0, mxu_gpr[XRb - 1]);
        tcg_gen_br(l_done);
        gen_set_label(l_not_less);
        gen_load_mxu_gpr(t0, XRb);
        gen_set_label(l_done);
        gen_store_mxu_gpr(t0, XRa);
    }
}

static void gen_mxu_D16CPS(DisasContext *ctx)
{
    uint32_t pad, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 5);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
    } else if (unlikely(XRa == 0)) {
    } else if (unlikely(XRb == 0)) {
        tcg_gen_movi_tl(mxu_gpr[XRa - 1], 0);
    } else if (unlikely(XRc == 0)) {
        tcg_gen_mov_tl(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
    } else {
        TCGv t0 = tcg_temp_new();
        TCGv t1 = tcg_temp_new();
        TCGLabel *l_done_hi = gen_new_label();
        TCGLabel *l_not_less_lo = gen_new_label();
        TCGLabel *l_done_lo = gen_new_label();

        tcg_gen_sextract_tl(t0, mxu_gpr[XRc - 1], 16, 16);
        tcg_gen_sextract_tl(t1, mxu_gpr[XRb - 1], 16, 16);
        tcg_gen_brcondi_tl(TCG_COND_GE, t0, 0, l_done_hi);
        tcg_gen_subfi_tl(t1, 0, t1);

        gen_set_label(l_done_hi);
        tcg_gen_shli_i32(t1, t1, 16);

        tcg_gen_sextract_tl(t0, mxu_gpr[XRc - 1],  0, 16);
        tcg_gen_brcondi_tl(TCG_COND_GE, t0, 0, l_not_less_lo);
        tcg_gen_sextract_tl(t0, mxu_gpr[XRb - 1],  0, 16);
        tcg_gen_subfi_tl(t0, 0, t0);
        tcg_gen_br(l_done_lo);

        gen_set_label(l_not_less_lo);
        tcg_gen_extract_tl(t0, mxu_gpr[XRb - 1],  0, 16);

        gen_set_label(l_done_lo);
        tcg_gen_deposit_tl(mxu_gpr[XRa - 1], t1, t0, 0, 16);
    }
}

static void gen_mxu_Q8ABD(DisasContext *ctx)
{
    uint32_t pad, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 3);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
    } else if (unlikely(XRa == 0)) {
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        tcg_gen_movi_tl(mxu_gpr[XRa - 1], 0);
    } else {
        TCGv t0 = tcg_temp_new();
        TCGv t1 = tcg_temp_new();
        TCGv t2 = tcg_temp_new();
        TCGv t3 = tcg_temp_new();
        TCGv t4 = tcg_temp_new();

        gen_load_mxu_gpr(t3, XRb);
        gen_load_mxu_gpr(t4, XRc);
        tcg_gen_movi_tl(t2, 0);

        for (int i = 0; i < 4; i++) {
            tcg_gen_extract_tl(t0, t3, 8 * i, 8);
            tcg_gen_extract_tl(t1, t4, 8 * i, 8);

            tcg_gen_sub_tl(t0, t0, t1);
            tcg_gen_abs_tl(t0, t0);

            tcg_gen_deposit_tl(t2, t2, t0, 8 * i, 8);
        }
        gen_store_mxu_gpr(t2, XRa);
    }
}

static void gen_mxu_Q8ADD(DisasContext *ctx)
{
    uint32_t aptn2, pad, XRc, XRb, XRa;

    aptn2 = extract32(ctx->opcode, 24, 2);
    pad   = extract32(ctx->opcode, 21, 3);
    XRc   = extract32(ctx->opcode, 14, 4);
    XRb   = extract32(ctx->opcode, 10, 4);
    XRa   = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
    } else if (unlikely(XRa == 0)) {
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        tcg_gen_movi_i32(mxu_gpr[XRa - 1], 0);
    } else {
        TCGv t0 = tcg_temp_new();
        TCGv t1 = tcg_temp_new();
        TCGv t2 = tcg_temp_new();
        TCGv t3 = tcg_temp_new();
        TCGv t4 = tcg_temp_new();

        gen_load_mxu_gpr(t3, XRb);
        gen_load_mxu_gpr(t4, XRc);

        for (int i = 0; i < 4; i++) {
            tcg_gen_andi_tl(t0, t3, 0xff);
            tcg_gen_andi_tl(t1, t4, 0xff);

            if (i < 2) {
                if (aptn2 & 0x01) {
                    tcg_gen_sub_tl(t0, t0, t1);
                } else {
                    tcg_gen_add_tl(t0, t0, t1);
                }
            } else {
                if (aptn2 & 0x02) {
                    tcg_gen_sub_tl(t0, t0, t1);
                } else {
                    tcg_gen_add_tl(t0, t0, t1);
                }
            }
            if (i < 3) {
                tcg_gen_shri_tl(t3, t3, 8);
                tcg_gen_shri_tl(t4, t4, 8);
            }
            if (i > 0) {
                tcg_gen_deposit_tl(t2, t2, t0, 8 * i, 8);
            } else {
                tcg_gen_andi_tl(t0, t0, 0xff);
                tcg_gen_mov_tl(t2, t0);
            }
        }
        gen_store_mxu_gpr(t2, XRa);
    }
}

static void gen_mxu_q8adde(DisasContext *ctx, bool accumulate)
{
    uint32_t aptn2, XRd, XRc, XRb, XRa;

    aptn2 = extract32(ctx->opcode, 24, 2);
    XRd   = extract32(ctx->opcode, 18, 4);
    XRc   = extract32(ctx->opcode, 14, 4);
    XRb   = extract32(ctx->opcode, 10, 4);
    XRa   = extract32(ctx->opcode,  6, 4);

    if (unlikely((XRb == 0) && (XRc == 0))) {
        if (XRa != 0) {
            tcg_gen_movi_tl(mxu_gpr[XRa - 1], 0);
        }
        if (XRd != 0) {
            tcg_gen_movi_tl(mxu_gpr[XRd - 1], 0);
        }
    } else {
        TCGv t0 = tcg_temp_new();
        TCGv t1 = tcg_temp_new();
        TCGv t2 = tcg_temp_new();
        TCGv t3 = tcg_temp_new();
        TCGv t4 = tcg_temp_new();
        TCGv t5 = tcg_temp_new();

        if (XRa != 0) {
            gen_extract_mxu_gpr(t0, XRb, 16, 8);
            gen_extract_mxu_gpr(t1, XRc, 16, 8);
            gen_extract_mxu_gpr(t2, XRb, 24, 8);
            gen_extract_mxu_gpr(t3, XRc, 24, 8);
            if (aptn2 & 2) {
                tcg_gen_sub_tl(t0, t0, t1);
                tcg_gen_sub_tl(t2, t2, t3);
            } else {
                tcg_gen_add_tl(t0, t0, t1);
                tcg_gen_add_tl(t2, t2, t3);
            }
            if (accumulate) {
                gen_load_mxu_gpr(t5, XRa);
                tcg_gen_extract_tl(t1, t5,  0, 16);
                tcg_gen_extract_tl(t3, t5, 16, 16);
                tcg_gen_add_tl(t0, t0, t1);
                tcg_gen_add_tl(t2, t2, t3);
            }
            tcg_gen_shli_tl(t2, t2, 16);
            tcg_gen_extract_tl(t0, t0, 0, 16);
            tcg_gen_or_tl(t4, t2, t0);
        }
        if (XRd != 0) {
            gen_extract_mxu_gpr(t0, XRb, 0, 8);
            gen_extract_mxu_gpr(t1, XRc, 0, 8);
            gen_extract_mxu_gpr(t2, XRb, 8, 8);
            gen_extract_mxu_gpr(t3, XRc, 8, 8);
            if (aptn2 & 1) {
                tcg_gen_sub_tl(t0, t0, t1);
                tcg_gen_sub_tl(t2, t2, t3);
            } else {
                tcg_gen_add_tl(t0, t0, t1);
                tcg_gen_add_tl(t2, t2, t3);
            }
            if (accumulate) {
                gen_load_mxu_gpr(t5, XRd);
                tcg_gen_extract_tl(t1, t5,  0, 16);
                tcg_gen_extract_tl(t3, t5, 16, 16);
                tcg_gen_add_tl(t0, t0, t1);
                tcg_gen_add_tl(t2, t2, t3);
            }
            tcg_gen_shli_tl(t2, t2, 16);
            tcg_gen_extract_tl(t0, t0, 0, 16);
            tcg_gen_or_tl(t5, t2, t0);
        }

        gen_store_mxu_gpr(t4, XRa);
        gen_store_mxu_gpr(t5, XRd);
    }
}

static void gen_mxu_d8sum(DisasContext *ctx, bool sumc)
{
    uint32_t pad, pad2, XRc, XRb, XRa;

    pad  = extract32(ctx->opcode, 24, 2);
    pad2 = extract32(ctx->opcode, 18, 4);
    XRc  = extract32(ctx->opcode, 14, 4);
    XRb  = extract32(ctx->opcode, 10, 4);
    XRa  = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0 || pad2 != 0)) {
    } else if (unlikely(XRa == 0)) {
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        tcg_gen_movi_tl(mxu_gpr[XRa - 1], 0);
    } else {
        TCGv t0 = tcg_temp_new();
        TCGv t1 = tcg_temp_new();
        TCGv t2 = tcg_temp_new();
        TCGv t3 = tcg_temp_new();
        TCGv t4 = tcg_temp_new();
        TCGv t5 = tcg_temp_new();

        if (XRb != 0) {
            tcg_gen_extract_tl(t0, mxu_gpr[XRb - 1],  0, 8);
            tcg_gen_extract_tl(t1, mxu_gpr[XRb - 1],  8, 8);
            tcg_gen_extract_tl(t2, mxu_gpr[XRb - 1], 16, 8);
            tcg_gen_extract_tl(t3, mxu_gpr[XRb - 1], 24, 8);
            tcg_gen_add_tl(t4, t0, t1);
            tcg_gen_add_tl(t4, t4, t2);
            tcg_gen_add_tl(t4, t4, t3);
        } else {
            tcg_gen_mov_tl(t4, 0);
        }
        if (XRc != 0) {
            tcg_gen_extract_tl(t0, mxu_gpr[XRc - 1],  0, 8);
            tcg_gen_extract_tl(t1, mxu_gpr[XRc - 1],  8, 8);
            tcg_gen_extract_tl(t2, mxu_gpr[XRc - 1], 16, 8);
            tcg_gen_extract_tl(t3, mxu_gpr[XRc - 1], 24, 8);
            tcg_gen_add_tl(t5, t0, t1);
            tcg_gen_add_tl(t5, t5, t2);
            tcg_gen_add_tl(t5, t5, t3);
        } else {
            tcg_gen_mov_tl(t5, 0);
        }

        if (sumc) {
            tcg_gen_addi_tl(t4, t4, 2);
            tcg_gen_addi_tl(t5, t5, 2);
        }
        tcg_gen_shli_tl(t4, t4, 16);

        tcg_gen_or_tl(mxu_gpr[XRa - 1], t4, t5);
    }
}

static void gen_mxu_q16add(DisasContext *ctx)
{
    uint32_t aptn2, optn2, XRc, XRb, XRa, XRd;

    aptn2 = extract32(ctx->opcode, 24, 2);
    optn2 = extract32(ctx->opcode, 22, 2);
    XRd   = extract32(ctx->opcode, 18, 4);
    XRc   = extract32(ctx->opcode, 14, 4);
    XRb   = extract32(ctx->opcode, 10, 4);
    XRa   = extract32(ctx->opcode,  6, 4);

    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    TCGv t3 = tcg_temp_new();
    TCGv t4 = tcg_temp_new();
    TCGv t5 = tcg_temp_new();

    gen_load_mxu_gpr(t1, XRb);
    tcg_gen_extract_tl(t0, t1,  0, 16);
    tcg_gen_extract_tl(t1, t1, 16, 16);

    gen_load_mxu_gpr(t3, XRc);
    tcg_gen_extract_tl(t2, t3,  0, 16);
    tcg_gen_extract_tl(t3, t3, 16, 16);

    switch (optn2) {
    case MXU_OPTN2_WW:
        tcg_gen_mov_tl(t4, t1);
        tcg_gen_mov_tl(t5, t0);
        break;
    case MXU_OPTN2_LW:
        tcg_gen_mov_tl(t4, t0);
        tcg_gen_mov_tl(t5, t0);
        break;
    case MXU_OPTN2_HW:
        tcg_gen_mov_tl(t4, t1);
        tcg_gen_mov_tl(t5, t1);
        break;
    case MXU_OPTN2_XW:
        tcg_gen_mov_tl(t4, t0);
        tcg_gen_mov_tl(t5, t1);
        break;
    }

    switch (aptn2) {
    case MXU_APTN2_AA:
        tcg_gen_add_tl(t0, t4, t3);
        tcg_gen_add_tl(t1, t5, t2);
        tcg_gen_add_tl(t4, t4, t3);
        tcg_gen_add_tl(t5, t5, t2);
        break;
    case MXU_APTN2_AS:
        tcg_gen_sub_tl(t0, t4, t3);
        tcg_gen_sub_tl(t1, t5, t2);
        tcg_gen_add_tl(t4, t4, t3);
        tcg_gen_add_tl(t5, t5, t2);
        break;
    case MXU_APTN2_SA:
        tcg_gen_add_tl(t0, t4, t3);
        tcg_gen_add_tl(t1, t5, t2);
        tcg_gen_sub_tl(t4, t4, t3);
        tcg_gen_sub_tl(t5, t5, t2);
        break;
    case MXU_APTN2_SS:
        tcg_gen_sub_tl(t0, t4, t3);
        tcg_gen_sub_tl(t1, t5, t2);
        tcg_gen_sub_tl(t4, t4, t3);
        tcg_gen_sub_tl(t5, t5, t2);
        break;
    }

    tcg_gen_shli_tl(t0, t0, 16);
    tcg_gen_extract_tl(t1, t1, 0, 16);
    tcg_gen_shli_tl(t4, t4, 16);
    tcg_gen_extract_tl(t5, t5, 0, 16);

    tcg_gen_or_tl(mxu_gpr[XRa - 1], t4, t5);
    tcg_gen_or_tl(mxu_gpr[XRd - 1], t0, t1);
}

static void gen_mxu_q16acc(DisasContext *ctx)
{
    uint32_t aptn2, XRc, XRb, XRa, XRd;

    aptn2 = extract32(ctx->opcode, 24, 2);
    XRd   = extract32(ctx->opcode, 18, 4);
    XRc   = extract32(ctx->opcode, 14, 4);
    XRb   = extract32(ctx->opcode, 10, 4);
    XRa   = extract32(ctx->opcode,  6, 4);

    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    TCGv t3 = tcg_temp_new();
    TCGv s3 = tcg_temp_new();
    TCGv s2 = tcg_temp_new();
    TCGv s1 = tcg_temp_new();
    TCGv s0 = tcg_temp_new();

    gen_load_mxu_gpr(t1, XRb);
    tcg_gen_extract_tl(t0, t1,  0, 16);
    tcg_gen_extract_tl(t1, t1, 16, 16);

    gen_load_mxu_gpr(t3, XRc);
    tcg_gen_extract_tl(t2, t3,  0, 16);
    tcg_gen_extract_tl(t3, t3, 16, 16);

    switch (aptn2) {
    case MXU_APTN2_AA:
        tcg_gen_add_tl(s3, t1, t3);
        tcg_gen_add_tl(s2, t0, t2);
        tcg_gen_add_tl(s1, t1, t3);
        tcg_gen_add_tl(s0, t0, t2);
        break;
    case MXU_APTN2_AS:
        tcg_gen_sub_tl(s3, t1, t3);
        tcg_gen_sub_tl(s2, t0, t2);
        tcg_gen_add_tl(s1, t1, t3);
        tcg_gen_add_tl(s0, t0, t2);
        break;
    case MXU_APTN2_SA:
        tcg_gen_add_tl(s3, t1, t3);
        tcg_gen_add_tl(s2, t0, t2);
        tcg_gen_sub_tl(s1, t1, t3);
        tcg_gen_sub_tl(s0, t0, t2);
        break;
    case MXU_APTN2_SS:
        tcg_gen_sub_tl(s3, t1, t3);
        tcg_gen_sub_tl(s2, t0, t2);
        tcg_gen_sub_tl(s1, t1, t3);
        tcg_gen_sub_tl(s0, t0, t2);
        break;
    }

    if (XRa != 0) {
        tcg_gen_add_tl(t0, mxu_gpr[XRa - 1], s0);
        tcg_gen_extract_tl(t0, t0, 0, 16);
        tcg_gen_extract_tl(t1, mxu_gpr[XRa - 1], 16, 16);
        tcg_gen_add_tl(t1, t1, s1);
        tcg_gen_shli_tl(t1, t1, 16);
        tcg_gen_or_tl(mxu_gpr[XRa - 1], t1, t0);
    }

    if (XRd != 0) {
        tcg_gen_add_tl(t0, mxu_gpr[XRd - 1], s2);
        tcg_gen_extract_tl(t0, t0, 0, 16);
        tcg_gen_extract_tl(t1, mxu_gpr[XRd - 1], 16, 16);
        tcg_gen_add_tl(t1, t1, s3);
        tcg_gen_shli_tl(t1, t1, 16);
        tcg_gen_or_tl(mxu_gpr[XRd - 1], t1, t0);
    }
}

static void gen_mxu_q16accm(DisasContext *ctx)
{
    uint32_t aptn2, XRc, XRb, XRa, XRd;

    aptn2 = extract32(ctx->opcode, 24, 2);
    XRd   = extract32(ctx->opcode, 18, 4);
    XRc   = extract32(ctx->opcode, 14, 4);
    XRb   = extract32(ctx->opcode, 10, 4);
    XRa   = extract32(ctx->opcode,  6, 4);

    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    TCGv t3 = tcg_temp_new();

    gen_load_mxu_gpr(t2, XRb);
    gen_load_mxu_gpr(t3, XRc);

    if (XRa != 0) {
        TCGv a0 = tcg_temp_new();
        TCGv a1 = tcg_temp_new();

        tcg_gen_extract_tl(t0, t2,  0, 16);
        tcg_gen_extract_tl(t1, t2, 16, 16);

        gen_load_mxu_gpr(a1, XRa);
        tcg_gen_extract_tl(a0, a1,  0, 16);
        tcg_gen_extract_tl(a1, a1, 16, 16);

        if (aptn2 & 2) {
            tcg_gen_sub_tl(a0, a0, t0);
            tcg_gen_sub_tl(a1, a1, t1);
        } else {
            tcg_gen_add_tl(a0, a0, t0);
            tcg_gen_add_tl(a1, a1, t1);
        }
        tcg_gen_extract_tl(a0, a0, 0, 16);
        tcg_gen_shli_tl(a1, a1, 16);
        tcg_gen_or_tl(mxu_gpr[XRa - 1], a1, a0);
    }

    if (XRd != 0) {
        TCGv a0 = tcg_temp_new();
        TCGv a1 = tcg_temp_new();

        tcg_gen_extract_tl(t0, t3,  0, 16);
        tcg_gen_extract_tl(t1, t3, 16, 16);

        gen_load_mxu_gpr(a1, XRd);
        tcg_gen_extract_tl(a0, a1,  0, 16);
        tcg_gen_extract_tl(a1, a1, 16, 16);

        if (aptn2 & 1) {
            tcg_gen_sub_tl(a0, a0, t0);
            tcg_gen_sub_tl(a1, a1, t1);
        } else {
            tcg_gen_add_tl(a0, a0, t0);
            tcg_gen_add_tl(a1, a1, t1);
        }
        tcg_gen_extract_tl(a0, a0, 0, 16);
        tcg_gen_shli_tl(a1, a1, 16);
        tcg_gen_or_tl(mxu_gpr[XRd - 1], a1, a0);
    }
}


static void gen_mxu_d16asum(DisasContext *ctx)
{
    uint32_t aptn2, XRc, XRb, XRa, XRd;

    aptn2 = extract32(ctx->opcode, 24, 2);
    XRd   = extract32(ctx->opcode, 18, 4);
    XRc   = extract32(ctx->opcode, 14, 4);
    XRb   = extract32(ctx->opcode, 10, 4);
    XRa   = extract32(ctx->opcode,  6, 4);

    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    TCGv t3 = tcg_temp_new();

    gen_load_mxu_gpr(t2, XRb);
    gen_load_mxu_gpr(t3, XRc);

    if (XRa != 0) {
        tcg_gen_sextract_tl(t0, t2,  0, 16);
        tcg_gen_sextract_tl(t1, t2, 16, 16);
        tcg_gen_add_tl(t0, t0, t1);
        if (aptn2 & 2) {
            tcg_gen_sub_tl(mxu_gpr[XRa - 1], mxu_gpr[XRa - 1], t0);
        } else {
            tcg_gen_add_tl(mxu_gpr[XRa - 1], mxu_gpr[XRa - 1], t0);
        }
    }

    if (XRd != 0) {
        tcg_gen_sextract_tl(t0, t3,  0, 16);
        tcg_gen_sextract_tl(t1, t3, 16, 16);
        tcg_gen_add_tl(t0, t0, t1);
        if (aptn2 & 1) {
            tcg_gen_sub_tl(mxu_gpr[XRd - 1], mxu_gpr[XRd - 1], t0);
        } else {
            tcg_gen_add_tl(mxu_gpr[XRd - 1], mxu_gpr[XRd - 1], t0);
        }
    }
}

static void gen_mxu_d32add(DisasContext *ctx)
{
    uint32_t aptn2, addc, XRc, XRb, XRa, XRd;

    aptn2 = extract32(ctx->opcode, 24, 2);
    addc  = extract32(ctx->opcode, 22, 2);
    XRd   = extract32(ctx->opcode, 18, 4);
    XRc   = extract32(ctx->opcode, 14, 4);
    XRb   = extract32(ctx->opcode, 10, 4);
    XRa   = extract32(ctx->opcode,  6, 4);

    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    TCGv cr = tcg_temp_new();

    if (unlikely(addc > 1)) {
    } else if (addc == 1) {
        if (unlikely(XRa == 0 && XRd == 0)) {
        } else {
            gen_load_mxu_gpr(t0, XRb);
            gen_load_mxu_gpr(t1, XRc);
            gen_load_mxu_cr(cr);
            if (XRa != 0) {
                tcg_gen_extract_tl(t2, cr, 31, 1);
                tcg_gen_add_tl(t0, t0, t2);
                tcg_gen_add_tl(mxu_gpr[XRa - 1], mxu_gpr[XRa - 1], t0);
            }
            if (XRd != 0) {
                tcg_gen_extract_tl(t2, cr, 30, 1);
                tcg_gen_add_tl(t1, t1, t2);
                tcg_gen_add_tl(mxu_gpr[XRd - 1], mxu_gpr[XRd - 1], t1);
            }
        }
    } else if (unlikely(XRa == 0 && XRd == 0)) {
    } else {
        TCGv carry = tcg_temp_new();

        gen_load_mxu_gpr(t0, XRb);
        gen_load_mxu_gpr(t1, XRc);
        gen_load_mxu_cr(cr);
        if (XRa != 0) {
            if (aptn2 & 2) {
                tcg_gen_sub_i32(t2, t0, t1);
                tcg_gen_setcond_tl(TCG_COND_GTU, carry, t0, t1);
            } else {
                tcg_gen_add_i32(t2, t0, t1);
                tcg_gen_setcond_tl(TCG_COND_GTU, carry, t0, t2);
            }
            tcg_gen_andi_tl(cr, cr, 0x7fffffff);
            tcg_gen_shli_tl(carry, carry, 31);
            tcg_gen_or_tl(cr, cr, carry);
            gen_store_mxu_gpr(t2, XRa);
        }
        if (XRd != 0) {
            if (aptn2 & 1) {
                tcg_gen_sub_i32(t2, t0, t1);
                tcg_gen_setcond_tl(TCG_COND_GTU, carry, t0, t1);
            } else {
                tcg_gen_add_i32(t2, t0, t1);
                tcg_gen_setcond_tl(TCG_COND_GTU, carry, t0, t2);
            }
            tcg_gen_andi_tl(cr, cr, 0xbfffffff);
            tcg_gen_shli_tl(carry, carry, 30);
            tcg_gen_or_tl(cr, cr, carry);
            gen_store_mxu_gpr(t2, XRd);
        }
        gen_store_mxu_cr(cr);
    }
}

static void gen_mxu_d32acc(DisasContext *ctx)
{
    uint32_t aptn2, XRc, XRb, XRa, XRd;

    aptn2 = extract32(ctx->opcode, 24, 2);
    XRd   = extract32(ctx->opcode, 18, 4);
    XRc   = extract32(ctx->opcode, 14, 4);
    XRb   = extract32(ctx->opcode, 10, 4);
    XRa   = extract32(ctx->opcode,  6, 4);

    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();

    if (unlikely(XRa == 0 && XRd == 0)) {
    } else {
        gen_load_mxu_gpr(t0, XRb);
        gen_load_mxu_gpr(t1, XRc);
        if (XRa != 0) {
            if (aptn2 & 2) {
                tcg_gen_sub_tl(t2, t0, t1);
            } else {
                tcg_gen_add_tl(t2, t0, t1);
            }
            tcg_gen_add_tl(mxu_gpr[XRa - 1], mxu_gpr[XRa - 1], t2);
        }
        if (XRd != 0) {
            if (aptn2 & 1) {
                tcg_gen_sub_tl(t2, t0, t1);
            } else {
                tcg_gen_add_tl(t2, t0, t1);
            }
            tcg_gen_add_tl(mxu_gpr[XRd - 1], mxu_gpr[XRd - 1], t2);
        }
    }
}

static void gen_mxu_d32accm(DisasContext *ctx)
{
    uint32_t aptn2, XRc, XRb, XRa, XRd;

    aptn2 = extract32(ctx->opcode, 24, 2);
    XRd   = extract32(ctx->opcode, 18, 4);
    XRc   = extract32(ctx->opcode, 14, 4);
    XRb   = extract32(ctx->opcode, 10, 4);
    XRa   = extract32(ctx->opcode,  6, 4);

    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();

    if (unlikely(XRa == 0 && XRd == 0)) {
    } else {
        gen_load_mxu_gpr(t0, XRb);
        gen_load_mxu_gpr(t1, XRc);
        if (XRa != 0) {
            tcg_gen_add_tl(t2, t0, t1);
            if (aptn2 & 2) {
                tcg_gen_sub_tl(mxu_gpr[XRa - 1], mxu_gpr[XRa - 1], t2);
            } else {
                tcg_gen_add_tl(mxu_gpr[XRa - 1], mxu_gpr[XRa - 1], t2);
            }
        }
        if (XRd != 0) {
            tcg_gen_sub_tl(t2, t0, t1);
            if (aptn2 & 1) {
                tcg_gen_sub_tl(mxu_gpr[XRd - 1], mxu_gpr[XRd - 1], t2);
            } else {
                tcg_gen_add_tl(mxu_gpr[XRd - 1], mxu_gpr[XRd - 1], t2);
            }
        }
    }
}

static void gen_mxu_d32asum(DisasContext *ctx)
{
    uint32_t aptn2, XRc, XRb, XRa, XRd;

    aptn2 = extract32(ctx->opcode, 24, 2);
    XRd   = extract32(ctx->opcode, 18, 4);
    XRc   = extract32(ctx->opcode, 14, 4);
    XRb   = extract32(ctx->opcode, 10, 4);
    XRa   = extract32(ctx->opcode,  6, 4);

    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();

    if (unlikely(XRa == 0 && XRd == 0)) {
    } else {
        gen_load_mxu_gpr(t0, XRb);
        gen_load_mxu_gpr(t1, XRc);
        if (XRa != 0) {
            if (aptn2 & 2) {
                tcg_gen_sub_tl(mxu_gpr[XRa - 1], mxu_gpr[XRa - 1], t0);
            } else {
                tcg_gen_add_tl(mxu_gpr[XRa - 1], mxu_gpr[XRa - 1], t0);
            }
        }
        if (XRd != 0) {
            if (aptn2 & 1) {
                tcg_gen_sub_tl(mxu_gpr[XRd - 1], mxu_gpr[XRd - 1], t1);
            } else {
                tcg_gen_add_tl(mxu_gpr[XRd - 1], mxu_gpr[XRd - 1], t1);
            }
        }
    }
}

static void gen_mxu_s32extr(DisasContext *ctx)
{
    TCGv t0, t1, t2, t3;
    uint32_t XRa, XRd, rs, bits5;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    t2 = tcg_temp_new();
    t3 = tcg_temp_new();

    XRa   = extract32(ctx->opcode,  6, 4);
    XRd   = extract32(ctx->opcode, 10, 4);
    bits5 = extract32(ctx->opcode, 16, 5);
    rs    = extract32(ctx->opcode, 21, 5);

    if (bits5 > 0) {
        TCGLabel *l_xra_only = gen_new_label();
        TCGLabel *l_done = gen_new_label();

        gen_load_mxu_gpr(t0, XRd);
        gen_load_mxu_gpr(t1, XRa);
        gen_load_gpr(t2, rs);
        tcg_gen_andi_tl(t2, t2, 0x1f);
        tcg_gen_subfi_tl(t2, 32, t2);
        tcg_gen_brcondi_tl(TCG_COND_GE, t2, bits5, l_xra_only);
        tcg_gen_subfi_tl(t2, bits5, t2);
        tcg_gen_subfi_tl(t3, 32, t2);
        tcg_gen_shr_tl(t0, t0, t3);
        tcg_gen_shl_tl(t1, t1, t2);
        tcg_gen_or_tl(t0, t0, t1);
        tcg_gen_br(l_done);
        gen_set_label(l_xra_only);
        tcg_gen_subi_tl(t2, t2, bits5);
        tcg_gen_shr_tl(t0, t1, t2);
        gen_set_label(l_done);
        tcg_gen_extract_tl(t0, t0, 0, bits5);
    } else {
        tcg_gen_movi_tl(t0, 0);
    }
    gen_store_mxu_gpr(t0, XRa);
}

static void gen_mxu_s32extrv(DisasContext *ctx)
{
    TCGv t0, t1, t2, t3, t4;
    uint32_t XRa, XRd, rs, rt;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    t2 = tcg_temp_new();
    t3 = tcg_temp_new();
    t4 = tcg_temp_new();
    TCGLabel *l_xra_only = gen_new_label();
    TCGLabel *l_done = gen_new_label();
    TCGLabel *l_zero = gen_new_label();
    TCGLabel *l_extract = gen_new_label();

    XRa = extract32(ctx->opcode,  6, 4);
    XRd = extract32(ctx->opcode, 10, 4);
    rt  = extract32(ctx->opcode, 16, 5);
    rs  = extract32(ctx->opcode, 21, 5);

    gen_load_mxu_gpr(t0, XRd);
    gen_load_mxu_gpr(t1, XRa);
    gen_load_gpr(t2, rs);
    gen_load_gpr(t4, rt);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t4, 0, l_zero);
    tcg_gen_andi_tl(t2, t2, 0x1f);
    tcg_gen_subfi_tl(t2, 32, t2);
    tcg_gen_brcond_tl(TCG_COND_GE, t2, t4, l_xra_only);
    tcg_gen_sub_tl(t2, t4, t2);
    tcg_gen_subfi_tl(t3, 32, t2);
    tcg_gen_shr_tl(t0, t0, t3);
    tcg_gen_shl_tl(t1, t1, t2);
    tcg_gen_or_tl(t0, t0, t1);
    tcg_gen_br(l_extract);

    gen_set_label(l_xra_only);
    tcg_gen_sub_tl(t2, t2, t4);
    tcg_gen_shr_tl(t0, t1, t2);
    tcg_gen_br(l_extract);

    gen_set_label(l_zero);
    tcg_gen_movi_tl(t0, 0);
    tcg_gen_br(l_done);

    gen_set_label(l_extract);
    tcg_gen_subfi_tl(t4, 32, t4);
    tcg_gen_shl_tl(t0, t0, t4);
    tcg_gen_shr_tl(t0, t0, t4);

    gen_set_label(l_done);
    gen_store_mxu_gpr(t0, XRa);
}

static void gen_mxu_s32lui(DisasContext *ctx)
{
    uint32_t XRa, s8, optn3, pad;

    XRa   = extract32(ctx->opcode,  6, 4);
    s8    = extract32(ctx->opcode, 10, 8);
    pad   = extract32(ctx->opcode, 21, 2);
    optn3 = extract32(ctx->opcode, 23, 3);

    if (unlikely(pad != 0)) {
    } else if (unlikely(XRa == 0)) {
    } else {
        uint32_t s16;
        TCGv t0 = tcg_temp_new();

        switch (optn3) {
        case 0:
            tcg_gen_movi_tl(t0, s8);
            break;
        case 1:
            tcg_gen_movi_tl(t0, s8 << 8);
            break;
        case 2:
            tcg_gen_movi_tl(t0, s8 << 16);
            break;
        case 3:
            tcg_gen_movi_tl(t0, s8 << 24);
            break;
        case 4:
            tcg_gen_movi_tl(t0, (s8 << 16) | s8);
            break;
        case 5:
            tcg_gen_movi_tl(t0, (s8 << 24) | (s8 << 8));
            break;
        case 6:
            s16 = (uint16_t)(int16_t)(int8_t)s8;
            tcg_gen_movi_tl(t0, (s16 << 16) | s16);
            break;
        case 7:
            tcg_gen_movi_tl(t0, (s8 << 24) | (s8 << 16) | (s8 << 8) | s8);
            break;
        }
        gen_store_mxu_gpr(t0, XRa);
    }
}

static void gen_mxu_Q16SAT(DisasContext *ctx)
{
    uint32_t pad, XRc, XRb, XRa;

    pad = extract32(ctx->opcode, 21, 3);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
    } else if (unlikely(XRa == 0)) {
    } else {
        TCGv t0 = tcg_temp_new();
        TCGv t1 = tcg_temp_new();
        TCGv t2 = tcg_temp_new();

        tcg_gen_movi_tl(t2, 0);
        if (XRb != 0) {
            TCGLabel *l_less_hi = gen_new_label();
            TCGLabel *l_less_lo = gen_new_label();
            TCGLabel *l_lo = gen_new_label();
            TCGLabel *l_greater_hi = gen_new_label();
            TCGLabel *l_greater_lo = gen_new_label();
            TCGLabel *l_done = gen_new_label();

            tcg_gen_sari_tl(t0, mxu_gpr[XRb - 1], 16);
            tcg_gen_brcondi_tl(TCG_COND_LT, t0, 0, l_less_hi);
            tcg_gen_brcondi_tl(TCG_COND_GT, t0, 255, l_greater_hi);
            tcg_gen_br(l_lo);
            gen_set_label(l_less_hi);
            tcg_gen_movi_tl(t0, 0);
            tcg_gen_br(l_lo);
            gen_set_label(l_greater_hi);
            tcg_gen_movi_tl(t0, 255);

            gen_set_label(l_lo);
            tcg_gen_shli_tl(t1, mxu_gpr[XRb - 1], 16);
            tcg_gen_sari_tl(t1, t1, 16);
            tcg_gen_brcondi_tl(TCG_COND_LT, t1, 0, l_less_lo);
            tcg_gen_brcondi_tl(TCG_COND_GT, t1, 255, l_greater_lo);
            tcg_gen_br(l_done);
            gen_set_label(l_less_lo);
            tcg_gen_movi_tl(t1, 0);
            tcg_gen_br(l_done);
            gen_set_label(l_greater_lo);
            tcg_gen_movi_tl(t1, 255);

            gen_set_label(l_done);
            tcg_gen_shli_tl(t2, t0, 24);
            tcg_gen_shli_tl(t1, t1, 16);
            tcg_gen_or_tl(t2, t2, t1);
        }

        if (XRc != 0) {
            TCGLabel *l_less_hi = gen_new_label();
            TCGLabel *l_less_lo = gen_new_label();
            TCGLabel *l_lo = gen_new_label();
            TCGLabel *l_greater_hi = gen_new_label();
            TCGLabel *l_greater_lo = gen_new_label();
            TCGLabel *l_done = gen_new_label();

            tcg_gen_sari_tl(t0, mxu_gpr[XRc - 1], 16);
            tcg_gen_brcondi_tl(TCG_COND_LT, t0, 0, l_less_hi);
            tcg_gen_brcondi_tl(TCG_COND_GT, t0, 255, l_greater_hi);
            tcg_gen_br(l_lo);
            gen_set_label(l_less_hi);
            tcg_gen_movi_tl(t0, 0);
            tcg_gen_br(l_lo);
            gen_set_label(l_greater_hi);
            tcg_gen_movi_tl(t0, 255);

            gen_set_label(l_lo);
            tcg_gen_shli_tl(t1, mxu_gpr[XRc - 1], 16);
            tcg_gen_sari_tl(t1, t1, 16);
            tcg_gen_brcondi_tl(TCG_COND_LT, t1, 0, l_less_lo);
            tcg_gen_brcondi_tl(TCG_COND_GT, t1, 255, l_greater_lo);
            tcg_gen_br(l_done);
            gen_set_label(l_less_lo);
            tcg_gen_movi_tl(t1, 0);
            tcg_gen_br(l_done);
            gen_set_label(l_greater_lo);
            tcg_gen_movi_tl(t1, 255);

            gen_set_label(l_done);
            tcg_gen_shli_tl(t0, t0, 8);
            tcg_gen_or_tl(t2, t2, t0);
            tcg_gen_or_tl(t2, t2, t1);
        }
        gen_store_mxu_gpr(t2, XRa);
    }
}

static void gen_mxu_q16scop(DisasContext *ctx)
{
    uint32_t XRd, XRc, XRb, XRa;

    XRd  = extract32(ctx->opcode, 18, 4);
    XRc  = extract32(ctx->opcode, 14, 4);
    XRb  = extract32(ctx->opcode, 10, 4);
    XRa  = extract32(ctx->opcode,  6, 4);

    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    TCGv t3 = tcg_temp_new();
    TCGv t4 = tcg_temp_new();

    TCGLabel *l_b_hi_lt = gen_new_label();
    TCGLabel *l_b_hi_gt = gen_new_label();
    TCGLabel *l_b_lo = gen_new_label();
    TCGLabel *l_b_lo_lt = gen_new_label();
    TCGLabel *l_c_hi = gen_new_label();
    TCGLabel *l_c_hi_lt = gen_new_label();
    TCGLabel *l_c_hi_gt = gen_new_label();
    TCGLabel *l_c_lo = gen_new_label();
    TCGLabel *l_c_lo_lt = gen_new_label();
    TCGLabel *l_done = gen_new_label();

    gen_load_mxu_gpr(t0, XRb);
    gen_load_mxu_gpr(t1, XRc);

    tcg_gen_sextract_tl(t2, t0, 16, 16);
    tcg_gen_brcondi_tl(TCG_COND_LT, t2, 0, l_b_hi_lt);
    tcg_gen_brcondi_tl(TCG_COND_GT, t2, 0, l_b_hi_gt);
    tcg_gen_movi_tl(t3, 0);
    tcg_gen_br(l_b_lo);
    gen_set_label(l_b_hi_lt);
    tcg_gen_movi_tl(t3, 0xffff0000);
    tcg_gen_br(l_b_lo);
    gen_set_label(l_b_hi_gt);
    tcg_gen_movi_tl(t3, 0x00010000);

    gen_set_label(l_b_lo);
    tcg_gen_sextract_tl(t2, t0, 0, 16);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t2, 0, l_c_hi);
    tcg_gen_brcondi_tl(TCG_COND_LT, t2, 0, l_b_lo_lt);
    tcg_gen_ori_tl(t3, t3, 0x00000001);
    tcg_gen_br(l_c_hi);
    gen_set_label(l_b_lo_lt);
    tcg_gen_ori_tl(t3, t3, 0x0000ffff);
    tcg_gen_br(l_c_hi);

    gen_set_label(l_c_hi);
    tcg_gen_sextract_tl(t2, t1, 16, 16);
    tcg_gen_brcondi_tl(TCG_COND_LT, t2, 0, l_c_hi_lt);
    tcg_gen_brcondi_tl(TCG_COND_GT, t2, 0, l_c_hi_gt);
    tcg_gen_movi_tl(t4, 0);
    tcg_gen_br(l_c_lo);
    gen_set_label(l_c_hi_lt);
    tcg_gen_movi_tl(t4, 0xffff0000);
    tcg_gen_br(l_c_lo);
    gen_set_label(l_c_hi_gt);
    tcg_gen_movi_tl(t4, 0x00010000);

    gen_set_label(l_c_lo);
    tcg_gen_sextract_tl(t2, t1, 0, 16);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t2, 0, l_done);
    tcg_gen_brcondi_tl(TCG_COND_LT, t2, 0, l_c_lo_lt);
    tcg_gen_ori_tl(t4, t4, 0x00000001);
    tcg_gen_br(l_done);
    gen_set_label(l_c_lo_lt);
    tcg_gen_ori_tl(t4, t4, 0x0000ffff);

    gen_set_label(l_done);
    gen_store_mxu_gpr(t3, XRa);
    gen_store_mxu_gpr(t4, XRd);
}

static void gen_mxu_s32sfl(DisasContext *ctx)
{
    uint32_t XRd, XRc, XRb, XRa, ptn2;

    XRd  = extract32(ctx->opcode, 18, 4);
    XRc  = extract32(ctx->opcode, 14, 4);
    XRb  = extract32(ctx->opcode, 10, 4);
    XRa  = extract32(ctx->opcode,  6, 4);
    ptn2 = extract32(ctx->opcode, 24, 2);

    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    TCGv t3 = tcg_temp_new();

    gen_load_mxu_gpr(t0, XRb);
    gen_load_mxu_gpr(t1, XRc);

    switch (ptn2) {
    case 0:
        tcg_gen_andi_tl(t2, t0, 0xff000000);
        tcg_gen_andi_tl(t3, t1, 0x000000ff);
        tcg_gen_deposit_tl(t3, t3, t0,  8, 8);
        tcg_gen_shri_tl(t0, t0,  8);
        tcg_gen_shri_tl(t1, t1,  8);
        tcg_gen_deposit_tl(t3, t3, t0, 24, 8);
        tcg_gen_deposit_tl(t3, t3, t1, 16, 8);
        tcg_gen_shri_tl(t0, t0,  8);
        tcg_gen_shri_tl(t1, t1,  8);
        tcg_gen_deposit_tl(t2, t2, t0,  8, 8);
        tcg_gen_deposit_tl(t2, t2, t1,  0, 8);
        tcg_gen_shri_tl(t1, t1,  8);
        tcg_gen_deposit_tl(t2, t2, t1, 16, 8);
        break;
    case 1:
        tcg_gen_andi_tl(t2, t0, 0xff000000);
        tcg_gen_andi_tl(t3, t1, 0x000000ff);
        tcg_gen_deposit_tl(t3, t3, t0, 16, 8);
        tcg_gen_shri_tl(t0, t0,  8);
        tcg_gen_shri_tl(t1, t1,  8);
        tcg_gen_deposit_tl(t2, t2, t0, 16, 8);
        tcg_gen_deposit_tl(t2, t2, t1,  0, 8);
        tcg_gen_shri_tl(t0, t0,  8);
        tcg_gen_shri_tl(t1, t1,  8);
        tcg_gen_deposit_tl(t3, t3, t0, 24, 8);
        tcg_gen_deposit_tl(t3, t3, t1,  8, 8);
        tcg_gen_shri_tl(t1, t1,  8);
        tcg_gen_deposit_tl(t2, t2, t1,  8, 8);
        break;
    case 2:
        tcg_gen_andi_tl(t2, t0, 0xff00ff00);
        tcg_gen_andi_tl(t3, t1, 0x00ff00ff);
        tcg_gen_deposit_tl(t3, t3, t0,  8, 8);
        tcg_gen_shri_tl(t0, t0, 16);
        tcg_gen_shri_tl(t1, t1,  8);
        tcg_gen_deposit_tl(t2, t2, t1,  0, 8);
        tcg_gen_deposit_tl(t3, t3, t0, 24, 8);
        tcg_gen_shri_tl(t1, t1, 16);
        tcg_gen_deposit_tl(t2, t2, t1, 16, 8);
        break;
    case 3:
        tcg_gen_andi_tl(t2, t0, 0xffff0000);
        tcg_gen_andi_tl(t3, t1, 0x0000ffff);
        tcg_gen_shri_tl(t1, t1, 16);
        tcg_gen_deposit_tl(t2, t2, t1,  0, 16);
        tcg_gen_deposit_tl(t3, t3, t0, 16, 16);
        break;
    }

    gen_store_mxu_gpr(t2, XRa);
    gen_store_mxu_gpr(t3, XRd);
}

static void gen_mxu_q8sad(DisasContext *ctx)
{
    uint32_t XRd, XRc, XRb, XRa;

    XRd = extract32(ctx->opcode, 18, 4);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    TCGv t3 = tcg_temp_new();
    TCGv t4 = tcg_temp_new();
    TCGv t5 = tcg_temp_new();

    gen_load_mxu_gpr(t2, XRb);
    gen_load_mxu_gpr(t3, XRc);
    gen_load_mxu_gpr(t5, XRd);
    tcg_gen_movi_tl(t4, 0);

    for (int i = 0; i < 4; i++) {
        tcg_gen_andi_tl(t0, t2, 0xff);
        tcg_gen_andi_tl(t1, t3, 0xff);
        tcg_gen_sub_tl(t0, t0, t1);
        tcg_gen_abs_tl(t0, t0);
        tcg_gen_add_tl(t4, t4, t0);
        if (i < 3) {
            tcg_gen_shri_tl(t2, t2, 8);
            tcg_gen_shri_tl(t3, t3, 8);
        }
    }
    tcg_gen_add_tl(t5, t5, t4);
    gen_store_mxu_gpr(t4, XRa);
    gen_store_mxu_gpr(t5, XRd);
}

static void gen_mxu_S32ALNI(DisasContext *ctx)
{
    uint32_t optn3, pad, XRc, XRb, XRa;

    optn3 = extract32(ctx->opcode,  23, 3);
    pad   = extract32(ctx->opcode,  21, 2);
    XRc   = extract32(ctx->opcode, 14, 4);
    XRb   = extract32(ctx->opcode, 10, 4);
    XRa   = extract32(ctx->opcode,  6, 4);

    if (unlikely(pad != 0)) {
    } else if (unlikely(XRa == 0)) {
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        tcg_gen_movi_i32(mxu_gpr[XRa - 1], 0);
    } else if (unlikely(XRb == 0)) {
        switch (optn3) {
        case MXU_OPTN3_PTN0:
            tcg_gen_movi_i32(mxu_gpr[XRa - 1], 0);
            break;
        case MXU_OPTN3_PTN1:
        case MXU_OPTN3_PTN2:
        case MXU_OPTN3_PTN3:
            tcg_gen_shri_i32(mxu_gpr[XRa - 1], mxu_gpr[XRc - 1],
                             8 * (4 - optn3));
            break;
        case MXU_OPTN3_PTN4:
            tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRc - 1]);
            break;
        }
    } else if (unlikely(XRc == 0)) {
        switch (optn3) {
        case MXU_OPTN3_PTN0:
            tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
            break;
        case MXU_OPTN3_PTN1:
        case MXU_OPTN3_PTN2:
        case MXU_OPTN3_PTN3:
            tcg_gen_shri_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1], 8 * optn3);
            break;
        case MXU_OPTN3_PTN4:
            tcg_gen_movi_i32(mxu_gpr[XRa - 1], 0);
            break;
        }
    } else if (unlikely(XRb == XRc)) {
        switch (optn3) {
        case MXU_OPTN3_PTN0:
        case MXU_OPTN3_PTN4:
            tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
            break;
        case MXU_OPTN3_PTN1:
        case MXU_OPTN3_PTN2:
        case MXU_OPTN3_PTN3:
            tcg_gen_rotli_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1], 8 * optn3);
            break;
        }
    } else {
        switch (optn3) {
        case MXU_OPTN3_PTN0:
            {

                tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRb - 1]);
            }
            break;
        case MXU_OPTN3_PTN1:
            {

                TCGv_i32 t0 = tcg_temp_new();
                TCGv_i32 t1 = tcg_temp_new();

                tcg_gen_andi_i32(t0, mxu_gpr[XRb - 1], 0x00FFFFFF);
                tcg_gen_shli_i32(t0, t0, 8);

                tcg_gen_andi_i32(t1, mxu_gpr[XRc - 1], 0xFF000000);
                tcg_gen_shri_i32(t1, t1, 24);

                tcg_gen_or_i32(mxu_gpr[XRa - 1], t0, t1);
            }
            break;
        case MXU_OPTN3_PTN2:
            {

                TCGv_i32 t0 = tcg_temp_new();
                TCGv_i32 t1 = tcg_temp_new();

                tcg_gen_andi_i32(t0, mxu_gpr[XRb - 1], 0x0000FFFF);
                tcg_gen_shli_i32(t0, t0, 16);

                tcg_gen_andi_i32(t1, mxu_gpr[XRc - 1], 0xFFFF0000);
                tcg_gen_shri_i32(t1, t1, 16);

                tcg_gen_or_i32(mxu_gpr[XRa - 1], t0, t1);
            }
            break;
        case MXU_OPTN3_PTN3:
            {

                TCGv_i32 t0 = tcg_temp_new();
                TCGv_i32 t1 = tcg_temp_new();

                tcg_gen_andi_i32(t0, mxu_gpr[XRb - 1], 0x000000FF);
                tcg_gen_shli_i32(t0, t0, 24);

                tcg_gen_andi_i32(t1, mxu_gpr[XRc - 1], 0xFFFFFF00);
                tcg_gen_shri_i32(t1, t1, 8);

                tcg_gen_or_i32(mxu_gpr[XRa - 1], t0, t1);
            }
            break;
        case MXU_OPTN3_PTN4:
            {

                tcg_gen_mov_i32(mxu_gpr[XRa - 1], mxu_gpr[XRc - 1]);
            }
            break;
        }
    }
}

static void gen_mxu_S32ALN(DisasContext *ctx)
{
    uint32_t rs, XRc, XRb, XRa;

    rs  = extract32(ctx->opcode, 21, 5);
    XRc = extract32(ctx->opcode, 14, 4);
    XRb = extract32(ctx->opcode, 10, 4);
    XRa = extract32(ctx->opcode,  6, 4);

    if (unlikely(XRa == 0)) {
    } else if (unlikely((XRb == 0) && (XRc == 0))) {
        tcg_gen_movi_tl(mxu_gpr[XRa - 1], 0);
    } else {
        TCGv t0 = tcg_temp_new();
        TCGv t1 = tcg_temp_new();
        TCGv t2 = tcg_temp_new();
        TCGv t3 = tcg_temp_new();
        TCGLabel *l_exit = gen_new_label();
        TCGLabel *l_b_only = gen_new_label();
        TCGLabel *l_c_only = gen_new_label();

        gen_load_mxu_gpr(t0, XRb);
        gen_load_mxu_gpr(t1, XRc);
        gen_load_gpr(t2, rs);
        tcg_gen_andi_tl(t2, t2, 0x07);

        tcg_gen_brcondi_tl(TCG_COND_GE, t2, 5, l_exit);

        tcg_gen_brcondi_tl(TCG_COND_EQ, t2, 0, l_b_only);
        tcg_gen_brcondi_tl(TCG_COND_EQ, t2, 4, l_c_only);

        tcg_gen_shli_tl(t2, t2, 3);
        tcg_gen_subfi_tl(t3, 32, t2);

        tcg_gen_shl_tl(t0, t0, t2);
        tcg_gen_shr_tl(t1, t1, t3);
        tcg_gen_or_tl(mxu_gpr[XRa - 1], t0, t1);
        tcg_gen_br(l_exit);

        gen_set_label(l_b_only);
        gen_store_mxu_gpr(t0, XRa);
        tcg_gen_br(l_exit);

        gen_set_label(l_c_only);
        gen_store_mxu_gpr(t1, XRa);

        gen_set_label(l_exit);
    }
}

static void gen_mxu_s32madd_sub(DisasContext *ctx, bool sub, bool uns)
{
    uint32_t XRa, XRd, Rb, Rc;

    XRa  = extract32(ctx->opcode,  6, 4);
    XRd  = extract32(ctx->opcode, 10, 4);
    Rb   = extract32(ctx->opcode, 16, 5);
    Rc   = extract32(ctx->opcode, 21, 5);

    if (unlikely(Rb == 0 || Rc == 0)) {
    } else if (unlikely(XRa == 0 && XRd == 0)) {
    } else {
        TCGv t0 = tcg_temp_new();
        TCGv t1 = tcg_temp_new();
        TCGv_i64 t2 = tcg_temp_new_i64();
        TCGv_i64 t3 = tcg_temp_new_i64();

        gen_load_gpr(t0, Rb);
        gen_load_gpr(t1, Rc);

        if (uns) {
            tcg_gen_extu_tl_i64(t2, t0);
            tcg_gen_extu_tl_i64(t3, t1);
        } else {
            tcg_gen_ext_tl_i64(t2, t0);
            tcg_gen_ext_tl_i64(t3, t1);
        }
        tcg_gen_mul_i64(t2, t2, t3);

        gen_load_mxu_gpr(t0, XRa);
        gen_load_mxu_gpr(t1, XRd);

        tcg_gen_concat_tl_i64(t3, t1, t0);
        if (sub) {
            tcg_gen_sub_i64(t3, t3, t2);
        } else {
            tcg_gen_add_i64(t3, t3, t2);
        }
        gen_move_low32(t1, t3);
        gen_move_high32(t0, t3);

        tcg_gen_mov_tl(cpu_HI[0], t0);
        tcg_gen_mov_tl(cpu_LO[0], t1);

        gen_store_mxu_gpr(t1, XRd);
        gen_store_mxu_gpr(t0, XRa);
    }
}

static void decode_opc_mxu__pool00(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 18, 3);

    switch (opcode) {
    case OPC_MXU_S32MAX:
    case OPC_MXU_S32MIN:
        gen_mxu_S32MAX_S32MIN(ctx);
        break;
    case OPC_MXU_D16MAX:
    case OPC_MXU_D16MIN:
        gen_mxu_D16MAX_D16MIN(ctx);
        break;
    case OPC_MXU_Q8MAX:
    case OPC_MXU_Q8MIN:
        gen_mxu_Q8MAX_Q8MIN(ctx);
        break;
    case OPC_MXU_Q8SLT:
        gen_mxu_q8slt(ctx, false);
        break;
    case OPC_MXU_Q8SLTU:
        gen_mxu_q8slt(ctx, true);
        break;
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

static bool decode_opc_mxu_s32madd_sub(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 0, 6);
    uint32_t pad  = extract32(ctx->opcode, 14, 2);

    if (pad != 2) {
        return false;
    }

    switch (opcode) {
    case OPC_MXU_S32MADD:
        gen_mxu_s32madd_sub(ctx, false, false);
        break;
    case OPC_MXU_S32MADDU:
        gen_mxu_s32madd_sub(ctx, false, true);
        break;
    case OPC_MXU_S32MSUB:
        gen_mxu_s32madd_sub(ctx, true, false);
        break;
    case OPC_MXU_S32MSUBU:
        gen_mxu_s32madd_sub(ctx, true, true);
        break;
    default:
        return false;
    }
    return true;
}

static void decode_opc_mxu__pool01(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 18, 3);

    switch (opcode) {
    case OPC_MXU_S32SLT:
        gen_mxu_S32SLT(ctx);
        break;
    case OPC_MXU_D16SLT:
        gen_mxu_D16SLT(ctx);
        break;
    case OPC_MXU_D16AVG:
        gen_mxu_d16avg(ctx, false);
        break;
    case OPC_MXU_D16AVGR:
        gen_mxu_d16avg(ctx, true);
        break;
    case OPC_MXU_Q8AVG:
        gen_mxu_q8avg(ctx, false);
        break;
    case OPC_MXU_Q8AVGR:
        gen_mxu_q8avg(ctx, true);
        break;
    case OPC_MXU_Q8ADD:
        gen_mxu_Q8ADD(ctx);
        break;
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void decode_opc_mxu__pool02(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 18, 3);

    switch (opcode) {
    case OPC_MXU_S32CPS:
        gen_mxu_S32CPS(ctx);
        break;
    case OPC_MXU_D16CPS:
        gen_mxu_D16CPS(ctx);
        break;
    case OPC_MXU_Q8ABD:
        gen_mxu_Q8ABD(ctx);
        break;
    case OPC_MXU_Q16SAT:
        gen_mxu_Q16SAT(ctx);
        break;
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void decode_opc_mxu__pool03(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 24, 2);

    switch (opcode) {
    case OPC_MXU_D16MULF:
        gen_mxu_d16mul(ctx, true, true);
        break;
    case OPC_MXU_D16MULE:
        gen_mxu_d16mul(ctx, true, false);
        break;
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void decode_opc_mxu__pool04(DisasContext *ctx)
{
    uint32_t reversed = extract32(ctx->opcode, 20, 1);
    uint32_t opcode = extract32(ctx->opcode, 10, 4);

    switch (opcode) {
    default:
        gen_mxu_s32ldxx(ctx, reversed, false);
        break;
    }
}

static void decode_opc_mxu__pool05(DisasContext *ctx)
{
    uint32_t reversed = extract32(ctx->opcode, 20, 1);
    uint32_t opcode = extract32(ctx->opcode, 10, 4);

    switch (opcode) {
    default:
        gen_mxu_s32stxx(ctx, reversed, false);
        break;
    }
}

static void decode_opc_mxu__pool06(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 10, 4);
    uint32_t strd2  = extract32(ctx->opcode, 14, 2);

    switch (opcode) {
    case OPC_MXU_S32LDST:
    case OPC_MXU_S32LDSTR:
        if (strd2 <= 2) {
            gen_mxu_s32ldxvx(ctx, opcode, false, strd2);
            break;
        }
        /* fallthrough */
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void decode_opc_mxu__pool07(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 10, 4);
    uint32_t strd2  = extract32(ctx->opcode, 14, 2);

    switch (opcode) {
    case OPC_MXU_S32LDST:
    case OPC_MXU_S32LDSTR:
        if (strd2 <= 2) {
            gen_mxu_s32stxvx(ctx, opcode, false, strd2);
            break;
        }
        /* fallthrough */
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void decode_opc_mxu__pool08(DisasContext *ctx)
{
    uint32_t reversed = extract32(ctx->opcode, 20, 1);
    uint32_t opcode = extract32(ctx->opcode, 10, 4);

    switch (opcode) {
    default:
        gen_mxu_s32ldxx(ctx, reversed, true);
        break;
    }
}

static void decode_opc_mxu__pool09(DisasContext *ctx)
{
    uint32_t reversed = extract32(ctx->opcode, 20, 1);
    uint32_t opcode = extract32(ctx->opcode, 10, 4);

    switch (opcode) {
    default:
        gen_mxu_s32stxx(ctx, reversed, true);
        break;
    }
}

static void decode_opc_mxu__pool10(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 10, 4);
    uint32_t strd2  = extract32(ctx->opcode, 14, 2);

    switch (opcode) {
    case OPC_MXU_S32LDST:
    case OPC_MXU_S32LDSTR:
        if (strd2 <= 2) {
            gen_mxu_s32ldxvx(ctx, opcode, true, strd2);
            break;
        }
        /* fallthrough */
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void decode_opc_mxu__pool11(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 10, 4);
    uint32_t strd2  = extract32(ctx->opcode, 14, 2);

    switch (opcode) {
    case OPC_MXU_S32LDST:
    case OPC_MXU_S32LDSTR:
        if (strd2 <= 2) {
            gen_mxu_s32stxvx(ctx, opcode, true, strd2);
            break;
        }
        /* fallthrough */
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void decode_opc_mxu__pool12(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 22, 2);

    switch (opcode) {
    case OPC_MXU_D32ACC:
        gen_mxu_d32acc(ctx);
        break;
    case OPC_MXU_D32ACCM:
        gen_mxu_d32accm(ctx);
        break;
    case OPC_MXU_D32ASUM:
        gen_mxu_d32asum(ctx);
        break;
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void decode_opc_mxu__pool13(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 22, 2);

    switch (opcode) {
    case OPC_MXU_Q16ACC:
        gen_mxu_q16acc(ctx);
        break;
    case OPC_MXU_Q16ACCM:
        gen_mxu_q16accm(ctx);
        break;
    case OPC_MXU_D16ASUM:
        gen_mxu_d16asum(ctx);
        break;
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void decode_opc_mxu__pool14(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 22, 2);

    switch (opcode) {
    case OPC_MXU_Q8ADDE:
        gen_mxu_q8adde(ctx, false);
        break;
    case OPC_MXU_D8SUM:
        gen_mxu_d8sum(ctx, false);
        break;
    case OPC_MXU_D8SUMC:
        gen_mxu_d8sum(ctx, true);
        break;
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void decode_opc_mxu__pool15(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 14, 2);

    switch (opcode) {
    case OPC_MXU_S32MUL:
        gen_mxu_s32mul(ctx, false);
        break;
    case OPC_MXU_S32MULU:
        gen_mxu_s32mul(ctx, true);
        break;
    case OPC_MXU_S32EXTR:
        gen_mxu_s32extr(ctx);
        break;
    case OPC_MXU_S32EXTRV:
        gen_mxu_s32extrv(ctx);
        break;
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void decode_opc_mxu__pool16(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 18, 3);

    switch (opcode) {
    case OPC_MXU_D32SARW:
        gen_mxu_d32sarl(ctx, true);
        break;
    case OPC_MXU_S32ALN:
        gen_mxu_S32ALN(ctx);
        break;
    case OPC_MXU_S32ALNI:
        gen_mxu_S32ALNI(ctx);
        break;
    case OPC_MXU_S32LUI:
        gen_mxu_s32lui(ctx);
        break;
    case OPC_MXU_S32NOR:
        gen_mxu_S32NOR(ctx);
        break;
    case OPC_MXU_S32AND:
        gen_mxu_S32AND(ctx);
        break;
    case OPC_MXU_S32OR:
        gen_mxu_S32OR(ctx);
        break;
    case OPC_MXU_S32XOR:
        gen_mxu_S32XOR(ctx);
        break;
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void decode_opc_mxu__pool17(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 6, 3);
    uint32_t strd2  = extract32(ctx->opcode, 9, 2);

    if (strd2 > 2) {
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        return;
    }

    switch (opcode) {
    case OPC_MXU_LXW:
          gen_mxu_lxx(ctx, strd2, mo_endian(ctx) | MO_UL);
          break;
    case OPC_MXU_LXB:
          gen_mxu_lxx(ctx, strd2, mo_endian(ctx) | MO_SB);
          break;
    case OPC_MXU_LXH:
          gen_mxu_lxx(ctx, strd2, mo_endian(ctx) | MO_SW);
          break;
    case OPC_MXU_LXBU:
          gen_mxu_lxx(ctx, strd2, mo_endian(ctx) | MO_UB);
          break;
    case OPC_MXU_LXHU:
          gen_mxu_lxx(ctx, strd2, mo_endian(ctx) | MO_UW);
          break;
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void decode_opc_mxu__pool18(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 18, 3);

    switch (opcode) {
    case OPC_MXU_D32SLLV:
        gen_mxu_d32sxxv(ctx, false, false);
        break;
    case OPC_MXU_D32SLRV:
        gen_mxu_d32sxxv(ctx, true, false);
        break;
    case OPC_MXU_D32SARV:
        gen_mxu_d32sxxv(ctx, true, true);
        break;
    case OPC_MXU_Q16SLLV:
        gen_mxu_q16sxxv(ctx, false, false);
        break;
    case OPC_MXU_Q16SLRV:
        gen_mxu_q16sxxv(ctx, true, false);
        break;
    case OPC_MXU_Q16SARV:
        gen_mxu_q16sxxv(ctx, true, true);
        break;
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void decode_opc_mxu__pool19(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 22, 4);

    switch (opcode) {
    case OPC_MXU_Q8MUL:
        gen_mxu_q8mul_mac(ctx, false, false);
        break;
    case OPC_MXU_Q8MULSU:
        gen_mxu_q8mul_mac(ctx, true, false);
        break;
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void decode_opc_mxu__pool20(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 18, 3);

    switch (opcode) {
    case OPC_MXU_Q8MOVZ:
        gen_mxu_q8movzn(ctx, TCG_COND_NE);
        break;
    case OPC_MXU_Q8MOVN:
        gen_mxu_q8movzn(ctx, TCG_COND_EQ);
        break;
    case OPC_MXU_D16MOVZ:
        gen_mxu_d16movzn(ctx, TCG_COND_NE);
        break;
    case OPC_MXU_D16MOVN:
        gen_mxu_d16movzn(ctx, TCG_COND_EQ);
        break;
    case OPC_MXU_S32MOVZ:
        gen_mxu_s32movzn(ctx, TCG_COND_NE);
        break;
    case OPC_MXU_S32MOVN:
        gen_mxu_s32movzn(ctx, TCG_COND_EQ);
        break;
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void decode_opc_mxu__pool21(DisasContext *ctx)
{
    uint32_t opcode = extract32(ctx->opcode, 22, 2);

    switch (opcode) {
    case OPC_MXU_Q8MAC:
        gen_mxu_q8mul_mac(ctx, false, true);
        break;
    case OPC_MXU_Q8MACSU:
        gen_mxu_q8mul_mac(ctx, true, true);
        break;
    default:
        MIPS_INVAL("decode_opc_mxu");
        gen_reserved_instruction(ctx);
        break;
    }
}


bool decode_ase_mxu(DisasContext *ctx, uint32_t insn)
{
    uint32_t opcode = extract32(insn, 0, 6);

    if (opcode == OPC_MXU_S32M2I) {
        gen_mxu_s32m2i(ctx);
        return true;
    }

    if (opcode == OPC_MXU_S32I2M) {
        gen_mxu_s32i2m(ctx);
        return true;
    }

    {
        TCGv t_mxu_cr = tcg_temp_new();
        TCGLabel *l_exit = gen_new_label();

        gen_load_mxu_cr(t_mxu_cr);
        tcg_gen_andi_tl(t_mxu_cr, t_mxu_cr, MXU_CR_MXU_EN);
        tcg_gen_brcondi_tl(TCG_COND_NE, t_mxu_cr, MXU_CR_MXU_EN, l_exit);

        switch (opcode) {
        case OPC_MXU_S32MADD:
        case OPC_MXU_S32MADDU:
        case OPC_MXU_S32MSUB:
        case OPC_MXU_S32MSUBU:
            return decode_opc_mxu_s32madd_sub(ctx);
        case OPC_MXU__POOL00:
            decode_opc_mxu__pool00(ctx);
            break;
        case OPC_MXU_D16MUL:
            gen_mxu_d16mul(ctx, false, false);
            break;
        case OPC_MXU_D16MAC:
            gen_mxu_d16mac(ctx, false, false);
            break;
        case OPC_MXU_D16MACF:
            gen_mxu_d16mac(ctx, true, true);
            break;
        case OPC_MXU_D16MADL:
            gen_mxu_d16madl(ctx);
            break;
        case OPC_MXU_S16MAD:
            gen_mxu_s16mad(ctx);
            break;
        case OPC_MXU_Q16ADD:
            gen_mxu_q16add(ctx);
            break;
        case OPC_MXU_D16MACE:
            gen_mxu_d16mac(ctx, true, false);
            break;
        case OPC_MXU__POOL01:
            decode_opc_mxu__pool01(ctx);
            break;
        case OPC_MXU__POOL02:
            decode_opc_mxu__pool02(ctx);
            break;
        case OPC_MXU__POOL03:
            decode_opc_mxu__pool03(ctx);
            break;
        case OPC_MXU__POOL04:
            decode_opc_mxu__pool04(ctx);
            break;
        case OPC_MXU__POOL05:
            decode_opc_mxu__pool05(ctx);
            break;
        case OPC_MXU__POOL06:
            decode_opc_mxu__pool06(ctx);
            break;
        case OPC_MXU__POOL07:
            decode_opc_mxu__pool07(ctx);
            break;
        case OPC_MXU__POOL08:
            decode_opc_mxu__pool08(ctx);
            break;
        case OPC_MXU__POOL09:
            decode_opc_mxu__pool09(ctx);
            break;
        case OPC_MXU__POOL10:
            decode_opc_mxu__pool10(ctx);
            break;
        case OPC_MXU__POOL11:
            decode_opc_mxu__pool11(ctx);
            break;
        case OPC_MXU_D32ADD:
            gen_mxu_d32add(ctx);
            break;
        case OPC_MXU__POOL12:
            decode_opc_mxu__pool12(ctx);
            break;
        case OPC_MXU__POOL13:
            decode_opc_mxu__pool13(ctx);
            break;
        case OPC_MXU__POOL14:
            decode_opc_mxu__pool14(ctx);
            break;
        case OPC_MXU_Q8ACCE:
            gen_mxu_q8adde(ctx, true);
            break;
        case OPC_MXU_S8LDD:
            gen_mxu_s8ldd(ctx, false);
            break;
        case OPC_MXU_S8STD:
            gen_mxu_s8std(ctx, false);
            break;
        case OPC_MXU_S8LDI:
            gen_mxu_s8ldd(ctx, true);
            break;
        case OPC_MXU_S8SDI:
            gen_mxu_s8std(ctx, true);
            break;
        case OPC_MXU__POOL15:
            decode_opc_mxu__pool15(ctx);
            break;
        case OPC_MXU__POOL16:
            decode_opc_mxu__pool16(ctx);
            break;
        case OPC_MXU__POOL17:
            decode_opc_mxu__pool17(ctx);
            break;
        case OPC_MXU_S16LDD:
            gen_mxu_s16ldd(ctx, false);
            break;
        case OPC_MXU_S16STD:
            gen_mxu_s16std(ctx, false);
            break;
        case OPC_MXU_S16LDI:
            gen_mxu_s16ldd(ctx, true);
            break;
        case OPC_MXU_S16SDI:
            gen_mxu_s16std(ctx, true);
            break;
        case OPC_MXU_D32SLL:
            gen_mxu_d32sxx(ctx, false, false);
            break;
        case OPC_MXU_D32SLR:
            gen_mxu_d32sxx(ctx, true, false);
            break;
        case OPC_MXU_D32SARL:
            gen_mxu_d32sarl(ctx, false);
            break;
        case OPC_MXU_D32SAR:
            gen_mxu_d32sxx(ctx, true, true);
            break;
        case OPC_MXU_Q16SLL:
            gen_mxu_q16sxx(ctx, false, false);
            break;
        case OPC_MXU__POOL18:
            decode_opc_mxu__pool18(ctx);
            break;
        case OPC_MXU_Q16SLR:
            gen_mxu_q16sxx(ctx, true, false);
            break;
        case OPC_MXU_Q16SAR:
            gen_mxu_q16sxx(ctx, true, true);
            break;
        case OPC_MXU__POOL19:
            decode_opc_mxu__pool19(ctx);
            break;
        case OPC_MXU__POOL20:
            decode_opc_mxu__pool20(ctx);
            break;
        case OPC_MXU__POOL21:
            decode_opc_mxu__pool21(ctx);
            break;
        case OPC_MXU_Q16SCOP:
            gen_mxu_q16scop(ctx);
            break;
        case OPC_MXU_Q8MADL:
            gen_mxu_q8madl(ctx);
            break;
        case OPC_MXU_S32SFL:
            gen_mxu_s32sfl(ctx);
            break;
        case OPC_MXU_Q8SAD:
            gen_mxu_q8sad(ctx);
            break;
        default:
            return false;
        }

        gen_set_label(l_exit);
    }

    return true;
}
