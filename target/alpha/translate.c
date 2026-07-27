/*
 *  Alpha emulation cpu translation for qemu.
 *
 *  Copyright (c) 2007 Jocelyn Mayer
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "system/cpus.h"
#include "qemu/host-utils.h"
#include "tcg/tcg-op.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/translator.h"
#include "exec/translation-block.h"
#include "exec/target_page.h"
#include "exec/log.h"

#define HELPER_H "helper.h"
#include "exec/helper-info.c.inc"
#undef  HELPER_H

#undef ALPHA_DEBUG_DISAS
#define CONFIG_SOFTFLOAT_INLINE

#ifdef ALPHA_DEBUG_DISAS
#  define LOG_DISAS(...) qemu_log_mask(CPU_LOG_TB_IN_ASM, ## __VA_ARGS__)
#else
#  define LOG_DISAS(...) do { } while (0)
#endif

typedef struct DisasContext DisasContext;
struct DisasContext {
    DisasContextBase base;

#ifdef CONFIG_USER_ONLY
    MemOp unalign;
#endif
    uint32_t tbflags;
    int mem_idx;

    bool pcrel;

    int implver;
    int amask;

    int tb_rm;
    int tb_ftz;

    TCGv *ir;

    TCGv zero;
    TCGv sink;
};

#ifdef CONFIG_USER_ONLY
#define UNALIGN(C)  (C)->unalign
#else
#define UNALIGN(C)  MO_ALIGN
#endif

#define DISAS_PC_UPDATED_NOCHAIN  DISAS_TARGET_0
#define DISAS_PC_UPDATED          DISAS_TARGET_1
#define DISAS_PC_STALE            DISAS_TARGET_2

static TCGv cpu_std_ir[31];
static TCGv cpu_fir[31];
static TCGv cpu_pc;
static TCGv cpu_lock_addr;
static TCGv cpu_lock_value;

#ifndef CONFIG_USER_ONLY
static TCGv cpu_pal_ir[31];
#endif

void alpha_translate_init(void)
{
#define DEF_VAR(V)  { &cpu_##V, #V, offsetof(CPUAlphaState, V) }

    typedef struct { TCGv *var; const char *name; int ofs; } GlobalVar;
    static const GlobalVar vars[] = {
        DEF_VAR(pc),
        DEF_VAR(lock_addr),
        DEF_VAR(lock_value),
    };

#undef DEF_VAR

    static const char greg_names[31][4] = {
        "v0", "t0", "t1", "t2", "t3", "t4", "t5", "t6",
        "t7", "s0", "s1", "s2", "s3", "s4", "s5", "fp",
        "a0", "a1", "a2", "a3", "a4", "a5", "t8", "t9",
        "t10", "t11", "ra", "t12", "at", "gp", "sp"
    };
    static const char freg_names[31][4] = {
        "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
        "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15",
        "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
        "f24", "f25", "f26", "f27", "f28", "f29", "f30"
    };
#ifndef CONFIG_USER_ONLY
    static const char shadow_names[8][8] = {
        "pal_t7", "pal_s0", "pal_s1", "pal_s2",
        "pal_s3", "pal_s4", "pal_s5", "pal_t11"
    };
#endif

    int i;

    for (i = 0; i < 31; i++) {
        cpu_std_ir[i] = tcg_global_mem_new_i64(tcg_env,
                                               offsetof(CPUAlphaState, ir[i]),
                                               greg_names[i]);
    }

    for (i = 0; i < 31; i++) {
        cpu_fir[i] = tcg_global_mem_new_i64(tcg_env,
                                            offsetof(CPUAlphaState, fir[i]),
                                            freg_names[i]);
    }

#ifndef CONFIG_USER_ONLY
    memcpy(cpu_pal_ir, cpu_std_ir, sizeof(cpu_pal_ir));
    for (i = 0; i < 8; i++) {
        int r = (i == 7 ? 25 : i + 8);
        cpu_pal_ir[r] = tcg_global_mem_new_i64(tcg_env,
                                               offsetof(CPUAlphaState,
                                                        shadow[i]),
                                               shadow_names[i]);
    }
#endif

    for (i = 0; i < ARRAY_SIZE(vars); ++i) {
        const GlobalVar *v = &vars[i];
        *v->var = tcg_global_mem_new_i64(tcg_env, v->ofs, v->name);
    }
}

static TCGv load_zero(DisasContext *ctx)
{
    if (!ctx->zero) {
        ctx->zero = tcg_constant_i64(0);
    }
    return ctx->zero;
}

static TCGv dest_sink(DisasContext *ctx)
{
    if (!ctx->sink) {
        ctx->sink = tcg_temp_new();
    }
    return ctx->sink;
}

static void free_context_temps(DisasContext *ctx)
{
    if (ctx->sink) {
        tcg_gen_discard_i64(ctx->sink);
        ctx->sink = NULL;
    }
}

static TCGv load_gpr(DisasContext *ctx, unsigned reg)
{
    if (likely(reg < 31)) {
        return ctx->ir[reg];
    } else {
        return load_zero(ctx);
    }
}

static TCGv load_gpr_lit(DisasContext *ctx, unsigned reg,
                         uint8_t lit, bool islit)
{
    if (islit) {
        return tcg_constant_i64(lit);
    } else if (likely(reg < 31)) {
        return ctx->ir[reg];
    } else {
        return load_zero(ctx);
    }
}

static TCGv dest_gpr(DisasContext *ctx, unsigned reg)
{
    if (likely(reg < 31)) {
        return ctx->ir[reg];
    } else {
        return dest_sink(ctx);
    }
}

static TCGv load_fpr(DisasContext *ctx, unsigned reg)
{
    if (likely(reg < 31)) {
        return cpu_fir[reg];
    } else {
        return load_zero(ctx);
    }
}

static TCGv dest_fpr(DisasContext *ctx, unsigned reg)
{
    if (likely(reg < 31)) {
        return cpu_fir[reg];
    } else {
        return dest_sink(ctx);
    }
}

static int get_flag_ofs(unsigned shift)
{
    int ofs = offsetof(CPUAlphaState, flags);
#if HOST_BIG_ENDIAN
    ofs += 3 - (shift / 8);
#else
    ofs += shift / 8;
#endif
    return ofs;
}

static void ld_flag_byte(TCGv val, unsigned shift)
{
    tcg_gen_ld8u_i64(val, tcg_env, get_flag_ofs(shift));
}

static void st_flag_byte(TCGv val, unsigned shift)
{
    tcg_gen_st8_i64(val, tcg_env, get_flag_ofs(shift));
}

static void gen_pc_disp(DisasContext *ctx, TCGv dest, int32_t disp)
{
    uint64_t addr = ctx->base.pc_next + disp;
    if (ctx->pcrel) {
        tcg_gen_addi_i64(dest, cpu_pc, addr - ctx->base.pc_first);
    } else {
        tcg_gen_movi_i64(dest, addr);
    }
}

static void gen_excp_1(int exception, int error_code)
{
    TCGv_i32 tmp1, tmp2;

    tmp1 = tcg_constant_i32(exception);
    tmp2 = tcg_constant_i32(error_code);
    gen_helper_excp(tcg_env, tmp1, tmp2);
}

static DisasJumpType gen_excp(DisasContext *ctx, int exception, int error_code)
{
    gen_pc_disp(ctx, cpu_pc, 0);
    gen_excp_1(exception, error_code);
    return DISAS_NORETURN;
}

static inline DisasJumpType gen_invalid(DisasContext *ctx)
{
    return gen_excp(ctx, EXCP_OPCDEC, 0);
}

static void gen_ldf(DisasContext *ctx, TCGv dest, TCGv addr)
{
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    tcg_gen_qemu_ld_i32(tmp32, addr, ctx->mem_idx, MO_LEUL | UNALIGN(ctx));
    gen_helper_memory_to_f(dest, tmp32);
}

static void gen_ldg(DisasContext *ctx, TCGv dest, TCGv addr)
{
    TCGv tmp = tcg_temp_new();
    tcg_gen_qemu_ld_i64(tmp, addr, ctx->mem_idx, MO_LEUQ | UNALIGN(ctx));
    gen_helper_memory_to_g(dest, tmp);
}

static void gen_lds(DisasContext *ctx, TCGv dest, TCGv addr)
{
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    tcg_gen_qemu_ld_i32(tmp32, addr, ctx->mem_idx, MO_LEUL | UNALIGN(ctx));
    gen_helper_memory_to_s(dest, tmp32);
}

static void gen_ldt(DisasContext *ctx, TCGv dest, TCGv addr)
{
    tcg_gen_qemu_ld_i64(dest, addr, ctx->mem_idx, MO_LEUQ | UNALIGN(ctx));
}

static void gen_load_fp(DisasContext *ctx, int ra, int rb, int32_t disp16,
                        void (*func)(DisasContext *, TCGv, TCGv))
{
    if (likely(ra != 31)) {
        TCGv addr = tcg_temp_new();
        tcg_gen_addi_i64(addr, load_gpr(ctx, rb), disp16);
        func(ctx, cpu_fir[ra], addr);
    }
}

static void gen_load_int(DisasContext *ctx, int ra, int rb, int32_t disp16,
                         MemOp op, bool clear, bool locked)
{
    TCGv addr, dest;

    if (unlikely(ra == 31)) {
        return;
    }

    addr = tcg_temp_new();
    tcg_gen_addi_i64(addr, load_gpr(ctx, rb), disp16);
    if (clear) {
        tcg_gen_andi_i64(addr, addr, ~0x7);
    } else if (!locked) {
        op |= UNALIGN(ctx);
    }

    dest = ctx->ir[ra];
    tcg_gen_qemu_ld_i64(dest, addr, ctx->mem_idx, op);

    if (locked) {
        tcg_gen_mov_i64(cpu_lock_addr, addr);
        tcg_gen_mov_i64(cpu_lock_value, dest);
    }
}

static void gen_stf(DisasContext *ctx, TCGv src, TCGv addr)
{
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    gen_helper_f_to_memory(tmp32, addr);
    tcg_gen_qemu_st_i32(tmp32, addr, ctx->mem_idx, MO_LEUL | UNALIGN(ctx));
}

static void gen_stg(DisasContext *ctx, TCGv src, TCGv addr)
{
    TCGv tmp = tcg_temp_new();
    gen_helper_g_to_memory(tmp, src);
    tcg_gen_qemu_st_i64(tmp, addr, ctx->mem_idx, MO_LEUQ | UNALIGN(ctx));
}

static void gen_sts(DisasContext *ctx, TCGv src, TCGv addr)
{
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    gen_helper_s_to_memory(tmp32, src);
    tcg_gen_qemu_st_i32(tmp32, addr, ctx->mem_idx, MO_LEUL | UNALIGN(ctx));
}

static void gen_stt(DisasContext *ctx, TCGv src, TCGv addr)
{
    tcg_gen_qemu_st_i64(src, addr, ctx->mem_idx, MO_LEUQ | UNALIGN(ctx));
}

static void gen_store_fp(DisasContext *ctx, int ra, int rb, int32_t disp16,
                         void (*func)(DisasContext *, TCGv, TCGv))
{
    TCGv addr = tcg_temp_new();
    tcg_gen_addi_i64(addr, load_gpr(ctx, rb), disp16);
    func(ctx, load_fpr(ctx, ra), addr);
}

static void gen_store_int(DisasContext *ctx, int ra, int rb, int32_t disp16,
                          MemOp op, bool clear)
{
    TCGv addr, src;

    addr = tcg_temp_new();
    tcg_gen_addi_i64(addr, load_gpr(ctx, rb), disp16);
    if (clear) {
        tcg_gen_andi_i64(addr, addr, ~0x7);
    } else {
        op |= UNALIGN(ctx);
    }

    src = load_gpr(ctx, ra);
    tcg_gen_qemu_st_i64(src, addr, ctx->mem_idx, op);
}

static DisasJumpType gen_store_conditional(DisasContext *ctx, int ra, int rb,
                                           int32_t disp16, int mem_idx,
                                           MemOp op)
{
    TCGLabel *lab_fail, *lab_done;
    TCGv addr, val;

    addr = tcg_temp_new_i64();
    tcg_gen_addi_i64(addr, load_gpr(ctx, rb), disp16);
    free_context_temps(ctx);

    lab_fail = gen_new_label();
    lab_done = gen_new_label();
    tcg_gen_brcond_i64(TCG_COND_NE, addr, cpu_lock_addr, lab_fail);

    val = tcg_temp_new_i64();
    tcg_gen_atomic_cmpxchg_i64(val, cpu_lock_addr, cpu_lock_value,
                               load_gpr(ctx, ra), mem_idx, op);
    free_context_temps(ctx);

    if (ra != 31) {
        tcg_gen_setcond_i64(TCG_COND_EQ, ctx->ir[ra], val, cpu_lock_value);
    }
    tcg_gen_br(lab_done);

    gen_set_label(lab_fail);
    if (ra != 31) {
        tcg_gen_movi_i64(ctx->ir[ra], 0);
    }

    gen_set_label(lab_done);
    tcg_gen_movi_i64(cpu_lock_addr, -1);
    return DISAS_NEXT;
}

static void gen_goto_tb(DisasContext *ctx, unsigned tb_slot_idx, int32_t disp)
{
    if (translator_use_goto_tb(&ctx->base, ctx->base.pc_next + disp)) {
        if (ctx->pcrel) {
            gen_pc_disp(ctx, cpu_pc, disp);
            tcg_gen_goto_tb(tb_slot_idx);
        } else {
            tcg_gen_goto_tb(tb_slot_idx);
            gen_pc_disp(ctx, cpu_pc, disp);
        }
        tcg_gen_exit_tb(ctx->base.tb, tb_slot_idx);
    } else {
        gen_pc_disp(ctx, cpu_pc, disp);
        tcg_gen_lookup_and_goto_ptr();
    }
}

static DisasJumpType gen_bdirect(DisasContext *ctx, int ra, int32_t disp)
{
    if (ra != 31) {
        gen_pc_disp(ctx, ctx->ir[ra], 0);
    }

    if (disp == 0) {
        return DISAS_NEXT;
    }
    gen_goto_tb(ctx, 0, disp);
    return DISAS_NORETURN;
}

static DisasJumpType gen_bcond_internal(DisasContext *ctx, TCGCond cond,
                                        TCGv cmp, uint64_t imm, int32_t disp)
{
    TCGLabel *lab_true = gen_new_label();

    tcg_gen_brcondi_i64(cond, cmp, imm, lab_true);
    gen_goto_tb(ctx, 0, 0);
    gen_set_label(lab_true);
    gen_goto_tb(ctx, 1, disp);

    return DISAS_NORETURN;
}

static DisasJumpType gen_bcond(DisasContext *ctx, TCGCond cond, int ra,
                               int32_t disp)
{
    return gen_bcond_internal(ctx, cond, load_gpr(ctx, ra),
                              is_tst_cond(cond), disp);
}

static TCGv_i64 gen_fold_mzero(TCGCond *pcond, uint64_t *pimm, TCGv_i64 src)
{
    TCGv_i64 tmp;

    *pimm = 0;
    switch (*pcond) {
    case TCG_COND_LE:
    case TCG_COND_GT:
        return src;

    case TCG_COND_EQ:
    case TCG_COND_NE:
        *pcond = *pcond == TCG_COND_EQ ? TCG_COND_TSTEQ : TCG_COND_TSTNE;
        *pimm = INT64_MAX;
        return src;

    case TCG_COND_GE:
    case TCG_COND_LT:
        tmp = tcg_temp_new_i64();
        tcg_gen_movcond_i64(TCG_COND_EQ, tmp,
                            src, tcg_constant_i64(INT64_MIN),
                            tcg_constant_i64(0), src);
        return tmp;

    default:
        g_assert_not_reached();
    }
}

static DisasJumpType gen_fbcond(DisasContext *ctx, TCGCond cond, int ra,
                                int32_t disp)
{
    uint64_t imm;
    TCGv_i64 tmp = gen_fold_mzero(&cond, &imm, load_fpr(ctx, ra));
    return gen_bcond_internal(ctx, cond, tmp, imm, disp);
}

static void gen_fcmov(DisasContext *ctx, TCGCond cond, int ra, int rb, int rc)
{
    uint64_t imm;
    TCGv_i64 tmp = gen_fold_mzero(&cond, &imm, load_fpr(ctx, ra));
    tcg_gen_movcond_i64(cond, dest_fpr(ctx, rc),
                        tmp, tcg_constant_i64(imm),
                        load_fpr(ctx, rb), load_fpr(ctx, rc));
}

#define QUAL_RM_N       0x080
#define QUAL_RM_C       0x000
#define QUAL_RM_M       0x040
#define QUAL_RM_D       0x0c0
#define QUAL_RM_MASK    0x0c0

#define QUAL_U          0x100
#define QUAL_V          0x100
#define QUAL_S          0x400
#define QUAL_I          0x200

static void gen_qual_roundmode(DisasContext *ctx, int fn11)
{
    TCGv_i32 tmp;

    fn11 &= QUAL_RM_MASK;
    if (fn11 == ctx->tb_rm) {
        return;
    }
    ctx->tb_rm = fn11;

    tmp = tcg_temp_new_i32();
    switch (fn11) {
    case QUAL_RM_N:
        tcg_gen_movi_i32(tmp, float_round_nearest_even);
        break;
    case QUAL_RM_C:
        tcg_gen_movi_i32(tmp, float_round_to_zero);
        break;
    case QUAL_RM_M:
        tcg_gen_movi_i32(tmp, float_round_down);
        break;
    case QUAL_RM_D:
        tcg_gen_ld8u_i32(tmp, tcg_env,
                         offsetof(CPUAlphaState, fpcr_dyn_round));
        break;
    }

#if defined(CONFIG_SOFTFLOAT_INLINE)
    tcg_gen_st8_i32(tmp, tcg_env,
                    offsetof(CPUAlphaState, fp_status.float_rounding_mode));
#else
    gen_helper_setroundmode(tmp);
#endif
}

static void gen_qual_flushzero(DisasContext *ctx, int fn11)
{
    TCGv_i32 tmp;

    fn11 &= QUAL_U;
    if (fn11 == ctx->tb_ftz) {
        return;
    }
    ctx->tb_ftz = fn11;

    tmp = tcg_temp_new_i32();
    if (fn11) {
        tcg_gen_ld8u_i32(tmp, tcg_env,
                         offsetof(CPUAlphaState, fpcr_flush_to_zero));
    } else {
        tcg_gen_movi_i32(tmp, 1);
    }

#if defined(CONFIG_SOFTFLOAT_INLINE)
    tcg_gen_st8_i32(tmp, tcg_env,
                    offsetof(CPUAlphaState, fp_status.flush_to_zero));
#else
    gen_helper_setflushzero(tmp);
#endif
}

static TCGv gen_ieee_input(DisasContext *ctx, int reg, int fn11, int is_cmp)
{
    TCGv val;

    if (unlikely(reg == 31)) {
        val = load_zero(ctx);
    } else {
        val = cpu_fir[reg];
        if ((fn11 & QUAL_S) == 0) {
            if (is_cmp) {
                gen_helper_ieee_input_cmp(tcg_env, val);
            } else {
                gen_helper_ieee_input(tcg_env, val);
            }
        } else {
#ifndef CONFIG_USER_ONLY
            gen_helper_ieee_input_s(tcg_env, val);
#endif
        }
    }
    return val;
}

static void gen_fp_exc_raise(int rc, int fn11)
{
    TCGv_i32 reg, ign;
    uint32_t ignore = 0;

    if (!(fn11 & QUAL_U)) {
        ignore |= FPCR_UNF | FPCR_IOV;
    }
    if (!(fn11 & QUAL_I)) {
        ignore |= FPCR_INE;
    }
    ign = tcg_constant_i32(ignore);

    reg = tcg_constant_i32(rc + 32);
    if (fn11 & QUAL_S) {
        gen_helper_fp_exc_raise_s(tcg_env, ign, reg);
    } else {
        gen_helper_fp_exc_raise(tcg_env, ign, reg);
    }
}

static void gen_cvtlq(TCGv vc, TCGv vb)
{
    TCGv tmp = tcg_temp_new();

    tcg_gen_shri_i64(tmp, vb, 29);
    tcg_gen_sari_i64(vc, vb, 32);
    tcg_gen_deposit_i64(vc, vc, tmp, 0, 30);
}

static void gen_ieee_arith2(DisasContext *ctx,
                            void (*helper)(TCGv, TCGv_ptr, TCGv),
                            int rb, int rc, int fn11)
{
    TCGv vb;

    gen_qual_roundmode(ctx, fn11);
    gen_qual_flushzero(ctx, fn11);

    vb = gen_ieee_input(ctx, rb, fn11, 0);
    helper(dest_fpr(ctx, rc), tcg_env, vb);

    gen_fp_exc_raise(rc, fn11);
}

#define IEEE_ARITH2(name)                                       \
static inline void glue(gen_, name)(DisasContext *ctx,          \
                                    int rb, int rc, int fn11)   \
{                                                               \
    gen_ieee_arith2(ctx, gen_helper_##name, rb, rc, fn11);      \
}
IEEE_ARITH2(sqrts)
IEEE_ARITH2(sqrtt)
IEEE_ARITH2(cvtst)
IEEE_ARITH2(cvtts)

static void gen_cvttq(DisasContext *ctx, int rb, int rc, int fn11)
{
    TCGv vb, vc;

    vb = gen_ieee_input(ctx, rb, fn11, 0);
    vc = dest_fpr(ctx, rc);

    if ((fn11 & QUAL_RM_MASK) == QUAL_RM_C) {
        gen_helper_cvttq_c(vc, tcg_env, vb);
    } else {
        gen_qual_roundmode(ctx, fn11);
        gen_helper_cvttq(vc, tcg_env, vb);
    }
    gen_fp_exc_raise(rc, fn11);
}

static void gen_ieee_intcvt(DisasContext *ctx,
                            void (*helper)(TCGv, TCGv_ptr, TCGv),
                            int rb, int rc, int fn11)
{
    TCGv vb, vc;

    gen_qual_roundmode(ctx, fn11);
    vb = load_fpr(ctx, rb);
    vc = dest_fpr(ctx, rc);

    if (fn11 & QUAL_I) {
        helper(vc, tcg_env, vb);
        gen_fp_exc_raise(rc, fn11);
    } else {
        helper(vc, tcg_env, vb);
    }
}

#define IEEE_INTCVT(name)                                       \
static inline void glue(gen_, name)(DisasContext *ctx,          \
                                    int rb, int rc, int fn11)   \
{                                                               \
    gen_ieee_intcvt(ctx, gen_helper_##name, rb, rc, fn11);      \
}
IEEE_INTCVT(cvtqs)
IEEE_INTCVT(cvtqt)

static void gen_cpy_mask(TCGv vc, TCGv va, TCGv vb, bool inv_a, uint64_t mask)
{
    TCGv vmask = tcg_constant_i64(mask);
    TCGv tmp = tcg_temp_new_i64();

    if (inv_a) {
        tcg_gen_andc_i64(tmp, vmask, va);
    } else {
        tcg_gen_and_i64(tmp, va, vmask);
    }

    tcg_gen_andc_i64(vc, vb, vmask);
    tcg_gen_or_i64(vc, vc, tmp);
}

static void gen_ieee_arith3(DisasContext *ctx,
                            void (*helper)(TCGv, TCGv_ptr, TCGv, TCGv),
                            int ra, int rb, int rc, int fn11)
{
    TCGv va, vb, vc;

    gen_qual_roundmode(ctx, fn11);
    gen_qual_flushzero(ctx, fn11);

    va = gen_ieee_input(ctx, ra, fn11, 0);
    vb = gen_ieee_input(ctx, rb, fn11, 0);
    vc = dest_fpr(ctx, rc);
    helper(vc, tcg_env, va, vb);

    gen_fp_exc_raise(rc, fn11);
}

#define IEEE_ARITH3(name)                                               \
static inline void glue(gen_, name)(DisasContext *ctx,                  \
                                    int ra, int rb, int rc, int fn11)   \
{                                                                       \
    gen_ieee_arith3(ctx, gen_helper_##name, ra, rb, rc, fn11);          \
}
IEEE_ARITH3(adds)
IEEE_ARITH3(subs)
IEEE_ARITH3(muls)
IEEE_ARITH3(divs)
IEEE_ARITH3(addt)
IEEE_ARITH3(subt)
IEEE_ARITH3(mult)
IEEE_ARITH3(divt)

static void gen_ieee_compare(DisasContext *ctx,
                             void (*helper)(TCGv, TCGv_ptr, TCGv, TCGv),
                             int ra, int rb, int rc, int fn11)
{
    TCGv va, vb, vc;

    va = gen_ieee_input(ctx, ra, fn11, 1);
    vb = gen_ieee_input(ctx, rb, fn11, 1);
    vc = dest_fpr(ctx, rc);
    helper(vc, tcg_env, va, vb);

    gen_fp_exc_raise(rc, fn11);
}

#define IEEE_CMP3(name)                                                 \
static inline void glue(gen_, name)(DisasContext *ctx,                  \
                                    int ra, int rb, int rc, int fn11)   \
{                                                                       \
    gen_ieee_compare(ctx, gen_helper_##name, ra, rb, rc, fn11);         \
}
IEEE_CMP3(cmptun)
IEEE_CMP3(cmpteq)
IEEE_CMP3(cmptlt)
IEEE_CMP3(cmptle)

static inline uint64_t zapnot_mask(uint8_t lit)
{
    uint64_t mask = 0;
    int i;

    for (i = 0; i < 8; ++i) {
        if ((lit >> i) & 1) {
            mask |= 0xffull << (i * 8);
        }
    }
    return mask;
}

static void gen_zapnoti(TCGv dest, TCGv src, uint8_t lit)
{
    switch (lit) {
    case 0x00:
        tcg_gen_movi_i64(dest, 0);
        break;
    case 0x01:
        tcg_gen_ext8u_i64(dest, src);
        break;
    case 0x03:
        tcg_gen_ext16u_i64(dest, src);
        break;
    case 0x0f:
        tcg_gen_ext32u_i64(dest, src);
        break;
    case 0xff:
        tcg_gen_mov_i64(dest, src);
        break;
    default:
        tcg_gen_andi_i64(dest, src, zapnot_mask(lit));
        break;
    }
}

static void gen_ext_h(DisasContext *ctx, TCGv vc, TCGv va, int rb, bool islit,
                      uint8_t lit, uint8_t byte_mask)
{
    if (islit) {
        int pos = (64 - lit * 8) & 0x3f;
        int len = cto32(byte_mask) * 8;
        if (pos < len) {
            tcg_gen_deposit_z_i64(vc, va, pos, len - pos);
        } else {
            tcg_gen_movi_i64(vc, 0);
        }
    } else {
        TCGv tmp = tcg_temp_new();
        tcg_gen_shli_i64(tmp, load_gpr(ctx, rb), 3);
        tcg_gen_neg_i64(tmp, tmp);
        tcg_gen_andi_i64(tmp, tmp, 0x3f);
        tcg_gen_shl_i64(vc, va, tmp);
    }
    gen_zapnoti(vc, vc, byte_mask);
}

static void gen_ext_l(DisasContext *ctx, TCGv vc, TCGv va, int rb, bool islit,
                      uint8_t lit, uint8_t byte_mask)
{
    if (islit) {
        int pos = (lit & 7) * 8;
        int len = cto32(byte_mask) * 8;
        if (pos + len >= 64) {
            len = 64 - pos;
        }
        tcg_gen_extract_i64(vc, va, pos, len);
    } else {
        TCGv tmp = tcg_temp_new();
        tcg_gen_andi_i64(tmp, load_gpr(ctx, rb), 7);
        tcg_gen_shli_i64(tmp, tmp, 3);
        tcg_gen_shr_i64(vc, va, tmp);
        gen_zapnoti(vc, vc, byte_mask);
    }
}

static void gen_ins_h(DisasContext *ctx, TCGv vc, TCGv va, int rb, bool islit,
                      uint8_t lit, uint8_t byte_mask)
{
    if (islit) {
        int pos = 64 - (lit & 7) * 8;
        int len = cto32(byte_mask) * 8;
        if (pos < len) {
            tcg_gen_extract_i64(vc, va, pos, len - pos);
        } else {
            tcg_gen_movi_i64(vc, 0);
        }
    } else {
        TCGv tmp = tcg_temp_new();
        TCGv shift = tcg_temp_new();

        gen_zapnoti(tmp, va, byte_mask);

        tcg_gen_shli_i64(shift, load_gpr(ctx, rb), 3);
        tcg_gen_not_i64(shift, shift);
        tcg_gen_andi_i64(shift, shift, 0x3f);

        tcg_gen_shr_i64(vc, tmp, shift);
        tcg_gen_shri_i64(vc, vc, 1);
    }
}

static void gen_ins_l(DisasContext *ctx, TCGv vc, TCGv va, int rb, bool islit,
                      uint8_t lit, uint8_t byte_mask)
{
    if (islit) {
        int pos = (lit & 7) * 8;
        int len = cto32(byte_mask) * 8;
        if (pos + len > 64) {
            len = 64 - pos;
        }
        tcg_gen_deposit_z_i64(vc, va, pos, len);
    } else {
        TCGv tmp = tcg_temp_new();
        TCGv shift = tcg_temp_new();

        gen_zapnoti(tmp, va, byte_mask);

        tcg_gen_andi_i64(shift, load_gpr(ctx, rb), 7);
        tcg_gen_shli_i64(shift, shift, 3);
        tcg_gen_shl_i64(vc, tmp, shift);
    }
}

static void gen_msk_h(DisasContext *ctx, TCGv vc, TCGv va, int rb, bool islit,
                      uint8_t lit, uint8_t byte_mask)
{
    if (islit) {
        gen_zapnoti(vc, va, ~((byte_mask << (lit & 7)) >> 8));
    } else {
        TCGv shift = tcg_temp_new();
        TCGv mask = tcg_temp_new();

        tcg_gen_shli_i64(shift, load_gpr(ctx, rb), 3);
        tcg_gen_not_i64(shift, shift);
        tcg_gen_andi_i64(shift, shift, 0x3f);
        tcg_gen_movi_i64(mask, zapnot_mask (byte_mask));
        tcg_gen_shr_i64(mask, mask, shift);
        tcg_gen_shri_i64(mask, mask, 1);

        tcg_gen_andc_i64(vc, va, mask);
    }
}

static void gen_msk_l(DisasContext *ctx, TCGv vc, TCGv va, int rb, bool islit,
                      uint8_t lit, uint8_t byte_mask)
{
    if (islit) {
        gen_zapnoti(vc, va, ~(byte_mask << (lit & 7)));
    } else {
        TCGv shift = tcg_temp_new();
        TCGv mask = tcg_temp_new();

        tcg_gen_andi_i64(shift, load_gpr(ctx, rb), 7);
        tcg_gen_shli_i64(shift, shift, 3);
        tcg_gen_movi_i64(mask, zapnot_mask(byte_mask));
        tcg_gen_shl_i64(mask, mask, shift);

        tcg_gen_andc_i64(vc, va, mask);
    }
}

static void gen_rx(DisasContext *ctx, int ra, int set)
{
    if (ra != 31) {
        ld_flag_byte(ctx->ir[ra], ENV_FLAG_RX_SHIFT);
    }

    st_flag_byte(tcg_constant_i64(set), ENV_FLAG_RX_SHIFT);
}

static DisasJumpType gen_call_pal(DisasContext *ctx, int palcode)
{

    if (palcode >= 0x80 && palcode < 0xC0) {
        switch (palcode) {
        case 0x86:
            break;
        case 0x9E:
            tcg_gen_ld_i64(ctx->ir[IR_V0], tcg_env,
                           offsetof(CPUAlphaState, unique));
            break;
        case 0x9F:
            tcg_gen_st_i64(ctx->ir[IR_A0], tcg_env,
                           offsetof(CPUAlphaState, unique));
            break;
        default:
            palcode &= 0xbf;
            goto do_call_pal;
        }
        return DISAS_NEXT;
    }

#ifndef CONFIG_USER_ONLY
    if (palcode < 0x40 && (ctx->tbflags & ENV_FLAG_PS_USER) == 0) {
        switch (palcode) {
        case 0x01:
            break;
        case 0x02:
            break;
        case 0x2D:
            tcg_gen_st_i64(ctx->ir[IR_A0], tcg_env,
                           offsetof(CPUAlphaState, vptptr));
            break;
        case 0x31:
            tcg_gen_st_i64(ctx->ir[IR_A0], tcg_env,
                           offsetof(CPUAlphaState, sysval));
            break;
        case 0x32:
            tcg_gen_ld_i64(ctx->ir[IR_V0], tcg_env,
                           offsetof(CPUAlphaState, sysval));
            break;

        case 0x35:
            ld_flag_byte(ctx->ir[IR_V0], ENV_FLAG_PS_SHIFT);

            {
                TCGv tmp = tcg_temp_new();
                tcg_gen_andi_i64(tmp, ctx->ir[IR_A0], PS_INT_MASK);
                st_flag_byte(tmp, ENV_FLAG_PS_SHIFT);
            }

            gen_pc_disp(ctx, cpu_pc, 0);
            return DISAS_PC_UPDATED_NOCHAIN;

        case 0x36:
            ld_flag_byte(ctx->ir[IR_V0], ENV_FLAG_PS_SHIFT);
            break;

        case 0x38:
            tcg_gen_st_i64(ctx->ir[IR_A0], tcg_env,
                           offsetof(CPUAlphaState, usp));
            break;
        case 0x3A:
            tcg_gen_ld_i64(ctx->ir[IR_V0], tcg_env,
                           offsetof(CPUAlphaState, usp));
            break;
        case 0x3C:
            gen_helper_whami(ctx->ir[IR_V0], tcg_env);
            break;

        case 0x3E:
            tcg_gen_st_i32(tcg_constant_i32(1), tcg_env,
                           -offsetof(AlphaCPU, env) +
                           offsetof(CPUState, halted));
            tcg_gen_movi_i64(ctx->ir[IR_V0], 0);
            return gen_excp(ctx, EXCP_HALTED, 0);

        default:
            palcode &= 0x3f;
            goto do_call_pal;
        }
        return DISAS_NEXT;
    }
#endif
    return gen_invalid(ctx);

 do_call_pal:
#ifdef CONFIG_USER_ONLY
    return gen_excp(ctx, EXCP_CALL_PAL, palcode);
#else
    {
        TCGv tmp = tcg_temp_new();

        gen_pc_disp(ctx, tmp, 0);
        if (ctx->tbflags & ENV_FLAG_PAL_MODE) {
            tcg_gen_ori_i64(tmp, tmp, 1);
        } else {
            st_flag_byte(tcg_constant_i64(1), ENV_FLAG_PAL_SHIFT);
        }
        tcg_gen_st_i64(tmp, tcg_env, offsetof(CPUAlphaState, exc_addr));

        tcg_gen_ld_i64(cpu_pc, tcg_env, offsetof(CPUAlphaState, palbr));
        tcg_gen_addi_i64(cpu_pc, cpu_pc,
                         palcode & 0x80
                         ? 0x2000 + (palcode - 0x80) * 64
                         : 0x1000 + palcode * 64);
        return DISAS_PC_UPDATED;
    }
#endif
}

#ifndef CONFIG_USER_ONLY

#define PR_LONG         0x200000

static int cpu_pr_data(int pr)
{
    switch (pr) {
    case  2: return offsetof(CPUAlphaState, pcc_ofs) | PR_LONG;
    case  3: return offsetof(CPUAlphaState, trap_arg0);
    case  4: return offsetof(CPUAlphaState, trap_arg1);
    case  5: return offsetof(CPUAlphaState, trap_arg2);
    case  6: return offsetof(CPUAlphaState, exc_addr);
    case  7: return offsetof(CPUAlphaState, palbr);
    case  8: return offsetof(CPUAlphaState, ptbr);
    case  9: return offsetof(CPUAlphaState, vptptr);
    case 10: return offsetof(CPUAlphaState, unique);
    case 11: return offsetof(CPUAlphaState, sysval);
    case 12: return offsetof(CPUAlphaState, usp);

    case 40 ... 63:
        return offsetof(CPUAlphaState, scratch[pr - 40]);

    case 251:
        return offsetof(CPUAlphaState, alarm_expire);
    }
    return 0;
}

static DisasJumpType gen_mfpr(DisasContext *ctx, TCGv va, int regno)
{
    void (*helper)(TCGv);
    int data;

    switch (regno) {
    case 32 ... 39:
        regno = regno == 39 ? 25 : regno - 32 + 8;
        tcg_gen_mov_i64(va, cpu_std_ir[regno]);
        break;

    case 250:
        helper = gen_helper_get_walltime;
        goto do_helper;
    case 249:
        helper = gen_helper_get_vmtime;
    do_helper:
        if (translator_io_start(&ctx->base)) {
            helper(va);
            return DISAS_PC_STALE;
        } else {
            helper(va);
        }
        break;

    case 0:
        ld_flag_byte(va, ENV_FLAG_PS_SHIFT);
        break;
    case 1:
        ld_flag_byte(va, ENV_FLAG_FEN_SHIFT);
        break;

    default:
        data = cpu_pr_data(regno);
        if (data == 0) {
            tcg_gen_movi_i64(va, 0);
        } else if (data & PR_LONG) {
            tcg_gen_ld32s_i64(va, tcg_env, data & ~PR_LONG);
        } else {
            tcg_gen_ld_i64(va, tcg_env, data);
        }
        break;
    }

    return DISAS_NEXT;
}

static DisasJumpType gen_mtpr(DisasContext *ctx, TCGv vb, int regno)
{
    int data;
    DisasJumpType ret = DISAS_NEXT;

    switch (regno) {
    case 255:
        gen_helper_tbia(tcg_env);
        break;

    case 254:
        gen_helper_tbis(tcg_env, vb);
        break;

    case 253:
        tcg_gen_st_i32(tcg_constant_i32(1), tcg_env,
                       -offsetof(AlphaCPU, env) + offsetof(CPUState, halted));
        return gen_excp(ctx, EXCP_HALTED, 0);

    case 252:
        gen_helper_halt(vb);
        return DISAS_PC_STALE;

    case 251:
        if (translator_io_start(&ctx->base)) {
            ret = DISAS_PC_STALE;
        }
        gen_helper_set_alarm(tcg_env, vb);
        break;

    case 7:
        tcg_gen_st_i64(vb, tcg_env, offsetof(CPUAlphaState, palbr));
        break;

    case 32 ... 39:
        regno = regno == 39 ? 25 : regno - 32 + 8;
        tcg_gen_mov_i64(cpu_std_ir[regno], vb);
        break;

    case 0:
        st_flag_byte(vb, ENV_FLAG_PS_SHIFT);
        break;
    case 1:
        st_flag_byte(vb, ENV_FLAG_FEN_SHIFT);
        break;

    default:
        data = cpu_pr_data(regno);
        if (data != 0) {
            if (data & PR_LONG) {
                tcg_gen_st32_i64(vb, tcg_env, data & ~PR_LONG);
            } else {
                tcg_gen_st_i64(vb, tcg_env, data);
            }
        }
        break;
    }

    return ret;
}
#endif

#define REQUIRE_NO_LIT                          \
    do {                                        \
        if (real_islit) {                       \
            goto invalid_opc;                   \
        }                                       \
    } while (0)

#define REQUIRE_AMASK(FLAG)                     \
    do {                                        \
        if ((ctx->amask & AMASK_##FLAG) == 0) { \
            goto invalid_opc;                   \
        }                                       \
    } while (0)

#define REQUIRE_TB_FLAG(FLAG)                   \
    do {                                        \
        if ((ctx->tbflags & (FLAG)) == 0) {     \
            goto invalid_opc;                   \
        }                                       \
    } while (0)

#define REQUIRE_REG_31(WHICH)                   \
    do {                                        \
        if (WHICH != 31) {                      \
            goto invalid_opc;                   \
        }                                       \
    } while (0)

#define REQUIRE_FEN                             \
    do {                                        \
        if (!(ctx->tbflags & ENV_FLAG_FEN)) {   \
            goto raise_fen;                     \
        }                                       \
    } while (0)

static DisasJumpType translate_one(DisasContext *ctx, uint32_t insn)
{
    int32_t disp21, disp16, disp12 __attribute__((unused));
    uint16_t fn11;
    uint8_t opc, ra, rb, rc, fpfn, fn7, lit;
    bool islit, real_islit;
    TCGv va, vb, vc, tmp, tmp2;
    TCGv_i32 t32;
    DisasJumpType ret;

    opc = extract32(insn, 26, 6);
    ra = extract32(insn, 21, 5);
    rb = extract32(insn, 16, 5);
    rc = extract32(insn, 0, 5);
    real_islit = islit = extract32(insn, 12, 1);
    lit = extract32(insn, 13, 8);

    disp21 = sextract32(insn, 0, 21) * 4;
    disp16 = sextract32(insn, 0, 16);
    disp12 = sextract32(insn, 0, 12);

    fn11 = extract32(insn, 5, 11);
    fpfn = extract32(insn, 5, 6);
    fn7 = extract32(insn, 5, 7);

    if (rb == 31 && !islit) {
        islit = true;
        lit = 0;
    }

    ret = DISAS_NEXT;
    switch (opc) {
    case 0x00:
        ret = gen_call_pal(ctx, insn & 0x03ffffff);
        break;
    case 0x01:
        goto invalid_opc;
    case 0x02:
        goto invalid_opc;
    case 0x03:
        goto invalid_opc;
    case 0x04:
        goto invalid_opc;
    case 0x05:
        goto invalid_opc;
    case 0x06:
        goto invalid_opc;
    case 0x07:
        goto invalid_opc;

    case 0x09:
        disp16 = (uint32_t)disp16 << 16;
        /* fall through */
    case 0x08:
        va = dest_gpr(ctx, ra);
        if (rb == 31) {
            tcg_gen_movi_i64(va, disp16);
        } else {
            tcg_gen_addi_i64(va, load_gpr(ctx, rb), disp16);
        }
        break;

    case 0x0A:
        REQUIRE_AMASK(BWX);
        gen_load_int(ctx, ra, rb, disp16, MO_UB, 0, 0);
        break;
    case 0x0B:
        gen_load_int(ctx, ra, rb, disp16, MO_LEUQ, 1, 0);
        break;
    case 0x0C:
        REQUIRE_AMASK(BWX);
        gen_load_int(ctx, ra, rb, disp16, MO_LEUW, 0, 0);
        break;
    case 0x0D:
        REQUIRE_AMASK(BWX);
        gen_store_int(ctx, ra, rb, disp16, MO_LEUW, 0);
        break;
    case 0x0E:
        REQUIRE_AMASK(BWX);
        gen_store_int(ctx, ra, rb, disp16, MO_UB, 0);
        break;
    case 0x0F:
        gen_store_int(ctx, ra, rb, disp16, MO_LEUQ, 1);
        break;

    case 0x10:
        vc = dest_gpr(ctx, rc);
        vb = load_gpr_lit(ctx, rb, lit, islit);

        if (ra == 31) {
            if (fn7 == 0x00) {
                tcg_gen_ext32s_i64(vc, vb);
                break;
            }
            if (fn7 == 0x29) {
                tcg_gen_neg_i64(vc, vb);
                break;
            }
        }

        va = load_gpr(ctx, ra);
        switch (fn7) {
        case 0x00:
            tcg_gen_add_i64(vc, va, vb);
            tcg_gen_ext32s_i64(vc, vc);
            break;
        case 0x02:
            tmp = tcg_temp_new();
            tcg_gen_shli_i64(tmp, va, 2);
            tcg_gen_add_i64(tmp, tmp, vb);
            tcg_gen_ext32s_i64(vc, tmp);
            break;
        case 0x09:
            tcg_gen_sub_i64(vc, va, vb);
            tcg_gen_ext32s_i64(vc, vc);
            break;
        case 0x0B:
            tmp = tcg_temp_new();
            tcg_gen_shli_i64(tmp, va, 2);
            tcg_gen_sub_i64(tmp, tmp, vb);
            tcg_gen_ext32s_i64(vc, tmp);
            break;
        case 0x0F:
            if (ra == 31) {
                gen_helper_cmpbe0(vc, vb);
            } else {
                gen_helper_cmpbge(vc, va, vb);
            }
            break;
        case 0x12:
            tmp = tcg_temp_new();
            tcg_gen_shli_i64(tmp, va, 3);
            tcg_gen_add_i64(tmp, tmp, vb);
            tcg_gen_ext32s_i64(vc, tmp);
            break;
        case 0x1B:
            tmp = tcg_temp_new();
            tcg_gen_shli_i64(tmp, va, 3);
            tcg_gen_sub_i64(tmp, tmp, vb);
            tcg_gen_ext32s_i64(vc, tmp);
            break;
        case 0x1D:
            tcg_gen_setcond_i64(TCG_COND_LTU, vc, va, vb);
            break;
        case 0x20:
            tcg_gen_add_i64(vc, va, vb);
            break;
        case 0x22:
            tmp = tcg_temp_new();
            tcg_gen_shli_i64(tmp, va, 2);
            tcg_gen_add_i64(vc, tmp, vb);
            break;
        case 0x29:
            tcg_gen_sub_i64(vc, va, vb);
            break;
        case 0x2B:
            tmp = tcg_temp_new();
            tcg_gen_shli_i64(tmp, va, 2);
            tcg_gen_sub_i64(vc, tmp, vb);
            break;
        case 0x2D:
            tcg_gen_setcond_i64(TCG_COND_EQ, vc, va, vb);
            break;
        case 0x32:
            tmp = tcg_temp_new();
            tcg_gen_shli_i64(tmp, va, 3);
            tcg_gen_add_i64(vc, tmp, vb);
            break;
        case 0x3B:
            tmp = tcg_temp_new();
            tcg_gen_shli_i64(tmp, va, 3);
            tcg_gen_sub_i64(vc, tmp, vb);
            break;
        case 0x3D:
            tcg_gen_setcond_i64(TCG_COND_LEU, vc, va, vb);
            break;
        case 0x40:
            tmp = tcg_temp_new();
            tcg_gen_ext32s_i64(tmp, va);
            tcg_gen_ext32s_i64(vc, vb);
            tcg_gen_add_i64(tmp, tmp, vc);
            tcg_gen_ext32s_i64(vc, tmp);
            gen_helper_check_overflow(tcg_env, vc, tmp);
            break;
        case 0x49:
            tmp = tcg_temp_new();
            tcg_gen_ext32s_i64(tmp, va);
            tcg_gen_ext32s_i64(vc, vb);
            tcg_gen_sub_i64(tmp, tmp, vc);
            tcg_gen_ext32s_i64(vc, tmp);
            gen_helper_check_overflow(tcg_env, vc, tmp);
            break;
        case 0x4D:
            tcg_gen_setcond_i64(TCG_COND_LT, vc, va, vb);
            break;
        case 0x60:
            tmp = tcg_temp_new();
            tmp2 = tcg_temp_new();
            tcg_gen_eqv_i64(tmp, va, vb);
            tcg_gen_mov_i64(tmp2, va);
            tcg_gen_add_i64(vc, va, vb);
            tcg_gen_xor_i64(tmp2, tmp2, vc);
            tcg_gen_and_i64(tmp, tmp, tmp2);
            tcg_gen_shri_i64(tmp, tmp, 63);
            tcg_gen_movi_i64(tmp2, 0);
            gen_helper_check_overflow(tcg_env, tmp, tmp2);
            break;
        case 0x69:
            tmp = tcg_temp_new();
            tmp2 = tcg_temp_new();
            tcg_gen_xor_i64(tmp, va, vb);
            tcg_gen_mov_i64(tmp2, va);
            tcg_gen_sub_i64(vc, va, vb);
            tcg_gen_xor_i64(tmp2, tmp2, vc);
            tcg_gen_and_i64(tmp, tmp, tmp2);
            tcg_gen_shri_i64(tmp, tmp, 63);
            tcg_gen_movi_i64(tmp2, 0);
            gen_helper_check_overflow(tcg_env, tmp, tmp2);
            break;
        case 0x6D:
            tcg_gen_setcond_i64(TCG_COND_LE, vc, va, vb);
            break;
        default:
            goto invalid_opc;
        }
        break;

    case 0x11:
        if (fn7 == 0x20) {
            if (rc == 31) {
                break;
            }
            if (ra == 31) {
                vc = dest_gpr(ctx, rc);
                if (islit) {
                    tcg_gen_movi_i64(vc, lit);
                } else {
                    tcg_gen_mov_i64(vc, load_gpr(ctx, rb));
                }
                break;
            }
        }

        vc = dest_gpr(ctx, rc);
        vb = load_gpr_lit(ctx, rb, lit, islit);

        if (fn7 == 0x28 && ra == 31) {
            tcg_gen_not_i64(vc, vb);
            break;
        }

        va = load_gpr(ctx, ra);
        switch (fn7) {
        case 0x00:
            tcg_gen_and_i64(vc, va, vb);
            break;
        case 0x08:
            tcg_gen_andc_i64(vc, va, vb);
            break;
        case 0x14:
            tcg_gen_movcond_i64(TCG_COND_TSTNE, vc, va, tcg_constant_i64(1),
                                vb, load_gpr(ctx, rc));
            break;
        case 0x16:
            tcg_gen_movcond_i64(TCG_COND_TSTEQ, vc, va, tcg_constant_i64(1),
                                vb, load_gpr(ctx, rc));
            break;
        case 0x20:
            tcg_gen_or_i64(vc, va, vb);
            break;
        case 0x24:
            tcg_gen_movcond_i64(TCG_COND_EQ, vc, va, load_zero(ctx),
                                vb, load_gpr(ctx, rc));
            break;
        case 0x26:
            tcg_gen_movcond_i64(TCG_COND_NE, vc, va, load_zero(ctx),
                                vb, load_gpr(ctx, rc));
            break;
        case 0x28:
            tcg_gen_orc_i64(vc, va, vb);
            break;
        case 0x40:
            tcg_gen_xor_i64(vc, va, vb);
            break;
        case 0x44:
            tcg_gen_movcond_i64(TCG_COND_LT, vc, va, load_zero(ctx),
                                vb, load_gpr(ctx, rc));
            break;
        case 0x46:
            tcg_gen_movcond_i64(TCG_COND_GE, vc, va, load_zero(ctx),
                                vb, load_gpr(ctx, rc));
            break;
        case 0x48:
            tcg_gen_eqv_i64(vc, va, vb);
            break;
        case 0x61:
            REQUIRE_REG_31(ra);
            tcg_gen_andi_i64(vc, vb, ~ctx->amask);
            break;
        case 0x64:
            tcg_gen_movcond_i64(TCG_COND_LE, vc, va, load_zero(ctx),
                                vb, load_gpr(ctx, rc));
            break;
        case 0x66:
            tcg_gen_movcond_i64(TCG_COND_GT, vc, va, load_zero(ctx),
                                vb, load_gpr(ctx, rc));
            break;
        case 0x6C:
            REQUIRE_REG_31(ra);
            tcg_gen_movi_i64(vc, ctx->implver);
            break;
        default:
            goto invalid_opc;
        }
        break;

    case 0x12:
        vc = dest_gpr(ctx, rc);
        va = load_gpr(ctx, ra);
        switch (fn7) {
        case 0x02:
            gen_msk_l(ctx, vc, va, rb, islit, lit, 0x01);
            break;
        case 0x06:
            gen_ext_l(ctx, vc, va, rb, islit, lit, 0x01);
            break;
        case 0x0B:
            gen_ins_l(ctx, vc, va, rb, islit, lit, 0x01);
            break;
        case 0x12:
            gen_msk_l(ctx, vc, va, rb, islit, lit, 0x03);
            break;
        case 0x16:
            gen_ext_l(ctx, vc, va, rb, islit, lit, 0x03);
            break;
        case 0x1B:
            gen_ins_l(ctx, vc, va, rb, islit, lit, 0x03);
            break;
        case 0x22:
            gen_msk_l(ctx, vc, va, rb, islit, lit, 0x0f);
            break;
        case 0x26:
            gen_ext_l(ctx, vc, va, rb, islit, lit, 0x0f);
            break;
        case 0x2B:
            gen_ins_l(ctx, vc, va, rb, islit, lit, 0x0f);
            break;
        case 0x30:
            if (islit) {
                gen_zapnoti(vc, va, ~lit);
            } else {
                gen_helper_zap(vc, va, load_gpr(ctx, rb));
            }
            break;
        case 0x31:
            if (islit) {
                gen_zapnoti(vc, va, lit);
            } else {
                gen_helper_zapnot(vc, va, load_gpr(ctx, rb));
            }
            break;
        case 0x32:
            gen_msk_l(ctx, vc, va, rb, islit, lit, 0xff);
            break;
        case 0x34:
            if (islit) {
                tcg_gen_shri_i64(vc, va, lit & 0x3f);
            } else {
                tmp = tcg_temp_new();
                vb = load_gpr(ctx, rb);
                tcg_gen_andi_i64(tmp, vb, 0x3f);
                tcg_gen_shr_i64(vc, va, tmp);
            }
            break;
        case 0x36:
            gen_ext_l(ctx, vc, va, rb, islit, lit, 0xff);
            break;
        case 0x39:
            if (islit) {
                tcg_gen_shli_i64(vc, va, lit & 0x3f);
            } else {
                tmp = tcg_temp_new();
                vb = load_gpr(ctx, rb);
                tcg_gen_andi_i64(tmp, vb, 0x3f);
                tcg_gen_shl_i64(vc, va, tmp);
            }
            break;
        case 0x3B:
            gen_ins_l(ctx, vc, va, rb, islit, lit, 0xff);
            break;
        case 0x3C:
            if (islit) {
                tcg_gen_sari_i64(vc, va, lit & 0x3f);
            } else {
                tmp = tcg_temp_new();
                vb = load_gpr(ctx, rb);
                tcg_gen_andi_i64(tmp, vb, 0x3f);
                tcg_gen_sar_i64(vc, va, tmp);
            }
            break;
        case 0x52:
            gen_msk_h(ctx, vc, va, rb, islit, lit, 0x03);
            break;
        case 0x57:
            gen_ins_h(ctx, vc, va, rb, islit, lit, 0x03);
            break;
        case 0x5A:
            gen_ext_h(ctx, vc, va, rb, islit, lit, 0x03);
            break;
        case 0x62:
            gen_msk_h(ctx, vc, va, rb, islit, lit, 0x0f);
            break;
        case 0x67:
            gen_ins_h(ctx, vc, va, rb, islit, lit, 0x0f);
            break;
        case 0x6A:
            gen_ext_h(ctx, vc, va, rb, islit, lit, 0x0f);
            break;
        case 0x72:
            gen_msk_h(ctx, vc, va, rb, islit, lit, 0xff);
            break;
        case 0x77:
            gen_ins_h(ctx, vc, va, rb, islit, lit, 0xff);
            break;
        case 0x7A:
            gen_ext_h(ctx, vc, va, rb, islit, lit, 0xff);
            break;
        default:
            goto invalid_opc;
        }
        break;

    case 0x13:
        vc = dest_gpr(ctx, rc);
        vb = load_gpr_lit(ctx, rb, lit, islit);
        va = load_gpr(ctx, ra);
        switch (fn7) {
        case 0x00:
            tcg_gen_mul_i64(vc, va, vb);
            tcg_gen_ext32s_i64(vc, vc);
            break;
        case 0x20:
            tcg_gen_mul_i64(vc, va, vb);
            break;
        case 0x30:
            tmp = tcg_temp_new();
            tcg_gen_mulu2_i64(tmp, vc, va, vb);
            break;
        case 0x40:
            tmp = tcg_temp_new();
            tcg_gen_ext32s_i64(tmp, va);
            tcg_gen_ext32s_i64(vc, vb);
            tcg_gen_mul_i64(tmp, tmp, vc);
            tcg_gen_ext32s_i64(vc, tmp);
            gen_helper_check_overflow(tcg_env, vc, tmp);
            break;
        case 0x60:
            tmp = tcg_temp_new();
            tmp2 = tcg_temp_new();
            tcg_gen_muls2_i64(vc, tmp, va, vb);
            tcg_gen_sari_i64(tmp2, vc, 63);
            gen_helper_check_overflow(tcg_env, tmp, tmp2);
            break;
        default:
            goto invalid_opc;
        }
        break;

    case 0x14:
        REQUIRE_AMASK(FIX);
        vc = dest_fpr(ctx, rc);
        switch (fpfn) {
        case 0x04:
            REQUIRE_REG_31(rb);
            REQUIRE_FEN;
            t32 = tcg_temp_new_i32();
            va = load_gpr(ctx, ra);
            tcg_gen_extrl_i64_i32(t32, va);
            gen_helper_memory_to_s(vc, t32);
            break;
        case 0x0A:
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            vb = load_fpr(ctx, rb);
            gen_helper_sqrtf(vc, tcg_env, vb);
            break;
        case 0x0B:
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            gen_sqrts(ctx, rb, rc, fn11);
            break;
        case 0x14:
            REQUIRE_REG_31(rb);
            REQUIRE_FEN;
            t32 = tcg_temp_new_i32();
            va = load_gpr(ctx, ra);
            tcg_gen_extrl_i64_i32(t32, va);
            gen_helper_memory_to_f(vc, t32);
            break;
        case 0x24:
            REQUIRE_REG_31(rb);
            REQUIRE_FEN;
            va = load_gpr(ctx, ra);
            tcg_gen_mov_i64(vc, va);
            break;
        case 0x2A:
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            vb = load_fpr(ctx, rb);
            gen_helper_sqrtg(vc, tcg_env, vb);
            break;
        case 0x02B:
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            gen_sqrtt(ctx, rb, rc, fn11);
            break;
        default:
            goto invalid_opc;
        }
        break;

    case 0x15:
        vc = dest_fpr(ctx, rc);
        vb = load_fpr(ctx, rb);
        va = load_fpr(ctx, ra);
        switch (fpfn) {
        case 0x00:
            REQUIRE_FEN;
            gen_helper_addf(vc, tcg_env, va, vb);
            break;
        case 0x01:
            REQUIRE_FEN;
            gen_helper_subf(vc, tcg_env, va, vb);
            break;
        case 0x02:
            REQUIRE_FEN;
            gen_helper_mulf(vc, tcg_env, va, vb);
            break;
        case 0x03:
            REQUIRE_FEN;
            gen_helper_divf(vc, tcg_env, va, vb);
            break;
        case 0x1E:
            REQUIRE_REG_31(ra);
            goto invalid_opc;
        case 0x20:
            REQUIRE_FEN;
            gen_helper_addg(vc, tcg_env, va, vb);
            break;
        case 0x21:
            REQUIRE_FEN;
            gen_helper_subg(vc, tcg_env, va, vb);
            break;
        case 0x22:
            REQUIRE_FEN;
            gen_helper_mulg(vc, tcg_env, va, vb);
            break;
        case 0x23:
            REQUIRE_FEN;
            gen_helper_divg(vc, tcg_env, va, vb);
            break;
        case 0x25:
            REQUIRE_FEN;
            gen_helper_cmpgeq(vc, tcg_env, va, vb);
            break;
        case 0x26:
            REQUIRE_FEN;
            gen_helper_cmpglt(vc, tcg_env, va, vb);
            break;
        case 0x27:
            REQUIRE_FEN;
            gen_helper_cmpgle(vc, tcg_env, va, vb);
            break;
        case 0x2C:
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            gen_helper_cvtgf(vc, tcg_env, vb);
            break;
        case 0x2D:
            REQUIRE_REG_31(ra);
            goto invalid_opc;
        case 0x2F:
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            gen_helper_cvtgq(vc, tcg_env, vb);
            break;
        case 0x3C:
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            gen_helper_cvtqf(vc, tcg_env, vb);
            break;
        case 0x3E:
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            gen_helper_cvtqg(vc, tcg_env, vb);
            break;
        default:
            goto invalid_opc;
        }
        break;

    case 0x16:
        switch (fpfn) {
        case 0x00:
            REQUIRE_FEN;
            gen_adds(ctx, ra, rb, rc, fn11);
            break;
        case 0x01:
            REQUIRE_FEN;
            gen_subs(ctx, ra, rb, rc, fn11);
            break;
        case 0x02:
            REQUIRE_FEN;
            gen_muls(ctx, ra, rb, rc, fn11);
            break;
        case 0x03:
            REQUIRE_FEN;
            gen_divs(ctx, ra, rb, rc, fn11);
            break;
        case 0x20:
            REQUIRE_FEN;
            gen_addt(ctx, ra, rb, rc, fn11);
            break;
        case 0x21:
            REQUIRE_FEN;
            gen_subt(ctx, ra, rb, rc, fn11);
            break;
        case 0x22:
            REQUIRE_FEN;
            gen_mult(ctx, ra, rb, rc, fn11);
            break;
        case 0x23:
            REQUIRE_FEN;
            gen_divt(ctx, ra, rb, rc, fn11);
            break;
        case 0x24:
            REQUIRE_FEN;
            gen_cmptun(ctx, ra, rb, rc, fn11);
            break;
        case 0x25:
            REQUIRE_FEN;
            gen_cmpteq(ctx, ra, rb, rc, fn11);
            break;
        case 0x26:
            REQUIRE_FEN;
            gen_cmptlt(ctx, ra, rb, rc, fn11);
            break;
        case 0x27:
            REQUIRE_FEN;
            gen_cmptle(ctx, ra, rb, rc, fn11);
            break;
        case 0x2C:
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            if (fn11 == 0x2AC || fn11 == 0x6AC) {
                gen_cvtst(ctx, rb, rc, fn11);
            } else {
                gen_cvtts(ctx, rb, rc, fn11);
            }
            break;
        case 0x2F:
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            gen_cvttq(ctx, rb, rc, fn11);
            break;
        case 0x3C:
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            gen_cvtqs(ctx, rb, rc, fn11);
            break;
        case 0x3E:
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            gen_cvtqt(ctx, rb, rc, fn11);
            break;
        default:
            goto invalid_opc;
        }
        break;

    case 0x17:
        switch (fn11) {
        case 0x010:
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            vc = dest_fpr(ctx, rc);
            vb = load_fpr(ctx, rb);
            gen_cvtlq(vc, vb);
            break;
        case 0x020:
            REQUIRE_FEN;
            if (rc == 31) {
            } else {
                vc = dest_fpr(ctx, rc);
                va = load_fpr(ctx, ra);
                if (ra == rb) {
                    tcg_gen_mov_i64(vc, va);
                } else {
                    vb = load_fpr(ctx, rb);
                    gen_cpy_mask(vc, va, vb, 0, 0x8000000000000000ULL);
                }
            }
            break;
        case 0x021:
            REQUIRE_FEN;
            vc = dest_fpr(ctx, rc);
            vb = load_fpr(ctx, rb);
            va = load_fpr(ctx, ra);
            gen_cpy_mask(vc, va, vb, 1, 0x8000000000000000ULL);
            break;
        case 0x022:
            REQUIRE_FEN;
            vc = dest_fpr(ctx, rc);
            vb = load_fpr(ctx, rb);
            va = load_fpr(ctx, ra);
            gen_cpy_mask(vc, va, vb, 0, 0xFFF0000000000000ULL);
            break;
        case 0x024:
            REQUIRE_FEN;
            va = load_fpr(ctx, ra);
            gen_helper_store_fpcr(tcg_env, va);
            if (ctx->tb_rm == QUAL_RM_D) {
                ctx->tb_rm = -1;
            }
            break;
        case 0x025:
            REQUIRE_FEN;
            va = dest_fpr(ctx, ra);
            gen_helper_load_fpcr(va, tcg_env);
            break;
        case 0x02A:
            REQUIRE_FEN;
            gen_fcmov(ctx, TCG_COND_EQ, ra, rb, rc);
            break;
        case 0x02B:
            REQUIRE_FEN;
            gen_fcmov(ctx, TCG_COND_NE, ra, rb, rc);
            break;
        case 0x02C:
            REQUIRE_FEN;
            gen_fcmov(ctx, TCG_COND_LT, ra, rb, rc);
            break;
        case 0x02D:
            REQUIRE_FEN;
            gen_fcmov(ctx, TCG_COND_GE, ra, rb, rc);
            break;
        case 0x02E:
            REQUIRE_FEN;
            gen_fcmov(ctx, TCG_COND_LE, ra, rb, rc);
            break;
        case 0x02F:
            REQUIRE_FEN;
            gen_fcmov(ctx, TCG_COND_GT, ra, rb, rc);
            break;
        case 0x030:
        case 0x130:
        case 0x530:
            REQUIRE_REG_31(ra);
            REQUIRE_FEN;
            vc = dest_fpr(ctx, rc);
            vb = load_fpr(ctx, rb);
            gen_helper_cvtql(vc, tcg_env, vb);
            gen_fp_exc_raise(rc, fn11);
            break;
        default:
            goto invalid_opc;
        }
        break;

    case 0x18:
        switch ((uint16_t)disp16) {
        case 0x0000:
            break;
        case 0x0400:
            break;
        case 0x4000:
            tcg_gen_mb(TCG_MO_ALL | TCG_BAR_SC);
            break;
        case 0x4400:
            tcg_gen_mb(TCG_MO_ST_ST | TCG_BAR_SC);
            break;
        case 0x8000:
            break;
        case 0xA000:
            break;
        case 0xC000:
            va = dest_gpr(ctx, ra);
            if (translator_io_start(&ctx->base)) {
                ret = DISAS_PC_STALE;
            }
            gen_helper_load_pcc(va, tcg_env);
            break;
        case 0xE000:
            gen_rx(ctx, ra, 0);
            break;
        case 0xE800:
            break;
        case 0xF000:
            gen_rx(ctx, ra, 1);
            break;
        case 0xF800:
            break;
        case 0xFC00:
            break;
        default:
            goto invalid_opc;
        }
        break;

    case 0x19:
