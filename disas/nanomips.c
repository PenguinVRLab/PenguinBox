/*
 *  Source file for nanoMIPS disassembler component of QEMU
 *
 *  Copyright (C) 2018  Wave Computing, Inc.
 *  Copyright (C) 2018  Matthew Fortune <matthew.fortune@mips.com>
 *  Copyright (C) 2018  Aleksandar Markovic <amarkovic@wavecomp.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

/*
 *  Documentation used while implementing this component:
 *
 *  [1] "MIPS® Architecture Base: nanoMIPS32(tm) Instruction Set Technical
 *      Reference Manual", Revision 01.01, April 27, 2018
 */

#include "qemu/osdep.h"
#include "disas/dis-asm.h"

typedef int64_t int64;
typedef uint64_t uint64;
typedef uint32_t uint32;
typedef uint16_t uint16;
typedef uint64_t img_address;

typedef struct Dis_info {
  img_address m_pc;
  fprintf_function fprintf_func;
  FILE *stream;
  sigjmp_buf buf;
} Dis_info;

#define IMGASSERTONCE(test)


static char * G_GNUC_PRINTF(1, 2) img_format(const char *format, ...)
{
    char *buffer;
    va_list args;
    va_start(args, format);
    buffer = g_strdup_vprintf(format, args);
    va_end(args);
    return buffer;
}


static char *to_string(img_address a)
{
    return g_strdup_printf("0x%" PRIx64, a);
}


static uint64 extract_bits(uint64 data, uint32 bit_offset, uint32 bit_size)
{
    return (data << (64 - (bit_size + bit_offset))) >> (64 - bit_size);
}


static int64 sign_extend(int64 data, int msb)
{
    uint64 shift = 63 - msb;
    return (data << shift) >> shift;
}


static uint64 renumber_registers(uint64 index, uint64 *register_list,
                               size_t register_list_size, Dis_info *info)
{
    if (index < register_list_size) {
        return register_list[index];
    }

    info->fprintf_func(info->stream, "Invalid register mapping index %" PRIu64
                       ", size of list = %zu", index, register_list_size);
    siglongjmp(info->buf, 1);
}


static uint64 decode_gpr_gpr4(uint64 d, Dis_info *info)
{
    static uint64 register_list[] = {  8,  9, 10, 11,  4,  5,  6,  7,
                                      16, 17, 18, 19, 20, 21, 22, 23 };
    return renumber_registers(d, register_list,
               sizeof(register_list) / sizeof(register_list[0]), info);
}


static uint64 decode_gpr_gpr4_zero(uint64 d, Dis_info *info)
{
    static uint64 register_list[] = {  8,  9, 10,  0,  4,  5,  6,  7,
                                      16, 17, 18, 19, 20, 21, 22, 23 };
    return renumber_registers(d, register_list,
               sizeof(register_list) / sizeof(register_list[0]), info);
}


static uint64 decode_gpr_gpr3(uint64 d, Dis_info *info)
{
    static uint64 register_list[] = { 16, 17, 18, 19,  4,  5,  6,  7 };
    return renumber_registers(d, register_list,
               sizeof(register_list) / sizeof(register_list[0]), info);
}


static uint64 decode_gpr_gpr3_src_store(uint64 d, Dis_info *info)
{
    static uint64 register_list[] = {  0, 17, 18, 19,  4,  5,  6,  7 };
    return renumber_registers(d, register_list,
               sizeof(register_list) / sizeof(register_list[0]), info);
}


static uint64 decode_gpr_gpr2_reg1(uint64 d, Dis_info *info)
{
    static uint64 register_list[] = {  4,  5,  6,  7 };
    return renumber_registers(d, register_list,
               sizeof(register_list) / sizeof(register_list[0]), info);
}


static uint64 decode_gpr_gpr2_reg2(uint64 d, Dis_info *info)
{
    static uint64 register_list[] = {  5,  6,  7,  8 };
    return renumber_registers(d, register_list,
               sizeof(register_list) / sizeof(register_list[0]), info);
}


static uint64 decode_gpr_gpr1(uint64 d, Dis_info *info)
{
    static uint64 register_list[] = {  4,  5 };
    return renumber_registers(d, register_list,
               sizeof(register_list) / sizeof(register_list[0]), info);
}


static int64 neg_copy(uint64 d)
{
    return 0ll - d;
}


static uint64 encode_count3_from_count(uint64 d)
{
    IMGASSERTONCE(d < 8);
    return d == 0ull ? 8ull : d;
}


static uint64 encode_shift3_from_shift(uint64 d)
{
    IMGASSERTONCE(d < 8);
    return d == 0ull ? 8ull : d;
}


static int64 encode_eu_from_s_li16(uint64 d)
{
    IMGASSERTONCE(d < 128);
    return d == 127 ? -1 : (int64)d;
}


static uint64 encode_msbd_from_size(uint64 d)
{
    IMGASSERTONCE(d < 32);
    return d + 1;
}


static uint64 encode_eu_from_u_andi16(uint64 d)
{
    IMGASSERTONCE(d < 16);
    if (d == 12) {
        return 0x00ffull;
    }
    if (d == 13) {
        return 0xffffull;
    }
    return d;
}


static uint64 encode_rt1_from_rt(uint64 d)
{
    return d ? 31 : 30;
}


static const char *GPR(uint64 reg, Dis_info *info)
{
    static const char *gpr_reg[32] = {
        "zero", "at",   "v0",   "v1",   "a0",   "a1",   "a2",   "a3",
        "a4",   "a5",   "a6",   "a7",   "r12",  "r13",  "r14",  "r15",
        "s0",   "s1",   "s2",   "s3",   "s4",   "s5",   "s6",   "s7",
        "r24",  "r25",  "k0",   "k1",   "gp",   "sp",   "fp",   "ra"
    };

    if (reg < 32) {
        return gpr_reg[reg];
    }

    info->fprintf_func(info->stream, "Invalid GPR register index %" PRIu64,
                       reg);
    siglongjmp(info->buf, 1);
}


static char *save_restore_list(uint64 rt, uint64 count, uint64 gp,
                               Dis_info *info)
{
    char *reg_list[34];
    reg_list[0] = (char *)"";

    assert(count <= 32);
    for (uint64 counter = 0; counter != count; counter++) {
        bool use_gp = gp && (counter == count - 1);
        uint64 this_rt = use_gp ? 28 : ((rt & 0x10) | (rt + counter)) & 0x1f;
        reg_list[counter + 1] = (char *)GPR(this_rt, info);
    }
    reg_list[count + 1] = NULL;

    return g_strjoinv(",", reg_list);
}


static const char *FPR(uint64 reg, Dis_info *info)
{
    static const char *fpr_reg[32] = {
        "f0",  "f1",  "f2",  "f3",  "f4",  "f5",  "f6",  "f7",
        "f8",  "f9",  "f10", "f11", "f12", "f13", "f14", "f15",
        "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
        "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31"
    };

    if (reg < 32) {
        return fpr_reg[reg];
    }

    info->fprintf_func(info->stream, "Invalid FPR register index %" PRIu64,
                       reg);
    siglongjmp(info->buf, 1);
}


static const char *AC(uint64 reg, Dis_info *info)
{
    static const char *ac_reg[4] = {
        "ac0",  "ac1",  "ac2",  "ac3"
    };

    if (reg < 4) {
        return ac_reg[reg];
    }

    info->fprintf_func(info->stream, "Invalid AC register index %" PRIu64,
                       reg);
    siglongjmp(info->buf, 1);
}


static char *ADDRESS(uint64 value, int instruction_size, Dis_info *info)
{
    img_address address = info->m_pc + value + instruction_size;
    return to_string(address);
}


static uint64 extract_op_code_value(const uint16 *data, int size)
{
    switch (size) {
    case 16:
        return data[0];
    case 32:
        return ((uint64)data[0] << 16) | data[1];
    case 48:
        return ((uint64)data[0] << 32) | ((uint64)data[1] << 16) | data[2];
    default:
        return data[0];
    }
}


static uint64 extract_code_18_to_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 19);
    return value;
}


static uint64 extract_shift3_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 3);
    return value;
}


static uint64 extract_u_11_10_9_8_7_6_5_4_3__s3(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 3, 9) << 3;
    return value;
}


static uint64 extract_count_3_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 4);
    return value;
}


static uint64 extract_rtz3_9_8_7(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 7, 3);
    return value;
}


static uint64 extract_u_17_to_1__s1(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 1, 17) << 1;
    return value;
}


static int64 extract_s__se9_20_19_18_17_16_15_14_13_12_11(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 11, 10);
    value = sign_extend(value, 9);
    return value;
}


static int64 extract_s__se11_0_10_9_8_7_6_5_4_3_2_1_0_s1(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 1) << 11;
    value |= extract_bits(instruction, 1, 10) << 1;
    value = sign_extend(value, 11);
    return value;
}


static uint64 extract_u_10(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 10, 1);
    return value;
}


static uint64 extract_rtz4_27_26_25_23_22_21(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 21, 3);
    value |= extract_bits(instruction, 25, 1) << 3;
    return value;
}


static uint64 extract_sa_15_14_13_12_11(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 11, 5);
    return value;
}


static uint64 extract_shift_4_3_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 5);
    return value;
}


static uint64 extract_shiftx_10_9_8_7__s1(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 7, 4) << 1;
    return value;
}


static uint64 extract_hint_25_24_23_22_21(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 21, 5);
    return value;
}


static uint64 extract_count3_14_13_12(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 12, 3);
    return value;
}


static int64 extract_s__se31_0_11_to_2_20_to_12_s12(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 1) << 31;
    value |= extract_bits(instruction, 2, 10) << 21;
    value |= extract_bits(instruction, 12, 9) << 12;
    value = sign_extend(value, 31);
    return value;
}


static int64 extract_s__se7_0_6_5_4_3_2_1_s1(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 1) << 7;
    value |= extract_bits(instruction, 1, 6) << 1;
    value = sign_extend(value, 7);
    return value;
}


static uint64 extract_u2_10_9(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 9, 2);
    return value;
}


static uint64 extract_code_25_24_23_22_21_20_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 10);
    return value;
}


static uint64 extract_rs_20_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 5);
    return value;
}


static uint64 extract_u_2_1__s1(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 1, 2) << 1;
    return value;
}


static uint64 extract_stripe_6(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 6, 1);
    return value;
}


static uint64 extract_ac_15_14(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 14, 2);
    return value;
}


static uint64 extract_shift_20_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 5);
    return value;
}


static uint64 extract_rdl_25_24(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 24, 1);
    return value;
}


static int64 extract_s__se10_0_9_8_7_6_5_4_3_2_1_s1(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 1) << 10;
    value |= extract_bits(instruction, 1, 9) << 1;
    value = sign_extend(value, 10);
    return value;
}


static uint64 extract_eu_6_5_4_3_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 7);
    return value;
}


static uint64 extract_shift_5_4_3_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 6);
    return value;
}


static uint64 extract_count_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 4);
    return value;
}


static uint64 extract_code_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 3);
    return value;
}


static uint64 extract_u_11_10_9_8_7_6_5_4_3_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 12);
    return value;
}


static uint64 extract_rs_4_3_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 5);
    return value;
}


static uint64 extract_u_20_to_3__s3(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 3, 18) << 3;
    return value;
}


static uint64 extract_u_3_2_1_0__s2(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 4) << 2;
    return value;
}


static uint64 extract_cofun_25_24_23(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 3, 23);
    return value;
}


static uint64 extract_u_2_1_0__s2(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 3) << 2;
    return value;
}


static uint64 extract_rd3_3_2_1(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 1, 3);
    return value;
}


static uint64 extract_sa_15_14_13_12(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 12, 4);
    return value;
}


static uint64 extract_rt_25_24_23_22_21(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 21, 5);
    return value;
}


static uint64 extract_ru_7_6_5_4_3(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 3, 5);
    return value;
}


static uint64 extract_u_17_to_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 18);
    return value;
}


static uint64 extract_rsz4_4_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 3);
    value |= extract_bits(instruction, 4, 1) << 3;
    return value;
}


static int64 extract_s__se21_0_20_to_1_s1(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 1) << 21;
    value |= extract_bits(instruction, 1, 20) << 1;
    value = sign_extend(value, 21);
    return value;
}


static uint64 extract_op_25_to_3(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 3, 23);
    return value;
}


static uint64 extract_rs4_4_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 3);
    value |= extract_bits(instruction, 4, 1) << 3;
    return value;
}


static uint64 extract_bit_23_22_21(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 21, 3);
    return value;
}


static uint64 extract_rt_41_40_39_38_37(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 37, 5);
    return value;
}


static int64 extract_shift__se5_21_20_19_18_17_16(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 16, 6);
    value = sign_extend(value, 5);
    return value;
}


static uint64 extract_rd2_3_8(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 3, 1) << 1;
    value |= extract_bits(instruction, 8, 1);
    return value;
}


static uint64 extract_code_17_to_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 18);
    return value;
}


static uint64 extract_size_20_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 5);
    return value;
}


static int64 extract_s__se8_15_7_6_5_4_3_2_s2(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 2, 6) << 2;
    value |= extract_bits(instruction, 15, 1) << 8;
    value = sign_extend(value, 8);
    return value;
}


static uint64 extract_u_15_to_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 16);
    return value;
}


static uint64 extract_fs_20_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 5);
    return value;
}


static int64 extract_s__se8_15_7_6_5_4_3_2_1_0(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 8);
    value |= extract_bits(instruction, 15, 1) << 8;
    value = sign_extend(value, 8);
    return value;
}


static uint64 extract_stype_20_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 5);
    return value;
}


static uint64 extract_rtl_11(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 9, 1);
    return value;
}


static uint64 extract_hs_20_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 5);
    return value;
}


static uint64 extract_sel_13_12_11(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 11, 3);
    return value;
}


static uint64 extract_lsb_4_3_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 5);
    return value;
}


static uint64 extract_gp_2(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 2, 1);
    return value;
}


static uint64 extract_rt3_9_8_7(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 7, 3);
    return value;
}


static uint64 extract_ft_25_24_23_22_21(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 21, 5);
    return value;
}


static uint64 extract_u_17_16_15_14_13_12_11(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 11, 7);
    return value;
}


static uint64 extract_cs_20_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 5);
    return value;
}


static uint64 extract_rt4_9_7_6_5(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 5, 3);
    value |= extract_bits(instruction, 9, 1) << 3;
    return value;
}


static uint64 extract_msbt_10_9_8_7_6(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 6, 5);
    return value;
}


static uint64 extract_u_5_4_3_2_1_0__s2(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 6) << 2;
    return value;
}


static uint64 extract_sa_15_14_13(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 13, 3);
    return value;
}


static int64 extract_s__se14_0_13_to_1_s1(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 1) << 14;
    value |= extract_bits(instruction, 1, 13) << 1;
    value = sign_extend(value, 14);
    return value;
}


static uint64 extract_rs3_6_5_4(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 4, 3);
    return value;
}


static uint64 extract_u_31_to_0__s32(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 32) << 32;
    return value;
}


static uint64 extract_shift_10_9_8_7_6(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 6, 5);
    return value;
}


static uint64 extract_cs_25_24_23_22_21(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 21, 5);
    return value;
}


static uint64 extract_shiftx_11_10_9_8_7_6(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 6, 6);
    return value;
}


static uint64 extract_rt_9_8_7_6_5(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 5, 5);
    return value;
}


static uint64 extract_op_25_24_23_22_21(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 21, 5);
    return value;
}


static uint64 extract_u_6_5_4_3_2_1_0__s2(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 7) << 2;
    return value;
}


static uint64 extract_bit_16_15_14_13_12_11(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 11, 6);
    return value;
}


static uint64 extract_mask_20_19_18_17_16_15_14(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 14, 7);
    return value;
}


static uint64 extract_eu_3_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 4);
    return value;
}


static uint64 extract_u_7_6_5_4__s4(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 4, 4) << 4;
    return value;
}


static int64 extract_s__se8_15_7_6_5_4_3_s3(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 3, 5) << 3;
    value |= extract_bits(instruction, 15, 1) << 8;
    value = sign_extend(value, 8);
    return value;
}


static uint64 extract_ft_15_14_13_12_11(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 11, 5);
    return value;
}


static int64 extract_s__se31_15_to_0_31_to_16(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 16) << 16;
    value |= extract_bits(instruction, 16, 16);
    value = sign_extend(value, 31);
    return value;
}


static uint64 extract_u_20_19_18_17_16_15_14_13(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 13, 8);
    return value;
}


static uint64 extract_u_17_to_2__s2(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 2, 16) << 2;
    return value;
}


static uint64 extract_rd_15_14_13_12_11(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 11, 5);
    return value;
}


static uint64 extract_c0s_20_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 5);
    return value;
}


static uint64 extract_code_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 2);
    return value;
}


static int64 extract_s__se25_0_24_to_1_s1(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 1) << 25;
    value |= extract_bits(instruction, 1, 24) << 1;
    value = sign_extend(value, 25);
    return value;
}


static uint64 extract_u_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 2);
    return value;
}


static uint64 extract_u_3_8__s2(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 3, 1) << 3;
    value |= extract_bits(instruction, 8, 1) << 2;
    return value;
}


static uint64 extract_fd_15_14_13_12_11(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 11, 5);
    return value;
}


static uint64 extract_u_4_3_2_1_0__s2(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 5) << 2;
    return value;
}


static uint64 extract_rtz4_9_7_6_5(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 5, 3);
    value |= extract_bits(instruction, 9, 1) << 3;
    return value;
}


static uint64 extract_sel_15_14_13_12_11(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 11, 5);
    return value;
}


static uint64 extract_ct_25_24_23_22_21(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 21, 5);
    return value;
}


static uint64 extract_u_20_to_2__s2(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 2, 19) << 2;
    return value;
}


static int64 extract_s__se3_4_2_1_0(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 3);
    value |= extract_bits(instruction, 4, 1) << 3;
    value = sign_extend(value, 3);
    return value;
}


static uint64 extract_u_3_2_1_0__s1(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 4) << 1;
    return value;
}



static bool ADDIU_32__cond(uint64 instruction)
{
    uint64 rt = extract_rt_25_24_23_22_21(instruction);
    return rt != 0;
}


static bool ADDIU_RS5__cond(uint64 instruction)
{
    uint64 rt = extract_rt_9_8_7_6_5(instruction);
    return rt != 0;
}


static bool BALRSC_cond(uint64 instruction)
{
    uint64 rt = extract_rt_25_24_23_22_21(instruction);
    return rt != 0;
}


static bool BEQC_16__cond(uint64 instruction)
{
    uint64 rs3 = extract_rs3_6_5_4(instruction);
    uint64 rt3 = extract_rt3_9_8_7(instruction);
    uint64 u = extract_u_3_2_1_0__s1(instruction);
    return rs3 < rt3 && u != 0;
}


static bool BNEC_16__cond(uint64 instruction)
{
    uint64 rs3 = extract_rs3_6_5_4(instruction);
    uint64 rt3 = extract_rt3_9_8_7(instruction);
    uint64 u = extract_u_3_2_1_0__s1(instruction);
    return rs3 >= rt3 && u != 0;
}


static bool MOVE_cond(uint64 instruction)
{
    uint64 rt = extract_rt_9_8_7_6_5(instruction);
    return rt != 0;
}


static bool P16_BR1_cond(uint64 instruction)
{
    uint64 u = extract_u_3_2_1_0__s1(instruction);
    return u != 0;
}


static bool PREF_S9__cond(uint64 instruction)
{
    uint64 hint = extract_hint_25_24_23_22_21(instruction);
    return hint != 31;
}


static bool PREFE_cond(uint64 instruction)
{
    uint64 hint = extract_hint_25_24_23_22_21(instruction);
    return hint != 31;
}


static bool SLTU_cond(uint64 instruction)
{
    uint64 rd = extract_rd_15_14_13_12_11(instruction);
    return rd != 0;
}



static char *ABS_D(uint64 instruction, Dis_info *info)
{
    uint64 fd_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *fs = FPR(fs_value, info);
    const char *fd = FPR(fd_value, info);

    return img_format("ABS.D %s, %s", fd, fs);
}


static char *ABS_S(uint64 instruction, Dis_info *info)
{
    uint64 fd_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *fs = FPR(fs_value, info);
    const char *fd = FPR(fd_value, info);

    return img_format("ABS.S %s, %s", fd, fs);
}


static char *ABSQ_S_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("ABSQ_S.PH %s, %s", rt, rs);
}


static char *ABSQ_S_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("ABSQ_S.QB %s, %s", rt, rs);
}


static char *ABSQ_S_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("ABSQ_S.W %s, %s", rt, rs);
}


static char *ACLR(uint64 instruction, Dis_info *info)
{
    uint64 bit_value = extract_bit_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("ACLR 0x%" PRIx64 ", %" PRId64 "(%s)",
                      bit_value, s_value, rs);
}


static char *ADD(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADD %s, %s, %s", rd, rs, rt);
}


static char *ADD_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);
    const char *fd = FPR(fd_value, info);

    return img_format("ADD.D %s, %s, %s", fd, fs, ft);
}


static char *ADD_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);
    const char *fd = FPR(fd_value, info);

    return img_format("ADD.S %s, %s, %s", fd, fs, ft);
}


static char *ADDIU_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_15_to_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("ADDIU %s, %s, 0x%" PRIx64, rt, rs, u_value);
}


static char *ADDIU_48_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    int64 s_value = extract_s__se31_15_to_0_31_to_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("ADDIU %s, %" PRId64, rt, s_value);
}


static char *ADDIU_GP48_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    int64 s_value = extract_s__se31_15_to_0_31_to_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("ADDIU %s, $%d, %" PRId64, rt, 28, s_value);
}


static char *ADDIU_GP_B_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_0(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("ADDIU %s, $%d, 0x%" PRIx64, rt, 28, u_value);
}


static char *ADDIU_GP_W_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_20_to_2__s2(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("ADDIU %s, $%d, 0x%" PRIx64, rt, 28, u_value);
}


static char *ADDIU_NEG_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    int64 u = neg_copy(u_value);

    return img_format("ADDIU %s, %s, %" PRId64, rt, rs, u);
}


static char *ADDIU_R1_SP_(uint64 instruction, Dis_info *info)
{
    uint64 u_value = extract_u_5_4_3_2_1_0__s2(instruction);
    uint64 rt3_value = extract_rt3_9_8_7(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);

    return img_format("ADDIU %s, $%d, 0x%" PRIx64, rt3, 29, u_value);
}


static char *ADDIU_R2_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_2_1_0__s2(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);

    return img_format("ADDIU %s, %s, 0x%" PRIx64, rt3, rs3, u_value);
}


static char *ADDIU_RS5_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_9_8_7_6_5(instruction);
    int64 s_value = extract_s__se3_4_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("ADDIU %s, %" PRId64, rt, s_value);
}


static char *ADDIUPC_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    int64 s_value = extract_s__se21_0_20_to_1_s1(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("ADDIUPC %s, %s", rt, s);
}


static char *ADDIUPC_48_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    int64 s_value = extract_s__se31_15_to_0_31_to_16(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 6, info);

    return img_format("ADDIUPC %s, %s", rt, s);
}


static char *ADDQ_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDQ.PH %s, %s, %s", rd, rs, rt);
}


static char *ADDQ_S_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDQ_S.PH %s, %s, %s", rd, rs, rt);
}


static char *ADDQ_S_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDQ_S.W %s, %s, %s", rd, rs, rt);
}


static char *ADDQH_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDQH.PH %s, %s, %s", rd, rs, rt);
}


static char *ADDQH_R_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDQH_R.PH %s, %s, %s", rd, rs, rt);
}


static char *ADDQH_R_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDQH_R.W %s, %s, %s", rd, rs, rt);
}


static char *ADDQH_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDQH.W %s, %s, %s", rd, rs, rt);
}


static char *ADDSC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDSC %s, %s, %s", rd, rs, rt);
}


static char *ADDU_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 rd3_value = extract_rd3_3_2_1(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);
    const char *rd3 = GPR(decode_gpr_gpr3(rd3_value, info), info);

    return img_format("ADDU %s, %s, %s", rd3, rs3, rt3);
}


