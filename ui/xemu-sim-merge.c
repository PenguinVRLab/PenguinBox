/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#include "xemu-sim-merge.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static double clampd(double v, double lo, double hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

bool sim_merge_bipolar(SimRange r)
{
    double lo = r.min <= r.max ? r.min : r.max, hi = r.min <= r.max ? r.max : r.min;
    return lo < r.rest && r.rest < hi;
}

static double far_end(SimRange r)
{
    return r.rest == r.min ? r.max : r.min;
}

double sim_merge_normalise(SimRange r, double v)
{
    if (sim_merge_bipolar(r)) {
        double c = v >= r.rest ? (v - r.rest) / (r.max - r.rest) : (v - r.rest) / (r.rest - r.min);
        return clampd(c, -1.0, 1.0);
    }
    double far = far_end(r);
    if (far == r.rest) {
        return 0.0;
    }
    return clampd((v - r.rest) / (far - r.rest), 0.0, 1.0);
}

double sim_merge_denormalise(SimRange r, double c)
{
    if (sim_merge_bipolar(r)) {
        return c >= 0 ? r.rest + c * (r.max - r.rest) : r.rest + c * (r.rest - r.min);
    }
    return r.rest + c * (far_end(r) - r.rest);
}

double sim_merge_response(const SimBinding *b, double c, bool bipolar, double dev_deadzone)
{
    if (b->resp_invert) {
        c = bipolar ? -c : 1.0 - c;
    }
    double m = fabs(c), s = c < 0 ? -1.0 : 1.0;
    double lo = b->has_window ? b->window_lo : 0.0, hi = b->has_window ? b->window_hi : 1.0;
    double dz = b->has_deadzone ? b->resp_deadzone : 0.0;
    if (dev_deadzone > dz) {
        dz = dev_deadzone;
    }
    double low = lo > dz ? lo : dz;
    if (m <= low) {
        m = 0.0;
    } else if (m >= hi || hi <= low) {
        m = 1.0;
    } else {
        m = (m - low) / (hi - low);
    }
    if (b->has_saturation && b->saturation > 0) {
        m = m / b->saturation;
        if (m > 1.0) {
            m = 1.0;
        }
    }
    if (b->has_curve && b->curve > 0 && m > 0) {
        m = pow(m, b->curve);
    }
    return s * m;
}

double sim_merge_policy(SimPolicy p, bool from_bipolar, double c, int which)
{
    if (p == SIM_POLICY_SPLIT) {
        if (!from_bipolar) {
            return which == 0 ? c : 0.0;
        }
        return which == 0 ? (c > 0 ? c : 0.0) : (c < 0 ? -c : 0.0);
    }
    if (from_bipolar) {
        switch (p) {
        case SIM_POLICY_HALF_POSITIVE: return c > 0 ? c : 0.0;
        case SIM_POLICY_HALF_NEGATIVE: return c < 0 ? -c : 0.0;
        case SIM_POLICY_ABSOLUTE:      return fabs(c);
        case SIM_POLICY_FULL_RANGE:    return (c + 1.0) / 2.0;
        default:                       return c;
        }
    }
    switch (p) {
    case SIM_POLICY_HALF_POSITIVE: return c;
    case SIM_POLICY_HALF_NEGATIVE: return -c;
    case SIM_POLICY_ABSOLUTE:      return c;
    case SIM_POLICY_FULL_RANGE:    return 2.0 * c - 1.0;
    default:                       return c;
    }
}

double sim_merge_round(double v, double rest)
{
    return v >= rest ? floor(v + 0.5) : ceil(v - 0.5);
}

int16_t sim_merge_sbc_aiming_axis(double wire)
{
    if (isnan(wire)) {
        wire = 32768.0;
    }
    long w = (long)sim_merge_round(clampd(wire, 0.0, 65535.0), 32768.0);
    long axis = w - 0x8000;
#ifdef SIM_MERGE_DRILL_TRUNC_AIMING
    long q = axis;
#else
    long q = (long)floor((double)axis / 256.0) * 256;
#endif
    return (int16_t)q;
}

#define DETENT_UNLISTED_SPAN 549755813888.0

static char position_kind(const SimInput *lever, int i)
{
    return lever->position_kinds ? lever->position_kinds[i] : 'S';
}

