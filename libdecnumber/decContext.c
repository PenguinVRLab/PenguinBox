/* Decimal context module for the decNumber C Library.
   Copyright (C) 2005, 2007 Free Software Foundation, Inc.
   Contributed by IBM Corporation.  Author Mike Cowlishaw.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 2, or (at your option) any later
   version.

   In addition to the permissions in the GNU General Public License,
   the Free Software Foundation gives you unlimited permission to link
   the compiled version of this file into combinations with other
   programs, and to distribute those combinations without any
   restriction coming from the use of this file.  (The General Public
   License restrictions do apply in other respects; for example, they
   cover modification of the file, and distribution when not linked
   into a combine executable.)

   GCC is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
   for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, see
   <https://www.gnu.org/licenses/>.  */

/* ------------------------------------------------------------------ */
/* Decimal Context module					      */
/* ------------------------------------------------------------------ */
/* This module comprises the routines for handling arithmetic	      */
/* context structures.						      */
/* ------------------------------------------------------------------ */

#include "qemu/osdep.h"
#include "libdecnumber/dconfig.h"
#include "libdecnumber/decContext.h"
#include "libdecnumber/decNumberLocal.h"

#if DECCHECK
static	const  Int mfcone=1;
static	const  Flag *mfctop=(Flag *)&mfcone;
#define LITEND *mfctop
#endif

const uByte DECSTICKYTAB[10]={1,1,2,3,4,6,6,7,8,9};

const uLong DECPOWERS[20] = {1, 10, 100, 1000, 10000, 100000, 1000000,
  10000000, 100000000, 1000000000, 10000000000ULL, 100000000000ULL,
  1000000000000ULL, 10000000000000ULL, 100000000000000ULL, 1000000000000000ULL,
  10000000000000000ULL, 100000000000000000ULL, 1000000000000000000ULL,
  10000000000000000000ULL,};

decContext *decContextClearStatus(decContext *context, uInt mask) {
  context->status&=~mask;
  return context;
  }

decContext * decContextDefault(decContext *context, Int kind) {
  context->digits=9;
  context->emax=DEC_MAX_EMAX;
  context->emin=DEC_MIN_EMIN;
  context->round=DEC_ROUND_HALF_UP;
  context->traps=DEC_Errors;
  context->status=0;
  context->clamp=0;
  #if DECSUBSET
  context->extended=0;
  #endif
  switch (kind) {
    case DEC_INIT_BASE:
      break;
    case DEC_INIT_DECIMAL32:
      context->digits=7;
      context->emax=96;
      context->emin=-95;
      context->round=DEC_ROUND_HALF_EVEN;
      context->traps=0;
      context->clamp=1;
      #if DECSUBSET
      context->extended=1;
      #endif
      break;
    case DEC_INIT_DECIMAL64:
      context->digits=16;
      context->emax=384;
      context->emin=-383;
      context->round=DEC_ROUND_HALF_EVEN;
      context->traps=0;
      context->clamp=1;
      #if DECSUBSET
      context->extended=1;
      #endif
      break;
    case DEC_INIT_DECIMAL128:
      context->digits=34;
      context->emax=6144;
      context->emin=-6143;
      context->round=DEC_ROUND_HALF_EVEN;
      context->traps=0;
      context->clamp=1;
      #if DECSUBSET
      context->extended=1;
      #endif
      break;

    default:
      decContextSetStatus(context, DEC_Invalid_operation);
    }

  #if DECCHECK
  if (LITEND!=DECLITEND) {
    const char *adj;
    if (LITEND) adj="little";
	   else adj="big";
    printf("Warning: DECLITEND is set to %d, but this computer appears to be %s-endian\n",
	   DECLITEND, adj);
    }
  #endif
  return context;}

enum rounding decContextGetRounding(decContext *context) {
  return context->round;
  }

uInt decContextGetStatus(decContext *context) {
  return context->status;
  }

decContext *decContextRestoreStatus(decContext *context,
				    uInt newstatus, uInt mask) {
  context->status&=~mask;
  context->status|=(mask&newstatus);
  return context;
  }

uInt decContextSaveStatus(decContext *context, uInt mask) {
  return context->status&mask;
  }

decContext *decContextSetRounding(decContext *context,
				  enum rounding newround) {
  context->round=newround;
  return context;
  }

decContext * decContextSetStatus(decContext *context, uInt status) {
  context->status|=status;
  if (status & context->traps) raise(SIGFPE);
  return context;}

