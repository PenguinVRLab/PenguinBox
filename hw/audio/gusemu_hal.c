/*
 * GUSEMU32 - bus interface part
 *
 * Copyright (C) 2000-2007 Tibor "TS" Schütz
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * TODO: check mixer: see 7.20 of sdk for panning pos (applies to all gus models?)?
 */

#include "qemu/osdep.h"
#include "gustate.h"
#include "gusemu.h"

#define GUSregb(position) (*            (gusptr+(position)))
#define GUSregw(position) (*(uint16_t *) (gusptr+(position)))
#define GUSregd(position) (*(uint32_t *)(gusptr + (position)))

unsigned int gus_read(GUSEmuState * state, int port, int size)
{
    int             value_read = 0;

    uint8_t        *gusptr;
    gusptr = state->gusdatapos;
    GUSregd(portaccesses)++;

    switch (port & 0xff0f)
    {
    case 0x206:
        return GUSregb(IRQStatReg2x6);
    case 0x208:
        if (GUSregb(GUS45TimerCtrl) & 1)
            return GUSregb(TimerStatus2x8);
        return GUSregb(AdLibStatus2x8);
    case 0x309:
    case 0x209:
        return GUSregb(AdLibData2x9);
    case 0x20A:
        return GUSregb(AdLibCommand2xA);

#if 0
    case 0x20B:
        switch (GUSregb(RegCtrl_2xF) & 0x07)
        {
        case 0:
            if (GUSregb(MixerCtrlReg2x0) & 0x40)
                return GUSregb(IRQ_2xB);
            else
                return GUSregb(DMA_2xB);
        case 6:
            return GUSregb(Jumper_2xB);
        default:;
        }
        break;
#endif

    case 0x20C:
        value_read = GUSregb(SB2xCd);
        if (GUSregb(StatRead_2xF) & 0x20)
            GUSregb(SB2xCd) ^= 0x80;
        return value_read;
    case 0x20E:
        if (GUSregb(RegCtrl_2xF) & 0x80)
        {
            GUSregb(StatRead_2xF) |= 0x80;
            GUS_irqrequest(state, state->gusirq, 1);
        }
        return GUSregb(SB2xE);
    case 0x20F:
        value_read = (GUSregb(StatRead_2xF) & 0xf9);
        if (GUSregb(MixerCtrlReg2x0) & 0x08)
            value_read |= 2;
        return value_read;
    case 0x302:
        return GUSregb(VoiceSelReg3x2);
    case 0x303:
        return GUSregb(FunkSelReg3x3);
    case 0x304:
    case 0x305:
        switch (GUSregb(FunkSelReg3x3))
        {
        case 0x41:
            value_read = GUSregb(GUS41DMACtrl);
            GUSregb(GUS41DMACtrl) &= 0xbb;
            if (state->gusdma >= 4)
                value_read |= 0x04;
            if (GUSregb(IRQStatReg2x6) & 0x80)
            {
                value_read |= 0x40;
                GUSregb(IRQStatReg2x6) &= 0x7f;
                if (!GUSregb(IRQStatReg2x6))
                    GUS_irqclear(state, state->gusirq);
            }
            return (uint8_t) value_read;
        case 0x45:
            return GUSregb(GUS45TimerCtrl);
        case 0x49:
            return GUSregb(GUS49SampCtrl) & 0xbf;
        case 0x80:
        case 0x81:
        case 0x82:
        case 0x83:
        case 0x84:
        case 0x85:
        case 0x86:
        case 0x87:
        case 0x88:
        case 0x89:
        case 0x8a:
        case 0x8b:
        case 0x8c:
        case 0x8d:
            {
                int             offset = 2 * (GUSregb(FunkSelReg3x3) & 0x0f);
                offset += ((int) GUSregb(VoiceSelReg3x2) & 0x1f) << 5;
                value_read = GUSregw(offset);
            }
            break;
        case 0x8e:
            return GUSregb(NumVoices);
        case 0x8f:
            return GUSregb(SynVoiceIRQ8f);
        default:
            return 0xffff;
        }
        if (size == 1)
        {
            if ((port & 0xff0f) == 0x305)
                value_read = value_read >> 8;
            value_read &= 0xff;
        }
        return (uint16_t) value_read;
    case 0x307:
        {
            uint8_t        *adr;
            adr = state->himemaddr + (GUSregd(GUSDRAMPOS24bit) & 0xfffff);
            return *adr;
        }
    default:;
    }
    return 0xffff;
}

