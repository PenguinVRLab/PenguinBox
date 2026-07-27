/*
 *  ARM translation: M-profile NOCP special-case instructions
 *
 *  Copyright (c) 2020 Linaro, Ltd.
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
#include "translate.h"
#include "translate-a32.h"

#include "decode-m-nocp.c.inc"

static bool trans_VLLDM_VLSTM(DisasContext *s, arg_VLLDM_VLSTM *a)
{
    TCGv_i32 fptr;

    if (!arm_dc_feature(s, ARM_FEATURE_M) ||
        !arm_dc_feature(s, ARM_FEATURE_V8)) {
        return false;
    }

    if (a->op) {
        if (!arm_dc_feature(s, ARM_FEATURE_V8_1M)) {
            return false;
        }
    } else {
        if (dc_isar_feature(aa32_simd_r32, s)) {
            unallocated_encoding(s);
            return true;
        }
    }

    if (!s->v8m_secure) {
        unallocated_encoding(s);
        return true;
    }

    s->eci_handled = true;

    if (!dc_isar_feature(aa32_vfp, s)) {
        clear_eci_state(s);
        return true;
    }

    fptr = load_reg(s, a->rn);
    if (a->l) {
        gen_helper_v7m_vlldm(tcg_env, fptr);
    } else {
        gen_helper_v7m_vlstm(tcg_env, fptr);
    }

    clear_eci_state(s);

    s->base.is_jmp = DISAS_UPDATE_EXIT;
    return true;
}

static bool trans_VSCCLRM(DisasContext *s, arg_VSCCLRM *a)
{
    int btmreg, topreg;
    TCGv_i64 zero;
    TCGv_i32 aspen, sfpa;

    if (!dc_isar_feature(aa32_m_sec_state, s)) {
        return false;
    }

    if (!arm_dc_feature(s, ARM_FEATURE_M_MAIN) || !s->v8m_secure) {
        unallocated_encoding(s);
        return true;
    }

    s->eci_handled = true;

    if (!dc_isar_feature(aa32_vfp_simd, s)) {
        clear_eci_state(s);
        return true;
    }

    aspen = load_cpu_field(v7m.fpccr[M_REG_S]);
    sfpa = load_cpu_field(v7m.control[M_REG_S]);
    tcg_gen_andi_i32(aspen, aspen, R_V7M_FPCCR_ASPEN_MASK);
    tcg_gen_xori_i32(aspen, aspen, R_V7M_FPCCR_ASPEN_MASK);
    tcg_gen_andi_i32(sfpa, sfpa, R_V7M_CONTROL_SFPA_MASK);
    tcg_gen_or_i32(sfpa, sfpa, aspen);
    arm_gen_condlabel(s);
    tcg_gen_brcondi_i32(TCG_COND_EQ, sfpa, 0, s->condlabel.label);

    if (s->fp_excp_el != 0) {
        gen_exception_insn_el(s, 0, EXCP_NOCP,
                              syn_uncategorized(), s->fp_excp_el);
        return true;
    }

    topreg = a->vd + a->imm - 1;
    btmreg = a->vd;

    if (a->size == 3) {
        topreg = topreg * 2 + 1;
        btmreg *= 2;
    }

    if (topreg > 63 || (topreg > 31 && !(topreg & 1))) {
        unallocated_encoding(s);
        return true;
    }

    if (topreg > 31 && !dc_isar_feature(aa32_simd_r32, s)) {
        topreg = 31;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    zero = tcg_constant_i64(0);
    if (btmreg & 1) {
        write_neon_element64(zero, btmreg >> 1, 1, MO_32);
        btmreg++;
    }
    for (; btmreg + 1 <= topreg; btmreg += 2) {
        write_neon_element64(zero, btmreg >> 1, 0, MO_64);
    }
    if (btmreg == topreg) {
        write_neon_element64(zero, btmreg >> 1, 0, MO_32);
        btmreg++;
    }
    assert(btmreg == topreg + 1);
    if (dc_isar_feature(aa32_mve, s)) {
        store_cpu_field(tcg_constant_i32(0), v7m.vpr);
    }

    clear_eci_state(s);
    return true;
}

typedef void fp_sysreg_storefn(DisasContext *s, void *opaque, TCGv_i32 value,
                               bool do_access);
typedef TCGv_i32 fp_sysreg_loadfn(DisasContext *s, void *opaque,
                                  bool do_access);

typedef enum FPSysRegCheckResult {
    FPSysRegCheckFailed,
    FPSysRegCheckDone,
    FPSysRegCheckContinue,
} FPSysRegCheckResult;

static FPSysRegCheckResult fp_sysreg_checks(DisasContext *s, int regno)
{
    if (!dc_isar_feature(aa32_fpsp_v2, s) && !dc_isar_feature(aa32_mve, s)) {
        return FPSysRegCheckFailed;
    }

    switch (regno) {
    case ARM_VFP_FPSCR:
    case QEMU_VFP_FPSCR_NZCV:
        break;
    case ARM_VFP_FPSCR_NZCVQC:
        if (!arm_dc_feature(s, ARM_FEATURE_V8_1M)) {
            return FPSysRegCheckFailed;
        }
        break;
    case ARM_VFP_FPCXT_S:
    case ARM_VFP_FPCXT_NS:
        if (!arm_dc_feature(s, ARM_FEATURE_V8_1M)) {
            return FPSysRegCheckFailed;
        }
        if (!s->v8m_secure) {
            return FPSysRegCheckFailed;
        }
        break;
    case ARM_VFP_VPR:
    case ARM_VFP_P0:
        if (!dc_isar_feature(aa32_mve, s)) {
            return FPSysRegCheckFailed;
        }
        break;
    default:
        return FPSysRegCheckFailed;
    }

    if (regno != ARM_VFP_FPCXT_NS && !vfp_access_check(s)) {
        return FPSysRegCheckDone;
    }
    return FPSysRegCheckContinue;
}

static void gen_branch_fpInactive(DisasContext *s, TCGCond cond,
                                  TCGLabel *label)
{
    assert(cond == TCG_COND_EQ || cond == TCG_COND_NE);

    TCGv_i32 aspen, fpca;
    aspen = load_cpu_field(v7m.fpccr[M_REG_NS]);
    fpca = load_cpu_field(v7m.control[M_REG_S]);
    tcg_gen_andi_i32(aspen, aspen, R_V7M_FPCCR_ASPEN_MASK);
    tcg_gen_xori_i32(aspen, aspen, R_V7M_FPCCR_ASPEN_MASK);
    tcg_gen_andi_i32(fpca, fpca, R_V7M_CONTROL_FPCA_MASK);
    tcg_gen_or_i32(fpca, fpca, aspen);
    tcg_gen_brcondi_i32(tcg_invert_cond(cond), fpca, 0, label);
}

static bool gen_M_fp_sysreg_write(DisasContext *s, int regno,
                                  fp_sysreg_loadfn *loadfn,
                                  void *opaque)
{
    TCGv_i32 tmp;
    TCGLabel *lab_end = NULL;

    switch (fp_sysreg_checks(s, regno)) {
    case FPSysRegCheckFailed:
        return false;
    case FPSysRegCheckDone:
        return true;
    case FPSysRegCheckContinue:
        break;
    }

    switch (regno) {
    case ARM_VFP_FPSCR:
        tmp = loadfn(s, opaque, true);
        gen_helper_vfp_set_fpscr(tcg_env, tmp);
        gen_lookup_tb(s);
        break;
    case ARM_VFP_FPSCR_NZCVQC:
    {
        TCGv_i32 fpscr;
        tmp = loadfn(s, opaque, true);
        if (dc_isar_feature(aa32_mve, s)) {
            TCGv_i32 qc = tcg_temp_new_i32();
            tcg_gen_andi_i32(qc, tmp, FPSR_QC);
            tcg_gen_gvec_dup_i32(MO_32, offsetof(CPUARMState, vfp.qc),
                                 16, 16, qc);
        }
        tcg_gen_andi_i32(tmp, tmp, FPSR_NZCV_MASK);
        fpscr = load_cpu_field_low32(vfp.fpsr);
        tcg_gen_andi_i32(fpscr, fpscr, ~FPSR_NZCV_MASK);
        tcg_gen_or_i32(fpscr, fpscr, tmp);
        store_cpu_field_low32(fpscr, vfp.fpsr);
        break;
    }
    case ARM_VFP_FPCXT_NS:
    {
        TCGLabel *lab_active = gen_new_label();

        lab_end = gen_new_label();
        gen_branch_fpInactive(s, TCG_COND_EQ, lab_active);
        loadfn(s, opaque, false);
        tcg_gen_br(lab_end);

        gen_set_label(lab_active);
        if (!vfp_access_check_m(s, true)) {
            s->base.is_jmp = DISAS_NEXT;
            break;
        }
    }
    /* fall through */
    case ARM_VFP_FPCXT_S:
    {
        TCGv_i32 sfpa, control;
        tmp = loadfn(s, opaque, true);
        sfpa = tcg_temp_new_i32();
        tcg_gen_shri_i32(sfpa, tmp, 31);
        control = load_cpu_field(v7m.control[M_REG_S]);
        tcg_gen_deposit_i32(control, control, sfpa,
                            R_V7M_CONTROL_SFPA_SHIFT, 1);
        store_cpu_field(control, v7m.control[M_REG_S]);
        tcg_gen_andi_i32(tmp, tmp, ~FPSR_NZCV_MASK);
        gen_helper_vfp_set_fpscr(tcg_env, tmp);
        s->base.is_jmp = DISAS_UPDATE_NOCHAIN;
        break;
    }
    case ARM_VFP_VPR:
        if (IS_USER(s)) {
            loadfn(s, opaque, false);
            break;
        }
        tmp = loadfn(s, opaque, true);
        store_cpu_field(tmp, v7m.vpr);
        s->base.is_jmp = DISAS_UPDATE_NOCHAIN;
        break;
    case ARM_VFP_P0:
    {
        TCGv_i32 vpr;
        tmp = loadfn(s, opaque, true);
        vpr = load_cpu_field(v7m.vpr);
        tcg_gen_deposit_i32(vpr, vpr, tmp,
                            R_V7M_VPR_P0_SHIFT, R_V7M_VPR_P0_LENGTH);
        store_cpu_field(vpr, v7m.vpr);
        s->base.is_jmp = DISAS_UPDATE_NOCHAIN;
        break;
    }
    default:
        g_assert_not_reached();
    }
    if (lab_end) {
        gen_set_label(lab_end);
    }
    return true;
}

