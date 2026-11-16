/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include "vr_padxr_core.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

const PadxrRange PADXR_RANGE_S16 = { -32768, 32767, 32767, false };
const PadxrRange PADXR_RANGE_U15 = { 0, 32767, 32767, true };
const PadxrRange PADXR_RANGE_U8  = { 0, 255, 127, false };

static const char *const k_claim[PADXR_CLAIM__COUNT] = {
    "none", "shared", "tee", "exclusive", "add", "set", "own" };
static const char *const k_res[PADXR_RES__COUNT] = {
    "mic", "layers", "haptics", "rightStick", "leftStick", "speed" };
static const char *const k_family[PADXR_FAM__COUNT] = {
    "PAD", "PER", "RF", "STREAM", "HOST", "CAM" };
static const char *const k_route[PADXR_ROUTE__COUNT] = {
    "merge", "adapter", "device", "stream", "host", "none", "camera" };
static const char *const k_out[PADXR_OUT__COUNT] = {
    "press", "hold", "schedule", "sequence", "axis", "device", "stream",
    "host", "display", "camera" };

#define NAME_OF(tab, n, i) ((i) >= 0 && (i) < (n) ? (tab)[i] : "?")

const char *padxr_claim_name(int c)    { return NAME_OF(k_claim, PADXR_CLAIM__COUNT, c); }
const char *padxr_res_name(int r)
{
    if (r == PADXR_CLASH_BUTTONS) {
        return "buttons";
    }
    return NAME_OF(k_res, PADXR_RES__COUNT, r);
}
const char *padxr_family_name(int f)   { return NAME_OF(k_family, PADXR_FAM__COUNT, f); }
const char *padxr_route_name(int r)    { return NAME_OF(k_route, PADXR_ROUTE__COUNT, r); }
const char *padxr_out_kind_name(int k) { return NAME_OF(k_out, PADXR_OUT__COUNT, k); }

bool padxr_needs_met(const PadxrMemberDesc *d, uint32_t caps, uint32_t *missing)
{
    for (int i = 0; i < 4; i++) {
        if (d->needs[i] != 0 && (d->needs[i] & caps) == 0) {
            if (missing) {
                *missing = d->needs[i];
            }
            return false;
        }
    }
    return true;
}

bool padxr_claim_pair_compatible(int a, int b)
{
    if (a == PADXR_CLAIM_NONE || b == PADXR_CLAIM_NONE) {
        return true;
    }
    if (a == PADXR_CLAIM_EXCLUSIVE || b == PADXR_CLAIM_EXCLUSIVE) {
        return false;
    }
#ifndef PADXR_PLANTED_TWO_OWN
    if (a == PADXR_CLAIM_OWN && b == PADXR_CLAIM_OWN) {
        return false;
    }
#endif
    return true;
}

bool padxr_claims_compatible(const PadxrMemberDesc *a, const PadxrMemberDesc *b,
                             int *clash)
{
    if (a->harness || b->harness) {
        return true;
    }
    for (int i = 0; i < PADXR_RES__COUNT; i++) {
        if (i == PADXR_RES_LAYERS) {
            continue;
        }
        if (!padxr_claim_pair_compatible(a->res[i], b->res[i])) {
            if (clash) {
                *clash = i;
            }
            return false;
        }
    }
#ifndef PADXR_PLANTED_TWO_OWN_BTN
    if (a->own_pad != PADXR_PAD_NONE && a->own_pad == b->own_pad &&
        (a->own_buttons & b->own_buttons) != 0) {
        if (clash) {
            *clash = PADXR_CLASH_BUTTONS;
        }
        return false;
    }
#endif
    return true;
}

bool padxr_layers_fit(unsigned claimed, const PadxrMemberDesc *d,
                      unsigned budget)
{
    return claimed + d->layers <= budget;
}

int padxr_manifest_line(const PadxrMemberDesc *d, char *buf, size_t n)
{
    return snprintf(buf, n, "%s\t%s\t%s\t%s\t%s", d->id, d->kind,
                    padxr_family_name(d->family), padxr_route_name(d->route),
                    padxr_out_kind_name(d->out_kind));
}

static int32_t clamp_i(int64_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : (int32_t)v);
}

static float clean(float v, float lo, float hi)
{
    if (!isfinite(v)) {
        return 0.0f;
    }
    return v < lo ? lo : (v > hi ? hi : v);
}

