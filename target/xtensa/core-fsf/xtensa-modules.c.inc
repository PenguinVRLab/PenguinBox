/* Xtensa configuration-specific ISA information.
   Copyright 2003, 2004, 2005 Free Software Foundation, Inc.

   This file is part of BFD, the Binary File Descriptor library.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see
   <https://www.gnu.org/licenses/>.  */

#include "qemu/osdep.h"
#include "xtensa-isa.h"
#include "xtensa-isa-internal.h"



static xtensa_sysreg_internal sysregs[] = {
  { "LBEG", 0, 0 },
  { "LEND", 1, 0 },
  { "LCOUNT", 2, 0 },
  { "PTEVADDR", 83, 0 },
  { "DDR", 104, 0 },
  { "176", 176, 0 },
  { "208", 208, 0 },
  { "INTERRUPT", 226, 0 },
  { "INTCLEAR", 227, 0 },
  { "CCOUNT", 234, 0 },
  { "PRID", 235, 0 },
  { "ICOUNT", 236, 0 },
  { "CCOMPARE0", 240, 0 },
  { "CCOMPARE1", 241, 0 },
  { "CCOMPARE2", 242, 0 },
  { "EPC1", 177, 0 },
  { "EPC2", 178, 0 },
  { "EPC3", 179, 0 },
  { "EPC4", 180, 0 },
  { "EXCSAVE1", 209, 0 },
  { "EXCSAVE2", 210, 0 },
  { "EXCSAVE3", 211, 0 },
  { "EXCSAVE4", 212, 0 },
  { "EPS2", 194, 0 },
  { "EPS3", 195, 0 },
  { "EPS4", 196, 0 },
  { "EXCCAUSE", 232, 0 },
  { "DEPC", 192, 0 },
  { "EXCVADDR", 238, 0 },
  { "WINDOWBASE", 72, 0 },
  { "WINDOWSTART", 73, 0 },
  { "SAR", 3, 0 },
  { "LITBASE", 5, 0 },
  { "PS", 230, 0 },
  { "MISC0", 244, 0 },
  { "MISC1", 245, 0 },
  { "INTENABLE", 228, 0 },
  { "DBREAKA0", 144, 0 },
  { "DBREAKC0", 160, 0 },
  { "DBREAKA1", 145, 0 },
  { "DBREAKC1", 161, 0 },
  { "IBREAKA0", 128, 0 },
  { "IBREAKA1", 129, 0 },
  { "IBREAKENABLE", 96, 0 },
  { "ICOUNTLEVEL", 237, 0 },
  { "DEBUGCAUSE", 233, 0 },
  { "RASID", 90, 0 },
  { "ITLBCFG", 91, 0 },
  { "DTLBCFG", 92, 0 }
};

#define NUM_SYSREGS 49
#define MAX_SPECIAL_REG 245
#define MAX_USER_REG 0



static xtensa_state_internal states[] = {
  { "LCOUNT", 32, 0 },
  { "PC", 32, 0 },
  { "ICOUNT", 32, 0 },
  { "DDR", 32, 0 },
  { "INTERRUPT", 17, 0 },
  { "CCOUNT", 32, 0 },
  { "XTSYNC", 1, 0 },
  { "EPC1", 32, 0 },
  { "EPC2", 32, 0 },
  { "EPC3", 32, 0 },
  { "EPC4", 32, 0 },
  { "EXCSAVE1", 32, 0 },
  { "EXCSAVE2", 32, 0 },
  { "EXCSAVE3", 32, 0 },
  { "EXCSAVE4", 32, 0 },
  { "EPS2", 15, 0 },
  { "EPS3", 15, 0 },
  { "EPS4", 15, 0 },
  { "EXCCAUSE", 6, 0 },
  { "PSINTLEVEL", 4, 0 },
  { "PSUM", 1, 0 },
  { "PSWOE", 1, 0 },
  { "PSRING", 2, 0 },
  { "PSEXCM", 1, 0 },
  { "DEPC", 32, 0 },
  { "EXCVADDR", 32, 0 },
  { "WindowBase", 4, 0 },
  { "WindowStart", 16, 0 },
  { "PSCALLINC", 2, 0 },
  { "PSOWB", 4, 0 },
  { "LBEG", 32, 0 },
  { "LEND", 32, 0 },
  { "SAR", 6, 0 },
  { "LITBADDR", 20, 0 },
  { "LITBEN", 1, 0 },
  { "MISC0", 32, 0 },
  { "MISC1", 32, 0 },
  { "InOCDMode", 1, 0 },
  { "INTENABLE", 17, 0 },
  { "DBREAKA0", 32, 0 },
  { "DBREAKC0", 8, 0 },
  { "DBREAKA1", 32, 0 },
  { "DBREAKC1", 8, 0 },
  { "IBREAKA0", 32, 0 },
  { "IBREAKA1", 32, 0 },
  { "IBREAKENABLE", 2, 0 },
  { "ICOUNTLEVEL", 4, 0 },
  { "DEBUGCAUSE", 6, 0 },
  { "DBNUM", 4, 0 },
  { "CCOMPARE0", 32, 0 },
  { "CCOMPARE1", 32, 0 },
  { "CCOMPARE2", 32, 0 },
  { "ASID3", 8, 0 },
  { "ASID2", 8, 0 },
  { "ASID1", 8, 0 },
  { "INSTPGSZID4", 2, 0 },
  { "DATAPGSZID4", 2, 0 },
  { "PTBASE", 10, 0 }
};

#define NUM_STATES 58

#define STATE_LCOUNT 0
#define STATE_PC 1
#define STATE_ICOUNT 2
#define STATE_DDR 3
#define STATE_INTERRUPT 4
#define STATE_CCOUNT 5
#define STATE_XTSYNC 6
#define STATE_EPC1 7
#define STATE_EPC2 8
#define STATE_EPC3 9
#define STATE_EPC4 10
#define STATE_EXCSAVE1 11
#define STATE_EXCSAVE2 12
#define STATE_EXCSAVE3 13
#define STATE_EXCSAVE4 14
#define STATE_EPS2 15
#define STATE_EPS3 16
#define STATE_EPS4 17
#define STATE_EXCCAUSE 18
#define STATE_PSINTLEVEL 19
#define STATE_PSUM 20
#define STATE_PSWOE 21
#define STATE_PSRING 22
#define STATE_PSEXCM 23
#define STATE_DEPC 24
#define STATE_EXCVADDR 25
#define STATE_WindowBase 26
#define STATE_WindowStart 27
#define STATE_PSCALLINC 28
#define STATE_PSOWB 29
#define STATE_LBEG 30
#define STATE_LEND 31
#define STATE_SAR 32
#define STATE_LITBADDR 33
#define STATE_LITBEN 34
#define STATE_MISC0 35
#define STATE_MISC1 36
#define STATE_InOCDMode 37
#define STATE_INTENABLE 38
#define STATE_DBREAKA0 39
#define STATE_DBREAKC0 40
#define STATE_DBREAKA1 41
#define STATE_DBREAKC1 42
#define STATE_IBREAKA0 43
#define STATE_IBREAKA1 44
#define STATE_IBREAKENABLE 45
#define STATE_ICOUNTLEVEL 46
#define STATE_DEBUGCAUSE 47
#define STATE_DBNUM 48
#define STATE_CCOMPARE0 49
#define STATE_CCOMPARE1 50
#define STATE_CCOMPARE2 51
#define STATE_ASID3 52
#define STATE_ASID2 53
#define STATE_ASID1 54
#define STATE_INSTPGSZID4 55
#define STATE_DATAPGSZID4 56
#define STATE_PTBASE 57



static unsigned
Field_t_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 12) >> 28);
  return tie_t;
}

static void
Field_t_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf0000) | (tie_t << 16);
}

static unsigned
Field_s_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 16) >> 28);
  return tie_t;
}

static void
Field_s_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf000) | (tie_t << 12);
}

static unsigned
Field_r_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 20) >> 28);
  return tie_t;
}

static void
Field_r_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf00) | (tie_t << 8);
}

static unsigned
Field_op2_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 28) >> 28);
  return tie_t;
}

static void
Field_op2_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf) | (tie_t << 0);
}

static unsigned
Field_op1_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 24) >> 28);
  return tie_t;
}

static void
Field_op1_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf0) | (tie_t << 4);
}

static unsigned
Field_op0_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 8) >> 28);
  return tie_t;
}

static void
Field_op0_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf00000) | (tie_t << 20);
}

static unsigned
Field_n_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 2) | ((insn[0] << 12) >> 30);
  return tie_t;
}

static void
Field_n_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 30) >> 30;
  insn[0] = (insn[0] & ~0xc0000) | (tie_t << 18);
}

static unsigned
Field_m_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 2) | ((insn[0] << 14) >> 30);
  return tie_t;
}

static void
Field_m_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 30) >> 30;
  insn[0] = (insn[0] & ~0x30000) | (tie_t << 16);
}

static unsigned
Field_sr_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 16) >> 28);
  tie_t = (tie_t << 4) | ((insn[0] << 20) >> 28);
  return tie_t;
}

static void
Field_sr_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf00) | (tie_t << 8);
  tie_t = (val << 24) >> 28;
  insn[0] = (insn[0] & ~0xf000) | (tie_t << 12);
}

static unsigned
Field_thi3_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 3) | ((insn[0] << 12) >> 29);
  return tie_t;
}

static void
Field_thi3_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 29) >> 29;
  insn[0] = (insn[0] & ~0xe0000) | (tie_t << 17);
}

static unsigned
Field_op0_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 16) >> 28);
  return tie_t;
}

static void
Field_op0_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf000) | (tie_t << 12);
}

static unsigned
Field_t_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 20) >> 28);
  return tie_t;
}

static void
Field_t_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf00) | (tie_t << 8);
}

static unsigned
Field_r_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 28) >> 28);
  return tie_t;
}

static void
Field_r_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf) | (tie_t << 0);
}

static unsigned
Field_op0_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 16) >> 28);
  return tie_t;
}

static void
Field_op0_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf000) | (tie_t << 12);
}

static unsigned
Field_z_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 21) >> 31);
  return tie_t;
}

static void
Field_z_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 31) >> 31;
  insn[0] = (insn[0] & ~0x400) | (tie_t << 10);
}

static unsigned
Field_i_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 20) >> 31);
  return tie_t;
}

static void
Field_i_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 31) >> 31;
  insn[0] = (insn[0] & ~0x800) | (tie_t << 11);
}

static unsigned
Field_s_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 24) >> 28);
  return tie_t;
}

static void
Field_s_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf0) | (tie_t << 4);
}

static unsigned
Field_t_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 20) >> 28);
  return tie_t;
}

static void
Field_t_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf00) | (tie_t << 8);
}

static unsigned
Field_bbi4_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 23) >> 31);
  return tie_t;
}

static void
Field_bbi4_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 31) >> 31;
  insn[0] = (insn[0] & ~0x100) | (tie_t << 8);
}

static unsigned
Field_bbi_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 23) >> 31);
  tie_t = (tie_t << 4) | ((insn[0] << 12) >> 28);
  return tie_t;
}

static void
Field_bbi_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf0000) | (tie_t << 16);
  tie_t = (val << 27) >> 31;
  insn[0] = (insn[0] & ~0x100) | (tie_t << 8);
}

static unsigned
Field_imm12_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 12) | ((insn[0] << 20) >> 20);
  return tie_t;
}

static void
Field_imm12_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 20) >> 20;
  insn[0] = (insn[0] & ~0xfff) | (tie_t << 0);
}

static unsigned
Field_imm8_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 8) | ((insn[0] << 24) >> 24);
  return tie_t;
}

static void
Field_imm8_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 24) >> 24;
  insn[0] = (insn[0] & ~0xff) | (tie_t << 0);
}

static unsigned
Field_s_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 24) >> 28);
  return tie_t;
}

static void
Field_s_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf0) | (tie_t << 4);
}

static unsigned
Field_imm12b_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 16) >> 28);
  tie_t = (tie_t << 8) | ((insn[0] << 24) >> 24);
  return tie_t;
}

static void
Field_imm12b_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 24) >> 24;
  insn[0] = (insn[0] & ~0xff) | (tie_t << 0);
  tie_t = (val << 20) >> 28;
  insn[0] = (insn[0] & ~0xf000) | (tie_t << 12);
}

static unsigned
Field_imm16_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 16) | ((insn[0] << 16) >> 16);
  return tie_t;
}

static void
Field_imm16_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 16) >> 16;
  insn[0] = (insn[0] & ~0xffff) | (tie_t << 0);
}

static unsigned
Field_offset_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 18) | ((insn[0] << 14) >> 14);
  return tie_t;
}

static void
Field_offset_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 14) >> 14;
  insn[0] = (insn[0] & ~0x3ffff) | (tie_t << 0);
}

static unsigned
Field_r_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 28) >> 28);
  return tie_t;
}

static void
Field_r_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf) | (tie_t << 0);
}

static unsigned
Field_sa4_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 31) >> 31);
  return tie_t;
}

static void
Field_sa4_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 31) >> 31;
  insn[0] = (insn[0] & ~0x1) | (tie_t << 0);
}

static unsigned
Field_sae4_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 27) >> 31);
  return tie_t;
}

static void
Field_sae4_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 31) >> 31;
  insn[0] = (insn[0] & ~0x10) | (tie_t << 4);
}

static unsigned
Field_sae_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 27) >> 31);
  tie_t = (tie_t << 4) | ((insn[0] << 16) >> 28);
  return tie_t;
}

static void
Field_sae_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf000) | (tie_t << 12);
  tie_t = (val << 27) >> 31;
  insn[0] = (insn[0] & ~0x10) | (tie_t << 4);
}

static unsigned
Field_sal_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 31) >> 31);
  tie_t = (tie_t << 4) | ((insn[0] << 12) >> 28);
  return tie_t;
}

static void
Field_sal_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf0000) | (tie_t << 16);
  tie_t = (val << 27) >> 31;
  insn[0] = (insn[0] & ~0x1) | (tie_t << 0);
}

static unsigned
Field_sargt_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 31) >> 31);
  tie_t = (tie_t << 4) | ((insn[0] << 16) >> 28);
  return tie_t;
}

static void
Field_sargt_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf000) | (tie_t << 12);
  tie_t = (val << 27) >> 31;
  insn[0] = (insn[0] & ~0x1) | (tie_t << 0);
}

static unsigned
Field_sas4_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 15) >> 31);
  return tie_t;
}

static void
Field_sas4_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 31) >> 31;
  insn[0] = (insn[0] & ~0x10000) | (tie_t << 16);
}

static unsigned
Field_sas_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 15) >> 31);
  tie_t = (tie_t << 4) | ((insn[0] << 16) >> 28);
  return tie_t;
}

static void
Field_sas_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf000) | (tie_t << 12);
  tie_t = (val << 27) >> 31;
  insn[0] = (insn[0] & ~0x10000) | (tie_t << 16);
}

static unsigned
Field_sr_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 24) >> 28);
  tie_t = (tie_t << 4) | ((insn[0] << 28) >> 28);
  return tie_t;
}

static void
Field_sr_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf) | (tie_t << 0);
  tie_t = (val << 24) >> 28;
  insn[0] = (insn[0] & ~0xf0) | (tie_t << 4);
}

static unsigned
Field_sr_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 24) >> 28);
  tie_t = (tie_t << 4) | ((insn[0] << 28) >> 28);
  return tie_t;
}

static void
Field_sr_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf) | (tie_t << 0);
  tie_t = (val << 24) >> 28;
  insn[0] = (insn[0] & ~0xf0) | (tie_t << 4);
}

static unsigned
Field_st_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 16) >> 28);
  tie_t = (tie_t << 4) | ((insn[0] << 12) >> 28);
  return tie_t;
}

static void
Field_st_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf0000) | (tie_t << 16);
  tie_t = (val << 24) >> 28;
  insn[0] = (insn[0] & ~0xf000) | (tie_t << 12);
}

static unsigned
Field_st_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 24) >> 28);
  tie_t = (tie_t << 4) | ((insn[0] << 20) >> 28);
  return tie_t;
}

static void
Field_st_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf00) | (tie_t << 8);
  tie_t = (val << 24) >> 28;
  insn[0] = (insn[0] & ~0xf0) | (tie_t << 4);
}

static unsigned
Field_st_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 24) >> 28);
  tie_t = (tie_t << 4) | ((insn[0] << 20) >> 28);
  return tie_t;
}

static void
Field_st_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf00) | (tie_t << 8);
  tie_t = (val << 24) >> 28;
  insn[0] = (insn[0] & ~0xf0) | (tie_t << 4);
}

static unsigned
Field_imm4_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 20) >> 28);
  return tie_t;
}

static void
Field_imm4_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf00) | (tie_t << 8);
}

static unsigned
Field_imm4_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 28) >> 28);
  return tie_t;
}

static void
Field_imm4_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf) | (tie_t << 0);
}

static unsigned
Field_imm4_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 28) >> 28);
  return tie_t;
}

static void
Field_imm4_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf) | (tie_t << 0);
}

static unsigned
Field_mn_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 2) | ((insn[0] << 12) >> 30);
  tie_t = (tie_t << 2) | ((insn[0] << 14) >> 30);
  return tie_t;
}

static void
Field_mn_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 30) >> 30;
  insn[0] = (insn[0] & ~0x30000) | (tie_t << 16);
  tie_t = (val << 28) >> 30;
  insn[0] = (insn[0] & ~0xc0000) | (tie_t << 18);
}

static unsigned
Field_i_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 20) >> 31);
  return tie_t;
}

static void
Field_i_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 31) >> 31;
  insn[0] = (insn[0] & ~0x800) | (tie_t << 11);
}

static unsigned
Field_imm6lo_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 28) >> 28);
  return tie_t;
}

static void
Field_imm6lo_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf) | (tie_t << 0);
}

static unsigned
Field_imm6lo_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 28) >> 28);
  return tie_t;
}

static void
Field_imm6lo_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf) | (tie_t << 0);
}

static unsigned
Field_imm6hi_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 2) | ((insn[0] << 22) >> 30);
  return tie_t;
}

static void
Field_imm6hi_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 30) >> 30;
  insn[0] = (insn[0] & ~0x300) | (tie_t << 8);
}

static unsigned
Field_imm6hi_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 2) | ((insn[0] << 22) >> 30);
  return tie_t;
}

static void
Field_imm6hi_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 30) >> 30;
  insn[0] = (insn[0] & ~0x300) | (tie_t << 8);
}

static unsigned
Field_imm7lo_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 28) >> 28);
  return tie_t;
}

static void
Field_imm7lo_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf) | (tie_t << 0);
}

static unsigned
Field_imm7lo_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 28) >> 28);
  return tie_t;
}

static void
Field_imm7lo_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf) | (tie_t << 0);
}

static unsigned
Field_imm7hi_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 3) | ((insn[0] << 21) >> 29);
  return tie_t;
}

static void
Field_imm7hi_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 29) >> 29;
  insn[0] = (insn[0] & ~0x700) | (tie_t << 8);
}

static unsigned
Field_imm7hi_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 3) | ((insn[0] << 21) >> 29);
  return tie_t;
}

static void
Field_imm7hi_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 29) >> 29;
  insn[0] = (insn[0] & ~0x700) | (tie_t << 8);
}

static unsigned
Field_z_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 21) >> 31);
  return tie_t;
}

static void
Field_z_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 31) >> 31;
  insn[0] = (insn[0] & ~0x400) | (tie_t << 10);
}

static unsigned
Field_imm6_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 2) | ((insn[0] << 22) >> 30);
  tie_t = (tie_t << 4) | ((insn[0] << 28) >> 28);
  return tie_t;
}

static void
Field_imm6_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf) | (tie_t << 0);
  tie_t = (val << 26) >> 30;
  insn[0] = (insn[0] & ~0x300) | (tie_t << 8);
}

static unsigned
Field_imm6_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 2) | ((insn[0] << 22) >> 30);
  tie_t = (tie_t << 4) | ((insn[0] << 28) >> 28);
  return tie_t;
}

static void
Field_imm6_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf) | (tie_t << 0);
  tie_t = (val << 26) >> 30;
  insn[0] = (insn[0] & ~0x300) | (tie_t << 8);
}

static unsigned
Field_imm7_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 3) | ((insn[0] << 21) >> 29);
  tie_t = (tie_t << 4) | ((insn[0] << 28) >> 28);
  return tie_t;
}

static void
Field_imm7_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf) | (tie_t << 0);
  tie_t = (val << 25) >> 29;
  insn[0] = (insn[0] & ~0x700) | (tie_t << 8);
}

static unsigned
Field_imm7_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 3) | ((insn[0] << 21) >> 29);
  tie_t = (tie_t << 4) | ((insn[0] << 28) >> 28);
  return tie_t;
}

static void
Field_imm7_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf) | (tie_t << 0);
  tie_t = (val << 25) >> 29;
  insn[0] = (insn[0] & ~0x700) | (tie_t << 8);
}

static void
Implicit_Field_set (xtensa_insnbuf insn ATTRIBUTE_UNUSED,
		    uint32 val ATTRIBUTE_UNUSED)
{
}

static unsigned
Implicit_Field_ar0_get (const xtensa_insnbuf insn ATTRIBUTE_UNUSED)
{
  return 0;
}

static unsigned
Implicit_Field_ar4_get (const xtensa_insnbuf insn ATTRIBUTE_UNUSED)
{
  return 4;
}

static unsigned
Implicit_Field_ar8_get (const xtensa_insnbuf insn ATTRIBUTE_UNUSED)
{
  return 8;
}

static unsigned
Implicit_Field_ar12_get (const xtensa_insnbuf insn ATTRIBUTE_UNUSED)
{
  return 12;
}



static xtensa_funcUnit_internal funcUnits[] = {

};



static xtensa_regfile_internal regfiles[] = {
  { "AR", "a", 0, 32, 64 }
};



static xtensa_interface_internal interfaces[] = {

};



static const unsigned CONST_TBL_ai4c_0[] = {
  0xffffffff,
  0x1,
  0x2,
  0x3,
  0x4,
  0x5,
  0x6,
  0x7,
  0x8,
  0x9,
  0xa,
  0xb,
  0xc,
  0xd,
  0xe,
  0xf,
  0
};

static const unsigned CONST_TBL_b4c_0[] = {
  0xffffffff,
  0x1,
  0x2,
  0x3,
  0x4,
  0x5,
  0x6,
  0x7,
  0x8,
  0xa,
  0xc,
  0x10,
  0x20,
  0x40,
  0x80,
  0x100,
  0
};

static const unsigned CONST_TBL_b4cu_0[] = {
  0x8000,
  0x10000,
  0x2,
  0x3,
  0x4,
  0x5,
  0x6,
  0x7,
  0x8,
  0xa,
  0xc,
  0x10,
  0x20,
  0x40,
  0x80,
  0x100,
  0
};



static int
Operand_soffsetx4_decode (uint32 *valp)
{
  unsigned soffsetx4_0, offset_0;
  offset_0 = *valp & 0x3ffff;
  soffsetx4_0 = 0x4 + ((((int) offset_0 << 14) >> 14) << 2);
  *valp = soffsetx4_0;
  return 0;
}