static char *ADDU_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDU %s, %s, %s", rd, rs, rt);
}


static char *ADDU_4X4_(uint64 instruction, Dis_info *info)
{
    uint64 rt4_value = extract_rt4_9_7_6_5(instruction);
    uint64 rs4_value = extract_rs4_4_2_1_0(instruction);

    const char *rs4 = GPR(decode_gpr_gpr4(rs4_value, info), info);
    const char *rt4 = GPR(decode_gpr_gpr4(rt4_value, info), info);

    return img_format("ADDU %s, %s", rs4, rt4);
}


static char *ADDU_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDU.PH %s, %s, %s", rd, rs, rt);
}


static char *ADDU_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDU.QB %s, %s, %s", rd, rs, rt);
}


static char *ADDU_S_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDU_S.PH %s, %s, %s", rd, rs, rt);
}


static char *ADDU_S_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDU_S.QB %s, %s, %s", rd, rs, rt);
}


static char *ADDUH_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDUH.QB %s, %s, %s", rd, rs, rt);
}


static char *ADDUH_R_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDUH_R.QB %s, %s, %s", rd, rs, rt);
}

static char *ADDWC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDWC %s, %s, %s", rd, rs, rt);
}


static char *ALUIPC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    int64 s_value = extract_s__se31_0_11_to_2_20_to_12_s12(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("ALUIPC %s, %%pcrel_hi(%s)", rt, s);
}


static char *AND_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);

    return img_format("AND %s, %s", rs3, rt3);
}


static char *AND_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("AND %s, %s, %s", rd, rs, rt);
}


static char *ANDI_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 eu_value = extract_eu_3_2_1_0(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);
    uint64 eu = encode_eu_from_u_andi16(eu_value);

    return img_format("ANDI %s, %s, 0x%" PRIx64, rt3, rs3, eu);
}


static char *ANDI_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("ANDI %s, %s, 0x%" PRIx64, rt, rs, u_value);
}


static char *APPEND(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("APPEND %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


static char *ASET(uint64 instruction, Dis_info *info)
{
    uint64 bit_value = extract_bit_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("ASET 0x%" PRIx64 ", %" PRId64 "(%s)",
                      bit_value, s_value, rs);
}


static char *BALC_16_(uint64 instruction, Dis_info *info)
{
    int64 s_value = extract_s__se10_0_9_8_7_6_5_4_3_2_1_s1(instruction);

    g_autofree char *s = ADDRESS(s_value, 2, info);

    return img_format("BALC %s", s);
}


static char *BALC_32_(uint64 instruction, Dis_info *info)
{
    int64 s_value = extract_s__se25_0_24_to_1_s1(instruction);

    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BALC %s", s);
}


static char *BALRSC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("BALRSC %s, %s", rt, rs);
}


static char *BBEQZC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 bit_value = extract_bit_16_15_14_13_12_11(instruction);
    int64 s_value = extract_s__se11_0_10_9_8_7_6_5_4_3_2_1_0_s1(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BBEQZC %s, 0x%" PRIx64 ", %s", rt, bit_value, s);
}


static char *BBNEZC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 bit_value = extract_bit_16_15_14_13_12_11(instruction);
    int64 s_value = extract_s__se11_0_10_9_8_7_6_5_4_3_2_1_0_s1(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BBNEZC %s, 0x%" PRIx64 ", %s", rt, bit_value, s);
}


static char *BC_16_(uint64 instruction, Dis_info *info)
{
    int64 s_value = extract_s__se10_0_9_8_7_6_5_4_3_2_1_s1(instruction);

    g_autofree char *s = ADDRESS(s_value, 2, info);

    return img_format("BC %s", s);
}


static char *BC_32_(uint64 instruction, Dis_info *info)
{
    int64 s_value = extract_s__se25_0_24_to_1_s1(instruction);

    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BC %s", s);
}


static char *BC1EQZC(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    const char *ft = FPR(ft_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BC1EQZC %s, %s", ft, s);
}


static char *BC1NEZC(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    const char *ft = FPR(ft_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BC1NEZC %s, %s", ft, s);
}


static char *BC2EQZC(uint64 instruction, Dis_info *info)
{
    uint64 ct_value = extract_ct_25_24_23_22_21(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BC2EQZC CP%" PRIu64 ", %s", ct_value, s);
}


static char *BC2NEZC(uint64 instruction, Dis_info *info)
{
    uint64 ct_value = extract_ct_25_24_23_22_21(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BC2NEZC CP%" PRIu64 ", %s", ct_value, s);
}


static char *BEQC_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_3_2_1_0__s1(instruction);

    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);
    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    g_autofree char *u = ADDRESS(u_value, 2, info);

    return img_format("BEQC %s, %s, %s", rs3, rt3, u);
}


static char *BEQC_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BEQC %s, %s, %s", rs, rt, s);
}


static char *BEQIC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_16_15_14_13_12_11(instruction);
    int64 s_value = extract_s__se11_0_10_9_8_7_6_5_4_3_2_1_0_s1(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BEQIC %s, 0x%" PRIx64 ", %s", rt, u_value, s);
}


static char *BEQZC_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    int64 s_value = extract_s__se7_0_6_5_4_3_2_1_s1(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    g_autofree char *s = ADDRESS(s_value, 2, info);

    return img_format("BEQZC %s, %s", rt3, s);
}


static char *BGEC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BGEC %s, %s, %s", rs, rt, s);
}


static char *BGEIC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_16_15_14_13_12_11(instruction);
    int64 s_value = extract_s__se11_0_10_9_8_7_6_5_4_3_2_1_0_s1(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BGEIC %s, 0x%" PRIx64 ", %s", rt, u_value, s);
}


static char *BGEIUC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_16_15_14_13_12_11(instruction);
    int64 s_value = extract_s__se11_0_10_9_8_7_6_5_4_3_2_1_0_s1(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BGEIUC %s, 0x%" PRIx64 ", %s", rt, u_value, s);
}


static char *BGEUC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BGEUC %s, %s, %s", rs, rt, s);
}


static char *BLTC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BLTC %s, %s, %s", rs, rt, s);
}


static char *BLTIC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_16_15_14_13_12_11(instruction);
    int64 s_value = extract_s__se11_0_10_9_8_7_6_5_4_3_2_1_0_s1(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BLTIC %s, 0x%" PRIx64 ", %s", rt, u_value, s);
}


static char *BLTIUC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_16_15_14_13_12_11(instruction);
    int64 s_value = extract_s__se11_0_10_9_8_7_6_5_4_3_2_1_0_s1(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BLTIUC %s, 0x%" PRIx64 ", %s", rt, u_value, s);
}


static char *BLTUC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BLTUC %s, %s, %s", rs, rt, s);
}


static char *BNEC_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_3_2_1_0__s1(instruction);

    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);
    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    g_autofree char *u = ADDRESS(u_value, 2, info);

    return img_format("BNEC %s, %s, %s", rs3, rt3, u);
}


static char *BNEC_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BNEC %s, %s, %s", rs, rt, s);
}


static char *BNEIC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_16_15_14_13_12_11(instruction);
    int64 s_value = extract_s__se11_0_10_9_8_7_6_5_4_3_2_1_0_s1(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BNEIC %s, 0x%" PRIx64 ", %s", rt, u_value, s);
}


static char *BNEZC_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    int64 s_value = extract_s__se7_0_6_5_4_3_2_1_s1(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    g_autofree char *s = ADDRESS(s_value, 2, info);

    return img_format("BNEZC %s, %s", rt3, s);
}


static char *BPOSGE32C(uint64 instruction, Dis_info *info)
{
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BPOSGE32C %s", s);
}


static char *BREAK_16_(uint64 instruction, Dis_info *info)
{
    uint64 code_value = extract_code_2_1_0(instruction);


    return img_format("BREAK 0x%" PRIx64, code_value);
}


static char *BREAK_32_(uint64 instruction, Dis_info *info)
{
    uint64 code_value = extract_code_18_to_0(instruction);


    return img_format("BREAK 0x%" PRIx64, code_value);
}


static char *BRSC(uint64 instruction, Dis_info *info)
{
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("BRSC %s", rs);
}


static char *CACHE(uint64 instruction, Dis_info *info)
{
    uint64 op_value = extract_op_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("CACHE 0x%" PRIx64 ", %" PRId64 "(%s)",
                      op_value, s_value, rs);
}


static char *CACHEE(uint64 instruction, Dis_info *info)
{
    uint64 op_value = extract_op_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("CACHEE 0x%" PRIx64 ", %" PRId64 "(%s)",
                      op_value, s_value, rs);
}


static char *CEIL_L_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CEIL.L.D %s, %s", ft, fs);
}


static char *CEIL_L_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CEIL.L.S %s, %s", ft, fs);
}


static char *CEIL_W_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CEIL.W.D %s, %s", ft, fs);
}


static char *CEIL_W_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CEIL.W.S %s, %s", ft, fs);
}


static char *CFC1(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("CFC1 %s, CP%" PRIu64, rt, cs_value);
}


static char *CFC2(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("CFC2 %s, CP%" PRIu64, rt, cs_value);
}


static char *CLASS_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CLASS.D %s, %s", ft, fs);
}


static char *CLASS_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CLASS.S %s, %s", ft, fs);
}


static char *CLO(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("CLO %s, %s", rt, rs);
}


static char *CLZ(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("CLZ %s, %s", rt, rs);
}


static char *CMP_AF_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.AF.D %s, %s, %s", fd, fs, ft);
}


static char *CMP_AF_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.AF.S %s, %s, %s", fd, fs, ft);
}


static char *CMP_EQ_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.EQ.D %s, %s, %s", fd, fs, ft);
}


static char *CMP_EQ_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("CMP.EQ.PH %s, %s", rs, rt);
}


static char *CMP_EQ_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.EQ.S %s, %s, %s", fd, fs, ft);
}


static char *CMP_LE_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.LE.D %s, %s, %s", fd, fs, ft);
}


static char *CMP_LE_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("CMP.LE.PH %s, %s", rs, rt);
}


static char *CMP_LE_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.LE.S %s, %s, %s", fd, fs, ft);
}


static char *CMP_LT_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.LT.D %s, %s, %s", fd, fs, ft);
}


static char *CMP_LT_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("CMP.LT.PH %s, %s", rs, rt);
}


static char *CMP_LT_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.LT.S %s, %s, %s", fd, fs, ft);
}


static char *CMP_NE_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.NE.D %s, %s, %s", fd, fs, ft);
}


static char *CMP_NE_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.NE.S %s, %s, %s", fd, fs, ft);
}


static char *CMP_OR_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.OR.D %s, %s, %s", fd, fs, ft);
}


static char *CMP_OR_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.OR.S %s, %s, %s", fd, fs, ft);
}


static char *CMP_SAF_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SAF.D %s, %s, %s", fd, fs, ft);
}


static char *CMP_SAF_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SAF.S %s, %s, %s", fd, fs, ft);
}


static char *CMP_SEQ_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SEQ.D %s, %s, %s", fd, fs, ft);
}


static char *CMP_SEQ_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SEQ.S %s, %s, %s", fd, fs, ft);
}


static char *CMP_SLE_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SLE.D %s, %s, %s", fd, fs, ft);
}


static char *CMP_SLE_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SLE.S %s, %s, %s", fd, fs, ft);
}


static char *CMP_SLT_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SLT.D %s, %s, %s", fd, fs, ft);
}


static char *CMP_SLT_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SLT.S %s, %s, %s", fd, fs, ft);
}


static char *CMP_SNE_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SNE.D %s, %s, %s", fd, fs, ft);
}


static char *CMP_SNE_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SNE.S %s, %s, %s", fd, fs, ft);
}


static char *CMP_SOR_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SOR.D %s, %s, %s", fd, fs, ft);
}


static char *CMP_SOR_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SOR.S %s, %s, %s", fd, fs, ft);
}


static char *CMP_SUEQ_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SUEQ.D %s, %s, %s", fd, fs, ft);
}


static char *CMP_SUEQ_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SUEQ.S %s, %s, %s", fd, fs, ft);
}


static char *CMP_SULE_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SULE.D %s, %s, %s", fd, fs, ft);
}


static char *CMP_SULE_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SULE.S %s, %s, %s", fd, fs, ft);
}


static char *CMP_SULT_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SULT.D %s, %s, %s", fd, fs, ft);
}


static char *CMP_SULT_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SULT.S %s, %s, %s", fd, fs, ft);
}


static char *CMP_SUN_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SUN.D %s, %s, %s", fd, fs, ft);
}


static char *CMP_SUNE_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SUNE.D %s, %s, %s", fd, fs, ft);
}


static char *CMP_SUNE_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SUNE.S %s, %s, %s", fd, fs, ft);
}


static char *CMP_SUN_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SUN.S %s, %s, %s", fd, fs, ft);
}


static char *CMP_UEQ_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.UEQ.D %s, %s, %s", fd, fs, ft);
}


static char *CMP_UEQ_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.UEQ.S %s, %s, %s", fd, fs, ft);
}


static char *CMP_ULE_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.ULE.D %s, %s, %s", fd, fs, ft);
}


static char *CMP_ULE_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.ULE.S %s, %s, %s", fd, fs, ft);
}


static char *CMP_ULT_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.ULT.D %s, %s, %s", fd, fs, ft);
}


static char *CMP_ULT_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.ULT.S %s, %s, %s", fd, fs, ft);
}


static char *CMP_UN_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.UN.D %s, %s, %s", fd, fs, ft);
}


static char *CMP_UNE_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.UNE.D %s, %s, %s", fd, fs, ft);
}


static char *CMP_UNE_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.UNE.S %s, %s, %s", fd, fs, ft);
}


static char *CMP_UN_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.UN.S %s, %s, %s", fd, fs, ft);
}


static char *CMPGDU_EQ_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("CMPGDU.EQ.QB %s, %s, %s", rd, rs, rt);
}


static char *CMPGDU_LE_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("CMPGDU.LE.QB %s, %s, %s", rd, rs, rt);
}


static char *CMPGDU_LT_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("CMPGDU.LT.QB %s, %s, %s", rd, rs, rt);
}


static char *CMPGU_EQ_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("CMPGU.EQ.QB %s, %s, %s", rd, rs, rt);
}


static char *CMPGU_LE_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("CMPGU.LE.QB %s, %s, %s", rd, rs, rt);
}


static char *CMPGU_LT_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("CMPGU.LT.QB %s, %s, %s", rd, rs, rt);
}


static char *CMPU_EQ_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("CMPU.EQ.QB %s, %s", rs, rt);
}


static char *CMPU_LE_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("CMPU.LE.QB %s, %s", rs, rt);
}


static char *CMPU_LT_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("CMPU.LT.QB %s, %s", rs, rt);
}


static char *COP2_1(uint64 instruction, Dis_info *info)
{
    uint64 cofun_value = extract_cofun_25_24_23(instruction);


    return img_format("COP2_1 0x%" PRIx64, cofun_value);
}


static char *CTC1(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("CTC1 %s, CP%" PRIu64, rt, cs_value);
}


static char *CTC2(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("CTC2 %s, CP%" PRIu64, rt, cs_value);
}


static char *CVT_D_L(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CVT.D.L %s, %s", ft, fs);
}


static char *CVT_D_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CVT.D.S %s, %s", ft, fs);
}


static char *CVT_D_W(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CVT.D.W %s, %s", ft, fs);
}


static char *CVT_L_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CVT.L.D %s, %s", ft, fs);
}


static char *CVT_L_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CVT.L.S %s, %s", ft, fs);
}


static char *CVT_S_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CVT.S.D %s, %s", ft, fs);
}


static char *CVT_S_L(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CVT.S.L %s, %s", ft, fs);
}


static char *CVT_S_PL(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CVT.S.PL %s, %s", ft, fs);
}


static char *CVT_S_PU(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CVT.S.PU %s, %s", ft, fs);
}


static char *CVT_S_W(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CVT.S.W %s, %s", ft, fs);
}


static char *CVT_W_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CVT.W.D %s, %s", ft, fs);
}


static char *CVT_W_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CVT.W.S %s, %s", ft, fs);
}


static char *DADDIU_48_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    int64 s_value = extract_s__se31_15_to_0_31_to_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("DADDIU %s, %" PRId64, rt, s_value);
}


static char *DADDIU_NEG_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    int64 u = neg_copy(u_value);

    return img_format("DADDIU %s, %s, %" PRId64, rt, rs, u);
}


static char *DADDIU_U12_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("DADDIU %s, %s, 0x%" PRIx64, rt, rs, u_value);
}


static char *DADD(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DADD %s, %s, %s", rd, rs, rt);
}


static char *DADDU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DADDU %s, %s, %s", rd, rs, rt);
}


static char *DCLO(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("DCLO %s, %s", rt, rs);
}


static char *DCLZ(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("DCLZ %s, %s", rt, rs);
}


static char *DDIV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DDIV %s, %s, %s", rd, rs, rt);
}


static char *DDIVU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DDIVU %s, %s, %s", rd, rs, rt);
}


static char *DERET(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("DERET ");
}


static char *DEXTM(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 msbd_value = extract_msbt_10_9_8_7_6(instruction);
    uint64 lsb_value = extract_lsb_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    uint64 msbd = encode_msbd_from_size(msbd_value);

    return img_format("DEXTM %s, %s, 0x%" PRIx64 ", 0x%" PRIx64,
                      rt, rs, lsb_value, msbd);
}


static char *DEXT(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 msbd_value = extract_msbt_10_9_8_7_6(instruction);
    uint64 lsb_value = extract_lsb_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    uint64 msbd = encode_msbd_from_size(msbd_value);

    return img_format("DEXT %s, %s, 0x%" PRIx64 ", 0x%" PRIx64,
                      rt, rs, lsb_value, msbd);
}


static char *DEXTU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 msbd_value = extract_msbt_10_9_8_7_6(instruction);
    uint64 lsb_value = extract_lsb_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    uint64 msbd = encode_msbd_from_size(msbd_value);

    return img_format("DEXTU %s, %s, 0x%" PRIx64 ", 0x%" PRIx64,
                      rt, rs, lsb_value, msbd);
}


static char *DINSM(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 msbd_value = extract_msbt_10_9_8_7_6(instruction);
    uint64 lsb_value = extract_lsb_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("DINSM %s, %s, 0x%" PRIx64 ", 0x%" PRIx64,
                      rt, rs, lsb_value, msbd_value);
}


static char *DINS(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 msbd_value = extract_msbt_10_9_8_7_6(instruction);
    uint64 lsb_value = extract_lsb_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("DINS %s, %s, 0x%" PRIx64 ", 0x%" PRIx64,
                      rt, rs, lsb_value, msbd_value);
}


static char *DINSU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 msbd_value = extract_msbt_10_9_8_7_6(instruction);
    uint64 lsb_value = extract_lsb_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("DINSU %s, %s, 0x%" PRIx64 ", 0x%" PRIx64,
                      rt, rs, lsb_value, msbd_value);
}


static char *DI(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("DI %s", rt);
}


static char *DIV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DIV %s, %s, %s", rd, rs, rt);
}


static char *DIV_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("DIV.D %s, %s, %s", fd, fs, ft);
}


static char *DIV_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("DIV.S %s, %s, %s", fd, fs, ft);
}


static char *DIVU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DIVU %s, %s, %s", rd, rs, rt);
}


static char *DLSA(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);
    uint64 u2_value = extract_u2_10_9(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DLSA %s, %s, %s, 0x%" PRIx64, rd, rs, rt, u2_value);
}


static char *DLUI_48_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    uint64 u_value = extract_u_31_to_0__s32(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("DLUI %s, 0x%" PRIx64, rt, u_value);
}


static char *DMFC0(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("DMFC0 %s, CP%" PRIu64 ", 0x%" PRIx64,
                      rt, c0s_value, sel_value);
}


static char *DMFC1(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("DMFC1 %s, %s", rt, fs);
}


static char *DMFC2(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("DMFC2 %s, CP%" PRIu64, rt, cs_value);
}


static char *DMFGC0(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("DMFGC0 %s, CP%" PRIu64 ", 0x%" PRIx64,
                      rt, c0s_value, sel_value);
}


static char *DMOD(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DMOD %s, %s, %s", rd, rs, rt);
}


static char *DMODU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DMODU %s, %s, %s", rd, rs, rt);
}


static char *DMTC0(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("DMTC0 %s, CP%" PRIu64 ", 0x%" PRIx64,
                      rt, c0s_value, sel_value);
}


static char *DMTC1(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("DMTC1 %s, %s", rt, fs);
}


static char *DMTC2(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("DMTC2 %s, CP%" PRIu64, rt, cs_value);
}


static char *DMTGC0(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("DMTGC0 %s, CP%" PRIu64 ", 0x%" PRIx64,
                      rt, c0s_value, sel_value);
}


static char *DMT(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("DMT %s", rt);
}


static char *DMUH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DMUH %s, %s, %s", rd, rs, rt);
}


static char *DMUHU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DMUHU %s, %s, %s", rd, rs, rt);
}


static char *DMUL(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DMUL %s, %s, %s", rd, rs, rt);
}


static char *DMULU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DMULU %s, %s, %s", rd, rs, rt);
}


static char *DPA_W_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPA.W.PH %s, %s, %s", ac, rs, rt);
}


static char *DPAQ_SA_L_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPAQ_SA.L.W %s, %s, %s", ac, rs, rt);
}


static char *DPAQ_S_W_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPAQ_S.W.PH %s, %s, %s", ac, rs, rt);
}


static char *DPAQX_SA_W_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPAQX_SA.W.PH %s, %s, %s", ac, rs, rt);
}


static char *DPAQX_S_W_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPAQX_S.W.PH %s, %s, %s", ac, rs, rt);
}


static char *DPAU_H_QBL(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPAU.H.QBL %s, %s, %s", ac, rs, rt);
}


static char *DPAU_H_QBR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPAU.H.QBR %s, %s, %s", ac, rs, rt);
}


static char *DPAX_W_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPAX.W.PH %s, %s, %s", ac, rs, rt);
}


static char *DPS_W_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPS.W.PH %s, %s, %s", ac, rs, rt);
}


static char *DPSQ_SA_L_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPSQ_SA.L.W %s, %s, %s", ac, rs, rt);
}


static char *DPSQ_S_W_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPSQ_S.W.PH %s, %s, %s", ac, rs, rt);
}


static char *DPSQX_SA_W_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPSQX_SA.W.PH %s, %s, %s", ac, rs, rt);
}


static char *DPSQX_S_W_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPSQX_S.W.PH %s, %s, %s", ac, rs, rt);
}


static char *DPSU_H_QBL(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPSU.H.QBL %s, %s, %s", ac, rs, rt);
}


static char *DPSU_H_QBR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPSU.H.QBR %s, %s, %s", ac, rs, rt);
}


static char *DPSX_W_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPSX.W.PH %s, %s, %s", ac, rs, rt);
}


static char *DROTR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("DROTR %s, %s, 0x%" PRIx64, rt, rs, shift_value);
}


static char *DROTR32(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("DROTR32 %s, %s, 0x%" PRIx64, rt, rs, shift_value);
}


static char *DROTRV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DROTRV %s, %s, %s", rd, rs, rt);
}


static char *DROTX(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shiftx_value = extract_shiftx_11_10_9_8_7_6(instruction);
    uint64 shift_value = extract_shift_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("DROTX %s, %s, 0x%" PRIx64 ", 0x%" PRIx64,
                      rt, rs, shift_value, shiftx_value);
}


static char *DSLL(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("DSLL %s, %s, 0x%" PRIx64, rt, rs, shift_value);
}


static char *DSLL32(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("DSLL32 %s, %s, 0x%" PRIx64, rt, rs, shift_value);
}


static char *DSLLV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DSLLV %s, %s, %s", rd, rs, rt);
}


static char *DSRA(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("DSRA %s, %s, 0x%" PRIx64, rt, rs, shift_value);
}


static char *DSRA32(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("DSRA32 %s, %s, 0x%" PRIx64, rt, rs, shift_value);
}