int32_t padxr_axis_value(const PadxrRange *r, int claim, float v)
{
    if (claim == PADXR_CLAIM_ADD) {
        return (int32_t)lround(clean(v, -1.0f, 1.0f) * (float)r->add_scale);
    }
    if (r->unipolar) {
        double span = (double)r->hi - (double)r->lo;
        return clamp_i((int64_t)r->lo + lround(clean(v, 0.0f, 1.0f) * span),
                       r->lo, r->hi);
    }
    double half = ((double)r->hi - (double)r->lo) / 2.0;
    return clamp_i((int64_t)r->lo +
                   lround(((double)clean(v, -1.0f, 1.0f) + 1.0) * half),
                   r->lo, r->hi);
}

int32_t padxr_axis_merge(const PadxrRange *r, int32_t real, int claim, float v)
{
    switch (claim) {
    case PADXR_CLAIM_ADD:
        return clamp_i((int64_t)real + padxr_axis_value(r, claim, v),
                       r->lo, r->hi);
    case PADXR_CLAIM_SET:
    case PADXR_CLAIM_OWN:
        return padxr_axis_value(r, claim, v);
    default:
        return real;
    }
}

void padxr_axis_cell_write(PadxrAxisCell *c, const PadxrRange *r, int claim,
                           float v)
{
    switch (claim) {
    case PADXR_CLAIM_OWN:
        if (!c->has_own) {
            c->has_own = true;
            c->own = padxr_axis_value(r, claim, v);
        }
        break;
    case PADXR_CLAIM_SET:
        if (!c->has_set) {
            c->has_set = true;
            c->set = padxr_axis_value(r, claim, v);
        }
        break;
    case PADXR_CLAIM_ADD:
        if (isfinite(v)) {
            c->add += padxr_axis_value(r, claim, v);
        }
        break;
    default:
        break;
    }
}

bool padxr_axis_cell_empty(const PadxrAxisCell *c)
{
    return !c->has_own && !c->has_set && c->add == 0;
}

int32_t padxr_axis_cell_apply(const PadxrAxisCell *c, const PadxrRange *r,
                              int32_t real)
{
    if (c->has_own) {
        return c->own;
    }
    if (c->has_set) {
        return c->set;
    }
    if (c->add == 0) {
        return real;
    }
    return clamp_i((int64_t)real + c->add, r->lo, r->hi);
}

uint64_t padxr_merge_buttons(uint64_t real, const PadxrButtonCell *c)
{
#ifdef PADXR_PLANTED_LAST_WRITER
    if (c->press || c->own_mask) {
        return c->press | (c->own_mask & c->own_down);
    }
    return real;
#else
    uint64_t w = real | c->press;
    return (w & ~c->own_mask) | (c->own_mask & c->own_down);
#endif
}

void padxr_sched_press(PadxrEdgeSched *s, int bit, uint32_t count)
{
    if (bit < 0 || bit >= PADXR_MAX_BUTTONS) {
        return;
    }
    s->remaining[bit] = count;
    if (count) {
        s->fresh |= 1ull << bit;
    }
}

void padxr_sched_reset(PadxrEdgeSched *s)
{
    memset(s, 0, sizeof(*s));
}

uint64_t padxr_sched_step(PadxrEdgeSched *s)
{
    uint64_t down = 0;
    for (int bit = 0; bit < PADXR_MAX_BUTTONS; bit++) {
        if (s->remaining[bit] == 0) {
            continue;
        }
        uint64_t b = 1ull << bit;
#ifndef PADXR_PLANTED_NO_RELEASE_FIRST
        if ((s->fresh & b) && (s->down_last & b)) {
            continue;
        }
#endif
        down |= b;
        s->remaining[bit]--;
    }
    s->fresh = 0;
    s->down_last = down;
    return down;
}

bool padxr_sched_idle(const PadxrEdgeSched *s)
{
    for (int bit = 0; bit < PADXR_MAX_BUTTONS; bit++) {
        if (s->remaining[bit]) {
            return false;
        }
    }
    return true;
}

bool padxr_cue_valid(const PadxrCue *c)
{
    return c->hand < 2 && isfinite(c->amplitude) && c->amplitude > 0.0f &&
           c->ms > 0;
}

bool padxr_cue_stronger(const PadxrCue *a, const PadxrCue *b)
{
    if (b->amplitude != a->amplitude) {
        return b->amplitude > a->amplitude;
    }
    return b->ms > a->ms;
}

