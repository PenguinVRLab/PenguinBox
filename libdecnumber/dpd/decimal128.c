/* Decimal 128-bit format module for the decNumber C Library.
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
/* Decimal 128-bit format module				      */
/* ------------------------------------------------------------------ */
/* This module comprises the routines for decimal128 format numbers.  */
/* Conversions are supplied to and from decNumber and String.	      */
/*								      */
/* This is used when decNumber provides operations, either for all    */
/* operations or as a proxy between decNumber and decSingle.	      */
/*								      */
/* Error handling is the same as decNumber (qv.).		      */
/* ------------------------------------------------------------------ */
#include "qemu/osdep.h"

#include "libdecnumber/dconfig.h"
#define	 DECNUMDIGITS 34
#include "libdecnumber/decNumber.h"
#include "libdecnumber/decNumberLocal.h"
#include "libdecnumber/dpd/decimal128.h"

extern const uInt   COMBEXP[32], COMBMSD[32];
extern const uByte  BIN2CHAR[4001];

extern void decDigitsFromDPD(decNumber *, const uInt *, Int);
extern void decDigitsToDPD(const decNumber *, uInt *, Int);

#if DECTRACE || DECCHECK
void decimal128Show(const decimal128 *);
extern void decNumberShow(const decNumber *);
#endif

#define DEC_clear(d) memset(d, 0, sizeof(*d))

decimal128 * decimal128FromNumber(decimal128 *d128, const decNumber *dn,
				  decContext *set) {
  uInt status=0;
  Int ae;
  decNumber  dw;
  decContext dc;
  uInt *pu;
  uInt comb, exp;
  uInt targar[4]={0,0,0,0};
  #define targhi targar[3]
  #define targmh targar[2]
  #define targml targar[1]
  #define targlo targar[0]

  ae=dn->exponent+dn->digits-1;
  if (dn->digits>DECIMAL128_Pmax
   || ae>DECIMAL128_Emax
   || ae<DECIMAL128_Emin) {
    decContextDefault(&dc, DEC_INIT_DECIMAL128);
    dc.round=set->round;
    decNumberPlus(&dw, dn, &dc);
    dw.bits|=dn->bits&DECNEG;
    status=dc.status;
    dn=&dw;
    }

  if (dn->bits&DECSPECIAL) {
    if (dn->bits&DECINF) targhi=DECIMAL_Inf<<24;
     else {
      if ((*dn->lsu!=0 || dn->digits>1)
       && (dn->digits<DECIMAL128_Pmax)) {
	decDigitsToDPD(dn, targar, 0);
	}
      if (dn->bits&DECNAN) targhi|=DECIMAL_NaN<<24;
       else targhi|=DECIMAL_sNaN<<24;
      }
    }

   else {
    if (decNumberIsZero(dn)) {
      if (dn->exponent<-DECIMAL128_Bias) {
	exp=0;
	status|=DEC_Clamped;
	}
       else {
	exp=dn->exponent+DECIMAL128_Bias;
	if (exp>DECIMAL128_Ehigh) {
	  exp=DECIMAL128_Ehigh;
	  status|=DEC_Clamped;
	  }
	}
      comb=(exp>>9) & 0x18;
      }
     else {
      uInt msd;
      Int pad=0;

      exp=(uInt)(dn->exponent+DECIMAL128_Bias);
      if (exp>DECIMAL128_Ehigh) {
	pad=exp-DECIMAL128_Ehigh;
	exp=DECIMAL128_Ehigh;
	status|=DEC_Clamped;
	}

      decDigitsToDPD(dn, targar, pad);
      msd=targhi>>14;
      targhi&=0x00003fff;

      if (msd>=8) comb=0x18 | ((exp>>11) & 0x06) | (msd & 0x01);
	     else comb=((exp>>9) & 0x18) | msd;
      }
    targhi|=comb<<26;
    targhi|=(exp&0xfff)<<14;
    }

  if (dn->bits&DECNEG) targhi|=0x80000000;

  pu=(uInt *)d128->bytes;
  if (DECLITEND) {
    pu[0]=targlo;
    pu[1]=targml;
    pu[2]=targmh;
    pu[3]=targhi;
    }
   else {
    pu[0]=targhi;
    pu[1]=targmh;
    pu[2]=targml;
    pu[3]=targlo;
    }

  if (status!=0) decContextSetStatus(set, status);
  return d128;
  }