void gus_write(GUSEmuState * state, int port, int size, unsigned int data)
{
    uint8_t        *gusptr;
    gusptr = state->gusdatapos;
    GUSregd(portaccesses)++;

    switch (port & 0xff0f)
    {
    case 0x200:
        GUSregb(MixerCtrlReg2x0) = (uint8_t) data;
        break;
    case 0x206:
        if (GUSregb(GUS45TimerCtrl) & 0x20)
        {
            GUSregb(TimerStatus2x8) |= 0x08;
            GUSregb(IRQStatReg2x6) = 0x10;
            GUS_irqrequest(state, state->gusirq, 1);
        }
        break;
    case 0x308:
    case 0x208:
        GUSregb(AdLibCommand2xA) = (uint8_t) data;
        break;
    case 0x309:
    case 0x209:
        if ((GUSregb(AdLibCommand2xA) == 0x04) && (!(GUSregb(GUS45TimerCtrl) & 1)))
        {
            if (data & 0x80)
                GUSregb(TimerStatus2x8) &= 0x1f;
            else
                GUSregb(TimerDataReg2x9) = (uint8_t) data;
        }
        else
        {
            GUSregb(AdLibData2x9) = (uint8_t) data;
            if (GUSregb(GUS45TimerCtrl) & 0x02)
            {
                GUSregb(TimerStatus2x8) |= 0x01;
                GUSregb(IRQStatReg2x6) = 0x10;
                GUS_irqrequest(state, state->gusirq, 1);
            }
        }
        break;
    case 0x20A:
        GUSregb(AdLibStatus2x8) = (uint8_t) data;
        break;
    case 0x20B:
        switch (GUSregb(RegCtrl_2xF) & 0x7)
        {
        case 0:
            if (GUSregb(MixerCtrlReg2x0) & 0x40)
                GUSregb(IRQ_2xB) = (uint8_t) data;
            else
                GUSregb(DMA_2xB) = (uint8_t) data;
            break;
        case 5:
            GUSregb(StatRead_2xF) = 0;
            if (!GUSregb(IRQStatReg2x6))
                GUS_irqclear(state, state->gusirq);
            break;
        case 6:
            GUSregb(Jumper_2xB) = (uint8_t) data;
            break;
        default:;
        }
        break;
    case 0x20C:
        if (GUSregb(GUS45TimerCtrl) & 0x20)
        {
            GUSregb(TimerStatus2x8) |= 0x10;
            GUSregb(IRQStatReg2x6) = 0x10;
            GUS_irqrequest(state, state->gusirq, 1);
        }
        /* fall through */
    case 0x20D:
        GUSregb(SB2xCd) = (uint8_t) data;
        break;
    case 0x20E:
        GUSregb(SB2xE) = (uint8_t) data;
        break;
    case 0x20F:
        GUSregb(RegCtrl_2xF) = (uint8_t) data;
        break;
    case 0x302:
        GUSregb(VoiceSelReg3x2) = (uint8_t) data;
        break;
    case 0x303:
        GUSregb(FunkSelReg3x3) = (uint8_t) data;
        if ((uint8_t) data == 0x8f)
        {
            int             voice;
            if (GUSregd(voicewavetableirq))
            {
                for (voice = 0; voice < 31; voice++)
                {
                    if (GUSregd(voicewavetableirq) & (1 << voice))
                    {
                        GUSregd(voicewavetableirq) ^= (1 << voice);
                        GUSregb(voice << 5) &= 0x7f;
                        if (!GUSregd(voicewavetableirq))
                            GUSregb(IRQStatReg2x6) &= 0xdf;
                        if (!GUSregb(IRQStatReg2x6))
                            GUS_irqclear(state, state->gusirq);
                        GUSregb(SynVoiceIRQ8f) = voice | 0x60;
                        return;
                    }
                }
            }
            else if (GUSregd(voicevolrampirq))
            {
                for (voice = 0; voice < 31; voice++)
                {
                    if (GUSregd(voicevolrampirq) & (1 << voice))
                    {
                        GUSregd(voicevolrampirq) ^= (1 << voice);
                        GUSregb((voice << 5) + VSRVolRampControl) &= 0x7f;
                        if (!GUSregd(voicevolrampirq))
                            GUSregb(IRQStatReg2x6) &= 0xbf;
                        if (!GUSregb(IRQStatReg2x6))
                            GUS_irqclear(state, state->gusirq);
                        GUSregb(SynVoiceIRQ8f) = voice | 0x80;
                        return;
                    }
                }
            }
            GUSregb(SynVoiceIRQ8f) = 0xe8;
        }
        break;
    case 0x304:
    case 0x305:
        {
            uint16_t         writedata = (uint16_t) data;
            uint16_t         readmask = 0x0000;
            if (size == 1)
            {
                readmask = 0xff00;
                writedata &= 0xff;
                if ((port & 0xff0f) == 0x305)
                {
                    writedata = (uint16_t) (writedata << 8);
                    readmask = 0x00ff;
                }
            }
            switch (GUSregb(FunkSelReg3x3))
            {
            case 0x00:
            case 0x01:
            case 0x02:
            case 0x03:
            case 0x04:
            case 0x05:
            case 0x06:
            case 0x07:
            case 0x08:
            case 0x09:
            case 0x0a:
            case 0x0b:
            case 0x0c:
            case 0x0d:
                {
                    int             offset;
                    if (!(GUSregb(GUS4cReset) & 0x01))
                        break;
                    offset = 2 * (GUSregb(FunkSelReg3x3) & 0x0f);
                    offset += (GUSregb(VoiceSelReg3x2) & 0x1f) << 5;
                    GUSregw(offset) = (uint16_t) ((GUSregw(offset) & readmask) | writedata);
                }
                break;
            case 0x0e:
                GUSregb(NumVoices) = (uint8_t) data;
                break;
            case 0x41:
                GUSregb(GUS41DMACtrl) = (uint8_t) data;
                if (data & 0x01)
                    GUS_dmarequest(state);
                break;
            case 0x42:
                GUSregw(GUS42DMAStart) = (GUSregw(GUS42DMAStart) & readmask) | writedata;
                GUSregb(GUS50DMAHigh) &= 0xf;
                break;
            case 0x43:
                GUSregd(GUSDRAMPOS24bit) =
                    (GUSregd(GUSDRAMPOS24bit) & (readmask | 0xff0000)) | writedata;
                break;
            case 0x44:
                GUSregd(GUSDRAMPOS24bit) =
                    (GUSregd(GUSDRAMPOS24bit) & 0xffff) | ((data & 0x0f) << 16);
                break;
            case 0x45:
                GUSregb(GUS45TimerCtrl) = (uint8_t) data;
                if (!(data & 0x20))
                    GUSregb(TimerStatus2x8) &= 0xe7;
                if (!(data & 0x02))
                    GUSregb(TimerStatus2x8) &= 0xfe;
                if (!(GUSregb(TimerStatus2x8) & 0x19))
                    GUSregb(IRQStatReg2x6) &= 0xef;
                if ((GUSregw(TimerIRQs) > 1) && (GUSregb(TimerDataReg2x9) & 3))
                {
                    if (GUSregb(TimerDataReg2x9) & 1)
                    {
                        if (!(GUSregb(TimerDataReg2x9) & 0x40))
                            GUSregb(TimerStatus2x8) |= 0xc0;
                        if (data & 4)
                        {
                            GUSregb(TimerStatus2x8) |= 4;
                            GUSregb(IRQStatReg2x6) |= 4;
                        }
                    }
                    if (GUSregb(TimerDataReg2x9) & 2)
                    {
                        if (!(GUSregb(TimerDataReg2x9) & 0x20))
                            GUSregb(TimerStatus2x8) |= 0xa0;
                        if (data & 8)
                        {
                            GUSregb(TimerStatus2x8) |= 2;
                            GUSregb(IRQStatReg2x6) |= 8;
                        }
                    }
                    GUSregw(TimerIRQs)--;
                    if (GUSregw(BusyTimerIRQs) > 1)
                        GUSregw(BusyTimerIRQs)--;
                    else
                        GUSregw(BusyTimerIRQs) =
                            GUS_irqrequest(state, state->gusirq, GUSregw(TimerIRQs));
                }
                else
                    GUSregw(TimerIRQs) = 0;

                if (!(data & 0x04))
                {
                    GUSregb(TimerStatus2x8) &= 0xfb;
                    GUSregb(IRQStatReg2x6)  &= 0xfb;
                }
                if (!(data & 0x08))
                {
                    GUSregb(TimerStatus2x8) &= 0xfd;
                    GUSregb(IRQStatReg2x6)  &= 0xf7;
                }
                if (!GUSregb(IRQStatReg2x6))
                    GUS_irqclear(state, state->gusirq);
                break;
            case 0x46:
                GUSregb(GUS46Counter1) = (uint8_t) data;
                break;
            case 0x47:
                GUSregb(GUS47Counter2) = (uint8_t) data;
                break;
            case 0x49:
                GUSregb(GUS49SampCtrl) = (uint8_t) data;
                break;
            case 0x4c:
                GUSregb(GUS4cReset) = (uint8_t) data;
                if (!(GUSregb(GUS4cReset) & 1))
                {
                    GUSregd(voicewavetableirq) = 0;
                    GUSregd(voicevolrampirq) = 0;
                    GUSregw(TimerIRQs) = 0;
                    GUSregw(BusyTimerIRQs) = 0;
                    GUSregb(NumVoices) = 0xcd;
                    GUSregb(IRQStatReg2x6) = 0;
                    GUSregb(TimerStatus2x8) = 0;
                    GUSregb(AdLibData2x9) = 0;
                    GUSregb(TimerDataReg2x9) = 0;
                    GUSregb(GUS41DMACtrl) = 0;
                    GUSregb(GUS45TimerCtrl) = 0;
                    GUSregb(GUS49SampCtrl) = 0;
                    GUSregb(GUS4cReset) &= 0xf9;
                    GUS_irqclear(state, state->gusirq);
                }
                break;
            }
        }
        break;
    case 0x307:
        {
            uint8_t        *adr;
            adr = state->himemaddr + (GUSregd(GUSDRAMPOS24bit) & 0xfffff);
            *adr = (uint8_t) data;
        }
        break;
    }
}