size_t padxr_glove_encode_v2(uint8_t out[8], int hand, uint8_t spot_mask,
                             int intensity, int ms)
{
    intensity = intensity < 0 ? 0 : (intensity > 100 ? 100 : intensity);
    ms = ms < 1 ? 1 : (ms > 2000 ? 2000 : ms);
#ifdef PADXR_PLANTED_V1
    (void)spot_mask;
    out[0] = 1;
    out[2] = 0;
#else
    out[0] = 2;
    out[2] = spot_mask ? spot_mask : PADXR_LOCUS_HAND;
#endif
    out[1] = hand == 1 ? 1 : 0;
    out[3] = (uint8_t)intensity;
    out[4] = (uint8_t)(ms & 0xFF);
    out[5] = (uint8_t)(ms >> 8);
    out[6] = 0;
    out[7] = 0;
    return 8;
}

size_t padxr_glove_encode_cue(uint8_t out[8], const PadxrCue *c)
{
    float a = clean(c->amplitude, 0.0f, 1.0f);
    return padxr_glove_encode_v2(out, c->hand, c->locus,
                                 (int)lroundf(a * 100.0f), c->ms);
}

uint32_t padxr_caps_from_input(const PadxrInput *in)
{
    uint32_t caps = PADXR_NEED_PAD;
    if (in->generation == 0 || in->stale) {
        return caps;
    }
    if (in->head.valid) {
        caps |= PADXR_NEED_HEAD_POSE | PADXR_NEED_HEAD_6DOF;
    }
    bool l = in->hands[0].aim.valid, r = in->hands[1].aim.valid;
    if (in->actions_active && (l || r)) {
        caps |= PADXR_NEED_CONTROLLERS;
    }
    if (in->actions_active && l && r) {
        caps |= PADXR_NEED_TWO_HANDS;
    }
    return caps;
}

void padxr_input_read_rule(const PadxrInput *pub, int64_t now_us,
                           PadxrInput *out)
{
    *out = *pub;
    bool stale = pub->generation == 0;
#ifdef PADXR_PLANTED_NO_STALE
    (void)now_us;
#else
    stale = stale || now_us - pub->published_us > PADXR_STALE_US ||
            now_us < pub->published_us - PADXR_STALE_US;
#endif
    if (stale) {
        uint32_t gen = pub->generation;
        int64_t at = pub->published_us;
        memset(out, 0, sizeof(*out));
        out->generation = gen;
        out->published_us = at;
        out->stale = true;
    }
    out->caps = padxr_caps_from_input(out);
}

bool padxr_gate_eval(const PadxrGate *g, const PadxrGameMem *mem)
{
    if (g->kind == PADXR_GATE_NONE) {
        return true;
    }
    if (g->kind == PADXR_GATE_MOD) {
        return mem && mem->mod_armed && g->mod_slug &&
               mem->mod_armed(mem->ctx, g->mod_slug);
    }
    if (g->kind != PADXR_GATE_MEM || !mem || !mem->read ||
        (g->width != 1 && g->width != 2 && g->width != 4)) {
        return false;
    }
    uint32_t addr = g->addr, v = 0;
    for (int i = 0; i < g->n_chain && i < 4; i++) {
        uint32_t p = 0;
        if (!mem->read(mem->ctx, addr, 4, &p) || p == 0) {
#ifdef PADXR_PLANTED_GATE_FAILOPEN
            return true;
#else
            return false;
#endif
        }
        addr = p + (uint32_t)g->chain[i];
    }
    if (!mem->read(mem->ctx, addr, g->width, &v)) {
#ifdef PADXR_PLANTED_GATE_FAILOPEN
        return true;
#else
        return false;
#endif
    }
    uint32_t m = g->mask ? g->mask : 0xFFFFFFFFu;
    bool eq = (v & m) == (g->value & m);
    return g->not_equals ? !eq : eq;
}

static PadxrPortOut *port_of(PadxrOutputs *o, int port)
{
    return (port >= 0 && port < PADXR_MAX_PORTS) ? &o->port[port] : NULL;
}

void padxr_out_press(PadxrOutputs *o, int port, int bit, uint32_t count)
{
    PadxrPortOut *p = port_of(o, port);
    if (!p || bit < 0 || bit >= PADXR_MAX_BUTTONS ||
        p->n_press >= PADXR_MAX_PRESSES) {
        o->refused++;
        return;
    }
    p->press[p->n_press].bit = (uint8_t)bit;
    p->press[p->n_press].count = count;
    p->n_press++;
}

void padxr_out_hold(PadxrOutputs *o, int port, int bit)
{
    PadxrPortOut *p = port_of(o, port);
    if (!p || bit < 0 || bit >= PADXR_MAX_BUTTONS) {
        o->refused++;
        return;
    }
    p->hold |= 1ull << bit;
}