double sim_merge_detent_int(const SimInput *lever, long long reading)
{
    for (int i = 0; lever && i < lever->npositions; i++) {
        if (position_kind(lever, i) == 'I' && lever->position_ints && lever->position_ints[i] == reading) {
            return (double)i;
        }
    }
    if (reading <= -(long long)DETENT_UNLISTED_SPAN || reading >= (long long)DETENT_UNLISTED_SPAN) {
        return -1.0;
    }
    return SIM_DETENT_UNLISTED + (double)reading;
}

double sim_merge_detent_text(const SimInput *lever, const char *reading)
{
    for (int i = 0; lever && reading && i < lever->npositions; i++) {
        if (position_kind(lever, i) == 'S' && !strcmp(lever->positions[i], reading)) {
            return (double)i;
        }
    }
    return -1.0;
}

double sim_merge_hat_value(double x, double y)
{
    unsigned bits = 0;
    if (isfinite(x)) {
        bits |= x < 0 ? SIM_HAT_LEFT : x > 0 ? SIM_HAT_RIGHT : 0u;
    }
    if (isfinite(y)) {
        bits |= y < 0 ? SIM_HAT_UP : y > 0 ? SIM_HAT_DOWN : 0u;
    }
    return (double)bits;
}

static bool detent_decode(const SimInput *lever, double v, int *index, long long *unlisted)
{
    *index = -1;
    if (!isfinite(v) || v != floor(v)) {
        return false;
    }
    if (v >= 0 && v < lever->npositions) {
        *index = (int)v;
        return true;
    }
    double r = v - SIM_DETENT_UNLISTED;
    if (r > -DETENT_UNLISTED_SPAN && r < DETENT_UNLISTED_SPAN) {
        double i = sim_merge_detent_int(lever, (long long)r);
        if (i >= 0 && i < SIM_DETENT_UNLISTED - DETENT_UNLISTED_SPAN) {
            *index = (int)i;
        } else {
            *unlisted = (long long)r;
        }
        return true;
    }
    return false;
}

static bool is_binding_position(const SimInput *s, const SimBinding *b, int i)
{
    for (int j = 0; j < s->npositions; j++) {
        if (s->positions[j] == b->position) {
            return j == i;
        }
    }
    return !strcmp(s->positions[i], b->position);
}

typedef struct MBind {
    const SimBinding *b;
    const SimPortDevice *dev;
    int dev_index;
    int src;
    int recentre;
    SimRange src_range;
    int out, out2;
    bool latched;
    bool key;
    bool was_pressed;
} MBind;

typedef struct OutState {
    bool has_p, has_y, has_keys;
    double p;
    double y;
    int *held;
    int nheld;
    int last;
} OutState;

struct SimMerge {
    int nbind;
    MBind *bind;
    int nout;
    SimMergeOut *out;
    OutState *st;
    double *best;
};

static int out_index(SimMerge *m, const SimInput *control)
{
    for (int i = 0; i < m->nout; i++) {
        if (m->out[i].control == control) {
            return i;
        }
    }
    m->out = realloc(m->out, sizeof(*m->out) * (m->nout + 1));
    memset(&m->out[m->nout], 0, sizeof(*m->out));
    m->out[m->nout].control = control;
    m->out[m->nout].bound = true;
    m->out[m->nout].position = -1;
    return m->nout++;
}

static SimRange calibrated_range(const SimPortDevice *d, const SimInput *in)
{
    for (int i = 0; i < d->ncalibrated; i++) {
        if (d->calibrated[i].input == in) {
            return d->calibrated[i].range;
        }
    }
    return in->range;
}

static bool target_domain(const SimInput *t, SimRange *r)
{
    if (t->kind == SIM_IN_AXIS && t->has_range) {
        *r = t->range;
        return true;
    }
    if (t->kind == SIM_IN_BUTTON && t->analog) {
        *r = t->analog_range;
        return true;
    }
    return false;
}