static char *DSRAV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DSRAV %s, %s, %s", rd, rs, rt);
}


static char *DSRL(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("DSRL %s, %s, 0x%" PRIx64, rt, rs, shift_value);
}


static char *DSRL32(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("DSRL32 %s, %s, 0x%" PRIx64, rt, rs, shift_value);
}


static char *DSRLV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DSRLV %s, %s, %s", rd, rs, rt);
}


static char *DSUB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DSUB %s, %s, %s", rd, rs, rt);
}


static char *DSUBU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DSUBU %s, %s, %s", rd, rs, rt);
}


static char *DVPE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("DVPE %s", rt);
}


static char *DVP(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("DVP %s", rt);
}


static char *EHB(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("EHB ");
}


static char *EI(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("EI %s", rt);
}


static char *EMT(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("EMT %s", rt);
}


static char *ERET(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("ERET ");
}


static char *ERETNC(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("ERETNC ");
}


static char *EVP(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("EVP %s", rt);
}


static char *EVPE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("EVPE %s", rt);
}


static char *EXT(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 msbd_value = extract_msbt_10_9_8_7_6(instruction);
    uint64 lsb_value = extract_lsb_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    uint64 msbd = encode_msbd_from_size(msbd_value);

    return img_format("EXT %s, %s, 0x%" PRIx64 ", 0x%" PRIx64,
                      rt, rs, lsb_value, msbd);
}


static char *EXTD(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);
    uint64 shift_value = extract_shift_10_9_8_7_6(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("EXTD %s, %s, %s, 0x%" PRIx64, rd, rs, rt, shift_value);
}


static char *EXTD32(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);
    uint64 shift_value = extract_shift_10_9_8_7_6(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("EXTD32 %s, %s, %s, 0x%" PRIx64, rd, rs, rt, shift_value);
}


static char *EXTPDP(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 size_value = extract_size_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);

    return img_format("EXTPDP %s, %s, 0x%" PRIx64, rt, ac, size_value);
}


static char *EXTPDPV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("EXTPDPV %s, %s, %s", rt, ac, rs);
}


static char *EXTP(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 size_value = extract_size_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);

    return img_format("EXTP %s, %s, 0x%" PRIx64, rt, ac, size_value);
}


static char *EXTPV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("EXTPV %s, %s, %s", rt, ac, rs);
}


static char *EXTR_RS_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 shift_value = extract_shift_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);

    return img_format("EXTR_RS.W %s, %s, 0x%" PRIx64, rt, ac, shift_value);
}


static char *EXTR_R_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 shift_value = extract_shift_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);

    return img_format("EXTR_R.W %s, %s, 0x%" PRIx64, rt, ac, shift_value);
}


static char *EXTR_S_H(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 shift_value = extract_shift_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);

    return img_format("EXTR_S.H %s, %s, 0x%" PRIx64, rt, ac, shift_value);
}


static char *EXTR_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 shift_value = extract_shift_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);

    return img_format("EXTR.W %s, %s, 0x%" PRIx64, rt, ac, shift_value);
}


static char *EXTRV_RS_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("EXTRV_RS.W %s, %s, %s", rt, ac, rs);
}


static char *EXTRV_R_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("EXTRV_R.W %s, %s, %s", rt, ac, rs);
}


static char *EXTRV_S_H(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("EXTRV_S.H %s, %s, %s", rt, ac, rs);
}


static char *EXTRV_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("EXTRV.W %s, %s, %s", rt, ac, rs);
}


static char *EXTW(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);
    uint64 shift_value = extract_shift_10_9_8_7_6(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("EXTW %s, %s, %s, 0x%" PRIx64, rd, rs, rt, shift_value);
}


static char *FLOOR_L_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("FLOOR.L.D %s, %s", ft, fs);
}


static char *FLOOR_L_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("FLOOR.L.S %s, %s", ft, fs);
}


static char *FLOOR_W_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("FLOOR.W.D %s, %s", ft, fs);
}


static char *FLOOR_W_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("FLOOR.W.S %s, %s", ft, fs);
}


static char *FORK(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("FORK %s, %s, %s", rd, rs, rt);
}


static char *HYPCALL(uint64 instruction, Dis_info *info)
{
    uint64 code_value = extract_code_17_to_0(instruction);


    return img_format("HYPCALL 0x%" PRIx64, code_value);
}


static char *HYPCALL_16_(uint64 instruction, Dis_info *info)
{
    uint64 code_value = extract_code_1_0(instruction);


    return img_format("HYPCALL 0x%" PRIx64, code_value);
}


static char *INS(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 msbd_value = extract_msbt_10_9_8_7_6(instruction);
    uint64 lsb_value = extract_lsb_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("INS %s, %s, 0x%" PRIx64 ", 0x%" PRIx64,
                      rt, rs, lsb_value, msbd_value);
}


static char *INSV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("INSV %s, %s", rt, rs);
}


static char *IRET(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("IRET ");
}


static char *JALRC_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_9_8_7_6_5(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("JALRC $%d, %s", 31, rt);
}


static char *JALRC_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("JALRC %s, %s", rt, rs);
}


static char *JALRC_HB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("JALRC.HB %s, %s", rt, rs);
}


static char *JRC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_9_8_7_6_5(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("JRC %s", rt);
}


static char *LB_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_1_0(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);

    return img_format("LB %s, 0x%" PRIx64 "(%s)", rt3, u_value, rs3);
}


static char *LB_GP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_0(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("LB %s, 0x%" PRIx64 "($%d)", rt, u_value, 28);
}


static char *LB_S9_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LB %s, %" PRId64 "(%s)", rt, s_value, rs);
}


static char *LB_U12_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LB %s, 0x%" PRIx64 "(%s)", rt, u_value, rs);
}


static char *LBE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LBE %s, %" PRId64 "(%s)", rt, s_value, rs);
}


static char *LBU_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_1_0(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);

    return img_format("LBU %s, 0x%" PRIx64 "(%s)", rt3, u_value, rs3);
}


static char *LBU_GP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_0(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("LBU %s, 0x%" PRIx64 "($%d)", rt, u_value, 28);
}


static char *LBU_S9_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LBU %s, %" PRId64 "(%s)", rt, s_value, rs);
}


static char *LBU_U12_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LBU %s, 0x%" PRIx64 "(%s)", rt, u_value, rs);
}


static char *LBUE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LBUE %s, %" PRId64 "(%s)", rt, s_value, rs);
}


static char *LBUX(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LBUX %s, %s(%s)", rd, rs, rt);
}


static char *LBX(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LBX %s, %s(%s)", rd, rs, rt);
}


static char *LD_GP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_20_to_3__s3(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("LD %s, 0x%" PRIx64 "($%d)", rt, u_value, 28);
}


static char *LD_S9_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LD %s, %" PRId64 "(%s)", rt, s_value, rs);
}


static char *LD_U12_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LD %s, 0x%" PRIx64 "(%s)", rt, u_value, rs);
}


static char *LDC1_GP_(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_2__s2(instruction);

    const char *ft = FPR(ft_value, info);

    return img_format("LDC1 %s, 0x%" PRIx64 "($%d)", ft, u_value, 28);
}


static char *LDC1_S9_(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LDC1 %s, %" PRId64 "(%s)", ft, s_value, rs);
}


static char *LDC1_U12_(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LDC1 %s, 0x%" PRIx64 "(%s)", ft, u_value, rs);
}


static char *LDC1XS(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ft_value = extract_ft_15_14_13_12_11(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LDC1XS %s, %s(%s)", ft, rs, rt);
}


static char *LDC1X(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ft_value = extract_ft_15_14_13_12_11(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LDC1X %s, %s(%s)", ft, rs, rt);
}


static char *LDC2(uint64 instruction, Dis_info *info)
{
    uint64 ct_value = extract_ct_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("LDC2 CP%" PRIu64 ", %" PRId64 "(%s)",
                      ct_value, s_value, rs);
}


static char *LDM(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);
    uint64 count3_value = extract_count3_14_13_12(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    uint64 count3 = encode_count3_from_count(count3_value);

    return img_format("LDM %s, %" PRId64 "(%s), 0x%" PRIx64,
                      rt, s_value, rs, count3);
}


static char *LDPC_48_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    int64 s_value = extract_s__se31_15_to_0_31_to_16(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 6, info);

    return img_format("LDPC %s, %s", rt, s);
}


static char *LDX(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LDX %s, %s(%s)", rd, rs, rt);
}


static char *LDXS(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LDXS %s, %s(%s)", rd, rs, rt);
}


static char *LH_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_2_1__s1(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);

    return img_format("LH %s, 0x%" PRIx64 "(%s)", rt3, u_value, rs3);
}


static char *LH_GP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_1__s1(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("LH %s, 0x%" PRIx64 "($%d)", rt, u_value, 28);
}


static char *LH_S9_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LH %s, %" PRId64 "(%s)", rt, s_value, rs);
}


static char *LH_U12_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LH %s, 0x%" PRIx64 "(%s)", rt, u_value, rs);
}


static char *LHE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LHE %s, %" PRId64 "(%s)", rt, s_value, rs);
}


static char *LHU_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_2_1__s1(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);

    return img_format("LHU %s, 0x%" PRIx64 "(%s)", rt3, u_value, rs3);
}


static char *LHU_GP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_1__s1(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("LHU %s, 0x%" PRIx64 "($%d)", rt, u_value, 28);
}


static char *LHU_S9_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LHU %s, %" PRId64 "(%s)", rt, s_value, rs);
}


static char *LHU_U12_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LHU %s, 0x%" PRIx64 "(%s)", rt, u_value, rs);
}


static char *LHUE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LHUE %s, %" PRId64 "(%s)", rt, s_value, rs);
}


static char *LHUX(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LHUX %s, %s(%s)", rd, rs, rt);
}


static char *LHUXS(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LHUXS %s, %s(%s)", rd, rs, rt);
}


static char *LHXS(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LHXS %s, %s(%s)", rd, rs, rt);
}


static char *LHX(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LHX %s, %s(%s)", rd, rs, rt);
}


static char *LI_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 eu_value = extract_eu_6_5_4_3_2_1_0(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    int64 eu = encode_eu_from_s_li16(eu_value);

    return img_format("LI %s, %" PRId64, rt3, eu);
}


static char *LI_48_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    int64 s_value = extract_s__se31_15_to_0_31_to_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("LI %s, %" PRId64, rt, s_value);
}


static char *LL(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_s2(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LL %s, %" PRId64 "(%s)", rt, s_value, rs);
}


static char *LLD(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_s3(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LLD %s, %" PRId64 "(%s)", rt, s_value, rs);
}


static char *LLDP(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ru_value = extract_ru_7_6_5_4_3(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ru = GPR(ru_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LLDP %s, %s, (%s)", rt, ru, rs);
}


static char *LLE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_s2(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LLE %s, %" PRId64 "(%s)", rt, s_value, rs);
}


static char *LLWP(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ru_value = extract_ru_7_6_5_4_3(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ru = GPR(ru_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LLWP %s, %s, (%s)", rt, ru, rs);
}


static char *LLWPE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ru_value = extract_ru_7_6_5_4_3(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ru = GPR(ru_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LLWPE %s, %s, (%s)", rt, ru, rs);
}


static char *LSA(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);
    uint64 u2_value = extract_u2_10_9(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LSA %s, %s, %s, 0x%" PRIx64, rd, rs, rt, u2_value);
}


static char *LUI(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    int64 s_value = extract_s__se31_0_11_to_2_20_to_12_s12(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("LUI %s, %%hi(%" PRId64 ")", rt, s_value);
}


static char *LW_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_3_2_1_0__s2(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);

    return img_format("LW %s, 0x%" PRIx64 "(%s)", rt3, u_value, rs3);
}


static char *LW_4X4_(uint64 instruction, Dis_info *info)
{
    uint64 rt4_value = extract_rt4_9_7_6_5(instruction);
    uint64 rs4_value = extract_rs4_4_2_1_0(instruction);
    uint64 u_value = extract_u_3_8__s2(instruction);

    const char *rt4 = GPR(decode_gpr_gpr4(rt4_value, info), info);
    const char *rs4 = GPR(decode_gpr_gpr4(rs4_value, info), info);

    return img_format("LW %s, 0x%" PRIx64 "(%s)", rt4, u_value, rs4);
}


static char *LW_GP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_20_to_2__s2(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("LW %s, 0x%" PRIx64 "($%d)", rt, u_value, 28);
}


static char *LW_GP16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 u_value = extract_u_6_5_4_3_2_1_0__s2(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);

    return img_format("LW %s, 0x%" PRIx64 "($%d)", rt3, u_value, 28);
}


static char *LW_S9_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LW %s, %" PRId64 "(%s)", rt, s_value, rs);
}


static char *LW_SP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_9_8_7_6_5(instruction);
    uint64 u_value = extract_u_4_3_2_1_0__s2(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("LW %s, 0x%" PRIx64 "($%d)", rt, u_value, 29);
}


static char *LW_U12_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LW %s, 0x%" PRIx64 "(%s)", rt, u_value, rs);
}


static char *LWC1_GP_(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_2__s2(instruction);

    const char *ft = FPR(ft_value, info);

    return img_format("LWC1 %s, 0x%" PRIx64 "($%d)", ft, u_value, 28);
}


static char *LWC1_S9_(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LWC1 %s, %" PRId64 "(%s)", ft, s_value, rs);
}


static char *LWC1_U12_(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LWC1 %s, 0x%" PRIx64 "(%s)", ft, u_value, rs);
}


static char *LWC1X(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ft_value = extract_ft_15_14_13_12_11(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LWC1X %s, %s(%s)", ft, rs, rt);
}


static char *LWC1XS(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ft_value = extract_ft_15_14_13_12_11(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LWC1XS %s, %s(%s)", ft, rs, rt);
}


static char *LWC2(uint64 instruction, Dis_info *info)
{
    uint64 ct_value = extract_ct_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("LWC2 CP%" PRIu64 ", %" PRId64 "(%s)",
                      ct_value, s_value, rs);
}


static char *LWE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LWE %s, %" PRId64 "(%s)", rt, s_value, rs);
}


static char *LWM(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);
    uint64 count3_value = extract_count3_14_13_12(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    uint64 count3 = encode_count3_from_count(count3_value);

    return img_format("LWM %s, %" PRId64 "(%s), 0x%" PRIx64,
                      rt, s_value, rs, count3);
}


static char *LWPC_48_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    int64 s_value = extract_s__se31_15_to_0_31_to_16(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 6, info);

    return img_format("LWPC %s, %s", rt, s);
}


static char *LWU_GP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_2__s2(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("LWU %s, 0x%" PRIx64 "($%d)", rt, u_value, 28);
}


static char *LWU_S9_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LWU %s, %" PRId64 "(%s)", rt, s_value, rs);
}


static char *LWU_U12_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LWU %s, 0x%" PRIx64 "(%s)", rt, u_value, rs);
}


static char *LWUX(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LWUX %s, %s(%s)", rd, rs, rt);
}


static char *LWUXS(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LWUXS %s, %s(%s)", rd, rs, rt);
}


static char *LWX(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LWX %s, %s(%s)", rd, rs, rt);
}


static char *LWXS_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 rd3_value = extract_rd3_3_2_1(instruction);

    const char *rd3 = GPR(decode_gpr_gpr3(rd3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);
    uint64 rt3 = decode_gpr_gpr3(rt3_value, info);

    return img_format("LWXS %s, %s(0x%" PRIx64 ")", rd3, rs3, rt3);
}


static char *LWXS_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LWXS %s, %s(%s)", rd, rs, rt);
}


static char *MADD_DSP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MADD %s, %s, %s", ac, rs, rt);
}


static char *MADDF_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MADDF.D %s, %s, %s", fd, fs, ft);
}


static char *MADDF_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MADDF.S %s, %s, %s", fd, fs, ft);
}


static char *MADDU_DSP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MADDU %s, %s, %s", ac, rs, rt);
}


static char *MAQ_S_W_PHL(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MAQ_S.W.PHL %s, %s, %s", ac, rs, rt);
}


static char *MAQ_S_W_PHR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MAQ_S.W.PHR %s, %s, %s", ac, rs, rt);
}


static char *MAQ_SA_W_PHL(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MAQ_SA.W.PHL %s, %s, %s", ac, rs, rt);
}


static char *MAQ_SA_W_PHR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MAQ_SA.W.PHR %s, %s, %s", ac, rs, rt);
}


static char *MAX_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MAX.D %s, %s, %s", fd, fs, ft);
}


static char *MAX_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MAX.S %s, %s, %s", fd, fs, ft);
}


static char *MAXA_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MAXA.D %s, %s, %s", fd, fs, ft);
}


static char *MAXA_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MAXA.S %s, %s, %s", fd, fs, ft);
}


static char *MFC0(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MFC0 %s, CP%" PRIu64 ", 0x%" PRIx64,
                      rt, c0s_value, sel_value);
}


static char *MFC1(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("MFC1 %s, %s", rt, fs);
}


static char *MFC2(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MFC2 %s, CP%" PRIu64, rt, cs_value);
}


static char *MFGC0(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MFGC0 %s, CP%" PRIu64 ", 0x%" PRIx64,
                      rt, c0s_value, sel_value);
}


static char *MFHC0(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MFHC0 %s, CP%" PRIu64 ", 0x%" PRIx64,
                      rt, c0s_value, sel_value);
}


static char *MFHC1(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("MFHC1 %s, %s", rt, fs);
}


static char *MFHC2(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MFHC2 %s, CP%" PRIu64, rt, cs_value);
}


static char *MFHGC0(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MFHGC0 %s, CP%" PRIu64 ", 0x%" PRIx64,
                      rt, c0s_value, sel_value);
}


static char *MFHI_DSP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);

    return img_format("MFHI %s, %s", rt, ac);
}


static char *MFHTR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);
    uint64 u_value = extract_u_10(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MFHTR %s, 0x%" PRIx64 ", 0x%" PRIx64 ", 0x%" PRIx64,
                      rt, c0s_value, u_value, sel_value);
}


static char *MFLO_DSP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);

    return img_format("MFLO %s, %s", rt, ac);
}


static char *MFTR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);
    uint64 u_value = extract_u_10(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MFTR %s, 0x%" PRIx64 ", 0x%" PRIx64 ", 0x%" PRIx64,
                      rt, c0s_value, u_value, sel_value);
}


static char *MIN_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MIN.D %s, %s, %s", fd, fs, ft);
}


static char *MIN_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MIN.S %s, %s, %s", fd, fs, ft);
}


static char *MINA_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MINA.D %s, %s, %s", fd, fs, ft);
}


static char *MINA_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MINA.S %s, %s, %s", fd, fs, ft);
}


static char *MOD(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MOD %s, %s, %s", rd, rs, rt);
}


static char *MODSUB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MODSUB %s, %s, %s", rd, rs, rt);
}


static char *MODU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MODU %s, %s, %s", rd, rs, rt);
}


static char *MOV_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("MOV.D %s, %s", ft, fs);
}


static char *MOV_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("MOV.S %s, %s", ft, fs);
}


static char *MOVE_BALC(uint64 instruction, Dis_info *info)
{
    uint64 rtz4_value = extract_rtz4_27_26_25_23_22_21(instruction);
    uint64 rd1_value = extract_rdl_25_24(instruction);
    int64 s_value = extract_s__se21_0_20_to_1_s1(instruction);

    const char *rd1 = GPR(decode_gpr_gpr1(rd1_value, info), info);
    const char *rtz4 = GPR(decode_gpr_gpr4_zero(rtz4_value, info), info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("MOVE.BALC %s, %s, %s", rd1, rtz4, s);
}


static char *MOVEP(uint64 instruction, Dis_info *info)
{
    uint64 rtz4_value = extract_rtz4_9_7_6_5(instruction);
    uint64 rd2_value = extract_rd2_3_8(instruction);
    uint64 rsz4_value = extract_rsz4_4_2_1_0(instruction);

    const char *rd2 = GPR(decode_gpr_gpr2_reg1(rd2_value, info), info);
    const char *re2 = GPR(decode_gpr_gpr2_reg2(rd2_value, info), info);
    const char *rsz4 = GPR(decode_gpr_gpr4_zero(rsz4_value, info), info);
    const char *rtz4 = GPR(decode_gpr_gpr4_zero(rtz4_value, info), info);

    return img_format("MOVEP %s, %s, %s, %s", rd2, re2, rsz4, rtz4);
}


static char *MOVEP_REV_(uint64 instruction, Dis_info *info)
{
    uint64 rt4_value = extract_rt4_9_7_6_5(instruction);
    uint64 rd2_value = extract_rd2_3_8(instruction);
    uint64 rs4_value = extract_rs4_4_2_1_0(instruction);

    const char *rs4 = GPR(decode_gpr_gpr4(rs4_value, info), info);
    const char *rt4 = GPR(decode_gpr_gpr4(rt4_value, info), info);
    const char *rd2 = GPR(decode_gpr_gpr2_reg1(rd2_value, info), info);
    const char *rs2 = GPR(decode_gpr_gpr2_reg2(rd2_value, info), info);

    return img_format("MOVEP %s, %s, %s, %s", rs4, rt4, rd2, rs2);
}


static char *MOVE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_9_8_7_6_5(instruction);
    uint64 rs_value = extract_rs_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("MOVE %s, %s", rt, rs);
}


static char *MOVN(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MOVN %s, %s, %s", rd, rs, rt);
}


static char *MOVZ(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MOVZ %s, %s, %s", rd, rs, rt);
}


static char *MSUB_DSP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MSUB %s, %s, %s", ac, rs, rt);
}


static char *MSUBF_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MSUBF.D %s, %s, %s", fd, fs, ft);
}


static char *MSUBF_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MSUBF.S %s, %s, %s", fd, fs, ft);
}


static char *MSUBU_DSP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MSUBU %s, %s, %s", ac, rs, rt);
}


static char *MTC0(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MTC0 %s, CP%" PRIu64 ", 0x%" PRIx64,
                      rt, c0s_value, sel_value);
}


static char *MTC1(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("MTC1 %s, %s", rt, fs);
}


static char *MTC2(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MTC2 %s, CP%" PRIu64, rt, cs_value);
}


static char *MTGC0(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MTGC0 %s, CP%" PRIu64 ", 0x%" PRIx64,
                      rt, c0s_value, sel_value);
}


static char *MTHC0(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MTHC0 %s, CP%" PRIu64 ", 0x%" PRIx64,
                      rt, c0s_value, sel_value);
}


static char *MTHC1(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("MTHC1 %s, %s", rt, fs);
}


static char *MTHC2(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MTHC2 %s, CP%" PRIu64, rt, cs_value);
}


static char *MTHGC0(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MTHGC0 %s, CP%" PRIu64 ", 0x%" PRIx64,
                      rt, c0s_value, sel_value);
}


static char *MTHI_DSP_(uint64 instruction, Dis_info *info)
{
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rs = GPR(rs_value, info);
    const char *ac = AC(ac_value, info);

    return img_format("MTHI %s, %s", rs, ac);
}


static char *MTHLIP(uint64 instruction, Dis_info *info)
{
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rs = GPR(rs_value, info);
    const char *ac = AC(ac_value, info);

    return img_format("MTHLIP %s, %s", rs, ac);
}


static char *MTHTR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);
    uint64 u_value = extract_u_10(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MTHTR %s, 0x%" PRIx64 ", 0x%" PRIx64 ", 0x%" PRIx64,
                      rt, c0s_value, u_value, sel_value);
}


static char *MTLO_DSP_(uint64 instruction, Dis_info *info)
{
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rs = GPR(rs_value, info);
    const char *ac = AC(ac_value, info);

    return img_format("MTLO %s, %s", rs, ac);
}


static char *MTTR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);
    uint64 u_value = extract_u_10(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MTTR %s, 0x%" PRIx64 ", 0x%" PRIx64 ", 0x%" PRIx64,
                      rt, c0s_value, u_value, sel_value);
}


static char *MUH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MUH %s, %s, %s", rd, rs, rt);
}


static char *MUHU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MUHU %s, %s, %s", rd, rs, rt);
}


static char *MUL_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MUL %s, %s, %s", rd, rs, rt);
}