static int
Operand_soffsetx4_encode (uint32 *valp)
{
  unsigned offset_0, soffsetx4_0;
  soffsetx4_0 = *valp;
  offset_0 = ((soffsetx4_0 - 0x4) >> 2) & 0x3ffff;
  *valp = offset_0;
  return 0;
}

static int
Operand_soffsetx4_ator (uint32 *valp, uint32 pc)
{
  *valp -= (pc & ~0x3);
  return 0;
}

static int
Operand_soffsetx4_rtoa (uint32 *valp, uint32 pc)
{
  *valp += (pc & ~0x3);
  return 0;
}

static int
Operand_uimm12x8_decode (uint32 *valp)
{
  unsigned uimm12x8_0, imm12_0;
  imm12_0 = *valp & 0xfff;
  uimm12x8_0 = imm12_0 << 3;
  *valp = uimm12x8_0;
  return 0;
}

static int
Operand_uimm12x8_encode (uint32 *valp)
{
  unsigned imm12_0, uimm12x8_0;
  uimm12x8_0 = *valp;
  imm12_0 = ((uimm12x8_0 >> 3) & 0xfff);
  *valp = imm12_0;
  return 0;
}

static int
Operand_simm4_decode (uint32 *valp)
{
  unsigned simm4_0, mn_0;
  mn_0 = *valp & 0xf;
  simm4_0 = ((int) mn_0 << 28) >> 28;
  *valp = simm4_0;
  return 0;
}

static int
Operand_simm4_encode (uint32 *valp)
{
  unsigned mn_0, simm4_0;
  simm4_0 = *valp;
  mn_0 = (simm4_0 & 0xf);
  *valp = mn_0;
  return 0;
}

static int
Operand_arr_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
Operand_arr_encode (uint32 *valp)
{
  return (*valp & ~0xf) != 0;
}

static int
Operand_ars_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
Operand_ars_encode (uint32 *valp)
{
  return (*valp & ~0xf) != 0;
}

static int
Operand_art_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
Operand_art_encode (uint32 *valp)
{
  return (*valp & ~0xf) != 0;
}

static int
Operand_ar0_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
Operand_ar0_encode (uint32 *valp)
{
  return (*valp & ~0x3f) != 0;
}

static int
Operand_ar4_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
Operand_ar4_encode (uint32 *valp)
{
  return (*valp & ~0x3f) != 0;
}

static int
Operand_ar8_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
Operand_ar8_encode (uint32 *valp)
{
  return (*valp & ~0x3f) != 0;
}

static int
Operand_ar12_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
Operand_ar12_encode (uint32 *valp)
{
  return (*valp & ~0x3f) != 0;
}

static int
Operand_ars_entry_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
Operand_ars_entry_encode (uint32 *valp)
{
  return (*valp & ~0x3f) != 0;
}

static int
Operand_immrx4_decode (uint32 *valp)
{
  unsigned immrx4_0, r_0;
  r_0 = *valp & 0xf;
  immrx4_0 = ((((0xfffffff)) << 4) | r_0) << 2;
  *valp = immrx4_0;
  return 0;
}

static int
Operand_immrx4_encode (uint32 *valp)
{
  unsigned r_0, immrx4_0;
  immrx4_0 = *valp;
  r_0 = ((immrx4_0 >> 2) & 0xf);
  *valp = r_0;
  return 0;
}

static int
Operand_lsi4x4_decode (uint32 *valp)
{
  unsigned lsi4x4_0, r_0;
  r_0 = *valp & 0xf;
  lsi4x4_0 = r_0 << 2;
  *valp = lsi4x4_0;
  return 0;
}

static int
Operand_lsi4x4_encode (uint32 *valp)
{
  unsigned r_0, lsi4x4_0;
  lsi4x4_0 = *valp;
  r_0 = ((lsi4x4_0 >> 2) & 0xf);
  *valp = r_0;
  return 0;
}

static int
Operand_simm7_decode (uint32 *valp)
{
  unsigned simm7_0, imm7_0;
  imm7_0 = *valp & 0x7f;
  simm7_0 = ((((-((((imm7_0 >> 6) & 1)) & (((imm7_0 >> 5) & 1)))) & 0x1ffffff)) << 7) | imm7_0;
  *valp = simm7_0;
  return 0;
}

static int
Operand_simm7_encode (uint32 *valp)
{
  unsigned imm7_0, simm7_0;
  simm7_0 = *valp;
  imm7_0 = (simm7_0 & 0x7f);
  *valp = imm7_0;
  return 0;
}

static int
Operand_uimm6_decode (uint32 *valp)
{
  unsigned uimm6_0, imm6_0;
  imm6_0 = *valp & 0x3f;
  uimm6_0 = 0x4 + ((((0)) << 6) | imm6_0);
  *valp = uimm6_0;
  return 0;
}

static int
Operand_uimm6_encode (uint32 *valp)
{
  unsigned imm6_0, uimm6_0;
  uimm6_0 = *valp;
  imm6_0 = (uimm6_0 - 0x4) & 0x3f;
  *valp = imm6_0;
  return 0;
}

static int
Operand_uimm6_ator (uint32 *valp, uint32 pc)
{
  *valp -= pc;
  return 0;
}

static int
Operand_uimm6_rtoa (uint32 *valp, uint32 pc)
{
  *valp += pc;
  return 0;
}

static int
Operand_ai4const_decode (uint32 *valp)
{
  unsigned ai4const_0, t_0;
  t_0 = *valp & 0xf;
  ai4const_0 = CONST_TBL_ai4c_0[t_0 & 0xf];
  *valp = ai4const_0;
  return 0;
}

static int
Operand_ai4const_encode (uint32 *valp)
{
  unsigned t_0, ai4const_0;
  ai4const_0 = *valp;
  switch (ai4const_0)
    {
    case 0xffffffff: t_0 = 0; break;
    case 0x1: t_0 = 0x1; break;
    case 0x2: t_0 = 0x2; break;
    case 0x3: t_0 = 0x3; break;
    case 0x4: t_0 = 0x4; break;
    case 0x5: t_0 = 0x5; break;
    case 0x6: t_0 = 0x6; break;
    case 0x7: t_0 = 0x7; break;
    case 0x8: t_0 = 0x8; break;
    case 0x9: t_0 = 0x9; break;
    case 0xa: t_0 = 0xa; break;
    case 0xb: t_0 = 0xb; break;
    case 0xc: t_0 = 0xc; break;
    case 0xd: t_0 = 0xd; break;
    case 0xe: t_0 = 0xe; break;
    default: t_0 = 0xf; break;
    }
  *valp = t_0;
  return 0;
}

static int
Operand_b4const_decode (uint32 *valp)
{
  unsigned b4const_0, r_0;
  r_0 = *valp & 0xf;
  b4const_0 = CONST_TBL_b4c_0[r_0 & 0xf];
  *valp = b4const_0;
  return 0;
}

static int
Operand_b4const_encode (uint32 *valp)
{
  unsigned r_0, b4const_0;
  b4const_0 = *valp;
  switch (b4const_0)
    {
    case 0xffffffff: r_0 = 0; break;
    case 0x1: r_0 = 0x1; break;
    case 0x2: r_0 = 0x2; break;
    case 0x3: r_0 = 0x3; break;
    case 0x4: r_0 = 0x4; break;
    case 0x5: r_0 = 0x5; break;
    case 0x6: r_0 = 0x6; break;
    case 0x7: r_0 = 0x7; break;
    case 0x8: r_0 = 0x8; break;
    case 0xa: r_0 = 0x9; break;
    case 0xc: r_0 = 0xa; break;
    case 0x10: r_0 = 0xb; break;
    case 0x20: r_0 = 0xc; break;
    case 0x40: r_0 = 0xd; break;
    case 0x80: r_0 = 0xe; break;
    default: r_0 = 0xf; break;
    }
  *valp = r_0;
  return 0;
}

static int
Operand_b4constu_decode (uint32 *valp)
{
  unsigned b4constu_0, r_0;
  r_0 = *valp & 0xf;
  b4constu_0 = CONST_TBL_b4cu_0[r_0 & 0xf];
  *valp = b4constu_0;
  return 0;
}

static int
Operand_b4constu_encode (uint32 *valp)
{
  unsigned r_0, b4constu_0;
  b4constu_0 = *valp;
  switch (b4constu_0)
    {
    case 0x8000: r_0 = 0; break;
    case 0x10000: r_0 = 0x1; break;
    case 0x2: r_0 = 0x2; break;
    case 0x3: r_0 = 0x3; break;
    case 0x4: r_0 = 0x4; break;
    case 0x5: r_0 = 0x5; break;
    case 0x6: r_0 = 0x6; break;
    case 0x7: r_0 = 0x7; break;
    case 0x8: r_0 = 0x8; break;
    case 0xa: r_0 = 0x9; break;
    case 0xc: r_0 = 0xa; break;
    case 0x10: r_0 = 0xb; break;
    case 0x20: r_0 = 0xc; break;
    case 0x40: r_0 = 0xd; break;
    case 0x80: r_0 = 0xe; break;
    default: r_0 = 0xf; break;
    }
  *valp = r_0;
  return 0;
}

static int
Operand_uimm8_decode (uint32 *valp)
{
  unsigned uimm8_0, imm8_0;
  imm8_0 = *valp & 0xff;
  uimm8_0 = imm8_0;
  *valp = uimm8_0;
  return 0;
}

static int
Operand_uimm8_encode (uint32 *valp)
{
  unsigned imm8_0, uimm8_0;
  uimm8_0 = *valp;
  imm8_0 = (uimm8_0 & 0xff);
  *valp = imm8_0;
  return 0;
}

static int
Operand_uimm8x2_decode (uint32 *valp)
{
  unsigned uimm8x2_0, imm8_0;
  imm8_0 = *valp & 0xff;
  uimm8x2_0 = imm8_0 << 1;
  *valp = uimm8x2_0;
  return 0;
}

static int
Operand_uimm8x2_encode (uint32 *valp)
{
  unsigned imm8_0, uimm8x2_0;
  uimm8x2_0 = *valp;
  imm8_0 = ((uimm8x2_0 >> 1) & 0xff);
  *valp = imm8_0;
  return 0;
}

static int
Operand_uimm8x4_decode (uint32 *valp)
{
  unsigned uimm8x4_0, imm8_0;
  imm8_0 = *valp & 0xff;
  uimm8x4_0 = imm8_0 << 2;
  *valp = uimm8x4_0;
  return 0;
}

static int
Operand_uimm8x4_encode (uint32 *valp)
{
  unsigned imm8_0, uimm8x4_0;
  uimm8x4_0 = *valp;
  imm8_0 = ((uimm8x4_0 >> 2) & 0xff);
  *valp = imm8_0;
  return 0;
}

static int
Operand_uimm4x16_decode (uint32 *valp)
{
  unsigned uimm4x16_0, op2_0;
  op2_0 = *valp & 0xf;
  uimm4x16_0 = op2_0 << 4;
  *valp = uimm4x16_0;
  return 0;
}

static int
Operand_uimm4x16_encode (uint32 *valp)
{
  unsigned op2_0, uimm4x16_0;
  uimm4x16_0 = *valp;
  op2_0 = ((uimm4x16_0 >> 4) & 0xf);
  *valp = op2_0;
  return 0;
}

static int
Operand_simm8_decode (uint32 *valp)
{
  unsigned simm8_0, imm8_0;
  imm8_0 = *valp & 0xff;
  simm8_0 = ((int) imm8_0 << 24) >> 24;
  *valp = simm8_0;
  return 0;
}

static int
Operand_simm8_encode (uint32 *valp)
{
  unsigned imm8_0, simm8_0;
  simm8_0 = *valp;
  imm8_0 = (simm8_0 & 0xff);
  *valp = imm8_0;
  return 0;
}

static int
Operand_simm8x256_decode (uint32 *valp)
{
  unsigned simm8x256_0, imm8_0;
  imm8_0 = *valp & 0xff;
  simm8x256_0 = (((int) imm8_0 << 24) >> 24) << 8;
  *valp = simm8x256_0;
  return 0;
}

static int
Operand_simm8x256_encode (uint32 *valp)
{
  unsigned imm8_0, simm8x256_0;
  simm8x256_0 = *valp;
  imm8_0 = ((simm8x256_0 >> 8) & 0xff);
  *valp = imm8_0;
  return 0;
}

static int
Operand_simm12b_decode (uint32 *valp)
{
  unsigned simm12b_0, imm12b_0;
  imm12b_0 = *valp & 0xfff;
  simm12b_0 = ((int) imm12b_0 << 20) >> 20;
  *valp = simm12b_0;
  return 0;
}

static int
Operand_simm12b_encode (uint32 *valp)
{
  unsigned imm12b_0, simm12b_0;
  simm12b_0 = *valp;
  imm12b_0 = (simm12b_0 & 0xfff);
  *valp = imm12b_0;
  return 0;
}

static int
Operand_msalp32_decode (uint32 *valp)
{
  unsigned msalp32_0, sal_0;
  sal_0 = *valp & 0x1f;
  msalp32_0 = 0x20 - sal_0;
  *valp = msalp32_0;
  return 0;
}

static int
Operand_msalp32_encode (uint32 *valp)
{
  unsigned sal_0, msalp32_0;
  msalp32_0 = *valp;
  sal_0 = (0x20 - msalp32_0) & 0x1f;
  *valp = sal_0;
  return 0;
}

static int
Operand_op2p1_decode (uint32 *valp)
{
  unsigned op2p1_0, op2_0;
  op2_0 = *valp & 0xf;
  op2p1_0 = op2_0 + 0x1;
  *valp = op2p1_0;
  return 0;
}

static int
Operand_op2p1_encode (uint32 *valp)
{
  unsigned op2_0, op2p1_0;
  op2p1_0 = *valp;
  op2_0 = (op2p1_0 - 0x1) & 0xf;
  *valp = op2_0;
  return 0;
}

static int
Operand_label8_decode (uint32 *valp)
{
  unsigned label8_0, imm8_0;
  imm8_0 = *valp & 0xff;
  label8_0 = 0x4 + (((int) imm8_0 << 24) >> 24);
  *valp = label8_0;
  return 0;
}

static int
Operand_label8_encode (uint32 *valp)
{
  unsigned imm8_0, label8_0;
  label8_0 = *valp;
  imm8_0 = (label8_0 - 0x4) & 0xff;
  *valp = imm8_0;
  return 0;
}

static int
Operand_label8_ator (uint32 *valp, uint32 pc)
{
  *valp -= pc;
  return 0;
}

static int
Operand_label8_rtoa (uint32 *valp, uint32 pc)
{
  *valp += pc;
  return 0;
}

static int
Operand_ulabel8_decode (uint32 *valp)
{
  unsigned ulabel8_0, imm8_0;
  imm8_0 = *valp & 0xff;
  ulabel8_0 = 0x4 + ((((0)) << 8) | imm8_0);
  *valp = ulabel8_0;
  return 0;
}

static int
Operand_ulabel8_encode (uint32 *valp)
{
  unsigned imm8_0, ulabel8_0;
  ulabel8_0 = *valp;
  imm8_0 = (ulabel8_0 - 0x4) & 0xff;
  *valp = imm8_0;
  return 0;
}

static int
Operand_ulabel8_ator (uint32 *valp, uint32 pc)
{
  *valp -= pc;
  return 0;
}

static int
Operand_ulabel8_rtoa (uint32 *valp, uint32 pc)
{
  *valp += pc;
  return 0;
}

static int
Operand_label12_decode (uint32 *valp)
{
  unsigned label12_0, imm12_0;
  imm12_0 = *valp & 0xfff;
  label12_0 = 0x4 + (((int) imm12_0 << 20) >> 20);
  *valp = label12_0;
  return 0;
}

static int
Operand_label12_encode (uint32 *valp)
{
  unsigned imm12_0, label12_0;
  label12_0 = *valp;
  imm12_0 = (label12_0 - 0x4) & 0xfff;
  *valp = imm12_0;
  return 0;
}

static int
Operand_label12_ator (uint32 *valp, uint32 pc)
{
  *valp -= pc;
  return 0;
}

static int
Operand_label12_rtoa (uint32 *valp, uint32 pc)
{
  *valp += pc;
  return 0;
}

static int
Operand_soffset_decode (uint32 *valp)
{
  unsigned soffset_0, offset_0;
  offset_0 = *valp & 0x3ffff;
  soffset_0 = 0x4 + (((int) offset_0 << 14) >> 14);
  *valp = soffset_0;
  return 0;
}

static int
Operand_soffset_encode (uint32 *valp)
{
  unsigned offset_0, soffset_0;
  soffset_0 = *valp;
  offset_0 = (soffset_0 - 0x4) & 0x3ffff;
  *valp = offset_0;
  return 0;
}

static int
Operand_soffset_ator (uint32 *valp, uint32 pc)
{
  *valp -= pc;
  return 0;
}

static int
Operand_soffset_rtoa (uint32 *valp, uint32 pc)
{
  *valp += pc;
  return 0;
}

static int
Operand_uimm16x4_decode (uint32 *valp)
{
  unsigned uimm16x4_0, imm16_0;
  imm16_0 = *valp & 0xffff;
  uimm16x4_0 = ((((0xffff)) << 16) | imm16_0) << 2;
  *valp = uimm16x4_0;
  return 0;
}

static int
Operand_uimm16x4_encode (uint32 *valp)
{
  unsigned imm16_0, uimm16x4_0;
  uimm16x4_0 = *valp;
  imm16_0 = (uimm16x4_0 >> 2) & 0xffff;
  *valp = imm16_0;
  return 0;
}

static int
Operand_uimm16x4_ator (uint32 *valp, uint32 pc)
{
  *valp -= ((pc + 3) & ~0x3);
  return 0;
}

static int
Operand_uimm16x4_rtoa (uint32 *valp, uint32 pc)
{
  *valp += ((pc + 3) & ~0x3);
  return 0;
}

static int
Operand_immt_decode (uint32 *valp)
{
  unsigned immt_0, t_0;
  t_0 = *valp & 0xf;
  immt_0 = t_0;
  *valp = immt_0;
  return 0;
}

static int
Operand_immt_encode (uint32 *valp)
{
  unsigned t_0, immt_0;
  immt_0 = *valp;
  t_0 = immt_0 & 0xf;
  *valp = t_0;
  return 0;
}

static int
Operand_imms_decode (uint32 *valp)
{
  unsigned imms_0, s_0;
  s_0 = *valp & 0xf;
  imms_0 = s_0;
  *valp = imms_0;
  return 0;
}

static int
Operand_imms_encode (uint32 *valp)
{
  unsigned s_0, imms_0;
  imms_0 = *valp;
  s_0 = imms_0 & 0xf;
  *valp = s_0;
  return 0;
}