decNumber * decimal128ToNumber(const decimal128 *d128, decNumber *dn) {
  uInt msd;
  uInt exp;
  uInt comb;
  const uInt *pu;
  Int  need;
  uInt sourar[4];
  #define sourhi sourar[3]
  #define sourmh sourar[2]
  #define sourml sourar[1]
  #define sourlo sourar[0]

  pu=(const uInt *)d128->bytes;
  if (DECLITEND) {
    sourlo=pu[0];
    sourml=pu[1];
    sourmh=pu[2];
    sourhi=pu[3];
    }
   else {
    sourhi=pu[0];
    sourmh=pu[1];
    sourml=pu[2];
    sourlo=pu[3];
    }

  comb=(sourhi>>26)&0x1f;

  decNumberZero(dn);
  if (sourhi&0x80000000) dn->bits=DECNEG;

  msd=COMBMSD[comb];
  exp=COMBEXP[comb];

  if (exp==3) {
    if (msd==0) {
      dn->bits|=DECINF;
      return dn;
      }
    else if (sourhi&0x02000000) dn->bits|=DECSNAN;
    else dn->bits|=DECNAN;
    msd=0;
    }
   else {
    dn->exponent=(exp<<12)+((sourhi>>14)&0xfff)-DECIMAL128_Bias;
    }

  sourhi&=0x00003fff;
  if (msd) {
    sourhi|=msd<<14;
    need=12;
    }
   else {
    if (sourhi) need=11;
     else if (sourmh) need=10;
     else if (sourml) need=7;
     else if (sourlo) need=4;
     else return dn;
    }

  decDigitsFromDPD(dn, sourar, need);
  return dn;
  }

char * decimal128ToEngString(const decimal128 *d128, char *string){
  decNumber dn;
  decimal128ToNumber(d128, &dn);
  decNumberToEngString(&dn, string);
  return string;
  }

