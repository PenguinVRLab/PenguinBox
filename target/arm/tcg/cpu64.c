/*
 * QEMU AArch64 TCG CPUs
 *
 * Copyright (c) 2013 Linaro Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "qemu/module.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"
#include "qemu/units.h"
#include "internals.h"
#include "cpu-features.h"
#include "cpregs.h"

static void aarch64_a35_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

    cpu->dtb_compatible = "arm,cortex-a35";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_BACKCOMPAT_CNTFRQ);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);

    cpu->midr = 0x411fd040;
    cpu->revidr = 0;
    cpu->ctr = 0x84448004;
    SET_IDREG(isar, ID_PFR0, 0x00000131);
    SET_IDREG(isar, ID_PFR1, 0x00011011);
    SET_IDREG(isar, ID_DFR0, 0x03010066);
    SET_IDREG(isar, ID_AFR0, 0);
    SET_IDREG(isar, ID_MMFR0, 0x10201105);
    SET_IDREG(isar, ID_MMFR1, 0x40000000);
    SET_IDREG(isar, ID_MMFR2, 0x01260000);
    SET_IDREG(isar, ID_MMFR3, 0x02102211);
    SET_IDREG(isar, ID_ISAR0, 0x02101110);
    SET_IDREG(isar, ID_ISAR1, 0x13112111);
    SET_IDREG(isar, ID_ISAR2, 0x21232042);
    SET_IDREG(isar, ID_ISAR3, 0x01112131);
    SET_IDREG(isar, ID_ISAR4, 0x00011142);
    SET_IDREG(isar, ID_ISAR5, 0x00011121);
    SET_IDREG(isar, ID_AA64PFR0, 0x00002222);
    SET_IDREG(isar, ID_AA64PFR1, 0);
    SET_IDREG(isar, ID_AA64DFR0, 0x10305106);
    SET_IDREG(isar, ID_AA64DFR1, 0);
    SET_IDREG(isar, ID_AA64ISAR0, 0x00011120);
    SET_IDREG(isar, ID_AA64ISAR1, 0);
    SET_IDREG(isar, ID_AA64MMFR0, 0x00101122);
    SET_IDREG(isar, ID_AA64MMFR1, 0);
    SET_IDREG(isar, CLIDR, 0x0a200023);
    cpu->dcz_blocksize = 4;

    cpu->reset_sctlr = 0x00c50838;

    cpu->isar.reset_pmcr_el0 = 0x410a3000;

    cpu->ccsidr[0] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 4, 64, 32 * KiB, 7);
    cpu->ccsidr[1] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 4, 64, 32 * KiB, 2);
    cpu->ccsidr[2] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 16, 64, 512 * KiB, 7);

    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    cpu->gic_pribits = 5;

    cpu->isar.dbgdidr = 0x3516d000;
    cpu->isar.dbgdevid = 0x00110f13;
    cpu->isar.dbgdevid1 = 0x2;

    cpu->reset_fpsid = 0x41034043;

    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x12111111;
    cpu->isar.mvfr2 = 0x00000043;

    define_cortex_a72_a57_a53_cp_reginfo(cpu);
}

static void cpu_max_get_sve_max_vq(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    uint32_t value;

    if (!cpu_isar_feature(aa64_sve, cpu)) {
        value = 0;
    } else {
        value = cpu->sve_max_vq;
    }
    visit_type_uint32(v, name, &value, errp);
}

static void cpu_max_set_sve_max_vq(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    uint32_t max_vq;

    if (!visit_type_uint32(v, name, &max_vq, errp)) {
        return;
    }

    if (max_vq == 0 || max_vq > ARM_MAX_VQ) {
        error_setg(errp, "unsupported SVE vector length");
        error_append_hint(errp, "Valid sve-max-vq in range [1-%d]\n",
                          ARM_MAX_VQ);
        return;
    }

    cpu->sve_max_vq = max_vq;
}

static bool cpu_arm_get_rme(Object *obj, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    return cpu_isar_feature(aa64_rme, cpu);
}

static void cpu_arm_set_rme(Object *obj, bool value, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);

    FIELD_DP64_IDREG(&cpu->isar, ID_AA64PFR0, RME, value ? 2 : 0);
}

static void cpu_max_set_l0gptsz(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    uint32_t value;

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }

    switch (value) {
    case 30:
    case 34:
    case 36:
    case 39:
        cpu->reset_l0gptsz = value - 30;
        break;
    default:
        error_setg(errp, "invalid value for l0gptsz");
        error_append_hint(errp, "valid values are 30, 34, 36, 39\n");
        break;
    }
}

static void cpu_max_get_l0gptsz(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    uint32_t value = cpu->reset_l0gptsz + 30;

    visit_type_uint32(v, name, &value, errp);
}

static const Property arm_cpu_lpa2_property =
    DEFINE_PROP_BOOL("lpa2", ARMCPU, prop_lpa2, true);

static void aarch64_a55_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

    cpu->dtb_compatible = "arm,cortex-a55";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_BACKCOMPAT_CNTFRQ);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);

    SET_IDREG(isar, CLIDR, 0x82000023);
    cpu->ctr = 0x84448004;
    cpu->dcz_blocksize = 4;
    SET_IDREG(isar, ID_AA64DFR0, 0x0000000010305408ull);
    SET_IDREG(isar, ID_AA64ISAR0, 0x0000100010211120ull);
    SET_IDREG(isar, ID_AA64ISAR1, 0x0000000000100001ull);
    SET_IDREG(isar, ID_AA64MMFR0, 0x0000000000101122ull);
    SET_IDREG(isar, ID_AA64MMFR1, 0x0000000010212122ull);
    SET_IDREG(isar, ID_AA64MMFR2, 0x0000000000001011ull);
    SET_IDREG(isar, ID_AA64PFR0, 0x0000000010112222ull);
    SET_IDREG(isar, ID_AA64PFR1, 0x0000000000000010ull);
    SET_IDREG(isar, ID_AFR0, 0x00000000);
    SET_IDREG(isar, ID_DFR0, 0x04010088);
    SET_IDREG(isar, ID_ISAR0, 0x02101110);
    SET_IDREG(isar, ID_ISAR1, 0x13112111);
    SET_IDREG(isar, ID_ISAR2, 0x21232042);
    SET_IDREG(isar, ID_ISAR3, 0x01112131);
    SET_IDREG(isar, ID_ISAR4, 0x00011142);
    SET_IDREG(isar, ID_ISAR5, 0x01011121);
    SET_IDREG(isar, ID_ISAR6, 0x00000010);
    SET_IDREG(isar, ID_MMFR0, 0x10201105);
    SET_IDREG(isar, ID_MMFR1, 0x40000000);
    SET_IDREG(isar, ID_MMFR2, 0x01260000);
    SET_IDREG(isar, ID_MMFR3, 0x02122211);
    SET_IDREG(isar, ID_MMFR4, 0x00021110);
    SET_IDREG(isar, ID_PFR0, 0x10010131);
    SET_IDREG(isar, ID_PFR1, 0x00011011);
    SET_IDREG(isar, ID_PFR2, 0x00000011);
    cpu->midr = 0x412FD050;
    cpu->revidr = 0;

    cpu->ccsidr[0] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 4, 64, 32 * KiB, 7);
    cpu->ccsidr[1] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 4, 64, 32 * KiB, 2);
    cpu->ccsidr[2] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 16, 64, 512 * KiB, 7);

    cpu->reset_sctlr = 0x30c50838;

    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    cpu->gic_pribits = 5;

    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x13211111;
    cpu->isar.mvfr2 = 0x00000043;

    cpu->isar.reset_pmcr_el0 = 0x410b3000;
}

static void aarch64_a72_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

    cpu->dtb_compatible = "arm,cortex-a72";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_BACKCOMPAT_CNTFRQ);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);
    cpu->midr = 0x410fd083;
    cpu->revidr = 0x00000000;
    cpu->reset_fpsid = 0x41034080;
    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x12111111;
    cpu->isar.mvfr2 = 0x00000043;
    cpu->ctr = 0x8444c004;
    cpu->reset_sctlr = 0x00c50838;
    SET_IDREG(isar, ID_PFR0, 0x00000131);
    SET_IDREG(isar, ID_PFR1, 0x00011011);
    SET_IDREG(isar, ID_DFR0, 0x03010066);
    SET_IDREG(isar, ID_AFR0, 0x00000000);
    SET_IDREG(isar, ID_MMFR0, 0x10201105);
    SET_IDREG(isar, ID_MMFR1, 0x40000000);
    SET_IDREG(isar, ID_MMFR2, 0x01260000);
    SET_IDREG(isar, ID_MMFR3, 0x02102211);
    SET_IDREG(isar, ID_ISAR0, 0x02101110);
    SET_IDREG(isar, ID_ISAR1, 0x13112111);
    SET_IDREG(isar, ID_ISAR2, 0x21232042);
    SET_IDREG(isar, ID_ISAR3, 0x01112131);
    SET_IDREG(isar, ID_ISAR4, 0x00011142);
    SET_IDREG(isar, ID_ISAR5, 0x00011121);
    SET_IDREG(isar, ID_AA64PFR0, 0x00002222);
    SET_IDREG(isar, ID_AA64DFR0, 0x10305106);
    SET_IDREG(isar, ID_AA64ISAR0, 0x00011120);
    SET_IDREG(isar, ID_AA64MMFR0, 0x00001124);
    cpu->isar.dbgdidr = 0x3516d000;
    cpu->isar.dbgdevid = 0x01110f13;
    cpu->isar.dbgdevid1 = 0x2;
    cpu->isar.reset_pmcr_el0 = 0x41023000;
    SET_IDREG(isar, CLIDR, 0x0a200023);
    cpu->ccsidr[0] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 4, 64, 32 * KiB, 7);
    cpu->ccsidr[1] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 3, 64, 48 * KiB, 2);
    cpu->ccsidr[2] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 16, 64, 1 * MiB, 7);
    cpu->dcz_blocksize = 4;
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    cpu->gic_pribits = 5;
    define_cortex_a72_a57_a53_cp_reginfo(cpu);
}

static void aarch64_a76_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

    cpu->dtb_compatible = "arm,cortex-a76";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_BACKCOMPAT_CNTFRQ);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);

    SET_IDREG(isar, CLIDR, 0x82000023);
    cpu->ctr = 0x8444C004;
    cpu->dcz_blocksize = 4;
    SET_IDREG(isar, ID_AA64DFR0, 0x0000000010305408ull);
    SET_IDREG(isar, ID_AA64ISAR0, 0x0000100010211120ull);
    SET_IDREG(isar, ID_AA64ISAR1, 0x0000000000100001ull);
    SET_IDREG(isar, ID_AA64MMFR0, 0x0000000000101122ull);
    SET_IDREG(isar, ID_AA64MMFR1, 0x0000000010212122ull);
    SET_IDREG(isar, ID_AA64MMFR2, 0x0000000000001011ull);
    SET_IDREG(isar, ID_AA64PFR0, 0x1100000010111112ull);
    SET_IDREG(isar, ID_AA64PFR1, 0x0000000000000010ull);
    SET_IDREG(isar, ID_AFR0, 0x00000000);
    SET_IDREG(isar, ID_DFR0, 0x04010088);
    SET_IDREG(isar, ID_ISAR0, 0x02101110);
    SET_IDREG(isar, ID_ISAR1, 0x13112111);
    SET_IDREG(isar, ID_ISAR2, 0x21232042);
    SET_IDREG(isar, ID_ISAR3, 0x01112131);
    SET_IDREG(isar, ID_ISAR4, 0x00010142);
    SET_IDREG(isar, ID_ISAR5, 0x01011121);
    SET_IDREG(isar, ID_ISAR6, 0x00000010);
    SET_IDREG(isar, ID_MMFR0, 0x10201105);
    SET_IDREG(isar, ID_MMFR1, 0x40000000);
    SET_IDREG(isar, ID_MMFR2, 0x01260000);
    SET_IDREG(isar, ID_MMFR3, 0x02122211);
    SET_IDREG(isar, ID_MMFR4, 0x00021110);
    SET_IDREG(isar, ID_PFR0, 0x10010131);
    SET_IDREG(isar, ID_PFR1, 0x00010000);
    SET_IDREG(isar, ID_PFR2, 0x00000011);
    cpu->midr = 0x414fd0b1;
    cpu->revidr = 0;

    cpu->ccsidr[0] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 4, 64, 64 * KiB, 7);
    cpu->ccsidr[1] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 4, 64, 64 * KiB, 2);
    cpu->ccsidr[2] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 8, 64, 512 * KiB, 7);

    cpu->reset_sctlr = 0x30c50838;

    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    cpu->gic_pribits = 5;

    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x13211111;
    cpu->isar.mvfr2 = 0x00000043;

    cpu->isar.reset_pmcr_el0 = 0x410b3000;
}

static void aarch64_a78ae_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

    cpu->dtb_compatible = "arm,cortex-a78ae";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);

    SET_IDREG(isar, CLIDR, 0x82000023);
    cpu->ctr = 0x9444c004;
    cpu->dcz_blocksize = 4;
    SET_IDREG(isar, ID_AA64DFR0, 0x0000000110305408ull);
    SET_IDREG(isar, ID_AA64ISAR0, 0x0010100010211120ull);
    SET_IDREG(isar, ID_AA64ISAR1, 0x0000000001200031ull);
    SET_IDREG(isar, ID_AA64MMFR0, 0x0000000000101125ull);
    SET_IDREG(isar, ID_AA64MMFR1, 0x0000000010212122ull);
    SET_IDREG(isar, ID_AA64MMFR2, 0x0000000100001011ull);
    SET_IDREG(isar, ID_AA64PFR0, 0x1100000010111112ull);
    SET_IDREG(isar, ID_AA64PFR1, 0x0000000000000010ull);
    SET_IDREG(isar, ID_AFR0, 0x00000000);
    SET_IDREG(isar, ID_DFR0, 0x04010088);
    SET_IDREG(isar, ID_ISAR0, 0x02101110);
    SET_IDREG(isar, ID_ISAR1, 0x13112111);
    SET_IDREG(isar, ID_ISAR2, 0x21232042);
    SET_IDREG(isar, ID_ISAR3, 0x01112131);
    SET_IDREG(isar, ID_ISAR4, 0x00010142);
    SET_IDREG(isar, ID_ISAR5, 0x01011121);
    SET_IDREG(isar, ID_ISAR6, 0x00000010);
    SET_IDREG(isar, ID_MMFR0, 0x10201105);
    SET_IDREG(isar, ID_MMFR1, 0x40000000);
    SET_IDREG(isar, ID_MMFR2, 0x01260000);
    SET_IDREG(isar, ID_MMFR3, 0x02122211);
    SET_IDREG(isar, ID_MMFR4, 0x00021110);
    SET_IDREG(isar, ID_PFR0, 0x10010131);
    SET_IDREG(isar, ID_PFR1, 0x00010000);
    SET_IDREG(isar, ID_PFR2, 0x00000011);
    cpu->midr = 0x410fd423;
    cpu->revidr = 0;

    cpu->ccsidr[0] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 4, 64, 64 * KiB, 7);
    cpu->ccsidr[1] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 4, 64, 64 * KiB, 2);
    cpu->ccsidr[2] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 8, 64, 512 * KiB, 7);

    cpu->reset_sctlr = 0x30c50838;

    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    cpu->gic_pribits = 5;

    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x13211111;
    cpu->isar.mvfr2 = 0x00000043;

    cpu->isar.reset_pmcr_el0 = 0x41223000;
}

static void aarch64_a64fx_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

    cpu->dtb_compatible = "arm,a64fx";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_BACKCOMPAT_CNTFRQ);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);
    cpu->midr = 0x461f0010;
    cpu->revidr = 0x00000000;
    cpu->ctr = 0x86668006;
    cpu->reset_sctlr = 0x30000180;
    SET_IDREG(isar, ID_AA64PFR0, 0x0000000101111111);
    SET_IDREG(isar, ID_AA64PFR1, 0x0000000000000000);
    SET_IDREG(isar, ID_AA64DFR0, 0x0000000010305408);
    SET_IDREG(isar, ID_AA64DFR1, 0x0000000000000000);
    SET_IDREG(isar, ID_AA64AFR0, 0x0000000000000000);
    SET_IDREG(isar, ID_AA64AFR1, 0x0000000000000000);
    SET_IDREG(isar, ID_AA64MMFR0, 0x0000000000001122);
    SET_IDREG(isar, ID_AA64MMFR1, 0x0000000011212100);
    SET_IDREG(isar, ID_AA64MMFR2, 0x0000000000001011);
    SET_IDREG(isar, ID_AA64ISAR0, 0x0000000010211120);
    SET_IDREG(isar, ID_AA64ISAR1, 0x0000000000010001);
    SET_IDREG(isar, ID_AA64ZFR0, 0x0000000000000000);
    SET_IDREG(isar, CLIDR, 0x0000000080000023);
    cpu->ccsidr[0] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 4, 256, 64 * KiB, 7);
    cpu->ccsidr[1] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 4, 256, 64 * KiB, 2);
    cpu->ccsidr[2] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 16, 256, 8 * MiB, 7);
    cpu->dcz_blocksize = 6;
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    cpu->gic_pribits = 5;

    aarch64_add_sve_properties(obj);
    cpu->sve_vq.supported = (1 << 0)
                          | (1 << 1)
                          | (1 << 3);

    cpu->isar.reset_pmcr_el0 = 0x46014040;

}

static CPAccessResult access_actlr_w(CPUARMState *env, const ARMCPRegInfo *r,
                                     bool read)
{
    if (!read) {
        int el = arm_current_el(env);

        if (el < 2 && arm_is_el2_enabled(env)) {
            return CP_ACCESS_TRAP_EL2;
        }
        if (el < 3 && arm_feature(env, ARM_FEATURE_EL3)) {
            return CP_ACCESS_TRAP_EL3;
        }
    }
    return CP_ACCESS_OK;
}

static const ARMCPRegInfo neoverse_n1_cp_reginfo[] = {
    { .name = "ATCR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 7, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_tvm_trvm, .fgt = FGT_TCR_EL1, },
    { .name = "ATCR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 15, .crm = 7, .opc2 = 0,
      .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "ATCR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 7, .opc2 = 0,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "ATCR_EL12", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 5, .crn = 15, .crm = 7, .opc2 = 0,
      .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "AVTCR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 15, .crm = 7, .opc2 = 1,
      .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUACTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 1, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "CPUACTLR2_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 1, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "CPUACTLR3_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 1, .opc2 = 2,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "CPUCFR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 0, .opc2 = 0,
      .access = PL1_R, .type = ARM_CP_CONST, .resetvalue = 4 },
    { .name = "CPUECTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 1, .opc2 = 4,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0x961563010,
      .accessfn = access_actlr_w },
    { .name = "CPUPCR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 8, .opc2 = 1,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPMR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 8, .opc2 = 3,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPOR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 8, .opc2 = 2,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPSELR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 8, .opc2 = 0,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPWRCTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 2, .opc2 = 7,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "ERXPFGCDN_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 2, .opc2 = 2,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "ERXPFGCTL_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 2, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "ERXPFGF_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 2, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
};

static void define_neoverse_n1_cp_reginfo(ARMCPU *cpu)
{
    define_arm_cp_regs(cpu, neoverse_n1_cp_reginfo);
}

static const ARMCPRegInfo neoverse_v1_cp_reginfo[] = {
    { .name = "CPUECTLR2_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 1, .opc2 = 5,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "CPUPPMCR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 2, .opc2 = 0,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPPMCR2_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 2, .opc2 = 1,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPPMCR3_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 2, .opc2 = 6,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
};

static void define_neoverse_v1_cp_reginfo(ARMCPU *cpu)
{
    define_arm_cp_regs(cpu, neoverse_n1_cp_reginfo);
    define_arm_cp_regs(cpu, neoverse_v1_cp_reginfo);
}

static void aarch64_neoverse_n1_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

    cpu->dtb_compatible = "arm,neoverse-n1";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_BACKCOMPAT_CNTFRQ);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);

    SET_IDREG(isar, CLIDR, 0x82000023);
    cpu->ctr = 0x8444c004;
    cpu->dcz_blocksize = 4;
    SET_IDREG(isar, ID_AA64DFR0, 0x0000000110305408ull);
    SET_IDREG(isar, ID_AA64ISAR0, 0x0000100010211120ull);
    SET_IDREG(isar, ID_AA64ISAR1, 0x0000000000100001ull);
    SET_IDREG(isar, ID_AA64MMFR0, 0x0000000000101125ull);
    SET_IDREG(isar, ID_AA64MMFR1, 0x0000000010212122ull);
    SET_IDREG(isar, ID_AA64MMFR2, 0x0000000000001011ull);
    SET_IDREG(isar, ID_AA64PFR0, 0x1100000010111112ull);
    SET_IDREG(isar, ID_AA64PFR1, 0x0000000000000020ull);
    SET_IDREG(isar, ID_AFR0, 0x00000000);
    SET_IDREG(isar, ID_DFR0, 0x04010088);
    SET_IDREG(isar, ID_ISAR0, 0x02101110);
    SET_IDREG(isar, ID_ISAR1, 0x13112111);
    SET_IDREG(isar, ID_ISAR2, 0x21232042);
    SET_IDREG(isar, ID_ISAR3, 0x01112131);
    SET_IDREG(isar, ID_ISAR4, 0x00010142);
    SET_IDREG(isar, ID_ISAR5, 0x01011121);
    SET_IDREG(isar, ID_ISAR6, 0x00000010);
    SET_IDREG(isar, ID_MMFR0, 0x10201105);
    SET_IDREG(isar, ID_MMFR1, 0x40000000);
    SET_IDREG(isar, ID_MMFR2, 0x01260000);
    SET_IDREG(isar, ID_MMFR3, 0x02122211);
    SET_IDREG(isar, ID_MMFR4, 0x00021110);
    SET_IDREG(isar, ID_PFR0, 0x10010131);
    SET_IDREG(isar, ID_PFR1, 0x00010000);
    SET_IDREG(isar, ID_PFR2, 0x00000011);
    cpu->midr = 0x414fd0c1;
    cpu->revidr = 0;

    cpu->ccsidr[0] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 4, 64, 64 * KiB, 7);
    cpu->ccsidr[1] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 4, 64, 64 * KiB, 2);
    cpu->ccsidr[2] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 8, 64, 1 * MiB, 7);

    cpu->reset_sctlr = 0x30c50838;

    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    cpu->gic_pribits = 5;

    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x13211111;
    cpu->isar.mvfr2 = 0x00000043;

    cpu->isar.reset_pmcr_el0 = 0x410c3000;

    define_neoverse_n1_cp_reginfo(cpu);
}

static void aarch64_neoverse_v1_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

    cpu->dtb_compatible = "arm,neoverse-v1";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_BACKCOMPAT_CNTFRQ);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);

    SET_IDREG(isar, CLIDR, 0x82000023);
    cpu->ctr = 0xb444c004;
    cpu->dcz_blocksize = 4;
    SET_IDREG(isar, ID_AA64AFR0, 0x00000000);
    SET_IDREG(isar, ID_AA64AFR1, 0x00000000);
    SET_IDREG(isar, ID_AA64DFR0, 0x000001f210305519ull);
    SET_IDREG(isar, ID_AA64DFR1, 0x00000000);
    SET_IDREG(isar, ID_AA64ISAR0, 0x1011111110212120ull);
    SET_IDREG(isar, ID_AA64ISAR1, 0x0011000001211032ull);
    SET_IDREG(isar, ID_AA64MMFR0, 0x0000000000101125ull);
    SET_IDREG(isar, ID_AA64MMFR1, 0x0000000010212122ull);
    SET_IDREG(isar, ID_AA64MMFR2, 0x0220011102101011ull);
    SET_IDREG(isar, ID_AA64PFR0, 0x1101110120111112ull);
    SET_IDREG(isar, ID_AA64PFR1, 0x0000000000000020ull);
    SET_IDREG(isar, ID_AFR0, 0x00000000);
    SET_IDREG(isar, ID_DFR0, 0x15011099);
    SET_IDREG(isar, ID_ISAR0, 0x02101110);
    SET_IDREG(isar, ID_ISAR1, 0x13112111);
    SET_IDREG(isar, ID_ISAR2, 0x21232042);
    SET_IDREG(isar, ID_ISAR3, 0x01112131);
    SET_IDREG(isar, ID_ISAR4, 0x00010142);
    SET_IDREG(isar, ID_ISAR5, 0x11011121);
    SET_IDREG(isar, ID_ISAR6, 0x01100111);
    SET_IDREG(isar, ID_MMFR0, 0x10201105);
    SET_IDREG(isar, ID_MMFR1, 0x40000000);
    SET_IDREG(isar, ID_MMFR2, 0x01260000);
    SET_IDREG(isar, ID_MMFR3, 0x02122211);
    SET_IDREG(isar, ID_MMFR4, 0x01021110);
    SET_IDREG(isar, ID_PFR0, 0x21110131);
    SET_IDREG(isar, ID_PFR1, 0x00010000);
    SET_IDREG(isar, ID_PFR2, 0x00000011);
    cpu->midr = 0x411FD402;
    cpu->revidr = 0;

    cpu->ccsidr[0] = make_ccsidr(CCSIDR_FORMAT_CCIDX, 4, 64, 64 * KiB, 0);
    cpu->ccsidr[1] = cpu->ccsidr[0];
    cpu->ccsidr[2] = make_ccsidr(CCSIDR_FORMAT_CCIDX, 8, 64, 1 * MiB, 0);

    cpu->reset_sctlr = 0x30c50838;

    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    cpu->gic_pribits = 5;

    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x13211111;
    cpu->isar.mvfr2 = 0x00000043;

    SET_IDREG(isar, ID_AA64ZFR0, 0x0000100000100000);
    cpu->sve_vq.supported = (1 << 0)
                            | (1 << 1);

    cpu->isar.reset_pmcr_el0 = 0x41213000;

    define_neoverse_v1_cp_reginfo(cpu);

    aarch64_add_pauth_properties(obj);
    aarch64_add_sve_properties(obj);
}

static const ARMCPRegInfo cortex_a710_cp_reginfo[] = {
    { .name = "CPUACTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 1, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "CPUACTLR2_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 1, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "CPUACTLR3_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 1, .opc2 = 2,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "CPUACTLR4_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 1, .opc2 = 3,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "CPUECTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 1, .opc2 = 4,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "CPUECTLR2_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 1, .opc2 = 5,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "CPUPPMCR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 2, .opc2 = 4,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPWRCTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 2, .opc2 = 7,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "ATCR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 7, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUACTLR5_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 8, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "CPUACTLR6_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 8, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "CPUACTLR7_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 8, .opc2 = 2,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "ATCR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 15, .crm = 7, .opc2 = 0,
      .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "AVTCR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 15, .crm = 7, .opc2 = 1,
      .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPPMCR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 2, .opc2 = 0,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPPMCR2_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 2, .opc2 = 1,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPPMCR4_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 2, .opc2 = 4,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPPMCR5_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 2, .opc2 = 5,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPPMCR6_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 2, .opc2 = 6,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUACTLR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 4, .opc2 = 0,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "ATCR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 7, .opc2 = 0,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPSELR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 8, .opc2 = 0,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPCR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 8, .opc2 = 1,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPOR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 8, .opc2 = 2,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPMR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 8, .opc2 = 3,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPOR2_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 8, .opc2 = 4,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPMR2_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 8, .opc2 = 5,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPFR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 8, .opc2 = 6,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUCFR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 0, .opc2 = 0,
      .access = PL1_R, .type = ARM_CP_CONST, .resetvalue = 4 },

    { .name = "RAMINDEX_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 15, .crm = 0, .opc2 = 0,
      .access = PL3_W, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IDATA0_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 0, .opc2 = 0,
      .access = PL3_R, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IDATA1_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 0, .opc2 = 1,
      .access = PL3_R, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IDATA2_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 0, .opc2 = 2,
      .access = PL3_R, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "DDATA0_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 1, .opc2 = 0,
      .access = PL3_R, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "DDATA1_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 1, .opc2 = 1,
      .access = PL3_R, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "DDATA2_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 1, .opc2 = 2,
      .access = PL3_R, .type = ARM_CP_CONST, .resetvalue = 0 },
};

static void aarch64_a710_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

    cpu->dtb_compatible = "arm,cortex-a710";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_BACKCOMPAT_CNTFRQ);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);

    cpu->midr          = 0x412FD471;
    cpu->revidr        = 0;
    SET_IDREG(isar, ID_PFR0, 0x21110131);
    SET_IDREG(isar, ID_PFR1, 0x00010000);
    SET_IDREG(isar, ID_DFR0, 0x16011099);
    SET_IDREG(isar, ID_AFR0, 0);
    SET_IDREG(isar, ID_MMFR0, 0x10201105);
    SET_IDREG(isar, ID_MMFR1, 0x40000000);
    SET_IDREG(isar, ID_MMFR2, 0x01260000);
    SET_IDREG(isar, ID_MMFR3, 0x02122211);
    SET_IDREG(isar, ID_ISAR0, 0x02101110);
    SET_IDREG(isar, ID_ISAR1, 0x13112111);
    SET_IDREG(isar, ID_ISAR2, 0x21232042);
    SET_IDREG(isar, ID_ISAR3, 0x01112131);
    SET_IDREG(isar, ID_ISAR4, 0x00010142);
    SET_IDREG(isar, ID_ISAR5, 0x11011121);
    SET_IDREG(isar, ID_MMFR4, 0x21021110);
    SET_IDREG(isar, ID_ISAR6, 0x01111111);
    cpu->isar.mvfr0    = 0x10110222;
    cpu->isar.mvfr1    = 0x13211111;
    cpu->isar.mvfr2    = 0x00000043;
    SET_IDREG(isar, ID_PFR2, 0x00000011);
    SET_IDREG(isar, ID_AA64PFR0, 0x1201111120111112ull);
    SET_IDREG(isar, ID_AA64PFR1, 0x0000000000000221ull);
    SET_IDREG(isar, ID_AA64ZFR0, 0x0000110100110021ull);
    SET_IDREG(isar, ID_AA64DFR0, 0x000011f010305619ull);
    SET_IDREG(isar, ID_AA64DFR1, 0);
    SET_IDREG(isar, ID_AA64AFR0, 0);
    SET_IDREG(isar, ID_AA64AFR1, 0);
    SET_IDREG(isar, ID_AA64ISAR0, 0x0221111110212120ull);
    SET_IDREG(isar, ID_AA64ISAR1, 0x0010111101211052ull);
    SET_IDREG(isar, ID_AA64MMFR0, 0x0000022200101122ull);
    SET_IDREG(isar, ID_AA64MMFR1, 0x0000000010212122ull);
    SET_IDREG(isar, ID_AA64MMFR2, 0x1221011110101011ull);
    SET_IDREG(isar, CLIDR, 0x0000001482000023ull);
    cpu->gm_blocksize      = 4;
    cpu->ctr               = 0x000000049444c004ull;
    cpu->dcz_blocksize     = 4;

    cpu->isar.reset_pmcr_el0 = 0xa000;

    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    cpu->gic_pribits = 5;

    cpu->sve_vq.supported = 1 << 0;

    cpu->ccsidr[0] = make_ccsidr(CCSIDR_FORMAT_CCIDX, 4, 64, 64 * KiB, 0);
    cpu->ccsidr[1] = cpu->ccsidr[0];
    cpu->ccsidr[2] = make_ccsidr(CCSIDR_FORMAT_CCIDX, 8, 64, 512 * KiB, 0);

    /* FIXME: Not documented -- copied from neoverse-v1 */
    cpu->reset_sctlr = 0x30c50838;

    define_arm_cp_regs(cpu, cortex_a710_cp_reginfo);

    aarch64_add_pauth_properties(obj);
    aarch64_add_sve_properties(obj);
}

