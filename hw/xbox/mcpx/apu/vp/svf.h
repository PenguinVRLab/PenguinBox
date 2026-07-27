/*
 * Adapted from SWH LADSPA Plugins package, modified for xemu
 *
 * Source:  https://github.com/swh/ladspa/blob/master/svf_1214.xml
 * Author:  Steve Harris, andy@vellocet
 * License: GPLv2
 *
 */

#ifndef SVF_H
#define SVF_H

#include <math.h>

#define flush_to_zero(x) x

#define F_LP 1
#define F_HP 2
#define F_BP 3
#define F_BR 4
#define F_AP 5

#define F_R 1

typedef struct {
    float f;
    float q;
    float qnrm;
    float h;
    float b;
    float l;
    float p;
    float n;
    float *op;
} sv_filter;

static inline void setup_svf(sv_filter *sv, float fc, float q, int t) {
    sv->f = fc;
    sv->q = q;
    sv->qnrm = sqrt(sv->q/2.0+0.01);
    switch(t) {
    case F_LP:
        sv->op = &(sv->l);
        break;
    case F_HP:
        sv->op = &(sv->h);
        break;
    case F_BP:
        sv->op = &(sv->b);
        break;
    case F_BR:
        sv->op = &(sv->n);
        break;
    default:
        sv->op = &(sv->p);
    }
}

static inline float run_svf(sv_filter *sv, float in) {
    float out;
    int i;
    in = sv->qnrm * in ;
    for (i=0; i < F_R; i++) {
        sv->b = flush_to_zero(sv->b - sv->b * sv->b * sv->b * 0.001f);
        sv->h = flush_to_zero(in - sv->l - sv->q * sv->b);
        sv->b = sv->b + sv->f * sv->h;
        sv->l = flush_to_zero(sv->l + sv->f * sv->b);
        sv->n = sv->l + sv->h;
        sv->p = sv->l - sv->h;
        out = *(sv->op);
        in = out;
    }
    return out;
}

#endif
