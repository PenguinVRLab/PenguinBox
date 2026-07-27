/*
 * These files from binutils are concatenated:
 * include/opcode/sparc.h, opcodes/sparc-opc.c, opcodes/sparc-dis.c
 */

/* include/opcode/sparc.h */

/* Definitions for opcode table for the sparc.
   Copyright 1989, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 2000, 2002,
   2003, 2005 Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler, GDB, the GNU debugger, and
   the GNU Binutils.

   GAS/GDB is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS/GDB is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS or GDB; see the file COPYING. If not,
   see <http://www.gnu.org/licenses/>.  */

#include "qemu/osdep.h"
#include "disas/dis-asm.h"

enum sparc_opcode_arch_val
{
  SPARC_OPCODE_ARCH_V6 = 0,
  SPARC_OPCODE_ARCH_V7,
  SPARC_OPCODE_ARCH_V8,
  SPARC_OPCODE_ARCH_SPARCLET,
  SPARC_OPCODE_ARCH_SPARCLITE,
  SPARC_OPCODE_ARCH_V9,
  SPARC_OPCODE_ARCH_V9A,
  SPARC_OPCODE_ARCH_V9B,
  SPARC_OPCODE_ARCH_BAD
};

#define SPARC_OPCODE_ARCH_MAX (SPARC_OPCODE_ARCH_BAD - 1)

#define SPARC_OPCODE_ARCH_MASK(arch) (1 << (arch))

#define SPARC_OPCODE_ARCH_V9_P(arch) ((arch) >= SPARC_OPCODE_ARCH_V9)

typedef struct sparc_opcode_arch
{
  const char *name;
  short supported;
} sparc_opcode_arch;

typedef struct sparc_opcode
{
  const char *name;
  unsigned long match;
  unsigned long lose;
  const char *args;
  char flags;
  short architecture;
} sparc_opcode;

#define F_DELAYED       1
#define F_ALIAS         2
#define F_UNBR          4
#define F_CONDBR        8
#define F_JSR           16
#define F_FLOAT         32
#define F_FBR           64

#define OP2(x)          (((x) & 0x7) << 22)
#define OP3(x)          (((x) & 0x3f) << 19)
#define OP(x)           ((unsigned) ((x) & 0x3) << 30)
#define OPF(x)          (((x) & 0x1ff) << 5)
#define OPF_LOW5(x)     OPF ((x) & 0x1f)
#define F3F(x, y, z)    (OP (x) | OP3 (y) | OPF (z))
#define F3I(x)          (((x) & 0x1) << 13)
#define F2(x, y)        (OP (x) | OP2(y))
#define F3(x, y, z)     (OP (x) | OP3(y) | F3I(z))
#define F1(x)           (OP (x))
#define DISP30(x)       ((x) & 0x3fffffff)
#define ASI(x)          (((x) & 0xff) << 5)
#define RS2(x)          ((x) & 0x1f)
#define SIMM13(x)       ((x) & 0x1fff)
#define RD(x)           (((x) & 0x1f) << 25)
#define RS1(x)          (((x) & 0x1f) << 14)
#define ASI_RS2(x)      (SIMM13 (x))
#define MEMBAR(x)       ((x) & 0x7f)
#define SLCPOP(x)       (((x) & 0x7f) << 6)

#define ANNUL   (1 << 29)
#define BPRED   (1 << 19)
#define IMMED   F3I (1)
#define RD_G0   RD (~0)
#define RS1_G0  RS1 (~0)
#define RS2_G0  RS2 (~0)

static const struct sparc_opcode sparc_opcodes[];

static const char *sparc_decode_asi_v8 (int);
static const char *sparc_decode_asi_v9 (int);
static const char *sparc_decode_membar (int);
static const char *sparc_decode_prefetch (int);
static const char *sparc_decode_sparclet_cpreg (int);

/* Table of opcodes for the sparc.
   Copyright 1989, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2002, 2004, 2005
   Free Software Foundation, Inc.

   This file is part of the BFD library.

   BFD is free software; you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 2, or (at your option) any later
   version.

   BFD is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
   for more details.

   You should have received a copy of the GNU General Public License
   along with this software; see the file COPYING.  If not,
   see <http://www.gnu.org/licenses/>.  */

#define MASK_V6         SPARC_OPCODE_ARCH_MASK (SPARC_OPCODE_ARCH_V6)
#define MASK_V7         SPARC_OPCODE_ARCH_MASK (SPARC_OPCODE_ARCH_V7)
#define MASK_V8         SPARC_OPCODE_ARCH_MASK (SPARC_OPCODE_ARCH_V8)
#define MASK_SPARCLET   SPARC_OPCODE_ARCH_MASK (SPARC_OPCODE_ARCH_SPARCLET)
#define MASK_SPARCLITE  SPARC_OPCODE_ARCH_MASK (SPARC_OPCODE_ARCH_SPARCLITE)
#define MASK_V9         SPARC_OPCODE_ARCH_MASK (SPARC_OPCODE_ARCH_V9)
#define MASK_V9A        SPARC_OPCODE_ARCH_MASK (SPARC_OPCODE_ARCH_V9A)
#define MASK_V9B        SPARC_OPCODE_ARCH_MASK (SPARC_OPCODE_ARCH_V9B)

#define v6              (MASK_V6 | MASK_V7 | MASK_V8 | MASK_SPARCLET \
                         | MASK_SPARCLITE | MASK_V9 | MASK_V9A | MASK_V9B)
#define v6notlet        (MASK_V6 | MASK_V7 | MASK_V8 \
                         | MASK_SPARCLITE | MASK_V9 | MASK_V9A | MASK_V9B)
#define v7              (MASK_V7 | MASK_V8 | MASK_SPARCLET \
                         | MASK_SPARCLITE | MASK_V9 | MASK_V9A | MASK_V9B)
#define v8              (MASK_V8 | MASK_SPARCLET | MASK_SPARCLITE \
                         | MASK_V9 | MASK_V9A | MASK_V9B)
#define sparclet        (MASK_SPARCLET)
#define sparclite       (MASK_SPARCLITE)
#define v9              (MASK_V9 | MASK_V9A | MASK_V9B)
#define v9a             (MASK_V9A | MASK_V9B)
#define v9b             (MASK_V9B)
#define v6notv9         (MASK_V6 | MASK_V7 | MASK_V8 \
                         | MASK_SPARCLET | MASK_SPARCLITE)
#define v9notv9a        (MASK_V9)

#define COND(x)         (((x) & 0xf) << 25)

#define MCOND(x,i_or_f) ((((i_or_f) & 1) << 18) | (((x) >> 11) & (0xf << 14)))

#define RCOND(x)        (((x) & 0x7) << 10)

#define CONDA   (COND (0x8))
#define CONDCC  (COND (0xd))
#define CONDCS  (COND (0x5))
#define CONDE   (COND (0x1))
#define CONDG   (COND (0xa))
#define CONDGE  (COND (0xb))
#define CONDGU  (COND (0xc))
#define CONDL   (COND (0x3))
#define CONDLE  (COND (0x2))
#define CONDLEU (COND (0x4))
#define CONDN   (COND (0x0))
#define CONDNE  (COND (0x9))
#define CONDNEG (COND (0x6))
#define CONDPOS (COND (0xe))
#define CONDVC  (COND (0xf))
#define CONDVS  (COND (0x7))

#define CONDNZ  CONDNE
#define CONDZ   CONDE
#define CONDGEU CONDCC
#define CONDLU  CONDCS

#define FCONDA          (COND (0x8))
#define FCONDE          (COND (0x9))
#define FCONDG          (COND (0x6))
#define FCONDGE         (COND (0xb))
#define FCONDL          (COND (0x4))
#define FCONDLE         (COND (0xd))
#define FCONDLG         (COND (0x2))
#define FCONDN          (COND (0x0))
#define FCONDNE         (COND (0x1))
#define FCONDO          (COND (0xf))
#define FCONDU          (COND (0x7))
#define FCONDUE         (COND (0xa))
#define FCONDUG         (COND (0x5))
#define FCONDUGE        (COND (0xc))
#define FCONDUL         (COND (0x3))
#define FCONDULE        (COND (0xe))

#define FCONDNZ FCONDNE
#define FCONDZ  FCONDE

#define ICC             (0)
#define XCC             (1 << 12)
#define FCC(x)          (((x) & 0x3) << 11)
#define FBFCC(x)        (((x) & 0x3) << 20)


#define COMMUTEOP(opcode, op3, arch_mask) \
{ opcode,       F3(2, op3, 0), F3(~2, ~op3, ~0)|ASI(~0),        "1,2,d", 0, arch_mask }, \
{ opcode,       F3(2, op3, 1), F3(~2, ~op3, ~1),                "1,i,d", 0, arch_mask }, \
{ opcode,       F3(2, op3, 1), F3(~2, ~op3, ~1),                "i,1,d", 0, arch_mask }