static const ARMCPRegInfo neoverse_n2_cp_reginfo[] = {
    { .name = "CPURNDBR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 3, .opc2 = 0,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPURNDPEID_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 3, .opc2 = 1,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
};

static void aarch64_neoverse_n2_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

    cpu->dtb_compatible = "arm,neoverse-n2";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_BACKCOMPAT_CNTFRQ);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);

    cpu->midr          = 0x410FD493;
    cpu->revidr        = 0;
    SET_IDREG(isar, ID_PFR0, 0x21110131);
    SET_IDREG(isar, ID_PFR1, 0x00010000);
    SET_IDREG(isar, ID_DFR0, 0x16011099);
    SET_IDREG(isar, ID_AFR0, 0);
    SET_IDREG(isar, ID_MMFR0, 0x10201105);
    SET_IDREG(isar, ID_MMFR1, 0x40000000);
    SET_IDREG(isar, ID_MMFR2, 0x01260000);
    SET_IDREG(isar, ID_MMFR3, 0x02122211);
    SET_IDREG(isar, ID_ISAR0, 0x02101110);
    SET_IDREG(isar, ID_ISAR1, 0x13112111);
    SET_IDREG(isar, ID_ISAR2, 0x21232042);
    SET_IDREG(isar, ID_ISAR3, 0x01112131);
    SET_IDREG(isar, ID_ISAR4, 0x00010142);
    SET_IDREG(isar, ID_ISAR5, 0x11011121);
    SET_IDREG(isar, ID_MMFR4, 0x01021110);
    SET_IDREG(isar, ID_ISAR6, 0x01111111);
    cpu->isar.mvfr0    = 0x10110222;
    cpu->isar.mvfr1    = 0x13211111;
    cpu->isar.mvfr2    = 0x00000043;
    SET_IDREG(isar, ID_PFR2, 0x00000011);
    SET_IDREG(isar, ID_AA64PFR0, 0x1201111120111112ull);
    SET_IDREG(isar, ID_AA64PFR1, 0x0000000000000221ull);
    SET_IDREG(isar, ID_AA64ZFR0, 0x0000110100110021ull);
    SET_IDREG(isar, ID_AA64DFR0, 0x000011f210305619ull);
    SET_IDREG(isar, ID_AA64DFR1, 0);
    SET_IDREG(isar, ID_AA64AFR0, 0);
    SET_IDREG(isar, ID_AA64AFR1, 0);
    SET_IDREG(isar, ID_AA64ISAR0, 0x1221111110212120ull);
    SET_IDREG(isar, ID_AA64ISAR1, 0x0011111101211052ull);
    SET_IDREG(isar, ID_AA64MMFR0, 0x0000022200101125ull);
    SET_IDREG(isar, ID_AA64MMFR1, 0x0000000010212122ull);
    SET_IDREG(isar, ID_AA64MMFR2, 0x1221011112101011ull);
    SET_IDREG(isar, CLIDR, 0x0000001482000023ull);
    cpu->gm_blocksize      = 4;
    cpu->ctr               = 0x00000004b444c004ull;
    cpu->dcz_blocksize     = 4;

    cpu->isar.reset_pmcr_el0 = 0x3000;

    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    cpu->gic_pribits = 5;

    cpu->sve_vq.supported = 1 << 0;

    cpu->ccsidr[0] = make_ccsidr(CCSIDR_FORMAT_CCIDX, 4, 64, 64 * KiB, 0);
    cpu->ccsidr[1] = cpu->ccsidr[0];
    cpu->ccsidr[2] = make_ccsidr(CCSIDR_FORMAT_CCIDX, 8, 64, 512 * KiB, 0);
    /* FIXME: Not documented -- copied from neoverse-v1 */
    cpu->reset_sctlr = 0x30c50838;

    define_arm_cp_regs(cpu, cortex_a710_cp_reginfo);
    define_arm_cp_regs(cpu, neoverse_n2_cp_reginfo);

    aarch64_add_pauth_properties(obj);
    aarch64_add_sve_properties(obj);
}

