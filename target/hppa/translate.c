/*
 * HPPA emulation cpu translation for qemu.
 *
 * Copyright (c) 2016 Richard Henderson <rth@twiddle.net>
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
#include "qemu/host-utils.h"
#include "exec/page-protection.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/translator.h"
#include "exec/translation-block.h"
#include "exec/target_page.h"
#include "exec/log.h"

#define HELPER_H "helper.h"
#include "exec/helper-info.c.inc"
#undef  HELPER_H

#undef tcg_temp_new

typedef struct DisasCond {
    TCGCond c;
    TCGv_i64 a0, a1;
} DisasCond;

typedef struct DisasIAQE {
    TCGv_i64 space;
    TCGv_i64 base;
    int64_t disp;
} DisasIAQE;

typedef struct DisasDelayException {
    struct DisasDelayException *next;
    TCGLabel *lab;
    uint32_t insn;
    bool set_iir;
    int8_t set_n;
    uint8_t excp;
    DisasIAQE iaq_f, iaq_b;
} DisasDelayException;

typedef struct DisasContext {
    DisasContextBase base;
    CPUState *cs;

    DisasIAQE iaq_f, iaq_b;
    DisasIAQE iaq_j, *iaq_n;

    uint64_t iaoq_first;
    uint64_t gva_offset_mask;

    DisasCond null_cond;
    TCGLabel *null_lab;

    DisasDelayException *delay_excp_list;
    TCGv_i64 zero;

    uint32_t insn;
    uint32_t tb_flags;
    int mmu_idx;
    int privilege;
    uint32_t psw_xb;
    bool psw_n_nonzero;
    bool psw_b_next;
    bool is_pa20;
    bool insn_start_updated;

#ifdef CONFIG_USER_ONLY
    MemOp unalign;
#endif
} DisasContext;

#ifdef CONFIG_USER_ONLY
#define UNALIGN(C)       (C)->unalign
#define MMU_DISABLED(C)  false
#else
#define UNALIGN(C)       MO_ALIGN
#define MMU_DISABLED(C)  MMU_IDX_MMU_DISABLED((C)->mmu_idx)
#endif

static inline MemOp mo_endian(DisasContext *ctx)
{
   return MO_BE;
}

static int expand_sm_imm(DisasContext *ctx, int val)
{
    if (ctx->is_pa20) {
        if (val & PSW_SM_W) {
            val |= PSW_W;
        }
        val &= ~(PSW_SM_W | PSW_SM_E | PSW_G);
    } else {
        val &= ~(PSW_SM_W | PSW_SM_E | PSW_O);
    }
    return val;
}

static int expand_sr3x(DisasContext *ctx, int val)
{
    return ~val;
}

static int ma_to_m(DisasContext *ctx, int val)
{
    return val & 2 ? (val & 1 ? -1 : 1) : 0;
}

static int pos_to_m(DisasContext *ctx, int val)
{
    return val ? 1 : -1;
}

static int neg_to_m(DisasContext *ctx, int val)
{
    return val ? -1 : 1;
}

static int expand_shl2(DisasContext *ctx, int val)
{
    return val << 2;
}

static int expand_shl11(DisasContext *ctx, int val)
{
    return val << 11;
}

static int assemble_6(DisasContext *ctx, int val)
{
    return (val ^ 31) + 1;
}

static int expand_11a(DisasContext *ctx, int val)
{
    int im10a = extract32(val, 1, 10);
    int s = extract32(val, 11, 2);
    int i = (-(val & 1) << 13) | (im10a << 3);

    if (ctx->tb_flags & PSW_W) {
        i ^= s << 13;
    }
    return i;
}

static int expand_12a(DisasContext *ctx, int val)
{
    int im11a = extract32(val, 1, 11);
    int s = extract32(val, 12, 2);
    int i = (-(val & 1) << 13) | (im11a << 2);

    if (ctx->tb_flags & PSW_W) {
        i ^= s << 13;
    }
    return i;
}

static int expand_16(DisasContext *ctx, int val)
{
    int s = extract32(val, 14, 2);
    int i = (-(val & 1) << 13) | extract32(val, 1, 13);

    if (ctx->tb_flags & PSW_W) {
        i ^= s << 13;
    }
    return i;
}

static int sp0_if_wide(DisasContext *ctx, int sp)
{
    return ctx->tb_flags & PSW_W ? 0 : sp;
}

static int cmpbid_c(DisasContext *ctx, int val)
{
    return val ? val : 4;
}

static int pa20_d(DisasContext *ctx, int val)
{
    return ctx->is_pa20 & val;
}

#include "decode-insns.c.inc"

#define DISAS_IAQ_N_UPDATED  DISAS_TARGET_0

#define DISAS_IAQ_N_STALE    DISAS_TARGET_1

#define DISAS_IAQ_N_STALE_EXIT      DISAS_TARGET_2
#define DISAS_EXIT                  DISAS_TARGET_3

static TCGv_i64 cpu_gr[32];
static TCGv_i64 cpu_sr[4];
static TCGv_i64 cpu_srH;
static TCGv_i64 cpu_iaoq_f;
static TCGv_i64 cpu_iaoq_b;
static TCGv_i64 cpu_iasq_f;
static TCGv_i64 cpu_iasq_b;
static TCGv_i64 cpu_sar;
static TCGv_i64 cpu_psw_n;
static TCGv_i64 cpu_psw_v;
static TCGv_i64 cpu_psw_cb;
static TCGv_i64 cpu_psw_cb_msb;
static TCGv_i32 cpu_psw_xb;

void hppa_translate_init(void)
{
#define DEF_VAR(V)  { &cpu_##V, #V, offsetof(CPUHPPAState, V) }

    typedef struct { TCGv_i64 *var; const char *name; int ofs; } GlobalVar;
    static const GlobalVar vars[] = {
        { &cpu_sar, "sar", offsetof(CPUHPPAState, cr[CR_SAR]) },
        DEF_VAR(psw_n),
        DEF_VAR(psw_v),
        DEF_VAR(psw_cb),
        DEF_VAR(psw_cb_msb),
        DEF_VAR(iaoq_f),
        DEF_VAR(iaoq_b),
    };

#undef DEF_VAR

    static const char gr_names[32][4] = {
        "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
        "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15",
        "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
        "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31"
    };
    static const char sr_names[5][4] = {
        "sr0", "sr1", "sr2", "sr3", "srH"
    };

    int i;

    cpu_gr[0] = NULL;
    for (i = 1; i < 32; i++) {
        cpu_gr[i] = tcg_global_mem_new(tcg_env,
                                       offsetof(CPUHPPAState, gr[i]),
                                       gr_names[i]);
    }
    for (i = 0; i < 4; i++) {
        cpu_sr[i] = tcg_global_mem_new_i64(tcg_env,
                                           offsetof(CPUHPPAState, sr[i]),
                                           sr_names[i]);
    }
    cpu_srH = tcg_global_mem_new_i64(tcg_env,
                                     offsetof(CPUHPPAState, sr[4]),
                                     sr_names[4]);

    for (i = 0; i < ARRAY_SIZE(vars); ++i) {
        const GlobalVar *v = &vars[i];
        *v->var = tcg_global_mem_new(tcg_env, v->ofs, v->name);
    }

    cpu_psw_xb = tcg_global_mem_new_i32(tcg_env,
                                        offsetof(CPUHPPAState, psw_xb),
                                        "psw_xb");
    cpu_iasq_f = tcg_global_mem_new_i64(tcg_env,
                                        offsetof(CPUHPPAState, iasq_f),
                                        "iasq_f");
    cpu_iasq_b = tcg_global_mem_new_i64(tcg_env,
                                        offsetof(CPUHPPAState, iasq_b),
                                        "iasq_b");
}

static void set_insn_breg(DisasContext *ctx, int breg)
{
    assert(!ctx->insn_start_updated);
    ctx->insn_start_updated = true;
    tcg_set_insn_start_param(ctx->base.insn_start, 2, breg);
}

static DisasCond cond_make_f(void)
{
    return (DisasCond){
        .c = TCG_COND_NEVER,
        .a0 = NULL,
        .a1 = NULL,
    };
}

static DisasCond cond_make_t(void)
{
    return (DisasCond){
        .c = TCG_COND_ALWAYS,
        .a0 = NULL,
        .a1 = NULL,
    };
}

static DisasCond cond_make_n(void)
{
    return (DisasCond){
        .c = TCG_COND_NE,
        .a0 = cpu_psw_n,
        .a1 = tcg_constant_i64(0)
    };
}

static DisasCond cond_make_tt(TCGCond c, TCGv_i64 a0, TCGv_i64 a1)
{
    assert (c != TCG_COND_NEVER && c != TCG_COND_ALWAYS);
    return (DisasCond){ .c = c, .a0 = a0, .a1 = a1 };
}

static DisasCond cond_make_ti(TCGCond c, TCGv_i64 a0, uint64_t imm)
{
    return cond_make_tt(c, a0, tcg_constant_i64(imm));
}

static DisasCond cond_make_vi(TCGCond c, TCGv_i64 a0, uint64_t imm)
{
    TCGv_i64 tmp = tcg_temp_new_i64();
    tcg_gen_mov_i64(tmp, a0);
    return cond_make_ti(c, tmp, imm);
}

static DisasCond cond_make_vv(TCGCond c, TCGv_i64 a0, TCGv_i64 a1)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();

    tcg_gen_mov_i64(t0, a0);
    tcg_gen_mov_i64(t1, a1);
    return cond_make_tt(c, t0, t1);
}

static TCGv_i64 load_gpr(DisasContext *ctx, unsigned reg)
{
    if (reg == 0) {
        return ctx->zero;
    } else {
        return cpu_gr[reg];
    }
}

static TCGv_i64 dest_gpr(DisasContext *ctx, unsigned reg)
{
    if (reg == 0 || ctx->null_cond.c != TCG_COND_NEVER) {
        return tcg_temp_new_i64();
    } else {
        return cpu_gr[reg];
    }
}

static void save_or_nullify(DisasContext *ctx, TCGv_i64 dest, TCGv_i64 t)
{
    if (ctx->null_cond.c != TCG_COND_NEVER) {
        tcg_gen_movcond_i64(ctx->null_cond.c, dest, ctx->null_cond.a0,
                            ctx->null_cond.a1, dest, t);
    } else {
        tcg_gen_mov_i64(dest, t);
    }
}

static void save_gpr(DisasContext *ctx, unsigned reg, TCGv_i64 t)
{
    if (reg != 0) {
        save_or_nullify(ctx, cpu_gr[reg], t);
    }
}

#if HOST_BIG_ENDIAN
# define HI_OFS  0
# define LO_OFS  4
#else
# define HI_OFS  4
# define LO_OFS  0
#endif

static TCGv_i32 load_frw_i32(unsigned rt)
{
    TCGv_i32 ret = tcg_temp_new_i32();
    tcg_gen_ld_i32(ret, tcg_env,
                   offsetof(CPUHPPAState, fr[rt & 31])
                   + (rt & 32 ? LO_OFS : HI_OFS));
    return ret;
}

static TCGv_i32 load_frw0_i32(unsigned rt)
{
    if (rt == 0) {
        TCGv_i32 ret = tcg_temp_new_i32();
        tcg_gen_movi_i32(ret, 0);
        return ret;
    } else {
        return load_frw_i32(rt);
    }
}

static TCGv_i64 load_frw0_i64(unsigned rt)
{
    TCGv_i64 ret = tcg_temp_new_i64();
    if (rt == 0) {
        tcg_gen_movi_i64(ret, 0);
    } else {
        tcg_gen_ld32u_i64(ret, tcg_env,
                          offsetof(CPUHPPAState, fr[rt & 31])
                          + (rt & 32 ? LO_OFS : HI_OFS));
    }
    return ret;
}

static void save_frw_i32(unsigned rt, TCGv_i32 val)
{
    tcg_gen_st_i32(val, tcg_env,
                   offsetof(CPUHPPAState, fr[rt & 31])
                   + (rt & 32 ? LO_OFS : HI_OFS));
}

#undef HI_OFS
#undef LO_OFS

static TCGv_i64 load_frd(unsigned rt)
{
    TCGv_i64 ret = tcg_temp_new_i64();
    tcg_gen_ld_i64(ret, tcg_env, offsetof(CPUHPPAState, fr[rt]));
    return ret;
}

static TCGv_i64 load_frd0(unsigned rt)
{
    if (rt == 0) {
        TCGv_i64 ret = tcg_temp_new_i64();
        tcg_gen_movi_i64(ret, 0);
        return ret;
    } else {
        return load_frd(rt);
    }
}

static void save_frd(unsigned rt, TCGv_i64 val)
{
    tcg_gen_st_i64(val, tcg_env, offsetof(CPUHPPAState, fr[rt]));
}

static void load_spr(DisasContext *ctx, TCGv_i64 dest, unsigned reg)
{
#ifdef CONFIG_USER_ONLY
    tcg_gen_movi_i64(dest, 0);
#else
    if (reg < 4) {
        tcg_gen_mov_i64(dest, cpu_sr[reg]);
    } else if (ctx->tb_flags & TB_FLAG_SR_SAME) {
        tcg_gen_mov_i64(dest, cpu_srH);
    } else {
        tcg_gen_ld_i64(dest, tcg_env, offsetof(CPUHPPAState, sr[reg]));
    }
#endif
}

static void store_psw_xb(DisasContext *ctx, uint32_t xb)
{
    tcg_debug_assert(xb == 0 || xb == PSW_B);
    if (ctx->psw_xb != xb) {
        tcg_gen_movi_i32(cpu_psw_xb, xb);
    }
}

static void set_psw_xb(DisasContext *ctx, uint32_t xb)
{
    store_psw_xb(ctx, xb);
    ctx->psw_xb = xb;
}

static void nullify_over(DisasContext *ctx)
{
    if (ctx->null_cond.c != TCG_COND_NEVER) {
        assert(ctx->null_cond.c != TCG_COND_ALWAYS);

        ctx->null_lab = gen_new_label();

        if (ctx->null_cond.a0 == cpu_psw_n) {
            ctx->null_cond.a0 = tcg_temp_new_i64();
            tcg_gen_mov_i64(ctx->null_cond.a0, cpu_psw_n);
        }
        if (ctx->psw_n_nonzero) {
            ctx->psw_n_nonzero = false;
            tcg_gen_movi_i64(cpu_psw_n, 0);
        }

        tcg_gen_brcond_i64(ctx->null_cond.c, ctx->null_cond.a0,
                           ctx->null_cond.a1, ctx->null_lab);
        ctx->null_cond = cond_make_f();
    }
}

static void nullify_save(DisasContext *ctx)
{
    if (ctx->null_cond.c == TCG_COND_NEVER) {
        if (ctx->psw_n_nonzero) {
            tcg_gen_movi_i64(cpu_psw_n, 0);
        }
        return;
    }
    if (ctx->null_cond.a0 != cpu_psw_n) {
        tcg_gen_setcond_i64(ctx->null_cond.c, cpu_psw_n,
                            ctx->null_cond.a0, ctx->null_cond.a1);
        ctx->psw_n_nonzero = true;
    }
    ctx->null_cond = cond_make_f();
}

static void nullify_set(DisasContext *ctx, bool x)
{
    if (ctx->psw_n_nonzero || x) {
        tcg_gen_movi_i64(cpu_psw_n, x);
    }
}

static bool nullify_end(DisasContext *ctx)
{
    TCGLabel *null_lab = ctx->null_lab;
    DisasJumpType status = ctx->base.is_jmp;

    assert(status != DISAS_IAQ_N_UPDATED);
    assert(!ctx->psw_b_next);

    if (likely(null_lab == NULL)) {
        return true;
    }
    ctx->null_lab = NULL;

    if (likely(ctx->null_cond.c == TCG_COND_NEVER)) {
        gen_set_label(null_lab);
    } else {
        nullify_save(ctx);
        gen_set_label(null_lab);
        ctx->null_cond = cond_make_n();
    }
    if (status == DISAS_NORETURN) {
        ctx->base.is_jmp = DISAS_NEXT;
    }
    return true;
}

static bool iaqe_variable(const DisasIAQE *e)
{
    return e->base || e->space;
}

static DisasIAQE iaqe_incr(const DisasIAQE *e, int64_t disp)
{
    return (DisasIAQE){
        .space = e->space,
        .base = e->base,
        .disp = e->disp + disp,
    };
}

static DisasIAQE iaqe_branchi(DisasContext *ctx, int64_t disp)
{
    return (DisasIAQE){
        .space = ctx->iaq_b.space,
        .disp = ctx->iaq_f.disp + 8 + disp,
    };
}

static DisasIAQE iaqe_next_absv(DisasContext *ctx, TCGv_i64 var)
{
    return (DisasIAQE){
        .space = ctx->iaq_b.space,
        .base = var,
    };
}

static void copy_iaoq_entry(DisasContext *ctx, TCGv_i64 dest,
                            const DisasIAQE *src)
{
    tcg_gen_addi_i64(dest, src->base ? : cpu_iaoq_f, src->disp);
}

static void install_iaq_entries(DisasContext *ctx, const DisasIAQE *f,
                                const DisasIAQE *b)
{
    DisasIAQE b_next;

    if (b == NULL) {
        b_next = iaqe_incr(f, 4);
        b = &b_next;
    }

    if (f->base != cpu_iaoq_b) {
        copy_iaoq_entry(ctx, cpu_iaoq_b, b);
        copy_iaoq_entry(ctx, cpu_iaoq_f, f);
    } else if (f->base == b->base) {
        copy_iaoq_entry(ctx, cpu_iaoq_f, f);
        tcg_gen_addi_i64(cpu_iaoq_b, cpu_iaoq_f, b->disp - f->disp);
    } else {
        TCGv_i64 tmp = tcg_temp_new_i64();
        copy_iaoq_entry(ctx, tmp, b);
        copy_iaoq_entry(ctx, cpu_iaoq_f, f);
        tcg_gen_mov_i64(cpu_iaoq_b, tmp);
    }

    if (f->space) {
        tcg_gen_mov_i64(cpu_iasq_f, f->space);
    }
    if (b->space || f->space) {
        tcg_gen_mov_i64(cpu_iasq_b, b->space ? : f->space);
    }
}

static void install_link(DisasContext *ctx, unsigned link, bool with_sr0)
{
    tcg_debug_assert(ctx->null_cond.c == TCG_COND_NEVER);
    if (!link) {
        return;
    }
    DisasIAQE next = iaqe_incr(&ctx->iaq_b, 4);
    copy_iaoq_entry(ctx, cpu_gr[link], &next);
#ifndef CONFIG_USER_ONLY
    if (with_sr0) {
        tcg_gen_mov_i64(cpu_sr[0], cpu_iasq_b);
    }
#endif
}

static void gen_excp_1(int exception)
{
    gen_helper_excp(tcg_env, tcg_constant_i32(exception));
}

static void gen_excp(DisasContext *ctx, int exception)
{
    install_iaq_entries(ctx, &ctx->iaq_f, &ctx->iaq_b);
    nullify_save(ctx);
    gen_excp_1(exception);
    ctx->base.is_jmp = DISAS_NORETURN;
}

static DisasDelayException *delay_excp(DisasContext *ctx, uint8_t excp)
{
    DisasDelayException *e = tcg_malloc(sizeof(DisasDelayException));

    memset(e, 0, sizeof(*e));
    e->next = ctx->delay_excp_list;
    ctx->delay_excp_list = e;

    e->lab = gen_new_label();
    e->insn = ctx->insn;
    e->set_iir = true;
    e->set_n = ctx->psw_n_nonzero ? 0 : -1;
    e->excp = excp;
    e->iaq_f = ctx->iaq_f;
    e->iaq_b = ctx->iaq_b;

    return e;
}

static bool gen_excp_iir(DisasContext *ctx, int exc)
{
    if (ctx->null_cond.c == TCG_COND_NEVER) {
        tcg_gen_st_i64(tcg_constant_i64(ctx->insn),
                       tcg_env, offsetof(CPUHPPAState, cr[CR_IIR]));
        gen_excp(ctx, exc);
    } else {
        DisasDelayException *e = delay_excp(ctx, exc);
        tcg_gen_brcond_i64(tcg_invert_cond(ctx->null_cond.c),
                           ctx->null_cond.a0, ctx->null_cond.a1, e->lab);
        ctx->null_cond = cond_make_f();
    }
    return true;
}

static bool gen_illegal(DisasContext *ctx)
{
    return gen_excp_iir(ctx, EXCP_ILL);
}

#ifdef CONFIG_USER_ONLY
#define CHECK_MOST_PRIVILEGED(EXCP) \
    return gen_excp_iir(ctx, EXCP)
#else
#define CHECK_MOST_PRIVILEGED(EXCP) \
    do {                                     \
        if (ctx->privilege != 0) {           \
            return gen_excp_iir(ctx, EXCP);  \
        }                                    \
    } while (0)
#endif

static bool use_goto_tb(DisasContext *ctx, const DisasIAQE *f,
                        const DisasIAQE *b)
{
    return (!iaqe_variable(f) &&
            (b == NULL || !iaqe_variable(b)) &&
            translator_use_goto_tb(&ctx->base, ctx->iaoq_first + f->disp));
}

static bool use_nullify_skip(DisasContext *ctx)
{
    return (!(tb_cflags(ctx->base.tb) & CF_BP_PAGE)
            && !iaqe_variable(&ctx->iaq_b)
            && (((ctx->iaoq_first + ctx->iaq_b.disp) ^ ctx->iaoq_first)
                & TARGET_PAGE_MASK) == 0);
}

static void gen_goto_tb(DisasContext *ctx, int which,
                        const DisasIAQE *f, const DisasIAQE *b)
{
    install_iaq_entries(ctx, f, b);
    if (use_goto_tb(ctx, f, b)) {
        tcg_gen_goto_tb(which);
        tcg_gen_exit_tb(ctx->base.tb, which);
    } else {
        tcg_gen_lookup_and_goto_ptr();
    }
}

static bool cond_need_sv(int c)
{
    return c == 2 || c == 3 || c == 6;
}

static bool cond_need_cb(int c)
{
    return c == 4 || c == 5;
}

static DisasCond do_cond(DisasContext *ctx, unsigned cf, bool d,
                         TCGv_i64 res, TCGv_i64 uv, TCGv_i64 sv)
{
    TCGCond sign_cond, zero_cond;
    uint64_t sign_imm, zero_imm;
    DisasCond cond;
    TCGv_i64 tmp;

    if (d) {
        sign_imm = 0;
        sign_cond = TCG_COND_LT;
        zero_imm = 0;
        zero_cond = TCG_COND_EQ;
    } else {
        sign_imm = 1ull << 31;
        sign_cond = TCG_COND_TSTNE;
        zero_imm = UINT32_MAX;
        zero_cond = TCG_COND_TSTEQ;
    }

    switch (cf >> 1) {
    case 0:
        cond = cond_make_f();
        break;
    case 1:
        cond = cond_make_vi(zero_cond, res, zero_imm);
        break;
    case 2:
        tmp = tcg_temp_new_i64();
        tcg_gen_xor_i64(tmp, res, sv);
        cond = cond_make_ti(sign_cond, tmp, sign_imm);
        break;
    case 3:
        tmp = tcg_temp_new_i64();
        tcg_gen_xor_i64(tmp, res, sv);
        tcg_gen_movcond_i64(sign_cond, tmp,
                            tmp, tcg_constant_i64(sign_imm),
                            ctx->zero, res);
        cond = cond_make_ti(zero_cond, tmp, zero_imm);
        break;
    case 4:
        cond = cond_make_vi(TCG_COND_EQ, uv, 0);
        break;
    case 5:
        tmp = tcg_temp_new_i64();
        tcg_gen_movcond_i64(TCG_COND_EQ, tmp, uv, ctx->zero, ctx->zero, res);
        cond = cond_make_ti(zero_cond, tmp, zero_imm);
        break;
    case 6:
        cond = cond_make_vi(sign_cond, sv, sign_imm);
        break;
    case 7:
        cond = cond_make_vi(TCG_COND_TSTNE, res, 1);
        break;
    default:
        g_assert_not_reached();
    }
    if (cf & 1) {
        cond.c = tcg_invert_cond(cond.c);
    }

    return cond;
}

static DisasCond do_sub_cond(DisasContext *ctx, unsigned cf, bool d,
                             TCGv_i64 res, TCGv_i64 in1,
                             TCGv_i64 in2, TCGv_i64 sv)
{
    TCGCond tc;
    bool ext_uns;

    switch (cf >> 1) {
    case 1:
        tc = TCG_COND_EQ;
        ext_uns = true;
        break;
    case 2:
        tc = TCG_COND_LT;
        ext_uns = false;
        break;
    case 3:
        tc = TCG_COND_LE;
        ext_uns = false;
        break;
    case 4:
        tc = TCG_COND_LTU;
        ext_uns = true;
        break;
    case 5:
        tc = TCG_COND_LEU;
        ext_uns = true;
        break;
    default:
        return do_cond(ctx, cf, d, res, NULL, sv);
    }

    if (cf & 1) {
        tc = tcg_invert_cond(tc);
    }
    if (!d) {
        TCGv_i64 t1 = tcg_temp_new_i64();
        TCGv_i64 t2 = tcg_temp_new_i64();

        if (ext_uns) {
            tcg_gen_ext32u_i64(t1, in1);
            tcg_gen_ext32u_i64(t2, in2);
        } else {
            tcg_gen_ext32s_i64(t1, in1);
            tcg_gen_ext32s_i64(t2, in2);
        }
        return cond_make_tt(tc, t1, t2);
    }
    return cond_make_vv(tc, in1, in2);
}

static DisasCond do_log_cond(DisasContext *ctx, unsigned cf, bool d,
                             TCGv_i64 res)
{
    TCGCond tc;
    uint64_t imm;

    switch (cf >> 1) {
    case 0:
    case 4:
    case 5:
    case 6:
        return cf & 1 ? cond_make_t() : cond_make_f();
    case 1:
        tc = d ? TCG_COND_EQ : TCG_COND_TSTEQ;
        imm = d ? 0 : UINT32_MAX;
        break;
    case 2:
        tc = d ? TCG_COND_LT : TCG_COND_TSTNE;
        imm = d ? 0 : 1ull << 31;
        break;
    case 3:
        tc = cf & 1 ? TCG_COND_GT : TCG_COND_LE;
        if (!d) {
            TCGv_i64 tmp = tcg_temp_new_i64();
            tcg_gen_ext32s_i64(tmp, res);
            return cond_make_ti(tc, tmp, 0);
        }
        return cond_make_vi(tc, res, 0);
    case 7:
        tc = TCG_COND_TSTNE;
        imm = 1;
        break;
    default:
        g_assert_not_reached();
    }
    if (cf & 1) {
        tc = tcg_invert_cond(tc);
    }
    return cond_make_vi(tc, res, imm);
}

static DisasCond do_sed_cond(DisasContext *ctx, unsigned orig, bool d,
                             TCGv_i64 res)
{
    unsigned c, f;

    c = orig & 3;
    if (c == 3) {
        c = 7;
    }
    f = (orig & 4) / 4;

    return do_log_cond(ctx, c * 2 + f, d, res);
}

static DisasCond do_unit_zero_cond(unsigned cf, bool d, TCGv_i64 res)
{
    TCGv_i64 tmp;
    uint64_t d_repl = d ? 0x0000000100000001ull : 1;
    uint64_t ones = 0, sgns = 0;

    switch (cf >> 1) {
    case 1:
        if (d) {
            ones = d_repl;
            sgns = d_repl << 31;
        }
        break;
    case 2:
        ones = d_repl * 0x01010101u;
        sgns = ones << 7;
        break;
    case 3:
        ones = d_repl * 0x00010001u;
        sgns = ones << 15;
        break;
    }
    if (ones == 0) {
        return cf & 1 ? cond_make_t() : cond_make_f();
    }

    tmp = tcg_temp_new_i64();
    tcg_gen_subi_i64(tmp, res, ones);
    tcg_gen_andc_i64(tmp, tmp, res);

    return cond_make_ti(cf & 1 ? TCG_COND_TSTEQ : TCG_COND_TSTNE, tmp, sgns);
}

static TCGv_i64 get_carry(DisasContext *ctx, bool d,
                          TCGv_i64 cb, TCGv_i64 cb_msb)
{
    if (!d) {
        TCGv_i64 t = tcg_temp_new_i64();
        tcg_gen_extract_i64(t, cb, 32, 1);
        return t;
    }
    return cb_msb;
}

static TCGv_i64 get_psw_carry(DisasContext *ctx, bool d)
{
    return get_carry(ctx, d, cpu_psw_cb, cpu_psw_cb_msb);
}

static TCGv_i64 do_add_sv(DisasContext *ctx, TCGv_i64 res,
                          TCGv_i64 in1, TCGv_i64 in2,
                          TCGv_i64 orig_in1, int shift, bool d)
{
    TCGv_i64 sv = tcg_temp_new_i64();
    TCGv_i64 tmp = tcg_temp_new_i64();

    tcg_gen_xor_i64(sv, res, in1);
    tcg_gen_xor_i64(tmp, in1, in2);
    tcg_gen_andc_i64(sv, sv, tmp);

    switch (shift) {
    case 0:
        break;
    case 1:
        tcg_gen_add_i64(tmp, orig_in1, orig_in1);
        tcg_gen_xor_i64(tmp, tmp, orig_in1);
        tcg_gen_or_i64(sv, sv, tmp);
        break;
    default:
        {
            int sign_bit = d ? 63 : 31;

            tcg_gen_sextract_i64(tmp, orig_in1, sign_bit, 1);
            tcg_gen_xor_i64(tmp, tmp, orig_in1);
            tcg_gen_extract_i64(tmp, tmp, sign_bit - shift, shift);
            tcg_gen_movcond_i64(TCG_COND_NE, sv, tmp, ctx->zero,
                                tcg_constant_i64(-1), sv);
        }
    }
    return sv;
}

static TCGv_i64 do_add_uv(DisasContext *ctx, TCGv_i64 cb, TCGv_i64 cb_msb,
                          TCGv_i64 in1, int shift, bool d)
{
    if (shift == 0) {
        return get_carry(ctx, d, cb, cb_msb);
    } else {
        TCGv_i64 tmp = tcg_temp_new_i64();
        tcg_gen_extract_i64(tmp, in1, (d ? 63 : 31) - shift, shift);
        tcg_gen_or_i64(tmp, tmp, get_carry(ctx, d, cb, cb_msb));
        return tmp;
    }
}

static TCGv_i64 do_sub_sv(DisasContext *ctx, TCGv_i64 res,
                          TCGv_i64 in1, TCGv_i64 in2)
{
    TCGv_i64 sv = tcg_temp_new_i64();
    TCGv_i64 tmp = tcg_temp_new_i64();

    tcg_gen_xor_i64(sv, res, in1);
    tcg_gen_xor_i64(tmp, in1, in2);
    tcg_gen_and_i64(sv, sv, tmp);

    return sv;
}

static void gen_tc(DisasContext *ctx, DisasCond *cond)
{
    DisasDelayException *e;

    switch (cond->c) {
    case TCG_COND_NEVER:
        break;
    case TCG_COND_ALWAYS:
        gen_excp_iir(ctx, EXCP_COND);
        break;
    default:
        e = delay_excp(ctx, EXCP_COND);
        tcg_gen_brcond_i64(cond->c, cond->a0, cond->a1, e->lab);
        *cond = cond_make_f();
        break;
    }
}

static void gen_tsv(DisasContext *ctx, TCGv_i64 *sv, bool d)
{
    DisasCond cond = do_cond(ctx, 12, d, NULL, NULL, *sv);
    DisasDelayException *e = delay_excp(ctx, EXCP_OVERFLOW);

    tcg_gen_brcond_i64(cond.c, cond.a0, cond.a1, e->lab);

    *sv = tcg_constant_i64(0);
}

static void do_add(DisasContext *ctx, unsigned rt, TCGv_i64 orig_in1,
                   TCGv_i64 in2, unsigned shift, bool is_l,
                   bool is_tsv, bool is_tc, bool is_c, unsigned cf, bool d)
{
    TCGv_i64 dest, cb, cb_msb, in1, uv, sv, tmp;
    unsigned c = cf >> 1;
    DisasCond cond;

    dest = tcg_temp_new_i64();
    cb = NULL;
    cb_msb = NULL;

    in1 = orig_in1;
    if (shift) {
        tmp = tcg_temp_new_i64();
        tcg_gen_shli_i64(tmp, in1, shift);
        in1 = tmp;
    }

    if (!is_l || cond_need_cb(c)) {
        cb_msb = tcg_temp_new_i64();
        cb = tcg_temp_new_i64();

        if (is_c) {
            tcg_gen_addcio_i64(dest, cb_msb, in1, in2, get_psw_carry(ctx, d));
        } else {
            tcg_gen_add2_i64(dest, cb_msb, in1, ctx->zero, in2, ctx->zero);
        }
        tcg_gen_xor_i64(cb, in1, in2);
        tcg_gen_xor_i64(cb, cb, dest);
    } else {
        tcg_gen_add_i64(dest, in1, in2);
        if (is_c) {
            tcg_gen_add_i64(dest, dest, get_psw_carry(ctx, d));
        }
    }

    sv = NULL;
    if (is_tsv || cond_need_sv(c)) {
        sv = do_add_sv(ctx, dest, in1, in2, orig_in1, shift, d);
        if (is_tsv) {
            gen_tsv(ctx, &sv, d);
        }
    }

    uv = NULL;
    if (cond_need_cb(c)) {
        uv = do_add_uv(ctx, cb, cb_msb, orig_in1, shift, d);
    }

    cond = do_cond(ctx, cf, d, dest, uv, sv);
    if (is_tc) {
        gen_tc(ctx, &cond);
    }

    if (!is_l) {
        save_or_nullify(ctx, cpu_psw_cb, cb);
        save_or_nullify(ctx, cpu_psw_cb_msb, cb_msb);
    }
    save_gpr(ctx, rt, dest);

    ctx->null_cond = cond;
}

static bool do_add_reg(DisasContext *ctx, arg_rrr_cf_d_sh *a,
                       bool is_l, bool is_tsv, bool is_tc, bool is_c)
{
    TCGv_i64 tcg_r1, tcg_r2;

    if (unlikely(is_tc && a->cf == 1)) {
        return gen_excp_iir(ctx, EXCP_COND);
    }
    if (a->cf) {
        nullify_over(ctx);
    }
    tcg_r1 = load_gpr(ctx, a->r1);
    tcg_r2 = load_gpr(ctx, a->r2);
    do_add(ctx, a->t, tcg_r1, tcg_r2, a->sh, is_l,
           is_tsv, is_tc, is_c, a->cf, a->d);
    return nullify_end(ctx);
}

static bool do_add_imm(DisasContext *ctx, arg_rri_cf *a,
                       bool is_tsv, bool is_tc)
{
    TCGv_i64 tcg_im, tcg_r2;

    if (unlikely(is_tc && a->cf == 1)) {
        return gen_excp_iir(ctx, EXCP_COND);
    }
    if (a->cf) {
        nullify_over(ctx);
    }
    tcg_im = tcg_constant_i64(a->i);
    tcg_r2 = load_gpr(ctx, a->r);
    do_add(ctx, a->t, tcg_im, tcg_r2, 0, 0, is_tsv, is_tc, 0, a->cf, false);
    return nullify_end(ctx);
}

static void do_sub(DisasContext *ctx, unsigned rt, TCGv_i64 in1,
                   TCGv_i64 in2, bool is_tsv, bool is_b,
                   bool is_tc, unsigned cf, bool d)
{
    TCGv_i64 dest, sv, cb, cb_msb;
    unsigned c = cf >> 1;
    DisasCond cond;

    dest = tcg_temp_new_i64();
    cb = tcg_temp_new_i64();
    cb_msb = tcg_temp_new_i64();

    if (is_b) {
        tcg_gen_not_i64(cb, in2);
        tcg_gen_addcio_i64(dest, cb_msb, in1, cb, get_psw_carry(ctx, d));
        tcg_gen_xor_i64(cb, cb, in1);
        tcg_gen_xor_i64(cb, cb, dest);
    } else {
        TCGv_i64 one = tcg_constant_i64(1);
        tcg_gen_sub2_i64(dest, cb_msb, in1, one, in2, ctx->zero);
        tcg_gen_eqv_i64(cb, in1, in2);
        tcg_gen_xor_i64(cb, cb, dest);
    }

    sv = NULL;
    if (is_tsv || cond_need_sv(c)) {
        sv = do_sub_sv(ctx, dest, in1, in2);
        if (is_tsv) {
            gen_tsv(ctx, &sv, d);
        }
    }

    if (!is_b) {
        cond = do_sub_cond(ctx, cf, d, dest, in1, in2, sv);
    } else {
        cond = do_cond(ctx, cf, d, dest, get_carry(ctx, d, cb, cb_msb), sv);
    }

    if (is_tc) {
        gen_tc(ctx, &cond);
    }

    save_or_nullify(ctx, cpu_psw_cb, cb);
    save_or_nullify(ctx, cpu_psw_cb_msb, cb_msb);
    save_gpr(ctx, rt, dest);

    ctx->null_cond = cond;
}

static bool do_sub_reg(DisasContext *ctx, arg_rrr_cf_d *a,
                       bool is_tsv, bool is_b, bool is_tc)
{
    TCGv_i64 tcg_r1, tcg_r2;

    if (a->cf) {
        nullify_over(ctx);
    }
    tcg_r1 = load_gpr(ctx, a->r1);
    tcg_r2 = load_gpr(ctx, a->r2);
    do_sub(ctx, a->t, tcg_r1, tcg_r2, is_tsv, is_b, is_tc, a->cf, a->d);
    return nullify_end(ctx);
}

static bool do_sub_imm(DisasContext *ctx, arg_rri_cf *a, bool is_tsv)
{
    TCGv_i64 tcg_im, tcg_r2;

    if (a->cf) {
        nullify_over(ctx);
    }
    tcg_im = tcg_constant_i64(a->i);
    tcg_r2 = load_gpr(ctx, a->r);
    do_sub(ctx, a->t, tcg_im, tcg_r2, is_tsv, 0, 0, a->cf, false);
    return nullify_end(ctx);
}

static void do_cmpclr(DisasContext *ctx, unsigned rt, TCGv_i64 in1,
                      TCGv_i64 in2, unsigned cf, bool d)
{
    TCGv_i64 dest, sv;
    DisasCond cond;

    dest = tcg_temp_new_i64();
    tcg_gen_sub_i64(dest, in1, in2);

    sv = NULL;
    if (cond_need_sv(cf >> 1)) {
        sv = do_sub_sv(ctx, dest, in1, in2);
    }

    cond = do_sub_cond(ctx, cf, d, dest, in1, in2, sv);

    tcg_gen_movi_i64(dest, 0);
    save_gpr(ctx, rt, dest);

    ctx->null_cond = cond;
}

static void do_log(DisasContext *ctx, unsigned rt, TCGv_i64 in1,
                   TCGv_i64 in2, unsigned cf, bool d,
                   void (*fn)(TCGv_i64, TCGv_i64, TCGv_i64))
{
    TCGv_i64 dest = dest_gpr(ctx, rt);

    fn(dest, in1, in2);
    save_gpr(ctx, rt, dest);

    ctx->null_cond = do_log_cond(ctx, cf, d, dest);
}

static bool do_log_reg(DisasContext *ctx, arg_rrr_cf_d *a,
                       void (*fn)(TCGv_i64, TCGv_i64, TCGv_i64))
{
    TCGv_i64 tcg_r1, tcg_r2;

    if (a->cf) {
        nullify_over(ctx);
    }
    tcg_r1 = load_gpr(ctx, a->r1);
    tcg_r2 = load_gpr(ctx, a->r2);
    do_log(ctx, a->t, tcg_r1, tcg_r2, a->cf, a->d, fn);
    return nullify_end(ctx);
}

static void do_unit_addsub(DisasContext *ctx, unsigned rt, TCGv_i64 in1,
                           TCGv_i64 in2, unsigned cf, bool d,
                           bool is_tc, bool is_add)
{
    TCGv_i64 dest = tcg_temp_new_i64();
    uint64_t test_cb = 0;
    DisasCond cond;

    switch (cf >> 1) {
    case 4:
        test_cb = dup_const(MO_8, 0x88);
        break;
    case 5:
        if (d) {
            test_cb = dup_const(MO_32, INT32_MIN);
        } else {
            cf &= 1;
        }
        break;
    case 6:
        test_cb = dup_const(MO_8, INT8_MIN);
        break;
    case 7:
        test_cb = dup_const(MO_16, INT16_MIN);
        break;
    }
    if (!d) {
        test_cb = (uint32_t)test_cb;
    }

    if (!test_cb) {
        if (is_add) {
            tcg_gen_add_i64(dest, in1, in2);
        } else {
            tcg_gen_sub_i64(dest, in1, in2);
        }
        cond = do_unit_zero_cond(cf, d, dest);
    } else {
        TCGv_i64 cb = tcg_temp_new_i64();

        if (d) {
            TCGv_i64 cb_msb = tcg_temp_new_i64();
            if (is_add) {
                tcg_gen_add2_i64(dest, cb_msb, in1, ctx->zero, in2, ctx->zero);
                tcg_gen_xor_i64(cb, in1, in2);
            } else {
                TCGv_i64 one = tcg_constant_i64(1);
                tcg_gen_sub2_i64(dest, cb_msb, in1, one, in2, ctx->zero);
                tcg_gen_eqv_i64(cb, in1, in2);
            }
            tcg_gen_xor_i64(cb, cb, dest);
            tcg_gen_extract2_i64(cb, cb, cb_msb, 1);
        } else {
            if (is_add) {
                tcg_gen_add_i64(dest, in1, in2);
                tcg_gen_xor_i64(cb, in1, in2);
            } else {
                tcg_gen_sub_i64(dest, in1, in2);
                tcg_gen_eqv_i64(cb, in1, in2);
            }
            tcg_gen_xor_i64(cb, cb, dest);
            tcg_gen_shri_i64(cb, cb, 1);
        }

        cond = cond_make_ti(cf & 1 ? TCG_COND_TSTEQ : TCG_COND_TSTNE,
                            cb, test_cb);
    }

    if (is_tc) {
        gen_tc(ctx, &cond);
    }
    save_gpr(ctx, rt, dest);

    ctx->null_cond = cond;
}

#ifndef CONFIG_USER_ONLY
static TCGv_i64 space_select(DisasContext *ctx, int sp, TCGv_i64 base)
{
    TCGv_ptr ptr;
    TCGv_i64 tmp;
    TCGv_i64 spc;

    if (sp != 0) {
        if (sp < 0) {
            sp = ~sp;
        }
        spc = tcg_temp_new_i64();
        load_spr(ctx, spc, sp);
        return spc;
    }
    if (ctx->tb_flags & TB_FLAG_SR_SAME) {
        return cpu_srH;
    }

    ptr = tcg_temp_new_ptr();
    tmp = tcg_temp_new_i64();
    spc = tcg_temp_new_i64();

    tcg_gen_shri_i64(tmp, base, (ctx->tb_flags & PSW_W ? 64 : 32) - 5);
    tcg_gen_andi_i64(tmp, tmp, 030);
    tcg_gen_trunc_i64_ptr(ptr, tmp);

    tcg_gen_add_ptr(ptr, ptr, tcg_env);
    tcg_gen_ld_i64(spc, ptr, offsetof(CPUHPPAState, sr[4]));

    return spc;
}
#endif

static void form_gva(DisasContext *ctx, TCGv_i64 *pgva, TCGv_i64 *pofs,
                     unsigned rb, unsigned rx, int scale, int64_t disp,
                     unsigned sp, int modify, bool is_phys)
{
    TCGv_i64 base = load_gpr(ctx, rb);
    TCGv_i64 ofs;
    TCGv_i64 addr;

    set_insn_breg(ctx, rb);

    if (rx) {
        ofs = tcg_temp_new_i64();
        tcg_gen_shli_i64(ofs, cpu_gr[rx], scale);
        tcg_gen_add_i64(ofs, ofs, base);
    } else if (disp || modify) {
        ofs = tcg_temp_new_i64();
        tcg_gen_addi_i64(ofs, base, disp);
    } else {
        ofs = base;
    }

    *pofs = ofs;
    *pgva = addr = tcg_temp_new_i64();
    tcg_gen_andi_i64(addr, modify <= 0 ? ofs : base,
                     ctx->gva_offset_mask);
#ifndef CONFIG_USER_ONLY
    if (!is_phys) {
        tcg_gen_or_i64(addr, addr, space_select(ctx, sp, base));
    }
#endif
}

static void do_load_32(DisasContext *ctx, TCGv_i32 dest, unsigned rb,
                       unsigned rx, int scale, int64_t disp,
                       unsigned sp, int modify, MemOp mop)
{
    TCGv_i64 ofs;
    TCGv_i64 addr;

    assert(ctx->null_cond.c == TCG_COND_NEVER);

    mop |= mo_endian(ctx);
    form_gva(ctx, &addr, &ofs, rb, rx, scale, disp, sp, modify,
             MMU_DISABLED(ctx));
    tcg_gen_qemu_ld_i32(dest, addr, ctx->mmu_idx, mop | UNALIGN(ctx));
    if (modify) {
        save_gpr(ctx, rb, ofs);
    }
}

static void do_load_64(DisasContext *ctx, TCGv_i64 dest, unsigned rb,
                       unsigned rx, int scale, int64_t disp,
                       unsigned sp, int modify, MemOp mop)
{
    TCGv_i64 ofs;
    TCGv_i64 addr;

    assert(ctx->null_cond.c == TCG_COND_NEVER);

    mop |= mo_endian(ctx);
    form_gva(ctx, &addr, &ofs, rb, rx, scale, disp, sp, modify,
             MMU_DISABLED(ctx));
    tcg_gen_qemu_ld_i64(dest, addr, ctx->mmu_idx, mop | UNALIGN(ctx));
    if (modify) {
        save_gpr(ctx, rb, ofs);
    }
}

static void do_store_32(DisasContext *ctx, TCGv_i32 src, unsigned rb,
                        unsigned rx, int scale, int64_t disp,
                        unsigned sp, int modify, MemOp mop)
{
    TCGv_i64 ofs;
    TCGv_i64 addr;

    assert(ctx->null_cond.c == TCG_COND_NEVER);

    mop |= mo_endian(ctx);
    form_gva(ctx, &addr, &ofs, rb, rx, scale, disp, sp, modify,
             MMU_DISABLED(ctx));
    tcg_gen_qemu_st_i32(src, addr, ctx->mmu_idx, mop | UNALIGN(ctx));
    if (modify) {
        save_gpr(ctx, rb, ofs);
    }
}

static void do_store_64(DisasContext *ctx, TCGv_i64 src, unsigned rb,
                        unsigned rx, int scale, int64_t disp,
                        unsigned sp, int modify, MemOp mop)
{
    TCGv_i64 ofs;
    TCGv_i64 addr;

    assert(ctx->null_cond.c == TCG_COND_NEVER);

    mop |= mo_endian(ctx);
    form_gva(ctx, &addr, &ofs, rb, rx, scale, disp, sp, modify,
             MMU_DISABLED(ctx));
    tcg_gen_qemu_st_i64(src, addr, ctx->mmu_idx, mop | UNALIGN(ctx));
    if (modify) {
        save_gpr(ctx, rb, ofs);
    }
}

static bool do_load(DisasContext *ctx, unsigned rt, unsigned rb,
                    unsigned rx, int scale, int64_t disp,
                    unsigned sp, int modify, MemOp mop)
{
    TCGv_i64 dest;

    nullify_over(ctx);

    if (modify == 0) {
        dest = dest_gpr(ctx, rt);
    } else {
        dest = tcg_temp_new_i64();
    }
    do_load_64(ctx, dest, rb, rx, scale, disp, sp, modify, mop);
    save_gpr(ctx, rt, dest);

    return nullify_end(ctx);
}

static bool do_floadw(DisasContext *ctx, unsigned rt, unsigned rb,
                      unsigned rx, int scale, int64_t disp,
                      unsigned sp, int modify)
{
    TCGv_i32 tmp;

    nullify_over(ctx);

    tmp = tcg_temp_new_i32();
    do_load_32(ctx, tmp, rb, rx, scale, disp, sp, modify, MO_UL);
    save_frw_i32(rt, tmp);

    if (rt == 0) {
        gen_helper_loaded_fr0(tcg_env);
    }

    return nullify_end(ctx);
}

static bool trans_fldw(DisasContext *ctx, arg_ldst *a)
{
    return do_floadw(ctx, a->t, a->b, a->x, a->scale ? 2 : 0,
                     a->disp, a->sp, a->m);
}

static bool do_floadd(DisasContext *ctx, unsigned rt, unsigned rb,
                      unsigned rx, int scale, int64_t disp,
                      unsigned sp, int modify)
{
    TCGv_i64 tmp;

    nullify_over(ctx);

    tmp = tcg_temp_new_i64();
    do_load_64(ctx, tmp, rb, rx, scale, disp, sp, modify, MO_UQ);
    save_frd(rt, tmp);

    if (rt == 0) {
        gen_helper_loaded_fr0(tcg_env);
    }

    return nullify_end(ctx);
}

static bool trans_fldd(DisasContext *ctx, arg_ldst *a)
{
    return do_floadd(ctx, a->t, a->b, a->x, a->scale ? 3 : 0,
                     a->disp, a->sp, a->m);
}

static bool do_store(DisasContext *ctx, unsigned rt, unsigned rb,
                     int64_t disp, unsigned sp,
                     int modify, MemOp mop)
{
    nullify_over(ctx);
    do_store_64(ctx, load_gpr(ctx, rt), rb, 0, 0, disp, sp, modify, mop);
    return nullify_end(ctx);
}

static bool do_fstorew(DisasContext *ctx, unsigned rt, unsigned rb,
                       unsigned rx, int scale, int64_t disp,
                       unsigned sp, int modify)
{
    TCGv_i32 tmp;

    nullify_over(ctx);

    tmp = load_frw_i32(rt);
    do_store_32(ctx, tmp, rb, rx, scale, disp, sp, modify, MO_UL);

    return nullify_end(ctx);
}

static bool trans_fstw(DisasContext *ctx, arg_ldst *a)
{
    return do_fstorew(ctx, a->t, a->b, a->x, a->scale ? 2 : 0,
                      a->disp, a->sp, a->m);
}

static bool do_fstored(DisasContext *ctx, unsigned rt, unsigned rb,
                       unsigned rx, int scale, int64_t disp,
                       unsigned sp, int modify)
{
    TCGv_i64 tmp;

    nullify_over(ctx);

    tmp = load_frd(rt);
    do_store_64(ctx, tmp, rb, rx, scale, disp, sp, modify, MO_UQ);

    return nullify_end(ctx);
}

static bool trans_fstd(DisasContext *ctx, arg_ldst *a)
{
    return do_fstored(ctx, a->t, a->b, a->x, a->scale ? 3 : 0,
                      a->disp, a->sp, a->m);
}

static bool do_fop_wew(DisasContext *ctx, unsigned rt, unsigned ra,
                       void (*func)(TCGv_i32, TCGv_env, TCGv_i32))
{
    TCGv_i32 tmp;

    nullify_over(ctx);
    tmp = load_frw0_i32(ra);

    func(tmp, tcg_env, tmp);

    save_frw_i32(rt, tmp);
    return nullify_end(ctx);
}

static bool do_fop_wed(DisasContext *ctx, unsigned rt, unsigned ra,
                       void (*func)(TCGv_i32, TCGv_env, TCGv_i64))
{
    TCGv_i32 dst;
    TCGv_i64 src;

    nullify_over(ctx);
    src = load_frd(ra);
    dst = tcg_temp_new_i32();

    func(dst, tcg_env, src);

    save_frw_i32(rt, dst);
    return nullify_end(ctx);
}

static bool do_fop_ded(DisasContext *ctx, unsigned rt, unsigned ra,
                       void (*func)(TCGv_i64, TCGv_env, TCGv_i64))
{
    TCGv_i64 tmp;

    nullify_over(ctx);
    tmp = load_frd0(ra);

    func(tmp, tcg_env, tmp);

    save_frd(rt, tmp);
    return nullify_end(ctx);
}

static bool do_fop_dew(DisasContext *ctx, unsigned rt, unsigned ra,
                       void (*func)(TCGv_i64, TCGv_env, TCGv_i32))
{
    TCGv_i32 src;
    TCGv_i64 dst;

    nullify_over(ctx);
    src = load_frw0_i32(ra);
    dst = tcg_temp_new_i64();

    func(dst, tcg_env, src);

    save_frd(rt, dst);
    return nullify_end(ctx);
}

static bool do_fop_weww(DisasContext *ctx, unsigned rt,
                        unsigned ra, unsigned rb,
                        void (*func)(TCGv_i32, TCGv_env, TCGv_i32, TCGv_i32))
{
    TCGv_i32 a, b;

    nullify_over(ctx);
    a = load_frw0_i32(ra);
    b = load_frw0_i32(rb);

    func(a, tcg_env, a, b);

    save_frw_i32(rt, a);
    return nullify_end(ctx);
}

static bool do_fop_dedd(DisasContext *ctx, unsigned rt,
                        unsigned ra, unsigned rb,
                        void (*func)(TCGv_i64, TCGv_env, TCGv_i64, TCGv_i64))
{
    TCGv_i64 a, b;

    nullify_over(ctx);
    a = load_frd0(ra);
    b = load_frd0(rb);

    func(a, tcg_env, a, b);

    save_frd(rt, a);
    return nullify_end(ctx);
}

static bool do_dbranch(DisasContext *ctx, int64_t disp,
                       unsigned link, bool is_n)
{
    ctx->iaq_j = iaqe_branchi(ctx, disp);

    if (ctx->null_cond.c == TCG_COND_NEVER && ctx->null_lab == NULL) {
        install_link(ctx, link, false);
        if (is_n) {
            if (use_nullify_skip(ctx)) {
                nullify_set(ctx, 0);
                store_psw_xb(ctx, 0);
                gen_goto_tb(ctx, 0, &ctx->iaq_j, NULL);
                ctx->base.is_jmp = DISAS_NORETURN;
                return true;
            }
            ctx->null_cond.c = TCG_COND_ALWAYS;
        }
        ctx->iaq_n = &ctx->iaq_j;
        ctx->psw_b_next = true;
    } else {
        nullify_over(ctx);

        install_link(ctx, link, false);
        if (is_n && use_nullify_skip(ctx)) {
            nullify_set(ctx, 0);
            store_psw_xb(ctx, 0);
            gen_goto_tb(ctx, 0, &ctx->iaq_j, NULL);
        } else {
            nullify_set(ctx, is_n);
            store_psw_xb(ctx, PSW_B);
            gen_goto_tb(ctx, 0, &ctx->iaq_b, &ctx->iaq_j);
        }
        nullify_end(ctx);

        nullify_set(ctx, 0);
        store_psw_xb(ctx, 0);
        gen_goto_tb(ctx, 1, &ctx->iaq_b, NULL);
        ctx->base.is_jmp = DISAS_NORETURN;
    }
    return true;
}

static bool do_cbranch(DisasContext *ctx, int64_t disp, bool is_n,
                       DisasCond *cond)
{
    DisasIAQE next;
    TCGLabel *taken = NULL;
    TCGCond c = cond->c;
    bool n;

    assert(ctx->null_cond.c == TCG_COND_NEVER);

    if (c == TCG_COND_ALWAYS) {
        return do_dbranch(ctx, disp, 0, is_n && disp >= 0);
    }

    taken = gen_new_label();
    tcg_gen_brcond_i64(c, cond->a0, cond->a1, taken);

    n = is_n && disp < 0;
    if (n && use_nullify_skip(ctx)) {
        nullify_set(ctx, 0);
        store_psw_xb(ctx, 0);
        next = iaqe_incr(&ctx->iaq_b, 4);
        gen_goto_tb(ctx, 0, &next, NULL);
    } else {
        if (!n && ctx->null_lab) {
            gen_set_label(ctx->null_lab);
            ctx->null_lab = NULL;
        }
        nullify_set(ctx, n);
        store_psw_xb(ctx, 0);
        gen_goto_tb(ctx, 0, &ctx->iaq_b, NULL);
    }

    gen_set_label(taken);

    n = is_n && disp >= 0;

    next = iaqe_branchi(ctx, disp);
    if (n && use_nullify_skip(ctx)) {
        nullify_set(ctx, 0);
        store_psw_xb(ctx, 0);
        gen_goto_tb(ctx, 1, &next, NULL);
    } else {
        nullify_set(ctx, n);
        store_psw_xb(ctx, PSW_B);
        gen_goto_tb(ctx, 1, &ctx->iaq_b, &next);
    }

    if (ctx->null_lab) {
        gen_set_label(ctx->null_lab);
        ctx->null_lab = NULL;
        ctx->base.is_jmp = DISAS_IAQ_N_STALE;
    } else {
        ctx->base.is_jmp = DISAS_NORETURN;
    }
    return true;
}

static bool do_ibranch(DisasContext *ctx, unsigned link,
                       bool with_sr0, bool is_n)
{
    if (ctx->null_cond.c == TCG_COND_NEVER && ctx->null_lab == NULL) {
        install_link(ctx, link, with_sr0);
        if (is_n) {
            if (use_nullify_skip(ctx)) {
                install_iaq_entries(ctx, &ctx->iaq_j, NULL);
                nullify_set(ctx, 0);
                ctx->base.is_jmp = DISAS_IAQ_N_UPDATED;
                return true;
            }
            ctx->null_cond.c = TCG_COND_ALWAYS;
        }
        ctx->iaq_n = &ctx->iaq_j;
        ctx->psw_b_next = true;
        return true;
    }

    nullify_over(ctx);

    install_link(ctx, link, with_sr0);
    if (is_n && use_nullify_skip(ctx)) {
        install_iaq_entries(ctx, &ctx->iaq_j, NULL);
        nullify_set(ctx, 0);
        store_psw_xb(ctx, 0);
    } else {
        install_iaq_entries(ctx, &ctx->iaq_b, &ctx->iaq_j);
        nullify_set(ctx, is_n);
        store_psw_xb(ctx, PSW_B);
    }

    tcg_gen_lookup_and_goto_ptr();
    ctx->base.is_jmp = DISAS_NORETURN;
    return nullify_end(ctx);
}

static TCGv_i64 do_ibranch_priv(DisasContext *ctx, TCGv_i64 offset)
{
    TCGv_i64 dest = tcg_temp_new_i64();
    switch (ctx->privilege) {
    case 0:
        tcg_gen_mov_i64(dest, offset);
        break;
    case 3:
        tcg_gen_ori_i64(dest, offset, 3);
        break;
    default:
        tcg_gen_andi_i64(dest, offset, -4);
        tcg_gen_ori_i64(dest, dest, ctx->privilege);
        tcg_gen_umax_i64(dest, dest, offset);
        break;
    }
    return dest;
}

#ifdef CONFIG_USER_ONLY
static void do_page_zero(DisasContext *ctx)
{
    assert(ctx->iaq_f.disp == 0);

    switch (ctx->null_cond.c) {
    case TCG_COND_NEVER:
        break;
    case TCG_COND_ALWAYS:
        tcg_gen_movi_i64(cpu_psw_n, 0);
        goto do_sigill;
    default:
        g_assert_not_reached();
    }

    if (ctx->psw_xb & PSW_B) {
        goto do_sigill;
    }

    switch (ctx->base.pc_first) {
    case 0x00:
        gen_excp_1(EXCP_IMP);
        ctx->base.is_jmp = DISAS_NORETURN;
        break;

    case 0xb0:
        gen_excp_1(EXCP_SYSCALL_LWS);
        ctx->base.is_jmp = DISAS_NORETURN;
        break;

    case 0xe0:
        {
            DisasIAQE next = { .base = tcg_temp_new_i64() };

            tcg_gen_st_i64(cpu_gr[26], tcg_env,
                           offsetof(CPUHPPAState, cr[27]));
            tcg_gen_ori_i64(next.base, cpu_gr[31], PRIV_USER);
            install_iaq_entries(ctx, &next, NULL);
            ctx->base.is_jmp = DISAS_IAQ_N_UPDATED;
        }
        break;

    case 0x100:
        gen_excp_1(EXCP_SYSCALL);
        ctx->base.is_jmp = DISAS_NORETURN;
        break;

    default:
    do_sigill:
        gen_excp_1(EXCP_ILL);
        ctx->base.is_jmp = DISAS_NORETURN;
        break;
    }
}
#endif

static bool trans_nop(DisasContext *ctx, arg_nop *a)
{
    ctx->null_cond = cond_make_f();
    return true;
}

static bool trans_break(DisasContext *ctx, arg_break *a)
{
    return gen_excp_iir(ctx, EXCP_BREAK);
}

static bool trans_sync(DisasContext *ctx, arg_sync *a)
{
    tcg_gen_mb(TCG_BAR_SC | TCG_MO_ALL);

    ctx->null_cond = cond_make_f();
    return true;
}

static bool trans_mfia(DisasContext *ctx, arg_mfia *a)
{
    TCGv_i64 dest = dest_gpr(ctx, a->t);

    copy_iaoq_entry(ctx, dest, &ctx->iaq_f);
    tcg_gen_andi_i64(dest, dest, -4);

    save_gpr(ctx, a->t, dest);
    ctx->null_cond = cond_make_f();
    return true;
}

static bool trans_mfsp(DisasContext *ctx, arg_mfsp *a)
{
    unsigned rt = a->t;
    unsigned rs = a->sp;
    TCGv_i64 t0 = tcg_temp_new_i64();

    load_spr(ctx, t0, rs);
    tcg_gen_shri_i64(t0, t0, 32);

    save_gpr(ctx, rt, t0);

    ctx->null_cond = cond_make_f();
    return true;
}

static bool trans_mfctl(DisasContext *ctx, arg_mfctl *a)
{
    unsigned rt = a->t;
    unsigned ctl = a->r;
    TCGv_i64 tmp;

    switch (ctl) {
    case CR_SAR:
        if (a->e == 0) {
            tmp = dest_gpr(ctx, rt);
            tcg_gen_andi_i64(tmp, cpu_sar, 31);
            save_gpr(ctx, rt, tmp);
            goto done;
        }
        save_gpr(ctx, rt, cpu_sar);
        goto done;
    case CR_IT:
        nullify_over(ctx);
        tmp = dest_gpr(ctx, rt);
        if (translator_io_start(&ctx->base)) {
            ctx->base.is_jmp = DISAS_IAQ_N_STALE;
        }
        gen_helper_read_interval_timer(tmp);
        save_gpr(ctx, rt, tmp);
        return nullify_end(ctx);
    case 26:
    case 27:
        break;
    default:
        CHECK_MOST_PRIVILEGED(EXCP_PRIV_REG);
        break;
    }

    tmp = tcg_temp_new_i64();
    tcg_gen_ld_i64(tmp, tcg_env, offsetof(CPUHPPAState, cr[ctl]));
    save_gpr(ctx, rt, tmp);

 done:
    ctx->null_cond = cond_make_f();
    return true;
}

static bool trans_mtsp(DisasContext *ctx, arg_mtsp *a)
{
    unsigned rr = a->r;
    unsigned rs = a->sp;
    TCGv_i64 tmp;

    if (rs >= 5) {
        CHECK_MOST_PRIVILEGED(EXCP_PRIV_REG);
    }
    nullify_over(ctx);

    tmp = tcg_temp_new_i64();
    tcg_gen_shli_i64(tmp, load_gpr(ctx, rr), 32);

    if (rs >= 4) {
        tcg_gen_st_i64(tmp, tcg_env, offsetof(CPUHPPAState, sr[rs]));
        ctx->tb_flags &= ~TB_FLAG_SR_SAME;
    } else {
        tcg_gen_mov_i64(cpu_sr[rs], tmp);
    }

    return nullify_end(ctx);
}

static bool trans_mtctl(DisasContext *ctx, arg_mtctl *a)
{
    unsigned ctl = a->t;
    TCGv_i64 reg;
    TCGv_i64 tmp;

    if (ctl == CR_SAR) {
        reg = load_gpr(ctx, a->r);
        tmp = tcg_temp_new_i64();
        tcg_gen_andi_i64(tmp, reg, ctx->is_pa20 ? 63 : 31);
        save_or_nullify(ctx, cpu_sar, tmp);

        ctx->null_cond = cond_make_f();
        return true;
    }

    CHECK_MOST_PRIVILEGED(EXCP_PRIV_REG);

#ifndef CONFIG_USER_ONLY
    nullify_over(ctx);

    if (ctx->is_pa20) {
        reg = load_gpr(ctx, a->r);
    } else {
        reg = tcg_temp_new_i64();
        tcg_gen_ext32u_i64(reg, load_gpr(ctx, a->r));
    }

    switch (ctl) {
    case CR_IT:
        if (translator_io_start(&ctx->base)) {
            ctx->base.is_jmp = DISAS_IAQ_N_STALE;
        }
        gen_helper_write_interval_timer(tcg_env, reg);
        break;
    case CR_EIRR:
        translator_io_start(&ctx->base);
        gen_helper_write_eirr(tcg_env, reg);
        ctx->base.is_jmp = DISAS_IAQ_N_STALE_EXIT;
        break;

    case CR_IIASQ:
    case CR_IIAOQ:
        tmp = tcg_temp_new_i64();
        tcg_gen_ld_i64(tmp, tcg_env,
                       offsetof(CPUHPPAState, cr_back[ctl - CR_IIASQ]));
        tcg_gen_st_i64(tmp, tcg_env, offsetof(CPUHPPAState, cr[ctl]));
        tcg_gen_st_i64(reg, tcg_env,
                       offsetof(CPUHPPAState, cr_back[ctl - CR_IIASQ]));
        break;

    case CR_PID1:
    case CR_PID2:
    case CR_PID3:
    case CR_PID4:
        tcg_gen_st_i64(reg, tcg_env, offsetof(CPUHPPAState, cr[ctl]));
#ifndef CONFIG_USER_ONLY
        gen_helper_change_prot_id(tcg_env);
#endif
        break;

    case CR_EIEM:
        ctx->base.is_jmp = DISAS_IAQ_N_STALE_EXIT;
        /* FALLTHRU */
    default:
        tcg_gen_st_i64(reg, tcg_env, offsetof(CPUHPPAState, cr[ctl]));
        break;
    }
    return nullify_end(ctx);