static const struct sparc_opcode sparc_opcodes[] = {

{ "ld", F3(3, 0x00, 0), F3(~3, ~0x00, ~0),              "[1+2],d", 0, v6 },
{ "ld", F3(3, 0x00, 0), F3(~3, ~0x00, ~0)|RS2_G0,       "[1],d", 0, v6 },
{ "ld", F3(3, 0x00, 1), F3(~3, ~0x00, ~1),              "[1+i],d", 0, v6 },
{ "ld", F3(3, 0x00, 1), F3(~3, ~0x00, ~1),              "[i+1],d", 0, v6 },
{ "ld", F3(3, 0x00, 1), F3(~3, ~0x00, ~1)|RS1_G0,       "[i],d", 0, v6 },
{ "ld", F3(3, 0x00, 1), F3(~3, ~0x00, ~1)|SIMM13(~0),   "[1],d", 0, v6 },
{ "ld", F3(3, 0x20, 0), F3(~3, ~0x20, ~0),              "[1+2],g", 0, v6 },
{ "ld", F3(3, 0x20, 0), F3(~3, ~0x20, ~0)|RS2_G0,       "[1],g", 0, v6 },
{ "ld", F3(3, 0x20, 1), F3(~3, ~0x20, ~1),              "[1+i],g", 0, v6 },
{ "ld", F3(3, 0x20, 1), F3(~3, ~0x20, ~1),              "[i+1],g", 0, v6 },
{ "ld", F3(3, 0x20, 1), F3(~3, ~0x20, ~1)|RS1_G0,       "[i],g", 0, v6 },
{ "ld", F3(3, 0x20, 1), F3(~3, ~0x20, ~1)|SIMM13(~0),   "[1],g", 0, v6 },

{ "ld", F3(3, 0x21, 0), F3(~3, ~0x21, ~0)|RD(~0),       "[1+2],F", 0, v6 },
{ "ld", F3(3, 0x21, 0), F3(~3, ~0x21, ~0)|RS2_G0|RD(~0),"[1],F", 0, v6 },
{ "ld", F3(3, 0x21, 1), F3(~3, ~0x21, ~1)|RD(~0),       "[1+i],F", 0, v6 },
{ "ld", F3(3, 0x21, 1), F3(~3, ~0x21, ~1)|RD(~0),       "[i+1],F", 0, v6 },
{ "ld", F3(3, 0x21, 1), F3(~3, ~0x21, ~1)|RS1_G0|RD(~0),"[i],F", 0, v6 },
{ "ld", F3(3, 0x21, 1), F3(~3, ~0x21, ~1)|SIMM13(~0)|RD(~0),"[1],F", 0, v6 },

{ "ld", F3(3, 0x30, 0), F3(~3, ~0x30, ~0),              "[1+2],D", 0, v6notv9 },
{ "ld", F3(3, 0x30, 0), F3(~3, ~0x30, ~0)|RS2_G0,       "[1],D", 0, v6notv9 },
{ "ld", F3(3, 0x30, 1), F3(~3, ~0x30, ~1),              "[1+i],D", 0, v6notv9 },
{ "ld", F3(3, 0x30, 1), F3(~3, ~0x30, ~1),              "[i+1],D", 0, v6notv9 },
{ "ld", F3(3, 0x30, 1), F3(~3, ~0x30, ~1)|RS1_G0,       "[i],D", 0, v6notv9 },
{ "ld", F3(3, 0x30, 1), F3(~3, ~0x30, ~1)|SIMM13(~0),   "[1],D", 0, v6notv9 },
{ "ld", F3(3, 0x31, 0), F3(~3, ~0x31, ~0),              "[1+2],C", 0, v6notv9 },
{ "ld", F3(3, 0x31, 0), F3(~3, ~0x31, ~0)|RS2_G0,       "[1],C", 0, v6notv9 },
{ "ld", F3(3, 0x31, 1), F3(~3, ~0x31, ~1),              "[1+i],C", 0, v6notv9 },
{ "ld", F3(3, 0x31, 1), F3(~3, ~0x31, ~1),              "[i+1],C", 0, v6notv9 },
{ "ld", F3(3, 0x31, 1), F3(~3, ~0x31, ~1)|RS1_G0,       "[i],C", 0, v6notv9 },
{ "ld", F3(3, 0x31, 1), F3(~3, ~0x31, ~1)|SIMM13(~0),   "[1],C", 0, v6notv9 },

{ "lduw",       F3(3, 0x00, 0), F3(~3, ~0x00, ~0),              "[1+2],d", F_ALIAS, v9 },
{ "lduw",       F3(3, 0x00, 0), F3(~3, ~0x00, ~0)|RS2_G0,       "[1],d", F_ALIAS, v9 },
{ "lduw",       F3(3, 0x00, 1), F3(~3, ~0x00, ~1),              "[1+i],d", F_ALIAS, v9 },
{ "lduw",       F3(3, 0x00, 1), F3(~3, ~0x00, ~1),              "[i+1],d", F_ALIAS, v9 },
{ "lduw",       F3(3, 0x00, 1), F3(~3, ~0x00, ~1)|RS1_G0,       "[i],d", F_ALIAS, v9 },
{ "lduw",       F3(3, 0x00, 1), F3(~3, ~0x00, ~1)|SIMM13(~0),   "[1],d", F_ALIAS, v9 },

{ "ldd",        F3(3, 0x03, 0), F3(~3, ~0x03, ~0)|ASI(~0),      "[1+2],d", 0, v6 },
{ "ldd",        F3(3, 0x03, 0), F3(~3, ~0x03, ~0)|ASI_RS2(~0),  "[1],d", 0, v6 },
{ "ldd",        F3(3, 0x03, 1), F3(~3, ~0x03, ~1),              "[1+i],d", 0, v6 },
{ "ldd",        F3(3, 0x03, 1), F3(~3, ~0x03, ~1),              "[i+1],d", 0, v6 },
{ "ldd",        F3(3, 0x03, 1), F3(~3, ~0x03, ~1)|RS1_G0,       "[i],d", 0, v6 },
{ "ldd",        F3(3, 0x03, 1), F3(~3, ~0x03, ~1)|SIMM13(~0),   "[1],d", 0, v6 },
{ "ldd",        F3(3, 0x23, 0), F3(~3, ~0x23, ~0)|ASI(~0),      "[1+2],H", 0, v6 },
{ "ldd",        F3(3, 0x23, 0), F3(~3, ~0x23, ~0)|ASI_RS2(~0),  "[1],H", 0, v6 },
{ "ldd",        F3(3, 0x23, 1), F3(~3, ~0x23, ~1),              "[1+i],H", 0, v6 },
{ "ldd",        F3(3, 0x23, 1), F3(~3, ~0x23, ~1),              "[i+1],H", 0, v6 },
{ "ldd",        F3(3, 0x23, 1), F3(~3, ~0x23, ~1)|RS1_G0,       "[i],H", 0, v6 },
{ "ldd",        F3(3, 0x23, 1), F3(~3, ~0x23, ~1)|SIMM13(~0),   "[1],H", 0, v6 },

{ "ldd",        F3(3, 0x33, 0), F3(~3, ~0x33, ~0)|ASI(~0),      "[1+2],D", 0, v6notv9 },
{ "ldd",        F3(3, 0x33, 0), F3(~3, ~0x33, ~0)|ASI_RS2(~0),  "[1],D", 0, v6notv9 },
{ "ldd",        F3(3, 0x33, 1), F3(~3, ~0x33, ~1),              "[1+i],D", 0, v6notv9 },
{ "ldd",        F3(3, 0x33, 1), F3(~3, ~0x33, ~1),              "[i+1],D", 0, v6notv9 },
{ "ldd",        F3(3, 0x33, 1), F3(~3, ~0x33, ~1)|RS1_G0,       "[i],D", 0, v6notv9 },
{ "ldd",        F3(3, 0x33, 1), F3(~3, ~0x33, ~1)|SIMM13(~0),   "[1],D", 0, v6notv9 },

{ "ldq",        F3(3, 0x22, 0), F3(~3, ~0x22, ~0)|ASI(~0),      "[1+2],J", 0, v9 },
{ "ldq",        F3(3, 0x22, 0), F3(~3, ~0x22, ~0)|ASI_RS2(~0),  "[1],J", 0, v9 },
{ "ldq",        F3(3, 0x22, 1), F3(~3, ~0x22, ~1),              "[1+i],J", 0, v9 },
{ "ldq",        F3(3, 0x22, 1), F3(~3, ~0x22, ~1),              "[i+1],J", 0, v9 },
{ "ldq",        F3(3, 0x22, 1), F3(~3, ~0x22, ~1)|RS1_G0,       "[i],J", 0, v9 },
{ "ldq",        F3(3, 0x22, 1), F3(~3, ~0x22, ~1)|SIMM13(~0),   "[1],J", 0, v9 },

{ "ldsb",       F3(3, 0x09, 0), F3(~3, ~0x09, ~0)|ASI(~0),      "[1+2],d", 0, v6 },
{ "ldsb",       F3(3, 0x09, 0), F3(~3, ~0x09, ~0)|ASI_RS2(~0),  "[1],d", 0, v6 },
{ "ldsb",       F3(3, 0x09, 1), F3(~3, ~0x09, ~1),              "[1+i],d", 0, v6 },
{ "ldsb",       F3(3, 0x09, 1), F3(~3, ~0x09, ~1),              "[i+1],d", 0, v6 },
{ "ldsb",       F3(3, 0x09, 1), F3(~3, ~0x09, ~1)|RS1_G0,       "[i],d", 0, v6 },
{ "ldsb",       F3(3, 0x09, 1), F3(~3, ~0x09, ~1)|SIMM13(~0),   "[1],d", 0, v6 },

{ "ldsh",       F3(3, 0x0a, 0), F3(~3, ~0x0a, ~0)|ASI_RS2(~0),  "[1],d", 0, v6 },
{ "ldsh",       F3(3, 0x0a, 0), F3(~3, ~0x0a, ~0)|ASI(~0),      "[1+2],d", 0, v6 },
{ "ldsh",       F3(3, 0x0a, 1), F3(~3, ~0x0a, ~1),              "[1+i],d", 0, v6 },
{ "ldsh",       F3(3, 0x0a, 1), F3(~3, ~0x0a, ~1),              "[i+1],d", 0, v6 },
{ "ldsh",       F3(3, 0x0a, 1), F3(~3, ~0x0a, ~1)|RS1_G0,       "[i],d", 0, v6 },
{ "ldsh",       F3(3, 0x0a, 1), F3(~3, ~0x0a, ~1)|SIMM13(~0),   "[1],d", 0, v6 },

{ "ldstub",     F3(3, 0x0d, 0), F3(~3, ~0x0d, ~0)|ASI(~0),      "[1+2],d", 0, v6 },
{ "ldstub",     F3(3, 0x0d, 0), F3(~3, ~0x0d, ~0)|ASI_RS2(~0),  "[1],d", 0, v6 },
{ "ldstub",     F3(3, 0x0d, 1), F3(~3, ~0x0d, ~1),              "[1+i],d", 0, v6 },
{ "ldstub",     F3(3, 0x0d, 1), F3(~3, ~0x0d, ~1),              "[i+1],d", 0, v6 },
{ "ldstub",     F3(3, 0x0d, 1), F3(~3, ~0x0d, ~1)|RS1_G0,       "[i],d", 0, v6 },
{ "ldstub",     F3(3, 0x0d, 1), F3(~3, ~0x0d, ~1)|SIMM13(~0),   "[1],d", 0, v6 },

{ "ldsw",       F3(3, 0x08, 0), F3(~3, ~0x08, ~0)|ASI(~0),      "[1+2],d", 0, v9 },
{ "ldsw",       F3(3, 0x08, 0), F3(~3, ~0x08, ~0)|ASI_RS2(~0),  "[1],d", 0, v9 },
{ "ldsw",       F3(3, 0x08, 1), F3(~3, ~0x08, ~1),              "[1+i],d", 0, v9 },
{ "ldsw",       F3(3, 0x08, 1), F3(~3, ~0x08, ~1),              "[i+1],d", 0, v9 },
{ "ldsw",       F3(3, 0x08, 1), F3(~3, ~0x08, ~1)|RS1_G0,       "[i],d", 0, v9 },
{ "ldsw",       F3(3, 0x08, 1), F3(~3, ~0x08, ~1)|SIMM13(~0),   "[1],d", 0, v9 },

{ "ldub",       F3(3, 0x01, 0), F3(~3, ~0x01, ~0)|ASI(~0),      "[1+2],d", 0, v6 },
{ "ldub",       F3(3, 0x01, 0), F3(~3, ~0x01, ~0)|ASI_RS2(~0),  "[1],d", 0, v6 },
{ "ldub",       F3(3, 0x01, 1), F3(~3, ~0x01, ~1),              "[1+i],d", 0, v6 },
{ "ldub",       F3(3, 0x01, 1), F3(~3, ~0x01, ~1),              "[i+1],d", 0, v6 },
{ "ldub",       F3(3, 0x01, 1), F3(~3, ~0x01, ~1)|RS1_G0,       "[i],d", 0, v6 },
{ "ldub",       F3(3, 0x01, 1), F3(~3, ~0x01, ~1)|SIMM13(~0),   "[1],d", 0, v6 },

{ "lduh",       F3(3, 0x02, 0), F3(~3, ~0x02, ~0)|ASI(~0),      "[1+2],d", 0, v6 },
{ "lduh",       F3(3, 0x02, 0), F3(~3, ~0x02, ~0)|ASI_RS2(~0),  "[1],d", 0, v6 },
{ "lduh",       F3(3, 0x02, 1), F3(~3, ~0x02, ~1),              "[1+i],d", 0, v6 },
{ "lduh",       F3(3, 0x02, 1), F3(~3, ~0x02, ~1),              "[i+1],d", 0, v6 },
{ "lduh",       F3(3, 0x02, 1), F3(~3, ~0x02, ~1)|RS1_G0,       "[i],d", 0, v6 },
{ "lduh",       F3(3, 0x02, 1), F3(~3, ~0x02, ~1)|SIMM13(~0),   "[1],d", 0, v6 },

{ "ldx",        F3(3, 0x0b, 0), F3(~3, ~0x0b, ~0)|ASI(~0),      "[1+2],d", 0, v9 },
{ "ldx",        F3(3, 0x0b, 0), F3(~3, ~0x0b, ~0)|ASI_RS2(~0),  "[1],d", 0, v9 },
{ "ldx",        F3(3, 0x0b, 1), F3(~3, ~0x0b, ~1),              "[1+i],d", 0, v9 },
{ "ldx",        F3(3, 0x0b, 1), F3(~3, ~0x0b, ~1),              "[i+1],d", 0, v9 },
{ "ldx",        F3(3, 0x0b, 1), F3(~3, ~0x0b, ~1)|RS1_G0,       "[i],d", 0, v9 },
{ "ldx",        F3(3, 0x0b, 1), F3(~3, ~0x0b, ~1)|SIMM13(~0),   "[1],d", 0, v9 },

{ "ldx",        F3(3, 0x21, 0)|RD(1), F3(~3, ~0x21, ~0)|RD(~1), "[1+2],F", 0, v9 },
{ "ldx",        F3(3, 0x21, 0)|RD(1), F3(~3, ~0x21, ~0)|RS2_G0|RD(~1),  "[1],F", 0, v9 },
{ "ldx",        F3(3, 0x21, 1)|RD(1), F3(~3, ~0x21, ~1)|RD(~1), "[1+i],F", 0, v9 },
{ "ldx",        F3(3, 0x21, 1)|RD(1), F3(~3, ~0x21, ~1)|RD(~1), "[i+1],F", 0, v9 },
{ "ldx",        F3(3, 0x21, 1)|RD(1), F3(~3, ~0x21, ~1)|RS1_G0|RD(~1),  "[i],F", 0, v9 },
{ "ldx",        F3(3, 0x21, 1)|RD(1), F3(~3, ~0x21, ~1)|SIMM13(~0)|RD(~1),"[1],F", 0, v9 },

{ "lda",        F3(3, 0x10, 0), F3(~3, ~0x10, ~0),              "[1+2]A,d", 0, v6 },
{ "lda",        F3(3, 0x10, 0), F3(~3, ~0x10, ~0)|RS2_G0,       "[1]A,d", 0, v6 },
{ "lda",        F3(3, 0x10, 1), F3(~3, ~0x10, ~1),              "[1+i]o,d", 0, v9 },
{ "lda",        F3(3, 0x10, 1), F3(~3, ~0x10, ~1),              "[i+1]o,d", 0, v9 },
{ "lda",        F3(3, 0x10, 1), F3(~3, ~0x10, ~1)|RS1_G0,       "[i]o,d", 0, v9 },
{ "lda",        F3(3, 0x10, 1), F3(~3, ~0x10, ~1)|SIMM13(~0),   "[1]o,d", 0, v9 },
{ "lda",        F3(3, 0x30, 0), F3(~3, ~0x30, ~0),              "[1+2]A,g", 0, v9 },
{ "lda",        F3(3, 0x30, 0), F3(~3, ~0x30, ~0)|RS2_G0,       "[1]A,g", 0, v9 },
{ "lda",        F3(3, 0x30, 1), F3(~3, ~0x30, ~1),              "[1+i]o,g", 0, v9 },
{ "lda",        F3(3, 0x30, 1), F3(~3, ~0x30, ~1),              "[i+1]o,g", 0, v9 },
{ "lda",        F3(3, 0x30, 1), F3(~3, ~0x30, ~1)|RS1_G0,       "[i]o,g", 0, v9 },
{ "lda",        F3(3, 0x30, 1), F3(~3, ~0x30, ~1)|SIMM13(~0),   "[1]o,g", 0, v9 },

{ "ldda",       F3(3, 0x13, 0), F3(~3, ~0x13, ~0),              "[1+2]A,d", 0, v6 },
{ "ldda",       F3(3, 0x13, 0), F3(~3, ~0x13, ~0)|RS2_G0,       "[1]A,d", 0, v6 },
{ "ldda",       F3(3, 0x13, 1), F3(~3, ~0x13, ~1),              "[1+i]o,d", 0, v9 },
{ "ldda",       F3(3, 0x13, 1), F3(~3, ~0x13, ~1),              "[i+1]o,d", 0, v9 },
{ "ldda",       F3(3, 0x13, 1), F3(~3, ~0x13, ~1)|RS1_G0,       "[i]o,d", 0, v9 },
{ "ldda",       F3(3, 0x13, 1), F3(~3, ~0x13, ~1)|SIMM13(~0),   "[1]o,d", 0, v9 },

{ "ldda",       F3(3, 0x33, 0), F3(~3, ~0x33, ~0),              "[1+2]A,H", 0, v9 },
{ "ldda",       F3(3, 0x33, 0), F3(~3, ~0x33, ~0)|RS2_G0,       "[1]A,H", 0, v9 },
{ "ldda",       F3(3, 0x33, 1), F3(~3, ~0x33, ~1),              "[1+i]o,H", 0, v9 },
{ "ldda",       F3(3, 0x33, 1), F3(~3, ~0x33, ~1),              "[i+1]o,H", 0, v9 },
{ "ldda",       F3(3, 0x33, 1), F3(~3, ~0x33, ~1)|RS1_G0,       "[i]o,H", 0, v9 },
{ "ldda",       F3(3, 0x33, 1), F3(~3, ~0x33, ~1)|SIMM13(~0),   "[1]o,H", 0, v9 },

{ "ldqa",       F3(3, 0x32, 0), F3(~3, ~0x32, ~0),              "[1+2]A,J", 0, v9 },
{ "ldqa",       F3(3, 0x32, 0), F3(~3, ~0x32, ~0)|RS2_G0,       "[1]A,J", 0, v9 },
{ "ldqa",       F3(3, 0x32, 1), F3(~3, ~0x32, ~1),              "[1+i]o,J", 0, v9 },
{ "ldqa",       F3(3, 0x32, 1), F3(~3, ~0x32, ~1),              "[i+1]o,J", 0, v9 },
{ "ldqa",       F3(3, 0x32, 1), F3(~3, ~0x32, ~1)|RS1_G0,       "[i]o,J", 0, v9 },
{ "ldqa",       F3(3, 0x32, 1), F3(~3, ~0x32, ~1)|SIMM13(~0),   "[1]o,J", 0, v9 },

{ "ldsba",      F3(3, 0x19, 0), F3(~3, ~0x19, ~0),              "[1+2]A,d", 0, v6 },
{ "ldsba",      F3(3, 0x19, 0), F3(~3, ~0x19, ~0)|RS2_G0,       "[1]A,d", 0, v6 },
{ "ldsba",      F3(3, 0x19, 1), F3(~3, ~0x19, ~1),              "[1+i]o,d", 0, v9 },
{ "ldsba",      F3(3, 0x19, 1), F3(~3, ~0x19, ~1),              "[i+1]o,d", 0, v9 },
{ "ldsba",      F3(3, 0x19, 1), F3(~3, ~0x19, ~1)|RS1_G0,       "[i]o,d", 0, v9 },
{ "ldsba",      F3(3, 0x19, 1), F3(~3, ~0x19, ~1)|SIMM13(~0),   "[1]o,d", 0, v9 },

{ "ldsha",      F3(3, 0x1a, 0), F3(~3, ~0x1a, ~0),              "[1+2]A,d", 0, v6 },
{ "ldsha",      F3(3, 0x1a, 0), F3(~3, ~0x1a, ~0)|RS2_G0,       "[1]A,d", 0, v6 },
{ "ldsha",      F3(3, 0x1a, 1), F3(~3, ~0x1a, ~1),              "[1+i]o,d", 0, v9 },
{ "ldsha",      F3(3, 0x1a, 1), F3(~3, ~0x1a, ~1),              "[i+1]o,d", 0, v9 },
{ "ldsha",      F3(3, 0x1a, 1), F3(~3, ~0x1a, ~1)|RS1_G0,       "[i]o,d", 0, v9 },
{ "ldsha",      F3(3, 0x1a, 1), F3(~3, ~0x1a, ~1)|SIMM13(~0),   "[1]o,d", 0, v9 },

{ "ldstuba",    F3(3, 0x1d, 0), F3(~3, ~0x1d, ~0),              "[1+2]A,d", 0, v6 },
{ "ldstuba",    F3(3, 0x1d, 0), F3(~3, ~0x1d, ~0)|RS2_G0,       "[1]A,d", 0, v6 },
{ "ldstuba",    F3(3, 0x1d, 1), F3(~3, ~0x1d, ~1),              "[1+i]o,d", 0, v9 },
{ "ldstuba",    F3(3, 0x1d, 1), F3(~3, ~0x1d, ~1),              "[i+1]o,d", 0, v9 },
{ "ldstuba",    F3(3, 0x1d, 1), F3(~3, ~0x1d, ~1)|RS1_G0,       "[i]o,d", 0, v9 },
{ "ldstuba",    F3(3, 0x1d, 1), F3(~3, ~0x1d, ~1)|SIMM13(~0),   "[1]o,d", 0, v9 },

{ "ldswa",      F3(3, 0x18, 0), F3(~3, ~0x18, ~0),              "[1+2]A,d", 0, v9 },
{ "ldswa",      F3(3, 0x18, 0), F3(~3, ~0x18, ~0)|RS2_G0,       "[1]A,d", 0, v9 },
{ "ldswa",      F3(3, 0x18, 1), F3(~3, ~0x18, ~1),              "[1+i]o,d", 0, v9 },
{ "ldswa",      F3(3, 0x18, 1), F3(~3, ~0x18, ~1),              "[i+1]o,d", 0, v9 },
{ "ldswa",      F3(3, 0x18, 1), F3(~3, ~0x18, ~1)|RS1_G0,       "[i]o,d", 0, v9 },
{ "ldswa",      F3(3, 0x18, 1), F3(~3, ~0x18, ~1)|SIMM13(~0),   "[1]o,d", 0, v9 },

{ "lduba",      F3(3, 0x11, 0), F3(~3, ~0x11, ~0),              "[1+2]A,d", 0, v6 },
{ "lduba",      F3(3, 0x11, 0), F3(~3, ~0x11, ~0)|RS2_G0,       "[1]A,d", 0, v6 },
{ "lduba",      F3(3, 0x11, 1), F3(~3, ~0x11, ~1),              "[1+i]o,d", 0, v9 },
{ "lduba",      F3(3, 0x11, 1), F3(~3, ~0x11, ~1),              "[i+1]o,d", 0, v9 },
{ "lduba",      F3(3, 0x11, 1), F3(~3, ~0x11, ~1)|RS1_G0,       "[i]o,d", 0, v9 },
{ "lduba",      F3(3, 0x11, 1), F3(~3, ~0x11, ~1)|SIMM13(~0),   "[1]o,d", 0, v9 },

{ "lduha",      F3(3, 0x12, 0), F3(~3, ~0x12, ~0),              "[1+2]A,d", 0, v6 },
{ "lduha",      F3(3, 0x12, 0), F3(~3, ~0x12, ~0)|RS2_G0,       "[1]A,d", 0, v6 },
{ "lduha",      F3(3, 0x12, 1), F3(~3, ~0x12, ~1),              "[1+i]o,d", 0, v9 },
{ "lduha",      F3(3, 0x12, 1), F3(~3, ~0x12, ~1),              "[i+1]o,d", 0, v9 },
{ "lduha",      F3(3, 0x12, 1), F3(~3, ~0x12, ~1)|RS1_G0,       "[i]o,d", 0, v9 },
{ "lduha",      F3(3, 0x12, 1), F3(~3, ~0x12, ~1)|SIMM13(~0),   "[1]o,d", 0, v9 },

{ "lduwa",      F3(3, 0x10, 0), F3(~3, ~0x10, ~0),              "[1+2]A,d", F_ALIAS, v9 },
{ "lduwa",      F3(3, 0x10, 0), F3(~3, ~0x10, ~0)|RS2_G0,       "[1]A,d", F_ALIAS, v9 },
{ "lduwa",      F3(3, 0x10, 1), F3(~3, ~0x10, ~1),              "[1+i]o,d", F_ALIAS, v9 },
{ "lduwa",      F3(3, 0x10, 1), F3(~3, ~0x10, ~1),              "[i+1]o,d", F_ALIAS, v9 },
{ "lduwa",      F3(3, 0x10, 1), F3(~3, ~0x10, ~1)|RS1_G0,       "[i]o,d", F_ALIAS, v9 },
{ "lduwa",      F3(3, 0x10, 1), F3(~3, ~0x10, ~1)|SIMM13(~0),   "[1]o,d", F_ALIAS, v9 },

{ "ldxa",       F3(3, 0x1b, 0), F3(~3, ~0x1b, ~0),              "[1+2]A,d", 0, v9 },
{ "ldxa",       F3(3, 0x1b, 0), F3(~3, ~0x1b, ~0)|RS2_G0,       "[1]A,d", 0, v9 },
{ "ldxa",       F3(3, 0x1b, 1), F3(~3, ~0x1b, ~1),              "[1+i]o,d", 0, v9 },
{ "ldxa",       F3(3, 0x1b, 1), F3(~3, ~0x1b, ~1),              "[i+1]o,d", 0, v9 },
{ "ldxa",       F3(3, 0x1b, 1), F3(~3, ~0x1b, ~1)|RS1_G0,       "[i]o,d", 0, v9 },
{ "ldxa",       F3(3, 0x1b, 1), F3(~3, ~0x1b, ~1)|SIMM13(~0),   "[1]o,d", 0, v9 },

{ "st", F3(3, 0x04, 0), F3(~3, ~0x04, ~0)|ASI(~0),              "d,[1+2]", 0, v6 },
{ "st", F3(3, 0x04, 0), F3(~3, ~0x04, ~0)|ASI_RS2(~0),          "d,[1]", 0, v6 },
{ "st", F3(3, 0x04, 1), F3(~3, ~0x04, ~1),                      "d,[1+i]", 0, v6 },
{ "st", F3(3, 0x04, 1), F3(~3, ~0x04, ~1),                      "d,[i+1]", 0, v6 },
{ "st", F3(3, 0x04, 1), F3(~3, ~0x04, ~1)|RS1_G0,               "d,[i]", 0, v6 },
{ "st", F3(3, 0x04, 1), F3(~3, ~0x04, ~1)|SIMM13(~0),           "d,[1]", 0, v6 },
{ "st", F3(3, 0x24, 0), F3(~3, ~0x24, ~0)|ASI(~0),              "g,[1+2]", 0, v6 },
{ "st", F3(3, 0x24, 0), F3(~3, ~0x24, ~0)|ASI_RS2(~0),          "g,[1]", 0, v6 },
{ "st", F3(3, 0x24, 1), F3(~3, ~0x24, ~1),                      "g,[1+i]", 0, v6 },
{ "st", F3(3, 0x24, 1), F3(~3, ~0x24, ~1),                      "g,[i+1]", 0, v6 },
{ "st", F3(3, 0x24, 1), F3(~3, ~0x24, ~1)|RS1_G0,               "g,[i]", 0, v6 },
{ "st", F3(3, 0x24, 1), F3(~3, ~0x24, ~1)|SIMM13(~0),           "g,[1]", 0, v6 },

{ "st", F3(3, 0x34, 0), F3(~3, ~0x34, ~0)|ASI(~0),              "D,[1+2]", 0, v6notv9 },
{ "st", F3(3, 0x34, 0), F3(~3, ~0x34, ~0)|ASI_RS2(~0),          "D,[1]", 0, v6notv9 },
{ "st", F3(3, 0x34, 1), F3(~3, ~0x34, ~1),                      "D,[1+i]", 0, v6notv9 },
{ "st", F3(3, 0x34, 1), F3(~3, ~0x34, ~1),                      "D,[i+1]", 0, v6notv9 },
{ "st", F3(3, 0x34, 1), F3(~3, ~0x34, ~1)|RS1_G0,               "D,[i]", 0, v6notv9 },
{ "st", F3(3, 0x34, 1), F3(~3, ~0x34, ~1)|SIMM13(~0),           "D,[1]", 0, v6notv9 },
{ "st", F3(3, 0x35, 0), F3(~3, ~0x35, ~0)|ASI(~0),              "C,[1+2]", 0, v6notv9 },
{ "st", F3(3, 0x35, 0), F3(~3, ~0x35, ~0)|ASI_RS2(~0),          "C,[1]", 0, v6notv9 },
{ "st", F3(3, 0x35, 1), F3(~3, ~0x35, ~1),                      "C,[1+i]", 0, v6notv9 },
{ "st", F3(3, 0x35, 1), F3(~3, ~0x35, ~1),                      "C,[i+1]", 0, v6notv9 },
{ "st", F3(3, 0x35, 1), F3(~3, ~0x35, ~1)|RS1_G0,               "C,[i]", 0, v6notv9 },
{ "st", F3(3, 0x35, 1), F3(~3, ~0x35, ~1)|SIMM13(~0),           "C,[1]", 0, v6notv9 },

{ "st", F3(3, 0x25, 0), F3(~3, ~0x25, ~0)|RD_G0|ASI(~0),        "F,[1+2]", 0, v6 },
{ "st", F3(3, 0x25, 0), F3(~3, ~0x25, ~0)|RD_G0|ASI_RS2(~0),    "F,[1]", 0, v6 },
{ "st", F3(3, 0x25, 1), F3(~3, ~0x25, ~1)|RD_G0,                "F,[1+i]", 0, v6 },
{ "st", F3(3, 0x25, 1), F3(~3, ~0x25, ~1)|RD_G0,                "F,[i+1]", 0, v6 },
{ "st", F3(3, 0x25, 1), F3(~3, ~0x25, ~1)|RD_G0|RS1_G0,         "F,[i]", 0, v6 },
{ "st", F3(3, 0x25, 1), F3(~3, ~0x25, ~1)|RD_G0|SIMM13(~0),     "F,[1]", 0, v6 },

{ "stw",        F3(3, 0x04, 0), F3(~3, ~0x04, ~0)|ASI(~0),      "d,[1+2]", F_ALIAS, v9 },
{ "stw",        F3(3, 0x04, 0), F3(~3, ~0x04, ~0)|ASI_RS2(~0),  "d,[1]", F_ALIAS, v9 },
{ "stw",        F3(3, 0x04, 1), F3(~3, ~0x04, ~1),              "d,[1+i]", F_ALIAS, v9 },
{ "stw",        F3(3, 0x04, 1), F3(~3, ~0x04, ~1),              "d,[i+1]", F_ALIAS, v9 },
{ "stw",        F3(3, 0x04, 1), F3(~3, ~0x04, ~1)|RS1_G0,       "d,[i]", F_ALIAS, v9 },
{ "stw",        F3(3, 0x04, 1), F3(~3, ~0x04, ~1)|SIMM13(~0),   "d,[1]", F_ALIAS, v9 },
{ "stsw",       F3(3, 0x04, 0), F3(~3, ~0x04, ~0)|ASI(~0),      "d,[1+2]", F_ALIAS, v9 },
{ "stsw",       F3(3, 0x04, 0), F3(~3, ~0x04, ~0)|ASI_RS2(~0),  "d,[1]", F_ALIAS, v9 },
{ "stsw",       F3(3, 0x04, 1), F3(~3, ~0x04, ~1),              "d,[1+i]", F_ALIAS, v9 },
{ "stsw",       F3(3, 0x04, 1), F3(~3, ~0x04, ~1),              "d,[i+1]", F_ALIAS, v9 },
{ "stsw",       F3(3, 0x04, 1), F3(~3, ~0x04, ~1)|RS1_G0,       "d,[i]", F_ALIAS, v9 },
{ "stsw",       F3(3, 0x04, 1), F3(~3, ~0x04, ~1)|SIMM13(~0),   "d,[1]", F_ALIAS, v9 },
{ "stuw",       F3(3, 0x04, 0), F3(~3, ~0x04, ~0)|ASI(~0),      "d,[1+2]", F_ALIAS, v9 },
{ "stuw",       F3(3, 0x04, 0), F3(~3, ~0x04, ~0)|ASI_RS2(~0),  "d,[1]", F_ALIAS, v9 },
{ "stuw",       F3(3, 0x04, 1), F3(~3, ~0x04, ~1),              "d,[1+i]", F_ALIAS, v9 },
{ "stuw",       F3(3, 0x04, 1), F3(~3, ~0x04, ~1),              "d,[i+1]", F_ALIAS, v9 },
{ "stuw",       F3(3, 0x04, 1), F3(~3, ~0x04, ~1)|RS1_G0,       "d,[i]", F_ALIAS, v9 },
{ "stuw",       F3(3, 0x04, 1), F3(~3, ~0x04, ~1)|SIMM13(~0),   "d,[1]", F_ALIAS, v9 },

{ "spill",      F3(3, 0x04, 0), F3(~3, ~0x04, ~0)|ASI(~0),      "d,[1+2]", F_ALIAS, v6 },
{ "spill",      F3(3, 0x04, 0), F3(~3, ~0x04, ~0)|ASI_RS2(~0),  "d,[1]", F_ALIAS, v6 },
{ "spill",      F3(3, 0x04, 1), F3(~3, ~0x04, ~1),              "d,[1+i]", F_ALIAS, v6 },
{ "spill",      F3(3, 0x04, 1), F3(~3, ~0x04, ~1),              "d,[i+1]", F_ALIAS, v6 },
{ "spill",      F3(3, 0x04, 1), F3(~3, ~0x04, ~1)|RS1_G0,       "d,[i]", F_ALIAS, v6 },
{ "spill",      F3(3, 0x04, 1), F3(~3, ~0x04, ~1)|SIMM13(~0),   "d,[1]", F_ALIAS, v6 },

{ "sta",        F3(3, 0x14, 0), F3(~3, ~0x14, ~0),              "d,[1+2]A", 0, v6 },
{ "sta",        F3(3, 0x14, 0), F3(~3, ~0x14, ~0)|RS2(~0),      "d,[1]A", 0, v6 },
{ "sta",        F3(3, 0x14, 1), F3(~3, ~0x14, ~1),              "d,[1+i]o", 0, v9 },
{ "sta",        F3(3, 0x14, 1), F3(~3, ~0x14, ~1),              "d,[i+1]o", 0, v9 },
{ "sta",        F3(3, 0x14, 1), F3(~3, ~0x14, ~1)|RS1_G0,       "d,[i]o", 0, v9 },
{ "sta",        F3(3, 0x14, 1), F3(~3, ~0x14, ~1)|SIMM13(~0),   "d,[1]o", 0, v9 },

{ "sta",        F3(3, 0x34, 0), F3(~3, ~0x34, ~0),              "g,[1+2]A", 0, v9 },
{ "sta",        F3(3, 0x34, 0), F3(~3, ~0x34, ~0)|RS2(~0),      "g,[1]A", 0, v9 },
{ "sta",        F3(3, 0x34, 1), F3(~3, ~0x34, ~1),              "g,[1+i]o", 0, v9 },
{ "sta",        F3(3, 0x34, 1), F3(~3, ~0x34, ~1),              "g,[i+1]o", 0, v9 },
{ "sta",        F3(3, 0x34, 1), F3(~3, ~0x34, ~1)|RS1_G0,       "g,[i]o", 0, v9 },
{ "sta",        F3(3, 0x34, 1), F3(~3, ~0x34, ~1)|SIMM13(~0),   "g,[1]o", 0, v9 },

{ "stwa",       F3(3, 0x14, 0), F3(~3, ~0x14, ~0),              "d,[1+2]A", F_ALIAS, v9 },
{ "stwa",       F3(3, 0x14, 0), F3(~3, ~0x14, ~0)|RS2(~0),      "d,[1]A", F_ALIAS, v9 },
{ "stwa",       F3(3, 0x14, 1), F3(~3, ~0x14, ~1),              "d,[1+i]o", F_ALIAS, v9 },
{ "stwa",       F3(3, 0x14, 1), F3(~3, ~0x14, ~1),              "d,[i+1]o", F_ALIAS, v9 },
{ "stwa",       F3(3, 0x14, 1), F3(~3, ~0x14, ~1)|RS1_G0,       "d,[i]o", F_ALIAS, v9 },
{ "stwa",       F3(3, 0x14, 1), F3(~3, ~0x14, ~1)|SIMM13(~0),   "d,[1]o", F_ALIAS, v9 },
{ "stswa",      F3(3, 0x14, 0), F3(~3, ~0x14, ~0),              "d,[1+2]A", F_ALIAS, v9 },
{ "stswa",      F3(3, 0x14, 0), F3(~3, ~0x14, ~0)|RS2(~0),      "d,[1]A", F_ALIAS, v9 },
{ "stswa",      F3(3, 0x14, 1), F3(~3, ~0x14, ~1),              "d,[1+i]o", F_ALIAS, v9 },
{ "stswa",      F3(3, 0x14, 1), F3(~3, ~0x14, ~1),              "d,[i+1]o", F_ALIAS, v9 },
{ "stswa",      F3(3, 0x14, 1), F3(~3, ~0x14, ~1)|RS1_G0,       "d,[i]o", F_ALIAS, v9 },
{ "stswa",      F3(3, 0x14, 1), F3(~3, ~0x14, ~1)|SIMM13(~0),   "d,[1]o", F_ALIAS, v9 },
{ "stuwa",      F3(3, 0x14, 0), F3(~3, ~0x14, ~0),              "d,[1+2]A", F_ALIAS, v9 },
{ "stuwa",      F3(3, 0x14, 0), F3(~3, ~0x14, ~0)|RS2(~0),      "d,[1]A", F_ALIAS, v9 },
{ "stuwa",      F3(3, 0x14, 1), F3(~3, ~0x14, ~1),              "d,[1+i]o", F_ALIAS, v9 },
{ "stuwa",      F3(3, 0x14, 1), F3(~3, ~0x14, ~1),              "d,[i+1]o", F_ALIAS, v9 },
{ "stuwa",      F3(3, 0x14, 1), F3(~3, ~0x14, ~1)|RS1_G0,       "d,[i]o", F_ALIAS, v9 },
{ "stuwa",      F3(3, 0x14, 1), F3(~3, ~0x14, ~1)|SIMM13(~0),   "d,[1]o", F_ALIAS, v9 },

{ "stb",        F3(3, 0x05, 0), F3(~3, ~0x05, ~0)|ASI(~0),      "d,[1+2]", 0, v6 },
{ "stb",        F3(3, 0x05, 0), F3(~3, ~0x05, ~0)|ASI_RS2(~0),  "d,[1]", 0, v6 },
{ "stb",        F3(3, 0x05, 1), F3(~3, ~0x05, ~1),              "d,[1+i]", 0, v6 },
{ "stb",        F3(3, 0x05, 1), F3(~3, ~0x05, ~1),              "d,[i+1]", 0, v6 },
{ "stb",        F3(3, 0x05, 1), F3(~3, ~0x05, ~1)|RS1_G0,       "d,[i]", 0, v6 },
{ "stb",        F3(3, 0x05, 1), F3(~3, ~0x05, ~1)|SIMM13(~0),   "d,[1]", 0, v6 },

{ "stsb",       F3(3, 0x05, 0), F3(~3, ~0x05, ~0)|ASI(~0),      "d,[1+2]", F_ALIAS, v6 },
{ "stsb",       F3(3, 0x05, 0), F3(~3, ~0x05, ~0)|ASI_RS2(~0),  "d,[1]", F_ALIAS, v6 },
{ "stsb",       F3(3, 0x05, 1), F3(~3, ~0x05, ~1),              "d,[1+i]", F_ALIAS, v6 },
{ "stsb",       F3(3, 0x05, 1), F3(~3, ~0x05, ~1),              "d,[i+1]", F_ALIAS, v6 },
{ "stsb",       F3(3, 0x05, 1), F3(~3, ~0x05, ~1)|RS1_G0,       "d,[i]", F_ALIAS, v6 },
{ "stsb",       F3(3, 0x05, 1), F3(~3, ~0x05, ~1)|SIMM13(~0),   "d,[1]", F_ALIAS, v6 },
{ "stub",       F3(3, 0x05, 0), F3(~3, ~0x05, ~0)|ASI(~0),      "d,[1+2]", F_ALIAS, v6 },
{ "stub",       F3(3, 0x05, 0), F3(~3, ~0x05, ~0)|ASI_RS2(~0),  "d,[1]", F_ALIAS, v6 },
{ "stub",       F3(3, 0x05, 1), F3(~3, ~0x05, ~1),              "d,[1+i]", F_ALIAS, v6 },
{ "stub",       F3(3, 0x05, 1), F3(~3, ~0x05, ~1),              "d,[i+1]", F_ALIAS, v6 },
{ "stub",       F3(3, 0x05, 1), F3(~3, ~0x05, ~1)|RS1_G0,       "d,[i]", F_ALIAS, v6 },
{ "stub",       F3(3, 0x05, 1), F3(~3, ~0x05, ~1)|SIMM13(~0),   "d,[1]", F_ALIAS, v6 },

{ "stba",       F3(3, 0x15, 0), F3(~3, ~0x15, ~0),              "d,[1+2]A", 0, v6 },
{ "stba",       F3(3, 0x15, 0), F3(~3, ~0x15, ~0)|RS2(~0),      "d,[1]A", 0, v6 },
{ "stba",       F3(3, 0x15, 1), F3(~3, ~0x15, ~1),              "d,[1+i]o", 0, v9 },
{ "stba",       F3(3, 0x15, 1), F3(~3, ~0x15, ~1),              "d,[i+1]o", 0, v9 },
{ "stba",       F3(3, 0x15, 1), F3(~3, ~0x15, ~1)|RS1_G0,       "d,[i]o", 0, v9 },
{ "stba",       F3(3, 0x15, 1), F3(~3, ~0x15, ~1)|SIMM13(~0),   "d,[1]o", 0, v9 },

{ "stsba",      F3(3, 0x15, 0), F3(~3, ~0x15, ~0),              "d,[1+2]A", F_ALIAS, v6 },
{ "stsba",      F3(3, 0x15, 0), F3(~3, ~0x15, ~0)|RS2(~0),      "d,[1]A", F_ALIAS, v6 },
{ "stsba",      F3(3, 0x15, 1), F3(~3, ~0x15, ~1),              "d,[1+i]o", F_ALIAS, v9 },
{ "stsba",      F3(3, 0x15, 1), F3(~3, ~0x15, ~1),              "d,[i+1]o", F_ALIAS, v9 },
{ "stsba",      F3(3, 0x15, 1), F3(~3, ~0x15, ~1)|RS1_G0,       "d,[i]o", F_ALIAS, v9 },
{ "stsba",      F3(3, 0x15, 1), F3(~3, ~0x15, ~1)|SIMM13(~0),   "d,[1]o", F_ALIAS, v9 },
{ "stuba",      F3(3, 0x15, 0), F3(~3, ~0x15, ~0),              "d,[1+2]A", F_ALIAS, v6 },
{ "stuba",      F3(3, 0x15, 0), F3(~3, ~0x15, ~0)|RS2(~0),      "d,[1]A", F_ALIAS, v6 },
{ "stuba",      F3(3, 0x15, 1), F3(~3, ~0x15, ~1),              "d,[1+i]o", F_ALIAS, v9 },
{ "stuba",      F3(3, 0x15, 1), F3(~3, ~0x15, ~1),              "d,[i+1]o", F_ALIAS, v9 },
{ "stuba",      F3(3, 0x15, 1), F3(~3, ~0x15, ~1)|RS1_G0,       "d,[i]o", F_ALIAS, v9 },
{ "stuba",      F3(3, 0x15, 1), F3(~3, ~0x15, ~1)|SIMM13(~0),   "d,[1]o", F_ALIAS, v9 },

{ "std",        F3(3, 0x07, 0), F3(~3, ~0x07, ~0)|ASI(~0),      "d,[1+2]", 0, v6 },
{ "std",        F3(3, 0x07, 0), F3(~3, ~0x07, ~0)|ASI_RS2(~0),  "d,[1]", 0, v6 },
{ "std",        F3(3, 0x07, 1), F3(~3, ~0x07, ~1),              "d,[1+i]", 0, v6 },
{ "std",        F3(3, 0x07, 1), F3(~3, ~0x07, ~1),              "d,[i+1]", 0, v6 },
{ "std",        F3(3, 0x07, 1), F3(~3, ~0x07, ~1)|RS1_G0,       "d,[i]", 0, v6 },
{ "std",        F3(3, 0x07, 1), F3(~3, ~0x07, ~1)|SIMM13(~0),   "d,[1]", 0, v6 },

{ "std",        F3(3, 0x26, 0), F3(~3, ~0x26, ~0)|ASI(~0),      "q,[1+2]", 0, v6notv9 },
{ "std",        F3(3, 0x26, 0), F3(~3, ~0x26, ~0)|ASI_RS2(~0),  "q,[1]", 0, v6notv9 },
{ "std",        F3(3, 0x26, 1), F3(~3, ~0x26, ~1),              "q,[1+i]", 0, v6notv9 },
{ "std",        F3(3, 0x26, 1), F3(~3, ~0x26, ~1),              "q,[i+1]", 0, v6notv9 },
{ "std",        F3(3, 0x26, 1), F3(~3, ~0x26, ~1)|RS1_G0,       "q,[i]", 0, v6notv9 },
{ "std",        F3(3, 0x26, 1), F3(~3, ~0x26, ~1)|SIMM13(~0),   "q,[1]", 0, v6notv9 },
{ "std",        F3(3, 0x27, 0), F3(~3, ~0x27, ~0)|ASI(~0),      "H,[1+2]", 0, v6 },
{ "std",        F3(3, 0x27, 0), F3(~3, ~0x27, ~0)|ASI_RS2(~0),  "H,[1]", 0, v6 },
{ "std",        F3(3, 0x27, 1), F3(~3, ~0x27, ~1),              "H,[1+i]", 0, v6 },
{ "std",        F3(3, 0x27, 1), F3(~3, ~0x27, ~1),              "H,[i+1]", 0, v6 },
{ "std",        F3(3, 0x27, 1), F3(~3, ~0x27, ~1)|RS1_G0,       "H,[i]", 0, v6 },
{ "std",        F3(3, 0x27, 1), F3(~3, ~0x27, ~1)|SIMM13(~0),   "H,[1]", 0, v6 },

{ "std",        F3(3, 0x36, 0), F3(~3, ~0x36, ~0)|ASI(~0),      "Q,[1+2]", 0, v6notv9 },
{ "std",        F3(3, 0x36, 0), F3(~3, ~0x36, ~0)|ASI_RS2(~0),  "Q,[1]", 0, v6notv9 },
{ "std",        F3(3, 0x36, 1), F3(~3, ~0x36, ~1),              "Q,[1+i]", 0, v6notv9 },
{ "std",        F3(3, 0x36, 1), F3(~3, ~0x36, ~1),              "Q,[i+1]", 0, v6notv9 },
{ "std",        F3(3, 0x36, 1), F3(~3, ~0x36, ~1)|RS1_G0,       "Q,[i]", 0, v6notv9 },
{ "std",        F3(3, 0x36, 1), F3(~3, ~0x36, ~1)|SIMM13(~0),   "Q,[1]", 0, v6notv9 },
{ "std",        F3(3, 0x37, 0), F3(~3, ~0x37, ~0)|ASI(~0),      "D,[1+2]", 0, v6notv9 },
{ "std",        F3(3, 0x37, 0), F3(~3, ~0x37, ~0)|ASI_RS2(~0),  "D,[1]", 0, v6notv9 },
{ "std",        F3(3, 0x37, 1), F3(~3, ~0x37, ~1),              "D,[1+i]", 0, v6notv9 },
{ "std",        F3(3, 0x37, 1), F3(~3, ~0x37, ~1),              "D,[i+1]", 0, v6notv9 },
{ "std",        F3(3, 0x37, 1), F3(~3, ~0x37, ~1)|RS1_G0,       "D,[i]", 0, v6notv9 },
{ "std",        F3(3, 0x37, 1), F3(~3, ~0x37, ~1)|SIMM13(~0),   "D,[1]", 0, v6notv9 },

{ "spilld",     F3(3, 0x07, 0), F3(~3, ~0x07, ~0)|ASI(~0),      "d,[1+2]", F_ALIAS, v6 },
{ "spilld",     F3(3, 0x07, 0), F3(~3, ~0x07, ~0)|ASI_RS2(~0),  "d,[1]", F_ALIAS, v6 },
{ "spilld",     F3(3, 0x07, 1), F3(~3, ~0x07, ~1),              "d,[1+i]", F_ALIAS, v6 },
{ "spilld",     F3(3, 0x07, 1), F3(~3, ~0x07, ~1),              "d,[i+1]", F_ALIAS, v6 },
{ "spilld",     F3(3, 0x07, 1), F3(~3, ~0x07, ~1)|RS1_G0,       "d,[i]", F_ALIAS, v6 },
{ "spilld",     F3(3, 0x07, 1), F3(~3, ~0x07, ~1)|SIMM13(~0),   "d,[1]", F_ALIAS, v6 },

{ "stda",       F3(3, 0x17, 0), F3(~3, ~0x17, ~0),              "d,[1+2]A", 0, v6 },
{ "stda",       F3(3, 0x17, 0), F3(~3, ~0x17, ~0)|RS2(~0),      "d,[1]A", 0, v6 },
{ "stda",       F3(3, 0x17, 1), F3(~3, ~0x17, ~1),              "d,[1+i]o", 0, v9 },
{ "stda",       F3(3, 0x17, 1), F3(~3, ~0x17, ~1),              "d,[i+1]o", 0, v9 },
{ "stda",       F3(3, 0x17, 1), F3(~3, ~0x17, ~1)|RS1_G0,       "d,[i]o", 0, v9 },
{ "stda",       F3(3, 0x17, 1), F3(~3, ~0x17, ~1)|SIMM13(~0),   "d,[1]o", 0, v9 },
{ "stda",       F3(3, 0x37, 0), F3(~3, ~0x37, ~0),              "H,[1+2]A", 0, v9 },
{ "stda",       F3(3, 0x37, 0), F3(~3, ~0x37, ~0)|RS2(~0),      "H,[1]A", 0, v9 },
{ "stda",       F3(3, 0x37, 1), F3(~3, ~0x37, ~1),              "H,[1+i]o", 0, v9 },
{ "stda",       F3(3, 0x37, 1), F3(~3, ~0x37, ~1),              "H,[i+1]o", 0, v9 },
{ "stda",       F3(3, 0x37, 1), F3(~3, ~0x37, ~1)|RS1_G0,       "H,[i]o", 0, v9 },
{ "stda",       F3(3, 0x37, 1), F3(~3, ~0x37, ~1)|SIMM13(~0),   "H,[1]o", 0, v9 },

{ "sth",        F3(3, 0x06, 0), F3(~3, ~0x06, ~0)|ASI(~0),      "d,[1+2]", 0, v6 },
{ "sth",        F3(3, 0x06, 0), F3(~3, ~0x06, ~0)|ASI_RS2(~0),  "d,[1]", 0, v6 },
{ "sth",        F3(3, 0x06, 1), F3(~3, ~0x06, ~1),              "d,[1+i]", 0, v6 },
{ "sth",        F3(3, 0x06, 1), F3(~3, ~0x06, ~1),              "d,[i+1]", 0, v6 },
{ "sth",        F3(3, 0x06, 1), F3(~3, ~0x06, ~1)|RS1_G0,       "d,[i]", 0, v6 },
{ "sth",        F3(3, 0x06, 1), F3(~3, ~0x06, ~1)|SIMM13(~0),   "d,[1]", 0, v6 },

{ "stsh",       F3(3, 0x06, 0), F3(~3, ~0x06, ~0)|ASI(~0),      "d,[1+2]", F_ALIAS, v6 },
{ "stsh",       F3(3, 0x06, 0), F3(~3, ~0x06, ~0)|ASI_RS2(~0),  "d,[1]", F_ALIAS, v6 },
{ "stsh",       F3(3, 0x06, 1), F3(~3, ~0x06, ~1),              "d,[1+i]", F_ALIAS, v6 },
{ "stsh",       F3(3, 0x06, 1), F3(~3, ~0x06, ~1),              "d,[i+1]", F_ALIAS, v6 },
{ "stsh",       F3(3, 0x06, 1), F3(~3, ~0x06, ~1)|RS1_G0,       "d,[i]", F_ALIAS, v6 },
{ "stsh",       F3(3, 0x06, 1), F3(~3, ~0x06, ~1)|SIMM13(~0),   "d,[1]", F_ALIAS, v6 },
{ "stuh",       F3(3, 0x06, 0), F3(~3, ~0x06, ~0)|ASI(~0),      "d,[1+2]", F_ALIAS, v6 },
{ "stuh",       F3(3, 0x06, 0), F3(~3, ~0x06, ~0)|ASI_RS2(~0),  "d,[1]", F_ALIAS, v6 },
{ "stuh",       F3(3, 0x06, 1), F3(~3, ~0x06, ~1),              "d,[1+i]", F_ALIAS, v6 },
{ "stuh",       F3(3, 0x06, 1), F3(~3, ~0x06, ~1),              "d,[i+1]", F_ALIAS, v6 },
{ "stuh",       F3(3, 0x06, 1), F3(~3, ~0x06, ~1)|RS1_G0,       "d,[i]", F_ALIAS, v6 },
{ "stuh",       F3(3, 0x06, 1), F3(~3, ~0x06, ~1)|SIMM13(~0),   "d,[1]", F_ALIAS, v6 },

{ "stha",       F3(3, 0x16, 0), F3(~3, ~0x16, ~0),              "d,[1+2]A", 0, v6 },
{ "stha",       F3(3, 0x16, 0), F3(~3, ~0x16, ~0)|RS2(~0),      "d,[1]A", 0, v6 },
{ "stha",       F3(3, 0x16, 1), F3(~3, ~0x16, ~1),              "d,[1+i]o", 0, v9 },
{ "stha",       F3(3, 0x16, 1), F3(~3, ~0x16, ~1),              "d,[i+1]o", 0, v9 },
{ "stha",       F3(3, 0x16, 1), F3(~3, ~0x16, ~1)|RS1_G0,       "d,[i]o", 0, v9 },
{ "stha",       F3(3, 0x16, 1), F3(~3, ~0x16, ~1)|SIMM13(~0),   "d,[1]o", 0, v9 },

{ "stsha",      F3(3, 0x16, 0), F3(~3, ~0x16, ~0),              "d,[1+2]A", F_ALIAS, v6 },
{ "stsha",      F3(3, 0x16, 0), F3(~3, ~0x16, ~0)|RS2(~0),      "d,[1]A", F_ALIAS, v6 },
{ "stsha",      F3(3, 0x16, 1), F3(~3, ~0x16, ~1),              "d,[1+i]o", F_ALIAS, v9 },
{ "stsha",      F3(3, 0x16, 1), F3(~3, ~0x16, ~1),              "d,[i+1]o", F_ALIAS, v9 },
{ "stsha",      F3(3, 0x16, 1), F3(~3, ~0x16, ~1)|RS1_G0,       "d,[i]o", F_ALIAS, v9 },
{ "stsha",      F3(3, 0x16, 1), F3(~3, ~0x16, ~1)|SIMM13(~0),   "d,[1]o", F_ALIAS, v9 },
{ "stuha",      F3(3, 0x16, 0), F3(~3, ~0x16, ~0),              "d,[1+2]A", F_ALIAS, v6 },
{ "stuha",      F3(3, 0x16, 0), F3(~3, ~0x16, ~0)|RS2(~0),      "d,[1]A", F_ALIAS, v6 },
{ "stuha",      F3(3, 0x16, 1), F3(~3, ~0x16, ~1),              "d,[1+i]o", F_ALIAS, v9 },
{ "stuha",      F3(3, 0x16, 1), F3(~3, ~0x16, ~1),              "d,[i+1]o", F_ALIAS, v9 },
{ "stuha",      F3(3, 0x16, 1), F3(~3, ~0x16, ~1)|RS1_G0,       "d,[i]o", F_ALIAS, v9 },
{ "stuha",      F3(3, 0x16, 1), F3(~3, ~0x16, ~1)|SIMM13(~0),   "d,[1]o", F_ALIAS, v9 },

{ "stx",        F3(3, 0x0e, 0), F3(~3, ~0x0e, ~0)|ASI(~0),      "d,[1+2]", 0, v9 },
{ "stx",        F3(3, 0x0e, 0), F3(~3, ~0x0e, ~0)|ASI_RS2(~0),  "d,[1]", 0, v9 },
{ "stx",        F3(3, 0x0e, 1), F3(~3, ~0x0e, ~1),              "d,[1+i]", 0, v9 },
{ "stx",        F3(3, 0x0e, 1), F3(~3, ~0x0e, ~1),              "d,[i+1]", 0, v9 },
{ "stx",        F3(3, 0x0e, 1), F3(~3, ~0x0e, ~1)|RS1_G0,       "d,[i]", 0, v9 },
{ "stx",        F3(3, 0x0e, 1), F3(~3, ~0x0e, ~1)|SIMM13(~0),   "d,[1]", 0, v9 },

{ "stx",        F3(3, 0x25, 0)|RD(1), F3(~3, ~0x25, ~0)|ASI(~0)|RD(~1), "F,[1+2]", 0, v9 },
{ "stx",        F3(3, 0x25, 0)|RD(1), F3(~3, ~0x25, ~0)|ASI_RS2(~0)|RD(~1),"F,[1]", 0, v9 },
{ "stx",        F3(3, 0x25, 1)|RD(1), F3(~3, ~0x25, ~1)|RD(~1),         "F,[1+i]", 0, v9 },
{ "stx",        F3(3, 0x25, 1)|RD(1), F3(~3, ~0x25, ~1)|RD(~1),         "F,[i+1]", 0, v9 },
{ "stx",        F3(3, 0x25, 1)|RD(1), F3(~3, ~0x25, ~1)|RS1_G0|RD(~1),  "F,[i]", 0, v9 },
{ "stx",        F3(3, 0x25, 1)|RD(1), F3(~3, ~0x25, ~1)|SIMM13(~0)|RD(~1),"F,[1]", 0, v9 },

{ "stxa",       F3(3, 0x1e, 0), F3(~3, ~0x1e, ~0),              "d,[1+2]A", 0, v9 },
{ "stxa",       F3(3, 0x1e, 0), F3(~3, ~0x1e, ~0)|RS2(~0),      "d,[1]A", 0, v9 },
{ "stxa",       F3(3, 0x1e, 1), F3(~3, ~0x1e, ~1),              "d,[1+i]o", 0, v9 },
{ "stxa",       F3(3, 0x1e, 1), F3(~3, ~0x1e, ~1),              "d,[i+1]o", 0, v9 },
{ "stxa",       F3(3, 0x1e, 1), F3(~3, ~0x1e, ~1)|RS1_G0,       "d,[i]o", 0, v9 },
{ "stxa",       F3(3, 0x1e, 1), F3(~3, ~0x1e, ~1)|SIMM13(~0),   "d,[1]o", 0, v9 },

{ "stq",        F3(3, 0x26, 0), F3(~3, ~0x26, ~0)|ASI(~0),      "J,[1+2]", 0, v9 },
{ "stq",        F3(3, 0x26, 0), F3(~3, ~0x26, ~0)|ASI_RS2(~0),  "J,[1]", 0, v9 },
{ "stq",        F3(3, 0x26, 1), F3(~3, ~0x26, ~1),              "J,[1+i]", 0, v9 },
{ "stq",        F3(3, 0x26, 1), F3(~3, ~0x26, ~1),              "J,[i+1]", 0, v9 },
{ "stq",        F3(3, 0x26, 1), F3(~3, ~0x26, ~1)|RS1_G0,       "J,[i]", 0, v9 },
{ "stq",        F3(3, 0x26, 1), F3(~3, ~0x26, ~1)|SIMM13(~0),   "J,[1]", 0, v9 },

{ "stqa",       F3(3, 0x36, 0), F3(~3, ~0x36, ~0)|ASI(~0),      "J,[1+2]A", 0, v9 },
{ "stqa",       F3(3, 0x36, 0), F3(~3, ~0x36, ~0)|ASI_RS2(~0),  "J,[1]A", 0, v9 },
{ "stqa",       F3(3, 0x36, 1), F3(~3, ~0x36, ~1),              "J,[1+i]o", 0, v9 },
{ "stqa",       F3(3, 0x36, 1), F3(~3, ~0x36, ~1),              "J,[i+1]o", 0, v9 },
{ "stqa",       F3(3, 0x36, 1), F3(~3, ~0x36, ~1)|RS1_G0,       "J,[i]o", 0, v9 },
{ "stqa",       F3(3, 0x36, 1), F3(~3, ~0x36, ~1)|SIMM13(~0),   "J,[1]o", 0, v9 },

{ "swap",       F3(3, 0x0f, 0), F3(~3, ~0x0f, ~0)|ASI(~0),      "[1+2],d", 0, v7 },
{ "swap",       F3(3, 0x0f, 0), F3(~3, ~0x0f, ~0)|ASI_RS2(~0),  "[1],d", 0, v7 },
{ "swap",       F3(3, 0x0f, 1), F3(~3, ~0x0f, ~1),              "[1+i],d", 0, v7 },
{ "swap",       F3(3, 0x0f, 1), F3(~3, ~0x0f, ~1),              "[i+1],d", 0, v7 },
{ "swap",       F3(3, 0x0f, 1), F3(~3, ~0x0f, ~1)|RS1_G0,       "[i],d", 0, v7 },
{ "swap",       F3(3, 0x0f, 1), F3(~3, ~0x0f, ~1)|SIMM13(~0),   "[1],d", 0, v7 },

{ "swapa",      F3(3, 0x1f, 0), F3(~3, ~0x1f, ~0),              "[1+2]A,d", 0, v7 },
{ "swapa",      F3(3, 0x1f, 0), F3(~3, ~0x1f, ~0)|RS2(~0),      "[1]A,d", 0, v7 },
{ "swapa",      F3(3, 0x1f, 1), F3(~3, ~0x1f, ~1),              "[1+i]o,d", 0, v9 },
{ "swapa",      F3(3, 0x1f, 1), F3(~3, ~0x1f, ~1),              "[i+1]o,d", 0, v9 },
{ "swapa",      F3(3, 0x1f, 1), F3(~3, ~0x1f, ~1)|RS1_G0,       "[i]o,d", 0, v9 },
{ "swapa",      F3(3, 0x1f, 1), F3(~3, ~0x1f, ~1)|SIMM13(~0),   "[1]o,d", 0, v9 },

{ "restore",    F3(2, 0x3d, 0), F3(~2, ~0x3d, ~0)|ASI(~0),                      "1,2,d", 0, v6 },
{ "restore",    F3(2, 0x3d, 0), F3(~2, ~0x3d, ~0)|RD_G0|RS1_G0|ASI_RS2(~0),     "", 0, v6 },
{ "restore",    F3(2, 0x3d, 1), F3(~2, ~0x3d, ~1),                              "1,i,d", 0, v6 },
{ "restore",    F3(2, 0x3d, 1), F3(~2, ~0x3d, ~1)|RD_G0|RS1_G0|SIMM13(~0),      "", 0, v6 },

{ "rett",       F3(2, 0x39, 0), F3(~2, ~0x39, ~0)|RD_G0|ASI(~0),        "1+2", F_UNBR|F_DELAYED, v6 },
{ "rett",       F3(2, 0x39, 0), F3(~2, ~0x39, ~0)|RD_G0|ASI_RS2(~0),    "1", F_UNBR|F_DELAYED, v6 },
{ "rett",       F3(2, 0x39, 1), F3(~2, ~0x39, ~1)|RD_G0,                "1+i", F_UNBR|F_DELAYED, v6 },
{ "rett",       F3(2, 0x39, 1), F3(~2, ~0x39, ~1)|RD_G0,                "i+1", F_UNBR|F_DELAYED, v6 },
{ "rett",       F3(2, 0x39, 1), F3(~2, ~0x39, ~1)|RD_G0|RS1_G0,         "i", F_UNBR|F_DELAYED, v6 },
{ "rett",       F3(2, 0x39, 1), F3(~2, ~0x39, ~1)|RD_G0|RS1_G0,         "i", F_UNBR|F_DELAYED, v6 },
{ "rett",       F3(2, 0x39, 1), F3(~2, ~0x39, ~1)|RD_G0|SIMM13(~0),     "1", F_UNBR|F_DELAYED, v6 },

{ "save",       F3(2, 0x3c, 0), F3(~2, ~0x3c, ~0)|ASI(~0),      "1,2,d", 0, v6 },
{ "save",       F3(2, 0x3c, 1), F3(~2, ~0x3c, ~1),              "1,i,d", 0, v6 },
{ "save",       0x81e00000,     ~0x81e00000,                    "", F_ALIAS, v6 },

{ "ret",  F3(2, 0x38, 1)|RS1(0x1f)|SIMM13(8), F3(~2, ~0x38, ~1)|SIMM13(~8),            "", F_UNBR|F_DELAYED, v6 },
{ "retl", F3(2, 0x38, 1)|RS1(0x0f)|SIMM13(8), F3(~2, ~0x38, ~1)|RS1(~0x0f)|SIMM13(~8), "", F_UNBR|F_DELAYED, v6 },

{ "jmpl",       F3(2, 0x38, 0), F3(~2, ~0x38, ~0)|ASI(~0),      "1+2,d", F_JSR|F_DELAYED, v6 },
{ "jmpl",       F3(2, 0x38, 0), F3(~2, ~0x38, ~0)|ASI_RS2(~0),  "1,d", F_JSR|F_DELAYED, v6 },
{ "jmpl",       F3(2, 0x38, 1), F3(~2, ~0x38, ~1)|SIMM13(~0),   "1,d", F_JSR|F_DELAYED, v6 },
{ "jmpl",       F3(2, 0x38, 1), F3(~2, ~0x38, ~1)|RS1_G0,       "i,d", F_JSR|F_DELAYED, v6 },
{ "jmpl",       F3(2, 0x38, 1), F3(~2, ~0x38, ~1),              "1+i,d", F_JSR|F_DELAYED, v6 },
{ "jmpl",       F3(2, 0x38, 1), F3(~2, ~0x38, ~1),              "i+1,d", F_JSR|F_DELAYED, v6 },

{ "done",       F3(2, 0x3e, 0)|RD(0), F3(~2, ~0x3e, ~0)|RD(~0)|RS1_G0|SIMM13(~0),       "", 0, v9 },
{ "retry",      F3(2, 0x3e, 0)|RD(1), F3(~2, ~0x3e, ~0)|RD(~1)|RS1_G0|SIMM13(~0),       "", 0, v9 },
{ "saved",      F3(2, 0x31, 0)|RD(0), F3(~2, ~0x31, ~0)|RD(~0)|RS1_G0|SIMM13(~0),       "", 0, v9 },
{ "restored",   F3(2, 0x31, 0)|RD(1), F3(~2, ~0x31, ~0)|RD(~1)|RS1_G0|SIMM13(~0),       "", 0, v9 },
{ "allclean",   F3(2, 0x31, 0)|RD(2), F3(~2, ~0x31, ~0)|RD(~2)|RS1_G0|SIMM13(~0),       "", 0, v9 },
{ "otherw",     F3(2, 0x31, 0)|RD(3), F3(~2, ~0x31, ~0)|RD(~3)|RS1_G0|SIMM13(~0),       "", 0, v9 },
{ "normalw",    F3(2, 0x31, 0)|RD(4), F3(~2, ~0x31, ~0)|RD(~4)|RS1_G0|SIMM13(~0),       "", 0, v9 },
{ "invalw",     F3(2, 0x31, 0)|RD(5), F3(~2, ~0x31, ~0)|RD(~5)|RS1_G0|SIMM13(~0),       "", 0, v9 },
{ "sir",        F3(2, 0x30, 1)|RD(0xf), F3(~2, ~0x30, ~1)|RD(~0xf)|RS1_G0,              "i", 0, v9 },

{ "flush",      F3(2, 0x3b, 0), F3(~2, ~0x3b, ~0)|ASI(~0),      "1+2", 0, v8 },
{ "flush",      F3(2, 0x3b, 0), F3(~2, ~0x3b, ~0)|ASI_RS2(~0),  "1", 0, v8 },
{ "flush",      F3(2, 0x3b, 1), F3(~2, ~0x3b, ~1)|SIMM13(~0),   "1", 0, v8 },
{ "flush",      F3(2, 0x3b, 1), F3(~2, ~0x3b, ~1)|RS1_G0,       "i", 0, v8 },
{ "flush",      F3(2, 0x3b, 1), F3(~2, ~0x3b, ~1),              "1+i", 0, v8 },
{ "flush",      F3(2, 0x3b, 1), F3(~2, ~0x3b, ~1),              "i+1", 0, v8 },

{ "iflush",     F3(2, 0x3b, 0), F3(~2, ~0x3b, ~0)|ASI(~0),      "1+2", F_ALIAS, v6 },
{ "iflush",     F3(2, 0x3b, 0), F3(~2, ~0x3b, ~0)|ASI_RS2(~0),  "1", F_ALIAS, v6 },
{ "iflush",     F3(2, 0x3b, 1), F3(~2, ~0x3b, ~1)|SIMM13(~0),   "1", F_ALIAS, v6 },
{ "iflush",     F3(2, 0x3b, 1), F3(~2, ~0x3b, ~1)|RS1_G0,       "i", F_ALIAS, v6 },
{ "iflush",     F3(2, 0x3b, 1), F3(~2, ~0x3b, ~1),              "1+i", F_ALIAS, v6 },
{ "iflush",     F3(2, 0x3b, 1), F3(~2, ~0x3b, ~1),              "i+1", F_ALIAS, v6 },

{ "return",     F3(2, 0x39, 0), F3(~2, ~0x39, ~0)|ASI(~0),      "1+2", 0, v9 },
{ "return",     F3(2, 0x39, 0), F3(~2, ~0x39, ~0)|ASI_RS2(~0),  "1", 0, v9 },
{ "return",     F3(2, 0x39, 1), F3(~2, ~0x39, ~1)|SIMM13(~0),   "1", 0, v9 },
{ "return",     F3(2, 0x39, 1), F3(~2, ~0x39, ~1)|RS1_G0,       "i", 0, v9 },
{ "return",     F3(2, 0x39, 1), F3(~2, ~0x39, ~1),              "1+i", 0, v9 },
{ "return",     F3(2, 0x39, 1), F3(~2, ~0x39, ~1),              "i+1", 0, v9 },

{ "flushw",     F3(2, 0x2b, 0), F3(~2, ~0x2b, ~0)|RD_G0|RS1_G0|ASI_RS2(~0),     "", 0, v9 },

{ "membar",     F3(2, 0x28, 1)|RS1(0xf), F3(~2, ~0x28, ~1)|RD_G0|RS1(~0xf)|SIMM13(~127), "K", 0, v9 },
{ "stbar",      F3(2, 0x28, 0)|RS1(0xf), F3(~2, ~0x28, ~0)|RD_G0|RS1(~0xf)|SIMM13(~0), "", 0, v8 },

{ "prefetch",   F3(3, 0x2d, 0), F3(~3, ~0x2d, ~0),              "[1+2],*", 0, v9 },
{ "prefetch",   F3(3, 0x2d, 0), F3(~3, ~0x2d, ~0)|RS2_G0,       "[1],*", 0, v9 },
{ "prefetch",   F3(3, 0x2d, 1), F3(~3, ~0x2d, ~1),              "[1+i],*", 0, v9 },
{ "prefetch",   F3(3, 0x2d, 1), F3(~3, ~0x2d, ~1),              "[i+1],*", 0, v9 },
{ "prefetch",   F3(3, 0x2d, 1), F3(~3, ~0x2d, ~1)|RS1_G0,       "[i],*", 0, v9 },
{ "prefetch",   F3(3, 0x2d, 1), F3(~3, ~0x2d, ~1)|SIMM13(~0),   "[1],*", 0, v9 },
{ "prefetcha",  F3(3, 0x3d, 0), F3(~3, ~0x3d, ~0),              "[1+2]A,*", 0, v9 },
{ "prefetcha",  F3(3, 0x3d, 0), F3(~3, ~0x3d, ~0)|RS2_G0,       "[1]A,*", 0, v9 },
{ "prefetcha",  F3(3, 0x3d, 1), F3(~3, ~0x3d, ~1),              "[1+i]o,*", 0, v9 },
{ "prefetcha",  F3(3, 0x3d, 1), F3(~3, ~0x3d, ~1),              "[i+1]o,*", 0, v9 },
{ "prefetcha",  F3(3, 0x3d, 1), F3(~3, ~0x3d, ~1)|RS1_G0,       "[i]o,*", 0, v9 },
{ "prefetcha",  F3(3, 0x3d, 1), F3(~3, ~0x3d, ~1)|SIMM13(~0),   "[1]o,*", 0, v9 },

{ "sll",        F3(2, 0x25, 0), F3(~2, ~0x25, ~0)|(1<<12)|(0x7f<<5),    "1,2,d", 0, v6 },
{ "sll",        F3(2, 0x25, 1), F3(~2, ~0x25, ~1)|(1<<12)|(0x7f<<5),    "1,X,d", 0, v6 },
{ "sra",        F3(2, 0x27, 0), F3(~2, ~0x27, ~0)|(1<<12)|(0x7f<<5),    "1,2,d", 0, v6 },
{ "sra",        F3(2, 0x27, 1), F3(~2, ~0x27, ~1)|(1<<12)|(0x7f<<5),    "1,X,d", 0, v6 },
{ "srl",        F3(2, 0x26, 0), F3(~2, ~0x26, ~0)|(1<<12)|(0x7f<<5),    "1,2,d", 0, v6 },
{ "srl",        F3(2, 0x26, 1), F3(~2, ~0x26, ~1)|(1<<12)|(0x7f<<5),    "1,X,d", 0, v6 },

{ "sllx",       F3(2, 0x25, 0)|(1<<12), F3(~2, ~0x25, ~0)|(0x7f<<5),    "1,2,d", 0, v9 },
{ "sllx",       F3(2, 0x25, 1)|(1<<12), F3(~2, ~0x25, ~1)|(0x3f<<6),    "1,Y,d", 0, v9 },
{ "srax",       F3(2, 0x27, 0)|(1<<12), F3(~2, ~0x27, ~0)|(0x7f<<5),    "1,2,d", 0, v9 },
{ "srax",       F3(2, 0x27, 1)|(1<<12), F3(~2, ~0x27, ~1)|(0x3f<<6),    "1,Y,d", 0, v9 },
{ "srlx",       F3(2, 0x26, 0)|(1<<12), F3(~2, ~0x26, ~0)|(0x7f<<5),    "1,2,d", 0, v9 },
{ "srlx",       F3(2, 0x26, 1)|(1<<12), F3(~2, ~0x26, ~1)|(0x3f<<6),    "1,Y,d", 0, v9 },

{ "mulscc",     F3(2, 0x24, 0), F3(~2, ~0x24, ~0)|ASI(~0),      "1,2,d", 0, v6 },
{ "mulscc",     F3(2, 0x24, 1), F3(~2, ~0x24, ~1),              "1,i,d", 0, v6 },

{ "divscc",     F3(2, 0x1d, 0), F3(~2, ~0x1d, ~0)|ASI(~0),      "1,2,d", 0, sparclite },
{ "divscc",     F3(2, 0x1d, 1), F3(~2, ~0x1d, ~1),              "1,i,d", 0, sparclite },

{ "scan",       F3(2, 0x2c, 0), F3(~2, ~0x2c, ~0)|ASI(~0),      "1,2,d", 0, sparclet|sparclite },
{ "scan",       F3(2, 0x2c, 1), F3(~2, ~0x2c, ~1),              "1,i,d", 0, sparclet|sparclite },

{ "popc",       F3(2, 0x2e, 0), F3(~2, ~0x2e, ~0)|RS1_G0|ASI(~0),"2,d", 0, v9 },
{ "popc",       F3(2, 0x2e, 1), F3(~2, ~0x2e, ~1)|RS1_G0,       "i,d", 0, v9 },

{ "clr",        F3(2, 0x02, 0), F3(~2, ~0x02, ~0)|RD_G0|RS1_G0|ASI_RS2(~0),     "d", F_ALIAS, v6 },
{ "clr",        F3(2, 0x02, 1), F3(~2, ~0x02, ~1)|RS1_G0|SIMM13(~0),            "d", F_ALIAS, v6 },
{ "clr",        F3(3, 0x04, 0), F3(~3, ~0x04, ~0)|RD_G0|ASI(~0),                "[1+2]", F_ALIAS, v6 },
{ "clr",        F3(3, 0x04, 0), F3(~3, ~0x04, ~0)|RD_G0|ASI_RS2(~0),            "[1]", F_ALIAS, v6 },
{ "clr",        F3(3, 0x04, 1), F3(~3, ~0x04, ~1)|RD_G0,                        "[1+i]", F_ALIAS, v6 },
{ "clr",        F3(3, 0x04, 1), F3(~3, ~0x04, ~1)|RD_G0,                        "[i+1]", F_ALIAS, v6 },
{ "clr",        F3(3, 0x04, 1), F3(~3, ~0x04, ~1)|RD_G0|RS1_G0,                 "[i]", F_ALIAS, v6 },
{ "clr",        F3(3, 0x04, 1), F3(~3, ~0x04, ~1)|RD_G0|SIMM13(~0),             "[1]", F_ALIAS, v6 },

{ "clrb",       F3(3, 0x05, 0), F3(~3, ~0x05, ~0)|RD_G0|ASI(~0),        "[1+2]", F_ALIAS, v6 },
{ "clrb",       F3(3, 0x05, 0), F3(~3, ~0x05, ~0)|RD_G0|ASI_RS2(~0),    "[1]", F_ALIAS, v6 },
{ "clrb",       F3(3, 0x05, 1), F3(~3, ~0x05, ~1)|RD_G0,                "[1+i]", F_ALIAS, v6 },
{ "clrb",       F3(3, 0x05, 1), F3(~3, ~0x05, ~1)|RD_G0,                "[i+1]", F_ALIAS, v6 },
{ "clrb",       F3(3, 0x05, 1), F3(~3, ~0x05, ~1)|RD_G0|RS1_G0,         "[i]", F_ALIAS, v6 },
{ "clrb",       F3(3, 0x05, 1), F3(~3, ~0x05, ~1)|RD_G0|SIMM13(~0),     "[1]", F_ALIAS, v6 },

{ "clrh",       F3(3, 0x06, 0), F3(~3, ~0x06, ~0)|RD_G0|ASI(~0),        "[1+2]", F_ALIAS, v6 },
{ "clrh",       F3(3, 0x06, 0), F3(~3, ~0x06, ~0)|RD_G0|ASI_RS2(~0),    "[1]", F_ALIAS, v6 },
{ "clrh",       F3(3, 0x06, 1), F3(~3, ~0x06, ~1)|RD_G0,                "[1+i]", F_ALIAS, v6 },
{ "clrh",       F3(3, 0x06, 1), F3(~3, ~0x06, ~1)|RD_G0,                "[i+1]", F_ALIAS, v6 },
{ "clrh",       F3(3, 0x06, 1), F3(~3, ~0x06, ~1)|RD_G0|RS1_G0,         "[i]", F_ALIAS, v6 },
{ "clrh",       F3(3, 0x06, 1), F3(~3, ~0x06, ~1)|RD_G0|SIMM13(~0),     "[1]", F_ALIAS, v6 },

{ "clrx",       F3(3, 0x0e, 0), F3(~3, ~0x0e, ~0)|RD_G0|ASI(~0),        "[1+2]", F_ALIAS, v9 },
{ "clrx",       F3(3, 0x0e, 0), F3(~3, ~0x0e, ~0)|RD_G0|ASI_RS2(~0),    "[1]", F_ALIAS, v9 },
{ "clrx",       F3(3, 0x0e, 1), F3(~3, ~0x0e, ~1)|RD_G0,                "[1+i]", F_ALIAS, v9 },
{ "clrx",       F3(3, 0x0e, 1), F3(~3, ~0x0e, ~1)|RD_G0,                "[i+1]", F_ALIAS, v9 },
{ "clrx",       F3(3, 0x0e, 1), F3(~3, ~0x0e, ~1)|RD_G0|RS1_G0,         "[i]", F_ALIAS, v9 },
{ "clrx",       F3(3, 0x0e, 1), F3(~3, ~0x0e, ~1)|RD_G0|SIMM13(~0),     "[1]", F_ALIAS, v9 },

{ "orcc",       F3(2, 0x12, 0), F3(~2, ~0x12, ~0)|ASI(~0),      "1,2,d", 0, v6 },
{ "orcc",       F3(2, 0x12, 1), F3(~2, ~0x12, ~1),              "1,i,d", 0, v6 },
{ "orcc",       F3(2, 0x12, 1), F3(~2, ~0x12, ~1),              "i,1,d", 0, v6 },

{ "orncc",      F3(2, 0x16, 0), F3(~2, ~0x16, ~0)|ASI(~0),      "1,2,d", 0, v6 },
{ "orncc",      F3(2, 0x16, 1), F3(~2, ~0x16, ~1),              "1,i,d", 0, v6 },

{ "orn",        F3(2, 0x06, 0), F3(~2, ~0x06, ~0)|ASI(~0),      "1,2,d", 0, v6 },
{ "orn",        F3(2, 0x06, 1), F3(~2, ~0x06, ~1),              "1,i,d", 0, v6 },

{ "tst",        F3(2, 0x12, 0), F3(~2, ~0x12, ~0)|RD_G0|ASI_RS2(~0),    "1", 0, v6 },
{ "tst",        F3(2, 0x12, 0), F3(~2, ~0x12, ~0)|RD_G0|RS1_G0|ASI(~0), "2", 0, v6 },
{ "tst",        F3(2, 0x12, 1), F3(~2, ~0x12, ~1)|RD_G0|SIMM13(~0),     "1", 0, v6 },

{ "wr", F3(2, 0x30, 0),         F3(~2, ~0x30, ~0)|ASI(~0),              "1,2,m", 0, v8 },
{ "wr", F3(2, 0x30, 1),         F3(~2, ~0x30, ~1),                      "1,i,m", 0, v8 },
{ "wr", F3(2, 0x30, 0),         F3(~2, ~0x30, ~0)|ASI_RS2(~0),          "1,m", F_ALIAS, v8 },
{ "wr", F3(2, 0x30, 0),         F3(~2, ~0x30, ~0)|RD_G0|ASI(~0),        "1,2,y", 0, v6 },
{ "wr", F3(2, 0x30, 1),         F3(~2, ~0x30, ~1)|RD_G0,                "1,i,y", 0, v6 },
{ "wr", F3(2, 0x30, 0),         F3(~2, ~0x30, ~0)|RD_G0|ASI_RS2(~0),    "1,y", F_ALIAS, v6 },
{ "wr", F3(2, 0x31, 0),         F3(~2, ~0x31, ~0)|RD_G0|ASI(~0),        "1,2,p", 0, v6notv9 },
{ "wr", F3(2, 0x31, 1),         F3(~2, ~0x31, ~1)|RD_G0,                "1,i,p", 0, v6notv9 },
{ "wr", F3(2, 0x31, 0),         F3(~2, ~0x31, ~0)|RD_G0|ASI_RS2(~0),    "1,p", F_ALIAS, v6notv9 },
{ "wr", F3(2, 0x32, 0),         F3(~2, ~0x32, ~0)|RD_G0|ASI(~0),        "1,2,w", 0, v6notv9 },
{ "wr", F3(2, 0x32, 1),         F3(~2, ~0x32, ~1)|RD_G0,                "1,i,w", 0, v6notv9 },
{ "wr", F3(2, 0x32, 0),         F3(~2, ~0x32, ~0)|RD_G0|ASI_RS2(~0),    "1,w", F_ALIAS, v6notv9 },
{ "wr", F3(2, 0x33, 0),         F3(~2, ~0x33, ~0)|RD_G0|ASI(~0),        "1,2,t", 0, v6notv9 },
{ "wr", F3(2, 0x33, 1),         F3(~2, ~0x33, ~1)|RD_G0,                "1,i,t", 0, v6notv9 },
{ "wr", F3(2, 0x33, 0),         F3(~2, ~0x33, ~0)|RD_G0|ASI_RS2(~0),    "1,t", F_ALIAS, v6notv9 },

{ "wr", F3(2, 0x30, 0)|RD(2),   F3(~2, ~0x30, ~0)|RD(~2)|ASI(~0),       "1,2,E", 0, v9 },
{ "wr", F3(2, 0x30, 1)|RD(2),   F3(~2, ~0x30, ~1)|RD(~2),               "1,i,E", 0, v9 },
{ "wr", F3(2, 0x30, 0)|RD(3),   F3(~2, ~0x30, ~0)|RD(~3)|ASI(~0),       "1,2,o", 0, v9 },
{ "wr", F3(2, 0x30, 1)|RD(3),   F3(~2, ~0x30, ~1)|RD(~3),               "1,i,o", 0, v9 },
{ "wr", F3(2, 0x30, 0)|RD(6),   F3(~2, ~0x30, ~0)|RD(~6)|ASI(~0),       "1,2,s", 0, v9 },
{ "wr", F3(2, 0x30, 1)|RD(6),   F3(~2, ~0x30, ~1)|RD(~6),               "1,i,s", 0, v9 },

{ "wr", F3(2, 0x30, 0)|RD(16),  F3(~2, ~0x30, ~0)|RD(~16)|ASI(~0),      "1,2,_", 0, v9a },
{ "wr", F3(2, 0x30, 1)|RD(16),  F3(~2, ~0x30, ~1)|RD(~16),              "1,i,_", 0, v9a },
{ "wr", F3(2, 0x30, 0)|RD(17),  F3(~2, ~0x30, ~0)|RD(~17)|ASI(~0),      "1,2,_", 0, v9a },
{ "wr", F3(2, 0x30, 1)|RD(17),  F3(~2, ~0x30, ~1)|RD(~17),              "1,i,_", 0, v9a },
{ "wr", F3(2, 0x30, 0)|RD(18),  F3(~2, ~0x30, ~0)|RD(~18)|ASI(~0),      "1,2,_", 0, v9a },
{ "wr", F3(2, 0x30, 1)|RD(18),  F3(~2, ~0x30, ~1)|RD(~18),              "1,i,_", 0, v9a },
{ "wr", F3(2, 0x30, 0)|RD(19),  F3(~2, ~0x30, ~0)|RD(~19)|ASI(~0),      "1,2,_", 0, v9a },
{ "wr", F3(2, 0x30, 1)|RD(19),  F3(~2, ~0x30, ~1)|RD(~19),              "1,i,_", 0, v9a },
{ "wr", F3(2, 0x30, 0)|RD(20),  F3(~2, ~0x30, ~0)|RD(~20)|ASI(~0),      "1,2,_", 0, v9a },
{ "wr", F3(2, 0x30, 1)|RD(20),  F3(~2, ~0x30, ~1)|RD(~20),              "1,i,_", 0, v9a },
{ "wr", F3(2, 0x30, 0)|RD(21),  F3(~2, ~0x30, ~0)|RD(~21)|ASI(~0),      "1,2,_", 0, v9a },
{ "wr", F3(2, 0x30, 1)|RD(21),  F3(~2, ~0x30, ~1)|RD(~21),              "1,i,_", 0, v9a },
{ "wr", F3(2, 0x30, 0)|RD(22),  F3(~2, ~0x30, ~0)|RD(~22)|ASI(~0),      "1,2,_", 0, v9a },
{ "wr", F3(2, 0x30, 1)|RD(22),  F3(~2, ~0x30, ~1)|RD(~22),              "1,i,_", 0, v9a },
{ "wr", F3(2, 0x30, 0)|RD(23),  F3(~2, ~0x30, ~0)|RD(~23)|ASI(~0),      "1,2,_", 0, v9a },
{ "wr", F3(2, 0x30, 1)|RD(23),  F3(~2, ~0x30, ~1)|RD(~23),              "1,i,_", 0, v9a },
{ "wr", F3(2, 0x30, 0)|RD(24),  F3(~2, ~0x30, ~0)|RD(~24)|ASI(~0),      "1,2,_", 0, v9b },
{ "wr", F3(2, 0x30, 1)|RD(24),  F3(~2, ~0x30, ~1)|RD(~24),              "1,i,_", 0, v9b },
{ "wr", F3(2, 0x30, 0)|RD(25),  F3(~2, ~0x30, ~0)|RD(~25)|ASI(~0),      "1,2,_", 0, v9b },
{ "wr", F3(2, 0x30, 1)|RD(25),  F3(~2, ~0x30, ~1)|RD(~25),              "1,i,_", 0, v9b },

{ "rd", F3(2, 0x28, 0),                 F3(~2, ~0x28, ~0)|SIMM13(~0),           "M,d", 0, v8 },
{ "rd", F3(2, 0x28, 0),                 F3(~2, ~0x28, ~0)|RS1_G0|SIMM13(~0),    "y,d", 0, v6 },
{ "rd", F3(2, 0x29, 0),                 F3(~2, ~0x29, ~0)|RS1_G0|SIMM13(~0),    "p,d", 0, v6notv9 },
{ "rd", F3(2, 0x2a, 0),                 F3(~2, ~0x2a, ~0)|RS1_G0|SIMM13(~0),    "w,d", 0, v6notv9 },
{ "rd", F3(2, 0x2b, 0),                 F3(~2, ~0x2b, ~0)|RS1_G0|SIMM13(~0),    "t,d", 0, v6notv9 },

{ "rd", F3(2, 0x28, 0)|RS1(2),          F3(~2, ~0x28, ~0)|RS1(~2)|SIMM13(~0),   "E,d", 0, v9 },
{ "rd", F3(2, 0x28, 0)|RS1(3),          F3(~2, ~0x28, ~0)|RS1(~3)|SIMM13(~0),   "o,d", 0, v9 },
{ "rd", F3(2, 0x28, 0)|RS1(4),          F3(~2, ~0x28, ~0)|RS1(~4)|SIMM13(~0),   "W,d", 0, v9 },
{ "rd", F3(2, 0x28, 0)|RS1(5),          F3(~2, ~0x28, ~0)|RS1(~5)|SIMM13(~0),   "P,d", 0, v9 },
{ "rd", F3(2, 0x28, 0)|RS1(6),          F3(~2, ~0x28, ~0)|RS1(~6)|SIMM13(~0),   "s,d", 0, v9 },

{ "rd", F3(2, 0x28, 0)|RS1(16),         F3(~2, ~0x28, ~0)|RS1(~16)|SIMM13(~0),  "/,d", 0, v9a },
{ "rd", F3(2, 0x28, 0)|RS1(17),         F3(~2, ~0x28, ~0)|RS1(~17)|SIMM13(~0),  "/,d", 0, v9a },
{ "rd", F3(2, 0x28, 0)|RS1(18),         F3(~2, ~0x28, ~0)|RS1(~18)|SIMM13(~0),  "/,d", 0, v9a },
{ "rd", F3(2, 0x28, 0)|RS1(19),         F3(~2, ~0x28, ~0)|RS1(~19)|SIMM13(~0),  "/,d", 0, v9a },
{ "rd", F3(2, 0x28, 0)|RS1(22),         F3(~2, ~0x28, ~0)|RS1(~22)|SIMM13(~0),  "/,d", 0, v9a },
{ "rd", F3(2, 0x28, 0)|RS1(23),         F3(~2, ~0x28, ~0)|RS1(~23)|SIMM13(~0),  "/,d", 0, v9a },
{ "rd", F3(2, 0x28, 0)|RS1(24),         F3(~2, ~0x28, ~0)|RS1(~24)|SIMM13(~0),  "/,d", 0, v9b },
{ "rd", F3(2, 0x28, 0)|RS1(25),         F3(~2, ~0x28, ~0)|RS1(~25)|SIMM13(~0),  "/,d", 0, v9b },

{ "rdpr",       F3(2, 0x2a, 0),         F3(~2, ~0x2a, ~0)|SIMM13(~0),   "?,d", 0, v9 },
{ "wrpr",       F3(2, 0x32, 0),         F3(~2, ~0x32, ~0),              "1,2,!", 0, v9 },
{ "wrpr",       F3(2, 0x32, 0),         F3(~2, ~0x32, ~0)|SIMM13(~0),   "1,!", 0, v9 },
{ "wrpr",       F3(2, 0x32, 1),         F3(~2, ~0x32, ~1),              "1,i,!", 0, v9 },
{ "wrpr",       F3(2, 0x32, 1),         F3(~2, ~0x32, ~1),              "i,1,!", F_ALIAS, v9 },
{ "wrpr",       F3(2, 0x32, 1),         F3(~2, ~0x32, ~1)|RS1(~0),      "i,!", 0, v9 },

{ "rdhpr",      F3(2, 0x29, 0),         F3(~2, ~0x29, ~0)|SIMM13(~0),   "$,d", 0, v9 },
{ "wrhpr",      F3(2, 0x33, 0),         F3(~2, ~0x33, ~0),              "1,2,%", 0, v9 },
{ "wrhpr",      F3(2, 0x33, 0),         F3(~2, ~0x33, ~0)|SIMM13(~0),   "1,%", 0, v9 },
{ "wrhpr",      F3(2, 0x33, 1),         F3(~2, ~0x33, ~1),              "1,i,%", 0, v9 },
{ "wrhpr",      F3(2, 0x33, 1),         F3(~2, ~0x33, ~1),              "i,1,%", F_ALIAS, v9 },
{ "wrhpr",      F3(2, 0x33, 1),         F3(~2, ~0x33, ~1)|RS1(~0),      "i,%", 0, v9 },

{ "mov",        F3(2, 0x30, 0), F3(~2, ~0x30, ~0)|ASI(~0),              "1,2,m", F_ALIAS, v8 },
{ "mov",        F3(2, 0x30, 1), F3(~2, ~0x30, ~1),                      "1,i,m", F_ALIAS, v8 },
{ "mov",        F3(2, 0x30, 0), F3(~2, ~0x30, ~0)|RD_G0|ASI(~0),        "1,2,y", F_ALIAS, v6 },
{ "mov",        F3(2, 0x30, 1), F3(~2, ~0x30, ~1)|RD_G0,                "1,i,y", F_ALIAS, v6 },
{ "mov",        F3(2, 0x31, 0), F3(~2, ~0x31, ~0)|RD_G0|ASI(~0),        "1,2,p", F_ALIAS, v6notv9 },
{ "mov",        F3(2, 0x31, 1), F3(~2, ~0x31, ~1)|RD_G0,                "1,i,p", F_ALIAS, v6notv9 },
{ "mov",        F3(2, 0x32, 0), F3(~2, ~0x32, ~0)|RD_G0|ASI(~0),        "1,2,w", F_ALIAS, v6notv9 },
{ "mov",        F3(2, 0x32, 1), F3(~2, ~0x32, ~1)|RD_G0,                "1,i,w", F_ALIAS, v6notv9 },
{ "mov",        F3(2, 0x33, 0), F3(~2, ~0x33, ~0)|RD_G0|ASI(~0),        "1,2,t", F_ALIAS, v6notv9 },
{ "mov",        F3(2, 0x33, 1), F3(~2, ~0x33, ~1)|RD_G0,                "1,i,t", F_ALIAS, v6notv9 },

{ "mov",        F3(2, 0x28, 0), F3(~2, ~0x28, ~0)|SIMM13(~0),           "M,d", F_ALIAS, v8 },
{ "mov",        F3(2, 0x28, 0), F3(~2, ~0x28, ~0)|RS1_G0|SIMM13(~0),    "y,d", F_ALIAS, v6 },
{ "mov",        F3(2, 0x29, 0), F3(~2, ~0x29, ~0)|RS1_G0|SIMM13(~0),    "p,d", F_ALIAS, v6notv9 },
{ "mov",        F3(2, 0x2a, 0), F3(~2, ~0x2a, ~0)|RS1_G0|SIMM13(~0),    "w,d", F_ALIAS, v6notv9 },
{ "mov",        F3(2, 0x2b, 0), F3(~2, ~0x2b, ~0)|RS1_G0|SIMM13(~0),    "t,d", F_ALIAS, v6notv9 },

{ "mov",        F3(2, 0x30, 0), F3(~2, ~0x30, ~0)|ASI_RS2(~0),          "1,m", F_ALIAS, v8 },
{ "mov",        F3(2, 0x30, 1), F3(~2, ~0x30, ~1),                      "i,m", F_ALIAS, v8 },
{ "mov",        F3(2, 0x30, 1), F3(~2, ~0x30, ~1)|SIMM13(~0),           "1,m", F_ALIAS, v8 },
{ "mov",        F3(2, 0x30, 0), F3(~2, ~0x30, ~0)|RD_G0|ASI_RS2(~0),    "1,y", F_ALIAS, v6 },
{ "mov",        F3(2, 0x30, 1), F3(~2, ~0x30, ~1)|RD_G0,                "i,y", F_ALIAS, v6 },
{ "mov",        F3(2, 0x30, 1), F3(~2, ~0x30, ~1)|RD_G0|SIMM13(~0),     "1,y", F_ALIAS, v6 },
{ "mov",        F3(2, 0x31, 0), F3(~2, ~0x31, ~0)|RD_G0|ASI_RS2(~0),    "1,p", F_ALIAS, v6notv9 },
{ "mov",        F3(2, 0x31, 1), F3(~2, ~0x31, ~1)|RD_G0,                "i,p", F_ALIAS, v6notv9 },
{ "mov",        F3(2, 0x31, 1), F3(~2, ~0x31, ~1)|RD_G0|SIMM13(~0),     "1,p", F_ALIAS, v6notv9 },
{ "mov",        F3(2, 0x32, 0), F3(~2, ~0x32, ~0)|RD_G0|ASI_RS2(~0),    "1,w", F_ALIAS, v6notv9 },
{ "mov",        F3(2, 0x32, 1), F3(~2, ~0x32, ~1)|RD_G0,                "i,w", F_ALIAS, v6notv9 },
{ "mov",        F3(2, 0x32, 1), F3(~2, ~0x32, ~1)|RD_G0|SIMM13(~0),     "1,w", F_ALIAS, v6notv9 },
{ "mov",        F3(2, 0x33, 0), F3(~2, ~0x33, ~0)|RD_G0|ASI_RS2(~0),    "1,t", F_ALIAS, v6notv9 },
{ "mov",        F3(2, 0x33, 1), F3(~2, ~0x33, ~1)|RD_G0,                "i,t", F_ALIAS, v6notv9 },
{ "mov",        F3(2, 0x33, 1), F3(~2, ~0x33, ~1)|RD_G0|SIMM13(~0),     "1,t", F_ALIAS, v6notv9 },

{ "mov",        F3(2, 0x02, 0), F3(~2, ~0x02, ~0)|RS1_G0|ASI(~0),       "2,d", 0, v6 },
{ "mov",        F3(2, 0x02, 1), F3(~2, ~0x02, ~1)|RS1_G0,               "i,d", 0, v6 },
{ "mov",        F3(2, 0x02, 0), F3(~2, ~0x02, ~0)|ASI_RS2(~0),          "1,d", 0, v6 },
{ "mov",        F3(2, 0x02, 1), F3(~2, ~0x02, ~1)|SIMM13(~0),           "1,d", 0, v6 },

{ "or", F3(2, 0x02, 0), F3(~2, ~0x02, ~0)|ASI(~0),      "1,2,d", 0, v6 },
{ "or", F3(2, 0x02, 1), F3(~2, ~0x02, ~1),              "1,i,d", 0, v6 },
{ "or", F3(2, 0x02, 1), F3(~2, ~0x02, ~1),              "i,1,d", 0, v6 },

{ "bset",       F3(2, 0x02, 0), F3(~2, ~0x02, ~0)|ASI(~0),      "2,r", F_ALIAS, v6 },
{ "bset",       F3(2, 0x02, 1), F3(~2, ~0x02, ~1),              "i,r", F_ALIAS, v6 },

{ "andn",       F3(2, 0x05, 0), F3(~2, ~0x05, ~0)|ASI(~0),      "1,2,d", 0, v6 },
{ "andn",       F3(2, 0x05, 1), F3(~2, ~0x05, ~1),              "1,i,d", 0, v6 },

{ "andncc",     F3(2, 0x15, 0), F3(~2, ~0x15, ~0)|ASI(~0),      "1,2,d", 0, v6 },
{ "andncc",     F3(2, 0x15, 1), F3(~2, ~0x15, ~1),              "1,i,d", 0, v6 },

{ "bclr",       F3(2, 0x05, 0), F3(~2, ~0x05, ~0)|ASI(~0),      "2,r", F_ALIAS, v6 },
{ "bclr",       F3(2, 0x05, 1), F3(~2, ~0x05, ~1),              "i,r", F_ALIAS, v6 },

{ "cmp",        F3(2, 0x14, 0), F3(~2, ~0x14, ~0)|RD_G0|ASI(~0),        "1,2", 0, v6 },
{ "cmp",        F3(2, 0x14, 1), F3(~2, ~0x14, ~1)|RD_G0,                "1,i", 0, v6 },

{ "sub",        F3(2, 0x04, 0), F3(~2, ~0x04, ~0)|ASI(~0),      "1,2,d", 0, v6 },
{ "sub",        F3(2, 0x04, 1), F3(~2, ~0x04, ~1),              "1,i,d", 0, v6 },

{ "subcc",      F3(2, 0x14, 0), F3(~2, ~0x14, ~0)|ASI(~0),      "1,2,d", 0, v6 },
{ "subcc",      F3(2, 0x14, 1), F3(~2, ~0x14, ~1),              "1,i,d", 0, v6 },

{ "subc",       F3(2, 0x0c, 0), F3(~2, ~0x0c, ~0)|ASI(~0),      "1,2,d", 0, v6 },
{ "subc",       F3(2, 0x0c, 1), F3(~2, ~0x0c, ~1),              "1,i,d", 0, v6 },

{ "subccc",     F3(2, 0x1c, 0), F3(~2, ~0x1c, ~0)|ASI(~0),      "1,2,d", 0, v6 },
{ "subccc",     F3(2, 0x1c, 1), F3(~2, ~0x1c, ~1),              "1,i,d", 0, v6 },

{ "and",        F3(2, 0x01, 0), F3(~2, ~0x01, ~0)|ASI(~0),      "1,2,d", 0, v6 },
{ "and",        F3(2, 0x01, 1), F3(~2, ~0x01, ~1),              "1,i,d", 0, v6 },
{ "and",        F3(2, 0x01, 1), F3(~2, ~0x01, ~1),              "i,1,d", 0, v6 },

{ "andcc",      F3(2, 0x11, 0), F3(~2, ~0x11, ~0)|ASI(~0),      "1,2,d", 0, v6 },
{ "andcc",      F3(2, 0x11, 1), F3(~2, ~0x11, ~1),              "1,i,d", 0, v6 },
{ "andcc",      F3(2, 0x11, 1), F3(~2, ~0x11, ~1),              "i,1,d", 0, v6 },

{ "dec",        F3(2, 0x04, 1)|SIMM13(0x1), F3(~2, ~0x04, ~1)|SIMM13(~0x0001), "r", F_ALIAS, v6 },
{ "dec",        F3(2, 0x04, 1),             F3(~2, ~0x04, ~1),                 "i,r", F_ALIAS, v8 },
{ "deccc",      F3(2, 0x14, 1)|SIMM13(0x1), F3(~2, ~0x14, ~1)|SIMM13(~0x0001), "r", F_ALIAS, v6 },
{ "deccc",      F3(2, 0x14, 1),             F3(~2, ~0x14, ~1),                 "i,r", F_ALIAS, v8 },
{ "inc",        F3(2, 0x00, 1)|SIMM13(0x1), F3(~2, ~0x00, ~1)|SIMM13(~0x0001), "r", F_ALIAS, v6 },
{ "inc",        F3(2, 0x00, 1),             F3(~2, ~0x00, ~1),                 "i,r", F_ALIAS, v8 },
{ "inccc",      F3(2, 0x10, 1)|SIMM13(0x1), F3(~2, ~0x10, ~1)|SIMM13(~0x0001), "r", F_ALIAS, v6 },
{ "inccc",      F3(2, 0x10, 1),             F3(~2, ~0x10, ~1),                 "i,r", F_ALIAS, v8 },

{ "btst",       F3(2, 0x11, 0), F3(~2, ~0x11, ~0)|RD_G0|ASI(~0), "1,2", F_ALIAS, v6 },
{ "btst",       F3(2, 0x11, 1), F3(~2, ~0x11, ~1)|RD_G0, "i,1", F_ALIAS, v6 },

{ "neg",        F3(2, 0x04, 0), F3(~2, ~0x04, ~0)|RS1_G0|ASI(~0), "2,d", F_ALIAS, v6 },
{ "neg",        F3(2, 0x04, 0), F3(~2, ~0x04, ~0)|RS1_G0|ASI(~0), "O", F_ALIAS, v6 },

{ "add",        F3(2, 0x00, 0), F3(~2, ~0x00, ~0)|ASI(~0),      "1,2,d", 0, v6 },
{ "add",        F3(2, 0x00, 1), F3(~2, ~0x00, ~1),              "1,i,d", 0, v6 },
{ "add",        F3(2, 0x00, 1), F3(~2, ~0x00, ~1),              "i,1,d", 0, v6 },
{ "addcc",      F3(2, 0x10, 0), F3(~2, ~0x10, ~0)|ASI(~0),      "1,2,d", 0, v6 },
{ "addcc",      F3(2, 0x10, 1), F3(~2, ~0x10, ~1),              "1,i,d", 0, v6 },
{ "addcc",      F3(2, 0x10, 1), F3(~2, ~0x10, ~1),              "i,1,d", 0, v6 },

{ "addc",       F3(2, 0x08, 0), F3(~2, ~0x08, ~0)|ASI(~0),      "1,2,d", 0, v6 },
{ "addc",       F3(2, 0x08, 1), F3(~2, ~0x08, ~1),              "1,i,d", 0, v6 },
{ "addc",       F3(2, 0x08, 1), F3(~2, ~0x08, ~1),              "i,1,d", 0, v6 },

{ "addccc",     F3(2, 0x18, 0), F3(~2, ~0x18, ~0)|ASI(~0),      "1,2,d", 0, v6 },
{ "addccc",     F3(2, 0x18, 1), F3(~2, ~0x18, ~1),              "1,i,d", 0, v6 },
{ "addccc",     F3(2, 0x18, 1), F3(~2, ~0x18, ~1),              "i,1,d", 0, v6 },

{ "smul",       F3(2, 0x0b, 0), F3(~2, ~0x0b, ~0)|ASI(~0),      "1,2,d", 0, v8 },
{ "smul",       F3(2, 0x0b, 1), F3(~2, ~0x0b, ~1),              "1,i,d", 0, v8 },
{ "smul",       F3(2, 0x0b, 1), F3(~2, ~0x0b, ~1),              "i,1,d", 0, v8 },
{ "smulcc",     F3(2, 0x1b, 0), F3(~2, ~0x1b, ~0)|ASI(~0),      "1,2,d", 0, v8 },
{ "smulcc",     F3(2, 0x1b, 1), F3(~2, ~0x1b, ~1),              "1,i,d", 0, v8 },
{ "smulcc",     F3(2, 0x1b, 1), F3(~2, ~0x1b, ~1),              "i,1,d", 0, v8 },
{ "umul",       F3(2, 0x0a, 0), F3(~2, ~0x0a, ~0)|ASI(~0),      "1,2,d", 0, v8 },
{ "umul",       F3(2, 0x0a, 1), F3(~2, ~0x0a, ~1),              "1,i,d", 0, v8 },
{ "umul",       F3(2, 0x0a, 1), F3(~2, ~0x0a, ~1),              "i,1,d", 0, v8 },
{ "umulcc",     F3(2, 0x1a, 0), F3(~2, ~0x1a, ~0)|ASI(~0),      "1,2,d", 0, v8 },
{ "umulcc",     F3(2, 0x1a, 1), F3(~2, ~0x1a, ~1),              "1,i,d", 0, v8 },
{ "umulcc",     F3(2, 0x1a, 1), F3(~2, ~0x1a, ~1),              "i,1,d", 0, v8 },
{ "sdiv",       F3(2, 0x0f, 0), F3(~2, ~0x0f, ~0)|ASI(~0),      "1,2,d", 0, v8 },
{ "sdiv",       F3(2, 0x0f, 1), F3(~2, ~0x0f, ~1),              "1,i,d", 0, v8 },
{ "sdiv",       F3(2, 0x0f, 1), F3(~2, ~0x0f, ~1),              "i,1,d", 0, v8 },
{ "sdivcc",     F3(2, 0x1f, 0), F3(~2, ~0x1f, ~0)|ASI(~0),      "1,2,d", 0, v8 },
{ "sdivcc",     F3(2, 0x1f, 1), F3(~2, ~0x1f, ~1),              "1,i,d", 0, v8 },
{ "sdivcc",     F3(2, 0x1f, 1), F3(~2, ~0x1f, ~1),              "i,1,d", 0, v8 },
{ "udiv",       F3(2, 0x0e, 0), F3(~2, ~0x0e, ~0)|ASI(~0),      "1,2,d", 0, v8 },
{ "udiv",       F3(2, 0x0e, 1), F3(~2, ~0x0e, ~1),              "1,i,d", 0, v8 },
{ "udiv",       F3(2, 0x0e, 1), F3(~2, ~0x0e, ~1),              "i,1,d", 0, v8 },
{ "udivcc",     F3(2, 0x1e, 0), F3(~2, ~0x1e, ~0)|ASI(~0),      "1,2,d", 0, v8 },
{ "udivcc",     F3(2, 0x1e, 1), F3(~2, ~0x1e, ~1),              "1,i,d", 0, v8 },
{ "udivcc",     F3(2, 0x1e, 1), F3(~2, ~0x1e, ~1),              "i,1,d", 0, v8 },

{ "mulx",       F3(2, 0x09, 0), F3(~2, ~0x09, ~0)|ASI(~0),      "1,2,d", 0, v9 },
{ "mulx",       F3(2, 0x09, 1), F3(~2, ~0x09, ~1),              "1,i,d", 0, v9 },
{ "sdivx",      F3(2, 0x2d, 0), F3(~2, ~0x2d, ~0)|ASI(~0),      "1,2,d", 0, v9 },
{ "sdivx",      F3(2, 0x2d, 1), F3(~2, ~0x2d, ~1),              "1,i,d", 0, v9 },
{ "udivx",      F3(2, 0x0d, 0), F3(~2, ~0x0d, ~0)|ASI(~0),      "1,2,d", 0, v9 },
{ "udivx",      F3(2, 0x0d, 1), F3(~2, ~0x0d, ~1),              "1,i,d", 0, v9 },

{ "call",       F1(0x1), F1(~0x1), "L", F_JSR|F_DELAYED, v6 },
{ "call",       F1(0x1), F1(~0x1), "L,#", F_JSR|F_DELAYED, v6 },

{ "call",       F3(2, 0x38, 0)|RD(0xf), F3(~2, ~0x38, ~0)|RD(~0xf)|ASI(~0),     "1+2", F_JSR|F_DELAYED, v6 },
{ "call",       F3(2, 0x38, 0)|RD(0xf), F3(~2, ~0x38, ~0)|RD(~0xf)|ASI(~0),     "1+2,#", F_JSR|F_DELAYED, v6 },
{ "call",       F3(2, 0x38, 0)|RD(0xf), F3(~2, ~0x38, ~0)|RD(~0xf)|ASI_RS2(~0), "1", F_JSR|F_DELAYED, v6 },
{ "call",       F3(2, 0x38, 0)|RD(0xf), F3(~2, ~0x38, ~0)|RD(~0xf)|ASI_RS2(~0), "1,#", F_JSR|F_DELAYED, v6 },
{ "call",       F3(2, 0x38, 1)|RD(0xf), F3(~2, ~0x38, ~1)|RD(~0xf),             "1+i", F_JSR|F_DELAYED, v6 },
{ "call",       F3(2, 0x38, 1)|RD(0xf), F3(~2, ~0x38, ~1)|RD(~0xf),             "1+i,#", F_JSR|F_DELAYED, v6 },
{ "call",       F3(2, 0x38, 1)|RD(0xf), F3(~2, ~0x38, ~1)|RD(~0xf),             "i+1", F_JSR|F_DELAYED, v6 },
{ "call",       F3(2, 0x38, 1)|RD(0xf), F3(~2, ~0x38, ~1)|RD(~0xf),             "i+1,#", F_JSR|F_DELAYED, v6 },
{ "call",       F3(2, 0x38, 1)|RD(0xf), F3(~2, ~0x38, ~1)|RD(~0xf)|RS1_G0,      "i", F_JSR|F_DELAYED, v6 },
{ "call",       F3(2, 0x38, 1)|RD(0xf), F3(~2, ~0x38, ~1)|RD(~0xf)|RS1_G0,      "i,#", F_JSR|F_DELAYED, v6 },
{ "call",       F3(2, 0x38, 1)|RD(0xf), F3(~2, ~0x38, ~1)|RD(~0xf)|SIMM13(~0),  "1", F_JSR|F_DELAYED, v6 },
{ "call",       F3(2, 0x38, 1)|RD(0xf), F3(~2, ~0x38, ~1)|RD(~0xf)|SIMM13(~0),  "1,#", F_JSR|F_DELAYED, v6 },


#define br(opcode, mask, lose, flags) \
 { opcode, (mask)|ANNUL, (lose),       ",a l",   (flags), v6 }, \
 { opcode, (mask)      , (lose)|ANNUL, "l",     (flags), v6 }

#define brx(opcode, mask, lose, flags) \
 { opcode, (mask)|(2<<20)|BPRED, ANNUL|(lose), "Z,G",      (flags), v9 }, \
 { opcode, (mask)|(2<<20)|BPRED, ANNUL|(lose), ",T Z,G",   (flags), v9 }, \
 { opcode, (mask)|(2<<20)|BPRED|ANNUL, (lose), ",a Z,G",   (flags), v9 }, \
 { opcode, (mask)|(2<<20)|BPRED|ANNUL, (lose), ",a,T Z,G", (flags), v9 }, \
 { opcode, (mask)|(2<<20), ANNUL|BPRED|(lose), ",N Z,G",   (flags), v9 }, \
 { opcode, (mask)|(2<<20)|ANNUL, BPRED|(lose), ",a,N Z,G", (flags), v9 }, \
 { opcode, (mask)|BPRED, ANNUL|(lose)|(2<<20), "z,G",      (flags), v9 }, \
 { opcode, (mask)|BPRED, ANNUL|(lose)|(2<<20), ",T z,G",   (flags), v9 }, \
 { opcode, (mask)|BPRED|ANNUL, (lose)|(2<<20), ",a z,G",   (flags), v9 }, \
 { opcode, (mask)|BPRED|ANNUL, (lose)|(2<<20), ",a,T z,G", (flags), v9 }, \
 { opcode, (mask), ANNUL|BPRED|(lose)|(2<<20), ",N z,G",   (flags), v9 }, \
 { opcode, (mask)|ANNUL, BPRED|(lose)|(2<<20), ",a,N z,G", (flags), v9 }

#define tr(opcode, mask, lose, flags) \
 { opcode, (mask)|(2<<11)|IMMED, (lose)|RS1_G0, "Z,i",   (flags), v9 }, \
 { opcode, (mask)|(2<<11)|IMMED, (lose),        "Z,1+i", (flags), v9 }, \
 { opcode, (mask)|(2<<11), IMMED|(lose),        "Z,1+2", (flags), v9 }, \
 { opcode, (mask)|(2<<11), IMMED|(lose)|RS2_G0, "Z,1",   (flags), v9 }, \
 { opcode, (mask)|IMMED, (lose)|RS1_G0, "z,i",   (flags)|F_ALIAS, v9 }, \
 { opcode, (mask)|IMMED, (lose),        "z,1+i", (flags)|F_ALIAS, v9 }, \
 { opcode, (mask), IMMED|(lose),        "z,1+2", (flags)|F_ALIAS, v9 }, \
 { opcode, (mask), IMMED|(lose)|RS2_G0, "z,1",   (flags)|F_ALIAS, v9 }, \
 { opcode, (mask)|IMMED, (lose)|RS1_G0,         "i",     (flags), v6 }, \
 { opcode, (mask)|IMMED, (lose),                "1+i",   (flags), v6 }, \
 { opcode, (mask), IMMED|(lose),                "1+2",   (flags), v6 }, \
 { opcode, (mask), IMMED|(lose)|RS2_G0,         "1",     (flags), v6 }

#define cond(bop, top, mask, flags) \
  brx(bop, F2(0, 1)|(mask), F2(~0, ~1)|((~mask)&COND(~0)), F_DELAYED|(flags)), \
  br(bop,  F2(0, 2)|(mask), F2(~0, ~2)|((~mask)&COND(~0)), F_DELAYED|(flags)), \
  tr(top,  F3(2, 0x3a, 0)|(mask), F3(~2, ~0x3a, 0)|((~mask)&COND(~0)), ((flags) & ~(F_UNBR|F_CONDBR)))

cond ("b",      "ta",   CONDA, F_UNBR),
cond ("ba",     "t",    CONDA, F_UNBR|F_ALIAS),

cond ("bcc",    "tcc",  CONDCC, F_CONDBR),
cond ("bcs",    "tcs",  CONDCS, F_CONDBR),
cond ("be",     "te",   CONDE, F_CONDBR),
cond ("beq",    "teq",  CONDE, F_CONDBR|F_ALIAS),
cond ("bg",     "tg",   CONDG, F_CONDBR),
cond ("bgt",    "tgt",  CONDG, F_CONDBR|F_ALIAS),
cond ("bge",    "tge",  CONDGE, F_CONDBR),
cond ("bgeu",   "tgeu", CONDGEU, F_CONDBR|F_ALIAS),
cond ("bgu",    "tgu",  CONDGU, F_CONDBR),
cond ("bl",     "tl",   CONDL, F_CONDBR),
cond ("blt",    "tlt",  CONDL, F_CONDBR|F_ALIAS),
cond ("ble",    "tle",  CONDLE, F_CONDBR),
cond ("bleu",   "tleu", CONDLEU, F_CONDBR),
cond ("blu",    "tlu",  CONDLU, F_CONDBR|F_ALIAS),
cond ("bn",     "tn",   CONDN, F_CONDBR),
cond ("bne",    "tne",  CONDNE, F_CONDBR),
cond ("bneg",   "tneg", CONDNEG, F_CONDBR),
cond ("bnz",    "tnz",  CONDNZ, F_CONDBR|F_ALIAS),
cond ("bpos",   "tpos", CONDPOS, F_CONDBR),
cond ("bvc",    "tvc",  CONDVC, F_CONDBR),
cond ("bvs",    "tvs",  CONDVS, F_CONDBR),
cond ("bz",     "tz",   CONDZ, F_CONDBR|F_ALIAS),

#undef cond
#undef br
#undef brr
#undef tr

#define brr(opcode, mask, lose, flags) \
 { opcode, (mask)|BPRED, ANNUL|(lose), "1,k",      F_DELAYED|(flags), v9 }, \
 { opcode, (mask)|BPRED, ANNUL|(lose), ",T 1,k",   F_DELAYED|(flags), v9 }, \
 { opcode, (mask)|BPRED|ANNUL, (lose), ",a 1,k",   F_DELAYED|(flags), v9 }, \
 { opcode, (mask)|BPRED|ANNUL, (lose), ",a,T 1,k", F_DELAYED|(flags), v9 }, \
 { opcode, (mask), ANNUL|BPRED|(lose), ",N 1,k",   F_DELAYED|(flags), v9 }, \
 { opcode, (mask)|ANNUL, BPRED|(lose), ",a,N 1,k", F_DELAYED|(flags), v9 }

#define condr(bop, mask, flags) \
  brr(bop, F2(0, 3)|COND(mask), F2(~0, ~3)|COND(~(mask)), (flags))

 condr("brnz", 0x5, F_CONDBR),
 condr("brz", 0x1, F_CONDBR),
 condr("brgez", 0x7, F_CONDBR),
 condr("brlz", 0x3, F_CONDBR),
 condr("brlez", 0x2, F_CONDBR),
 condr("brgz", 0x6, F_CONDBR),

#undef condr
#undef brr

#define movr(opcode, mask, flags) \
 { opcode, F3(2, 0x2f, 0)|RCOND(mask), F3(~2, ~0x2f, ~0)|RCOND(~(mask)), "1,2,d", (flags), v9 }, \
 { opcode, F3(2, 0x2f, 1)|RCOND(mask), F3(~2, ~0x2f, ~1)|RCOND(~(mask)), "1,j,d", (flags), v9 }

#define fmrrs(opcode, mask, lose, flags) \
 { opcode, (mask), (lose), "1,f,g", (flags) | F_FLOAT, v9 }
#define fmrrd(opcode, mask, lose, flags) \
 { opcode, (mask), (lose), "1,B,H", (flags) | F_FLOAT, v9 }
#define fmrrq(opcode, mask, lose, flags) \
 { opcode, (mask), (lose), "1,R,J", (flags) | F_FLOAT, v9 }

#define fmovrs(mop, mask, flags) \
  fmrrs(mop, F3(2, 0x35, 0)|OPF_LOW5(5)|RCOND(mask), F3(~2, ~0x35, 0)|OPF_LOW5(~5)|RCOND(~(mask)), (flags))
#define fmovrd(mop, mask, flags) \
  fmrrd(mop, F3(2, 0x35, 0)|OPF_LOW5(6)|RCOND(mask), F3(~2, ~0x35, 0)|OPF_LOW5(~6)|RCOND(~(mask)), (flags))
#define fmovrq(mop, mask, flags) \
  fmrrq(mop, F3(2, 0x35, 0)|OPF_LOW5(7)|RCOND(mask), F3(~2, ~0x35, 0)|OPF_LOW5(~7)|RCOND(~(mask)), (flags))

 movr("movrne", 0x5, 0),
 movr("movre", 0x1, 0),
 movr("movrgez", 0x7, 0),
 movr("movrlz", 0x3, 0),
 movr("movrlez", 0x2, 0),
 movr("movrgz", 0x6, 0),
 movr("movrnz", 0x5, F_ALIAS),
 movr("movrz", 0x1, F_ALIAS),

 fmovrs("fmovrsne", 0x5, 0),
 fmovrs("fmovrse", 0x1, 0),
 fmovrs("fmovrsgez", 0x7, 0),
 fmovrs("fmovrslz", 0x3, 0),
 fmovrs("fmovrslez", 0x2, 0),
 fmovrs("fmovrsgz", 0x6, 0),
 fmovrs("fmovrsnz", 0x5, F_ALIAS),
 fmovrs("fmovrsz", 0x1, F_ALIAS),

 fmovrd("fmovrdne", 0x5, 0),
 fmovrd("fmovrde", 0x1, 0),
 fmovrd("fmovrdgez", 0x7, 0),
 fmovrd("fmovrdlz", 0x3, 0),
 fmovrd("fmovrdlez", 0x2, 0),
 fmovrd("fmovrdgz", 0x6, 0),
 fmovrd("fmovrdnz", 0x5, F_ALIAS),
 fmovrd("fmovrdz", 0x1, F_ALIAS),

 fmovrq("fmovrqne", 0x5, 0),
 fmovrq("fmovrqe", 0x1, 0),
 fmovrq("fmovrqgez", 0x7, 0),
 fmovrq("fmovrqlz", 0x3, 0),
 fmovrq("fmovrqlez", 0x2, 0),
 fmovrq("fmovrqgz", 0x6, 0),
 fmovrq("fmovrqnz", 0x5, F_ALIAS),
 fmovrq("fmovrqz", 0x1, F_ALIAS),

#undef movr
#undef fmovr
#undef fmrr

#define movicc(opcode, cond, flags) \
  { opcode, F3(2, 0x2c, 0)|MCOND(cond,1)|ICC, F3(~2, ~0x2c, ~0)|MCOND(~cond,~1)|XCC|(1<<11), "z,2,d", flags, v9 }, \
  { opcode, F3(2, 0x2c, 1)|MCOND(cond,1)|ICC, F3(~2, ~0x2c, ~1)|MCOND(~cond,~1)|XCC|(1<<11), "z,I,d", flags, v9 }, \
  { opcode, F3(2, 0x2c, 0)|MCOND(cond,1)|XCC, F3(~2, ~0x2c, ~0)|MCOND(~cond,~1)|(1<<11),     "Z,2,d", flags, v9 }, \
  { opcode, F3(2, 0x2c, 1)|MCOND(cond,1)|XCC, F3(~2, ~0x2c, ~1)|MCOND(~cond,~1)|(1<<11),     "Z,I,d", flags, v9 }

#define movfcc(opcode, fcond, flags) \
  { opcode, F3(2, 0x2c, 0)|FCC(0)|MCOND(fcond,0), MCOND(~fcond,~0)|FCC(~0)|F3(~2, ~0x2c, ~0), "6,2,d", flags, v9 }, \
  { opcode, F3(2, 0x2c, 1)|FCC(0)|MCOND(fcond,0), MCOND(~fcond,~0)|FCC(~0)|F3(~2, ~0x2c, ~1), "6,I,d", flags, v9 }, \
  { opcode, F3(2, 0x2c, 0)|FCC(1)|MCOND(fcond,0), MCOND(~fcond,~0)|FCC(~1)|F3(~2, ~0x2c, ~0), "7,2,d", flags, v9 }, \
  { opcode, F3(2, 0x2c, 1)|FCC(1)|MCOND(fcond,0), MCOND(~fcond,~0)|FCC(~1)|F3(~2, ~0x2c, ~1), "7,I,d", flags, v9 }, \
  { opcode, F3(2, 0x2c, 0)|FCC(2)|MCOND(fcond,0), MCOND(~fcond,~0)|FCC(~2)|F3(~2, ~0x2c, ~0), "8,2,d", flags, v9 }, \
  { opcode, F3(2, 0x2c, 1)|FCC(2)|MCOND(fcond,0), MCOND(~fcond,~0)|FCC(~2)|F3(~2, ~0x2c, ~1), "8,I,d", flags, v9 }, \
  { opcode, F3(2, 0x2c, 0)|FCC(3)|MCOND(fcond,0), MCOND(~fcond,~0)|FCC(~3)|F3(~2, ~0x2c, ~0), "9,2,d", flags, v9 }, \
  { opcode, F3(2, 0x2c, 1)|FCC(3)|MCOND(fcond,0), MCOND(~fcond,~0)|FCC(~3)|F3(~2, ~0x2c, ~1), "9,I,d", flags, v9 }

#define movcc(opcode, cond, fcond, flags) \
  movfcc (opcode, fcond, flags), \
  movicc (opcode, cond, flags)

 movcc  ("mova",        CONDA, FCONDA, 0),
 movicc ("movcc",       CONDCC, 0),
 movicc ("movgeu",      CONDGEU, F_ALIAS),
 movicc ("movcs",       CONDCS, 0),
 movicc ("movlu",       CONDLU, F_ALIAS),
 movcc  ("move",        CONDE, FCONDE, 0),
 movcc  ("movg",        CONDG, FCONDG, 0),
 movcc  ("movge",       CONDGE, FCONDGE, 0),
 movicc ("movgu",       CONDGU, 0),
 movcc  ("movl",        CONDL, FCONDL, 0),
 movcc  ("movle",       CONDLE, FCONDLE, 0),
 movicc ("movleu",      CONDLEU, 0),
 movfcc ("movlg",       FCONDLG, 0),
 movcc  ("movn",        CONDN, FCONDN, 0),
 movcc  ("movne",       CONDNE, FCONDNE, 0),
 movicc ("movneg",      CONDNEG, 0),
 movcc  ("movnz",       CONDNZ, FCONDNZ, F_ALIAS),
 movfcc ("movo",        FCONDO, 0),
 movicc ("movpos",      CONDPOS, 0),
 movfcc ("movu",        FCONDU, 0),
 movfcc ("movue",       FCONDUE, 0),
 movfcc ("movug",       FCONDUG, 0),
 movfcc ("movuge",      FCONDUGE, 0),
 movfcc ("movul",       FCONDUL, 0),
 movfcc ("movule",      FCONDULE, 0),
 movicc ("movvc",       CONDVC, 0),
 movicc ("movvs",       CONDVS, 0),
 movcc  ("movz",        CONDZ, FCONDZ, F_ALIAS),

#undef movicc
#undef movfcc
#undef movcc

#define FM_SF 1
#define FM_DF 2
#define FM_QF 3

#define fmoviccx(opcode, fpsize, args, cond, flags) \
{ opcode, F3F(2, 0x35, 0x100+fpsize)|MCOND(cond,0),  F3F(~2, ~0x35, ~(0x100+fpsize))|MCOND(~cond,~0),  "z," args, flags, v9 }, \
{ opcode, F3F(2, 0x35, 0x180+fpsize)|MCOND(cond,0),  F3F(~2, ~0x35, ~(0x180+fpsize))|MCOND(~cond,~0),  "Z," args, flags, v9 }

#define fmovfccx(opcode, fpsize, args, fcond, flags) \
{ opcode, F3F(2, 0x35, 0x000+fpsize)|MCOND(fcond,0), F3F(~2, ~0x35, ~(0x000+fpsize))|MCOND(~fcond,~0), "6," args, flags, v9 }, \
{ opcode, F3F(2, 0x35, 0x040+fpsize)|MCOND(fcond,0), F3F(~2, ~0x35, ~(0x040+fpsize))|MCOND(~fcond,~0), "7," args, flags, v9 }, \
{ opcode, F3F(2, 0x35, 0x080+fpsize)|MCOND(fcond,0), F3F(~2, ~0x35, ~(0x080+fpsize))|MCOND(~fcond,~0), "8," args, flags, v9 }, \
{ opcode, F3F(2, 0x35, 0x0c0+fpsize)|MCOND(fcond,0), F3F(~2, ~0x35, ~(0x0c0+fpsize))|MCOND(~fcond,~0), "9," args, flags, v9 }

#define fmovccx(opcode, fpsize, args, cond, fcond, flags) \
{ opcode, F3F(2, 0x35, 0x100+fpsize)|MCOND(cond,0),  F3F(~2, ~0x35, ~(0x100+fpsize))|MCOND(~cond,~0),  "z," args, flags | F_FLOAT, v9 }, \
{ opcode, F3F(2, 0x35, 0x000+fpsize)|MCOND(fcond,0), F3F(~2, ~0x35, ~(0x000+fpsize))|MCOND(~fcond,~0), "6," args, flags | F_FLOAT, v9 }, \
{ opcode, F3F(2, 0x35, 0x180+fpsize)|MCOND(cond,0),  F3F(~2, ~0x35, ~(0x180+fpsize))|MCOND(~cond,~0),  "Z," args, flags | F_FLOAT, v9 }, \
{ opcode, F3F(2, 0x35, 0x040+fpsize)|MCOND(fcond,0), F3F(~2, ~0x35, ~(0x040+fpsize))|MCOND(~fcond,~0), "7," args, flags | F_FLOAT, v9 }, \
{ opcode, F3F(2, 0x35, 0x080+fpsize)|MCOND(fcond,0), F3F(~2, ~0x35, ~(0x080+fpsize))|MCOND(~fcond,~0), "8," args, flags | F_FLOAT, v9 }, \
{ opcode, F3F(2, 0x35, 0x0c0+fpsize)|MCOND(fcond,0), F3F(~2, ~0x35, ~(0x0c0+fpsize))|MCOND(~fcond,~0), "9," args, flags | F_FLOAT, v9 }

#define fmovicc(suffix, cond, flags) \
fmoviccx("fmovd" suffix, FM_DF, "B,H", cond, flags),            \
fmoviccx("fmovq" suffix, FM_QF, "R,J", cond, flags),            \
fmoviccx("fmovs" suffix, FM_SF, "f,g", cond, flags)

#define fmovfcc(suffix, fcond, flags) \
fmovfccx("fmovd" suffix, FM_DF, "B,H", fcond, flags),           \
fmovfccx("fmovq" suffix, FM_QF, "R,J", fcond, flags),           \
fmovfccx("fmovs" suffix, FM_SF, "f,g", fcond, flags)

#define fmovcc(suffix, cond, fcond, flags) \
fmovccx("fmovd" suffix, FM_DF, "B,H", cond, fcond, flags),      \
fmovccx("fmovq" suffix, FM_QF, "R,J", cond, fcond, flags),      \
fmovccx("fmovs" suffix, FM_SF, "f,g", cond, fcond, flags)

 fmovcc  ("a", CONDA, FCONDA, 0),
 fmovicc ("cc", CONDCC, 0),
 fmovicc ("cs", CONDCS, 0),
 fmovcc  ("e", CONDE, FCONDE, 0),
 fmovcc  ("g", CONDG, FCONDG, 0),
 fmovcc  ("ge", CONDGE, FCONDGE, 0),
 fmovicc ("geu", CONDGEU, F_ALIAS),
 fmovicc ("gu", CONDGU, 0),
 fmovcc  ("l", CONDL, FCONDL, 0),
 fmovcc  ("le", CONDLE, FCONDLE, 0),
 fmovicc ("leu", CONDLEU, 0),
 fmovfcc ("lg", FCONDLG, 0),
 fmovicc ("lu", CONDLU, F_ALIAS),
 fmovcc  ("n", CONDN, FCONDN, 0),
 fmovcc  ("ne", CONDNE, FCONDNE, 0),
 fmovicc ("neg", CONDNEG, 0),
 fmovcc  ("nz", CONDNZ, FCONDNZ, F_ALIAS),
 fmovfcc ("o", FCONDO, 0),
 fmovicc ("pos", CONDPOS, 0),
 fmovfcc ("u", FCONDU, 0),
 fmovfcc ("ue", FCONDUE, 0),
 fmovfcc ("ug", FCONDUG, 0),
 fmovfcc ("uge", FCONDUGE, 0),
 fmovfcc ("ul", FCONDUL, 0),
 fmovfcc ("ule", FCONDULE, 0),
 fmovicc ("vc", CONDVC, 0),
 fmovicc ("vs", CONDVS, 0),
 fmovcc  ("z", CONDZ, FCONDZ, F_ALIAS),

#undef fmoviccx
#undef fmovfccx
#undef fmovccx
#undef fmovicc
#undef fmovfcc
#undef fmovcc
#undef FM_DF
#undef FM_QF
#undef FM_SF

#define CBR(opcode, mask, lose, flags, arch) \
 { opcode, (mask), ANNUL | (lose), "l",    flags | F_DELAYED, arch }, \
 { opcode, (mask) | ANNUL, (lose), ",a l", flags | F_DELAYED, arch }

#define FBR(opcode, mask, lose, flags) \
 { opcode, (mask), ANNUL | (lose), "l",    flags | F_DELAYED | F_FBR, v6 }, \
 { opcode, (mask) | ANNUL, (lose), ",a l", flags | F_DELAYED | F_FBR, v6 }

#define FBRX(opcode, mask, lose, flags) \
 { opcode, FBFCC(0)|(mask)|BPRED, ANNUL|FBFCC(~0)|(lose), "6,G",      flags|F_DELAYED|F_FBR, v9 }, \
 { opcode, FBFCC(0)|(mask)|BPRED, ANNUL|FBFCC(~0)|(lose), ",T 6,G",   flags|F_DELAYED|F_FBR, v9 }, \
 { opcode, FBFCC(0)|(mask)|BPRED|ANNUL, FBFCC(~0)|(lose), ",a 6,G",   flags|F_DELAYED|F_FBR, v9 }, \
 { opcode, FBFCC(0)|(mask)|BPRED|ANNUL, FBFCC(~0)|(lose), ",a,T 6,G", flags|F_DELAYED|F_FBR, v9 }, \
 { opcode, FBFCC(0)|(mask), ANNUL|BPRED|FBFCC(~0)|(lose), ",N 6,G",   flags|F_DELAYED|F_FBR, v9 }, \
 { opcode, FBFCC(0)|(mask)|ANNUL, BPRED|FBFCC(~0)|(lose), ",a,N 6,G", flags|F_DELAYED|F_FBR, v9 }, \
 { opcode, FBFCC(1)|(mask)|BPRED, ANNUL|FBFCC(~1)|(lose), "7,G",      flags|F_DELAYED|F_FBR, v9 }, \
 { opcode, FBFCC(1)|(mask)|BPRED, ANNUL|FBFCC(~1)|(lose), ",T 7,G",   flags|F_DELAYED|F_FBR, v9 }, \
 { opcode, FBFCC(1)|(mask)|BPRED|ANNUL, FBFCC(~1)|(lose), ",a 7,G",   flags|F_DELAYED|F_FBR, v9 }, \
 { opcode, FBFCC(1)|(mask)|BPRED|ANNUL, FBFCC(~1)|(lose), ",a,T 7,G", flags|F_DELAYED|F_FBR, v9 }, \
 { opcode, FBFCC(1)|(mask), ANNUL|BPRED|FBFCC(~1)|(lose), ",N 7,G",   flags|F_DELAYED|F_FBR, v9 }, \
 { opcode, FBFCC(1)|(mask)|ANNUL, BPRED|FBFCC(~1)|(lose), ",a,N 7,G", flags|F_DELAYED|F_FBR, v9 }, \
 { opcode, FBFCC(2)|(mask)|BPRED, ANNUL|FBFCC(~2)|(lose), "8,G",      flags|F_DELAYED|F_FBR, v9 }, \
 { opcode, FBFCC(2)|(mask)|BPRED, ANNUL|FBFCC(~2)|(lose), ",T 8,G",   flags|F_DELAYED|F_FBR, v9 }, \
 { opcode, FBFCC(2)|(mask)|BPRED|ANNUL, FBFCC(~2)|(lose), ",a 8,G",   flags|F_DELAYED|F_FBR, v9 }, \
 { opcode, FBFCC(2)|(mask)|BPRED|ANNUL, FBFCC(~2)|(lose), ",a,T 8,G", flags|F_DELAYED|F_FBR, v9 }, \
 { opcode, FBFCC(2)|(mask), ANNUL|BPRED|FBFCC(~2)|(lose), ",N 8,G",   flags|F_DELAYED|F_FBR, v9 }, \
 { opcode, FBFCC(2)|(mask)|ANNUL, BPRED|FBFCC(~2)|(lose), ",a,N 8,G", flags|F_DELAYED|F_FBR, v9 }, \
 { opcode, FBFCC(3)|(mask)|BPRED, ANNUL|FBFCC(~3)|(lose), "9,G",      flags|F_DELAYED|F_FBR, v9 }, \
 { opcode, FBFCC(3)|(mask)|BPRED, ANNUL|FBFCC(~3)|(lose), ",T 9,G",   flags|F_DELAYED|F_FBR, v9 }, \
 { opcode, FBFCC(3)|(mask)|BPRED|ANNUL, FBFCC(~3)|(lose), ",a 9,G",   flags|F_DELAYED|F_FBR, v9 }, \
 { opcode, FBFCC(3)|(mask)|BPRED|ANNUL, FBFCC(~3)|(lose), ",a,T 9,G", flags|F_DELAYED|F_FBR, v9 }, \
 { opcode, FBFCC(3)|(mask), ANNUL|BPRED|FBFCC(~3)|(lose), ",N 9,G",   flags|F_DELAYED|F_FBR, v9 }, \
 { opcode, FBFCC(3)|(mask)|ANNUL, BPRED|FBFCC(~3)|(lose), ",a,N 9,G", flags|F_DELAYED|F_FBR, v9 }

#define CONDFC(fop, cop, mask, flags) \
  FBRX(fop, F2(0, 5)|COND(mask), F2(~0, ~5)|COND(~(mask)), flags), \
  FBR(fop, F2(0, 6)|COND(mask), F2(~0, ~6)|COND(~(mask)), flags), \
  CBR(cop, F2(0, 7)|COND(mask), F2(~0, ~7)|COND(~(mask)), flags, v6notlet)

#define CONDFCL(fop, cop, mask, flags) \
  FBRX(fop, F2(0, 5)|COND(mask), F2(~0, ~5)|COND(~(mask)), flags), \
  FBR(fop, F2(0, 6)|COND(mask), F2(~0, ~6)|COND(~(mask)), flags), \
  CBR(cop, F2(0, 7)|COND(mask), F2(~0, ~7)|COND(~(mask)), flags, v6)

#define CONDF(fop, mask, flags) \
  FBRX(fop, F2(0, 5)|COND(mask), F2(~0, ~5)|COND(~(mask)), flags), \
  FBR(fop, F2(0, 6)|COND(mask), F2(~0, ~6)|COND(~(mask)), flags)

CONDFC  ("fb",    "cb",    0x8, F_UNBR),
CONDFCL ("fba",   "cba",   0x8, F_UNBR|F_ALIAS),
CONDFC  ("fbe",   "cb0",   0x9, F_CONDBR),
CONDF   ("fbz",            0x9, F_CONDBR|F_ALIAS),
CONDFC  ("fbg",   "cb2",   0x6, F_CONDBR),
CONDFC  ("fbge",  "cb02",  0xb, F_CONDBR),
CONDFC  ("fbl",   "cb1",   0x4, F_CONDBR),
CONDFC  ("fble",  "cb01",  0xd, F_CONDBR),
CONDFC  ("fblg",  "cb12",  0x2, F_CONDBR),
CONDFCL ("fbn",   "cbn",   0x0, F_UNBR),
CONDFC  ("fbne",  "cb123", 0x1, F_CONDBR),
CONDF   ("fbnz",           0x1, F_CONDBR|F_ALIAS),
CONDFC  ("fbo",   "cb012", 0xf, F_CONDBR),
CONDFC  ("fbu",   "cb3",   0x7, F_CONDBR),
CONDFC  ("fbue",  "cb03",  0xa, F_CONDBR),
CONDFC  ("fbug",  "cb23",  0x5, F_CONDBR),
CONDFC  ("fbuge", "cb023", 0xc, F_CONDBR),
CONDFC  ("fbul",  "cb13",  0x3, F_CONDBR),
CONDFC  ("fbule", "cb013", 0xe, F_CONDBR),

#undef CONDFC
#undef CONDFCL
#undef CONDF
#undef CBR
#undef FBR
#undef FBRX

{ "jmp",        F3(2, 0x38, 0), F3(~2, ~0x38, ~0)|RD_G0|ASI(~0),        "1+2", F_UNBR|F_DELAYED, v6 },
{ "jmp",        F3(2, 0x38, 0), F3(~2, ~0x38, ~0)|RD_G0|ASI_RS2(~0),    "1", F_UNBR|F_DELAYED, v6 },
{ "jmp",        F3(2, 0x38, 1), F3(~2, ~0x38, ~1)|RD_G0,                "1+i", F_UNBR|F_DELAYED, v6 },
{ "jmp",        F3(2, 0x38, 1), F3(~2, ~0x38, ~1)|RD_G0,                "i+1", F_UNBR|F_DELAYED, v6 },
{ "jmp",        F3(2, 0x38, 1), F3(~2, ~0x38, ~1)|RD_G0|RS1_G0,         "i", F_UNBR|F_DELAYED, v6 },
{ "jmp",        F3(2, 0x38, 1), F3(~2, ~0x38, ~1)|RD_G0|SIMM13(~0),     "1", F_UNBR|F_DELAYED, v6 },

{ "nop",        F2(0, 4), 0xfeffffff, "", 0, v6 },

{ "set",        F2(0x0, 0x4), F2(~0x0, ~0x4), "S0,d", F_ALIAS, v6 },
{ "setuw",      F2(0x0, 0x4), F2(~0x0, ~0x4), "S0,d", F_ALIAS, v9 },
{ "setsw",      F2(0x0, 0x4), F2(~0x0, ~0x4), "S0,d", F_ALIAS, v9 },
{ "setx",       F2(0x0, 0x4), F2(~0x0, ~0x4), "S0,1,d", F_ALIAS, v9 },

{ "sethi",      F2(0x0, 0x4), F2(~0x0, ~0x4), "h,d", 0, v6 },

{ "taddcc",     F3(2, 0x20, 0), F3(~2, ~0x20, ~0)|ASI(~0),      "1,2,d", 0, v6 },
{ "taddcc",     F3(2, 0x20, 1), F3(~2, ~0x20, ~1),              "1,i,d", 0, v6 },
{ "taddcc",     F3(2, 0x20, 1), F3(~2, ~0x20, ~1),              "i,1,d", 0, v6 },
{ "taddcctv",   F3(2, 0x22, 0), F3(~2, ~0x22, ~0)|ASI(~0),      "1,2,d", 0, v6 },
{ "taddcctv",   F3(2, 0x22, 1), F3(~2, ~0x22, ~1),              "1,i,d", 0, v6 },
{ "taddcctv",   F3(2, 0x22, 1), F3(~2, ~0x22, ~1),              "i,1,d", 0, v6 },

{ "tsubcc",     F3(2, 0x21, 0), F3(~2, ~0x21, ~0)|ASI(~0),      "1,2,d", 0, v6 },
{ "tsubcc",     F3(2, 0x21, 1), F3(~2, ~0x21, ~1),              "1,i,d", 0, v6 },
{ "tsubcctv",   F3(2, 0x23, 0), F3(~2, ~0x23, ~0)|ASI(~0),      "1,2,d", 0, v6 },
{ "tsubcctv",   F3(2, 0x23, 1), F3(~2, ~0x23, ~1),              "1,i,d", 0, v6 },

{ "unimp",      F2(0x0, 0x0), 0xffc00000, "n", 0, v6notv9 },
{ "illtrap",    F2(0, 0), F2(~0, ~0)|RD_G0, "n", 0, v9 },

{ "xnor",       F3(2, 0x07, 0), F3(~2, ~0x07, ~0)|ASI(~0),      "1,2,d", 0, v6 },
{ "xnor",       F3(2, 0x07, 1), F3(~2, ~0x07, ~1),              "1,i,d", 0, v6 },
{ "xnor",       F3(2, 0x07, 1), F3(~2, ~0x07, ~1),              "i,1,d", 0, v6 },
{ "xnorcc",     F3(2, 0x17, 0), F3(~2, ~0x17, ~0)|ASI(~0),      "1,2,d", 0, v6 },
{ "xnorcc",     F3(2, 0x17, 1), F3(~2, ~0x17, ~1),              "1,i,d", 0, v6 },
{ "xnorcc",     F3(2, 0x17, 1), F3(~2, ~0x17, ~1),              "i,1,d", 0, v6 },
{ "xor",        F3(2, 0x03, 0), F3(~2, ~0x03, ~0)|ASI(~0),      "1,2,d", 0, v6 },
{ "xor",        F3(2, 0x03, 1), F3(~2, ~0x03, ~1),              "1,i,d", 0, v6 },
{ "xor",        F3(2, 0x03, 1), F3(~2, ~0x03, ~1),              "i,1,d", 0, v6 },
{ "xorcc",      F3(2, 0x13, 0), F3(~2, ~0x13, ~0)|ASI(~0),      "1,2,d", 0, v6 },
{ "xorcc",      F3(2, 0x13, 1), F3(~2, ~0x13, ~1),              "1,i,d", 0, v6 },
{ "xorcc",      F3(2, 0x13, 1), F3(~2, ~0x13, ~1),              "i,1,d", 0, v6 },

{ "not",        F3(2, 0x07, 0), F3(~2, ~0x07, ~0)|ASI(~0), "1,d", F_ALIAS, v6 },
{ "not",        F3(2, 0x07, 0), F3(~2, ~0x07, ~0)|ASI(~0), "r", F_ALIAS, v6 },

{ "btog",       F3(2, 0x03, 0), F3(~2, ~0x03, ~0)|ASI(~0),      "2,r", F_ALIAS, v6 },
{ "btog",       F3(2, 0x03, 1), F3(~2, ~0x03, ~1),              "i,r", F_ALIAS, v6 },

{ "fdtoi",      F3F(2, 0x34, 0x0d2), F3F(~2, ~0x34, ~0x0d2)|RS1_G0, "B,g", F_FLOAT, v6 },
{ "fstoi",      F3F(2, 0x34, 0x0d1), F3F(~2, ~0x34, ~0x0d1)|RS1_G0, "f,g", F_FLOAT, v6 },
{ "fqtoi",      F3F(2, 0x34, 0x0d3), F3F(~2, ~0x34, ~0x0d3)|RS1_G0, "R,g", F_FLOAT, v8 },

{ "fdtox",      F3F(2, 0x34, 0x082), F3F(~2, ~0x34, ~0x082)|RS1_G0, "B,H", F_FLOAT, v9 },
{ "fstox",      F3F(2, 0x34, 0x081), F3F(~2, ~0x34, ~0x081)|RS1_G0, "f,H", F_FLOAT, v9 },
{ "fqtox",      F3F(2, 0x34, 0x083), F3F(~2, ~0x34, ~0x083)|RS1_G0, "R,H", F_FLOAT, v9 },

{ "fitod",      F3F(2, 0x34, 0x0c8), F3F(~2, ~0x34, ~0x0c8)|RS1_G0, "f,H", F_FLOAT, v6 },
{ "fitos",      F3F(2, 0x34, 0x0c4), F3F(~2, ~0x34, ~0x0c4)|RS1_G0, "f,g", F_FLOAT, v6 },
{ "fitoq",      F3F(2, 0x34, 0x0cc), F3F(~2, ~0x34, ~0x0cc)|RS1_G0, "f,J", F_FLOAT, v8 },

{ "fxtod",      F3F(2, 0x34, 0x088), F3F(~2, ~0x34, ~0x088)|RS1_G0, "B,H", F_FLOAT, v9 },
{ "fxtos",      F3F(2, 0x34, 0x084), F3F(~2, ~0x34, ~0x084)|RS1_G0, "B,g", F_FLOAT, v9 },
{ "fxtoq",      F3F(2, 0x34, 0x08c), F3F(~2, ~0x34, ~0x08c)|RS1_G0, "B,J", F_FLOAT, v9 },

{ "fdtoq",      F3F(2, 0x34, 0x0ce), F3F(~2, ~0x34, ~0x0ce)|RS1_G0, "B,J", F_FLOAT, v8 },
{ "fdtos",      F3F(2, 0x34, 0x0c6), F3F(~2, ~0x34, ~0x0c6)|RS1_G0, "B,g", F_FLOAT, v6 },
{ "fqtod",      F3F(2, 0x34, 0x0cb), F3F(~2, ~0x34, ~0x0cb)|RS1_G0, "R,H", F_FLOAT, v8 },
{ "fqtos",      F3F(2, 0x34, 0x0c7), F3F(~2, ~0x34, ~0x0c7)|RS1_G0, "R,g", F_FLOAT, v8 },
{ "fstod",      F3F(2, 0x34, 0x0c9), F3F(~2, ~0x34, ~0x0c9)|RS1_G0, "f,H", F_FLOAT, v6 },
{ "fstoq",      F3F(2, 0x34, 0x0cd), F3F(~2, ~0x34, ~0x0cd)|RS1_G0, "f,J", F_FLOAT, v8 },

{ "fdivd",      F3F(2, 0x34, 0x04e), F3F(~2, ~0x34, ~0x04e), "v,B,H", F_FLOAT, v6 },
{ "fdivq",      F3F(2, 0x34, 0x04f), F3F(~2, ~0x34, ~0x04f), "V,R,J", F_FLOAT, v8 },
{ "fdivx",      F3F(2, 0x34, 0x04f), F3F(~2, ~0x34, ~0x04f), "V,R,J", F_FLOAT|F_ALIAS, v8 },
{ "fdivs",      F3F(2, 0x34, 0x04d), F3F(~2, ~0x34, ~0x04d), "e,f,g", F_FLOAT, v6 },
{ "fmuld",      F3F(2, 0x34, 0x04a), F3F(~2, ~0x34, ~0x04a), "v,B,H", F_FLOAT, v6 },
{ "fmulq",      F3F(2, 0x34, 0x04b), F3F(~2, ~0x34, ~0x04b), "V,R,J", F_FLOAT, v8 },
{ "fmulx",      F3F(2, 0x34, 0x04b), F3F(~2, ~0x34, ~0x04b), "V,R,J", F_FLOAT|F_ALIAS, v8 },
{ "fmuls",      F3F(2, 0x34, 0x049), F3F(~2, ~0x34, ~0x049), "e,f,g", F_FLOAT, v6 },

{ "fdmulq",     F3F(2, 0x34, 0x06e), F3F(~2, ~0x34, ~0x06e), "v,B,J", F_FLOAT, v8 },
{ "fdmulx",     F3F(2, 0x34, 0x06e), F3F(~2, ~0x34, ~0x06e), "v,B,J", F_FLOAT|F_ALIAS, v8 },
{ "fsmuld",     F3F(2, 0x34, 0x069), F3F(~2, ~0x34, ~0x069), "e,f,H", F_FLOAT, v8 },

{ "fsqrtd",     F3F(2, 0x34, 0x02a), F3F(~2, ~0x34, ~0x02a)|RS1_G0, "B,H", F_FLOAT, v7 },
{ "fsqrtq",     F3F(2, 0x34, 0x02b), F3F(~2, ~0x34, ~0x02b)|RS1_G0, "R,J", F_FLOAT, v8 },
{ "fsqrtx",     F3F(2, 0x34, 0x02b), F3F(~2, ~0x34, ~0x02b)|RS1_G0, "R,J", F_FLOAT|F_ALIAS, v8 },
{ "fsqrts",     F3F(2, 0x34, 0x029), F3F(~2, ~0x34, ~0x029)|RS1_G0, "f,g", F_FLOAT, v7 },

{ "fabsd",      F3F(2, 0x34, 0x00a), F3F(~2, ~0x34, ~0x00a)|RS1_G0, "B,H", F_FLOAT, v9 },
{ "fabsq",      F3F(2, 0x34, 0x00b), F3F(~2, ~0x34, ~0x00b)|RS1_G0, "R,J", F_FLOAT, v9 },
{ "fabsx",      F3F(2, 0x34, 0x00b), F3F(~2, ~0x34, ~0x00b)|RS1_G0, "R,J", F_FLOAT|F_ALIAS, v9 },
{ "fabss",      F3F(2, 0x34, 0x009), F3F(~2, ~0x34, ~0x009)|RS1_G0, "f,g", F_FLOAT, v6 },
{ "fmovd",      F3F(2, 0x34, 0x002), F3F(~2, ~0x34, ~0x002)|RS1_G0, "B,H", F_FLOAT, v9 },
{ "fmovq",      F3F(2, 0x34, 0x003), F3F(~2, ~0x34, ~0x003)|RS1_G0, "R,J", F_FLOAT, v9 },
{ "fmovx",      F3F(2, 0x34, 0x003), F3F(~2, ~0x34, ~0x003)|RS1_G0, "R,J", F_FLOAT|F_ALIAS, v9 },
{ "fmovs",      F3F(2, 0x34, 0x001), F3F(~2, ~0x34, ~0x001)|RS1_G0, "f,g", F_FLOAT, v6 },
{ "fnegd",      F3F(2, 0x34, 0x006), F3F(~2, ~0x34, ~0x006)|RS1_G0, "B,H", F_FLOAT, v9 },
{ "fnegq",      F3F(2, 0x34, 0x007), F3F(~2, ~0x34, ~0x007)|RS1_G0, "R,J", F_FLOAT, v9 },
{ "fnegx",      F3F(2, 0x34, 0x007), F3F(~2, ~0x34, ~0x007)|RS1_G0, "R,J", F_FLOAT|F_ALIAS, v9 },
{ "fnegs",      F3F(2, 0x34, 0x005), F3F(~2, ~0x34, ~0x005)|RS1_G0, "f,g", F_FLOAT, v6 },

{ "faddd",      F3F(2, 0x34, 0x042), F3F(~2, ~0x34, ~0x042), "v,B,H", F_FLOAT, v6 },
{ "faddq",      F3F(2, 0x34, 0x043), F3F(~2, ~0x34, ~0x043), "V,R,J", F_FLOAT, v8 },
{ "faddx",      F3F(2, 0x34, 0x043), F3F(~2, ~0x34, ~0x043), "V,R,J", F_FLOAT|F_ALIAS, v8 },
{ "fadds",      F3F(2, 0x34, 0x041), F3F(~2, ~0x34, ~0x041), "e,f,g", F_FLOAT, v6 },
{ "fsubd",      F3F(2, 0x34, 0x046), F3F(~2, ~0x34, ~0x046), "v,B,H", F_FLOAT, v6 },
{ "fsubq",      F3F(2, 0x34, 0x047), F3F(~2, ~0x34, ~0x047), "V,R,J", F_FLOAT, v8 },
{ "fsubx",      F3F(2, 0x34, 0x047), F3F(~2, ~0x34, ~0x047), "V,R,J", F_FLOAT|F_ALIAS, v8 },
{ "fsubs",      F3F(2, 0x34, 0x045), F3F(~2, ~0x34, ~0x045), "e,f,g", F_FLOAT, v6 },

#define CMPFCC(x)       (((x)&0x3)<<25)

{ "fcmpd",                F3F(2, 0x35, 0x052),            F3F(~2, ~0x35, ~0x052)|RD_G0,  "v,B",   F_FLOAT, v6 },
{ "fcmpd",      CMPFCC(0)|F3F(2, 0x35, 0x052), CMPFCC(~0)|F3F(~2, ~0x35, ~0x052),        "6,v,B", F_FLOAT, v9 },
{ "fcmpd",      CMPFCC(1)|F3F(2, 0x35, 0x052), CMPFCC(~1)|F3F(~2, ~0x35, ~0x052),        "7,v,B", F_FLOAT, v9 },
{ "fcmpd",      CMPFCC(2)|F3F(2, 0x35, 0x052), CMPFCC(~2)|F3F(~2, ~0x35, ~0x052),        "8,v,B", F_FLOAT, v9 },
{ "fcmpd",      CMPFCC(3)|F3F(2, 0x35, 0x052), CMPFCC(~3)|F3F(~2, ~0x35, ~0x052),        "9,v,B", F_FLOAT, v9 },
{ "fcmped",               F3F(2, 0x35, 0x056),            F3F(~2, ~0x35, ~0x056)|RD_G0,  "v,B",   F_FLOAT, v6 },
{ "fcmped",     CMPFCC(0)|F3F(2, 0x35, 0x056), CMPFCC(~0)|F3F(~2, ~0x35, ~0x056),        "6,v,B", F_FLOAT, v9 },
{ "fcmped",     CMPFCC(1)|F3F(2, 0x35, 0x056), CMPFCC(~1)|F3F(~2, ~0x35, ~0x056),        "7,v,B", F_FLOAT, v9 },
{ "fcmped",     CMPFCC(2)|F3F(2, 0x35, 0x056), CMPFCC(~2)|F3F(~2, ~0x35, ~0x056),        "8,v,B", F_FLOAT, v9 },
{ "fcmped",     CMPFCC(3)|F3F(2, 0x35, 0x056), CMPFCC(~3)|F3F(~2, ~0x35, ~0x056),        "9,v,B", F_FLOAT, v9 },
{ "fcmpq",                F3F(2, 0x35, 0x053),            F3F(~2, ~0x35, ~0x053)|RD_G0,  "V,R", F_FLOAT, v8 },
{ "fcmpq",      CMPFCC(0)|F3F(2, 0x35, 0x053), CMPFCC(~0)|F3F(~2, ~0x35, ~0x053),        "6,V,R", F_FLOAT, v9 },
{ "fcmpq",      CMPFCC(1)|F3F(2, 0x35, 0x053), CMPFCC(~1)|F3F(~2, ~0x35, ~0x053),        "7,V,R", F_FLOAT, v9 },
{ "fcmpq",      CMPFCC(2)|F3F(2, 0x35, 0x053), CMPFCC(~2)|F3F(~2, ~0x35, ~0x053),        "8,V,R", F_FLOAT, v9 },
{ "fcmpq",      CMPFCC(3)|F3F(2, 0x35, 0x053), CMPFCC(~3)|F3F(~2, ~0x35, ~0x053),        "9,V,R", F_FLOAT, v9 },
{ "fcmpeq",               F3F(2, 0x35, 0x057),            F3F(~2, ~0x35, ~0x057)|RD_G0,  "V,R", F_FLOAT, v8 },
{ "fcmpeq",     CMPFCC(0)|F3F(2, 0x35, 0x057), CMPFCC(~0)|F3F(~2, ~0x35, ~0x057),        "6,V,R", F_FLOAT, v9 },
{ "fcmpeq",     CMPFCC(1)|F3F(2, 0x35, 0x057), CMPFCC(~1)|F3F(~2, ~0x35, ~0x057),        "7,V,R", F_FLOAT, v9 },
{ "fcmpeq",     CMPFCC(2)|F3F(2, 0x35, 0x057), CMPFCC(~2)|F3F(~2, ~0x35, ~0x057),        "8,V,R", F_FLOAT, v9 },
{ "fcmpeq",     CMPFCC(3)|F3F(2, 0x35, 0x057), CMPFCC(~3)|F3F(~2, ~0x35, ~0x057),        "9,V,R", F_FLOAT, v9 },
{ "fcmpx",                F3F(2, 0x35, 0x053),            F3F(~2, ~0x35, ~0x053)|RD_G0,  "V,R", F_FLOAT|F_ALIAS, v8 },
{ "fcmpx",      CMPFCC(0)|F3F(2, 0x35, 0x053), CMPFCC(~0)|F3F(~2, ~0x35, ~0x053),        "6,V,R", F_FLOAT|F_ALIAS, v9 },
{ "fcmpx",      CMPFCC(1)|F3F(2, 0x35, 0x053), CMPFCC(~1)|F3F(~2, ~0x35, ~0x053),        "7,V,R", F_FLOAT|F_ALIAS, v9 },
{ "fcmpx",      CMPFCC(2)|F3F(2, 0x35, 0x053), CMPFCC(~2)|F3F(~2, ~0x35, ~0x053),        "8,V,R", F_FLOAT|F_ALIAS, v9 },
{ "fcmpx",      CMPFCC(3)|F3F(2, 0x35, 0x053), CMPFCC(~3)|F3F(~2, ~0x35, ~0x053),        "9,V,R", F_FLOAT|F_ALIAS, v9 },
{ "fcmpex",               F3F(2, 0x35, 0x057),            F3F(~2, ~0x35, ~0x057)|RD_G0,  "V,R", F_FLOAT|F_ALIAS, v8 },
{ "fcmpex",     CMPFCC(0)|F3F(2, 0x35, 0x057), CMPFCC(~0)|F3F(~2, ~0x35, ~0x057),        "6,V,R", F_FLOAT|F_ALIAS, v9 },
{ "fcmpex",     CMPFCC(1)|F3F(2, 0x35, 0x057), CMPFCC(~1)|F3F(~2, ~0x35, ~0x057),        "7,V,R", F_FLOAT|F_ALIAS, v9 },
{ "fcmpex",     CMPFCC(2)|F3F(2, 0x35, 0x057), CMPFCC(~2)|F3F(~2, ~0x35, ~0x057),        "8,V,R", F_FLOAT|F_ALIAS, v9 },
{ "fcmpex",     CMPFCC(3)|F3F(2, 0x35, 0x057), CMPFCC(~3)|F3F(~2, ~0x35, ~0x057),        "9,V,R", F_FLOAT|F_ALIAS, v9 },
{ "fcmps",                F3F(2, 0x35, 0x051),            F3F(~2, ~0x35, ~0x051)|RD_G0, "e,f",   F_FLOAT, v6 },
{ "fcmps",      CMPFCC(0)|F3F(2, 0x35, 0x051), CMPFCC(~0)|F3F(~2, ~0x35, ~0x051),        "6,e,f", F_FLOAT, v9 },
{ "fcmps",      CMPFCC(1)|F3F(2, 0x35, 0x051), CMPFCC(~1)|F3F(~2, ~0x35, ~0x051),        "7,e,f", F_FLOAT, v9 },
{ "fcmps",      CMPFCC(2)|F3F(2, 0x35, 0x051), CMPFCC(~2)|F3F(~2, ~0x35, ~0x051),        "8,e,f", F_FLOAT, v9 },
{ "fcmps",      CMPFCC(3)|F3F(2, 0x35, 0x051), CMPFCC(~3)|F3F(~2, ~0x35, ~0x051),        "9,e,f", F_FLOAT, v9 },
{ "fcmpes",               F3F(2, 0x35, 0x055),            F3F(~2, ~0x35, ~0x055)|RD_G0, "e,f",   F_FLOAT, v6 },
{ "fcmpes",     CMPFCC(0)|F3F(2, 0x35, 0x055), CMPFCC(~0)|F3F(~2, ~0x35, ~0x055),        "6,e,f", F_FLOAT, v9 },
{ "fcmpes",     CMPFCC(1)|F3F(2, 0x35, 0x055), CMPFCC(~1)|F3F(~2, ~0x35, ~0x055),        "7,e,f", F_FLOAT, v9 },
{ "fcmpes",     CMPFCC(2)|F3F(2, 0x35, 0x055), CMPFCC(~2)|F3F(~2, ~0x35, ~0x055),        "8,e,f", F_FLOAT, v9 },
{ "fcmpes",     CMPFCC(3)|F3F(2, 0x35, 0x055), CMPFCC(~3)|F3F(~2, ~0x35, ~0x055),        "9,e,f", F_FLOAT, v9 },

#define EFPOP1_2(name, op, args) { name, F3F(2, 0x36, op), F3F(~2, ~0x36, ~op)|RS1_G0, args, 0, sparclite }
#define EFPOP1_3(name, op, args) { name, F3F(2, 0x36, op), F3F(~2, ~0x36, ~op),        args, 0, sparclite }
#define EFPOP2_2(name, op, args) { name, F3F(2, 0x37, op), F3F(~2, ~0x37, ~op)|RD_G0,  args, 0, sparclite }

EFPOP1_2 ("efitod",     0x0c8, "f,H"),
EFPOP1_2 ("efitos",     0x0c4, "f,g"),
EFPOP1_2 ("efdtoi",     0x0d2, "B,g"),
EFPOP1_2 ("efstoi",     0x0d1, "f,g"),
EFPOP1_2 ("efstod",     0x0c9, "f,H"),
EFPOP1_2 ("efdtos",     0x0c6, "B,g"),
EFPOP1_2 ("efmovs",     0x001, "f,g"),
EFPOP1_2 ("efnegs",     0x005, "f,g"),
EFPOP1_2 ("efabss",     0x009, "f,g"),
EFPOP1_2 ("efsqrtd",    0x02a, "B,H"),
EFPOP1_2 ("efsqrts",    0x029, "f,g"),
EFPOP1_3 ("efaddd",     0x042, "v,B,H"),
EFPOP1_3 ("efadds",     0x041, "e,f,g"),
EFPOP1_3 ("efsubd",     0x046, "v,B,H"),
EFPOP1_3 ("efsubs",     0x045, "e,f,g"),
EFPOP1_3 ("efdivd",     0x04e, "v,B,H"),
EFPOP1_3 ("efdivs",     0x04d, "e,f,g"),
EFPOP1_3 ("efmuld",     0x04a, "v,B,H"),
EFPOP1_3 ("efmuls",     0x049, "e,f,g"),
EFPOP1_3 ("efsmuld",    0x069, "e,f,H"),
EFPOP2_2 ("efcmpd",     0x052, "v,B"),
EFPOP2_2 ("efcmped",    0x056, "v,B"),
EFPOP2_2 ("efcmps",     0x051, "e,f"),
EFPOP2_2 ("efcmpes",    0x055, "e,f"),

#undef EFPOP1_2
#undef EFPOP1_3
#undef EFPOP2_2

{ "cpop1",      F3(2, 0x36, 0), F3(~2, ~0x36, ~1), "[1+2],d", F_ALIAS, v6notv9 },
{ "cpop2",      F3(2, 0x37, 0), F3(~2, ~0x37, ~1), "[1+2],d", F_ALIAS, v6notv9 },

COMMUTEOP ("umac", 0x3e, sparclet),
COMMUTEOP ("smac", 0x3f, sparclet),
COMMUTEOP ("umacd", 0x2e, sparclet),
COMMUTEOP ("smacd", 0x2f, sparclet),
COMMUTEOP ("umuld", 0x09, sparclet),
COMMUTEOP ("smuld", 0x0d, sparclet),

{ "shuffle",    F3(2, 0x2d, 0), F3(~2, ~0x2d, ~0)|ASI(~0),      "1,2,d", 0, sparclet },
{ "shuffle",    F3(2, 0x2d, 1), F3(~2, ~0x2d, ~1),              "1,i,d", 0, sparclet },

#define BIT5 (1<<5)
{ "crdcxt",     F3(2, 0x36, 0)|SLCPOP(4), F3(~2, ~0x36, ~0)|SLCPOP(~4)|BIT5|RS2(~0),    "U,d", 0, sparclet },
{ "cwrcxt",     F3(2, 0x36, 0)|SLCPOP(3), F3(~2, ~0x36, ~0)|SLCPOP(~3)|BIT5|RS2(~0),    "1,u", 0, sparclet },
{ "cpush",      F3(2, 0x36, 0)|SLCPOP(0), F3(~2, ~0x36, ~0)|SLCPOP(~0)|BIT5|RD(~0),     "1,2", 0, sparclet },
{ "cpush",      F3(2, 0x36, 1)|SLCPOP(0), F3(~2, ~0x36, ~1)|SLCPOP(~0)|RD(~0),          "1,Y", 0, sparclet },
{ "cpusha",     F3(2, 0x36, 0)|SLCPOP(1), F3(~2, ~0x36, ~0)|SLCPOP(~1)|BIT5|RD(~0),     "1,2", 0, sparclet },
{ "cpusha",     F3(2, 0x36, 1)|SLCPOP(1), F3(~2, ~0x36, ~1)|SLCPOP(~1)|RD(~0),          "1,Y", 0, sparclet },
{ "cpull",      F3(2, 0x36, 0)|SLCPOP(2), F3(~2, ~0x36, ~0)|SLCPOP(~2)|BIT5|RS1(~0)|RS2(~0), "d", 0, sparclet },
#undef BIT5

#define SLCBCC2(opcode, mask, lose) \
 { opcode, (mask), ANNUL|(lose), "l",    F_DELAYED|F_CONDBR, sparclet }, \
 { opcode, (mask)|ANNUL, (lose), ",a l", F_DELAYED|F_CONDBR, sparclet }
#define SLCBCC(opcode, mask) \
  SLCBCC2(opcode, F2(0, 7)|COND(mask), F2(~0, ~7)|COND(~(mask)))

SLCBCC("cbe", 1),
SLCBCC("cbf", 2),
SLCBCC("cbef", 3),
SLCBCC("cbr", 4),
SLCBCC("cber", 5),
SLCBCC("cbfr", 6),
SLCBCC("cbefr", 7),
SLCBCC("cbne", 9),
SLCBCC("cbnf", 10),
SLCBCC("cbnef", 11),
SLCBCC("cbnr", 12),
SLCBCC("cbner", 13),
SLCBCC("cbnfr", 14),
SLCBCC("cbnefr", 15),

#undef SLCBCC2
#undef SLCBCC

{ "casa",       F3(3, 0x3c, 0), F3(~3, ~0x3c, ~0), "[1]A,2,d", 0, v9 },
{ "casa",       F3(3, 0x3c, 1), F3(~3, ~0x3c, ~1), "[1]o,2,d", 0, v9 },
{ "casxa",      F3(3, 0x3e, 0), F3(~3, ~0x3e, ~0), "[1]A,2,d", 0, v9 },
{ "casxa",      F3(3, 0x3e, 1), F3(~3, ~0x3e, ~1), "[1]o,2,d", 0, v9 },

{ "iprefetch",  F2(0, 1)|(2<<20)|BPRED, F2(~0, ~1)|(1<<20)|ANNUL|COND(~0), "G", 0, v9 },
{ "signx",      F3(2, 0x27, 0), F3(~2, ~0x27, ~0)|(1<<12)|ASI(~0)|RS2_G0, "1,d", F_ALIAS, v9 },
{ "signx",      F3(2, 0x27, 0), F3(~2, ~0x27, ~0)|(1<<12)|ASI(~0)|RS2_G0, "r", F_ALIAS, v9 },
{ "clruw",      F3(2, 0x26, 0), F3(~2, ~0x26, ~0)|(1<<12)|ASI(~0)|RS2_G0, "1,d", F_ALIAS, v9 },
{ "clruw",      F3(2, 0x26, 0), F3(~2, ~0x26, ~0)|(1<<12)|ASI(~0)|RS2_G0, "r", F_ALIAS, v9 },
{ "cas",        F3(3, 0x3c, 0)|ASI(0x80), F3(~3, ~0x3c, ~0)|ASI(~0x80), "[1],2,d", F_ALIAS, v9 },
{ "casl",       F3(3, 0x3c, 0)|ASI(0x88), F3(~3, ~0x3c, ~0)|ASI(~0x88), "[1],2,d", F_ALIAS, v9 },
{ "casx",       F3(3, 0x3e, 0)|ASI(0x80), F3(~3, ~0x3e, ~0)|ASI(~0x80), "[1],2,d", F_ALIAS, v9 },
{ "casxl",      F3(3, 0x3e, 0)|ASI(0x88), F3(~3, ~0x3e, ~0)|ASI(~0x88), "[1],2,d", F_ALIAS, v9 },

{ "shutdown",   F3F(2, 0x36, 0x080), F3F(~2, ~0x36, ~0x080)|RD_G0|RS1_G0|RS2_G0, "", 0, v9a },

{ "fpadd16",    F3F(2, 0x36, 0x050), F3F(~2, ~0x36, ~0x050), "v,B,H", 0, v9a },
{ "fpadd16s",   F3F(2, 0x36, 0x051), F3F(~2, ~0x36, ~0x051), "e,f,g", 0, v9a },
{ "fpadd32",    F3F(2, 0x36, 0x052), F3F(~2, ~0x36, ~0x052), "v,B,H", 0, v9a },
{ "fpadd32s",   F3F(2, 0x36, 0x053), F3F(~2, ~0x36, ~0x053), "e,f,g", 0, v9a },
{ "fpsub16",    F3F(2, 0x36, 0x054), F3F(~2, ~0x36, ~0x054), "v,B,H", 0, v9a },
{ "fpsub16s",   F3F(2, 0x36, 0x055), F3F(~2, ~0x36, ~0x055), "e,f,g", 0, v9a },
{ "fpsub32",    F3F(2, 0x36, 0x056), F3F(~2, ~0x36, ~0x056), "v,B,H", 0, v9a },
{ "fpsub32s",   F3F(2, 0x36, 0x057), F3F(~2, ~0x36, ~0x057), "e,f,g", 0, v9a },

{ "fpack32",    F3F(2, 0x36, 0x03a), F3F(~2, ~0x36, ~0x03a), "v,B,H", 0, v9a },
{ "fpack16",    F3F(2, 0x36, 0x03b), F3F(~2, ~0x36, ~0x03b)|RS1_G0, "B,g", 0, v9a },
{ "fpackfix",   F3F(2, 0x36, 0x03d), F3F(~2, ~0x36, ~0x03d)|RS1_G0, "B,g", 0, v9a },
{ "fexpand",    F3F(2, 0x36, 0x04d), F3F(~2, ~0x36, ~0x04d)|RS1_G0, "f,H", 0, v9a },
{ "fpmerge",    F3F(2, 0x36, 0x04b), F3F(~2, ~0x36, ~0x04b), "e,f,H", 0, v9a },

{ "fmul8x16",           F3F(2, 0x36, 0x031), F3F(~2, ~0x36, ~0x031), "e,B,H", 0, v9a },
{ "fmul8x16au",         F3F(2, 0x36, 0x033), F3F(~2, ~0x36, ~0x033), "e,f,H", 0, v9a },
{ "fmul8x16al",         F3F(2, 0x36, 0x035), F3F(~2, ~0x36, ~0x035), "e,f,H", 0, v9a },
{ "fmul8sux16",         F3F(2, 0x36, 0x036), F3F(~2, ~0x36, ~0x036), "v,B,H", 0, v9a },
{ "fmul8ulx16",         F3F(2, 0x36, 0x037), F3F(~2, ~0x36, ~0x037), "v,B,H", 0, v9a },
{ "fmuld8sux16",        F3F(2, 0x36, 0x038), F3F(~2, ~0x36, ~0x038), "e,f,H", 0, v9a },
{ "fmuld8ulx16",        F3F(2, 0x36, 0x039), F3F(~2, ~0x36, ~0x039), "e,f,H", 0, v9a },

{ "alignaddr",  F3F(2, 0x36, 0x018), F3F(~2, ~0x36, ~0x018), "1,2,d", 0, v9a },
{ "alignaddrl", F3F(2, 0x36, 0x01a), F3F(~2, ~0x36, ~0x01a), "1,2,d", 0, v9a },
{ "faligndata", F3F(2, 0x36, 0x048), F3F(~2, ~0x36, ~0x048), "v,B,H", 0, v9a },

{ "fzero",      F3F(2, 0x36, 0x060), F3F(~2, ~0x36, ~0x060), "H", 0, v9a },
{ "fzeros",     F3F(2, 0x36, 0x061), F3F(~2, ~0x36, ~0x061), "g", 0, v9a },
{ "fone",       F3F(2, 0x36, 0x07e), F3F(~2, ~0x36, ~0x07e), "H", 0, v9a },
{ "fones",      F3F(2, 0x36, 0x07f), F3F(~2, ~0x36, ~0x07f), "g", 0, v9a },
{ "fsrc1",      F3F(2, 0x36, 0x074), F3F(~2, ~0x36, ~0x074), "v,H", 0, v9a },
{ "fsrc1s",     F3F(2, 0x36, 0x075), F3F(~2, ~0x36, ~0x075), "e,g", 0, v9a },
{ "fsrc2",      F3F(2, 0x36, 0x078), F3F(~2, ~0x36, ~0x078), "B,H", 0, v9a },
{ "fsrc2s",     F3F(2, 0x36, 0x079), F3F(~2, ~0x36, ~0x079), "f,g", 0, v9a },
{ "fnot1",      F3F(2, 0x36, 0x06a), F3F(~2, ~0x36, ~0x06a), "v,H", 0, v9a },
{ "fnot1s",     F3F(2, 0x36, 0x06b), F3F(~2, ~0x36, ~0x06b), "e,g", 0, v9a },
{ "fnot2",      F3F(2, 0x36, 0x066), F3F(~2, ~0x36, ~0x066), "B,H", 0, v9a },
{ "fnot2s",     F3F(2, 0x36, 0x067), F3F(~2, ~0x36, ~0x067), "f,g", 0, v9a },
{ "for",        F3F(2, 0x36, 0x07c), F3F(~2, ~0x36, ~0x07c), "v,B,H", 0, v9a },
{ "fors",       F3F(2, 0x36, 0x07d), F3F(~2, ~0x36, ~0x07d), "e,f,g", 0, v9a },
{ "fnor",       F3F(2, 0x36, 0x062), F3F(~2, ~0x36, ~0x062), "v,B,H", 0, v9a },
{ "fnors",      F3F(2, 0x36, 0x063), F3F(~2, ~0x36, ~0x063), "e,f,g", 0, v9a },
{ "fand",       F3F(2, 0x36, 0x070), F3F(~2, ~0x36, ~0x070), "v,B,H", 0, v9a },
{ "fands",      F3F(2, 0x36, 0x071), F3F(~2, ~0x36, ~0x071), "e,f,g", 0, v9a },
{ "fnand",      F3F(2, 0x36, 0x06e), F3F(~2, ~0x36, ~0x06e), "v,B,H", 0, v9a },
{ "fnands",     F3F(2, 0x36, 0x06f), F3F(~2, ~0x36, ~0x06f), "e,f,g", 0, v9a },
{ "fxor",       F3F(2, 0x36, 0x06c), F3F(~2, ~0x36, ~0x06c), "v,B,H", 0, v9a },
{ "fxors",      F3F(2, 0x36, 0x06d), F3F(~2, ~0x36, ~0x06d), "e,f,g", 0, v9a },
{ "fxnor",      F3F(2, 0x36, 0x072), F3F(~2, ~0x36, ~0x072), "v,B,H", 0, v9a },
{ "fxnors",     F3F(2, 0x36, 0x073), F3F(~2, ~0x36, ~0x073), "e,f,g", 0, v9a },
{ "fornot1",    F3F(2, 0x36, 0x07a), F3F(~2, ~0x36, ~0x07a), "v,B,H", 0, v9a },
{ "fornot1s",   F3F(2, 0x36, 0x07b), F3F(~2, ~0x36, ~0x07b), "e,f,g", 0, v9a },
{ "fornot2",    F3F(2, 0x36, 0x076), F3F(~2, ~0x36, ~0x076), "v,B,H", 0, v9a },
{ "fornot2s",   F3F(2, 0x36, 0x077), F3F(~2, ~0x36, ~0x077), "e,f,g", 0, v9a },
{ "fandnot1",   F3F(2, 0x36, 0x068), F3F(~2, ~0x36, ~0x068), "v,B,H", 0, v9a },
{ "fandnot1s",  F3F(2, 0x36, 0x069), F3F(~2, ~0x36, ~0x069), "e,f,g", 0, v9a },
{ "fandnot2",   F3F(2, 0x36, 0x064), F3F(~2, ~0x36, ~0x064), "v,B,H", 0, v9a },
{ "fandnot2s",  F3F(2, 0x36, 0x065), F3F(~2, ~0x36, ~0x065), "e,f,g", 0, v9a },

{ "fcmpgt16",   F3F(2, 0x36, 0x028), F3F(~2, ~0x36, ~0x028), "v,B,d", 0, v9a },
{ "fcmpgt32",   F3F(2, 0x36, 0x02c), F3F(~2, ~0x36, ~0x02c), "v,B,d", 0, v9a },
{ "fcmple16",   F3F(2, 0x36, 0x020), F3F(~2, ~0x36, ~0x020), "v,B,d", 0, v9a },
{ "fcmple32",   F3F(2, 0x36, 0x024), F3F(~2, ~0x36, ~0x024), "v,B,d", 0, v9a },
{ "fcmpne16",   F3F(2, 0x36, 0x022), F3F(~2, ~0x36, ~0x022), "v,B,d", 0, v9a },
{ "fcmpne32",   F3F(2, 0x36, 0x026), F3F(~2, ~0x36, ~0x026), "v,B,d", 0, v9a },
{ "fcmpeq16",   F3F(2, 0x36, 0x02a), F3F(~2, ~0x36, ~0x02a), "v,B,d", 0, v9a },
{ "fcmpeq32",   F3F(2, 0x36, 0x02e), F3F(~2, ~0x36, ~0x02e), "v,B,d", 0, v9a },

{ "edge8",      F3F(2, 0x36, 0x000), F3F(~2, ~0x36, ~0x000), "1,2,d", 0, v9a },
{ "edge8l",     F3F(2, 0x36, 0x002), F3F(~2, ~0x36, ~0x002), "1,2,d", 0, v9a },
{ "edge16",     F3F(2, 0x36, 0x004), F3F(~2, ~0x36, ~0x004), "1,2,d", 0, v9a },
{ "edge16l",    F3F(2, 0x36, 0x006), F3F(~2, ~0x36, ~0x006), "1,2,d", 0, v9a },
{ "edge32",     F3F(2, 0x36, 0x008), F3F(~2, ~0x36, ~0x008), "1,2,d", 0, v9a },
{ "edge32l",    F3F(2, 0x36, 0x00a), F3F(~2, ~0x36, ~0x00a), "1,2,d", 0, v9a },

{ "pdist",      F3F(2, 0x36, 0x03e), F3F(~2, ~0x36, ~0x03e), "v,B,H", 0, v9a },

{ "array8",     F3F(2, 0x36, 0x010), F3F(~2, ~0x36, ~0x010), "1,2,d", 0, v9a },
{ "array16",    F3F(2, 0x36, 0x012), F3F(~2, ~0x36, ~0x012), "1,2,d", 0, v9a },
{ "array32",    F3F(2, 0x36, 0x014), F3F(~2, ~0x36, ~0x014), "1,2,d", 0, v9a },

{ "edge8n",    F3F(2, 0x36, 0x001), F3F(~2, ~0x36, ~0x001), "1,2,d", 0, v9b },
{ "edge8ln",   F3F(2, 0x36, 0x003), F3F(~2, ~0x36, ~0x003), "1,2,d", 0, v9b },
{ "edge16n",   F3F(2, 0x36, 0x005), F3F(~2, ~0x36, ~0x005), "1,2,d", 0, v9b },
{ "edge16ln",  F3F(2, 0x36, 0x007), F3F(~2, ~0x36, ~0x007), "1,2,d", 0, v9b },
{ "edge32n",   F3F(2, 0x36, 0x009), F3F(~2, ~0x36, ~0x009), "1,2,d", 0, v9b },
{ "edge32ln",  F3F(2, 0x36, 0x00b), F3F(~2, ~0x36, ~0x00b), "1,2,d", 0, v9b },

{ "bmask",     F3F(2, 0x36, 0x019), F3F(~2, ~0x36, ~0x019), "1,2,d", 0, v9b },
{ "bshuffle",  F3F(2, 0x36, 0x04c), F3F(~2, ~0x36, ~0x04c), "v,B,H", 0, v9b },

{ "siam",      F3F(2, 0x36, 0x081), F3F(~2, ~0x36, ~0x081)|RD_G0|RS1_G0|RS2(~7), "3", 0, v9b },

#define IMPDEP(name, code) \
{ name, F3(2, code, 0), F3(~2, ~code, ~0)|ASI(~0), "1,2,d", 0, v9notv9a }, \
{ name, F3(2, code, 1), F3(~2, ~code, ~1),         "1,i,d", 0, v9notv9a }, \
{ name, F3(2, code, 0), F3(~2, ~code, ~0),         "x,1,2,d", 0, v9notv9a }, \
{ name, F3(2, code, 0), F3(~2, ~code, ~0),         "x,e,f,g", 0, v9notv9a }

IMPDEP ("impdep1", 0x36),
IMPDEP ("impdep2", 0x37),

#undef IMPDEP

{ "addxc", F3F(2, 0x36, 0x011), F3F(~2, ~0x36, ~0x011), "1,2,d", 0, v9b },
{ "addxccc", F3F(2, 0x36, 0x013), F3F(~2, ~0x36, ~0x013), "1,2,d", 0, v9b },
{ "umulxhi", F3F(2, 0x36, 0x016), F3F(~2, ~0x36, ~0x016), "1,2,d", 0, v9b },

};

