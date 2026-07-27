/*
 * Toshiba TX79-specific instructions translation routines
 *
 *  Copyright (c) 2018 Fredrik Noring
 *  Copyright (c) 2021 Philippe Mathieu-Daudé
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "translate.h"
#include "tcg/tcg-op-gvec.h"

#include "decode-tx79.c.inc"

bool decode_ext_tx79(DisasContext *ctx, uint32_t insn)
{
    if (TARGET_LONG_BITS == 64 && decode_tx79(ctx, insn)) {
        return true;
    }
    return false;
}

static bool trans_MFHI1(DisasContext *ctx, arg_r *a)
{
    gen_store_gpr(cpu_HI[1], a->rd);

    return true;
}

static bool trans_MFLO1(DisasContext *ctx, arg_r *a)
{
    gen_store_gpr(cpu_LO[1], a->rd);

    return true;
}

static bool trans_MTHI1(DisasContext *ctx, arg_r *a)
{
    gen_load_gpr(cpu_HI[1], a->rs);

    return true;
}

static bool trans_MTLO1(DisasContext *ctx, arg_r *a)
{
    gen_load_gpr(cpu_LO[1], a->rs);

    return true;
}

static bool trans_parallel_arith(DisasContext *ctx, arg_r *a,
                                 void (*gen_logic_i64)(TCGv_i64, TCGv_i64, TCGv_i64))
{
    TCGv_i64 ax, bx;

    if (a->rd == 0) {
        return true;
    }

    ax = tcg_temp_new_i64();
    bx = tcg_temp_new_i64();

    gen_load_gpr(ax, a->rs);
    gen_load_gpr(bx, a->rt);
    gen_logic_i64(cpu_gpr[a->rd], ax, bx);

    gen_load_gpr_hi(ax, a->rs);
    gen_load_gpr_hi(bx, a->rt);
    gen_logic_i64(cpu_gpr_hi[a->rd], ax, bx);
    return true;
}

static bool trans_PSUBB(DisasContext *ctx, arg_r *a)
{
    return trans_parallel_arith(ctx, a, tcg_gen_vec_sub8_i64);
}

static bool trans_PSUBH(DisasContext *ctx, arg_r *a)
{
    return trans_parallel_arith(ctx, a, tcg_gen_vec_sub16_i64);
}

static bool trans_PSUBW(DisasContext *ctx, arg_r *a)
{
    return trans_parallel_arith(ctx, a, tcg_gen_vec_sub32_i64);
}

static bool trans_PAND(DisasContext *ctx, arg_r *a)
{
    return trans_parallel_arith(ctx, a, tcg_gen_and_i64);
}

static bool trans_POR(DisasContext *ctx, arg_r *a)
{
    return trans_parallel_arith(ctx, a, tcg_gen_or_i64);
}

static bool trans_PXOR(DisasContext *ctx, arg_r *a)
{
    return trans_parallel_arith(ctx, a, tcg_gen_xor_i64);
}

static bool trans_PNOR(DisasContext *ctx, arg_r *a)
{
    return trans_parallel_arith(ctx, a, tcg_gen_nor_i64);
}

static bool trans_parallel_compare(DisasContext *ctx, arg_r *a,
                                   TCGCond cond, unsigned wlen)
{
    TCGv_i64 c0, c1, ax, bx, t0, t1, t2;

    if (a->rd == 0) {
        return true;
    }

    c0 = tcg_constant_tl(0);
    c1 = tcg_constant_tl(0xffffffff);
    ax = tcg_temp_new_i64();
    bx = tcg_temp_new_i64();
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();

    gen_load_gpr(ax, a->rs);
    gen_load_gpr(bx, a->rt);
    for (int i = 0; i < (64 / wlen); i++) {
        tcg_gen_sextract_i64(t0, ax, wlen * i, wlen);
        tcg_gen_sextract_i64(t1, bx, wlen * i, wlen);
        tcg_gen_movcond_i64(cond, t2, t1, t0, c1, c0);
        tcg_gen_deposit_i64(cpu_gpr[a->rd], cpu_gpr[a->rd], t2, wlen * i, wlen);
    }
    gen_load_gpr_hi(ax, a->rs);
    gen_load_gpr_hi(bx, a->rt);
    for (int i = 0; i < (64 / wlen); i++) {
        tcg_gen_sextract_i64(t0, ax, wlen * i, wlen);
        tcg_gen_sextract_i64(t1, bx, wlen * i, wlen);
        tcg_gen_movcond_i64(cond, t2, t1, t0, c1, c0);
        tcg_gen_deposit_i64(cpu_gpr_hi[a->rd], cpu_gpr_hi[a->rd], t2, wlen * i, wlen);
    }
    return true;
}

static bool trans_PCGTB(DisasContext *ctx, arg_r *a)
{
    return trans_parallel_compare(ctx, a, TCG_COND_GE, 8);
}

static bool trans_PCEQB(DisasContext *ctx, arg_r *a)
{
    return trans_parallel_compare(ctx, a, TCG_COND_EQ, 8);
}

static bool trans_PCGTH(DisasContext *ctx, arg_r *a)
{
    return trans_parallel_compare(ctx, a, TCG_COND_GE, 16);
}

static bool trans_PCEQH(DisasContext *ctx, arg_r *a)
{
    return trans_parallel_compare(ctx, a, TCG_COND_EQ, 16);
}

static bool trans_PCGTW(DisasContext *ctx, arg_r *a)
{
    return trans_parallel_compare(ctx, a, TCG_COND_GE, 32);
}

static bool trans_PCEQW(DisasContext *ctx, arg_r *a)
{
    return trans_parallel_compare(ctx, a, TCG_COND_EQ, 32);
}

static bool trans_LQ(DisasContext *ctx, arg_i *a)
{
    TCGv_i64 t0;
    TCGv addr;

    if (a->rt == 0) {
        return true;
    }

    t0 = tcg_temp_new_i64();
    addr = tcg_temp_new();

    gen_base_offset_addr(ctx, addr, a->base, a->offset);
    tcg_gen_andi_tl(addr, addr, ~0xf);

    tcg_gen_qemu_ld_i64(t0, addr, ctx->mem_idx, mo_endian(ctx) | MO_UQ);
    gen_store_gpr(t0, a->rt);

    tcg_gen_addi_i64(addr, addr, 8);
    tcg_gen_qemu_ld_i64(t0, addr, ctx->mem_idx, mo_endian(ctx) | MO_UQ);
    gen_store_gpr_hi(t0, a->rt);
    return true;
}

static bool trans_SQ(DisasContext *ctx, arg_i *a)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv addr = tcg_temp_new();

    gen_base_offset_addr(ctx, addr, a->base, a->offset);
    tcg_gen_andi_tl(addr, addr, ~0xf);

    gen_load_gpr(t0, a->rt);
    tcg_gen_qemu_st_i64(t0, addr, ctx->mem_idx, mo_endian(ctx) | MO_UQ);

    tcg_gen_addi_i64(addr, addr, 8);
    gen_load_gpr_hi(t0, a->rt);
    tcg_gen_qemu_st_i64(t0, addr, ctx->mem_idx, mo_endian(ctx) | MO_UQ);
    return true;
}

static bool trans_PPACW(DisasContext *ctx, arg_r *a)
{
    TCGv_i64 a0, b0, t0;

    if (a->rd == 0) {
        return true;
    }

    a0 = tcg_temp_new_i64();
    b0 = tcg_temp_new_i64();
    t0 = tcg_temp_new_i64();

    gen_load_gpr(a0, a->rs);
    gen_load_gpr(b0, a->rt);

    gen_load_gpr_hi(t0, a->rt);
    tcg_gen_deposit_i64(cpu_gpr[a->rd], b0, t0, 32, 32);

    gen_load_gpr_hi(t0, a->rs);
    tcg_gen_deposit_i64(cpu_gpr_hi[a->rd], a0, t0, 32, 32);
    return true;
}

static void gen_pextw(TCGv_i64 dl, TCGv_i64 dh, TCGv_i64 a, TCGv_i64 b)
{
    tcg_gen_deposit_i64(dl, b, a, 32, 32);
    tcg_gen_shri_i64(b, b, 32);
    tcg_gen_deposit_i64(dh, a, b, 0, 32);
}

static bool trans_PEXTLx(DisasContext *ctx, arg_r *a, unsigned wlen)
{
    TCGv_i64 ax, bx;

    if (a->rd == 0) {
        return true;
    }

    ax = tcg_temp_new_i64();
    bx = tcg_temp_new_i64();

    gen_load_gpr(ax, a->rs);
    gen_load_gpr(bx, a->rt);

    for (int i = 0; i < 64 / (2 * wlen); i++) {
        tcg_gen_deposit_i64(cpu_gpr[a->rd],
                            cpu_gpr[a->rd], bx, 2 * wlen * i, wlen);
        tcg_gen_deposit_i64(cpu_gpr[a->rd],
                            cpu_gpr[a->rd], ax, 2 * wlen * i + wlen, wlen);
        tcg_gen_shri_i64(bx, bx, wlen);
        tcg_gen_shri_i64(ax, ax, wlen);
    }
    for (int i = 0; i < 64 / (2 * wlen); i++) {
        tcg_gen_deposit_i64(cpu_gpr_hi[a->rd],
                            cpu_gpr_hi[a->rd], bx, 2 * wlen * i, wlen);
        tcg_gen_deposit_i64(cpu_gpr_hi[a->rd],
                            cpu_gpr_hi[a->rd], ax, 2 * wlen * i + wlen, wlen);
        tcg_gen_shri_i64(bx, bx, wlen);
        tcg_gen_shri_i64(ax, ax, wlen);
    }
    return true;
}

static bool trans_PEXTLB(DisasContext *ctx, arg_r *a)
{
    return trans_PEXTLx(ctx, a, 8);
}

static bool trans_PEXTLH(DisasContext *ctx, arg_r *a)
{
    return trans_PEXTLx(ctx, a, 16);
}

static bool trans_PEXTLW(DisasContext *ctx, arg_r *a)
{
    TCGv_i64 ax, bx;

    if (a->rd == 0) {
        return true;
    }

    ax = tcg_temp_new_i64();
    bx = tcg_temp_new_i64();

    gen_load_gpr(ax, a->rs);
    gen_load_gpr(bx, a->rt);
    gen_pextw(cpu_gpr[a->rd], cpu_gpr_hi[a->rd], ax, bx);
    return true;
}

static bool trans_PEXTUW(DisasContext *ctx, arg_r *a)
{
    TCGv_i64 ax, bx;

    if (a->rd == 0) {
        return true;
    }

    ax = tcg_temp_new_i64();
    bx = tcg_temp_new_i64();

    gen_load_gpr_hi(ax, a->rs);
    gen_load_gpr_hi(bx, a->rt);
    gen_pextw(cpu_gpr[a->rd], cpu_gpr_hi[a->rd], ax, bx);
    return true;
}

static bool trans_PCPYH(DisasContext *s, arg_r *a)
{
    if (a->rd == 0) {
        return true;
    }

    if (a->rt == 0) {
        tcg_gen_movi_i64(cpu_gpr[a->rd], 0);
        tcg_gen_movi_i64(cpu_gpr_hi[a->rd], 0);
        return true;
    }

    tcg_gen_deposit_i64(cpu_gpr[a->rd], cpu_gpr[a->rt], cpu_gpr[a->rt], 16, 16);
    tcg_gen_deposit_i64(cpu_gpr[a->rd], cpu_gpr[a->rd], cpu_gpr[a->rd], 32, 32);
    tcg_gen_deposit_i64(cpu_gpr_hi[a->rd], cpu_gpr_hi[a->rt], cpu_gpr_hi[a->rt], 16, 16);
    tcg_gen_deposit_i64(cpu_gpr_hi[a->rd], cpu_gpr_hi[a->rd], cpu_gpr_hi[a->rd], 32, 32);

    return true;
}

static bool trans_PCPYLD(DisasContext *s, arg_r *a)
{
    if (a->rd == 0) {
        return true;
    }

    if (a->rs == 0) {
        tcg_gen_movi_i64(cpu_gpr_hi[a->rd], 0);
    } else {
        tcg_gen_mov_i64(cpu_gpr_hi[a->rd], cpu_gpr[a->rs]);
    }

    if (a->rt == 0) {
        tcg_gen_movi_i64(cpu_gpr[a->rd], 0);
    } else if (a->rd != a->rt) {
        tcg_gen_mov_i64(cpu_gpr[a->rd], cpu_gpr[a->rt]);
    }

    return true;
}

static bool trans_PCPYUD(DisasContext *s, arg_r *a)
{
    if (a->rd == 0) {
        return true;
    }

    gen_load_gpr_hi(cpu_gpr[a->rd], a->rs);

    if (a->rt == 0) {
        tcg_gen_movi_i64(cpu_gpr_hi[a->rd], 0);
    } else if (a->rd != a->rt) {
        tcg_gen_mov_i64(cpu_gpr_hi[a->rd], cpu_gpr_hi[a->rt]);
    }

    return true;
}

static bool trans_PROT3W(DisasContext *ctx, arg_r *a)
{
    TCGv_i64 ax;

    if (a->rd == 0) {
        return true;
    }
    if (a->rt == 0) {
        tcg_gen_movi_i64(cpu_gpr[a->rd], 0);
        tcg_gen_movi_i64(cpu_gpr_hi[a->rd], 0);
        return true;
    }

    ax = tcg_temp_new_i64();

    tcg_gen_mov_i64(ax, cpu_gpr_hi[a->rt]);
    tcg_gen_deposit_i64(cpu_gpr_hi[a->rd], ax, cpu_gpr[a->rt], 0, 32);

    tcg_gen_deposit_i64(cpu_gpr[a->rd], cpu_gpr[a->rt], ax, 0, 32);
    tcg_gen_rotri_i64(cpu_gpr[a->rd], cpu_gpr[a->rd], 32);
    return true;
}
