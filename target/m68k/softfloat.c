/*
 * Ported from a work by Andreas Grabher for Previous, NeXT Computer Emulator,
 * derived from NetBSD M68040 FPSP functions,
 * derived from release 2a of the SoftFloat IEC/IEEE Floating-point Arithmetic
 * Package. Those parts of the code (and some later contributions) are
 * provided under that license, as detailed below.
 * It has subsequently been modified by contributors to the QEMU Project,
 * so some portions are provided under:
 *  the SoftFloat-2a license
 *  the BSD license
 *  GPL-v2-or-later
 *
 * Any future contributions to this file will be taken to be licensed under
 * the Softfloat-2a license unless specifically indicated otherwise.
 */

/*
 * Portions of this work are licensed under the terms of the GNU GPL,
 * version 2 or later. See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "softfloat.h"
#include "fpu/softfloat-macros.h"
#include "softfloat_fpsp_tables.h"

#define pi_exp      0x4000
#define piby2_exp   0x3FFF
#define pi_sig      UINT64_C(0xc90fdaa22168c235)

static floatx80 propagateFloatx80NaNOneArg(floatx80 a, float_status *status)
{
    if (floatx80_is_signaling_nan(a, status)) {
        float_raise(float_flag_invalid, status);
        a = floatx80_silence_nan(a, status);
    }

    if (status->default_nan_mode) {
        return floatx80_default_nan(status);
    }

    return a;
}

floatx80 floatx80_getman(floatx80 a, float_status *status)
{
    bool aSign;
    int32_t aExp;
    uint64_t aSig;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a , status);
        }
        float_raise(float_flag_invalid , status);
        return floatx80_default_nan(status);
    }

    if (aExp == 0) {
        if (aSig == 0) {
            return packFloatx80(aSign, 0, 0);
        }
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }

    return roundAndPackFloatx80(status->floatx80_rounding_precision, aSign,
                                0x3FFF, aSig, 0, status);
}

floatx80 floatx80_getexp(floatx80 a, float_status *status)
{
    bool aSign;
    int32_t aExp;
    uint64_t aSig;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a , status);
        }
        float_raise(float_flag_invalid , status);
        return floatx80_default_nan(status);
    }

    if (aExp == 0) {
        if (aSig == 0) {
            return packFloatx80(aSign, 0, 0);
        }
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }

    return int32_to_floatx80(aExp - 0x3FFF, status);
}

floatx80 floatx80_scale(floatx80 a, floatx80 b, float_status *status)
{
    bool aSign, bSign;
    int32_t aExp, bExp, shiftCount;
    uint64_t aSig, bSig;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    bSig = extractFloatx80Frac(b);
    bExp = extractFloatx80Exp(b);
    bSign = extractFloatx80Sign(b);

    if (bExp == 0x7FFF) {
        if ((uint64_t) (bSig << 1) ||
            ((aExp == 0x7FFF) && (uint64_t) (aSig << 1))) {
            return propagateFloatx80NaN(a, b, status);
        }
        float_raise(float_flag_invalid , status);
        return floatx80_default_nan(status);
    }
    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaN(a, b, status);
        }
        return floatx80_default_inf(aSign, status);
    }
    if (aExp == 0) {
        if (aSig == 0) {
            return packFloatx80(aSign, 0, 0);
        }
        if (bExp < 0x3FFF) {
            return a;
        }
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }

    if (bExp < 0x3FFF) {
        return a;
    }

    if (0x400F < bExp) {
        aExp = bSign ? -0x6001 : 0xE000;
        return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                    aSign, aExp, aSig, 0, status);
    }

    shiftCount = 0x403E - bExp;
    bSig >>= shiftCount;
    aExp = bSign ? (aExp - bSig) : (aExp + bSig);

    return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                aSign, aExp, aSig, 0, status);
}

floatx80 floatx80_move(floatx80 a, float_status *status)
{
    bool aSign;
    int32_t aExp;
    uint64_t aSig;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t)(aSig << 1)) {
            return propagateFloatx80NaNOneArg(a, status);
        }
        return a;
    }
    if (aExp == 0) {
        if (aSig == 0) {
            return a;
        }
        normalizeRoundAndPackFloatx80(status->floatx80_rounding_precision,
                                      aSign, aExp, aSig, 0, status);
    }
    return roundAndPackFloatx80(status->floatx80_rounding_precision, aSign,
                                aExp, aSig, 0, status);
}

/*
 * Algorithms for transcendental functions supported by MC68881 and MC68882
 * mathematical coprocessors. The functions are derived from FPSP library.
 */

#define one_exp     0x3FFF
#define one_sig     UINT64_C(0x8000000000000000)

static int32_t floatx80_make_compact(int32_t aExp, uint64_t aSig)
{
    return (aExp << 16) | (aSig >> 48);
}

floatx80 floatx80_lognp1(floatx80 a, float_status *status)
{
    bool aSign;
    int32_t aExp;
    uint64_t aSig, fSig;

    FloatRoundMode user_rnd_mode;
    FloatX80RoundPrec user_rnd_prec;

    int32_t compact, j, k;
    floatx80 fp0, fp1, fp2, fp3, f, logof2, klog2, saveu;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            propagateFloatx80NaNOneArg(a, status);
        }
        if (aSign) {
            float_raise(float_flag_invalid, status);
            return floatx80_default_nan(status);
        }
        return floatx80_default_inf(0, status);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(aSign, 0, 0);
    }

    if (aSign && aExp >= one_exp) {
        if (aExp == one_exp && aSig == one_sig) {
            float_raise(float_flag_divbyzero, status);
            return floatx80_default_inf(aSign, status);
        }
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }

    if (aExp < 0x3f99 || (aExp == 0x3f99 && aSig == one_sig)) {
        float_raise(float_flag_inexact, status);
        return floatx80_move(a, status);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = floatx80_precision_x;

    compact = floatx80_make_compact(aExp, aSig);

    fp0 = a;
    fp1 = a;

    fp0 = floatx80_add(fp0, float32_to_floatx80(make_float32(0x3F800000),
                       status), status);

    aExp = extractFloatx80Exp(fp0);
    aSig = extractFloatx80Frac(fp0);

    compact = floatx80_make_compact(aExp, aSig);

    if (compact < 0x3FFE8000 || compact > 0x3FFFC000) {
        k = aExp - 0x3FFF;
        fp1 = int32_to_floatx80(k, status);

        fSig = (aSig & UINT64_C(0xFE00000000000000)) | UINT64_C(0x0100000000000000);
        j = (fSig >> 56) & 0x7E;

        f = packFloatx80(0, 0x3FFF, fSig);
        fp0 = packFloatx80(0, 0x3FFF, aSig);

        fp0 = floatx80_sub(fp0, f, status);

    lp1cont1:
        fp0 = floatx80_mul(fp0, log_tbl[j], status);
        logof2 = packFloatx80(0, 0x3FFE, UINT64_C(0xB17217F7D1CF79AC));
        klog2 = floatx80_mul(fp1, logof2, status);
        fp2 = floatx80_mul(fp0, fp0, status);

        fp3 = fp2;
        fp1 = fp2;

        fp1 = floatx80_mul(fp1, float64_to_floatx80(
                           make_float64(0x3FC2499AB5E4040B), status),
                           status);
        fp2 = floatx80_mul(fp2, float64_to_floatx80(
                           make_float64(0xBFC555B5848CB7DB), status),
                           status);
        fp1 = floatx80_add(fp1, float64_to_floatx80(
                           make_float64(0x3FC99999987D8730), status),
                           status);
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0xBFCFFFFFFF6F7E97), status),
                           status);
        fp1 = floatx80_mul(fp1, fp3, status);
        fp2 = floatx80_mul(fp2, fp3, status);
        fp1 = floatx80_add(fp1, float64_to_floatx80(
                           make_float64(0x3FD55555555555A4), status),
                           status);
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0xBFE0000000000008), status),
                           status);
        fp1 = floatx80_mul(fp1, fp3, status);
        fp2 = floatx80_mul(fp2, fp3, status);
        fp1 = floatx80_mul(fp1, fp0, status);
        fp0 = floatx80_add(fp0, fp2, status);

        fp1 = floatx80_add(fp1, log_tbl[j + 1],
                           status);
        fp0 = floatx80_add(fp0, fp1, status);

        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_add(fp0, klog2, status);

        float_raise(float_flag_inexact, status);

        return a;
    } else if (compact < 0x3FFEF07D || compact > 0x3FFF8841) {
        fSig = (aSig & UINT64_C(0xFE00000000000000)) | UINT64_C(0x0100000000000000);
        f = packFloatx80(0, 0x3FFF, fSig);
        j = (fSig >> 56) & 0x7E;

        if (compact >= 0x3FFF8000) {
            fp0 = floatx80_sub(float32_to_floatx80(make_float32(0x3F800000),
                               status), f, status);
            fp0 = floatx80_add(fp0, fp1, status);
            fp1 = packFloatx80(0, 0, 0);
        } else {
            fp0 = floatx80_sub(float32_to_floatx80(make_float32(0x40000000),
                               status), f, status);
            fp1 = floatx80_add(fp1, fp1, status);
            fp0 = floatx80_add(fp0, fp1, status);
            fp1 = packFloatx80(1, one_exp, one_sig);
        }
        goto lp1cont1;
    } else {
        fp1 = floatx80_add(fp1, fp1, status);
        fp0 = floatx80_add(fp0, float32_to_floatx80(make_float32(0x3F800000),
                           status), status);

        fp1 = floatx80_div(fp1, fp0, status);
        saveu = fp1;
        fp0 = floatx80_mul(fp1, fp1, status);
        fp1 = floatx80_mul(fp0, fp0, status);

        fp3 = float64_to_floatx80(make_float64(0x3F175496ADD7DAD6),
                                  status);
        fp2 = float64_to_floatx80(make_float64(0x3F3C71C2FE80C7E0),
                                  status);
        fp3 = floatx80_mul(fp3, fp1, status);
        fp2 = floatx80_mul(fp2, fp1, status);
        fp3 = floatx80_add(fp3, float64_to_floatx80(
                           make_float64(0x3F624924928BCCFF), status),
                           status);
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0x3F899999999995EC), status),
                           status);
        fp1 = floatx80_mul(fp1, fp3, status);
        fp2 = floatx80_mul(fp2, fp0, status);
        fp1 = floatx80_add(fp1, float64_to_floatx80(
                           make_float64(0x3FB5555555555555), status),
                           status);

        fp0 = floatx80_mul(fp0, saveu, status);
        fp1 = floatx80_add(fp1, fp2,
                           status);
        fp0 = floatx80_mul(fp0, fp1,
                           status);

        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_add(fp0, saveu, status);

            float_raise(float_flag_inexact, status);

        return a;
    }
}