static xtensa_operand_internal operands[] = {
  { "soffsetx4", 10, -1, 0,
    XTENSA_OPERAND_IS_PCRELATIVE,
    Operand_soffsetx4_encode, Operand_soffsetx4_decode,
    Operand_soffsetx4_ator, Operand_soffsetx4_rtoa },
  { "uimm12x8", 3, -1, 0,
    0,
    Operand_uimm12x8_encode, Operand_uimm12x8_decode,
    0, 0 },
  { "simm4", 26, -1, 0,
    0,
    Operand_simm4_encode, Operand_simm4_decode,
    0, 0 },
  { "arr", 14, 0, 1,
    XTENSA_OPERAND_IS_REGISTER,
    Operand_arr_encode, Operand_arr_decode,
    0, 0 },
  { "ars", 5, 0, 1,
    XTENSA_OPERAND_IS_REGISTER,
    Operand_ars_encode, Operand_ars_decode,
    0, 0 },
  { "*ars_invisible", 5, 0, 1,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_INVISIBLE,
    Operand_ars_encode, Operand_ars_decode,
    0, 0 },
  { "art", 0, 0, 1,
    XTENSA_OPERAND_IS_REGISTER,
    Operand_art_encode, Operand_art_decode,
    0, 0 },
  { "ar0", 35, 0, 1,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_INVISIBLE,
    Operand_ar0_encode, Operand_ar0_decode,
    0, 0 },
  { "ar4", 36, 0, 1,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_INVISIBLE,
    Operand_ar4_encode, Operand_ar4_decode,
    0, 0 },
  { "ar8", 37, 0, 1,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_INVISIBLE,
    Operand_ar8_encode, Operand_ar8_decode,
    0, 0 },
  { "ar12", 38, 0, 1,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_INVISIBLE,
    Operand_ar12_encode, Operand_ar12_decode,
    0, 0 },
  { "ars_entry", 5, 0, 1,
    XTENSA_OPERAND_IS_REGISTER,
    Operand_ars_entry_encode, Operand_ars_entry_decode,
    0, 0 },
  { "immrx4", 14, -1, 0,
    0,
    Operand_immrx4_encode, Operand_immrx4_decode,
    0, 0 },
  { "lsi4x4", 14, -1, 0,
    0,
    Operand_lsi4x4_encode, Operand_lsi4x4_decode,
    0, 0 },
  { "simm7", 34, -1, 0,
    0,
    Operand_simm7_encode, Operand_simm7_decode,
    0, 0 },
  { "uimm6", 33, -1, 0,
    XTENSA_OPERAND_IS_PCRELATIVE,
    Operand_uimm6_encode, Operand_uimm6_decode,
    Operand_uimm6_ator, Operand_uimm6_rtoa },
  { "ai4const", 0, -1, 0,
    0,
    Operand_ai4const_encode, Operand_ai4const_decode,
    0, 0 },
  { "b4const", 14, -1, 0,
    0,
    Operand_b4const_encode, Operand_b4const_decode,
    0, 0 },
  { "b4constu", 14, -1, 0,
    0,
    Operand_b4constu_encode, Operand_b4constu_decode,
    0, 0 },
  { "uimm8", 4, -1, 0,
    0,
    Operand_uimm8_encode, Operand_uimm8_decode,
    0, 0 },
  { "uimm8x2", 4, -1, 0,
    0,
    Operand_uimm8x2_encode, Operand_uimm8x2_decode,
    0, 0 },
  { "uimm8x4", 4, -1, 0,
    0,
    Operand_uimm8x4_encode, Operand_uimm8x4_decode,
    0, 0 },
  { "uimm4x16", 13, -1, 0,
    0,
    Operand_uimm4x16_encode, Operand_uimm4x16_decode,
    0, 0 },
  { "simm8", 4, -1, 0,
    0,
    Operand_simm8_encode, Operand_simm8_decode,
    0, 0 },
  { "simm8x256", 4, -1, 0,
    0,
    Operand_simm8x256_encode, Operand_simm8x256_decode,
    0, 0 },
  { "simm12b", 6, -1, 0,
    0,
    Operand_simm12b_encode, Operand_simm12b_decode,
    0, 0 },
  { "msalp32", 18, -1, 0,
    0,
    Operand_msalp32_encode, Operand_msalp32_decode,
    0, 0 },
  { "op2p1", 13, -1, 0,
    0,
    Operand_op2p1_encode, Operand_op2p1_decode,
    0, 0 },
  { "label8", 4, -1, 0,
    XTENSA_OPERAND_IS_PCRELATIVE,
    Operand_label8_encode, Operand_label8_decode,
    Operand_label8_ator, Operand_label8_rtoa },
  { "ulabel8", 4, -1, 0,
    XTENSA_OPERAND_IS_PCRELATIVE,
    Operand_ulabel8_encode, Operand_ulabel8_decode,
    Operand_ulabel8_ator, Operand_ulabel8_rtoa },
  { "label12", 3, -1, 0,
    XTENSA_OPERAND_IS_PCRELATIVE,
    Operand_label12_encode, Operand_label12_decode,
    Operand_label12_ator, Operand_label12_rtoa },
  { "soffset", 10, -1, 0,
    XTENSA_OPERAND_IS_PCRELATIVE,
    Operand_soffset_encode, Operand_soffset_decode,
    Operand_soffset_ator, Operand_soffset_rtoa },
  { "uimm16x4", 7, -1, 0,
    XTENSA_OPERAND_IS_PCRELATIVE,
    Operand_uimm16x4_encode, Operand_uimm16x4_decode,
    Operand_uimm16x4_ator, Operand_uimm16x4_rtoa },
  { "immt", 0, -1, 0,
    0,
    Operand_immt_encode, Operand_immt_decode,
    0, 0 },
  { "imms", 5, -1, 0,
    0,
    Operand_imms_encode, Operand_imms_decode,
    0, 0 },
  { "t", 0, -1, 0, 0, 0, 0, 0, 0 },
  { "bbi4", 1, -1, 0, 0, 0, 0, 0, 0 },
  { "bbi", 2, -1, 0, 0, 0, 0, 0, 0 },
  { "imm12", 3, -1, 0, 0, 0, 0, 0, 0 },
  { "imm8", 4, -1, 0, 0, 0, 0, 0, 0 },
  { "s", 5, -1, 0, 0, 0, 0, 0, 0 },
  { "imm12b", 6, -1, 0, 0, 0, 0, 0, 0 },
  { "imm16", 7, -1, 0, 0, 0, 0, 0, 0 },
  { "m", 8, -1, 0, 0, 0, 0, 0, 0 },
  { "n", 9, -1, 0, 0, 0, 0, 0, 0 },
  { "offset", 10, -1, 0, 0, 0, 0, 0, 0 },
  { "op0", 11, -1, 0, 0, 0, 0, 0, 0 },
  { "op1", 12, -1, 0, 0, 0, 0, 0, 0 },
  { "op2", 13, -1, 0, 0, 0, 0, 0, 0 },
  { "r", 14, -1, 0, 0, 0, 0, 0, 0 },
  { "sa4", 15, -1, 0, 0, 0, 0, 0, 0 },
  { "sae4", 16, -1, 0, 0, 0, 0, 0, 0 },
  { "sae", 17, -1, 0, 0, 0, 0, 0, 0 },
  { "sal", 18, -1, 0, 0, 0, 0, 0, 0 },
  { "sargt", 19, -1, 0, 0, 0, 0, 0, 0 },
  { "sas4", 20, -1, 0, 0, 0, 0, 0, 0 },
  { "sas", 21, -1, 0, 0, 0, 0, 0, 0 },
  { "sr", 22, -1, 0, 0, 0, 0, 0, 0 },
  { "st", 23, -1, 0, 0, 0, 0, 0, 0 },
  { "thi3", 24, -1, 0, 0, 0, 0, 0, 0 },
  { "imm4", 25, -1, 0, 0, 0, 0, 0, 0 },
  { "mn", 26, -1, 0, 0, 0, 0, 0, 0 },
  { "i", 27, -1, 0, 0, 0, 0, 0, 0 },
  { "imm6lo", 28, -1, 0, 0, 0, 0, 0, 0 },
  { "imm6hi", 29, -1, 0, 0, 0, 0, 0, 0 },
  { "imm7lo", 30, -1, 0, 0, 0, 0, 0, 0 },
  { "imm7hi", 31, -1, 0, 0, 0, 0, 0, 0 },
  { "z", 32, -1, 0, 0, 0, 0, 0, 0 },
  { "imm6", 33, -1, 0, 0, 0, 0, 0, 0 },
  { "imm7", 34, -1, 0, 0, 0, 0, 0, 0 }
};



