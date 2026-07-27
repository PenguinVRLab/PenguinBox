/* Decimal 64-bit format module for the decNumber C Library.
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
/* Decimal 64-bit format module					      */
/* ------------------------------------------------------------------ */
/* This module comprises the routines for decimal64 format numbers.   */
/* Conversions are supplied to and from decNumber and String.	      */
/*								      */
/* This is used when decNumber provides operations, either for all    */
/* operations or as a proxy between decNumber and decSingle.	      */
/*								      */
/* Error handling is the same as decNumber (qv.).		      */
/* ------------------------------------------------------------------ */
#include "qemu/osdep.h"

#include "libdecnumber/dconfig.h"
#define	 DECNUMDIGITS 16
#include "libdecnumber/decNumber.h"
#include "libdecnumber/decNumberLocal.h"
#include "libdecnumber/dpd/decimal64.h"

extern const uInt COMBEXP[32], COMBMSD[32];
extern const uByte  BIN2CHAR[4001];

extern void decDigitsFromDPD(decNumber *, const uInt *, Int);
extern void decDigitsToDPD(const decNumber *, uInt *, Int);

#if DECTRACE || DECCHECK
void decimal64Show(const decimal64 *);
extern void decNumberShow(const decNumber *);
#endif

#define DEC_clear(d) memset(d, 0, sizeof(*d))

#define DEC_BIN2CHAR 1
#define DEC_DPD2BIN  1
#define DEC_BIN2DPD  1
#include "libdecnumber/decDPD.h"

decimal64 * decimal64FromNumber(decimal64 *d64, const decNumber *dn,
				decContext *set) {
  uInt status=0;
  Int ae;
  decNumber  dw;
  decContext dc;
  uInt *pu;
  uInt comb, exp;
  uInt targar[2]={0, 0};
  #define targhi targar[1]
  #define targlo targar[0]

  ae=dn->exponent+dn->digits-1;
  if (dn->digits>DECIMAL64_Pmax
   || ae>DECIMAL64_Emax
   || ae<DECIMAL64_Emin) {
    decContextDefault(&dc, DEC_INIT_DECIMAL64);
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
       && (dn->digits<DECIMAL64_Pmax)) {
	decDigitsToDPD(dn, targar, 0);
	}
      if (dn->bits&DECNAN) targhi|=DECIMAL_NaN<<24;
       else targhi|=DECIMAL_sNaN<<24;
      }
    }

   else {
    if (decNumberIsZero(dn)) {
      if (dn->exponent<-DECIMAL64_Bias) {
	exp=0;
	status|=DEC_Clamped;
	}
       else {
	exp=dn->exponent+DECIMAL64_Bias;
	if (exp>DECIMAL64_Ehigh) {
	  exp=DECIMAL64_Ehigh;
	  status|=DEC_Clamped;
	  }
	}
      comb=(exp>>5) & 0x18;
      }
     else {
      uInt msd;
      Int pad=0;

      exp=(uInt)(dn->exponent+DECIMAL64_Bias);
      if (exp>DECIMAL64_Ehigh) {
	pad=exp-DECIMAL64_Ehigh;
	exp=DECIMAL64_Ehigh;
	status|=DEC_Clamped;
	}

      if (DECDPUN==3 && pad==0) {
	uInt dpd[6]={0,0,0,0,0,0};
	uInt i;
	Int d=dn->digits;
	for (i=0; d>0; i++, d-=3) dpd[i]=BIN2DPD[dn->lsu[i]];
	targlo =dpd[0];
	targlo|=dpd[1]<<10;
	targlo|=dpd[2]<<20;
	if (dn->digits>6) {
	  targlo|=dpd[3]<<30;
	  targhi =dpd[3]>>2;
	  targhi|=dpd[4]<<8;
	  }
	msd=dpd[5];
	}
       else {
	decDigitsToDPD(dn, targar, pad);
	msd=targhi>>18;
	targhi&=0x0003ffff;
	}

      if (msd>=8) comb=0x18 | ((exp>>7) & 0x06) | (msd & 0x01);
	     else comb=((exp>>5) & 0x18) | msd;
      }
    targhi|=comb<<26;
    targhi|=(exp&0xff)<<18;
    }

  if (dn->bits&DECNEG) targhi|=0x80000000;

  pu=(uInt *)d64->bytes;
  if (DECLITEND) {
    pu[0]=targar[0];
    pu[1]=targar[1];
    }
   else {
    pu[0]=targar[1];
    pu[1]=targar[0];
    }

  if (status!=0) decContextSetStatus(set, status);
  return d64;
  }