static bool gen_M_fp_sysreg_read(DisasContext *s, int regno,
                                 fp_sysreg_storefn *storefn,
                                 void *opaque)
{
    TCGv_i32 tmp;
    TCGLabel *lab_end = NULL;
    bool lookup_tb = false;

    switch (fp_sysreg_checks(s, regno)) {
    case FPSysRegCheckFailed:
        return false;
    case FPSysRegCheckDone:
        return true;
    case FPSysRegCheckContinue:
        break;
    }

    if (regno == ARM_VFP_FPSCR_NZCVQC && !dc_isar_feature(aa32_mve, s)) {
        regno = QEMU_VFP_FPSCR_NZCV;
    }

    switch (regno) {
    case ARM_VFP_FPSCR:
        tmp = tcg_temp_new_i32();
        gen_helper_vfp_get_fpscr(tmp, tcg_env);
        storefn(s, opaque, tmp, true);
        break;
    case ARM_VFP_FPSCR_NZCVQC:
        tmp = tcg_temp_new_i32();
        gen_helper_vfp_get_fpscr(tmp, tcg_env);
        tcg_gen_andi_i32(tmp, tmp, FPSR_NZCVQC_MASK);
        storefn(s, opaque, tmp, true);
        break;
    case QEMU_VFP_FPSCR_NZCV:
        tmp = load_cpu_field_low32(vfp.fpsr);
        tcg_gen_andi_i32(tmp, tmp, FPSR_NZCV_MASK);
        storefn(s, opaque, tmp, true);
        break;
    case ARM_VFP_FPCXT_S:
    {
        TCGv_i32 control, sfpa, fpscr;
        tmp = tcg_temp_new_i32();
        sfpa = tcg_temp_new_i32();
        gen_helper_vfp_get_fpscr(tmp, tcg_env);
        tcg_gen_andi_i32(tmp, tmp, ~FPSR_NZCV_MASK);
        control = load_cpu_field(v7m.control[M_REG_S]);
        tcg_gen_andi_i32(sfpa, control, R_V7M_CONTROL_SFPA_MASK);
        tcg_gen_shli_i32(sfpa, sfpa, 31 - R_V7M_CONTROL_SFPA_SHIFT);
        tcg_gen_or_i32(tmp, tmp, sfpa);
        storefn(s, opaque, tmp, true);
        tcg_gen_andi_i32(control, control, ~R_V7M_CONTROL_SFPA_MASK);
        store_cpu_field(control, v7m.control[M_REG_S]);
        fpscr = load_cpu_field(v7m.fpdscr[M_REG_NS]);
        gen_helper_vfp_set_fpscr(tcg_env, fpscr);
        lookup_tb = true;
        break;
    }
    case ARM_VFP_FPCXT_NS:
    {
        TCGv_i32 control, sfpa, fpscr, fpdscr;
        TCGLabel *lab_active = gen_new_label();

        lookup_tb = true;

        gen_branch_fpInactive(s, TCG_COND_EQ, lab_active);
        tmp = load_cpu_field(v7m.fpdscr[M_REG_NS]);
        storefn(s, opaque, tmp, true);
        lab_end = gen_new_label();
        tcg_gen_br(lab_end);

        gen_set_label(lab_active);
        if (!vfp_access_check_m(s, true)) {
            s->base.is_jmp = DISAS_NEXT;
            break;
        }
        tmp = tcg_temp_new_i32();
        sfpa = tcg_temp_new_i32();
        fpscr = tcg_temp_new_i32();
        gen_helper_vfp_get_fpscr(fpscr, tcg_env);
        tcg_gen_andi_i32(tmp, fpscr, ~FPSR_NZCV_MASK);
        control = load_cpu_field(v7m.control[M_REG_S]);
        tcg_gen_andi_i32(sfpa, control, R_V7M_CONTROL_SFPA_MASK);
        tcg_gen_shli_i32(sfpa, sfpa, 31 - R_V7M_CONTROL_SFPA_SHIFT);
        tcg_gen_or_i32(tmp, tmp, sfpa);
        storefn(s, opaque, tmp, true);
        fpdscr = load_cpu_field(v7m.fpdscr[M_REG_NS]);
        tcg_gen_movcond_i32(TCG_COND_EQ, fpscr, sfpa, tcg_constant_i32(0),
                            fpdscr, fpscr);
        gen_helper_vfp_set_fpscr(tcg_env, fpscr);
        break;
    }
    case ARM_VFP_VPR:
        if (IS_USER(s)) {
            storefn(s, opaque, NULL, false);
            break;
        }
        tmp = load_cpu_field(v7m.vpr);
        storefn(s, opaque, tmp, true);
        break;
    case ARM_VFP_P0:
        tmp = load_cpu_field(v7m.vpr);
        tcg_gen_extract_i32(tmp, tmp, R_V7M_VPR_P0_SHIFT, R_V7M_VPR_P0_LENGTH);
        storefn(s, opaque, tmp, true);
        break;
    default:
        g_assert_not_reached();
    }

    if (lab_end) {
        gen_set_label(lab_end);
    }
    if (lookup_tb) {
        gen_lookup_tb(s);
    }
    return true;
}