SimMerge *sim_merge_new(const SimPort *port)
{
    SimMerge *m = calloc(1, sizeof(*m));
    if (!port || !port->target) {
        return m;
    }
    for (int d = 0; d < port->ndevices; d++) {
        const SimPortDevice *dev = &port->devices[d];
        if (!dev->usable) {
            continue;
        }
        for (int k = 0; k < dev->nbindings; k++) {
            const SimBinding *b = &dev->bindings[k];
            if (!b->source || !b->target) {
                continue;
            }
            m->bind = realloc(m->bind, sizeof(*m->bind) * (m->nbind + 1));
            MBind *mb = &m->bind[m->nbind++];
            memset(mb, 0, sizeof(*mb));
            mb->b = b;
            mb->dev = dev;
            mb->dev_index = d;
            mb->src = (int)(b->source - dev->inputs);
            mb->recentre = b->recentre ? (int)(b->recentre - dev->inputs) : -1;
            mb->src_range = b->source->has_range ? calibrated_range(dev, b->source) : b->source->range;
            mb->out = out_index(m, b->target);
            mb->out2 = b->target2 ? out_index(m, b->target2) : -1;
            mb->key = b->source->kind == SIM_IN_BUTTON && b->target->kind == SIM_IN_AXIS &&
                      (b->has_value || b->has_rate);
        }
    }
    m->best = calloc(m->nout ? m->nout : 1, sizeof(*m->best));
    m->st = calloc(m->nout ? m->nout : 1, sizeof(*m->st));
    for (int i = 0; i < m->nout; i++) {
        m->st[i].held = calloc(m->nbind ? m->nbind : 1, sizeof(int));
    }
    for (int k = 0; k < m->nbind; k++) {
        const SimBinding *b = m->bind[k].b;
        OutState *st = &m->st[m->bind[k].out];
        if (m->bind[k].key) {
            st->has_keys = true;
            st->has_p = st->has_p || b->has_rate;
            st->has_y = st->has_y || !b->has_rate;
        } else if (b->source->kind == SIM_IN_RELATIVE && b->integrate) {
            st->has_p = true;
        }
    }
    sim_merge_reset(m);
    return m;
}

void sim_merge_free(SimMerge *m)
{
    if (!m) {
        return;
    }
    for (int i = 0; i < m->nout; i++) {
        free(m->st[i].held);
    }
    free(m->st);
    free(m->bind);
    free(m->out);
    free(m->best);
    free(m);
}

void sim_merge_reset(SimMerge *m)
{
    for (int i = 0; i < m->nbind; i++) {
        m->bind[i].latched = false;
        m->bind[i].was_pressed = false;
    }
    for (int i = 0; i < m->nout; i++) {
        m->st[i].p = m->out[i].control->range.rest;
        m->st[i].y = 0.0;
        m->st[i].nheld = 0;
        m->st[i].last = -1;
    }
}

static bool button_pressed(const SimInput *in, double v)
{
    if (!isfinite(v)) {
        return false;
    }
    if (in->analog) {
        return sim_merge_normalise(in->analog_range, v) > 0.0;
    }
    return v != 0.0;
}

static void offer_axis(SimMerge *m, int i, double c)
{
    double mag = fabs(c);
    if (mag > m->best[i]) {
        m->best[i] = mag;
        m->out[i].canonical = c;
    }
}

static void offer_press(SimMerge *m, int i, double level)
{
    m->out[i].pressed = true;
    if (level > m->out[i].canonical) {
        m->out[i].canonical = level;
    }
}

static void axis_onto(SimMerge *m, const MBind *mb, int i, double c, bool src_bipolar, int which)
{
    const SimInput *t = m->out[i].control;
    SimRange tr;
    if (!target_domain(t, &tr)) {
        return;
    }
    bool tb = sim_merge_bipolar(tr);
    double v = c;
    if (mb->b->policy == SIM_POLICY_SPLIT) {
        v = sim_merge_policy(SIM_POLICY_SPLIT, src_bipolar, c, which);
    } else if (src_bipolar != tb) {
        v = sim_merge_policy(mb->b->policy, src_bipolar, c, 0);
    }
    if (t->kind == SIM_IN_BUTTON) {
        if (v > 0) {
            offer_press(m, i, v);
        }
        return;
    }
    offer_axis(m, i, v);
}

static double toward(double y, double goal, double step)
{
    if (y < goal) {
        return y + step >= goal ? goal : y + step;
    }
    if (y > goal) {
        return y - step <= goal ? goal : y - step;
    }
    return y;
}