#ifndef CONFIG_USER_ONLY
        REQUIRE_TB_FLAG(ENV_FLAG_PAL_MODE);
        va = dest_gpr(ctx, ra);
        ret = gen_mfpr(ctx, va, insn & 0xffff);
        break;
#else
        goto invalid_opc;
#endif

    case 0x1A:
        vb = load_gpr(ctx, rb);
        if (ra != 31) {
            tmp = tcg_temp_new();
            tcg_gen_andi_i64(tmp, vb, ~3);
            gen_pc_disp(ctx, ctx->ir[ra], 0);
            tcg_gen_mov_i64(cpu_pc, tmp);
        } else {
            tcg_gen_andi_i64(cpu_pc, vb, ~3);
        }
        ret = DISAS_PC_UPDATED;
        break;

    case 0x1B:
#ifndef CONFIG_USER_ONLY
        REQUIRE_TB_FLAG(ENV_FLAG_PAL_MODE);
        {
            TCGv addr = tcg_temp_new();
            vb = load_gpr(ctx, rb);
            va = dest_gpr(ctx, ra);

            tcg_gen_addi_i64(addr, vb, disp12);
            switch ((insn >> 12) & 0xF) {
            case 0x0:
                tcg_gen_qemu_ld_i64(va, addr, MMU_PHYS_IDX, MO_LESL | MO_ALIGN);
                break;
            case 0x1:
                tcg_gen_qemu_ld_i64(va, addr, MMU_PHYS_IDX, MO_LEUQ | MO_ALIGN);
                break;
            case 0x2:
                tcg_gen_qemu_ld_i64(va, addr, MMU_PHYS_IDX, MO_LESL | MO_ALIGN);
                tcg_gen_mov_i64(cpu_lock_addr, addr);
                tcg_gen_mov_i64(cpu_lock_value, va);
                break;
            case 0x3:
                tcg_gen_qemu_ld_i64(va, addr, MMU_PHYS_IDX, MO_LEUQ | MO_ALIGN);
                tcg_gen_mov_i64(cpu_lock_addr, addr);
                tcg_gen_mov_i64(cpu_lock_value, va);
                break;
            case 0x4:
                goto invalid_opc;
            case 0x5:
                goto invalid_opc;
                break;
            case 0x6:
                goto invalid_opc;
            case 0x7:
                goto invalid_opc;
            case 0x8:
                goto invalid_opc;
            case 0x9:
                goto invalid_opc;
            case 0xA:
                tcg_gen_qemu_ld_i64(va, addr, MMU_KERNEL_IDX,
                                    MO_LESL | MO_ALIGN);
                break;
            case 0xB:
                tcg_gen_qemu_ld_i64(va, addr, MMU_KERNEL_IDX,
                                    MO_LEUQ | MO_ALIGN);
                break;
            case 0xC:
                goto invalid_opc;
            case 0xD:
                goto invalid_opc;
            case 0xE:
                tcg_gen_qemu_ld_i64(va, addr, MMU_USER_IDX,
                                    MO_LESL | MO_ALIGN);
                break;
            case 0xF:
                tcg_gen_qemu_ld_i64(va, addr, MMU_USER_IDX,
                                    MO_LEUQ | MO_ALIGN);
                break;
            }
            break;
        }
