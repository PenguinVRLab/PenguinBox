/*
 *  S/390 translation
 *
 *  Copyright (c) 2009 Ulrich Hecht
 *  Copyright (c) 2010 Alexander Graf
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

/* #define DEBUG_INLINE_BRANCHES */
#define S390X_DEBUG_DISAS

#ifdef S390X_DEBUG_DISAS_VERBOSE
#  define LOG_DISAS(...) qemu_log(__VA_ARGS__)
#else
#  define LOG_DISAS(...) do { } while (0)
#endif

#include "qemu/osdep.h"
#include "cpu.h"
#include "s390x-internal.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

#include "exec/translator.h"
#include "exec/translation-block.h"
#include "exec/log.h"
#include "qemu/atomic128.h"

#define HELPER_H "helper.h"
#include "exec/helper-info.c.inc"
#undef  HELPER_H


typedef struct DisasContext DisasContext;
typedef struct DisasInsn DisasInsn;
typedef struct DisasFields DisasFields;

enum DisasFieldIndexO {
    FLD_O_r1,
    FLD_O_r2,
    FLD_O_r3,
    FLD_O_m1,
    FLD_O_m3,
    FLD_O_m4,
    FLD_O_m5,
    FLD_O_m6,
    FLD_O_b1,
    FLD_O_b2,
    FLD_O_b4,
    FLD_O_d1,
    FLD_O_d2,
    FLD_O_d4,
    FLD_O_x2,
    FLD_O_l1,
    FLD_O_l2,
    FLD_O_i1,
    FLD_O_i2,
    FLD_O_i3,
    FLD_O_i4,
    FLD_O_i5,
    FLD_O_v1,
    FLD_O_v2,
    FLD_O_v3,
    FLD_O_v4,
};

enum DisasFieldIndexC {
    FLD_C_r1 = 0,
    FLD_C_m1 = 0,
    FLD_C_b1 = 0,
    FLD_C_i1 = 0,
    FLD_C_v1 = 0,

    FLD_C_r2 = 1,
    FLD_C_b2 = 1,
    FLD_C_i2 = 1,

    FLD_C_r3 = 2,
    FLD_C_m3 = 2,
    FLD_C_i3 = 2,
    FLD_C_v3 = 2,

    FLD_C_m4 = 3,
    FLD_C_b4 = 3,
    FLD_C_i4 = 3,
    FLD_C_l1 = 3,
    FLD_C_v4 = 3,

    FLD_C_i5 = 4,
    FLD_C_d1 = 4,
    FLD_C_m5 = 4,

    FLD_C_d2 = 5,
    FLD_C_m6 = 5,

    FLD_C_d4 = 6,
    FLD_C_x2 = 6,
    FLD_C_l2 = 6,
    FLD_C_v2 = 6,

    NUM_C_FIELD = 7
};

struct DisasFields {
    uint64_t raw_insn;
    unsigned op:8;
    unsigned op2:8;
    unsigned presentC:16;
    unsigned int presentO;
    int c[NUM_C_FIELD];
};

struct DisasContext {
    DisasContextBase base;
    const DisasInsn *insn;
    DisasFields fields;
    uint64_t ex_value;
    uint64_t pc_tmp;
    uint32_t ilen;
    enum cc_op cc_op;
    bool exit_to_mainloop;
};

typedef struct {
    TCGCond cond:8;
    bool is_64;
    union {
        struct { TCGv_i64 a, b; } s64;
        struct { TCGv_i32 a, b; } s32;
    } u;
} DisasCompare;

#ifdef DEBUG_INLINE_BRANCHES
static uint64_t inline_branch_hit[CC_OP_MAX];
static uint64_t inline_branch_miss[CC_OP_MAX];
#endif

static void pc_to_link_info(TCGv_i64 out, DisasContext *s, uint64_t pc)
{
    if (s->base.tb->flags & FLAG_MASK_32) {
        if (s->base.tb->flags & FLAG_MASK_64) {
            tcg_gen_movi_i64(out, pc);
            return;
        }
        pc |= 0x80000000;
    }
    assert(!(s->base.tb->flags & FLAG_MASK_64));
    tcg_gen_deposit_i64(out, out, tcg_constant_i64(pc), 0, 32);
}

static TCGv_i64 psw_addr;
static TCGv_i64 psw_mask;
static TCGv_i64 gbea;

static TCGv_i32 cc_op;
static TCGv_i64 cc_src;
static TCGv_i64 cc_dst;
static TCGv_i64 cc_vr;

static char cpu_reg_names[16][4];
static TCGv_i64 regs[16];

void s390x_translate_init(void)
{
    int i;

    psw_addr = tcg_global_mem_new_i64(tcg_env,
                                      offsetof(CPUS390XState, psw.addr),
                                      "psw_addr");
    psw_mask = tcg_global_mem_new_i64(tcg_env,
                                      offsetof(CPUS390XState, psw.mask),
                                      "psw_mask");
    gbea = tcg_global_mem_new_i64(tcg_env,
                                  offsetof(CPUS390XState, gbea),
                                  "gbea");

    cc_op = tcg_global_mem_new_i32(tcg_env, offsetof(CPUS390XState, cc_op),
                                   "cc_op");
    cc_src = tcg_global_mem_new_i64(tcg_env, offsetof(CPUS390XState, cc_src),
                                    "cc_src");
    cc_dst = tcg_global_mem_new_i64(tcg_env, offsetof(CPUS390XState, cc_dst),
                                    "cc_dst");
    cc_vr = tcg_global_mem_new_i64(tcg_env, offsetof(CPUS390XState, cc_vr),
                                   "cc_vr");

    for (i = 0; i < 16; i++) {
        snprintf(cpu_reg_names[i], sizeof(cpu_reg_names[0]), "r%d", i);
        regs[i] = tcg_global_mem_new(tcg_env,
                                     offsetof(CPUS390XState, regs[i]),
                                     cpu_reg_names[i]);
    }
}

static inline int vec_full_reg_offset(uint8_t reg)
{
    g_assert(reg < 32);
    return offsetof(CPUS390XState, vregs[reg][0]);
}

static inline int vec_reg_offset(uint8_t reg, uint8_t enr, MemOp es)
{
    const uint8_t bytes = 1 << es;
    int offs = enr * bytes;

    g_assert(es <= MO_64);
    if (!HOST_BIG_ENDIAN) {
        offs ^= (8 - bytes);
    }
    return offs + vec_full_reg_offset(reg);
}

static inline int freg64_offset(uint8_t reg)
{
    g_assert(reg < 16);
    return vec_reg_offset(reg, 0, MO_64);
}

static inline int freg32_offset(uint8_t reg)
{
    g_assert(reg < 16);
    return vec_reg_offset(reg, 0, MO_32);
}

static TCGv_i64 load_reg(int reg)
{
    TCGv_i64 r = tcg_temp_new_i64();
    tcg_gen_mov_i64(r, regs[reg]);
    return r;
}

static TCGv_i64 load_freg(int reg)
{
    TCGv_i64 r = tcg_temp_new_i64();

    tcg_gen_ld_i64(r, tcg_env, freg64_offset(reg));
    return r;
}

static TCGv_i64 load_freg32_i64(int reg)
{
    TCGv_i64 r = tcg_temp_new_i64();

    tcg_gen_ld32u_i64(r, tcg_env, freg32_offset(reg));
    return r;
}

static TCGv_i128 load_freg_128(int reg)
{
    TCGv_i64 h = load_freg(reg);
    TCGv_i64 l = load_freg(reg + 2);
    TCGv_i128 r = tcg_temp_new_i128();

    tcg_gen_concat_i64_i128(r, l, h);
    return r;
}

static void store_reg(int reg, TCGv_i64 v)
{
    tcg_gen_mov_i64(regs[reg], v);
}

static void store_freg(int reg, TCGv_i64 v)
{
    tcg_gen_st_i64(v, tcg_env, freg64_offset(reg));
}

static void store_reg32_i64(int reg, TCGv_i64 v)
{
    tcg_gen_deposit_i64(regs[reg], regs[reg], v, 0, 32);
}

static void store_reg32h_i64(int reg, TCGv_i64 v)
{
    tcg_gen_deposit_i64(regs[reg], regs[reg], v, 32, 32);
}

static void store_freg32_i64(int reg, TCGv_i64 v)
{
    tcg_gen_st32_i64(v, tcg_env, freg32_offset(reg));
}

static void update_psw_addr(DisasContext *s)
{
    tcg_gen_movi_i64(psw_addr, s->base.pc_next);
}

static void per_branch(DisasContext *s, TCGv_i64 dest)
{
#ifndef CONFIG_USER_ONLY
    if (s->base.tb->flags & FLAG_MASK_PER_BRANCH) {
        gen_helper_per_branch(tcg_env, dest, tcg_constant_i32(s->ilen));
    }
#endif
}

static void per_breaking_event(DisasContext *s)
{
    tcg_gen_movi_i64(gbea, s->base.pc_next);
}

static void update_cc_op(DisasContext *s)
{
    if (s->cc_op != CC_OP_DYNAMIC && s->cc_op != CC_OP_STATIC) {
        tcg_gen_movi_i32(cc_op, s->cc_op);
    }
}

static inline uint64_t ld_code2(CPUS390XState *env, DisasContext *s,
                                uint64_t pc)
{
    return (uint64_t)translator_lduw(env, &s->base, pc);
}

static inline uint64_t ld_code4(CPUS390XState *env, DisasContext *s,
                                uint64_t pc)
{
    return (uint64_t)(uint32_t)translator_ldl(env, &s->base, pc);
}

static int get_mem_index(DisasContext *s)
{
#ifdef CONFIG_USER_ONLY
    return MMU_USER_IDX;
#else
    if (!(s->base.tb->flags & FLAG_MASK_DAT)) {
        return MMU_REAL_IDX;
    }

    switch (s->base.tb->flags & FLAG_MASK_ASC) {
    case PSW_ASC_PRIMARY >> FLAG_MASK_PSW_SHIFT:
        return MMU_PRIMARY_IDX;
    case PSW_ASC_SECONDARY >> FLAG_MASK_PSW_SHIFT:
        return MMU_SECONDARY_IDX;
    case PSW_ASC_HOME >> FLAG_MASK_PSW_SHIFT:
        return MMU_HOME_IDX;
    default:
        g_assert_not_reached();
    }
#endif
}

static void gen_exception(int excp)
{
    gen_helper_exception(tcg_env, tcg_constant_i32(excp));
}

static void gen_program_exception(DisasContext *s, int code)
{
    tcg_gen_st_i32(tcg_constant_i32(code), tcg_env,
                   offsetof(CPUS390XState, int_pgm_code));

    tcg_gen_st_i32(tcg_constant_i32(s->ilen), tcg_env,
                   offsetof(CPUS390XState, int_pgm_ilen));

    update_psw_addr(s);

    update_cc_op(s);

    gen_exception(EXCP_PGM);
}

static inline void gen_illegal_opcode(DisasContext *s)
{
    gen_program_exception(s, PGM_OPERATION);
}

static inline void gen_data_exception(uint8_t dxc)
{
    gen_helper_data_exception(tcg_env, tcg_constant_i32(dxc));
}

static inline void gen_trap(DisasContext *s)
{
    gen_data_exception(0xff);
}

static void gen_addi_and_wrap_i64(DisasContext *s, TCGv_i64 dst, TCGv_i64 src,
                                  int64_t imm)
{
    tcg_gen_addi_i64(dst, src, imm);
    if (!(s->base.tb->flags & FLAG_MASK_64)) {
        if (s->base.tb->flags & FLAG_MASK_32) {
            tcg_gen_andi_i64(dst, dst, 0x7fffffff);
        } else {
            tcg_gen_andi_i64(dst, dst, 0x00ffffff);
        }
    }
}

static TCGv_i64 get_address(DisasContext *s, int x2, int b2, int d2)
{
    TCGv_i64 tmp = tcg_temp_new_i64();

    if (b2 && x2) {
        tcg_gen_add_i64(tmp, regs[b2], regs[x2]);
        gen_addi_and_wrap_i64(s, tmp, tmp, d2);
    } else if (b2) {
        gen_addi_and_wrap_i64(s, tmp, regs[b2], d2);
    } else if (x2) {
        gen_addi_and_wrap_i64(s, tmp, regs[x2], d2);
    } else if (!(s->base.tb->flags & FLAG_MASK_64)) {
        if (s->base.tb->flags & FLAG_MASK_32) {
            tcg_gen_movi_i64(tmp, d2 & 0x7fffffff);
        } else {
            tcg_gen_movi_i64(tmp, d2 & 0x00ffffff);
        }
    } else {
        tcg_gen_movi_i64(tmp, d2);
    }

    return tmp;
}

static inline bool live_cc_data(DisasContext *s)
{
    return (s->cc_op != CC_OP_DYNAMIC
            && s->cc_op != CC_OP_STATIC
            && s->cc_op > 3);
}

static inline void gen_op_movi_cc(DisasContext *s, uint32_t val)
{
    if (live_cc_data(s)) {
        tcg_gen_discard_i64(cc_src);
        tcg_gen_discard_i64(cc_dst);
        tcg_gen_discard_i64(cc_vr);
    }
    s->cc_op = CC_OP_CONST0 + val;
}

static void gen_op_update1_cc_i64(DisasContext *s, enum cc_op op, TCGv_i64 dst)
{
    if (live_cc_data(s)) {
        tcg_gen_discard_i64(cc_src);
        tcg_gen_discard_i64(cc_vr);
    }
    tcg_gen_mov_i64(cc_dst, dst);
    s->cc_op = op;
}

static void gen_op_update2_cc_i64(DisasContext *s, enum cc_op op, TCGv_i64 src,
                                  TCGv_i64 dst)
{
    if (live_cc_data(s)) {
        tcg_gen_discard_i64(cc_vr);
    }
    tcg_gen_mov_i64(cc_src, src);
    tcg_gen_mov_i64(cc_dst, dst);
    s->cc_op = op;
}

static void gen_op_update3_cc_i64(DisasContext *s, enum cc_op op, TCGv_i64 src,
                                  TCGv_i64 dst, TCGv_i64 vr)
{
    tcg_gen_mov_i64(cc_src, src);
    tcg_gen_mov_i64(cc_dst, dst);
    tcg_gen_mov_i64(cc_vr, vr);
    s->cc_op = op;
}

static void set_cc_nz_u64(DisasContext *s, TCGv_i64 val)
{
    gen_op_update1_cc_i64(s, CC_OP_NZ, val);
}

static void set_cc_static(DisasContext *s)
{
    if (live_cc_data(s)) {
        tcg_gen_discard_i64(cc_src);
        tcg_gen_discard_i64(cc_dst);
        tcg_gen_discard_i64(cc_vr);
    }
    s->cc_op = CC_OP_STATIC;
}

static void gen_op_calc_cc(DisasContext *s)
{
    TCGv_i32 local_cc_op = NULL;
    TCGv_i64 dummy = NULL;

    switch (s->cc_op) {
    default:
        dummy = tcg_constant_i64(0);
        /* FALLTHRU */
    case CC_OP_ADD_64:
    case CC_OP_SUB_64:
    case CC_OP_ADD_32:
    case CC_OP_SUB_32:
        local_cc_op = tcg_constant_i32(s->cc_op);
        break;
    case CC_OP_CONST0:
    case CC_OP_CONST1:
    case CC_OP_CONST2:
    case CC_OP_CONST3:
    case CC_OP_STATIC:
    case CC_OP_DYNAMIC:
        break;
    }

    switch (s->cc_op) {
    case CC_OP_CONST0:
    case CC_OP_CONST1:
    case CC_OP_CONST2:
    case CC_OP_CONST3:
        tcg_gen_movi_i32(cc_op, s->cc_op - CC_OP_CONST0);
        break;
    case CC_OP_STATIC:
        break;
    case CC_OP_NZ:
        tcg_gen_setcondi_i64(TCG_COND_NE, cc_dst, cc_dst, 0);
        tcg_gen_extrl_i64_i32(cc_op, cc_dst);
        break;
    case CC_OP_ABS_64:
    case CC_OP_NABS_64:
    case CC_OP_ABS_32:
    case CC_OP_NABS_32:
    case CC_OP_LTGT0_32:
    case CC_OP_LTGT0_64:
    case CC_OP_COMP_32:
    case CC_OP_COMP_64:
    case CC_OP_NZ_F32:
    case CC_OP_NZ_F64:
    case CC_OP_FLOGR:
    case CC_OP_LCBB:
    case CC_OP_MULS_32:
        gen_helper_calc_cc(cc_op, tcg_env, local_cc_op, dummy, cc_dst, dummy);
        break;
    case CC_OP_ADDU:
    case CC_OP_ICM:
    case CC_OP_LTGT_32:
    case CC_OP_LTGT_64:
    case CC_OP_LTUGTU_32:
    case CC_OP_LTUGTU_64:
    case CC_OP_TM_32:
    case CC_OP_TM_64:
    case CC_OP_SLA:
    case CC_OP_SUBU:
    case CC_OP_NZ_F128:
    case CC_OP_VC:
    case CC_OP_MULS_64:
        gen_helper_calc_cc(cc_op, tcg_env, local_cc_op, cc_src, cc_dst, dummy);
        break;
    case CC_OP_ADD_64:
    case CC_OP_SUB_64:
    case CC_OP_ADD_32:
    case CC_OP_SUB_32:
        gen_helper_calc_cc(cc_op, tcg_env, local_cc_op, cc_src, cc_dst, cc_vr);
        break;
    case CC_OP_DYNAMIC:
        gen_helper_calc_cc(cc_op, tcg_env, cc_op, cc_src, cc_dst, cc_vr);
        break;
    default:
        g_assert_not_reached();
    }

    set_cc_static(s);
}

static bool use_goto_tb(DisasContext *s, uint64_t dest)
{
    return translator_use_goto_tb(&s->base, dest);
}

static void account_noninline_branch(DisasContext *s, int cc_op)
{
#ifdef DEBUG_INLINE_BRANCHES
    inline_branch_miss[cc_op]++;
#endif
}

static void account_inline_branch(DisasContext *s, int cc_op)
{
#ifdef DEBUG_INLINE_BRANCHES
    inline_branch_hit[cc_op]++;
#endif
}

static const TCGCond ltgt_cond[16] = {
    TCG_COND_NEVER,  TCG_COND_NEVER,
    TCG_COND_GT,     TCG_COND_GT,
    TCG_COND_LT,     TCG_COND_LT,
    TCG_COND_NE,     TCG_COND_NE,
    TCG_COND_EQ,     TCG_COND_EQ,
    TCG_COND_GE,     TCG_COND_GE,
    TCG_COND_LE,     TCG_COND_LE,
    TCG_COND_ALWAYS, TCG_COND_ALWAYS,
};

static const TCGCond nz_cond[16] = {
    TCG_COND_NEVER, TCG_COND_NEVER,
    TCG_COND_NEVER, TCG_COND_NEVER,
    TCG_COND_NE, TCG_COND_NE,
    TCG_COND_NE, TCG_COND_NE,
    TCG_COND_EQ, TCG_COND_EQ,
    TCG_COND_EQ, TCG_COND_EQ,
    TCG_COND_ALWAYS, TCG_COND_ALWAYS,
    TCG_COND_ALWAYS, TCG_COND_ALWAYS,
};

