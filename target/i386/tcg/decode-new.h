/*
 * Decode table flags, mostly based on Intel SDM.
 *
 *  Copyright (c) 2022 Red Hat, Inc.
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
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

typedef enum X86OpType {
    X86_TYPE_None,

    X86_TYPE_A,
    X86_TYPE_B,
    X86_TYPE_C,
    X86_TYPE_D,
    X86_TYPE_E,
    X86_TYPE_F,
    X86_TYPE_G,
    X86_TYPE_H,
    X86_TYPE_I,
    X86_TYPE_J,
    X86_TYPE_L,
    X86_TYPE_M,
    X86_TYPE_N,
    X86_TYPE_O,
    X86_TYPE_P,
    X86_TYPE_Q,
    X86_TYPE_R,
    X86_TYPE_S,
    X86_TYPE_U,
    X86_TYPE_V,
    X86_TYPE_W,
    X86_TYPE_X,
    X86_TYPE_Y,

    X86_TYPE_EM,
    X86_TYPE_WM,
    X86_TYPE_I_unsigned,
    X86_TYPE_nop,
    X86_TYPE_2op,
    X86_TYPE_LoBits,
    X86_TYPE_0,
    X86_TYPE_1,
    X86_TYPE_2,
    X86_TYPE_3,
    X86_TYPE_4,
    X86_TYPE_5,
    X86_TYPE_6,
    X86_TYPE_7,
    X86_TYPE_ES,
    X86_TYPE_CS,
    X86_TYPE_SS,
    X86_TYPE_DS,
    X86_TYPE_FS,
    X86_TYPE_GS,
} X86OpType;

typedef enum X86OpSize {
    X86_SIZE_None,

    X86_SIZE_a,
    X86_SIZE_b,
    X86_SIZE_d,
    X86_SIZE_dq,
    X86_SIZE_p,
    X86_SIZE_pd,
    X86_SIZE_pi,
    X86_SIZE_ps,
    X86_SIZE_q,
    X86_SIZE_qq,
    X86_SIZE_s,
    X86_SIZE_sd,
    X86_SIZE_ss,
    X86_SIZE_si,
    X86_SIZE_v,
    X86_SIZE_w,
    X86_SIZE_x,
    X86_SIZE_y,
    X86_SIZE_y_d64,
    X86_SIZE_z,
    X86_SIZE_z_f64,

    X86_SIZE_d64,
    X86_SIZE_f64,
    X86_SIZE_xh,
} X86OpSize;

typedef enum X86CPUIDFeature {
    X86_FEAT_None,
    X86_FEAT_3DNOW,
    X86_FEAT_ADX,
    X86_FEAT_AES,
    X86_FEAT_AVX,
    X86_FEAT_AVX2,
    X86_FEAT_BMI1,
    X86_FEAT_BMI2,
    X86_FEAT_CLFLUSH,
    X86_FEAT_CLFLUSHOPT,
    X86_FEAT_CLWB,
    X86_FEAT_CMOV,
    X86_FEAT_CMPCCXADD,
    X86_FEAT_CX8,
    X86_FEAT_CX16,
    X86_FEAT_F16C,
    X86_FEAT_FMA,
    X86_FEAT_FSGSBASE,
    X86_FEAT_FXSR,
    X86_FEAT_MOVBE,
    X86_FEAT_PCLMULQDQ,
    X86_FEAT_POPCNT,
    X86_FEAT_SHA_NI,
    X86_FEAT_SSE,
    X86_FEAT_SSE2,
    X86_FEAT_SSE3,
    X86_FEAT_SSSE3,
    X86_FEAT_SSE41,
    X86_FEAT_SSE42,
    X86_FEAT_SSE4A,
    X86_FEAT_XSAVE,
    X86_FEAT_XSAVEOPT,
} X86CPUIDFeature;

typedef enum X86OpUnit {
    X86_OP_SKIP,
    X86_OP_SEG,
    X86_OP_CR,
    X86_OP_DR,
    X86_OP_INT,
    X86_OP_IMM,
    X86_OP_SSE,
    X86_OP_MMX,
} X86OpUnit;

typedef enum X86InsnCheck {
    X86_CHECK_i64 = 1,
    X86_CHECK_o64 = 2,

    X86_CHECK_no_vm86 = 4,

    X86_CHECK_cpl0 = 8,
    X86_CHECK_vm86_iopl = 16,
    X86_CHECK_cpl_iopl = 32,
    X86_CHECK_iopl = X86_CHECK_cpl_iopl | X86_CHECK_vm86_iopl,

    X86_CHECK_VEX128 = 64,

    X86_CHECK_W0 = 128,

    X86_CHECK_W1 = 256,

    X86_CHECK_prot_or_vm86 = 512,
    X86_CHECK_prot = X86_CHECK_prot_or_vm86 | X86_CHECK_no_vm86,

    X86_CHECK_smm = 1024,

    X86_CHECK_i64_amd = 2048,
    X86_CHECK_o64_intel = 4096,
} X86InsnCheck;

typedef enum X86InsnSpecial {
    X86_SPECIAL_None,

    X86_SPECIAL_HasLock,

    X86_SPECIAL_Locked,

    X86_SPECIAL_BitTest,

    X86_SPECIAL_NoLoadEA,

    X86_SPECIAL_Op0_Rd,

    X86_SPECIAL_Op2_Ry,

    X86_SPECIAL_AVXExtMov,

    X86_SPECIAL_MMX,

    X86_SPECIAL_SExtT0,
    X86_SPECIAL_ZExtT0,

    X86_SPECIAL_Op0_Mw,
} X86InsnSpecial;

typedef enum X86VEXSpecial {
    X86_VEX_SSEUnaligned,

    X86_VEX_REPScalar,

    X86_VEX_AVX2_256,
} X86VEXSpecial;


typedef struct X86OpEntry  X86OpEntry;
typedef struct X86DecodedInsn X86DecodedInsn;
struct DisasContext;

typedef void (*X86DecodeFunc)(struct DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b);

typedef void (*X86GenFunc)(struct DisasContext *s, X86DecodedInsn *decode);

struct X86OpEntry {
    union {
        X86GenFunc gen;
        X86DecodeFunc decode;
    };
    X86OpType    op0:8;
    X86OpSize    s0:8;
    X86OpType    op1:8;
    X86OpSize    s1:8;
    X86OpType    op2:8;
    X86OpSize    s2:8;
    X86OpType    op3:8;
    X86OpSize    s3:8;

    X86InsnSpecial special:8;
    X86CPUIDFeature cpuid:8;
    unsigned     vex_class:8;
    X86VEXSpecial vex_special:8;
    unsigned     valid_prefix:16;
    unsigned     check:16;
    unsigned     intercept:8;
    bool         has_intercept:1;
    bool         is_decode:1;
};

typedef struct X86DecodedOp {
    int8_t n;
    MemOp ot;
    X86OpUnit unit;
    bool has_ea;
    int offset;

    union {
	target_ulong imm;
        TCGv_ptr v_ptr;
    };
} X86DecodedOp;

typedef struct AddressParts {
    int def_seg;
    int base;
    int index;
    int scale;
    target_long disp;
} AddressParts;

struct X86DecodedInsn {
    X86OpEntry e;
    X86DecodedOp op[3];
    target_ulong immediate;
    AddressParts mem;

    TCGv cc_dst, cc_src, cc_src2;
    TCGv_i32 cc_op_dynamic;
    int8_t cc_op;

    uint8_t b;
};

static void gen_lea_modrm(struct DisasContext *s, X86DecodedInsn *decode);