floatx80 floatx80_logn(floatx80 a, float_status *status)
{
    bool aSign;
    int32_t aExp;
    uint64_t aSig, fSig;

    FloatRoundMode user_rnd_mode;
    FloatX80RoundPrec user_rnd_prec;

    int32_t compact, j, k, adjk;
    floatx80 fp0, fp1, fp2, fp3, f, logof2, klog2, saveu;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            propagateFloatx80NaNOneArg(a, status);
        }
        if (aSign == 0) {
            return floatx80_default_inf(0, status);
        }
    }

    adjk = 0;

    if (aExp == 0) {
        if (aSig == 0) {
            float_raise(float_flag_divbyzero, status);
            return floatx80_default_inf(1, status);
        }
        if ((aSig & one_sig) == 0) {
            normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
            adjk = -100;
            aExp += 100;
            a = packFloatx80(aSign, aExp, aSig);
        }
    }

    if (aSign) {
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = floatx80_precision_x;

    compact = floatx80_make_compact(aExp, aSig);

    if (compact < 0x3FFEF07D || compact > 0x3FFF8841) {
        k = aExp - 0x3FFF;
        k += adjk;
        fp1 = int32_to_floatx80(k, status);

        fSig = (aSig & UINT64_C(0xFE00000000000000)) | UINT64_C(0x0100000000000000);
        j = (fSig >> 56) & 0x7E;

        f = packFloatx80(0, 0x3FFF, fSig);
        fp0 = packFloatx80(0, 0x3FFF, aSig);

        fp0 = floatx80_sub(fp0, f, status);

        fp0 = floatx80_mul(fp0, log_tbl[j], status);
        logof2 = packFloatx80(0, 0x3FFE, UINT64_C(0xB17217F7D1CF79AC));
        klog2 = floatx80_mul(fp1, logof2, status);
        fp2 = floatx80_mul(fp0, fp0, status);

        fp3 = fp2;
        fp1 = fp2;

        fp1 = floatx80_mul(fp1, float64_to_floatx80(
                           make_float64(0x3FC2499AB5E4040B), status),
                           status);
        fp2 = floatx80_mul(fp2, float64_to_floatx80(
                           make_float64(0xBFC555B5848CB7DB), status),
                           status);
        fp1 = floatx80_add(fp1, float64_to_floatx80(
                           make_float64(0x3FC99999987D8730), status),
                           status);
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0xBFCFFFFFFF6F7E97), status),
                           status);
        fp1 = floatx80_mul(fp1, fp3, status);
        fp2 = floatx80_mul(fp2, fp3, status);
        fp1 = floatx80_add(fp1, float64_to_floatx80(
                           make_float64(0x3FD55555555555A4), status),
                           status);
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0xBFE0000000000008), status),
                           status);
        fp1 = floatx80_mul(fp1, fp3, status);
        fp2 = floatx80_mul(fp2, fp3, status);
        fp1 = floatx80_mul(fp1, fp0, status);
        fp0 = floatx80_add(fp0, fp2, status);

        fp1 = floatx80_add(fp1, log_tbl[j + 1],
                           status);
        fp0 = floatx80_add(fp0, fp1, status);

        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_add(fp0, klog2, status);

        float_raise(float_flag_inexact, status);

        return a;
    } else {
        fp0 = a;
        fp1 = a;
        fp1 = floatx80_sub(fp1, float32_to_floatx80(make_float32(0x3F800000),
                           status), status);
        fp0 = floatx80_add(fp0, float32_to_floatx80(make_float32(0x3F800000),
                           status), status);
        fp1 = floatx80_add(fp1, fp1, status);

        fp1 = floatx80_div(fp1, fp0, status);
        saveu = fp1;
        fp0 = floatx80_mul(fp1, fp1, status);
        fp1 = floatx80_mul(fp0, fp0, status);

        fp3 = float64_to_floatx80(make_float64(0x3F175496ADD7DAD6),
                                  status);
        fp2 = float64_to_floatx80(make_float64(0x3F3C71C2FE80C7E0),
                                  status);
        fp3 = floatx80_mul(fp3, fp1, status);
        fp2 = floatx80_mul(fp2, fp1, status);
        fp3 = floatx80_add(fp3, float64_to_floatx80(
                           make_float64(0x3F624924928BCCFF), status),
                           status);
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0x3F899999999995EC), status),
                           status);
        fp1 = floatx80_mul(fp1, fp3, status);
        fp2 = floatx80_mul(fp2, fp0, status);
        fp1 = floatx80_add(fp1, float64_to_floatx80(
                           make_float64(0x3FB5555555555555), status),
                           status);

        fp0 = floatx80_mul(fp0, saveu, status);
        fp1 = floatx80_add(fp1, fp2, status);
        fp0 = floatx80_mul(fp0, fp1,
                           status);

        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_add(fp0, saveu, status);

            float_raise(float_flag_inexact, status);

        return a;
    }
}

floatx80 floatx80_log10(floatx80 a, float_status *status)
{
    bool aSign;
    int32_t aExp;
    uint64_t aSig;

    FloatRoundMode user_rnd_mode;
    FloatX80RoundPrec user_rnd_prec;

    floatx80 fp0, fp1;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            propagateFloatx80NaNOneArg(a, status);
        }
        if (aSign == 0) {
            return floatx80_default_inf(0, status);
        }
    }

    if (aExp == 0 && aSig == 0) {
        float_raise(float_flag_divbyzero, status);
        return floatx80_default_inf(1, status);
    }

    if (aSign) {
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = floatx80_precision_x;

    fp0 = floatx80_logn(a, status);
    fp1 = packFloatx80(0, 0x3FFD, UINT64_C(0xDE5BD8A937287195));

    status->float_rounding_mode = user_rnd_mode;
    status->floatx80_rounding_precision = user_rnd_prec;

    a = floatx80_mul(fp0, fp1, status);

    float_raise(float_flag_inexact, status);

    return a;
}

floatx80 floatx80_log2(floatx80 a, float_status *status)
{
    bool aSign;
    int32_t aExp;
    uint64_t aSig;

    FloatRoundMode user_rnd_mode;
    FloatX80RoundPrec user_rnd_prec;

    floatx80 fp0, fp1;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            propagateFloatx80NaNOneArg(a, status);
        }
        if (aSign == 0) {
            return floatx80_default_inf(0, status);
        }
    }

    if (aExp == 0) {
        if (aSig == 0) {
            float_raise(float_flag_divbyzero, status);
            return floatx80_default_inf(1, status);
        }
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }

    if (aSign) {
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = floatx80_precision_x;

    if (aSig == one_sig) {
        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = int32_to_floatx80(aExp - 0x3FFF, status);
    } else {
        fp0 = floatx80_logn(a, status);
        fp1 = packFloatx80(0, 0x3FFF, UINT64_C(0xB8AA3B295C17F0BC));

        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_mul(fp0, fp1, status);
    }

    float_raise(float_flag_inexact, status);

    return a;
}

floatx80 floatx80_etox(floatx80 a, float_status *status)
{
    bool aSign;
    int32_t aExp;
    uint64_t aSig;

    FloatRoundMode user_rnd_mode;
    FloatX80RoundPrec user_rnd_prec;

    int32_t compact, n, j, k, m, m1;
    floatx80 fp0, fp1, fp2, fp3, l2, scale, adjscale;
    bool adjflag;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a, status);
        }
        if (aSign) {
            return packFloatx80(0, 0, 0);
        }
        return floatx80_default_inf(0, status);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(0, one_exp, one_sig);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = floatx80_precision_x;

    adjflag = 0;

    if (aExp >= 0x3FBE) {
        compact = floatx80_make_compact(aExp, aSig);

        if (compact < 0x400CB167) {
            fp0 = a;
            fp1 = a;
            fp0 = floatx80_mul(fp0, float32_to_floatx80(
                               make_float32(0x42B8AA3B), status),
                               status);
            adjflag = 0;
            n = floatx80_to_int32(fp0, status);
            fp0 = int32_to_floatx80(n, status);

            j = n & 0x3F;
            m = n / 64;
            if (n < 0 && j) {
                m--;
            }
            m += 0x3FFF;

        expcont1:
            fp2 = fp0;
            fp0 = floatx80_mul(fp0, float32_to_floatx80(
                               make_float32(0xBC317218), status),
                               status);
            l2 = packFloatx80(0, 0x3FDC, UINT64_C(0x82E308654361C4C6));
            fp2 = floatx80_mul(fp2, l2, status);
            fp0 = floatx80_add(fp0, fp1, status);
            fp0 = floatx80_add(fp0, fp2, status);

            fp1 = floatx80_mul(fp0, fp0, status);
            fp2 = float32_to_floatx80(make_float32(0x3AB60B70),
                                      status);
            fp2 = floatx80_mul(fp2, fp1, status);
            fp3 = floatx80_mul(float32_to_floatx80(make_float32(0x3C088895),
                               status), fp1,
                               status);
            fp2 = floatx80_add(fp2, float64_to_floatx80(make_float64(
                               0x3FA5555555554431), status),
                               status);
            fp3 = floatx80_add(fp3, float64_to_floatx80(make_float64(
                               0x3FC5555555554018), status),
                               status);
            fp2 = floatx80_mul(fp2, fp1, status);
            fp3 = floatx80_mul(fp3, fp1, status);
            fp2 = floatx80_add(fp2, float32_to_floatx80(
                               make_float32(0x3F000000), status),
                               status);
            fp3 = floatx80_mul(fp3, fp0, status);
            fp2 = floatx80_mul(fp2, fp1,
                               status);
            fp0 = floatx80_add(fp0, fp3, status);
            fp0 = floatx80_add(fp0, fp2, status);

            fp1 = exp_tbl[j];
            fp0 = floatx80_mul(fp0, fp1, status);
            fp0 = floatx80_add(fp0, float32_to_floatx80(exp_tbl2[j], status),
                               status);
            fp0 = floatx80_add(fp0, fp1,
                               status);

            scale = packFloatx80(0, m, one_sig);
            if (adjflag) {
                adjscale = packFloatx80(0, m1, one_sig);
                fp0 = floatx80_mul(fp0, adjscale, status);
            }

            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_mul(fp0, scale, status);

            float_raise(float_flag_inexact, status);

            return a;
        } else {
            if (compact > 0x400CB27C) {
                status->float_rounding_mode = user_rnd_mode;
                status->floatx80_rounding_precision = user_rnd_prec;
                if (aSign) {
                    a = roundAndPackFloatx80(
                                           status->floatx80_rounding_precision,
                                           0, -0x1000, aSig, 0, status);
                } else {
                    a = roundAndPackFloatx80(
                                           status->floatx80_rounding_precision,
                                           0, 0x8000, aSig, 0, status);
                }
                float_raise(float_flag_inexact, status);

                return a;
            } else {
                fp0 = a;
                fp1 = a;
                fp0 = floatx80_mul(fp0, float32_to_floatx80(
                                   make_float32(0x42B8AA3B), status),
                                   status);
                adjflag = 1;
                n = floatx80_to_int32(fp0, status);
                fp0 = int32_to_floatx80(n, status);

                j = n & 0x3F;
                k = n / 64;
                if (n < 0 && j) {
                    k--;
                }
                m1 = k / 2;
                if (k < 0 && (k & 1)) {
                    m1--;
                }
                m = k - m1;
                m1 += 0x3FFF;
                m += 0x3FFF;

                goto expcont1;
            }
        }
    } else {
        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_add(a, float32_to_floatx80(make_float32(0x3F800000),
                         status), status);

        float_raise(float_flag_inexact, status);

        return a;
    }
}