decNumber * decimal64ToNumber(const decimal64 *d64, decNumber *dn) {
  uInt msd;
  uInt exp;
  uInt comb;
  const uInt *pu;
  Int  need;
  uInt sourar[2];
  #define sourhi sourar[1]
  #define sourlo sourar[0]

  pu=(const uInt *)d64->bytes;
  if (DECLITEND) {
    sourlo=pu[0];
    sourhi=pu[1];
    }
   else {
    sourhi=pu[0];
    sourlo=pu[1];
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
    dn->exponent=(exp<<8)+((sourhi>>18)&0xff)-DECIMAL64_Bias;
    }

  sourhi&=0x0003ffff;
  if (msd) {
    sourhi|=msd<<18;
    need=6;
    }
   else {
    if (!sourhi) {
      if (!sourlo) return dn;
      need=3;
      if (sourlo&0xc0000000) need++;
      }
     else {
      need=4;
      if (sourhi&0x0003ff00) need++;
      }
    }

  decDigitsFromDPD(dn, sourar, need);
  return dn;
  }


char * decimal64ToEngString(const decimal64 *d64, char *string){
  decNumber dn;
  decimal64ToNumber(d64, &dn);
  decNumberToEngString(&dn, string);
  return string;
  }

char * decimal64ToString(const decimal64 *d64, char *string){
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

  uInt sourar[2];
  #define sourhi sourar[1]
  #define sourlo sourar[0]

  pu=(const uInt *)d64->bytes;
  if (DECLITEND) {
    sourlo=pu[0];
    sourhi=pu[1];
    }
   else {
    sourhi=pu[0];
    sourlo=pu[1];
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
    if (sourlo==0 && (sourhi&0x0003ffff)==0) return string;
    exp=0; msd=0;
    }
   else exp=(exp<<8)+((sourhi>>18)&0xff)-DECIMAL64_Bias;

  cstart=c;
  if (msd) *c++='0'+(char)msd;

  #define dpd2char u=&BIN2CHAR[DPD2BIN[dpd]*4];			  \
		   if (c!=cstart) {memcpy(c, u+1, 4); c+=3;}	  \
		    else if (*u)  {memcpy(c, u+4-*u, 4); c+=*u;}

  dpd=(sourhi>>8)&0x3ff;
  dpd2char;
  dpd=((sourhi&0xff)<<2) | (sourlo>>30);
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

decimal64 * decimal64FromString(decimal64 *result, const char *string,
				decContext *set) {
  decContext dc;
  decNumber dn;

  decContextDefault(&dc, DEC_INIT_DECIMAL64);
  dc.round=set->round;

  decNumberFromString(&dn, string, &dc);

  decimal64FromNumber(result, &dn, &dc);
  if (dc.status!=0) {
    decContextSetStatus(set, dc.status);
    }
  return result;
  }

uint32_t decimal64IsCanonical(const decimal64 *d64) {
  decNumber dn;
  decimal64 canon;
  decContext dc;
  decContextDefault(&dc, DEC_INIT_DECIMAL64);
  decimal64ToNumber(d64, &dn);
  decimal64FromNumber(&canon, &dn, &dc);
  return memcmp(d64, &canon, DECIMAL64_Bytes)==0;
  }

decimal64 * decimal64Canonical(decimal64 *result, const decimal64 *d64) {
  decNumber dn;
  decContext dc;
  decContextDefault(&dc, DEC_INIT_DECIMAL64);
  decimal64ToNumber(d64, &dn);
  decimal64FromNumber(result, &dn, &dc);
  return result;
  }

#if DECTRACE || DECCHECK
#define decimal64Sign(d)       ((unsigned)(d)->bytes[0]>>7)

#define decimal64Comb(d)       (((d)->bytes[0] & 0x7c)>>2)

#define decimal64ExpCon(d)     ((((d)->bytes[0] & 0x03)<<6)	      \
			     | ((unsigned)(d)->bytes[1]>>2))

