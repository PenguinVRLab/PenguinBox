#ifndef TARGET_ARM_TRANSLATE_H
#define TARGET_ARM_TRANSLATE_H

#include "cpu.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "exec/translator.h"
#include "exec/translation-block.h"
#include "exec/helper-gen.h"
#include "internals.h"
#include "cpu-features.h"

typedef struct DisasLabel {
    TCGLabel *label;
    target_ulong pc_save;
} DisasLabel;

typedef struct DisasDelayException {
    struct DisasDelayException *next;
    TCGLabel *lab;
    target_long pc_curr;
    target_long pc_save;
    int condexec_mask;
    int condexec_cond;
    uint32_t excp;
    uint32_t syn;
    uint32_t target_el;
} DisasDelayException;

typedef struct DisasContext {
    DisasContextBase base;
    const ARMISARegisters *isar;
    DisasDelayException *delay_excp_list;

    target_ulong pc_curr;
    target_ulong pc_save;
    target_ulong page_start;
    uint32_t insn;
    int condjmp;
    DisasLabel condlabel;
    int condexec_mask;
    int condexec_cond;
    int eci;
    bool eci_handled;
    int sctlr_b;
    MemOp be_data;
#if !defined(CONFIG_USER_ONLY)
    int user;
#endif
    ARMMMUIdx mmu_idx;
    uint8_t tbii;
    uint8_t tbid;
    uint8_t tcma;
    bool ns;
    int fp_excp_el;
    int sve_excp_el;
    int sme_excp_el;
    int zt0_excp_el;
    int vl;
    int svl;
    int max_svl;
    bool vfp_enabled;
    int vec_len;
    int vec_stride;
    bool v7m_handler_mode;
    bool v8m_secure;
    bool v8m_stackcheck;
    bool v8m_fpccr_s_wrong;
    bool v7m_new_fp_ctxt_needed;
    bool v7m_lspact;
    uint32_t svc_imm;
    int current_el;
    GHashTable *cp_regs;
    uint64_t features;
    bool aarch64;
    bool thumb;
    bool lse2;
    int8_t fp_access_checked;
    int8_t sve_access_checked;
    bool ss_active;
    bool pstate_ss;
    bool is_ldex;
    bool unpriv;
    bool pauth_active;
    bool ata[2];
    bool mte_active[2];
    bool bt;
    bool hstr_active;
    bool align_mem;
    bool pstate_il;
    bool pstate_sm;
    bool pstate_za;
    bool sme_trap_nonstreaming;
    bool is_nonstreaming;
    bool mve_no_pred;
    bool fgt_active;
    bool fgt_svc;
    bool trap_eret;
    bool naa;
    bool e2h;
    bool nv;
    bool nv1;
    bool nv2;
    bool nv2_mem_e20;
    bool nv2_mem_be;
    bool fpcr_ah;
    bool fpcr_nep;
    bool gcs_en;
    bool gcs_rvcen;
    uint8_t gcsstr_el;
    int8_t btype;
    uint8_t dcz_blocksize;
    uint8_t gm_blocksize;
    bool insn_start_updated;
    uint32_t nv2_redirect_offset;
} DisasContext;

typedef struct DisasCompare {
    TCGCond cond;
    TCGv_i32 value;
} DisasCompare;

extern TCGv_i32 cpu_NF, cpu_ZF, cpu_CF, cpu_VF;
extern TCGv_i64 cpu_exclusive_addr;
extern TCGv_i64 cpu_exclusive_val;

static inline int negate(DisasContext *s, int x)
{
    return -x;
}

static inline int plus_1(DisasContext *s, int x)
{
    return x + 1;
}

static inline int plus_2(DisasContext *s, int x)
{
    return x + 2;
}

static inline int plus_8(DisasContext *s, int x)
{
    return x + 8;
}

static inline int plus_12(DisasContext *s, int x)
{
    return x + 12;
}

static inline int times_2(DisasContext *s, int x)
{
    return x * 2;
}

static inline int times_4(DisasContext *s, int x)
{
    return x * 4;
}

static inline int times_8(DisasContext *s, int x)
{
    return x * 8;
}

static inline int times_2_plus_1(DisasContext *s, int x)
{
    return x * 2 + 1;
}

static inline int rsub_64(DisasContext *s, int x)
{
    return 64 - x;
}

static inline int rsub_32(DisasContext *s, int x)
{
    return 32 - x;
}