static char *MUL_4X4_(uint64 instruction, Dis_info *info)
{
    uint64 rt4_value = extract_rt4_9_7_6_5(instruction);
    uint64 rs4_value = extract_rs4_4_2_1_0(instruction);

    const char *rs4 = GPR(decode_gpr_gpr4(rs4_value, info), info);
    const char *rt4 = GPR(decode_gpr_gpr4(rt4_value, info), info);

    return img_format("MUL %s, %s", rs4, rt4);
}


static char *MUL_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MUL.D %s, %s, %s", fd, fs, ft);
}


static char *MUL_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MUL.PH %s, %s, %s", rd, rs, rt);
}


static char *MUL_S_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MUL_S.PH %s, %s, %s", rd, rs, rt);
}


static char *MUL_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MUL.S %s, %s, %s", fd, fs, ft);
}


static char *MULEQ_S_W_PHL(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MULEQ_S.W.PHL %s, %s, %s", rd, rs, rt);
}


static char *MULEQ_S_W_PHR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MULEQ_S.W.PHR %s, %s, %s", rd, rs, rt);
}


static char *MULEU_S_PH_QBL(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MULEU_S.PH.QBL %s, %s, %s", rd, rs, rt);
}


static char *MULEU_S_PH_QBR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MULEU_S.PH.QBR %s, %s, %s", rd, rs, rt);
}


static char *MULQ_RS_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MULQ_RS.PH %s, %s, %s", rd, rs, rt);
}


static char *MULQ_RS_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MULQ_RS.W %s, %s, %s", rd, rs, rt);
}


static char *MULQ_S_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MULQ_S.PH %s, %s, %s", rd, rs, rt);
}


static char *MULQ_S_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MULQ_S.W %s, %s, %s", rd, rs, rt);
}


static char *MULSA_W_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MULSA.W.PH %s, %s, %s", ac, rs, rt);
}


static char *MULSAQ_S_W_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MULSAQ_S.W.PH %s, %s, %s", ac, rs, rt);
}


static char *MULT_DSP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MULT %s, %s, %s", ac, rs, rt);
}


static char *MULTU_DSP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MULTU %s, %s, %s", ac, rs, rt);
}


static char *MULU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MULU %s, %s, %s", rd, rs, rt);
}


static char *NEG_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("NEG.D %s, %s", ft, fs);
}


static char *NEG_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("NEG.S %s, %s", ft, fs);
}


static char *NOP_16_(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("NOP ");
}


static char *NOP_32_(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("NOP ");
}


static char *NOR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("NOR %s, %s, %s", rd, rs, rt);
}


static char *NOT_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);

    return img_format("NOT %s, %s", rt3, rs3);
}


static char *OR_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);

    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);
    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);

    return img_format("OR %s, %s", rs3, rt3);
}


static char *OR_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("OR %s, %s, %s", rd, rs, rt);
}


static char *ORI(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("ORI %s, %s, 0x%" PRIx64, rt, rs, u_value);
}


static char *PACKRL_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("PACKRL.PH %s, %s, %s", rd, rs, rt);
}


static char *PAUSE(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("PAUSE ");
}


static char *PICK_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("PICK.PH %s, %s, %s", rd, rs, rt);
}


static char *PICK_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("PICK.QB %s, %s, %s", rd, rs, rt);
}


static char *PRECEQ_W_PHL(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("PRECEQ.W.PHL %s, %s", rt, rs);
}


static char *PRECEQ_W_PHR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("PRECEQ.W.PHR %s, %s", rt, rs);
}


static char *PRECEQU_PH_QBLA(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("PRECEQU.PH.QBLA %s, %s", rt, rs);
}


static char *PRECEQU_PH_QBL(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("PRECEQU.PH.QBL %s, %s", rt, rs);
}


static char *PRECEQU_PH_QBRA(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("PRECEQU.PH.QBRA %s, %s", rt, rs);
}


static char *PRECEQU_PH_QBR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("PRECEQU.PH.QBR %s, %s", rt, rs);
}


static char *PRECEU_PH_QBLA(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("PRECEU.PH.QBLA %s, %s", rt, rs);
}


static char *PRECEU_PH_QBL(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("PRECEU.PH.QBL %s, %s", rt, rs);
}


static char *PRECEU_PH_QBRA(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("PRECEU.PH.QBRA %s, %s", rt, rs);
}


static char *PRECEU_PH_QBR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("PRECEU.PH.QBR %s, %s", rt, rs);
}


static char *PRECR_QB_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("PRECR.QB.PH %s, %s, %s", rd, rs, rt);
}


static char *PRECR_SRA_PH_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("PRECR_SRA.PH.W %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


static char *PRECR_SRA_R_PH_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("PRECR_SRA_R.PH.W %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


static char *PRECRQ_PH_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("PRECRQ.PH.W %s, %s, %s", rd, rs, rt);
}


static char *PRECRQ_QB_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("PRECRQ.QB.PH %s, %s, %s", rd, rs, rt);
}


static char *PRECRQ_RS_PH_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("PRECRQ_RS.PH.W %s, %s, %s", rd, rs, rt);
}


static char *PRECRQU_S_QB_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("PRECRQU_S.QB.PH %s, %s, %s", rd, rs, rt);
}


static char *PREF_S9_(uint64 instruction, Dis_info *info)
{
    uint64 hint_value = extract_hint_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("PREF 0x%" PRIx64 ", %" PRId64 "(%s)",
                      hint_value, s_value, rs);
}


static char *PREF_U12_(uint64 instruction, Dis_info *info)
{
    uint64 hint_value = extract_hint_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("PREF 0x%" PRIx64 ", 0x%" PRIx64 "(%s)",
                      hint_value, u_value, rs);
}


static char *PREFE(uint64 instruction, Dis_info *info)
{
    uint64 hint_value = extract_hint_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("PREFE 0x%" PRIx64 ", %" PRId64 "(%s)",
                      hint_value, s_value, rs);
}


static char *PREPEND(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("PREPEND %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


static char *RADDU_W_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("RADDU.W.QB %s, %s", rt, rs);
}


static char *RDDSP(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 mask_value = extract_mask_20_19_18_17_16_15_14(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("RDDSP %s, 0x%" PRIx64, rt, mask_value);
}


static char *RDHWR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 hs_value = extract_hs_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("RDHWR %s, CP%" PRIu64 ", 0x%" PRIx64,
                      rt, hs_value, sel_value);
}


static char *RDPGPR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("RDPGPR %s, %s", rt, rs);
}


static char *RECIP_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("RECIP.D %s, %s", ft, fs);
}


static char *RECIP_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("RECIP.S %s, %s", ft, fs);
}


static char *REPL_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    int64 s_value = extract_s__se9_20_19_18_17_16_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("REPL.PH %s, %" PRId64, rt, s_value);
}


static char *REPL_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_20_19_18_17_16_15_14_13(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("REPL.QB %s, 0x%" PRIx64, rt, u_value);
}


static char *REPLV_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("REPLV.PH %s, %s", rt, rs);
}


static char *REPLV_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("REPLV.QB %s, %s", rt, rs);
}


static char *RESTORE_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 count_value = extract_count_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3__s3(instruction);
    uint64 gp_value = extract_gp_2(instruction);

    g_autofree char *save_restore_str = save_restore_list(
        rt_value, count_value, gp_value, info);
    return img_format("RESTORE 0x%" PRIx64 "%s", u_value, save_restore_str);
}


static char *RESTORE_JRC_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt1_value = extract_rtl_11(instruction);
    uint64 u_value = extract_u_7_6_5_4__s4(instruction);
    uint64 count_value = extract_count_3_2_1_0(instruction);

    g_autofree char *save_restore_str = save_restore_list(
        encode_rt1_from_rt(rt1_value), count_value, 0, info);
    return img_format("RESTORE.JRC 0x%" PRIx64 "%s", u_value, save_restore_str);
}


static char *RESTORE_JRC_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 count_value = extract_count_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3__s3(instruction);
    uint64 gp_value = extract_gp_2(instruction);

    g_autofree char *save_restore_str = save_restore_list(
        rt_value, count_value, gp_value, info);
    return img_format("RESTORE.JRC 0x%" PRIx64 "%s", u_value,
                      save_restore_str);
}


static char *RESTOREF(uint64 instruction, Dis_info *info)
{
    uint64 count_value = extract_count_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3__s3(instruction);


    return img_format("RESTOREF 0x%" PRIx64 ", 0x%" PRIx64,
                      u_value, count_value);
}


static char *RINT_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("RINT.D %s, %s", ft, fs);
}


static char *RINT_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("RINT.S %s, %s", ft, fs);
}


static char *ROTR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("ROTR %s, %s, 0x%" PRIx64, rt, rs, shift_value);
}


static char *ROTRV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ROTRV %s, %s, %s", rd, rs, rt);
}


static char *ROTX(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shiftx_value = extract_shiftx_10_9_8_7__s1(instruction);
    uint64 stripe_value = extract_stripe_6(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("ROTX %s, %s, 0x%" PRIx64 ", 0x%" PRIx64 ", 0x%" PRIx64,
                       rt, rs, shift_value, shiftx_value, stripe_value);
}


static char *ROUND_L_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("ROUND.L.D %s, %s", ft, fs);
}


static char *ROUND_L_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("ROUND.L.S %s, %s", ft, fs);
}


static char *ROUND_W_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("ROUND.W.D %s, %s", ft, fs);
}


static char *ROUND_W_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("ROUND.W.S %s, %s", ft, fs);
}


static char *RSQRT_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("RSQRT.D %s, %s", ft, fs);
}


static char *RSQRT_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("RSQRT.S %s, %s", ft, fs);
}


static char *SAVE_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt1_value = extract_rtl_11(instruction);
    uint64 u_value = extract_u_7_6_5_4__s4(instruction);
    uint64 count_value = extract_count_3_2_1_0(instruction);

    g_autofree char *save_restore_str = save_restore_list(
        encode_rt1_from_rt(rt1_value), count_value, 0, info);
    return img_format("SAVE 0x%" PRIx64 "%s", u_value, save_restore_str);
}


static char *SAVE_32_(uint64 instruction, Dis_info *info)
{
    uint64 count_value = extract_count_19_18_17_16(instruction);
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3__s3(instruction);
    uint64 gp_value = extract_gp_2(instruction);

    g_autofree char *save_restore_str = save_restore_list(
        rt_value, count_value, gp_value, info);
    return img_format("SAVE 0x%" PRIx64 "%s", u_value, save_restore_str);
}


static char *SAVEF(uint64 instruction, Dis_info *info)
{
    uint64 count_value = extract_count_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3__s3(instruction);


    return img_format("SAVEF 0x%" PRIx64 ", 0x%" PRIx64, u_value, count_value);
}


static char *SB_16_(uint64 instruction, Dis_info *info)
{
    uint64 rtz3_value = extract_rtz3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_1_0(instruction);

    const char *rtz3 = GPR(decode_gpr_gpr3_src_store(rtz3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);

    return img_format("SB %s, 0x%" PRIx64 "(%s)", rtz3, u_value, rs3);
}


static char *SB_GP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_0(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("SB %s, 0x%" PRIx64 "($%d)", rt, u_value, 28);
}


static char *SB_S9_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SB %s, %" PRId64 "(%s)", rt, s_value, rs);
}


static char *SB_U12_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SB %s, 0x%" PRIx64 "(%s)", rt, u_value, rs);
}


static char *SBE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SBE %s, %" PRId64 "(%s)", rt, s_value, rs);
}


static char *SBX(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SBX %s, %s(%s)", rd, rs, rt);
}


static char *SC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_s2(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SC %s, %" PRId64 "(%s)", rt, s_value, rs);
}


static char *SCD(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_s3(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SCD %s, %" PRId64 "(%s)", rt, s_value, rs);
}


static char *SCDP(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ru_value = extract_ru_7_6_5_4_3(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ru = GPR(ru_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SCDP %s, %s, (%s)", rt, ru, rs);
}


static char *SCE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_s2(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SCE %s, %" PRId64 "(%s)", rt, s_value, rs);
}


static char *SCWP(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ru_value = extract_ru_7_6_5_4_3(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ru = GPR(ru_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SCWP %s, %s, (%s)", rt, ru, rs);
}


static char *SCWPE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ru_value = extract_ru_7_6_5_4_3(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ru = GPR(ru_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SCWPE %s, %s, (%s)", rt, ru, rs);
}


static char *SD_GP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_20_to_3__s3(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("SD %s, 0x%" PRIx64 "($%d)", rt, u_value, 28);
}


static char *SD_S9_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SD %s, %" PRId64 "(%s)", rt, s_value, rs);
}


static char *SD_U12_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SD %s, 0x%" PRIx64 "(%s)", rt, u_value, rs);
}


static char *SDBBP_16_(uint64 instruction, Dis_info *info)
{
    uint64 code_value = extract_code_2_1_0(instruction);


    return img_format("SDBBP 0x%" PRIx64, code_value);
}


static char *SDBBP_32_(uint64 instruction, Dis_info *info)
{
    uint64 code_value = extract_code_18_to_0(instruction);


    return img_format("SDBBP 0x%" PRIx64, code_value);
}


static char *SDC1_GP_(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_2__s2(instruction);

    const char *ft = FPR(ft_value, info);

    return img_format("SDC1 %s, 0x%" PRIx64 "($%d)", ft, u_value, 28);
}


static char *SDC1_S9_(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SDC1 %s, %" PRId64 "(%s)", ft, s_value, rs);
}


static char *SDC1_U12_(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SDC1 %s, 0x%" PRIx64 "(%s)", ft, u_value, rs);
}


static char *SDC1X(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ft_value = extract_ft_15_14_13_12_11(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SDC1X %s, %s(%s)", ft, rs, rt);
}


static char *SDC1XS(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ft_value = extract_ft_15_14_13_12_11(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SDC1XS %s, %s(%s)", ft, rs, rt);
}


static char *SDC2(uint64 instruction, Dis_info *info)
{
    uint64 cs_value = extract_cs_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("SDC2 CP%" PRIu64 ", %" PRId64 "(%s)",
                      cs_value, s_value, rs);
}


static char *SDM(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);
    uint64 count3_value = extract_count3_14_13_12(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    uint64 count3 = encode_count3_from_count(count3_value);

    return img_format("SDM %s, %" PRId64 "(%s), 0x%" PRIx64,
                      rt, s_value, rs, count3);
}


static char *SDPC_48_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    int64 s_value = extract_s__se31_15_to_0_31_to_16(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 6, info);

    return img_format("SDPC %s, %s", rt, s);
}


static char *SDXS(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SDXS %s, %s(%s)", rd, rs, rt);
}


static char *SDX(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SDX %s, %s(%s)", rd, rs, rt);
}


static char *SEB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SEB %s, %s", rt, rs);
}


static char *SEH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SEH %s, %s", rt, rs);
}


static char *SEL_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("SEL.D %s, %s, %s", fd, fs, ft);
}


static char *SEL_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("SEL.S %s, %s, %s", fd, fs, ft);
}


static char *SELEQZ_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("SELEQZ.D %s, %s, %s", fd, fs, ft);
}


static char *SELEQZ_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("SELEQZ.S %s, %s, %s", fd, fs, ft);
}


static char *SELNEZ_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("SELNEZ.D %s, %s, %s", fd, fs, ft);
}


static char *SELNEZ_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("SELNEZ.S %s, %s, %s", fd, fs, ft);
}


static char *SEQI(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SEQI %s, %s, 0x%" PRIx64, rt, rs, u_value);
}


static char *SH_16_(uint64 instruction, Dis_info *info)
{
    uint64 rtz3_value = extract_rtz3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_2_1__s1(instruction);

    const char *rtz3 = GPR(decode_gpr_gpr3_src_store(rtz3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);

    return img_format("SH %s, 0x%" PRIx64 "(%s)", rtz3, u_value, rs3);
}


static char *SH_GP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_1__s1(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("SH %s, 0x%" PRIx64 "($%d)", rt, u_value, 28);
}


static char *SH_S9_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SH %s, %" PRId64 "(%s)", rt, s_value, rs);
}


static char *SH_U12_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SH %s, 0x%" PRIx64 "(%s)", rt, u_value, rs);
}


static char *SHE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHE %s, %" PRId64 "(%s)", rt, s_value, rs);
}


static char *SHILO(uint64 instruction, Dis_info *info)
{
    int64 shift_value = extract_shift__se5_21_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);

    return img_format("SHILO %s, 0x%" PRIx64, ac, shift_value);
}


static char *SHILOV(uint64 instruction, Dis_info *info)
{
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rs = GPR(rs_value, info);
    const char *ac = AC(ac_value, info);

    return img_format("SHILOV %s, %s", ac, rs);
}


static char *SHLL_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHLL.PH %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


static char *SHLL_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHLL.QB %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


static char *SHLL_S_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHLL_S.PH %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


static char *SHLL_S_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHLL_S.W %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


static char *SHLLV_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHLLV.PH %s, %s, %s", rd, rt, rs);
}


static char *SHLLV_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHLLV.QB %s, %s, %s", rd, rt, rs);
}


static char *SHLLV_S_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHLLV_S.PH %s, %s, %s", rd, rt, rs);
}


static char *SHLLV_S_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHLLV_S.W %s, %s, %s", rd, rt, rs);
}


static char *SHRA_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRA.PH %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


static char *SHRA_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRA.QB %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


static char *SHRA_R_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRA_R.PH %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


static char *SHRA_R_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRA_R.QB %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


static char *SHRA_R_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRA_R.W %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


static char *SHRAV_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRAV.PH %s, %s, %s", rd, rt, rs);
}


static char *SHRAV_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRAV.QB %s, %s, %s", rd, rt, rs);
}


static char *SHRAV_R_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRAV_R.PH %s, %s, %s", rd, rt, rs);
}


static char *SHRAV_R_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRAV_R.QB %s, %s, %s", rd, rt, rs);
}


static char *SHRAV_R_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRAV_R.W %s, %s, %s", rd, rt, rs);
}


static char *SHRL_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRL.PH %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


static char *SHRL_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRL.QB %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


static char *SHRLV_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRLV.PH %s, %s, %s", rd, rt, rs);
}


static char *SHRLV_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRLV.QB %s, %s, %s", rd, rt, rs);
}


static char *SHX(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SHX %s, %s(%s)", rd, rs, rt);
}


static char *SHXS(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SHXS %s, %s(%s)", rd, rs, rt);
}


static char *SIGRIE(uint64 instruction, Dis_info *info)
{
    uint64 code_value = extract_code_18_to_0(instruction);


    return img_format("SIGRIE 0x%" PRIx64, code_value);
}


static char *SLL_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 shift3_value = extract_shift3_2_1_0(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);
    uint64 shift3 = encode_shift3_from_shift(shift3_value);

    return img_format("SLL %s, %s, 0x%" PRIx64, rt3, rs3, shift3);
}


static char *SLL_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SLL %s, %s, 0x%" PRIx64, rt, rs, shift_value);
}


static char *SLLV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SLLV %s, %s, %s", rd, rs, rt);
}


static char *SLT(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SLT %s, %s, %s", rd, rs, rt);
}


static char *SLTI(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SLTI %s, %s, 0x%" PRIx64, rt, rs, u_value);
}


static char *SLTIU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SLTIU %s, %s, 0x%" PRIx64, rt, rs, u_value);
}


static char *SLTU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SLTU %s, %s, %s", rd, rs, rt);
}


static char *SOV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SOV %s, %s, %s", rd, rs, rt);
}


static char *SPECIAL2(uint64 instruction, Dis_info *info)
{
    uint64 op_value = extract_op_25_to_3(instruction);


    return img_format("SPECIAL2 0x%" PRIx64, op_value);
}


static char *SQRT_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("SQRT.D %s, %s", ft, fs);
}


static char *SQRT_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("SQRT.S %s, %s", ft, fs);
}


static char *SRA(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SRA %s, %s, 0x%" PRIx64, rt, rs, shift_value);
}


static char *SRAV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SRAV %s, %s, %s", rd, rs, rt);
}


static char *SRL_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 shift3_value = extract_shift3_2_1_0(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);
    uint64 shift3 = encode_shift3_from_shift(shift3_value);

    return img_format("SRL %s, %s, 0x%" PRIx64, rt3, rs3, shift3);
}


static char *SRL_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SRL %s, %s, 0x%" PRIx64, rt, rs, shift_value);
}


static char *SRLV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SRLV %s, %s, %s", rd, rs, rt);
}


static char *SUB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUB %s, %s, %s", rd, rs, rt);
}


static char *SUB_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("SUB.D %s, %s, %s", fd, fs, ft);
}


static char *SUB_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("SUB.S %s, %s, %s", fd, fs, ft);
}


static char *SUBQ_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBQ.PH %s, %s, %s", rd, rs, rt);
}


static char *SUBQ_S_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBQ_S.PH %s, %s, %s", rd, rs, rt);
}


static char *SUBQ_S_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBQ_S.W %s, %s, %s", rd, rs, rt);
}


static char *SUBQH_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBQH.PH %s, %s, %s", rd, rs, rt);
}


static char *SUBQH_R_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBQH_R.PH %s, %s, %s", rd, rs, rt);
}


static char *SUBQH_R_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBQH_R.W %s, %s, %s", rd, rs, rt);
}


static char *SUBQH_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBQH.W %s, %s, %s", rd, rs, rt);
}


static char *SUBU_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 rd3_value = extract_rd3_3_2_1(instruction);

    const char *rd3 = GPR(decode_gpr_gpr3(rd3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);
    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);

    return img_format("SUBU %s, %s, %s", rd3, rs3, rt3);
}


static char *SUBU_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBU %s, %s, %s", rd, rs, rt);
}


static char *SUBU_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBU.PH %s, %s, %s", rd, rs, rt);
}


static char *SUBU_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBU.QB %s, %s, %s", rd, rs, rt);
}


static char *SUBU_S_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBU_S.PH %s, %s, %s", rd, rs, rt);
}


static char *SUBU_S_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBU_S.QB %s, %s, %s", rd, rs, rt);
}


static char *SUBUH_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBUH.QB %s, %s, %s", rd, rs, rt);
}


static char *SUBUH_R_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBUH_R.QB %s, %s, %s", rd, rs, rt);
}


static char *SW_16_(uint64 instruction, Dis_info *info)
{
    uint64 rtz3_value = extract_rtz3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_3_2_1_0__s2(instruction);

    const char *rtz3 = GPR(decode_gpr_gpr3_src_store(rtz3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);

    return img_format("SW %s, 0x%" PRIx64 "(%s)", rtz3, u_value, rs3);
}


static char *SW_4X4_(uint64 instruction, Dis_info *info)
{
    uint64 rtz4_value = extract_rtz4_9_7_6_5(instruction);
    uint64 rs4_value = extract_rs4_4_2_1_0(instruction);
    uint64 u_value = extract_u_3_8__s2(instruction);

    const char *rtz4 = GPR(decode_gpr_gpr4_zero(rtz4_value, info), info);
    const char *rs4 = GPR(decode_gpr_gpr4(rs4_value, info), info);

    return img_format("SW %s, 0x%" PRIx64 "(%s)", rtz4, u_value, rs4);
}


static char *SW_GP16_(uint64 instruction, Dis_info *info)
{
    uint64 u_value = extract_u_6_5_4_3_2_1_0__s2(instruction);
    uint64 rtz3_value = extract_rtz3_9_8_7(instruction);

    const char *rtz3 = GPR(decode_gpr_gpr3_src_store(rtz3_value, info), info);

    return img_format("SW %s, 0x%" PRIx64 "($%d)", rtz3, u_value, 28);
}


static char *SW_GP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_20_to_2__s2(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("SW %s, 0x%" PRIx64 "($%d)", rt, u_value, 28);
}


static char *SW_S9_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SW %s, %" PRId64 "(%s)", rt, s_value, rs);
}


static char *SW_SP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_9_8_7_6_5(instruction);
    uint64 u_value = extract_u_4_3_2_1_0__s2(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("SW %s, 0x%" PRIx64 "($%d)", rt, u_value, 29);
}