static xtensa_arg_internal Iclass_xt_iclass_rfe_stateArgs[] = {
  { { STATE_PSRING }, 'i' },
  { { STATE_PSEXCM }, 'm' },
  { { STATE_EPC1 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rfde_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DEPC }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_call12_args[] = {
  { { 0 }, 'i' },
  { { 10 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_call12_stateArgs[] = {
  { { STATE_PSCALLINC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_call8_args[] = {
  { { 0 }, 'i' },
  { { 9 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_call8_stateArgs[] = {
  { { STATE_PSCALLINC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_call4_args[] = {
  { { 0 }, 'i' },
  { { 8 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_call4_stateArgs[] = {
  { { STATE_PSCALLINC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_callx12_args[] = {
  { { 4 }, 'i' },
  { { 10 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_callx12_stateArgs[] = {
  { { STATE_PSCALLINC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_callx8_args[] = {
  { { 4 }, 'i' },
  { { 9 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_callx8_stateArgs[] = {
  { { STATE_PSCALLINC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_callx4_args[] = {
  { { 4 }, 'i' },
  { { 8 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_callx4_stateArgs[] = {
  { { STATE_PSCALLINC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_entry_args[] = {
  { { 11 }, 's' },
  { { 4 }, 'i' },
  { { 1 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_entry_stateArgs[] = {
  { { STATE_PSCALLINC }, 'i' },
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSWOE }, 'i' },
  { { STATE_WindowBase }, 'm' },
  { { STATE_WindowStart }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_movsp_args[] = {
  { { 6 }, 'o' },
  { { 4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_movsp_stateArgs[] = {
  { { STATE_WindowBase }, 'i' },
  { { STATE_WindowStart }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rotw_args[] = {
  { { 2 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rotw_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_WindowBase }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_retw_args[] = {
  { { 5 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_retw_stateArgs[] = {
  { { STATE_WindowBase }, 'm' },
  { { STATE_WindowStart }, 'm' },
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSWOE }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rfwou_stateArgs[] = {
  { { STATE_EPC1 }, 'i' },
  { { STATE_PSEXCM }, 'm' },
  { { STATE_PSRING }, 'i' },
  { { STATE_WindowBase }, 'm' },
  { { STATE_WindowStart }, 'm' },
  { { STATE_PSOWB }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_l32e_args[] = {
  { { 6 }, 'o' },
  { { 4 }, 'i' },
  { { 12 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_l32e_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_s32e_args[] = {
  { { 6 }, 'i' },
  { { 4 }, 'i' },
  { { 12 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_s32e_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_windowbase_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_windowbase_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_WindowBase }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_windowbase_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_windowbase_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_WindowBase }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_windowbase_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_windowbase_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_WindowBase }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_windowstart_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_windowstart_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_WindowStart }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_windowstart_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_windowstart_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_WindowStart }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_windowstart_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_windowstart_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_WindowStart }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_add_n_args[] = {
  { { 3 }, 'o' },
  { { 4 }, 'i' },
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_addi_n_args[] = {
  { { 3 }, 'o' },
  { { 4 }, 'i' },
  { { 16 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_bz6_args[] = {
  { { 4 }, 'i' },
  { { 15 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_loadi4_args[] = {
  { { 6 }, 'o' },
  { { 4 }, 'i' },
  { { 13 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_mov_n_args[] = {
  { { 6 }, 'o' },
  { { 4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_movi_n_args[] = {
  { { 4 }, 'o' },
  { { 14 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_retn_args[] = {
  { { 5 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_storei4_args[] = {
  { { 6 }, 'i' },
  { { 4 }, 'i' },
  { { 13 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_addi_args[] = {
  { { 6 }, 'o' },
  { { 4 }, 'i' },
  { { 23 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_addmi_args[] = {
  { { 6 }, 'o' },
  { { 4 }, 'i' },
  { { 24 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_addsub_args[] = {
  { { 3 }, 'o' },
  { { 4 }, 'i' },
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_bit_args[] = {
  { { 3 }, 'o' },
  { { 4 }, 'i' },
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_bsi8_args[] = {
  { { 4 }, 'i' },
  { { 17 }, 'i' },
  { { 28 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_bsi8b_args[] = {
  { { 4 }, 'i' },
  { { 37 }, 'i' },
  { { 28 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_bsi8u_args[] = {
  { { 4 }, 'i' },
  { { 18 }, 'i' },
  { { 28 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_bst8_args[] = {
  { { 4 }, 'i' },
  { { 6 }, 'i' },
  { { 28 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_bsz12_args[] = {
  { { 4 }, 'i' },
  { { 30 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_call0_args[] = {
  { { 0 }, 'i' },
  { { 7 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_callx0_args[] = {
  { { 4 }, 'i' },
  { { 7 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_exti_args[] = {
  { { 3 }, 'o' },
  { { 6 }, 'i' },
  { { 52 }, 'i' },
  { { 27 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_jump_args[] = {
  { { 31 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_jumpx_args[] = {
  { { 4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_l16ui_args[] = {
  { { 6 }, 'o' },
  { { 4 }, 'i' },
  { { 20 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_l16si_args[] = {
  { { 6 }, 'o' },
  { { 4 }, 'i' },
  { { 20 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_l32i_args[] = {
  { { 6 }, 'o' },
  { { 4 }, 'i' },
  { { 21 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_l32r_args[] = {
  { { 6 }, 'o' },
  { { 32 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_l32r_stateArgs[] = {
  { { STATE_LITBADDR }, 'i' },
  { { STATE_LITBEN }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_l8i_args[] = {
  { { 6 }, 'o' },
  { { 4 }, 'i' },
  { { 19 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_loop_args[] = {
  { { 4 }, 'i' },
  { { 29 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_loop_stateArgs[] = {
  { { STATE_LBEG }, 'o' },
  { { STATE_LEND }, 'o' },
  { { STATE_LCOUNT }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_loopz_args[] = {
  { { 4 }, 'i' },
  { { 29 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_loopz_stateArgs[] = {
  { { STATE_LBEG }, 'o' },
  { { STATE_LEND }, 'o' },
  { { STATE_LCOUNT }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_movi_args[] = {
  { { 6 }, 'o' },
  { { 25 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_movz_args[] = {
  { { 3 }, 'm' },
  { { 4 }, 'i' },
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_neg_args[] = {
  { { 3 }, 'o' },
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_return_args[] = {
  { { 5 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_s16i_args[] = {
  { { 6 }, 'i' },
  { { 4 }, 'i' },
  { { 20 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_s32i_args[] = {
  { { 6 }, 'i' },
  { { 4 }, 'i' },
  { { 21 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_s8i_args[] = {
  { { 6 }, 'i' },
  { { 4 }, 'i' },
  { { 19 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_sar_args[] = {
  { { 4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_sar_stateArgs[] = {
  { { STATE_SAR }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_sari_args[] = {
  { { 56 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_sari_stateArgs[] = {
  { { STATE_SAR }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_shifts_args[] = {
  { { 3 }, 'o' },
  { { 4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_shifts_stateArgs[] = {
  { { STATE_SAR }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_shiftst_args[] = {
  { { 3 }, 'o' },
  { { 4 }, 'i' },
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_shiftst_stateArgs[] = {
  { { STATE_SAR }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_shiftt_args[] = {
  { { 3 }, 'o' },
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_shiftt_stateArgs[] = {
  { { STATE_SAR }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_slli_args[] = {
  { { 3 }, 'o' },
  { { 4 }, 'i' },
  { { 26 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_srai_args[] = {
  { { 3 }, 'o' },
  { { 6 }, 'i' },
  { { 54 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_srli_args[] = {
  { { 3 }, 'o' },
  { { 6 }, 'i' },
  { { 40 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_sync_stateArgs[] = {
  { { STATE_XTSYNC }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsil_args[] = {
  { { 6 }, 'o' },
  { { 40 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsil_stateArgs[] = {
  { { STATE_PSWOE }, 'i' },
  { { STATE_PSCALLINC }, 'i' },
  { { STATE_PSOWB }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_PSUM }, 'i' },
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSINTLEVEL }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_lend_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_lend_stateArgs[] = {
  { { STATE_LEND }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_lend_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_lend_stateArgs[] = {
  { { STATE_LEND }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_lend_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_lend_stateArgs[] = {
  { { STATE_LEND }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_lcount_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_lcount_stateArgs[] = {
  { { STATE_LCOUNT }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_lcount_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_lcount_stateArgs[] = {
  { { STATE_XTSYNC }, 'o' },
  { { STATE_LCOUNT }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_lcount_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_lcount_stateArgs[] = {
  { { STATE_XTSYNC }, 'o' },
  { { STATE_LCOUNT }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_lbeg_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_lbeg_stateArgs[] = {
  { { STATE_LBEG }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_lbeg_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_lbeg_stateArgs[] = {
  { { STATE_LBEG }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_lbeg_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_lbeg_stateArgs[] = {
  { { STATE_LBEG }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_sar_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_sar_stateArgs[] = {
  { { STATE_SAR }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_sar_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_sar_stateArgs[] = {
  { { STATE_SAR }, 'o' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_sar_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_sar_stateArgs[] = {
  { { STATE_SAR }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_litbase_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_litbase_stateArgs[] = {
  { { STATE_LITBADDR }, 'i' },
  { { STATE_LITBEN }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_litbase_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_litbase_stateArgs[] = {
  { { STATE_LITBADDR }, 'o' },
  { { STATE_LITBEN }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_litbase_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_litbase_stateArgs[] = {
  { { STATE_LITBADDR }, 'm' },
  { { STATE_LITBEN }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_176_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_176_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_208_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_208_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ps_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ps_stateArgs[] = {
  { { STATE_PSWOE }, 'i' },
  { { STATE_PSCALLINC }, 'i' },
  { { STATE_PSOWB }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_PSUM }, 'i' },
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSINTLEVEL }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ps_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ps_stateArgs[] = {
  { { STATE_PSWOE }, 'o' },
  { { STATE_PSCALLINC }, 'o' },
  { { STATE_PSOWB }, 'o' },
  { { STATE_PSRING }, 'm' },
  { { STATE_PSUM }, 'o' },
  { { STATE_PSEXCM }, 'm' },
  { { STATE_PSINTLEVEL }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ps_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ps_stateArgs[] = {
  { { STATE_PSWOE }, 'm' },
  { { STATE_PSCALLINC }, 'm' },
  { { STATE_PSOWB }, 'm' },
  { { STATE_PSRING }, 'm' },
  { { STATE_PSUM }, 'm' },
  { { STATE_PSEXCM }, 'm' },
  { { STATE_PSINTLEVEL }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_epc1_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_epc1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC1 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_epc1_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_epc1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC1 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_epc1_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_epc1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC1 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_excsave1_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_excsave1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE1 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_excsave1_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_excsave1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE1 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_excsave1_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_excsave1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE1 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_epc2_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_epc2_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC2 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_epc2_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_epc2_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC2 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_epc2_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_epc2_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC2 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_excsave2_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_excsave2_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE2 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_excsave2_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_excsave2_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE2 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_excsave2_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_excsave2_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE2 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_epc3_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_epc3_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC3 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_epc3_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_epc3_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC3 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_epc3_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_epc3_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC3 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_excsave3_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_excsave3_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE3 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_excsave3_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_excsave3_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE3 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_excsave3_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_excsave3_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE3 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_epc4_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_epc4_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_epc4_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_epc4_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC4 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_epc4_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_epc4_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC4 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_excsave4_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_excsave4_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_excsave4_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_excsave4_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE4 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_excsave4_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_excsave4_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE4 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_eps2_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_eps2_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPS2 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_eps2_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_eps2_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPS2 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_eps2_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_eps2_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPS2 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_eps3_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_eps3_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPS3 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_eps3_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_eps3_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPS3 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_eps3_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_eps3_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPS3 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_eps4_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_eps4_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPS4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_eps4_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_eps4_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPS4 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_eps4_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_eps4_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPS4 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_excvaddr_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_excvaddr_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCVADDR }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_excvaddr_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_excvaddr_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCVADDR }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_excvaddr_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_excvaddr_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCVADDR }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_depc_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_depc_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DEPC }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_depc_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_depc_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DEPC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_depc_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_depc_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DEPC }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_exccause_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_exccause_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCCAUSE }, 'i' },
  { { STATE_XTSYNC }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_exccause_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_exccause_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCCAUSE }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_exccause_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_exccause_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCCAUSE }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_misc0_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_misc0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_MISC0 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_misc0_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_misc0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_MISC0 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_misc0_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_misc0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_MISC0 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_misc1_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_misc1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_MISC1 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_misc1_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_misc1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_MISC1 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_misc1_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_misc1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_MISC1 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_prid_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_prid_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rfi_args[] = {
  { { 40 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rfi_stateArgs[] = {
  { { STATE_PSWOE }, 'o' },
  { { STATE_PSCALLINC }, 'o' },
  { { STATE_PSOWB }, 'o' },
  { { STATE_PSRING }, 'm' },
  { { STATE_PSUM }, 'o' },
  { { STATE_PSEXCM }, 'm' },
  { { STATE_PSINTLEVEL }, 'o' },
  { { STATE_EPC1 }, 'i' },
  { { STATE_EPC2 }, 'i' },
  { { STATE_EPC3 }, 'i' },
  { { STATE_EPC4 }, 'i' },
  { { STATE_EPS2 }, 'i' },
  { { STATE_EPS3 }, 'i' },
  { { STATE_EPS4 }, 'i' },
  { { STATE_InOCDMode }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_wait_args[] = {
  { { 40 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wait_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_PSINTLEVEL }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_interrupt_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_interrupt_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_INTERRUPT }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_intset_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_intset_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_XTSYNC }, 'o' },
  { { STATE_INTERRUPT }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_intclear_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_intclear_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_XTSYNC }, 'o' },
  { { STATE_INTERRUPT }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_intenable_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_intenable_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_INTENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_intenable_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_intenable_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_INTENABLE }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_intenable_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_intenable_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_INTENABLE }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_break_args[] = {
  { { 34 }, 'i' },
  { { 33 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_break_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSINTLEVEL }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_break_n_args[] = {
  { { 34 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_break_n_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSINTLEVEL }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_dbreaka0_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_dbreaka0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DBREAKA0 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_dbreaka0_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_dbreaka0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DBREAKA0 }, 'o' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_dbreaka0_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_dbreaka0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DBREAKA0 }, 'm' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_dbreakc0_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_dbreakc0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DBREAKC0 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_dbreakc0_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_dbreakc0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DBREAKC0 }, 'o' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_dbreakc0_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_dbreakc0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DBREAKC0 }, 'm' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_dbreaka1_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_dbreaka1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DBREAKA1 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_dbreaka1_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_dbreaka1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DBREAKA1 }, 'o' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_dbreaka1_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_dbreaka1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DBREAKA1 }, 'm' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_dbreakc1_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_dbreakc1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DBREAKC1 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_dbreakc1_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_dbreakc1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DBREAKC1 }, 'o' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_dbreakc1_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_dbreakc1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DBREAKC1 }, 'm' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ibreaka0_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ibreaka0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_IBREAKA0 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ibreaka0_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ibreaka0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_IBREAKA0 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ibreaka0_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ibreaka0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_IBREAKA0 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ibreaka1_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ibreaka1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_IBREAKA1 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ibreaka1_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ibreaka1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_IBREAKA1 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ibreaka1_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ibreaka1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_IBREAKA1 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ibreakenable_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ibreakenable_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_IBREAKENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ibreakenable_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ibreakenable_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_IBREAKENABLE }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ibreakenable_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ibreakenable_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_IBREAKENABLE }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_debugcause_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_debugcause_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DEBUGCAUSE }, 'i' },
  { { STATE_DBNUM }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_debugcause_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_debugcause_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DEBUGCAUSE }, 'o' },
  { { STATE_DBNUM }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_debugcause_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_debugcause_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DEBUGCAUSE }, 'm' },
  { { STATE_DBNUM }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_icount_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_icount_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_ICOUNT }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_icount_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_icount_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_XTSYNC }, 'o' },
  { { STATE_ICOUNT }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_icount_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_icount_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_XTSYNC }, 'o' },
  { { STATE_ICOUNT }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_icountlevel_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_icountlevel_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_ICOUNTLEVEL }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_icountlevel_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_icountlevel_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_ICOUNTLEVEL }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_icountlevel_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_icountlevel_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_ICOUNTLEVEL }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ddr_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ddr_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DDR }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ddr_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ddr_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_XTSYNC }, 'o' },
  { { STATE_DDR }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ddr_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ddr_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_XTSYNC }, 'o' },
  { { STATE_DDR }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rfdo_stateArgs[] = {
  { { STATE_InOCDMode }, 'm' },
  { { STATE_EPC4 }, 'i' },
  { { STATE_PSWOE }, 'o' },
  { { STATE_PSCALLINC }, 'o' },
  { { STATE_PSOWB }, 'o' },
  { { STATE_PSRING }, 'o' },
  { { STATE_PSUM }, 'o' },
  { { STATE_PSEXCM }, 'o' },
  { { STATE_PSINTLEVEL }, 'o' },
  { { STATE_EPS4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rfdd_stateArgs[] = {
  { { STATE_InOCDMode }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ccount_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ccount_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_CCOUNT }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ccount_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ccount_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_XTSYNC }, 'o' },
  { { STATE_CCOUNT }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ccount_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ccount_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_XTSYNC }, 'o' },
  { { STATE_CCOUNT }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ccompare0_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ccompare0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_CCOMPARE0 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ccompare0_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ccompare0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_CCOMPARE0 }, 'o' },
  { { STATE_INTERRUPT }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ccompare0_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ccompare0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_CCOMPARE0 }, 'm' },
  { { STATE_INTERRUPT }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ccompare1_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ccompare1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_CCOMPARE1 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ccompare1_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ccompare1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_CCOMPARE1 }, 'o' },
  { { STATE_INTERRUPT }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ccompare1_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ccompare1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_CCOMPARE1 }, 'm' },
  { { STATE_INTERRUPT }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ccompare2_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ccompare2_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_CCOMPARE2 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ccompare2_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ccompare2_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_CCOMPARE2 }, 'o' },
  { { STATE_INTERRUPT }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ccompare2_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ccompare2_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_CCOMPARE2 }, 'm' },
  { { STATE_INTERRUPT }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_icache_args[] = {
  { { 4 }, 'i' },
  { { 21 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_icache_inv_args[] = {
  { { 4 }, 'i' },
  { { 21 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_icache_inv_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_licx_args[] = {
  { { 6 }, 'o' },
  { { 4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_licx_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_sicx_args[] = {
  { { 6 }, 'i' },
  { { 4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_sicx_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_dcache_args[] = {
  { { 4 }, 'i' },
  { { 21 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_dcache_ind_args[] = {
  { { 4 }, 'i' },
  { { 22 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_dcache_ind_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_dcache_inv_args[] = {
  { { 4 }, 'i' },
  { { 21 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_dcache_inv_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_dpf_args[] = {
  { { 4 }, 'i' },
  { { 21 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_sdct_args[] = {
  { { 6 }, 'i' },
  { { 4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_sdct_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_ldct_args[] = {
  { { 6 }, 'o' },
  { { 4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_ldct_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ptevaddr_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ptevaddr_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_PTBASE }, 'o' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ptevaddr_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ptevaddr_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_PTBASE }, 'i' },
  { { STATE_EXCVADDR }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ptevaddr_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ptevaddr_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_PTBASE }, 'm' },
  { { STATE_EXCVADDR }, 'i' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_rasid_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_rasid_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_ASID3 }, 'i' },
  { { STATE_ASID2 }, 'i' },
  { { STATE_ASID1 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_rasid_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_rasid_stateArgs[] = {
  { { STATE_XTSYNC }, 'o' },
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_ASID3 }, 'o' },
  { { STATE_ASID2 }, 'o' },
  { { STATE_ASID1 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_rasid_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_rasid_stateArgs[] = {
  { { STATE_XTSYNC }, 'o' },
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_ASID3 }, 'm' },
  { { STATE_ASID2 }, 'm' },
  { { STATE_ASID1 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_itlbcfg_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_itlbcfg_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_INSTPGSZID4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_itlbcfg_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_itlbcfg_stateArgs[] = {
  { { STATE_XTSYNC }, 'o' },
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_INSTPGSZID4 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_itlbcfg_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_itlbcfg_stateArgs[] = {
  { { STATE_XTSYNC }, 'o' },
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_INSTPGSZID4 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_dtlbcfg_args[] = {
  { { 6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_dtlbcfg_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DATAPGSZID4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_dtlbcfg_args[] = {
  { { 6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_dtlbcfg_stateArgs[] = {
  { { STATE_XTSYNC }, 'o' },
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DATAPGSZID4 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_dtlbcfg_args[] = {
  { { 6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_dtlbcfg_stateArgs[] = {
  { { STATE_XTSYNC }, 'o' },
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DATAPGSZID4 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_idtlb_args[] = {
  { { 4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_idtlb_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rdtlb_args[] = {
  { { 6 }, 'o' },
  { { 4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rdtlb_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wdtlb_args[] = {
  { { 6 }, 'i' },
  { { 4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wdtlb_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_iitlb_args[] = {
  { { 4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_iitlb_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_ritlb_args[] = {
  { { 6 }, 'o' },
  { { 4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_ritlb_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_witlb_args[] = {
  { { 6 }, 'i' },
  { { 4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_witlb_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_ldpte_stateArgs[] = {
  { { STATE_PTBASE }, 'i' },
  { { STATE_EXCVADDR }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_hwwitlba_stateArgs[] = {
  { { STATE_EXCVADDR }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_hwwdtlba_stateArgs[] = {
  { { STATE_EXCVADDR }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_nsa_args[] = {
  { { 6 }, 'o' },
  { { 4 }, 'i' }
};

static xtensa_iclass_internal iclasses[] = {
  { 0, 0 ,
    0, 0, 0, 0 },
  { 0, 0 ,
    3, Iclass_xt_iclass_rfe_stateArgs, 0, 0 },
  { 0, 0 ,
    3, Iclass_xt_iclass_rfde_stateArgs, 0, 0 },
  { 0, 0 ,
    0, 0, 0, 0 },
  { 0, 0 ,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_call12_args,
    1, Iclass_xt_iclass_call12_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_call8_args,
    1, Iclass_xt_iclass_call8_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_call4_args,
    1, Iclass_xt_iclass_call4_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_callx12_args,
    1, Iclass_xt_iclass_callx12_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_callx8_args,
    1, Iclass_xt_iclass_callx8_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_callx4_args,
    1, Iclass_xt_iclass_callx4_stateArgs, 0, 0 },
  { 3, Iclass_xt_iclass_entry_args,
    5, Iclass_xt_iclass_entry_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_movsp_args,
    2, Iclass_xt_iclass_movsp_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rotw_args,
    3, Iclass_xt_iclass_rotw_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_retw_args,
    4, Iclass_xt_iclass_retw_stateArgs, 0, 0 },
  { 0, 0 ,
    6, Iclass_xt_iclass_rfwou_stateArgs, 0, 0 },
  { 3, Iclass_xt_iclass_l32e_args,
    2, Iclass_xt_iclass_l32e_stateArgs, 0, 0 },
  { 3, Iclass_xt_iclass_s32e_args,
    2, Iclass_xt_iclass_s32e_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_windowbase_args,
    3, Iclass_xt_iclass_rsr_windowbase_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_windowbase_args,
    3, Iclass_xt_iclass_wsr_windowbase_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_windowbase_args,
    3, Iclass_xt_iclass_xsr_windowbase_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_windowstart_args,
    3, Iclass_xt_iclass_rsr_windowstart_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_windowstart_args,
    3, Iclass_xt_iclass_wsr_windowstart_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_windowstart_args,
    3, Iclass_xt_iclass_xsr_windowstart_stateArgs, 0, 0 },
  { 3, Iclass_xt_iclass_add_n_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_addi_n_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_bz6_args,
    0, 0, 0, 0 },
  { 0, 0 ,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_loadi4_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_mov_n_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_movi_n_args,
    0, 0, 0, 0 },
  { 0, 0 ,
    0, 0, 0, 0 },
  { 1, Iclass_xt_iclass_retn_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_storei4_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_addi_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_addmi_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_addsub_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_bit_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_bsi8_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_bsi8b_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_bsi8u_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_bst8_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_bsz12_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_call0_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_callx0_args,
    0, 0, 0, 0 },
  { 4, Iclass_xt_iclass_exti_args,
    0, 0, 0, 0 },
  { 0, 0 ,
    0, 0, 0, 0 },
  { 1, Iclass_xt_iclass_jump_args,
    0, 0, 0, 0 },
  { 1, Iclass_xt_iclass_jumpx_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_l16ui_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_l16si_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_l32i_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_l32r_args,
    2, Iclass_xt_iclass_l32r_stateArgs, 0, 0 },
  { 3, Iclass_xt_iclass_l8i_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_loop_args,
    3, Iclass_xt_iclass_loop_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_loopz_args,
    3, Iclass_xt_iclass_loopz_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_movi_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_movz_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_neg_args,
    0, 0, 0, 0 },
  { 0, 0 ,
    0, 0, 0, 0 },
  { 1, Iclass_xt_iclass_return_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_s16i_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_s32i_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_s8i_args,
    0, 0, 0, 0 },
  { 1, Iclass_xt_iclass_sar_args,
    1, Iclass_xt_iclass_sar_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_sari_args,
    1, Iclass_xt_iclass_sari_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_shifts_args,
    1, Iclass_xt_iclass_shifts_stateArgs, 0, 0 },
  { 3, Iclass_xt_iclass_shiftst_args,
    1, Iclass_xt_iclass_shiftst_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_shiftt_args,
    1, Iclass_xt_iclass_shiftt_stateArgs, 0, 0 },
  { 3, Iclass_xt_iclass_slli_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_srai_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_srli_args,
    0, 0, 0, 0 },
  { 0, 0 ,
    0, 0, 0, 0 },
  { 0, 0 ,
    0, 0, 0, 0 },
  { 0, 0 ,
    0, 0, 0, 0 },
  { 0, 0 ,
    1, Iclass_xt_iclass_sync_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_rsil_args,
    7, Iclass_xt_iclass_rsil_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_lend_args,
    1, Iclass_xt_iclass_rsr_lend_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_lend_args,
    1, Iclass_xt_iclass_wsr_lend_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_lend_args,
    1, Iclass_xt_iclass_xsr_lend_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_lcount_args,
    1, Iclass_xt_iclass_rsr_lcount_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_lcount_args,
    2, Iclass_xt_iclass_wsr_lcount_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_lcount_args,
    2, Iclass_xt_iclass_xsr_lcount_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_lbeg_args,
    1, Iclass_xt_iclass_rsr_lbeg_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_lbeg_args,
    1, Iclass_xt_iclass_wsr_lbeg_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_lbeg_args,
    1, Iclass_xt_iclass_xsr_lbeg_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_sar_args,
    1, Iclass_xt_iclass_rsr_sar_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_sar_args,
    2, Iclass_xt_iclass_wsr_sar_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_sar_args,
    1, Iclass_xt_iclass_xsr_sar_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_litbase_args,
    2, Iclass_xt_iclass_rsr_litbase_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_litbase_args,
    2, Iclass_xt_iclass_wsr_litbase_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_litbase_args,
    2, Iclass_xt_iclass_xsr_litbase_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_176_args,
    2, Iclass_xt_iclass_rsr_176_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_208_args,
    2, Iclass_xt_iclass_rsr_208_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_ps_args,
    7, Iclass_xt_iclass_rsr_ps_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_ps_args,
    7, Iclass_xt_iclass_wsr_ps_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_ps_args,
    7, Iclass_xt_iclass_xsr_ps_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_epc1_args,
    3, Iclass_xt_iclass_rsr_epc1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_epc1_args,
    3, Iclass_xt_iclass_wsr_epc1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_epc1_args,
    3, Iclass_xt_iclass_xsr_epc1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_excsave1_args,
    3, Iclass_xt_iclass_rsr_excsave1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_excsave1_args,
    3, Iclass_xt_iclass_wsr_excsave1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_excsave1_args,
    3, Iclass_xt_iclass_xsr_excsave1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_epc2_args,
    3, Iclass_xt_iclass_rsr_epc2_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_epc2_args,
    3, Iclass_xt_iclass_wsr_epc2_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_epc2_args,
    3, Iclass_xt_iclass_xsr_epc2_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_excsave2_args,
    3, Iclass_xt_iclass_rsr_excsave2_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_excsave2_args,
    3, Iclass_xt_iclass_wsr_excsave2_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_excsave2_args,
    3, Iclass_xt_iclass_xsr_excsave2_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_epc3_args,
    3, Iclass_xt_iclass_rsr_epc3_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_epc3_args,
    3, Iclass_xt_iclass_wsr_epc3_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_epc3_args,
    3, Iclass_xt_iclass_xsr_epc3_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_excsave3_args,
    3, Iclass_xt_iclass_rsr_excsave3_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_excsave3_args,
    3, Iclass_xt_iclass_wsr_excsave3_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_excsave3_args,
    3, Iclass_xt_iclass_xsr_excsave3_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_epc4_args,
    3, Iclass_xt_iclass_rsr_epc4_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_epc4_args,
    3, Iclass_xt_iclass_wsr_epc4_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_epc4_args,
    3, Iclass_xt_iclass_xsr_epc4_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_excsave4_args,
    3, Iclass_xt_iclass_rsr_excsave4_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_excsave4_args,
    3, Iclass_xt_iclass_wsr_excsave4_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_excsave4_args,
    3, Iclass_xt_iclass_xsr_excsave4_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_eps2_args,
    3, Iclass_xt_iclass_rsr_eps2_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_eps2_args,
    3, Iclass_xt_iclass_wsr_eps2_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_eps2_args,
    3, Iclass_xt_iclass_xsr_eps2_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_eps3_args,
    3, Iclass_xt_iclass_rsr_eps3_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_eps3_args,
    3, Iclass_xt_iclass_wsr_eps3_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_eps3_args,
    3, Iclass_xt_iclass_xsr_eps3_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_eps4_args,
    3, Iclass_xt_iclass_rsr_eps4_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_eps4_args,
    3, Iclass_xt_iclass_wsr_eps4_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_eps4_args,
    3, Iclass_xt_iclass_xsr_eps4_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_excvaddr_args,
    3, Iclass_xt_iclass_rsr_excvaddr_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_excvaddr_args,
    3, Iclass_xt_iclass_wsr_excvaddr_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_excvaddr_args,
    3, Iclass_xt_iclass_xsr_excvaddr_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_depc_args,
    3, Iclass_xt_iclass_rsr_depc_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_depc_args,
    3, Iclass_xt_iclass_wsr_depc_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_depc_args,
    3, Iclass_xt_iclass_xsr_depc_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_exccause_args,
    4, Iclass_xt_iclass_rsr_exccause_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_exccause_args,
    3, Iclass_xt_iclass_wsr_exccause_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_exccause_args,
    3, Iclass_xt_iclass_xsr_exccause_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_misc0_args,
    3, Iclass_xt_iclass_rsr_misc0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_misc0_args,
    3, Iclass_xt_iclass_wsr_misc0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_misc0_args,
    3, Iclass_xt_iclass_xsr_misc0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_misc1_args,
    3, Iclass_xt_iclass_rsr_misc1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_misc1_args,
    3, Iclass_xt_iclass_wsr_misc1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_misc1_args,
    3, Iclass_xt_iclass_xsr_misc1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_prid_args,
    2, Iclass_xt_iclass_rsr_prid_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rfi_args,
    15, Iclass_xt_iclass_rfi_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wait_args,
    3, Iclass_xt_iclass_wait_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_interrupt_args,
    3, Iclass_xt_iclass_rsr_interrupt_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_intset_args,
    4, Iclass_xt_iclass_wsr_intset_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_intclear_args,
    4, Iclass_xt_iclass_wsr_intclear_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_intenable_args,
    3, Iclass_xt_iclass_rsr_intenable_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_intenable_args,
    3, Iclass_xt_iclass_wsr_intenable_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_intenable_args,
    3, Iclass_xt_iclass_xsr_intenable_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_break_args,
    2, Iclass_xt_iclass_break_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_break_n_args,
    2, Iclass_xt_iclass_break_n_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_dbreaka0_args,
    3, Iclass_xt_iclass_rsr_dbreaka0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_dbreaka0_args,
    4, Iclass_xt_iclass_wsr_dbreaka0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_dbreaka0_args,
    4, Iclass_xt_iclass_xsr_dbreaka0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_dbreakc0_args,
    3, Iclass_xt_iclass_rsr_dbreakc0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_dbreakc0_args,
    4, Iclass_xt_iclass_wsr_dbreakc0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_dbreakc0_args,
    4, Iclass_xt_iclass_xsr_dbreakc0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_dbreaka1_args,
    3, Iclass_xt_iclass_rsr_dbreaka1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_dbreaka1_args,
    4, Iclass_xt_iclass_wsr_dbreaka1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_dbreaka1_args,
    4, Iclass_xt_iclass_xsr_dbreaka1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_dbreakc1_args,
    3, Iclass_xt_iclass_rsr_dbreakc1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_dbreakc1_args,
    4, Iclass_xt_iclass_wsr_dbreakc1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_dbreakc1_args,
    4, Iclass_xt_iclass_xsr_dbreakc1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_ibreaka0_args,
    3, Iclass_xt_iclass_rsr_ibreaka0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_ibreaka0_args,
    3, Iclass_xt_iclass_wsr_ibreaka0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_ibreaka0_args,
    3, Iclass_xt_iclass_xsr_ibreaka0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_ibreaka1_args,
    3, Iclass_xt_iclass_rsr_ibreaka1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_ibreaka1_args,
    3, Iclass_xt_iclass_wsr_ibreaka1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_ibreaka1_args,
    3, Iclass_xt_iclass_xsr_ibreaka1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_ibreakenable_args,
    3, Iclass_xt_iclass_rsr_ibreakenable_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_ibreakenable_args,
    3, Iclass_xt_iclass_wsr_ibreakenable_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_ibreakenable_args,
    3, Iclass_xt_iclass_xsr_ibreakenable_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_debugcause_args,
    4, Iclass_xt_iclass_rsr_debugcause_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_debugcause_args,
    4, Iclass_xt_iclass_wsr_debugcause_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_debugcause_args,
    4, Iclass_xt_iclass_xsr_debugcause_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_icount_args,
    3, Iclass_xt_iclass_rsr_icount_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_icount_args,
    4, Iclass_xt_iclass_wsr_icount_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_icount_args,
    4, Iclass_xt_iclass_xsr_icount_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_icountlevel_args,
    3, Iclass_xt_iclass_rsr_icountlevel_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_icountlevel_args,
    3, Iclass_xt_iclass_wsr_icountlevel_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_icountlevel_args,
    3, Iclass_xt_iclass_xsr_icountlevel_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_ddr_args,
    3, Iclass_xt_iclass_rsr_ddr_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_ddr_args,
    4, Iclass_xt_iclass_wsr_ddr_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_ddr_args,
    4, Iclass_xt_iclass_xsr_ddr_stateArgs, 0, 0 },
  { 0, 0 ,
    10, Iclass_xt_iclass_rfdo_stateArgs, 0, 0 },
  { 0, 0 ,
    1, Iclass_xt_iclass_rfdd_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_ccount_args,
    3, Iclass_xt_iclass_rsr_ccount_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_ccount_args,
    4, Iclass_xt_iclass_wsr_ccount_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_ccount_args,
    4, Iclass_xt_iclass_xsr_ccount_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_ccompare0_args,
    3, Iclass_xt_iclass_rsr_ccompare0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_ccompare0_args,
    4, Iclass_xt_iclass_wsr_ccompare0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_ccompare0_args,
    4, Iclass_xt_iclass_xsr_ccompare0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_ccompare1_args,
    3, Iclass_xt_iclass_rsr_ccompare1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_ccompare1_args,
    4, Iclass_xt_iclass_wsr_ccompare1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_ccompare1_args,
    4, Iclass_xt_iclass_xsr_ccompare1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_ccompare2_args,
    3, Iclass_xt_iclass_rsr_ccompare2_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_ccompare2_args,
    4, Iclass_xt_iclass_wsr_ccompare2_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_ccompare2_args,
    4, Iclass_xt_iclass_xsr_ccompare2_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_icache_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_icache_inv_args,
    2, Iclass_xt_iclass_icache_inv_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_licx_args,
    2, Iclass_xt_iclass_licx_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_sicx_args,
    2, Iclass_xt_iclass_sicx_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_dcache_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_dcache_ind_args,
    2, Iclass_xt_iclass_dcache_ind_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_dcache_inv_args,
    2, Iclass_xt_iclass_dcache_inv_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_dpf_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_sdct_args,
    2, Iclass_xt_iclass_sdct_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_ldct_args,
    2, Iclass_xt_iclass_ldct_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_ptevaddr_args,
    4, Iclass_xt_iclass_wsr_ptevaddr_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_ptevaddr_args,
    4, Iclass_xt_iclass_rsr_ptevaddr_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_ptevaddr_args,
    5, Iclass_xt_iclass_xsr_ptevaddr_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_rasid_args,
    5, Iclass_xt_iclass_rsr_rasid_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_rasid_args,
    6, Iclass_xt_iclass_wsr_rasid_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_rasid_args,
    6, Iclass_xt_iclass_xsr_rasid_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_itlbcfg_args,
    3, Iclass_xt_iclass_rsr_itlbcfg_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_itlbcfg_args,
    4, Iclass_xt_iclass_wsr_itlbcfg_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_itlbcfg_args,
    4, Iclass_xt_iclass_xsr_itlbcfg_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_dtlbcfg_args,
    3, Iclass_xt_iclass_rsr_dtlbcfg_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_dtlbcfg_args,
    4, Iclass_xt_iclass_wsr_dtlbcfg_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_dtlbcfg_args,
    4, Iclass_xt_iclass_xsr_dtlbcfg_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_idtlb_args,
    3, Iclass_xt_iclass_idtlb_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_rdtlb_args,
    2, Iclass_xt_iclass_rdtlb_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_wdtlb_args,
    3, Iclass_xt_iclass_wdtlb_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_iitlb_args,
    2, Iclass_xt_iclass_iitlb_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_ritlb_args,
    2, Iclass_xt_iclass_ritlb_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_witlb_args,
    2, Iclass_xt_iclass_witlb_stateArgs, 0, 0 },
  { 0, 0 ,
    2, Iclass_xt_iclass_ldpte_stateArgs, 0, 0 },
  { 0, 0 ,
    1, Iclass_xt_iclass_hwwitlba_stateArgs, 0, 0 },
  { 0, 0 ,
    1, Iclass_xt_iclass_hwwdtlba_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_nsa_args,
    0, 0, 0, 0 }
};



static void
Opcode_excw_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x80200;
}

static void
Opcode_rfe_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x300;
}

static void
Opcode_rfde_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2300;
}

static void
Opcode_syscall_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x500;
}

static void
Opcode_simcall_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x1500;
}

static void
Opcode_call12_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x5c0000;
}

static void
Opcode_call8_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x580000;
}

static void
Opcode_call4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x540000;
}

static void
Opcode_callx12_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf0000;
}

static void
Opcode_callx8_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb0000;
}

static void
Opcode_callx4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x70000;
}

static void
Opcode_entry_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6c0000;
}

static void
Opcode_movsp_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x100;
}

static void
Opcode_rotw_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x804;
}

static void
Opcode_retw_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x60000;
}

static void
Opcode_retw_n_Slot_inst16b_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd10f;
}

static void
Opcode_rfwo_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x4300;
}

static void
Opcode_rfwu_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x5300;
}

static void
Opcode_l32e_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x90;
}

static void
Opcode_s32e_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x94;
}

static void
Opcode_rsr_windowbase_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x4830;
}

static void
Opcode_wsr_windowbase_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x4831;
}

static void
Opcode_xsr_windowbase_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x4816;
}

static void
Opcode_rsr_windowstart_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x4930;
}

static void
Opcode_wsr_windowstart_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x4931;
}

static void
Opcode_xsr_windowstart_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x4916;
}

static void
Opcode_add_n_Slot_inst16a_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xa000;
}

static void
Opcode_addi_n_Slot_inst16a_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb000;
}

static void
Opcode_beqz_n_Slot_inst16b_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc800;
}

static void
Opcode_bnez_n_Slot_inst16b_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xcc00;
}

static void
Opcode_ill_n_Slot_inst16b_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd60f;
}

static void
Opcode_l32i_n_Slot_inst16a_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x8000;
}

static void
Opcode_mov_n_Slot_inst16b_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd000;
}

static void
Opcode_movi_n_Slot_inst16b_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc000;
}

static void
Opcode_nop_n_Slot_inst16b_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd30f;
}

static void
Opcode_ret_n_Slot_inst16b_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd00f;
}

static void
Opcode_s32i_n_Slot_inst16a_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x9000;
}

static void
Opcode_addi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x200c00;
}

static void
Opcode_addmi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x200d00;
}

static void
Opcode_add_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x8;
}

static void
Opcode_sub_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc;
}

static void
Opcode_addx2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x9;
}

static void
Opcode_addx4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xa;
}

static void
Opcode_addx8_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb;
}

static void
Opcode_subx2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd;
}

static void
Opcode_subx4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe;
}

static void
Opcode_subx8_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf;
}

static void
Opcode_and_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x1;
}

static void
Opcode_or_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2;
}

static void
Opcode_xor_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3;
}

static void
Opcode_beqi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x680000;
}

static void
Opcode_bnei_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x690000;
}

static void
Opcode_bgei_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6b0000;
}

static void
Opcode_blti_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6a0000;
}

static void
Opcode_bbci_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x700600;
}

static void
Opcode_bbsi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x700e00;
}

static void
Opcode_bgeui_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6f0000;
}

static void
Opcode_bltui_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6e0000;
}

static void
Opcode_beq_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x700100;
}

static void
Opcode_bne_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x700900;
}

static void
Opcode_bge_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x700a00;
}

static void
Opcode_blt_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x700200;
}

static void
Opcode_bgeu_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x700b00;
}

static void
Opcode_bltu_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x700300;
}

static void
Opcode_bany_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x700800;
}

static void
Opcode_bnone_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x700000;
}

static void
Opcode_ball_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x700400;
}

static void
Opcode_bnall_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x700c00;
}

static void
Opcode_bbc_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x700500;
}

static void
Opcode_bbs_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x700d00;
}

static void
Opcode_beqz_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x640000;
}

static void
Opcode_bnez_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x650000;
}

static void
Opcode_bgez_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x670000;
}

static void
Opcode_bltz_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x660000;
}

static void
Opcode_call0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x500000;
}

static void
Opcode_callx0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x30000;
}

static void
Opcode_extui_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x40;
}

static void
Opcode_ill_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0;
}

static void
Opcode_j_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x600000;
}

static void
Opcode_jx_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xa0000;
}

static void
Opcode_l16ui_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x200100;
}

static void
Opcode_l16si_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x200900;
}

static void
Opcode_l32i_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x200200;
}

static void
Opcode_l32r_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x100000;
}

static void
Opcode_l8ui_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x200000;
}

static void
Opcode_loop_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6d0800;
}

static void
Opcode_loopnez_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6d0900;
}

static void
Opcode_loopgtz_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6d0a00;
}

static void
Opcode_movi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x200a00;
}

static void
Opcode_moveqz_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x38;
}

static void
Opcode_movnez_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x39;
}

static void
Opcode_movltz_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3a;
}

static void
Opcode_movgez_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3b;
}

static void
Opcode_neg_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6;
}

static void
Opcode_abs_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x1006;
}

static void
Opcode_nop_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf0200;
}

static void
Opcode_ret_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x20000;
}

static void
Opcode_s16i_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x200500;
}

static void
Opcode_s32i_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x200600;
}

static void
Opcode_s8i_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x200400;
}

static void
Opcode_ssr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x4;
}

static void
Opcode_ssl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x104;
}

static void
Opcode_ssa8l_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x204;
}

static void
Opcode_ssa8b_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x304;
}

static void
Opcode_ssai_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x404;
}

static void
Opcode_sll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x1a;
}

static void
Opcode_src_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x18;
}

static void
Opcode_srl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x19;
}

static void
Opcode_sra_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x1b;
}

static void
Opcode_slli_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x10;
}

static void
Opcode_srai_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x12;
}

static void
Opcode_srli_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x14;
}

static void
Opcode_memw_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc0200;
}

static void
Opcode_extw_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd0200;
}

static void
Opcode_isync_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x200;
}

static void
Opcode_rsync_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x10200;
}

static void
Opcode_esync_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x20200;
}

static void
Opcode_dsync_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x30200;
}

static void
Opcode_rsil_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x600;
}

static void
Opcode_rsr_lend_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x130;
}

static void
Opcode_wsr_lend_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x131;
}

static void
Opcode_xsr_lend_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x116;
}

static void
Opcode_rsr_lcount_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x230;
}

static void
Opcode_wsr_lcount_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x231;
}

static void
Opcode_xsr_lcount_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x216;
}

static void
Opcode_rsr_lbeg_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x30;
}

static void
Opcode_wsr_lbeg_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x31;
}

static void
Opcode_xsr_lbeg_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x16;
}

static void
Opcode_rsr_sar_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x330;
}

static void
Opcode_wsr_sar_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x331;
}

static void
Opcode_xsr_sar_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x316;
}

static void
Opcode_rsr_litbase_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x530;
}

static void
Opcode_wsr_litbase_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x531;
}

static void
Opcode_xsr_litbase_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x516;
}

static void
Opcode_rsr_176_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb030;
}

static void
Opcode_rsr_208_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd030;
}

static void
Opcode_rsr_ps_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe630;
}

static void
Opcode_wsr_ps_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe631;
}

static void
Opcode_xsr_ps_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe616;
}

static void
Opcode_rsr_epc1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb130;
}

static void
Opcode_wsr_epc1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb131;
}

static void
Opcode_xsr_epc1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb116;
}

static void
Opcode_rsr_excsave1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd130;
}

static void
Opcode_wsr_excsave1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd131;
}

static void
Opcode_xsr_excsave1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd116;
}

static void
Opcode_rsr_epc2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb230;
}

static void
Opcode_wsr_epc2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb231;
}

static void
Opcode_xsr_epc2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb216;
}

static void
Opcode_rsr_excsave2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd230;
}

static void
Opcode_wsr_excsave2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd231;
}

static void
Opcode_xsr_excsave2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd216;
}

static void
Opcode_rsr_epc3_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb330;
}

static void
Opcode_wsr_epc3_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb331;
}

static void
Opcode_xsr_epc3_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb316;
}

static void
Opcode_rsr_excsave3_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd330;
}

static void
Opcode_wsr_excsave3_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd331;
}

static void
Opcode_xsr_excsave3_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd316;
}

static void
Opcode_rsr_epc4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb430;
}

static void
Opcode_wsr_epc4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb431;
}

static void
Opcode_xsr_epc4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb416;
}

static void
Opcode_rsr_excsave4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd430;
}

static void
Opcode_wsr_excsave4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd431;
}

static void
Opcode_xsr_excsave4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd416;
}

static void
Opcode_rsr_eps2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc230;
}

static void
Opcode_wsr_eps2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc231;
}

static void
Opcode_xsr_eps2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc216;
}

static void
Opcode_rsr_eps3_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc330;
}

static void
Opcode_wsr_eps3_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc331;
}

static void
Opcode_xsr_eps3_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc316;
}

static void
Opcode_rsr_eps4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc430;
}

static void
Opcode_wsr_eps4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc431;
}

static void
Opcode_xsr_eps4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc416;
}

static void
Opcode_rsr_excvaddr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xee30;
}

static void
Opcode_wsr_excvaddr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xee31;
}

static void
Opcode_xsr_excvaddr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xee16;
}

static void
Opcode_rsr_depc_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc030;
}

static void
Opcode_wsr_depc_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc031;
}

static void
Opcode_xsr_depc_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc016;
}

static void
Opcode_rsr_exccause_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe830;
}

static void
Opcode_wsr_exccause_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe831;
}

static void
Opcode_xsr_exccause_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe816;
}

static void
Opcode_rsr_misc0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf430;
}

static void
Opcode_wsr_misc0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf431;
}

static void
Opcode_xsr_misc0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf416;
}

static void
Opcode_rsr_misc1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf530;
}

static void
Opcode_wsr_misc1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf531;
}

static void
Opcode_xsr_misc1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf516;
}

static void
Opcode_rsr_prid_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xeb30;
}

static void
Opcode_rfi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x10300;
}

static void
Opcode_waiti_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x700;
}

static void
Opcode_rsr_interrupt_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe230;
}

static void
Opcode_wsr_intset_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe231;
}

static void
Opcode_wsr_intclear_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe331;
}

static void
Opcode_rsr_intenable_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe430;
}

static void
Opcode_wsr_intenable_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe431;
}

static void
Opcode_xsr_intenable_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe416;
}

static void
Opcode_break_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x400;
}

static void
Opcode_break_n_Slot_inst16b_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd20f;
}

static void
Opcode_rsr_dbreaka0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x9030;
}

static void
Opcode_wsr_dbreaka0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x9031;
}

static void
Opcode_xsr_dbreaka0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x9016;
}

static void
Opcode_rsr_dbreakc0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xa030;
}

static void
Opcode_wsr_dbreakc0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xa031;
}

static void
Opcode_xsr_dbreakc0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xa016;
}

static void
Opcode_rsr_dbreaka1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x9130;
}

static void
Opcode_wsr_dbreaka1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x9131;
}

static void
Opcode_xsr_dbreaka1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x9116;
}

static void
Opcode_rsr_dbreakc1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xa130;
}

static void
Opcode_wsr_dbreakc1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xa131;
}

static void
Opcode_xsr_dbreakc1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xa116;
}

static void
Opcode_rsr_ibreaka0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x8030;
}

static void
Opcode_wsr_ibreaka0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x8031;
}

static void
Opcode_xsr_ibreaka0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x8016;
}

static void
Opcode_rsr_ibreaka1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x8130;
}

static void
Opcode_wsr_ibreaka1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x8131;
}

static void
Opcode_xsr_ibreaka1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x8116;
}

static void
Opcode_rsr_ibreakenable_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6030;
}

static void
Opcode_wsr_ibreakenable_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6031;
}

static void
Opcode_xsr_ibreakenable_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6016;
}

static void
Opcode_rsr_debugcause_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe930;
}

static void
Opcode_wsr_debugcause_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe931;
}

static void
Opcode_xsr_debugcause_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe916;
}

static void
Opcode_rsr_icount_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xec30;
}

static void
Opcode_wsr_icount_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xec31;
}

static void
Opcode_xsr_icount_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xec16;
}

static void
Opcode_rsr_icountlevel_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xed30;
}

static void
Opcode_wsr_icountlevel_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xed31;
}

static void
Opcode_xsr_icountlevel_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xed16;
}

static void
Opcode_rsr_ddr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6830;
}

static void
Opcode_wsr_ddr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6831;
}

static void
Opcode_xsr_ddr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6816;
}

static void
Opcode_rfdo_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe1f;
}

static void
Opcode_rfdd_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x10e1f;
}

static void
Opcode_rsr_ccount_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xea30;
}

static void
Opcode_wsr_ccount_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xea31;
}

static void
Opcode_xsr_ccount_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xea16;
}

static void
Opcode_rsr_ccompare0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf030;
}

static void
Opcode_wsr_ccompare0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf031;
}

static void
Opcode_xsr_ccompare0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf016;
}

static void
Opcode_rsr_ccompare1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf130;
}

static void
Opcode_wsr_ccompare1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf131;
}

static void
Opcode_xsr_ccompare1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf116;
}

static void
Opcode_rsr_ccompare2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf230;
}

static void
Opcode_wsr_ccompare2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf231;
}

static void
Opcode_xsr_ccompare2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf216;
}

static void
Opcode_ipf_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2c0700;
}

static void
Opcode_ihi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2e0700;
}

static void
Opcode_iii_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2f0700;
}

static void
Opcode_lict_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x1f;
}

static void
Opcode_licw_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x21f;
}

static void
Opcode_sict_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x11f;
}

static void
Opcode_sicw_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x31f;
}

static void
Opcode_dhwb_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x240700;
}

static void
Opcode_dhwbi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x250700;
}

static void
Opcode_diwb_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x280740;
}

static void
Opcode_diwbi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x280750;
}

static void
Opcode_dhi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x260700;
}

static void
Opcode_dii_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x270700;
}

static void
Opcode_dpfr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x200700;
}

static void
Opcode_dpfw_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x210700;
}

static void
Opcode_dpfro_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x220700;
}

static void
Opcode_dpfwo_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x230700;
}