static inline int rsub_16(DisasContext *s, int x)
{
    return 16 - x;
}

static inline int rsub_8(DisasContext *s, int x)
{
    return 8 - x;
}

static inline int shl_12(DisasContext *s, int x)
{
    return x << 12;
}

static inline int xor_2(DisasContext *s, int x)
{
    return x ^ 2;
}

static inline int neon_3same_fp_size(DisasContext *s, int x)
{
    return MO_32 - x;
}

static inline int arm_dc_feature(DisasContext *dc, int feature)
{
    return (dc->features & (1ULL << feature)) != 0;
}

static inline int get_mem_index(DisasContext *s)
{
    return arm_to_core_mmu_idx(s->mmu_idx);
}

static inline void disas_set_insn_syndrome(DisasContext *s, uint32_t syn)
{
    syn &= ARM_INSN_START_WORD2_MASK;
    syn >>= ARM_INSN_START_WORD2_SHIFT;

    assert(!s->insn_start_updated);
    s->insn_start_updated = true;
    tcg_set_insn_start_param(s->base.insn_start, 2, syn);
}

static inline int curr_insn_len(DisasContext *s)
{
    return s->base.pc_next - s->pc_curr;
}

#define DISAS_JUMP      DISAS_TARGET_0
#define DISAS_UPDATE_EXIT  DISAS_TARGET_1
#define DISAS_WFI       DISAS_TARGET_2
#define DISAS_SWI       DISAS_TARGET_3
#define DISAS_WFE       DISAS_TARGET_4
#define DISAS_HVC       DISAS_TARGET_5
#define DISAS_SMC       DISAS_TARGET_6
#define DISAS_YIELD     DISAS_TARGET_7
#define DISAS_BX_EXCRET DISAS_TARGET_8
#define DISAS_EXIT      DISAS_TARGET_9
#define DISAS_UPDATE_NOCHAIN  DISAS_TARGET_10

#ifdef TARGET_AARCH64
void a64_translate_init(void);
void gen_a64_update_pc(DisasContext *s, target_long diff);
extern const TranslatorOps aarch64_translator_ops;
#else
static inline void a64_translate_init(void)
{
}

static inline void gen_a64_update_pc(DisasContext *s, target_long diff)
{
}
#endif

void arm_test_cc(DisasCompare *cmp, int cc);
void arm_jump_cc(DisasCompare *cmp, TCGLabel *label);
void arm_gen_test_cc(int cc, TCGLabel *label);
MemOp pow2_align(unsigned i);
void unallocated_encoding(DisasContext *s);
void gen_exception_internal(int excp);
void gen_exception_insn_el(DisasContext *s, target_long pc_diff, int excp,
                           uint32_t syn, uint32_t target_el);
void gen_exception_insn(DisasContext *s, target_long pc_diff,
                        int excp, uint32_t syn);
TCGLabel *delay_exception_el(DisasContext *s, int excp,
                             uint32_t syn, uint32_t target_el);
TCGLabel *delay_exception(DisasContext *s, int excp, uint32_t syn);
void emit_delayed_exceptions(DisasContext *s);

static inline TCGv_i32 get_ahp_flag(void)
{
    TCGv_i32 ret = tcg_temp_new_i32();

    tcg_gen_ld_i32(ret, tcg_env, offsetoflow32(CPUARMState, vfp.fpcr));
    tcg_gen_extract_i32(ret, ret, 26, 1);

    return ret;
}

static inline void set_pstate_bits(uint64_t bits)
{
    TCGv_i64 p = tcg_temp_new_i64();

    tcg_debug_assert(!(bits & CACHED_PSTATE_BITS));

    tcg_gen_ld_i64(p, tcg_env, offsetof(CPUARMState, pstate));
    tcg_gen_ori_i64(p, p, bits);
    tcg_gen_st_i64(p, tcg_env, offsetof(CPUARMState, pstate));
}

static inline void clear_pstate_bits(uint64_t bits)
{
    TCGv_i64 p = tcg_temp_new_i64();

    tcg_debug_assert(!(bits & CACHED_PSTATE_BITS));

    tcg_gen_ld_i64(p, tcg_env, offsetof(CPUARMState, pstate));
    tcg_gen_andi_i64(p, p, ~bits);
    tcg_gen_st_i64(p, tcg_env, offsetof(CPUARMState, pstate));
}

static inline void gen_ss_advance(DisasContext *s)
{
    if (s->ss_active) {
        s->pstate_ss = 0;
        clear_pstate_bits(PSTATE_SS);
    }
}

