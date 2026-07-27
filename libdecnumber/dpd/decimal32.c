/* Decimal 32-bit format module for the decNumber C Library.
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
/* Decimal 32-bit format module					      */
/* ------------------------------------------------------------------ */
/* This module comprises the routines for decimal32 format numbers.   */
/* Conversions are supplied to and from decNumber and String.	      */
/*								      */
/* This is used when decNumber provides operations, either for all    */
/* operations or as a proxy between decNumber and decSingle.	      */
/*								      */
/* Error handling is the same as decNumber (qv.).		      */
/* ------------------------------------------------------------------ */
#include "qemu/osdep.h"

#include "libdecnumber/dconfig.h"
#define	 DECNUMDIGITS  7
#include "libdecnumber/decNumber.h"
#include "libdecnumber/decNumberLocal.h"
#include "libdecnumber/dpd/decimal32.h"

extern const uInt   COMBEXP[32], COMBMSD[32];
extern const uByte  BIN2CHAR[4001];

extern void decDigitsToDPD(const decNumber *, uInt *, Int);
extern void decDigitsFromDPD(decNumber *, const uInt *, Int);

#if DECTRACE || DECCHECK
void decimal32Show(const decimal32 *);
extern void decNumberShow(const decNumber *);
#endif

#define DEC_clear(d) memset(d, 0, sizeof(*d))

decimal32 * decimal32FromNumber(decimal32 *d32, const decNumber *dn,
			      decContext *set) {
  uInt status=0;
  Int ae;
  decNumber  dw;
  decContext dc;
  uInt *pu;
  uInt comb, exp;
  uInt targ=0;

  ae=dn->exponent+dn->digits-1;
  if (dn->digits>DECIMAL32_Pmax
   || ae>DECIMAL32_Emax
   || ae<DECIMAL32_Emin) {
    decContextDefault(&dc, DEC_INIT_DECIMAL32);
    dc.round=set->round;
    decNumberPlus(&dw, dn, &dc);
    dw.bits|=dn->bits&DECNEG;
    status=dc.status;
    dn=&dw;
    }

  if (dn->bits&DECSPECIAL) {
    if (dn->bits&DECINF) targ=DECIMAL_Inf<<24;
     else {
      if ((*dn->lsu!=0 || dn->digits>1)
       && (dn->digits<DECIMAL32_Pmax)) {
	decDigitsToDPD(dn, &targ, 0);
	}
      if (dn->bits&DECNAN) targ|=DECIMAL_NaN<<24;
       else targ|=DECIMAL_sNaN<<24;
      }
    }

   else {
    if (decNumberIsZero(dn)) {
      if (dn->exponent<-DECIMAL32_Bias) {
	exp=0;
	status|=DEC_Clamped;
	}
       else {
	exp=dn->exponent+DECIMAL32_Bias;
	if (exp>DECIMAL32_Ehigh) {
	  exp=DECIMAL32_Ehigh;
	  status|=DEC_Clamped;
	  }
	}
      comb=(exp>>3) & 0x18;
      }
     else {
      uInt msd;
      Int pad=0;

      exp=(uInt)(dn->exponent+DECIMAL32_Bias);
      if (exp>DECIMAL32_Ehigh) {
	pad=exp-DECIMAL32_Ehigh;
	exp=DECIMAL32_Ehigh;
	status|=DEC_Clamped;
	}

      if (DECDPUN==3 && pad==0) {
	targ=BIN2DPD[dn->lsu[0]];
	if (dn->digits>3) targ|=(uInt)(BIN2DPD[dn->lsu[1]])<<10;
	msd=(dn->digits==7 ? dn->lsu[2] : 0);
	}
       else {
	decDigitsToDPD(dn, &targ, pad);
	msd=targ>>20;
	targ&=0x000fffff;
	}

      if (msd>=8) comb=0x18 | ((exp>>5) & 0x06) | (msd & 0x01);
	     else comb=((exp>>3) & 0x18) | msd;
      }
    targ|=comb<<26;
    targ|=(exp&0x3f)<<20;
    }

  if (dn->bits&DECNEG) targ|=0x80000000;

  pu=(uInt *)d32->bytes;
  *pu=targ;

  if (status!=0) decContextSetStatus(set, status);
  return d32;
  }

decNumber * decimal32ToNumber(const decimal32 *d32, decNumber *dn) {
  uInt msd;
  uInt exp;
  uInt comb;
  uInt sour;
  const uInt *pu;

  pu=(const uInt *)d32->bytes;
  sour=*pu;

  comb=(sour>>26)&0x1f;

  decNumberZero(dn);
  if (sour&0x80000000) dn->bits=DECNEG;

  msd=COMBMSD[comb];
  exp=COMBEXP[comb];

  if (exp==3) {
    if (msd==0) {
      dn->bits|=DECINF;
      return dn;
      }
    else if (sour&0x02000000) dn->bits|=DECSNAN;
    else dn->bits|=DECNAN;
    msd=0;
    }
   else {
    dn->exponent=(exp<<6)+((sour>>20)&0x3f)-DECIMAL32_Bias;
    }

  sour&=0x000fffff;
  if (msd) {
    sour|=msd<<20;
    decDigitsFromDPD(dn, &sour, 3);
    return dn;
    }
  if (!sour) return dn;
  if (sour&0x000ffc00)
    decDigitsFromDPD(dn, &sour, 2);
   else
    decDigitsFromDPD(dn, &sour, 1);
  return dn;
  }