floatx80 floatx80_twotox(floatx80 a, float_status *status)
{
    bool aSign;
    int32_t aExp;
    uint64_t aSig;

    FloatRoundMode user_rnd_mode;
    FloatX80RoundPrec user_rnd_prec;

    int32_t compact, n, j, l, m, m1;
    floatx80 fp0, fp1, fp2, fp3, adjfact, fact1, fact2;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a, status);
        }
        if (aSign) {
            return packFloatx80(0, 0, 0);
        }
        return floatx80_default_inf(0, status);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(0, one_exp, one_sig);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = floatx80_precision_x;

    fp0 = a;

    compact = floatx80_make_compact(aExp, aSig);

    if (compact < 0x3FB98000 || compact > 0x400D80C0) {
        if (compact > 0x3FFF8000) {
            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            if (aSign) {
                return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                            0, -0x1000, aSig, 0, status);
            } else {
                return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                            0, 0x8000, aSig, 0, status);
            }
        } else {
            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_add(fp0, float32_to_floatx80(
                             make_float32(0x3F800000), status),
                             status);

            float_raise(float_flag_inexact, status);

            return a;
        }
    } else {
        fp1 = fp0;
        fp1 = floatx80_mul(fp1, float32_to_floatx80(
                           make_float32(0x42800000), status),
                           status);
        n = floatx80_to_int32(fp1, status);
        fp1 = int32_to_floatx80(n, status);
        j = n & 0x3F;
        l = n / 64;
        if (n < 0 && j) {
            l--;
        }
        m = l / 2;
        if (l < 0 && (l & 1)) {
            m--;
        }
        m1 = l - m;
        m1 += 0x3FFF;

        adjfact = packFloatx80(0, m1, one_sig);
        fact1 = exp2_tbl[j];
        fact1.high += m;
        fact2.high = exp2_tbl2[j] >> 16;
        fact2.high += m;
        fact2.low = (uint64_t)(exp2_tbl2[j] & 0xFFFF);
        fact2.low <<= 48;

        fp1 = floatx80_mul(fp1, float32_to_floatx80(
                           make_float32(0x3C800000), status),
                           status);
        fp0 = floatx80_sub(fp0, fp1, status);
        fp2 = packFloatx80(0, 0x3FFE, UINT64_C(0xB17217F7D1CF79AC));
        fp0 = floatx80_mul(fp0, fp2, status);

        fp1 = floatx80_mul(fp0, fp0, status);
        fp2 = float64_to_floatx80(make_float64(0x3F56C16D6F7BD0B2),
                                  status);
        fp3 = float64_to_floatx80(make_float64(0x3F811112302C712C),
                                  status);
        fp2 = floatx80_mul(fp2, fp1, status);
        fp3 = floatx80_mul(fp3, fp1, status);
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0x3FA5555555554CC1), status),
                           status);
        fp3 = floatx80_add(fp3, float64_to_floatx80(
                           make_float64(0x3FC5555555554A54), status),
                           status);
        fp2 = floatx80_mul(fp2, fp1, status);
        fp3 = floatx80_mul(fp3, fp1, status);
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0x3FE0000000000000), status),
                           status);
        fp3 = floatx80_mul(fp3, fp0, status);

        fp2 = floatx80_mul(fp2, fp1, status);
        fp0 = floatx80_add(fp0, fp3, status);
        fp0 = floatx80_add(fp0, fp2, status);

        fp0 = floatx80_mul(fp0, fact1, status);
        fp0 = floatx80_add(fp0, fact2, status);
        fp0 = floatx80_add(fp0, fact1, status);

        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_mul(fp0, adjfact, status);

        float_raise(float_flag_inexact, status);

        return a;
    }
}

floatx80 floatx80_tentox(floatx80 a, float_status *status)
{
    bool aSign;
    int32_t aExp;
    uint64_t aSig;

    FloatRoundMode user_rnd_mode;
    FloatX80RoundPrec user_rnd_prec;

    int32_t compact, n, j, l, m, m1;
    floatx80 fp0, fp1, fp2, fp3, adjfact, fact1, fact2;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a, status);
        }
        if (aSign) {
            return packFloatx80(0, 0, 0);
        }
        return floatx80_default_inf(0, status);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(0, one_exp, one_sig);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = floatx80_precision_x;

    fp0 = a;

    compact = floatx80_make_compact(aExp, aSig);

    if (compact < 0x3FB98000 || compact > 0x400B9B07) {
        if (compact > 0x3FFF8000) {
            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            if (aSign) {
                return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                            0, -0x1000, aSig, 0, status);
            } else {
                return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                            0, 0x8000, aSig, 0, status);
            }
        } else {
            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_add(fp0, float32_to_floatx80(
                             make_float32(0x3F800000), status),
                             status);

            float_raise(float_flag_inexact, status);

            return a;
        }
    } else {
        fp1 = fp0;
        fp1 = floatx80_mul(fp1, float64_to_floatx80(
                           make_float64(0x406A934F0979A371),
                           status), status);
        n = floatx80_to_int32(fp1, status);
        fp1 = int32_to_floatx80(n, status);

        j = n & 0x3F;
        l = n / 64;
        if (n < 0 && j) {
            l--;
        }
        m = l / 2;
        if (l < 0 && (l & 1)) {
            m--;
        }
        m1 = l - m;
        m1 += 0x3FFF;

        adjfact = packFloatx80(0, m1, one_sig);
        fact1 = exp2_tbl[j];
        fact1.high += m;
        fact2.high = exp2_tbl2[j] >> 16;
        fact2.high += m;
        fact2.low = (uint64_t)(exp2_tbl2[j] & 0xFFFF);
        fact2.low <<= 48;

        fp2 = fp1;
        fp1 = floatx80_mul(fp1, float64_to_floatx80(
                           make_float64(0x3F734413509F8000), status),
                           status);
        fp3 = packFloatx80(1, 0x3FCD, UINT64_C(0xC0219DC1DA994FD2));
        fp2 = floatx80_mul(fp2, fp3, status);
        fp0 = floatx80_sub(fp0, fp1, status);
        fp0 = floatx80_sub(fp0, fp2, status);
        fp2 = packFloatx80(0, 0x4000, UINT64_C(0x935D8DDDAAA8AC17));
        fp0 = floatx80_mul(fp0, fp2, status);

        fp1 = floatx80_mul(fp0, fp0, status);
        fp2 = float64_to_floatx80(make_float64(0x3F56C16D6F7BD0B2),
                                  status);
        fp3 = float64_to_floatx80(make_float64(0x3F811112302C712C),
                                  status);
        fp2 = floatx80_mul(fp2, fp1, status);
        fp3 = floatx80_mul(fp3, fp1, status);
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0x3FA5555555554CC1), status),
                           status);
        fp3 = floatx80_add(fp3, float64_to_floatx80(
                           make_float64(0x3FC5555555554A54), status),
                           status);
        fp2 = floatx80_mul(fp2, fp1, status);
        fp3 = floatx80_mul(fp3, fp1, status);
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0x3FE0000000000000), status),
                           status);
        fp3 = floatx80_mul(fp3, fp0, status);

        fp2 = floatx80_mul(fp2, fp1, status);
        fp0 = floatx80_add(fp0, fp3, status);
        fp0 = floatx80_add(fp0, fp2, status);

        fp0 = floatx80_mul(fp0, fact1, status);
        fp0 = floatx80_add(fp0, fact2, status);
        fp0 = floatx80_add(fp0, fact1, status);

        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_mul(fp0, adjfact, status);

        float_raise(float_flag_inexact, status);

        return a;
    }
}