#endif
}

static bool trans_mtsarcm(DisasContext *ctx, arg_mtsarcm *a)
{
    TCGv_i64 tmp = tcg_temp_new_i64();

    tcg_gen_not_i64(tmp, load_gpr(ctx, a->r));
    tcg_gen_andi_i64(tmp, tmp, ctx->is_pa20 ? 63 : 31);
    save_or_nullify(ctx, cpu_sar, tmp);

    ctx->null_cond = cond_make_f();
    return true;
}

static bool trans_ldsid(DisasContext *ctx, arg_ldsid *a)
{
    TCGv_i64 dest = dest_gpr(ctx, a->t);

#ifdef CONFIG_USER_ONLY
    tcg_gen_movi_i64(dest, 0);
#else
    tcg_gen_mov_i64(dest, space_select(ctx, a->sp, load_gpr(ctx, a->b)));
    tcg_gen_shri_i64(dest, dest, 32);
#endif
    save_gpr(ctx, a->t, dest);

    ctx->null_cond = cond_make_f();
    return true;
}

static bool trans_rsm(DisasContext *ctx, arg_rsm *a)
{
#ifdef CONFIG_USER_ONLY
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
#else
    TCGv_i64 tmp;

    if (a->i) {
        CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
    }

    nullify_over(ctx);

    tmp = tcg_temp_new_i64();
    tcg_gen_ld_i64(tmp, tcg_env, offsetof(CPUHPPAState, psw));
    tcg_gen_andi_i64(tmp, tmp, ~a->i);
    gen_helper_swap_system_mask(tmp, tcg_env, tmp);
    save_gpr(ctx, a->t, tmp);

    ctx->base.is_jmp = DISAS_IAQ_N_STALE_EXIT;
    return nullify_end(ctx);
#endif
}

