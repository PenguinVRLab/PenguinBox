/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "qemu/seqlock.h"
#include "ui/xemu-settings.h"
#include "ui/xemu-input.h"
#include "hw/xbox/nv2a/nv2a_int.h"

#include "hw/xbox/nv2a/pgraph/vr_padxr.h"
#include "hw/xbox/nv2a/pgraph/vr_padxr_profiles.h"
#include "hw/xbox/nv2a/pgraph/vr_worldproj_host.h"
#include "hw/xbox/nv2a/pgraph/vr_mods.h"

#define LOG_PREFIX "PenguinBox: PadXR: "
#define READ_TRIES 3
#define PRESS_RING 32
#define TELEMETRY_US (5 * G_USEC_PER_SEC)

static QemuSeqLock s_in_seq[PADXR_MAX_SEATS];
static PadxrInput s_in_pub[PADXR_MAX_SEATS];
static uint32_t s_in_gen[PADXR_MAX_SEATS];

void vr_padxr_publish_input(int seat, const PadxrInput *in)
{
    if (seat < 0 || seat >= PADXR_MAX_SEATS) {
        return;
    }
    PadxrInput p = *in;
    p.generation = ++s_in_gen[seat] ? s_in_gen[seat] : ++s_in_gen[seat];
    p.published_us = g_get_monotonic_time();
    p.stale = false;
    p.caps = padxr_caps_from_input(&p);
    seqlock_write_begin(&s_in_seq[seat]);
    s_in_pub[seat] = p;
    seqlock_write_end(&s_in_seq[seat]);
    if (p.generation == 1) {
        fprintf(stderr, LOG_PREFIX "input published (seat %d)\n", seat);
    }
}

bool vr_padxr_input(int seat, PadxrInput *out)
{
    PadxrInput copy;
    memset(&copy, 0, sizeof(copy));
    if (seat >= 0 && seat < PADXR_MAX_SEATS) {
        for (int i = 0; i < READ_TRIES; i++) {
            unsigned start = seqlock_read_begin(&s_in_seq[seat]);
            copy = s_in_pub[seat];
            if (!seqlock_read_retry(&s_in_seq[seat], start)) {
                goto read;
            }
        }
        memset(&copy, 0, sizeof(copy));
    }
read:
    padxr_input_read_rule(&copy, g_get_monotonic_time(), out);
    return !out->stale;
}

static QemuSeqLock s_port_seq[PADXR_MAX_PORTS];
static PadxrPortCells s_port_pub[PADXR_MAX_PORTS];

typedef struct PressRing {
    uint32_t head, tail;
    struct { uint8_t bit; uint32_t count; } e[PRESS_RING];
} PressRing;
static PressRing s_ring[PADXR_MAX_PORTS];

static PadxrEdgeSched s_sched[PADXR_MAX_PORTS];
static uint64_t s_transactions[PADXR_MAX_PORTS];

static int s_armed;

static const PadxrRange *const k_gp_ranges[CONTROLLER_AXIS__COUNT] = {
    [CONTROLLER_AXIS_LTRIG] = &PADXR_RANGE_U15,
    [CONTROLLER_AXIS_RTRIG] = &PADXR_RANGE_U15,
    [CONTROLLER_AXIS_LSTICK_X] = &PADXR_RANGE_S16,
    [CONTROLLER_AXIS_LSTICK_Y] = &PADXR_RANGE_S16,
    [CONTROLLER_AXIS_RSTICK_X] = &PADXR_RANGE_S16,
    [CONTROLLER_AXIS_RSTICK_Y] = &PADXR_RANGE_S16,
};

static const PadxrRange *const k_sbc_ranges[SBC_AXIS__COUNT] = {
    [SBC_AXIS_AIMING_X] = &PADXR_RANGE_S16,
    [SBC_AXIS_AIMING_Y] = &PADXR_RANGE_S16,
    [SBC_AXIS_ROTATION_LEVER] = &PADXR_RANGE_S16,
    [SBC_AXIS_LEFT_PEDAL] = &PADXR_RANGE_U15,
    [SBC_AXIS_MIDDLE_PEDAL] = &PADXR_RANGE_U15,
    [SBC_AXIS_RIGHT_PEDAL] = &PADXR_RANGE_U15,
    [SBC_AXIS_SIGHT_CHANGE_X] = &PADXR_RANGE_S16,
    [SBC_AXIS_SIGHT_CHANGE_Y] = &PADXR_RANGE_S16,
};