static char *SW_U12_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SW %s, 0x%" PRIx64 "(%s)", rt, u_value, rs);
}


static char *SWC1_GP_(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_2__s2(instruction);

    const char *ft = FPR(ft_value, info);

    return img_format("SWC1 %s, 0x%" PRIx64 "($%d)", ft, u_value, 28);
}


static char *SWC1_S9_(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SWC1 %s, %" PRId64 "(%s)", ft, s_value, rs);
}


static char *SWC1_U12_(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SWC1 %s, 0x%" PRIx64 "(%s)", ft, u_value, rs);
}


static char *SWC1X(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ft_value = extract_ft_15_14_13_12_11(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SWC1X %s, %s(%s)", ft, rs, rt);
}


static char *SWC1XS(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ft_value = extract_ft_15_14_13_12_11(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SWC1XS %s, %s(%s)", ft, rs, rt);
}


static char *SWC2(uint64 instruction, Dis_info *info)
{
    uint64 cs_value = extract_cs_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("SWC2 CP%" PRIu64 ", %" PRId64 "(%s)",
                      cs_value, s_value, rs);
}


static char *SWE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SWE %s, %" PRId64 "(%s)", rt, s_value, rs);
}


static char *SWM(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);
    uint64 count3_value = extract_count3_14_13_12(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    uint64 count3 = encode_count3_from_count(count3_value);

    return img_format("SWM %s, %" PRId64 "(%s), 0x%" PRIx64,
                      rt, s_value, rs, count3);
}


static char *SWPC_48_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    int64 s_value = extract_s__se31_15_to_0_31_to_16(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 6, info);

    return img_format("SWPC %s, %s", rt, s);
}


static char *SWX(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SWX %s, %s(%s)", rd, rs, rt);
}


static char *SWXS(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SWXS %s, %s(%s)", rd, rs, rt);
}


static char *SYNC(uint64 instruction, Dis_info *info)
{
    uint64 stype_value = extract_stype_20_19_18_17_16(instruction);


    return img_format("SYNC 0x%" PRIx64, stype_value);
}


static char *SYNCI(uint64 instruction, Dis_info *info)
{
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("SYNCI %" PRId64 "(%s)", s_value, rs);
}


static char *SYNCIE(uint64 instruction, Dis_info *info)
{
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("SYNCIE %" PRId64 "(%s)", s_value, rs);
}


static char *SYSCALL_16_(uint64 instruction, Dis_info *info)
{
    uint64 code_value = extract_code_1_0(instruction);


    return img_format("SYSCALL 0x%" PRIx64, code_value);
}


static char *SYSCALL_32_(uint64 instruction, Dis_info *info)
{
    uint64 code_value = extract_code_17_to_0(instruction);


    return img_format("SYSCALL 0x%" PRIx64, code_value);
}


static char *TEQ(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("TEQ %s, %s", rs, rt);
}


static char *TLBGINV(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("TLBGINV ");
}


static char *TLBGINVF(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("TLBGINVF ");
}


static char *TLBGP(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("TLBGP ");
}


static char *TLBGR(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("TLBGR ");
}


static char *TLBGWI(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("TLBGWI ");
}


static char *TLBGWR(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("TLBGWR ");
}


static char *TLBINV(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("TLBINV ");
}


static char *TLBINVF(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("TLBINVF ");
}


static char *TLBP(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("TLBP ");
}


static char *TLBR(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("TLBR ");
}


static char *TLBWI(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("TLBWI ");
}


static char *TLBWR(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("TLBWR ");
}


static char *TNE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("TNE %s, %s", rs, rt);
}


static char *TRUNC_L_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("TRUNC.L.D %s, %s", ft, fs);
}


static char *TRUNC_L_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("TRUNC.L.S %s, %s", ft, fs);
}


static char *TRUNC_W_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("TRUNC.W.D %s, %s", ft, fs);
}


static char *TRUNC_W_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("TRUNC.W.S %s, %s", ft, fs);
}


static char *UALDM(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);
    uint64 count3_value = extract_count3_14_13_12(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    uint64 count3 = encode_count3_from_count(count3_value);

    return img_format("UALDM %s, %" PRId64 "(%s), 0x%" PRIx64,
                      rt, s_value, rs, count3);
}


static char *UALH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("UALH %s, %" PRId64 "(%s)", rt, s_value, rs);
}


static char *UALWM(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);
    uint64 count3_value = extract_count3_14_13_12(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    uint64 count3 = encode_count3_from_count(count3_value);

    return img_format("UALWM %s, %" PRId64 "(%s), 0x%" PRIx64,
                      rt, s_value, rs, count3);
}


static char *UASDM(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);
    uint64 count3_value = extract_count3_14_13_12(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    uint64 count3 = encode_count3_from_count(count3_value);

    return img_format("UASDM %s, %" PRId64 "(%s), 0x%" PRIx64,
                      rt, s_value, rs, count3);
}


static char *UASH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("UASH %s, %" PRId64 "(%s)", rt, s_value, rs);
}


static char *UASWM(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);
    uint64 count3_value = extract_count3_14_13_12(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    uint64 count3 = encode_count3_from_count(count3_value);

    return img_format("UASWM %s, %" PRId64 "(%s), 0x%" PRIx64,
                      rt, s_value, rs, count3);
}


static char *UDI(uint64 instruction, Dis_info *info)
{
    uint64 op_value = extract_op_25_to_3(instruction);


    return img_format("UDI 0x%" PRIx64, op_value);
}


static char *WAIT(uint64 instruction, Dis_info *info)
{
    uint64 code_value = extract_code_25_24_23_22_21_20_19_18_17_16(instruction);


    return img_format("WAIT 0x%" PRIx64, code_value);
}


static char *WRDSP(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 mask_value = extract_mask_20_19_18_17_16_15_14(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("WRDSP %s, 0x%" PRIx64, rt, mask_value);
}


static char *WRPGPR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("WRPGPR %s, %s", rt, rs);
}


static char *XOR_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);

    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);
    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);

    return img_format("XOR %s, %s", rs3, rt3);
}


static char *XOR_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("XOR %s, %s, %s", rd, rs, rt);
}


static char *XORI(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("XORI %s, %s, 0x%" PRIx64, rt, rs, u_value);
}


static char *YIELD(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("YIELD %s, %s", rt, rs);
}



typedef enum  {
    instruction,
    call_instruction,
    branch_instruction,
    return_instruction,
    reserved_block,
    pool,
} TABLE_ENTRY_TYPE;

typedef enum {
    MIPS64_    = 0x00000001,
    XNP_       = 0x00000002,
    XMMS_      = 0x00000004,
    EVA_       = 0x00000008,
    DSP_       = 0x00000010,
    MT_        = 0x00000020,
    EJTAG_     = 0x00000040,
    TLBINV_    = 0x00000080,
    CP0_       = 0x00000100,
    CP1_       = 0x00000200,
    CP2_       = 0x00000400,
    UDI_       = 0x00000800,
    MCU_       = 0x00001000,
    VZ_        = 0x00002000,
    TLB_       = 0x00004000,
    MVH_       = 0x00008000,
    ALL_ATTRIBUTES = 0xffffffffull,
} TABLE_ATTRIBUTE_TYPE;

typedef bool (*conditional_function)(uint64 instruction);
typedef char * (*disassembly_function)(uint64 instruction,
                                            Dis_info *info);

typedef struct Pool {
    TABLE_ENTRY_TYPE     type;
    const struct Pool    *next_table;
    int                  next_table_size;
    int                  instructions_size;
    uint64               mask;
    uint64               value;
    disassembly_function disassembly;
    conditional_function condition;
    uint64               attributes;
} Pool;

static const Pool P_SYSCALL[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfffc0000, 0x00080000, &SYSCALL_32_      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfffc0000, 0x000c0000, &HYPCALL          , 0,
       CP0_ | VZ_          },
};


static const Pool P_RI[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfff80000, 0x00000000, &SIGRIE           , 0,
       0x0                 },
    { pool                , P_SYSCALL           , 2   , 32,
       0xfff80000, 0x00080000, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfff80000, 0x00100000, &BREAK_32_        , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfff80000, 0x00180000, &SDBBP_32_        , 0,
       EJTAG_              },
};


static const Pool P_ADDIU[2] = {
    { pool                , P_RI                , 4   , 32,
       0xffe00000, 0x00000000, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc000000, 0x00000000, &ADDIU_32_        , &ADDIU_32__cond   ,
       0x0                 },
};


static const Pool P_TRAP[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000000, &TEQ              , 0,
       XMMS_               },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000400, &TNE              , 0,
       XMMS_               },
};


static const Pool P_CMOVE[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000210, &MOVZ             , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000610, &MOVN             , 0,
       0x0                 },
};


static const Pool P_D_MT_VPE[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc1f3fff, 0x20010ab0, &DMT              , 0,
       MT_                 },
    { instruction         , 0                   , 0   , 32,
       0xfc1f3fff, 0x20000ab0, &DVPE             , 0,
       MT_                 },
};


static const Pool P_E_MT_VPE[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc1f3fff, 0x20010eb0, &EMT              , 0,
       MT_                 },
    { instruction         , 0                   , 0   , 32,
       0xfc1f3fff, 0x20000eb0, &EVPE             , 0,
       MT_                 },
};


static const Pool _P_MT_VPE[2] = {
    { pool                , P_D_MT_VPE          , 2   , 32,
       0xfc003fff, 0x20000ab0, 0                      , 0,
       0x0                 },
    { pool                , P_E_MT_VPE          , 2   , 32,
       0xfc003fff, 0x20000eb0, 0                      , 0,
       0x0                 },
};


static const Pool P_MT_VPE[8] = {
    { reserved_block      , 0                   , 0   , 32,
       0xfc003bff, 0x200002b0, 0                      , 0,
       0x0                 },
    { pool                , _P_MT_VPE           , 2   , 32,
       0xfc003bff, 0x20000ab0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003bff, 0x200012b0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003bff, 0x20001ab0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003bff, 0x200022b0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003bff, 0x20002ab0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003bff, 0x200032b0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003bff, 0x20003ab0, 0                      , 0,
       0x0                 },
};


static const Pool P_DVP[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x20000390, &DVP              , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x20000790, &EVP              , 0,
       0x0                 },
};


static const Pool P_SLTU[2] = {
    { pool                , P_DVP               , 2   , 32,
       0xfc00fbff, 0x20000390, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000390, &SLTU             , &SLTU_cond        ,
       0x0                 },
};


static const Pool _POOL32A0[128] = {
    { pool                , P_TRAP              , 2   , 32,
       0xfc0003ff, 0x20000000, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000008, &SEB              , 0,
       XMMS_               },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000010, &SLLV             , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000018, &MUL_32_          , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000020, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000028, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000030, &MFC0             , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000038, &MFHC0            , 0,
       CP0_ | MVH_         },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000040, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000048, &SEH              , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000050, &SRLV             , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000058, &MUH              , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000060, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000068, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000070, &MTC0             , 0,
       CP0_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000078, &MTHC0            , 0,
       CP0_ | MVH_         },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000080, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000088, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000090, &SRAV             , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000098, &MULU             , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000a0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000a8, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000b0, &MFGC0            , 0,
       CP0_ | VZ_          },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000b8, &MFHGC0           , 0,
       CP0_ | VZ_ | MVH_   },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000c0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000c8, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000d0, &ROTRV            , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000d8, &MUHU             , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000e0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000e8, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000f0, &MTGC0            , 0,
       CP0_ | VZ_          },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000f8, &MTHGC0           , 0,
       CP0_ | VZ_ | MVH_   },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000100, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000108, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000110, &ADD              , 0,
       XMMS_               },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000118, &DIV              , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000120, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000128, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000130, &DMFC0            , 0,
       CP0_ | MIPS64_      },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000138, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000140, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000148, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000150, &ADDU_32_         , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000158, &MOD              , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000160, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000168, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000170, &DMTC0            , 0,
       CP0_ | MIPS64_      },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000178, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000180, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000188, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000190, &SUB              , 0,
       XMMS_               },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000198, &DIVU             , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001a0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001a8, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200001b0, &DMFGC0           , 0,
       CP0_ | MIPS64_ | VZ_},
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001b8, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200001c0, &RDHWR            , 0,
       XMMS_               },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001c8, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200001d0, &SUBU_32_         , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200001d8, &MODU             , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001e0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001e8, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200001f0, &DMTGC0           , 0,
       CP0_ | MIPS64_ | VZ_},
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001f8, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000200, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000208, 0                      , 0,
       0x0                 },
    { pool                , P_CMOVE             , 2   , 32,
       0xfc0003ff, 0x20000210, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000218, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000220, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000228, &FORK             , 0,
       MT_                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000230, &MFTR             , 0,
       MT_                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000238, &MFHTR            , 0,
       MT_                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000240, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000248, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000250, &AND_32_          , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000258, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000260, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000268, &YIELD            , 0,
       MT_                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000270, &MTTR             , 0,
       MT_                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000278, &MTHTR            , 0,
       MT_                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000280, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000288, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000290, &OR_32_           , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000298, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002a0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002a8, 0                      , 0,
       0x0                 },
    { pool                , P_MT_VPE            , 8   , 32,
       0xfc0003ff, 0x200002b0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002b8, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002c0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002c8, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200002d0, &NOR              , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002d8, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002e0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002e8, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002f0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002f8, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000300, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000308, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000310, &XOR_32_          , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000318, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000320, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000328, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000330, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000338, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000340, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000348, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000350, &SLT              , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000358, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000360, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000368, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000370, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000378, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000380, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000388, 0                      , 0,
       0x0                 },
    { pool                , P_SLTU              , 2   , 32,
       0xfc0003ff, 0x20000390, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000398, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003a0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003a8, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003b0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003b8, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003c0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003c8, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200003d0, &SOV              , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003d8, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003e0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003e8, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003f0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003f8, 0                      , 0,
       0x0                 },
};


static const Pool ADDQ__S__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000000d, &ADDQ_PH          , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000040d, &ADDQ_S_PH        , 0,
       DSP_                },
};


static const Pool MUL__S__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000002d, &MUL_PH           , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000042d, &MUL_S_PH         , 0,
       DSP_                },
};


static const Pool ADDQH__R__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000004d, &ADDQH_PH         , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000044d, &ADDQH_R_PH       , 0,
       DSP_                },
};


static const Pool ADDQH__R__W[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000008d, &ADDQH_W          , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000048d, &ADDQH_R_W        , 0,
       DSP_                },
};


static const Pool ADDU__S__QB[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200000cd, &ADDU_QB          , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200004cd, &ADDU_S_QB        , 0,
       DSP_                },
};


static const Pool ADDU__S__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000010d, &ADDU_PH          , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000050d, &ADDU_S_PH        , 0,
       DSP_                },
};


static const Pool ADDUH__R__QB[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000014d, &ADDUH_QB         , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000054d, &ADDUH_R_QB       , 0,
       DSP_                },
};


static const Pool SHRAV__R__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000018d, &SHRAV_PH         , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000058d, &SHRAV_R_PH       , 0,
       DSP_                },
};


static const Pool SHRAV__R__QB[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200001cd, &SHRAV_QB         , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200005cd, &SHRAV_R_QB       , 0,
       DSP_                },
};


static const Pool SUBQ__S__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000020d, &SUBQ_PH          , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000060d, &SUBQ_S_PH        , 0,
       DSP_                },
};


static const Pool SUBQH__R__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000024d, &SUBQH_PH         , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000064d, &SUBQH_R_PH       , 0,
       DSP_                },
};


static const Pool SUBQH__R__W[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000028d, &SUBQH_W          , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000068d, &SUBQH_R_W        , 0,
       DSP_                },
};


static const Pool SUBU__S__QB[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200002cd, &SUBU_QB          , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200006cd, &SUBU_S_QB        , 0,
       DSP_                },
};


static const Pool SUBU__S__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000030d, &SUBU_PH          , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000070d, &SUBU_S_PH        , 0,
       DSP_                },
};


static const Pool SHRA__R__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000335, &SHRA_PH          , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000735, &SHRA_R_PH        , 0,
       DSP_                },
};


static const Pool SUBUH__R__QB[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000034d, &SUBUH_QB         , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000074d, &SUBUH_R_QB       , 0,
       DSP_                },
};


static const Pool SHLLV__S__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000038d, &SHLLV_PH         , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000078d, &SHLLV_S_PH       , 0,
       DSP_                },
};


static const Pool SHLL__S__PH[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc000fff, 0x200003b5, &SHLL_PH          , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000fff, 0x200007b5, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc000fff, 0x20000bb5, &SHLL_S_PH        , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000fff, 0x20000fb5, 0                      , 0,
       0x0                 },
};


static const Pool PRECR_SRA__R__PH_W[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200003cd, &PRECR_SRA_PH_W   , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200007cd, &PRECR_SRA_R_PH_W , 0,
       DSP_                },
};


static const Pool _POOL32A5[128] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000005, &CMP_EQ_PH        , 0,
       DSP_                },
    { pool                , ADDQ__S__PH         , 2   , 32,
       0xfc0003ff, 0x2000000d, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000015, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x2000001d, &SHILO            , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000025, &MULEQ_S_W_PHL    , 0,
       DSP_                },
    { pool                , MUL__S__PH          , 2   , 32,
       0xfc0003ff, 0x2000002d, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000035, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x2000003d, &REPL_PH          , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000045, &CMP_LT_PH        , 0,
       DSP_                },
    { pool                , ADDQH__R__PH        , 2   , 32,
       0xfc0003ff, 0x2000004d, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000055, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000005d, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000065, &MULEQ_S_W_PHR    , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x2000006d, &PRECR_QB_PH      , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000075, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000007d, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000085, &CMP_LE_PH        , 0,
       DSP_                },
    { pool                , ADDQH__R__W         , 2   , 32,
       0xfc0003ff, 0x2000008d, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000095, &MULEU_S_PH_QBL   , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000009d, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000a5, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000ad, &PRECRQ_QB_PH     , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000b5, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000bd, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000c5, &CMPGU_EQ_QB      , 0,
       DSP_                },
    { pool                , ADDU__S__QB         , 2   , 32,
       0xfc0003ff, 0x200000cd, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000d5, &MULEU_S_PH_QBR   , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000dd, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000e5, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000ed, &PRECRQ_PH_W      , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000f5, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000fd, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000105, &CMPGU_LT_QB      , 0,
       DSP_                },
    { pool                , ADDU__S__PH         , 2   , 32,
       0xfc0003ff, 0x2000010d, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000115, &MULQ_RS_PH       , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000011d, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000125, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x2000012d, &PRECRQ_RS_PH_W   , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000135, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000013d, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000145, &CMPGU_LE_QB      , 0,
       DSP_                },
    { pool                , ADDUH__R__QB        , 2   , 32,
       0xfc0003ff, 0x2000014d, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000155, &MULQ_S_PH        , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000015d, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000165, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x2000016d, &PRECRQU_S_QB_PH  , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000175, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000017d, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000185, &CMPGDU_EQ_QB     , 0,
       DSP_                },
    { pool                , SHRAV__R__PH        , 2   , 32,
       0xfc0003ff, 0x2000018d, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000195, &MULQ_RS_W        , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000019d, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001a5, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200001ad, &PACKRL_PH        , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001b5, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001bd, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200001c5, &CMPGDU_LT_QB     , 0,
       DSP_                },
    { pool                , SHRAV__R__QB        , 2   , 32,
       0xfc0003ff, 0x200001cd, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200001d5, &MULQ_S_W         , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001dd, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001e5, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200001ed, &PICK_QB          , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001f5, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001fd, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000205, &CMPGDU_LE_QB     , 0,
       DSP_                },
    { pool                , SUBQ__S__PH         , 2   , 32,
       0xfc0003ff, 0x2000020d, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000215, &APPEND           , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000021d, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000225, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x2000022d, &PICK_PH          , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000235, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000023d, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000245, &CMPU_EQ_QB       , 0,
       DSP_                },
    { pool                , SUBQH__R__PH        , 2   , 32,
       0xfc0003ff, 0x2000024d, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000255, &PREPEND          , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000025d, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000265, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000026d, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000275, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000027d, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000285, &CMPU_LT_QB       , 0,
       DSP_                },
    { pool                , SUBQH__R__W         , 2   , 32,
       0xfc0003ff, 0x2000028d, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000295, &MODSUB           , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000029d, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002a5, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002ad, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002b5, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002bd, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200002c5, &CMPU_LE_QB       , 0,
       DSP_                },
    { pool                , SUBU__S__QB         , 2   , 32,
       0xfc0003ff, 0x200002cd, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200002d5, &SHRAV_R_W        , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002dd, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002e5, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002ed, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200002f5, &SHRA_R_W         , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002fd, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000305, &ADDQ_S_W         , 0,
       DSP_                },
    { pool                , SUBU__S__PH         , 2   , 32,
       0xfc0003ff, 0x2000030d, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000315, &SHRLV_PH         , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000031d, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000325, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000032d, 0                      , 0,
       0x0                 },
    { pool                , SHRA__R__PH         , 2   , 32,
       0xfc0003ff, 0x20000335, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000033d, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000345, &SUBQ_S_W         , 0,
       DSP_                },
    { pool                , SUBUH__R__QB        , 2   , 32,
       0xfc0003ff, 0x2000034d, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000355, &SHRLV_QB         , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000035d, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000365, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000036d, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000375, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000037d, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000385, &ADDSC            , 0,
       DSP_                },
    { pool                , SHLLV__S__PH        , 2   , 32,
       0xfc0003ff, 0x2000038d, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000395, &SHLLV_QB         , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000039d, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003a5, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003ad, 0                      , 0,
       0x0                 },
    { pool                , SHLL__S__PH         , 4   , 32,
       0xfc0003ff, 0x200003b5, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003bd, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200003c5, &ADDWC            , 0,
       DSP_                },
    { pool                , PRECR_SRA__R__PH_W  , 2   , 32,
       0xfc0003ff, 0x200003cd, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200003d5, &SHLLV_S_W        , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003dd, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003e5, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003ed, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200003f5, &SHLL_S_W         , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003fd, 0                      , 0,
       0x0                 },
};


static const Pool PP_LSX[16] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000007, &LBX              , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000087, &SBX              , 0,
       XMMS_               },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000107, &LBUX             , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0x20000187, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000207, &LHX              , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000287, &SHX              , 0,
       XMMS_               },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000307, &LHUX             , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000387, &LWUX             , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000407, &LWX              , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000487, &SWX              , 0,
       XMMS_               },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000507, &LWC1X            , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000587, &SWC1X            , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000607, &LDX              , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000687, &SDX              , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000707, &LDC1X            , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000787, &SDC1X            , 0,
       CP1_                },
};


static const Pool PP_LSXS[16] = {
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0x20000047, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0x200000c7, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0x20000147, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0x200001c7, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000247, &LHXS             , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200002c7, &SHXS             , 0,
       XMMS_               },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000347, &LHUXS            , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200003c7, &LWUXS            , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000447, &LWXS_32_         , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200004c7, &SWXS             , 0,
       XMMS_               },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000547, &LWC1XS           , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200005c7, &SWC1XS           , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000647, &LDXS             , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200006c7, &SDXS             , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000747, &LDC1XS           , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200007c7, &SDC1XS           , 0,
       CP1_                },
};


static const Pool P_LSX[2] = {
    { pool                , PP_LSX              , 16  , 32,
       0xfc00007f, 0x20000007, 0                      , 0,
       0x0                 },
    { pool                , PP_LSXS             , 16  , 32,
       0xfc00007f, 0x20000047, 0                      , 0,
       0x0                 },
};


static const Pool POOL32Axf_1_0[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000007f, &MFHI_DSP_        , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000107f, &MFLO_DSP_        , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000207f, &MTHI_DSP_        , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000307f, &MTLO_DSP_        , 0,
       DSP_                },
};


