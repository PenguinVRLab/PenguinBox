#ifndef FMOPL_H
#define FMOPL_H


typedef void (*OPL_TIMERHANDLER)(void *param, int channel, double interval_Sec);

typedef struct fm_opl_slot {
	int32_t TL;
	int32_t TLL;
	uint8_t  KSR;
	int32_t *AR;
	int32_t *DR;
	int32_t SL;
	int32_t *RR;
	uint8_t ksl;
	uint8_t ksr;
	uint32_t mul;
	uint32_t Cnt;
	uint32_t Incr;
	uint8_t eg_typ;
	uint8_t evm;
	int32_t evc;
	int32_t eve;
	int32_t evs;
	int32_t evsa;
	int32_t evsd;
	int32_t evsr;
	uint8_t ams;
	uint8_t vib;
	int32_t **wavetable;
}OPL_SLOT;

typedef struct fm_opl_channel {
	OPL_SLOT SLOT[2];
	uint8_t CON;
	uint8_t FB;
	int32_t *connect1;
	int32_t *connect2;
	int32_t op1_out[2];
	uint32_t  block_fnum;
	uint8_t kcode;
	uint32_t  fc;
	uint32_t  ksl_base;
	uint8_t keyon;
} OPL_CH;

typedef struct fm_opl_f {
	int clock;
	int rate;
	double freqbase;
	double TimerBase;
	uint8_t address;
	uint8_t status;
	uint8_t statusmask;
	uint32_t mode;
	int T[2];
	uint8_t st[2];
	OPL_CH *P_CH;
	int	max_ch;
	uint8_t rhythm;
	int32_t AR_TABLE[76];
	int32_t DR_TABLE[76];
	uint32_t FN_TABLE[1024];
	int32_t *ams_table;
	int32_t *vib_table;
	int32_t amsCnt;
	int32_t amsIncr;
	int32_t vibCnt;
	int32_t vibIncr;
	uint8_t wavesel;
	OPL_TIMERHANDLER  TimerHandler;
    void *TimerParam;
} FM_OPL;

FM_OPL *OPLCreate(int clock, int rate);
void OPLDestroy(FM_OPL *OPL);
void OPLSetTimerHandler(FM_OPL *OPL, OPL_TIMERHANDLER TimerHandler,
                        void *param);

int OPLWrite(FM_OPL *OPL,int a,int v);
unsigned char OPLRead(FM_OPL *OPL,int a);
int OPLTimerOver(FM_OPL *OPL,int c);

void YM3812UpdateOne(FM_OPL *OPL, int16_t *buffer, int length);
#endif