void padxr_out_own(PadxrOutputs *o, int port, int bit, bool down)
{
    PadxrPortOut *p = port_of(o, port);
    if (!p || bit < 0 || bit >= PADXR_MAX_BUTTONS || !o->writer ||
        !(o->writer->own_buttons & (1ull << bit))) {
        o->refused++;
        return;
    }
    uint64_t b = 1ull << bit;
    if (p->buttons.own_mask & b) {
        return;
    }
    p->buttons.own_mask |= b;
    if (down) {
        p->buttons.own_down |= b;
    }
}

void padxr_out_axis(PadxrOutputs *o, int port, int axis, const PadxrRange *r,
                    int claim, float v)
{
    PadxrPortOut *p = port_of(o, port);
    if (!p || axis < 0 || axis >= PADXR_MAX_AXES || !o->writer) {
        o->refused++;
        return;
    }
    padxr_axis_cell_write(&p->axis[axis], r, claim, v);
}

void padxr_out_cue(PadxrOutputs *o, const PadxrCue *c)
{
    if (!padxr_cue_valid(c) || o->n_cue >= PADXR_MAX_CUES) {
        o->refused++;
        return;
    }
    o->cue[o->n_cue++] = *c;
}

static void logf_seat(PadxrSeat *s, const char *fmt, const char *a,
                      const char *b)
{
    if (s->log) {
        char line[160];
        snprintf(line, sizeof(line), fmt, a, b);
        s->log(s->log_ctx, line);
    }
}

void padxr_seat_init(PadxrSeat *s, unsigned layer_budget, PadxrLog log,
                     void *log_ctx)
{
    memset(s, 0, sizeof(*s));
    s->layer_budget = layer_budget;
    s->log = log;
    s->log_ctx = log_ctx;
}

int padxr_seat_arm(PadxrSeat *s, const PadxrMemberOps *ops, void *self,
                   const PadxrGate *gates, int n_gates, uint32_t caps)
{
    const PadxrMemberDesc *d = ops ? ops->desc : NULL;
    if (!d) {
        return -1;
    }
    if (s->n >= PADXR_MAX_MEMBERS || n_gates > PADXR_MAX_GATES) {
        logf_seat(s, "PadXR: %s not armed: %s", d->id, "no slot");
        return -1;
    }
    uint32_t missing = 0;
    if (!padxr_needs_met(d, caps, &missing)) {
        char why[32];
        snprintf(why, sizeof(why), "need 0x%x unmet", (unsigned)missing);
        logf_seat(s, "PadXR: %s not armed: %s", d->id, why);
        return -1;
    }
    if (d->online == PADXR_ONLINE_OFFLINE && s->online) {
        logf_seat(s, "PadXR: %s not armed: %s", d->id, "offline member, online");
        return -1;
    }
    unsigned layers = 0;
    for (int i = 0; i < s->n; i++) {
        const PadxrMemberDesc *o = s->slot[i].ops->desc;
        int clash = -1;
        if (!padxr_claims_compatible(o, d, &clash)) {
            char why[64];
            snprintf(why, sizeof(why), "%s clashes with %s",
                     padxr_res_name(clash), o->id);
            logf_seat(s, "PadXR: %s not armed: %s", d->id, why);
            return -1;
        }
        layers += o->layers;
    }
    if (!padxr_layers_fit(layers, d, s->layer_budget)) {
        logf_seat(s, "PadXR: %s not armed: %s", d->id, "layer budget");
        return -1;
    }
    const char *why = NULL;
    if (!ops->arm || !ops->tick) {
        logf_seat(s, "PadXR: %s not armed: %s", d->id,
                  "not built on this core");
        return -1;
    }
    if (!ops->arm(self, &why)) {
        logf_seat(s, "PadXR: %s not armed: %s", d->id, why ? why : "arm failed");
        return -1;
    }
    PadxrSlot *sl = &s->slot[s->n];
    memset(sl, 0, sizeof(*sl));
    sl->ops = ops;
    sl->self = self;
    for (int i = 0; i < n_gates; i++) {
        sl->gates[i] = gates[i];
    }
    sl->n_gates = (uint8_t)n_gates;
    sl->st.armed = true;
    logf_seat(s, "PadXR: %s armed (%s)", d->id, d->kind);
    return s->n++;
}