static const Pool POOL32Axf_1_1[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000027f, &MTHLIP           , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000127f, &SHILOV           , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0x2000227f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0x2000327f, 0                      , 0,
       0x0                 },
};


static const Pool POOL32Axf_1_3[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000067f, &RDDSP            , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000167f, &WRDSP            , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000267f, &EXTP             , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000367f, &EXTPDP           , 0,
       DSP_                },
};


static const Pool POOL32Axf_1_4[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc001fff, 0x2000087f, &SHLL_QB          , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc001fff, 0x2000187f, &SHRL_QB          , 0,
       DSP_                },
};


static const Pool MAQ_S_A__W_PHR[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20000a7f, &MAQ_S_W_PHR      , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20002a7f, &MAQ_SA_W_PHR     , 0,
       DSP_                },
};


static const Pool MAQ_S_A__W_PHL[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20001a7f, &MAQ_S_W_PHL      , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20003a7f, &MAQ_SA_W_PHL     , 0,
       DSP_                },
};


static const Pool POOL32Axf_1_5[2] = {
    { pool                , MAQ_S_A__W_PHR      , 2   , 32,
       0xfc001fff, 0x20000a7f, 0                      , 0,
       0x0                 },
    { pool                , MAQ_S_A__W_PHL      , 2   , 32,
       0xfc001fff, 0x20001a7f, 0                      , 0,
       0x0                 },
};


static const Pool POOL32Axf_1_7[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20000e7f, &EXTR_W           , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20001e7f, &EXTR_R_W         , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20002e7f, &EXTR_RS_W        , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20003e7f, &EXTR_S_H         , 0,
       DSP_                },
};


static const Pool POOL32Axf_1[8] = {
    { pool                , POOL32Axf_1_0       , 4   , 32,
       0xfc000fff, 0x2000007f, 0                      , 0,
       0x0                 },
    { pool                , POOL32Axf_1_1       , 4   , 32,
       0xfc000fff, 0x2000027f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000fff, 0x2000047f, 0                      , 0,
       0x0                 },
    { pool                , POOL32Axf_1_3       , 4   , 32,
       0xfc000fff, 0x2000067f, 0                      , 0,
       0x0                 },
    { pool                , POOL32Axf_1_4       , 2   , 32,
       0xfc000fff, 0x2000087f, 0                      , 0,
       0x0                 },
    { pool                , POOL32Axf_1_5       , 2   , 32,
       0xfc000fff, 0x20000a7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000fff, 0x20000c7f, 0                      , 0,
       0x0                 },
    { pool                , POOL32Axf_1_7       , 4   , 32,
       0xfc000fff, 0x20000e7f, 0                      , 0,
       0x0                 },
};


static const Pool POOL32Axf_2_DSP__0_7[8] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200000bf, &DPA_W_PH         , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200002bf, &DPAQ_S_W_PH      , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200004bf, &DPS_W_PH         , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200006bf, &DPSQ_S_W_PH      , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0x200008bf, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20000abf, &MADD_DSP_        , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20000cbf, &MULT_DSP_        , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20000ebf, &EXTRV_W          , 0,
       DSP_                },
};


static const Pool POOL32Axf_2_DSP__8_15[8] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200010bf, &DPAX_W_PH        , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200012bf, &DPAQ_SA_L_W      , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200014bf, &DPSX_W_PH        , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200016bf, &DPSQ_SA_L_W      , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0x200018bf, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20001abf, &MADDU_DSP_       , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20001cbf, &MULTU_DSP_       , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20001ebf, &EXTRV_R_W        , 0,
       DSP_                },
};


static const Pool POOL32Axf_2_DSP__16_23[8] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200020bf, &DPAU_H_QBL       , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200022bf, &DPAQX_S_W_PH     , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200024bf, &DPSU_H_QBL       , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200026bf, &DPSQX_S_W_PH     , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200028bf, &EXTPV            , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20002abf, &MSUB_DSP_        , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20002cbf, &MULSA_W_PH       , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20002ebf, &EXTRV_RS_W       , 0,
       DSP_                },
};


static const Pool POOL32Axf_2_DSP__24_31[8] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200030bf, &DPAU_H_QBR       , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200032bf, &DPAQX_SA_W_PH    , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200034bf, &DPSU_H_QBR       , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200036bf, &DPSQX_SA_W_PH    , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200038bf, &EXTPDPV          , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20003abf, &MSUBU_DSP_       , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20003cbf, &MULSAQ_S_W_PH    , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20003ebf, &EXTRV_S_H        , 0,
       DSP_                },
};


static const Pool POOL32Axf_2[4] = {
    { pool                , POOL32Axf_2_DSP__0_7, 8   , 32,
       0xfc0031ff, 0x200000bf, 0                      , 0,
       0x0                 },
    { pool                , POOL32Axf_2_DSP__8_15, 8   , 32,
       0xfc0031ff, 0x200010bf, 0                      , 0,
       0x0                 },
    { pool                , POOL32Axf_2_DSP__16_23, 8   , 32,
       0xfc0031ff, 0x200020bf, 0                      , 0,
       0x0                 },
    { pool                , POOL32Axf_2_DSP__24_31, 8   , 32,
       0xfc0031ff, 0x200030bf, 0                      , 0,
       0x0                 },
};


static const Pool POOL32Axf_4[128] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000013f, &ABSQ_S_QB        , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000033f, &REPLV_PH         , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000053f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000073f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000093f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20000b3f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20000d3f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20000f3f, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000113f, &ABSQ_S_PH        , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000133f, &REPLV_QB         , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000153f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000173f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000193f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20001b3f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20001d3f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20001f3f, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000213f, &ABSQ_S_W         , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000233f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000253f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000273f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000293f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20002b3f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20002d3f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20002f3f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000313f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000333f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000353f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000373f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000393f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20003b3f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20003d3f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20003f3f, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000413f, &INSV             , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000433f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000453f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000473f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000493f, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x20004b3f, &CLO              , 0,
       XMMS_               },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x20004d3f, &MFC2             , 0,
       CP2_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20004f3f, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000513f, &PRECEQ_W_PHL     , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000533f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000553f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000573f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000593f, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x20005b3f, &CLZ              , 0,
       XMMS_               },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x20005d3f, &MTC2             , 0,
       CP2_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20005f3f, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000613f, &PRECEQ_W_PHR     , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000633f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000653f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000673f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000693f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20006b3f, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x20006d3f, &DMFC2            , 0,
       CP2_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20006f3f, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000713f, &PRECEQU_PH_QBL   , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000733f, &PRECEQU_PH_QBLA  , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000753f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000773f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000793f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20007b3f, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x20007d3f, &DMTC2            , 0,
       CP2_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20007f3f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000813f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000833f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000853f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000873f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000893f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20008b3f, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x20008d3f, &MFHC2            , 0,
       CP2_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20008f3f, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000913f, &PRECEQU_PH_QBR   , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000933f, &PRECEQU_PH_QBRA  , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000953f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000973f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000993f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20009b3f, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x20009d3f, &MTHC2            , 0,
       CP2_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20009f3f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000a13f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000a33f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000a53f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000a73f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000a93f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000ab3f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000ad3f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000af3f, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000b13f, &PRECEU_PH_QBL    , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000b33f, &PRECEU_PH_QBLA   , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000b53f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000b73f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000b93f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000bb3f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000bd3f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000bf3f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000c13f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000c33f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000c53f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000c73f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000c93f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000cb3f, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000cd3f, &CFC2             , 0,
       CP2_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000cf3f, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000d13f, &PRECEU_PH_QBR    , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000d33f, &PRECEU_PH_QBRA   , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000d53f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000d73f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000d93f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000db3f, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000dd3f, &CTC2             , 0,
       CP2_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000df3f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000e13f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000e33f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000e53f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000e73f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000e93f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000eb3f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000ed3f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000ef3f, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000f13f, &RADDU_W_QB       , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000f33f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000f53f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000f73f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000f93f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000fb3f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000fd3f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000ff3f, 0                      , 0,
       0x0                 },
};


static const Pool POOL32Axf_5_group0[32] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000017f, &TLBGP            , 0,
       CP0_ | VZ_ | TLB_   },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000037f, &TLBP             , 0,
       CP0_ | TLB_         },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000057f, &TLBGINV          , 0,
       CP0_ | VZ_ | TLB_ | TLBINV_},
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000077f, &TLBINV           , 0,
       CP0_ | TLB_ | TLBINV_},
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000097f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20000b7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20000d7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20000f7f, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000117f, &TLBGR            , 0,
       CP0_ | VZ_ | TLB_   },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000137f, &TLBR             , 0,
       CP0_ | TLB_         },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000157f, &TLBGINVF         , 0,
       CP0_ | VZ_ | TLB_ | TLBINV_},
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000177f, &TLBINVF          , 0,
       CP0_ | TLB_ | TLBINV_},
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000197f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20001b7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20001d7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20001f7f, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000217f, &TLBGWI           , 0,
       CP0_ | VZ_ | TLB_   },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000237f, &TLBWI            , 0,
       CP0_ | TLB_         },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000257f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000277f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000297f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20002b7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20002d7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20002f7f, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000317f, &TLBGWR           , 0,
       CP0_ | VZ_ | TLB_   },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000337f, &TLBWR            , 0,
       CP0_ | TLB_         },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000357f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000377f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000397f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20003b7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20003d7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20003f7f, 0                      , 0,
       0x0                 },
};


static const Pool POOL32Axf_5_group1[32] = {
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000417f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000437f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000457f, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000477f, &DI               , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000497f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20004b7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20004d7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20004f7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000517f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000537f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000557f, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000577f, &EI               , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000597f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20005b7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20005d7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20005f7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000617f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000637f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000657f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000677f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000697f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20006b7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20006d7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20006f7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000717f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000737f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000757f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000777f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000797f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20007b7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20007d7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20007f7f, 0                      , 0,
       0x0                 },
};


static const Pool ERETx[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc01ffff, 0x2000f37f, &ERET             , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc01ffff, 0x2001f37f, &ERETNC           , 0,
       0x0                 },
};


static const Pool POOL32Axf_5_group3[32] = {
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000c17f, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000c37f, &WAIT             , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000c57f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000c77f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000c97f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000cb7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000cd7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000cf7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000d17f, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000d37f, &IRET             , 0,
       MCU_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000d57f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000d77f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000d97f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000db7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000dd7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000df7f, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000e17f, &RDPGPR           , 0,
       CP0_                },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000e37f, &DERET            , 0,
       EJTAG_              },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000e57f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000e77f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000e97f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000eb7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000ed7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000ef7f, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000f17f, &WRPGPR           , 0,
       CP0_                },
    { pool                , ERETx               , 2   , 32,
       0xfc00ffff, 0x2000f37f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000f57f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000f77f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000f97f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000fb7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000fd7f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000ff7f, 0                      , 0,
       0x0                 },
};


static const Pool POOL32Axf_5[4] = {
    { pool                , POOL32Axf_5_group0  , 32  , 32,
       0xfc00c1ff, 0x2000017f, 0                      , 0,
       0x0                 },
    { pool                , POOL32Axf_5_group1  , 32  , 32,
       0xfc00c1ff, 0x2000417f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00c1ff, 0x2000817f, 0                      , 0,
       0x0                 },
    { pool                , POOL32Axf_5_group3  , 32  , 32,
       0xfc00c1ff, 0x2000c17f, 0                      , 0,
       0x0                 },
};


static const Pool SHRA__R__QB[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc001fff, 0x200001ff, &SHRA_QB          , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc001fff, 0x200011ff, &SHRA_R_QB        , 0,
       DSP_                },
};


static const Pool POOL32Axf_7[8] = {
    { pool                , SHRA__R__QB         , 2   , 32,
       0xfc000fff, 0x200001ff, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc000fff, 0x200003ff, &SHRL_PH          , 0,
       DSP_                },
    { instruction         , 0                   , 0   , 32,
       0xfc000fff, 0x200005ff, &REPL_QB          , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000fff, 0x200007ff, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000fff, 0x200009ff, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000fff, 0x20000bff, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000fff, 0x20000dff, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000fff, 0x20000fff, 0                      , 0,
       0x0                 },
};


static const Pool POOL32Axf[8] = {
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0x2000003f, 0                      , 0,
       0x0                 },
    { pool                , POOL32Axf_1         , 8   , 32,
       0xfc0001ff, 0x2000007f, 0                      , 0,
       0x0                 },
    { pool                , POOL32Axf_2         , 4   , 32,
       0xfc0001ff, 0x200000bf, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0x200000ff, 0                      , 0,
       0x0                 },
    { pool                , POOL32Axf_4         , 128 , 32,
       0xfc0001ff, 0x2000013f, 0                      , 0,
       0x0                 },
    { pool                , POOL32Axf_5         , 4   , 32,
       0xfc0001ff, 0x2000017f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0x200001bf, 0                      , 0,
       0x0                 },
    { pool                , POOL32Axf_7         , 8   , 32,
       0xfc0001ff, 0x200001ff, 0                      , 0,
       0x0                 },
};


static const Pool _POOL32A7[8] = {
    { pool                , P_LSX               , 2   , 32,
       0xfc00003f, 0x20000007, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00003f, 0x2000000f, &LSA              , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0x20000017, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00003f, 0x2000001f, &EXTW             , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0x20000027, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0x2000002f, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0x20000037, 0                      , 0,
       0x0                 },
    { pool                , POOL32Axf           , 8   , 32,
       0xfc00003f, 0x2000003f, 0                      , 0,
       0x0                 },
};


static const Pool P32A[8] = {
    { pool                , _POOL32A0           , 128 , 32,
       0xfc000007, 0x20000000, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc000007, 0x20000001, &SPECIAL2         , 0,
       UDI_                },
    { instruction         , 0                   , 0   , 32,
       0xfc000007, 0x20000002, &COP2_1           , 0,
       CP2_                },
    { instruction         , 0                   , 0   , 32,
       0xfc000007, 0x20000003, &UDI              , 0,
       UDI_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000007, 0x20000004, 0                      , 0,
       0x0                 },
    { pool                , _POOL32A5           , 128 , 32,
       0xfc000007, 0x20000005, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000007, 0x20000006, 0                      , 0,
       0x0                 },
    { pool                , _POOL32A7           , 8   , 32,
       0xfc000007, 0x20000007, 0                      , 0,
       0x0                 },
};


static const Pool P_GP_D[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc000007, 0x40000001, &LD_GP_           , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc000007, 0x40000005, &SD_GP_           , 0,
       MIPS64_             },
};


static const Pool P_GP_W[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc000003, 0x40000000, &ADDIU_GP_W_      , 0,
       0x0                 },
    { pool                , P_GP_D              , 2   , 32,
       0xfc000003, 0x40000001, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc000003, 0x40000002, &LW_GP_           , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc000003, 0x40000003, &SW_GP_           , 0,
       0x0                 },
};


static const Pool POOL48I[32] = {
    { instruction         , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600000000000ull, &LI_48_           , 0,
       XMMS_               },
    { instruction         , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600100000000ull, &ADDIU_48_        , 0,
       XMMS_               },
    { instruction         , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600200000000ull, &ADDIU_GP48_      , 0,
       XMMS_               },
    { instruction         , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600300000000ull, &ADDIUPC_48_      , 0,
       XMMS_               },
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600400000000ull, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600500000000ull, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600600000000ull, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600700000000ull, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600800000000ull, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600900000000ull, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600a00000000ull, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600b00000000ull, &LWPC_48_         , 0,
       XMMS_               },
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600c00000000ull, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600d00000000ull, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600e00000000ull, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600f00000000ull, &SWPC_48_         , 0,
       XMMS_               },
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601000000000ull, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601100000000ull, &DADDIU_48_       , 0,
       MIPS64_             },
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601200000000ull, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601300000000ull, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601400000000ull, &DLUI_48_         , 0,
       MIPS64_             },
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601500000000ull, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601600000000ull, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601700000000ull, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601800000000ull, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601900000000ull, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601a00000000ull, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601b00000000ull, &LDPC_48_         , 0,
       MIPS64_             },
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601c00000000ull, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601d00000000ull, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601e00000000ull, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601f00000000ull, &SDPC_48_         , 0,
       MIPS64_             },
};


static const Pool PP_SR[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc10f003, 0x80003000, &SAVE_32_         , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc10f003, 0x80003001, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc10f003, 0x80003002, &RESTORE_32_      , 0,
       0x0                 },
    { return_instruction  , 0                   , 0   , 32,
       0xfc10f003, 0x80003003, &RESTORE_JRC_32_  , 0,
       0x0                 },
};


static const Pool P_SR_F[8] = {
    { instruction         , 0                   , 0   , 32,
       0xfc10f007, 0x80103000, &SAVEF            , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc10f007, 0x80103001, &RESTOREF         , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc10f007, 0x80103002, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc10f007, 0x80103003, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc10f007, 0x80103004, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc10f007, 0x80103005, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc10f007, 0x80103006, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc10f007, 0x80103007, 0                      , 0,
       0x0                 },
};


static const Pool P_SR[2] = {
    { pool                , PP_SR               , 4   , 32,
       0xfc10f000, 0x80003000, 0                      , 0,
       0x0                 },
    { pool                , P_SR_F              , 8   , 32,
       0xfc10f000, 0x80103000, 0                      , 0,
       0x0                 },
};


static const Pool P_SLL[5] = {
    { instruction         , 0                   , 0   , 32,
       0xffe0f1ff, 0x8000c000, &NOP_32_          , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xffe0f1ff, 0x8000c003, &EHB              , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xffe0f1ff, 0x8000c005, &PAUSE            , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xffe0f1ff, 0x8000c006, &SYNC             , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c000, &SLL_32_          , 0,
       0x0                 },
};


static const Pool P_SHIFT[16] = {
    { pool                , P_SLL               , 5   , 32,
       0xfc00f1e0, 0x8000c000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c020, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c040, &SRL_32_          , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c060, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c080, &SRA              , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c0a0, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c0c0, &ROTR             , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c0e0, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c100, &DSLL             , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c120, &DSLL32           , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c140, &DSRL             , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c160, &DSRL32           , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c180, &DSRA             , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c1a0, &DSRA32           , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c1c0, &DROTR            , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c1e0, &DROTR32          , 0,
       MIPS64_             },
};


static const Pool P_ROTX[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00f820, 0x8000d000, &ROTX             , 0,
       XMMS_               },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f820, 0x8000d020, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f820, 0x8000d800, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f820, 0x8000d820, 0                      , 0,
       0x0                 },
};


static const Pool P_INS[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00f820, 0x8000e000, &INS              , 0,
       XMMS_               },
    { instruction         , 0                   , 0   , 32,
       0xfc00f820, 0x8000e020, &DINSU            , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc00f820, 0x8000e800, &DINSM            , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc00f820, 0x8000e820, &DINS             , 0,
       MIPS64_             },
};


static const Pool P_EXT[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00f820, 0x8000f000, &EXT              , 0,
       XMMS_               },
    { instruction         , 0                   , 0   , 32,
       0xfc00f820, 0x8000f020, &DEXTU            , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc00f820, 0x8000f800, &DEXTM            , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc00f820, 0x8000f820, &DEXT             , 0,
       MIPS64_             },
};


static const Pool P_U12[16] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x80000000, &ORI              , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x80001000, &XORI             , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x80002000, &ANDI_32_         , 0,
       0x0                 },
    { pool                , P_SR                , 2   , 32,
       0xfc00f000, 0x80003000, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x80004000, &SLTI             , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x80005000, &SLTIU            , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x80006000, &SEQI             , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x80007000, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x80008000, &ADDIU_NEG_       , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x80009000, &DADDIU_U12_      , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x8000a000, &DADDIU_NEG_      , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x8000b000, &DROTX            , 0,
       MIPS64_             },
    { pool                , P_SHIFT             , 16  , 32,
       0xfc00f000, 0x8000c000, 0                      , 0,
       0x0                 },
    { pool                , P_ROTX              , 4   , 32,
       0xfc00f000, 0x8000d000, 0                      , 0,
       0x0                 },
    { pool                , P_INS               , 4   , 32,
       0xfc00f000, 0x8000e000, 0                      , 0,
       0x0                 },
    { pool                , P_EXT               , 4   , 32,
       0xfc00f000, 0x8000f000, 0                      , 0,
       0x0                 },
};


static const Pool RINT_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000020, &RINT_S           , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000220, &RINT_D           , 0,
       CP1_                },
};


static const Pool ADD_fmt0[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000030, &ADD_S            , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000230, 0                      , 0,
       CP1_                },
};


static const Pool SELEQZ_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000038, &SELEQZ_S         , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000238, &SELEQZ_D         , 0,
       CP1_                },
};


static const Pool CLASS_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000060, &CLASS_S          , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000260, &CLASS_D          , 0,
       CP1_                },
};


static const Pool SUB_fmt0[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000070, &SUB_S            , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000270, 0                      , 0,
       CP1_                },
};


static const Pool SELNEZ_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000078, &SELNEZ_S         , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000278, &SELNEZ_D         , 0,
       CP1_                },
};


static const Pool MUL_fmt0[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00000b0, &MUL_S            , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0xa00002b0, 0                      , 0,
       CP1_                },
};


static const Pool SEL_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00000b8, &SEL_S            , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00002b8, &SEL_D            , 0,
       CP1_                },
};


static const Pool DIV_fmt0[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00000f0, &DIV_S            , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0xa00002f0, 0                      , 0,
       CP1_                },
};


static const Pool ADD_fmt1[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000130, &ADD_D            , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000330, 0                      , 0,
       CP1_                },
};


static const Pool SUB_fmt1[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000170, &SUB_D            , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000370, 0                      , 0,
       CP1_                },
};


static const Pool MUL_fmt1[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00001b0, &MUL_D            , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0xa00003b0, 0                      , 0,
       CP1_                },
};


static const Pool MADDF_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00001b8, &MADDF_S          , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00003b8, &MADDF_D          , 0,
       CP1_                },
};


static const Pool DIV_fmt1[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00001f0, &DIV_D            , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0xa00003f0, 0                      , 0,
       CP1_                },
};


static const Pool MSUBF_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00001f8, &MSUBF_S          , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00003f8, &MSUBF_D          , 0,
       CP1_                },
};


static const Pool POOL32F_0[64] = {
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000000, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000008, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000010, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000018, 0                      , 0,
       CP1_                },
    { pool                , RINT_fmt            , 2   , 32,
       0xfc0001ff, 0xa0000020, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000028, 0                      , 0,
       CP1_                },
    { pool                , ADD_fmt0            , 2   , 32,
       0xfc0001ff, 0xa0000030, 0                      , 0,
       CP1_                },
    { pool                , SELEQZ_fmt          , 2   , 32,
       0xfc0001ff, 0xa0000038, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000040, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000048, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000050, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000058, 0                      , 0,
       CP1_                },
    { pool                , CLASS_fmt           , 2   , 32,
       0xfc0001ff, 0xa0000060, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000068, 0                      , 0,
       CP1_                },
    { pool                , SUB_fmt0            , 2   , 32,
       0xfc0001ff, 0xa0000070, 0                      , 0,
       CP1_                },
    { pool                , SELNEZ_fmt          , 2   , 32,
       0xfc0001ff, 0xa0000078, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000080, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000088, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000090, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000098, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00000a0, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00000a8, 0                      , 0,
       CP1_                },
    { pool                , MUL_fmt0            , 2   , 32,
       0xfc0001ff, 0xa00000b0, 0                      , 0,
       CP1_                },
    { pool                , SEL_fmt             , 2   , 32,
       0xfc0001ff, 0xa00000b8, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00000c0, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00000c8, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00000d0, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00000d8, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00000e0, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00000e8, 0                      , 0,
       CP1_                },
    { pool                , DIV_fmt0            , 2   , 32,
       0xfc0001ff, 0xa00000f0, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00000f8, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000100, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000108, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000110, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000118, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000120, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000128, 0                      , 0,
       CP1_                },
    { pool                , ADD_fmt1            , 2   , 32,
       0xfc0001ff, 0xa0000130, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000138, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000140, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000148, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000150, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000158, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000160, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000168, 0                      , 0,
       CP1_                },
    { pool                , SUB_fmt1            , 2   , 32,
       0xfc0001ff, 0xa0000170, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000178, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000180, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000188, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000190, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000198, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00001a0, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00001a8, 0                      , 0,
       CP1_                },
    { pool                , MUL_fmt1            , 2   , 32,
       0xfc0001ff, 0xa00001b0, 0                      , 0,
       CP1_                },
    { pool                , MADDF_fmt           , 2   , 32,
       0xfc0001ff, 0xa00001b8, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00001c0, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00001c8, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00001d0, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00001d8, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00001e0, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00001e8, 0                      , 0,
       CP1_                },
    { pool                , DIV_fmt1            , 2   , 32,
       0xfc0001ff, 0xa00001f0, 0                      , 0,
       CP1_                },
    { pool                , MSUBF_fmt           , 2   , 32,
       0xfc0001ff, 0xa00001f8, 0                      , 0,
       CP1_                },
};