static const int sparc_num_opcodes = ((sizeof sparc_opcodes)/(sizeof sparc_opcodes[0]));


typedef struct
{
  int value;
  const char *name;
} arg;

static const char *
lookup_value (const arg *table, int value)
{
  const arg *p;

  for (p = table; p->name; ++p)
    if (value == p->value)
      return p->name;

  return NULL;
}


static const arg asi_table_v8[] =
{
  { 0x00, "#ASI_M_RES00" },
  { 0x01, "#ASI_M_UNA01" },
  { 0x02, "#ASI_M_MXCC" },
  { 0x03, "#ASI_M_FLUSH_PROBE" },
  { 0x04, "#ASI_M_MMUREGS" },
  { 0x05, "#ASI_M_TLBDIAG" },
  { 0x06, "#ASI_M_DIAGS" },
  { 0x07, "#ASI_M_IODIAG" },
  { 0x08, "#ASI_M_USERTXT" },
  { 0x09, "#ASI_M_KERNELTXT" },
  { 0x0A, "#ASI_M_USERDATA" },
  { 0x0B, "#ASI_M_KERNELDATA" },
  { 0x0C, "#ASI_M_TXTC_TAG" },
  { 0x0D, "#ASI_M_TXTC_DATA" },
  { 0x0E, "#ASI_M_DATAC_TAG" },
  { 0x0F, "#ASI_M_DATAC_DATA" },
  { 0x10, "#ASI_M_FLUSH_PAGE" },
  { 0x11, "#ASI_M_FLUSH_SEG" },
  { 0x12, "#ASI_M_FLUSH_REGION" },
  { 0x13, "#ASI_M_FLUSH_CTX" },
  { 0x14, "#ASI_M_FLUSH_USER" },
  { 0x17, "#ASI_M_BCOPY" },
  { 0x18, "#ASI_M_IFLUSH_PAGE" },
  { 0x19, "#ASI_M_IFLUSH_SEG" },
  { 0x1A, "#ASI_M_IFLUSH_REGION" },
  { 0x1B, "#ASI_M_IFLUSH_CTX" },
  { 0x1C, "#ASI_M_IFLUSH_USER" },
  { 0x1F, "#ASI_M_BFILL" },
  { 0x20, "#ASI_M_BYPASS" },
  { 0x29, "#ASI_M_FBMEM" },
  { 0x2A, "#ASI_M_VMEUS" },
  { 0x2B, "#ASI_M_VMEPS" },
  { 0x2C, "#ASI_M_VMEUT" },
  { 0x2D, "#ASI_M_VMEPT" },
  { 0x2E, "#ASI_M_SBUS" },
  { 0x2F, "#ASI_M_CTL" },
  { 0x31, "#ASI_M_FLUSH_IWHOLE" },
  { 0x36, "#ASI_M_IC_FLCLEAR" },
  { 0x37, "#ASI_M_DC_FLCLEAR" },
  { 0x39, "#ASI_M_DCDR" },
  { 0x40, "#ASI_M_VIKING_TMP1" },
  { 0x41, "#ASI_M_VIKING_TMP2" },
  { 0x4c, "#ASI_M_ACTION" },
  { 0, NULL }
};