static void fp_sysreg_to_gpr(DisasContext *s, void *opaque, TCGv_i32 value,
                             bool do_access)
{
    arg_VMSR_VMRS *a = opaque;

    if (!do_access) {
        return;
    }

    if (a->rt == 15) {
        gen_set_nzcv(value);
    } else {
        store_reg(s, a->rt, value);
    }
}

static TCGv_i32 gpr_to_fp_sysreg(DisasContext *s, void *opaque, bool do_access)
{
    arg_VMSR_VMRS *a = opaque;

    if (!do_access) {
        return NULL;
    }
    return load_reg(s, a->rt);
}

static bool trans_VMSR_VMRS(DisasContext *s, arg_VMSR_VMRS *a)
{
    if (a->rt == 15) {
        if (a->l && a->reg == ARM_VFP_FPSCR) {
            a->reg = QEMU_VFP_FPSCR_NZCV;
        } else {
            return false;
        }
    }

    if (a->l) {
        return gen_M_fp_sysreg_read(s, a->reg, fp_sysreg_to_gpr, a);
    } else {
        return gen_M_fp_sysreg_write(s, a->reg, gpr_to_fp_sysreg, a);
    }
}

static void fp_sysreg_to_memory(DisasContext *s, void *opaque, TCGv_i32 value,
                                bool do_access)
{
    arg_vldr_sysreg *a = opaque;
    uint32_t offset = a->imm;
    TCGv_i32 addr;

    if (!a->a) {
        offset = -offset;
    }

    if (!do_access && !a->w) {
        return;
    }

    addr = load_reg(s, a->rn);
    if (a->p) {
        tcg_gen_addi_i32(addr, addr, offset);
    }

    if (s->v8m_stackcheck && a->rn == 13 && a->w) {
        gen_helper_v8m_stackcheck(tcg_env, addr);
    }

    if (do_access) {
        gen_aa32_st_i32(s, value, addr, get_mem_index(s),
                        MO_UL | MO_ALIGN | s->be_data);
    }

    if (a->w) {
        if (!a->p) {
            tcg_gen_addi_i32(addr, addr, offset);
        }
        store_reg(s, a->rn, addr);
    }
}