void gus_dma_transferdata(GUSEmuState * state, char *dma_addr, unsigned int count, int TC)
{

    char           *srcaddr;
    char           *destaddr;
    char            msbmask = 0;
    uint8_t        *gusptr;
    gusptr = state->gusdatapos;

    srcaddr = dma_addr;
    {
        int             offset = (GUSregw(GUS42DMAStart) << 4) + (GUSregb(GUS50DMAHigh) & 0xf);
        if (state->gusdma >= 4)
            offset = (offset & 0xc0000) + (2 * (offset & 0x1fff0));
        destaddr = (char *) state->himemaddr + offset;
    }

    GUSregw(GUS42DMAStart) += (uint16_t)  (count >> 4);
    GUSregb(GUS50DMAHigh)   = (uint8_t) ((count + GUSregb(GUS50DMAHigh)) & 0xf);

    if (GUSregb(GUS41DMACtrl) & 0x02)
    {
        char           *tmpaddr = destaddr;
        destaddr = srcaddr;
        srcaddr = tmpaddr;
    }

    if ((GUSregb(GUS41DMACtrl) & 0x80) && (!(GUSregb(GUS41DMACtrl) & 0x02)))
        msbmask = (const char) 0x80;
    for (; count > 0; count--)
    {
        if (GUSregb(GUS41DMACtrl) & 0x40)
            *(destaddr++) = *(srcaddr++);
        else
            *(destaddr++) = (msbmask ^ (*(srcaddr++)));
        if (state->gusdma >= 4)
            *(destaddr++) = (msbmask ^ (*(srcaddr++)));
    }

    if (TC)
    {
        (GUSregb(GUS41DMACtrl)) &= 0xfe;
        if (GUSregb(GUS41DMACtrl) & 0x20)
        {
            GUSregb(IRQStatReg2x6) |= 0x80;
            GUS_irqrequest(state, state->gusirq, 1);
        }
    }
}