char * decimal128ToString(const decimal128 *d128, char *string){
  uInt msd;
  Int  exp;
  uInt comb;
  char *cstart;
  char *c;
  const uInt *pu;
  char *s, *t;
  Int  dpd;
  Int  pre, e;
  const uByte *u;

  uInt sourar[4];
  #define sourhi sourar[3]
  #define sourmh sourar[2]
  #define sourml sourar[1]
  #define sourlo sourar[0]

  pu=(const uInt *)d128->bytes;
  if (DECLITEND) {
    sourlo=pu[0];
    sourml=pu[1];
    sourmh=pu[2];
    sourhi=pu[3];
    }
   else {
    sourhi=pu[0];
    sourmh=pu[1];
    sourml=pu[2];
    sourlo=pu[3];
    }

  c=string;
  if (((Int)sourhi)<0) *c++='-';

  comb=(sourhi>>26)&0x1f;
  msd=COMBMSD[comb];
  exp=COMBEXP[comb];

  if (exp==3) {
    if (msd==0) {
      strcpy(c,	  "Inf");
      strcpy(c+3, "inity");
      return string;
      }
    if (sourhi&0x02000000) *c++='s';
    strcpy(c, "NaN");
    c+=3;
    if (sourlo==0 && sourml==0 && sourmh==0
     && (sourhi&0x0003ffff)==0) return string;
    exp=0; msd=0;
    }
   else exp=(exp<<12)+((sourhi>>14)&0xfff)-DECIMAL128_Bias;

  cstart=c;
  if (msd) *c++='0'+(char)msd;

  #define dpd2char u=&BIN2CHAR[DPD2BIN[dpd]*4];			  \
		   if (c!=cstart) {memcpy(c, u+1, 4); c+=3;}	  \
		    else if (*u)  {memcpy(c, u+4-*u, 4); c+=*u;}
  dpd=(sourhi>>4)&0x3ff;
  dpd2char;
  dpd=((sourhi&0xf)<<6) | (sourmh>>26);
  dpd2char;
  dpd=(sourmh>>16)&0x3ff;
  dpd2char;
  dpd=(sourmh>>6)&0x3ff;
  dpd2char;
  dpd=((sourmh&0x3f)<<4) | (sourml>>28);
  dpd2char;
  dpd=(sourml>>18)&0x3ff;
  dpd2char;
  dpd=(sourml>>8)&0x3ff;
  dpd2char;
  dpd=((sourml&0xff)<<2) | (sourlo>>30);
  dpd2char;
  dpd=(sourlo>>20)&0x3ff;
  dpd2char;
  dpd=(sourlo>>10)&0x3ff;
  dpd2char;
  dpd=(sourlo)&0x3ff;
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
      if (e<1000) {
	u=&BIN2CHAR[e*4];
	memcpy(c, u+4-*u, 4);
	c+=*u;
	}
       else {
	Int thou=((e>>3)*1049)>>17;
	Int rem=e-(1000*thou);
	*c++='0'+(char)thou;
	u=&BIN2CHAR[rem*4];
	memcpy(c, u+1, 4);
	c+=3;
	}
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

decimal128 * decimal128FromString(decimal128 *result, const char *string,
				  decContext *set) {
  decContext dc;
  decNumber dn;

  decContextDefault(&dc, DEC_INIT_DECIMAL128);
  dc.round=set->round;

  decNumberFromString(&dn, string, &dc);
  decimal128FromNumber(result, &dn, &dc);
  if (dc.status!=0) {
    decContextSetStatus(set, dc.status);
    }
  return result;
  }

uint32_t decimal128IsCanonical(const decimal128 *d128) {
  decNumber dn;
  decimal128 canon;
  decContext dc;
  decContextDefault(&dc, DEC_INIT_DECIMAL128);
  decimal128ToNumber(d128, &dn);
  decimal128FromNumber(&canon, &dn, &dc);
  return memcmp(d128, &canon, DECIMAL128_Bytes)==0;
  }

decimal128 * decimal128Canonical(decimal128 *result, const decimal128 *d128) {
  decNumber dn;
  decContext dc;
  decContextDefault(&dc, DEC_INIT_DECIMAL128);
  decimal128ToNumber(d128, &dn);
  decimal128FromNumber(result, &dn, &dc);
  return result;
  }

#if DECTRACE || DECCHECK
#define decimal128Sign(d)	((unsigned)(d)->bytes[0]>>7)

#define decimal128Comb(d)	(((d)->bytes[0] & 0x7c)>>2)

#define decimal128ExpCon(d)	((((d)->bytes[0] & 0x03)<<10)	      \
			      | ((unsigned)(d)->bytes[1]<<2)	      \
			      | ((unsigned)(d)->bytes[2]>>6))

#define decimal128SetSign(d, b) {				      \
  (d)->bytes[0]|=((unsigned)(b)<<7);}

#define decimal128SetExpCon(d, e) {				      \
  (d)->bytes[0]|=(uint8_t)((e)>>10);				      \
  (d)->bytes[1] =(uint8_t)(((e)&0x3fc)>>2);			      \
  (d)->bytes[2]|=(uint8_t)(((e)&0x03)<<6);}

void decimal128Show(const decimal128 *d128) {
  char buf[DECIMAL128_Bytes*2+1];
  Int i, j=0;

  if (DECLITEND) {
    for (i=0; i<DECIMAL128_Bytes; i++, j+=2) {
      sprintf(&buf[j], "%02x", d128->bytes[15-i]);
      }
    printf(" D128> %s [S:%d Cb:%02x Ec:%02x] LittleEndian\n", buf,
	   d128->bytes[15]>>7, (d128->bytes[15]>>2)&0x1f,
	   ((d128->bytes[15]&0x3)<<10)|(d128->bytes[14]<<2)|
	   (d128->bytes[13]>>6));
    }
   else {
    for (i=0; i<DECIMAL128_Bytes; i++, j+=2) {
      sprintf(&buf[j], "%02x", d128->bytes[i]);
      }
    printf(" D128> %s [S:%d Cb:%02x Ec:%02x] BigEndian\n", buf,
	   decimal128Sign(d128), decimal128Comb(d128),
	   decimal128ExpCon(d128));
    }
  }
#endif