void aarch64_max_tcg_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;
    uint64_t t;
    uint32_t u;

    unset_feature(&cpu->env, ARM_FEATURE_BACKCOMPAT_CNTFRQ);

    t = FIELD_DP64(0, MIDR_EL1, IMPLEMENTER, 0);
    t = FIELD_DP64(t, MIDR_EL1, ARCHITECTURE, 0xf);
    t = FIELD_DP64(t, MIDR_EL1, PARTNUM, 'Q');
    t = FIELD_DP64(t, MIDR_EL1, VARIANT, 0);
    t = FIELD_DP64(t, MIDR_EL1, REVISION, 0);
    cpu->midr = t;

    u = GET_IDREG(isar, CLIDR);
    u = FIELD_DP32(u, CLIDR_EL1, LOUIS, 0);
    u = FIELD_DP32(u, CLIDR_EL1, LOUU, 0);
    SET_IDREG(isar, CLIDR, u);

    t = cpu->ctr;
    t = FIELD_DP64(t, CTR_EL0, IDC, 1);
    t = FIELD_DP64(t, CTR_EL0, DIC, 1);
    cpu->ctr = t;

    t = GET_IDREG(isar, ID_AA64ISAR0);
    t = FIELD_DP64(t, ID_AA64ISAR0, AES, 2);
    t = FIELD_DP64(t, ID_AA64ISAR0, SHA1, 1);
    t = FIELD_DP64(t, ID_AA64ISAR0, SHA2, 2);
    t = FIELD_DP64(t, ID_AA64ISAR0, CRC32, 1);
    t = FIELD_DP64(t, ID_AA64ISAR0, ATOMIC, 3);
    t = FIELD_DP64(t, ID_AA64ISAR0, RDM, 1);
    t = FIELD_DP64(t, ID_AA64ISAR0, SHA3, 1);
    t = FIELD_DP64(t, ID_AA64ISAR0, SM3, 1);
    t = FIELD_DP64(t, ID_AA64ISAR0, SM4, 1);
    t = FIELD_DP64(t, ID_AA64ISAR0, DP, 1);
    t = FIELD_DP64(t, ID_AA64ISAR0, FHM, 1);
    t = FIELD_DP64(t, ID_AA64ISAR0, TS, 2);
    t = FIELD_DP64(t, ID_AA64ISAR0, TLB, 2);
    t = FIELD_DP64(t, ID_AA64ISAR0, RNDR, 1);
    SET_IDREG(isar, ID_AA64ISAR0, t);

    t = GET_IDREG(isar, ID_AA64ISAR1);
    t = FIELD_DP64(t, ID_AA64ISAR1, DPB, 2);
    t = FIELD_DP64(t, ID_AA64ISAR1, APA, PauthFeat_FPACCOMBINED);
    t = FIELD_DP64(t, ID_AA64ISAR1, API, 1);
    t = FIELD_DP64(t, ID_AA64ISAR1, JSCVT, 1);
    t = FIELD_DP64(t, ID_AA64ISAR1, FCMA, 1);
    t = FIELD_DP64(t, ID_AA64ISAR1, LRCPC, 2);
    t = FIELD_DP64(t, ID_AA64ISAR1, FRINTTS, 1);
    t = FIELD_DP64(t, ID_AA64ISAR1, SB, 1);
    t = FIELD_DP64(t, ID_AA64ISAR1, SPECRES, 1);
    t = FIELD_DP64(t, ID_AA64ISAR1, BF16, 2);
    t = FIELD_DP64(t, ID_AA64ISAR1, DGH, 1);
    t = FIELD_DP64(t, ID_AA64ISAR1, I8MM, 1);
    t = FIELD_DP64(t, ID_AA64ISAR1, XS, 1);
    SET_IDREG(isar, ID_AA64ISAR1, t);

    t = GET_IDREG(isar, ID_AA64ISAR2);
    t = FIELD_DP64(t, ID_AA64ISAR2, RPRES, 1);
    t = FIELD_DP64(t, ID_AA64ISAR2, MOPS, 1);
    t = FIELD_DP64(t, ID_AA64ISAR2, BC, 1);
    t = FIELD_DP64(t, ID_AA64ISAR2, WFXT, 2);
    t = FIELD_DP64(t, ID_AA64ISAR2, CSSC, 1);
    t = FIELD_DP64(t, ID_AA64ISAR2, ATS1A, 1);
    SET_IDREG(isar, ID_AA64ISAR2, t);

    t = GET_IDREG(isar, ID_AA64PFR0);
    t = FIELD_DP64(t, ID_AA64PFR0, FP, 1);
    t = FIELD_DP64(t, ID_AA64PFR0, ADVSIMD, 1);
    t = FIELD_DP64(t, ID_AA64PFR0, RAS, 2);
    t = FIELD_DP64(t, ID_AA64PFR0, SVE, 1);
    t = FIELD_DP64(t, ID_AA64PFR0, SEL2, 1);
    t = FIELD_DP64(t, ID_AA64PFR0, DIT, 1);
    t = FIELD_DP64(t, ID_AA64PFR0, CSV2, 3);
    t = FIELD_DP64(t, ID_AA64PFR0, CSV3, 1);
    SET_IDREG(isar, ID_AA64PFR0, t);

    t = GET_IDREG(isar, ID_AA64PFR1);
    t = FIELD_DP64(t, ID_AA64PFR1, BT, 1);
    t = FIELD_DP64(t, ID_AA64PFR1, SSBS, 2);
    t = FIELD_DP64(t, ID_AA64PFR1, MTE, 3);
    t = FIELD_DP64(t, ID_AA64PFR1, RAS_FRAC, 0);
    t = FIELD_DP64(t, ID_AA64PFR1, SME, 2);
    t = FIELD_DP64(t, ID_AA64PFR1, CSV2_FRAC, 0);
    t = FIELD_DP64(t, ID_AA64PFR1, NMI, 1);
    t = FIELD_DP64(t, ID_AA64PFR1, GCS, 1);
    SET_IDREG(isar, ID_AA64PFR1, t);

    t = GET_IDREG(isar, ID_AA64MMFR0);
    t = FIELD_DP64(t, ID_AA64MMFR0, PARANGE, 6);
    t = FIELD_DP64(t, ID_AA64MMFR0, TGRAN16, 1);
    t = FIELD_DP64(t, ID_AA64MMFR0, TGRAN16_2, 2);
    t = FIELD_DP64(t, ID_AA64MMFR0, TGRAN64_2, 2);
    t = FIELD_DP64(t, ID_AA64MMFR0, TGRAN4_2, 2);
    t = FIELD_DP64(t, ID_AA64MMFR0, FGT, 1);
    t = FIELD_DP64(t, ID_AA64MMFR0, ECV, 2);
    SET_IDREG(isar, ID_AA64MMFR0, t);

    t = GET_IDREG(isar, ID_AA64MMFR1);
    t = FIELD_DP64(t, ID_AA64MMFR1, HAFDBS, 2);
    t = FIELD_DP64(t, ID_AA64MMFR1, VMIDBITS, 2);
    t = FIELD_DP64(t, ID_AA64MMFR1, VH, 1);
    t = FIELD_DP64(t, ID_AA64MMFR1, HPDS, 2);
    t = FIELD_DP64(t, ID_AA64MMFR1, LO, 1);
    t = FIELD_DP64(t, ID_AA64MMFR1, PAN, 3);
    t = FIELD_DP64(t, ID_AA64MMFR1, XNX, 1);
    t = FIELD_DP64(t, ID_AA64MMFR1, ETS, 2);
    t = FIELD_DP64(t, ID_AA64MMFR1, HCX, 1);
    t = FIELD_DP64(t, ID_AA64MMFR1, AFP, 1);
    t = FIELD_DP64(t, ID_AA64MMFR1, TIDCP1, 1);
    t = FIELD_DP64(t, ID_AA64MMFR1, CMOW, 1);
    SET_IDREG(isar, ID_AA64MMFR1, t);

    t = GET_IDREG(isar, ID_AA64MMFR2);
    t = FIELD_DP64(t, ID_AA64MMFR2, CNP, 1);
    t = FIELD_DP64(t, ID_AA64MMFR2, UAO, 1);
    t = FIELD_DP64(t, ID_AA64MMFR2, IESB, 1);
    t = FIELD_DP64(t, ID_AA64MMFR2, VARANGE, 1);
    t = FIELD_DP64(t, ID_AA64MMFR2, NV, 2);
    t = FIELD_DP64(t, ID_AA64MMFR2, ST, 1);
    t = FIELD_DP64(t, ID_AA64MMFR2, AT, 1);
    t = FIELD_DP64(t, ID_AA64MMFR2, IDS, 1);
    t = FIELD_DP64(t, ID_AA64MMFR2, FWB, 1);
    t = FIELD_DP64(t, ID_AA64MMFR2, TTL, 1);
    t = FIELD_DP64(t, ID_AA64MMFR2, BBM, 2);
    t = FIELD_DP64(t, ID_AA64MMFR2, EVT, 2);
    t = FIELD_DP64(t, ID_AA64MMFR2, E0PD, 1);
    SET_IDREG(isar, ID_AA64MMFR2, t);

    t = GET_IDREG(isar, ID_AA64MMFR3);
    t = FIELD_DP64(t, ID_AA64MMFR3, TCRX, 1);
    t = FIELD_DP64(t, ID_AA64MMFR3, SCTLRX, 1);
    t = FIELD_DP64(t, ID_AA64MMFR3, MEC, 1);
    t = FIELD_DP64(t, ID_AA64MMFR3, SPEC_FPACC, 1);
    t = FIELD_DP64(t, ID_AA64MMFR3, S1PIE, 1);
    t = FIELD_DP64(t, ID_AA64MMFR3, S2PIE, 1);
    t = FIELD_DP64(t, ID_AA64MMFR3, AIE, 1);
    SET_IDREG(isar, ID_AA64MMFR3, t);

    t = GET_IDREG(isar, ID_AA64ZFR0);
    t = FIELD_DP64(t, ID_AA64ZFR0, SVEVER, 2);
    t = FIELD_DP64(t, ID_AA64ZFR0, AES, 2);
    t = FIELD_DP64(t, ID_AA64ZFR0, BITPERM, 1);
    t = FIELD_DP64(t, ID_AA64ZFR0, BFLOAT16, 2);
    t = FIELD_DP64(t, ID_AA64ZFR0, B16B16, 1);
    t = FIELD_DP64(t, ID_AA64ZFR0, SHA3, 1);
    t = FIELD_DP64(t, ID_AA64ZFR0, SM4, 1);
    t = FIELD_DP64(t, ID_AA64ZFR0, I8MM, 1);
    t = FIELD_DP64(t, ID_AA64ZFR0, F32MM, 1);
    t = FIELD_DP64(t, ID_AA64ZFR0, F64MM, 1);
    SET_IDREG(isar, ID_AA64ZFR0, t);

    t = GET_IDREG(isar, ID_AA64DFR0);
    t = FIELD_DP64(t, ID_AA64DFR0, DEBUGVER, 10);
    t = FIELD_DP64(t, ID_AA64DFR0, PMUVER, 6);
    t = FIELD_DP64(t, ID_AA64DFR0, HPMN0, 1);
    SET_IDREG(isar, ID_AA64DFR0, t);

    t = GET_IDREG(isar, ID_AA64SMFR0);
    t = FIELD_DP64(t, ID_AA64SMFR0, F32F32, 1);
    t = FIELD_DP64(t, ID_AA64SMFR0, BI32I32, 1);
    t = FIELD_DP64(t, ID_AA64SMFR0, B16F32, 1);
    t = FIELD_DP64(t, ID_AA64SMFR0, F16F32, 1);
    t = FIELD_DP64(t, ID_AA64SMFR0, I8I32, 0xf);
    t = FIELD_DP64(t, ID_AA64SMFR0, F16F16, 1);
    t = FIELD_DP64(t, ID_AA64SMFR0, B16B16, 1);
    t = FIELD_DP64(t, ID_AA64SMFR0, I16I32, 5);
    t = FIELD_DP64(t, ID_AA64SMFR0, F64F64, 1);
    t = FIELD_DP64(t, ID_AA64SMFR0, I16I64, 0xf);
    t = FIELD_DP64(t, ID_AA64SMFR0, SMEVER, 2);
    t = FIELD_DP64(t, ID_AA64SMFR0, FA64, 1);
    SET_IDREG(isar, ID_AA64SMFR0, t);

    aa32_max_features(cpu);