static void
Opcode_sdct_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x91f;
}

static void
Opcode_ldct_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x81f;
}

static void
Opcode_wsr_ptevaddr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x5331;
}

static void
Opcode_rsr_ptevaddr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x5330;
}

static void
Opcode_xsr_ptevaddr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x5316;
}

static void
Opcode_rsr_rasid_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x5a30;
}

static void
Opcode_wsr_rasid_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x5a31;
}

static void
Opcode_xsr_rasid_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x5a16;
}

static void
Opcode_rsr_itlbcfg_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x5b30;
}

static void
Opcode_wsr_itlbcfg_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x5b31;
}

static void
Opcode_xsr_itlbcfg_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x5b16;
}

static void
Opcode_rsr_dtlbcfg_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x5c30;
}

static void
Opcode_wsr_dtlbcfg_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x5c31;
}

static void
Opcode_xsr_dtlbcfg_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x5c16;
}

static void
Opcode_idtlb_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc05;
}

static void
Opcode_pdtlb_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd05;
}

static void
Opcode_rdtlb0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb05;
}

static void
Opcode_rdtlb1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf05;
}

static void
Opcode_wdtlb_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe05;
}

static void
Opcode_iitlb_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x405;
}

static void
Opcode_pitlb_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x505;
}

static void
Opcode_ritlb0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x305;
}

static void
Opcode_ritlb1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x705;
}

static void
Opcode_witlb_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x605;
}

static void
Opcode_ldpte_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf1f;
}

static void
Opcode_hwwitlba_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x105;
}

static void
Opcode_hwwdtlba_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x905;
}

static void
Opcode_nsa_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe04;
}

static void
Opcode_nsau_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf04;
}