static bool trans_ssm(DisasContext *ctx, arg_ssm *a)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
#ifndef CONFIG_USER_ONLY
    TCGv_i64 tmp;

    nullify_over(ctx);

    tmp = tcg_temp_new_i64();
    tcg_gen_ld_i64(tmp, tcg_env, offsetof(CPUHPPAState, psw));
    tcg_gen_ori_i64(tmp, tmp, a->i);
    gen_helper_swap_system_mask(tmp, tcg_env, tmp);
    save_gpr(ctx, a->t, tmp);

    ctx->base.is_jmp = DISAS_IAQ_N_STALE_EXIT;
    return nullify_end(ctx);
#endif
}

static bool trans_mtsm(DisasContext *ctx, arg_mtsm *a)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
#ifndef CONFIG_USER_ONLY
    TCGv_i64 tmp, reg;
    nullify_over(ctx);

    reg = load_gpr(ctx, a->r);
    tmp = tcg_temp_new_i64();
    gen_helper_swap_system_mask(tmp, tcg_env, reg);

    ctx->base.is_jmp = DISAS_IAQ_N_STALE_EXIT;
    return nullify_end(ctx);
#endif
}

static bool do_rfi(DisasContext *ctx, bool rfi_r)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
#ifndef CONFIG_USER_ONLY
    nullify_over(ctx);

    if (rfi_r) {
        gen_helper_rfi_r(tcg_env);
    } else {
        gen_helper_rfi(tcg_env);
    }
    tcg_gen_exit_tb(NULL, 0);
    ctx->base.is_jmp = DISAS_NORETURN;

    return nullify_end(ctx);