static const arg asi_table_v9[] =
{
  { 0x04, "#ASI_N" },
  { 0x0c, "#ASI_N_L" },
  { 0x10, "#ASI_AIUP" },
  { 0x11, "#ASI_AIUS" },
  { 0x18, "#ASI_AIUP_L" },
  { 0x19, "#ASI_AIUS_L" },
  { 0x80, "#ASI_P" },
  { 0x81, "#ASI_S" },
  { 0x82, "#ASI_PNF" },
  { 0x83, "#ASI_SNF" },
  { 0x88, "#ASI_P_L" },
  { 0x89, "#ASI_S_L" },
  { 0x8a, "#ASI_PNF_L" },
  { 0x8b, "#ASI_SNF_L" },
  { 0x04, "#ASI_NUCLEUS" },
  { 0x0c, "#ASI_NUCLEUS_LITTLE" },
  { 0x10, "#ASI_AS_IF_USER_PRIMARY" },
  { 0x11, "#ASI_AS_IF_USER_SECONDARY" },
  { 0x18, "#ASI_AS_IF_USER_PRIMARY_LITTLE" },
  { 0x19, "#ASI_AS_IF_USER_SECONDARY_LITTLE" },
  { 0x80, "#ASI_PRIMARY" },
  { 0x81, "#ASI_SECONDARY" },
  { 0x82, "#ASI_PRIMARY_NOFAULT" },
  { 0x83, "#ASI_SECONDARY_NOFAULT" },
  { 0x88, "#ASI_PRIMARY_LITTLE" },
  { 0x89, "#ASI_SECONDARY_LITTLE" },
  { 0x8a, "#ASI_PRIMARY_NOFAULT_LITTLE" },
  { 0x8b, "#ASI_SECONDARY_NOFAULT_LITTLE" },
  { 0x14, "#ASI_PHYS_USE_EC"},
  { 0x15, "#ASI_PHYS_BYPASS_EC_WITH_EBIT"},
  { 0x45, "#ASI_LSU_CONTROL_REG"},
  { 0x47, "#ASI_DCACHE_TAG"},
  { 0x4a, "#ASI_UPA_CONFIG_REG"},
  { 0x50, "#ASI_IMMU" },
  { 0x51, "#ASI_IMMU_TSB_8KB_PTR_REG" },
  { 0x52, "#ASI_IMMU_TSB_64KB_PTR_REG" },
  { 0x54, "#ASI_ITLB_DATA_IN_REG" },
  { 0x55, "#ASI_ITLB_DATA_ACCESS_REG" },
  { 0x56, "#ASI_ITLB_TAG_READ_REG" },
  { 0x57, "#ASI_IMMU_DEMAP" },
  { 0x58, "#ASI_DMMU" },
  { 0x59, "#ASI_DMMU_TSB_8KB_PTR_REG" },
  { 0x5a, "#ASI_DMMU_TSB_64KB_PTR_REG" },
  { 0x5b, "#ASI_DMMU_TSB_DIRECT_PTR_REG" },
  { 0x5c, "#ASI_DTLB_DATA_IN_REG" },
  { 0x5d, "#ASI_DTLB_DATA_ACCESS_REG" },
  { 0x5e, "#ASI_DTLB_TAG_READ_REG" },
  { 0x5f, "#ASI_DMMU_DEMAP" },
  { 0x67, "#ASI_IC_TAG"},
  { 0, NULL }
};