floatx80 floatx80_tan(floatx80 a, float_status *status)
{
    bool aSign, xSign;
    int32_t aExp, xExp;
    uint64_t aSig, xSig;

    FloatRoundMode user_rnd_mode;
    FloatX80RoundPrec user_rnd_prec;

    int32_t compact, l, n, j;
    floatx80 fp0, fp1, fp2, fp3, fp4, fp5, invtwopi, twopi1, twopi2;
    float32 twoto63;
    bool endflag;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a, status);
        }
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(aSign, 0, 0);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = floatx80_precision_x;

    compact = floatx80_make_compact(aExp, aSig);

    fp0 = a;

    if (compact < 0x3FD78000 || compact > 0x4004BC7E) {
        if (compact > 0x3FFF8000) {
            fp1 = packFloatx80(0, 0, 0);
            if (compact == 0x7FFEFFFF) {
                twopi1 = packFloatx80(aSign ^ 1, 0x7FFE,
                                      UINT64_C(0xC90FDAA200000000));
                twopi2 = packFloatx80(aSign ^ 1, 0x7FDC,
                                      UINT64_C(0x85A308D300000000));
                fp0 = floatx80_add(fp0, twopi1, status);
                fp1 = fp0;
                fp0 = floatx80_add(fp0, twopi2, status);
                fp1 = floatx80_sub(fp1, fp0, status);
                fp1 = floatx80_add(fp1, twopi2, status);
            }
        loop:
            xSign = extractFloatx80Sign(fp0);
            xExp = extractFloatx80Exp(fp0);
            xExp -= 0x3FFF;
            if (xExp <= 28) {
                l = 0;
                endflag = true;
            } else {
                l = xExp - 27;
                endflag = false;
            }
            invtwopi = packFloatx80(0, 0x3FFE - l,
                                    UINT64_C(0xA2F9836E4E44152A));
            twopi1 = packFloatx80(0, 0x3FFF + l, UINT64_C(0xC90FDAA200000000));
            twopi2 = packFloatx80(0, 0x3FDD + l, UINT64_C(0x85A308D300000000));

            twoto63 = packFloat32(xSign, 0xBE, 0);

            fp2 = floatx80_mul(fp0, invtwopi, status);
            fp2 = floatx80_add(fp2, float32_to_floatx80(twoto63, status),
                               status);
            fp2 = floatx80_sub(fp2, float32_to_floatx80(twoto63, status),
                               status);
            fp4 = floatx80_mul(twopi1, fp2, status);
            fp5 = floatx80_mul(twopi2, fp2, status);
            fp3 = floatx80_add(fp4, fp5, status);
            fp4 = floatx80_sub(fp4, fp3, status);
            fp0 = floatx80_sub(fp0, fp3, status);
            fp4 = floatx80_add(fp4, fp5, status);
            fp3 = fp0;
            fp1 = floatx80_sub(fp1, fp4, status);
            fp0 = floatx80_add(fp0, fp1, status);

            if (endflag) {
                n = floatx80_to_int32(fp2, status);
                goto tancont;
            }
            fp3 = floatx80_sub(fp3, fp0, status);
            fp1 = floatx80_add(fp1, fp3, status);
            goto loop;
        } else {
            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_move(a, status);

            float_raise(float_flag_inexact, status);

            return a;
        }
    } else {
        fp1 = floatx80_mul(fp0, float64_to_floatx80(
                           make_float64(0x3FE45F306DC9C883), status),
                           status);

        n = floatx80_to_int32(fp1, status);
        j = 32 + n;

        fp0 = floatx80_sub(fp0, pi_tbl[j], status);
        fp0 = floatx80_sub(fp0, float32_to_floatx80(pi_tbl2[j], status),
                           status);

    tancont:
        if (n & 1) {
            fp1 = fp0;
            fp0 = floatx80_mul(fp0, fp0, status);
            fp3 = float64_to_floatx80(make_float64(0x3EA0B759F50F8688),
                                      status);
            fp2 = float64_to_floatx80(make_float64(0xBEF2BAA5A8924F04),
                                      status);
            fp3 = floatx80_mul(fp3, fp0, status);
            fp2 = floatx80_mul(fp2, fp0, status);
            fp3 = floatx80_add(fp3, float64_to_floatx80(
                               make_float64(0xBF346F59B39BA65F), status),
                               status);
            fp4 = packFloatx80(0, 0x3FF6, UINT64_C(0xE073D3FC199C4A00));
            fp2 = floatx80_add(fp2, fp4, status);
            fp3 = floatx80_mul(fp3, fp0, status);
            fp2 = floatx80_mul(fp2, fp0, status);
            fp4 = packFloatx80(0, 0x3FF9, UINT64_C(0xD23CD68415D95FA1));
            fp3 = floatx80_add(fp3, fp4, status);
            fp4 = packFloatx80(1, 0x3FFC, UINT64_C(0x8895A6C5FB423BCA));
            fp2 = floatx80_add(fp2, fp4, status);
            fp3 = floatx80_mul(fp3, fp0, status);
            fp2 = floatx80_mul(fp2, fp0, status);
            fp4 = packFloatx80(1, 0x3FFD, UINT64_C(0xEEF57E0DA84BC8CE));
            fp3 = floatx80_add(fp3, fp4, status);
            fp2 = floatx80_mul(fp2, fp1, status);
            fp0 = floatx80_mul(fp0, fp3, status);
            fp1 = floatx80_add(fp1, fp2, status);
            fp0 = floatx80_add(fp0, float32_to_floatx80(
                               make_float32(0x3F800000), status),
                               status);

            xSign = extractFloatx80Sign(fp1);
            xExp = extractFloatx80Exp(fp1);
            xSig = extractFloatx80Frac(fp1);
            xSign ^= 1;
            fp1 = packFloatx80(xSign, xExp, xSig);

            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_div(fp0, fp1, status);

            float_raise(float_flag_inexact, status);

            return a;
        } else {
            fp1 = floatx80_mul(fp0, fp0, status);
            fp3 = float64_to_floatx80(make_float64(0x3EA0B759F50F8688),
                                      status);
            fp2 = float64_to_floatx80(make_float64(0xBEF2BAA5A8924F04),
                                      status);
            fp3 = floatx80_mul(fp3, fp1, status);
            fp2 = floatx80_mul(fp2, fp1, status);
            fp3 = floatx80_add(fp3, float64_to_floatx80(
                               make_float64(0xBF346F59B39BA65F), status),
                               status);
            fp4 = packFloatx80(0, 0x3FF6, UINT64_C(0xE073D3FC199C4A00));
            fp2 = floatx80_add(fp2, fp4, status);
            fp3 = floatx80_mul(fp3, fp1, status);
            fp2 = floatx80_mul(fp2, fp1, status);
            fp4 = packFloatx80(0, 0x3FF9, UINT64_C(0xD23CD68415D95FA1));
            fp3 = floatx80_add(fp3, fp4, status);
            fp4 = packFloatx80(1, 0x3FFC, UINT64_C(0x8895A6C5FB423BCA));
            fp2 = floatx80_add(fp2, fp4, status);
            fp3 = floatx80_mul(fp3, fp1, status);
            fp2 = floatx80_mul(fp2, fp1, status);
            fp4 = packFloatx80(1, 0x3FFD, UINT64_C(0xEEF57E0DA84BC8CE));
            fp3 = floatx80_add(fp3, fp4, status);
            fp2 = floatx80_mul(fp2, fp0, status);
            fp1 = floatx80_mul(fp1, fp3, status);
            fp0 = floatx80_add(fp0, fp2, status);
            fp1 = floatx80_add(fp1, float32_to_floatx80(
                               make_float32(0x3F800000), status),
                               status);

            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_div(fp0, fp1, status);

            float_raise(float_flag_inexact, status);

            return a;
        }
    }
}

