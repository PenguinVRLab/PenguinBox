/*
 * GUSEMU32 - API
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

#ifndef GUSEMU_H
#define GUSEMU_H

typedef struct _GUSEmuState
{
 uint8_t *himemaddr;
 uint8_t *gusdatapos;
 uint32_t gusirq;
 uint32_t gusdma;
 unsigned int timer1fraction;
 unsigned int timer2fraction;
 void *opaque;
} GUSEmuState;

int  GUS_irqrequest(GUSEmuState *state, int hwirq, int num);
void GUS_irqclear(  GUSEmuState *state, int hwirq);
void GUS_dmarequest(GUSEmuState *state);

unsigned int gus_read( GUSEmuState *state, int port, int size);
void         gus_write(GUSEmuState *state, int port, int size, unsigned int data);

void gus_dma_transferdata(GUSEmuState *state, char *dma_addr, unsigned int count, int TC);

void gus_mixvoices(GUSEmuState *state, unsigned int playback_freq, unsigned int numsamples, int16_t *bufferpos);

void gus_irqgen(GUSEmuState *state, unsigned int elapsed_time);

#endif
