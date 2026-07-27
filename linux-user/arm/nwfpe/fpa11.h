/*
    NetWinder Floating Point Emulator
    (c) Rebel.com, 1998-1999

    Direct questions, comments to Scott Bambrough <scottb@netwinder.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#ifndef FPA11_H
#define FPA11_H

#include "cpu.h"

#define GET_FPA11() (qemufpa)

extern CPUARMState *user_registers;

#define GET_USERREG() (user_registers)

#include "fpsr.h"
#include "fpu/softfloat.h"

#define		typeNone		0x00
#define		typeSingle		0x01
#define		typeDouble		0x02
#define		typeExtended		0x03

typedef union tagFPREG {
   floatx80 fExtended;
   float64  fDouble;
   float32  fSingle;
} FPREG;

typedef struct tagFPA11 {
  FPREG fpreg[8];
  FPSR fpsr;
  FPCR fpcr;
  unsigned char fType[8];
  int initflag;
    float_status fp_status;
} FPA11;

extern FPA11* qemufpa;

void resetFPA11(void);
void SetRoundingMode(const unsigned int);
void SetRoundingPrecision(const unsigned int);

static inline unsigned int readRegister(unsigned int reg)
{
    return (user_registers->regs[(reg)]);
}

static inline void writeRegister(unsigned int x, unsigned int y)
{
#if 0
	printf("writing %d to r%d\n",y,x);
#endif
        user_registers->regs[(x)]=(y);
}

static inline void writeConditionCodes(unsigned int x)
{
    cpsr_write(user_registers, x, CPSR_NZCV, CPSRWriteByInstr);
}

#define ARM_REG_PC 15

unsigned int EmulateAll(unsigned int opcode, FPA11* qfpa, CPUARMState* qregs);

unsigned int EmulateCPDO(const unsigned int);
unsigned int EmulateCPDT(const unsigned int);
unsigned int EmulateCPRT(const unsigned int);

unsigned int SingleCPDO(const unsigned int opcode);
unsigned int DoubleCPDO(const unsigned int opcode);
unsigned int ExtendedCPDO(const unsigned int opcode);


#include "qemu.h"

#endif