floatx80 floatx80_sin(floatx80 a, float_status *status)
{
    bool aSign, xSign;
    int32_t aExp, xExp;
    uint64_t aSig, xSig;

    FloatRoundMode user_rnd_mode;
    FloatX80RoundPrec user_rnd_prec;

    int32_t compact, l, n, j;
    floatx80 fp0, fp1, fp2, fp3, fp4, fp5, x, invtwopi, twopi1, twopi2;
    float32 posneg1, twoto63;
    bool endflag;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a, status);
        }
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(aSign, 0, 0);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = floatx80_precision_x;

    compact = floatx80_make_compact(aExp, aSig);

    fp0 = a;

    if (compact < 0x3FD78000 || compact > 0x4004BC7E) {
        if (compact > 0x3FFF8000) {
            fp1 = packFloatx80(0, 0, 0);
            if (compact == 0x7FFEFFFF) {
                twopi1 = packFloatx80(aSign ^ 1, 0x7FFE,
                                      UINT64_C(0xC90FDAA200000000));
                twopi2 = packFloatx80(aSign ^ 1, 0x7FDC,
                                      UINT64_C(0x85A308D300000000));
                fp0 = floatx80_add(fp0, twopi1, status);
                fp1 = fp0;
                fp0 = floatx80_add(fp0, twopi2, status);
                fp1 = floatx80_sub(fp1, fp0, status);
                fp1 = floatx80_add(fp1, twopi2, status);
            }
        loop:
            xSign = extractFloatx80Sign(fp0);
            xExp = extractFloatx80Exp(fp0);
            xExp -= 0x3FFF;
            if (xExp <= 28) {
                l = 0;
                endflag = true;
            } else {
                l = xExp - 27;
                endflag = false;
            }
            invtwopi = packFloatx80(0, 0x3FFE - l,
                                    UINT64_C(0xA2F9836E4E44152A));
            twopi1 = packFloatx80(0, 0x3FFF + l, UINT64_C(0xC90FDAA200000000));
            twopi2 = packFloatx80(0, 0x3FDD + l, UINT64_C(0x85A308D300000000));

            twoto63 = packFloat32(xSign, 0xBE, 0);

            fp2 = floatx80_mul(fp0, invtwopi, status);
            fp2 = floatx80_add(fp2, float32_to_floatx80(twoto63, status),
                               status);
            fp2 = floatx80_sub(fp2, float32_to_floatx80(twoto63, status),
                               status);
            fp4 = floatx80_mul(twopi1, fp2, status);
            fp5 = floatx80_mul(twopi2, fp2, status);
            fp3 = floatx80_add(fp4, fp5, status);
            fp4 = floatx80_sub(fp4, fp3, status);
            fp0 = floatx80_sub(fp0, fp3, status);
            fp4 = floatx80_add(fp4, fp5, status);
            fp3 = fp0;
            fp1 = floatx80_sub(fp1, fp4, status);
            fp0 = floatx80_add(fp0, fp1, status);

            if (endflag) {
                n = floatx80_to_int32(fp2, status);
                goto sincont;
            }
            fp3 = floatx80_sub(fp3, fp0, status);
            fp1 = floatx80_add(fp1, fp3, status);
            goto loop;
        } else {
            fp0 = float32_to_floatx80(make_float32(0x3F800000),
                                      status);

            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_move(a, status);
            float_raise(float_flag_inexact, status);

            return a;
        }
    } else {
        fp1 = floatx80_mul(fp0, float64_to_floatx80(
                           make_float64(0x3FE45F306DC9C883), status),
                           status);

        n = floatx80_to_int32(fp1, status);
        j = 32 + n;

        fp0 = floatx80_sub(fp0, pi_tbl[j], status);
        fp0 = floatx80_sub(fp0, float32_to_floatx80(pi_tbl2[j], status),
                           status);

    sincont:
        if (n & 1) {
            fp0 = floatx80_mul(fp0, fp0, status);
            fp1 = floatx80_mul(fp0, fp0, status);
            fp2 = float64_to_floatx80(make_float64(0x3D2AC4D0D6011EE3),
                                      status);
            fp3 = float64_to_floatx80(make_float64(0xBDA9396F9F45AC19),
                                      status);

            xSign = extractFloatx80Sign(fp0);
            xExp = extractFloatx80Exp(fp0);
            xSig = extractFloatx80Frac(fp0);

            if ((n >> 1) & 1) {
                xSign ^= 1;
                posneg1 = make_float32(0xBF800000);
            } else {
                xSign ^= 0;
                posneg1 = make_float32(0x3F800000);
            }

            fp2 = floatx80_mul(fp2, fp1, status);
            fp3 = floatx80_mul(fp3, fp1, status);
            fp2 = floatx80_add(fp2, float64_to_floatx80(
                               make_float64(0x3E21EED90612C972), status),
                               status);
            fp3 = floatx80_add(fp3, float64_to_floatx80(
                               make_float64(0xBE927E4FB79D9FCF), status),
                               status);
            fp2 = floatx80_mul(fp2, fp1, status);
            fp3 = floatx80_mul(fp3, fp1, status);
            fp2 = floatx80_add(fp2, float64_to_floatx80(
                               make_float64(0x3EFA01A01A01D423), status),
                               status);
            fp4 = packFloatx80(1, 0x3FF5, UINT64_C(0xB60B60B60B61D438));
            fp3 = floatx80_add(fp3, fp4, status);
            fp2 = floatx80_mul(fp2, fp1, status);
            fp1 = floatx80_mul(fp1, fp3, status);
            fp4 = packFloatx80(0, 0x3FFA, UINT64_C(0xAAAAAAAAAAAAAB5E));
            fp2 = floatx80_add(fp2, fp4, status);
            fp1 = floatx80_add(fp1, float32_to_floatx80(
                               make_float32(0xBF000000), status),
                               status);
            fp0 = floatx80_mul(fp0, fp2, status);
            fp0 = floatx80_add(fp0, fp1, status);

            x = packFloatx80(xSign, xExp, xSig);
            fp0 = floatx80_mul(fp0, x, status);

            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_add(fp0, float32_to_floatx80(posneg1, status), status);

            float_raise(float_flag_inexact, status);

            return a;
        } else {
            xSign = extractFloatx80Sign(fp0);
            xExp = extractFloatx80Exp(fp0);
            xSig = extractFloatx80Frac(fp0);

            xSign ^= (n >> 1) & 1;

            fp0 = floatx80_mul(fp0, fp0, status);
            fp1 = floatx80_mul(fp0, fp0, status);
            fp3 = float64_to_floatx80(make_float64(0xBD6AAA77CCC994F5),
                                      status);
            fp2 = float64_to_floatx80(make_float64(0x3DE612097AAE8DA1),
                                      status);
            fp3 = floatx80_mul(fp3, fp1, status);
            fp2 = floatx80_mul(fp2, fp1, status);
            fp3 = floatx80_add(fp3, float64_to_floatx80(
                               make_float64(0xBE5AE6452A118AE4), status),
                               status);
            fp2 = floatx80_add(fp2, float64_to_floatx80(
                               make_float64(0x3EC71DE3A5341531), status),
                               status);
            fp3 = floatx80_mul(fp3, fp1, status);
            fp2 = floatx80_mul(fp2, fp1, status);
            fp3 = floatx80_add(fp3, float64_to_floatx80(
                               make_float64(0xBF2A01A01A018B59), status),
                               status);
            fp4 = packFloatx80(0, 0x3FF8, UINT64_C(0x88888888888859AF));
            fp2 = floatx80_add(fp2, fp4, status);
            fp1 = floatx80_mul(fp1, fp3, status);
            fp2 = floatx80_mul(fp2, fp0, status);
            fp4 = packFloatx80(1, 0x3FFC, UINT64_C(0xAAAAAAAAAAAAAA99));
            fp1 = floatx80_add(fp1, fp4, status);
            fp1 = floatx80_add(fp1, fp2,
                               status);

            x = packFloatx80(xSign, xExp, xSig);
            fp0 = floatx80_mul(fp0, x, status);
            fp0 = floatx80_mul(fp0, fp1, status);

            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_add(fp0, x, status);

            float_raise(float_flag_inexact, status);

            return a;
        }
    }
}

floatx80 floatx80_cos(floatx80 a, float_status *status)
{
    bool aSign, xSign;
    int32_t aExp, xExp;
    uint64_t aSig, xSig;

    FloatRoundMode user_rnd_mode;
    FloatX80RoundPrec user_rnd_prec;

    int32_t compact, l, n, j;
    floatx80 fp0, fp1, fp2, fp3, fp4, fp5, x, invtwopi, twopi1, twopi2;
    float32 posneg1, twoto63;
    bool endflag;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a, status);
        }
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(0, one_exp, one_sig);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = floatx80_precision_x;

    compact = floatx80_make_compact(aExp, aSig);

    fp0 = a;

    if (compact < 0x3FD78000 || compact > 0x4004BC7E) {
        if (compact > 0x3FFF8000) {
            fp1 = packFloatx80(0, 0, 0);
            if (compact == 0x7FFEFFFF) {
                twopi1 = packFloatx80(aSign ^ 1, 0x7FFE,
                                      UINT64_C(0xC90FDAA200000000));
                twopi2 = packFloatx80(aSign ^ 1, 0x7FDC,
                                      UINT64_C(0x85A308D300000000));
                fp0 = floatx80_add(fp0, twopi1, status);
                fp1 = fp0;
                fp0 = floatx80_add(fp0, twopi2, status);
                fp1 = floatx80_sub(fp1, fp0, status);
                fp1 = floatx80_add(fp1, twopi2, status);
            }
        loop:
            xSign = extractFloatx80Sign(fp0);
            xExp = extractFloatx80Exp(fp0);
            xExp -= 0x3FFF;
            if (xExp <= 28) {
                l = 0;
                endflag = true;
            } else {
                l = xExp - 27;
                endflag = false;
            }
            invtwopi = packFloatx80(0, 0x3FFE - l,
                                    UINT64_C(0xA2F9836E4E44152A));
            twopi1 = packFloatx80(0, 0x3FFF + l, UINT64_C(0xC90FDAA200000000));
            twopi2 = packFloatx80(0, 0x3FDD + l, UINT64_C(0x85A308D300000000));

            twoto63 = packFloat32(xSign, 0xBE, 0);

            fp2 = floatx80_mul(fp0, invtwopi, status);
            fp2 = floatx80_add(fp2, float32_to_floatx80(twoto63, status),
                               status);
            fp2 = floatx80_sub(fp2, float32_to_floatx80(twoto63, status),
                               status);
            fp4 = floatx80_mul(twopi1, fp2, status);
            fp5 = floatx80_mul(twopi2, fp2, status);
            fp3 = floatx80_add(fp4, fp5, status);
            fp4 = floatx80_sub(fp4, fp3, status);
            fp0 = floatx80_sub(fp0, fp3, status);
            fp4 = floatx80_add(fp4, fp5, status);
            fp3 = fp0;
            fp1 = floatx80_sub(fp1, fp4, status);
            fp0 = floatx80_add(fp0, fp1, status);

            if (endflag) {
                n = floatx80_to_int32(fp2, status);
                goto sincont;
            }
            fp3 = floatx80_sub(fp3, fp0, status);
            fp1 = floatx80_add(fp1, fp3, status);
            goto loop;
        } else {
            fp0 = float32_to_floatx80(make_float32(0x3F800000), status);

            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_sub(fp0, float32_to_floatx80(
                             make_float32(0x00800000), status),
                             status);
            float_raise(float_flag_inexact, status);

            return a;
        }
    } else {
        fp1 = floatx80_mul(fp0, float64_to_floatx80(
                           make_float64(0x3FE45F306DC9C883), status),
                           status);

        n = floatx80_to_int32(fp1, status);
        j = 32 + n;

        fp0 = floatx80_sub(fp0, pi_tbl[j], status);
        fp0 = floatx80_sub(fp0, float32_to_floatx80(pi_tbl2[j], status),
                           status);

    sincont:
        if ((n + 1) & 1) {
            fp0 = floatx80_mul(fp0, fp0, status);
            fp1 = floatx80_mul(fp0, fp0, status);
            fp2 = float64_to_floatx80(make_float64(0x3D2AC4D0D6011EE3),
                                      status);
            fp3 = float64_to_floatx80(make_float64(0xBDA9396F9F45AC19),
                                      status);

            xSign = extractFloatx80Sign(fp0);
            xExp = extractFloatx80Exp(fp0);
            xSig = extractFloatx80Frac(fp0);

            if (((n + 1) >> 1) & 1) {
                xSign ^= 1;
                posneg1 = make_float32(0xBF800000);
            } else {
                xSign ^= 0;
                posneg1 = make_float32(0x3F800000);
            }

            fp2 = floatx80_mul(fp2, fp1, status);
            fp3 = floatx80_mul(fp3, fp1, status);
            fp2 = floatx80_add(fp2, float64_to_floatx80(
                               make_float64(0x3E21EED90612C972), status),
                               status);
            fp3 = floatx80_add(fp3, float64_to_floatx80(
                               make_float64(0xBE927E4FB79D9FCF), status),
                               status);
            fp2 = floatx80_mul(fp2, fp1, status);
            fp3 = floatx80_mul(fp3, fp1, status);
            fp2 = floatx80_add(fp2, float64_to_floatx80(
                               make_float64(0x3EFA01A01A01D423), status),
                               status);
            fp4 = packFloatx80(1, 0x3FF5, UINT64_C(0xB60B60B60B61D438));
            fp3 = floatx80_add(fp3, fp4, status);
            fp2 = floatx80_mul(fp2, fp1, status);
            fp1 = floatx80_mul(fp1, fp3, status);
            fp4 = packFloatx80(0, 0x3FFA, UINT64_C(0xAAAAAAAAAAAAAB5E));
            fp2 = floatx80_add(fp2, fp4, status);
            fp1 = floatx80_add(fp1, float32_to_floatx80(
                               make_float32(0xBF000000), status),
                               status);
            fp0 = floatx80_mul(fp0, fp2, status);
            fp0 = floatx80_add(fp0, fp1, status);

            x = packFloatx80(xSign, xExp, xSig);
            fp0 = floatx80_mul(fp0, x, status);

            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_add(fp0, float32_to_floatx80(posneg1, status), status);

            float_raise(float_flag_inexact, status);

            return a;
        } else {
            xSign = extractFloatx80Sign(fp0);
            xExp = extractFloatx80Exp(fp0);
            xSig = extractFloatx80Frac(fp0);

            xSign ^= ((n + 1) >> 1) & 1;

            fp0 = floatx80_mul(fp0, fp0, status);
            fp1 = floatx80_mul(fp0, fp0, status);
            fp3 = float64_to_floatx80(make_float64(0xBD6AAA77CCC994F5),
                                      status);
            fp2 = float64_to_floatx80(make_float64(0x3DE612097AAE8DA1),
                                      status);
            fp3 = floatx80_mul(fp3, fp1, status);
            fp2 = floatx80_mul(fp2, fp1, status);
            fp3 = floatx80_add(fp3, float64_to_floatx80(
                               make_float64(0xBE5AE6452A118AE4), status),
                               status);
            fp2 = floatx80_add(fp2, float64_to_floatx80(
                               make_float64(0x3EC71DE3A5341531), status),
                               status);
            fp3 = floatx80_mul(fp3, fp1, status);
            fp2 = floatx80_mul(fp2, fp1, status);
            fp3 = floatx80_add(fp3, float64_to_floatx80(
                               make_float64(0xBF2A01A01A018B59), status),
                               status);
            fp4 = packFloatx80(0, 0x3FF8, UINT64_C(0x88888888888859AF));
            fp2 = floatx80_add(fp2, fp4, status);
            fp1 = floatx80_mul(fp1, fp3, status);
            fp2 = floatx80_mul(fp2, fp0, status);
            fp4 = packFloatx80(1, 0x3FFC, UINT64_C(0xAAAAAAAAAAAAAA99));
            fp1 = floatx80_add(fp1, fp4, status);
            fp1 = floatx80_add(fp1, fp2, status);

            x = packFloatx80(xSign, xExp, xSig);
            fp0 = floatx80_mul(fp0, x, status);
            fp0 = floatx80_mul(fp0, fp1, status);

            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_add(fp0, x, status);

            float_raise(float_flag_inexact, status);

            return a;
        }
    }
}