#define decimal64SetSign(d, b) {				      \
  (d)->bytes[0]|=((unsigned)(b)<<7);}

#define decimal64SetExpCon(d, e) {				      \
  (d)->bytes[0]|=(uint8_t)((e)>>6);				      \
  (d)->bytes[1]|=(uint8_t)(((e)&0x3F)<<2);}

void decimal64Show(const decimal64 *d64) {
  char buf[DECIMAL64_Bytes*2+1];
  Int i, j=0;

  if (DECLITEND) {
    for (i=0; i<DECIMAL64_Bytes; i++, j+=2) {
      sprintf(&buf[j], "%02x", d64->bytes[7-i]);
      }
    printf(" D64> %s [S:%d Cb:%02x Ec:%02x] LittleEndian\n", buf,
	   d64->bytes[7]>>7, (d64->bytes[7]>>2)&0x1f,
	   ((d64->bytes[7]&0x3)<<6)| (d64->bytes[6]>>2));
    }
   else {
    for (i=0; i<DECIMAL64_Bytes; i++, j+=2) {
      sprintf(&buf[j], "%02x", d64->bytes[i]);
      }
    printf(" D64> %s [S:%d Cb:%02x Ec:%02x] BigEndian\n", buf,
	   decimal64Sign(d64), decimal64Comb(d64), decimal64ExpCon(d64));
    }
  }
#endif

#if DECDPUN==3
  #define DEC_DPD2BIN 1
#else
  #define DEC_DPD2BCD 1
#endif
#include "libdecnumber/decDPD.h"

#define DECMAX754   34
#define DECMAXUNITS ((DECMAX754+DECDPUN-1)/DECDPUN)

const uInt COMBEXP[32]={0, 0, 0, 0, 0, 0, 0, 0,
			1, 1, 1, 1, 1, 1, 1, 1,
			2, 2, 2, 2, 2, 2, 2, 2,
			0, 0, 1, 1, 2, 2, 3, 3};
const uInt COMBMSD[32]={0, 1, 2, 3, 4, 5, 6, 7,
			0, 1, 2, 3, 4, 5, 6, 7,
			0, 1, 2, 3, 4, 5, 6, 7,
			8, 9, 8, 9, 8, 9, 0, 1};

#if DECDPUN<=4
static const uInt multies[]={131073, 26215, 5243, 1049, 210};
#define QUOT10(u, n) ((((uInt)(u)>>(n))*multies[n])>>17)
#endif
void decDigitsToDPD(const decNumber *dn, uInt *targ, Int shift) {
  Int  cut;
  Int  digits=dn->digits;
  uInt dpd;
  uInt bin;
  uInt *uout=targ;
  uInt	uoff=0;
  const Unit *inu=dn->lsu;
  Unit	uar[DECMAXUNITS];
  #if DECDPUN!=3
    Unit in;
  #endif

  if (shift!=0) {
    const Unit *source;
    Unit  *target, *first;
    uInt  next=0;

    source=dn->lsu+D2U(digits)-1;
    target=uar+D2U(digits)-1+D2U(shift);
    cut=DECDPUN-MSUDIGITS(shift);
    if (cut==0) {
      for (; source>=dn->lsu; source--, target--) *target=*source;
      }
     else {
      first=uar+D2U(digits+shift)-1;
      for (; source>=dn->lsu; source--, target--) {
	#if DECDPUN<=4
	  uInt quot=QUOT10(*source, cut);
	  uInt rem=*source-quot*DECPOWERS[cut];
	  next+=quot;
	#else
	  uInt rem=*source%DECPOWERS[cut];
	  next+=*source/DECPOWERS[cut];
	#endif
	if (target<=first) *target=(Unit)next;
	next=rem*DECPOWERS[DECDPUN-cut];
	}
      }
    for (; target>=uar; target--) {
      *target=(Unit)next;
      next=0;
      }
    digits+=shift;
    inu=uar;
    }

  #if DECDPUN!=3
    in=*inu;
    cut=0;
    bin=0;
  #endif

  while (digits > 0) {
    #if DECDPUN==3
      bin=*inu;
      digits-=3;
      inu++;

    #else
      Unit dig;
      Int j;
      for (j=0; j<3; j++) {
	#if DECDPUN<=4
	  Unit temp=(Unit)((uInt)(in*6554)>>16);
	  dig=(Unit)(in-X10(temp));
	  in=temp;
	#else
	  dig=in%10;
	  in=in/10;
	#endif
	if (j==0) bin=dig;
	 else if (j==1)	 bin+=X10(dig);
	 else bin+=X100(dig);
	digits--;
	if (digits==0) break;
	cut++;
	if (cut==DECDPUN) {inu++; in=*inu; cut=0;}
	}
    #endif

    dpd=BIN2DPD[bin];

    *uout|=dpd<<uoff;
    uoff+=10;
    if (uoff<32) continue;
    uout++;
    uoff-=32;
    *uout|=dpd>>(10-uoff);
    }
  return;
  }