static void disas_jcc(DisasContext *s, DisasCompare *c, uint32_t mask)
{
    TCGCond cond;
    enum cc_op old_cc_op = s->cc_op;

    if (mask == 15 || mask == 0) {
        c->cond = (mask ? TCG_COND_ALWAYS : TCG_COND_NEVER);
        c->u.s32.a = cc_op;
        c->u.s32.b = cc_op;
        c->is_64 = false;
        return;
    }

    switch (old_cc_op) {
    case CC_OP_LTGT0_32:
    case CC_OP_LTGT0_64:
    case CC_OP_LTGT_32:
    case CC_OP_LTGT_64:
        cond = ltgt_cond[mask];
        if (cond == TCG_COND_NEVER) {
            goto do_dynamic;
        }
        account_inline_branch(s, old_cc_op);
        break;

    case CC_OP_LTUGTU_32:
    case CC_OP_LTUGTU_64:
        cond = tcg_unsigned_cond(ltgt_cond[mask]);
        if (cond == TCG_COND_NEVER) {
            goto do_dynamic;
        }
        account_inline_branch(s, old_cc_op);
        break;

    case CC_OP_NZ:
        cond = nz_cond[mask];
        if (cond == TCG_COND_NEVER) {
            goto do_dynamic;
        }
        account_inline_branch(s, old_cc_op);
        break;

    case CC_OP_TM_32:
    case CC_OP_TM_64:
        switch (mask) {
        case 8:
            cond = TCG_COND_TSTEQ;
            break;
        case 4 | 2 | 1:
            cond = TCG_COND_TSTNE;
            break;
        default:
            goto do_dynamic;
        }
        account_inline_branch(s, old_cc_op);
        break;

    case CC_OP_ICM:
        switch (mask) {
        case 8:
            cond = TCG_COND_TSTEQ;
            break;
        case 4 | 2 | 1:
        case 4 | 2:
            cond = TCG_COND_TSTNE;
            break;
        default:
            goto do_dynamic;
        }
        account_inline_branch(s, old_cc_op);
        break;

    case CC_OP_FLOGR:
        switch (mask & 0xa) {
        case 8:
            cond = TCG_COND_EQ;
            break;
        case 2:
            cond = TCG_COND_NE;
            break;
        default:
            goto do_dynamic;
        }
        account_inline_branch(s, old_cc_op);
        break;

    case CC_OP_ADDU:
    case CC_OP_SUBU:
        switch (mask) {
        case 8 | 2:
            cond = TCG_COND_EQ;
            break;
        case 4 | 1:
            cond = TCG_COND_NE;
            break;
        case 8 | 4:
            cond = old_cc_op == CC_OP_ADDU ? TCG_COND_EQ : TCG_COND_NE;
            break;
        case 2 | 1:
            cond = old_cc_op == CC_OP_ADDU ? TCG_COND_NE : TCG_COND_EQ;
            break;
        default:
            goto do_dynamic;
        }
        account_inline_branch(s, old_cc_op);
        break;

    default:
    do_dynamic:
        gen_op_calc_cc(s);
        /* FALLTHRU */

    case CC_OP_STATIC:
        account_noninline_branch(s, old_cc_op);
        old_cc_op = CC_OP_STATIC;
        cond = TCG_COND_NEVER;
        break;
    }

    c->is_64 = true;
    switch (old_cc_op) {
    case CC_OP_LTGT0_32:
        c->is_64 = false;
        c->u.s32.a = tcg_temp_new_i32();
        tcg_gen_extrl_i64_i32(c->u.s32.a, cc_dst);
        c->u.s32.b = tcg_constant_i32(0);
        break;
    case CC_OP_LTGT_32:
    case CC_OP_LTUGTU_32:
        c->is_64 = false;
        c->u.s32.a = tcg_temp_new_i32();
        tcg_gen_extrl_i64_i32(c->u.s32.a, cc_src);
        c->u.s32.b = tcg_temp_new_i32();
        tcg_gen_extrl_i64_i32(c->u.s32.b, cc_dst);
        break;

    case CC_OP_LTGT0_64:
    case CC_OP_NZ:
    case CC_OP_FLOGR:
        c->u.s64.a = cc_dst;
        c->u.s64.b = tcg_constant_i64(0);
        break;

    case CC_OP_LTGT_64:
    case CC_OP_LTUGTU_64:
    case CC_OP_TM_32:
    case CC_OP_TM_64:
    case CC_OP_ICM:
        c->u.s64.a = cc_src;
        c->u.s64.b = cc_dst;
        break;

    case CC_OP_ADDU:
    case CC_OP_SUBU:
        c->is_64 = true;
        c->u.s64.b = tcg_constant_i64(0);
        switch (mask) {
        case 8 | 2:
        case 4 | 1:
            c->u.s64.a = cc_dst;
            break;
        case 8 | 4:
        case 2 | 1:
            c->u.s64.a = cc_src;
            break;
        default:
            g_assert_not_reached();
        }
        break;

    case CC_OP_STATIC:
        c->is_64 = false;
        c->u.s32.a = cc_op;

        switch (mask & 8 ? mask ^ 0xf : mask) {
        case 0x1:
            cond = TCG_COND_EQ;
            c->u.s32.b = tcg_constant_i32(3);
            break;
        case 0x2:
            cond = TCG_COND_EQ;
            c->u.s32.b = tcg_constant_i32(2);
            break;
        case 0x4:
            cond = TCG_COND_EQ;
            c->u.s32.b = tcg_constant_i32(1);
            break;
        case 0x2 | 0x1:
            cond = TCG_COND_GTU;
            c->u.s32.b = tcg_constant_i32(1);
            break;
        case 0x4 | 0x1:
            cond = TCG_COND_TSTNE;
            c->u.s32.b = tcg_constant_i32(1);
            break;
        case 0x4 | 0x2:
            cond = TCG_COND_LEU;
            c->u.s32.a = tcg_temp_new_i32();
            c->u.s32.b = tcg_constant_i32(1);
            tcg_gen_addi_i32(c->u.s32.a, cc_op, -1);
            break;
        case 0x4 | 0x2 | 0x1:
            cond = TCG_COND_NE;
            c->u.s32.b = tcg_constant_i32(0);
            break;
        default:
            g_assert_not_reached();
        }
        if (mask & 8) {
            cond = tcg_invert_cond(cond);
        }
        break;

    default:
        abort();
    }
    c->cond = cond;
}

#define F0(N)                         FMT_##N,
#define F1(N, X1)                     F0(N)
#define F2(N, X1, X2)                 F0(N)
#define F3(N, X1, X2, X3)             F0(N)
#define F4(N, X1, X2, X3, X4)         F0(N)
#define F5(N, X1, X2, X3, X4, X5)     F0(N)
#define F6(N, X1, X2, X3, X4, X5, X6) F0(N)

typedef enum {
#include "insn-format.h.inc"
} DisasFormat;

#undef F0
#undef F1
#undef F2
#undef F3
#undef F4
#undef F5
#undef F6

#define have_field(S, F)  have_field1((S), FLD_O_##F)
#define get_field(S, F)   get_field1((S), FLD_O_##F, FLD_C_##F)

static bool have_field1(const DisasContext *s, enum DisasFieldIndexO c)
{
    return (s->fields.presentO >> c) & 1;
}

static int get_field1(const DisasContext *s, enum DisasFieldIndexO o,
                      enum DisasFieldIndexC c)
{
    assert(have_field1(s, o));
    return s->fields.c[c];
}

typedef struct DisasField {
    unsigned int beg:8;
    unsigned int size:8;
    unsigned int type:2;
    unsigned int indexC:6;
    enum DisasFieldIndexO indexO:8;
} DisasField;

typedef struct DisasFormatInfo {
    DisasField op[NUM_C_FIELD];
} DisasFormatInfo;