static const char *
sparc_decode_asi_v9 (int value)
{
  return lookup_value (asi_table_v9, value);
}

static const char *
sparc_decode_asi_v8 (int value)
{
  return lookup_value (asi_table_v8, value);
}


static const arg membar_table[] =
{
  { 0x40, "#Sync" },
  { 0x20, "#MemIssue" },
  { 0x10, "#Lookaside" },
  { 0x08, "#StoreStore" },
  { 0x04, "#LoadStore" },
  { 0x02, "#StoreLoad" },
  { 0x01, "#LoadLoad" },
  { 0, NULL }
};

static const char *
sparc_decode_membar (int value)
{
  return lookup_value (membar_table, value);
}


static const arg prefetch_table[] =
{
  { 0, "#n_reads" },
  { 1, "#one_read" },
  { 2, "#n_writes" },
  { 3, "#one_write" },
  { 4, "#page" },
  { 16, "#invalidate" },
  { 0, NULL }
};

static const char *
sparc_decode_prefetch (int value)
{
  return lookup_value (prefetch_table, value);
}


static const arg sparclet_cpreg_table[] =
{
  { 0, "%ccsr" },
  { 1, "%ccfr" },
  { 2, "%cccrcr" },
  { 3, "%ccpr" },
  { 4, "%ccsr2" },
  { 5, "%cccrr" },
  { 6, "%ccrstr" },
  { 0, NULL }
};