#endif
}

static bool trans_rfi(DisasContext *ctx, arg_rfi *a)
{
    return do_rfi(ctx, false);
}

static bool trans_rfi_r(DisasContext *ctx, arg_rfi_r *a)
{
    return do_rfi(ctx, true);
}

static bool trans_halt(DisasContext *ctx, arg_halt *a)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
#ifndef CONFIG_USER_ONLY
    set_psw_xb(ctx, 0);
    nullify_over(ctx);
    gen_helper_halt(tcg_env);
    ctx->base.is_jmp = DISAS_NORETURN;
    return nullify_end(ctx);
#endif
}

static bool trans_reset(DisasContext *ctx, arg_reset *a)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
#ifndef CONFIG_USER_ONLY
    set_psw_xb(ctx, 0);
    nullify_over(ctx);
    gen_helper_reset(tcg_env);
    ctx->base.is_jmp = DISAS_NORETURN;
    return nullify_end(ctx);
#endif
}

static bool do_getshadowregs(DisasContext *ctx)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
    nullify_over(ctx);
    tcg_gen_ld_i64(cpu_gr[1], tcg_env, offsetof(CPUHPPAState, shadow[0]));
    tcg_gen_ld_i64(cpu_gr[8], tcg_env, offsetof(CPUHPPAState, shadow[1]));
    tcg_gen_ld_i64(cpu_gr[9], tcg_env, offsetof(CPUHPPAState, shadow[2]));
    tcg_gen_ld_i64(cpu_gr[16], tcg_env, offsetof(CPUHPPAState, shadow[3]));
    tcg_gen_ld_i64(cpu_gr[17], tcg_env, offsetof(CPUHPPAState, shadow[4]));
    tcg_gen_ld_i64(cpu_gr[24], tcg_env, offsetof(CPUHPPAState, shadow[5]));
    tcg_gen_ld_i64(cpu_gr[25], tcg_env, offsetof(CPUHPPAState, shadow[6]));
    return nullify_end(ctx);
}

static bool do_putshadowregs(DisasContext *ctx)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
    nullify_over(ctx);
    tcg_gen_st_i64(cpu_gr[1], tcg_env, offsetof(CPUHPPAState, shadow[0]));
    tcg_gen_st_i64(cpu_gr[8], tcg_env, offsetof(CPUHPPAState, shadow[1]));
    tcg_gen_st_i64(cpu_gr[9], tcg_env, offsetof(CPUHPPAState, shadow[2]));
    tcg_gen_st_i64(cpu_gr[16], tcg_env, offsetof(CPUHPPAState, shadow[3]));
    tcg_gen_st_i64(cpu_gr[17], tcg_env, offsetof(CPUHPPAState, shadow[4]));
    tcg_gen_st_i64(cpu_gr[24], tcg_env, offsetof(CPUHPPAState, shadow[5]));
    tcg_gen_st_i64(cpu_gr[25], tcg_env, offsetof(CPUHPPAState, shadow[6]));
    return nullify_end(ctx);
}

static bool trans_getshadowregs(DisasContext *ctx, arg_getshadowregs *a)
{
    return do_getshadowregs(ctx);
}

static bool trans_nop_addrx(DisasContext *ctx, arg_ldst *a)
{
    if (a->m) {
        TCGv_i64 dest = dest_gpr(ctx, a->b);
        TCGv_i64 src1 = load_gpr(ctx, a->b);
        TCGv_i64 src2 = load_gpr(ctx, a->x);

        tcg_gen_add_i64(dest, src1, src2);
        save_gpr(ctx, a->b, dest);
    }
    ctx->null_cond = cond_make_f();
    return true;
}

