/* Decimal 64-bit format module header for the decNumber C Library.
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
/* Decimal 64-bit format module header				      */
/* ------------------------------------------------------------------ */

#ifndef DECIMAL64_H
#define DECIMAL64_H

  #define DEC64NAME	"decimal64"
  #define DEC64FULLNAME "Decimal 64-bit Number"
  #define DEC64AUTHOR	"Mike Cowlishaw"


  #define DECIMAL64_Bytes  8
  #define DECIMAL64_Pmax   16
  #define DECIMAL64_Emax   384
  #define DECIMAL64_Emin  -383
  #define DECIMAL64_Bias   398
  #define DECIMAL64_String 24
  #define DECIMAL64_EconL  8
  #define DECIMAL64_Ehigh  (DECIMAL64_Emax+DECIMAL64_Bias-DECIMAL64_Pmax+1)

  #if defined(DECNUMDIGITS)
    #if (DECNUMDIGITS<DECIMAL64_Pmax)
      #error decimal64.h needs pre-defined DECNUMDIGITS>=16 for safe use
    #endif
  #endif


  #ifndef DECNUMDIGITS
    #define DECNUMDIGITS DECIMAL64_Pmax
  #endif
  #include "libdecnumber/decNumber.h"

  typedef struct {
    uint8_t bytes[DECIMAL64_Bytes];
    } decimal64;

  #if !defined(DECIMAL_NaN)
    #define DECIMAL_NaN	    0x7c
    #define DECIMAL_sNaN    0x7e
    #define DECIMAL_Inf	    0x78
  #endif

  decimal64 * decimal64FromString(decimal64 *, const char *, decContext *);
  char * decimal64ToString(const decimal64 *, char *);
  char * decimal64ToEngString(const decimal64 *, char *);

  decimal64 * decimal64FromNumber(decimal64 *, const decNumber *,
				  decContext *);
  decNumber * decimal64ToNumber(const decimal64 *, decNumber *);

  uint32_t    decimal64IsCanonical(const decimal64 *);
  decimal64 * decimal64Canonical(decimal64 *, const decimal64 *);

#endif