static const char *
sparc_decode_sparclet_cpreg (int value)
{
  return lookup_value (sparclet_cpreg_table, value);
}

#undef MASK_V9

/* Print SPARC instructions.
   Copyright 1989, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.  */

#define MASK_V9 ((1 << SPARC_OPCODE_ARCH_V9) \
                 | (1 << SPARC_OPCODE_ARCH_V9A) \
                 | (1 << SPARC_OPCODE_ARCH_V9B))
#define V9_ONLY_P(insn) (! ((insn)->architecture & ~MASK_V9))
#define V9_P(insn) (((insn)->architecture & MASK_V9) != 0)

static const sparc_opcode **sorted_opcodes;

#define HASH_SIZE 256
static const int opcode_bits[4] = { 0x01c00000, 0x0, 0x01f80000, 0x01f80000 };
#define HASH_INSN(INSN) \
  ((((INSN) >> 24) & 0xc0) | (((INSN) & opcode_bits[((INSN) >> 30) & 3]) >> 19))
typedef struct sparc_opcode_hash
{
  struct sparc_opcode_hash *next;
  const sparc_opcode *opcode;
} sparc_opcode_hash;

static sparc_opcode_hash *opcode_hash_table[HASH_SIZE];

#define SEX(value, bits) \
        ((((int)(value)) << ((8 * sizeof (int)) - bits))        \
                         >> ((8 * sizeof (int)) - bits) )