floatx80 floatx80_atan(floatx80 a, float_status *status)
{
    bool aSign;
    int32_t aExp;
    uint64_t aSig;

    FloatRoundMode user_rnd_mode;
    FloatX80RoundPrec user_rnd_prec;

    int32_t compact, tbl_index;
    floatx80 fp0, fp1, fp2, fp3, xsave;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a, status);
        }
        a = packFloatx80(aSign, piby2_exp, pi_sig);
        float_raise(float_flag_inexact, status);
        return floatx80_move(a, status);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(aSign, 0, 0);
    }

    compact = floatx80_make_compact(aExp, aSig);

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = floatx80_precision_x;

    if (compact < 0x3FFB8000 || compact > 0x4002FFFF) {
        if (compact > 0x3FFF8000) {
            if (compact > 0x40638000) {
                fp0 = packFloatx80(aSign, piby2_exp, pi_sig);
                fp1 = packFloatx80(aSign, 0x0001, one_sig);

                status->float_rounding_mode = user_rnd_mode;
                status->floatx80_rounding_precision = user_rnd_prec;

                a = floatx80_sub(fp0, fp1, status);

                float_raise(float_flag_inexact, status);

                return a;
            } else {
                fp0 = a;
                fp1 = packFloatx80(1, one_exp, one_sig);
                fp1 = floatx80_div(fp1, fp0, status);
                xsave = fp1;
                fp0 = floatx80_mul(fp1, fp1, status);
                fp1 = floatx80_mul(fp0, fp0, status);
                fp3 = float64_to_floatx80(make_float64(0xBFB70BF398539E6A),
                                          status);
                fp2 = float64_to_floatx80(make_float64(0x3FBC7187962D1D7D),
                                          status);
                fp3 = floatx80_mul(fp3, fp1, status);
                fp2 = floatx80_mul(fp2, fp1, status);
                fp3 = floatx80_add(fp3, float64_to_floatx80(
                                   make_float64(0xBFC24924827107B8), status),
                                   status);
                fp2 = floatx80_add(fp2, float64_to_floatx80(
                                   make_float64(0x3FC999999996263E), status),
                                   status);
                fp1 = floatx80_mul(fp1, fp3, status);
                fp2 = floatx80_mul(fp2, fp0, status);
                fp1 = floatx80_add(fp1, float64_to_floatx80(
                                   make_float64(0xBFD5555555555536), status),
                                   status);
                fp0 = floatx80_mul(fp0, xsave, status);
                fp1 = floatx80_add(fp1, fp2, status);
                fp0 = floatx80_mul(fp0, fp1, status);
                fp0 = floatx80_add(fp0, xsave, status);
                fp1 = packFloatx80(aSign, piby2_exp, pi_sig);

                status->float_rounding_mode = user_rnd_mode;
                status->floatx80_rounding_precision = user_rnd_prec;

                a = floatx80_add(fp0, fp1, status);

                float_raise(float_flag_inexact, status);

                return a;
            }
        } else {
            if (compact < 0x3FD78000) {
                status->float_rounding_mode = user_rnd_mode;
                status->floatx80_rounding_precision = user_rnd_prec;

                a = floatx80_move(a, status);

                float_raise(float_flag_inexact, status);

                return a;
            } else {
                fp0 = a;
                xsave = a;
                fp0 = floatx80_mul(fp0, fp0, status);
                fp1 = floatx80_mul(fp0, fp0, status);
                fp2 = float64_to_floatx80(make_float64(0x3FB344447F876989),
                                          status);
                fp3 = float64_to_floatx80(make_float64(0xBFB744EE7FAF45DB),
                                          status);
                fp2 = floatx80_mul(fp2, fp1, status);
                fp3 = floatx80_mul(fp3, fp1, status);
                fp2 = floatx80_add(fp2, float64_to_floatx80(
                                   make_float64(0x3FBC71C646940220), status),
                                   status);
                fp3 = floatx80_add(fp3, float64_to_floatx80(
                                   make_float64(0xBFC24924921872F9),
                                   status), status);
                fp2 = floatx80_mul(fp2, fp1, status);
                fp1 = floatx80_mul(fp1, fp3, status);
                fp2 = floatx80_add(fp2, float64_to_floatx80(
                                   make_float64(0x3FC9999999998FA9), status),
                                   status);
                fp1 = floatx80_add(fp1, float64_to_floatx80(
                                   make_float64(0xBFD5555555555555), status),
                                   status);
                fp2 = floatx80_mul(fp2, fp0, status);
                fp0 = floatx80_mul(fp0, xsave, status);
                fp1 = floatx80_add(fp1, fp2, status);
                fp0 = floatx80_mul(fp0, fp1, status);

                status->float_rounding_mode = user_rnd_mode;
                status->floatx80_rounding_precision = user_rnd_prec;

                a = floatx80_add(fp0, xsave, status);

                float_raise(float_flag_inexact, status);

                return a;
            }
        }
    } else {
        aSig &= UINT64_C(0xF800000000000000);
        aSig |= UINT64_C(0x0400000000000000);
        xsave = packFloatx80(aSign, aExp, aSig);
        fp0 = a;
        fp1 = a;
        fp2 = packFloatx80(0, one_exp, one_sig);
        fp1 = floatx80_mul(fp1, xsave, status);
        fp0 = floatx80_sub(fp0, xsave, status);
        fp1 = floatx80_add(fp1, fp2, status);
        fp0 = floatx80_div(fp0, fp1, status);

        tbl_index = compact;

        tbl_index &= 0x7FFF0000;
        tbl_index -= 0x3FFB0000;
        tbl_index >>= 1;
        tbl_index += compact & 0x00007800;
        tbl_index >>= 11;

        fp3 = atan_tbl[tbl_index];

        fp3.high |= aSign ? 0x8000 : 0;

        fp1 = floatx80_mul(fp0, fp0, status);
        fp2 = float64_to_floatx80(make_float64(0xBFF6687E314987D8),
                                  status);
        fp2 = floatx80_add(fp2, fp1, status);
        fp2 = floatx80_mul(fp2, fp1, status);
        fp1 = floatx80_mul(fp1, fp0, status);
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0x4002AC6934A26DB3), status),
                           status);
        fp1 = floatx80_mul(fp1, float64_to_floatx80(
                           make_float64(0xBFC2476F4E1DA28E), status),
                           status);
        fp1 = floatx80_mul(fp1, fp2, status);
        fp0 = floatx80_add(fp0, fp1, status);

        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_add(fp0, fp3, status);

        float_raise(float_flag_inexact, status);

        return a;
    }
}

