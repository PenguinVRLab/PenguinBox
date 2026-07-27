/*
    NetWinder Floating Point Emulator
    (c) Rebel.COM, 1998,1999

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

#include "qemu/osdep.h"
#include "fpa11.h"

#include "fpopcode.h"

FPA11* qemufpa = NULL;
CPUARMState* user_registers;

void resetFPA11(void)
{
  int i;
  FPA11 *fpa11 = GET_FPA11();

  for (i=0;i<=7;i++)
  {
    fpa11->fType[i] = typeNone;
  }

  fpa11->fpsr = FP_EMULATOR | BIT_AC;

#ifdef MAINTAIN_FPCR
  fpa11->fpcr = MASK_RESET;
#endif

  set_float_2nan_prop_rule(float_2nan_prop_s_ab, &fpa11->fp_status);
  set_float_default_nan_pattern(0b01000000, &fpa11->fp_status);
}

void SetRoundingMode(const unsigned int opcode)
{
    int rounding_mode;
   FPA11 *fpa11 = GET_FPA11();

#ifdef MAINTAIN_FPCR
   fpa11->fpcr &= ~MASK_ROUNDING_MODE;
#endif
   switch (opcode & MASK_ROUNDING_MODE)
   {
      default:
      case ROUND_TO_NEAREST:
         rounding_mode = float_round_nearest_even;
#ifdef MAINTAIN_FPCR
         fpa11->fpcr |= ROUND_TO_NEAREST;
#endif
      break;

      case ROUND_TO_PLUS_INFINITY:
         rounding_mode = float_round_up;
#ifdef MAINTAIN_FPCR
         fpa11->fpcr |= ROUND_TO_PLUS_INFINITY;
#endif
      break;

      case ROUND_TO_MINUS_INFINITY:
         rounding_mode = float_round_down;
#ifdef MAINTAIN_FPCR
         fpa11->fpcr |= ROUND_TO_MINUS_INFINITY;
#endif
      break;

      case ROUND_TO_ZERO:
         rounding_mode = float_round_to_zero;
#ifdef MAINTAIN_FPCR
         fpa11->fpcr |= ROUND_TO_ZERO;
#endif
      break;
  }
   set_float_rounding_mode(rounding_mode, &fpa11->fp_status);
}

void SetRoundingPrecision(const unsigned int opcode)
{
    FloatX80RoundPrec rounding_precision;
    FPA11 *fpa11 = GET_FPA11();
#ifdef MAINTAIN_FPCR
    fpa11->fpcr &= ~MASK_ROUNDING_PRECISION;
#endif
    switch (opcode & MASK_ROUNDING_PRECISION) {
    case ROUND_SINGLE:
        rounding_precision = floatx80_precision_s;
#ifdef MAINTAIN_FPCR
        fpa11->fpcr |= ROUND_SINGLE;
#endif
        break;

    case ROUND_DOUBLE:
        rounding_precision = floatx80_precision_d;
#ifdef MAINTAIN_FPCR
        fpa11->fpcr |= ROUND_DOUBLE;
#endif
        break;

    case ROUND_EXTENDED:
        rounding_precision = floatx80_precision_x;
#ifdef MAINTAIN_FPCR
        fpa11->fpcr |= ROUND_EXTENDED;
#endif
        break;

    default:
        rounding_precision = floatx80_precision_x;
        break;
    }
    set_floatx80_rounding_precision(rounding_precision, &fpa11->fp_status);
}

unsigned int EmulateAll(unsigned int opcode, FPA11* qfpa, CPUARMState* qregs)
{
  unsigned int nRc = 0;
  FPA11 *fpa11;
  unsigned int cp;

  cp = (opcode >> 8) & 0xf;
  if (cp != 1 && cp != 2) {
    return 0;
  }

  qemufpa=qfpa;
  user_registers=qregs;

#if 0
  fprintf(stderr,"emulating FP insn 0x%08x, PC=0x%08x\n",
          opcode, qregs[ARM_REG_PC]);
#endif
  fpa11 = GET_FPA11();

  if (fpa11->initflag == 0)
  {
    resetFPA11();
    SetRoundingMode(ROUND_TO_NEAREST);
    SetRoundingPrecision(ROUND_EXTENDED);
    fpa11->initflag = 1;
  }

  set_float_exception_flags(0, &fpa11->fp_status);

  if (TEST_OPCODE(opcode,MASK_CPRT))
  {
    nRc = EmulateCPRT(opcode);
  }
  else if (TEST_OPCODE(opcode,MASK_CPDO))
  {
    nRc = EmulateCPDO(opcode);
  }
  else if (TEST_OPCODE(opcode,MASK_CPDT))
  {
    nRc = EmulateCPDT(opcode);
  }
  else
  {
    nRc = 0;
  }

  if(nRc == 1 && get_float_exception_flags(&fpa11->fp_status))
  {
    nRc = -get_float_exception_flags(&fpa11->fp_status);
  }

  return(nRc);
}

#if 0
unsigned int EmulateAll1(unsigned int opcode)
{
  switch ((opcode >> 24) & 0xf)
  {
     case 0xc:
     case 0xd:
       if ((opcode >> 20) & 0x1)
       {
          switch ((opcode >> 8) & 0xf)
          {
             case 0x1: return PerformLDF(opcode); break;
             case 0x2: return PerformLFM(opcode); break;
             default: return 0;
          }
       }
       else
       {
          switch ((opcode >> 8) & 0xf)
          {
             case 0x1: return PerformSTF(opcode); break;
             case 0x2: return PerformSFM(opcode); break;
             default: return 0;
          }
      }
     break;

     case 0xe:
       if (opcode & 0x10)
         return EmulateCPDO(opcode);
       else
         return EmulateCPRT(opcode);
     break;

     default: return 0;
  }
}
#endif