static bool trans_fic(DisasContext *ctx, arg_ldst *a)
{
    ctx->base.is_jmp = DISAS_IAQ_N_STALE;
    return trans_nop_addrx(ctx, a);
}

static bool trans_probe(DisasContext *ctx, arg_probe *a)
{
    TCGv_i64 dest, ofs;
    TCGv_i32 level, want;
    TCGv_i64 addr;

    nullify_over(ctx);

    dest = dest_gpr(ctx, a->t);
    form_gva(ctx, &addr, &ofs, a->b, 0, 0, 0, a->sp, 0, false);

    if (a->imm) {
        level = tcg_constant_i32(a->ri & 3);
    } else {
        level = tcg_temp_new_i32();
        tcg_gen_extrl_i64_i32(level, load_gpr(ctx, a->ri));
        tcg_gen_andi_i32(level, level, 3);
    }
    want = tcg_constant_i32(a->write ? PAGE_WRITE : PAGE_READ);

    gen_helper_probe(dest, tcg_env, addr, level, want);

    save_gpr(ctx, a->t, dest);
    return nullify_end(ctx);
}

static bool trans_ixtlbx(DisasContext *ctx, arg_ixtlbx *a)
{
    if (ctx->is_pa20) {
        return false;
    }
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
#ifndef CONFIG_USER_ONLY
    TCGv_i64 addr;
    TCGv_i64 ofs, reg;

    nullify_over(ctx);

    form_gva(ctx, &addr, &ofs, a->b, 0, 0, 0, a->sp, 0, false);
    reg = load_gpr(ctx, a->r);
    if (a->addr) {
        gen_helper_itlba_pa11(tcg_env, addr, reg);
    } else {
        gen_helper_itlbp_pa11(tcg_env, addr, reg);
    }

    if (ctx->tb_flags & PSW_C) {
        ctx->base.is_jmp = DISAS_IAQ_N_STALE;
    }
    return nullify_end(ctx);
#endif
}

static bool do_pxtlb(DisasContext *ctx, arg_ldst *a, bool local)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
#ifndef CONFIG_USER_ONLY
    TCGv_i64 addr;
    TCGv_i64 ofs;

    nullify_over(ctx);

    form_gva(ctx, &addr, &ofs, a->b, a->x, 0, 0, a->sp, a->m, false);

    tcg_gen_andi_i64(addr, addr, TARGET_PAGE_MASK);
    if (ctx->is_pa20) {
        tcg_gen_deposit_i64(addr, addr, load_gpr(ctx, a->b), 0, 4);
    }

    if (local) {
        gen_helper_ptlb_l(tcg_env, addr);
    } else {
        gen_helper_ptlb(tcg_env, addr);
    }

    if (a->m) {
        save_gpr(ctx, a->b, ofs);
    }

    if (ctx->tb_flags & PSW_C) {
        ctx->base.is_jmp = DISAS_IAQ_N_STALE;
    }
    return nullify_end(ctx);
#endif
}

static bool trans_pxtlb(DisasContext *ctx, arg_ldst *a)
{
    return do_pxtlb(ctx, a, false);
}

static bool trans_pxtlb_l(DisasContext *ctx, arg_ldst *a)
{
    return ctx->is_pa20 && do_pxtlb(ctx, a, true);
}

static bool trans_pxtlbe(DisasContext *ctx, arg_ldst *a)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
#ifndef CONFIG_USER_ONLY
    nullify_over(ctx);

    trans_nop_addrx(ctx, a);
    gen_helper_ptlbe(tcg_env);

    if (ctx->tb_flags & PSW_C) {
        ctx->base.is_jmp = DISAS_IAQ_N_STALE;
    }
    return nullify_end(ctx);
#endif
}

static bool trans_ixtlbxf(DisasContext *ctx, arg_ixtlbxf *a)
{
    if (ctx->is_pa20) {
        return false;
    }
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
#ifndef CONFIG_USER_ONLY
    TCGv_i64 addr, atl, stl;
    TCGv_i64 reg;

    nullify_over(ctx);

    atl = tcg_temp_new_i64();
    stl = tcg_temp_new_i64();
    addr = tcg_temp_new_i64();

    tcg_gen_ld32u_i64(stl, tcg_env,
                      a->data ? offsetof(CPUHPPAState, cr[CR_ISR])
                      : offsetof(CPUHPPAState, cr[CR_IIASQ]));
    tcg_gen_ld32u_i64(atl, tcg_env,
                      a->data ? offsetof(CPUHPPAState, cr[CR_IOR])
                      : offsetof(CPUHPPAState, cr[CR_IIAOQ]));
    tcg_gen_shli_i64(stl, stl, 32);
    tcg_gen_or_i64(addr, atl, stl);

    reg = load_gpr(ctx, a->r);
    if (a->addr) {
        gen_helper_itlba_pa11(tcg_env, addr, reg);
    } else {
        gen_helper_itlbp_pa11(tcg_env, addr, reg);
    }

    if (ctx->tb_flags & PSW_C) {
        ctx->base.is_jmp = DISAS_IAQ_N_STALE;
    }
    return nullify_end(ctx);
#endif
}

static bool trans_ixtlbt(DisasContext *ctx, arg_ixtlbt *a)
{
    if (!ctx->is_pa20) {
        return false;
    }
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
#ifndef CONFIG_USER_ONLY
    nullify_over(ctx);
    {
        TCGv_i64 src1 = load_gpr(ctx, a->r1);
        TCGv_i64 src2 = load_gpr(ctx, a->r2);

        if (a->data) {
            gen_helper_idtlbt_pa20(tcg_env, src1, src2);
        } else {
            gen_helper_iitlbt_pa20(tcg_env, src1, src2);
        }
    }
    if (ctx->tb_flags & PSW_C) {
        ctx->base.is_jmp = DISAS_IAQ_N_STALE;
    }
    return nullify_end(ctx);
#endif
}

static bool trans_lpa(DisasContext *ctx, arg_ldst *a)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
#ifndef CONFIG_USER_ONLY
    TCGv_i64 vaddr;
    TCGv_i64 ofs, paddr;

    nullify_over(ctx);

    form_gva(ctx, &vaddr, &ofs, a->b, a->x, 0, 0, a->sp, a->m, false);

    paddr = tcg_temp_new_i64();
    gen_helper_lpa(paddr, tcg_env, vaddr);

    if (a->m) {
        save_gpr(ctx, a->b, ofs);
    }
    save_gpr(ctx, a->t, paddr);

    return nullify_end(ctx);
#endif
}

static bool trans_lci(DisasContext *ctx, arg_lci *a)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);

    save_gpr(ctx, a->t, ctx->zero);

    ctx->null_cond = cond_make_f();
    return true;
}

static bool trans_add(DisasContext *ctx, arg_rrr_cf_d_sh *a)
{
    return do_add_reg(ctx, a, false, false, false, false);
}

static bool trans_add_l(DisasContext *ctx, arg_rrr_cf_d_sh *a)
{
    return do_add_reg(ctx, a, true, false, false, false);
}

static bool trans_add_tsv(DisasContext *ctx, arg_rrr_cf_d_sh *a)
{
    return do_add_reg(ctx, a, false, true, false, false);
}

static bool trans_add_c(DisasContext *ctx, arg_rrr_cf_d_sh *a)
{
    return do_add_reg(ctx, a, false, false, false, true);
}

static bool trans_add_c_tsv(DisasContext *ctx, arg_rrr_cf_d_sh *a)
{
    return do_add_reg(ctx, a, false, true, false, true);
}

static bool trans_sub(DisasContext *ctx, arg_rrr_cf_d *a)
{
    return do_sub_reg(ctx, a, false, false, false);
}

static bool trans_sub_tsv(DisasContext *ctx, arg_rrr_cf_d *a)
{
    return do_sub_reg(ctx, a, true, false, false);
}

static bool trans_sub_tc(DisasContext *ctx, arg_rrr_cf_d *a)
{
    return do_sub_reg(ctx, a, false, false, true);
}

static bool trans_sub_tsv_tc(DisasContext *ctx, arg_rrr_cf_d *a)
{
    return do_sub_reg(ctx, a, true, false, true);
}

static bool trans_sub_b(DisasContext *ctx, arg_rrr_cf_d *a)
{
    return do_sub_reg(ctx, a, false, true, false);
}

static bool trans_sub_b_tsv(DisasContext *ctx, arg_rrr_cf_d *a)
{
    return do_sub_reg(ctx, a, true, true, false);
}

static bool trans_andcm(DisasContext *ctx, arg_rrr_cf_d *a)
{
    return do_log_reg(ctx, a, tcg_gen_andc_i64);
}

static bool trans_and(DisasContext *ctx, arg_rrr_cf_d *a)
{
    return do_log_reg(ctx, a, tcg_gen_and_i64);
}

static bool trans_or(DisasContext *ctx, arg_rrr_cf_d *a)
{
    if (a->cf == 0) {
        unsigned r2 = a->r2;
        unsigned r1 = a->r1;
        unsigned rt = a->t;

        if (rt == 0) {
            ctx->null_cond = cond_make_f();
            return true;
        }
        if (r2 == 0) {
            if (r1 == 0) {
                TCGv_i64 dest = dest_gpr(ctx, rt);
                tcg_gen_movi_i64(dest, 0);
                save_gpr(ctx, rt, dest);
            } else {
                save_gpr(ctx, rt, cpu_gr[r1]);
            }
            ctx->null_cond = cond_make_f();
            return true;
        }
#ifndef CONFIG_USER_ONLY
        if ((rt == 10 || rt == 31) && r1 == rt && r2 == rt) {

            set_psw_xb(ctx, 0);

            nullify_over(ctx);

            install_iaq_entries(ctx, &ctx->iaq_b, NULL);
            nullify_set(ctx, 0);

            tcg_gen_st_i32(tcg_constant_i32(1), tcg_env,
                           offsetof(CPUState, halted) - offsetof(HPPACPU, env));
            gen_excp_1(EXCP_HALTED);
            ctx->base.is_jmp = DISAS_NORETURN;

            return nullify_end(ctx);
        }
#endif
    }
    return do_log_reg(ctx, a, tcg_gen_or_i64);
}

static bool trans_xor(DisasContext *ctx, arg_rrr_cf_d *a)
{
    return do_log_reg(ctx, a, tcg_gen_xor_i64);
}

static bool trans_cmpclr(DisasContext *ctx, arg_rrr_cf_d *a)
{
    TCGv_i64 tcg_r1, tcg_r2;

    if (a->cf) {
        nullify_over(ctx);
    }
    tcg_r1 = load_gpr(ctx, a->r1);
    tcg_r2 = load_gpr(ctx, a->r2);
    do_cmpclr(ctx, a->t, tcg_r1, tcg_r2, a->cf, a->d);
    return nullify_end(ctx);
}

static bool trans_uxor(DisasContext *ctx, arg_rrr_cf_d *a)
{
    TCGv_i64 tcg_r1, tcg_r2, dest;

    if (a->cf) {
        nullify_over(ctx);
    }

    tcg_r1 = load_gpr(ctx, a->r1);
    tcg_r2 = load_gpr(ctx, a->r2);
    dest = dest_gpr(ctx, a->t);

    tcg_gen_xor_i64(dest, tcg_r1, tcg_r2);
    save_gpr(ctx, a->t, dest);

    ctx->null_cond = do_unit_zero_cond(a->cf, a->d, dest);
    return nullify_end(ctx);
}

static bool do_uaddcm(DisasContext *ctx, arg_rrr_cf_d *a, bool is_tc)
{
    TCGv_i64 tcg_r1, tcg_r2, tmp;

    if (a->cf == 0) {
        tcg_r2 = load_gpr(ctx, a->r2);
        tmp = dest_gpr(ctx, a->t);

        if (a->r1 == 0) {
            tcg_gen_not_i64(tmp, tcg_r2);
        } else {
            tcg_r1 = load_gpr(ctx, a->r1);
            tcg_gen_sub_i64(tmp, tcg_r1, tcg_r2);
            tcg_gen_subi_i64(tmp, tmp, 1);
        }
        save_gpr(ctx, a->t, tmp);
        ctx->null_cond = cond_make_f();
        return true;
    }

    nullify_over(ctx);
    tcg_r1 = load_gpr(ctx, a->r1);
    tcg_r2 = load_gpr(ctx, a->r2);
    tmp = tcg_temp_new_i64();
    tcg_gen_not_i64(tmp, tcg_r2);
    do_unit_addsub(ctx, a->t, tcg_r1, tmp, a->cf, a->d, is_tc, true);
    return nullify_end(ctx);
}

static bool trans_uaddcm(DisasContext *ctx, arg_rrr_cf_d *a)
{
    return do_uaddcm(ctx, a, false);
}

static bool trans_uaddcm_tc(DisasContext *ctx, arg_rrr_cf_d *a)
{
    return do_uaddcm(ctx, a, true);
}

static bool do_dcor(DisasContext *ctx, arg_rr_cf_d *a, bool is_i)
{
    TCGv_i64 tmp;

    nullify_over(ctx);

    tmp = tcg_temp_new_i64();
    tcg_gen_extract2_i64(tmp, cpu_psw_cb, cpu_psw_cb_msb, 4);
    if (!is_i) {
        tcg_gen_not_i64(tmp, tmp);
    }
    tcg_gen_andi_i64(tmp, tmp, (uint64_t)0x1111111111111111ull);
    tcg_gen_muli_i64(tmp, tmp, 6);
    do_unit_addsub(ctx, a->t, load_gpr(ctx, a->r), tmp,
                   a->cf, a->d, false, is_i);
    return nullify_end(ctx);
}

static bool trans_dcor(DisasContext *ctx, arg_rr_cf_d *a)
{
    return do_dcor(ctx, a, false);
}

static bool trans_dcor_i(DisasContext *ctx, arg_rr_cf_d *a)
{
    return do_dcor(ctx, a, true);
}

static bool trans_ds(DisasContext *ctx, arg_rrr_cf *a)
{
    TCGv_i64 dest, add1, add2, addc, in1, in2;

    nullify_over(ctx);

    in1 = load_gpr(ctx, a->r1);
    in2 = load_gpr(ctx, a->r2);

    add1 = tcg_temp_new_i64();
    add2 = tcg_temp_new_i64();
    addc = tcg_temp_new_i64();
    dest = tcg_temp_new_i64();

    tcg_gen_add_i64(add1, in1, in1);
    tcg_gen_add_i64(add1, add1, get_psw_carry(ctx, false));

    tcg_gen_sextract_i64(addc, cpu_psw_v, 31, 1);
    tcg_gen_xor_i64(add2, in2, addc);
    tcg_gen_andi_i64(addc, addc, 1);

    tcg_gen_addcio_i64(dest, cpu_psw_cb_msb, add1, add2, addc);

    save_gpr(ctx, a->t, dest);

    tcg_gen_xor_i64(cpu_psw_cb, add1, add2);
    tcg_gen_xor_i64(cpu_psw_cb, cpu_psw_cb, dest);

    tcg_gen_shri_i64(cpu_psw_v, cpu_psw_cb, 1);
    tcg_gen_xor_i64(cpu_psw_v, cpu_psw_v, in2);

    if (a->cf) {
        TCGv_i64 sv = NULL, uv = NULL;
        if (cond_need_sv(a->cf >> 1)) {
            sv = do_add_sv(ctx, dest, add1, add2, in1, 1, false);
        } else if (cond_need_cb(a->cf >> 1)) {
            uv = do_add_uv(ctx, cpu_psw_cb, NULL, in1, 1, false);
        }
        ctx->null_cond = do_cond(ctx, a->cf, false, dest, uv, sv);
    }

    return nullify_end(ctx);
}

static bool trans_addi(DisasContext *ctx, arg_rri_cf *a)
{
    return do_add_imm(ctx, a, false, false);
}

static bool trans_addi_tsv(DisasContext *ctx, arg_rri_cf *a)
{
    return do_add_imm(ctx, a, true, false);
}

static bool trans_addi_tc(DisasContext *ctx, arg_rri_cf *a)
{
    return do_add_imm(ctx, a, false, true);
}

static bool trans_addi_tc_tsv(DisasContext *ctx, arg_rri_cf *a)
{
    return do_add_imm(ctx, a, true, true);
}

static bool trans_subi(DisasContext *ctx, arg_rri_cf *a)
{
    return do_sub_imm(ctx, a, false);
}

static bool trans_subi_tsv(DisasContext *ctx, arg_rri_cf *a)
{
    return do_sub_imm(ctx, a, true);
}

static bool trans_cmpiclr(DisasContext *ctx, arg_rri_cf_d *a)
{
    TCGv_i64 tcg_im, tcg_r2;

    if (a->cf) {
        nullify_over(ctx);
    }

    tcg_im = tcg_constant_i64(a->i);
    tcg_r2 = load_gpr(ctx, a->r);
    do_cmpclr(ctx, a->t, tcg_im, tcg_r2, a->cf, a->d);

    return nullify_end(ctx);
}

static bool do_multimedia(DisasContext *ctx, arg_rrr *a,
                          void (*fn)(TCGv_i64, TCGv_i64, TCGv_i64))
{
    TCGv_i64 r1, r2, dest;

    if (!ctx->is_pa20) {
        return false;
    }

    nullify_over(ctx);

    r1 = load_gpr(ctx, a->r1);
    r2 = load_gpr(ctx, a->r2);
    dest = dest_gpr(ctx, a->t);

    fn(dest, r1, r2);
    save_gpr(ctx, a->t, dest);

    return nullify_end(ctx);
}

static bool do_multimedia_sh(DisasContext *ctx, arg_rri *a,
                             void (*fn)(TCGv_i64, TCGv_i64, int64_t))
{
    TCGv_i64 r, dest;

    if (!ctx->is_pa20) {
        return false;
    }

    nullify_over(ctx);

    r = load_gpr(ctx, a->r);
    dest = dest_gpr(ctx, a->t);

    fn(dest, r, a->i);
    save_gpr(ctx, a->t, dest);

    return nullify_end(ctx);
}