static TCGv_i32 memory_to_fp_sysreg(DisasContext *s, void *opaque,
                                    bool do_access)
{
    arg_vldr_sysreg *a = opaque;
    uint32_t offset = a->imm;
    TCGv_i32 addr;
    TCGv_i32 value = NULL;

    if (!a->a) {
        offset = -offset;
    }

    if (!do_access && !a->w) {
        return NULL;
    }

    addr = load_reg(s, a->rn);
    if (a->p) {
        tcg_gen_addi_i32(addr, addr, offset);
    }

    if (s->v8m_stackcheck && a->rn == 13 && a->w) {
        gen_helper_v8m_stackcheck(tcg_env, addr);
    }

    if (do_access) {
        value = tcg_temp_new_i32();
        gen_aa32_ld_i32(s, value, addr, get_mem_index(s),
                        MO_UL | MO_ALIGN | s->be_data);
    }

    if (a->w) {
        if (!a->p) {
            tcg_gen_addi_i32(addr, addr, offset);
        }
        store_reg(s, a->rn, addr);
    }
    return value;
}

static bool trans_VLDR_sysreg(DisasContext *s, arg_vldr_sysreg *a)
{
    if (!arm_dc_feature(s, ARM_FEATURE_V8_1M)) {
        return false;
    }
    if (a->rn == 15) {
        return false;
    }
    return gen_M_fp_sysreg_write(s, a->reg, memory_to_fp_sysreg, a);
}