static void keys_advance(SimMerge *m, int i, double dt_ms)
{
    OutState *st = &m->st[i];
    const SimRange tr = m->out[i].control->range;
    if (st->nheld > 0) {
        const SimBinding *d = m->bind[st->held[st->nheld - 1]].b;
        if (d->has_rate) {
            double lo = tr.min <= tr.max ? tr.min : tr.max, hi = tr.min <= tr.max ? tr.max : tr.min;
            st->p = clampd(st->p + d->rate * dt_ms * (tr.max - tr.min) / 1000.0, lo, hi);
        } else if (d->has_ramp && d->press_ms > 0) {
            st->y = toward(st->y, d->value, dt_ms * fabs(d->value) / d->press_ms);
        }
    } else if (st->last >= 0) {
        const SimBinding *d = m->bind[st->last].b;
        if (!d->has_rate && d->has_ramp && d->release_ms > 0) {
            st->y = toward(st->y, 0.0, dt_ms * fabs(d->value) / d->release_ms);
        }
    }
}

static void keys_settle(SimMerge *m, int i)
{
    OutState *st = &m->st[i];
    if (st->nheld > 0) {
        st->last = st->held[st->nheld - 1];
        const SimBinding *d = m->bind[st->last].b;
        if (!d->has_rate && !(d->has_ramp && d->press_ms > 0)) {
            st->y = d->value;
        }
    } else if (st->last >= 0) {
        const SimBinding *d = m->bind[st->last].b;
        if (!d->has_rate && !(d->has_ramp && d->release_ms > 0)) {
            st->y = 0.0;
        }
    }
    if (st->has_y) {
        offer_axis(m, i, st->y);
    }
}

void sim_merge_update(SimMerge *m, const double *const *values)
{
    sim_merge_update_dt(m, values, 0.0);
}