static xtensa_opcode_encode_fn Opcode_excw_encode_fns[] = {
  Opcode_excw_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rfe_encode_fns[] = {
  Opcode_rfe_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rfde_encode_fns[] = {
  Opcode_rfde_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_syscall_encode_fns[] = {
  Opcode_syscall_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_simcall_encode_fns[] = {
  Opcode_simcall_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_call12_encode_fns[] = {
  Opcode_call12_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_call8_encode_fns[] = {
  Opcode_call8_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_call4_encode_fns[] = {
  Opcode_call4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_callx12_encode_fns[] = {
  Opcode_callx12_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_callx8_encode_fns[] = {
  Opcode_callx8_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_callx4_encode_fns[] = {
  Opcode_callx4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_entry_encode_fns[] = {
  Opcode_entry_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_movsp_encode_fns[] = {
  Opcode_movsp_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rotw_encode_fns[] = {
  Opcode_rotw_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_retw_encode_fns[] = {
  Opcode_retw_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_retw_n_encode_fns[] = {
  0, 0, Opcode_retw_n_Slot_inst16b_encode
};

static xtensa_opcode_encode_fn Opcode_rfwo_encode_fns[] = {
  Opcode_rfwo_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rfwu_encode_fns[] = {
  Opcode_rfwu_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_l32e_encode_fns[] = {
  Opcode_l32e_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_s32e_encode_fns[] = {
  Opcode_s32e_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_windowbase_encode_fns[] = {
  Opcode_rsr_windowbase_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_windowbase_encode_fns[] = {
  Opcode_wsr_windowbase_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_windowbase_encode_fns[] = {
  Opcode_xsr_windowbase_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_windowstart_encode_fns[] = {
  Opcode_rsr_windowstart_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_windowstart_encode_fns[] = {
  Opcode_wsr_windowstart_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_windowstart_encode_fns[] = {
  Opcode_xsr_windowstart_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_add_n_encode_fns[] = {
  0, Opcode_add_n_Slot_inst16a_encode, 0
};

static xtensa_opcode_encode_fn Opcode_addi_n_encode_fns[] = {
  0, Opcode_addi_n_Slot_inst16a_encode, 0
};

static xtensa_opcode_encode_fn Opcode_beqz_n_encode_fns[] = {
  0, 0, Opcode_beqz_n_Slot_inst16b_encode
};

static xtensa_opcode_encode_fn Opcode_bnez_n_encode_fns[] = {
  0, 0, Opcode_bnez_n_Slot_inst16b_encode
};

static xtensa_opcode_encode_fn Opcode_ill_n_encode_fns[] = {
  0, 0, Opcode_ill_n_Slot_inst16b_encode
};

static xtensa_opcode_encode_fn Opcode_l32i_n_encode_fns[] = {
  0, Opcode_l32i_n_Slot_inst16a_encode, 0
};

static xtensa_opcode_encode_fn Opcode_mov_n_encode_fns[] = {
  0, 0, Opcode_mov_n_Slot_inst16b_encode
};

static xtensa_opcode_encode_fn Opcode_movi_n_encode_fns[] = {
  0, 0, Opcode_movi_n_Slot_inst16b_encode
};

static xtensa_opcode_encode_fn Opcode_nop_n_encode_fns[] = {
  0, 0, Opcode_nop_n_Slot_inst16b_encode
};

static xtensa_opcode_encode_fn Opcode_ret_n_encode_fns[] = {
  0, 0, Opcode_ret_n_Slot_inst16b_encode
};

static xtensa_opcode_encode_fn Opcode_s32i_n_encode_fns[] = {
  0, Opcode_s32i_n_Slot_inst16a_encode, 0
};

static xtensa_opcode_encode_fn Opcode_addi_encode_fns[] = {
  Opcode_addi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_addmi_encode_fns[] = {
  Opcode_addmi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_add_encode_fns[] = {
  Opcode_add_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_sub_encode_fns[] = {
  Opcode_sub_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_addx2_encode_fns[] = {
  Opcode_addx2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_addx4_encode_fns[] = {
  Opcode_addx4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_addx8_encode_fns[] = {
  Opcode_addx8_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_subx2_encode_fns[] = {
  Opcode_subx2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_subx4_encode_fns[] = {
  Opcode_subx4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_subx8_encode_fns[] = {
  Opcode_subx8_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_and_encode_fns[] = {
  Opcode_and_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_or_encode_fns[] = {
  Opcode_or_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xor_encode_fns[] = {
  Opcode_xor_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_beqi_encode_fns[] = {
  Opcode_beqi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bnei_encode_fns[] = {
  Opcode_bnei_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bgei_encode_fns[] = {
  Opcode_bgei_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_blti_encode_fns[] = {
  Opcode_blti_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bbci_encode_fns[] = {
  Opcode_bbci_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bbsi_encode_fns[] = {
  Opcode_bbsi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bgeui_encode_fns[] = {
  Opcode_bgeui_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bltui_encode_fns[] = {
  Opcode_bltui_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_beq_encode_fns[] = {
  Opcode_beq_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bne_encode_fns[] = {
  Opcode_bne_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bge_encode_fns[] = {
  Opcode_bge_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_blt_encode_fns[] = {
  Opcode_blt_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bgeu_encode_fns[] = {
  Opcode_bgeu_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bltu_encode_fns[] = {
  Opcode_bltu_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bany_encode_fns[] = {
  Opcode_bany_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bnone_encode_fns[] = {
  Opcode_bnone_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ball_encode_fns[] = {
  Opcode_ball_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bnall_encode_fns[] = {
  Opcode_bnall_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bbc_encode_fns[] = {
  Opcode_bbc_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bbs_encode_fns[] = {
  Opcode_bbs_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_beqz_encode_fns[] = {
  Opcode_beqz_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bnez_encode_fns[] = {
  Opcode_bnez_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bgez_encode_fns[] = {
  Opcode_bgez_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bltz_encode_fns[] = {
  Opcode_bltz_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_call0_encode_fns[] = {
  Opcode_call0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_callx0_encode_fns[] = {
  Opcode_callx0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_extui_encode_fns[] = {
  Opcode_extui_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ill_encode_fns[] = {
  Opcode_ill_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_j_encode_fns[] = {
  Opcode_j_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_jx_encode_fns[] = {
  Opcode_jx_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_l16ui_encode_fns[] = {
  Opcode_l16ui_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_l16si_encode_fns[] = {
  Opcode_l16si_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_l32i_encode_fns[] = {
  Opcode_l32i_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_l32r_encode_fns[] = {
  Opcode_l32r_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_l8ui_encode_fns[] = {
  Opcode_l8ui_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_loop_encode_fns[] = {
  Opcode_loop_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_loopnez_encode_fns[] = {
  Opcode_loopnez_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_loopgtz_encode_fns[] = {
  Opcode_loopgtz_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_movi_encode_fns[] = {
  Opcode_movi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_moveqz_encode_fns[] = {
  Opcode_moveqz_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_movnez_encode_fns[] = {
  Opcode_movnez_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_movltz_encode_fns[] = {
  Opcode_movltz_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_movgez_encode_fns[] = {
  Opcode_movgez_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_neg_encode_fns[] = {
  Opcode_neg_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_abs_encode_fns[] = {
  Opcode_abs_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_nop_encode_fns[] = {
  Opcode_nop_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ret_encode_fns[] = {
  Opcode_ret_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_s16i_encode_fns[] = {
  Opcode_s16i_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_s32i_encode_fns[] = {
  Opcode_s32i_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_s8i_encode_fns[] = {
  Opcode_s8i_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ssr_encode_fns[] = {
  Opcode_ssr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ssl_encode_fns[] = {
  Opcode_ssl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ssa8l_encode_fns[] = {
  Opcode_ssa8l_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ssa8b_encode_fns[] = {
  Opcode_ssa8b_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ssai_encode_fns[] = {
  Opcode_ssai_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_sll_encode_fns[] = {
  Opcode_sll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_src_encode_fns[] = {
  Opcode_src_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_srl_encode_fns[] = {
  Opcode_srl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_sra_encode_fns[] = {
  Opcode_sra_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_slli_encode_fns[] = {
  Opcode_slli_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_srai_encode_fns[] = {
  Opcode_srai_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_srli_encode_fns[] = {
  Opcode_srli_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_memw_encode_fns[] = {
  Opcode_memw_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_extw_encode_fns[] = {
  Opcode_extw_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_isync_encode_fns[] = {
  Opcode_isync_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsync_encode_fns[] = {
  Opcode_rsync_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_esync_encode_fns[] = {
  Opcode_esync_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_dsync_encode_fns[] = {
  Opcode_dsync_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsil_encode_fns[] = {
  Opcode_rsil_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_lend_encode_fns[] = {
  Opcode_rsr_lend_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_lend_encode_fns[] = {
  Opcode_wsr_lend_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_lend_encode_fns[] = {
  Opcode_xsr_lend_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_lcount_encode_fns[] = {
  Opcode_rsr_lcount_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_lcount_encode_fns[] = {
  Opcode_wsr_lcount_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_lcount_encode_fns[] = {
  Opcode_xsr_lcount_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_lbeg_encode_fns[] = {
  Opcode_rsr_lbeg_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_lbeg_encode_fns[] = {
  Opcode_wsr_lbeg_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_lbeg_encode_fns[] = {
  Opcode_xsr_lbeg_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_sar_encode_fns[] = {
  Opcode_rsr_sar_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_sar_encode_fns[] = {
  Opcode_wsr_sar_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_sar_encode_fns[] = {
  Opcode_xsr_sar_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_litbase_encode_fns[] = {
  Opcode_rsr_litbase_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_litbase_encode_fns[] = {
  Opcode_wsr_litbase_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_litbase_encode_fns[] = {
  Opcode_xsr_litbase_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_176_encode_fns[] = {
  Opcode_rsr_176_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_208_encode_fns[] = {
  Opcode_rsr_208_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_ps_encode_fns[] = {
  Opcode_rsr_ps_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_ps_encode_fns[] = {
  Opcode_wsr_ps_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_ps_encode_fns[] = {
  Opcode_xsr_ps_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_epc1_encode_fns[] = {
  Opcode_rsr_epc1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_epc1_encode_fns[] = {
  Opcode_wsr_epc1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_epc1_encode_fns[] = {
  Opcode_xsr_epc1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_excsave1_encode_fns[] = {
  Opcode_rsr_excsave1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_excsave1_encode_fns[] = {
  Opcode_wsr_excsave1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_excsave1_encode_fns[] = {
  Opcode_xsr_excsave1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_epc2_encode_fns[] = {
  Opcode_rsr_epc2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_epc2_encode_fns[] = {
  Opcode_wsr_epc2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_epc2_encode_fns[] = {
  Opcode_xsr_epc2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_excsave2_encode_fns[] = {
  Opcode_rsr_excsave2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_excsave2_encode_fns[] = {
  Opcode_wsr_excsave2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_excsave2_encode_fns[] = {
  Opcode_xsr_excsave2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_epc3_encode_fns[] = {
  Opcode_rsr_epc3_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_epc3_encode_fns[] = {
  Opcode_wsr_epc3_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_epc3_encode_fns[] = {
  Opcode_xsr_epc3_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_excsave3_encode_fns[] = {
  Opcode_rsr_excsave3_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_excsave3_encode_fns[] = {
  Opcode_wsr_excsave3_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_excsave3_encode_fns[] = {
  Opcode_xsr_excsave3_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_epc4_encode_fns[] = {
  Opcode_rsr_epc4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_epc4_encode_fns[] = {
  Opcode_wsr_epc4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_epc4_encode_fns[] = {
  Opcode_xsr_epc4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_excsave4_encode_fns[] = {
  Opcode_rsr_excsave4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_excsave4_encode_fns[] = {
  Opcode_wsr_excsave4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_excsave4_encode_fns[] = {
  Opcode_xsr_excsave4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_eps2_encode_fns[] = {
  Opcode_rsr_eps2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_eps2_encode_fns[] = {
  Opcode_wsr_eps2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_eps2_encode_fns[] = {
  Opcode_xsr_eps2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_eps3_encode_fns[] = {
  Opcode_rsr_eps3_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_eps3_encode_fns[] = {
  Opcode_wsr_eps3_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_eps3_encode_fns[] = {
  Opcode_xsr_eps3_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_eps4_encode_fns[] = {
  Opcode_rsr_eps4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_eps4_encode_fns[] = {
  Opcode_wsr_eps4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_eps4_encode_fns[] = {
  Opcode_xsr_eps4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_excvaddr_encode_fns[] = {
  Opcode_rsr_excvaddr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_excvaddr_encode_fns[] = {
  Opcode_wsr_excvaddr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_excvaddr_encode_fns[] = {
  Opcode_xsr_excvaddr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_depc_encode_fns[] = {
  Opcode_rsr_depc_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_depc_encode_fns[] = {
  Opcode_wsr_depc_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_depc_encode_fns[] = {
  Opcode_xsr_depc_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_exccause_encode_fns[] = {
  Opcode_rsr_exccause_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_exccause_encode_fns[] = {
  Opcode_wsr_exccause_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_exccause_encode_fns[] = {
  Opcode_xsr_exccause_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_misc0_encode_fns[] = {
  Opcode_rsr_misc0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_misc0_encode_fns[] = {
  Opcode_wsr_misc0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_misc0_encode_fns[] = {
  Opcode_xsr_misc0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_misc1_encode_fns[] = {
  Opcode_rsr_misc1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_misc1_encode_fns[] = {
  Opcode_wsr_misc1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_misc1_encode_fns[] = {
  Opcode_xsr_misc1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_prid_encode_fns[] = {
  Opcode_rsr_prid_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rfi_encode_fns[] = {
  Opcode_rfi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_waiti_encode_fns[] = {
  Opcode_waiti_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_interrupt_encode_fns[] = {
  Opcode_rsr_interrupt_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_intset_encode_fns[] = {
  Opcode_wsr_intset_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_intclear_encode_fns[] = {
  Opcode_wsr_intclear_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_intenable_encode_fns[] = {
  Opcode_rsr_intenable_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_intenable_encode_fns[] = {
  Opcode_wsr_intenable_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_intenable_encode_fns[] = {
  Opcode_xsr_intenable_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_break_encode_fns[] = {
  Opcode_break_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_break_n_encode_fns[] = {
  0, 0, Opcode_break_n_Slot_inst16b_encode
};

static xtensa_opcode_encode_fn Opcode_rsr_dbreaka0_encode_fns[] = {
  Opcode_rsr_dbreaka0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_dbreaka0_encode_fns[] = {
  Opcode_wsr_dbreaka0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_dbreaka0_encode_fns[] = {
  Opcode_xsr_dbreaka0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_dbreakc0_encode_fns[] = {
  Opcode_rsr_dbreakc0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_dbreakc0_encode_fns[] = {
  Opcode_wsr_dbreakc0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_dbreakc0_encode_fns[] = {
  Opcode_xsr_dbreakc0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_dbreaka1_encode_fns[] = {
  Opcode_rsr_dbreaka1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_dbreaka1_encode_fns[] = {
  Opcode_wsr_dbreaka1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_dbreaka1_encode_fns[] = {
  Opcode_xsr_dbreaka1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_dbreakc1_encode_fns[] = {
  Opcode_rsr_dbreakc1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_dbreakc1_encode_fns[] = {
  Opcode_wsr_dbreakc1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_dbreakc1_encode_fns[] = {
  Opcode_xsr_dbreakc1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_ibreaka0_encode_fns[] = {
  Opcode_rsr_ibreaka0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_ibreaka0_encode_fns[] = {
  Opcode_wsr_ibreaka0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_ibreaka0_encode_fns[] = {
  Opcode_xsr_ibreaka0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_ibreaka1_encode_fns[] = {
  Opcode_rsr_ibreaka1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_ibreaka1_encode_fns[] = {
  Opcode_wsr_ibreaka1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_ibreaka1_encode_fns[] = {
  Opcode_xsr_ibreaka1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_ibreakenable_encode_fns[] = {
  Opcode_rsr_ibreakenable_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_ibreakenable_encode_fns[] = {
  Opcode_wsr_ibreakenable_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_ibreakenable_encode_fns[] = {
  Opcode_xsr_ibreakenable_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_debugcause_encode_fns[] = {
  Opcode_rsr_debugcause_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_debugcause_encode_fns[] = {
  Opcode_wsr_debugcause_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_debugcause_encode_fns[] = {
  Opcode_xsr_debugcause_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_icount_encode_fns[] = {
  Opcode_rsr_icount_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_icount_encode_fns[] = {
  Opcode_wsr_icount_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_icount_encode_fns[] = {
  Opcode_xsr_icount_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_icountlevel_encode_fns[] = {
  Opcode_rsr_icountlevel_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_icountlevel_encode_fns[] = {
  Opcode_wsr_icountlevel_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_icountlevel_encode_fns[] = {
  Opcode_xsr_icountlevel_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_ddr_encode_fns[] = {
  Opcode_rsr_ddr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_ddr_encode_fns[] = {
  Opcode_wsr_ddr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_ddr_encode_fns[] = {
  Opcode_xsr_ddr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rfdo_encode_fns[] = {
  Opcode_rfdo_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rfdd_encode_fns[] = {
  Opcode_rfdd_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_ccount_encode_fns[] = {
  Opcode_rsr_ccount_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_ccount_encode_fns[] = {
  Opcode_wsr_ccount_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_ccount_encode_fns[] = {
  Opcode_xsr_ccount_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_ccompare0_encode_fns[] = {
  Opcode_rsr_ccompare0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_ccompare0_encode_fns[] = {
  Opcode_wsr_ccompare0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_ccompare0_encode_fns[] = {
  Opcode_xsr_ccompare0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_ccompare1_encode_fns[] = {
  Opcode_rsr_ccompare1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_ccompare1_encode_fns[] = {
  Opcode_wsr_ccompare1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_ccompare1_encode_fns[] = {
  Opcode_xsr_ccompare1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_ccompare2_encode_fns[] = {
  Opcode_rsr_ccompare2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_ccompare2_encode_fns[] = {
  Opcode_wsr_ccompare2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_ccompare2_encode_fns[] = {
  Opcode_xsr_ccompare2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ipf_encode_fns[] = {
  Opcode_ipf_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ihi_encode_fns[] = {
  Opcode_ihi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_iii_encode_fns[] = {
  Opcode_iii_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_lict_encode_fns[] = {
  Opcode_lict_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_licw_encode_fns[] = {
  Opcode_licw_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_sict_encode_fns[] = {
  Opcode_sict_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_sicw_encode_fns[] = {
  Opcode_sicw_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_dhwb_encode_fns[] = {
  Opcode_dhwb_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_dhwbi_encode_fns[] = {
  Opcode_dhwbi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_diwb_encode_fns[] = {
  Opcode_diwb_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_diwbi_encode_fns[] = {
  Opcode_diwbi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_dhi_encode_fns[] = {
  Opcode_dhi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_dii_encode_fns[] = {
  Opcode_dii_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_dpfr_encode_fns[] = {
  Opcode_dpfr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_dpfw_encode_fns[] = {
  Opcode_dpfw_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_dpfro_encode_fns[] = {
  Opcode_dpfro_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_dpfwo_encode_fns[] = {
  Opcode_dpfwo_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_sdct_encode_fns[] = {
  Opcode_sdct_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ldct_encode_fns[] = {
  Opcode_ldct_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_ptevaddr_encode_fns[] = {
  Opcode_wsr_ptevaddr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_ptevaddr_encode_fns[] = {
  Opcode_rsr_ptevaddr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_ptevaddr_encode_fns[] = {
  Opcode_xsr_ptevaddr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_rasid_encode_fns[] = {
  Opcode_rsr_rasid_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_rasid_encode_fns[] = {
  Opcode_wsr_rasid_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_rasid_encode_fns[] = {
  Opcode_xsr_rasid_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_itlbcfg_encode_fns[] = {
  Opcode_rsr_itlbcfg_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_itlbcfg_encode_fns[] = {
  Opcode_wsr_itlbcfg_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_itlbcfg_encode_fns[] = {
  Opcode_xsr_itlbcfg_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_dtlbcfg_encode_fns[] = {
  Opcode_rsr_dtlbcfg_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_dtlbcfg_encode_fns[] = {
  Opcode_wsr_dtlbcfg_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_dtlbcfg_encode_fns[] = {
  Opcode_xsr_dtlbcfg_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_idtlb_encode_fns[] = {
  Opcode_idtlb_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_pdtlb_encode_fns[] = {
  Opcode_pdtlb_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rdtlb0_encode_fns[] = {
  Opcode_rdtlb0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rdtlb1_encode_fns[] = {
  Opcode_rdtlb1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wdtlb_encode_fns[] = {
  Opcode_wdtlb_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_iitlb_encode_fns[] = {
  Opcode_iitlb_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_pitlb_encode_fns[] = {
  Opcode_pitlb_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ritlb0_encode_fns[] = {
  Opcode_ritlb0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ritlb1_encode_fns[] = {
  Opcode_ritlb1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_witlb_encode_fns[] = {
  Opcode_witlb_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ldpte_encode_fns[] = {
  Opcode_ldpte_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_hwwitlba_encode_fns[] = {
  Opcode_hwwitlba_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_hwwdtlba_encode_fns[] = {
  Opcode_hwwdtlba_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_nsa_encode_fns[] = {
  Opcode_nsa_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_nsau_encode_fns[] = {
  Opcode_nsau_Slot_inst_encode, 0, 0
};



static xtensa_opcode_internal opcodes[] = {
  { "excw", 0 ,
    0,
    Opcode_excw_encode_fns, 0, 0 },
  { "rfe", 1 ,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_rfe_encode_fns, 0, 0 },
  { "rfde", 2 ,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_rfde_encode_fns, 0, 0 },
  { "syscall", 3 ,
    0,
    Opcode_syscall_encode_fns, 0, 0 },
  { "simcall", 4 ,
    0,
    Opcode_simcall_encode_fns, 0, 0 },
  { "call12", 5 ,
    XTENSA_OPCODE_IS_CALL,
    Opcode_call12_encode_fns, 0, 0 },
  { "call8", 6 ,
    XTENSA_OPCODE_IS_CALL,
    Opcode_call8_encode_fns, 0, 0 },
  { "call4", 7 ,
    XTENSA_OPCODE_IS_CALL,
    Opcode_call4_encode_fns, 0, 0 },
  { "callx12", 8 ,
    XTENSA_OPCODE_IS_CALL,
    Opcode_callx12_encode_fns, 0, 0 },
  { "callx8", 9 ,
    XTENSA_OPCODE_IS_CALL,
    Opcode_callx8_encode_fns, 0, 0 },
  { "callx4", 10 ,
    XTENSA_OPCODE_IS_CALL,
    Opcode_callx4_encode_fns, 0, 0 },
  { "entry", 11 ,
    0,
    Opcode_entry_encode_fns, 0, 0 },
  { "movsp", 12 ,
    0,
    Opcode_movsp_encode_fns, 0, 0 },
  { "rotw", 13 ,
    0,
    Opcode_rotw_encode_fns, 0, 0 },
  { "retw", 14 ,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_retw_encode_fns, 0, 0 },
  { "retw.n", 14 ,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_retw_n_encode_fns, 0, 0 },
  { "rfwo", 15 ,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_rfwo_encode_fns, 0, 0 },
  { "rfwu", 15 ,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_rfwu_encode_fns, 0, 0 },
  { "l32e", 16 ,
    0,
    Opcode_l32e_encode_fns, 0, 0 },
  { "s32e", 17 ,
    0,
    Opcode_s32e_encode_fns, 0, 0 },
  { "rsr.windowbase", 18 ,
    0,
    Opcode_rsr_windowbase_encode_fns, 0, 0 },
  { "wsr.windowbase", 19 ,
    0,
    Opcode_wsr_windowbase_encode_fns, 0, 0 },
  { "xsr.windowbase", 20 ,
    0,
    Opcode_xsr_windowbase_encode_fns, 0, 0 },
  { "rsr.windowstart", 21 ,
    0,
    Opcode_rsr_windowstart_encode_fns, 0, 0 },
  { "wsr.windowstart", 22 ,
    0,
    Opcode_wsr_windowstart_encode_fns, 0, 0 },
  { "xsr.windowstart", 23 ,
    0,
    Opcode_xsr_windowstart_encode_fns, 0, 0 },
  { "add.n", 24 ,
    0,
    Opcode_add_n_encode_fns, 0, 0 },
  { "addi.n", 25 ,
    0,
    Opcode_addi_n_encode_fns, 0, 0 },
  { "beqz.n", 26 ,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_beqz_n_encode_fns, 0, 0 },
  { "bnez.n", 26 ,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bnez_n_encode_fns, 0, 0 },
  { "ill.n", 27 ,
    0,
    Opcode_ill_n_encode_fns, 0, 0 },
  { "l32i.n", 28 ,
    0,
    Opcode_l32i_n_encode_fns, 0, 0 },
  { "mov.n", 29 ,
    0,
    Opcode_mov_n_encode_fns, 0, 0 },
  { "movi.n", 30 ,
    0,
    Opcode_movi_n_encode_fns, 0, 0 },
  { "nop.n", 31 ,
    0,
    Opcode_nop_n_encode_fns, 0, 0 },
  { "ret.n", 32 ,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_ret_n_encode_fns, 0, 0 },
  { "s32i.n", 33 ,
    0,
    Opcode_s32i_n_encode_fns, 0, 0 },
  { "addi", 34 ,
    0,
    Opcode_addi_encode_fns, 0, 0 },
  { "addmi", 35 ,
    0,
    Opcode_addmi_encode_fns, 0, 0 },
  { "add", 36 ,
    0,
    Opcode_add_encode_fns, 0, 0 },
  { "sub", 36 ,
    0,
    Opcode_sub_encode_fns, 0, 0 },
  { "addx2", 36 ,
    0,
    Opcode_addx2_encode_fns, 0, 0 },
  { "addx4", 36 ,
    0,
    Opcode_addx4_encode_fns, 0, 0 },
  { "addx8", 36 ,
    0,
    Opcode_addx8_encode_fns, 0, 0 },
  { "subx2", 36 ,
    0,
    Opcode_subx2_encode_fns, 0, 0 },
  { "subx4", 36 ,
    0,
    Opcode_subx4_encode_fns, 0, 0 },
  { "subx8", 36 ,
    0,
    Opcode_subx8_encode_fns, 0, 0 },
  { "and", 37 ,
    0,
    Opcode_and_encode_fns, 0, 0 },
  { "or", 37 ,
    0,
    Opcode_or_encode_fns, 0, 0 },
  { "xor", 37 ,
    0,
    Opcode_xor_encode_fns, 0, 0 },
  { "beqi", 38 ,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_beqi_encode_fns, 0, 0 },
  { "bnei", 38 ,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bnei_encode_fns, 0, 0 },
  { "bgei", 38 ,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bgei_encode_fns, 0, 0 },
  { "blti", 38 ,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_blti_encode_fns, 0, 0 },
  { "bbci", 39 ,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bbci_encode_fns, 0, 0 },
  { "bbsi", 39 ,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bbsi_encode_fns, 0, 0 },
  { "bgeui", 40 ,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bgeui_encode_fns, 0, 0 },
  { "bltui", 40 ,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bltui_encode_fns, 0, 0 },
  { "beq", 41 ,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_beq_encode_fns, 0, 0 },
  { "bne", 41 ,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bne_encode_fns, 0, 0 },
  { "bge", 41 ,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bge_encode_fns, 0, 0 },
  { "blt", 41 ,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_blt_encode_fns, 0, 0 },
  { "bgeu", 41 ,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bgeu_encode_fns, 0, 0 },
  { "bltu", 41 ,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bltu_encode_fns, 0, 0 },
  { "bany", 41 ,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bany_encode_fns, 0, 0 },
  { "bnone", 41 ,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bnone_encode_fns, 0, 0 },
  { "ball", 41 ,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_ball_encode_fns, 0, 0 },
  { "bnall", 41 ,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bnall_encode_fns, 0, 0 },
  { "bbc", 41 ,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bbc_encode_fns, 0, 0 },
  { "bbs", 41 ,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bbs_encode_fns, 0, 0 },
  { "beqz", 42 ,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_beqz_encode_fns, 0, 0 },
  { "bnez", 42 ,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bnez_encode_fns, 0, 0 },
  { "bgez", 42 ,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bgez_encode_fns, 0, 0 },
  { "bltz", 42 ,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bltz_encode_fns, 0, 0 },
  { "call0", 43 ,
    XTENSA_OPCODE_IS_CALL,
    Opcode_call0_encode_fns, 0, 0 },
  { "callx0", 44 ,
    XTENSA_OPCODE_IS_CALL,
    Opcode_callx0_encode_fns, 0, 0 },
  { "extui", 45 ,
    0,
    Opcode_extui_encode_fns, 0, 0 },
  { "ill", 46 ,
    0,
    Opcode_ill_encode_fns, 0, 0 },
  { "j", 47 ,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_j_encode_fns, 0, 0 },
  { "jx", 48 ,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_jx_encode_fns, 0, 0 },
  { "l16ui", 49 ,
    0,
    Opcode_l16ui_encode_fns, 0, 0 },
  { "l16si", 50 ,
    0,
    Opcode_l16si_encode_fns, 0, 0 },
  { "l32i", 51 ,
    0,
    Opcode_l32i_encode_fns, 0, 0 },
  { "l32r", 52 ,
    0,
    Opcode_l32r_encode_fns, 0, 0 },
  { "l8ui", 53 ,
    0,
    Opcode_l8ui_encode_fns, 0, 0 },
  { "loop", 54 ,
    XTENSA_OPCODE_IS_LOOP,
    Opcode_loop_encode_fns, 0, 0 },
  { "loopnez", 55 ,
    XTENSA_OPCODE_IS_LOOP,
    Opcode_loopnez_encode_fns, 0, 0 },
  { "loopgtz", 55 ,
    XTENSA_OPCODE_IS_LOOP,
    Opcode_loopgtz_encode_fns, 0, 0 },
  { "movi", 56 ,
    0,
    Opcode_movi_encode_fns, 0, 0 },
  { "moveqz", 57 ,
    0,
    Opcode_moveqz_encode_fns, 0, 0 },
  { "movnez", 57 ,
    0,
    Opcode_movnez_encode_fns, 0, 0 },
  { "movltz", 57 ,
    0,
    Opcode_movltz_encode_fns, 0, 0 },
  { "movgez", 57 ,
    0,
    Opcode_movgez_encode_fns, 0, 0 },
  { "neg", 58 ,
    0,
    Opcode_neg_encode_fns, 0, 0 },
  { "abs", 58 ,
    0,
    Opcode_abs_encode_fns, 0, 0 },
  { "nop", 59 ,
    0,
    Opcode_nop_encode_fns, 0, 0 },
  { "ret", 60 ,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_ret_encode_fns, 0, 0 },
  { "s16i", 61 ,
    0,
    Opcode_s16i_encode_fns, 0, 0 },
  { "s32i", 62 ,
    0,
    Opcode_s32i_encode_fns, 0, 0 },
  { "s8i", 63 ,
    0,
    Opcode_s8i_encode_fns, 0, 0 },
  { "ssr", 64 ,
    0,
    Opcode_ssr_encode_fns, 0, 0 },
  { "ssl", 64 ,
    0,
    Opcode_ssl_encode_fns, 0, 0 },
  { "ssa8l", 64 ,
    0,
    Opcode_ssa8l_encode_fns, 0, 0 },
  { "ssa8b", 64 ,
    0,
    Opcode_ssa8b_encode_fns, 0, 0 },
  { "ssai", 65 ,
    0,
    Opcode_ssai_encode_fns, 0, 0 },
  { "sll", 66 ,
    0,
    Opcode_sll_encode_fns, 0, 0 },
  { "src", 67 ,
    0,
    Opcode_src_encode_fns, 0, 0 },
  { "srl", 68 ,
    0,
    Opcode_srl_encode_fns, 0, 0 },
  { "sra", 68 ,
    0,
    Opcode_sra_encode_fns, 0, 0 },
  { "slli", 69 ,
    0,
    Opcode_slli_encode_fns, 0, 0 },
  { "srai", 70 ,
    0,
    Opcode_srai_encode_fns, 0, 0 },
  { "srli", 71 ,
    0,
    Opcode_srli_encode_fns, 0, 0 },
  { "memw", 72 ,
    0,
    Opcode_memw_encode_fns, 0, 0 },
  { "extw", 73 ,
    0,
    Opcode_extw_encode_fns, 0, 0 },
  { "isync", 74 ,
    0,
    Opcode_isync_encode_fns, 0, 0 },
  { "rsync", 75 ,
    0,
    Opcode_rsync_encode_fns, 0, 0 },
  { "esync", 75 ,
    0,
    Opcode_esync_encode_fns, 0, 0 },
  { "dsync", 75 ,
    0,
    Opcode_dsync_encode_fns, 0, 0 },
  { "rsil", 76 ,
    0,
    Opcode_rsil_encode_fns, 0, 0 },
  { "rsr.lend", 77 ,
    0,
    Opcode_rsr_lend_encode_fns, 0, 0 },
  { "wsr.lend", 78 ,
    0,
    Opcode_wsr_lend_encode_fns, 0, 0 },
  { "xsr.lend", 79 ,
    0,
    Opcode_xsr_lend_encode_fns, 0, 0 },
  { "rsr.lcount", 80 ,
    0,
    Opcode_rsr_lcount_encode_fns, 0, 0 },
  { "wsr.lcount", 81 ,
    0,
    Opcode_wsr_lcount_encode_fns, 0, 0 },
  { "xsr.lcount", 82 ,
    0,
    Opcode_xsr_lcount_encode_fns, 0, 0 },
  { "rsr.lbeg", 83 ,
    0,
    Opcode_rsr_lbeg_encode_fns, 0, 0 },
  { "wsr.lbeg", 84 ,
    0,
    Opcode_wsr_lbeg_encode_fns, 0, 0 },
  { "xsr.lbeg", 85 ,
    0,
    Opcode_xsr_lbeg_encode_fns, 0, 0 },
  { "rsr.sar", 86 ,
    0,
    Opcode_rsr_sar_encode_fns, 0, 0 },
  { "wsr.sar", 87 ,
    0,
    Opcode_wsr_sar_encode_fns, 0, 0 },
  { "xsr.sar", 88 ,
    0,
    Opcode_xsr_sar_encode_fns, 0, 0 },
  { "rsr.litbase", 89 ,
    0,
    Opcode_rsr_litbase_encode_fns, 0, 0 },
  { "wsr.litbase", 90 ,
    0,
    Opcode_wsr_litbase_encode_fns, 0, 0 },
  { "xsr.litbase", 91 ,
    0,
    Opcode_xsr_litbase_encode_fns, 0, 0 },
  { "rsr.176", 92 ,
    0,
    Opcode_rsr_176_encode_fns, 0, 0 },
  { "rsr.208", 93 ,
    0,
    Opcode_rsr_208_encode_fns, 0, 0 },
  { "rsr.ps", 94 ,
    0,
    Opcode_rsr_ps_encode_fns, 0, 0 },
  { "wsr.ps", 95 ,
    0,
    Opcode_wsr_ps_encode_fns, 0, 0 },
  { "xsr.ps", 96 ,
    0,
    Opcode_xsr_ps_encode_fns, 0, 0 },
  { "rsr.epc1", 97 ,
    0,
    Opcode_rsr_epc1_encode_fns, 0, 0 },
  { "wsr.epc1", 98 ,
    0,
    Opcode_wsr_epc1_encode_fns, 0, 0 },
  { "xsr.epc1", 99 ,
    0,
    Opcode_xsr_epc1_encode_fns, 0, 0 },
  { "rsr.excsave1", 100 ,
    0,
    Opcode_rsr_excsave1_encode_fns, 0, 0 },
  { "wsr.excsave1", 101 ,
    0,
    Opcode_wsr_excsave1_encode_fns, 0, 0 },
  { "xsr.excsave1", 102 ,
    0,
    Opcode_xsr_excsave1_encode_fns, 0, 0 },
  { "rsr.epc2", 103 ,
    0,
    Opcode_rsr_epc2_encode_fns, 0, 0 },
  { "wsr.epc2", 104 ,
    0,
    Opcode_wsr_epc2_encode_fns, 0, 0 },
  { "xsr.epc2", 105 ,
    0,
    Opcode_xsr_epc2_encode_fns, 0, 0 },
  { "rsr.excsave2", 106 ,
    0,
    Opcode_rsr_excsave2_encode_fns, 0, 0 },
  { "wsr.excsave2", 107 ,
    0,
    Opcode_wsr_excsave2_encode_fns, 0, 0 },
  { "xsr.excsave2", 108 ,
    0,
    Opcode_xsr_excsave2_encode_fns, 0, 0 },
  { "rsr.epc3", 109 ,
    0,
    Opcode_rsr_epc3_encode_fns, 0, 0 },
  { "wsr.epc3", 110 ,
    0,
    Opcode_wsr_epc3_encode_fns, 0, 0 },
  { "xsr.epc3", 111 ,
    0,
    Opcode_xsr_epc3_encode_fns, 0, 0 },
  { "rsr.excsave3", 112 ,
    0,
    Opcode_rsr_excsave3_encode_fns, 0, 0 },
  { "wsr.excsave3", 113 ,
    0,
    Opcode_wsr_excsave3_encode_fns, 0, 0 },
  { "xsr.excsave3", 114 ,
    0,
    Opcode_xsr_excsave3_encode_fns, 0, 0 },
  { "rsr.epc4", 115 ,
    0,
    Opcode_rsr_epc4_encode_fns, 0, 0 },
  { "wsr.epc4", 116 ,
    0,
    Opcode_wsr_epc4_encode_fns, 0, 0 },
  { "xsr.epc4", 117 ,
    0,
    Opcode_xsr_epc4_encode_fns, 0, 0 },
  { "rsr.excsave4", 118 ,
    0,
    Opcode_rsr_excsave4_encode_fns, 0, 0 },
  { "wsr.excsave4", 119 ,
    0,
    Opcode_wsr_excsave4_encode_fns, 0, 0 },
  { "xsr.excsave4", 120 ,
    0,
    Opcode_xsr_excsave4_encode_fns, 0, 0 },
  { "rsr.eps2", 121 ,
    0,
    Opcode_rsr_eps2_encode_fns, 0, 0 },
  { "wsr.eps2", 122 ,
    0,
    Opcode_wsr_eps2_encode_fns, 0, 0 },
  { "xsr.eps2", 123 ,
    0,
    Opcode_xsr_eps2_encode_fns, 0, 0 },
  { "rsr.eps3", 124 ,
    0,
    Opcode_rsr_eps3_encode_fns, 0, 0 },
  { "wsr.eps3", 125 ,
    0,
    Opcode_wsr_eps3_encode_fns, 0, 0 },
  { "xsr.eps3", 126 ,
    0,
    Opcode_xsr_eps3_encode_fns, 0, 0 },
  { "rsr.eps4", 127 ,
    0,
    Opcode_rsr_eps4_encode_fns, 0, 0 },
  { "wsr.eps4", 128 ,
    0,
    Opcode_wsr_eps4_encode_fns, 0, 0 },
  { "xsr.eps4", 129 ,
    0,
    Opcode_xsr_eps4_encode_fns, 0, 0 },
  { "rsr.excvaddr", 130 ,
    0,
    Opcode_rsr_excvaddr_encode_fns, 0, 0 },
  { "wsr.excvaddr", 131 ,
    0,
    Opcode_wsr_excvaddr_encode_fns, 0, 0 },
  { "xsr.excvaddr", 132 ,
    0,
    Opcode_xsr_excvaddr_encode_fns, 0, 0 },
  { "rsr.depc", 133 ,
    0,
    Opcode_rsr_depc_encode_fns, 0, 0 },
  { "wsr.depc", 134 ,
    0,
    Opcode_wsr_depc_encode_fns, 0, 0 },
  { "xsr.depc", 135 ,
    0,
    Opcode_xsr_depc_encode_fns, 0, 0 },
  { "rsr.exccause", 136 ,
    0,
    Opcode_rsr_exccause_encode_fns, 0, 0 },
  { "wsr.exccause", 137 ,
    0,
    Opcode_wsr_exccause_encode_fns, 0, 0 },
  { "xsr.exccause", 138 ,
    0,
    Opcode_xsr_exccause_encode_fns, 0, 0 },
  { "rsr.misc0", 139 ,
    0,
    Opcode_rsr_misc0_encode_fns, 0, 0 },
  { "wsr.misc0", 140 ,
    0,
    Opcode_wsr_misc0_encode_fns, 0, 0 },
  { "xsr.misc0", 141 ,
    0,
    Opcode_xsr_misc0_encode_fns, 0, 0 },
  { "rsr.misc1", 142 ,
    0,
    Opcode_rsr_misc1_encode_fns, 0, 0 },
  { "wsr.misc1", 143 ,
    0,
    Opcode_wsr_misc1_encode_fns, 0, 0 },
  { "xsr.misc1", 144 ,
    0,
    Opcode_xsr_misc1_encode_fns, 0, 0 },
  { "rsr.prid", 145 ,
    0,
    Opcode_rsr_prid_encode_fns, 0, 0 },
  { "rfi", 146 ,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_rfi_encode_fns, 0, 0 },
  { "waiti", 147 ,
    0,
    Opcode_waiti_encode_fns, 0, 0 },
  { "rsr.interrupt", 148 ,
    0,
    Opcode_rsr_interrupt_encode_fns, 0, 0 },
  { "wsr.intset", 149 ,
    0,
    Opcode_wsr_intset_encode_fns, 0, 0 },
  { "wsr.intclear", 150 ,
    0,
    Opcode_wsr_intclear_encode_fns, 0, 0 },
  { "rsr.intenable", 151 ,
    0,
    Opcode_rsr_intenable_encode_fns, 0, 0 },
  { "wsr.intenable", 152 ,
    0,
    Opcode_wsr_intenable_encode_fns, 0, 0 },
  { "xsr.intenable", 153 ,
    0,
    Opcode_xsr_intenable_encode_fns, 0, 0 },
  { "break", 154 ,
    0,
    Opcode_break_encode_fns, 0, 0 },
  { "break.n", 155 ,
    0,
    Opcode_break_n_encode_fns, 0, 0 },
  { "rsr.dbreaka0", 156 ,
    0,
    Opcode_rsr_dbreaka0_encode_fns, 0, 0 },
  { "wsr.dbreaka0", 157 ,
    0,
    Opcode_wsr_dbreaka0_encode_fns, 0, 0 },
  { "xsr.dbreaka0", 158 ,
    0,
    Opcode_xsr_dbreaka0_encode_fns, 0, 0 },
  { "rsr.dbreakc0", 159 ,
    0,
    Opcode_rsr_dbreakc0_encode_fns, 0, 0 },
  { "wsr.dbreakc0", 160 ,
    0,
    Opcode_wsr_dbreakc0_encode_fns, 0, 0 },
  { "xsr.dbreakc0", 161 ,
    0,
    Opcode_xsr_dbreakc0_encode_fns, 0, 0 },
  { "rsr.dbreaka1", 162 ,
    0,
    Opcode_rsr_dbreaka1_encode_fns, 0, 0 },
  { "wsr.dbreaka1", 163 ,
    0,
    Opcode_wsr_dbreaka1_encode_fns, 0, 0 },
  { "xsr.dbreaka1", 164 ,
    0,
    Opcode_xsr_dbreaka1_encode_fns, 0, 0 },
  { "rsr.dbreakc1", 165 ,
    0,
    Opcode_rsr_dbreakc1_encode_fns, 0, 0 },
  { "wsr.dbreakc1", 166 ,
    0,
    Opcode_wsr_dbreakc1_encode_fns, 0, 0 },
  { "xsr.dbreakc1", 167 ,
    0,
    Opcode_xsr_dbreakc1_encode_fns, 0, 0 },
  { "rsr.ibreaka0", 168 ,
    0,
    Opcode_rsr_ibreaka0_encode_fns, 0, 0 },
  { "wsr.ibreaka0", 169 ,
    0,
    Opcode_wsr_ibreaka0_encode_fns, 0, 0 },
  { "xsr.ibreaka0", 170 ,
    0,
    Opcode_xsr_ibreaka0_encode_fns, 0, 0 },
  { "rsr.ibreaka1", 171 ,
    0,
    Opcode_rsr_ibreaka1_encode_fns, 0, 0 },
  { "wsr.ibreaka1", 172 ,
    0,
    Opcode_wsr_ibreaka1_encode_fns, 0, 0 },
  { "xsr.ibreaka1", 173 ,
    0,
    Opcode_xsr_ibreaka1_encode_fns, 0, 0 },
  { "rsr.ibreakenable", 174 ,
    0,
    Opcode_rsr_ibreakenable_encode_fns, 0, 0 },
  { "wsr.ibreakenable", 175 ,
    0,
    Opcode_wsr_ibreakenable_encode_fns, 0, 0 },
  { "xsr.ibreakenable", 176 ,
    0,
    Opcode_xsr_ibreakenable_encode_fns, 0, 0 },
  { "rsr.debugcause", 177 ,
    0,
    Opcode_rsr_debugcause_encode_fns, 0, 0 },
  { "wsr.debugcause", 178 ,
    0,
    Opcode_wsr_debugcause_encode_fns, 0, 0 },
  { "xsr.debugcause", 179 ,
    0,
    Opcode_xsr_debugcause_encode_fns, 0, 0 },
  { "rsr.icount", 180 ,
    0,
    Opcode_rsr_icount_encode_fns, 0, 0 },
  { "wsr.icount", 181 ,
    0,
    Opcode_wsr_icount_encode_fns, 0, 0 },
  { "xsr.icount", 182 ,
    0,
    Opcode_xsr_icount_encode_fns, 0, 0 },
  { "rsr.icountlevel", 183 ,
    0,
    Opcode_rsr_icountlevel_encode_fns, 0, 0 },
  { "wsr.icountlevel", 184 ,
    0,
    Opcode_wsr_icountlevel_encode_fns, 0, 0 },
  { "xsr.icountlevel", 185 ,
    0,
    Opcode_xsr_icountlevel_encode_fns, 0, 0 },
  { "rsr.ddr", 186 ,
    0,
    Opcode_rsr_ddr_encode_fns, 0, 0 },
  { "wsr.ddr", 187 ,
    0,
    Opcode_wsr_ddr_encode_fns, 0, 0 },
  { "xsr.ddr", 188 ,
    0,
    Opcode_xsr_ddr_encode_fns, 0, 0 },
  { "rfdo", 189 ,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_rfdo_encode_fns, 0, 0 },
  { "rfdd", 190 ,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_rfdd_encode_fns, 0, 0 },
  { "rsr.ccount", 191 ,
    0,
    Opcode_rsr_ccount_encode_fns, 0, 0 },
  { "wsr.ccount", 192 ,
    0,
    Opcode_wsr_ccount_encode_fns, 0, 0 },
  { "xsr.ccount", 193 ,
    0,
    Opcode_xsr_ccount_encode_fns, 0, 0 },
  { "rsr.ccompare0", 194 ,
    0,
    Opcode_rsr_ccompare0_encode_fns, 0, 0 },
  { "wsr.ccompare0", 195 ,
    0,
    Opcode_wsr_ccompare0_encode_fns, 0, 0 },
  { "xsr.ccompare0", 196 ,
    0,
    Opcode_xsr_ccompare0_encode_fns, 0, 0 },
  { "rsr.ccompare1", 197 ,
    0,
    Opcode_rsr_ccompare1_encode_fns, 0, 0 },
  { "wsr.ccompare1", 198 ,
    0,
    Opcode_wsr_ccompare1_encode_fns, 0, 0 },
  { "xsr.ccompare1", 199 ,
    0,
    Opcode_xsr_ccompare1_encode_fns, 0, 0 },
  { "rsr.ccompare2", 200 ,
    0,
    Opcode_rsr_ccompare2_encode_fns, 0, 0 },
  { "wsr.ccompare2", 201 ,
    0,
    Opcode_wsr_ccompare2_encode_fns, 0, 0 },
  { "xsr.ccompare2", 202 ,
    0,
    Opcode_xsr_ccompare2_encode_fns, 0, 0 },
  { "ipf", 203 ,
    0,
    Opcode_ipf_encode_fns, 0, 0 },
  { "ihi", 203 ,
    0,
    Opcode_ihi_encode_fns, 0, 0 },
  { "iii", 204 ,
    0,
    Opcode_iii_encode_fns, 0, 0 },
  { "lict", 205 ,
    0,
    Opcode_lict_encode_fns, 0, 0 },
  { "licw", 205 ,
    0,
    Opcode_licw_encode_fns, 0, 0 },
  { "sict", 206 ,
    0,
    Opcode_sict_encode_fns, 0, 0 },
  { "sicw", 206 ,
    0,
    Opcode_sicw_encode_fns, 0, 0 },
  { "dhwb", 207 ,
    0,
    Opcode_dhwb_encode_fns, 0, 0 },
  { "dhwbi", 207 ,
    0,
    Opcode_dhwbi_encode_fns, 0, 0 },
  { "diwb", 208 ,
    0,
    Opcode_diwb_encode_fns, 0, 0 },
  { "diwbi", 208 ,
    0,
    Opcode_diwbi_encode_fns, 0, 0 },
  { "dhi", 209 ,
    0,
    Opcode_dhi_encode_fns, 0, 0 },
  { "dii", 209 ,
    0,
    Opcode_dii_encode_fns, 0, 0 },
  { "dpfr", 210 ,
    0,
    Opcode_dpfr_encode_fns, 0, 0 },
  { "dpfw", 210 ,
    0,
    Opcode_dpfw_encode_fns, 0, 0 },
  { "dpfro", 210 ,
    0,
    Opcode_dpfro_encode_fns, 0, 0 },
  { "dpfwo", 210 ,
    0,
    Opcode_dpfwo_encode_fns, 0, 0 },
  { "sdct", 211 ,
    0,
    Opcode_sdct_encode_fns, 0, 0 },
  { "ldct", 212 ,
    0,
    Opcode_ldct_encode_fns, 0, 0 },
  { "wsr.ptevaddr", 213 ,
    0,
    Opcode_wsr_ptevaddr_encode_fns, 0, 0 },
  { "rsr.ptevaddr", 214 ,
    0,
    Opcode_rsr_ptevaddr_encode_fns, 0, 0 },
  { "xsr.ptevaddr", 215 ,
    0,
    Opcode_xsr_ptevaddr_encode_fns, 0, 0 },
  { "rsr.rasid", 216 ,
    0,
    Opcode_rsr_rasid_encode_fns, 0, 0 },
  { "wsr.rasid", 217 ,
    0,
    Opcode_wsr_rasid_encode_fns, 0, 0 },
  { "xsr.rasid", 218 ,
    0,
    Opcode_xsr_rasid_encode_fns, 0, 0 },
  { "rsr.itlbcfg", 219 ,
    0,
    Opcode_rsr_itlbcfg_encode_fns, 0, 0 },
  { "wsr.itlbcfg", 220 ,
    0,
    Opcode_wsr_itlbcfg_encode_fns, 0, 0 },
  { "xsr.itlbcfg", 221 ,
    0,
    Opcode_xsr_itlbcfg_encode_fns, 0, 0 },
  { "rsr.dtlbcfg", 222 ,
    0,
    Opcode_rsr_dtlbcfg_encode_fns, 0, 0 },
  { "wsr.dtlbcfg", 223 ,
    0,
    Opcode_wsr_dtlbcfg_encode_fns, 0, 0 },
  { "xsr.dtlbcfg", 224 ,
    0,
    Opcode_xsr_dtlbcfg_encode_fns, 0, 0 },
  { "idtlb", 225 ,
    0,
    Opcode_idtlb_encode_fns, 0, 0 },
  { "pdtlb", 226 ,
    0,
    Opcode_pdtlb_encode_fns, 0, 0 },
  { "rdtlb0", 226 ,
    0,
    Opcode_rdtlb0_encode_fns, 0, 0 },
  { "rdtlb1", 226 ,
    0,
    Opcode_rdtlb1_encode_fns, 0, 0 },
  { "wdtlb", 227 ,
    0,
    Opcode_wdtlb_encode_fns, 0, 0 },
  { "iitlb", 228 ,
    0,
    Opcode_iitlb_encode_fns, 0, 0 },
  { "pitlb", 229 ,
    0,
    Opcode_pitlb_encode_fns, 0, 0 },
  { "ritlb0", 229 ,
    0,
    Opcode_ritlb0_encode_fns, 0, 0 },
  { "ritlb1", 229 ,
    0,
    Opcode_ritlb1_encode_fns, 0, 0 },
  { "witlb", 230 ,
    0,
    Opcode_witlb_encode_fns, 0, 0 },
  { "ldpte", 231 ,
    0,
    Opcode_ldpte_encode_fns, 0, 0 },
  { "hwwitlba", 232 ,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_hwwitlba_encode_fns, 0, 0 },
  { "hwwdtlba", 233 ,
    0,
    Opcode_hwwdtlba_encode_fns, 0, 0 },
  { "nsa", 234 ,
    0,
    Opcode_nsa_encode_fns, 0, 0 },
  { "nsau", 234 ,
    0,
    Opcode_nsau_encode_fns, 0, 0 }
};



static int
Slot_inst_decode (const xtensa_insnbuf insn)
{
  switch (Field_op0_Slot_inst_get (insn))
    {
    case 0:
      switch (Field_op1_Slot_inst_get (insn))
	{
	case 0:
	  switch (Field_op2_Slot_inst_get (insn))
	    {
	    case 0:
	      switch (Field_r_Slot_inst_get (insn))
		{
		case 0:
		  switch (Field_m_Slot_inst_get (insn))
		    {
		    case 0:
		      if (Field_s_Slot_inst_get (insn) == 0 &&
			  Field_n_Slot_inst_get (insn) == 0)
			return 77;
		      break;
		    case 2:
		      switch (Field_n_Slot_inst_get (insn))
			{
			case 0:
			  return 96;
			case 1:
			  return 14;
			case 2:
			  return 79;
			}
		      break;
		    case 3:
		      switch (Field_n_Slot_inst_get (insn))
			{
			case 0:
			  return 75;
			case 1:
			  return 10;
			case 2:
			  return 9;
			case 3:
			  return 8;
			}
		      break;
		    }
		  break;
		case 1:
		  return 12;
		case 2:
		  if (Field_s_Slot_inst_get (insn) == 0)
		    {
		      switch (Field_t_Slot_inst_get (insn))
			{
			case 0:
			  return 114;
			case 1:
			  return 115;
			case 2:
			  return 116;
			case 3:
			  return 117;
			case 8:
			  return 0;
			case 12:
			  return 112;
			case 13:
			  return 113;
			case 15:
			  return 95;
			}
		    }
		  break;
		case 3:
		  switch (Field_t_Slot_inst_get (insn))
		    {
		    case 0:
		      switch (Field_s_Slot_inst_get (insn))
			{
			case 0:
			  return 1;
			case 2:
			  return 2;
			case 4:
			  return 16;
			case 5:
			  return 17;
			}
		      break;
		    case 1:
		      return 188;
		    }
		  break;
		case 4:
		  return 196;
		case 5:
		  switch (Field_s_Slot_inst_get (insn))
		    {
		    case 0:
		      if (Field_t_Slot_inst_get (insn) == 0)
			return 3;
		      break;
		    case 1:
		      if (Field_t_Slot_inst_get (insn) == 0)
			return 4;
		      break;
		    }
		  break;
		case 6:
		  return 118;
		case 7:
		  if (Field_t_Slot_inst_get (insn) == 0)
		    return 189;
		  break;
		}
	      break;
	    case 1:
	      return 47;
	    case 2:
	      return 48;
	    case 3:
	      return 49;
	    case 4:
	      switch (Field_r_Slot_inst_get (insn))
		{
		case 0:
		  if (Field_t_Slot_inst_get (insn) == 0)
		    return 100;
		  break;
		case 1:
		  if (Field_t_Slot_inst_get (insn) == 0)
		    return 101;
		  break;
		case 2:
		  if (Field_t_Slot_inst_get (insn) == 0)
		    return 102;
		  break;
		case 3:
		  if (Field_t_Slot_inst_get (insn) == 0)
		    return 103;
		  break;
		case 4:
		  if (Field_thi3_Slot_inst_get (insn) == 0)
		    return 104;
		  break;
		case 8:
		  if (Field_s_Slot_inst_get (insn) == 0)
		    return 13;
		  break;
		case 14:
		  return 289;
		case 15:
		  return 290;
		}
	      break;
	    case 5:
	      switch (Field_r_Slot_inst_get (insn))
		{
		case 1:
		  return 287;
		case 3:
		  return 283;
		case 4:
		  if (Field_t_Slot_inst_get (insn) == 0)
		    return 281;
		  break;
		case 5:
		  return 282;
		case 6:
		  return 285;
		case 7:
		  return 284;
		case 9:
		  return 288;
		case 11:
		  return 278;
		case 12:
		  if (Field_t_Slot_inst_get (insn) == 0)
		    return 276;
		  break;
		case 13:
		  return 277;
		case 14:
		  return 280;
		case 15:
		  return 279;
		}
	      break;
	    case 6:
	      switch (Field_s_Slot_inst_get (insn))
		{
		case 0:
		  return 93;
		case 1:
		  return 94;
		}
	      break;
	    case 8:
	      return 39;
	    case 9:
	      return 41;
	    case 10:
	      return 42;
	    case 11:
	      return 43;
	    case 12:
	      return 40;
	    case 13:
	      return 44;
	    case 14:
	      return 45;
	    case 15:
	      return 46;
	    }
	  break;
	case 1:
	  switch (Field_op2_Slot_inst_get (insn))
	    {
	    case 0:
	    case 1:
	      return 109;
	    case 2:
	    case 3:
	      return 110;
	    case 4:
	      return 111;
	    case 6:
	      switch (Field_sr_Slot_inst_get (insn))
		{
		case 0:
		  return 127;
		case 1:
		  return 121;
		case 2:
		  return 124;
		case 3:
		  return 130;
		case 5:
		  return 133;
		case 72:
		  return 22;
		case 73:
		  return 25;
		case 83:
		  return 266;
		case 90:
		  return 269;
		case 91:
		  return 272;
		case 92:
		  return 275;
		case 96:
		  return 218;
		case 104:
		  return 230;
		case 128:
		  return 212;
		case 129:
		  return 215;
		case 144:
		  return 200;
		case 145:
		  return 206;
		case 160:
		  return 203;
		case 161:
		  return 209;
		case 177:
		  return 141;
		case 178:
		  return 147;
		case 179:
		  return 153;
		case 180:
		  return 159;
		case 192:
		  return 177;
		case 194:
		  return 165;
		case 195:
		  return 168;
		case 196:
		  return 171;
		case 209:
		  return 144;
		case 210:
		  return 150;
		case 211:
		  return 156;
		case 212:
		  return 162;
		case 228:
		  return 195;
		case 230:
		  return 138;
		case 232:
		  return 180;
		case 233:
		  return 221;
		case 234:
		  return 235;
		case 236:
		  return 224;
		case 237:
		  return 227;
		case 238:
		  return 174;
		case 240:
		  return 238;
		case 241:
		  return 241;
		case 242:
		  return 244;
		case 244:
		  return 183;
		case 245:
		  return 186;
		}
	      break;
	    case 8:
	      return 106;
	    case 9:
	      if (Field_s_Slot_inst_get (insn) == 0)
		return 107;
	      break;
	    case 10:
	      if (Field_t_Slot_inst_get (insn) == 0)
		return 105;
	      break;
	    case 11:
	      if (Field_s_Slot_inst_get (insn) == 0)
		return 108;
	      break;
	    case 15:
	      switch (Field_r_Slot_inst_get (insn))
		{
		case 0:
		  return 248;
		case 1:
		  return 250;
		case 2:
		  return 249;
		case 3:
		  return 251;
		case 8:
		  return 263;
		case 9:
		  return 262;
		case 14:
		  if (Field_t_Slot_inst_get (insn) == 0 &&
		      Field_s_Slot_inst_get (insn) == 0)
		    return 231;
		  if (Field_t_Slot_inst_get (insn) == 1 &&
		      Field_s_Slot_inst_get (insn) == 0)
		    return 232;
		  break;
		case 15:
		  return 286;
		}
	      break;
	    }
	  break;
	case 3:
	  switch (Field_op2_Slot_inst_get (insn))
	    {
	    case 0:
	      switch (Field_sr_Slot_inst_get (insn))
		{
		case 0:
		  return 125;
		case 1:
		  return 119;
		case 2:
		  return 122;
		case 3:
		  return 128;
		case 5:
		  return 131;
		case 72:
		  return 20;
		case 73:
		  return 23;
		case 83:
		  return 265;
		case 90:
		  return 267;
		case 91:
		  return 270;
		case 92:
		  return 273;
		case 96:
		  return 216;
		case 104:
		  return 228;
		case 128:
		  return 210;
		case 129:
		  return 213;
		case 144:
		  return 198;
		case 145:
		  return 204;
		case 160:
		  return 201;
		case 161:
		  return 207;
		case 176:
		  return 134;
		case 177:
		  return 139;
		case 178:
		  return 145;
		case 179:
		  return 151;
		case 180:
		  return 157;
		case 192:
		  return 175;
		case 194:
		  return 163;
		case 195:
		  return 166;
		case 196:
		  return 169;
		case 208:
		  return 135;
		case 209:
		  return 142;
		case 210:
		  return 148;
		case 211:
		  return 154;
		case 212:
		  return 160;
		case 226:
		  return 190;
		case 228:
		  return 193;
		case 230:
		  return 136;
		case 232:
		  return 178;
		case 233:
		  return 219;
		case 234:
		  return 233;
		case 235:
		  return 187;
		case 236:
		  return 222;
		case 237:
		  return 225;
		case 238:
		  return 172;
		case 240:
		  return 236;
		case 241:
		  return 239;
		case 242:
		  return 242;
		case 244:
		  return 181;
		case 245:
		  return 184;
		}
	      break;
	    case 1:
	      switch (Field_sr_Slot_inst_get (insn))
		{
		case 0:
		  return 126;
		case 1:
		  return 120;
		case 2:
		  return 123;
		case 3:
		  return 129;
		case 5:
		  return 132;
		case 72:
		  return 21;
		case 73:
		  return 24;
		case 83:
		  return 264;
		case 90:
		  return 268;
		case 91:
		  return 271;
		case 92:
		  return 274;
		case 96:
		  return 217;
		case 104:
		  return 229;
		case 128:
		  return 211;
		case 129:
		  return 214;
		case 144:
		  return 199;
		case 145:
		  return 205;
		case 160:
		  return 202;
		case 161:
		  return 208;
		case 177:
		  return 140;
		case 178:
		  return 146;
		case 179:
		  return 152;
		case 180:
		  return 158;
		case 192:
		  return 176;
		case 194:
		  return 164;
		case 195:
		  return 167;
		case 196:
		  return 170;
		case 209:
		  return 143;
		case 210:
		  return 149;
		case 211:
		  return 155;
		case 212:
		  return 161;
		case 226:
		  return 191;
		case 227:
		  return 192;
		case 228:
		  return 194;
		case 230:
		  return 137;
		case 232:
		  return 179;
		case 233:
		  return 220;
		case 234:
		  return 234;
		case 236:
		  return 223;
		case 237:
		  return 226;
		case 238:
		  return 173;
		case 240:
		  return 237;
		case 241:
		  return 240;
		case 242:
		  return 243;
		case 244:
		  return 182;
		case 245:
		  return 185;
		}
	      break;
	    case 8:
	      return 89;
	    case 9:
	      return 90;
	    case 10:
	      return 91;
	    case 11:
	      return 92;
	    }
	  break;
	case 4:
	case 5:
	  return 76;
	case 9:
	  switch (Field_op2_Slot_inst_get (insn))
	    {
	    case 0:
	      return 18;
	    case 4:
	      return 19;
	    }
	  break;
	}
      break;
    case 1:
      return 83;
    case 2:
      switch (Field_r_Slot_inst_get (insn))
	{
	case 0:
	  return 84;
	case 1:
	  return 80;
	case 2:
	  return 82;
	case 4:
	  return 99;
	case 5:
	  return 97;
	case 6:
	  return 98;
	case 7:
	  switch (Field_t_Slot_inst_get (insn))
	    {
	    case 0:
	      return 258;
	    case 1:
	      return 259;
	    case 2:
	      return 260;
	    case 3:
	      return 261;
	    case 4:
	      return 252;
	    case 5:
	      return 253;
	    case 6:
	      return 256;
	    case 7:
	      return 257;
	    case 8:
	      switch (Field_op1_Slot_inst_get (insn))
		{
		case 4:
		  return 254;
		case 5:
		  return 255;
		}
	      break;
	    case 12:
	      return 245;
	    case 14:
	      return 246;
	    case 15:
	      return 247;
	    }
	  break;
	case 9:
	  return 81;
	case 10:
	  return 88;
	case 12:
	  return 37;
	case 13:
	  return 38;
	}
      break;
    case 5:
      switch (Field_n_Slot_inst_get (insn))
	{
	case 0:
	  return 74;
	case 1:
	  return 7;
	case 2:
	  return 6;
	case 3:
	  return 5;
	}
      break;
    case 6:
      switch (Field_n_Slot_inst_get (insn))
	{
	case 0:
	  return 78;
	case 1:
	  switch (Field_m_Slot_inst_get (insn))
	    {
	    case 0:
	      return 70;
	    case 1:
	      return 71;
	    case 2:
	      return 73;
	    case 3:
	      return 72;
	    }
	  break;
	case 2:
	  switch (Field_m_Slot_inst_get (insn))
	    {
	    case 0:
	      return 50;
	    case 1:
	      return 51;
	    case 2:
	      return 53;
	    case 3:
	      return 52;
	    }
	  break;
	case 3:
	  switch (Field_m_Slot_inst_get (insn))
	    {
	    case 0:
	      return 11;
	    case 1:
	      switch (Field_r_Slot_inst_get (insn))
		{
		case 8:
		  return 85;
		case 9:
		  return 86;
		case 10:
		  return 87;
		}
	      break;
	    case 2:
	      return 57;
	    case 3:
	      return 56;
	    }
	  break;
	}
      break;
    case 7:
      switch (Field_r_Slot_inst_get (insn))
	{
	case 0:
	  return 65;
	case 1:
	  return 58;
	case 2:
	  return 61;
	case 3:
	  return 63;
	case 4:
	  return 66;
	case 5:
	  return 68;
	case 6:
	case 7:
	  return 54;
	case 8:
	  return 64;
	case 9:
	  return 59;
	case 10:
	  return 60;
	case 11:
	  return 62;
	case 12:
	  return 67;
	case 13:
	  return 69;
	case 14:
	case 15:
	  return 55;
	}
      break;
    }
  return XTENSA_UNDEFINED;
}

static int
Slot_inst16b_decode (const xtensa_insnbuf insn)
{
  switch (Field_op0_Slot_inst16b_get (insn))
    {
    case 12:
      switch (Field_i_Slot_inst16b_get (insn))
	{
	case 0:
	  return 33;
	case 1:
	  switch (Field_z_Slot_inst16b_get (insn))
	    {
	    case 0:
	      return 28;
	    case 1:
	      return 29;
	    }
	  break;
	}
      break;
    case 13:
      switch (Field_r_Slot_inst16b_get (insn))
	{
	case 0:
	  return 32;
	case 15:
	  switch (Field_t_Slot_inst16b_get (insn))
	    {
	    case 0:
	      return 35;
	    case 1:
	      return 15;
	    case 2:
	      return 197;
	    case 3:
	      if (Field_s_Slot_inst16b_get (insn) == 0)
		return 34;
	      break;
	    case 6:
	      if (Field_s_Slot_inst16b_get (insn) == 0)
		return 30;
	      break;
	    }
	  break;
	}
      break;
    }
  return XTENSA_UNDEFINED;
}

static int
Slot_inst16a_decode (const xtensa_insnbuf insn)
{
  switch (Field_op0_Slot_inst16a_get (insn))
    {
    case 8:
      return 31;
    case 9:
      return 36;
    case 10:
      return 26;
    case 11:
      return 27;
    }
  return XTENSA_UNDEFINED;
}



static void
Slot_x24_Format_inst_0_get (const xtensa_insnbuf insn,
			    xtensa_insnbuf slotbuf)
{
  slotbuf[0] = (insn[0] & 0xffffff);
}

static void
Slot_x24_Format_inst_0_set (xtensa_insnbuf insn,
			    const xtensa_insnbuf slotbuf)
{
  insn[0] = (insn[0] & ~0xffffff) | (slotbuf[0] & 0xffffff);
}

static void
Slot_x16a_Format_inst16a_0_get (const xtensa_insnbuf insn,
				xtensa_insnbuf slotbuf)
{
  slotbuf[0] = ((insn[0] & 0xffff00) >> 8);
}

static void
Slot_x16a_Format_inst16a_0_set (xtensa_insnbuf insn,
				const xtensa_insnbuf slotbuf)
{
  insn[0] = (insn[0] & ~0xffff00) | ((slotbuf[0] & 0xffff) << 8);
}

static void
Slot_x16b_Format_inst16b_0_get (const xtensa_insnbuf insn,
				xtensa_insnbuf slotbuf)
{
  slotbuf[0] = ((insn[0] & 0xffff00) >> 8);
}

static void
Slot_x16b_Format_inst16b_0_set (xtensa_insnbuf insn,
				const xtensa_insnbuf slotbuf)
{
  insn[0] = (insn[0] & ~0xffff00) | ((slotbuf[0] & 0xffff) << 8);
}

static xtensa_get_field_fn
Slot_inst_get_field_fns[] = {
  Field_t_Slot_inst_get,
  Field_bbi4_Slot_inst_get,
  Field_bbi_Slot_inst_get,
  Field_imm12_Slot_inst_get,
  Field_imm8_Slot_inst_get,
  Field_s_Slot_inst_get,
  Field_imm12b_Slot_inst_get,
  Field_imm16_Slot_inst_get,
  Field_m_Slot_inst_get,
  Field_n_Slot_inst_get,
  Field_offset_Slot_inst_get,
  Field_op0_Slot_inst_get,
  Field_op1_Slot_inst_get,
  Field_op2_Slot_inst_get,
  Field_r_Slot_inst_get,
  Field_sa4_Slot_inst_get,
  Field_sae4_Slot_inst_get,
  Field_sae_Slot_inst_get,
  Field_sal_Slot_inst_get,
  Field_sargt_Slot_inst_get,
  Field_sas4_Slot_inst_get,
  Field_sas_Slot_inst_get,
  Field_sr_Slot_inst_get,
  Field_st_Slot_inst_get,
  Field_thi3_Slot_inst_get,
  Field_imm4_Slot_inst_get,
  Field_mn_Slot_inst_get,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  Implicit_Field_ar0_get,
  Implicit_Field_ar4_get,
  Implicit_Field_ar8_get,
  Implicit_Field_ar12_get
};

static xtensa_set_field_fn
Slot_inst_set_field_fns[] = {
  Field_t_Slot_inst_set,
  Field_bbi4_Slot_inst_set,
  Field_bbi_Slot_inst_set,
  Field_imm12_Slot_inst_set,
  Field_imm8_Slot_inst_set,
  Field_s_Slot_inst_set,
  Field_imm12b_Slot_inst_set,
  Field_imm16_Slot_inst_set,
  Field_m_Slot_inst_set,
  Field_n_Slot_inst_set,
  Field_offset_Slot_inst_set,
  Field_op0_Slot_inst_set,
  Field_op1_Slot_inst_set,
  Field_op2_Slot_inst_set,
  Field_r_Slot_inst_set,
  Field_sa4_Slot_inst_set,
  Field_sae4_Slot_inst_set,
  Field_sae_Slot_inst_set,
  Field_sal_Slot_inst_set,
  Field_sargt_Slot_inst_set,
  Field_sas4_Slot_inst_set,
  Field_sas_Slot_inst_set,
  Field_sr_Slot_inst_set,
  Field_st_Slot_inst_set,
  Field_thi3_Slot_inst_set,
  Field_imm4_Slot_inst_set,
  Field_mn_Slot_inst_set,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set
};

static xtensa_get_field_fn
Slot_inst16a_get_field_fns[] = {
  Field_t_Slot_inst16a_get,
  0,
  0,
  0,
  0,
  Field_s_Slot_inst16a_get,
  0,
  0,
  0,
  0,
  0,
  Field_op0_Slot_inst16a_get,
  0,
  0,
  Field_r_Slot_inst16a_get,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  Field_sr_Slot_inst16a_get,
  Field_st_Slot_inst16a_get,
  0,
  Field_imm4_Slot_inst16a_get,
  0,
  Field_i_Slot_inst16a_get,
  Field_imm6lo_Slot_inst16a_get,
  Field_imm6hi_Slot_inst16a_get,
  Field_imm7lo_Slot_inst16a_get,
  Field_imm7hi_Slot_inst16a_get,
  Field_z_Slot_inst16a_get,
  Field_imm6_Slot_inst16a_get,
  Field_imm7_Slot_inst16a_get,
  Implicit_Field_ar0_get,
  Implicit_Field_ar4_get,
  Implicit_Field_ar8_get,
  Implicit_Field_ar12_get
};

static xtensa_set_field_fn
Slot_inst16a_set_field_fns[] = {
  Field_t_Slot_inst16a_set,
  0,
  0,
  0,
  0,
  Field_s_Slot_inst16a_set,
  0,
  0,
  0,
  0,
  0,
  Field_op0_Slot_inst16a_set,
  0,
  0,
  Field_r_Slot_inst16a_set,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  Field_sr_Slot_inst16a_set,
  Field_st_Slot_inst16a_set,
  0,
  Field_imm4_Slot_inst16a_set,
  0,
  Field_i_Slot_inst16a_set,
  Field_imm6lo_Slot_inst16a_set,
  Field_imm6hi_Slot_inst16a_set,
  Field_imm7lo_Slot_inst16a_set,
  Field_imm7hi_Slot_inst16a_set,
  Field_z_Slot_inst16a_set,
  Field_imm6_Slot_inst16a_set,
  Field_imm7_Slot_inst16a_set,
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set
};

static xtensa_get_field_fn
Slot_inst16b_get_field_fns[] = {
  Field_t_Slot_inst16b_get,
  0,
  0,
  0,
  0,
  Field_s_Slot_inst16b_get,
  0,
  0,
  0,
  0,
  0,
  Field_op0_Slot_inst16b_get,
  0,
  0,
  Field_r_Slot_inst16b_get,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  Field_sr_Slot_inst16b_get,
  Field_st_Slot_inst16b_get,
  0,
  Field_imm4_Slot_inst16b_get,
  0,
  Field_i_Slot_inst16b_get,
  Field_imm6lo_Slot_inst16b_get,
  Field_imm6hi_Slot_inst16b_get,
  Field_imm7lo_Slot_inst16b_get,
  Field_imm7hi_Slot_inst16b_get,
  Field_z_Slot_inst16b_get,
  Field_imm6_Slot_inst16b_get,
  Field_imm7_Slot_inst16b_get,
  Implicit_Field_ar0_get,
  Implicit_Field_ar4_get,
  Implicit_Field_ar8_get,
  Implicit_Field_ar12_get
};

static xtensa_set_field_fn
Slot_inst16b_set_field_fns[] = {
  Field_t_Slot_inst16b_set,
  0,
  0,
  0,
  0,
  Field_s_Slot_inst16b_set,
  0,
  0,
  0,
  0,
  0,
  Field_op0_Slot_inst16b_set,
  0,
  0,
  Field_r_Slot_inst16b_set,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  Field_sr_Slot_inst16b_set,
  Field_st_Slot_inst16b_set,
  0,
  Field_imm4_Slot_inst16b_set,
  0,
  Field_i_Slot_inst16b_set,
  Field_imm6lo_Slot_inst16b_set,
  Field_imm6hi_Slot_inst16b_set,
  Field_imm7lo_Slot_inst16b_set,
  Field_imm7hi_Slot_inst16b_set,
  Field_z_Slot_inst16b_set,
  Field_imm6_Slot_inst16b_set,
  Field_imm7_Slot_inst16b_set,
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set
};

static xtensa_slot_internal slots[] = {
  { "Inst", "x24", 0,
    Slot_x24_Format_inst_0_get, Slot_x24_Format_inst_0_set,
    Slot_inst_get_field_fns, Slot_inst_set_field_fns,
    Slot_inst_decode, "nop" },
  { "Inst16a", "x16a", 0,
    Slot_x16a_Format_inst16a_0_get, Slot_x16a_Format_inst16a_0_set,
    Slot_inst16a_get_field_fns, Slot_inst16a_set_field_fns,
    Slot_inst16a_decode, "" },
  { "Inst16b", "x16b", 0,
    Slot_x16b_Format_inst16b_0_get, Slot_x16b_Format_inst16b_0_set,
    Slot_inst16b_get_field_fns, Slot_inst16b_set_field_fns,
    Slot_inst16b_decode, "nop.n" }
};



static void
Format_x24_encode (xtensa_insnbuf insn)
{
  insn[0] = 0;
}

static void
Format_x16a_encode (xtensa_insnbuf insn)
{
  insn[0] = 0x800000;
}

static void
Format_x16b_encode (xtensa_insnbuf insn)
{
  insn[0] = 0xc00000;
}

static int Format_x24_slots[] = { 0 };

static int Format_x16a_slots[] = { 1 };

static int Format_x16b_slots[] = { 2 };

static xtensa_format_internal formats[] = {
  { "x24", 3, Format_x24_encode, 1, Format_x24_slots },
  { "x16a", 2, Format_x16a_encode, 1, Format_x16a_slots },
  { "x16b", 2, Format_x16b_encode, 1, Format_x16b_slots }
};


static int
format_decoder (const xtensa_insnbuf insn)
{
  if ((insn[0] & 0x800000) == 0)
    return 0;
  if ((insn[0] & 0xc00000) == 0x800000)
    return 1;
  if ((insn[0] & 0xe00000) == 0xc00000)
    return 2;
  return -1;
}

static int length_table[16] = {
  3,
  3,
  3,
  3,
  3,
  3,
  3,
  3,
  2,
  2,
  2,
  2,
  2,
  2,
  -1,
  -1
};

static int
length_decoder (const unsigned char *insn)
{
  int op0 = (insn[0] >> 4) & 0xf;
  return length_table[op0];
}



xtensa_isa_internal xtensa_modules = {
  1 ,
  3 , 0,
  3, formats, format_decoder, length_decoder,
  3, slots,
  39 ,
  70, operands,
  235, iclasses,
  291, opcodes, 0,
  1, regfiles,
  NUM_STATES, states, 0,
  NUM_SYSREGS, sysregs, 0,
  { MAX_SPECIAL_REG, MAX_USER_REG }, { 0, 0 },
  0, interfaces, 0,
  0, funcUnits, 0
};