#else
        goto invalid_opc;
#endif

    case 0x1C:
        vc = dest_gpr(ctx, rc);
        if (fn7 == 0x70) {
            REQUIRE_AMASK(FIX);
            REQUIRE_REG_31(rb);
            va = load_fpr(ctx, ra);
            tcg_gen_mov_i64(vc, va);
            break;
        } else if (fn7 == 0x78) {
            REQUIRE_AMASK(FIX);
            REQUIRE_REG_31(rb);
            t32 = tcg_temp_new_i32();
            va = load_fpr(ctx, ra);
            gen_helper_s_to_memory(t32, va);
            tcg_gen_ext_i32_i64(vc, t32);
            break;
        }

        vb = load_gpr_lit(ctx, rb, lit, islit);
        switch (fn7) {
        case 0x00:
            REQUIRE_AMASK(BWX);
            REQUIRE_REG_31(ra);
            tcg_gen_ext8s_i64(vc, vb);
            break;
        case 0x01:
            REQUIRE_AMASK(BWX);
            REQUIRE_REG_31(ra);
            tcg_gen_ext16s_i64(vc, vb);
            break;
        case 0x30:
            REQUIRE_AMASK(CIX);
            REQUIRE_REG_31(ra);
            REQUIRE_NO_LIT;
            tcg_gen_ctpop_i64(vc, vb);
            break;
        case 0x31:
            REQUIRE_AMASK(MVI);
            REQUIRE_NO_LIT;
            va = load_gpr(ctx, ra);
            gen_helper_perr(vc, va, vb);
            break;
        case 0x32:
            REQUIRE_AMASK(CIX);
            REQUIRE_REG_31(ra);
            REQUIRE_NO_LIT;
            tcg_gen_clzi_i64(vc, vb, 64);
            break;
        case 0x33:
            REQUIRE_AMASK(CIX);
            REQUIRE_REG_31(ra);
            REQUIRE_NO_LIT;
            tcg_gen_ctzi_i64(vc, vb, 64);
            break;
        case 0x34:
            REQUIRE_AMASK(MVI);
            REQUIRE_REG_31(ra);
            REQUIRE_NO_LIT;
            gen_helper_unpkbw(vc, vb);
            break;
        case 0x35:
            REQUIRE_AMASK(MVI);
            REQUIRE_REG_31(ra);
            REQUIRE_NO_LIT;
            gen_helper_unpkbl(vc, vb);
            break;
        case 0x36:
            REQUIRE_AMASK(MVI);
            REQUIRE_REG_31(ra);
            REQUIRE_NO_LIT;
            gen_helper_pkwb(vc, vb);
            break;
        case 0x37:
            REQUIRE_AMASK(MVI);
            REQUIRE_REG_31(ra);
            REQUIRE_NO_LIT;
            gen_helper_pklb(vc, vb);
            break;
        case 0x38:
            REQUIRE_AMASK(MVI);
            va = load_gpr(ctx, ra);
            gen_helper_minsb8(vc, va, vb);
            break;
        case 0x39:
            REQUIRE_AMASK(MVI);
            va = load_gpr(ctx, ra);
            gen_helper_minsw4(vc, va, vb);
            break;
        case 0x3A:
            REQUIRE_AMASK(MVI);
            va = load_gpr(ctx, ra);
            gen_helper_minub8(vc, va, vb);
            break;
        case 0x3B:
            REQUIRE_AMASK(MVI);
            va = load_gpr(ctx, ra);
            gen_helper_minuw4(vc, va, vb);
            break;
        case 0x3C:
            REQUIRE_AMASK(MVI);
            va = load_gpr(ctx, ra);
            gen_helper_maxub8(vc, va, vb);
            break;
        case 0x3D:
            REQUIRE_AMASK(MVI);
            va = load_gpr(ctx, ra);
            gen_helper_maxuw4(vc, va, vb);
            break;
        case 0x3E:
            REQUIRE_AMASK(MVI);
            va = load_gpr(ctx, ra);
            gen_helper_maxsb8(vc, va, vb);
            break;
        case 0x3F:
            REQUIRE_AMASK(MVI);
            va = load_gpr(ctx, ra);
            gen_helper_maxsw4(vc, va, vb);
            break;
        default:
            goto invalid_opc;
        }
        break;

    case 0x1D:
#ifndef CONFIG_USER_ONLY
        REQUIRE_TB_FLAG(ENV_FLAG_PAL_MODE);
        vb = load_gpr(ctx, rb);
        ret = gen_mtpr(ctx, vb, insn & 0xffff);
        break;
#else
        goto invalid_opc;
#endif

    case 0x1E:
#ifndef CONFIG_USER_ONLY
        REQUIRE_TB_FLAG(ENV_FLAG_PAL_MODE);
        if (rb == 31) {
            vb = dest_sink(ctx);
            tcg_gen_ld_i64(vb, tcg_env, offsetof(CPUAlphaState, exc_addr));
        } else {
            vb = load_gpr(ctx, rb);
        }
        tcg_gen_movi_i64(cpu_lock_addr, -1);
        st_flag_byte(load_zero(ctx), ENV_FLAG_RX_SHIFT);
        tmp = tcg_temp_new();
        tcg_gen_andi_i64(tmp, vb, 1);
        st_flag_byte(tmp, ENV_FLAG_PAL_SHIFT);
        tcg_gen_andi_i64(cpu_pc, vb, ~3);
        ret = DISAS_PC_UPDATED_NOCHAIN;
        break;
#else
        goto invalid_opc;
#endif

    case 0x1F:
#ifndef CONFIG_USER_ONLY
        REQUIRE_TB_FLAG(ENV_FLAG_PAL_MODE);
        {
            switch ((insn >> 12) & 0xF) {
            case 0x0:
                va = load_gpr(ctx, ra);
                vb = load_gpr(ctx, rb);
                tmp = tcg_temp_new();
                tcg_gen_addi_i64(tmp, vb, disp12);
                tcg_gen_qemu_st_i64(va, tmp, MMU_PHYS_IDX, MO_LESL | MO_ALIGN);
                break;
            case 0x1:
                va = load_gpr(ctx, ra);
                vb = load_gpr(ctx, rb);
                tmp = tcg_temp_new();
                tcg_gen_addi_i64(tmp, vb, disp12);
                tcg_gen_qemu_st_i64(va, tmp, MMU_PHYS_IDX, MO_LEUQ | MO_ALIGN);
                break;
            case 0x2:
                ret = gen_store_conditional(ctx, ra, rb, disp12,
                                            MMU_PHYS_IDX, MO_LESL | MO_ALIGN);
                break;
            case 0x3:
                ret = gen_store_conditional(ctx, ra, rb, disp12,
                                            MMU_PHYS_IDX, MO_LEUQ | MO_ALIGN);
                break;
            case 0x4:
                goto invalid_opc;
            case 0x5:
                goto invalid_opc;
            case 0x6:
                goto invalid_opc;
            case 0x7:
                goto invalid_opc;
            case 0x8:
                goto invalid_opc;
            case 0x9:
                goto invalid_opc;
            case 0xA:
                goto invalid_opc;
            case 0xB:
                goto invalid_opc;
            case 0xC:
                goto invalid_opc;
            case 0xD:
                goto invalid_opc;
            case 0xE:
                goto invalid_opc;
            case 0xF:
                goto invalid_opc;
            }
            break;
        }