static inline void gen_swstep_exception(DisasContext *s, int isv, int ex)
{
    uint32_t syn = syn_swstep(false, isv, ex);
    gen_helper_exception_swstep(tcg_env, tcg_constant_i32(syn));
}

uint64_t vfp_expand_imm(int size, uint8_t imm8);

static inline void gen_vfp_absh(TCGv_i32 d, TCGv_i32 s)
{
    tcg_gen_andi_i32(d, s, INT16_MAX);
}

static inline void gen_vfp_abss(TCGv_i32 d, TCGv_i32 s)
{
    tcg_gen_andi_i32(d, s, INT32_MAX);
}

static inline void gen_vfp_absd(TCGv_i64 d, TCGv_i64 s)
{
    tcg_gen_andi_i64(d, s, INT64_MAX);
}

static inline void gen_vfp_negh(TCGv_i32 d, TCGv_i32 s)
{
    tcg_gen_xori_i32(d, s, 1u << 15);
}

static inline void gen_vfp_negs(TCGv_i32 d, TCGv_i32 s)
{
    tcg_gen_xori_i32(d, s, 1u << 31);
}

static inline void gen_vfp_negd(TCGv_i64 d, TCGv_i64 s)
{
    tcg_gen_xori_i64(d, s, 1ull << 63);
}