#ifdef CONFIG_USER_ONLY
    cpu->ctr = 0x80038003;
    cpu->dcz_blocksize = 7;
#endif
    cpu->gm_blocksize = 6;

    cpu->sve_vq.supported = MAKE_64BIT_MASK(0, ARM_MAX_VQ);
    cpu->sme_vq.supported = SVE_VQ_POW2_MAP;

    aarch64_add_pauth_properties(obj);
    aarch64_add_sve_properties(obj);
    aarch64_add_sme_properties(obj);
    object_property_add(obj, "sve-max-vq", "uint32", cpu_max_get_sve_max_vq,
                        cpu_max_set_sve_max_vq, NULL, NULL);
    object_property_add_bool(obj, "x-rme", cpu_arm_get_rme, cpu_arm_set_rme);
    object_property_add(obj, "x-l0gptsz", "uint32", cpu_max_get_l0gptsz,
                        cpu_max_set_l0gptsz, NULL, NULL);
    qdev_property_add_static(DEVICE(obj), &arm_cpu_lpa2_property);
}

static const ARMCPUInfo aarch64_cpus[] = {
    { .name = "cortex-a35",         .initfn = aarch64_a35_initfn },
    { .name = "cortex-a55",         .initfn = aarch64_a55_initfn },
    { .name = "cortex-a72",         .initfn = aarch64_a72_initfn },
    { .name = "cortex-a76",         .initfn = aarch64_a76_initfn },
    { .name = "cortex-a78ae",       .initfn = aarch64_a78ae_initfn },
    { .name = "cortex-a710",        .initfn = aarch64_a710_initfn },
    { .name = "a64fx",              .initfn = aarch64_a64fx_initfn },
    { .name = "neoverse-n1",        .initfn = aarch64_neoverse_n1_initfn },
    { .name = "neoverse-v1",        .initfn = aarch64_neoverse_v1_initfn },
    { .name = "neoverse-n2",        .initfn = aarch64_neoverse_n2_initfn },
};

static void aarch64_cpu_register_types(void)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(aarch64_cpus); ++i) {
        arm_cpu_register(&aarch64_cpus[i]);
    }
}

type_init(aarch64_cpu_register_types)