#else
        goto invalid_opc;
#endif
    case 0x20:
        REQUIRE_FEN;
        gen_load_fp(ctx, ra, rb, disp16, gen_ldf);
        break;
    case 0x21:
        REQUIRE_FEN;
        gen_load_fp(ctx, ra, rb, disp16, gen_ldg);
        break;
    case 0x22:
        REQUIRE_FEN;
        gen_load_fp(ctx, ra, rb, disp16, gen_lds);
        break;
    case 0x23:
        REQUIRE_FEN;
        gen_load_fp(ctx, ra, rb, disp16, gen_ldt);
        break;
    case 0x24:
        REQUIRE_FEN;
        gen_store_fp(ctx, ra, rb, disp16, gen_stf);
        break;
    case 0x25:
        REQUIRE_FEN;
        gen_store_fp(ctx, ra, rb, disp16, gen_stg);
        break;
    case 0x26:
        REQUIRE_FEN;
        gen_store_fp(ctx, ra, rb, disp16, gen_sts);
        break;
    case 0x27:
        REQUIRE_FEN;
        gen_store_fp(ctx, ra, rb, disp16, gen_stt);
        break;
    case 0x28:
        gen_load_int(ctx, ra, rb, disp16, MO_LESL, 0, 0);
        break;
    case 0x29:
        gen_load_int(ctx, ra, rb, disp16, MO_LEUQ, 0, 0);
        break;
    case 0x2A:
        gen_load_int(ctx, ra, rb, disp16, MO_LESL | MO_ALIGN, 0, 1);
        break;
    case 0x2B:
        gen_load_int(ctx, ra, rb, disp16, MO_LEUQ | MO_ALIGN, 0, 1);
        break;
    case 0x2C:
        gen_store_int(ctx, ra, rb, disp16, MO_LEUL, 0);
        break;
    case 0x2D:
        gen_store_int(ctx, ra, rb, disp16, MO_LEUQ, 0);
        break;
    case 0x2E:
        ret = gen_store_conditional(ctx, ra, rb, disp16,
                                    ctx->mem_idx, MO_LESL | MO_ALIGN);
        break;
    case 0x2F:
        ret = gen_store_conditional(ctx, ra, rb, disp16,
                                    ctx->mem_idx, MO_LEUQ | MO_ALIGN);
        break;
    case 0x30:
        ret = gen_bdirect(ctx, ra, disp21);
        break;
    case 0x31:
        REQUIRE_FEN;
        ret = gen_fbcond(ctx, TCG_COND_EQ, ra, disp21);
        break;
    case 0x32:
        REQUIRE_FEN;
        ret = gen_fbcond(ctx, TCG_COND_LT, ra, disp21);
        break;
    case 0x33:
        REQUIRE_FEN;
        ret = gen_fbcond(ctx, TCG_COND_LE, ra, disp21);
        break;
    case 0x34:
        ret = gen_bdirect(ctx, ra, disp21);
        break;
    case 0x35:
        REQUIRE_FEN;
        ret = gen_fbcond(ctx, TCG_COND_NE, ra, disp21);
        break;
    case 0x36:
        REQUIRE_FEN;
        ret = gen_fbcond(ctx, TCG_COND_GE, ra, disp21);
        break;
    case 0x37:
        REQUIRE_FEN;
        ret = gen_fbcond(ctx, TCG_COND_GT, ra, disp21);
        break;
    case 0x38:
        ret = gen_bcond(ctx, TCG_COND_TSTEQ, ra, disp21);
        break;
    case 0x39:
        ret = gen_bcond(ctx, TCG_COND_EQ, ra, disp21);
        break;
    case 0x3A:
        ret = gen_bcond(ctx, TCG_COND_LT, ra, disp21);
        break;
    case 0x3B:
        ret = gen_bcond(ctx, TCG_COND_LE, ra, disp21);
        break;
    case 0x3C:
        ret = gen_bcond(ctx, TCG_COND_TSTNE, ra, disp21);
        break;
    case 0x3D:
        ret = gen_bcond(ctx, TCG_COND_NE, ra, disp21);
        break;
    case 0x3E:
        ret = gen_bcond(ctx, TCG_COND_GE, ra, disp21);
        break;
    case 0x3F:
        ret = gen_bcond(ctx, TCG_COND_GT, ra, disp21);
        break;
    invalid_opc:
        ret = gen_invalid(ctx);
        break;
    raise_fen:
        ret = gen_excp(ctx, EXCP_FEN, 0);
        break;
    }

    return ret;
}