static const Pool MIN_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00023f, 0xa0000003, &MIN_S            , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc00023f, 0xa0000203, &MIN_D            , 0,
       CP1_                },
};


static const Pool MAX_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00023f, 0xa000000b, &MAX_S            , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc00023f, 0xa000020b, &MAX_D            , 0,
       CP1_                },
};


static const Pool MINA_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00023f, 0xa0000023, &MINA_S           , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc00023f, 0xa0000223, &MINA_D           , 0,
       CP1_                },
};


static const Pool MAXA_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00023f, 0xa000002b, &MAXA_S           , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc00023f, 0xa000022b, &MAXA_D           , 0,
       CP1_                },
};


static const Pool CVT_L_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000013b, &CVT_L_S          , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000413b, &CVT_L_D          , 0,
       CP1_                },
};


static const Pool RSQRT_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000023b, &RSQRT_S          , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000423b, &RSQRT_D          , 0,
       CP1_                },
};


static const Pool FLOOR_L_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000033b, &FLOOR_L_S        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000433b, &FLOOR_L_D        , 0,
       CP1_                },
};


static const Pool CVT_W_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000093b, &CVT_W_S          , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000493b, &CVT_W_D          , 0,
       CP1_                },
};


static const Pool SQRT_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0000a3b, &SQRT_S           , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0004a3b, &SQRT_D           , 0,
       CP1_                },
};


static const Pool FLOOR_W_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0000b3b, &FLOOR_W_S        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0004b3b, &FLOOR_W_D        , 0,
       CP1_                },
};


static const Pool RECIP_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000123b, &RECIP_S          , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000523b, &RECIP_D          , 0,
       CP1_                },
};


static const Pool CEIL_L_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000133b, &CEIL_L_S         , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000533b, &CEIL_L_D         , 0,
       CP1_                },
};


static const Pool CEIL_W_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0001b3b, &CEIL_W_S         , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0005b3b, &CEIL_W_D         , 0,
       CP1_                },
};


static const Pool TRUNC_L_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000233b, &TRUNC_L_S        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000633b, &TRUNC_L_D        , 0,
       CP1_                },
};


static const Pool TRUNC_W_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0002b3b, &TRUNC_W_S        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0006b3b, &TRUNC_W_D        , 0,
       CP1_                },
};


static const Pool ROUND_L_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000333b, &ROUND_L_S        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000733b, &ROUND_L_D        , 0,
       CP1_                },
};


static const Pool ROUND_W_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0003b3b, &ROUND_W_S        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0007b3b, &ROUND_W_D        , 0,
       CP1_                },
};


static const Pool POOL32Fxf_0[64] = {
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000003b, 0                      , 0,
       CP1_                },
    { pool                , CVT_L_fmt           , 2   , 32,
       0xfc003fff, 0xa000013b, 0                      , 0,
       CP1_                },
    { pool                , RSQRT_fmt           , 2   , 32,
       0xfc003fff, 0xa000023b, 0                      , 0,
       CP1_                },
    { pool                , FLOOR_L_fmt         , 2   , 32,
       0xfc003fff, 0xa000033b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000043b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000053b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000063b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000073b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000083b, 0                      , 0,
       CP1_                },
    { pool                , CVT_W_fmt           , 2   , 32,
       0xfc003fff, 0xa000093b, 0                      , 0,
       CP1_                },
    { pool                , SQRT_fmt            , 2   , 32,
       0xfc003fff, 0xa0000a3b, 0                      , 0,
       CP1_                },
    { pool                , FLOOR_W_fmt         , 2   , 32,
       0xfc003fff, 0xa0000b3b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0000c3b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0000d3b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0000e3b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0000f3b, 0                      , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0xa000103b, &CFC1             , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000113b, 0                      , 0,
       CP1_                },
    { pool                , RECIP_fmt           , 2   , 32,
       0xfc003fff, 0xa000123b, 0                      , 0,
       CP1_                },
    { pool                , CEIL_L_fmt          , 2   , 32,
       0xfc003fff, 0xa000133b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000143b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000153b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000163b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000173b, 0                      , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0xa000183b, &CTC1             , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000193b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0001a3b, 0                      , 0,
       CP1_                },
    { pool                , CEIL_W_fmt          , 2   , 32,
       0xfc003fff, 0xa0001b3b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0001c3b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0001d3b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0001e3b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0001f3b, 0                      , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0xa000203b, &MFC1             , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0xa000213b, &CVT_S_PL         , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000223b, 0                      , 0,
       CP1_                },
    { pool                , TRUNC_L_fmt         , 2   , 32,
       0xfc003fff, 0xa000233b, 0                      , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0xa000243b, &DMFC1            , 0,
       CP1_ | MIPS64_      },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000253b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000263b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000273b, 0                      , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0xa000283b, &MTC1             , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0xa000293b, &CVT_S_PU         , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0002a3b, 0                      , 0,
       CP1_                },
    { pool                , TRUNC_W_fmt         , 2   , 32,
       0xfc003fff, 0xa0002b3b, 0                      , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0xa0002c3b, &DMTC1            , 0,
       CP1_ | MIPS64_      },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0002d3b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0002e3b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0002f3b, 0                      , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0xa000303b, &MFHC1            , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000313b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000323b, 0                      , 0,
       CP1_                },
    { pool                , ROUND_L_fmt         , 2   , 32,
       0xfc003fff, 0xa000333b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000343b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000353b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000363b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000373b, 0                      , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0xa000383b, &MTHC1            , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000393b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0003a3b, 0                      , 0,
       CP1_                },
    { pool                , ROUND_W_fmt         , 2   , 32,
       0xfc003fff, 0xa0003b3b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0003c3b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0003d3b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0003e3b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0003f3b, 0                      , 0,
       CP1_                },
};


static const Pool MOV_fmt[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000007b, &MOV_S            , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000207b, &MOV_D            , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007fff, 0xa000407b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007fff, 0xa000607b, 0                      , 0,
       CP1_                },
};


static const Pool ABS_fmt[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000037b, &ABS_S            , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000237b, &ABS_D            , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007fff, 0xa000437b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007fff, 0xa000637b, 0                      , 0,
       CP1_                },
};


static const Pool NEG_fmt[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0000b7b, &NEG_S            , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0002b7b, &NEG_D            , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007fff, 0xa0004b7b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007fff, 0xa0006b7b, 0                      , 0,
       CP1_                },
};


static const Pool CVT_D_fmt[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000137b, &CVT_D_S          , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000337b, &CVT_D_W          , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000537b, &CVT_D_L          , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007fff, 0xa000737b, 0                      , 0,
       CP1_                },
};


static const Pool CVT_S_fmt[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0001b7b, &CVT_S_D          , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0003b7b, &CVT_S_W          , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0005b7b, &CVT_S_L          , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007fff, 0xa0007b7b, 0                      , 0,
       CP1_                },
};


static const Pool POOL32Fxf_1[32] = {
    { pool                , MOV_fmt             , 4   , 32,
       0xfc001fff, 0xa000007b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000017b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000027b, 0                      , 0,
       CP1_                },
    { pool                , ABS_fmt             , 4   , 32,
       0xfc001fff, 0xa000037b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000047b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000057b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000067b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000077b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000087b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000097b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa0000a7b, 0                      , 0,
       CP1_                },
    { pool                , NEG_fmt             , 4   , 32,
       0xfc001fff, 0xa0000b7b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa0000c7b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa0000d7b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa0000e7b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa0000f7b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000107b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000117b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000127b, 0                      , 0,
       CP1_                },
    { pool                , CVT_D_fmt           , 4   , 32,
       0xfc001fff, 0xa000137b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000147b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000157b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000167b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000177b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000187b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000197b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa0001a7b, 0                      , 0,
       CP1_                },
    { pool                , CVT_S_fmt           , 4   , 32,
       0xfc001fff, 0xa0001b7b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa0001c7b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa0001d7b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa0001e7b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa0001f7b, 0                      , 0,
       CP1_                },
};


static const Pool POOL32Fxf[4] = {
    { pool                , POOL32Fxf_0         , 64  , 32,
       0xfc0000ff, 0xa000003b, 0                      , 0,
       CP1_                },
    { pool                , POOL32Fxf_1         , 32  , 32,
       0xfc0000ff, 0xa000007b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0000ff, 0xa00000bb, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0000ff, 0xa00000fb, 0                      , 0,
       CP1_                },
};


static const Pool POOL32F_3[8] = {
    { pool                , MIN_fmt             , 2   , 32,
       0xfc00003f, 0xa0000003, 0                      , 0,
       CP1_                },
    { pool                , MAX_fmt             , 2   , 32,
       0xfc00003f, 0xa000000b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xa0000013, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xa000001b, 0                      , 0,
       CP1_                },
    { pool                , MINA_fmt            , 2   , 32,
       0xfc00003f, 0xa0000023, 0                      , 0,
       CP1_                },
    { pool                , MAXA_fmt            , 2   , 32,
       0xfc00003f, 0xa000002b, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xa0000033, 0                      , 0,
       CP1_                },
    { pool                , POOL32Fxf           , 4   , 32,
       0xfc00003f, 0xa000003b, 0                      , 0,
       CP1_                },
};


static const Pool CMP_condn_S[32] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000005, &CMP_AF_S         , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000045, &CMP_UN_S         , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000085, &CMP_EQ_S         , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00000c5, &CMP_UEQ_S        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000105, &CMP_LT_S         , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000145, &CMP_ULT_S        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000185, &CMP_LE_S         , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00001c5, &CMP_ULE_S        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000205, &CMP_SAF_S        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000245, &CMP_SUN_S        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000285, &CMP_SEQ_S        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00002c5, &CMP_SUEQ_S       , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000305, &CMP_SLT_S        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000345, &CMP_SULT_S       , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000385, &CMP_SLE_S        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00003c5, &CMP_SULE_S       , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000405, 0                      , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000445, &CMP_OR_S         , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000485, &CMP_UNE_S        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00004c5, &CMP_NE_S         , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000505, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000545, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000585, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa00005c5, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000605, 0                      , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000645, &CMP_SOR_S        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000685, &CMP_SUNE_S       , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00006c5, &CMP_SNE_S        , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000705, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000745, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000785, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa00007c5, 0                      , 0,
       CP1_                },
};


static const Pool CMP_condn_D[32] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000015, &CMP_AF_D         , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000055, &CMP_UN_D         , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000095, &CMP_EQ_D         , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00000d5, &CMP_UEQ_D        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000115, &CMP_LT_D         , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000155, &CMP_ULT_D        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000195, &CMP_LE_D         , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00001d5, &CMP_ULE_D        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000215, &CMP_SAF_D        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000255, &CMP_SUN_D        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000295, &CMP_SEQ_D        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00002d5, &CMP_SUEQ_D       , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000315, &CMP_SLT_D        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000355, &CMP_SULT_D       , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000395, &CMP_SLE_D        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00003d5, &CMP_SULE_D       , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000415, 0                      , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000455, &CMP_OR_D         , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000495, &CMP_UNE_D        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00004d5, &CMP_NE_D         , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000515, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000555, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000595, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa00005d5, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000615, 0                      , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000655, &CMP_SOR_D        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000695, &CMP_SUNE_D       , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00006d5, &CMP_SNE_D        , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000715, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000755, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000795, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa00007d5, 0                      , 0,
       CP1_                },
};


static const Pool POOL32F_5[8] = {
    { pool                , CMP_condn_S         , 32  , 32,
       0xfc00003f, 0xa0000005, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xa000000d, 0                      , 0,
       CP1_                },
    { pool                , CMP_condn_D         , 32  , 32,
       0xfc00003f, 0xa0000015, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xa000001d, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xa0000025, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xa000002d, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xa0000035, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xa000003d, 0                      , 0,
       CP1_                },
};


static const Pool POOL32F[8] = {
    { pool                , POOL32F_0           , 64  , 32,
       0xfc000007, 0xa0000000, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000007, 0xa0000001, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000007, 0xa0000002, 0                      , 0,
       CP1_                },
    { pool                , POOL32F_3           , 8   , 32,
       0xfc000007, 0xa0000003, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000007, 0xa0000004, 0                      , 0,
       CP1_                },
    { pool                , POOL32F_5           , 8   , 32,
       0xfc000007, 0xa0000005, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000007, 0xa0000006, 0                      , 0,
       CP1_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000007, 0xa0000007, 0                      , 0,
       CP1_                },
};


static const Pool POOL32S_0[64] = {
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000000, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000008, &DLSA             , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000010, &DSLLV            , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000018, &DMUL             , 0,
       MIPS64_             },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000020, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000028, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000030, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000038, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000040, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000048, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000050, &DSRLV            , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000058, &DMUH             , 0,
       MIPS64_             },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000060, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000068, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000070, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000078, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000080, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000088, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000090, &DSRAV            , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000098, &DMULU            , 0,
       MIPS64_             },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000a0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000a8, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000b0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000b8, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000c0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000c8, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000d0, &DROTRV           , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000d8, &DMUHU            , 0,
       MIPS64_             },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000e0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000e8, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000f0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000f8, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000100, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000108, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000110, &DADD             , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000118, &DDIV             , 0,
       MIPS64_             },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000120, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000128, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000130, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000138, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000140, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000148, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000150, &DADDU            , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000158, &DMOD             , 0,
       MIPS64_             },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000160, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000168, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000170, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000178, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000180, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000188, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000190, &DSUB             , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000198, &DDIVU            , 0,
       MIPS64_             },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001a0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001a8, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001b0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001b8, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001c0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001c8, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001d0, &DSUBU            , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001d8, &DMODU            , 0,
       MIPS64_             },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001e0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001e8, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001f0, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001f8, 0                      , 0,
       0x0                 },
};


static const Pool POOL32Sxf_4[128] = {
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000013c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000033c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000053c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000073c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000093c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0000b3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0000d3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0000f3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000113c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000133c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000153c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000173c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000193c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0001b3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0001d3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0001f3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000213c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000233c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000253c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000273c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000293c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0002b3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0002d3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0002f3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000313c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000333c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000353c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000373c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000393c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0003b3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0003d3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0003f3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000413c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000433c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000453c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000473c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000493c, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0xc0004b3c, &DCLO             , 0,
       MIPS64_             },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0004d3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0004f3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000513c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000533c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000553c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000573c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000593c, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0xc0005b3c, &DCLZ             , 0,
       MIPS64_             },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0005d3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0005f3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000613c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000633c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000653c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000673c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000693c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0006b3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0006d3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0006f3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000713c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000733c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000753c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000773c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000793c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0007b3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0007d3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0007f3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000813c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000833c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000853c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000873c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000893c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0008b3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0008d3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0008f3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000913c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000933c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000953c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000973c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000993c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0009b3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0009d3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0009f3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000a13c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000a33c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000a53c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000a73c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000a93c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000ab3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000ad3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000af3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000b13c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000b33c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000b53c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000b73c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000b93c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000bb3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000bd3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000bf3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000c13c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000c33c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000c53c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000c73c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000c93c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000cb3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000cd3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000cf3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000d13c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000d33c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000d53c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000d73c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000d93c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000db3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000dd3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000df3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000e13c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000e33c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000e53c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000e73c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000e93c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000eb3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000ed3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000ef3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000f13c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000f33c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000f53c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000f73c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000f93c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000fb3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000fd3c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000ff3c, 0                      , 0,
       0x0                 },
};


static const Pool POOL32Sxf[8] = {
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc000003c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc000007c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000bc, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000fc, 0                      , 0,
       0x0                 },
    { pool                , POOL32Sxf_4         , 128 , 32,
       0xfc0001ff, 0xc000013c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc000017c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001bc, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001fc, 0                      , 0,
       0x0                 },
};


static const Pool POOL32S_4[8] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00003f, 0xc0000004, &EXTD             , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc00003f, 0xc000000c, &EXTD32           , 0,
       MIPS64_             },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xc0000014, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xc000001c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xc0000024, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xc000002c, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xc0000034, 0                      , 0,
       0x0                 },
    { pool                , POOL32Sxf           , 8   , 32,
       0xfc00003f, 0xc000003c, 0                      , 0,
       0x0                 },
};


static const Pool POOL32S[8] = {
    { pool                , POOL32S_0           , 64  , 32,
       0xfc000007, 0xc0000000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000007, 0xc0000001, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000007, 0xc0000002, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000007, 0xc0000003, 0                      , 0,
       0x0                 },
    { pool                , POOL32S_4           , 8   , 32,
       0xfc000007, 0xc0000004, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000007, 0xc0000005, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000007, 0xc0000006, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000007, 0xc0000007, 0                      , 0,
       0x0                 },
};


static const Pool P_LUI[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc000002, 0xe0000000, &LUI              , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc000002, 0xe0000002, &ALUIPC           , 0,
       0x0                 },
};


static const Pool P_GP_LH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc1c0001, 0x44100000, &LH_GP_           , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc1c0001, 0x44100001, &LHU_GP_          , 0,
       0x0                 },
};


static const Pool P_GP_SH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc1c0001, 0x44140000, &SH_GP_           , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1c0001, 0x44140001, 0                      , 0,
       0x0                 },
};


static const Pool P_GP_CP1[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc1c0003, 0x44180000, &LWC1_GP_         , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc1c0003, 0x44180001, &SWC1_GP_         , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc1c0003, 0x44180002, &LDC1_GP_         , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc1c0003, 0x44180003, &SDC1_GP_         , 0,
       CP1_                },
};


static const Pool P_GP_M64[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc1c0003, 0x441c0000, &LWU_GP_          , 0,
       MIPS64_             },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1c0003, 0x441c0001, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1c0003, 0x441c0002, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1c0003, 0x441c0003, 0                      , 0,
       0x0                 },
};


static const Pool P_GP_BH[8] = {
    { instruction         , 0                   , 0   , 32,
       0xfc1c0000, 0x44000000, &LB_GP_           , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc1c0000, 0x44040000, &SB_GP_           , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc1c0000, 0x44080000, &LBU_GP_          , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc1c0000, 0x440c0000, &ADDIU_GP_B_      , 0,
       0x0                 },
    { pool                , P_GP_LH             , 2   , 32,
       0xfc1c0000, 0x44100000, 0                      , 0,
       0x0                 },
    { pool                , P_GP_SH             , 2   , 32,
       0xfc1c0000, 0x44140000, 0                      , 0,
       0x0                 },
    { pool                , P_GP_CP1            , 4   , 32,
       0xfc1c0000, 0x44180000, 0                      , 0,
       0x0                 },
    { pool                , P_GP_M64            , 4   , 32,
       0xfc1c0000, 0x441c0000, 0                      , 0,
       0x0                 },
};


static const Pool P_LS_U12[16] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84000000, &LB_U12_          , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84001000, &SB_U12_          , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84002000, &LBU_U12_         , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84003000, &PREF_U12_        , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84004000, &LH_U12_          , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84005000, &SH_U12_          , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84006000, &LHU_U12_         , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84007000, &LWU_U12_         , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84008000, &LW_U12_          , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84009000, &SW_U12_          , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x8400a000, &LWC1_U12_        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x8400b000, &SWC1_U12_        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x8400c000, &LD_U12_          , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x8400d000, &SD_U12_          , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x8400e000, &LDC1_U12_        , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x8400f000, &SDC1_U12_        , 0,
       CP1_                },
};


static const Pool P_PREF_S9_[2] = {
    { instruction         , 0                   , 0   , 32,
       0xffe07f00, 0xa7e01800, &SYNCI            , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4001800, &PREF_S9_         , &PREF_S9__cond    ,
       0x0                 },
};


static const Pool P_LS_S0[16] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4000000, &LB_S9_           , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4000800, &SB_S9_           , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4001000, &LBU_S9_          , 0,
       0x0                 },
    { pool                , P_PREF_S9_          , 2   , 32,
       0xfc007f00, 0xa4001800, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4002000, &LH_S9_           , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4002800, &SH_S9_           , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4003000, &LHU_S9_          , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4003800, &LWU_S9_          , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4004000, &LW_S9_           , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4004800, &SW_S9_           , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4005000, &LWC1_S9_         , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4005800, &SWC1_S9_         , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4006000, &LD_S9_           , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4006800, &SD_S9_           , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4007000, &LDC1_S9_         , 0,
       CP1_                },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4007800, &SDC1_S9_         , 0,
       CP1_                },
};


static const Pool ASET_ACLR[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfe007f00, 0xa4001100, &ASET             , 0,
       MCU_                },
    { instruction         , 0                   , 0   , 32,
       0xfe007f00, 0xa6001100, &ACLR             , 0,
       MCU_                },
};


static const Pool P_LL[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007f03, 0xa4005100, &LL               , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc007f03, 0xa4005101, &LLWP             , 0,
       XNP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f03, 0xa4005102, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f03, 0xa4005103, 0                      , 0,
       0x0                 },
};


static const Pool P_SC[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007f03, 0xa4005900, &SC               , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc007f03, 0xa4005901, &SCWP             , 0,
       XNP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f03, 0xa4005902, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f03, 0xa4005903, 0                      , 0,
       0x0                 },
};


static const Pool P_LLD[8] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007f07, 0xa4007100, &LLD              , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc007f07, 0xa4007101, &LLDP             , 0,
       MIPS64_             },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f07, 0xa4007102, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f07, 0xa4007103, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f07, 0xa4007104, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f07, 0xa4007105, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f07, 0xa4007106, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f07, 0xa4007107, 0                      , 0,
       0x0                 },
};


static const Pool P_SCD[8] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007f07, 0xa4007900, &SCD              , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc007f07, 0xa4007901, &SCDP             , 0,
       MIPS64_             },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f07, 0xa4007902, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f07, 0xa4007903, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f07, 0xa4007904, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f07, 0xa4007905, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f07, 0xa4007906, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f07, 0xa4007907, 0                      , 0,
       0x0                 },
};


static const Pool P_LS_S1[16] = {
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f00, 0xa4000100, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f00, 0xa4000900, 0                      , 0,
       0x0                 },
    { pool                , ASET_ACLR           , 2   , 32,
       0xfc007f00, 0xa4001100, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f00, 0xa4001900, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4002100, &UALH             , 0,
       XMMS_               },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4002900, &UASH             , 0,
       XMMS_               },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f00, 0xa4003100, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4003900, &CACHE            , 0,
       CP0_                },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4004100, &LWC2             , 0,
       CP2_                },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4004900, &SWC2             , 0,
       CP2_                },
    { pool                , P_LL                , 4   , 32,
       0xfc007f00, 0xa4005100, 0                      , 0,
       0x0                 },
    { pool                , P_SC                , 4   , 32,
       0xfc007f00, 0xa4005900, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4006100, &LDC2             , 0,
       CP2_                },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4006900, &SDC2             , 0,
       CP2_                },
    { pool                , P_LLD               , 8   , 32,
       0xfc007f00, 0xa4007100, 0                      , 0,
       0x0                 },
    { pool                , P_SCD               , 8   , 32,
       0xfc007f00, 0xa4007900, 0                      , 0,
       0x0                 },
};


