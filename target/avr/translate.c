/*
 * QEMU AVR CPU
 *
 * Copyright (c) 2019-2020 Michael Rolnik
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "tcg/tcg.h"
#include "cpu.h"
#include "exec/translation-block.h"
#include "tcg/tcg-op.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/log.h"
#include "exec/translator.h"
#include "exec/target_page.h"

#define HELPER_H "helper.h"
#include "exec/helper-info.c.inc"
#undef  HELPER_H


#undef BREAKPOINT_ON_BREAK

static TCGv cpu_pc;

static TCGv cpu_Cf;
static TCGv cpu_Zf;
static TCGv cpu_Nf;
static TCGv cpu_Vf;
static TCGv cpu_Sf;
static TCGv cpu_Hf;
static TCGv cpu_Tf;
static TCGv cpu_If;

static TCGv cpu_rampD;
static TCGv cpu_rampX;
static TCGv cpu_rampY;
static TCGv cpu_rampZ;

static TCGv cpu_r[NUMBER_OF_CPU_REGISTERS];
static TCGv cpu_eind;
static TCGv cpu_sp;

static TCGv cpu_skip;

static const char reg_names[NUMBER_OF_CPU_REGISTERS][8] = {
    "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
    "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15",
    "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
    "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",
};
#define REG(x) (cpu_r[x])

#define DISAS_EXIT   DISAS_TARGET_0
#define DISAS_LOOKUP DISAS_TARGET_1
#define DISAS_CHAIN  DISAS_TARGET_2

typedef struct DisasContext DisasContext;

struct DisasContext {
    DisasContextBase base;

    CPUAVRState *env;
    CPUState *cs;

    target_long npc;
    uint32_t opcode;

    int memidx;

    TCGv skip_var0;
    TCGv skip_var1;
    TCGCond skip_cond;
};

void avr_cpu_tcg_init(void)
{
    int i;

#define AVR_REG_OFFS(x) offsetof(CPUAVRState, x)
    cpu_pc = tcg_global_mem_new_i32(tcg_env, AVR_REG_OFFS(pc_w), "pc");
    cpu_Cf = tcg_global_mem_new_i32(tcg_env, AVR_REG_OFFS(sregC), "Cf");
    cpu_Zf = tcg_global_mem_new_i32(tcg_env, AVR_REG_OFFS(sregZ), "Zf");
    cpu_Nf = tcg_global_mem_new_i32(tcg_env, AVR_REG_OFFS(sregN), "Nf");
    cpu_Vf = tcg_global_mem_new_i32(tcg_env, AVR_REG_OFFS(sregV), "Vf");
    cpu_Sf = tcg_global_mem_new_i32(tcg_env, AVR_REG_OFFS(sregS), "Sf");
    cpu_Hf = tcg_global_mem_new_i32(tcg_env, AVR_REG_OFFS(sregH), "Hf");
    cpu_Tf = tcg_global_mem_new_i32(tcg_env, AVR_REG_OFFS(sregT), "Tf");
    cpu_If = tcg_global_mem_new_i32(tcg_env, AVR_REG_OFFS(sregI), "If");
    cpu_rampD = tcg_global_mem_new_i32(tcg_env, AVR_REG_OFFS(rampD), "rampD");
    cpu_rampX = tcg_global_mem_new_i32(tcg_env, AVR_REG_OFFS(rampX), "rampX");
    cpu_rampY = tcg_global_mem_new_i32(tcg_env, AVR_REG_OFFS(rampY), "rampY");
    cpu_rampZ = tcg_global_mem_new_i32(tcg_env, AVR_REG_OFFS(rampZ), "rampZ");
    cpu_eind = tcg_global_mem_new_i32(tcg_env, AVR_REG_OFFS(eind), "eind");
    cpu_sp = tcg_global_mem_new_i32(tcg_env, AVR_REG_OFFS(sp), "sp");
    cpu_skip = tcg_global_mem_new_i32(tcg_env, AVR_REG_OFFS(skip), "skip");

    for (i = 0; i < NUMBER_OF_CPU_REGISTERS; i++) {
        cpu_r[i] = tcg_global_mem_new_i32(tcg_env, AVR_REG_OFFS(r[i]),
                                          reg_names[i]);
    }
#undef AVR_REG_OFFS
}

static int to_regs_16_31_by_one(DisasContext *ctx, int indx)
{
    return 16 + (indx % 16);
}

static int to_regs_16_23_by_one(DisasContext *ctx, int indx)
{
    return 16 + (indx % 8);
}

static int to_regs_24_30_by_two(DisasContext *ctx, int indx)
{
    return 24 + (indx % 4) * 2;
}

static int to_regs_00_30_by_two(DisasContext *ctx, int indx)
{
    return (indx % 16) * 2;
}

static uint16_t next_word(DisasContext *ctx)
{
    return translator_lduw(ctx->env, &ctx->base, ctx->npc++ * 2);
}

static int append_16(DisasContext *ctx, int x)
{
    return x << 16 | next_word(ctx);
}

static bool avr_have_feature(DisasContext *ctx, int feature)
{
    if (!avr_feature(ctx->env, feature)) {
        gen_helper_unsupported(tcg_env);
        ctx->base.is_jmp = DISAS_NORETURN;
        return false;
    }
    return true;
}

static bool decode_insn(DisasContext *ctx, uint16_t insn);
#include "decode-insn.c.inc"

static void gen_inb(DisasContext *ctx, TCGv data, int port);
static void gen_outb(DisasContext *ctx, TCGv data, int port);

static void gen_add_CHf(TCGv R, TCGv Rd, TCGv Rr)
{
    TCGv t1 = tcg_temp_new_i32();
    TCGv t2 = tcg_temp_new_i32();
    TCGv t3 = tcg_temp_new_i32();

    tcg_gen_and_tl(t1, Rd, Rr);
    tcg_gen_andc_tl(t2, Rd, R);
    tcg_gen_andc_tl(t3, Rr, R);
    tcg_gen_or_tl(t1, t1, t2);
    tcg_gen_or_tl(t1, t1, t3);

    tcg_gen_shri_tl(cpu_Cf, t1, 7);
    tcg_gen_shri_tl(cpu_Hf, t1, 3);
    tcg_gen_andi_tl(cpu_Hf, cpu_Hf, 1);
}

static void gen_add_Vf(TCGv R, TCGv Rd, TCGv Rr)
{
    TCGv t1 = tcg_temp_new_i32();
    TCGv t2 = tcg_temp_new_i32();

    tcg_gen_xor_tl(t1, Rd, R);
    tcg_gen_xor_tl(t2, Rd, Rr);
    tcg_gen_andc_tl(t1, t1, t2);

    tcg_gen_shri_tl(cpu_Vf, t1, 7);
}

static void gen_sub_CHf(TCGv R, TCGv Rd, TCGv Rr)
{
    TCGv t1 = tcg_temp_new_i32();
    TCGv t2 = tcg_temp_new_i32();
    TCGv t3 = tcg_temp_new_i32();

    tcg_gen_not_tl(t1, Rd);
    tcg_gen_and_tl(t2, t1, Rr);
    tcg_gen_or_tl(t3, t1, Rr);
    tcg_gen_and_tl(t3, t3, R);
    tcg_gen_or_tl(t2, t2, t3);

    tcg_gen_shri_tl(cpu_Cf, t2, 7);
    tcg_gen_shri_tl(cpu_Hf, t2, 3);
    tcg_gen_andi_tl(cpu_Hf, cpu_Hf, 1);
}

static void gen_sub_Vf(TCGv R, TCGv Rd, TCGv Rr)
{
    TCGv t1 = tcg_temp_new_i32();
    TCGv t2 = tcg_temp_new_i32();

    tcg_gen_xor_tl(t1, Rd, R);
    tcg_gen_xor_tl(t2, Rd, Rr);
    tcg_gen_and_tl(t1, t1, t2);

    tcg_gen_shri_tl(cpu_Vf, t1, 7);
}

static void gen_NSf(TCGv R)
{
    tcg_gen_shri_tl(cpu_Nf, R, 7);
    tcg_gen_xor_tl(cpu_Sf, cpu_Nf, cpu_Vf);
}

static void gen_ZNSf(TCGv R)
{
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0);

    tcg_gen_shri_tl(cpu_Nf, R, 7);
    tcg_gen_xor_tl(cpu_Sf, cpu_Nf, cpu_Vf);
}

static bool trans_ADD(DisasContext *ctx, arg_ADD *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_add_tl(R, Rd, Rr);
    tcg_gen_andi_tl(R, R, 0xff);

    gen_add_CHf(R, Rd, Rr);
    gen_add_Vf(R, Rd, Rr);
    gen_ZNSf(R);

    tcg_gen_mov_tl(Rd, R);
    return true;
}

static bool trans_ADC(DisasContext *ctx, arg_ADC *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_add_tl(R, Rd, Rr);
    tcg_gen_add_tl(R, R, cpu_Cf);
    tcg_gen_andi_tl(R, R, 0xff);

    gen_add_CHf(R, Rd, Rr);
    gen_add_Vf(R, Rd, Rr);
    gen_ZNSf(R);

    tcg_gen_mov_tl(Rd, R);
    return true;
}

static bool trans_ADIW(DisasContext *ctx, arg_ADIW *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_ADIW_SBIW)) {
        return true;
    }

    TCGv RdL = cpu_r[a->rd];
    TCGv RdH = cpu_r[a->rd + 1];
    int Imm = (a->imm);
    TCGv R = tcg_temp_new_i32();
    TCGv Rd = tcg_temp_new_i32();

    tcg_gen_deposit_tl(Rd, RdL, RdH, 8, 8);
    tcg_gen_addi_tl(R, Rd, Imm);
    tcg_gen_andi_tl(R, R, 0xffff);

    tcg_gen_andc_tl(cpu_Cf, Rd, R);
    tcg_gen_shri_tl(cpu_Cf, cpu_Cf, 15);
    tcg_gen_andc_tl(cpu_Vf, R, Rd);
    tcg_gen_shri_tl(cpu_Vf, cpu_Vf, 15);
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0);
    tcg_gen_shri_tl(cpu_Nf, R, 15);
    tcg_gen_xor_tl(cpu_Sf, cpu_Nf, cpu_Vf);

    tcg_gen_andi_tl(RdL, R, 0xff);
    tcg_gen_shri_tl(RdH, R, 8);
    return true;
}

static bool trans_SUB(DisasContext *ctx, arg_SUB *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_sub_tl(R, Rd, Rr);
    tcg_gen_andi_tl(R, R, 0xff);

    tcg_gen_andc_tl(cpu_Cf, Rd, R);
    gen_sub_CHf(R, Rd, Rr);
    gen_sub_Vf(R, Rd, Rr);
    gen_ZNSf(R);

    tcg_gen_mov_tl(Rd, R);
    return true;
}

static bool trans_SUBI(DisasContext *ctx, arg_SUBI *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = tcg_constant_i32(a->imm);
    TCGv R = tcg_temp_new_i32();

    tcg_gen_sub_tl(R, Rd, Rr);
    tcg_gen_andi_tl(R, R, 0xff);

    gen_sub_CHf(R, Rd, Rr);
    gen_sub_Vf(R, Rd, Rr);
    gen_ZNSf(R);

    tcg_gen_mov_tl(Rd, R);
    return true;
}

static bool trans_SBC(DisasContext *ctx, arg_SBC *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();
    TCGv zero = tcg_constant_i32(0);

    tcg_gen_sub_tl(R, Rd, Rr);
    tcg_gen_sub_tl(R, R, cpu_Cf);
    tcg_gen_andi_tl(R, R, 0xff);

    gen_sub_CHf(R, Rd, Rr);
    gen_sub_Vf(R, Rd, Rr);
    gen_NSf(R);

    tcg_gen_movcond_tl(TCG_COND_EQ, cpu_Zf, R, zero, cpu_Zf, zero);

    tcg_gen_mov_tl(Rd, R);
    return true;
}

static bool trans_SBCI(DisasContext *ctx, arg_SBCI *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = tcg_constant_i32(a->imm);
    TCGv R = tcg_temp_new_i32();
    TCGv zero = tcg_constant_i32(0);

    tcg_gen_sub_tl(R, Rd, Rr);
    tcg_gen_sub_tl(R, R, cpu_Cf);
    tcg_gen_andi_tl(R, R, 0xff);

    gen_sub_CHf(R, Rd, Rr);
    gen_sub_Vf(R, Rd, Rr);
    gen_NSf(R);

    tcg_gen_movcond_tl(TCG_COND_EQ, cpu_Zf, R, zero, cpu_Zf, zero);

    tcg_gen_mov_tl(Rd, R);
    return true;
}

static bool trans_SBIW(DisasContext *ctx, arg_SBIW *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_ADIW_SBIW)) {
        return true;
    }

    TCGv RdL = cpu_r[a->rd];
    TCGv RdH = cpu_r[a->rd + 1];
    int Imm = (a->imm);
    TCGv R = tcg_temp_new_i32();
    TCGv Rd = tcg_temp_new_i32();

    tcg_gen_deposit_tl(Rd, RdL, RdH, 8, 8);
    tcg_gen_subi_tl(R, Rd, Imm);
    tcg_gen_andi_tl(R, R, 0xffff);

    tcg_gen_andc_tl(cpu_Cf, R, Rd);
    tcg_gen_shri_tl(cpu_Cf, cpu_Cf, 15);
    tcg_gen_andc_tl(cpu_Vf, Rd, R);
    tcg_gen_shri_tl(cpu_Vf, cpu_Vf, 15);
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0);
    tcg_gen_shri_tl(cpu_Nf, R, 15);
    tcg_gen_xor_tl(cpu_Sf, cpu_Nf, cpu_Vf);

    tcg_gen_andi_tl(RdL, R, 0xff);
    tcg_gen_shri_tl(RdH, R, 8);
    return true;
}

static bool trans_AND(DisasContext *ctx, arg_AND *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_and_tl(R, Rd, Rr);

    tcg_gen_movi_tl(cpu_Vf, 0);
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0);
    gen_ZNSf(R);

    tcg_gen_mov_tl(Rd, R);
    return true;
}

static bool trans_ANDI(DisasContext *ctx, arg_ANDI *a)
{
    TCGv Rd = cpu_r[a->rd];
    int Imm = (a->imm);

    tcg_gen_andi_tl(Rd, Rd, Imm);

    tcg_gen_movi_tl(cpu_Vf, 0x00);
    gen_ZNSf(Rd);

    return true;
}

static bool trans_OR(DisasContext *ctx, arg_OR *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_or_tl(R, Rd, Rr);

    tcg_gen_movi_tl(cpu_Vf, 0);
    gen_ZNSf(R);

    tcg_gen_mov_tl(Rd, R);
    return true;
}

static bool trans_ORI(DisasContext *ctx, arg_ORI *a)
{
    TCGv Rd = cpu_r[a->rd];
    int Imm = (a->imm);

    tcg_gen_ori_tl(Rd, Rd, Imm);

    tcg_gen_movi_tl(cpu_Vf, 0x00);
    gen_ZNSf(Rd);

    return true;
}

static bool trans_EOR(DisasContext *ctx, arg_EOR *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];

    tcg_gen_xor_tl(Rd, Rd, Rr);

    tcg_gen_movi_tl(cpu_Vf, 0);
    gen_ZNSf(Rd);

    return true;
}

static bool trans_COM(DisasContext *ctx, arg_COM *a)
{
    TCGv Rd = cpu_r[a->rd];

    tcg_gen_xori_tl(Rd, Rd, 0xff);

    tcg_gen_movi_tl(cpu_Cf, 1);
    tcg_gen_movi_tl(cpu_Vf, 0);
    gen_ZNSf(Rd);
    return true;
}

static bool trans_NEG(DisasContext *ctx, arg_NEG *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv t0 = tcg_constant_i32(0);
    TCGv R = tcg_temp_new_i32();

    tcg_gen_sub_tl(R, t0, Rd);
    tcg_gen_andi_tl(R, R, 0xff);

    gen_sub_CHf(R, t0, Rd);
    gen_sub_Vf(R, t0, Rd);
    gen_ZNSf(R);

    tcg_gen_mov_tl(Rd, R);
    return true;
}

static bool trans_INC(DisasContext *ctx, arg_INC *a)
{
    TCGv Rd = cpu_r[a->rd];

    tcg_gen_addi_tl(Rd, Rd, 1);
    tcg_gen_andi_tl(Rd, Rd, 0xff);

    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Vf, Rd, 0x80);
    gen_ZNSf(Rd);

    return true;
}

static bool trans_DEC(DisasContext *ctx, arg_DEC *a)
{
    TCGv Rd = cpu_r[a->rd];

    tcg_gen_subi_tl(Rd, Rd, 1);
    tcg_gen_andi_tl(Rd, Rd, 0xff);

    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Vf, Rd, 0x7f);
    gen_ZNSf(Rd);

    return true;
}

static bool trans_MUL(DisasContext *ctx, arg_MUL *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_MUL)) {
        return true;
    }

    TCGv R0 = cpu_r[0];
    TCGv R1 = cpu_r[1];
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_mul_tl(R, Rd, Rr);
    tcg_gen_andi_tl(R0, R, 0xff);
    tcg_gen_shri_tl(R1, R, 8);

    tcg_gen_shri_tl(cpu_Cf, R, 15);
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0);
    return true;
}

static bool trans_MULS(DisasContext *ctx, arg_MULS *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_MUL)) {
        return true;
    }

    TCGv R0 = cpu_r[0];
    TCGv R1 = cpu_r[1];
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();
    TCGv t0 = tcg_temp_new_i32();
    TCGv t1 = tcg_temp_new_i32();

    tcg_gen_ext8s_tl(t0, Rd);
    tcg_gen_ext8s_tl(t1, Rr);
    tcg_gen_mul_tl(R, t0, t1);
    tcg_gen_andi_tl(R, R, 0xffff);
    tcg_gen_andi_tl(R0, R, 0xff);
    tcg_gen_shri_tl(R1, R, 8);

    tcg_gen_shri_tl(cpu_Cf, R, 15);
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0);
    return true;
}

static bool trans_MULSU(DisasContext *ctx, arg_MULSU *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_MUL)) {
        return true;
    }

    TCGv R0 = cpu_r[0];
    TCGv R1 = cpu_r[1];
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();
    TCGv t0 = tcg_temp_new_i32();

    tcg_gen_ext8s_tl(t0, Rd);
    tcg_gen_mul_tl(R, t0, Rr);
    tcg_gen_andi_tl(R, R, 0xffff);
    tcg_gen_andi_tl(R0, R, 0xff);
    tcg_gen_shri_tl(R1, R, 8);

    tcg_gen_shri_tl(cpu_Cf, R, 15);
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0);
    return true;
}

static bool trans_FMUL(DisasContext *ctx, arg_FMUL *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_MUL)) {
        return true;
    }

    TCGv R0 = cpu_r[0];
    TCGv R1 = cpu_r[1];
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_mul_tl(R, Rd, Rr);

    tcg_gen_shri_tl(cpu_Cf, R, 15);
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0);

    tcg_gen_shli_tl(R, R, 1);
    tcg_gen_andi_tl(R0, R, 0xff);
    tcg_gen_shri_tl(R1, R, 8);
    tcg_gen_andi_tl(R1, R1, 0xff);
    return true;
}

static bool trans_FMULS(DisasContext *ctx, arg_FMULS *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_MUL)) {
        return true;
    }

    TCGv R0 = cpu_r[0];
    TCGv R1 = cpu_r[1];
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();
    TCGv t0 = tcg_temp_new_i32();
    TCGv t1 = tcg_temp_new_i32();

    tcg_gen_ext8s_tl(t0, Rd);
    tcg_gen_ext8s_tl(t1, Rr);
    tcg_gen_mul_tl(R, t0, t1);
    tcg_gen_andi_tl(R, R, 0xffff);

    tcg_gen_shri_tl(cpu_Cf, R, 15);
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0);

    tcg_gen_shli_tl(R, R, 1);
    tcg_gen_andi_tl(R0, R, 0xff);
    tcg_gen_shri_tl(R1, R, 8);
    tcg_gen_andi_tl(R1, R1, 0xff);
    return true;
}

static bool trans_FMULSU(DisasContext *ctx, arg_FMULSU *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_MUL)) {
        return true;
    }

    TCGv R0 = cpu_r[0];
    TCGv R1 = cpu_r[1];
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();
    TCGv t0 = tcg_temp_new_i32();

    tcg_gen_ext8s_tl(t0, Rd);
    tcg_gen_mul_tl(R, t0, Rr);
    tcg_gen_andi_tl(R, R, 0xffff);

    tcg_gen_shri_tl(cpu_Cf, R, 15);
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0);

    tcg_gen_shli_tl(R, R, 1);
    tcg_gen_andi_tl(R0, R, 0xff);
    tcg_gen_shri_tl(R1, R, 8);
    tcg_gen_andi_tl(R1, R1, 0xff);
    return true;
}

static bool trans_DES(DisasContext *ctx, arg_DES *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_DES)) {
        return true;
    }

    qemu_log_mask(LOG_UNIMP, "%s: not implemented\n", __func__);

    return true;
}

static void gen_jmp_ez(DisasContext *ctx)
{
    tcg_gen_deposit_tl(cpu_pc, cpu_r[30], cpu_r[31], 8, 8);
    tcg_gen_or_tl(cpu_pc, cpu_pc, cpu_eind);
    ctx->base.is_jmp = DISAS_LOOKUP;
}

static void gen_jmp_z(DisasContext *ctx)
{
    tcg_gen_deposit_tl(cpu_pc, cpu_r[30], cpu_r[31], 8, 8);
    ctx->base.is_jmp = DISAS_LOOKUP;
}

static void gen_push_ret(DisasContext *ctx, int ret)
{
    if (avr_feature(ctx->env, AVR_FEATURE_1_BYTE_PC)) {
        TCGv t0 = tcg_constant_i32(ret & 0x0000ff);

        tcg_gen_qemu_st_tl(t0, cpu_sp, MMU_DATA_IDX, MO_UB);
        tcg_gen_subi_tl(cpu_sp, cpu_sp, 1);
    } else if (avr_feature(ctx->env, AVR_FEATURE_2_BYTE_PC)) {
        TCGv t0 = tcg_constant_i32(ret & 0x00ffff);

        tcg_gen_subi_tl(cpu_sp, cpu_sp, 1);
        tcg_gen_qemu_st_tl(t0, cpu_sp, MMU_DATA_IDX, MO_BEUW);
        tcg_gen_subi_tl(cpu_sp, cpu_sp, 1);
    } else if (avr_feature(ctx->env, AVR_FEATURE_3_BYTE_PC)) {
        TCGv lo = tcg_constant_i32(ret & 0x0000ff);
        TCGv hi = tcg_constant_i32((ret & 0xffff00) >> 8);

        tcg_gen_qemu_st_tl(lo, cpu_sp, MMU_DATA_IDX, MO_UB);
        tcg_gen_subi_tl(cpu_sp, cpu_sp, 2);
        tcg_gen_qemu_st_tl(hi, cpu_sp, MMU_DATA_IDX, MO_BEUW);
        tcg_gen_subi_tl(cpu_sp, cpu_sp, 1);
    }
}

static void gen_pop_ret(DisasContext *ctx, TCGv ret)
{
    if (avr_feature(ctx->env, AVR_FEATURE_1_BYTE_PC)) {
        tcg_gen_addi_tl(cpu_sp, cpu_sp, 1);
        tcg_gen_qemu_ld_tl(ret, cpu_sp, MMU_DATA_IDX, MO_UB);
    } else if (avr_feature(ctx->env, AVR_FEATURE_2_BYTE_PC)) {
        tcg_gen_addi_tl(cpu_sp, cpu_sp, 1);
        tcg_gen_qemu_ld_tl(ret, cpu_sp, MMU_DATA_IDX, MO_BEUW);
        tcg_gen_addi_tl(cpu_sp, cpu_sp, 1);
    } else if (avr_feature(ctx->env, AVR_FEATURE_3_BYTE_PC)) {
        TCGv lo = tcg_temp_new_i32();
        TCGv hi = tcg_temp_new_i32();

        tcg_gen_addi_tl(cpu_sp, cpu_sp, 1);
        tcg_gen_qemu_ld_tl(hi, cpu_sp, MMU_DATA_IDX, MO_BEUW);

        tcg_gen_addi_tl(cpu_sp, cpu_sp, 2);
        tcg_gen_qemu_ld_tl(lo, cpu_sp, MMU_DATA_IDX, MO_UB);

        tcg_gen_deposit_tl(ret, lo, hi, 8, 16);
    }
}

static void gen_goto_tb(DisasContext *ctx, unsigned tb_slot_idx,
                        target_ulong dest)
{
    const TranslationBlock *tb = ctx->base.tb;

    if (translator_use_goto_tb(&ctx->base, dest)) {
        tcg_gen_goto_tb(tb_slot_idx);
        tcg_gen_movi_i32(cpu_pc, dest);
        tcg_gen_exit_tb(tb, tb_slot_idx);
    } else {
        tcg_gen_movi_i32(cpu_pc, dest);
        tcg_gen_lookup_and_goto_ptr();
    }
    ctx->base.is_jmp = DISAS_NORETURN;
}

static bool trans_RJMP(DisasContext *ctx, arg_RJMP *a)
{
    int dst = ctx->npc + a->imm;

    gen_goto_tb(ctx, 0, dst);

    return true;
}

static bool trans_IJMP(DisasContext *ctx, arg_IJMP *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_IJMP_ICALL)) {
        return true;
    }

    gen_jmp_z(ctx);

    return true;
}

static bool trans_EIJMP(DisasContext *ctx, arg_EIJMP *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_EIJMP_EICALL)) {
        return true;
    }

    gen_jmp_ez(ctx);
    return true;
}

static bool trans_JMP(DisasContext *ctx, arg_JMP *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_JMP_CALL)) {
        return true;
    }

    gen_goto_tb(ctx, 0, a->imm);

    return true;
}

static bool trans_RCALL(DisasContext *ctx, arg_RCALL *a)
{
    int ret = ctx->npc;
    int dst = ctx->npc + a->imm;

    gen_push_ret(ctx, ret);
    gen_goto_tb(ctx, 0, dst);

    return true;
}

static bool trans_ICALL(DisasContext *ctx, arg_ICALL *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_IJMP_ICALL)) {
        return true;
    }

    int ret = ctx->npc;

    gen_push_ret(ctx, ret);
    gen_jmp_z(ctx);

    return true;
}

static bool trans_EICALL(DisasContext *ctx, arg_EICALL *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_EIJMP_EICALL)) {
        return true;
    }

    int ret = ctx->npc;

    gen_push_ret(ctx, ret);
    gen_jmp_ez(ctx);
    return true;
}

static bool trans_CALL(DisasContext *ctx, arg_CALL *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_JMP_CALL)) {
        return true;
    }

    int Imm = a->imm;
    int ret = ctx->npc;

    gen_push_ret(ctx, ret);
    gen_goto_tb(ctx, 0, Imm);

    return true;
}

static bool trans_RET(DisasContext *ctx, arg_RET *a)
{
    gen_pop_ret(ctx, cpu_pc);

    ctx->base.is_jmp = DISAS_LOOKUP;
    return true;
}

static bool trans_RETI(DisasContext *ctx, arg_RETI *a)
{
    gen_pop_ret(ctx, cpu_pc);
    tcg_gen_movi_tl(cpu_If, 1);

    ctx->base.is_jmp = DISAS_EXIT;
    return true;
}

static bool trans_CPSE(DisasContext *ctx, arg_CPSE *a)
{
    ctx->skip_cond = TCG_COND_EQ;
    ctx->skip_var0 = cpu_r[a->rd];
    ctx->skip_var1 = cpu_r[a->rr];
    return true;
}

static bool trans_CP(DisasContext *ctx, arg_CP *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_sub_tl(R, Rd, Rr);
    tcg_gen_andi_tl(R, R, 0xff);

    gen_sub_CHf(R, Rd, Rr);
    gen_sub_Vf(R, Rd, Rr);
    gen_ZNSf(R);
    return true;
}

static bool trans_CPC(DisasContext *ctx, arg_CPC *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();
    TCGv zero = tcg_constant_i32(0);

    tcg_gen_sub_tl(R, Rd, Rr);
    tcg_gen_sub_tl(R, R, cpu_Cf);
    tcg_gen_andi_tl(R, R, 0xff);
    gen_sub_CHf(R, Rd, Rr);
    gen_sub_Vf(R, Rd, Rr);
    gen_NSf(R);

    tcg_gen_movcond_tl(TCG_COND_EQ, cpu_Zf, R, zero, cpu_Zf, zero);
    return true;
}

static bool trans_CPI(DisasContext *ctx, arg_CPI *a)
{
    TCGv Rd = cpu_r[a->rd];
    int Imm = a->imm;
    TCGv Rr = tcg_constant_i32(Imm);
    TCGv R = tcg_temp_new_i32();

    tcg_gen_sub_tl(R, Rd, Rr);
    tcg_gen_andi_tl(R, R, 0xff);

    gen_sub_CHf(R, Rd, Rr);
    gen_sub_Vf(R, Rd, Rr);
    gen_ZNSf(R);
    return true;
}

static bool trans_SBRC(DisasContext *ctx, arg_SBRC *a)
{
    TCGv Rr = cpu_r[a->rr];

    ctx->skip_cond = TCG_COND_EQ;
    ctx->skip_var0 = tcg_temp_new();

    tcg_gen_andi_tl(ctx->skip_var0, Rr, 1 << a->bit);
    return true;
}

static bool trans_SBRS(DisasContext *ctx, arg_SBRS *a)
{
    TCGv Rr = cpu_r[a->rr];

    ctx->skip_cond = TCG_COND_NE;
    ctx->skip_var0 = tcg_temp_new();

    tcg_gen_andi_tl(ctx->skip_var0, Rr, 1 << a->bit);
    return true;
}

static bool trans_SBIC(DisasContext *ctx, arg_SBIC *a)
{
    TCGv data = tcg_temp_new_i32();

    gen_inb(ctx, data, a->reg);
    tcg_gen_andi_tl(data, data, 1 << a->bit);
    ctx->skip_cond = TCG_COND_EQ;
    ctx->skip_var0 = data;

    return true;
}

static bool trans_SBIS(DisasContext *ctx, arg_SBIS *a)
{
    TCGv data = tcg_temp_new_i32();

    gen_inb(ctx, data, a->reg);
    tcg_gen_andi_tl(data, data, 1 << a->bit);
    ctx->skip_cond = TCG_COND_NE;
    ctx->skip_var0 = data;

    return true;
}

static bool trans_BRBC(DisasContext *ctx, arg_BRBC *a)
{
    TCGLabel *not_taken = gen_new_label();

    TCGv var;

    switch (a->bit) {
    case 0x00:
        var = cpu_Cf;
        break;
    case 0x01:
        var = cpu_Zf;
        break;
    case 0x02:
        var = cpu_Nf;
        break;
    case 0x03:
        var = cpu_Vf;
        break;
    case 0x04:
        var = cpu_Sf;
        break;
    case 0x05:
        var = cpu_Hf;
        break;
    case 0x06:
        var = cpu_Tf;
        break;
    case 0x07:
        var = cpu_If;
        break;
    default:
        g_assert_not_reached();
    }

    tcg_gen_brcondi_i32(TCG_COND_NE, var, 0, not_taken);
    gen_goto_tb(ctx, 0, ctx->npc + a->imm);
    gen_set_label(not_taken);

    ctx->base.is_jmp = DISAS_CHAIN;
    return true;
}

static bool trans_BRBS(DisasContext *ctx, arg_BRBS *a)
{
    TCGLabel *not_taken = gen_new_label();

    TCGv var;

    switch (a->bit) {
    case 0x00:
        var = cpu_Cf;
        break;
    case 0x01:
        var = cpu_Zf;
        break;
    case 0x02:
        var = cpu_Nf;
        break;
    case 0x03:
        var = cpu_Vf;
        break;
    case 0x04:
        var = cpu_Sf;
        break;
    case 0x05:
        var = cpu_Hf;
        break;
    case 0x06:
        var = cpu_Tf;
        break;
    case 0x07:
        var = cpu_If;
        break;
    default:
        g_assert_not_reached();
    }

    tcg_gen_brcondi_i32(TCG_COND_EQ, var, 0, not_taken);
    gen_goto_tb(ctx, 0, ctx->npc + a->imm);
    gen_set_label(not_taken);

    ctx->base.is_jmp = DISAS_CHAIN;
    return true;
}

static void gen_set_addr(TCGv addr, TCGv H, TCGv M, TCGv L)
{

    tcg_gen_andi_tl(L, addr, 0x000000ff);

    tcg_gen_andi_tl(M, addr, 0x0000ff00);
    tcg_gen_shri_tl(M, M, 8);

    tcg_gen_andi_tl(H, addr, 0x00ff0000);
}

static void gen_set_xaddr(TCGv addr)
{
    gen_set_addr(addr, cpu_rampX, cpu_r[27], cpu_r[26]);
}

static void gen_set_yaddr(TCGv addr)
{
    gen_set_addr(addr, cpu_rampY, cpu_r[29], cpu_r[28]);
}

static void gen_set_zaddr(TCGv addr)
{
    gen_set_addr(addr, cpu_rampZ, cpu_r[31], cpu_r[30]);
}

static TCGv gen_get_addr(TCGv H, TCGv M, TCGv L)
{
    TCGv addr = tcg_temp_new_i32();

    tcg_gen_deposit_tl(addr, M, H, 8, 8);
    tcg_gen_deposit_tl(addr, L, addr, 8, 16);

    return addr;
}

static TCGv gen_get_xaddr(void)
{
    return gen_get_addr(cpu_rampX, cpu_r[27], cpu_r[26]);
}

static TCGv gen_get_yaddr(void)
{
    return gen_get_addr(cpu_rampY, cpu_r[29], cpu_r[28]);
}

static TCGv gen_get_zaddr(void)
{
    return gen_get_addr(cpu_rampZ, cpu_r[31], cpu_r[30]);
}

static void gen_data_store(DisasContext *ctx, TCGv data, TCGv addr)
{
    if (ctx->base.tb->flags & TB_FLAGS_FULL_ACCESS) {
        gen_helper_fullwr(tcg_env, data, addr);
    } else {
        tcg_gen_qemu_st_tl(data, addr, MMU_DATA_IDX, MO_UB);
    }
}

static void gen_data_load(DisasContext *ctx, TCGv data, TCGv addr)
{
    tcg_gen_qemu_ld_tl(data, addr, MMU_DATA_IDX, MO_UB);
}

static void gen_inb(DisasContext *ctx, TCGv data, int port)
{
    gen_data_load(ctx, data, tcg_constant_i32(port + NUMBER_OF_CPU_REGISTERS));
}

static void gen_outb(DisasContext *ctx, TCGv data, int port)
{
    gen_helper_fullwr(tcg_env, data,
                      tcg_constant_i32(port + NUMBER_OF_CPU_REGISTERS));
}

static bool trans_MOV(DisasContext *ctx, arg_MOV *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];

    tcg_gen_mov_tl(Rd, Rr);

    return true;
}

static bool trans_MOVW(DisasContext *ctx, arg_MOVW *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_MOVW)) {
        return true;
    }

    TCGv RdL = cpu_r[a->rd];
    TCGv RdH = cpu_r[a->rd + 1];
    TCGv RrL = cpu_r[a->rr];
    TCGv RrH = cpu_r[a->rr + 1];

    tcg_gen_mov_tl(RdH, RrH);
    tcg_gen_mov_tl(RdL, RrL);

    return true;
}

static bool trans_LDI(DisasContext *ctx, arg_LDI *a)
{
    TCGv Rd = cpu_r[a->rd];
    int imm = a->imm;

    tcg_gen_movi_tl(Rd, imm);

    return true;
}

static bool trans_LDS(DisasContext *ctx, arg_LDS *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = tcg_temp_new_i32();
    TCGv H = cpu_rampD;

    tcg_gen_mov_tl(addr, H);
    tcg_gen_shli_tl(addr, addr, 16);
    tcg_gen_ori_tl(addr, addr, a->imm);

    gen_data_load(ctx, Rd, addr);
    return true;
}

static bool trans_LDX1(DisasContext *ctx, arg_LDX1 *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_xaddr();

    gen_data_load(ctx, Rd, addr);
    return true;
}

static bool trans_LDX2(DisasContext *ctx, arg_LDX2 *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_xaddr();

    gen_data_load(ctx, Rd, addr);
    tcg_gen_addi_tl(addr, addr, 1);

    gen_set_xaddr(addr);
    return true;
}

static bool trans_LDX3(DisasContext *ctx, arg_LDX3 *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_xaddr();

    tcg_gen_subi_tl(addr, addr, 1);
    gen_data_load(ctx, Rd, addr);
    gen_set_xaddr(addr);
    return true;
}

static bool trans_LDY2(DisasContext *ctx, arg_LDY2 *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_yaddr();

    gen_data_load(ctx, Rd, addr);
    tcg_gen_addi_tl(addr, addr, 1);

    gen_set_yaddr(addr);
    return true;
}

static bool trans_LDY3(DisasContext *ctx, arg_LDY3 *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_yaddr();

    tcg_gen_subi_tl(addr, addr, 1);
    gen_data_load(ctx, Rd, addr);
    gen_set_yaddr(addr);
    return true;
}

static bool trans_LDDY(DisasContext *ctx, arg_LDDY *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_yaddr();

    tcg_gen_addi_tl(addr, addr, a->imm);
    gen_data_load(ctx, Rd, addr);
    return true;
}

static bool trans_LDZ2(DisasContext *ctx, arg_LDZ2 *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_zaddr();

    gen_data_load(ctx, Rd, addr);
    tcg_gen_addi_tl(addr, addr, 1);

    gen_set_zaddr(addr);
    return true;
}

static bool trans_LDZ3(DisasContext *ctx, arg_LDZ3 *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_zaddr();

    tcg_gen_subi_tl(addr, addr, 1);
    gen_data_load(ctx, Rd, addr);

    gen_set_zaddr(addr);
    return true;
}

static bool trans_LDDZ(DisasContext *ctx, arg_LDDZ *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_zaddr();

    tcg_gen_addi_tl(addr, addr, a->imm);
    gen_data_load(ctx, Rd, addr);
    return true;
}

static bool trans_STS(DisasContext *ctx, arg_STS *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = tcg_temp_new_i32();
    TCGv H = cpu_rampD;

    tcg_gen_mov_tl(addr, H);
    tcg_gen_shli_tl(addr, addr, 16);
    tcg_gen_ori_tl(addr, addr, a->imm);
    gen_data_store(ctx, Rd, addr);
    return true;
}

static bool trans_STX1(DisasContext *ctx, arg_STX1 *a)
{
    TCGv Rd = cpu_r[a->rr];
    TCGv addr = gen_get_xaddr();

    gen_data_store(ctx, Rd, addr);
    return true;
}

static bool trans_STX2(DisasContext *ctx, arg_STX2 *a)
{
    TCGv Rd = cpu_r[a->rr];
    TCGv addr = gen_get_xaddr();

    gen_data_store(ctx, Rd, addr);
    tcg_gen_addi_tl(addr, addr, 1);
    gen_set_xaddr(addr);
    return true;
}

static bool trans_STX3(DisasContext *ctx, arg_STX3 *a)
{
    TCGv Rd = cpu_r[a->rr];
    TCGv addr = gen_get_xaddr();

    tcg_gen_subi_tl(addr, addr, 1);
    gen_data_store(ctx, Rd, addr);
    gen_set_xaddr(addr);
    return true;
}

static bool trans_STY2(DisasContext *ctx, arg_STY2 *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_yaddr();

    gen_data_store(ctx, Rd, addr);
    tcg_gen_addi_tl(addr, addr, 1);
    gen_set_yaddr(addr);
    return true;
}

static bool trans_STY3(DisasContext *ctx, arg_STY3 *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_yaddr();

    tcg_gen_subi_tl(addr, addr, 1);
    gen_data_store(ctx, Rd, addr);
    gen_set_yaddr(addr);
    return true;
}

static bool trans_STDY(DisasContext *ctx, arg_STDY *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_yaddr();

    tcg_gen_addi_tl(addr, addr, a->imm);
    gen_data_store(ctx, Rd, addr);
    return true;
}

static bool trans_STZ2(DisasContext *ctx, arg_STZ2 *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_zaddr();

    gen_data_store(ctx, Rd, addr);
    tcg_gen_addi_tl(addr, addr, 1);

    gen_set_zaddr(addr);
    return true;
}

static bool trans_STZ3(DisasContext *ctx, arg_STZ3 *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_zaddr();

    tcg_gen_subi_tl(addr, addr, 1);
    gen_data_store(ctx, Rd, addr);

    gen_set_zaddr(addr);
    return true;
}

static bool trans_STDZ(DisasContext *ctx, arg_STDZ *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_zaddr();

    tcg_gen_addi_tl(addr, addr, a->imm);
    gen_data_store(ctx, Rd, addr);
    return true;
}

static bool trans_LPM1(DisasContext *ctx, arg_LPM1 *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_LPM)) {
        return true;
    }

    TCGv Rd = cpu_r[0];
    TCGv addr = tcg_temp_new_i32();
    TCGv H = cpu_r[31];
    TCGv L = cpu_r[30];

    tcg_gen_shli_tl(addr, H, 8);
    tcg_gen_or_tl(addr, addr, L);
    tcg_gen_qemu_ld_tl(Rd, addr, MMU_CODE_IDX, MO_UB);
    return true;
}

static bool trans_LPM2(DisasContext *ctx, arg_LPM2 *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_LPM)) {
        return true;
    }

    TCGv Rd = cpu_r[a->rd];
    TCGv addr = tcg_temp_new_i32();
    TCGv H = cpu_r[31];
    TCGv L = cpu_r[30];

    tcg_gen_shli_tl(addr, H, 8);
    tcg_gen_or_tl(addr, addr, L);
    tcg_gen_qemu_ld_tl(Rd, addr, MMU_CODE_IDX, MO_UB);
    return true;
}

static bool trans_LPMX(DisasContext *ctx, arg_LPMX *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_LPMX)) {
        return true;
    }

    TCGv Rd = cpu_r[a->rd];
    TCGv addr = tcg_temp_new_i32();
    TCGv H = cpu_r[31];
    TCGv L = cpu_r[30];

    tcg_gen_shli_tl(addr, H, 8);
    tcg_gen_or_tl(addr, addr, L);
    tcg_gen_qemu_ld_tl(Rd, addr, MMU_CODE_IDX, MO_UB);
    tcg_gen_addi_tl(addr, addr, 1);
    tcg_gen_andi_tl(L, addr, 0xff);
    tcg_gen_shri_tl(addr, addr, 8);
    tcg_gen_andi_tl(H, addr, 0xff);
    return true;
}

static bool trans_ELPM1(DisasContext *ctx, arg_ELPM1 *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_ELPM)) {
        return true;
    }

    TCGv Rd = cpu_r[0];
    TCGv addr = gen_get_zaddr();

    tcg_gen_qemu_ld_tl(Rd, addr, MMU_CODE_IDX, MO_UB);
    return true;
}

static bool trans_ELPM2(DisasContext *ctx, arg_ELPM2 *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_ELPM)) {
        return true;
    }

    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_zaddr();

    tcg_gen_qemu_ld_tl(Rd, addr, MMU_CODE_IDX, MO_UB);
    return true;
}

static bool trans_ELPMX(DisasContext *ctx, arg_ELPMX *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_ELPMX)) {
        return true;
    }

    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_zaddr();

    tcg_gen_qemu_ld_tl(Rd, addr, MMU_CODE_IDX, MO_UB);
    tcg_gen_addi_tl(addr, addr, 1);
    gen_set_zaddr(addr);
    return true;
}

static bool trans_SPM(DisasContext *ctx, arg_SPM *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_SPM)) {
        return true;
    }

    return true;
}

static bool trans_SPMX(DisasContext *ctx, arg_SPMX *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_SPMX)) {
        return true;
    }

    return true;
}

static bool trans_IN(DisasContext *ctx, arg_IN *a)
{
    TCGv Rd = cpu_r[a->rd];

    gen_inb(ctx, Rd, a->imm);
    return true;
}

static bool trans_OUT(DisasContext *ctx, arg_OUT *a)
{
    TCGv Rd = cpu_r[a->rd];

    gen_outb(ctx, Rd, a->imm);
    return true;
}

static bool trans_PUSH(DisasContext *ctx, arg_PUSH *a)
{
    TCGv Rd = cpu_r[a->rd];

    gen_data_store(ctx, Rd, cpu_sp);
    tcg_gen_subi_tl(cpu_sp, cpu_sp, 1);

    return true;
}

static bool trans_POP(DisasContext *ctx, arg_POP *a)
{
    TCGv t1 = tcg_temp_new_i32();
    TCGv Rd = cpu_r[a->rd];

    tcg_gen_addi_tl(t1, cpu_sp, 1);
    gen_data_load(ctx, Rd, t1);
    tcg_gen_mov_tl(cpu_sp, t1);

    return true;
}

static bool trans_XCH(DisasContext *ctx, arg_XCH *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_RMW)) {
        return true;
    }

    TCGv Rd = cpu_r[a->rd];
    TCGv t0 = tcg_temp_new_i32();
    TCGv addr = gen_get_zaddr();

    gen_data_load(ctx, t0, addr);
    gen_data_store(ctx, Rd, addr);
    tcg_gen_mov_tl(Rd, t0);
    return true;
}

static bool trans_LAS(DisasContext *ctx, arg_LAS *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_RMW)) {
        return true;
    }

    TCGv Rr = cpu_r[a->rd];
    TCGv addr = gen_get_zaddr();
    TCGv t0 = tcg_temp_new_i32();
    TCGv t1 = tcg_temp_new_i32();

    gen_data_load(ctx, t0, addr);
    tcg_gen_or_tl(t1, t0, Rr);
    tcg_gen_mov_tl(Rr, t0);
    gen_data_store(ctx, t1, addr);
    return true;
}

static bool trans_LAC(DisasContext *ctx, arg_LAC *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_RMW)) {
        return true;
    }

    TCGv Rr = cpu_r[a->rd];
    TCGv addr = gen_get_zaddr();
    TCGv t0 = tcg_temp_new_i32();
    TCGv t1 = tcg_temp_new_i32();

    gen_data_load(ctx, t0, addr);
    tcg_gen_andc_tl(t1, t0, Rr);
    tcg_gen_mov_tl(Rr, t0);
    gen_data_store(ctx, t1, addr);
    return true;
}


static bool trans_LAT(DisasContext *ctx, arg_LAT *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_RMW)) {
        return true;
    }

    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_zaddr();
    TCGv t0 = tcg_temp_new_i32();
    TCGv t1 = tcg_temp_new_i32();

    gen_data_load(ctx, t0, addr);
    tcg_gen_xor_tl(t1, t0, Rd);
    tcg_gen_mov_tl(Rd, t0);
    gen_data_store(ctx, t1, addr);
    return true;
}

static void gen_rshift_ZNVSf(TCGv R)
{
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0);
    tcg_gen_shri_tl(cpu_Nf, R, 7);
    tcg_gen_xor_tl(cpu_Vf, cpu_Nf, cpu_Cf);
    tcg_gen_xor_tl(cpu_Sf, cpu_Nf, cpu_Vf);
}

static bool trans_LSR(DisasContext *ctx, arg_LSR *a)
{
    TCGv Rd = cpu_r[a->rd];

    tcg_gen_andi_tl(cpu_Cf, Rd, 1);
    tcg_gen_shri_tl(Rd, Rd, 1);

    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, Rd, 0);
    tcg_gen_movi_tl(cpu_Nf, 0);
    tcg_gen_mov_tl(cpu_Vf, cpu_Cf);
    tcg_gen_mov_tl(cpu_Sf, cpu_Vf);

    return true;
}

static bool trans_ROR(DisasContext *ctx, arg_ROR *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv t0 = tcg_temp_new_i32();

    tcg_gen_shli_tl(t0, cpu_Cf, 7);

    tcg_gen_andi_tl(cpu_Cf, Rd, 1);

    tcg_gen_shri_tl(Rd, Rd, 1);
    tcg_gen_or_tl(Rd, Rd, t0);

    gen_rshift_ZNVSf(Rd);
    return true;
}

static bool trans_ASR(DisasContext *ctx, arg_ASR *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv t0 = tcg_temp_new_i32();

    tcg_gen_andi_tl(cpu_Cf, Rd, 1);

    tcg_gen_andi_tl(t0, Rd, 0x80);
    tcg_gen_shri_tl(Rd, Rd, 1);
    tcg_gen_or_tl(Rd, Rd, t0);

    gen_rshift_ZNVSf(Rd);
    return true;
}

static bool trans_SWAP(DisasContext *ctx, arg_SWAP *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv t0 = tcg_temp_new_i32();
    TCGv t1 = tcg_temp_new_i32();

    tcg_gen_andi_tl(t0, Rd, 0x0f);
    tcg_gen_shli_tl(t0, t0, 4);
    tcg_gen_andi_tl(t1, Rd, 0xf0);
    tcg_gen_shri_tl(t1, t1, 4);
    tcg_gen_or_tl(Rd, t0, t1);
    return true;
}

static bool trans_SBI(DisasContext *ctx, arg_SBI *a)
{
    TCGv data = tcg_temp_new_i32();

    gen_inb(ctx, data, a->reg);
    tcg_gen_ori_tl(data, data, 1 << a->bit);
    gen_outb(ctx, data, a->reg);
    return true;
}

static bool trans_CBI(DisasContext *ctx, arg_CBI *a)
{
    TCGv data = tcg_temp_new_i32();

    gen_inb(ctx, data, a->reg);
    tcg_gen_andi_tl(data, data, ~(1 << a->bit));
    gen_outb(ctx, data, a->reg);
    return true;
}

static bool trans_BST(DisasContext *ctx, arg_BST *a)
{
    TCGv Rd = cpu_r[a->rd];

    tcg_gen_andi_tl(cpu_Tf, Rd, 1 << a->bit);
    tcg_gen_shri_tl(cpu_Tf, cpu_Tf, a->bit);

    return true;
}

static bool trans_BLD(DisasContext *ctx, arg_BLD *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv t1 = tcg_temp_new_i32();

    tcg_gen_andi_tl(Rd, Rd, ~(1u << a->bit));
    tcg_gen_shli_tl(t1, cpu_Tf, a->bit);
    tcg_gen_or_tl(Rd, Rd, t1);
    return true;
}

static bool trans_BSET(DisasContext *ctx, arg_BSET *a)
{
    switch (a->bit) {
    case 0x00:
        tcg_gen_movi_tl(cpu_Cf, 0x01);
        break;
    case 0x01:
        tcg_gen_movi_tl(cpu_Zf, 0x01);
        break;
    case 0x02:
        tcg_gen_movi_tl(cpu_Nf, 0x01);
        break;
    case 0x03:
        tcg_gen_movi_tl(cpu_Vf, 0x01);
        break;
    case 0x04:
        tcg_gen_movi_tl(cpu_Sf, 0x01);
        break;
    case 0x05:
        tcg_gen_movi_tl(cpu_Hf, 0x01);
        break;
    case 0x06:
        tcg_gen_movi_tl(cpu_Tf, 0x01);
        break;
    case 0x07:
        tcg_gen_movi_tl(cpu_If, 0x01);
        break;
    }

    return true;
}

static bool trans_BCLR(DisasContext *ctx, arg_BCLR *a)
{
    switch (a->bit) {
    case 0x00:
        tcg_gen_movi_tl(cpu_Cf, 0x00);
        break;
    case 0x01:
        tcg_gen_movi_tl(cpu_Zf, 0x00);
        break;
    case 0x02:
        tcg_gen_movi_tl(cpu_Nf, 0x00);
        break;
    case 0x03:
        tcg_gen_movi_tl(cpu_Vf, 0x00);
        break;
    case 0x04:
        tcg_gen_movi_tl(cpu_Sf, 0x00);
        break;
    case 0x05:
        tcg_gen_movi_tl(cpu_Hf, 0x00);
        break;
    case 0x06:
        tcg_gen_movi_tl(cpu_Tf, 0x00);
        break;
    case 0x07:
        tcg_gen_movi_tl(cpu_If, 0x00);
        break;
    }

    return true;
}

static bool trans_BREAK(DisasContext *ctx, arg_BREAK *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_BREAK)) {
        return true;
    }

#ifdef BREAKPOINT_ON_BREAK
    tcg_gen_movi_tl(cpu_pc, ctx->npc - 1);
    gen_helper_debug(tcg_env);
    ctx->base.is_jmp = DISAS_EXIT;
#else
#endif

    return true;
}

static bool trans_NOP(DisasContext *ctx, arg_NOP *a)
{

    return true;
}

static bool trans_SLEEP(DisasContext *ctx, arg_SLEEP *a)
{
    gen_helper_sleep(tcg_env);
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_WDR(DisasContext *ctx, arg_WDR *a)
{
    gen_helper_wdr(tcg_env);

    return true;
}

static void translate(DisasContext *ctx)
{
    uint32_t opcode = next_word(ctx);

    if (!decode_insn(ctx, opcode)) {
        gen_helper_unsupported(tcg_env);
        ctx->base.is_jmp = DISAS_NORETURN;
    }
}

static bool canonicalize_skip(DisasContext *ctx)
{
    switch (ctx->skip_cond) {
    case TCG_COND_NEVER:
        return false;

    case TCG_COND_ALWAYS:
        return false;

    case TCG_COND_NE:
        if (ctx->skip_var1 == NULL) {
            tcg_gen_mov_tl(cpu_skip, ctx->skip_var0);
        } else {
            tcg_gen_xor_tl(cpu_skip, ctx->skip_var0, ctx->skip_var1);
            ctx->skip_var1 = NULL;
        }
        break;

    default:
        if (ctx->skip_var1 == NULL) {
            tcg_gen_setcondi_tl(ctx->skip_cond, cpu_skip, ctx->skip_var0, 0);
        } else {
            tcg_gen_setcond_tl(ctx->skip_cond, cpu_skip,
                               ctx->skip_var0, ctx->skip_var1);
            ctx->skip_var1 = NULL;
        }
        ctx->skip_cond = TCG_COND_NE;
        break;
    }
    ctx->skip_var0 = cpu_skip;
    return true;
}

static void avr_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    uint32_t tb_flags = ctx->base.tb->flags;

    ctx->cs = cs;
    ctx->env = cpu_env(cs);
    ctx->npc = ctx->base.pc_first / 2;

    ctx->skip_cond = TCG_COND_NEVER;
    if (tb_flags & TB_FLAGS_SKIP) {
        ctx->skip_cond = TCG_COND_ALWAYS;
        ctx->skip_var0 = cpu_skip;
    }

    if (tb_flags & TB_FLAGS_FULL_ACCESS) {
        ctx->base.max_insns = 1;
    }
}

static void avr_tr_tb_start(DisasContextBase *db, CPUState *cs)
{
}

static void avr_tr_insn_start(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    tcg_gen_insn_start(ctx->npc);
}

static void avr_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    TCGLabel *skip_label = NULL;

    if (ctx->skip_cond != TCG_COND_NEVER) {
        skip_label = gen_new_label();
        if (ctx->skip_var0 == cpu_skip) {
            ctx->skip_var0 = tcg_temp_new();
            tcg_gen_mov_tl(ctx->skip_var0, cpu_skip);
            tcg_gen_movi_tl(cpu_skip, 0);
        }
        if (ctx->skip_var1 == NULL) {
            tcg_gen_brcondi_tl(ctx->skip_cond, ctx->skip_var0, 0, skip_label);
        } else {
            tcg_gen_brcond_tl(ctx->skip_cond, ctx->skip_var0,
                              ctx->skip_var1, skip_label);
            ctx->skip_var1 = NULL;
        }
        ctx->skip_cond = TCG_COND_NEVER;
        ctx->skip_var0 = NULL;
    }

    translate(ctx);

    ctx->base.pc_next = ctx->npc * 2;

    if (skip_label) {
        canonicalize_skip(ctx);
        gen_set_label(skip_label);

        switch (ctx->base.is_jmp) {
        case DISAS_NORETURN:
            ctx->base.is_jmp = DISAS_CHAIN;
            break;
        case DISAS_NEXT:
            if (ctx->base.tb->flags & TB_FLAGS_SKIP) {
                ctx->base.is_jmp = DISAS_TOO_MANY;
            }
            break;
        default:
            break;
        }
    }

    if (ctx->base.is_jmp == DISAS_NEXT) {
        target_ulong page_first = ctx->base.pc_first & TARGET_PAGE_MASK;

        if ((ctx->base.pc_next - page_first) >= TARGET_PAGE_SIZE - 4) {
            ctx->base.is_jmp = DISAS_TOO_MANY;
        }
    }
}

static void avr_tr_tb_stop(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    bool nonconst_skip = canonicalize_skip(ctx);
    bool force_exit = ctx->base.tb->flags & TB_FLAGS_SKIP;

    switch (ctx->base.is_jmp) {
    case DISAS_NORETURN:
        assert(!nonconst_skip);
        break;
    case DISAS_NEXT:
    case DISAS_TOO_MANY:
    case DISAS_CHAIN:
        if (!nonconst_skip && !force_exit) {
            gen_goto_tb(ctx, 1, ctx->npc);
            break;
        }
        tcg_gen_movi_tl(cpu_pc, ctx->npc);
        /* fall through */
    case DISAS_LOOKUP:
        if (!force_exit) {
            tcg_gen_lookup_and_goto_ptr();
            break;
        }
        /* fall through */
    case DISAS_EXIT:
        tcg_gen_exit_tb(NULL, 0);
        break;
    default:
        g_assert_not_reached();
    }
}

static const TranslatorOps avr_tr_ops = {
    .init_disas_context = avr_tr_init_disas_context,
    .tb_start           = avr_tr_tb_start,
    .insn_start         = avr_tr_insn_start,
    .translate_insn     = avr_tr_translate_insn,
    .tb_stop            = avr_tr_tb_stop,
};

void avr_cpu_translate_code(CPUState *cs, TranslationBlock *tb,
                            int *max_insns, vaddr pc, void *host_pc)
{
    DisasContext dc = { };
    translator_loop(cs, tb, max_insns, pc, host_pc, &avr_tr_ops, &dc.base);
}