floatx80 floatx80_asin(floatx80 a, float_status *status)
{
    bool aSign;
    int32_t aExp;
    uint64_t aSig;

    FloatRoundMode user_rnd_mode;
    FloatX80RoundPrec user_rnd_prec;

    int32_t compact;
    floatx80 fp0, fp1, fp2, one;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF && (uint64_t) (aSig << 1)) {
        return propagateFloatx80NaNOneArg(a, status);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(aSign, 0, 0);
    }

    compact = floatx80_make_compact(aExp, aSig);

    if (compact >= 0x3FFF8000) {
        if (aExp == one_exp && aSig == one_sig) {
            float_raise(float_flag_inexact, status);
            a = packFloatx80(aSign, piby2_exp, pi_sig);
            return floatx80_move(a, status);
        } else {
            float_raise(float_flag_invalid, status);
            return floatx80_default_nan(status);
        }

    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = floatx80_precision_x;

    one = packFloatx80(0, one_exp, one_sig);
    fp0 = a;

    fp1 = floatx80_sub(one, fp0, status);
    fp2 = floatx80_add(one, fp0, status);
    fp1 = floatx80_mul(fp2, fp1, status);
    fp1 = floatx80_sqrt(fp1, status);
    fp0 = floatx80_div(fp0, fp1, status);

    status->float_rounding_mode = user_rnd_mode;
    status->floatx80_rounding_precision = user_rnd_prec;

    a = floatx80_atan(fp0, status);

    float_raise(float_flag_inexact, status);

    return a;
}

floatx80 floatx80_acos(floatx80 a, float_status *status)
{
    bool aSign;
    int32_t aExp;
    uint64_t aSig;

    FloatRoundMode user_rnd_mode;
    FloatX80RoundPrec user_rnd_prec;

    int32_t compact;
    floatx80 fp0, fp1, one;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF && (uint64_t) (aSig << 1)) {
        return propagateFloatx80NaNOneArg(a, status);
    }
    if (aExp == 0 && aSig == 0) {
        float_raise(float_flag_inexact, status);
        return roundAndPackFloatx80(status->floatx80_rounding_precision, 0,
                                    piby2_exp, pi_sig, 0, status);
    }

    compact = floatx80_make_compact(aExp, aSig);

    if (compact >= 0x3FFF8000) {
        if (aExp == one_exp && aSig == one_sig) {
            if (aSign) {
                a = packFloatx80(0, pi_exp, pi_sig);
                float_raise(float_flag_inexact, status);
                return floatx80_move(a, status);
            } else {
                return packFloatx80(0, 0, 0);
            }
        } else {
            float_raise(float_flag_invalid, status);
            return floatx80_default_nan(status);
        }
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = floatx80_precision_x;

    one = packFloatx80(0, one_exp, one_sig);
    fp0 = a;

    fp1 = floatx80_add(one, fp0, status);
    fp0 = floatx80_sub(one, fp0, status);
    fp0 = floatx80_div(fp0, fp1, status);
    fp0 = floatx80_sqrt(fp0, status);
    fp0 = floatx80_atan(fp0, status);

    status->float_rounding_mode = user_rnd_mode;
    status->floatx80_rounding_precision = user_rnd_prec;

    a = floatx80_add(fp0, fp0, status);

    float_raise(float_flag_inexact, status);

    return a;
}

floatx80 floatx80_atanh(floatx80 a, float_status *status)
{
    bool aSign;
    int32_t aExp;
    uint64_t aSig;

    FloatRoundMode user_rnd_mode;
    FloatX80RoundPrec user_rnd_prec;

    int32_t compact;
    floatx80 fp0, fp1, fp2, one;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF && (uint64_t) (aSig << 1)) {
        return propagateFloatx80NaNOneArg(a, status);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(aSign, 0, 0);
    }

    compact = floatx80_make_compact(aExp, aSig);

    if (compact >= 0x3FFF8000) {
        if (aExp == one_exp && aSig == one_sig) {
            float_raise(float_flag_divbyzero, status);
            return floatx80_default_inf(aSign, status);
        } else {
            float_raise(float_flag_invalid, status);
            return floatx80_default_nan(status);
        }
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = floatx80_precision_x;

    one = packFloatx80(0, one_exp, one_sig);
    fp2 = packFloatx80(aSign, 0x3FFE, one_sig);
    fp0 = packFloatx80(0, aExp, aSig);
    fp1 = packFloatx80(1, aExp, aSig);
    fp0 = floatx80_add(fp0, fp0, status);
    fp1 = floatx80_add(fp1, one, status);
    fp0 = floatx80_div(fp0, fp1, status);
    fp0 = floatx80_lognp1(fp0, status);

    status->float_rounding_mode = user_rnd_mode;
    status->floatx80_rounding_precision = user_rnd_prec;

    a = floatx80_mul(fp0, fp2,
                     status);

    float_raise(float_flag_inexact, status);

    return a;
}

floatx80 floatx80_etoxm1(floatx80 a, float_status *status)
{
    bool aSign;
    int32_t aExp;
    uint64_t aSig;

    FloatRoundMode user_rnd_mode;
    FloatX80RoundPrec user_rnd_prec;

    int32_t compact, n, j, m, m1;
    floatx80 fp0, fp1, fp2, fp3, l2, sc, onebysc;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a, status);
        }
        if (aSign) {
            return packFloatx80(aSign, one_exp, one_sig);
        }
        return floatx80_default_inf(0, status);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(aSign, 0, 0);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = floatx80_precision_x;

    if (aExp >= 0x3FFD) {
        compact = floatx80_make_compact(aExp, aSig);

        if (compact <= 0x4004C215) {
            fp0 = a;
            fp1 = a;
            fp0 = floatx80_mul(fp0, float32_to_floatx80(
                               make_float32(0x42B8AA3B), status),
                               status);
            n = floatx80_to_int32(fp0, status);
            fp0 = int32_to_floatx80(n, status);

            j = n & 0x3F;
            m = n / 64;
            if (n < 0 && j) {
                m--;
            }
            m1 = -m;

            fp2 = fp0;
            fp0 = floatx80_mul(fp0, float32_to_floatx80(
                               make_float32(0xBC317218), status),
                               status);
            l2 = packFloatx80(0, 0x3FDC, UINT64_C(0x82E308654361C4C6));
            fp2 = floatx80_mul(fp2, l2, status);
            fp0 = floatx80_add(fp0, fp1, status);
            fp0 = floatx80_add(fp0, fp2, status);

            fp1 = floatx80_mul(fp0, fp0, status);
            fp2 = float32_to_floatx80(make_float32(0x3950097B),
                                      status);
            fp2 = floatx80_mul(fp2, fp1, status);
            fp3 = floatx80_mul(float32_to_floatx80(make_float32(0x3AB60B6A),
                               status), fp1, status);
            fp2 = floatx80_add(fp2, float64_to_floatx80(
                               make_float64(0x3F81111111174385), status),
                               status);
            fp3 = floatx80_add(fp3, float64_to_floatx80(
                               make_float64(0x3FA5555555554F5A), status),
                               status);
            fp2 = floatx80_mul(fp2, fp1, status);
            fp3 = floatx80_mul(fp3, fp1, status);
            fp2 = floatx80_add(fp2, float64_to_floatx80(
                               make_float64(0x3FC5555555555555), status),
                               status);
            fp3 = floatx80_add(fp3, float32_to_floatx80(
                               make_float32(0x3F000000), status),
                               status);
            fp2 = floatx80_mul(fp2, fp1,
                               status);
            fp1 = floatx80_mul(fp1, fp3,
                               status);
            fp2 = floatx80_mul(fp2, fp0,
                               status);
            fp0 = floatx80_add(fp0, fp1,
                               status);
            fp0 = floatx80_add(fp0, fp2, status);

            fp0 = floatx80_mul(fp0, exp_tbl[j],
                               status);

            if (m >= 64) {
                fp1 = float32_to_floatx80(exp_tbl2[j], status);
                onebysc = packFloatx80(1, m1 + 0x3FFF, one_sig);
                fp1 = floatx80_add(fp1, onebysc, status);
                fp0 = floatx80_add(fp0, fp1, status);
                fp0 = floatx80_add(fp0, exp_tbl[j], status);
            } else if (m < -3) {
                fp0 = floatx80_add(fp0, float32_to_floatx80(exp_tbl2[j],
                                   status), status);
                fp0 = floatx80_add(fp0, exp_tbl[j], status);
                onebysc = packFloatx80(1, m1 + 0x3FFF, one_sig);
                fp0 = floatx80_add(fp0, onebysc, status);
            } else {
                fp1 = exp_tbl[j];
                fp0 = floatx80_add(fp0, float32_to_floatx80(exp_tbl2[j],
                                   status), status);
                onebysc = packFloatx80(1, m1 + 0x3FFF, one_sig);
                fp1 = floatx80_add(fp1, onebysc, status);
                fp0 = floatx80_add(fp0, fp1, status);
            }

            sc = packFloatx80(0, m + 0x3FFF, one_sig);

            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_mul(fp0, sc, status);

            float_raise(float_flag_inexact, status);

            return a;
        } else {
            if (aSign) {
                fp0 = float32_to_floatx80(make_float32(0xBF800000),
                      status);

                status->float_rounding_mode = user_rnd_mode;
                status->floatx80_rounding_precision = user_rnd_prec;

                a = floatx80_add(fp0, float32_to_floatx80(
                                 make_float32(0x00800000), status),
                                 status);

                float_raise(float_flag_inexact, status);

                return a;
            } else {
                status->float_rounding_mode = user_rnd_mode;
                status->floatx80_rounding_precision = user_rnd_prec;

                return floatx80_etox(a, status);
            }
        }
    } else {
        if (aExp >= 0x3FBE) {
            fp0 = a;
            fp0 = floatx80_mul(fp0, fp0, status);
            fp1 = float32_to_floatx80(make_float32(0x2F30CAA8),
                                      status);
            fp1 = floatx80_mul(fp1, fp0, status);
            fp2 = float32_to_floatx80(make_float32(0x310F8290),
                                      status);
            fp1 = floatx80_add(fp1, float32_to_floatx80(
                               make_float32(0x32D73220), status),
                               status);
            fp2 = floatx80_mul(fp2, fp0, status);
            fp1 = floatx80_mul(fp1, fp0, status);
            fp2 = floatx80_add(fp2, float32_to_floatx80(
                               make_float32(0x3493F281), status),
                               status);
            fp1 = floatx80_add(fp1, float64_to_floatx80(
                               make_float64(0x3EC71DE3A5774682), status),
                               status);
            fp2 = floatx80_mul(fp2, fp0, status);
            fp1 = floatx80_mul(fp1, fp0, status);
            fp2 = floatx80_add(fp2, float64_to_floatx80(
                               make_float64(0x3EFA01A019D7CB68), status),
                               status);
            fp1 = floatx80_add(fp1, float64_to_floatx80(
                               make_float64(0x3F2A01A01A019DF3), status),
                               status);
            fp2 = floatx80_mul(fp2, fp0, status);
            fp1 = floatx80_mul(fp1, fp0, status);
            fp2 = floatx80_add(fp2, float64_to_floatx80(
                               make_float64(0x3F56C16C16C170E2), status),
                               status);
            fp1 = floatx80_add(fp1, float64_to_floatx80(
                               make_float64(0x3F81111111111111), status),
                               status);
            fp2 = floatx80_mul(fp2, fp0, status);
            fp1 = floatx80_mul(fp1, fp0, status);
            fp2 = floatx80_add(fp2, float64_to_floatx80(
                               make_float64(0x3FA5555555555555), status),
                               status);
            fp3 = packFloatx80(0, 0x3FFC, UINT64_C(0xAAAAAAAAAAAAAAAB));
            fp1 = floatx80_add(fp1, fp3, status);
            fp2 = floatx80_mul(fp2, fp0, status);
            fp1 = floatx80_mul(fp1, fp0, status);

            fp2 = floatx80_mul(fp2, fp0, status);
            fp1 = floatx80_mul(fp1, a, status);

            fp0 = floatx80_mul(fp0, float32_to_floatx80(
                               make_float32(0x3F000000), status),
                               status);
            fp1 = floatx80_add(fp1, fp2, status);
            fp0 = floatx80_add(fp0, fp1, status);

            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_add(fp0, a, status);

            float_raise(float_flag_inexact, status);

            return a;
        } else {
            sc = packFloatx80(1, 1, one_sig);
            fp0 = a;

            if (aExp < 0x0033) {
                fp0 = floatx80_mul(fp0, float64_to_floatx80(
                                   make_float64(0x48B0000000000000), status),
                                   status);
                fp0 = floatx80_add(fp0, sc, status);

                status->float_rounding_mode = user_rnd_mode;
                status->floatx80_rounding_precision = user_rnd_prec;

                a = floatx80_mul(fp0, float64_to_floatx80(
                                 make_float64(0x3730000000000000), status),
                                 status);
            } else {
                status->float_rounding_mode = user_rnd_mode;
                status->floatx80_rounding_precision = user_rnd_prec;

                a = floatx80_add(fp0, sc, status);
            }

            float_raise(float_flag_inexact, status);

            return a;
        }
    }
}