void sim_merge_update_dt(SimMerge *m, const double *const *values, double dt_ms)
{
    if (!(dt_ms > 0)) {
        dt_ms = 0.0;
    }
    for (int i = 0; i < m->nout; i++) {
        m->out[i].canonical = 0.0;
        m->out[i].pressed = false;
        m->out[i].has_reading = false;
        m->out[i].position = -1;
        m->out[i].raw = 0.0;
        m->best[i] = 0.0;
        if (m->st[i].has_keys) {
            keys_advance(m, i, dt_ms);
        }
    }
    for (int k = 0; k < m->nbind; k++) {
        MBind *mb = &m->bind[k];
        const SimBinding *b = mb->b;
        const SimInput *s = b->source;
        const SimInput *t = b->target;
        const double *vals = values ? values[mb->dev_index] : NULL;
        double rest = s->kind == SIM_IN_AXIS ? mb->src_range.rest : s->kind == SIM_IN_DETENT ? -1.0 : 0.0;
        double raw = vals ? vals[mb->src] : rest;
        if (!isfinite(raw)) {
            raw = rest;
        }
        switch (s->kind) {
        case SIM_IN_AXIS: {
            bool sb = sim_merge_bipolar(mb->src_range);
            double c = sim_merge_normalise(mb->src_range, raw);
            if (s->invert) {
                c = sb ? -c : 1.0 - c;
            }
            if (t->kind == SIM_IN_BUTTON && b->has_threshold) {
                double o = b->resp_invert ? (sb ? -c : 1.0 - c) : c;
                double th = b->threshold, hy = b->hysteresis;
                if (th >= 0) {
                    mb->latched = mb->latched ? o >= th - hy : o >= th;
                } else {
                    mb->latched = mb->latched ? o <= th + hy : o <= th;
                }
                if (mb->latched) {
                    offer_press(m, mb->out, 1.0);
                }
                break;
            }
            double o = sim_merge_response(b, c, sb, 0.0);
            axis_onto(m, mb, mb->out, o, sb, 0);
            if (mb->out2 >= 0) {
                axis_onto(m, mb, mb->out2, o, sb, 1);
            }
            break;
        }
        case SIM_IN_BUTTON: {
            bool now = button_pressed(s, raw);
            if (mb->key) {
                OutState *st = &m->st[mb->out];
                if (now && !mb->was_pressed) {
                    st->held[st->nheld++] = k;
                } else if (!now && mb->was_pressed) {
                    int n = 0;
                    for (int j = 0; j < st->nheld; j++) {
                        if (st->held[j] != k) {
                            st->held[n++] = st->held[j];
                        }
                    }
                    st->nheld = n;
                }
                mb->was_pressed = now;
                break;
            }
            if (now && t->kind != SIM_IN_AXIS) {
                offer_press(m, mb->out, 1.0);
            }
            break;
        }
        case SIM_IN_HAT: {
            unsigned bits = raw >= 0 && raw < 16 ? (unsigned)raw : 0u;
            unsigned want = 0;
            if (b->direction) {
                want = !strcmp(b->direction, "up") ? SIM_HAT_UP : !strcmp(b->direction, "right") ? SIM_HAT_RIGHT :
                       !strcmp(b->direction, "down") ? SIM_HAT_DOWN : !strcmp(b->direction, "left") ? SIM_HAT_LEFT : 0;
            }
            if (want && (bits & want)) {
                offer_press(m, mb->out, 1.0);
            }
            break;
        }
        case SIM_IN_DETENT: {
            int idx;
            long long unlisted = 0;
            bool reading = detent_decode(s, raw, &idx, &unlisted);
            if (t->kind == SIM_IN_DETENT) {
                SimMergeOut *o = &m->out[mb->out];
                if (reading && !o->has_reading) {
                    o->has_reading = true;
                    o->position = idx;
                    if (idx < 0) {
                        o->raw = (double)unlisted;
                    } else if (position_kind(s, idx) == 'I' && s->position_ints) {
                        o->raw = (double)s->position_ints[idx];
                    } else {
                        o->raw = 0.0;
                    }
                    o->canonical = o->raw;
                }
                break;
            }
            if (b->position && idx >= 0 && is_binding_position(s, b, idx)) {
                offer_press(m, mb->out, 1.0);
            }
            break;
        }
        case SIM_IN_RELATIVE: {
            if (!b->integrate || b->counts_per_travel <= 0 || t->kind != SIM_IN_AXIS) {
                break;
            }
            OutState *st = &m->st[mb->out];
            SimRange tr = t->range;
            double lo = tr.min <= tr.max ? tr.min : tr.max, hi = tr.min <= tr.max ? tr.max : tr.min;
            double d = (vals ? raw : 0.0) * (tr.max - tr.min) / b->counts_per_travel;
            if (b->resp_invert != s->invert) {
                d = -d;
            }
            st->p = clampd(st->p + d, lo, hi);
            if (mb->recentre >= 0 && vals && button_pressed(&mb->dev->inputs[mb->recentre], vals[mb->recentre])) {
                st->p = tr.rest;
            }
            break;
        }
        }
    }
    for (int i = 0; i < m->nout; i++) {
        SimMergeOut *o = &m->out[i];
        SimRange tr;
        if (m->st[i].has_keys) {
            keys_settle(m, i);
        }
        bool p_drives = false;
        if (m->st[i].has_p) {
            double pc = sim_merge_normalise(o->control->range, m->st[i].p);
            offer_axis(m, i, pc);
            p_drives = o->canonical == pc;
        }
        if (o->control->kind == SIM_IN_AXIS && target_domain(o->control, &tr)) {
            o->raw = sim_merge_round(p_drives ? m->st[i].p : sim_merge_denormalise(tr, o->canonical), tr.rest);
        } else if (o->control->kind == SIM_IN_BUTTON && o->control->analog) {
            tr = o->control->analog_range;
            o->raw = sim_merge_round(sim_merge_denormalise(tr, o->canonical), tr.rest);
        } else if (o->control->kind == SIM_IN_DETENT) {
            o->pressed = false;
        } else {
            o->raw = o->pressed ? 1.0 : 0.0;
            o->canonical = o->pressed ? 1.0 : 0.0;
        }
    }
}

int sim_merge_noutputs(const SimMerge *m)
{
    return m->nout;
}

const SimMergeOut *sim_merge_output(const SimMerge *m, int i)
{
    return i >= 0 && i < m->nout ? &m->out[i] : NULL;
}

const SimMergeOut *sim_merge_find(const SimMerge *m, const char *control_id)
{
    for (int i = 0; i < m->nout; i++) {
        if (m->out[i].control->id && !strcmp(m->out[i].control->id, control_id)) {
            return &m->out[i];
        }
    }
    return NULL;
}