void padxr_seat_disarm_all(PadxrSeat *s, int reason)
{
    for (int i = 0; i < s->n; i++) {
        PadxrSlot *sl = &s->slot[i];
        if (sl->ops->disarm) {
            sl->ops->disarm(sl->self, reason);
        }
        logf_seat(s, "PadXR: %s disarmed%s", sl->ops->desc->id, "");
    }
    s->n = 0;
}

void padxr_seat_state_loaded(PadxrSeat *s)
{
    for (int i = 0; i < s->n; i++) {
        if (s->slot[i].ops->state_loaded) {
            s->slot[i].ops->state_loaded(s->slot[i].self);
        }
    }
}

bool padxr_seat_any_armed(const PadxrSeat *s)
{
    return s->n > 0;
}

static bool session_open(const PadxrSeat *s, const PadxrMemberDesc *d,
                         const PadxrInput *in)
{
    uint8_t g = d->session_gates;
    if ((g & PADXR_SG_LIVE) && in->generation == 0) {
        return false;
    }
    if ((g & PADXR_SG_ACTIONS) && !in->actions_active) {
        return false;
    }
    if ((g & PADXR_SG_NOT_STALE) && in->stale) {
        return false;
    }
    if ((g & PADXR_SG_ONLINE_LOCK) && d->online == PADXR_ONLINE_OFFLINE &&
        s->online) {
        return false;
    }
    return true;
}

void padxr_seat_tick(PadxrSeat *s, const PadxrInput *in,
                     const PadxrGameMem *mem, int64_t wall_us,
                     PadxrOutputs *out)
{
    memset(out, 0, sizeof(*out));
    s->tick++;
    PadxrTickCtx ctx = { in, s->tick, wall_us, mem };
    for (int i = 0; i < s->n; i++) {
        PadxrSlot *sl = &s->slot[i];
        bool open = session_open(s, sl->ops->desc, in);
        for (int g = 0; open && g < sl->n_gates; g++) {
            open = padxr_gate_eval(&sl->gates[g], mem);
        }
#ifdef PADXR_PLANTED_UNGATED_TICK
        open = true;
#endif
        if (open != sl->st.gated_open) {
            sl->st.gated_open = open;
            logf_seat(s, "PadXR: %s gate %s", sl->ops->desc->id,
                      open ? "open" : "closed");
        }
        if (!open) {
            continue;
        }
        uint32_t refused0 = out->refused;
        uint8_t cues0 = out->n_cue;
        unsigned presses0 = 0;
        for (int p = 0; p < PADXR_MAX_PORTS; p++) {
            presses0 += out->port[p].n_press;
        }
        out->writer = sl->ops->desc;
        sl->ops->tick(sl->self, &ctx, out);
        out->writer = NULL;
        unsigned presses1 = 0;
        for (int p = 0; p < PADXR_MAX_PORTS; p++) {
            presses1 += out->port[p].n_press;
        }
        sl->st.ticks++;
        sl->st.presses += presses1 - presses0;
        sl->st.cues_sent += (uint64_t)(out->n_cue - cues0);
        sl->st.refusals += out->refused - refused0;
    }
}

void padxr_port_cells_from_out(const PadxrPortOut *o, PadxrPortCells *c)
{
    memset(c, 0, sizeof(*c));
    c->buttons = o->buttons;
    c->buttons.press |= o->hold;
    bool live = c->buttons.press || c->buttons.own_mask;
    for (int i = 0; i < PADXR_MAX_AXES; i++) {
        c->axis[i] = o->axis[i];
        live = live || !padxr_axis_cell_empty(&o->axis[i]);
    }
    c->live = live;
}

void padxr_merge_port(const PadxrPortCells *c, uint64_t sched_down,
                      uint64_t *buttons, int16_t *axis, int n_axis,
                      const PadxrRange *const *ranges)
{
    if ((!c || !c->live) && sched_down == 0) {
#ifdef PADXR_PLANTED_MERGE_AXIS0
        if (n_axis > 0) {
            axis[0] = 0;
        }
#endif
        return;
    }
    PadxrButtonCell b = { 0 };
    if (c && c->live) {
        b = c->buttons;
    }
    b.press |= sched_down;
    *buttons = padxr_merge_buttons(*buttons, &b);
    if (!c || !c->live) {
        return;
    }
    for (int i = 0; i < n_axis && i < PADXR_MAX_AXES; i++) {
        if (ranges[i] && !padxr_axis_cell_empty(&c->axis[i])) {
            axis[i] = (int16_t)padxr_axis_cell_apply(&c->axis[i], ranges[i],
                                                     axis[i]);
        }
    }
}