static void alpha_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    CPUAlphaState *env = cpu_env(cpu);
    int64_t bound;

    ctx->tbflags = ctx->base.tb->flags;
    ctx->mem_idx = alpha_env_mmu_index(env);
    ctx->pcrel = ctx->base.tb->cflags & CF_PCREL;
    ctx->implver = env->implver;
    ctx->amask = env->amask;

#ifdef CONFIG_USER_ONLY
    ctx->ir = cpu_std_ir;
    ctx->unalign = (ctx->tbflags & TB_FLAG_UNALIGN ? MO_UNALN : MO_ALIGN);
#else
    ctx->ir = (ctx->tbflags & ENV_FLAG_PAL_MODE ? cpu_pal_ir : cpu_std_ir);
#endif

    ctx->tb_rm = -1;
    ctx->tb_ftz = -1;

    ctx->zero = NULL;
    ctx->sink = NULL;

    bound = -(ctx->base.pc_first | TARGET_PAGE_MASK) / 4;
    ctx->base.max_insns = MIN(ctx->base.max_insns, bound);
}

static void alpha_tr_tb_start(DisasContextBase *db, CPUState *cpu)
{
}

static void alpha_tr_insn_start(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    if (ctx->pcrel) {
        tcg_gen_insn_start(dcbase->pc_next & ~TARGET_PAGE_MASK);
    } else {
        tcg_gen_insn_start(dcbase->pc_next);
    }
}