void gen_gvec_ceq0(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                   uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_clt0(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                   uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_cgt0(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                   uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_cle0(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                   uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_cge0(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                   uint32_t opr_sz, uint32_t max_sz);

void gen_gvec_mla(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                  uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_mls(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                  uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);

void gen_gvec_cmtst(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_sshl(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_ushl(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_srshl(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_urshl(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_neon_sqshl(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_neon_uqshl(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_neon_sqrshl(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                     uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_neon_uqrshl(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                     uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);

void gen_neon_sqshli(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                     int64_t c, uint32_t opr_sz, uint32_t max_sz);
void gen_neon_uqshli(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                     int64_t c, uint32_t opr_sz, uint32_t max_sz);
void gen_neon_sqshlui(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                      int64_t c, uint32_t opr_sz, uint32_t max_sz);

void gen_gvec_shadd(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_uhadd(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_shsub(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_uhsub(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_srhadd(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                     uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_urhadd(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                     uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);

void gen_cmtst_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b);
void gen_ushl_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b);
void gen_sshl_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b);
void gen_ushl_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b);
void gen_sshl_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b);

void gen_uqadd_bhs(TCGv_i64 res, TCGv_i64 qc,
                   TCGv_i64 a, TCGv_i64 b, MemOp esz);
void gen_uqadd_d(TCGv_i64 d, TCGv_i64 q, TCGv_i64 a, TCGv_i64 b);
void gen_gvec_uqadd_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                       uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);

void gen_sqadd_bhs(TCGv_i64 res, TCGv_i64 qc,
                   TCGv_i64 a, TCGv_i64 b, MemOp esz);
void gen_sqadd_d(TCGv_i64 d, TCGv_i64 q, TCGv_i64 a, TCGv_i64 b);
void gen_gvec_sqadd_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                       uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);

void gen_uqsub_bhs(TCGv_i64 res, TCGv_i64 qc,
                   TCGv_i64 a, TCGv_i64 b, MemOp esz);
void gen_uqsub_d(TCGv_i64 d, TCGv_i64 q, TCGv_i64 a, TCGv_i64 b);
void gen_gvec_uqsub_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                       uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);

void gen_sqsub_bhs(TCGv_i64 res, TCGv_i64 qc,
                   TCGv_i64 a, TCGv_i64 b, MemOp esz);
void gen_sqsub_d(TCGv_i64 d, TCGv_i64 q, TCGv_i64 a, TCGv_i64 b);
void gen_gvec_sqsub_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                       uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);

void gen_gvec_sshr(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                   int64_t shift, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_ushr(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                   int64_t shift, uint32_t opr_sz, uint32_t max_sz);

void gen_gvec_ssra(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                   int64_t shift, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_usra(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                   int64_t shift, uint32_t opr_sz, uint32_t max_sz);

void gen_srshr32_i32(TCGv_i32 d, TCGv_i32 a, int32_t sh);
void gen_srshr64_i64(TCGv_i64 d, TCGv_i64 a, int64_t sh);
void gen_urshr32_i32(TCGv_i32 d, TCGv_i32 a, int32_t sh);
void gen_urshr64_i64(TCGv_i64 d, TCGv_i64 a, int64_t sh);

void gen_gvec_srshr(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                    int64_t shift, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_urshr(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                    int64_t shift, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_srsra(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                    int64_t shift, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_ursra(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                    int64_t shift, uint32_t opr_sz, uint32_t max_sz);

void gen_gvec_sri(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                  int64_t shift, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_sli(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                  int64_t shift, uint32_t opr_sz, uint32_t max_sz);

void gen_gvec_sqdmulh_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                         uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_sqrdmulh_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                          uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_sqrdmlah_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                          uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_sqrdmlsh_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                          uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);

void gen_gvec_sabd(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_uabd(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);

void gen_gvec_saba(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_uaba(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);

void gen_gvec_addp(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_smaxp(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_sminp(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_umaxp(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_uminp(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);

void gen_gvec_cls(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                  uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_clz(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                  uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_cnt(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                  uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_rbit(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_rev16(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_rev32(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_rev64(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t opr_sz, uint32_t max_sz);

void gen_gvec_saddlp(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                     uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_sadalp(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                     uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_uaddlp(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                     uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_uadalp(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                     uint32_t opr_sz, uint32_t max_sz);

void gen_gvec_fabs(unsigned vece, uint32_t dofs, uint32_t aofs,
                   uint32_t oprsz, uint32_t maxsz);
void gen_gvec_fneg(unsigned vece, uint32_t dofs, uint32_t aofs,
                   uint32_t oprsz, uint32_t maxsz);

void gen_gvec_urecpe(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                     uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_ursqrte(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                      uint32_t opr_sz, uint32_t max_sz);

#define dc_isar_feature(name, ctx) \
    ({ DisasContext *ctx_ = (ctx); isar_feature_##name(ctx_->isar); })

typedef void GVecGen2Fn(unsigned, uint32_t, uint32_t, uint32_t, uint32_t);
typedef void GVecGen2iFn(unsigned, uint32_t, uint32_t, int64_t,
                         uint32_t, uint32_t);
typedef void GVecGen3Fn(unsigned, uint32_t, uint32_t,
                        uint32_t, uint32_t, uint32_t);
typedef void GVecGen4Fn(unsigned, uint32_t, uint32_t, uint32_t,
                        uint32_t, uint32_t, uint32_t);
typedef void GVecGen3FnVar(unsigned, TCGv_ptr, uint32_t, TCGv_ptr, uint32_t,
                           TCGv_ptr, uint32_t, uint32_t, uint32_t);

typedef void NeonGenOneOpFn(TCGv_i32, TCGv_i32);
typedef void NeonGenOneOpEnvFn(TCGv_i32, TCGv_ptr, TCGv_i32);
typedef void NeonGenTwoOpFn(TCGv_i32, TCGv_i32, TCGv_i32);
typedef void NeonGenTwoOpEnvFn(TCGv_i32, TCGv_ptr, TCGv_i32, TCGv_i32);
typedef void NeonGenThreeOpEnvFn(TCGv_i32, TCGv_env, TCGv_i32,
                                 TCGv_i32, TCGv_i32);
typedef void NeonGenTwo64OpFn(TCGv_i64, TCGv_i64, TCGv_i64);
typedef void NeonGenTwo64OpEnvFn(TCGv_i64, TCGv_ptr, TCGv_i64, TCGv_i64);
typedef void NeonGenNarrowFn(TCGv_i32, TCGv_i64);
typedef void NeonGenWidenFn(TCGv_i64, TCGv_i32);
typedef void NeonGenTwoOpWidenFn(TCGv_i64, TCGv_i32, TCGv_i32);
typedef void NeonGenOneSingleOpFn(TCGv_i32, TCGv_i32, TCGv_ptr);
typedef void NeonGenTwoSingleOpFn(TCGv_i32, TCGv_i32, TCGv_i32, TCGv_ptr);
typedef void NeonGenTwoDoubleOpFn(TCGv_i64, TCGv_i64, TCGv_i64, TCGv_ptr);
typedef void NeonGenOne64OpFn(TCGv_i64, TCGv_i64);
typedef void NeonGenOne64OpEnvFn(TCGv_i64, TCGv_env, TCGv_i64);
typedef void CryptoTwoOpFn(TCGv_ptr, TCGv_ptr);
typedef void CryptoThreeOpIntFn(TCGv_ptr, TCGv_ptr, TCGv_i32);
typedef void CryptoThreeOpFn(TCGv_ptr, TCGv_ptr, TCGv_ptr);
typedef void AtomicThreeOpFn(TCGv_i64, TCGv_i64, TCGv_i64, TCGArg, MemOp);
typedef void WideShiftImmFn(TCGv_i64, TCGv_i64, int64_t shift);
typedef void WideShiftFn(TCGv_i64, TCGv_ptr, TCGv_i64, TCGv_i32);
typedef void ShiftImmFn(TCGv_i32, TCGv_i32, int32_t shift);
typedef void ShiftFn(TCGv_i32, TCGv_ptr, TCGv_i32, TCGv_i32);

static inline CPUARMTBFlags arm_tbflags_from_tb(const TranslationBlock *tb)
{
    return (CPUARMTBFlags){ tb->flags, tb->cs_base };
}

static inline TCGv_ptr fpstatus_ptr(ARMFPStatusFlavour flavour)
{
    TCGv_ptr statusptr = tcg_temp_new_ptr();
    int offset = offsetof(CPUARMState, vfp.fp_status[flavour]);

    tcg_gen_addi_ptr(statusptr, tcg_env, offset);
    return statusptr;
}

static inline MemOp finalize_memop_atom(DisasContext *s, MemOp opc, MemOp atom)
{
    if (!(opc & MO_AMASK)) {
        opc |= MO_ALIGN | (s->align_mem ? 0 : MO_ALIGN_TLB_ONLY);
    }
    return opc | atom | s->be_data;
}

static inline MemOp finalize_memop(DisasContext *s, MemOp opc)
{
    MemOp atom = s->lse2 ? MO_ATOM_WITHIN16 : MO_ATOM_IFALIGN;
    return finalize_memop_atom(s, opc, atom);
}

static inline MemOp finalize_memop_pair(DisasContext *s, MemOp opc)
{
    MemOp atom = s->lse2 ? MO_ATOM_WITHIN16_PAIR : MO_ATOM_IFALIGN_PAIR;
    return finalize_memop_atom(s, opc, atom);
}

static inline MemOp finalize_memop_asimd(DisasContext *s, MemOp opc)
{
    if ((opc & MO_SIZE) == MO_128) {
        return finalize_memop_atom(s, opc, MO_ATOM_IFALIGN_PAIR);
    }
    return finalize_memop(s, opc);
}

uint64_t asimd_imm_const(uint32_t imm, int cmode, int op);

static inline DisasLabel gen_disas_label(DisasContext *s)
{
    return (DisasLabel){
        .label = gen_new_label(),
        .pc_save = s->pc_save,
    };
}

static inline void set_disas_label(DisasContext *s, DisasLabel l)
{
    gen_set_label(l.label);
    s->pc_save = l.pc_save;
}

static inline TCGv_ptr gen_lookup_cp_reg(uint32_t key)
{
    TCGv_ptr ret = tcg_temp_new_ptr();
    gen_helper_lookup_cp_reg(ret, tcg_env, tcg_constant_i32(key));
    return ret;
}

static inline TCGv_i32 gen_set_rmode(ARMFPRounding rmode, TCGv_ptr fpst)
{
    TCGv_i32 new = tcg_constant_i32(arm_rmode_to_sf(rmode));
    TCGv_i32 old = tcg_temp_new_i32();

    gen_helper_set_rmode(old, new, fpst);
    return old;
}

static inline void gen_restore_rmode(TCGv_i32 old, TCGv_ptr fpst)
{
    gen_helper_set_rmode(old, old, fpst);
}

#define TRANS(NAME, FUNC, ...) \
    static bool trans_##NAME(DisasContext *s, arg_##NAME *a) \
    { return FUNC(s, __VA_ARGS__); }
#define TRANS_FEAT(NAME, FEAT, FUNC, ...) \
    static bool trans_##NAME(DisasContext *s, arg_##NAME *a) \
    { return dc_isar_feature(FEAT, s) && FUNC(s, __VA_ARGS__); }

#define TRANS_FEAT_NONSTREAMING(NAME, FEAT, FUNC, ...)            \
    static bool trans_##NAME(DisasContext *s, arg_##NAME *a)      \
    {                                                             \
        s->is_nonstreaming = true;                                \
        return dc_isar_feature(FEAT, s) && FUNC(s, __VA_ARGS__);  \
    }

#endif
