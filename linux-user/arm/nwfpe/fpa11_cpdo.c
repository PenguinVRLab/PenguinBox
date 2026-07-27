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

unsigned int EmulateCPDO(const unsigned int opcode)
{
   FPA11 *fpa11 = GET_FPA11();
   unsigned int Fd, nType, nDest, nRc = 1;

   nDest = getDestinationSize(opcode);
   if (typeNone == nDest) return 0;

   SetRoundingMode(opcode);

   if (MONADIC_INSTRUCTION(opcode))
     nType = nDest;
   else
     nType = fpa11->fType[getFn(opcode)];

   if (!CONSTANT_FM(opcode))
   {
     register unsigned int Fm = getFm(opcode);
     if (nType < fpa11->fType[Fm])
     {
        nType = fpa11->fType[Fm];
     }
   }

   switch (nType)
   {
      case typeSingle   : nRc = SingleCPDO(opcode);   break;
      case typeDouble   : nRc = DoubleCPDO(opcode);   break;
      case typeExtended : nRc = ExtendedCPDO(opcode); break;
      default           : nRc = 0;
   }

   Fd = getFd(opcode);
   nType = fpa11->fType[Fd];
   if ((0 != nRc) && (nDest != nType))
   {
     switch (nDest)
     {
       case typeSingle:
       {
         if (typeDouble == nType)
           fpa11->fpreg[Fd].fSingle =
              float64_to_float32(fpa11->fpreg[Fd].fDouble, &fpa11->fp_status);
         else
           fpa11->fpreg[Fd].fSingle =
              floatx80_to_float32(fpa11->fpreg[Fd].fExtended, &fpa11->fp_status);
       }
       break;

       case typeDouble:
       {
         if (typeSingle == nType)
           fpa11->fpreg[Fd].fDouble =
              float32_to_float64(fpa11->fpreg[Fd].fSingle, &fpa11->fp_status);
         else
           fpa11->fpreg[Fd].fDouble =
              floatx80_to_float64(fpa11->fpreg[Fd].fExtended, &fpa11->fp_status);
       }
       break;

       case typeExtended:
       {
         if (typeSingle == nType)
           fpa11->fpreg[Fd].fExtended =
              float32_to_floatx80(fpa11->fpreg[Fd].fSingle, &fpa11->fp_status);
         else
           fpa11->fpreg[Fd].fExtended =
              float64_to_floatx80(fpa11->fpreg[Fd].fDouble, &fpa11->fp_status);
       }
       break;
     }

     fpa11->fType[Fd] = nDest;
   }

   return nRc;
}