static void alpha_tr_translate_insn(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    uint32_t insn = translator_ldl(cpu_env(cpu), &ctx->base,
                                   ctx->base.pc_next);

    ctx->base.pc_next += 4;
    ctx->base.is_jmp = translate_one(ctx, insn);

    free_context_temps(ctx);
}

static void alpha_tr_tb_stop(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    switch (ctx->base.is_jmp) {
    case DISAS_NORETURN:
        break;
    case DISAS_TOO_MANY:
        gen_goto_tb(ctx, 0, 0);
        break;
    case DISAS_PC_STALE:
        gen_pc_disp(ctx, cpu_pc, 0);
        /* FALLTHRU */
    case DISAS_PC_UPDATED:
        tcg_gen_lookup_and_goto_ptr();
        break;
    case DISAS_PC_UPDATED_NOCHAIN:
        tcg_gen_exit_tb(NULL, 0);
        break;
    default:
        g_assert_not_reached();
    }
}

static const TranslatorOps alpha_tr_ops = {
    .init_disas_context = alpha_tr_init_disas_context,
    .tb_start           = alpha_tr_tb_start,
    .insn_start         = alpha_tr_insn_start,
    .translate_insn     = alpha_tr_translate_insn,
    .tb_stop            = alpha_tr_tb_stop,
};

void alpha_translate_code(CPUState *cpu, TranslationBlock *tb,
                          int *max_insns, vaddr pc, void *host_pc)
{
    DisasContext dc;
    translator_loop(cpu, tb, max_insns, pc, host_pc, &alpha_tr_ops, &dc.base);
}