static const Pool P_PREFE[2] = {
    { instruction         , 0                   , 0   , 32,
       0xffe07f00, 0xa7e01a00, &SYNCIE           , 0,
       CP0_ | EVA_         },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4001a00, &PREFE            , &PREFE_cond       ,
       CP0_ | EVA_         },
};


static const Pool P_LLE[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007f03, 0xa4005200, &LLE              , 0,
       CP0_ | EVA_         },
    { instruction         , 0                   , 0   , 32,
       0xfc007f03, 0xa4005201, &LLWPE            , 0,
       CP0_ | EVA_         },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f03, 0xa4005202, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f03, 0xa4005203, 0                      , 0,
       0x0                 },
};


static const Pool P_SCE[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007f03, 0xa4005a00, &SCE              , 0,
       CP0_ | EVA_         },
    { instruction         , 0                   , 0   , 32,
       0xfc007f03, 0xa4005a01, &SCWPE            , 0,
       CP0_ | EVA_         },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f03, 0xa4005a02, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f03, 0xa4005a03, 0                      , 0,
       0x0                 },
};


static const Pool P_LS_E0[16] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4000200, &LBE              , 0,
       CP0_ | EVA_         },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4000a00, &SBE              , 0,
       CP0_ | EVA_         },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4001200, &LBUE             , 0,
       CP0_ | EVA_         },
    { pool                , P_PREFE             , 2   , 32,
       0xfc007f00, 0xa4001a00, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4002200, &LHE              , 0,
       CP0_ | EVA_         },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4002a00, &SHE              , 0,
       CP0_ | EVA_         },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4003200, &LHUE             , 0,
       CP0_ | EVA_         },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4003a00, &CACHEE           , 0,
       CP0_ | EVA_         },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4004200, &LWE              , 0,
       CP0_ | EVA_         },
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4004a00, &SWE              , 0,
       CP0_ | EVA_         },
    { pool                , P_LLE               , 4   , 32,
       0xfc007f00, 0xa4005200, 0                      , 0,
       0x0                 },
    { pool                , P_SCE               , 4   , 32,
       0xfc007f00, 0xa4005a00, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f00, 0xa4006200, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f00, 0xa4006a00, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f00, 0xa4007200, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f00, 0xa4007a00, 0                      , 0,
       0x0                 },
};


static const Pool P_LS_WM[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc000f00, 0xa4000400, &LWM              , 0,
       XMMS_               },
    { instruction         , 0                   , 0   , 32,
       0xfc000f00, 0xa4000c00, &SWM              , 0,
       XMMS_               },
};


static const Pool P_LS_UAWM[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc000f00, 0xa4000500, &UALWM            , 0,
       XMMS_               },
    { instruction         , 0                   , 0   , 32,
       0xfc000f00, 0xa4000d00, &UASWM            , 0,
       XMMS_               },
};


static const Pool P_LS_DM[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc000f00, 0xa4000600, &LDM              , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc000f00, 0xa4000e00, &SDM              , 0,
       MIPS64_             },
};


static const Pool P_LS_UADM[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc000f00, 0xa4000700, &UALDM            , 0,
       MIPS64_             },
    { instruction         , 0                   , 0   , 32,
       0xfc000f00, 0xa4000f00, &UASDM            , 0,
       MIPS64_             },
};


static const Pool P_LS_S9[8] = {
    { pool                , P_LS_S0             , 16  , 32,
       0xfc000700, 0xa4000000, 0                      , 0,
       0x0                 },
    { pool                , P_LS_S1             , 16  , 32,
       0xfc000700, 0xa4000100, 0                      , 0,
       0x0                 },
    { pool                , P_LS_E0             , 16  , 32,
       0xfc000700, 0xa4000200, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000700, 0xa4000300, 0                      , 0,
       0x0                 },
    { pool                , P_LS_WM             , 2   , 32,
       0xfc000700, 0xa4000400, 0                      , 0,
       0x0                 },
    { pool                , P_LS_UAWM           , 2   , 32,
       0xfc000700, 0xa4000500, 0                      , 0,
       0x0                 },
    { pool                , P_LS_DM             , 2   , 32,
       0xfc000700, 0xa4000600, 0                      , 0,
       0x0                 },
    { pool                , P_LS_UADM           , 2   , 32,
       0xfc000700, 0xa4000700, 0                      , 0,
       0x0                 },
};


static const Pool P_BAL[2] = {
    { branch_instruction  , 0                   , 0   , 32,
       0xfe000000, 0x28000000, &BC_32_           , 0,
       0x0                 },
    { call_instruction    , 0                   , 0   , 32,
       0xfe000000, 0x2a000000, &BALC_32_         , 0,
       0x0                 },
};


static const Pool P_BALRSC[2] = {
    { branch_instruction  , 0                   , 0   , 32,
       0xffe0f000, 0x48008000, &BRSC             , 0,
       0x0                 },
    { call_instruction    , 0                   , 0   , 32,
       0xfc00f000, 0x48008000, &BALRSC           , &BALRSC_cond      ,
       0x0                 },
};


static const Pool P_J[16] = {
    { call_instruction    , 0                   , 0   , 32,
       0xfc00f000, 0x48000000, &JALRC_32_        , 0,
       0x0                 },
    { call_instruction    , 0                   , 0   , 32,
       0xfc00f000, 0x48001000, &JALRC_HB         , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x48002000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x48003000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x48004000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x48005000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x48006000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x48007000, 0                      , 0,
       0x0                 },
    { pool                , P_BALRSC            , 2   , 32,
       0xfc00f000, 0x48008000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x48009000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x4800a000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x4800b000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x4800c000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x4800d000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x4800e000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x4800f000, 0                      , 0,
       0x0                 },
};


static const Pool P_BR3A[32] = {
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1fc000, 0x88004000, &BC1EQZC          , 0,
       CP1_                },
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1fc000, 0x88014000, &BC1NEZC          , 0,
       CP1_                },
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1fc000, 0x88024000, &BC2EQZC          , 0,
       CP2_                },
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1fc000, 0x88034000, &BC2NEZC          , 0,
       CP2_                },
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1fc000, 0x88044000, &BPOSGE32C        , 0,
       DSP_                },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88054000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88064000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88074000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88084000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88094000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x880a4000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x880b4000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x880c4000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x880d4000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x880e4000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x880f4000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88104000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88114000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88124000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88134000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88144000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88154000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88164000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88174000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88184000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88194000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x881a4000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x881b4000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x881c4000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x881d4000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x881e4000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x881f4000, 0                      , 0,
       0x0                 },
};


static const Pool P_BR1[4] = {
    { branch_instruction  , 0                   , 0   , 32,
       0xfc00c000, 0x88000000, &BEQC_32_         , 0,
       0x0                 },
    { pool                , P_BR3A              , 32  , 32,
       0xfc00c000, 0x88004000, 0                      , 0,
       0x0                 },
    { branch_instruction  , 0                   , 0   , 32,
       0xfc00c000, 0x88008000, &BGEC             , 0,
       0x0                 },
    { branch_instruction  , 0                   , 0   , 32,
       0xfc00c000, 0x8800c000, &BGEUC            , 0,
       0x0                 },
};


static const Pool P_BR2[4] = {
    { branch_instruction  , 0                   , 0   , 32,
       0xfc00c000, 0xa8000000, &BNEC_32_         , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc00c000, 0xa8004000, 0                      , 0,
       0x0                 },
    { branch_instruction  , 0                   , 0   , 32,
       0xfc00c000, 0xa8008000, &BLTC             , 0,
       0x0                 },
    { branch_instruction  , 0                   , 0   , 32,
       0xfc00c000, 0xa800c000, &BLTUC            , 0,
       0x0                 },
};


static const Pool P_BRI[8] = {
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1c0000, 0xc8000000, &BEQIC            , 0,
       0x0                 },
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1c0000, 0xc8040000, &BBEQZC           , 0,
       XMMS_               },
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1c0000, 0xc8080000, &BGEIC            , 0,
       0x0                 },
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1c0000, 0xc80c0000, &BGEIUC           , 0,
       0x0                 },
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1c0000, 0xc8100000, &BNEIC            , 0,
       0x0                 },
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1c0000, 0xc8140000, &BBNEZC           , 0,
       XMMS_               },
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1c0000, 0xc8180000, &BLTIC            , 0,
       0x0                 },
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1c0000, 0xc81c0000, &BLTIUC           , 0,
       0x0                 },
};


static const Pool P32[32] = {
    { pool                , P_ADDIU             , 2   , 32,
       0xfc000000, 0x00000000, 0                      , 0,
       0x0                 },
    { pool                , P32A                , 8   , 32,
       0xfc000000, 0x20000000, 0                      , 0,
       0x0                 },
    { pool                , P_GP_W              , 4   , 32,
       0xfc000000, 0x40000000, 0                      , 0,
       0x0                 },
    { pool                , POOL48I             , 32  , 48,
       0xfc0000000000ull, 0x600000000000ull, 0                      , 0,
       0x0                 },
    { pool                , P_U12               , 16  , 32,
       0xfc000000, 0x80000000, 0                      , 0,
       0x0                 },
    { pool                , POOL32F             , 8   , 32,
       0xfc000000, 0xa0000000, 0                      , 0,
       CP1_                },
    { pool                , POOL32S             , 8   , 32,
       0xfc000000, 0xc0000000, 0                      , 0,
       0x0                 },
    { pool                , P_LUI               , 2   , 32,
       0xfc000000, 0xe0000000, 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 32,
       0xfc000000, 0x04000000, &ADDIUPC_32_      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0x24000000, 0                      , 0,
       0x0                 },
    { pool                , P_GP_BH             , 8   , 32,
       0xfc000000, 0x44000000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0x64000000, 0                      , 0,
       0x0                 },
    { pool                , P_LS_U12            , 16  , 32,
       0xfc000000, 0x84000000, 0                      , 0,
       0x0                 },
    { pool                , P_LS_S9             , 8   , 32,
       0xfc000000, 0xa4000000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0xc4000000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0xe4000000, 0                      , 0,
       0x0                 },
    { call_instruction    , 0                   , 0   , 32,
       0xfc000000, 0x08000000, &MOVE_BALC        , 0,
       XMMS_               },
    { pool                , P_BAL               , 2   , 32,
       0xfc000000, 0x28000000, 0                      , 0,
       0x0                 },
    { pool                , P_J                 , 16  , 32,
       0xfc000000, 0x48000000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0x68000000, 0                      , 0,
       0x0                 },
    { pool                , P_BR1               , 4   , 32,
       0xfc000000, 0x88000000, 0                      , 0,
       0x0                 },
    { pool                , P_BR2               , 4   , 32,
       0xfc000000, 0xa8000000, 0                      , 0,
       0x0                 },
    { pool                , P_BRI               , 8   , 32,
       0xfc000000, 0xc8000000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0xe8000000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0x0c000000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0x2c000000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0x4c000000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0x6c000000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0x8c000000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0xac000000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0xcc000000, 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0xec000000, 0                      , 0,
       0x0                 },
};


static const Pool P16_SYSCALL[2] = {
    { instruction         , 0                   , 0   , 16,
       0xfffc    , 0x1008    , &SYSCALL_16_      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 16,
       0xfffc    , 0x100c    , &HYPCALL_16_      , 0,
       CP0_ | VZ_          },
};


static const Pool P16_RI[4] = {
    { reserved_block      , 0                   , 0   , 16,
       0xfff8    , 0x1000    , 0                      , 0,
       0x0                 },
    { pool                , P16_SYSCALL         , 2   , 16,
       0xfff8    , 0x1008    , 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 16,
       0xfff8    , 0x1010    , &BREAK_16_        , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 16,
       0xfff8    , 0x1018    , &SDBBP_16_        , 0,
       EJTAG_              },
};


static const Pool P16_MV[2] = {
    { pool                , P16_RI              , 4   , 16,
       0xffe0    , 0x1000    , 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0x1000    , &MOVE             , &MOVE_cond        ,
       0x0                 },
};


static const Pool P16_SHIFT[2] = {
    { instruction         , 0                   , 0   , 16,
       0xfc08    , 0x3000    , &SLL_16_          , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 16,
       0xfc08    , 0x3008    , &SRL_16_          , 0,
       0x0                 },
};


static const Pool POOL16C_00[4] = {
    { instruction         , 0                   , 0   , 16,
       0xfc0f    , 0x5000    , &NOT_16_          , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 16,
       0xfc0f    , 0x5004    , &XOR_16_          , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 16,
       0xfc0f    , 0x5008    , &AND_16_          , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 16,
       0xfc0f    , 0x500c    , &OR_16_           , 0,
       0x0                 },
};


static const Pool POOL16C_0[2] = {
    { pool                , POOL16C_00          , 4   , 16,
       0xfc03    , 0x5000    , 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 16,
       0xfc03    , 0x5002    , 0                      , 0,
       0x0                 },
};


static const Pool P16C[2] = {
    { pool                , POOL16C_0           , 2   , 16,
       0xfc01    , 0x5000    , 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 16,
       0xfc01    , 0x5001    , &LWXS_16_         , 0,
       0x0                 },
};


static const Pool P16_A1[2] = {
    { reserved_block      , 0                   , 0   , 16,
       0xfc40    , 0x7000    , 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 16,
       0xfc40    , 0x7040    , &ADDIU_R1_SP_     , 0,
       0x0                 },
};


static const Pool P_ADDIU_RS5_[2] = {
    { instruction         , 0                   , 0   , 16,
       0xffe8    , 0x9008    , &NOP_16_          , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 16,
       0xfc08    , 0x9008    , &ADDIU_RS5_       , &ADDIU_RS5__cond  ,
       0x0                 },
};


static const Pool P16_A2[2] = {
    { instruction         , 0                   , 0   , 16,
       0xfc08    , 0x9000    , &ADDIU_R2_        , 0,
       0x0                 },
    { pool                , P_ADDIU_RS5_        , 2   , 16,
       0xfc08    , 0x9008    , 0                      , 0,
       0x0                 },
};


static const Pool P16_ADDU[2] = {
    { instruction         , 0                   , 0   , 16,
       0xfc01    , 0xb000    , &ADDU_16_         , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 16,
       0xfc01    , 0xb001    , &SUBU_16_         , 0,
       0x0                 },
};


static const Pool P16_JRC[2] = {
    { branch_instruction  , 0                   , 0   , 16,
       0xfc1f    , 0xd800    , &JRC              , 0,
       0x0                 },
    { call_instruction    , 0                   , 0   , 16,
       0xfc1f    , 0xd810    , &JALRC_16_        , 0,
       0x0                 },
};


static const Pool P16_BR1[2] = {
    { branch_instruction  , 0                   , 0   , 16,
       0xfc00    , 0xd800    , &BEQC_16_         , &BEQC_16__cond    ,
       XMMS_               },
    { branch_instruction  , 0                   , 0   , 16,
       0xfc00    , 0xd800    , &BNEC_16_         , &BNEC_16__cond    ,
       XMMS_               },
};


static const Pool P16_BR[2] = {
    { pool                , P16_JRC             , 2   , 16,
       0xfc0f    , 0xd800    , 0                      , 0,
       0x0                 },
    { pool                , P16_BR1             , 2   , 16,
       0xfc00    , 0xd800    , 0                      , &P16_BR1_cond     ,
       0x0                 },
};


static const Pool P16_SR[2] = {
    { instruction         , 0                   , 0   , 16,
       0xfd00    , 0x1c00    , &SAVE_16_         , 0,
       0x0                 },
    { return_instruction  , 0                   , 0   , 16,
       0xfd00    , 0x1d00    , &RESTORE_JRC_16_  , 0,
       0x0                 },
};


static const Pool P16_4X4[4] = {
    { instruction         , 0                   , 0   , 16,
       0xfd08    , 0x3c00    , &ADDU_4X4_        , 0,
       XMMS_               },
    { instruction         , 0                   , 0   , 16,
       0xfd08    , 0x3c08    , &MUL_4X4_         , 0,
       XMMS_               },
    { reserved_block      , 0                   , 0   , 16,
       0xfd08    , 0x3d00    , 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 16,
       0xfd08    , 0x3d08    , 0                      , 0,
       0x0                 },
};


static const Pool P16_LB[4] = {
    { instruction         , 0                   , 0   , 16,
       0xfc0c    , 0x5c00    , &LB_16_           , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 16,
       0xfc0c    , 0x5c04    , &SB_16_           , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 16,
       0xfc0c    , 0x5c08    , &LBU_16_          , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 16,
       0xfc0c    , 0x5c0c    , 0                      , 0,
       0x0                 },
};


static const Pool P16_LH[4] = {
    { instruction         , 0                   , 0   , 16,
       0xfc09    , 0x7c00    , &LH_16_           , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 16,
       0xfc09    , 0x7c01    , &SH_16_           , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 16,
       0xfc09    , 0x7c08    , &LHU_16_          , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 16,
       0xfc09    , 0x7c09    , 0                      , 0,
       0x0                 },
};


static const Pool P16[32] = {
    { pool                , P16_MV              , 2   , 16,
       0xfc00    , 0x1000    , 0                      , 0,
       0x0                 },
    { pool                , P16_SHIFT           , 2   , 16,
       0xfc00    , 0x3000    , 0                      , 0,
       0x0                 },
    { pool                , P16C                , 2   , 16,
       0xfc00    , 0x5000    , 0                      , 0,
       0x0                 },
    { pool                , P16_A1              , 2   , 16,
       0xfc00    , 0x7000    , 0                      , 0,
       0x0                 },
    { pool                , P16_A2              , 2   , 16,
       0xfc00    , 0x9000    , 0                      , 0,
       0x0                 },
    { pool                , P16_ADDU            , 2   , 16,
       0xfc00    , 0xb000    , 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0xd000    , &LI_16_           , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0xf000    , &ANDI_16_         , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0x1400    , &LW_16_           , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0x3400    , &LW_SP_           , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0x5400    , &LW_GP16_         , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0x7400    , &LW_4X4_          , 0,
       XMMS_               },
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0x9400    , &SW_16_           , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0xb400    , &SW_SP_           , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0xd400    , &SW_GP16_         , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0xf400    , &SW_4X4_          , 0,
       XMMS_               },
    { branch_instruction  , 0                   , 0   , 16,
       0xfc00    , 0x1800    , &BC_16_           , 0,
       0x0                 },
    { call_instruction    , 0                   , 0   , 16,
       0xfc00    , 0x3800    , &BALC_16_         , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 16,
       0xfc00    , 0x5800    , 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 16,
       0xfc00    , 0x7800    , 0                      , 0,
       0x0                 },
    { branch_instruction  , 0                   , 0   , 16,
       0xfc00    , 0x9800    , &BEQZC_16_        , 0,
       0x0                 },
    { branch_instruction  , 0                   , 0   , 16,
       0xfc00    , 0xb800    , &BNEZC_16_        , 0,
       0x0                 },
    { pool                , P16_BR              , 2   , 16,
       0xfc00    , 0xd800    , 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 16,
       0xfc00    , 0xf800    , 0                      , 0,
       0x0                 },
    { pool                , P16_SR              , 2   , 16,
       0xfc00    , 0x1c00    , 0                      , 0,
       0x0                 },
    { pool                , P16_4X4             , 4   , 16,
       0xfc00    , 0x3c00    , 0                      , 0,
       0x0                 },
    { pool                , P16_LB              , 4   , 16,
       0xfc00    , 0x5c00    , 0                      , 0,
       0x0                 },
    { pool                , P16_LH              , 4   , 16,
       0xfc00    , 0x7c00    , 0                      , 0,
       0x0                 },
    { reserved_block      , 0                   , 0   , 16,
       0xfc00    , 0x9c00    , 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0xbc00    , &MOVEP            , 0,
       XMMS_               },
    { reserved_block      , 0                   , 0   , 16,
       0xfc00    , 0xdc00    , 0                      , 0,
       0x0                 },
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0xfc00    , &MOVEP_REV_       , 0,
       XMMS_               },
};


static const Pool MAJOR[2] = {
    { pool                , P32                 , 32  , 32,
       0x10000000, 0x00000000, 0                      , 0,
       0x0                 },
    { pool                , P16                 , 32  , 16,
       0x1000    , 0x1000    , 0                      , 0,
       0x0                 },
};

static int Disassemble(const uint16 *data, char **dis,
                     TABLE_ENTRY_TYPE *type, const Pool *table,
                     int table_size, Dis_info *info)
{
    for (int i = 0; i < table_size; i++) {
        uint64 op_code = extract_op_code_value(data,
                             table[i].instructions_size);
        if ((op_code & table[i].mask) == table[i].value) {
            conditional_function cond = table[i].condition;
            if ((cond == NULL) || cond(op_code)) {
                if (table[i].type == pool) {
                    return Disassemble(data, dis, type,
                                       table[i].next_table,
                                       table[i].next_table_size,
                                       info);
                } else if ((table[i].type == instruction) ||
                           (table[i].type == call_instruction) ||
                           (table[i].type == branch_instruction) ||
                           (table[i].type == return_instruction)) {
                    disassembly_function dis_fn = table[i].disassembly;
                    if (dis_fn == 0) {
                        *dis = g_strdup(
                            "disassembler failure - bad table entry");
                        return -6;
                    }
                    *type = table[i].type;
                    *dis = dis_fn(op_code, info);
                    return table[i].instructions_size;
                } else {
                    *dis = g_strdup("reserved instruction");
                    return -2;
                }
            }
        }
    }
    *dis = g_strdup("failed to disassemble");
    return -1;
}


static bool nanomips_dis(const uint16_t *data, char **buf, Dis_info *info)
{
    TABLE_ENTRY_TYPE type;

    if (unlikely(sigsetjmp(info->buf, 0) != 0)) {
        return false;
    }
    return Disassemble(data, buf, &type, MAJOR, ARRAY_SIZE(MAJOR), info) >= 0;
}

static bool read_u16(uint16_t *ret, bfd_vma memaddr,
                     struct disassemble_info *info)
{
    int status = (*info->read_memory_func)(memaddr, (bfd_byte *)ret, 2, info);
    if (status != 0) {
        (*info->memory_error_func)(status, memaddr, info);
        return false;
    }

    if ((info->endian == BFD_ENDIAN_BIG) != HOST_BIG_ENDIAN) {
        bswap16s(ret);
    }
    return true;
}

int print_insn_nanomips(bfd_vma memaddr, struct disassemble_info *info)
{
    int length;
    uint16_t words[3] = { };
    g_autofree char *buf = NULL;

    info->bytes_per_chunk = 2;
    info->display_endian = info->endian;
    info->insn_info_valid = 1;
    info->branch_delay_insns = 0;
    info->data_size = 0;
    info->insn_type = dis_nonbranch;
    info->target = 0;
    info->target2 = 0;

    Dis_info disassm_info;
    disassm_info.m_pc = memaddr;
    disassm_info.fprintf_func = info->fprintf_func;
    disassm_info.stream = info->stream;

    if (!read_u16(&words[0], memaddr, info)) {
        return -1;
    }
    length = 2;

    if ((words[0] & 0x1000) == 0) {
        if (!read_u16(&words[1], memaddr + 2, info)) {
            return -1;
        }
        length = 4;

        if ((words[0] >> 10) == 0x18) {
            if (!read_u16(&words[1], memaddr + 4, info)) {
                return -1;
            }
            length = 6;
        }
    }

    for (int i = 0; i < ARRAY_SIZE(words); i++) {
        if (i * 2 < length) {
            (*info->fprintf_func)(info->stream, "%04x ", words[i]);
        } else {
            (*info->fprintf_func)(info->stream, "     ");
        }
    }

    if (nanomips_dis(words, &buf, &disassm_info)) {
        (*info->fprintf_func) (info->stream, "%s", buf);
    }

    return length;
}