static const char * const reg_names[] =
{ "g0", "g1", "g2", "g3", "g4", "g5", "g6", "g7",
  "o0", "o1", "o2", "o3", "o4", "o5", "sp", "o7",
  "l0", "l1", "l2", "l3", "l4", "l5", "l6", "l7",
  "i0", "i1", "i2", "i3", "i4", "i5", "fp", "i7",
  "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
  "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15",
  "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
  "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31",
  "f32", "f33", "f34", "f35", "f36", "f37", "f38", "f39",
  "f40", "f41", "f42", "f43", "f44", "f45", "f46", "f47",
  "f48", "f49", "f50", "f51", "f52", "f53", "f54", "f55",
  "f56", "f57", "f58", "f59", "f60", "f61", "f62", "f63",
  "y", "psr", "wim", "tbr", "pc", "npc", "fpsr", "cpsr"
};

#define freg_names      (&reg_names[4 * 8])

static const char * const v9_priv_reg_names[] =
{
  "tpc", "tnpc", "tstate", "tt", "tick", "tba", "pstate", "tl",
  "pil", "cwp", "cansave", "canrestore", "cleanwin", "otherwin",
  "wstate", "fq", "gl"
};

static const char * const v9_hpriv_reg_names[] =
{
  "hpstate", "htstate", "resv2", "hintp", "resv4", "htba", "hver",
  "resv7", "resv8", "resv9", "resv10", "resv11", "resv12", "resv13",
  "resv14", "resv15", "resv16", "resv17", "resv18", "resv19", "resv20",
  "resv21", "resv22", "resv23", "resv24", "resv25", "resv26", "resv27",
  "resv28", "resv29", "resv30", "hstick_cmpr"
};