#define R(N, B)       {  B,  4, 0, FLD_C_r##N, FLD_O_r##N }
#define M(N, B)       {  B,  4, 0, FLD_C_m##N, FLD_O_m##N }
#define V(N, B)       {  B,  4, 3, FLD_C_v##N, FLD_O_v##N }
#define BD(N, BB, BD) { BB,  4, 0, FLD_C_b##N, FLD_O_b##N }, \
                      { BD, 12, 0, FLD_C_d##N, FLD_O_d##N }
#define BXD(N)        { 16,  4, 0, FLD_C_b##N, FLD_O_b##N }, \
                      { 12,  4, 0, FLD_C_x##N, FLD_O_x##N }, \
                      { 20, 12, 0, FLD_C_d##N, FLD_O_d##N }
#define BDL(N)        { 16,  4, 0, FLD_C_b##N, FLD_O_b##N }, \
                      { 20, 20, 2, FLD_C_d##N, FLD_O_d##N }
#define BXDL(N)       { 16,  4, 0, FLD_C_b##N, FLD_O_b##N }, \
                      { 12,  4, 0, FLD_C_x##N, FLD_O_x##N }, \
                      { 20, 20, 2, FLD_C_d##N, FLD_O_d##N }
#define I(N, B, S)    {  B,  S, 1, FLD_C_i##N, FLD_O_i##N }
#define L(N, B, S)    {  B,  S, 0, FLD_C_l##N, FLD_O_l##N }

#define F0(N)                     { { } },
#define F1(N, X1)                 { { X1 } },
#define F2(N, X1, X2)             { { X1, X2 } },
#define F3(N, X1, X2, X3)         { { X1, X2, X3 } },
#define F4(N, X1, X2, X3, X4)     { { X1, X2, X3, X4 } },
#define F5(N, X1, X2, X3, X4, X5) { { X1, X2, X3, X4, X5 } },
#define F6(N, X1, X2, X3, X4, X5, X6)       { { X1, X2, X3, X4, X5, X6 } },

static const DisasFormatInfo format_info[] = {
#include "insn-format.h.inc"
};

#undef F0
#undef F1
#undef F2
#undef F3
#undef F4
#undef F5
#undef F6
#undef R
#undef M
#undef V
#undef BD
#undef BXD
#undef BDL
#undef BXDL
#undef I
#undef L

typedef struct {
    TCGv_i64 out, out2, in1, in2;
    TCGv_i64 addr1;
    TCGv_i128 out_128, in1_128, in2_128;
} DisasOps;

#define SPEC_r1_even    1
#define SPEC_r2_even    2
#define SPEC_r3_even    4
#define SPEC_r1_f128    8
#define SPEC_r2_f128    16

#define DISAS_PC_UPDATED        DISAS_TARGET_0

#define DISAS_PC_CC_UPDATED     DISAS_TARGET_2


#define IF_AFP1     0x0001
#define IF_AFP2     0x0002
#define IF_AFP3     0x0004
#define IF_BFP      0x0008
#define IF_DFP      0x0010
#define IF_PRIV     0x0020
#define IF_VEC      0x0040
#define IF_IO       0x0080

struct DisasInsn {
    unsigned opc:16;
    unsigned flags:16;
    DisasFormat fmt:8;
    unsigned fac:8;
    unsigned spec:8;

    const char *name;

    void (*help_in1)(DisasContext *, DisasOps *);
    void (*help_in2)(DisasContext *, DisasOps *);
    void (*help_prep)(DisasContext *, DisasOps *);

    void (*help_wout)(DisasContext *, DisasOps *);
    void (*help_cout)(DisasContext *, DisasOps *);

    DisasJumpType (*help_op)(DisasContext *, DisasOps *);

    uint64_t data;
};

static DisasJumpType help_goto_direct(DisasContext *s, uint64_t dest)
{
    update_cc_op(s);
    per_breaking_event(s);
    per_branch(s, tcg_constant_i64(dest));

    if (dest == s->pc_tmp) {
        return DISAS_NEXT;
    }
    if (use_goto_tb(s, dest)) {
        tcg_gen_goto_tb(0);
        tcg_gen_movi_i64(psw_addr, dest);
        tcg_gen_exit_tb(s->base.tb, 0);
        return DISAS_NORETURN;
    } else {
        tcg_gen_movi_i64(psw_addr, dest);
        return DISAS_PC_CC_UPDATED;
    }
}

static DisasJumpType help_goto_indirect(DisasContext *s, TCGv_i64 dest)
{
    update_cc_op(s);
    per_breaking_event(s);
    tcg_gen_mov_i64(psw_addr, dest);
    per_branch(s, psw_addr);
    return DISAS_PC_CC_UPDATED;
}

static DisasJumpType help_branch(DisasContext *s, DisasCompare *c,
                                 bool is_imm, int imm, TCGv_i64 cdest)
{
    uint64_t dest = s->base.pc_next + (int64_t)imm * 2;
    TCGLabel *lab;

    if (c->cond == TCG_COND_NEVER) {
        return DISAS_NEXT;
    }
    if (is_imm) {
        if (c->cond == TCG_COND_ALWAYS
            || (dest == s->pc_tmp &&
                !(s->base.tb->flags & FLAG_MASK_PER_BRANCH))) {
            return help_goto_direct(s, dest);
        }
    } else {
        if (!cdest) {
            return DISAS_NEXT;
        }
        if (c->cond == TCG_COND_ALWAYS) {
            return help_goto_indirect(s, cdest);
        }
    }

    update_cc_op(s);

    lab = gen_new_label();
    if (c->is_64) {
        tcg_gen_brcond_i64(tcg_invert_cond(c->cond),
                           c->u.s64.a, c->u.s64.b, lab);
    } else {
        tcg_gen_brcond_i32(tcg_invert_cond(c->cond),
                           c->u.s32.a, c->u.s32.b, lab);
    }

    per_breaking_event(s);
    if (is_imm) {
        tcg_gen_movi_i64(psw_addr, dest);
    } else {
        tcg_gen_mov_i64(psw_addr, cdest);
    }
    per_branch(s, psw_addr);

    if (is_imm && use_goto_tb(s, dest)) {
        tcg_gen_goto_tb(0);
        tcg_gen_exit_tb(s->base.tb, 0);
    } else {
        tcg_gen_lookup_and_goto_ptr();
    }

    gen_set_label(lab);

    tcg_gen_movi_i64(psw_addr, s->pc_tmp);
    if (use_goto_tb(s, s->pc_tmp)) {
        tcg_gen_goto_tb(1);
        tcg_gen_exit_tb(s->base.tb, 1);
        return DISAS_NORETURN;
    }
    return DISAS_PC_CC_UPDATED;
}

static DisasJumpType op_abs(DisasContext *s, DisasOps *o)
{
    tcg_gen_abs_i64(o->out, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_absf32(DisasContext *s, DisasOps *o)
{
    tcg_gen_andi_i64(o->out, o->in2, 0x7fffffffull);
    return DISAS_NEXT;
}

static DisasJumpType op_absf64(DisasContext *s, DisasOps *o)
{
    tcg_gen_andi_i64(o->out, o->in2, 0x7fffffffffffffffull);
    return DISAS_NEXT;
}

static DisasJumpType op_absf128(DisasContext *s, DisasOps *o)
{
    tcg_gen_andi_i64(o->out, o->in1, 0x7fffffffffffffffull);
    tcg_gen_mov_i64(o->out2, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_add(DisasContext *s, DisasOps *o)
{
    tcg_gen_add_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_addu64(DisasContext *s, DisasOps *o)
{
    tcg_gen_movi_i64(cc_src, 0);
    tcg_gen_add2_i64(o->out, cc_src, o->in1, cc_src, o->in2, cc_src);
    return DISAS_NEXT;
}

static void compute_carry(DisasContext *s)
{
    switch (s->cc_op) {
    case CC_OP_ADDU:
        break;
    case CC_OP_SUBU:
        tcg_gen_addi_i64(cc_src, cc_src, 1);
        break;
    default:
        gen_op_calc_cc(s);
        /* fall through */
    case CC_OP_STATIC:
        tcg_gen_extu_i32_i64(cc_src, cc_op);
        tcg_gen_shri_i64(cc_src, cc_src, 1);
        break;
    }
}

static DisasJumpType op_addc32(DisasContext *s, DisasOps *o)
{
    compute_carry(s);
    tcg_gen_add_i64(o->out, o->in1, o->in2);
    tcg_gen_add_i64(o->out, o->out, cc_src);
    return DISAS_NEXT;
}

static DisasJumpType op_addc64(DisasContext *s, DisasOps *o)
{
    compute_carry(s);
    tcg_gen_addcio_i64(o->out, cc_src, o->in1, o->in2, cc_src);
    return DISAS_NEXT;
}

static DisasJumpType op_asi(DisasContext *s, DisasOps *o)
{
    bool non_atomic = !s390_has_feat(S390_FEAT_STFLE_45);

    o->in1 = tcg_temp_new_i64();
    if (non_atomic) {
        tcg_gen_qemu_ld_tl(o->in1, o->addr1, get_mem_index(s), s->insn->data);
    } else {
        tcg_gen_atomic_fetch_add_i64(o->in1, o->addr1, o->in2, get_mem_index(s),
                                     s->insn->data);
    }

    tcg_gen_add_i64(o->out, o->in1, o->in2);

    if (non_atomic) {
        tcg_gen_qemu_st_tl(o->out, o->addr1, get_mem_index(s), s->insn->data);
    }
    return DISAS_NEXT;
}

static DisasJumpType op_asiu64(DisasContext *s, DisasOps *o)
{
    bool non_atomic = !s390_has_feat(S390_FEAT_STFLE_45);

    o->in1 = tcg_temp_new_i64();
    if (non_atomic) {
        tcg_gen_qemu_ld_tl(o->in1, o->addr1, get_mem_index(s), s->insn->data);
    } else {
        tcg_gen_atomic_fetch_add_i64(o->in1, o->addr1, o->in2, get_mem_index(s),
                                     s->insn->data);
    }

    tcg_gen_movi_i64(cc_src, 0);
    tcg_gen_add2_i64(o->out, cc_src, o->in1, cc_src, o->in2, cc_src);

    if (non_atomic) {
        tcg_gen_qemu_st_tl(o->out, o->addr1, get_mem_index(s), s->insn->data);
    }
    return DISAS_NEXT;
}

static DisasJumpType op_aeb(DisasContext *s, DisasOps *o)
{
    gen_helper_aeb(o->out, tcg_env, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_adb(DisasContext *s, DisasOps *o)
{
    gen_helper_adb(o->out, tcg_env, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_axb(DisasContext *s, DisasOps *o)
{
    gen_helper_axb(o->out_128, tcg_env, o->in1_128, o->in2_128);
    return DISAS_NEXT;
}

static DisasJumpType op_and(DisasContext *s, DisasOps *o)
{
    tcg_gen_and_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_andi(DisasContext *s, DisasOps *o)
{
    int shift = s->insn->data & 0xff;
    int size = s->insn->data >> 8;
    uint64_t mask = ((1ull << size) - 1) << shift;
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_shli_i64(t, o->in2, shift);
    tcg_gen_ori_i64(t, t, ~mask);
    tcg_gen_and_i64(o->out, o->in1, t);

    tcg_gen_andi_i64(cc_dst, o->out, mask);
    set_cc_nz_u64(s, cc_dst);
    return DISAS_NEXT;
}

static DisasJumpType op_andc(DisasContext *s, DisasOps *o)
{
    tcg_gen_andc_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_orc(DisasContext *s, DisasOps *o)
{
    tcg_gen_orc_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_nand(DisasContext *s, DisasOps *o)
{
    tcg_gen_nand_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_nor(DisasContext *s, DisasOps *o)
{
    tcg_gen_nor_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_nxor(DisasContext *s, DisasOps *o)
{
    tcg_gen_eqv_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_ni(DisasContext *s, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();

    if (!s390_has_feat(S390_FEAT_INTERLOCKED_ACCESS_2)) {
        tcg_gen_qemu_ld_tl(o->in1, o->addr1, get_mem_index(s), s->insn->data);
    } else {
        tcg_gen_atomic_fetch_and_i64(o->in1, o->addr1, o->in2, get_mem_index(s),
                                     s->insn->data);
    }

    tcg_gen_and_i64(o->out, o->in1, o->in2);

    if (!s390_has_feat(S390_FEAT_INTERLOCKED_ACCESS_2)) {
        tcg_gen_qemu_st_tl(o->out, o->addr1, get_mem_index(s), s->insn->data);
    }
    return DISAS_NEXT;
}

static DisasJumpType op_bas(DisasContext *s, DisasOps *o)
{
    pc_to_link_info(o->out, s, s->pc_tmp);
    if (o->in2) {
        return help_goto_indirect(s, o->in2);
    } else {
        return DISAS_NEXT;
    }
}

static void save_link_info(DisasContext *s, DisasOps *o)
{
    TCGv_i64 t;

    if (s->base.tb->flags & (FLAG_MASK_32 | FLAG_MASK_64)) {
        pc_to_link_info(o->out, s, s->pc_tmp);
        return;
    }
    gen_op_calc_cc(s);
    tcg_gen_andi_i64(o->out, o->out, 0xffffffff00000000ull);
    tcg_gen_ori_i64(o->out, o->out, ((s->ilen / 2) << 30) | s->pc_tmp);
    t = tcg_temp_new_i64();
    tcg_gen_shri_i64(t, psw_mask, 16);
    tcg_gen_andi_i64(t, t, 0x0f000000);
    tcg_gen_or_i64(o->out, o->out, t);
    tcg_gen_extu_i32_i64(t, cc_op);
    tcg_gen_shli_i64(t, t, 28);
    tcg_gen_or_i64(o->out, o->out, t);
}

static DisasJumpType op_bal(DisasContext *s, DisasOps *o)
{
    save_link_info(s, o);
    if (o->in2) {
        return help_goto_indirect(s, o->in2);
    } else {
        return DISAS_NEXT;
    }
}

#define disas_jdest(s, ri, is_imm, imm, cdest) do {                            \
    if (have_field(s, ri)) {                                                   \
        if (unlikely(s->ex_value)) {                                           \
            cdest = tcg_temp_new_i64();                                        \
            tcg_gen_ld_i64(cdest, tcg_env, offsetof(CPUS390XState, ex_target));\
            tcg_gen_addi_i64(cdest, cdest, (int64_t)get_field(s, ri) * 2);     \
            is_imm = false;                                                    \
        } else {                                                               \
            is_imm = true;                                                     \
        }                                                                      \
    } else {                                                                   \
        is_imm = false;                                                        \
    }                                                                          \
    imm = is_imm ? get_field(s, ri) : 0;                                       \
} while (false)

static DisasJumpType op_basi(DisasContext *s, DisasOps *o)
{
    DisasCompare c;
    bool is_imm;
    int imm;

    pc_to_link_info(o->out, s, s->pc_tmp);

    disas_jdest(s, i2, is_imm, imm, o->in2);
    disas_jcc(s, &c, 0xf);
    return help_branch(s, &c, is_imm, imm, o->in2);
}

static DisasJumpType op_bc(DisasContext *s, DisasOps *o)
{
    int m1 = get_field(s, m1);
    DisasCompare c;
    bool is_imm;
    int imm;

    if (have_field(s, r2) && get_field(s, r2) == 0) {
        if (m1 == 14) {
            tcg_gen_mb(TCG_MO_ALL | TCG_BAR_SC);
        }
        if (m1 == 15) {
            tcg_gen_mb(TCG_MO_ALL | TCG_BAR_SC);
        }
        return DISAS_NEXT;
    }

    disas_jdest(s, i2, is_imm, imm, o->in2);
    disas_jcc(s, &c, m1);
    return help_branch(s, &c, is_imm, imm, o->in2);
}

static DisasJumpType op_bct32(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    DisasCompare c;
    bool is_imm;
    TCGv_i64 t;
    int imm;

    c.cond = TCG_COND_NE;
    c.is_64 = false;

    t = tcg_temp_new_i64();
    tcg_gen_subi_i64(t, regs[r1], 1);
    store_reg32_i64(r1, t);
    c.u.s32.a = tcg_temp_new_i32();
    c.u.s32.b = tcg_constant_i32(0);
    tcg_gen_extrl_i64_i32(c.u.s32.a, t);

    disas_jdest(s, i2, is_imm, imm, o->in2);
    return help_branch(s, &c, is_imm, imm, o->in2);
}

static DisasJumpType op_bcth(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int imm = get_field(s, i2);
    DisasCompare c;
    TCGv_i64 t;

    c.cond = TCG_COND_NE;
    c.is_64 = false;

    t = tcg_temp_new_i64();
    tcg_gen_shri_i64(t, regs[r1], 32);
    tcg_gen_subi_i64(t, t, 1);
    store_reg32h_i64(r1, t);
    c.u.s32.a = tcg_temp_new_i32();
    c.u.s32.b = tcg_constant_i32(0);
    tcg_gen_extrl_i64_i32(c.u.s32.a, t);

    return help_branch(s, &c, 1, imm, o->in2);
}

static DisasJumpType op_bct64(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    DisasCompare c;
    bool is_imm;
    int imm;

    c.cond = TCG_COND_NE;
    c.is_64 = true;

    tcg_gen_subi_i64(regs[r1], regs[r1], 1);
    c.u.s64.a = regs[r1];
    c.u.s64.b = tcg_constant_i64(0);

    disas_jdest(s, i2, is_imm, imm, o->in2);
    return help_branch(s, &c, is_imm, imm, o->in2);
}

static DisasJumpType op_bx32(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r3 = get_field(s, r3);
    DisasCompare c;
    bool is_imm;
    TCGv_i64 t;
    int imm;

    c.cond = (s->insn->data ? TCG_COND_LE : TCG_COND_GT);
    c.is_64 = false;

    t = tcg_temp_new_i64();
    tcg_gen_add_i64(t, regs[r1], regs[r3]);
    c.u.s32.a = tcg_temp_new_i32();
    c.u.s32.b = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(c.u.s32.a, t);
    tcg_gen_extrl_i64_i32(c.u.s32.b, regs[r3 | 1]);
    store_reg32_i64(r1, t);

    disas_jdest(s, i2, is_imm, imm, o->in2);
    return help_branch(s, &c, is_imm, imm, o->in2);
}

static DisasJumpType op_bx64(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r3 = get_field(s, r3);
    DisasCompare c;
    bool is_imm;
    int imm;

    c.cond = (s->insn->data ? TCG_COND_LE : TCG_COND_GT);
    c.is_64 = true;

    if (r1 == (r3 | 1)) {
        c.u.s64.b = load_reg(r3 | 1);
    } else {
        c.u.s64.b = regs[r3 | 1];
    }

    tcg_gen_add_i64(regs[r1], regs[r1], regs[r3]);
    c.u.s64.a = regs[r1];

    disas_jdest(s, i2, is_imm, imm, o->in2);
    return help_branch(s, &c, is_imm, imm, o->in2);
}

static DisasJumpType op_cj(DisasContext *s, DisasOps *o)
{
    int imm, m3 = get_field(s, m3);
    bool is_imm;
    DisasCompare c;

    c.cond = ltgt_cond[m3];
    if (s->insn->data) {
        c.cond = tcg_unsigned_cond(c.cond);
    }
    c.is_64 = true;
    c.u.s64.a = o->in1;
    c.u.s64.b = o->in2;

    o->out = NULL;
    disas_jdest(s, i4, is_imm, imm, o->out);
    if (!is_imm && !o->out) {
        imm = 0;
        o->out = get_address(s, 0, get_field(s, b4),
                             get_field(s, d4));
    }

    return help_branch(s, &c, is_imm, imm, o->out);
}

static DisasJumpType op_ceb(DisasContext *s, DisasOps *o)
{
    gen_helper_ceb(cc_op, tcg_env, o->in1, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_cdb(DisasContext *s, DisasOps *o)
{
    gen_helper_cdb(cc_op, tcg_env, o->in1, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_cxb(DisasContext *s, DisasOps *o)
{
    gen_helper_cxb(cc_op, tcg_env, o->in1_128, o->in2_128);
    set_cc_static(s);
    return DISAS_NEXT;
}

static TCGv_i32 fpinst_extract_m34(DisasContext *s, bool m3_with_fpe,
                                   bool m4_with_fpe)
{
    const bool fpe = s390_has_feat(S390_FEAT_FLOATING_POINT_EXT);
    uint8_t m3 = get_field(s, m3);
    uint8_t m4 = get_field(s, m4);

    if (!fpe && m3_with_fpe) {
        m3 = 0;
    }
    if (!fpe && m4_with_fpe) {
        m4 = 0;
    }

    if (m3 == 2 || m3 > 7 || (!fpe && m3 == 3)) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return NULL;
    }

    return tcg_constant_i32(deposit32(m3, 4, 4, m4));
}

static DisasJumpType op_cfeb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_cfeb(o->out, tcg_env, o->in2, m34);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_cfdb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_cfdb(o->out, tcg_env, o->in2, m34);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_cfxb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_cfxb(o->out, tcg_env, o->in2_128, m34);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_cgeb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_cgeb(o->out, tcg_env, o->in2, m34);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_cgdb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_cgdb(o->out, tcg_env, o->in2, m34);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_cgxb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_cgxb(o->out, tcg_env, o->in2_128, m34);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_clfeb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, false);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_clfeb(o->out, tcg_env, o->in2, m34);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_clfdb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, false);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_clfdb(o->out, tcg_env, o->in2, m34);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_clfxb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, false);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_clfxb(o->out, tcg_env, o->in2_128, m34);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_clgeb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, false);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_clgeb(o->out, tcg_env, o->in2, m34);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_clgdb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, false);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_clgdb(o->out, tcg_env, o->in2, m34);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_clgxb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, false);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_clgxb(o->out, tcg_env, o->in2_128, m34);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_cegb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, true, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_cegb(o->out, tcg_env, o->in2, m34);
    return DISAS_NEXT;
}

static DisasJumpType op_cdgb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, true, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_cdgb(o->out, tcg_env, o->in2, m34);
    return DISAS_NEXT;
}

static DisasJumpType op_cxgb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, true, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_cxgb(o->out_128, tcg_env, o->in2, m34);
    return DISAS_NEXT;
}

static DisasJumpType op_celgb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, false);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_celgb(o->out, tcg_env, o->in2, m34);
    return DISAS_NEXT;
}

static DisasJumpType op_cdlgb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, false);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_cdlgb(o->out, tcg_env, o->in2, m34);
    return DISAS_NEXT;
}

static DisasJumpType op_cxlgb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, false);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_cxlgb(o->out_128, tcg_env, o->in2, m34);
    return DISAS_NEXT;
}

static DisasJumpType op_cksm(DisasContext *s, DisasOps *o)
{
    int r2 = get_field(s, r2);
    TCGv_i128 pair = tcg_temp_new_i128();
    TCGv_i64 len = tcg_temp_new_i64();

    gen_helper_cksm(pair, tcg_env, o->in1, o->in2, regs[r2 + 1]);
    set_cc_static(s);
    tcg_gen_extr_i128_i64(o->out, len, pair);

    tcg_gen_add_i64(regs[r2], regs[r2], len);
    tcg_gen_sub_i64(regs[r2 + 1], regs[r2 + 1], len);

    return DISAS_NEXT;
}

static DisasJumpType op_clc(DisasContext *s, DisasOps *o)
{
    int l = get_field(s, l1);
    TCGv_i64 src;
    TCGv_i32 vl;
    MemOp mop;

    switch (l + 1) {
    case 1:
    case 2:
    case 4:
    case 8:
        mop = ctz32(l + 1) | MO_TE;
        src = tcg_temp_new_i64();
        tcg_gen_qemu_ld_tl(src, o->addr1, get_mem_index(s), mop);
        tcg_gen_qemu_ld_tl(cc_dst, o->in2, get_mem_index(s), mop);
        gen_op_update2_cc_i64(s, CC_OP_LTUGTU_64, src, cc_dst);
        return DISAS_NEXT;
    default:
        vl = tcg_constant_i32(l);
        gen_helper_clc(cc_op, tcg_env, vl, o->addr1, o->in2);
        set_cc_static(s);
        return DISAS_NEXT;
    }
}

static DisasJumpType op_clcl(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r2 = get_field(s, r2);
    TCGv_i32 t1, t2;

    if (r1 & 1 || r2 & 1) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    t1 = tcg_constant_i32(r1);
    t2 = tcg_constant_i32(r2);
    gen_helper_clcl(cc_op, tcg_env, t1, t2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_clcle(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r3 = get_field(s, r3);
    TCGv_i32 t1, t3;

    if (r1 & 1 || r3 & 1) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    t1 = tcg_constant_i32(r1);
    t3 = tcg_constant_i32(r3);
    gen_helper_clcle(cc_op, tcg_env, t1, o->in2, t3);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_clclu(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r3 = get_field(s, r3);
    TCGv_i32 t1, t3;

    if (r1 & 1 || r3 & 1) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    t1 = tcg_constant_i32(r1);
    t3 = tcg_constant_i32(r3);
    gen_helper_clclu(cc_op, tcg_env, t1, o->in2, t3);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_clm(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m3 = tcg_constant_i32(get_field(s, m3));
    TCGv_i32 t1 = tcg_temp_new_i32();

    tcg_gen_extrl_i64_i32(t1, o->in1);
    gen_helper_clm(cc_op, tcg_env, t1, m3, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_clst(DisasContext *s, DisasOps *o)
{
    TCGv_i128 pair = tcg_temp_new_i128();

    gen_helper_clst(pair, tcg_env, regs[0], o->in1, o->in2);
    tcg_gen_extr_i128_i64(o->in2, o->in1, pair);

    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_cps(DisasContext *s, DisasOps *o)
{
    TCGv_i64 t = tcg_temp_new_i64();
    tcg_gen_andi_i64(t, o->in1, 0x8000000000000000ull);
    tcg_gen_andi_i64(o->out, o->in2, 0x7fffffffffffffffull);
    tcg_gen_or_i64(o->out, o->out, t);
    return DISAS_NEXT;
}

static DisasJumpType op_cs(DisasContext *s, DisasOps *o)
{
    int d2 = get_field(s, d2);
    int b2 = get_field(s, b2);
    TCGv_i64 addr, cc;

    addr = get_address(s, 0, b2, d2);
    tcg_gen_atomic_cmpxchg_i64(o->out, addr, o->in2, o->in1,
                               get_mem_index(s), s->insn->data | MO_ALIGN);

    cc = tcg_temp_new_i64();
    tcg_gen_setcond_i64(TCG_COND_NE, cc, o->in2, o->out);
    tcg_gen_extrl_i64_i32(cc_op, cc);
    set_cc_static(s);

    return DISAS_NEXT;
}

static DisasJumpType op_cdsg(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);

    o->out_128 = tcg_temp_new_i128();
    tcg_gen_concat_i64_i128(o->out_128, regs[r1 + 1], regs[r1]);

    tcg_gen_atomic_cmpxchg_i128(o->out_128, o->addr1, o->out_128, o->in2_128,
                                get_mem_index(s), MO_BE | MO_128 | MO_ALIGN);

    tcg_gen_extr_i128_i64(cc_src, cc_dst, o->out_128);
    tcg_gen_xor_i64(cc_dst, cc_dst, regs[r1]);
    tcg_gen_xor_i64(cc_src, cc_src, regs[r1 + 1]);
    tcg_gen_or_i64(cc_dst, cc_dst, cc_src);
    set_cc_nz_u64(s, cc_dst);

    return DISAS_NEXT;
}

static DisasJumpType op_csst(DisasContext *s, DisasOps *o)
{
    int r3 = get_field(s, r3);
    TCGv_i32 t_r3 = tcg_constant_i32(r3);

    if (tb_cflags(s->base.tb) & CF_PARALLEL) {
        gen_helper_csst_parallel(cc_op, tcg_env, t_r3, o->addr1, o->in2);
    } else {
        gen_helper_csst(cc_op, tcg_env, t_r3, o->addr1, o->in2);
    }

    set_cc_static(s);
    return DISAS_NEXT;
}

#ifndef CONFIG_USER_ONLY
static DisasJumpType op_csp(DisasContext *s, DisasOps *o)
{
    MemOp mop = s->insn->data;
    TCGv_i64 addr, old, cc;
    TCGLabel *lab = gen_new_label();

    addr = tcg_temp_new_i64();
    old = tcg_temp_new_i64();
    tcg_gen_andi_i64(addr, o->in2, -1ULL << (mop & MO_SIZE));
    tcg_gen_atomic_cmpxchg_i64(old, addr, o->in1, o->out2,
                               get_mem_index(s), mop | MO_ALIGN);

    cc = tcg_temp_new_i64();
    tcg_gen_setcond_i64(TCG_COND_NE, cc, o->in1, old);
    tcg_gen_extrl_i64_i32(cc_op, cc);

    if ((mop & MO_SIZE) == MO_32) {
        tcg_gen_deposit_i64(o->out, o->out, old, 0, 32);
    } else {
        tcg_gen_mov_i64(o->out, old);
    }

    tcg_gen_xori_i64(cc, cc, 1);
    tcg_gen_and_i64(cc, cc, o->in2);
    tcg_gen_brcondi_i64(TCG_COND_EQ, cc, 0, lab);

    gen_helper_purge(tcg_env);
    gen_set_label(lab);

    return DISAS_NEXT;
}
#endif

static DisasJumpType op_cvb(DisasContext *s, DisasOps *o)
{
    TCGv_i64 t = tcg_temp_new_i64();
    tcg_gen_qemu_ld_i64(t, o->addr1, get_mem_index(s), MO_TEUQ);
    gen_helper_cvb(tcg_env, tcg_constant_i32(get_field(s, r1)), t);
    return DISAS_NEXT;
}

static DisasJumpType op_cvbg(DisasContext *s, DisasOps *o)
{
    TCGv_i128 t = tcg_temp_new_i128();
    tcg_gen_qemu_ld_i128(t, o->addr1, get_mem_index(s), MO_TE | MO_128);
    gen_helper_cvbg(o->out, tcg_env, t);
    return DISAS_NEXT;
}

static DisasJumpType op_cvd(DisasContext *s, DisasOps *o)
{
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i32 t2 = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(t2, o->in1);
    gen_helper_cvd(t1, t2);
    tcg_gen_qemu_st_i64(t1, o->in2, get_mem_index(s), MO_TEUQ);
    return DISAS_NEXT;
}

static DisasJumpType op_cvdg(DisasContext *s, DisasOps *o)
{
    TCGv_i128 t = tcg_temp_new_i128();
    gen_helper_cvdg(t, o->in1);
    tcg_gen_qemu_st_i128(t, o->in2, get_mem_index(s), MO_TE | MO_128);
    return DISAS_NEXT;
}

static DisasJumpType op_ct(DisasContext *s, DisasOps *o)
{
    int m3 = get_field(s, m3);
    TCGLabel *lab = gen_new_label();
    TCGCond c;

    c = tcg_invert_cond(ltgt_cond[m3]);
    if (s->insn->data) {
        c = tcg_unsigned_cond(c);
    }
    tcg_gen_brcond_i64(c, o->in1, o->in2, lab);

    gen_trap(s);

    gen_set_label(lab);
    return DISAS_NEXT;
}

static DisasJumpType op_cuXX(DisasContext *s, DisasOps *o)
{
    int m3 = get_field(s, m3);
    int r1 = get_field(s, r1);
    int r2 = get_field(s, r2);
    TCGv_i32 tr1, tr2, chk;

    if ((r1 | r2) & 1) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }
    if (!s390_has_feat(S390_FEAT_ETF3_ENH)) {
        m3 = 0;
    }

    tr1 = tcg_constant_i32(r1);
    tr2 = tcg_constant_i32(r2);
    chk = tcg_constant_i32(m3);

    switch (s->insn->data) {
    case 12:
        gen_helper_cu12(cc_op, tcg_env, tr1, tr2, chk);
        break;
    case 14:
        gen_helper_cu14(cc_op, tcg_env, tr1, tr2, chk);
        break;
    case 21:
        gen_helper_cu21(cc_op, tcg_env, tr1, tr2, chk);
        break;
    case 24:
        gen_helper_cu24(cc_op, tcg_env, tr1, tr2, chk);
        break;
    case 41:
        gen_helper_cu41(cc_op, tcg_env, tr1, tr2, chk);
        break;
    case 42:
        gen_helper_cu42(cc_op, tcg_env, tr1, tr2, chk);
        break;
    default:
        g_assert_not_reached();
    }

    set_cc_static(s);
    return DISAS_NEXT;
}

#ifndef CONFIG_USER_ONLY
static DisasJumpType op_diag(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r3 = tcg_constant_i32(get_field(s, r3));
    TCGv_i32 func_code = tcg_constant_i32(get_field(s, i2));

    gen_helper_diag(tcg_env, r1, r3, func_code);
    return DISAS_NEXT;
}
#endif

static DisasJumpType op_divs32(DisasContext *s, DisasOps *o)
{
    gen_helper_divs32(o->out, tcg_env, o->in1, o->in2);
    tcg_gen_extr32_i64(o->out2, o->out, o->out);
    return DISAS_NEXT;
}

static DisasJumpType op_divu32(DisasContext *s, DisasOps *o)
{
    gen_helper_divu32(o->out, tcg_env, o->in1, o->in2);
    tcg_gen_extr32_i64(o->out2, o->out, o->out);
    return DISAS_NEXT;
}

static DisasJumpType op_divs64(DisasContext *s, DisasOps *o)
{
    TCGv_i128 t = tcg_temp_new_i128();

    gen_helper_divs64(t, tcg_env, o->in1, o->in2);
    tcg_gen_extr_i128_i64(o->out2, o->out, t);
    return DISAS_NEXT;
}

static DisasJumpType op_divu64(DisasContext *s, DisasOps *o)
{
    TCGv_i128 t = tcg_temp_new_i128();

    gen_helper_divu64(t, tcg_env, o->out, o->out2, o->in2);
    tcg_gen_extr_i128_i64(o->out2, o->out, t);
    return DISAS_NEXT;
}

static DisasJumpType op_deb(DisasContext *s, DisasOps *o)
{
    gen_helper_deb(o->out, tcg_env, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_ddb(DisasContext *s, DisasOps *o)
{
    gen_helper_ddb(o->out, tcg_env, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_dxb(DisasContext *s, DisasOps *o)
{
    gen_helper_dxb(o->out_128, tcg_env, o->in1_128, o->in2_128);
    return DISAS_NEXT;
}

static DisasJumpType op_ear(DisasContext *s, DisasOps *o)
{
    int r2 = get_field(s, r2);
    tcg_gen_ld32u_i64(o->out, tcg_env, offsetof(CPUS390XState, aregs[r2]));
    return DISAS_NEXT;
}

static DisasJumpType op_ecag(DisasContext *s, DisasOps *o)
{
    tcg_gen_movi_i64(o->out, -1);
    return DISAS_NEXT;
}

static DisasJumpType op_efpc(DisasContext *s, DisasOps *o)
{
    tcg_gen_ld32u_i64(o->out, tcg_env, offsetof(CPUS390XState, fpc));
    return DISAS_NEXT;
}

static DisasJumpType op_epsw(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r2 = get_field(s, r2);
    TCGv_i64 t = tcg_temp_new_i64();
    TCGv_i64 t_cc = tcg_temp_new_i64();

    gen_op_calc_cc(s);
    tcg_gen_extu_i32_i64(t_cc, cc_op);
    tcg_gen_shri_i64(t, psw_mask, 32);
    tcg_gen_deposit_i64(t, t, t_cc, 12, 2);
    store_reg32_i64(r1, t);
    if (r2 != 0) {
        store_reg32_i64(r2, psw_mask);
    }
    return DISAS_NEXT;
}

static DisasJumpType op_ex(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    TCGv_i32 ilen;
    TCGv_i64 v1;

    if (unlikely(s->ex_value)) {
        gen_program_exception(s, PGM_EXECUTE);
        return DISAS_NORETURN;
    }

    update_psw_addr(s);
    update_cc_op(s);

    if (r1 == 0) {
        v1 = tcg_constant_i64(0);
    } else {
        v1 = regs[r1];
    }

    ilen = tcg_constant_i32(s->ilen);
    gen_helper_ex(tcg_env, ilen, v1, o->in2);

    return DISAS_PC_CC_UPDATED;
}

static DisasJumpType op_fieb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_fieb(o->out, tcg_env, o->in2, m34);
    return DISAS_NEXT;
}

static DisasJumpType op_fidb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_fidb(o->out, tcg_env, o->in2, m34);
    return DISAS_NEXT;
}

static DisasJumpType op_fixb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_fixb(o->out_128, tcg_env, o->in2_128, m34);
    return DISAS_NEXT;
}

static DisasJumpType op_flogr(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_FLOGR, o->in2);

    tcg_gen_clzi_i64(o->out, o->in2, 64);

    tcg_gen_movi_i64(o->out2, 0x8000000000000000ull);
    tcg_gen_shr_i64(o->out2, o->out2, o->out);
    tcg_gen_andc_i64(o->out2, cc_dst, o->out2);
    return DISAS_NEXT;
}

static DisasJumpType op_icm(DisasContext *s, DisasOps *o)
{
    int m3 = get_field(s, m3);
    int pos, len, base = s->insn->data;
    TCGv_i64 tmp = tcg_temp_new_i64();
    uint64_t ccm;

    switch (m3) {
    case 0xf:
        tcg_gen_qemu_ld_i64(tmp, o->in2, get_mem_index(s), MO_TEUL);
        len = 32;
        goto one_insert;

    case 0xc:
    case 0x6:
    case 0x3:
        tcg_gen_qemu_ld_i64(tmp, o->in2, get_mem_index(s), MO_TEUW);
        len = 16;
        goto one_insert;

    case 0x8:
    case 0x4:
    case 0x2:
    case 0x1:
        tcg_gen_qemu_ld_i64(tmp, o->in2, get_mem_index(s), MO_UB);
        len = 8;
        goto one_insert;

    one_insert:
        pos = base + ctz32(m3) * 8;
        tcg_gen_deposit_i64(o->out, o->out, tmp, pos, len);
        ccm = ((1ull << len) - 1) << pos;
        break;

    case 0:
        tcg_gen_qemu_ld_i64(tmp, o->in2, get_mem_index(s), MO_UB);
        gen_op_movi_cc(s, 0);
        return DISAS_NEXT;

    default:
        pos = base + 32 - 8;
        ccm = 0;
        while (m3) {
            if (m3 & 0x8) {
                tcg_gen_qemu_ld_i64(tmp, o->in2, get_mem_index(s), MO_UB);
                tcg_gen_addi_i64(o->in2, o->in2, 1);
                tcg_gen_deposit_i64(o->out, o->out, tmp, pos, 8);
                ccm |= 0xffull << pos;
            }
            m3 = (m3 << 1) & 0xf;
            pos -= 8;
        }
        break;
    }

    tcg_gen_movi_i64(tmp, ccm);
    gen_op_update2_cc_i64(s, CC_OP_ICM, tmp, o->out);
    return DISAS_NEXT;
}

static DisasJumpType op_insi(DisasContext *s, DisasOps *o)
{
    int shift = s->insn->data & 0xff;
    int size = s->insn->data >> 8;
    tcg_gen_deposit_i64(o->out, o->in1, o->in2, shift, size);
    return DISAS_NEXT;
}

static DisasJumpType op_ipm(DisasContext *s, DisasOps *o)
{
    TCGv_i64 t1, t2;

    gen_op_calc_cc(s);
    t1 = tcg_temp_new_i64();
    tcg_gen_extract_i64(t1, psw_mask, 40, 4);
    t2 = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(t2, cc_op);
    tcg_gen_deposit_i64(t1, t1, t2, 4, 60);
    tcg_gen_deposit_i64(o->out, o->out, t1, 24, 8);
    return DISAS_NEXT;
}

#ifndef CONFIG_USER_ONLY
static DisasJumpType op_idte(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m4;

    if (s390_has_feat(S390_FEAT_LOCAL_TLB_CLEARING)) {
        m4 = tcg_constant_i32(get_field(s, m4));
    } else {
        m4 = tcg_constant_i32(0);
    }
    gen_helper_idte(tcg_env, o->in1, o->in2, m4);
    return DISAS_NEXT;
}

static DisasJumpType op_ipte(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m4;

    if (s390_has_feat(S390_FEAT_LOCAL_TLB_CLEARING)) {
        m4 = tcg_constant_i32(get_field(s, m4));
    } else {
        m4 = tcg_constant_i32(0);
    }
    gen_helper_ipte(tcg_env, o->in1, o->in2, m4);
    return DISAS_NEXT;
}

static DisasJumpType op_iske(DisasContext *s, DisasOps *o)
{
    gen_helper_iske(o->out, tcg_env, o->in2);
    return DISAS_NEXT;
}
#endif

static DisasJumpType op_msa(DisasContext *s, DisasOps *o)
{
    int r1 = have_field(s, r1) ? get_field(s, r1) : 0;
    int r2 = have_field(s, r2) ? get_field(s, r2) : 0;
    int r3 = have_field(s, r3) ? get_field(s, r3) : 0;
    TCGv_i32 t_r1, t_r2, t_r3, type;

    switch (s->insn->data) {
    case S390_FEAT_TYPE_KMA:
        if (r3 == r1 || r3 == r2) {
            gen_program_exception(s, PGM_SPECIFICATION);
            return DISAS_NORETURN;
        }
        /* FALL THROUGH */
    case S390_FEAT_TYPE_KMCTR:
        if (r3 & 1 || !r3) {
            gen_program_exception(s, PGM_SPECIFICATION);
            return DISAS_NORETURN;
        }
        /* FALL THROUGH */
    case S390_FEAT_TYPE_PPNO:
    case S390_FEAT_TYPE_KMF:
    case S390_FEAT_TYPE_KMC:
    case S390_FEAT_TYPE_KMO:
    case S390_FEAT_TYPE_KM:
        if (r1 & 1 || !r1) {
            gen_program_exception(s, PGM_SPECIFICATION);
            return DISAS_NORETURN;
        }
        /* FALL THROUGH */
    case S390_FEAT_TYPE_KMAC:
    case S390_FEAT_TYPE_KIMD:
    case S390_FEAT_TYPE_KLMD:
        if (r2 & 1 || !r2) {
            gen_program_exception(s, PGM_SPECIFICATION);
            return DISAS_NORETURN;
        }
        /* FALL THROUGH */
    case S390_FEAT_TYPE_PCKMO:
    case S390_FEAT_TYPE_PCC:
        break;
    default:
        g_assert_not_reached();
    };

    t_r1 = tcg_constant_i32(r1);
    t_r2 = tcg_constant_i32(r2);
    t_r3 = tcg_constant_i32(r3);
    type = tcg_constant_i32(s->insn->data);
    gen_helper_msa(cc_op, tcg_env, t_r1, t_r2, t_r3, type);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_keb(DisasContext *s, DisasOps *o)
{
    gen_helper_keb(cc_op, tcg_env, o->in1, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_kdb(DisasContext *s, DisasOps *o)
{
    gen_helper_kdb(cc_op, tcg_env, o->in1, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_kxb(DisasContext *s, DisasOps *o)
{
    gen_helper_kxb(cc_op, tcg_env, o->in1_128, o->in2_128);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType help_laa(DisasContext *s, DisasOps *o, bool addu64)
{
    tcg_gen_atomic_fetch_add_i64(o->in2, o->in2, o->in1, get_mem_index(s),
                                 s->insn->data | MO_ALIGN);
    if (addu64) {
        tcg_gen_movi_i64(cc_src, 0);
        tcg_gen_add2_i64(o->out, cc_src, o->in1, cc_src, o->in2, cc_src);
    } else {
        tcg_gen_add_i64(o->out, o->in1, o->in2);
    }
    return DISAS_NEXT;
}

static DisasJumpType op_laa(DisasContext *s, DisasOps *o)
{
    return help_laa(s, o, false);
}

static DisasJumpType op_laa_addu64(DisasContext *s, DisasOps *o)
{
    return help_laa(s, o, true);
}

static DisasJumpType op_lan(DisasContext *s, DisasOps *o)
{
    tcg_gen_atomic_fetch_and_i64(o->in2, o->in2, o->in1, get_mem_index(s),
                                 s->insn->data | MO_ALIGN);
    tcg_gen_and_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_lao(DisasContext *s, DisasOps *o)
{
    tcg_gen_atomic_fetch_or_i64(o->in2, o->in2, o->in1, get_mem_index(s),
                                s->insn->data | MO_ALIGN);
    tcg_gen_or_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_lax(DisasContext *s, DisasOps *o)
{
    tcg_gen_atomic_fetch_xor_i64(o->in2, o->in2, o->in1, get_mem_index(s),
                                 s->insn->data | MO_ALIGN);
    tcg_gen_xor_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_ldeb(DisasContext *s, DisasOps *o)
{
    gen_helper_ldeb(o->out, tcg_env, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_ledb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, true, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_ledb(o->out, tcg_env, o->in2, m34);
    return DISAS_NEXT;
}

static DisasJumpType op_ldxb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, true, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_ldxb(o->out, tcg_env, o->in2_128, m34);
    return DISAS_NEXT;
}

static DisasJumpType op_lexb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, true, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_lexb(o->out, tcg_env, o->in2_128, m34);
    return DISAS_NEXT;
}

static DisasJumpType op_lxdb(DisasContext *s, DisasOps *o)
{
    gen_helper_lxdb(o->out_128, tcg_env, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_lxeb(DisasContext *s, DisasOps *o)
{
    gen_helper_lxeb(o->out_128, tcg_env, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_lde(DisasContext *s, DisasOps *o)
{
    tcg_gen_shli_i64(o->out, o->in2, 32);
    return DISAS_NEXT;
}

static DisasJumpType op_llgt(DisasContext *s, DisasOps *o)
{
    tcg_gen_andi_i64(o->out, o->in2, 0x7fffffff);
    return DISAS_NEXT;
}

static DisasJumpType op_ld8s(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld_i64(o->out, o->in2, get_mem_index(s), MO_SB);
    return DISAS_NEXT;
}

static DisasJumpType op_ld8u(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld_i64(o->out, o->in2, get_mem_index(s), MO_UB);
    return DISAS_NEXT;
}

static DisasJumpType op_ld16s(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld_i64(o->out, o->in2, get_mem_index(s), MO_TESW);
    return DISAS_NEXT;
}

static DisasJumpType op_ld16u(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld_i64(o->out, o->in2, get_mem_index(s), MO_TEUW);
    return DISAS_NEXT;
}

static DisasJumpType op_ld32s(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld_tl(o->out, o->in2, get_mem_index(s),
                       MO_TESL | s->insn->data);
    return DISAS_NEXT;
}

static DisasJumpType op_ld32u(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld_tl(o->out, o->in2, get_mem_index(s),
                       MO_TEUL | s->insn->data);
    return DISAS_NEXT;
}

static DisasJumpType op_ld64(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld_i64(o->out, o->in2, get_mem_index(s),
                        MO_TEUQ | s->insn->data);
    return DISAS_NEXT;
}

static DisasJumpType op_lat(DisasContext *s, DisasOps *o)
{
    TCGLabel *lab = gen_new_label();
    store_reg32_i64(get_field(s, r1), o->in2);
    tcg_gen_brcondi_i64(TCG_COND_NE, o->in2, 0, lab);
    gen_trap(s);
    gen_set_label(lab);
    return DISAS_NEXT;
}

static DisasJumpType op_lgat(DisasContext *s, DisasOps *o)
{
    TCGLabel *lab = gen_new_label();
    tcg_gen_qemu_ld_i64(o->out, o->in2, get_mem_index(s), MO_TEUQ);
    tcg_gen_brcondi_i64(TCG_COND_NE, o->out, 0, lab);
    gen_trap(s);
    gen_set_label(lab);
    return DISAS_NEXT;
}

static DisasJumpType op_lfhat(DisasContext *s, DisasOps *o)
{
    TCGLabel *lab = gen_new_label();
    store_reg32h_i64(get_field(s, r1), o->in2);
    tcg_gen_brcondi_i64(TCG_COND_NE, o->in2, 0, lab);
    gen_trap(s);
    gen_set_label(lab);
    return DISAS_NEXT;
}

static DisasJumpType op_llgfat(DisasContext *s, DisasOps *o)
{
    TCGLabel *lab = gen_new_label();

    tcg_gen_qemu_ld_i64(o->out, o->in2, get_mem_index(s), MO_TEUL);
    tcg_gen_brcondi_i64(TCG_COND_NE, o->out, 0, lab);
    gen_trap(s);
    gen_set_label(lab);
    return DISAS_NEXT;
}

static DisasJumpType op_llgtat(DisasContext *s, DisasOps *o)
{
    TCGLabel *lab = gen_new_label();
    tcg_gen_andi_i64(o->out, o->in2, 0x7fffffff);
    tcg_gen_brcondi_i64(TCG_COND_NE, o->out, 0, lab);
    gen_trap(s);
    gen_set_label(lab);
    return DISAS_NEXT;
}

static DisasJumpType op_loc(DisasContext *s, DisasOps *o)
{
    DisasCompare c;

    if (have_field(s, m3)) {
        disas_jcc(s, &c, get_field(s, m3));
    } else {
        disas_jcc(s, &c, get_field(s, m4));
    }

    if (c.is_64) {
        tcg_gen_movcond_i64(c.cond, o->out, c.u.s64.a, c.u.s64.b,
                            o->in2, o->in1);
    } else {
        TCGv_i32 t32 = tcg_temp_new_i32();
        TCGv_i64 t, z;

        tcg_gen_setcond_i32(c.cond, t32, c.u.s32.a, c.u.s32.b);

        t = tcg_temp_new_i64();
        tcg_gen_extu_i32_i64(t, t32);

        z = tcg_constant_i64(0);
        tcg_gen_movcond_i64(TCG_COND_NE, o->out, t, z, o->in2, o->in1);
    }

    return DISAS_NEXT;
}

#ifndef CONFIG_USER_ONLY
static DisasJumpType op_lctl(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r3 = tcg_constant_i32(get_field(s, r3));

    gen_helper_lctl(tcg_env, r1, o->in2, r3);
    s->exit_to_mainloop = true;
    return DISAS_TOO_MANY;
}

static DisasJumpType op_lctlg(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r3 = tcg_constant_i32(get_field(s, r3));

    gen_helper_lctlg(tcg_env, r1, o->in2, r3);
    s->exit_to_mainloop = true;
    return DISAS_TOO_MANY;
}

static DisasJumpType op_lra(DisasContext *s, DisasOps *o)
{
    gen_helper_lra(o->out, tcg_env, o->out, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_lpp(DisasContext *s, DisasOps *o)
{
    tcg_gen_st_i64(o->in2, tcg_env, offsetof(CPUS390XState, pp));
    return DISAS_NEXT;
}

static DisasJumpType op_lpsw(DisasContext *s, DisasOps *o)
{
    TCGv_i64 mask, addr;

    per_breaking_event(s);

    mask = tcg_temp_new_i64();
    addr = tcg_temp_new_i64();
    tcg_gen_qemu_ld_i64(mask, o->in2, get_mem_index(s), MO_TEUQ | MO_ALIGN_8);
    tcg_gen_andi_i64(addr, mask, PSW_MASK_SHORT_ADDR);
    tcg_gen_andi_i64(mask, mask, PSW_MASK_SHORT_CTRL);
    tcg_gen_xori_i64(mask, mask, PSW_MASK_SHORTPSW);
    gen_helper_load_psw(tcg_env, mask, addr);
    return DISAS_NORETURN;
}

static DisasJumpType op_lpswe(DisasContext *s, DisasOps *o)
{
    TCGv_i64 t1, t2;

    per_breaking_event(s);

    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    tcg_gen_qemu_ld_i64(t1, o->in2, get_mem_index(s),
                        MO_TEUQ | MO_ALIGN_8);
    tcg_gen_addi_i64(o->in2, o->in2, 8);
    tcg_gen_qemu_ld_i64(t2, o->in2, get_mem_index(s), MO_TEUQ);
    gen_helper_load_psw(tcg_env, t1, t2);
    return DISAS_NORETURN;
}
#endif

static DisasJumpType op_lam(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r3 = tcg_constant_i32(get_field(s, r3));

    gen_helper_lam(tcg_env, r1, o->in2, r3);
    return DISAS_NEXT;
}

static DisasJumpType op_lm32(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r3 = get_field(s, r3);
    TCGv_i64 t1, t2;

    t1 = tcg_temp_new_i64();
    if (unlikely(r1 == r3)) {
        tcg_gen_qemu_ld_i64(t1, o->in2, get_mem_index(s), MO_TEUL);
        store_reg32_i64(r1, t1);
        return DISAS_NEXT;
    }

    t2 = tcg_temp_new_i64();
    tcg_gen_qemu_ld_i64(t1, o->in2, get_mem_index(s), MO_TEUL);
    tcg_gen_addi_i64(t2, o->in2, 4 * ((r3 - r1) & 15));
    tcg_gen_qemu_ld_i64(t2, t2, get_mem_index(s), MO_TEUL);
    store_reg32_i64(r1, t1);
    store_reg32_i64(r3, t2);

    if (((r1 + 1) & 15) == r3) {
        return DISAS_NEXT;
    }

    r3 = (r3 - 1) & 15;
    tcg_gen_movi_i64(t2, 4);
    while (r1 != r3) {
        r1 = (r1 + 1) & 15;
        tcg_gen_add_i64(o->in2, o->in2, t2);
        tcg_gen_qemu_ld_i64(t1, o->in2, get_mem_index(s), MO_TEUL);
        store_reg32_i64(r1, t1);
    }
    return DISAS_NEXT;
}

static DisasJumpType op_lmh(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r3 = get_field(s, r3);
    TCGv_i64 t1, t2;

    t1 = tcg_temp_new_i64();
    if (unlikely(r1 == r3)) {
        tcg_gen_qemu_ld_i64(t1, o->in2, get_mem_index(s), MO_TEUL);
        store_reg32h_i64(r1, t1);
        return DISAS_NEXT;
    }

    t2 = tcg_temp_new_i64();
    tcg_gen_qemu_ld_i64(t1, o->in2, get_mem_index(s), MO_TEUL);
    tcg_gen_addi_i64(t2, o->in2, 4 * ((r3 - r1) & 15));
    tcg_gen_qemu_ld_i64(t2, t2, get_mem_index(s), MO_TEUL);
    store_reg32h_i64(r1, t1);
    store_reg32h_i64(r3, t2);

    if (((r1 + 1) & 15) == r3) {
        return DISAS_NEXT;
    }

    r3 = (r3 - 1) & 15;
    tcg_gen_movi_i64(t2, 4);
    while (r1 != r3) {
        r1 = (r1 + 1) & 15;
        tcg_gen_add_i64(o->in2, o->in2, t2);
        tcg_gen_qemu_ld_i64(t1, o->in2, get_mem_index(s), MO_TEUL);
        store_reg32h_i64(r1, t1);
    }
    return DISAS_NEXT;
}

static DisasJumpType op_lm64(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r3 = get_field(s, r3);
    TCGv_i64 t1, t2;

    if (unlikely(r1 == r3)) {
        tcg_gen_qemu_ld_i64(regs[r1], o->in2, get_mem_index(s), MO_TEUQ);
        return DISAS_NEXT;
    }

    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    tcg_gen_qemu_ld_i64(t1, o->in2, get_mem_index(s), MO_TEUQ);
    tcg_gen_addi_i64(t2, o->in2, 8 * ((r3 - r1) & 15));
    tcg_gen_qemu_ld_i64(regs[r3], t2, get_mem_index(s), MO_TEUQ);
    tcg_gen_mov_i64(regs[r1], t1);

    if (((r1 + 1) & 15) == r3) {
        return DISAS_NEXT;
    }

    r3 = (r3 - 1) & 15;
    tcg_gen_movi_i64(t1, 8);
    while (r1 != r3) {
        r1 = (r1 + 1) & 15;
        tcg_gen_add_i64(o->in2, o->in2, t1);
        tcg_gen_qemu_ld_i64(regs[r1], o->in2, get_mem_index(s), MO_TEUQ);
    }
    return DISAS_NEXT;
}

static DisasJumpType op_lpd(DisasContext *s, DisasOps *o)
{
    TCGv_i64 a1, a2;
    MemOp mop = s->insn->data;

    if (tb_cflags(s->base.tb) & CF_PARALLEL) {
        update_psw_addr(s);
        update_cc_op(s);
        gen_exception(EXCP_ATOMIC);
        return DISAS_NORETURN;
    }

    a1 = get_address(s, 0, get_field(s, b1), get_field(s, d1));
    a2 = get_address(s, 0, get_field(s, b2), get_field(s, d2));
    tcg_gen_qemu_ld_i64(o->out, a1, get_mem_index(s), mop | MO_ALIGN);
    tcg_gen_qemu_ld_i64(o->out2, a2, get_mem_index(s), mop | MO_ALIGN);

    gen_op_movi_cc(s, 0);
    return DISAS_NEXT;
}

static DisasJumpType op_lpq(DisasContext *s, DisasOps *o)
{
    o->out_128 = tcg_temp_new_i128();
    tcg_gen_qemu_ld_i128(o->out_128, o->in2, get_mem_index(s),
                         MO_TE | MO_128 | MO_ALIGN);
    return DISAS_NEXT;
}

#ifndef CONFIG_USER_ONLY
static DisasJumpType op_lura(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld_tl(o->out, o->in2, MMU_REAL_IDX, s->insn->data);
    return DISAS_NEXT;
}
#endif

static DisasJumpType op_lzrb(DisasContext *s, DisasOps *o)
{
    tcg_gen_andi_i64(o->out, o->in2, -256);
    return DISAS_NEXT;
}

static DisasJumpType op_lcbb(DisasContext *s, DisasOps *o)
{
    const int64_t block_size = (1ull << (get_field(s, m3) + 6));

    if (get_field(s, m3) > 6) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    tcg_gen_ori_i64(o->addr1, o->addr1, -block_size);
    tcg_gen_neg_i64(o->addr1, o->addr1);
    tcg_gen_movi_i64(o->out, 16);
    tcg_gen_umin_i64(o->out, o->out, o->addr1);
    gen_op_update1_cc_i64(s, CC_OP_LCBB, o->out);
    return DISAS_NEXT;
}

static DisasJumpType op_mc(DisasContext *s, DisasOps *o)
{
    const uint8_t monitor_class = get_field(s, i2);

    if (monitor_class & 0xf0) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

#if !defined(CONFIG_USER_ONLY)
    gen_helper_monitor_call(tcg_env, o->addr1,
                            tcg_constant_i32(monitor_class));
#endif
    return DISAS_NEXT;
}

static DisasJumpType op_mov2(DisasContext *s, DisasOps *o)
{
    o->out = o->in2;
    o->in2 = NULL;
    return DISAS_NEXT;
}

static DisasJumpType op_mov2e(DisasContext *s, DisasOps *o)
{
    int b2 = get_field(s, b2);
    TCGv ar1 = tcg_temp_new_i64();
    int r1 = get_field(s, r1);

    o->out = o->in2;
    o->in2 = NULL;

    switch (s->base.tb->flags & FLAG_MASK_ASC) {
    case PSW_ASC_PRIMARY >> FLAG_MASK_PSW_SHIFT:
        tcg_gen_movi_i64(ar1, 0);
        break;
    case PSW_ASC_ACCREG >> FLAG_MASK_PSW_SHIFT:
        tcg_gen_movi_i64(ar1, 1);
        break;
    case PSW_ASC_SECONDARY >> FLAG_MASK_PSW_SHIFT:
        if (b2) {
            tcg_gen_ld32u_i64(ar1, tcg_env, offsetof(CPUS390XState, aregs[b2]));
        } else {
            tcg_gen_movi_i64(ar1, 0);
        }
        break;
    case PSW_ASC_HOME >> FLAG_MASK_PSW_SHIFT:
        tcg_gen_movi_i64(ar1, 2);
        break;
    }

    tcg_gen_st32_i64(ar1, tcg_env, offsetof(CPUS390XState, aregs[r1]));
    return DISAS_NEXT;
}

static DisasJumpType op_movx(DisasContext *s, DisasOps *o)
{
    o->out = o->in1;
    o->out2 = o->in2;
    o->in1 = NULL;
    o->in2 = NULL;
    return DISAS_NEXT;
}

static DisasJumpType op_mvc(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_constant_i32(get_field(s, l1));

    gen_helper_mvc(tcg_env, l, o->addr1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_mvcrl(DisasContext *s, DisasOps *o)
{
    gen_helper_mvcrl(tcg_env, regs[0], o->addr1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_mvcin(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_constant_i32(get_field(s, l1));

    gen_helper_mvcin(tcg_env, l, o->addr1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_mvcl(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r2 = get_field(s, r2);
    TCGv_i32 t1, t2;

    if (r1 & 1 || r2 & 1) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    t1 = tcg_constant_i32(r1);
    t2 = tcg_constant_i32(r2);
    gen_helper_mvcl(cc_op, tcg_env, t1, t2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_mvcle(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r3 = get_field(s, r3);
    TCGv_i32 t1, t3;

    if (r1 & 1 || r3 & 1) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    t1 = tcg_constant_i32(r1);
    t3 = tcg_constant_i32(r3);
    gen_helper_mvcle(cc_op, tcg_env, t1, o->in2, t3);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_mvclu(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r3 = get_field(s, r3);
    TCGv_i32 t1, t3;

    if (r1 & 1 || r3 & 1) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    t1 = tcg_constant_i32(r1);
    t3 = tcg_constant_i32(r3);
    gen_helper_mvclu(cc_op, tcg_env, t1, o->in2, t3);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_mvcos(DisasContext *s, DisasOps *o)
{
    int r3 = get_field(s, r3);
    gen_helper_mvcos(cc_op, tcg_env, o->addr1, o->in2, regs[r3]);
    set_cc_static(s);
    return DISAS_NEXT;
}

#ifndef CONFIG_USER_ONLY
static DisasJumpType op_mvcp(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, l1);
    int r3 = get_field(s, r3);
    gen_helper_mvcp(cc_op, tcg_env, regs[r1], o->addr1, o->in2, regs[r3]);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_mvcs(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, l1);
    int r3 = get_field(s, r3);
    gen_helper_mvcs(cc_op, tcg_env, regs[r1], o->addr1, o->in2, regs[r3]);
    set_cc_static(s);
    return DISAS_NEXT;
}
#endif

static DisasJumpType op_mvn(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_constant_i32(get_field(s, l1));

    gen_helper_mvn(tcg_env, l, o->addr1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_mvo(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_constant_i32(get_field(s, l1));

    gen_helper_mvo(tcg_env, l, o->addr1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_mvpg(DisasContext *s, DisasOps *o)
{
    TCGv_i32 t1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 t2 = tcg_constant_i32(get_field(s, r2));

    gen_helper_mvpg(cc_op, tcg_env, regs[0], t1, t2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_mvst(DisasContext *s, DisasOps *o)
{
    TCGv_i32 t1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 t2 = tcg_constant_i32(get_field(s, r2));

    gen_helper_mvst(cc_op, tcg_env, t1, t2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_mvz(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_constant_i32(get_field(s, l1));

    gen_helper_mvz(tcg_env, l, o->addr1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_mul(DisasContext *s, DisasOps *o)
{
    tcg_gen_mul_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_mul128(DisasContext *s, DisasOps *o)
{
    tcg_gen_mulu2_i64(o->out2, o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_muls128(DisasContext *s, DisasOps *o)
{
    tcg_gen_muls2_i64(o->out2, o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_meeb(DisasContext *s, DisasOps *o)
{
    gen_helper_meeb(o->out, tcg_env, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_mdeb(DisasContext *s, DisasOps *o)
{
    gen_helper_mdeb(o->out, tcg_env, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_mdb(DisasContext *s, DisasOps *o)
{
    gen_helper_mdb(o->out, tcg_env, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_mxb(DisasContext *s, DisasOps *o)
{
    gen_helper_mxb(o->out_128, tcg_env, o->in1_128, o->in2_128);
    return DISAS_NEXT;
}

static DisasJumpType op_mxdb(DisasContext *s, DisasOps *o)
{
    gen_helper_mxdb(o->out_128, tcg_env, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_maeb(DisasContext *s, DisasOps *o)
{
    TCGv_i64 r3 = load_freg32_i64(get_field(s, r3));
    gen_helper_maeb(o->out, tcg_env, o->in1, o->in2, r3);
    return DISAS_NEXT;
}

static DisasJumpType op_madb(DisasContext *s, DisasOps *o)
{
    TCGv_i64 r3 = load_freg(get_field(s, r3));
    gen_helper_madb(o->out, tcg_env, o->in1, o->in2, r3);
    return DISAS_NEXT;
}

static DisasJumpType op_mseb(DisasContext *s, DisasOps *o)
{
    TCGv_i64 r3 = load_freg32_i64(get_field(s, r3));
    gen_helper_mseb(o->out, tcg_env, o->in1, o->in2, r3);
    return DISAS_NEXT;
}

static DisasJumpType op_msdb(DisasContext *s, DisasOps *o)
{
    TCGv_i64 r3 = load_freg(get_field(s, r3));
    gen_helper_msdb(o->out, tcg_env, o->in1, o->in2, r3);
    return DISAS_NEXT;
}

static DisasJumpType op_nabs(DisasContext *s, DisasOps *o)
{
    TCGv_i64 z = tcg_constant_i64(0);
    TCGv_i64 n = tcg_temp_new_i64();

    tcg_gen_neg_i64(n, o->in2);
    tcg_gen_movcond_i64(TCG_COND_GE, o->out, o->in2, z, n, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_nabsf32(DisasContext *s, DisasOps *o)
{
    tcg_gen_ori_i64(o->out, o->in2, 0x80000000ull);
    return DISAS_NEXT;
}

static DisasJumpType op_nabsf64(DisasContext *s, DisasOps *o)
{
    tcg_gen_ori_i64(o->out, o->in2, 0x8000000000000000ull);
    return DISAS_NEXT;
}

static DisasJumpType op_nabsf128(DisasContext *s, DisasOps *o)
{
    tcg_gen_ori_i64(o->out, o->in1, 0x8000000000000000ull);
    tcg_gen_mov_i64(o->out2, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_nc(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_constant_i32(get_field(s, l1));

    gen_helper_nc(cc_op, tcg_env, l, o->addr1, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_neg(DisasContext *s, DisasOps *o)
{
    tcg_gen_neg_i64(o->out, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_negf32(DisasContext *s, DisasOps *o)
{
    tcg_gen_xori_i64(o->out, o->in2, 0x80000000ull);
    return DISAS_NEXT;
}

static DisasJumpType op_negf64(DisasContext *s, DisasOps *o)
{
    tcg_gen_xori_i64(o->out, o->in2, 0x8000000000000000ull);
    return DISAS_NEXT;
}

static DisasJumpType op_negf128(DisasContext *s, DisasOps *o)
{
    tcg_gen_xori_i64(o->out, o->in1, 0x8000000000000000ull);
    tcg_gen_mov_i64(o->out2, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_oc(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_constant_i32(get_field(s, l1));

    gen_helper_oc(cc_op, tcg_env, l, o->addr1, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_or(DisasContext *s, DisasOps *o)
{
    tcg_gen_or_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_ori(DisasContext *s, DisasOps *o)
{
    int shift = s->insn->data & 0xff;
    int size = s->insn->data >> 8;
    uint64_t mask = ((1ull << size) - 1) << shift;
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_shli_i64(t, o->in2, shift);
    tcg_gen_or_i64(o->out, o->in1, t);

    tcg_gen_andi_i64(cc_dst, o->out, mask);
    set_cc_nz_u64(s, cc_dst);
    return DISAS_NEXT;
}

static DisasJumpType op_oi(DisasContext *s, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();

    if (!s390_has_feat(S390_FEAT_INTERLOCKED_ACCESS_2)) {
        tcg_gen_qemu_ld_tl(o->in1, o->addr1, get_mem_index(s), s->insn->data);
    } else {
        tcg_gen_atomic_fetch_or_i64(o->in1, o->addr1, o->in2, get_mem_index(s),
                                    s->insn->data);
    }

    tcg_gen_or_i64(o->out, o->in1, o->in2);

    if (!s390_has_feat(S390_FEAT_INTERLOCKED_ACCESS_2)) {
        tcg_gen_qemu_st_tl(o->out, o->addr1, get_mem_index(s), s->insn->data);
    }
    return DISAS_NEXT;
}

static DisasJumpType op_pack(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_constant_i32(get_field(s, l1));

    gen_helper_pack(tcg_env, l, o->addr1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_pka(DisasContext *s, DisasOps *o)
{
    int l2 = get_field(s, l2) + 1;
    TCGv_i32 l;

    if (l2 > 32) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }
    l = tcg_constant_i32(l2);
    gen_helper_pka(tcg_env, o->addr1, o->in2, l);
    return DISAS_NEXT;
}

static DisasJumpType op_pku(DisasContext *s, DisasOps *o)
{
    int l2 = get_field(s, l2) + 1;
    TCGv_i32 l;

    if ((l2 & 1) || (l2 > 64)) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }
    l = tcg_constant_i32(l2);
    gen_helper_pku(tcg_env, o->addr1, o->in2, l);
    return DISAS_NEXT;
}

static DisasJumpType op_popcnt(DisasContext *s, DisasOps *o)
{
    const uint8_t m3 = get_field(s, m3);

    if ((m3 & 8) && s390_has_feat(S390_FEAT_MISC_INSTRUCTION_EXT3)) {
        tcg_gen_ctpop_i64(o->out, o->in2);
    } else {
        gen_helper_popcnt(o->out, o->in2);
    }
    return DISAS_NEXT;
}

#ifndef CONFIG_USER_ONLY
static DisasJumpType op_ptlb(DisasContext *s, DisasOps *o)
{
    gen_helper_ptlb(tcg_env);
    return DISAS_NEXT;
}
#endif

static DisasJumpType op_risbg(DisasContext *s, DisasOps *o)
{
    int i3 = get_field(s, i3);
    int i4 = get_field(s, i4);
    int i5 = get_field(s, i5);
    int do_zero = i4 & 0x80;
    uint64_t mask, imask, pmask;
    int pos, len, rot;

    switch (s->fields.op2) {
    case 0x55:
    case 0x59:
        i3 &= 63;
        i4 &= 63;
        pmask = ~0;
        break;
    case 0x5d:
        i3 &= 31;
        i4 &= 31;
        pmask = 0xffffffff00000000ull;
        break;
    case 0x51:
        i3 = (i3 & 31) + 32;
        i4 = (i4 & 31) + 32;
        pmask = 0x00000000ffffffffull;
        break;
    default:
        g_assert_not_reached();
    }

    if (i3 <= i4) {
        mask = (-1ull >> i3) & (-1ull << (63 - i4));
    } else {
        mask = (-1ull >> i3) | (-1ull << (63 - i4));
    }
    mask &= pmask;

    imask = ~mask | ~pmask;
    if (do_zero) {
        imask = ~pmask;
    }

    len = i4 - i3 + 1;
    pos = 63 - i4;
    rot = i5 & 63;

    if (imask == 0 && pos == 0 && len > 0 && len <= rot) {
        tcg_gen_extract_i64(o->out, o->in2, 64 - rot, len);
        return DISAS_NEXT;
    }

    if (len > 0 && (imask == 0 || ~mask == imask)) {
        rot = (rot - pos) & 63;
    } else {
        pos = -1;
    }

    tcg_gen_rotli_i64(o->in2, o->in2, rot);

    if (pos >= 0) {
        if (imask == 0) {
            tcg_gen_deposit_z_i64(o->out, o->in2, pos, len);
        } else {
            tcg_gen_deposit_i64(o->out, o->out, o->in2, pos, len);
        }
    } else if (imask == 0) {
        tcg_gen_andi_i64(o->out, o->in2, mask);
    } else {
        tcg_gen_andi_i64(o->in2, o->in2, mask);
        tcg_gen_andi_i64(o->out, o->out, imask);
        tcg_gen_or_i64(o->out, o->out, o->in2);
    }
    return DISAS_NEXT;
}

static DisasJumpType op_rosbg(DisasContext *s, DisasOps *o)
{
    int i3 = get_field(s, i3);
    int i4 = get_field(s, i4);
    int i5 = get_field(s, i5);
    TCGv_i64 orig_out;
    uint64_t mask;

    if (i3 & 0x80) {
        tcg_debug_assert(o->out != NULL);
        orig_out = o->out;
        o->out = tcg_temp_new_i64();
        tcg_gen_mov_i64(o->out, orig_out);
    }

    i3 &= 63;
    i4 &= 63;
    i5 &= 63;

    mask = ~0ull >> i3;
    if (i3 <= i4) {
        mask ^= ~0ull >> i4 >> 1;
    } else {
        mask |= ~(~0ull >> i4 >> 1);
    }

    tcg_gen_rotli_i64(o->in2, o->in2, i5);

    switch (s->fields.op2) {
    case 0x54:
        tcg_gen_ori_i64(o->in2, o->in2, ~mask);
        tcg_gen_and_i64(o->out, o->out, o->in2);
        break;
    case 0x56:
        tcg_gen_andi_i64(o->in2, o->in2, mask);
        tcg_gen_or_i64(o->out, o->out, o->in2);
        break;
    case 0x57:
        tcg_gen_andi_i64(o->in2, o->in2, mask);
        tcg_gen_xor_i64(o->out, o->out, o->in2);
        break;
    default:
        abort();
    }

    tcg_gen_andi_i64(cc_dst, o->out, mask);
    set_cc_nz_u64(s, cc_dst);
    return DISAS_NEXT;
}

static DisasJumpType op_rev16(DisasContext *s, DisasOps *o)
{
    tcg_gen_bswap16_i64(o->out, o->in2, TCG_BSWAP_IZ | TCG_BSWAP_OZ);
    return DISAS_NEXT;
}

static DisasJumpType op_rev32(DisasContext *s, DisasOps *o)
{
    tcg_gen_bswap32_i64(o->out, o->in2, TCG_BSWAP_IZ | TCG_BSWAP_OZ);
    return DISAS_NEXT;
}

static DisasJumpType op_rev64(DisasContext *s, DisasOps *o)
{
    tcg_gen_bswap64_i64(o->out, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_rll32(DisasContext *s, DisasOps *o)
{
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    TCGv_i32 to = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(t1, o->in1);
    tcg_gen_extrl_i64_i32(t2, o->in2);
    tcg_gen_rotl_i32(to, t1, t2);
    tcg_gen_extu_i32_i64(o->out, to);
    return DISAS_NEXT;
}

static DisasJumpType op_rll64(DisasContext *s, DisasOps *o)
{
    tcg_gen_rotl_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

#ifndef CONFIG_USER_ONLY
static DisasJumpType op_rrbe(DisasContext *s, DisasOps *o)
{
    gen_helper_rrbe(cc_op, tcg_env, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_sacf(DisasContext *s, DisasOps *o)
{
    gen_helper_sacf(tcg_env, o->in2);
    return DISAS_TOO_MANY;
}
#endif

static DisasJumpType op_sam(DisasContext *s, DisasOps *o)
{
    int sam = s->insn->data;
    TCGv_i64 tsam;
    uint64_t mask;

    switch (sam) {
    case 0:
        mask = 0xffffff;
        break;
    case 1:
        mask = 0x7fffffff;
        break;
    default:
        mask = -1;
        break;
    }

    if (s->base.pc_next & ~mask) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }
    s->pc_tmp &= mask;

    tsam = tcg_constant_i64(sam);
    tcg_gen_deposit_i64(psw_mask, psw_mask, tsam, 31, 2);

    return DISAS_TOO_MANY;
}

static DisasJumpType op_sar(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    tcg_gen_st32_i64(o->in2, tcg_env, offsetof(CPUS390XState, aregs[r1]));
    return DISAS_NEXT;
}

static DisasJumpType op_seb(DisasContext *s, DisasOps *o)
{
    gen_helper_seb(o->out, tcg_env, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_sdb(DisasContext *s, DisasOps *o)
{
    gen_helper_sdb(o->out, tcg_env, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_sxb(DisasContext *s, DisasOps *o)
{
    gen_helper_sxb(o->out_128, tcg_env, o->in1_128, o->in2_128);
    return DISAS_NEXT;
}

static DisasJumpType op_sqeb(DisasContext *s, DisasOps *o)
{
    gen_helper_sqeb(o->out, tcg_env, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_sqdb(DisasContext *s, DisasOps *o)
{
    gen_helper_sqdb(o->out, tcg_env, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_sqxb(DisasContext *s, DisasOps *o)
{
    gen_helper_sqxb(o->out_128, tcg_env, o->in2_128);
    return DISAS_NEXT;
}

#ifndef CONFIG_USER_ONLY
static DisasJumpType op_servc(DisasContext *s, DisasOps *o)
{
    gen_helper_servc(cc_op, tcg_env, o->in2, o->in1);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_sigp(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r3 = tcg_constant_i32(get_field(s, r3));

    gen_helper_sigp(cc_op, tcg_env, o->in2, r1, r3);
    set_cc_static(s);
    return DISAS_NEXT;
}
#endif

static DisasJumpType op_soc(DisasContext *s, DisasOps *o)
{
    DisasCompare c;
    TCGv_i64 a, h;
    TCGLabel *lab;
    int r1;

    disas_jcc(s, &c, get_field(s, m3));

    c.cond = tcg_invert_cond(c.cond);

    lab = gen_new_label();
    if (c.is_64) {
        tcg_gen_brcond_i64(c.cond, c.u.s64.a, c.u.s64.b, lab);
    } else {
        tcg_gen_brcond_i32(c.cond, c.u.s32.a, c.u.s32.b, lab);
    }

    r1 = get_field(s, r1);
    a = get_address(s, 0, get_field(s, b2), get_field(s, d2));
    switch (s->insn->data) {
    case 1:
        tcg_gen_qemu_st_i64(regs[r1], a, get_mem_index(s), MO_TEUQ);
        break;
    case 0:
        tcg_gen_qemu_st_i64(regs[r1], a, get_mem_index(s), MO_TEUL);
        break;
    case 2:
        h = tcg_temp_new_i64();
        tcg_gen_shri_i64(h, regs[r1], 32);
        tcg_gen_qemu_st_i64(h, a, get_mem_index(s), MO_TEUL);
        break;
    default:
        g_assert_not_reached();
    }

    gen_set_label(lab);
    return DISAS_NEXT;
}

static DisasJumpType op_sla(DisasContext *s, DisasOps *o)
{
    TCGv_i64 t;
    uint64_t sign = 1ull << s->insn->data;
    if (s->insn->data == 31) {
        t = tcg_temp_new_i64();
        tcg_gen_shli_i64(t, o->in1, 32);
    } else {
        t = o->in1;
    }
    gen_op_update2_cc_i64(s, CC_OP_SLA, t, o->in2);
    tcg_gen_shl_i64(o->out, o->in1, o->in2);
    tcg_gen_andi_i64(o->out, o->out, ~sign);
    tcg_gen_andi_i64(o->in1, o->in1, sign);
    tcg_gen_or_i64(o->out, o->out, o->in1);
    return DISAS_NEXT;
}

static DisasJumpType op_sll(DisasContext *s, DisasOps *o)
{
    tcg_gen_shl_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_sra(DisasContext *s, DisasOps *o)
{
    tcg_gen_sar_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_srl(DisasContext *s, DisasOps *o)
{
    tcg_gen_shr_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_sfpc(DisasContext *s, DisasOps *o)
{
    gen_helper_sfpc(tcg_env, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_sfas(DisasContext *s, DisasOps *o)
{
    gen_helper_sfas(tcg_env, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_srnm(DisasContext *s, DisasOps *o)
{
    tcg_gen_andi_i64(o->addr1, o->addr1, 0x3ull);
    gen_helper_srnm(tcg_env, o->addr1);
    return DISAS_NEXT;
}

static DisasJumpType op_srnmb(DisasContext *s, DisasOps *o)
{
    tcg_gen_andi_i64(o->addr1, o->addr1, 0xffull);
    gen_helper_srnm(tcg_env, o->addr1);
    return DISAS_NEXT;
}

static DisasJumpType op_srnmt(DisasContext *s, DisasOps *o)
{
    TCGv_i64 tmp = tcg_temp_new_i64();

    tcg_gen_andi_i64(o->addr1, o->addr1, 0x7ull);

    tcg_gen_ld32u_i64(tmp, tcg_env, offsetof(CPUS390XState, fpc));
    tcg_gen_deposit_i64(tmp, tmp, o->addr1, 4, 3);
    tcg_gen_st32_i64(tmp, tcg_env, offsetof(CPUS390XState, fpc));
    return DISAS_NEXT;
}

static DisasJumpType op_spm(DisasContext *s, DisasOps *o)
{
    tcg_gen_extrl_i64_i32(cc_op, o->in1);
    tcg_gen_extract_i32(cc_op, cc_op, 28, 2);
    set_cc_static(s);

    tcg_gen_shri_i64(o->in1, o->in1, 24);
    tcg_gen_deposit_i64(psw_mask, psw_mask, o->in1, PSW_SHIFT_MASK_PM, 4);
    return DISAS_NEXT;
}

static DisasJumpType op_ectg(DisasContext *s, DisasOps *o)
{
    int b1 = get_field(s, b1);
    int d1 = get_field(s, d1);
    int b2 = get_field(s, b2);
    int d2 = get_field(s, d2);
    int r3 = get_field(s, r3);
    TCGv_i64 tmp = tcg_temp_new_i64();

    o->in1 = tcg_temp_new_i64();
    tcg_gen_addi_i64(o->in1, regs[b1], d1);
    o->in2 = tcg_temp_new_i64();
    tcg_gen_addi_i64(o->in2, regs[b2], d2);
    o->addr1 = tcg_temp_new_i64();
    gen_addi_and_wrap_i64(s, o->addr1, regs[r3], 0);

    tcg_gen_qemu_ld_i64(regs[r3], o->addr1, get_mem_index(s), MO_TEUQ);

    gen_helper_stpt(tmp, tcg_env);
    tcg_gen_sub_i64(regs[0], o->in1, tmp);

    tcg_gen_mov_i64(regs[1], o->in2);
    return DISAS_NEXT;
}

#ifndef CONFIG_USER_ONLY
static DisasJumpType op_spka(DisasContext *s, DisasOps *o)
{
    tcg_gen_shri_i64(o->in2, o->in2, 4);
    tcg_gen_deposit_i64(psw_mask, psw_mask, o->in2, PSW_SHIFT_KEY, 4);
    return DISAS_NEXT;
}

static DisasJumpType op_sske(DisasContext *s, DisasOps *o)
{
    gen_helper_sske(tcg_env, o->in1, o->in2);
    return DISAS_NEXT;
}

static void gen_check_psw_mask(DisasContext *s)
{
    TCGv_i64 reserved = tcg_temp_new_i64();
    TCGLabel *ok = gen_new_label();

    tcg_gen_andi_i64(reserved, psw_mask, PSW_MASK_RESERVED);
    tcg_gen_brcondi_i64(TCG_COND_EQ, reserved, 0, ok);
    gen_program_exception(s, PGM_SPECIFICATION);
    gen_set_label(ok);
}

static DisasJumpType op_ssm(DisasContext *s, DisasOps *o)
{
    tcg_gen_deposit_i64(psw_mask, psw_mask, o->in2, 56, 8);

    gen_check_psw_mask(s);

    s->exit_to_mainloop = true;
    return DISAS_TOO_MANY;
}

static DisasJumpType op_stap(DisasContext *s, DisasOps *o)
{
    tcg_gen_ld32u_i64(o->out, tcg_env, offsetof(CPUS390XState, core_id));
    return DISAS_NEXT;
}
#endif

static DisasJumpType op_stck(DisasContext *s, DisasOps *o)
{
    gen_helper_stck(o->out, tcg_env);
    gen_op_movi_cc(s, 0);
    return DISAS_NEXT;
}

static DisasJumpType op_stcke(DisasContext *s, DisasOps *o)
{
    TCGv_i64 c1 = tcg_temp_new_i64();
    TCGv_i64 c2 = tcg_temp_new_i64();
    TCGv_i64 todpr = tcg_temp_new_i64();
    gen_helper_stck(c1, tcg_env);
    tcg_gen_ld32u_i64(todpr, tcg_env, offsetof(CPUS390XState, todpr));
    tcg_gen_shli_i64(c2, c1, 56);
    tcg_gen_shri_i64(c1, c1, 8);
    tcg_gen_ori_i64(c2, c2, 0x10000);
    tcg_gen_or_i64(c2, c2, todpr);
    tcg_gen_qemu_st_i64(c1, o->in2, get_mem_index(s), MO_TEUQ);
    tcg_gen_addi_i64(o->in2, o->in2, 8);
    tcg_gen_qemu_st_i64(c2, o->in2, get_mem_index(s), MO_TEUQ);
    gen_op_movi_cc(s, 0);
    return DISAS_NEXT;
}

#ifndef CONFIG_USER_ONLY
static DisasJumpType op_sck(DisasContext *s, DisasOps *o)
{
    gen_helper_sck(cc_op, tcg_env, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_sckc(DisasContext *s, DisasOps *o)
{
    gen_helper_sckc(tcg_env, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_sckpf(DisasContext *s, DisasOps *o)
{
    gen_helper_sckpf(tcg_env, regs[0]);
    return DISAS_NEXT;
}

static DisasJumpType op_stckc(DisasContext *s, DisasOps *o)
{
    gen_helper_stckc(o->out, tcg_env);
    return DISAS_NEXT;
}

static DisasJumpType op_stctg(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r3 = tcg_constant_i32(get_field(s, r3));

    gen_helper_stctg(tcg_env, r1, o->in2, r3);
    return DISAS_NEXT;
}

static DisasJumpType op_stctl(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r3 = tcg_constant_i32(get_field(s, r3));

    gen_helper_stctl(tcg_env, r1, o->in2, r3);
    return DISAS_NEXT;
}

static DisasJumpType op_stidp(DisasContext *s, DisasOps *o)
{
    tcg_gen_ld_i64(o->out, tcg_env, offsetof(CPUS390XState, cpuid));
    return DISAS_NEXT;
}

static DisasJumpType op_spt(DisasContext *s, DisasOps *o)
{
    gen_helper_spt(tcg_env, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_stfl(DisasContext *s, DisasOps *o)
{
    gen_helper_stfl(tcg_env);
    return DISAS_NEXT;
}

static DisasJumpType op_stpt(DisasContext *s, DisasOps *o)
{
    gen_helper_stpt(o->out, tcg_env);
    return DISAS_NEXT;
}

static DisasJumpType op_stsi(DisasContext *s, DisasOps *o)
{
    gen_helper_stsi(cc_op, tcg_env, o->in2, regs[0], regs[1]);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_spx(DisasContext *s, DisasOps *o)
{
    gen_helper_spx(tcg_env, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_xsch(DisasContext *s, DisasOps *o)
{
    gen_helper_xsch(tcg_env, regs[1]);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_csch(DisasContext *s, DisasOps *o)
{
    gen_helper_csch(tcg_env, regs[1]);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_hsch(DisasContext *s, DisasOps *o)
{
    gen_helper_hsch(tcg_env, regs[1]);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_msch(DisasContext *s, DisasOps *o)
{
    gen_helper_msch(tcg_env, regs[1], o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_rchp(DisasContext *s, DisasOps *o)
{
    gen_helper_rchp(tcg_env, regs[1]);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_rsch(DisasContext *s, DisasOps *o)
{
    gen_helper_rsch(tcg_env, regs[1]);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_sal(DisasContext *s, DisasOps *o)
{
    gen_helper_sal(tcg_env, regs[1]);
    return DISAS_NEXT;
}

static DisasJumpType op_schm(DisasContext *s, DisasOps *o)
{
    gen_helper_schm(tcg_env, regs[1], regs[2], o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_siga(DisasContext *s, DisasOps *o)
{
    gen_op_movi_cc(s, 3);
    return DISAS_NEXT;
}

static DisasJumpType op_stcps(DisasContext *s, DisasOps *o)
{
    return DISAS_NEXT;
}

static DisasJumpType op_ssch(DisasContext *s, DisasOps *o)
{
    gen_helper_ssch(tcg_env, regs[1], o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_stsch(DisasContext *s, DisasOps *o)
{
    gen_helper_stsch(tcg_env, regs[1], o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_stcrw(DisasContext *s, DisasOps *o)
{
    gen_helper_stcrw(tcg_env, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_tpi(DisasContext *s, DisasOps *o)
{
    gen_helper_tpi(cc_op, tcg_env, o->addr1);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_tsch(DisasContext *s, DisasOps *o)
{
    gen_helper_tsch(tcg_env, regs[1], o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_chsc(DisasContext *s, DisasOps *o)
{
    gen_helper_chsc(tcg_env, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_stpx(DisasContext *s, DisasOps *o)
{
    tcg_gen_ld_i64(o->out, tcg_env, offsetof(CPUS390XState, psa));
    tcg_gen_andi_i64(o->out, o->out, 0x7fffe000);
    return DISAS_NEXT;
}

static DisasJumpType op_stnosm(DisasContext *s, DisasOps *o)
{
    uint64_t i2 = get_field(s, i2);
    TCGv_i64 t;

    t = tcg_temp_new_i64();
    tcg_gen_shri_i64(t, psw_mask, 56);
    tcg_gen_qemu_st_i64(t, o->addr1, get_mem_index(s), MO_UB);

    if (s->fields.op == 0xac) {
        tcg_gen_andi_i64(psw_mask, psw_mask,
                         (i2 << 56) | 0x00ffffffffffffffull);
    } else {
        tcg_gen_ori_i64(psw_mask, psw_mask, i2 << 56);
    }

    gen_check_psw_mask(s);

    s->exit_to_mainloop = true;
    return DISAS_TOO_MANY;
}

static DisasJumpType op_stura(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st_tl(o->in1, o->in2, MMU_REAL_IDX, s->insn->data);

    if (s->base.tb->flags & FLAG_MASK_PER_STORE_REAL) {
        update_cc_op(s);
        update_psw_addr(s);
        gen_helper_per_store_real(tcg_env, tcg_constant_i32(s->ilen));
        return DISAS_NORETURN;
    }
    return DISAS_NEXT;
}
#endif

static DisasJumpType op_stfle(DisasContext *s, DisasOps *o)
{
    gen_helper_stfle(cc_op, tcg_env, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_st8(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st_i64(o->in1, o->in2, get_mem_index(s), MO_UB);
    return DISAS_NEXT;
}

static DisasJumpType op_st16(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st_i64(o->in1, o->in2, get_mem_index(s), MO_TEUW);
    return DISAS_NEXT;
}

static DisasJumpType op_st32(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st_tl(o->in1, o->in2, get_mem_index(s),
                       MO_TEUL | s->insn->data);
    return DISAS_NEXT;
}

static DisasJumpType op_st64(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st_i64(o->in1, o->in2, get_mem_index(s),
                        MO_TEUQ | s->insn->data);
    return DISAS_NEXT;
}

static DisasJumpType op_stam(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r3 = tcg_constant_i32(get_field(s, r3));

    gen_helper_stam(tcg_env, r1, o->in2, r3);
    return DISAS_NEXT;
}

static DisasJumpType op_stcm(DisasContext *s, DisasOps *o)
{
    int m3 = get_field(s, m3);
    int pos, base = s->insn->data;
    TCGv_i64 tmp = tcg_temp_new_i64();

    pos = base + ctz32(m3) * 8;
    switch (m3) {
    case 0xf:
        tcg_gen_shri_i64(tmp, o->in1, pos);
        tcg_gen_qemu_st_i64(tmp, o->in2, get_mem_index(s), MO_TEUL);
        break;

    case 0xc:
    case 0x6:
    case 0x3:
        tcg_gen_shri_i64(tmp, o->in1, pos);
        tcg_gen_qemu_st_i64(tmp, o->in2, get_mem_index(s), MO_TEUW);
        break;

    case 0x8:
    case 0x4:
    case 0x2:
    case 0x1:
        tcg_gen_shri_i64(tmp, o->in1, pos);
        tcg_gen_qemu_st_i64(tmp, o->in2, get_mem_index(s), MO_UB);
        break;

    default:
        pos = base + 32 - 8;
        while (m3) {
            if (m3 & 0x8) {
                tcg_gen_shri_i64(tmp, o->in1, pos);
                tcg_gen_qemu_st_i64(tmp, o->in2, get_mem_index(s), MO_UB);
                tcg_gen_addi_i64(o->in2, o->in2, 1);
            }
            m3 = (m3 << 1) & 0xf;
            pos -= 8;
        }
        break;
    }
    return DISAS_NEXT;
}

static DisasJumpType op_stm(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r3 = get_field(s, r3);
    int size = s->insn->data;
    TCGv_i64 tsize = tcg_constant_i64(size);

    while (1) {
        tcg_gen_qemu_st_i64(regs[r1], o->in2, get_mem_index(s),
                            size == 8 ? MO_TEUQ : MO_TEUL);
        if (r1 == r3) {
            break;
        }
        tcg_gen_add_i64(o->in2, o->in2, tsize);
        r1 = (r1 + 1) & 15;
    }

    return DISAS_NEXT;
}

static DisasJumpType op_stmh(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r3 = get_field(s, r3);
    TCGv_i64 t = tcg_temp_new_i64();
    TCGv_i64 t4 = tcg_constant_i64(4);
    TCGv_i64 t32 = tcg_constant_i64(32);

    while (1) {
        tcg_gen_shl_i64(t, regs[r1], t32);
        tcg_gen_qemu_st_i64(t, o->in2, get_mem_index(s), MO_TEUL);
        if (r1 == r3) {
            break;
        }
        tcg_gen_add_i64(o->in2, o->in2, t4);
        r1 = (r1 + 1) & 15;
    }
    return DISAS_NEXT;
}

static DisasJumpType op_stpq(DisasContext *s, DisasOps *o)
{
    TCGv_i128 t16 = tcg_temp_new_i128();

    tcg_gen_concat_i64_i128(t16, o->out2, o->out);
    tcg_gen_qemu_st_i128(t16, o->in2, get_mem_index(s),
                         MO_TE | MO_128 | MO_ALIGN);
    return DISAS_NEXT;
}

static DisasJumpType op_srst(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r2 = tcg_constant_i32(get_field(s, r2));

    gen_helper_srst(tcg_env, r1, r2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_srstu(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r2 = tcg_constant_i32(get_field(s, r2));

    gen_helper_srstu(tcg_env, r1, r2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_sub(DisasContext *s, DisasOps *o)
{
    tcg_gen_sub_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_subu64(DisasContext *s, DisasOps *o)
{
    tcg_gen_movi_i64(cc_src, 0);
    tcg_gen_sub2_i64(o->out, cc_src, o->in1, cc_src, o->in2, cc_src);
    return DISAS_NEXT;
}

static void compute_borrow(DisasContext *s)
{
    switch (s->cc_op) {
    case CC_OP_SUBU:
        break;
    default:
        gen_op_calc_cc(s);
        /* fall through */
    case CC_OP_STATIC:
        tcg_gen_extu_i32_i64(cc_src, cc_op);
        tcg_gen_shri_i64(cc_src, cc_src, 1);
        /* fall through */
    case CC_OP_ADDU:
        tcg_gen_subi_i64(cc_src, cc_src, 1);
        break;
    }
}

static DisasJumpType op_subb32(DisasContext *s, DisasOps *o)
{
    compute_borrow(s);

    tcg_gen_add_i64(o->out, o->in1, cc_src);
    tcg_gen_sub_i64(o->out, o->out, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_subb64(DisasContext *s, DisasOps *o)
{
    compute_borrow(s);

    TCGv_i64 zero = tcg_constant_i64(0);
    tcg_gen_add2_i64(o->out, cc_src, o->in1, zero, cc_src, cc_src);
    tcg_gen_sub2_i64(o->out, cc_src, o->out, cc_src, o->in2, zero);

    return DISAS_NEXT;
}

static DisasJumpType op_svc(DisasContext *s, DisasOps *o)
{
    TCGv_i32 t;

    update_psw_addr(s);
    update_cc_op(s);

    t = tcg_constant_i32(get_field(s, i1) & 0xff);
    tcg_gen_st_i32(t, tcg_env, offsetof(CPUS390XState, int_svc_code));

    t = tcg_constant_i32(s->ilen);
    tcg_gen_st_i32(t, tcg_env, offsetof(CPUS390XState, int_svc_ilen));

    gen_exception(EXCP_SVC);
    return DISAS_NORETURN;
}

static DisasJumpType op_tam(DisasContext *s, DisasOps *o)
{
    int cc = 0;

    cc |= (s->base.tb->flags & FLAG_MASK_64) ? 2 : 0;
    cc |= (s->base.tb->flags & FLAG_MASK_32) ? 1 : 0;
    gen_op_movi_cc(s, cc);
    return DISAS_NEXT;
}

static DisasJumpType op_tceb(DisasContext *s, DisasOps *o)
{
    gen_helper_tceb(cc_op, tcg_env, o->in1, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_tcdb(DisasContext *s, DisasOps *o)
{
    gen_helper_tcdb(cc_op, tcg_env, o->in1, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_tcxb(DisasContext *s, DisasOps *o)
{
    gen_helper_tcxb(cc_op, tcg_env, o->in1_128, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

#ifndef CONFIG_USER_ONLY

static DisasJumpType op_testblock(DisasContext *s, DisasOps *o)
{
    gen_helper_testblock(cc_op, tcg_env, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_tprot(DisasContext *s, DisasOps *o)
{
    gen_helper_tprot(cc_op, tcg_env, o->addr1, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

#endif

static DisasJumpType op_tp(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l1 = tcg_constant_i32(get_field(s, l1) + 1);

    gen_helper_tp(cc_op, tcg_env, o->addr1, l1);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_tr(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_constant_i32(get_field(s, l1));

    gen_helper_tr(tcg_env, l, o->addr1, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_tre(DisasContext *s, DisasOps *o)
{
    TCGv_i128 pair = tcg_temp_new_i128();

    gen_helper_tre(pair, tcg_env, o->out, o->out2, o->in2);
    tcg_gen_extr_i128_i64(o->out2, o->out, pair);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_trt(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_constant_i32(get_field(s, l1));

    gen_helper_trt(cc_op, tcg_env, l, o->addr1, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_trtr(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_constant_i32(get_field(s, l1));

    gen_helper_trtr(cc_op, tcg_env, l, o->addr1, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_trXX(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r2 = tcg_constant_i32(get_field(s, r2));
    TCGv_i32 sizes = tcg_constant_i32(s->insn->opc & 3);
    TCGv_i32 tst = tcg_temp_new_i32();
    int m3 = get_field(s, m3);

    if (!s390_has_feat(S390_FEAT_ETF2_ENH)) {
        m3 = 0;
    }
    if (m3 & 1) {
        tcg_gen_movi_i32(tst, -1);
    } else {
        tcg_gen_extrl_i64_i32(tst, regs[0]);
        if (s->insn->opc & 3) {
            tcg_gen_ext8u_i32(tst, tst);
        } else {
            tcg_gen_ext16u_i32(tst, tst);
        }
    }
    gen_helper_trXX(cc_op, tcg_env, r1, r2, tst, sizes);

    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_ts(DisasContext *s, DisasOps *o)
{
    TCGv_i32 ff = tcg_constant_i32(0xff);
    TCGv_i32 t1 = tcg_temp_new_i32();

    tcg_gen_atomic_xchg_i32(t1, o->in2, ff, get_mem_index(s), MO_UB);
    tcg_gen_extract_i32(cc_op, t1, 7, 1);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_unpk(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_constant_i32(get_field(s, l1));

    gen_helper_unpk(tcg_env, l, o->addr1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_unpka(DisasContext *s, DisasOps *o)
{
    int l1 = get_field(s, l1) + 1;
    TCGv_i32 l;

    if (l1 > 32) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }
    l = tcg_constant_i32(l1);
    gen_helper_unpka(cc_op, tcg_env, o->addr1, l, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_unpku(DisasContext *s, DisasOps *o)
{
    int l1 = get_field(s, l1) + 1;
    TCGv_i32 l;

    if ((l1 & 1) || (l1 > 64)) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }
    l = tcg_constant_i32(l1);
    gen_helper_unpku(cc_op, tcg_env, o->addr1, l, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}


static DisasJumpType op_xc(DisasContext *s, DisasOps *o)
{
    int d1 = get_field(s, d1);
    int d2 = get_field(s, d2);
    int b1 = get_field(s, b1);
    int b2 = get_field(s, b2);
    int l = get_field(s, l1);
    TCGv_i32 t32;

    o->addr1 = get_address(s, 0, b1, d1);

    if (b1 == b2 && d1 == d2 && (l + 1) <= 32) {
        o->in2 = tcg_constant_i64(0);

        l++;
        while (l >= 8) {
            tcg_gen_qemu_st_i64(o->in2, o->addr1, get_mem_index(s), MO_UQ);
            l -= 8;
            if (l > 0) {
                tcg_gen_addi_i64(o->addr1, o->addr1, 8);
            }
        }
        if (l >= 4) {
            tcg_gen_qemu_st_i64(o->in2, o->addr1, get_mem_index(s), MO_UL);
            l -= 4;
            if (l > 0) {
                tcg_gen_addi_i64(o->addr1, o->addr1, 4);
            }
        }
        if (l >= 2) {
            tcg_gen_qemu_st_i64(o->in2, o->addr1, get_mem_index(s), MO_UW);
            l -= 2;
            if (l > 0) {
                tcg_gen_addi_i64(o->addr1, o->addr1, 2);
            }
        }
        if (l) {
            tcg_gen_qemu_st_i64(o->in2, o->addr1, get_mem_index(s), MO_UB);
        }
        gen_op_movi_cc(s, 0);
        return DISAS_NEXT;
    }

    o->in2 = get_address(s, 0, b2, d2);
    t32 = tcg_constant_i32(l);
    gen_helper_xc(cc_op, tcg_env, t32, o->addr1, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_xor(DisasContext *s, DisasOps *o)
{
    tcg_gen_xor_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_xori(DisasContext *s, DisasOps *o)
{
    int shift = s->insn->data & 0xff;
    int size = s->insn->data >> 8;
    uint64_t mask = ((1ull << size) - 1) << shift;
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_shli_i64(t, o->in2, shift);
    tcg_gen_xor_i64(o->out, o->in1, t);

    tcg_gen_andi_i64(cc_dst, o->out, mask);
    set_cc_nz_u64(s, cc_dst);
    return DISAS_NEXT;
}

static DisasJumpType op_xi(DisasContext *s, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();

    if (!s390_has_feat(S390_FEAT_INTERLOCKED_ACCESS_2)) {
        tcg_gen_qemu_ld_tl(o->in1, o->addr1, get_mem_index(s), s->insn->data);
    } else {
        tcg_gen_atomic_fetch_xor_i64(o->in1, o->addr1, o->in2, get_mem_index(s),
                                     s->insn->data);
    }

    tcg_gen_xor_i64(o->out, o->in1, o->in2);

    if (!s390_has_feat(S390_FEAT_INTERLOCKED_ACCESS_2)) {
        tcg_gen_qemu_st_tl(o->out, o->addr1, get_mem_index(s), s->insn->data);
    }
    return DISAS_NEXT;
}

static DisasJumpType op_zero(DisasContext *s, DisasOps *o)
{
    o->out = tcg_constant_i64(0);
    return DISAS_NEXT;
}

static DisasJumpType op_zero2(DisasContext *s, DisasOps *o)
{
    o->out = tcg_constant_i64(0);
    o->out2 = o->out;
    return DISAS_NEXT;
}

#ifndef CONFIG_USER_ONLY
static DisasJumpType op_clp(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r2 = tcg_constant_i32(get_field(s, r2));

    gen_helper_clp(tcg_env, r2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_pcilg(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r2 = tcg_constant_i32(get_field(s, r2));

    gen_helper_pcilg(tcg_env, r1, r2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_pcistg(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r2 = tcg_constant_i32(get_field(s, r2));

    gen_helper_pcistg(tcg_env, r1, r2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_stpcifc(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 ar = tcg_constant_i32(get_field(s, b2));

    gen_helper_stpcifc(tcg_env, r1, o->addr1, ar);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_sic(DisasContext *s, DisasOps *o)
{
    gen_helper_sic(tcg_env, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_rpcit(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r2 = tcg_constant_i32(get_field(s, r2));

    gen_helper_rpcit(tcg_env, r1, r2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_pcistb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r3 = tcg_constant_i32(get_field(s, r3));
    TCGv_i32 ar = tcg_constant_i32(get_field(s, b2));

    gen_helper_pcistb(tcg_env, r1, r3, o->addr1, ar);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_mpcifc(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 ar = tcg_constant_i32(get_field(s, b2));

    gen_helper_mpcifc(tcg_env, r1, o->addr1, ar);
    set_cc_static(s);
    return DISAS_NEXT;
}
#endif

#include "translate_vx.c.inc"

static void cout_abs32(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_ABS_32, o->out);
}

static void cout_abs64(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_ABS_64, o->out);
}

static void cout_adds32(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_ADD_32, o->in1, o->in2, o->out);
}

static void cout_adds64(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_ADD_64, o->in1, o->in2, o->out);
}

static void cout_addu32(DisasContext *s, DisasOps *o)
{
    tcg_gen_shri_i64(cc_src, o->out, 32);
    tcg_gen_ext32u_i64(cc_dst, o->out);
    gen_op_update2_cc_i64(s, CC_OP_ADDU, cc_src, cc_dst);
}

static void cout_addu64(DisasContext *s, DisasOps *o)
{
    gen_op_update2_cc_i64(s, CC_OP_ADDU, cc_src, o->out);
}

static void cout_cmps32(DisasContext *s, DisasOps *o)
{
    gen_op_update2_cc_i64(s, CC_OP_LTGT_32, o->in1, o->in2);
}

static void cout_cmps64(DisasContext *s, DisasOps *o)
{
    gen_op_update2_cc_i64(s, CC_OP_LTGT_64, o->in1, o->in2);
}

static void cout_cmpu32(DisasContext *s, DisasOps *o)
{
    gen_op_update2_cc_i64(s, CC_OP_LTUGTU_32, o->in1, o->in2);
}

static void cout_cmpu64(DisasContext *s, DisasOps *o)
{
    gen_op_update2_cc_i64(s, CC_OP_LTUGTU_64, o->in1, o->in2);
}

static void cout_f32(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_NZ_F32, o->out);
}

static void cout_f64(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_NZ_F64, o->out);
}

static void cout_f128(DisasContext *s, DisasOps *o)
{
    gen_op_update2_cc_i64(s, CC_OP_NZ_F128, o->out, o->out2);
}

static void cout_nabs32(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_NABS_32, o->out);
}

static void cout_nabs64(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_NABS_64, o->out);
}

static void cout_neg32(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_COMP_32, o->out);
}

static void cout_neg64(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_COMP_64, o->out);
}

static void cout_nz32(DisasContext *s, DisasOps *o)
{
    tcg_gen_ext32u_i64(cc_dst, o->out);
    gen_op_update1_cc_i64(s, CC_OP_NZ, cc_dst);
}

static void cout_nz64(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_NZ, o->out);
}

static void cout_s32(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_LTGT0_32, o->out);
}

static void cout_s64(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_LTGT0_64, o->out);
}

static void cout_subs32(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_SUB_32, o->in1, o->in2, o->out);
}

static void cout_subs64(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_SUB_64, o->in1, o->in2, o->out);
}

static void cout_subu32(DisasContext *s, DisasOps *o)
{
    tcg_gen_sari_i64(cc_src, o->out, 32);
    tcg_gen_ext32u_i64(cc_dst, o->out);
    gen_op_update2_cc_i64(s, CC_OP_SUBU, cc_src, cc_dst);
}

static void cout_subu64(DisasContext *s, DisasOps *o)
{
    gen_op_update2_cc_i64(s, CC_OP_SUBU, cc_src, o->out);
}

static void cout_tm32(DisasContext *s, DisasOps *o)
{
    gen_op_update2_cc_i64(s, CC_OP_TM_32, o->in1, o->in2);
}

static void cout_tm64(DisasContext *s, DisasOps *o)
{
    gen_op_update2_cc_i64(s, CC_OP_TM_64, o->in1, o->in2);
}

static void cout_muls32(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_MULS_32, o->out);
}

static void cout_muls64(DisasContext *s, DisasOps *o)
{
    gen_op_update2_cc_i64(s, CC_OP_MULS_64, o->out, o->out2);
}

static void prep_new(DisasContext *s, DisasOps *o)
{
    o->out = tcg_temp_new_i64();
}
#define SPEC_prep_new 0

static void prep_new_P(DisasContext *s, DisasOps *o)
{
    o->out = tcg_temp_new_i64();
    o->out2 = tcg_temp_new_i64();
}
#define SPEC_prep_new_P 0

static void prep_new_x(DisasContext *s, DisasOps *o)
{
    o->out_128 = tcg_temp_new_i128();
}
#define SPEC_prep_new_x 0

static void prep_r1(DisasContext *s, DisasOps *o)
{
    o->out = regs[get_field(s, r1)];
}
#define SPEC_prep_r1 0

static void prep_r1_P(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    o->out = regs[r1];
    o->out2 = regs[r1 + 1];
}
#define SPEC_prep_r1_P SPEC_r1_even

static void wout_r1(DisasContext *s, DisasOps *o)
{
    store_reg(get_field(s, r1), o->out);
}
#define SPEC_wout_r1 0

static void wout_out2_r1(DisasContext *s, DisasOps *o)
{
    store_reg(get_field(s, r1), o->out2);
}
#define SPEC_wout_out2_r1 0

static void wout_r1_8(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    tcg_gen_deposit_i64(regs[r1], regs[r1], o->out, 0, 8);
}
#define SPEC_wout_r1_8 0

static void wout_r1_16(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    tcg_gen_deposit_i64(regs[r1], regs[r1], o->out, 0, 16);
}
#define SPEC_wout_r1_16 0

static void wout_r1_32(DisasContext *s, DisasOps *o)
{
    store_reg32_i64(get_field(s, r1), o->out);
}
#define SPEC_wout_r1_32 0

static void wout_r1_32h(DisasContext *s, DisasOps *o)
{
    store_reg32h_i64(get_field(s, r1), o->out);
}
#define SPEC_wout_r1_32h 0

static void wout_r1_P32(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    store_reg32_i64(r1, o->out);
    store_reg32_i64(r1 + 1, o->out2);
}
#define SPEC_wout_r1_P32 SPEC_r1_even

static void wout_r1_D32(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    TCGv_i64 t = tcg_temp_new_i64();
    store_reg32_i64(r1 + 1, o->out);
    tcg_gen_shri_i64(t, o->out, 32);
    store_reg32_i64(r1, t);
}
#define SPEC_wout_r1_D32 SPEC_r1_even

static void wout_r1_D64(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    tcg_gen_extr_i128_i64(regs[r1 + 1], regs[r1], o->out_128);
}
#define SPEC_wout_r1_D64 SPEC_r1_even

static void wout_r3_P32(DisasContext *s, DisasOps *o)
{
    int r3 = get_field(s, r3);
    store_reg32_i64(r3, o->out);
    store_reg32_i64(r3 + 1, o->out2);
}
#define SPEC_wout_r3_P32 SPEC_r3_even

static void wout_r3_P64(DisasContext *s, DisasOps *o)
{
    int r3 = get_field(s, r3);
    store_reg(r3, o->out);
    store_reg(r3 + 1, o->out2);
}
#define SPEC_wout_r3_P64 SPEC_r3_even

static void wout_e1(DisasContext *s, DisasOps *o)
{
    store_freg32_i64(get_field(s, r1), o->out);
}
#define SPEC_wout_e1 0

static void wout_f1(DisasContext *s, DisasOps *o)
{
    store_freg(get_field(s, r1), o->out);
}
#define SPEC_wout_f1 0

static void wout_x1(DisasContext *s, DisasOps *o)
{
    int f1 = get_field(s, r1);

    tcg_debug_assert(o->out == NULL);
    o->out = tcg_temp_new_i64();
    o->out2 = tcg_temp_new_i64();

    tcg_gen_extr_i128_i64(o->out2, o->out, o->out_128);
    store_freg(f1, o->out);
    store_freg(f1 + 2, o->out2);
}
#define SPEC_wout_x1 SPEC_r1_f128

static void wout_x1_P(DisasContext *s, DisasOps *o)
{
    int f1 = get_field(s, r1);
    store_freg(f1, o->out);
    store_freg(f1 + 2, o->out2);
}
#define SPEC_wout_x1_P SPEC_r1_f128

static void wout_cond_r1r2_32(DisasContext *s, DisasOps *o)
{
    if (get_field(s, r1) != get_field(s, r2)) {
        store_reg32_i64(get_field(s, r1), o->out);
    }
}
#define SPEC_wout_cond_r1r2_32 0

static void wout_cond_e1e2(DisasContext *s, DisasOps *o)
{
    if (get_field(s, r1) != get_field(s, r2)) {
        store_freg32_i64(get_field(s, r1), o->out);
    }
}
#define SPEC_wout_cond_e1e2 0

static void wout_m1_8(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st_i64(o->out, o->addr1, get_mem_index(s), MO_UB);
}
#define SPEC_wout_m1_8 0

static void wout_m1_16(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st_i64(o->out, o->addr1, get_mem_index(s), MO_TEUW);
}
#define SPEC_wout_m1_16 0

#ifndef CONFIG_USER_ONLY
static void wout_m1_16a(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st_tl(o->out, o->addr1, get_mem_index(s), MO_TEUW | MO_ALIGN);
}
#define SPEC_wout_m1_16a 0
#endif

static void wout_m1_32(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st_i64(o->out, o->addr1, get_mem_index(s), MO_TEUL);
}
#define SPEC_wout_m1_32 0

#ifndef CONFIG_USER_ONLY
static void wout_m1_32a(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st_tl(o->out, o->addr1, get_mem_index(s), MO_TEUL | MO_ALIGN);
}
#define SPEC_wout_m1_32a 0
#endif

static void wout_m1_64(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st_i64(o->out, o->addr1, get_mem_index(s), MO_TEUQ);
}
#define SPEC_wout_m1_64 0

#ifndef CONFIG_USER_ONLY
static void wout_m1_64a(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st_i64(o->out, o->addr1, get_mem_index(s), MO_TEUQ | MO_ALIGN);
}
#define SPEC_wout_m1_64a 0
#endif

static void wout_m2_32(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st_i64(o->out, o->in2, get_mem_index(s), MO_TEUL);
}
#define SPEC_wout_m2_32 0

static void wout_in2_r1(DisasContext *s, DisasOps *o)
{
    store_reg(get_field(s, r1), o->in2);
}
#define SPEC_wout_in2_r1 0

static void wout_in2_r1_32(DisasContext *s, DisasOps *o)
{
    store_reg32_i64(get_field(s, r1), o->in2);
}
#define SPEC_wout_in2_r1_32 0

static void in1_r1(DisasContext *s, DisasOps *o)
{
    o->in1 = load_reg(get_field(s, r1));
}
#define SPEC_in1_r1 0

static void in1_r1_o(DisasContext *s, DisasOps *o)
{
    o->in1 = regs[get_field(s, r1)];
}
#define SPEC_in1_r1_o 0

static void in1_r1_32s(DisasContext *s, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_ext32s_i64(o->in1, regs[get_field(s, r1)]);
}
#define SPEC_in1_r1_32s 0

static void in1_r1_32u(DisasContext *s, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(o->in1, regs[get_field(s, r1)]);
}
#define SPEC_in1_r1_32u 0

static void in1_r1_sr32(DisasContext *s, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_shri_i64(o->in1, regs[get_field(s, r1)], 32);
}
#define SPEC_in1_r1_sr32 0

static void in1_r1p1(DisasContext *s, DisasOps *o)
{
    o->in1 = load_reg(get_field(s, r1) + 1);
}
#define SPEC_in1_r1p1 SPEC_r1_even

static void in1_r1p1_o(DisasContext *s, DisasOps *o)
{
    o->in1 = regs[get_field(s, r1) + 1];
}
#define SPEC_in1_r1p1_o SPEC_r1_even

static void in1_r1p1_32s(DisasContext *s, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_ext32s_i64(o->in1, regs[get_field(s, r1) + 1]);
}
#define SPEC_in1_r1p1_32s SPEC_r1_even

static void in1_r1p1_32u(DisasContext *s, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(o->in1, regs[get_field(s, r1) + 1]);
}
#define SPEC_in1_r1p1_32u SPEC_r1_even

static void in1_r1_D32(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_concat32_i64(o->in1, regs[r1 + 1], regs[r1]);
}
#define SPEC_in1_r1_D32 SPEC_r1_even

static void in1_r2(DisasContext *s, DisasOps *o)
{
    o->in1 = load_reg(get_field(s, r2));
}
#define SPEC_in1_r2 0

static void in1_r2_sr32(DisasContext *s, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_shri_i64(o->in1, regs[get_field(s, r2)], 32);
}
#define SPEC_in1_r2_sr32 0

static void in1_r2_32u(DisasContext *s, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(o->in1, regs[get_field(s, r2)]);
}
#define SPEC_in1_r2_32u 0

static void in1_r3(DisasContext *s, DisasOps *o)
{
    o->in1 = load_reg(get_field(s, r3));
}
#define SPEC_in1_r3 0

static void in1_r3_o(DisasContext *s, DisasOps *o)
{
    o->in1 = regs[get_field(s, r3)];
}
#define SPEC_in1_r3_o 0

static void in1_r3_32s(DisasContext *s, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_ext32s_i64(o->in1, regs[get_field(s, r3)]);
}
#define SPEC_in1_r3_32s 0

static void in1_r3_32u(DisasContext *s, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(o->in1, regs[get_field(s, r3)]);
}
#define SPEC_in1_r3_32u 0

static void in1_r3_D32(DisasContext *s, DisasOps *o)
{
    int r3 = get_field(s, r3);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_concat32_i64(o->in1, regs[r3 + 1], regs[r3]);
}
#define SPEC_in1_r3_D32 SPEC_r3_even

static void in1_r3_sr32(DisasContext *s, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_shri_i64(o->in1, regs[get_field(s, r3)], 32);
}
#define SPEC_in1_r3_sr32 0

static void in1_e1(DisasContext *s, DisasOps *o)
{
    o->in1 = load_freg32_i64(get_field(s, r1));
}
#define SPEC_in1_e1 0

static void in1_f1(DisasContext *s, DisasOps *o)
{
    o->in1 = load_freg(get_field(s, r1));
}
#define SPEC_in1_f1 0

static void in1_x1(DisasContext *s, DisasOps *o)
{
    o->in1_128 = load_freg_128(get_field(s, r1));
}
#define SPEC_in1_x1 SPEC_r1_f128

static void in1_x2h(DisasContext *s, DisasOps *o)
{
    o->in1 = load_freg(get_field(s, r2));
}
#define SPEC_in1_x2h SPEC_r2_f128

static void in1_f3(DisasContext *s, DisasOps *o)
{
    o->in1 = load_freg(get_field(s, r3));
}
#define SPEC_in1_f3 0

static void in1_la1(DisasContext *s, DisasOps *o)
{
    o->addr1 = get_address(s, 0, get_field(s, b1), get_field(s, d1));
}
#define SPEC_in1_la1 0

static void in1_la2(DisasContext *s, DisasOps *o)
{
    int x2 = have_field(s, x2) ? get_field(s, x2) : 0;
    o->addr1 = get_address(s, x2, get_field(s, b2), get_field(s, d2));
}
#define SPEC_in1_la2 0

static void in1_m1_8u(DisasContext *s, DisasOps *o)
{
    in1_la1(s, o);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_qemu_ld_i64(o->in1, o->addr1, get_mem_index(s), MO_UB);
}
#define SPEC_in1_m1_8u 0

static void in1_m1_16s(DisasContext *s, DisasOps *o)
{
    in1_la1(s, o);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_qemu_ld_i64(o->in1, o->addr1, get_mem_index(s), MO_TESW);
}
#define SPEC_in1_m1_16s 0

static void in1_m1_16u(DisasContext *s, DisasOps *o)
{
    in1_la1(s, o);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_qemu_ld_i64(o->in1, o->addr1, get_mem_index(s), MO_TEUW);
}
#define SPEC_in1_m1_16u 0

static void in1_m1_32s(DisasContext *s, DisasOps *o)
{
    in1_la1(s, o);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_qemu_ld_i64(o->in1, o->addr1, get_mem_index(s), MO_TESL);
}
#define SPEC_in1_m1_32s 0

static void in1_m1_32u(DisasContext *s, DisasOps *o)
{
    in1_la1(s, o);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_qemu_ld_i64(o->in1, o->addr1, get_mem_index(s), MO_TEUL);
}
#define SPEC_in1_m1_32u 0

static void in1_m1_64(DisasContext *s, DisasOps *o)
{
    in1_la1(s, o);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_qemu_ld_i64(o->in1, o->addr1, get_mem_index(s), MO_TEUQ);
}
#define SPEC_in1_m1_64 0

static void in2_r1_o(DisasContext *s, DisasOps *o)
{
    o->in2 = regs[get_field(s, r1)];
}
#define SPEC_in2_r1_o 0

static void in2_r1_16u(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext16u_i64(o->in2, regs[get_field(s, r1)]);
}
#define SPEC_in2_r1_16u 0

static void in2_r1_32u(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(o->in2, regs[get_field(s, r1)]);
}
#define SPEC_in2_r1_32u 0

static void in2_r1_D32(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    o->in2 = tcg_temp_new_i64();
    tcg_gen_concat32_i64(o->in2, regs[r1 + 1], regs[r1]);
}
#define SPEC_in2_r1_D32 SPEC_r1_even

static void in2_r2(DisasContext *s, DisasOps *o)
{
    o->in2 = load_reg(get_field(s, r2));
}
#define SPEC_in2_r2 0

static void in2_r2_o(DisasContext *s, DisasOps *o)
{
    o->in2 = regs[get_field(s, r2)];
}
#define SPEC_in2_r2_o 0

static void in2_r2_nz(DisasContext *s, DisasOps *o)
{
    int r2 = get_field(s, r2);
    if (r2 != 0) {
        o->in2 = load_reg(r2);
        gen_addi_and_wrap_i64(s, o->in2, o->in2, 0);
    }
}
#define SPEC_in2_r2_nz 0

static void in2_r2_8s(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext8s_i64(o->in2, regs[get_field(s, r2)]);
}
#define SPEC_in2_r2_8s 0

static void in2_r2_8u(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext8u_i64(o->in2, regs[get_field(s, r2)]);
}
#define SPEC_in2_r2_8u 0

static void in2_r2_16s(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext16s_i64(o->in2, regs[get_field(s, r2)]);
}
#define SPEC_in2_r2_16s 0

static void in2_r2_16u(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext16u_i64(o->in2, regs[get_field(s, r2)]);
}
#define SPEC_in2_r2_16u 0

static void in2_r3(DisasContext *s, DisasOps *o)
{
    o->in2 = load_reg(get_field(s, r3));
}
#define SPEC_in2_r3 0

static void in2_r3_D64(DisasContext *s, DisasOps *o)
{
    int r3 = get_field(s, r3);
    o->in2_128 = tcg_temp_new_i128();
    tcg_gen_concat_i64_i128(o->in2_128, regs[r3 + 1], regs[r3]);
}
#define SPEC_in2_r3_D64 SPEC_r3_even

static void in2_r3_sr32(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_shri_i64(o->in2, regs[get_field(s, r3)], 32);
}
#define SPEC_in2_r3_sr32 0

static void in2_r3_32u(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(o->in2, regs[get_field(s, r3)]);
}
#define SPEC_in2_r3_32u 0

static void in2_r2_32s(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext32s_i64(o->in2, regs[get_field(s, r2)]);
}
#define SPEC_in2_r2_32s 0

static void in2_r2_32u(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(o->in2, regs[get_field(s, r2)]);
}
#define SPEC_in2_r2_32u 0

static void in2_r2_sr32(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_shri_i64(o->in2, regs[get_field(s, r2)], 32);
}
#define SPEC_in2_r2_sr32 0

static void in2_e2(DisasContext *s, DisasOps *o)
{
    o->in2 = load_freg32_i64(get_field(s, r2));
}
#define SPEC_in2_e2 0

static void in2_f2(DisasContext *s, DisasOps *o)
{
    o->in2 = load_freg(get_field(s, r2));
}
#define SPEC_in2_f2 0

static void in2_x2(DisasContext *s, DisasOps *o)
{
    o->in2_128 = load_freg_128(get_field(s, r2));
}
#define SPEC_in2_x2 SPEC_r2_f128

static void in2_x2l(DisasContext *s, DisasOps *o)
{
    o->in2 = load_freg(get_field(s, r2) + 2);
}
#define SPEC_in2_x2l SPEC_r2_f128

static void in2_ra2(DisasContext *s, DisasOps *o)
{
    int r2 = get_field(s, r2);

    o->in2 = tcg_temp_new_i64();
    gen_addi_and_wrap_i64(s, o->in2, regs[r2], 0);
}
#define SPEC_in2_ra2 0

static void in2_ra2_E(DisasContext *s, DisasOps *o)
{
    return in2_ra2(s, o);
}
#define SPEC_in2_ra2_E SPEC_r2_even

static void in2_a2(DisasContext *s, DisasOps *o)
{
    int x2 = have_field(s, x2) ? get_field(s, x2) : 0;
    o->in2 = get_address(s, x2, get_field(s, b2), get_field(s, d2));
}
#define SPEC_in2_a2 0

static TCGv gen_ri2(DisasContext *s)
{
    TCGv ri2 = NULL;
    bool is_imm;
    int imm;

    disas_jdest(s, i2, is_imm, imm, ri2);
    if (is_imm) {
        ri2 = tcg_constant_i64(s->base.pc_next + (int64_t)imm * 2);
    }

    return ri2;
}

static void in2_ri2(DisasContext *s, DisasOps *o)
{
    o->in2 = gen_ri2(s);
}
#define SPEC_in2_ri2 0

static void in2_sh(DisasContext *s, DisasOps *o)
{
    int b2 = get_field(s, b2);
    int d2 = get_field(s, d2);

    if (b2 == 0) {
        o->in2 = tcg_constant_i64(d2 & 0x3f);
    } else {
        o->in2 = get_address(s, 0, b2, d2);
        tcg_gen_andi_i64(o->in2, o->in2, 0x3f);
    }
}
#define SPEC_in2_sh 0

static void in2_m2_8u(DisasContext *s, DisasOps *o)
{
    in2_a2(s, o);
    tcg_gen_qemu_ld_i64(o->in2, o->in2, get_mem_index(s), MO_UB);
}
#define SPEC_in2_m2_8u 0

static void in2_m2_16s(DisasContext *s, DisasOps *o)
{
    in2_a2(s, o);
    tcg_gen_qemu_ld_i64(o->in2, o->in2, get_mem_index(s), MO_TESW);
}
#define SPEC_in2_m2_16s 0

static void in2_m2_16u(DisasContext *s, DisasOps *o)
{
    in2_a2(s, o);
    tcg_gen_qemu_ld_i64(o->in2, o->in2, get_mem_index(s), MO_TEUW);
}
#define SPEC_in2_m2_16u 0

static void in2_m2_32s(DisasContext *s, DisasOps *o)
{
    in2_a2(s, o);
    tcg_gen_qemu_ld_i64(o->in2, o->in2, get_mem_index(s), MO_TESL);
}
#define SPEC_in2_m2_32s 0

static void in2_m2_32u(DisasContext *s, DisasOps *o)
{
    in2_a2(s, o);
    tcg_gen_qemu_ld_i64(o->in2, o->in2, get_mem_index(s), MO_TEUL);
}
#define SPEC_in2_m2_32u 0

#ifndef CONFIG_USER_ONLY
static void in2_m2_32ua(DisasContext *s, DisasOps *o)
{
    in2_a2(s, o);
    tcg_gen_qemu_ld_tl(o->in2, o->in2, get_mem_index(s), MO_TEUL | MO_ALIGN);
}
#define SPEC_in2_m2_32ua 0
#endif

static void in2_m2_64(DisasContext *s, DisasOps *o)
{
    in2_a2(s, o);
    tcg_gen_qemu_ld_i64(o->in2, o->in2, get_mem_index(s), MO_TEUQ);
}
#define SPEC_in2_m2_64 0

static void in2_m2_64w(DisasContext *s, DisasOps *o)
{
    in2_a2(s, o);
    tcg_gen_qemu_ld_i64(o->in2, o->in2, get_mem_index(s), MO_TEUQ);
    gen_addi_and_wrap_i64(s, o->in2, o->in2, 0);
}
#define SPEC_in2_m2_64w 0

#ifndef CONFIG_USER_ONLY
static void in2_m2_64a(DisasContext *s, DisasOps *o)
{
    in2_a2(s, o);
    tcg_gen_qemu_ld_i64(o->in2, o->in2, get_mem_index(s), MO_TEUQ | MO_ALIGN);
}
#define SPEC_in2_m2_64a 0
#endif

static void in2_mri2_16s(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_qemu_ld_i64(o->in2, gen_ri2(s), get_mem_index(s), MO_TESW);
}
#define SPEC_in2_mri2_16s 0

static void in2_mri2_16u(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_qemu_ld_i64(o->in2, gen_ri2(s), get_mem_index(s), MO_TEUW);
}
#define SPEC_in2_mri2_16u 0

static void in2_mri2_32s(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_qemu_ld_tl(o->in2, gen_ri2(s), get_mem_index(s),
                       MO_TESL | MO_ALIGN);
}
#define SPEC_in2_mri2_32s 0

static void in2_mri2_32u(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_qemu_ld_tl(o->in2, gen_ri2(s), get_mem_index(s),
                       MO_TEUL | MO_ALIGN);
}
#define SPEC_in2_mri2_32u 0

static void in2_mri2_64(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_qemu_ld_i64(o->in2, gen_ri2(s), get_mem_index(s),
                        MO_TEUQ | MO_ALIGN);
}
#define SPEC_in2_mri2_64 0

static void in2_i2(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_constant_i64(get_field(s, i2));
}
#define SPEC_in2_i2 0

static void in2_i2_8u(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_constant_i64((uint8_t)get_field(s, i2));
}
#define SPEC_in2_i2_8u 0

static void in2_i2_16u(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_constant_i64((uint16_t)get_field(s, i2));
}
#define SPEC_in2_i2_16u 0

static void in2_i2_32u(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_constant_i64((uint32_t)get_field(s, i2));
}
#define SPEC_in2_i2_32u 0

static void in2_i2_16u_shl(DisasContext *s, DisasOps *o)
{
    uint64_t i2 = (uint16_t)get_field(s, i2);
    o->in2 = tcg_constant_i64(i2 << s->insn->data);
}
#define SPEC_in2_i2_16u_shl 0

static void in2_i2_32u_shl(DisasContext *s, DisasOps *o)
{
    uint64_t i2 = (uint32_t)get_field(s, i2);
    o->in2 = tcg_constant_i64(i2 << s->insn->data);
}
#define SPEC_in2_i2_32u_shl 0

#ifndef CONFIG_USER_ONLY
static void in2_insn(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_constant_i64(s->fields.raw_insn);
}
#define SPEC_in2_insn 0
#endif

#define C(OPC, NM, FT, FC, I1, I2, P, W, OP, CC) \
    E(OPC, NM, FT, FC, I1, I2, P, W, OP, CC, 0, 0)

#define D(OPC, NM, FT, FC, I1, I2, P, W, OP, CC, D) \
    E(OPC, NM, FT, FC, I1, I2, P, W, OP, CC, D, 0)

#define F(OPC, NM, FT, FC, I1, I2, P, W, OP, CC, FL) \
    E(OPC, NM, FT, FC, I1, I2, P, W, OP, CC, 0, FL)

#define E(OPC, NM, FT, FC, I1, I2, P, W, OP, CC, D, FL) insn_ ## NM,

enum DisasInsnEnum {
#include "insn-data.h.inc"
};

#undef E
#define E(OPC, NM, FT, FC, I1, I2, P, W, OP, CC, D, FL) {                   \
    .opc = OPC,                                                             \
    .flags = FL,                                                            \
    .fmt = FMT_##FT,                                                        \
    .fac = FAC_##FC,                                                        \
    .spec = SPEC_in1_##I1 | SPEC_in2_##I2 | SPEC_prep_##P | SPEC_wout_##W,  \
    .name = #NM,                                                            \
    .help_in1 = in1_##I1,                                                   \
    .help_in2 = in2_##I2,                                                   \
    .help_prep = prep_##P,                                                  \
    .help_wout = wout_##W,                                                  \
    .help_cout = cout_##CC,                                                 \
    .help_op = op_##OP,                                                     \
    .data = D                                                               \
 },

#define in1_0  NULL
#define in2_0  NULL
#define prep_0  NULL
#define wout_0  NULL
#define cout_0  NULL
#define op_0  NULL

#define SPEC_in1_0 0
#define SPEC_in2_0 0
#define SPEC_prep_0 0
#define SPEC_wout_0 0

#define FAC_Z           S390_FEAT_ZARCH
#define FAC_CASS        S390_FEAT_COMPARE_AND_SWAP_AND_STORE
#define FAC_DFP         S390_FEAT_DFP
#define FAC_DFPR        S390_FEAT_FLOATING_POINT_SUPPORT_ENH
#define FAC_DO          S390_FEAT_STFLE_45
#define FAC_EE          S390_FEAT_EXECUTE_EXT
#define FAC_EI          S390_FEAT_EXTENDED_IMMEDIATE
#define FAC_FPE         S390_FEAT_FLOATING_POINT_EXT
#define FAC_FPSSH       S390_FEAT_FLOATING_POINT_SUPPORT_ENH
#define FAC_FPRGR       S390_FEAT_FLOATING_POINT_SUPPORT_ENH
#define FAC_GIE         S390_FEAT_GENERAL_INSTRUCTIONS_EXT
#define FAC_HFP_MA      S390_FEAT_HFP_MADDSUB
#define FAC_HW          S390_FEAT_STFLE_45
#define FAC_IEEEE_SIM   S390_FEAT_FLOATING_POINT_SUPPORT_ENH
#define FAC_MIE         S390_FEAT_STFLE_49
#define FAC_LAT         S390_FEAT_STFLE_49
#define FAC_LOC         S390_FEAT_STFLE_45
#define FAC_LOC2        S390_FEAT_STFLE_53
#define FAC_LD          S390_FEAT_LONG_DISPLACEMENT
#define FAC_PC          S390_FEAT_STFLE_45
#define FAC_SCF         S390_FEAT_STORE_CLOCK_FAST
#define FAC_SFLE        S390_FEAT_STFLE
#define FAC_ILA         S390_FEAT_STFLE_45
#define FAC_MVCOS       S390_FEAT_MOVE_WITH_OPTIONAL_SPEC
#define FAC_LPP         S390_FEAT_SET_PROGRAM_PARAMETERS
#define FAC_DAT_ENH     S390_FEAT_DAT_ENH
#define FAC_E2          S390_FEAT_EXTENDED_TRANSLATION_2
#define FAC_EH          S390_FEAT_STFLE_49
#define FAC_PPA         S390_FEAT_STFLE_49
#define FAC_LZRB        S390_FEAT_STFLE_53
#define FAC_ETF3        S390_FEAT_EXTENDED_TRANSLATION_3
#define FAC_MSA         S390_FEAT_MSA
#define FAC_MSA3        S390_FEAT_MSA_EXT_3
#define FAC_MSA4        S390_FEAT_MSA_EXT_4
#define FAC_MSA5        S390_FEAT_MSA_EXT_5
#define FAC_MSA8        S390_FEAT_MSA_EXT_8
#define FAC_ECT         S390_FEAT_EXTRACT_CPU_TIME
#define FAC_PCI         S390_FEAT_ZPCI
#define FAC_AIS         S390_FEAT_ADAPTER_INT_SUPPRESSION
#define FAC_V           S390_FEAT_VECTOR
#define FAC_VE          S390_FEAT_VECTOR_ENH
#define FAC_VE2         S390_FEAT_VECTOR_ENH2
#define FAC_MIE2        S390_FEAT_MISC_INSTRUCTION_EXT2
#define FAC_MIE3        S390_FEAT_MISC_INSTRUCTION_EXT3

static const DisasInsn insn_info[] = {
#include "insn-data.h.inc"
};

#undef E
#define E(OPC, NM, FT, FC, I1, I2, P, W, OP, CC, D, FL) \
    case OPC: return &insn_info[insn_ ## NM];

static const DisasInsn *lookup_opc(uint16_t opc)
{
    switch (opc) {
#include "insn-data.h.inc"
    default:
        return NULL;
    }
}

#undef F
#undef E
#undef D
#undef C

static void extract_field(DisasFields *o, const DisasField *f, uint64_t insn)
{
    uint32_t r, m;

    if (f->size == 0) {
        return;
    }

    r = (insn << f->beg) >> (64 - f->size);

    switch (f->type) {
    case 0:
        break;
    case 1:
        assert(f->size <= 32);
        m = 1u << (f->size - 1);
        r = (r ^ m) - m;
        break;
    case 2:
        r = ((int8_t)r << 12) | (r >> 8);
        break;
    case 3:
        g_assert(f->size == 4);
        switch (f->beg) {
        case 8:
            r |= extract64(insn, 63 - 36, 1) << 4;
            break;
        case 12:
            r |= extract64(insn, 63 - 37, 1) << 4;
            break;
        case 16:
            r |= extract64(insn, 63 - 38, 1) << 4;
            break;
        case 32:
            r |= extract64(insn, 63 - 39, 1) << 4;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    default:
        abort();
    }

    assert(((o->presentC >> f->indexC) & 1) == 0);
    o->presentC |= 1 << f->indexC;
    o->presentO |= 1 << f->indexO;

    o->c[f->indexC] = r;
}

static const DisasInsn *extract_insn(CPUS390XState *env, DisasContext *s)
{
    uint64_t insn, pc = s->base.pc_next;
    int op, op2, ilen;
    const DisasInsn *info;

    if (unlikely(s->ex_value)) {
        uint64_t be_insn;

        tcg_gen_st_i64(tcg_constant_i64(0), tcg_env,
                       offsetof(CPUS390XState, ex_value));

        insn = s->ex_value & 0xffffffffffff0000ull;
        ilen = s->ex_value & 0xf;
        op = insn >> 56;

        be_insn = cpu_to_be64(insn);
        translator_fake_ld(&s->base, &be_insn, get_ilen(op));
    } else {
        insn = ld_code2(env, s, pc);
        op = (insn >> 8) & 0xff;
        ilen = get_ilen(op);
        switch (ilen) {
        case 2:
            insn = insn << 48;
            break;
        case 4:
            insn = ld_code4(env, s, pc) << 32;
            break;
        case 6:
            insn = (insn << 48) | (ld_code4(env, s, pc + 2) << 16);
            break;
        default:
            g_assert_not_reached();
        }
    }
    s->pc_tmp = s->base.pc_next + ilen;
    s->ilen = ilen;

    switch (op) {
    case 0x01:
    case 0x80:
    case 0x82:
    case 0x93:
    case 0xb2:
    case 0xb3:
    case 0xb9:
    case 0xe5:
        op2 = (insn << 8) >> 56;
        break;
    case 0xa5:
    case 0xa7:
    case 0xc0:
    case 0xc2:
    case 0xc4:
    case 0xc6:
    case 0xc8:
    case 0xcc:
        op2 = (insn << 12) >> 60;
        break;
    case 0xc5:
    case 0xc7:
    case 0xd0 ... 0xdf:
    case 0xe1:
    case 0xe2:
    case 0xe8:
    case 0xe9:
    case 0xea:
    case 0xee ... 0xf3:
    case 0xf8 ... 0xfd:
        op2 = 0;
        break;
    default:
        op2 = (insn << 40) >> 56;
        break;
    }

    memset(&s->fields, 0, sizeof(s->fields));
    s->fields.raw_insn = insn;
    s->fields.op = op;
    s->fields.op2 = op2;

    info = lookup_opc(op << 8 | op2);
    s->insn = info;

    if (info != NULL) {
        DisasFormat fmt = info->fmt;
        int i;

        for (i = 0; i < NUM_C_FIELD; ++i) {
            extract_field(&s->fields, &format_info[fmt].op[i], insn);
        }
    }
    return info;
}

static bool is_afp_reg(int reg)
{
    return reg % 2 || reg > 6;
}

static bool is_fp_pair(int reg)
{
    return !(reg & 0x2);
}

static DisasJumpType translate_one(CPUS390XState *env, DisasContext *s)
{
    const DisasInsn *insn;
    DisasJumpType ret = DISAS_NEXT;
    DisasOps o = {};
    bool icount = false;

    insn = extract_insn(env, s);

    tcg_set_insn_start_param(s->base.insn_start, 2, s->ilen);

    if (insn == NULL) {
        qemu_log_mask(LOG_UNIMP, "unimplemented opcode 0x%02x%02x\n",
                      s->fields.op, s->fields.op2);
        gen_illegal_opcode(s);
        ret = DISAS_NORETURN;
        goto out;
    }

#ifndef CONFIG_USER_ONLY
    if (s->base.tb->flags & FLAG_MASK_PER_IFETCH) {
        gen_helper_per_ifetch(tcg_env, tcg_constant_i32(s->ilen));
    }
#endif

    if (insn->flags) {
        if ((s->base.tb->flags & FLAG_MASK_PSTATE) && (insn->flags & IF_PRIV)) {
            gen_program_exception(s, PGM_PRIVILEGED);
            ret = DISAS_NORETURN;
            goto out;
        }

        if (!(s->base.tb->flags & FLAG_MASK_AFP)) {
            uint8_t dxc = 0;

            if ((insn->flags & IF_AFP1) && is_afp_reg(get_field(s, r1))) {
                dxc = 1;
            }
            if ((insn->flags & IF_AFP2) && is_afp_reg(get_field(s, r2))) {
                dxc = 1;
            }
            if ((insn->flags & IF_AFP3) && is_afp_reg(get_field(s, r3))) {
                dxc = 1;
            }
            if (insn->flags & IF_BFP) {
                dxc = 2;
            }
            if (insn->flags & IF_DFP) {
                dxc = 3;
            }
            if (insn->flags & IF_VEC) {
                dxc = 0xfe;
            }
            if (dxc) {
                gen_data_exception(dxc);
                ret = DISAS_NORETURN;
                goto out;
            }
        }

        if (insn->flags & IF_VEC) {
            if (!((s->base.tb->flags & FLAG_MASK_VECTOR))) {
                gen_data_exception(0xfe);
                ret = DISAS_NORETURN;
                goto out;
            }
        }

        if (unlikely(insn->flags & IF_IO)) {
            icount = translator_io_start(&s->base);
        }
    }

    if (insn->spec) {
        if ((insn->spec & SPEC_r1_even && get_field(s, r1) & 1) ||
            (insn->spec & SPEC_r2_even && get_field(s, r2) & 1) ||
            (insn->spec & SPEC_r3_even && get_field(s, r3) & 1) ||
            (insn->spec & SPEC_r1_f128 && !is_fp_pair(get_field(s, r1))) ||
            (insn->spec & SPEC_r2_f128 && !is_fp_pair(get_field(s, r2)))) {
            gen_program_exception(s, PGM_SPECIFICATION);
            ret = DISAS_NORETURN;
            goto out;
        }
    }

    if (insn->help_in1) {
        insn->help_in1(s, &o);
    }
    if (insn->help_in2) {
        insn->help_in2(s, &o);
    }
    if (insn->help_prep) {
        insn->help_prep(s, &o);
    }
    if (insn->help_op) {
        ret = insn->help_op(s, &o);
        if (ret == DISAS_NORETURN) {
            goto out;
        }
    }
    if (insn->help_wout) {
        insn->help_wout(s, &o);
    }
    if (insn->help_cout) {
        insn->help_cout(s, &o);
    }

    if (unlikely(icount && ret == DISAS_NEXT)) {
        ret = DISAS_TOO_MANY;
    }

#ifndef CONFIG_USER_ONLY
    if (s->base.tb->flags & FLAG_MASK_PER_IFETCH) {
        switch (ret) {
        case DISAS_TOO_MANY:
            s->base.is_jmp = DISAS_PC_CC_UPDATED;
            /* fall through */
        case DISAS_NEXT:
            tcg_gen_movi_i64(psw_addr, s->pc_tmp);
            break;
        default:
            break;
        }
        update_cc_op(s);
        gen_helper_per_check_exception(tcg_env);
    }
#endif

out:
    s->base.pc_next = s->pc_tmp;
    return ret;
}

static void s390x_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    if (dc->base.tb->flags & FLAG_MASK_32) {
        if (!(dc->base.tb->flags & FLAG_MASK_64)) {
            assert(!(dc->base.pc_first & ~((1ULL << 31) - 1)));
        }
    } else {
        assert(!(dc->base.pc_first & ~((1ULL << 24) - 1)));
    }

    dc->cc_op = CC_OP_DYNAMIC;
    dc->ex_value = dc->base.tb->cs_base;
    dc->exit_to_mainloop = dc->ex_value;
}

static void s390x_tr_tb_start(DisasContextBase *db, CPUState *cs)
{
}

static void s390x_tr_insn_start(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    tcg_gen_insn_start(dc->base.pc_next, dc->cc_op, 0);
}

static target_ulong get_next_pc(CPUS390XState *env, DisasContext *s,
                                uint64_t pc)
{
    uint64_t insn = translator_lduw(env, &s->base, pc);

    return pc + get_ilen((insn >> 8) & 0xff);
}

static void s390x_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    CPUS390XState *env = cpu_env(cs);
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    dc->base.is_jmp = translate_one(env, dc);
    if (dc->base.is_jmp == DISAS_NEXT) {
        if (dc->ex_value ||
            !translator_is_same_page(dcbase, dc->base.pc_next) ||
            !translator_is_same_page(dcbase, get_next_pc(env, dc, dc->base.pc_next))) {
            dc->base.is_jmp = DISAS_TOO_MANY;
        }
    }
}

static void s390x_tr_tb_stop(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    switch (dc->base.is_jmp) {
    case DISAS_NORETURN:
        break;
    case DISAS_TOO_MANY:
        update_psw_addr(dc);
        /* FALLTHRU */
    case DISAS_PC_UPDATED:
        update_cc_op(dc);
        /* FALLTHRU */
    case DISAS_PC_CC_UPDATED:
        if (dc->exit_to_mainloop) {
            tcg_gen_exit_tb(NULL, 0);
        } else {
            tcg_gen_lookup_and_goto_ptr();
        }
        break;
    default:
        g_assert_not_reached();
    }
}

static bool s390x_tr_disas_log(const DisasContextBase *dcbase,
                               CPUState *cs, FILE *logfile)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    if (unlikely(dc->ex_value)) {
        fprintf(logfile, "IN: EXECUTE\n");
        target_disas(logfile, cs, &dc->base);
        return true;
    }
    return false;
}

static const TranslatorOps s390x_tr_ops = {
    .init_disas_context = s390x_tr_init_disas_context,
    .tb_start           = s390x_tr_tb_start,
    .insn_start         = s390x_tr_insn_start,
    .translate_insn     = s390x_tr_translate_insn,
    .tb_stop            = s390x_tr_tb_stop,
    .disas_log          = s390x_tr_disas_log,
};

void s390x_translate_code(CPUState *cs, TranslationBlock *tb,
                          int *max_insns, vaddr pc, void *host_pc)
{
    DisasContext dc;

    translator_loop(cs, tb, max_insns, pc, host_pc, &s390x_tr_ops, &dc.base);
}

void s390x_restore_state_to_opc(CPUState *cs,
                                const TranslationBlock *tb,
                                const uint64_t *data)
{
    CPUS390XState *env = cpu_env(cs);
    int cc_op = data[1];

    env->psw.addr = data[0];

    if ((cc_op != CC_OP_DYNAMIC) && (cc_op != CC_OP_STATIC)) {
        env->cc_op = cc_op;
    }

    env->int_pgm_ilen = data[2];
}