static bool ring_empty(PressRing *r)
{
    return qatomic_read(&r->head) == qatomic_read(&r->tail);
}

static void ring_push(PressRing *r, uint8_t bit, uint32_t count)
{
    uint32_t h = qatomic_read(&r->head);
    if (h - qatomic_load_acquire(&r->tail) >= PRESS_RING) {
        return;
    }
    r->e[h % PRESS_RING].bit = bit;
    r->e[h % PRESS_RING].count = count;
    qatomic_store_release(&r->head, h + 1);
}

static bool port_transaction(int port, PadxrPortCells *cells, uint64_t *down)
{
    if (port < 0 || port >= PADXR_MAX_PORTS) {
        return false;
    }
    qatomic_set(&s_transactions[port], s_transactions[port] + 1);
    PressRing *r = &s_ring[port];
    if (qatomic_read(&s_armed) == 0 && ring_empty(r) &&
        padxr_sched_idle(&s_sched[port])) {
        return false;
    }
    uint32_t t = qatomic_read(&r->tail);
    while (t != qatomic_load_acquire(&r->head)) {
        padxr_sched_press(&s_sched[port], r->e[t % PRESS_RING].bit,
                          r->e[t % PRESS_RING].count);
        qatomic_store_release(&r->tail, ++t);
    }
    *down = padxr_sched_step(&s_sched[port]);
    memset(cells, 0, sizeof(*cells));
    for (int i = 0; i < READ_TRIES; i++) {
        unsigned start = seqlock_read_begin(&s_port_seq[port]);
        *cells = s_port_pub[port];
        if (!seqlock_read_retry(&s_port_seq[port], start)) {
            return true;
        }
    }
    memset(cells, 0, sizeof(*cells));
    return true;
}

void vr_padxr_merge_gamepad(int port, uint16_t *buttons, int16_t *axis,
                            int n_axis)
{
    PadxrPortCells cells;
    uint64_t down = 0;
    if (!port_transaction(port, &cells, &down)) {
        return;
    }
    uint64_t b = *buttons;
    padxr_merge_port(&cells, down, &b, axis,
                     MIN(n_axis, CONTROLLER_AXIS__COUNT), k_gp_ranges);
    *buttons = (uint16_t)b;
}

void vr_padxr_merge_sbc(int port, uint64_t *buttons, int16_t *axis, int n_axis,
                        uint8_t *gear, uint8_t *tuner, uint8_t *toggles)
{
    (void)gear;
    (void)tuner;
    (void)toggles;
    PadxrPortCells cells;
    uint64_t down = 0;
    if (!port_transaction(port, &cells, &down)) {
        return;
    }
    padxr_merge_port(&cells, down, buttons, axis,
                     MIN(n_axis, SBC_AXIS__COUNT), k_sbc_ranges);
}

uint64_t vr_padxr_transactions(int port)
{
    return (port >= 0 && port < PADXR_MAX_PORTS)
               ? qatomic_read(&s_transactions[port]) : 0;
}

static uint64_t s_cues_dropped;

void vr_padxr_cue(int seat, const PadxrCue *cue)
{
    (void)seat;
    if (!padxr_cue_valid(cue)) {
        return;
    }
    if (qatomic_fetch_inc(&s_cues_dropped) == 0) {
        fprintf(stderr, LOG_PREFIX "no haptics backend on this build; "
                        "cues are dropped\n");
    }
}

static PadxrSeat s_seat[PADXR_MAX_SEATS];
static bool s_seat_ready;
static int64_t s_title = -2;
static int64_t s_next_telemetry_us;

static void seat_log(void *ctx, const char *line)
{
    (void)ctx;
    fprintf(stderr, "PenguinBox: %s\n", line);
}

typedef struct GuestMem { const uint8_t *ram; uint64_t size; } GuestMem;

static bool guest_read(void *ctx, uint32_t addr, int width, uint32_t *v)
{
    const GuestMem *g = ctx;
    if (!g->ram || (uint64_t)addr + (uint64_t)width > g->size) {
        return false;
    }
    *v = 0;
    memcpy(v, g->ram + addr, (size_t)width);
    return true;
}

static bool mod_armed(void *ctx, const char *slug)
{
    (void)ctx;
    return vr_mods_armed(slug);
}

