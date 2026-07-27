/*
 * Power ISA decode for misc instructions
 *
 * Copyright (c) 2024, IBM Corporation.
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

/*
 * Memory Barrier Instructions
 */

static bool trans_SYNC(DisasContext *ctx, arg_X_sync *a)
{
    TCGBar bar = TCG_MO_ALL;
    uint32_t l = a->l;
    uint32_t sc = a->sc;

    if (!(ctx->insns_flags & PPC_MEM_SYNC)) {
        if (ctx->insns_flags & PPC_BOOKE) {
            tcg_gen_mb(bar | TCG_BAR_SC);
            return true;
        }

        return false;
    }

    if (!(ctx->insns_flags2 & PPC2_ISA310)) {
        l &= 0x3;
        sc = 0;
    }

    if (sc) {
        bar = TCG_MO_ST_ST;
    } else {
        if (((l == 1) && (ctx->insns_flags2 & PPC2_MEM_LWSYNC)) || (l == 5)) {
            bar = TCG_MO_LD_LD | TCG_MO_LD_ST | TCG_MO_ST_ST;
        }

        if (((l == 2) || !(ctx->insns_flags & PPC_64B)) && !ctx->pr) {
            gen_check_tlb_flush(ctx, true);
        }
    }

    tcg_gen_mb(bar | TCG_BAR_SC);

    return true;
}

static bool trans_EIEIO(DisasContext *ctx, arg_EIEIO *a)
{
    TCGBar bar = TCG_MO_ALL;

    if (!(ctx->insns_flags & PPC_MEM_EIEIO)) {
        if ((ctx->insns_flags & PPC_BOOKE) ||
            (ctx->insns_flags2 & PPC2_BOOKE206)) {
            tcg_gen_mb(bar | TCG_BAR_SC);
            return true;
        }
        return false;
    }

    if (ctx->opcode & 0x2000000) {
        if (!(ctx->insns_flags2 & PPC2_ISA300)) {
            qemu_log_mask(LOG_GUEST_ERROR, "invalid eieio using bit 6 at @"
                          TARGET_FMT_lx "\n", ctx->cia);
        } else {
            bar = TCG_MO_ST_LD;
        }
    }

    tcg_gen_mb(bar | TCG_BAR_SC);

    return true;
}

static bool trans_ATTN(DisasContext *ctx, arg_ATTN *a)
{
#if defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY)
    gen_helper_attn(tcg_env);
    return true;
#else
    return false;
#endif
}