floatx80 floatx80_tanh(floatx80 a, float_status *status)
{
    bool aSign, vSign;
    int32_t aExp, vExp;
    uint64_t aSig, vSig;

    FloatRoundMode user_rnd_mode;
    FloatX80RoundPrec user_rnd_prec;

    int32_t compact;
    floatx80 fp0, fp1;
    uint32_t sign;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a, status);
        }
        return packFloatx80(aSign, one_exp, one_sig);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(aSign, 0, 0);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = floatx80_precision_x;

    compact = floatx80_make_compact(aExp, aSig);

    if (compact < 0x3FD78000 || compact > 0x3FFFDDCE) {
        if (compact < 0x3FFF8000) {
            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_move(a, status);

            float_raise(float_flag_inexact, status);

            return a;
        } else {
            if (compact > 0x40048AA1) {
                sign = 0x3F800000;
                sign |= aSign ? 0x80000000 : 0x00000000;
                fp0 = float32_to_floatx80(make_float32(sign), status);
                sign &= 0x80000000;
                sign ^= 0x80800000;

                status->float_rounding_mode = user_rnd_mode;
                status->floatx80_rounding_precision = user_rnd_prec;

                a = floatx80_add(fp0, float32_to_floatx80(make_float32(sign),
                                 status), status);

                float_raise(float_flag_inexact, status);

                return a;
            } else {
                fp0 = packFloatx80(0, aExp + 1, aSig);
                fp0 = floatx80_etox(fp0, status);
                fp0 = floatx80_add(fp0, float32_to_floatx80(
                                   make_float32(0x3F800000),
                                   status), status);
                sign = aSign ? 0x80000000 : 0x00000000;
                fp1 = floatx80_div(float32_to_floatx80(make_float32(
                                   sign ^ 0xC0000000), status), fp0,
                                   status);
                fp0 = float32_to_floatx80(make_float32(sign | 0x3F800000),
                                          status);

                status->float_rounding_mode = user_rnd_mode;
                status->floatx80_rounding_precision = user_rnd_prec;

                a = floatx80_add(fp1, fp0, status);

                float_raise(float_flag_inexact, status);

                return a;
            }
        }
    } else {
        fp0 = packFloatx80(0, aExp + 1, aSig);
        fp0 = floatx80_etoxm1(fp0, status);
        fp1 = floatx80_add(fp0, float32_to_floatx80(make_float32(0x40000000),
                           status),
                           status);

        vSign = extractFloatx80Sign(fp1);
        vExp = extractFloatx80Exp(fp1);
        vSig = extractFloatx80Frac(fp1);

        fp1 = packFloatx80(vSign ^ aSign, vExp, vSig);

        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_div(fp0, fp1, status);

        float_raise(float_flag_inexact, status);

        return a;
    }
}

floatx80 floatx80_sinh(floatx80 a, float_status *status)
{
    bool aSign;
    int32_t aExp;
    uint64_t aSig;

    FloatRoundMode user_rnd_mode;
    FloatX80RoundPrec user_rnd_prec;

    int32_t compact;
    floatx80 fp0, fp1, fp2;
    float32 fact;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a, status);
        }
        return floatx80_default_inf(aSign, status);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(aSign, 0, 0);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = floatx80_precision_x;

    compact = floatx80_make_compact(aExp, aSig);

    if (compact > 0x400CB167) {
        if (compact > 0x400CB2B3) {
            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                        aSign, 0x8000, aSig, 0, status);
        } else {
            fp0 = floatx80_abs(a);
            fp0 = floatx80_sub(fp0, float64_to_floatx80(
                               make_float64(0x40C62D38D3D64634), status),
                               status);
            fp0 = floatx80_sub(fp0, float64_to_floatx80(
                               make_float64(0x3D6F90AEB1E75CC7), status),
                               status);
            fp0 = floatx80_etox(fp0, status);
            fp2 = packFloatx80(aSign, 0x7FFB, one_sig);

            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_mul(fp0, fp2, status);

            float_raise(float_flag_inexact, status);

            return a;
        }
    } else {
        fp0 = floatx80_abs(a);
        fp0 = floatx80_etoxm1(fp0, status);
        fp1 = floatx80_add(fp0, float32_to_floatx80(make_float32(0x3F800000),
                           status), status);
        fp2 = fp0;
        fp0 = floatx80_div(fp0, fp1, status);
        fp0 = floatx80_add(fp0, fp2, status);

        fact = packFloat32(aSign, 0x7E, 0);

        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_mul(fp0, float32_to_floatx80(fact, status), status);

        float_raise(float_flag_inexact, status);

        return a;
    }
}

floatx80 floatx80_cosh(floatx80 a, float_status *status)
{
    int32_t aExp;
    uint64_t aSig;

    FloatRoundMode user_rnd_mode;
    FloatX80RoundPrec user_rnd_prec;

    int32_t compact;
    floatx80 fp0, fp1;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a, status);
        }
        return floatx80_default_inf(0, status);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(0, one_exp, one_sig);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = floatx80_precision_x;

    compact = floatx80_make_compact(aExp, aSig);

    if (compact > 0x400CB167) {
        if (compact > 0x400CB2B3) {
            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;
            return roundAndPackFloatx80(status->floatx80_rounding_precision, 0,
                                        0x8000, one_sig, 0, status);
        } else {
            fp0 = packFloatx80(0, aExp, aSig);
            fp0 = floatx80_sub(fp0, float64_to_floatx80(
                               make_float64(0x40C62D38D3D64634), status),
                               status);
            fp0 = floatx80_sub(fp0, float64_to_floatx80(
                               make_float64(0x3D6F90AEB1E75CC7), status),
                               status);
            fp0 = floatx80_etox(fp0, status);
            fp1 = packFloatx80(0, 0x7FFB, one_sig);

            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_mul(fp0, fp1, status);

            float_raise(float_flag_inexact, status);

            return a;
        }
    }

    fp0 = packFloatx80(0, aExp, aSig);
    fp0 = floatx80_etox(fp0, status);
    fp0 = floatx80_mul(fp0, float32_to_floatx80(make_float32(0x3F000000),
                       status), status);
    fp1 = float32_to_floatx80(make_float32(0x3E800000), status);
    fp1 = floatx80_div(fp1, fp0, status);

    status->float_rounding_mode = user_rnd_mode;
    status->floatx80_rounding_precision = user_rnd_prec;

    a = floatx80_add(fp0, fp1, status);

    float_raise(float_flag_inexact, status);

    return a;
}
