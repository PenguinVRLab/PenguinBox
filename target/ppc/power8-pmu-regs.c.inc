/*
 * PMU register read/write functions for TCG IBM POWER chips
 *
 * Copyright IBM Corp. 2021
 *
 * Authors:
 *  Daniel Henrique Barboza      <danielhb413@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#if defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY)

static bool spr_groupA_read_allowed(DisasContext *ctx)
{
    if (!ctx->mmcr0_pmcc0 && ctx->mmcr0_pmcc1) {
        gen_exception_err(ctx, POWERPC_EXCP_FU, FSCR_IC_PMU);
        return false;
    }

    return true;
}

static bool spr_groupA_write_allowed(DisasContext *ctx)
{
    if (ctx->mmcr0_pmcc0) {
        return true;
    }

    if (ctx->mmcr0_pmcc1) {
        gen_exception_err(ctx, POWERPC_EXCP_FU, FSCR_IC_PMU);
    } else {
        gen_hvpriv_exception(ctx, POWERPC_EXCP_PRIV_REG);
    }

    return false;
}

static TCGv masked_gprn_for_spr_write(int gprn, int sprn,
                                      uint64_t spr_mask)
{
    TCGv ret = tcg_temp_new();
    TCGv t0 = tcg_temp_new();

    gen_load_spr(ret, sprn);
    tcg_gen_andi_tl(ret, ret, ~(spr_mask));

    tcg_gen_andi_tl(t0, cpu_gpr[gprn], spr_mask);

    tcg_gen_or_tl(ret, ret, t0);

    return ret;
}

void spr_read_MMCR0_ureg(DisasContext *ctx, int gprn, int sprn)
{
    TCGv t0;

    if (!spr_groupA_read_allowed(ctx)) {
        return;
    }

    t0 = tcg_temp_new();

    gen_load_spr(t0, SPR_POWER_MMCR0);
    tcg_gen_andi_tl(t0, t0, MMCR0_UREG_MASK);
    tcg_gen_mov_tl(cpu_gpr[gprn], t0);
}

static void write_MMCR0_common(DisasContext *ctx, TCGv val)
{
    translator_io_start(&ctx->base);
    gen_helper_store_mmcr0(tcg_env, val);

    ctx->base.is_jmp = DISAS_EXIT_UPDATE;
}

void spr_write_MMCR0_ureg(DisasContext *ctx, int sprn, int gprn)
{
    TCGv masked_gprn;

    if (!spr_groupA_write_allowed(ctx)) {
        return;
    }

    masked_gprn = masked_gprn_for_spr_write(gprn, SPR_POWER_MMCR0,
                                            MMCR0_UREG_MASK);
    write_MMCR0_common(ctx, masked_gprn);
}

void spr_read_MMCR2_ureg(DisasContext *ctx, int gprn, int sprn)
{
    TCGv t0;

    if (!spr_groupA_read_allowed(ctx)) {
        return;
    }

    t0 = tcg_temp_new();

    gen_load_spr(t0, SPR_POWER_MMCR2);
    tcg_gen_andi_tl(t0, t0, MMCR2_UREG_MASK);
    tcg_gen_mov_tl(cpu_gpr[gprn], t0);
}

void spr_write_MMCR2_ureg(DisasContext *ctx, int sprn, int gprn)
{
    TCGv masked_gprn;

    if (!spr_groupA_write_allowed(ctx)) {
        return;
    }

    masked_gprn = masked_gprn_for_spr_write(gprn, SPR_POWER_MMCR2,
                                            MMCR2_UREG_MASK);
    gen_store_spr(SPR_POWER_MMCR2, masked_gprn);
}

void spr_write_MMCRA(DisasContext *ctx, int sprn, int gprn)
{
    gen_helper_store_mmcrA(tcg_env, cpu_gpr[gprn]);
}

void spr_read_PMC(DisasContext *ctx, int gprn, int sprn)
{
    TCGv_i32 t_sprn = tcg_constant_i32(sprn);

    translator_io_start(&ctx->base);
    gen_helper_read_pmc(cpu_gpr[gprn], tcg_env, t_sprn);
}

void spr_read_PMC14_ureg(DisasContext *ctx, int gprn, int sprn)
{
    if (!spr_groupA_read_allowed(ctx)) {
        return;
    }

    spr_read_PMC(ctx, gprn, sprn + 0x10);
}

void spr_read_PMC56_ureg(DisasContext *ctx, int gprn, int sprn)
{
    if (ctx->mmcr0_pmcc0 && ctx->mmcr0_pmcc1) {
        gen_exception_err(ctx, POWERPC_EXCP_FU, FSCR_IC_PMU);
        return;
    }

    spr_read_PMC14_ureg(ctx, gprn, sprn);
}

void spr_write_PMC(DisasContext *ctx, int sprn, int gprn)
{
    TCGv_i32 t_sprn = tcg_constant_i32(sprn);

    translator_io_start(&ctx->base);
    gen_helper_store_pmc(tcg_env, t_sprn, cpu_gpr[gprn]);
}

void spr_write_PMC14_ureg(DisasContext *ctx, int sprn, int gprn)
{
    if (!spr_groupA_write_allowed(ctx)) {
        return;
    }

    spr_write_PMC(ctx, sprn + 0x10, gprn);
}

void spr_write_PMC56_ureg(DisasContext *ctx, int sprn, int gprn)
{
    if (ctx->mmcr0_pmcc0 && ctx->mmcr0_pmcc1) {
        gen_exception_err(ctx, POWERPC_EXCP_FU, FSCR_IC_PMU);
        return;
    }

    spr_write_PMC14_ureg(ctx, sprn, gprn);
}

void spr_write_MMCR0(DisasContext *ctx, int sprn, int gprn)
{
    write_MMCR0_common(ctx, cpu_gpr[gprn]);
}

void spr_write_MMCR1(DisasContext *ctx, int sprn, int gprn)
{
    translator_io_start(&ctx->base);
    gen_helper_store_mmcr1(tcg_env, cpu_gpr[gprn]);
}
#else
void spr_read_MMCR0_ureg(DisasContext *ctx, int gprn, int sprn)
{
    spr_read_ureg(ctx, gprn, sprn);
}

void spr_write_MMCR0_ureg(DisasContext *ctx, int sprn, int gprn)
{
    spr_noaccess(ctx, gprn, sprn);
}

void spr_read_MMCR2_ureg(DisasContext *ctx, int gprn, int sprn)
{
    spr_read_ureg(ctx, gprn, sprn);
}

void spr_write_MMCR2_ureg(DisasContext *ctx, int sprn, int gprn)
{
    spr_noaccess(ctx, gprn, sprn);
}

void spr_read_PMC14_ureg(DisasContext *ctx, int gprn, int sprn)
{
    spr_read_ureg(ctx, gprn, sprn);
}

void spr_read_PMC56_ureg(DisasContext *ctx, int gprn, int sprn)
{
    spr_read_ureg(ctx, gprn, sprn);
}

void spr_write_PMC14_ureg(DisasContext *ctx, int sprn, int gprn)
{
    spr_noaccess(ctx, gprn, sprn);
}

void spr_write_PMC56_ureg(DisasContext *ctx, int sprn, int gprn)
{
    spr_noaccess(ctx, gprn, sprn);
}

void spr_write_MMCR0(DisasContext *ctx, int sprn, int gprn)
{
    spr_write_generic(ctx, sprn, gprn);
}

void spr_write_MMCR1(DisasContext *ctx, int sprn, int gprn)
{
    spr_write_generic(ctx, sprn, gprn);
}

void spr_write_PMC(DisasContext *ctx, int sprn, int gprn)
{
    spr_write_generic(ctx, sprn, gprn);
}
#endif