static bool do_multimedia_shadd(DisasContext *ctx, arg_rrr_sh *a,
                                void (*fn)(TCGv_i64, TCGv_i64,
                                           TCGv_i64, TCGv_i32))
{
    TCGv_i64 r1, r2, dest;

    if (!ctx->is_pa20) {
        return false;
    }

    nullify_over(ctx);

    r1 = load_gpr(ctx, a->r1);
    r2 = load_gpr(ctx, a->r2);
    dest = dest_gpr(ctx, a->t);

    fn(dest, r1, r2, tcg_constant_i32(a->sh));
    save_gpr(ctx, a->t, dest);

    return nullify_end(ctx);
}

static bool trans_hadd(DisasContext *ctx, arg_rrr *a)
{
    return do_multimedia(ctx, a, tcg_gen_vec_add16_i64);
}

static bool trans_hadd_ss(DisasContext *ctx, arg_rrr *a)
{
    return do_multimedia(ctx, a, gen_helper_hadd_ss);
}

static bool trans_hadd_us(DisasContext *ctx, arg_rrr *a)
{
    return do_multimedia(ctx, a, gen_helper_hadd_us);
}

static bool trans_havg(DisasContext *ctx, arg_rrr *a)
{
    return do_multimedia(ctx, a, gen_helper_havg);
}

static bool trans_hshl(DisasContext *ctx, arg_rri *a)
{
    return do_multimedia_sh(ctx, a, tcg_gen_vec_shl16i_i64);
}

static bool trans_hshr_s(DisasContext *ctx, arg_rri *a)
{
    return do_multimedia_sh(ctx, a, tcg_gen_vec_sar16i_i64);
}

static bool trans_hshr_u(DisasContext *ctx, arg_rri *a)
{
    return do_multimedia_sh(ctx, a, tcg_gen_vec_shr16i_i64);
}

static bool trans_hshladd(DisasContext *ctx, arg_rrr_sh *a)
{
    return do_multimedia_shadd(ctx, a, gen_helper_hshladd);
}

static bool trans_hshradd(DisasContext *ctx, arg_rrr_sh *a)
{
    return do_multimedia_shadd(ctx, a, gen_helper_hshradd);
}

static bool trans_hsub(DisasContext *ctx, arg_rrr *a)
{
    return do_multimedia(ctx, a, tcg_gen_vec_sub16_i64);
}

static bool trans_hsub_ss(DisasContext *ctx, arg_rrr *a)
{
    return do_multimedia(ctx, a, gen_helper_hsub_ss);
}

static bool trans_hsub_us(DisasContext *ctx, arg_rrr *a)
{
    return do_multimedia(ctx, a, gen_helper_hsub_us);
}

static void gen_mixh_l(TCGv_i64 dst, TCGv_i64 r1, TCGv_i64 r2)
{
    uint64_t mask = 0xffff0000ffff0000ull;
    TCGv_i64 tmp = tcg_temp_new_i64();

    tcg_gen_andi_i64(tmp, r2, mask);
    tcg_gen_andi_i64(dst, r1, mask);
    tcg_gen_shri_i64(tmp, tmp, 16);
    tcg_gen_or_i64(dst, dst, tmp);
}

static bool trans_mixh_l(DisasContext *ctx, arg_rrr *a)
{
    return do_multimedia(ctx, a, gen_mixh_l);
}

static void gen_mixh_r(TCGv_i64 dst, TCGv_i64 r1, TCGv_i64 r2)
{
    uint64_t mask = 0x0000ffff0000ffffull;
    TCGv_i64 tmp = tcg_temp_new_i64();

    tcg_gen_andi_i64(tmp, r1, mask);
    tcg_gen_andi_i64(dst, r2, mask);
    tcg_gen_shli_i64(tmp, tmp, 16);
    tcg_gen_or_i64(dst, dst, tmp);
}

static bool trans_mixh_r(DisasContext *ctx, arg_rrr *a)
{
    return do_multimedia(ctx, a, gen_mixh_r);
}

static void gen_mixw_l(TCGv_i64 dst, TCGv_i64 r1, TCGv_i64 r2)
{
    TCGv_i64 tmp = tcg_temp_new_i64();

    tcg_gen_shri_i64(tmp, r2, 32);
    tcg_gen_deposit_i64(dst, r1, tmp, 0, 32);
}

static bool trans_mixw_l(DisasContext *ctx, arg_rrr *a)
{
    return do_multimedia(ctx, a, gen_mixw_l);
}

static void gen_mixw_r(TCGv_i64 dst, TCGv_i64 r1, TCGv_i64 r2)
{
    tcg_gen_deposit_i64(dst, r2, r1, 32, 32);
}

static bool trans_mixw_r(DisasContext *ctx, arg_rrr *a)
{
    return do_multimedia(ctx, a, gen_mixw_r);
}

static bool trans_permh(DisasContext *ctx, arg_permh *a)
{
    TCGv_i64 r, t0, t1, t2, t3;

    if (!ctx->is_pa20) {
        return false;
    }

    nullify_over(ctx);

    r = load_gpr(ctx, a->r1);
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    t3 = tcg_temp_new_i64();

    tcg_gen_extract_i64(t0, r, (3 - a->c0) * 16, 16);
    tcg_gen_extract_i64(t1, r, (3 - a->c1) * 16, 16);
    tcg_gen_extract_i64(t2, r, (3 - a->c2) * 16, 16);
    tcg_gen_extract_i64(t3, r, (3 - a->c3) * 16, 16);

    tcg_gen_deposit_i64(t0, t1, t0, 16, 48);
    tcg_gen_deposit_i64(t2, t3, t2, 16, 48);
    tcg_gen_deposit_i64(t0, t2, t0, 32, 32);

    save_gpr(ctx, a->t, t0);
    return nullify_end(ctx);
}

static bool trans_ld(DisasContext *ctx, arg_ldst *a)
{
    if (ctx->is_pa20) {
        if (a->t == 0) {
            return trans_nop_addrx(ctx, a);
        }
    } else if (a->size > MO_32) {
        return gen_illegal(ctx);
    }
    return do_load(ctx, a->t, a->b, a->x, a->scale ? a->size : 0,
                   a->disp, a->sp, a->m, a->size);
}

static bool trans_st(DisasContext *ctx, arg_ldst *a)
{
    assert(a->x == 0 && a->scale == 0);
    if (!ctx->is_pa20 && a->size > MO_32) {
        return gen_illegal(ctx);
    }
    return do_store(ctx, a->t, a->b, a->disp, a->sp, a->m, a->size);
}

static bool trans_ldc(DisasContext *ctx, arg_ldst *a)
{
    MemOp mop = mo_endian(ctx) | MO_ALIGN | a->size;
    TCGv_i64 dest, ofs;
    TCGv_i64 addr;

    if (!ctx->is_pa20 && a->size > MO_32) {
        return gen_illegal(ctx);
    }

    nullify_over(ctx);

    if (a->m) {
        dest = tcg_temp_new_i64();
    } else {
        dest = dest_gpr(ctx, a->t);
    }

    form_gva(ctx, &addr, &ofs, a->b, a->x, a->scale ? 3 : 0,
             a->disp, a->sp, a->m, MMU_DISABLED(ctx));

    gen_helper_ldc_check(addr);

    tcg_gen_atomic_xchg_i64(dest, addr, ctx->zero, ctx->mmu_idx, mop);

    if (a->m) {
        save_gpr(ctx, a->b, ofs);
    }
    save_gpr(ctx, a->t, dest);

    return nullify_end(ctx);
}

static bool trans_stby(DisasContext *ctx, arg_stby *a)
{
    TCGv_i64 ofs, val;
    TCGv_i64 addr;

    nullify_over(ctx);

    form_gva(ctx, &addr, &ofs, a->b, 0, 0, a->disp, a->sp, a->m,
             MMU_DISABLED(ctx));
    val = load_gpr(ctx, a->r);
    if (a->a) {
        if (tb_cflags(ctx->base.tb) & CF_PARALLEL) {
            gen_helper_stby_e_parallel(tcg_env, addr, val);
        } else {
            gen_helper_stby_e(tcg_env, addr, val);
        }
    } else {
        if (tb_cflags(ctx->base.tb) & CF_PARALLEL) {
            gen_helper_stby_b_parallel(tcg_env, addr, val);
        } else {
            gen_helper_stby_b(tcg_env, addr, val);
        }
    }
    if (a->m) {
        tcg_gen_andi_i64(ofs, ofs, ~3);
        save_gpr(ctx, a->b, ofs);
    }

    return nullify_end(ctx);
}

static bool trans_stdby(DisasContext *ctx, arg_stby *a)
{
    TCGv_i64 ofs, val;
    TCGv_i64 addr;

    if (!ctx->is_pa20) {
        return false;
    }
    nullify_over(ctx);

    form_gva(ctx, &addr, &ofs, a->b, 0, 0, a->disp, a->sp, a->m,
             MMU_DISABLED(ctx));
    val = load_gpr(ctx, a->r);
    if (a->a) {
        if (tb_cflags(ctx->base.tb) & CF_PARALLEL) {
            gen_helper_stdby_e_parallel(tcg_env, addr, val);
        } else {
            gen_helper_stdby_e(tcg_env, addr, val);
        }
    } else {
        if (tb_cflags(ctx->base.tb) & CF_PARALLEL) {
            gen_helper_stdby_b_parallel(tcg_env, addr, val);
        } else {
            gen_helper_stdby_b(tcg_env, addr, val);
        }
    }
    if (a->m) {
        tcg_gen_andi_i64(ofs, ofs, ~7);
        save_gpr(ctx, a->b, ofs);
    }

    return nullify_end(ctx);
}

static bool trans_lda(DisasContext *ctx, arg_ldst *a)
{
    int hold_mmu_idx = ctx->mmu_idx;

    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
    ctx->mmu_idx = ctx->tb_flags & PSW_W ? MMU_ABS_W_IDX : MMU_ABS_IDX;
    trans_ld(ctx, a);
    ctx->mmu_idx = hold_mmu_idx;
    return true;
}

static bool trans_sta(DisasContext *ctx, arg_ldst *a)
{
    int hold_mmu_idx = ctx->mmu_idx;

    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
    ctx->mmu_idx = ctx->tb_flags & PSW_W ? MMU_ABS_W_IDX : MMU_ABS_IDX;
    trans_st(ctx, a);
    ctx->mmu_idx = hold_mmu_idx;
    return true;
}

static bool trans_ldil(DisasContext *ctx, arg_ldil *a)
{
    TCGv_i64 tcg_rt = dest_gpr(ctx, a->t);

    tcg_gen_movi_i64(tcg_rt, a->i);
    save_gpr(ctx, a->t, tcg_rt);
    ctx->null_cond = cond_make_f();
    return true;
}

static bool trans_addil(DisasContext *ctx, arg_addil *a)
{
    TCGv_i64 tcg_rt = load_gpr(ctx, a->r);
    TCGv_i64 tcg_r1 = dest_gpr(ctx, 1);

    tcg_gen_addi_i64(tcg_r1, tcg_rt, a->i);
    save_gpr(ctx, 1, tcg_r1);
    ctx->null_cond = cond_make_f();
    return true;
}

static bool trans_ldo(DisasContext *ctx, arg_ldo *a)
{
    TCGv_i64 tcg_rt = dest_gpr(ctx, a->t);

    if (a->b == 0) {
        tcg_gen_movi_i64(tcg_rt, a->i);
    } else {
        tcg_gen_addi_i64(tcg_rt, cpu_gr[a->b], a->i);
    }
    save_gpr(ctx, a->t, tcg_rt);
    ctx->null_cond = cond_make_f();
    return true;
}

static bool do_cmpb(DisasContext *ctx, unsigned r, TCGv_i64 in1,
                    unsigned c, unsigned f, bool d, unsigned n, int disp)
{
    TCGv_i64 dest, in2, sv;
    DisasCond cond;

    in2 = load_gpr(ctx, r);
    dest = tcg_temp_new_i64();

    tcg_gen_sub_i64(dest, in1, in2);

    sv = NULL;
    if (cond_need_sv(c)) {
        sv = do_sub_sv(ctx, dest, in1, in2);
    }

    cond = do_sub_cond(ctx, c * 2 + f, d, dest, in1, in2, sv);
    return do_cbranch(ctx, disp, n, &cond);
}

static bool trans_cmpb(DisasContext *ctx, arg_cmpb *a)
{
    if (!ctx->is_pa20 && a->d) {
        return false;
    }
    nullify_over(ctx);
    return do_cmpb(ctx, a->r2, load_gpr(ctx, a->r1),
                   a->c, a->f, a->d, a->n, a->disp);
}

static bool trans_cmpbi(DisasContext *ctx, arg_cmpbi *a)
{
    if (!ctx->is_pa20 && a->d) {
        return false;
    }
    nullify_over(ctx);
    return do_cmpb(ctx, a->r, tcg_constant_i64(a->i),
                   a->c, a->f, a->d, a->n, a->disp);
}

static bool do_addb(DisasContext *ctx, unsigned r, TCGv_i64 in1,
                    unsigned c, unsigned f, unsigned n, int disp)
{
    TCGv_i64 dest, in2, sv, cb_cond;
    DisasCond cond;
    bool d = false;

    if (ctx->tb_flags & PSW_W) {
        d = c >= 5;
        if (d) {
            c &= 3;
        }
    }

    in2 = load_gpr(ctx, r);
    dest = tcg_temp_new_i64();
    sv = NULL;
    cb_cond = NULL;

    if (cond_need_cb(c)) {
        TCGv_i64 cb = tcg_temp_new_i64();
        TCGv_i64 cb_msb = tcg_temp_new_i64();

        tcg_gen_add2_i64(dest, cb_msb, in1, ctx->zero, in2, ctx->zero);
        tcg_gen_xor_i64(cb, in1, in2);
        tcg_gen_xor_i64(cb, cb, dest);
        cb_cond = get_carry(ctx, d, cb, cb_msb);
    } else {
        tcg_gen_add_i64(dest, in1, in2);
    }
    if (cond_need_sv(c)) {
        sv = do_add_sv(ctx, dest, in1, in2, in1, 0, d);
    }

    cond = do_cond(ctx, c * 2 + f, d, dest, cb_cond, sv);
    save_gpr(ctx, r, dest);
    return do_cbranch(ctx, disp, n, &cond);
}

static bool trans_addb(DisasContext *ctx, arg_addb *a)
{
    nullify_over(ctx);
    return do_addb(ctx, a->r2, load_gpr(ctx, a->r1), a->c, a->f, a->n, a->disp);
}

static bool trans_addbi(DisasContext *ctx, arg_addbi *a)
{
    nullify_over(ctx);
    return do_addb(ctx, a->r, tcg_constant_i64(a->i), a->c, a->f, a->n, a->disp);
}

static bool trans_bb_sar(DisasContext *ctx, arg_bb_sar *a)
{
    TCGv_i64 tmp, tcg_r;
    DisasCond cond;

    nullify_over(ctx);

    tmp = tcg_temp_new_i64();
    tcg_r = load_gpr(ctx, a->r);
    if (a->d) {
        tcg_gen_shl_i64(tmp, tcg_r, cpu_sar);
    } else {
        tcg_gen_ori_i64(tmp, cpu_sar, 32);
        tcg_gen_shl_i64(tmp, tcg_r, tmp);
    }

    cond = cond_make_ti(a->c ? TCG_COND_GE : TCG_COND_LT, tmp, 0);
    return do_cbranch(ctx, a->disp, a->n, &cond);
}

static bool trans_bb_imm(DisasContext *ctx, arg_bb_imm *a)
{
    DisasCond cond;
    int p = a->p | (a->d ? 0 : 32);

    nullify_over(ctx);
    cond = cond_make_vi(a->c ? TCG_COND_TSTEQ : TCG_COND_TSTNE,
                        load_gpr(ctx, a->r), 1ull << (63 - p));
    return do_cbranch(ctx, a->disp, a->n, &cond);
}

static bool trans_movb(DisasContext *ctx, arg_movb *a)
{
    TCGv_i64 dest;
    DisasCond cond;

    nullify_over(ctx);

    dest = dest_gpr(ctx, a->r2);
    if (a->r1 == 0) {
        tcg_gen_movi_i64(dest, 0);
    } else {
        tcg_gen_mov_i64(dest, cpu_gr[a->r1]);
    }

    cond = do_sed_cond(ctx, a->c, false, dest);
    return do_cbranch(ctx, a->disp, a->n, &cond);
}

static bool trans_movbi(DisasContext *ctx, arg_movbi *a)
{
    TCGv_i64 dest;
    DisasCond cond;

    nullify_over(ctx);

    dest = dest_gpr(ctx, a->r);
    tcg_gen_movi_i64(dest, a->i);

    cond = do_sed_cond(ctx, a->c, false, dest);
    return do_cbranch(ctx, a->disp, a->n, &cond);
}

static bool trans_shrp_sar(DisasContext *ctx, arg_shrp_sar *a)
{
    TCGv_i64 dest, src2;

    if (!ctx->is_pa20 && a->d) {
        return false;
    }
    if (a->c) {
        nullify_over(ctx);
    }

    dest = dest_gpr(ctx, a->t);
    src2 = load_gpr(ctx, a->r2);
    if (a->r1 == 0) {
        if (a->d) {
            tcg_gen_shr_i64(dest, src2, cpu_sar);
        } else {
            TCGv_i64 tmp = tcg_temp_new_i64();

            tcg_gen_ext32u_i64(dest, src2);
            tcg_gen_andi_i64(tmp, cpu_sar, 31);
            tcg_gen_shr_i64(dest, dest, tmp);
        }
    } else if (a->r1 == a->r2) {
        if (a->d) {
            tcg_gen_rotr_i64(dest, src2, cpu_sar);
        } else {
            TCGv_i32 t32 = tcg_temp_new_i32();
            TCGv_i32 s32 = tcg_temp_new_i32();

            tcg_gen_extrl_i64_i32(t32, src2);
            tcg_gen_extrl_i64_i32(s32, cpu_sar);
            tcg_gen_andi_i32(s32, s32, 31);
            tcg_gen_rotr_i32(t32, t32, s32);
            tcg_gen_extu_i32_i64(dest, t32);
        }
    } else {
        TCGv_i64 src1 = load_gpr(ctx, a->r1);

        if (a->d) {
            TCGv_i64 t = tcg_temp_new_i64();
            TCGv_i64 n = tcg_temp_new_i64();

            tcg_gen_xori_i64(n, cpu_sar, 63);
            tcg_gen_shl_i64(t, src1, n);
            tcg_gen_shli_i64(t, t, 1);
            tcg_gen_shr_i64(dest, src2, cpu_sar);
            tcg_gen_or_i64(dest, dest, t);
        } else {
            TCGv_i64 t = tcg_temp_new_i64();
            TCGv_i64 s = tcg_temp_new_i64();

            tcg_gen_concat32_i64(t, src2, src1);
            tcg_gen_andi_i64(s, cpu_sar, 31);
            tcg_gen_shr_i64(dest, t, s);
        }
    }
    save_gpr(ctx, a->t, dest);

    ctx->null_cond = do_sed_cond(ctx, a->c, a->d, dest);
    return nullify_end(ctx);
}

