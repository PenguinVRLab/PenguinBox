/*
 *  S/390 FPU helper routines
 *
 *  Copyright (c) 2009 Ulrich Hecht
 *  Copyright (c) 2009 Alexander Graf
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
#include "s390x-internal.h"
#include "tcg_s390x.h"
#include "exec/helper-proto.h"
#include "fpu/softfloat.h"

#ifdef DEBUG_HELPER
#define HELPER_LOG(x...) qemu_log(x)
#else
#define HELPER_LOG(x...)
#endif

static inline Int128 RET128(float128 f)
{
    return int128_make128(f.low, f.high);
}

static inline float128 ARG128(Int128 i)
{
    return make_float128(int128_gethi(i), int128_getlo(i));
}

uint8_t s390_softfloat_exc_to_ieee(unsigned int exc)
{
    uint8_t s390_exc = 0;

    s390_exc |= (exc & float_flag_invalid) ? S390_IEEE_MASK_INVALID : 0;
    s390_exc |= (exc & float_flag_divbyzero) ? S390_IEEE_MASK_DIVBYZERO : 0;
    s390_exc |= (exc & float_flag_overflow) ? S390_IEEE_MASK_OVERFLOW : 0;
    s390_exc |= (exc & float_flag_underflow) ? S390_IEEE_MASK_UNDERFLOW : 0;
    s390_exc |= (exc & (float_flag_inexact | float_flag_invalid_cvti)) ?
                S390_IEEE_MASK_INEXACT : 0;

    return s390_exc;
}

static void handle_exceptions(CPUS390XState *env, bool XxC, uintptr_t retaddr)
{
    unsigned s390_exc, qemu_exc;

    qemu_exc = env->fpu_status.float_exception_flags;
    if (qemu_exc == 0) {
        return;
    }
    env->fpu_status.float_exception_flags = 0;
    s390_exc = s390_softfloat_exc_to_ieee(qemu_exc);

    if (!(s390_exc & S390_IEEE_MASK_INEXACT) &&
        !((env->fpc >> 24) & S390_IEEE_MASK_UNDERFLOW)) {
        s390_exc &= ~S390_IEEE_MASK_UNDERFLOW;
    }

    if (s390_exc & ~S390_IEEE_MASK_INEXACT) {
        if (s390_exc & ~S390_IEEE_MASK_INEXACT & env->fpc >> 24) {
            tcg_s390_data_exception(env, s390_exc, retaddr);
        }
        env->fpc |= (s390_exc & ~S390_IEEE_MASK_INEXACT) << 16;
    }

    if (s390_exc & S390_IEEE_MASK_INEXACT && !XxC) {
        if (s390_exc & S390_IEEE_MASK_INEXACT & env->fpc >> 24) {
            tcg_s390_data_exception(env, s390_exc & S390_IEEE_MASK_INEXACT,
                                    retaddr);
        }
        env->fpc |= (s390_exc & S390_IEEE_MASK_INEXACT) << 16;
    }
}

int float_comp_to_cc(CPUS390XState *env, FloatRelation float_compare)
{
    switch (float_compare) {
    case float_relation_equal:
        return 0;
    case float_relation_less:
        return 1;
    case float_relation_greater:
        return 2;
    case float_relation_unordered:
        return 3;
    default:
        cpu_abort(env_cpu(env), "unknown return value for float compare\n");
    }
}

uint32_t set_cc_nz_f32(float32 v)
{
    if (float32_is_any_nan(v)) {
        return 3;
    } else if (float32_is_zero(v)) {
        return 0;
    } else if (float32_is_neg(v)) {
        return 1;
    } else {
        return 2;
    }
}

uint32_t set_cc_nz_f64(float64 v)
{
    if (float64_is_any_nan(v)) {
        return 3;
    } else if (float64_is_zero(v)) {
        return 0;
    } else if (float64_is_neg(v)) {
        return 1;
    } else {
        return 2;
    }
}

uint32_t set_cc_nz_f128(float128 v)
{
    if (float128_is_any_nan(v)) {
        return 3;
    } else if (float128_is_zero(v)) {
        return 0;
    } else if (float128_is_neg(v)) {
        return 1;
    } else {
        return 2;
    }
}

static uint32_t set_cc_conv_f32(float32 v, float_status *stat)
{
    if (stat->float_exception_flags & float_flag_invalid) {
        return 3;
    } else {
        return set_cc_nz_f32(v);
    }
}

static uint32_t set_cc_conv_f64(float64 v, float_status *stat)
{
    if (stat->float_exception_flags & float_flag_invalid) {
        return 3;
    } else {
        return set_cc_nz_f64(v);
    }
}

static uint32_t set_cc_conv_f128(float128 v, float_status *stat)
{
    if (stat->float_exception_flags & float_flag_invalid) {
        return 3;
    } else {
        return set_cc_nz_f128(v);
    }
}

static inline uint8_t round_from_m34(uint32_t m34)
{
    return extract32(m34, 0, 4);
}

static inline bool xxc_from_m34(uint32_t m34)
{
    return extract32(m34, 4 + 3 - 1, 1);
}

uint64_t HELPER(aeb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float32 ret = float32_add(f1, f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

uint64_t HELPER(adb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float64 ret = float64_add(f1, f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

Int128 HELPER(axb)(CPUS390XState *env, Int128 a, Int128 b)
{
    float128 ret = float128_add(ARG128(a), ARG128(b), &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return RET128(ret);
}

uint64_t HELPER(seb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float32 ret = float32_sub(f1, f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

uint64_t HELPER(sdb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float64 ret = float64_sub(f1, f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

Int128 HELPER(sxb)(CPUS390XState *env, Int128 a, Int128 b)
{
    float128 ret = float128_sub(ARG128(a), ARG128(b), &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return RET128(ret);
}

uint64_t HELPER(deb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float32 ret = float32_div(f1, f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

uint64_t HELPER(ddb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float64 ret = float64_div(f1, f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

Int128 HELPER(dxb)(CPUS390XState *env, Int128 a, Int128 b)
{
    float128 ret = float128_div(ARG128(a), ARG128(b), &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return RET128(ret);
}

uint64_t HELPER(meeb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float32 ret = float32_mul(f1, f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

uint64_t HELPER(mdb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float64 ret = float64_mul(f1, f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

uint64_t HELPER(mdeb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float64 f1_64 = float32_to_float64(f1, &env->fpu_status);
    float64 ret = float32_to_float64(f2, &env->fpu_status);
    ret = float64_mul(f1_64, ret, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

Int128 HELPER(mxb)(CPUS390XState *env, Int128 a, Int128 b)
{
    float128 ret = float128_mul(ARG128(a), ARG128(b), &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return RET128(ret);
}

Int128 HELPER(mxdb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float128 f1_128 = float64_to_float128(f1, &env->fpu_status);
    float128 ret = float64_to_float128(f2, &env->fpu_status);
    ret = float128_mul(f1_128, ret, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return RET128(ret);
}

uint64_t HELPER(ldeb)(CPUS390XState *env, uint64_t f2)
{
    float64 ret = float32_to_float64(f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

uint64_t HELPER(ldxb)(CPUS390XState *env, Int128 a, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float64 ret = float128_to_float64(ARG128(a), &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    return ret;
}

Int128 HELPER(lxdb)(CPUS390XState *env, uint64_t f2)
{
    float128 ret = float64_to_float128(f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return RET128(ret);
}

Int128 HELPER(lxeb)(CPUS390XState *env, uint64_t f2)
{
    float128 ret = float32_to_float128(f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return RET128(ret);
}

uint64_t HELPER(ledb)(CPUS390XState *env, uint64_t f2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float32 ret = float64_to_float32(f2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    return ret;
}

uint64_t HELPER(lexb)(CPUS390XState *env, Int128 a, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float32 ret = float128_to_float32(ARG128(a), &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    return ret;
}

uint32_t HELPER(ceb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    FloatRelation cmp = float32_compare_quiet(f1, f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return float_comp_to_cc(env, cmp);
}

uint32_t HELPER(cdb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    FloatRelation cmp = float64_compare_quiet(f1, f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return float_comp_to_cc(env, cmp);
}

uint32_t HELPER(cxb)(CPUS390XState *env, Int128 a, Int128 b)
{
    FloatRelation cmp = float128_compare_quiet(ARG128(a), ARG128(b),
                                               &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return float_comp_to_cc(env, cmp);
}

int s390_swap_bfp_rounding_mode(CPUS390XState *env, int m3)
{
    int ret = env->fpu_status.float_rounding_mode;

    switch (m3) {
    case 0:
        break;
    case 1:
        set_float_rounding_mode(float_round_ties_away, &env->fpu_status);
        break;
    case 3:
        set_float_rounding_mode(float_round_to_odd, &env->fpu_status);
        break;
    case 4:
        set_float_rounding_mode(float_round_nearest_even, &env->fpu_status);
        break;
    case 5:
        set_float_rounding_mode(float_round_to_zero, &env->fpu_status);
        break;
    case 6:
        set_float_rounding_mode(float_round_up, &env->fpu_status);
        break;
    case 7:
        set_float_rounding_mode(float_round_down, &env->fpu_status);
        break;
    default:
        g_assert_not_reached();
    }
    return ret;
}

void s390_restore_bfp_rounding_mode(CPUS390XState *env, int old_mode)
{
    set_float_rounding_mode(old_mode, &env->fpu_status);
}

uint64_t HELPER(cegb)(CPUS390XState *env, int64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float32 ret = int64_to_float32(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    return ret;
}

uint64_t HELPER(cdgb)(CPUS390XState *env, int64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float64 ret = int64_to_float64(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    return ret;
}

Int128 HELPER(cxgb)(CPUS390XState *env, int64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float128 ret = int64_to_float128(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    return RET128(ret);
}

uint64_t HELPER(celgb)(CPUS390XState *env, uint64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float32 ret = uint64_to_float32(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    return ret;
}

uint64_t HELPER(cdlgb)(CPUS390XState *env, uint64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float64 ret = uint64_to_float64(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    return ret;
}

Int128 HELPER(cxlgb)(CPUS390XState *env, uint64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float128 ret = uint64_to_float128(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    return RET128(ret);
}

uint64_t HELPER(cgeb)(CPUS390XState *env, uint64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    int64_t ret = float32_to_int64(v2, &env->fpu_status);
    uint32_t cc = set_cc_conv_f32(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    env->cc_op = cc;
    if (float32_is_any_nan(v2)) {
        return INT64_MIN;
    }
    return ret;
}

uint64_t HELPER(cgdb)(CPUS390XState *env, uint64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    int64_t ret = float64_to_int64(v2, &env->fpu_status);
    uint32_t cc = set_cc_conv_f64(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    env->cc_op = cc;
    if (float64_is_any_nan(v2)) {
        return INT64_MIN;
    }
    return ret;
}

uint64_t HELPER(cgxb)(CPUS390XState *env, Int128 i2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float128 v2 = ARG128(i2);
    int64_t ret = float128_to_int64(v2, &env->fpu_status);
    uint32_t cc = set_cc_conv_f128(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    env->cc_op = cc;
    if (float128_is_any_nan(v2)) {
        return INT64_MIN;
    }
    return ret;
}

uint64_t HELPER(cfeb)(CPUS390XState *env, uint64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    int32_t ret = float32_to_int32(v2, &env->fpu_status);
    uint32_t cc = set_cc_conv_f32(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    env->cc_op = cc;
    if (float32_is_any_nan(v2)) {
        return INT32_MIN;
    }
    return ret;
}

uint64_t HELPER(cfdb)(CPUS390XState *env, uint64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    int32_t ret = float64_to_int32(v2, &env->fpu_status);
    uint32_t cc = set_cc_conv_f64(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    env->cc_op = cc;
    if (float64_is_any_nan(v2)) {
        return INT32_MIN;
    }
    return ret;
}

uint64_t HELPER(cfxb)(CPUS390XState *env, Int128 i2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float128 v2 = ARG128(i2);
    int32_t ret = float128_to_int32(v2, &env->fpu_status);
    uint32_t cc = set_cc_conv_f128(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    env->cc_op = cc;
    if (float128_is_any_nan(v2)) {
        return INT32_MIN;
    }
    return ret;
}

uint64_t HELPER(clgeb)(CPUS390XState *env, uint64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    uint64_t ret = float32_to_uint64(v2, &env->fpu_status);
    uint32_t cc = set_cc_conv_f32(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    env->cc_op = cc;
    if (float32_is_any_nan(v2)) {
        return 0;
    }
    return ret;
}

uint64_t HELPER(clgdb)(CPUS390XState *env, uint64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    uint64_t ret = float64_to_uint64(v2, &env->fpu_status);
    uint32_t cc = set_cc_conv_f64(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    env->cc_op = cc;
    if (float64_is_any_nan(v2)) {
        return 0;
    }
    return ret;
}

uint64_t HELPER(clgxb)(CPUS390XState *env, Int128 i2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float128 v2 = ARG128(i2);
    uint64_t ret = float128_to_uint64(v2, &env->fpu_status);
    uint32_t cc = set_cc_conv_f128(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    env->cc_op = cc;
    if (float128_is_any_nan(v2)) {
        return 0;
    }
    return ret;
}

uint64_t HELPER(clfeb)(CPUS390XState *env, uint64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    uint32_t ret = float32_to_uint32(v2, &env->fpu_status);
    uint32_t cc = set_cc_conv_f32(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    env->cc_op = cc;
    if (float32_is_any_nan(v2)) {
        return 0;
    }
    return ret;
}

uint64_t HELPER(clfdb)(CPUS390XState *env, uint64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    uint32_t ret = float64_to_uint32(v2, &env->fpu_status);
    uint32_t cc = set_cc_conv_f64(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    env->cc_op = cc;
    if (float64_is_any_nan(v2)) {
        return 0;
    }
    return ret;
}

uint64_t HELPER(clfxb)(CPUS390XState *env, Int128 i2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float128 v2 = ARG128(i2);
    uint32_t ret = float128_to_uint32(v2, &env->fpu_status);
    uint32_t cc = set_cc_conv_f128(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    env->cc_op = cc;
    if (float128_is_any_nan(v2)) {
        return 0;
    }
    return ret;
}

uint64_t HELPER(fieb)(CPUS390XState *env, uint64_t f2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float32 ret = float32_round_to_int(f2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    return ret;
}

uint64_t HELPER(fidb)(CPUS390XState *env, uint64_t f2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float64 ret = float64_round_to_int(f2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    return ret;
}

Int128 HELPER(fixb)(CPUS390XState *env, Int128 a, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float128 ret = float128_round_to_int(ARG128(a), &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    return RET128(ret);
}

uint32_t HELPER(keb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    FloatRelation cmp = float32_compare(f1, f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return float_comp_to_cc(env, cmp);
}

uint32_t HELPER(kdb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    FloatRelation cmp = float64_compare(f1, f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return float_comp_to_cc(env, cmp);
}

uint32_t HELPER(kxb)(CPUS390XState *env, Int128 a, Int128 b)
{
    FloatRelation cmp = float128_compare(ARG128(a), ARG128(b),
                                         &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return float_comp_to_cc(env, cmp);
}

uint64_t HELPER(maeb)(CPUS390XState *env, uint64_t f1,
                      uint64_t f2, uint64_t f3)
{
    float32 ret = float32_muladd(f3, f2, f1, 0, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

uint64_t HELPER(madb)(CPUS390XState *env, uint64_t f1,
                      uint64_t f2, uint64_t f3)
{
    float64 ret = float64_muladd(f3, f2, f1, 0, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

uint64_t HELPER(mseb)(CPUS390XState *env, uint64_t f1,
                      uint64_t f2, uint64_t f3)
{
    float32 ret = float32_muladd(f3, f2, f1, float_muladd_negate_c,
                                 &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

uint64_t HELPER(msdb)(CPUS390XState *env, uint64_t f1,
                      uint64_t f2, uint64_t f3)
{
    float64 ret = float64_muladd(f3, f2, f1, float_muladd_negate_c,
                                 &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

static inline uint16_t dcmask(int bit, bool neg)
{
    return 1 << (11 - bit - neg);
}

#define DEF_FLOAT_DCMASK(_TYPE) \
uint16_t _TYPE##_dcmask(CPUS390XState *env, _TYPE f1)              \
{                                                                  \
    const bool neg = _TYPE##_is_neg(f1);                           \
                                                                   \
 \
    if (_TYPE##_is_normal(f1)) {                                   \
        return dcmask(2, neg);                                     \
    } else if (_TYPE##_is_zero(f1)) {                              \
        return dcmask(0, neg);                                     \
    } else if (_TYPE##_is_denormal(f1)) {                          \
        return dcmask(4, neg);                                     \
    } else if (_TYPE##_is_infinity(f1)) {                          \
        return dcmask(6, neg);                                     \
    } else if (_TYPE##_is_quiet_nan(f1, &env->fpu_status)) {       \
        return dcmask(8, neg);                                     \
    }                                                              \
                    \
    return dcmask(10, neg);                                        \
}
DEF_FLOAT_DCMASK(float32)
DEF_FLOAT_DCMASK(float64)
DEF_FLOAT_DCMASK(float128)

uint32_t HELPER(tceb)(CPUS390XState *env, uint64_t f1, uint64_t m2)
{
    return (m2 & float32_dcmask(env, f1)) != 0;
}

uint32_t HELPER(tcdb)(CPUS390XState *env, uint64_t v1, uint64_t m2)
{
    return (m2 & float64_dcmask(env, v1)) != 0;
}

uint32_t HELPER(tcxb)(CPUS390XState *env, Int128 a, uint64_t m2)
{
    return (m2 & float128_dcmask(env, ARG128(a))) != 0;
}

uint64_t HELPER(sqeb)(CPUS390XState *env, uint64_t f2)
{
    float32 ret = float32_sqrt(f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

uint64_t HELPER(sqdb)(CPUS390XState *env, uint64_t f2)
{
    float64 ret = float64_sqrt(f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

Int128 HELPER(sqxb)(CPUS390XState *env, Int128 a)
{
    float128 ret = float128_sqrt(ARG128(a), &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return RET128(ret);
}

static const int fpc_to_rnd[8] = {
    float_round_nearest_even,
    float_round_to_zero,
    float_round_up,
    float_round_down,
    -1,
    -1,
    -1,
    float_round_to_odd,
};

void HELPER(sfpc)(CPUS390XState *env, uint64_t fpc)
{
    if (fpc_to_rnd[fpc & 0x7] == -1 || fpc & 0x03030088u ||
        (!s390_has_feat(S390_FEAT_FLOATING_POINT_EXT) && fpc & 0x4)) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, GETPC());
    }

    env->fpc = fpc;

    set_float_rounding_mode(fpc_to_rnd[fpc & 0x7], &env->fpu_status);
}

void HELPER(sfas)(CPUS390XState *env, uint64_t fpc)
{
    uint32_t signalling = env->fpc;
    uint32_t s390_exc;

    if (fpc_to_rnd[fpc & 0x7] == -1 || fpc & 0x03030088u ||
        (!s390_has_feat(S390_FEAT_FLOATING_POINT_EXT) && fpc & 0x4)) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, GETPC());
    }

    env->fpc = fpc | (signalling & 0x00ff0000);
    set_float_rounding_mode(fpc_to_rnd[fpc & 0x7], &env->fpu_status);

    s390_exc = (signalling >> 16) & (fpc >> 24);
    if (s390_exc) {
        if (s390_exc & S390_IEEE_MASK_INVALID) {
            s390_exc = S390_IEEE_MASK_INVALID;
        } else if (s390_exc & S390_IEEE_MASK_DIVBYZERO) {
            s390_exc = S390_IEEE_MASK_DIVBYZERO;
        } else if (s390_exc & S390_IEEE_MASK_OVERFLOW) {
            s390_exc &= (S390_IEEE_MASK_OVERFLOW | S390_IEEE_MASK_INEXACT);
        } else if (s390_exc & S390_IEEE_MASK_UNDERFLOW) {
            s390_exc &= (S390_IEEE_MASK_UNDERFLOW | S390_IEEE_MASK_INEXACT);
        } else if (s390_exc & S390_IEEE_MASK_INEXACT) {
            s390_exc = S390_IEEE_MASK_INEXACT;
        } else if (s390_exc & S390_IEEE_MASK_QUANTUM) {
            s390_exc = S390_IEEE_MASK_QUANTUM;
        }
        tcg_s390_data_exception(env, s390_exc | 3, GETPC());
    }
}

void HELPER(srnm)(CPUS390XState *env, uint64_t rnd)
{
    if (rnd > 0x7 || fpc_to_rnd[rnd & 0x7] == -1) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, GETPC());
    }

    env->fpc = deposit32(env->fpc, 0, 3, rnd);
    set_float_rounding_mode(fpc_to_rnd[rnd & 0x7], &env->fpu_status);
}