decContext * decContextSetStatusFromString(decContext *context,
					   const char *string) {
  if (strcmp(string, DEC_Condition_CS)==0)
    return decContextSetStatus(context, DEC_Conversion_syntax);
  if (strcmp(string, DEC_Condition_DZ)==0)
    return decContextSetStatus(context, DEC_Division_by_zero);
  if (strcmp(string, DEC_Condition_DI)==0)
    return decContextSetStatus(context, DEC_Division_impossible);
  if (strcmp(string, DEC_Condition_DU)==0)
    return decContextSetStatus(context, DEC_Division_undefined);
  if (strcmp(string, DEC_Condition_IE)==0)
    return decContextSetStatus(context, DEC_Inexact);
  if (strcmp(string, DEC_Condition_IS)==0)
    return decContextSetStatus(context, DEC_Insufficient_storage);
  if (strcmp(string, DEC_Condition_IC)==0)
    return decContextSetStatus(context, DEC_Invalid_context);
  if (strcmp(string, DEC_Condition_IO)==0)
    return decContextSetStatus(context, DEC_Invalid_operation);
  #if DECSUBSET
  if (strcmp(string, DEC_Condition_LD)==0)
    return decContextSetStatus(context, DEC_Lost_digits);
  #endif
  if (strcmp(string, DEC_Condition_OV)==0)
    return decContextSetStatus(context, DEC_Overflow);
  if (strcmp(string, DEC_Condition_PA)==0)
    return decContextSetStatus(context, DEC_Clamped);
  if (strcmp(string, DEC_Condition_RO)==0)
    return decContextSetStatus(context, DEC_Rounded);
  if (strcmp(string, DEC_Condition_SU)==0)
    return decContextSetStatus(context, DEC_Subnormal);
  if (strcmp(string, DEC_Condition_UN)==0)
    return decContextSetStatus(context, DEC_Underflow);
  if (strcmp(string, DEC_Condition_ZE)==0)
    return context;
  return NULL;
  }

decContext * decContextSetStatusFromStringQuiet(decContext *context,
						const char *string) {
  if (strcmp(string, DEC_Condition_CS)==0)
    return decContextSetStatusQuiet(context, DEC_Conversion_syntax);
  if (strcmp(string, DEC_Condition_DZ)==0)
    return decContextSetStatusQuiet(context, DEC_Division_by_zero);
  if (strcmp(string, DEC_Condition_DI)==0)
    return decContextSetStatusQuiet(context, DEC_Division_impossible);
  if (strcmp(string, DEC_Condition_DU)==0)
    return decContextSetStatusQuiet(context, DEC_Division_undefined);
  if (strcmp(string, DEC_Condition_IE)==0)
    return decContextSetStatusQuiet(context, DEC_Inexact);
  if (strcmp(string, DEC_Condition_IS)==0)
    return decContextSetStatusQuiet(context, DEC_Insufficient_storage);
  if (strcmp(string, DEC_Condition_IC)==0)
    return decContextSetStatusQuiet(context, DEC_Invalid_context);
  if (strcmp(string, DEC_Condition_IO)==0)
    return decContextSetStatusQuiet(context, DEC_Invalid_operation);
  #if DECSUBSET
  if (strcmp(string, DEC_Condition_LD)==0)
    return decContextSetStatusQuiet(context, DEC_Lost_digits);
  #endif
  if (strcmp(string, DEC_Condition_OV)==0)
    return decContextSetStatusQuiet(context, DEC_Overflow);
  if (strcmp(string, DEC_Condition_PA)==0)
    return decContextSetStatusQuiet(context, DEC_Clamped);
  if (strcmp(string, DEC_Condition_RO)==0)
    return decContextSetStatusQuiet(context, DEC_Rounded);
  if (strcmp(string, DEC_Condition_SU)==0)
    return decContextSetStatusQuiet(context, DEC_Subnormal);
  if (strcmp(string, DEC_Condition_UN)==0)
    return decContextSetStatusQuiet(context, DEC_Underflow);
  if (strcmp(string, DEC_Condition_ZE)==0)
    return context;
  return NULL;
  }

decContext * decContextSetStatusQuiet(decContext *context, uInt status) {
  context->status|=status;
  return context;}

const char *decContextStatusToString(const decContext *context) {
  Int status=context->status;

  if (status==DEC_Invalid_operation    ) return DEC_Condition_IO;
  if (status==DEC_Division_by_zero     ) return DEC_Condition_DZ;
  if (status==DEC_Overflow	       ) return DEC_Condition_OV;
  if (status==DEC_Underflow	       ) return DEC_Condition_UN;
  if (status==DEC_Inexact	       ) return DEC_Condition_IE;

  if (status==DEC_Division_impossible  ) return DEC_Condition_DI;
  if (status==DEC_Division_undefined   ) return DEC_Condition_DU;
  if (status==DEC_Rounded	       ) return DEC_Condition_RO;
  if (status==DEC_Clamped	       ) return DEC_Condition_PA;
  if (status==DEC_Subnormal	       ) return DEC_Condition_SU;
  if (status==DEC_Conversion_syntax    ) return DEC_Condition_CS;
  if (status==DEC_Insufficient_storage ) return DEC_Condition_IS;
  if (status==DEC_Invalid_context      ) return DEC_Condition_IC;
  #if DECSUBSET
  if (status==DEC_Lost_digits	       ) return DEC_Condition_LD;
  #endif
  if (status==0			       ) return DEC_Condition_ZE;
  return DEC_Condition_MU;
  }

uInt decContextTestSavedStatus(uInt oldstatus, uInt mask) {
  return (oldstatus&mask)!=0;
  }

uInt decContextTestStatus(decContext *context, uInt mask) {
  return (context->status&mask)!=0;
  }

decContext *decContextZeroStatus(decContext *context) {
  context->status=0;
  return context;
  }