static bool cfg_allows(int cfg)
{
    switch (cfg) {
    case PADXR_CFG_SBC_PANEL:
        return g_config.vr.sbc_panel_enable;
    case PADXR_CFG_GLOVES:
        return false;
    default:
        return true;
    }
}

static void arm_for_title(int64_t title, uint32_t caps)
{
    PadxrSeat *s = &s_seat[0];
    if (padxr_seat_any_armed(s)) {
        qatomic_sub(&s_armed, s->n);
        padxr_seat_disarm_all(s, PADXR_DISARM_PROFILE);
    }
    const PadxrProfileRow *row = padxr_profile_for_title(title);
    if (!row) {
        fprintf(stderr, LOG_PREFIX "title 0x%08" PRIx32 ": no profile row, "
                "no member armed\n", (uint32_t)title);
        return;
    }
    for (int i = 0; i < PADXR_ROW_MEMBERS && row->members[i].id; i++) {
        const char *id = row->members[i].id;
        const PadxrMemberEntry *e = padxr_member_find(id);
        if (!e) {
            fprintf(stderr, LOG_PREFIX "%s: unknown member, skipped\n", id);
            continue;
        }
        if (!cfg_allows(row->members[i].cfg)) {
            fprintf(stderr, LOG_PREFIX "%s not armed: its setting is off\n", id);
            continue;
        }
        const PadxrMemberOps only = { e->desc, NULL, NULL, NULL, NULL };
        padxr_seat_arm(s, &only, NULL, NULL, 0, caps);
    }
    qatomic_set(&s_armed, s->n);
    fprintf(stderr, LOG_PREFIX "title 0x%08" PRIx32 " (%s): %d member(s) "
            "armed\n", (uint32_t)title, row->title, s->n);
}

static void publish_ports(const PadxrOutputs *out)
{
    for (int p = 0; p < PADXR_MAX_PORTS; p++) {
        PadxrPortCells c;
        padxr_port_cells_from_out(&out->port[p], &c);
        seqlock_write_begin(&s_port_seq[p]);
        s_port_pub[p] = c;
        seqlock_write_end(&s_port_seq[p]);
        for (int i = 0; i < out->port[p].n_press; i++) {
            ring_push(&s_ring[p], out->port[p].press[i].bit,
                      out->port[p].press[i].count);
        }
    }
    for (int i = 0; i < out->n_cue; i++) {
        vr_padxr_cue(0, &out->cue[i]);
    }
}

static void telemetry(int64_t now)
{
    const PadxrSeat *s = &s_seat[0];
    if (!padxr_seat_any_armed(s) || now < s_next_telemetry_us) {
        return;
    }
    s_next_telemetry_us = now + TELEMETRY_US;
    for (int i = 0; i < s->n; i++) {
        const PadxrMemberStatus *st = &s->slot[i].st;
        fprintf(stderr, LOG_PREFIX "%s: gate %s, ticks %" PRIu64 ", presses %"
                PRIu64 ", cues %" PRIu64 ", refusals %" PRIu64
                ", transactions p0 %" PRIu64 "\n", s->slot[i].ops->desc->id,
                st->gated_open ? "open" : "closed", st->ticks, st->presses,
                st->cues_sent, st->refusals, vr_padxr_transactions(0));
    }
}

void vr_padxr_frame(struct PGRAPHState *pg)
{
    if (!s_seat_ready) {
        for (int i = 0; i < PADXR_MAX_SEATS; i++) {
            padxr_seat_init(&s_seat[i], 1, seat_log, NULL);
        }
        s_seat_ready = true;
    }
    const int64_t now = g_get_monotonic_time();
    PadxrInput in;
    vr_padxr_input(0, &in);

    const int64_t title = vr_wp_host_title_id();
    if (title != s_title) {
        s_title = title;
        arm_for_title(title, in.caps);
    }
    if (!padxr_seat_any_armed(&s_seat[0])) {
        return;
    }
    NV2AState *d = container_of(pg, NV2AState, pgraph);
    GuestMem gm = { d->vram_ptr, memory_region_size(d->vram) };
    const PadxrGameMem mem = { guest_read, mod_armed, &gm };
    static PadxrOutputs out;
    padxr_seat_tick(&s_seat[0], &in, &mem, now, &out);
    publish_ports(&out);
    telemetry(now);
}