static bool trans_shrp_imm(DisasContext *ctx, arg_shrp_imm *a)
{
    unsigned width, sa;
    TCGv_i64 dest, t2;

    if (!ctx->is_pa20 && a->d) {
        return false;
    }
    if (a->c) {
        nullify_over(ctx);
    }

    width = a->d ? 64 : 32;
    sa = width - 1 - a->cpos;

    dest = dest_gpr(ctx, a->t);
    t2 = load_gpr(ctx, a->r2);
    if (a->r1 == 0) {
        tcg_gen_extract_i64(dest, t2, sa, width - sa);
    } else if (width == TARGET_LONG_BITS) {
        tcg_gen_extract2_i64(dest, t2, cpu_gr[a->r1], sa);
    } else {
        assert(!a->d);
        if (a->r1 == a->r2) {
            TCGv_i32 t32 = tcg_temp_new_i32();
            tcg_gen_extrl_i64_i32(t32, t2);
            tcg_gen_rotri_i32(t32, t32, sa);
            tcg_gen_extu_i32_i64(dest, t32);
        } else {
            tcg_gen_concat32_i64(dest, t2, cpu_gr[a->r1]);
            tcg_gen_extract_i64(dest, dest, sa, 32);
        }
    }
    save_gpr(ctx, a->t, dest);

    ctx->null_cond = do_sed_cond(ctx, a->c, a->d, dest);
    return nullify_end(ctx);
}

static bool trans_extr_sar(DisasContext *ctx, arg_extr_sar *a)
{
    unsigned widthm1 = a->d ? 63 : 31;
    TCGv_i64 dest, src, tmp;

    if (!ctx->is_pa20 && a->d) {
        return false;
    }
    if (a->c) {
        nullify_over(ctx);
    }

    dest = dest_gpr(ctx, a->t);
    src = load_gpr(ctx, a->r);
    tmp = tcg_temp_new_i64();

    tcg_gen_andi_i64(tmp, cpu_sar, widthm1);
    tcg_gen_xori_i64(tmp, tmp, widthm1);

    if (a->se) {
        if (!a->d) {
            tcg_gen_ext32s_i64(dest, src);
            src = dest;
        }
        tcg_gen_sar_i64(dest, src, tmp);
        tcg_gen_sextract_i64(dest, dest, 0, a->len);
    } else {
        if (!a->d) {
            tcg_gen_ext32u_i64(dest, src);
            src = dest;
        }
        tcg_gen_shr_i64(dest, src, tmp);
        tcg_gen_extract_i64(dest, dest, 0, a->len);
    }
    save_gpr(ctx, a->t, dest);

    ctx->null_cond = do_sed_cond(ctx, a->c, a->d, dest);
    return nullify_end(ctx);
}

static bool trans_extr_imm(DisasContext *ctx, arg_extr_imm *a)
{
    unsigned len, cpos, width;
    TCGv_i64 dest, src;

    if (!ctx->is_pa20 && a->d) {
        return false;
    }
    if (a->c) {
        nullify_over(ctx);
    }

    len = a->len;
    width = a->d ? 64 : 32;
    cpos = width - 1 - a->pos;
    if (cpos + len > width) {
        len = width - cpos;
    }

    dest = dest_gpr(ctx, a->t);
    src = load_gpr(ctx, a->r);
    if (a->se) {
        tcg_gen_sextract_i64(dest, src, cpos, len);
    } else {
        tcg_gen_extract_i64(dest, src, cpos, len);
    }
    save_gpr(ctx, a->t, dest);

    ctx->null_cond = do_sed_cond(ctx, a->c, a->d, dest);
    return nullify_end(ctx);
}

static bool trans_depi_imm(DisasContext *ctx, arg_depi_imm *a)
{
    unsigned len, width;
    uint64_t mask0, mask1;
    TCGv_i64 dest;

    if (!ctx->is_pa20 && a->d) {
        return false;
    }
    if (a->c) {
        nullify_over(ctx);
    }

    len = a->len;
    width = a->d ? 64 : 32;
    if (a->cpos + len > width) {
        len = width - a->cpos;
    }

    dest = dest_gpr(ctx, a->t);
    mask0 = deposit64(0, a->cpos, len, a->i);
    mask1 = deposit64(-1, a->cpos, len, a->i);

    if (a->nz) {
        TCGv_i64 src = load_gpr(ctx, a->t);
        tcg_gen_andi_i64(dest, src, mask1);
        tcg_gen_ori_i64(dest, dest, mask0);
    } else {
        tcg_gen_movi_i64(dest, mask0);
    }
    save_gpr(ctx, a->t, dest);

    ctx->null_cond = do_sed_cond(ctx, a->c, a->d, dest);
    return nullify_end(ctx);
}

static bool trans_dep_imm(DisasContext *ctx, arg_dep_imm *a)
{
    unsigned rs = a->nz ? a->t : 0;
    unsigned len, width;
    TCGv_i64 dest, val;

    if (!ctx->is_pa20 && a->d) {
        return false;
    }
    if (a->c) {
        nullify_over(ctx);
    }

    len = a->len;
    width = a->d ? 64 : 32;
    if (a->cpos + len > width) {
        len = width - a->cpos;
    }

    dest = dest_gpr(ctx, a->t);
    val = load_gpr(ctx, a->r);
    if (rs == 0) {
        tcg_gen_deposit_z_i64(dest, val, a->cpos, len);
    } else {
        tcg_gen_deposit_i64(dest, cpu_gr[rs], val, a->cpos, len);
    }
    save_gpr(ctx, a->t, dest);

    ctx->null_cond = do_sed_cond(ctx, a->c, a->d, dest);
    return nullify_end(ctx);
}

static bool do_dep_sar(DisasContext *ctx, unsigned rt, unsigned c,
                       bool d, bool nz, unsigned len, TCGv_i64 val)
{
    unsigned rs = nz ? rt : 0;
    unsigned widthm1 = d ? 63 : 31;
    TCGv_i64 mask, tmp, shift, dest;
    uint64_t msb = 1ULL << (len - 1);

    dest = dest_gpr(ctx, rt);
    shift = tcg_temp_new_i64();
    tmp = tcg_temp_new_i64();

    tcg_gen_andi_i64(shift, cpu_sar, widthm1);
    tcg_gen_xori_i64(shift, shift, widthm1);

    mask = tcg_temp_new_i64();
    tcg_gen_movi_i64(mask, msb + (msb - 1));
    tcg_gen_and_i64(tmp, val, mask);
    if (rs) {
        tcg_gen_shl_i64(mask, mask, shift);
        tcg_gen_shl_i64(tmp, tmp, shift);
        tcg_gen_andc_i64(dest, cpu_gr[rs], mask);
        tcg_gen_or_i64(dest, dest, tmp);
    } else {
        tcg_gen_shl_i64(dest, tmp, shift);
    }
    save_gpr(ctx, rt, dest);

    ctx->null_cond = do_sed_cond(ctx, c, d, dest);
    return nullify_end(ctx);
}

static bool trans_dep_sar(DisasContext *ctx, arg_dep_sar *a)
{
    if (!ctx->is_pa20 && a->d) {
        return false;
    }
    if (a->c) {
        nullify_over(ctx);
    }
    return do_dep_sar(ctx, a->t, a->c, a->d, a->nz, a->len,
                      load_gpr(ctx, a->r));
}

static bool trans_depi_sar(DisasContext *ctx, arg_depi_sar *a)
{
    if (!ctx->is_pa20 && a->d) {
        return false;
    }
    if (a->c) {
        nullify_over(ctx);
    }
    return do_dep_sar(ctx, a->t, a->c, a->d, a->nz, a->len,
                      tcg_constant_i64(a->i));
}

static bool trans_be(DisasContext *ctx, arg_be *a)
{
#ifndef CONFIG_USER_ONLY
    ctx->iaq_j.space = tcg_temp_new_i64();
    load_spr(ctx, ctx->iaq_j.space, a->sp);
#endif

    ctx->iaq_j.base = tcg_temp_new_i64();
    ctx->iaq_j.disp = 0;

    tcg_gen_addi_i64(ctx->iaq_j.base, load_gpr(ctx, a->b), a->disp);
    ctx->iaq_j.base = do_ibranch_priv(ctx, ctx->iaq_j.base);

    return do_ibranch(ctx, a->l, true, a->n);
}

static bool trans_bl(DisasContext *ctx, arg_bl *a)
{
    return do_dbranch(ctx, a->disp, a->l, a->n);
}

static bool trans_b_gate(DisasContext *ctx, arg_b_gate *a)
{
    int64_t disp = a->disp;
    bool indirect = false;

    if (ctx->psw_xb & PSW_B) {
        return gen_illegal(ctx);
    }

    nullify_over(ctx);

#ifndef CONFIG_USER_ONLY
    if (ctx->privilege == 0) {
    } else if (!(ctx->tb_flags & PSW_C)) {
        disp -= ctx->privilege;
    } else {
        TCGv_i64 off = tcg_temp_new_i64();

        copy_iaoq_entry(ctx, off, &ctx->iaq_f);
        gen_helper_b_gate_priv(off, tcg_env, off);

        ctx->iaq_j.base = off;
        ctx->iaq_j.disp = disp + 8;
        indirect = true;
    }
#endif

    if (a->l) {
        TCGv_i64 tmp = dest_gpr(ctx, a->l);
        if (ctx->privilege < 3) {
            tcg_gen_andi_i64(tmp, tmp, -4);
        }
        tcg_gen_ori_i64(tmp, tmp, ctx->privilege);
        save_gpr(ctx, a->l, tmp);
    }

    if (indirect) {
        return do_ibranch(ctx, 0, false, a->n);
    }
    return do_dbranch(ctx, disp, 0, a->n);
}

static bool trans_blr(DisasContext *ctx, arg_blr *a)
{
    if (a->x) {
        DisasIAQE next = iaqe_incr(&ctx->iaq_f, 8);
        TCGv_i64 t0 = tcg_temp_new_i64();
        TCGv_i64 t1 = tcg_temp_new_i64();

        copy_iaoq_entry(ctx, t0, &next);
        tcg_gen_shli_i64(t1, load_gpr(ctx, a->x), 3);
        tcg_gen_add_i64(t0, t0, t1);

        ctx->iaq_j = iaqe_next_absv(ctx, t0);
        return do_ibranch(ctx, a->l, false, a->n);
    } else {
        return do_dbranch(ctx, 0, a->l, a->n);
    }
}

static bool trans_bv(DisasContext *ctx, arg_bv *a)
{
    TCGv_i64 dest;

    if (a->x == 0) {
        dest = load_gpr(ctx, a->b);
    } else {
        dest = tcg_temp_new_i64();
        tcg_gen_shli_i64(dest, load_gpr(ctx, a->x), 3);
        tcg_gen_add_i64(dest, dest, load_gpr(ctx, a->b));
    }
    dest = do_ibranch_priv(ctx, dest);
    ctx->iaq_j = iaqe_next_absv(ctx, dest);

    return do_ibranch(ctx, 0, false, a->n);
}

static bool trans_bve(DisasContext *ctx, arg_bve *a)
{
    TCGv_i64 b = load_gpr(ctx, a->b);

#ifndef CONFIG_USER_ONLY
    ctx->iaq_j.space = space_select(ctx, 0, b);
#endif
    ctx->iaq_j.base = do_ibranch_priv(ctx, b);
    ctx->iaq_j.disp = 0;

    return do_ibranch(ctx, a->l, false, a->n);
}

static bool trans_nopbts(DisasContext *ctx, arg_nopbts *a)
{
    return ctx->is_pa20;
}

static void gen_fcpy_f(TCGv_i32 dst, TCGv_env unused, TCGv_i32 src)
{
    tcg_gen_mov_i32(dst, src);
}

static bool trans_fid_f(DisasContext *ctx, arg_fid_f *a)
{
    uint64_t ret;

    if (ctx->is_pa20) {
        ret = 0x13080000000000ULL;
    } else {
        ret = 0x0f080000000000ULL;
    }

    nullify_over(ctx);
    save_frd(0, tcg_constant_i64(ret));
    return nullify_end(ctx);
}

static bool trans_fcpy_f(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wew(ctx, a->t, a->r, gen_fcpy_f);
}

static void gen_fcpy_d(TCGv_i64 dst, TCGv_env unused, TCGv_i64 src)
{
    tcg_gen_mov_i64(dst, src);
}

static bool trans_fcpy_d(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_ded(ctx, a->t, a->r, gen_fcpy_d);
}

static void gen_fabs_f(TCGv_i32 dst, TCGv_env unused, TCGv_i32 src)
{
    tcg_gen_andi_i32(dst, src, INT32_MAX);
}

static bool trans_fabs_f(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wew(ctx, a->t, a->r, gen_fabs_f);
}

static void gen_fabs_d(TCGv_i64 dst, TCGv_env unused, TCGv_i64 src)
{
    tcg_gen_andi_i64(dst, src, INT64_MAX);
}

static bool trans_fabs_d(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_ded(ctx, a->t, a->r, gen_fabs_d);
}

static bool trans_fsqrt_f(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wew(ctx, a->t, a->r, gen_helper_fsqrt_s);
}

static bool trans_fsqrt_d(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_ded(ctx, a->t, a->r, gen_helper_fsqrt_d);
}

static bool trans_frnd_f(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wew(ctx, a->t, a->r, gen_helper_frnd_s);
}

static bool trans_frnd_d(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_ded(ctx, a->t, a->r, gen_helper_frnd_d);
}

static void gen_fneg_f(TCGv_i32 dst, TCGv_env unused, TCGv_i32 src)
{
    tcg_gen_xori_i32(dst, src, INT32_MIN);
}

static bool trans_fneg_f(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wew(ctx, a->t, a->r, gen_fneg_f);
}

static void gen_fneg_d(TCGv_i64 dst, TCGv_env unused, TCGv_i64 src)
{
    tcg_gen_xori_i64(dst, src, INT64_MIN);
}

static bool trans_fneg_d(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_ded(ctx, a->t, a->r, gen_fneg_d);
}

static void gen_fnegabs_f(TCGv_i32 dst, TCGv_env unused, TCGv_i32 src)
{
    tcg_gen_ori_i32(dst, src, INT32_MIN);
}

static bool trans_fnegabs_f(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wew(ctx, a->t, a->r, gen_fnegabs_f);
}

static void gen_fnegabs_d(TCGv_i64 dst, TCGv_env unused, TCGv_i64 src)
{
    tcg_gen_ori_i64(dst, src, INT64_MIN);
}

static bool trans_fnegabs_d(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_ded(ctx, a->t, a->r, gen_fnegabs_d);
}

static bool trans_fcnv_d_f(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wed(ctx, a->t, a->r, gen_helper_fcnv_d_s);
}

static bool trans_fcnv_f_d(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_dew(ctx, a->t, a->r, gen_helper_fcnv_s_d);
}

static bool trans_fcnv_w_f(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wew(ctx, a->t, a->r, gen_helper_fcnv_w_s);
}

static bool trans_fcnv_q_f(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wed(ctx, a->t, a->r, gen_helper_fcnv_dw_s);
}

static bool trans_fcnv_w_d(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_dew(ctx, a->t, a->r, gen_helper_fcnv_w_d);
}

static bool trans_fcnv_q_d(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_ded(ctx, a->t, a->r, gen_helper_fcnv_dw_d);
}

static bool trans_fcnv_f_w(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wew(ctx, a->t, a->r, gen_helper_fcnv_s_w);
}

static bool trans_fcnv_d_w(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wed(ctx, a->t, a->r, gen_helper_fcnv_d_w);
}

static bool trans_fcnv_f_q(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_dew(ctx, a->t, a->r, gen_helper_fcnv_s_dw);
}

static bool trans_fcnv_d_q(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_ded(ctx, a->t, a->r, gen_helper_fcnv_d_dw);
}

static bool trans_fcnv_t_f_w(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wew(ctx, a->t, a->r, gen_helper_fcnv_t_s_w);
}

static bool trans_fcnv_t_d_w(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wed(ctx, a->t, a->r, gen_helper_fcnv_t_d_w);
}

static bool trans_fcnv_t_f_q(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_dew(ctx, a->t, a->r, gen_helper_fcnv_t_s_dw);
}

static bool trans_fcnv_t_d_q(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_ded(ctx, a->t, a->r, gen_helper_fcnv_t_d_dw);
}

static bool trans_fcnv_uw_f(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wew(ctx, a->t, a->r, gen_helper_fcnv_uw_s);
}

static bool trans_fcnv_uq_f(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wed(ctx, a->t, a->r, gen_helper_fcnv_udw_s);
}

static bool trans_fcnv_uw_d(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_dew(ctx, a->t, a->r, gen_helper_fcnv_uw_d);
}

static bool trans_fcnv_uq_d(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_ded(ctx, a->t, a->r, gen_helper_fcnv_udw_d);
}

static bool trans_fcnv_f_uw(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wew(ctx, a->t, a->r, gen_helper_fcnv_s_uw);
}

static bool trans_fcnv_d_uw(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wed(ctx, a->t, a->r, gen_helper_fcnv_d_uw);
}

static bool trans_fcnv_f_uq(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_dew(ctx, a->t, a->r, gen_helper_fcnv_s_udw);
}

static bool trans_fcnv_d_uq(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_ded(ctx, a->t, a->r, gen_helper_fcnv_d_udw);
}

static bool trans_fcnv_t_f_uw(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wew(ctx, a->t, a->r, gen_helper_fcnv_t_s_uw);
}

static bool trans_fcnv_t_d_uw(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wed(ctx, a->t, a->r, gen_helper_fcnv_t_d_uw);
}

static bool trans_fcnv_t_f_uq(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_dew(ctx, a->t, a->r, gen_helper_fcnv_t_s_udw);
}

static bool trans_fcnv_t_d_uq(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_ded(ctx, a->t, a->r, gen_helper_fcnv_t_d_udw);
}

