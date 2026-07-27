/*
**
** File: fmopl.c -- software implementation of FM sound generator
**
** Copyright (C) 1999,2000 Tatsuyuki Satoh , MultiArcadeMachineEmurator development
**
** Version 0.37a
**
*/

/*
	preliminary :
	Problem :
	note:
*/

/* This version of fmopl.c is a fork of the MAME one, relicensed under the LGPL.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include <math.h>
#include "fmopl.h"
#ifndef PI
#define PI 3.14159265358979323846
#endif

#ifdef OPL_OUTPUT_LOG
static FILE *opl_dbg_fp = NULL;
static FM_OPL *opl_dbg_opl[16];
static int opl_dbg_maxchip,opl_dbg_chip;
#endif

#define OPL_ARRATE     141280
#define OPL_DRRATE    1956000

#define DELTAT_MIXING_LEVEL (1)

#define FREQ_BITS 24

#define FREQ_RATE   (1<<(FREQ_BITS-20))
#define TL_BITS    (FREQ_BITS+2)

#define OPL_OUTSB   (TL_BITS+3-16)
#define OPL_MAXOUT (0x7fff<<OPL_OUTSB)
#define OPL_MINOUT (-0x8000<<OPL_OUTSB)

#define SIN_ENT 2048

#define ENV_BITS 16
#define EG_ENT   4096

#define EG_OFF   ((2*EG_ENT)<<ENV_BITS)
#define EG_DED   EG_OFF
#define EG_DST   (EG_ENT<<ENV_BITS)
#define EG_AED   EG_DST
#define EG_AST   0

#define EG_STEP (96.0/EG_ENT)

#define VIB_ENT 512
#define VIB_SHIFT (32-9)
#define AMS_ENT 512
#define AMS_SHIFT (32-9)

#define VIB_RATE 256

#define SLOT1 0
#define SLOT2 1

#define ENV_MOD_RR  0x00
#define ENV_MOD_DR  0x01
#define ENV_MOD_AR  0x02

static const int slot_array[32]=
{
	 0, 2, 4, 1, 3, 5,-1,-1,
	 6, 8,10, 7, 9,11,-1,-1,
	12,14,16,13,15,17,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1
};

#define DV (EG_STEP/2)
static const uint32_t KSL_TABLE[8*16]=
{
	 0.000/DV, 0.000/DV, 0.000/DV, 0.000/DV,
	 0.000/DV, 0.000/DV, 0.000/DV, 0.000/DV,
	 0.000/DV, 0.000/DV, 0.000/DV, 0.000/DV,
	 0.000/DV, 0.000/DV, 0.000/DV, 0.000/DV,
	 0.000/DV, 0.000/DV, 0.000/DV, 0.000/DV,
	 0.000/DV, 0.000/DV, 0.000/DV, 0.000/DV,
	 0.000/DV, 0.750/DV, 1.125/DV, 1.500/DV,
	 1.875/DV, 2.250/DV, 2.625/DV, 3.000/DV,
	 0.000/DV, 0.000/DV, 0.000/DV, 0.000/DV,
	 0.000/DV, 1.125/DV, 1.875/DV, 2.625/DV,
	 3.000/DV, 3.750/DV, 4.125/DV, 4.500/DV,
	 4.875/DV, 5.250/DV, 5.625/DV, 6.000/DV,
	 0.000/DV, 0.000/DV, 0.000/DV, 1.875/DV,
	 3.000/DV, 4.125/DV, 4.875/DV, 5.625/DV,
	 6.000/DV, 6.750/DV, 7.125/DV, 7.500/DV,
	 7.875/DV, 8.250/DV, 8.625/DV, 9.000/DV,
	 0.000/DV, 0.000/DV, 3.000/DV, 4.875/DV,
	 6.000/DV, 7.125/DV, 7.875/DV, 8.625/DV,
	 9.000/DV, 9.750/DV,10.125/DV,10.500/DV,
	10.875/DV,11.250/DV,11.625/DV,12.000/DV,
	 0.000/DV, 3.000/DV, 6.000/DV, 7.875/DV,
	 9.000/DV,10.125/DV,10.875/DV,11.625/DV,
	12.000/DV,12.750/DV,13.125/DV,13.500/DV,
	13.875/DV,14.250/DV,14.625/DV,15.000/DV,
	 0.000/DV, 6.000/DV, 9.000/DV,10.875/DV,
	12.000/DV,13.125/DV,13.875/DV,14.625/DV,
	15.000/DV,15.750/DV,16.125/DV,16.500/DV,
	16.875/DV,17.250/DV,17.625/DV,18.000/DV,
	 0.000/DV, 9.000/DV,12.000/DV,13.875/DV,
	15.000/DV,16.125/DV,16.875/DV,17.625/DV,
	18.000/DV,18.750/DV,19.125/DV,19.500/DV,
	19.875/DV,20.250/DV,20.625/DV,21.000/DV
};
#undef DV

#define SC(db) (db*((3/EG_STEP)*(1<<ENV_BITS)))+EG_DST
static const int32_t SL_TABLE[16]={
 SC( 0),SC( 1),SC( 2),SC(3 ),SC(4 ),SC(5 ),SC(6 ),SC( 7),
 SC( 8),SC( 9),SC(10),SC(11),SC(12),SC(13),SC(14),SC(31)
};
#undef SC

#define TL_MAX (EG_ENT*2)
static int32_t *TL_TABLE;

static int32_t **SIN_TABLE;

static int32_t *AMS_TABLE;
static int32_t *VIB_TABLE;

static int32_t *ENV_CURVE;

#define ML 2
static const uint32_t MUL_TABLE[16]= {
   0.50*ML, 1.00*ML, 2.00*ML, 3.00*ML, 4.00*ML, 5.00*ML, 6.00*ML, 7.00*ML,
   8.00*ML, 9.00*ML,10.00*ML,10.00*ML,12.00*ML,12.00*ML,15.00*ML,15.00*ML
};
#undef ML

static int32_t RATE_0[16]=
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

static int num_lock = 0;

static void *cur_chip = NULL;
static OPL_CH *S_CH;
static OPL_CH *E_CH;
static OPL_SLOT *SLOT7_1, *SLOT7_2, *SLOT8_1, *SLOT8_2;

static int32_t outd[1];
static int32_t ams;
static int32_t vib;
static int32_t *ams_table;
static int32_t *vib_table;
static int32_t amsIncr;
static int32_t vibIncr;
static int32_t feedback2;

#define LOG_ERR  3
#define LOG_WAR  2
#define LOG_INF  1

#define LOG_LEVEL	LOG_ERR

#define LOG(n,x)

static inline int Limit( int val, int max, int min ) {
	if ( val > max )
		val = max;
	else if ( val < min )
		val = min;

	return val;
}

static inline void OPL_STATUS_SET(FM_OPL *OPL,int flag)
{
	OPL->status |= flag;
	if(!(OPL->status & 0x80))
	{
		if(OPL->status & OPL->statusmask)
		{
			OPL->status |= 0x80;
		}
	}
}

static inline void OPL_STATUS_RESET(FM_OPL *OPL,int flag)
{
	OPL->status &=~flag;
	if((OPL->status & 0x80))
	{
		if (!(OPL->status & OPL->statusmask) )
		{
			OPL->status &= 0x7f;
		}
	}
}

static inline void OPL_STATUSMASK_SET(FM_OPL *OPL,int flag)
{
	OPL->statusmask = flag;
	OPL_STATUS_SET(OPL,0);
	OPL_STATUS_RESET(OPL,0);
}

static inline void OPL_KEYON(OPL_SLOT *SLOT)
{
	SLOT->Cnt = 0;
	SLOT->evm = ENV_MOD_AR;
	SLOT->evs = SLOT->evsa;
	SLOT->evc = EG_AST;
	SLOT->eve = EG_AED;
}
static inline void OPL_KEYOFF(OPL_SLOT *SLOT)
{
	if( SLOT->evm > ENV_MOD_RR)
	{
		SLOT->evm = ENV_MOD_RR;
		if( !(SLOT->evc&EG_DST) )
			SLOT->evc = EG_DST;
		SLOT->eve = EG_DED;
		SLOT->evs = SLOT->evsr;
	}
}

static inline uint32_t OPL_CALC_SLOT( OPL_SLOT *SLOT )
{
	if( (SLOT->evc+=SLOT->evs) >= SLOT->eve )
	{
		switch( SLOT->evm ){
		case ENV_MOD_AR:
			SLOT->evm = ENV_MOD_DR;
			SLOT->evc = EG_DST;
			SLOT->eve = SLOT->SL;
			SLOT->evs = SLOT->evsd;
			break;
		case ENV_MOD_DR:
			SLOT->evc = SLOT->SL;
			SLOT->eve = EG_DED;
			if(SLOT->eg_typ)
			{
				SLOT->evs = 0;
			}
			else
			{
				SLOT->evm = ENV_MOD_RR;
				SLOT->evs = SLOT->evsr;
			}
			break;
		case ENV_MOD_RR:
			SLOT->evc = EG_OFF;
			SLOT->eve = EG_OFF+1;
			SLOT->evs = 0;
			break;
		}
	}
	return SLOT->TLL+ENV_CURVE[SLOT->evc>>ENV_BITS]+(SLOT->ams ? ams : 0);
}

static void set_algorithm( OPL_CH *CH)
{
	int32_t *carrier = &outd[0];
	CH->connect1 = CH->CON ? carrier : &feedback2;
	CH->connect2 = carrier;
}

static inline void CALC_FCSLOT(OPL_CH *CH,OPL_SLOT *SLOT)
{
	int ksr;

	SLOT->Incr = CH->fc * SLOT->mul;
	ksr = CH->kcode >> SLOT->KSR;

	if( SLOT->ksr != ksr )
	{
		SLOT->ksr = ksr;
		SLOT->evsa = SLOT->AR[ksr];
		SLOT->evsd = SLOT->DR[ksr];
		SLOT->evsr = SLOT->RR[ksr];
	}
	SLOT->TLL = SLOT->TL + (CH->ksl_base>>SLOT->ksl);
}

static inline void set_mul(FM_OPL *OPL,int slot,int v)
{
	OPL_CH   *CH   = &OPL->P_CH[slot/2];
	OPL_SLOT *SLOT = &CH->SLOT[slot&1];

	SLOT->mul    = MUL_TABLE[v&0x0f];
	SLOT->KSR    = (v&0x10) ? 0 : 2;
	SLOT->eg_typ = (v&0x20)>>5;
	SLOT->vib    = (v&0x40);
	SLOT->ams    = (v&0x80);
	CALC_FCSLOT(CH,SLOT);
}

static inline void set_ksl_tl(FM_OPL *OPL,int slot,int v)
{
	OPL_CH   *CH   = &OPL->P_CH[slot/2];
	OPL_SLOT *SLOT = &CH->SLOT[slot&1];
	int ksl = v>>6;

	SLOT->ksl = ksl ? 3-ksl : 31;
	SLOT->TL  = (v&0x3f)*(0.75/EG_STEP);

	if( !(OPL->mode&0x80) )
	{
		SLOT->TLL = SLOT->TL + (CH->ksl_base>>SLOT->ksl);
	}
}

static inline void set_ar_dr(FM_OPL *OPL,int slot,int v)
{
	OPL_CH   *CH   = &OPL->P_CH[slot/2];
	OPL_SLOT *SLOT = &CH->SLOT[slot&1];
	int ar = v>>4;
	int dr = v&0x0f;

	SLOT->AR = ar ? &OPL->AR_TABLE[ar<<2] : RATE_0;
	SLOT->evsa = SLOT->AR[SLOT->ksr];
	if( SLOT->evm == ENV_MOD_AR ) SLOT->evs = SLOT->evsa;

	SLOT->DR = dr ? &OPL->DR_TABLE[dr<<2] : RATE_0;
	SLOT->evsd = SLOT->DR[SLOT->ksr];
	if( SLOT->evm == ENV_MOD_DR ) SLOT->evs = SLOT->evsd;
}

static inline void set_sl_rr(FM_OPL *OPL,int slot,int v)
{
	OPL_CH   *CH   = &OPL->P_CH[slot/2];
	OPL_SLOT *SLOT = &CH->SLOT[slot&1];
	int sl = v>>4;
	int rr = v & 0x0f;

	SLOT->SL = SL_TABLE[sl];
	if( SLOT->evm == ENV_MOD_DR ) SLOT->eve = SLOT->SL;
	SLOT->RR = &OPL->DR_TABLE[rr<<2];
	SLOT->evsr = SLOT->RR[SLOT->ksr];
	if( SLOT->evm == ENV_MOD_RR ) SLOT->evs = SLOT->evsr;
}

#define OP_OUT(slot,env,con)   slot->wavetable[((slot->Cnt+con)/(0x1000000/SIN_ENT))&(SIN_ENT-1)][env]
static inline void OPL_CALC_CH( OPL_CH *CH )
{
	uint32_t env_out;
	OPL_SLOT *SLOT;

	feedback2 = 0;
	SLOT = &CH->SLOT[SLOT1];
	env_out=OPL_CALC_SLOT(SLOT);
	if( env_out < EG_ENT-1 )
	{
		if(SLOT->vib) SLOT->Cnt += (SLOT->Incr*vib/VIB_RATE);
		else          SLOT->Cnt += SLOT->Incr;
		if(CH->FB)
		{
			int feedback1 = (CH->op1_out[0]+CH->op1_out[1])>>CH->FB;
			CH->op1_out[1] = CH->op1_out[0];
			*CH->connect1 += CH->op1_out[0] = OP_OUT(SLOT,env_out,feedback1);
		}
		else
		{
			*CH->connect1 += OP_OUT(SLOT,env_out,0);
		}
	}else
	{
		CH->op1_out[1] = CH->op1_out[0];
		CH->op1_out[0] = 0;
	}
	SLOT = &CH->SLOT[SLOT2];
	env_out=OPL_CALC_SLOT(SLOT);
	if( env_out < EG_ENT-1 )
	{
		if(SLOT->vib) SLOT->Cnt += (SLOT->Incr*vib/VIB_RATE);
		else          SLOT->Cnt += SLOT->Incr;
		outd[0] += OP_OUT(SLOT,env_out, feedback2);
	}
}

#define WHITE_NOISE_db 6.0
static inline void OPL_CALC_RH( OPL_CH *CH )
{
	uint32_t env_tam,env_sd,env_top,env_hh;
	int whitenoise = (rand()&1)*(WHITE_NOISE_db/EG_STEP);
	int32_t tone8;

	OPL_SLOT *SLOT;
	int env_out;

	feedback2 = 0;
	SLOT = &CH[6].SLOT[SLOT1];
	env_out=OPL_CALC_SLOT(SLOT);
	if( env_out < EG_ENT-1 )
	{
		if(SLOT->vib) SLOT->Cnt += (SLOT->Incr*vib/VIB_RATE);
		else          SLOT->Cnt += SLOT->Incr;
		if(CH[6].FB)
		{
			int feedback1 = (CH[6].op1_out[0]+CH[6].op1_out[1])>>CH[6].FB;
			CH[6].op1_out[1] = CH[6].op1_out[0];
			feedback2 = CH[6].op1_out[0] = OP_OUT(SLOT,env_out,feedback1);
		}
		else
		{
			feedback2 = OP_OUT(SLOT,env_out,0);
		}
	}else
	{
		feedback2 = 0;
		CH[6].op1_out[1] = CH[6].op1_out[0];
		CH[6].op1_out[0] = 0;
	}
	SLOT = &CH[6].SLOT[SLOT2];
	env_out=OPL_CALC_SLOT(SLOT);
	if( env_out < EG_ENT-1 )
	{
		if(SLOT->vib) SLOT->Cnt += (SLOT->Incr*vib/VIB_RATE);
		else          SLOT->Cnt += SLOT->Incr;
		outd[0] += OP_OUT(SLOT,env_out, feedback2)*2;
	}

	env_sd =OPL_CALC_SLOT(SLOT7_2) + whitenoise;
	env_tam=OPL_CALC_SLOT(SLOT8_1);
	env_top=OPL_CALC_SLOT(SLOT8_2);
	env_hh =OPL_CALC_SLOT(SLOT7_1) + whitenoise;

	if(SLOT7_1->vib) SLOT7_1->Cnt += (2*SLOT7_1->Incr*vib/VIB_RATE);
	else             SLOT7_1->Cnt += 2*SLOT7_1->Incr;
	if(SLOT7_2->vib) SLOT7_2->Cnt += ((CH[7].fc*8)*vib/VIB_RATE);
	else             SLOT7_2->Cnt += (CH[7].fc*8);
	if(SLOT8_1->vib) SLOT8_1->Cnt += (SLOT8_1->Incr*vib/VIB_RATE);
	else             SLOT8_1->Cnt += SLOT8_1->Incr;
	if(SLOT8_2->vib) SLOT8_2->Cnt += ((CH[8].fc*48)*vib/VIB_RATE);
	else             SLOT8_2->Cnt += (CH[8].fc*48);

	tone8 = OP_OUT(SLOT8_2,whitenoise,0 );

	if( env_sd < EG_ENT-1 )
		outd[0] += OP_OUT(SLOT7_1,env_sd, 0)*8;
	if( env_tam < EG_ENT-1 )
		outd[0] += OP_OUT(SLOT8_1,env_tam, 0)*2;
	if( env_top < EG_ENT-1 )
		outd[0] += OP_OUT(SLOT7_2,env_top,tone8)*2;
	if( env_hh  < EG_ENT-1 )
		outd[0] += OP_OUT(SLOT7_2,env_hh,tone8)*2;
}

static void init_timetables( FM_OPL *OPL , int ARRATE , int DRRATE )
{
	int i;
	double rate;

	for (i = 0;i < 4;i++) OPL->AR_TABLE[i] = OPL->DR_TABLE[i] = 0;
	for (i = 4;i <= 60;i++){
		rate  = OPL->freqbase;
		if( i < 60 ) rate *= 1.0+(i&3)*0.25;
		rate *= 1<<((i>>2)-1);
		rate *= (double)(EG_ENT<<ENV_BITS);
		OPL->AR_TABLE[i] = rate / ARRATE;
		OPL->DR_TABLE[i] = rate / DRRATE;
	}
	for (i = 60; i < ARRAY_SIZE(OPL->AR_TABLE); i++)
	{
		OPL->AR_TABLE[i] = EG_AED-1;
		OPL->DR_TABLE[i] = OPL->DR_TABLE[60];
	}
#if 0
	for (i = 0;i < 64 ;i++){
		LOG(LOG_WAR, ("rate %2d , ar %f ms , dr %f ms\n", i,
			((double)(EG_ENT<<ENV_BITS) / OPL->AR_TABLE[i]) * (1000.0 / OPL->rate),
			((double)(EG_ENT<<ENV_BITS) / OPL->DR_TABLE[i]) * (1000.0 / OPL->rate) ));
	}
#endif
}

static int OPLOpenTable( void )
{
	int s,t;
	double rate;
	int i,j;
	double pom;

	if( (TL_TABLE = malloc(TL_MAX*2*sizeof(int32_t))) == NULL)
		return 0;
	if( (SIN_TABLE = malloc(SIN_ENT*4 *sizeof(int32_t *))) == NULL)
	{
		free(TL_TABLE);
		return 0;
	}
	if( (AMS_TABLE = malloc(AMS_ENT*2 *sizeof(int32_t))) == NULL)
	{
		free(TL_TABLE);
		free(SIN_TABLE);
		return 0;
	}
	if( (VIB_TABLE = malloc(VIB_ENT*2 *sizeof(int32_t))) == NULL)
	{
		free(TL_TABLE);
		free(SIN_TABLE);
		free(AMS_TABLE);
		return 0;
	}
    ENV_CURVE = g_new(int32_t, 2 * EG_ENT + 1);
	for (t = 0;t < EG_ENT-1 ;t++){
		rate = ((1<<TL_BITS)-1)/pow(10,EG_STEP*t/20);
		TL_TABLE[       t] =  (int)rate;
		TL_TABLE[TL_MAX+t] = -TL_TABLE[t];
	}
	for ( t = EG_ENT-1; t < TL_MAX ;t++){
		TL_TABLE[t] = TL_TABLE[TL_MAX+t] = 0;
	}

	SIN_TABLE[0] = SIN_TABLE[SIN_ENT/2]         = &TL_TABLE[EG_ENT-1];
	for (s = 1;s <= SIN_ENT/4;s++){
		pom = sin(2*PI*s/SIN_ENT);
		pom = 20*log10(1/pom);
		j = pom / EG_STEP;

		SIN_TABLE[          s] = SIN_TABLE[SIN_ENT/2-s] = &TL_TABLE[j];
		SIN_TABLE[SIN_ENT/2+s] = SIN_TABLE[SIN_ENT  -s] = &TL_TABLE[TL_MAX+j];
	}
	for (s = 0;s < SIN_ENT;s++)
	{
		SIN_TABLE[SIN_ENT*1+s] = s<(SIN_ENT/2) ? SIN_TABLE[s] : &TL_TABLE[EG_ENT];
		SIN_TABLE[SIN_ENT*2+s] = SIN_TABLE[s % (SIN_ENT/2)];
		SIN_TABLE[SIN_ENT*3+s] = (s/(SIN_ENT/4))&1 ? &TL_TABLE[EG_ENT] : SIN_TABLE[SIN_ENT*2+s];
	}

	for (i=0; i<EG_ENT; i++)
	{
		pom = pow( ((double)(EG_ENT-1-i)/EG_ENT) , 8 ) * EG_ENT;
		ENV_CURVE[i] = (int)pom;
		ENV_CURVE[(EG_DST>>ENV_BITS)+i]= i;
	}
	ENV_CURVE[EG_OFF>>ENV_BITS]= EG_ENT-1;
	for (i=0; i<AMS_ENT; i++)
	{
		pom = (1.0+sin(2*PI*i/AMS_ENT))/2;
		AMS_TABLE[i]         = (1.0/EG_STEP)*pom;
		AMS_TABLE[AMS_ENT+i] = (4.8/EG_STEP)*pom;
	}
	for (i=0; i<VIB_ENT; i++)
	{
		pom = (double)VIB_RATE*0.06*sin(2*PI*i/VIB_ENT);
		VIB_TABLE[i]         = VIB_RATE + (pom*0.07);
		VIB_TABLE[VIB_ENT+i] = VIB_RATE + (pom*0.14);
	}
	return 1;
}


static void OPLCloseTable( void )
{
    g_free(ENV_CURVE);
	free(TL_TABLE);
	free(SIN_TABLE);
	free(AMS_TABLE);
	free(VIB_TABLE);
}

static inline void CSMKeyControll(OPL_CH *CH)
{
	OPL_SLOT *slot1 = &CH->SLOT[SLOT1];
	OPL_SLOT *slot2 = &CH->SLOT[SLOT2];
	OPL_KEYOFF(slot1);
	OPL_KEYOFF(slot2);
	slot1->TLL = slot1->TL + (CH->ksl_base>>slot1->ksl);
	slot1->TLL = slot1->TL + (CH->ksl_base>>slot1->ksl);
	CH->op1_out[0] = CH->op1_out[1] = 0;
	OPL_KEYON(slot1);
	OPL_KEYON(slot2);
}

static void OPL_initialize(FM_OPL *OPL)
{
	int fn;

	OPL->freqbase = (OPL->rate) ? ((double)OPL->clock / OPL->rate) / 72  : 0;
	OPL->TimerBase = 1.0/((double)OPL->clock / 72.0 );
	init_timetables( OPL , OPL_ARRATE , OPL_DRRATE );
	for( fn=0 ; fn < 1024 ; fn++ )
	{
		OPL->FN_TABLE[fn] = OPL->freqbase * fn * FREQ_RATE * (1<<7) / 2;
	}
	OPL->amsIncr = OPL->rate ? (double)AMS_ENT*(1<<AMS_SHIFT) / OPL->rate * 3.7 * ((double)OPL->clock/3600000) : 0;
	OPL->vibIncr = OPL->rate ? (double)VIB_ENT*(1<<VIB_SHIFT) / OPL->rate * 6.4 * ((double)OPL->clock/3600000) : 0;
}

static void OPLWriteReg(FM_OPL *OPL, int r, int v)
{
	OPL_CH *CH;
	int slot;
	int block_fnum;

	switch(r&0xe0)
	{
	case 0x00:
		switch(r&0x1f)
		{
		case 0x01:
			OPL->wavesel = v&0x20;
                        if(!OPL->wavesel)
			{
				int c;
				for(c=0;c<OPL->max_ch;c++)
				{
					OPL->P_CH[c].SLOT[SLOT1].wavetable = &SIN_TABLE[0];
					OPL->P_CH[c].SLOT[SLOT2].wavetable = &SIN_TABLE[0];
				}
			}
			return;
		case 0x02:
			OPL->T[0] = (256-v)*4;
			break;
		case 0x03:
			OPL->T[1] = (256-v)*16;
			return;
		case 0x04:
			if(v&0x80)
			{
				OPL_STATUS_RESET(OPL,0x7f);
			}
			else
			{
				uint8_t st1 = v&1;
				uint8_t st2 = (v>>1)&1;
				OPL_STATUS_RESET(OPL,v&0x78);
				OPL_STATUSMASK_SET(OPL,((~v)&0x78)|0x01);
				if(OPL->st[1] != st2)
				{
					double interval = st2 ? (double)OPL->T[1]*OPL->TimerBase : 0.0;
					OPL->st[1] = st2;
                    if (OPL->TimerHandler) {
                        (OPL->TimerHandler)(OPL->TimerParam, 1, interval);
                    }
				}
				if(OPL->st[0] != st1)
				{
					double interval = st1 ? (double)OPL->T[0]*OPL->TimerBase : 0.0;
					OPL->st[0] = st1;
                    if (OPL->TimerHandler) {
                        (OPL->TimerHandler)(OPL->TimerParam, 0, interval);
                    }
				}
			}
			return;
		}
		break;
	case 0x20:
		slot = slot_array[r&0x1f];
		if(slot == -1) return;
		set_mul(OPL,slot,v);
		return;
	case 0x40:
		slot = slot_array[r&0x1f];
		if(slot == -1) return;
		set_ksl_tl(OPL,slot,v);
		return;
	case 0x60:
		slot = slot_array[r&0x1f];
		if(slot == -1) return;
		set_ar_dr(OPL,slot,v);
		return;
	case 0x80:
		slot = slot_array[r&0x1f];
		if(slot == -1) return;
		set_sl_rr(OPL,slot,v);
		return;
	case 0xa0:
		switch(r)
		{
		case 0xbd:
			{
			uint8_t rkey = OPL->rhythm^v;
			OPL->ams_table = &AMS_TABLE[v&0x80 ? AMS_ENT : 0];
			OPL->vib_table = &VIB_TABLE[v&0x40 ? VIB_ENT : 0];
			OPL->rhythm  = v&0x3f;
			if(OPL->rhythm&0x20)
			{
#if 0
				usrintf_showmessage("OPL Rhythm mode select");
#endif
				if(rkey&0x10)
				{
					if(v&0x10)
					{
						OPL->P_CH[6].op1_out[0] = OPL->P_CH[6].op1_out[1] = 0;
						OPL_KEYON(&OPL->P_CH[6].SLOT[SLOT1]);
						OPL_KEYON(&OPL->P_CH[6].SLOT[SLOT2]);
					}
					else
					{
						OPL_KEYOFF(&OPL->P_CH[6].SLOT[SLOT1]);
						OPL_KEYOFF(&OPL->P_CH[6].SLOT[SLOT2]);
					}
				}
				if(rkey&0x08)
				{
					if(v&0x08) OPL_KEYON(&OPL->P_CH[7].SLOT[SLOT2]);
					else       OPL_KEYOFF(&OPL->P_CH[7].SLOT[SLOT2]);
				}
				if(rkey&0x04)
				{
					if(v&0x04) OPL_KEYON(&OPL->P_CH[8].SLOT[SLOT1]);
					else       OPL_KEYOFF(&OPL->P_CH[8].SLOT[SLOT1]);
				}
				if(rkey&0x02)
				{
					if(v&0x02) OPL_KEYON(&OPL->P_CH[8].SLOT[SLOT2]);
					else       OPL_KEYOFF(&OPL->P_CH[8].SLOT[SLOT2]);
				}
				if(rkey&0x01)
				{
					if(v&0x01) OPL_KEYON(&OPL->P_CH[7].SLOT[SLOT1]);
					else       OPL_KEYOFF(&OPL->P_CH[7].SLOT[SLOT1]);
				}
			}
			}
			return;
		}
		if( (r&0x0f) > 8) return;
		CH = &OPL->P_CH[r&0x0f];
		if(!(r&0x10))
		{
			block_fnum  = (CH->block_fnum&0x1f00) | v;
		}
		else
		{
			int keyon = (v>>5)&1;
			block_fnum = ((v&0x1f)<<8) | (CH->block_fnum&0xff);
			if(CH->keyon != keyon)
			{
				if( (CH->keyon=keyon) )
				{
					CH->op1_out[0] = CH->op1_out[1] = 0;
					OPL_KEYON(&CH->SLOT[SLOT1]);
					OPL_KEYON(&CH->SLOT[SLOT2]);
				}
				else
				{
					OPL_KEYOFF(&CH->SLOT[SLOT1]);
					OPL_KEYOFF(&CH->SLOT[SLOT2]);
				}
			}
		}
		if(CH->block_fnum != block_fnum)
		{
			int blockRv = 7-(block_fnum>>10);
			int fnum   = block_fnum&0x3ff;
			CH->block_fnum = block_fnum;

			CH->ksl_base = KSL_TABLE[block_fnum>>6];
			CH->fc = OPL->FN_TABLE[fnum]>>blockRv;
			CH->kcode = CH->block_fnum>>9;
			if( (OPL->mode&0x40) && CH->block_fnum&0x100) CH->kcode |=1;
			CALC_FCSLOT(CH,&CH->SLOT[SLOT1]);
			CALC_FCSLOT(CH,&CH->SLOT[SLOT2]);
		}
		return;
	case 0xc0:
		if( (r&0x0f) > 8) return;
		CH = &OPL->P_CH[r&0x0f];
		{
		int feedback = (v>>1)&7;
		CH->FB   = feedback ? (8+1) - feedback : 0;
		CH->CON = v&1;
		set_algorithm(CH);
		}
		return;
	case 0xe0:
		slot = slot_array[r&0x1f];
		if(slot == -1) return;
		CH = &OPL->P_CH[slot/2];
		if(OPL->wavesel)
		{
			CH->SLOT[slot&1].wavetable = &SIN_TABLE[(v&0x03)*SIN_ENT];
		}
		return;
	}
}

static int OPL_LockTable(void)
{
	num_lock++;
	if(num_lock>1) return 0;
	cur_chip = NULL;
	if( !OPLOpenTable() )
	{
		num_lock--;
		return -1;
	}
	return 0;
}

static void OPL_UnLockTable(void)
{
	if(num_lock) num_lock--;
	if(num_lock) return;
	cur_chip = NULL;
	OPLCloseTable();
}

void YM3812UpdateOne(FM_OPL *OPL, int16_t *buffer, int length)
{
    int i;
	int data;
	int16_t *buf = buffer;
	uint32_t amsCnt  = OPL->amsCnt;
	uint32_t  vibCnt  = OPL->vibCnt;
	uint8_t rhythm = OPL->rhythm&0x20;
	OPL_CH *CH,*R_CH;

	if( (void *)OPL != cur_chip ){
		cur_chip = (void *)OPL;
		S_CH = OPL->P_CH;
		E_CH = &S_CH[9];
		SLOT7_1 = &S_CH[7].SLOT[SLOT1];
		SLOT7_2 = &S_CH[7].SLOT[SLOT2];
		SLOT8_1 = &S_CH[8].SLOT[SLOT1];
		SLOT8_2 = &S_CH[8].SLOT[SLOT2];
		amsIncr = OPL->amsIncr;
		vibIncr = OPL->vibIncr;
		ams_table = OPL->ams_table;
		vib_table = OPL->vib_table;
	}
	R_CH = rhythm ? &S_CH[6] : E_CH;
    for( i=0; i < length ; i++ )
	{
		ams = ams_table[(amsCnt+=amsIncr)>>AMS_SHIFT];
		vib = vib_table[(vibCnt+=vibIncr)>>VIB_SHIFT];
		outd[0] = 0;
		for(CH=S_CH ; CH < R_CH ; CH++)
			OPL_CALC_CH(CH);
		if(rhythm)
			OPL_CALC_RH(S_CH);
		data = Limit( outd[0] , OPL_MAXOUT, OPL_MINOUT );
		buf[i] = data >> OPL_OUTSB;
	}

	OPL->amsCnt = amsCnt;
	OPL->vibCnt = vibCnt;
#ifdef OPL_OUTPUT_LOG
	if(opl_dbg_fp)
	{
		for(opl_dbg_chip=0;opl_dbg_chip<opl_dbg_maxchip;opl_dbg_chip++)
			if( opl_dbg_opl[opl_dbg_chip] == OPL) break;
		fprintf(opl_dbg_fp,"%c%c%c",0x20+opl_dbg_chip,length&0xff,length/256);
	}
#endif
}

static void OPLResetChip(FM_OPL *OPL)
{
	int c,s;
	int i;

	OPL->mode   = 0;
	OPL_STATUS_RESET(OPL,0x7f);
	OPLWriteReg(OPL,0x01,0);
	OPLWriteReg(OPL,0x02,0);
	OPLWriteReg(OPL,0x03,0);
	OPLWriteReg(OPL,0x04,0);
	for(i = 0xff ; i >= 0x20 ; i-- ) OPLWriteReg(OPL,i,0);
	for( c = 0 ; c < OPL->max_ch ; c++ )
	{
		OPL_CH *CH = &OPL->P_CH[c];
		for(s = 0 ; s < 2 ; s++ )
		{
			CH->SLOT[s].wavetable = &SIN_TABLE[0];
			CH->SLOT[s].evc = EG_OFF;
			CH->SLOT[s].eve = EG_OFF+1;
			CH->SLOT[s].evs = 0;
		}
	}
}

FM_OPL *OPLCreate(int clock, int rate)
{
	char *ptr;
	FM_OPL *OPL;
	int state_size;
	int max_ch = 9;

	if( OPL_LockTable() ==-1) return NULL;
	state_size  = sizeof(FM_OPL);
	state_size += sizeof(OPL_CH)*max_ch;
	ptr = malloc(state_size);
	if(ptr==NULL) return NULL;
	memset(ptr,0,state_size);
	OPL        = (FM_OPL *)ptr; ptr+=sizeof(FM_OPL);
	OPL->P_CH  = (OPL_CH *)ptr; ptr+=sizeof(OPL_CH)*max_ch;
	OPL->clock = clock;
	OPL->rate  = rate;
	OPL->max_ch = max_ch;
	OPL_initialize(OPL);
	OPLResetChip(OPL);
#ifdef OPL_OUTPUT_LOG
	if(!opl_dbg_fp)
	{
		opl_dbg_fp = fopen("opllog.opl","wb");
		opl_dbg_maxchip = 0;
	}
	if(opl_dbg_fp)
	{
		opl_dbg_opl[opl_dbg_maxchip] = OPL;
		fprintf(opl_dbg_fp,"%c%c%c%c%c%c",0x00+opl_dbg_maxchip,
			type,
			clock&0xff,
			(clock/0x100)&0xff,
			(clock/0x10000)&0xff,
			(clock/0x1000000)&0xff);
		opl_dbg_maxchip++;
	}
#endif
	return OPL;
}

void OPLDestroy(FM_OPL *OPL)
{
#ifdef OPL_OUTPUT_LOG
	if(opl_dbg_fp)
	{
		fclose(opl_dbg_fp);
		opl_dbg_fp = NULL;
	}
#endif
	OPL_UnLockTable();
	free(OPL);
}

void OPLSetTimerHandler(FM_OPL *OPL, OPL_TIMERHANDLER TimerHandler,
                        void *param)
{
	OPL->TimerHandler   = TimerHandler;
    OPL->TimerParam = param;
}

int OPLWrite(FM_OPL *OPL,int a,int v)
{
	if( !(a&1) )
	{
		OPL->address = v & 0xff;
	}
	else
	{
#ifdef OPL_OUTPUT_LOG
	if(opl_dbg_fp)
	{
		for(opl_dbg_chip=0;opl_dbg_chip<opl_dbg_maxchip;opl_dbg_chip++)
			if( opl_dbg_opl[opl_dbg_chip] == OPL) break;
		fprintf(opl_dbg_fp,"%c%c%c",0x10+opl_dbg_chip,OPL->address,v);
	}
#endif
		OPLWriteReg(OPL,OPL->address,v);
	}
	return OPL->status>>7;
}

unsigned char OPLRead(FM_OPL *OPL,int a)
{
	if( !(a&1) )
	{
		return OPL->status & (OPL->statusmask|0x80);
	}
	switch(OPL->address)
	{
	case 0x05:
		return 0;
#if 0
	case 0x0f:
		return 0;
#endif
	case 0x19:
		return 0;
	case 0x1a:
		return 0;
	}
	return 0;
}

int OPLTimerOver(FM_OPL *OPL,int c)
{
	if( c )
	{
		OPL_STATUS_SET(OPL,0x20);
	}
	else
	{
		OPL_STATUS_SET(OPL,0x40);
		if( OPL->mode & 0x80 )
		{
			int ch;
			for(ch=0;ch<9;ch++)
				CSMKeyControll( &OPL->P_CH[ch] );
		}
	}
    if (OPL->TimerHandler) {
        (OPL->TimerHandler)(OPL->TimerParam, c,
                            (double)OPL->T[c] * OPL->TimerBase);
    }
	return OPL->status>>7;
}
