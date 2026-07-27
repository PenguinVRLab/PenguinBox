/*
 *  AArch64 translation, common definitions.
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

#ifndef TARGET_ARM_TRANSLATE_A64_H
#define TARGET_ARM_TRANSLATE_A64_H

TCGv_i64 cpu_reg(DisasContext *s, int reg);
TCGv_i64 cpu_reg_sp(DisasContext *s, int reg);
TCGv_i64 read_cpu_reg(DisasContext *s, int reg, int sf);
TCGv_i64 read_cpu_reg_sp(DisasContext *s, int reg, int sf);
void write_fp_dreg(DisasContext *s, int reg, TCGv_i64 v);
bool logic_imm_decode_wmask(uint64_t *result, unsigned int immn,
                            unsigned int imms, unsigned int immr);
bool sve_access_check(DisasContext *s);
bool sme_enabled_check(DisasContext *s);
bool sme_enabled_check_with_svcr(DisasContext *s, unsigned);
uint64_t make_svemte_desc(DisasContext *s, unsigned vsz, uint32_t nregs,
                          uint32_t msz, bool is_write, uint32_t data);

static inline bool sme_sm_enabled_check(DisasContext *s)
{
    return sme_enabled_check_with_svcr(s, R_SVCR_SM_MASK);
}

static inline bool sme_za_enabled_check(DisasContext *s)
{
    return sme_enabled_check_with_svcr(s, R_SVCR_ZA_MASK);
}

static inline bool sme_smza_enabled_check(DisasContext *s)
{
    return sme_enabled_check_with_svcr(s, R_SVCR_SM_MASK | R_SVCR_ZA_MASK);
}

TCGv_i64 clean_data_tbi(DisasContext *s, TCGv_i64 addr);
TCGv_i64 gen_mte_check1(DisasContext *s, TCGv_i64 addr, bool is_write,
                        bool tag_checked, MemOp memop);
TCGv_i64 gen_mte_checkN(DisasContext *s, TCGv_i64 addr, bool is_write,
                        bool tag_checked, int total_size, MemOp memop);

static inline void assert_fp_access_checked(DisasContext *s)
{
#ifdef CONFIG_DEBUG_TCG
    if (unlikely(s->fp_access_checked <= 0)) {
        fprintf(stderr, "target-arm: FP access check missing for "
                "instruction 0x%08x\n", s->insn);
        abort();
    }
#endif
}

static inline int vec_reg_offset(DisasContext *s, int regno,
                                 int element, MemOp size)
{
    int element_size = 1 << size;
    int offs = element * element_size;
#if HOST_BIG_ENDIAN
    if (element_size < 8) {
        offs ^= 8 - element_size;
    }
#endif
    offs += offsetof(CPUARMState, vfp.zregs[regno]);
    assert_fp_access_checked(s);
    return offs;
}

static inline int vec_full_reg_offset(DisasContext *s, int regno)
{
    assert_fp_access_checked(s);
    return offsetof(CPUARMState, vfp.zregs[regno]);
}

static inline TCGv_ptr vec_full_reg_ptr(DisasContext *s, int regno)
{
    TCGv_ptr ret = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(ret, tcg_env, vec_full_reg_offset(s, regno));
    return ret;
}

static inline int vec_full_reg_size(DisasContext *s)
{
    return s->vl;
}

static inline int streaming_vec_reg_size(DisasContext *s)
{
    return s->svl;
}

static inline int pred_full_reg_offset(DisasContext *s, int regno)
{
    return offsetof(CPUARMState, vfp.pregs[regno]);
}

static inline int pred_full_reg_size(DisasContext *s)
{
    return s->vl >> 3;
}

static inline int streaming_pred_reg_size(DisasContext *s)
{
    return s->svl >> 3;
}

static inline int size_for_gvec(int size)
{
    if (size <= 8) {
        return 8;
    } else {
        return QEMU_ALIGN_UP(size, 16);
    }
}

static inline int pred_gvec_reg_size(DisasContext *s)
{
    return size_for_gvec(pred_full_reg_size(s));
}

static inline TCGv_ptr pred_full_reg_ptr(DisasContext *s, int regno)
{
    TCGv_ptr ret = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(ret, tcg_env, pred_full_reg_offset(s, regno));
    return ret;
}

static inline ARMFPStatusFlavour select_ah_fpst(DisasContext *s, MemOp esz)
{
    if (s->fpcr_ah) {
        return esz == MO_16 ? FPST_AH_F16 : FPST_AH;
    } else {
        return esz == MO_16 ? FPST_A64_F16 : FPST_A64;
    }
}

bool disas_sve(DisasContext *, uint32_t);
bool disas_sme(DisasContext *, uint32_t);

void gen_gvec_rax1(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_xar(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                  uint32_t rm_ofs, int64_t shift,
                  uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_eor3(unsigned vece, uint32_t d, uint32_t n, uint32_t m,
                   uint32_t a, uint32_t oprsz, uint32_t maxsz);
void gen_gvec_bcax(unsigned vece, uint32_t d, uint32_t n, uint32_t m,
                   uint32_t a, uint32_t oprsz, uint32_t maxsz);

void gen_suqadd_bhs(TCGv_i64 res, TCGv_i64 qc,
                    TCGv_i64 a, TCGv_i64 b, MemOp esz);
void gen_suqadd_d(TCGv_i64 res, TCGv_i64 qc, TCGv_i64 a, TCGv_i64 b);
void gen_gvec_suqadd_qc(unsigned vece, uint32_t rd_ofs,
                        uint32_t rn_ofs, uint32_t rm_ofs,
                        uint32_t opr_sz, uint32_t max_sz);

void gen_usqadd_bhs(TCGv_i64 res, TCGv_i64 qc,
                    TCGv_i64 a, TCGv_i64 b, MemOp esz);
void gen_usqadd_d(TCGv_i64 res, TCGv_i64 qc, TCGv_i64 a, TCGv_i64 b);
void gen_gvec_usqadd_qc(unsigned vece, uint32_t rd_ofs,
                        uint32_t rn_ofs, uint32_t rm_ofs,
                        uint32_t opr_sz, uint32_t max_sz);

void gen_gvec_sve2_sqdmulh(unsigned vece, uint32_t rd_ofs,
                           uint32_t rn_ofs, uint32_t rm_ofs,
                           uint32_t opr_sz, uint32_t max_sz);

void gen_sve_ldr(DisasContext *s, TCGv_ptr, int vofs,
                 int len, int rn, int imm, MemOp align);
void gen_sve_str(DisasContext *s, TCGv_ptr, int vofs,
                 int len, int rn, int imm, MemOp align);

#endif