static bool trans_fcmp_f(DisasContext *ctx, arg_fclass2 *a)
{
    TCGv_i32 ta, tb, tc, ty;

    nullify_over(ctx);

    ta = load_frw0_i32(a->r1);
    tb = load_frw0_i32(a->r2);
    ty = tcg_constant_i32(a->y);
    tc = tcg_constant_i32(a->c);

    gen_helper_fcmp_s(tcg_env, ta, tb, ty, tc);

    return nullify_end(ctx);
}

static bool trans_fcmp_d(DisasContext *ctx, arg_fclass2 *a)
{
    TCGv_i64 ta, tb;
    TCGv_i32 tc, ty;

    nullify_over(ctx);

    ta = load_frd0(a->r1);
    tb = load_frd0(a->r2);
    ty = tcg_constant_i32(a->y);
    tc = tcg_constant_i32(a->c);

    gen_helper_fcmp_d(tcg_env, ta, tb, ty, tc);

    return nullify_end(ctx);
}

static bool trans_ftest(DisasContext *ctx, arg_ftest *a)
{
    TCGCond tc = TCG_COND_TSTNE;
    uint32_t mask;
    TCGv_i64 t;

    nullify_over(ctx);

    t = tcg_temp_new_i64();
    tcg_gen_ld32u_i64(t, tcg_env, offsetof(CPUHPPAState, fr0_shadow));

    if (a->y == 1) {
        switch (a->c) {
        case 0:
            mask = R_FPSR_C_MASK;
            break;
        case 2:
            tc = TCG_COND_TSTEQ;
            /* fallthru */
        case 1:
            mask = R_FPSR_C_MASK | R_FPSR_CQ_MASK;
            break;
        case 6:
            tc = TCG_COND_TSTEQ;
            /* fallthru */
        case 5:
            mask = R_FPSR_C_MASK | R_FPSR_CQ0_6_MASK;
            break;
        case 9:
            mask = R_FPSR_C_MASK | R_FPSR_CQ0_4_MASK;
            break;
        case 13:
            mask = R_FPSR_C_MASK | R_FPSR_CQ0_2_MASK;
            break;
        case 17:
            mask = R_FPSR_C_MASK | R_FPSR_CQ0_MASK;
            break;
        default:
            gen_illegal(ctx);
            return true;
        }
    } else {
        unsigned cbit = (a->y ^ 1) - 1;
        mask = R_FPSR_CA0_MASK >> cbit;
    }

    ctx->null_cond = cond_make_ti(tc, t, mask);
    return nullify_end(ctx);
}

static bool trans_fadd_f(DisasContext *ctx, arg_fclass3 *a)
{
    return do_fop_weww(ctx, a->t, a->r1, a->r2, gen_helper_fadd_s);
}

static bool trans_fadd_d(DisasContext *ctx, arg_fclass3 *a)
{
    return do_fop_dedd(ctx, a->t, a->r1, a->r2, gen_helper_fadd_d);
}

static bool trans_fsub_f(DisasContext *ctx, arg_fclass3 *a)
{
    return do_fop_weww(ctx, a->t, a->r1, a->r2, gen_helper_fsub_s);
}

static bool trans_fsub_d(DisasContext *ctx, arg_fclass3 *a)
{
    return do_fop_dedd(ctx, a->t, a->r1, a->r2, gen_helper_fsub_d);
}

static bool trans_fmpy_f(DisasContext *ctx, arg_fclass3 *a)
{
    return do_fop_weww(ctx, a->t, a->r1, a->r2, gen_helper_fmpy_s);
}

static bool trans_fmpy_d(DisasContext *ctx, arg_fclass3 *a)
{
    return do_fop_dedd(ctx, a->t, a->r1, a->r2, gen_helper_fmpy_d);
}

static bool trans_fdiv_f(DisasContext *ctx, arg_fclass3 *a)
{
    return do_fop_weww(ctx, a->t, a->r1, a->r2, gen_helper_fdiv_s);
}

static bool trans_fdiv_d(DisasContext *ctx, arg_fclass3 *a)
{
    return do_fop_dedd(ctx, a->t, a->r1, a->r2, gen_helper_fdiv_d);
}

static bool trans_xmpyu(DisasContext *ctx, arg_xmpyu *a)
{
    TCGv_i64 x, y;

    nullify_over(ctx);

    x = load_frw0_i64(a->r1);
    y = load_frw0_i64(a->r2);
    tcg_gen_mul_i64(x, x, y);
    save_frd(a->t, x);

    return nullify_end(ctx);
}

static inline int fmpyadd_s_reg(unsigned r)
{
    return (r & 16) * 2 + 16 + (r & 15);
}

static bool do_fmpyadd_s(DisasContext *ctx, arg_mpyadd *a, bool is_sub)
{
    int tm = fmpyadd_s_reg(a->tm);
    int ra = fmpyadd_s_reg(a->ra);
    int ta = fmpyadd_s_reg(a->ta);
    int rm2 = fmpyadd_s_reg(a->rm2);
    int rm1 = fmpyadd_s_reg(a->rm1);

    nullify_over(ctx);

    do_fop_weww(ctx, tm, rm1, rm2, gen_helper_fmpy_s);
    do_fop_weww(ctx, ta, ta, ra,
                is_sub ? gen_helper_fsub_s : gen_helper_fadd_s);

    return nullify_end(ctx);
}

static bool trans_fmpyadd_f(DisasContext *ctx, arg_mpyadd *a)
{
    return do_fmpyadd_s(ctx, a, false);
}

static bool trans_fmpysub_f(DisasContext *ctx, arg_mpyadd *a)
{
    return do_fmpyadd_s(ctx, a, true);
}

static bool do_fmpyadd_d(DisasContext *ctx, arg_mpyadd *a, bool is_sub)
{
    nullify_over(ctx);

    do_fop_dedd(ctx, a->tm, a->rm1, a->rm2, gen_helper_fmpy_d);
    do_fop_dedd(ctx, a->ta, a->ta, a->ra,
                is_sub ? gen_helper_fsub_d : gen_helper_fadd_d);

    return nullify_end(ctx);
}

static bool trans_fmpyadd_d(DisasContext *ctx, arg_mpyadd *a)
{
    return do_fmpyadd_d(ctx, a, false);
}

static bool trans_fmpysub_d(DisasContext *ctx, arg_mpyadd *a)
{
    return do_fmpyadd_d(ctx, a, true);
}

static bool trans_fmpyfadd_f(DisasContext *ctx, arg_fmpyfadd_f *a)
{
    TCGv_i32 x, y, z;

    nullify_over(ctx);
    x = load_frw0_i32(a->rm1);
    y = load_frw0_i32(a->rm2);
    z = load_frw0_i32(a->ra3);

    if (a->neg) {
        gen_helper_fmpynfadd_s(x, tcg_env, x, y, z);
    } else {
        gen_helper_fmpyfadd_s(x, tcg_env, x, y, z);
    }

    save_frw_i32(a->t, x);
    return nullify_end(ctx);
}

static bool trans_fmpyfadd_d(DisasContext *ctx, arg_fmpyfadd_d *a)
{
    TCGv_i64 x, y, z;

    nullify_over(ctx);
    x = load_frd0(a->rm1);
    y = load_frd0(a->rm2);
    z = load_frd0(a->ra3);

    if (a->neg) {
        gen_helper_fmpynfadd_d(x, tcg_env, x, y, z);
    } else {
        gen_helper_fmpyfadd_d(x, tcg_env, x, y, z);
    }

    save_frd(a->t, x);
    return nullify_end(ctx);
}

static bool trans_diag_btlb(DisasContext *ctx, arg_diag_btlb *a)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
#ifndef CONFIG_USER_ONLY
    nullify_over(ctx);
    gen_helper_diag_btlb(tcg_env);
    return nullify_end(ctx);
#endif
}

static bool trans_diag_cout(DisasContext *ctx, arg_diag_cout *a)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
#ifndef CONFIG_USER_ONLY
    nullify_over(ctx);
    gen_helper_diag_console_output(tcg_env);
    return nullify_end(ctx);
#endif
}

static bool trans_diag_getshadowregs_pa1(DisasContext *ctx, arg_empty *a)
{
    return !ctx->is_pa20 && do_getshadowregs(ctx);
}

static bool trans_diag_putshadowregs_pa1(DisasContext *ctx, arg_empty *a)
{
    return !ctx->is_pa20 && do_putshadowregs(ctx);
}

static bool trans_diag_mfdiag(DisasContext *ctx, arg_diag_mfdiag *a)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
    nullify_over(ctx);
    TCGv_i64 dest = dest_gpr(ctx, a->rt);
    tcg_gen_ld_i64(dest, tcg_env,
                       offsetof(CPUHPPAState, dr[a->dr]));
    save_gpr(ctx, a->rt, dest);
    return nullify_end(ctx);
}

static bool trans_diag_mtdiag(DisasContext *ctx, arg_diag_mtdiag *a)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
    nullify_over(ctx);
    tcg_gen_st_i64(load_gpr(ctx, a->r1), tcg_env,
                        offsetof(CPUHPPAState, dr[a->dr]));
#ifndef CONFIG_USER_ONLY
    if (ctx->is_pa20 && (a->dr == 2)) {
        gen_helper_update_gva_offset_mask(tcg_env);
        ctx->base.is_jmp = DISAS_IAQ_N_STALE_EXIT;
    }
#endif
    return nullify_end(ctx);
}

static bool trans_diag_unimp(DisasContext *ctx, arg_diag_unimp *a)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
    qemu_log_mask(LOG_UNIMP, "DIAG opcode 0x%04x ignored\n", a->i);
    return true;
}

static void hppa_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    uint64_t cs_base;
    int bound;

    ctx->cs = cs;
    ctx->tb_flags = ctx->base.tb->flags;
    ctx->is_pa20 = hppa_is_pa20(cpu_env(cs));
    ctx->psw_xb = ctx->tb_flags & (PSW_X | PSW_B);
    ctx->gva_offset_mask = cpu_env(cs)->gva_offset_mask;

#ifdef CONFIG_USER_ONLY
    ctx->privilege = PRIV_USER;
    ctx->mmu_idx = MMU_USER_IDX;
    ctx->unalign = (ctx->tb_flags & TB_FLAG_UNALIGN ? MO_UNALN : MO_ALIGN);
#else
    ctx->privilege = (ctx->tb_flags >> TB_FLAG_PRIV_SHIFT) & 3;
    ctx->mmu_idx = (ctx->tb_flags & PSW_D
                    ? PRIV_P_TO_MMU_IDX(ctx->privilege, ctx->tb_flags & PSW_P)
                    : ctx->tb_flags & PSW_W ? MMU_ABS_W_IDX : MMU_ABS_IDX);
#endif

    cs_base = ctx->base.tb->cs_base;
    ctx->iaoq_first = ctx->base.pc_first + ctx->privilege;

    if (unlikely(cs_base & CS_BASE_DIFFSPACE)) {
        ctx->iaq_b.space = cpu_iasq_b;
        ctx->iaq_b.base = cpu_iaoq_b;
    } else if (unlikely(cs_base & CS_BASE_DIFFPAGE)) {
        ctx->iaq_b.base = cpu_iaoq_b;
    } else {
        uint64_t iaoq_f_pgofs = ctx->iaoq_first & ~TARGET_PAGE_MASK;
        uint64_t iaoq_b_pgofs = cs_base & ~TARGET_PAGE_MASK;
        ctx->iaq_b.disp = iaoq_b_pgofs - iaoq_f_pgofs;
    }

    ctx->zero = tcg_constant_i64(0);

    bound = -(ctx->base.pc_first | TARGET_PAGE_MASK) / 4;
    ctx->base.max_insns = MIN(ctx->base.max_insns, bound);
}

static void hppa_tr_tb_start(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    ctx->null_cond = cond_make_f();
    ctx->psw_n_nonzero = false;
    if (ctx->tb_flags & PSW_N) {
        ctx->null_cond.c = TCG_COND_ALWAYS;
        ctx->psw_n_nonzero = true;
    }
    ctx->null_lab = NULL;
}

static void hppa_tr_insn_start(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    uint64_t iaoq_f, iaoq_b;
    int64_t diff;

    tcg_debug_assert(!iaqe_variable(&ctx->iaq_f));

    iaoq_f = ctx->iaoq_first + ctx->iaq_f.disp;
    if (iaqe_variable(&ctx->iaq_b)) {
        diff = INT32_MIN;
    } else {
        iaoq_b = ctx->iaoq_first + ctx->iaq_b.disp;
        diff = iaoq_b - iaoq_f;
        tcg_debug_assert(diff == (int32_t)diff);
        tcg_debug_assert(diff != INT32_MIN);
    }

    tcg_gen_insn_start(iaoq_f & ~TARGET_PAGE_MASK, diff, 0);
    ctx->insn_start_updated = false;
}

static void hppa_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    CPUHPPAState *env = cpu_env(cs);
    DisasJumpType ret;

#ifdef CONFIG_USER_ONLY
    if (ctx->base.pc_next < TARGET_PAGE_SIZE) {
        do_page_zero(ctx);
        ret = ctx->base.is_jmp;
        assert(ret != DISAS_NEXT);
    } else
#endif
    {
        uint32_t insn = translator_ldl(env, &ctx->base, ctx->base.pc_next);

        ctx->iaq_n = NULL;
        memset(&ctx->iaq_j, 0, sizeof(ctx->iaq_j));
        ctx->psw_b_next = false;

        if (unlikely(ctx->null_cond.c == TCG_COND_ALWAYS)) {
            ctx->null_cond.c = TCG_COND_NEVER;
            ret = DISAS_NEXT;
        } else {
            ctx->insn = insn;
            if (!decode(ctx, insn)) {
                gen_illegal(ctx);
            }
            ret = ctx->base.is_jmp;
            assert(ctx->null_lab == NULL);
        }

        if (ret != DISAS_NORETURN) {
            set_psw_xb(ctx, ctx->psw_b_next ? PSW_B : 0);
        }
    }

    ctx->base.pc_next += 4;
    if (ret != DISAS_NEXT) {
        return;
    }
    if (iaqe_variable(&ctx->iaq_b)
        || ctx->iaq_b.disp != ctx->iaq_f.disp + 4) {
        ctx->base.is_jmp = DISAS_IAQ_N_STALE;
        return;
    }

    ctx->iaq_f.disp = ctx->iaq_b.disp;
    if (!ctx->iaq_n) {
        ctx->iaq_b.disp += 4;
        return;
    }
    if (ctx->iaq_n->base) {
        copy_iaoq_entry(ctx, cpu_iaoq_b, ctx->iaq_n);
        ctx->iaq_b.base = cpu_iaoq_b;
        ctx->iaq_b.disp = 0;
    } else {
        ctx->iaq_b.disp = ctx->iaq_n->disp;
    }
    if (ctx->iaq_n->space) {
        tcg_gen_mov_i64(cpu_iasq_b, ctx->iaq_n->space);
        ctx->iaq_b.space = cpu_iasq_b;
    }
}

static void hppa_tr_tb_stop(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    DisasJumpType is_jmp = ctx->base.is_jmp;
    DisasIAQE *f = &ctx->iaq_b;
    DisasIAQE *b = ctx->iaq_n;

    switch (is_jmp) {
    case DISAS_NORETURN:
        break;
    case DISAS_TOO_MANY:
        f = &ctx->iaq_f;
        b = &ctx->iaq_b;
        /* FALLTHRU */
    case DISAS_IAQ_N_STALE:
        if (use_goto_tb(ctx, f, b)
            && (ctx->null_cond.c == TCG_COND_NEVER
                || ctx->null_cond.c == TCG_COND_ALWAYS)) {
            nullify_set(ctx, ctx->null_cond.c == TCG_COND_ALWAYS);
            gen_goto_tb(ctx, 0, f, b);
            break;
        }
        /* FALLTHRU */
    case DISAS_IAQ_N_STALE_EXIT:
        install_iaq_entries(ctx, f, b);
        nullify_save(ctx);
        if (is_jmp == DISAS_IAQ_N_STALE_EXIT) {
            tcg_gen_exit_tb(NULL, 0);
            break;
        }
        /* FALLTHRU */
    case DISAS_IAQ_N_UPDATED:
        tcg_gen_lookup_and_goto_ptr();
        break;
    case DISAS_EXIT:
        tcg_gen_exit_tb(NULL, 0);
        break;
    default:
        g_assert_not_reached();
    }

    for (DisasDelayException *e = ctx->delay_excp_list; e ; e = e->next) {
        gen_set_label(e->lab);
        if (e->set_n >= 0) {
            tcg_gen_movi_i64(cpu_psw_n, e->set_n);
        }
        if (e->set_iir) {
            tcg_gen_st_i64(tcg_constant_i64(e->insn), tcg_env,
                           offsetof(CPUHPPAState, cr[CR_IIR]));
        }
        install_iaq_entries(ctx, &e->iaq_f, &e->iaq_b);
        gen_excp_1(e->excp);
    }
}

#ifdef CONFIG_USER_ONLY
static bool hppa_tr_disas_log(const DisasContextBase *dcbase,
                              CPUState *cs, FILE *logfile)
{
    target_ulong pc = dcbase->pc_first;

    switch (pc) {
    case 0x00:
        fprintf(logfile, "IN:\n0x00000000:  (null)\n");
        return true;
    case 0xb0:
        fprintf(logfile, "IN:\n0x000000b0:  light-weight-syscall\n");
        return true;
    case 0xe0:
        fprintf(logfile, "IN:\n0x000000e0:  set-thread-pointer-syscall\n");
        return true;
    case 0x100:
        fprintf(logfile, "IN:\n0x00000100:  syscall\n");
        return true;
    }
    return false;
}
#endif

static const TranslatorOps hppa_tr_ops = {
    .init_disas_context = hppa_tr_init_disas_context,
    .tb_start           = hppa_tr_tb_start,
    .insn_start         = hppa_tr_insn_start,
    .translate_insn     = hppa_tr_translate_insn,
    .tb_stop            = hppa_tr_tb_stop,
#ifdef CONFIG_USER_ONLY
    .disas_log          = hppa_tr_disas_log,
#endif
};

void hppa_translate_code(CPUState *cs, TranslationBlock *tb,
                         int *max_insns, vaddr pc, void *host_pc)
{
    DisasContext ctx = { };
    translator_loop(cs, tb, max_insns, pc, host_pc, &hppa_tr_ops, &ctx.base);
}