char * decimal32ToEngString(const decimal32 *d32, char *string){
  decNumber dn;
  decimal32ToNumber(d32, &dn);
  decNumberToEngString(&dn, string);
  return string;
  }

char * decimal32ToString(const decimal32 *d32, char *string){
  uInt msd;
  Int  exp;
  uInt comb;
  char *cstart;
  char *c;
  const uInt *pu;
  const uByte *u;
  char *s, *t;
  Int  dpd;
  Int  pre, e;
  uInt sour;

  pu=(const uInt *)d32->bytes;
  sour=*pu;

  c=string;
  if (((Int)sour)<0) *c++='-';

  comb=(sour>>26)&0x1f;
  msd=COMBMSD[comb];
  exp=COMBEXP[comb];

  if (exp==3) {
    if (msd==0) {
      strcpy(c,	  "Inf");
      strcpy(c+3, "inity");
      return string;
      }
    if (sour&0x02000000) *c++='s';
    strcpy(c, "NaN");
    c+=3;
    if ((sour&0x000fffff)==0) return string;
    exp=0; msd=0;
    }
   else exp=(exp<<6)+((sour>>20)&0x3f)-DECIMAL32_Bias;

  cstart=c;
  if (msd) *c++='0'+(char)msd;

  #define dpd2char u=&BIN2CHAR[DPD2BIN[dpd]*4];			  \
		   if (c!=cstart) {memcpy(c, u+1, 4); c+=3;}	  \
		    else if (*u)  {memcpy(c, u+4-*u, 4); c+=*u;}

  dpd=(sour>>10)&0x3ff;
  dpd2char;
  dpd=(sour)&0x3ff;
  dpd2char;

  if (c==cstart) *c++='0';

  if (exp==0) {
    *c='\0';
    return string;
    }

  e=0;
  pre=c-cstart+exp;
  if (exp>0 || pre<-5) {
    e=pre-1;
    pre=1;
    }

  s=c-1;
  if (pre>0) {
    char *dotat=cstart+pre;
    if (dotat<c) {
      t=c;
      for (; s>=dotat; s--, t--) *t=*s;
      *t='.';
      c++;
      }

    if (e!=0) {
      *c++='E';
      *c++='+';
      if (e<0) {
	*(c-1)='-';
	e=-e;
	}
      u=&BIN2CHAR[e*4];
      memcpy(c, u+4-*u, 4);
      c+=*u;
      }
    *c='\0';
    return string;
    }

  t=c+1-pre;
  *(t+1)='\0';
  for (; s>=cstart; s--, t--) *t=*s;
  c=cstart;
  *c++='0';
  *c++='.';
  for (; pre<0; pre++) *c++='0';
  return string;
  }

decimal32 * decimal32FromString(decimal32 *result, const char *string,
				decContext *set) {
  decContext dc;
  decNumber dn;

  decContextDefault(&dc, DEC_INIT_DECIMAL32);
  dc.round=set->round;

  decNumberFromString(&dn, string, &dc);
  decimal32FromNumber(result, &dn, &dc);
  if (dc.status!=0) {
    decContextSetStatus(set, dc.status);
    }
  return result;
  }

uint32_t decimal32IsCanonical(const decimal32 *d32) {
  decNumber dn;
  decimal32 canon;
  decContext dc;
  decContextDefault(&dc, DEC_INIT_DECIMAL32);
  decimal32ToNumber(d32, &dn);
  decimal32FromNumber(&canon, &dn, &dc);
  return memcmp(d32, &canon, DECIMAL32_Bytes)==0;
  }

decimal32 * decimal32Canonical(decimal32 *result, const decimal32 *d32) {
  decNumber dn;
  decContext dc;
  decContextDefault(&dc, DEC_INIT_DECIMAL32);
  decimal32ToNumber(d32, &dn);
  decimal32FromNumber(result, &dn, &dc);
  return result;
  }

#if DECTRACE || DECCHECK
#define decimal32Sign(d)       ((unsigned)(d)->bytes[0]>>7)

#define decimal32Comb(d)       (((d)->bytes[0] & 0x7c)>>2)

#define decimal32ExpCon(d)     ((((d)->bytes[0] & 0x03)<<4)	      \
			     | ((unsigned)(d)->bytes[1]>>4))

#define decimal32SetSign(d, b) {				      \
  (d)->bytes[0]|=((unsigned)(b)<<7);}

#define decimal32SetExpCon(d, e) {				      \
  (d)->bytes[0]|=(uint8_t)((e)>>4);				      \
  (d)->bytes[1]|=(uint8_t)(((e)&0x0F)<<4);}

void decimal32Show(const decimal32 *d32) {
  char buf[DECIMAL32_Bytes*2+1];
  Int i, j=0;

  if (DECLITEND) {
    for (i=0; i<DECIMAL32_Bytes; i++, j+=2) {
      sprintf(&buf[j], "%02x", d32->bytes[3-i]);
      }
    printf(" D32> %s [S:%d Cb:%02x Ec:%02x] LittleEndian\n", buf,
	   d32->bytes[3]>>7, (d32->bytes[3]>>2)&0x1f,
	   ((d32->bytes[3]&0x3)<<4)| (d32->bytes[2]>>4));
    }
   else {
    for (i=0; i<DECIMAL32_Bytes; i++, j+=2) {
      sprintf(&buf[j], "%02x", d32->bytes[i]);
      }
    printf(" D32> %s [S:%d Cb:%02x Ec:%02x] BigEndian\n", buf,
	   decimal32Sign(d32), decimal32Comb(d32), decimal32ExpCon(d32));
    }
  }
#endif