static bool trans_VSTR_sysreg(DisasContext *s, arg_vldr_sysreg *a)
{
    if (!arm_dc_feature(s, ARM_FEATURE_V8_1M)) {
        return false;
    }
    if (a->rn == 15) {
        return false;
    }
    return gen_M_fp_sysreg_read(s, a->reg, fp_sysreg_to_memory, a);
}

static bool trans_NOCP(DisasContext *s, arg_nocp *a)
{
    assert(arm_dc_feature(s, ARM_FEATURE_M));

    if (a->cp == 11) {
        a->cp = 10;
    }
    if (arm_dc_feature(s, ARM_FEATURE_V8_1M) &&
        (a->cp == 8 || a->cp == 9 || a->cp == 14 || a->cp == 15)) {
        a->cp = 10;
    }

    if (a->cp != 10) {
        gen_exception_insn(s, 0, EXCP_NOCP, syn_uncategorized());
        return true;
    }

    if (s->fp_excp_el != 0) {
        gen_exception_insn_el(s, 0, EXCP_NOCP,
                              syn_uncategorized(), s->fp_excp_el);
        return true;
    }

    return false;
}

static bool trans_NOCP_8_1(DisasContext *s, arg_nocp *a)
{
    if (!arm_dc_feature(s, ARM_FEATURE_V8_1M)) {
        return false;
    }
    return trans_NOCP(s, a);
}