void decDigitsFromDPD(decNumber *dn, const uInt *sour, Int declets) {

  uInt	dpd;
  Int	n;
  Unit	*uout=dn->lsu;
  Unit	*last=uout;
  const uInt *uin=sour;
  uInt	uoff=0;

  #if DECDPUN!=3
  uInt	bcd;
  uInt	nibble;
  Unit	out=0;
  Int	cut=0;
  #endif
  #if DECDPUN>4
  uInt const *pow;
  #endif

  for (n=declets-1; n>=0; n--) {
    dpd=*uin>>uoff;
    uoff+=10;
    if (uoff>32) {
      uin++;
      uoff-=32;
      dpd|=*uin<<(10-uoff);
      }
    dpd&=0x3ff;

  #if DECDPUN==3
    if (dpd==0) *uout=0;
     else {
      *uout=DPD2BIN[dpd];
      last=uout;
      }
    uout++;
    }

  #else
    if (dpd==0) {
      cut++;
      if (cut==DECDPUN) {*uout=out; if (out) {last=uout; out=0;} uout++; cut=0;}
      if (n==0) break;
      cut++;
      if (cut==DECDPUN) {*uout=out; if (out) {last=uout; out=0;} uout++; cut=0;}
      cut++;
      if (cut==DECDPUN) {*uout=out; if (out) {last=uout; out=0;} uout++; cut=0;}
      continue;
      }

    bcd=DPD2BCD[dpd];

    nibble=bcd & 0x00f;
    if (nibble) out=(Unit)(out+nibble*DECPOWERS[cut]);
    cut++;
    if (cut==DECDPUN) {*uout=out; if (out) {last=uout; out=0;} uout++; cut=0;}
    bcd>>=4;

    if (n==0 && !bcd) break;

    nibble=bcd & 0x00f;
    if (nibble) out=(Unit)(out+nibble*DECPOWERS[cut]);
    cut++;
    if (cut==DECDPUN) {*uout=out; if (out) {last=uout; out=0;} uout++; cut=0;}
    bcd>>=4;

    nibble=bcd & 0x00f;
    if (nibble) out=(Unit)(out+nibble*DECPOWERS[cut]);
    cut++;
    if (cut==DECDPUN) {*uout=out; if (out) {last=uout; out=0;} uout++; cut=0;}
    }
  if (cut!=0) {
    *uout=out;
    if (out) last=uout;
    }
  #endif

  dn->digits=(last-dn->lsu)*DECDPUN+1;
  #if DECDPUN>1
  if (*last<10) return;
  dn->digits++;
  #if DECDPUN>2
  if (*last<100) return;
  dn->digits++;
  #if DECDPUN>3
  if (*last<1000) return;
  dn->digits++;
  #if DECDPUN>4
  for (pow=&DECPOWERS[4]; *last>=*pow; pow++) dn->digits++;
  #endif
  #endif
  #endif
  #endif
  return;
  }