static const char * const v9a_asr_reg_names[] =
{
  "pcr", "pic", "dcr", "gsr", "set_softint", "clear_softint",
  "softint", "tick_cmpr", "sys_tick", "sys_tick_cmpr"
};

#define X_RD(i)      (((i) >> 25) & 0x1f)
#define X_RS1(i)     (((i) >> 14) & 0x1f)
#define X_LDST_I(i)  (((i) >> 13) & 1)
#define X_ASI(i)     (((i) >> 5) & 0xff)
#define X_RS2(i)     (((i) >> 0) & 0x1f)
#define X_IMM(i,n)   (((i) >> 0) & ((1 << (n)) - 1))
#define X_SIMM(i,n)  SEX (X_IMM ((i), (n)), (n))
#define X_DISP22(i)  (((i) >> 0) & 0x3fffff)
#define X_IMM22(i)   X_DISP22 (i)
#define X_DISP30(i)  (((i) >> 0) & 0x3fffffff)

#define X_DISP16(i)  (((((i) >> 20) & 3) << 14) | (((i) >> 0) & 0x3fff))
#define X_DISP19(i)  (((i) >> 0) & 0x7ffff)
#define X_MEMBAR(i)  ((i) & 0x7f)

static int
is_delayed_branch (unsigned long insn)
{
  sparc_opcode_hash *op;

  for (op = opcode_hash_table[HASH_INSN (insn)]; op; op = op->next)
    {
      const sparc_opcode *opcode = op->opcode;

      if ((opcode->match & insn) == opcode->match
          && (opcode->lose & insn) == 0)
        return opcode->flags & F_DELAYED;
    }
  return 0;
}

static unsigned int current_arch_mask;

static int
compute_arch_mask (unsigned long mach)
{
  switch (mach)
    {
    case 0 :
    case bfd_mach_sparc :
      return SPARC_OPCODE_ARCH_MASK (SPARC_OPCODE_ARCH_V8);
    case bfd_mach_sparc_sparclet :
      return SPARC_OPCODE_ARCH_MASK (SPARC_OPCODE_ARCH_SPARCLET);
    case bfd_mach_sparc_sparclite :
    case bfd_mach_sparc_sparclite_le :
      return (SPARC_OPCODE_ARCH_MASK (SPARC_OPCODE_ARCH_SPARCLITE)
              | SPARC_OPCODE_ARCH_MASK (SPARC_OPCODE_ARCH_V8));
    case bfd_mach_sparc_v8plus :
    case bfd_mach_sparc_v9 :
      return SPARC_OPCODE_ARCH_MASK (SPARC_OPCODE_ARCH_V9);
    case bfd_mach_sparc_v8plusa :
    case bfd_mach_sparc_v9a :
      return SPARC_OPCODE_ARCH_MASK (SPARC_OPCODE_ARCH_V9A);
    case bfd_mach_sparc_v8plusb :
    case bfd_mach_sparc_v9b :
      return SPARC_OPCODE_ARCH_MASK (SPARC_OPCODE_ARCH_V9B);
    }
  abort ();
}

static int
compare_opcodes (const void * a, const void * b)
{
  sparc_opcode *op0 = * (sparc_opcode **) a;
  sparc_opcode *op1 = * (sparc_opcode **) b;
  unsigned long int match0 = op0->match, match1 = op1->match;
  unsigned long int lose0 = op0->lose, lose1 = op1->lose;
  register unsigned int i;

  if (op0->architecture & current_arch_mask)
    {
      if (! (op1->architecture & current_arch_mask))
        return -1;
    }
  else
    {
      if (op1->architecture & current_arch_mask)
        return 1;
      else if (op0->architecture != op1->architecture)
        return op0->architecture - op1->architecture;
    }

  if (match0 & lose0)
    {
      fprintf
        (stderr,
         "Internal error:  bad sparc-opcode.h: \"%s\", %#.8lx, %#.8lx\n",
         op0->name, match0, lose0);
      op0->lose &= ~op0->match;
      lose0 = op0->lose;
    }

  if (match1 & lose1)
    {
      fprintf
        (stderr,
         "Internal error: bad sparc-opcode.h: \"%s\", %#.8lx, %#.8lx\n",
         op1->name, match1, lose1);
      op1->lose &= ~op1->match;
      lose1 = op1->lose;
    }

  for (i = 0; i < 32; ++i)
    {
      unsigned long int x = 1 << i;
      int x0 = (match0 & x) != 0;
      int x1 = (match1 & x) != 0;

      if (x0 != x1)
        return x1 - x0;
    }

  for (i = 0; i < 32; ++i)
    {
      unsigned long int x = 1 << i;
      int x0 = (lose0 & x) != 0;
      int x1 = (lose1 & x) != 0;

      if (x0 != x1)
        return x1 - x0;
    }

  {
    int alias_diff = (op0->flags & F_ALIAS) - (op1->flags & F_ALIAS);

    if (alias_diff != 0)
      return alias_diff;
  }

  i = strcmp (op0->name, op1->name);
  if (i)
    {
      if (op0->flags & F_ALIAS)
        return i;
      else
        fprintf (stderr,
                 "Internal error: bad sparc-opcode.h: \"%s\" == \"%s\"\n",
                 op0->name, op1->name);
    }

  {
    int length_diff = strlen (op0->args) - strlen (op1->args);

    if (length_diff != 0)
      return length_diff;
  }

  {
    char *p0 = (char *) strchr (op0->args, '+');
    char *p1 = (char *) strchr (op1->args, '+');

    if (p0 && p1)
      {
        if (p0[-1] == 'i' && p1[1] == 'i')
          return 1;
        if (p0[1] == 'i' && p1[-1] == 'i')
          return -1;
      }
  }

  {
    int i0 = strncmp (op0->args, "i,1", 3) == 0;
    int i1 = strncmp (op1->args, "i,1", 3) == 0;

    if (i0 ^ i1)
      return i0 - i1;
  }

  return 0;
}

static void
build_hash_table (const sparc_opcode **opcode_table,
                  sparc_opcode_hash **hash_table,
                  int num_opcodes)
{
  int i;
  int hash_count[HASH_SIZE];
  static sparc_opcode_hash *hash_buf = NULL;

  memset (hash_table, 0, HASH_SIZE * sizeof (hash_table[0]));
  memset (hash_count, 0, HASH_SIZE * sizeof (hash_count[0]));
  free(hash_buf);
  hash_buf = malloc (sizeof (* hash_buf) * num_opcodes);
  for (i = num_opcodes - 1; i >= 0; --i)
    {
      int hash = HASH_INSN (opcode_table[i]->match);
      sparc_opcode_hash *h = &hash_buf[i];

      h->next = hash_table[hash];
      h->opcode = opcode_table[i];
      hash_table[hash] = h;
      ++hash_count[hash];
    }

#if 0
  {
    int min_count = num_opcodes, max_count = 0;
    int total;

    for (i = 0; i < HASH_SIZE; ++i)
      {
        if (hash_count[i] < min_count)
          min_count = hash_count[i];
        if (hash_count[i] > max_count)
          max_count = hash_count[i];
        total += hash_count[i];
      }

    printf ("Opcode hash table stats: min %d, max %d, ave %f\n",
            min_count, max_count, (double) total / HASH_SIZE);
  }
#endif
}

int
print_insn_sparc (bfd_vma memaddr, disassemble_info *info)
{
  FILE *stream = info->stream;
  bfd_byte buffer[4];
  unsigned long insn;
  sparc_opcode_hash *op;
  static int opcodes_initialized = 0;
  static unsigned long current_mach = 0;
  bfd_vma (*getword) (const unsigned char *);

  if (!opcodes_initialized
      || info->mach != current_mach)
    {
      int i;

      current_arch_mask = compute_arch_mask (info->mach);

      if (!opcodes_initialized)
        sorted_opcodes =
          malloc (sparc_num_opcodes * sizeof (sparc_opcode *));
      for (i = 0; i < sparc_num_opcodes; ++i)
        sorted_opcodes[i] = &sparc_opcodes[i];
      qsort ((char *) sorted_opcodes, sparc_num_opcodes,
             sizeof (sorted_opcodes[0]), compare_opcodes);

      build_hash_table (sorted_opcodes, opcode_hash_table, sparc_num_opcodes);
      current_mach = info->mach;
      opcodes_initialized = 1;
    }

  {
    int status =
      (*info->read_memory_func) (memaddr, buffer, sizeof (buffer), info);

    if (status != 0)
      {
        (*info->memory_error_func) (status, memaddr, info);
        return -1;
      }
  }

  if (info->endian == BFD_ENDIAN_BIG || info->mach == bfd_mach_sparc_sparclite)
    getword = bfd_getb32;
  else
    getword = bfd_getl32;

  insn = getword (buffer);

  info->insn_info_valid = 1;
  info->insn_type = dis_nonbranch;
  info->branch_delay_insns = 0;
  info->target = 0;

  for (op = opcode_hash_table[HASH_INSN (insn)]; op; op = op->next)
    {
      const sparc_opcode *opcode = op->opcode;

      if (! (opcode->architecture & current_arch_mask))
        continue;

      if ((opcode->match & insn) == opcode->match
          && (opcode->lose & insn) == 0)
        {
          int imm_added_to_rs1 = 0;
          int imm_ored_to_rs1 = 0;

          int found_plus = 0;

          if (opcode->match == 0x80102000)
            imm_ored_to_rs1 = 1;
          if (opcode->match == 0x80002000)
            imm_added_to_rs1 = 1;

          if (X_RS1 (insn) != X_RD (insn)
              && strchr (opcode->args, 'r') != NULL)
              continue;
          if (X_RS2 (insn) != X_RD (insn)
              && strchr (opcode->args, 'O') != NULL)
              continue;

          (*info->fprintf_func) (stream, "%s", opcode->name);

          {
            const char *s;

            if (opcode->args[0] != ',')
              (*info->fprintf_func) (stream, " ");

            for (s = opcode->args; *s != '\0'; ++s)
              {
                while (*s == ',')
                  {
                    (*info->fprintf_func) (stream, ",");
                    ++s;
                    switch (*s)
                      {
                      case 'a':
                        (*info->fprintf_func) (stream, "a");
                        ++s;
                        continue;
                      case 'N':
                        (*info->fprintf_func) (stream, "pn");
                        ++s;
                        continue;

                      case 'T':
                        (*info->fprintf_func) (stream, "pt");
                        ++s;
                        continue;

                      default:
                        break;
                      }
                  }

                (*info->fprintf_func) (stream, " ");

                switch (*s)
                  {
                  case '+':
                    found_plus = 1;
                    /* Fall through.  */

                  default:
                    (*info->fprintf_func) (stream, "%c", *s);
                    break;

                  case '#':
                    (*info->fprintf_func) (stream, "0");
                    break;

#define reg(n)  (*info->fprintf_func) (stream, "%%%s", reg_names[n])
                  case '1':
                  case 'r':
                    reg (X_RS1 (insn));
                    break;

                  case '2':
                  case 'O':
                    reg (X_RS2 (insn));
                    break;

                  case 'd':
                    reg (X_RD (insn));
                    break;
#undef  reg

#define freg(n)         (*info->fprintf_func) (stream, "%%%s", freg_names[n])
#define fregx(n)        (*info->fprintf_func) (stream, "%%%s", freg_names[((n) & ~1) | (((n) & 1) << 5)])
                  case 'e':
                    freg (X_RS1 (insn));
                    break;
                  case 'v':
                  case 'V':
                    fregx (X_RS1 (insn));
                    break;

                  case 'f':
                    freg (X_RS2 (insn));
                    break;
                  case 'B':
                  case 'R':
                    fregx (X_RS2 (insn));
                    break;

                  case 'g':
                    freg (X_RD (insn));
                    break;
                  case 'H':
                  case 'J':
                    fregx (X_RD (insn));
                    break;
#undef  freg
#undef  fregx

#define creg(n) (*info->fprintf_func) (stream, "%%c%u", (unsigned int) (n))
                  case 'b':
                    creg (X_RS1 (insn));
                    break;

                  case 'c':
                    creg (X_RS2 (insn));
                    break;

                  case 'D':
                    creg (X_RD (insn));
                    break;
#undef  creg

                  case 'h':
                    (*info->fprintf_func) (stream, "%%hi(%#x)",
                                           ((unsigned) 0xFFFFFFFF
                                            & ((int) X_IMM22 (insn) << 10)));
                    break;

                  case 'i':
                  case 'I':
                  case 'j':
                    {
                      int imm;

                      if (*s == 'i')
                        imm = X_SIMM (insn, 13);
                      else if (*s == 'I')
                        imm = X_SIMM (insn, 11);
                      else
                        imm = X_SIMM (insn, 10);

                      if (found_plus)
                        imm_added_to_rs1 = 1;

                      if (imm <= 9)
                        (*info->fprintf_func) (stream, "%d", imm);
                      else
                        (*info->fprintf_func) (stream, "%#x", imm);
                    }
                    break;

                  case 'X':
                  case 'Y':
                    {
                      int imm = X_IMM (insn, *s == 'X' ? 5 : 6);

                      if (imm <= 9)
                        (info->fprintf_func) (stream, "%d", imm);
                      else
                        (info->fprintf_func) (stream, "%#x", (unsigned) imm);
                    }
                    break;

                  case '3':
                    (info->fprintf_func) (stream, "%ld", X_IMM (insn, 3));
                    break;

                  case 'K':
                    {
                      int mask = X_MEMBAR (insn);
                      int bit = 0x40, printed_one = 0;
                      const char *name;

                      if (mask == 0)
                        (info->fprintf_func) (stream, "0");
                      else
                        while (bit)
                          {
                            if (mask & bit)
                              {
                                if (printed_one)
                                  (info->fprintf_func) (stream, "|");
                                name = sparc_decode_membar (bit);
                                (info->fprintf_func) (stream, "%s", name);
                                printed_one = 1;
                              }
                            bit >>= 1;
                          }
                      break;
                    }

                  case 'k':
                    info->target = memaddr + SEX (X_DISP16 (insn), 16) * 4;
                    (*info->print_address_func) (info->target, info);
                    break;

                  case 'G':
                    info->target = memaddr + SEX (X_DISP19 (insn), 19) * 4;
                    (*info->print_address_func) (info->target, info);
                    break;

                  case '6':
                  case '7':
                  case '8':
                  case '9':
                    (*info->fprintf_func) (stream, "%%fcc%c", *s - '6' + '0');
                    break;

                  case 'z':
                    (*info->fprintf_func) (stream, "%%icc");
                    break;

                  case 'Z':
                    (*info->fprintf_func) (stream, "%%xcc");
                    break;

                  case 'E':
                    (*info->fprintf_func) (stream, "%%ccr");
                    break;

                  case 's':
                    (*info->fprintf_func) (stream, "%%fprs");
                    break;

                  case 'o':
                    (*info->fprintf_func) (stream, "%%asi");
                    break;

                  case 'W':
                    (*info->fprintf_func) (stream, "%%tick");
                    break;

                  case 'P':
                    (*info->fprintf_func) (stream, "%%pc");
                    break;

                  case '?':
                    if (X_RS1 (insn) == 31)
                      (*info->fprintf_func) (stream, "%%ver");
                    else if ((unsigned) X_RS1 (insn) < 17)
                      (*info->fprintf_func) (stream, "%%%s",
                                             v9_priv_reg_names[X_RS1 (insn)]);
                    else
                      (*info->fprintf_func) (stream, "%%reserved");
                    break;

                  case '!':
                    if ((unsigned) X_RD (insn) < 17)
                      (*info->fprintf_func) (stream, "%%%s",
                                             v9_priv_reg_names[X_RD (insn)]);
                    else
                      (*info->fprintf_func) (stream, "%%reserved");
                    break;

                  case '$':
                    if ((unsigned) X_RS1 (insn) < 32)
                      (*info->fprintf_func) (stream, "%%%s",
                                             v9_hpriv_reg_names[X_RS1 (insn)]);
                    else
                      (*info->fprintf_func) (stream, "%%reserved");
                    break;

                  case '%':
                    if ((unsigned) X_RD (insn) < 32)
                      (*info->fprintf_func) (stream, "%%%s",
                                             v9_hpriv_reg_names[X_RD (insn)]);
                    else
                      (*info->fprintf_func) (stream, "%%reserved");
                    break;

                  case '/':
                    if (X_RS1 (insn) < 16 || X_RS1 (insn) > 25)
                      (*info->fprintf_func) (stream, "%%reserved");
                    else
                      (*info->fprintf_func) (stream, "%%%s",
                                             v9a_asr_reg_names[X_RS1 (insn)-16]);
                    break;

                  case '_':
                    if (X_RD (insn) < 16 || X_RD (insn) > 25)
                      (*info->fprintf_func) (stream, "%%reserved");
                    else
                      (*info->fprintf_func) (stream, "%%%s",
                                             v9a_asr_reg_names[X_RD (insn)-16]);
                    break;

                  case '*':
                    {
                      const char *name = sparc_decode_prefetch (X_RD (insn));

                      if (name)
                        (*info->fprintf_func) (stream, "%s", name);
                      else
                        (*info->fprintf_func) (stream, "%ld", X_RD (insn));
                      break;
                    }

                  case 'M':
                    (*info->fprintf_func) (stream, "%%asr%ld", X_RS1 (insn));
                    break;

                  case 'm':
                    (*info->fprintf_func) (stream, "%%asr%ld", X_RD (insn));
                    break;

                  case 'L':
                    info->target = memaddr + SEX (X_DISP30 (insn), 30) * 4;
                    (*info->print_address_func) (info->target, info);
                    break;

                  case 'n':
                    (*info->fprintf_func)
                      (stream, "%#x", SEX (X_DISP22 (insn), 22));
                    break;

                  case 'l':
                    info->target = memaddr + SEX (X_DISP22 (insn), 22) * 4;
                    (*info->print_address_func) (info->target, info);
                    break;

                  case 'A':
                    {
                      const char *name;

                      if ((info->mach == bfd_mach_sparc_v8plusa) ||
                          ((info->mach >= bfd_mach_sparc_v9) &&
                           (info->mach <= bfd_mach_sparc_v9b)))
                        name = sparc_decode_asi_v9 (X_ASI (insn));
                      else
                        name = sparc_decode_asi_v8 (X_ASI (insn));

                      if (name)
                        (*info->fprintf_func) (stream, "%s", name);
                      else
                        (*info->fprintf_func) (stream, "(%ld)", X_ASI (insn));
                      break;
                    }

                  case 'C':
                    (*info->fprintf_func) (stream, "%%csr");
                    break;

                  case 'F':
                    (*info->fprintf_func) (stream, "%%fsr");
                    break;

                  case 'p':
                    (*info->fprintf_func) (stream, "%%psr");
                    break;

                  case 'q':
                    (*info->fprintf_func) (stream, "%%fq");
                    break;

                  case 'Q':
                    (*info->fprintf_func) (stream, "%%cq");
                    break;

                  case 't':
                    (*info->fprintf_func) (stream, "%%tbr");
                    break;

                  case 'w':
                    (*info->fprintf_func) (stream, "%%wim");
                    break;

                  case 'x':
                    (*info->fprintf_func) (stream, "%ld",
                                           ((X_LDST_I (insn) << 8)
                                            + X_ASI (insn)));
                    break;

                  case 'y':
                    (*info->fprintf_func) (stream, "%%y");
                    break;

                  case 'u':
                  case 'U':
                    {
                      int val = *s == 'U' ? X_RS1 (insn) : X_RD (insn);
                      const char *name = sparc_decode_sparclet_cpreg (val);

                      if (name)
                        (*info->fprintf_func) (stream, "%s", name);
                      else
                        (*info->fprintf_func) (stream, "%%cpreg(%d)", val);
                      break;
                    }
                  }
              }
          }

          if (imm_ored_to_rs1 || imm_added_to_rs1)
            {
              unsigned long prev_insn;
              int errcode;

              if (memaddr >= 4)
                errcode =
                  (*info->read_memory_func)
                  (memaddr - 4, buffer, sizeof (buffer), info);
              else
                errcode = 1;

              prev_insn = getword (buffer);

              if (errcode == 0)
                {

                  if (is_delayed_branch (prev_insn))
                    {
                      if (memaddr >= 8)
                        errcode = (*info->read_memory_func)
                          (memaddr - 8, buffer, sizeof (buffer), info);
                      else
                        errcode = 1;

                      prev_insn = getword (buffer);
                    }
                }

              if (errcode == 0)
                {
                  if ((prev_insn & 0xc1c00000) == 0x01000000
                      && X_RD (prev_insn) == X_RS1 (insn))
                    {
                      (*info->fprintf_func) (stream, "\t! ");
                      info->target =
                        ((unsigned) 0xFFFFFFFF
                         & ((int) X_IMM22 (prev_insn) << 10));
                      if (imm_added_to_rs1)
                        info->target += X_SIMM (insn, 13);
                      else
                        info->target |= X_SIMM (insn, 13);
                      (*info->print_address_func) (info->target, info);
                      info->insn_type = dis_dref;
                      info->data_size = 4;
                    }
                }
            }

          if (opcode->flags & (F_UNBR|F_CONDBR|F_JSR))
            {
              if (opcode->flags & F_UNBR)
                info->insn_type = dis_branch;
              if (opcode->flags & F_CONDBR)
                info->insn_type = dis_condbranch;
              if (opcode->flags & F_JSR)
                info->insn_type = dis_jsr;
              if (opcode->flags & F_DELAYED)
                info->branch_delay_insns = 1;
            }

          return sizeof (buffer);
        }
    }

  info->insn_type = dis_noninsn;
  (*info->fprintf_func) (stream, ".long %#08lx", insn);
  return sizeof (buffer);
}
