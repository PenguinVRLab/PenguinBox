/*
 * Copyright (c) 2026 Patrick Carey
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
#include "qemu/timer.h"
#include "qemu/atomic.h"
#include "phase_timers.h"

int nv2a_phase_timers_state = -1;

static struct PhaseTimers {
    int64_t vcpu_blocked_ns;
    int64_t vcpu_dl_ns;
    int64_t vcpu_dl_count;

    int64_t pfifo_idle_ns;
    int64_t dl_proc_ns;
    int64_t present_ns;

    int64_t pf_leaf_ns[PF_LEAF__COUNT];
    int64_t pf_method_ns;
    int64_t pf_child_ns;

    int64_t pf_msub_ns[PF_MSUB__COUNT];
    int64_t pf_psub_ns[PF_PSUB__COUNT];

    int      pf_leaf_depth;
    int64_t  pf_pusher_child_snap;

    int      pf_msub_active;
    int      pf_msub_cur;
    int64_t  pf_msub_last_ns;
    int64_t  pf_msub_child_snap;

    int64_t last_flip_ns;

    int64_t snap_blocked_ns;
    int64_t snap_dl_ns;
    int64_t snap_dl_count;
    int64_t snap_pfifo_idle_ns;
    int64_t snap_dl_proc_ns;
    int64_t snap_present_ns;
    int64_t snap_pf_leaf_ns[PF_LEAF__COUNT];
    int64_t snap_pf_method_ns;
    int64_t snap_pf_msub_ns[PF_MSUB__COUNT];
    int64_t snap_pf_psub_ns[PF_PSUB__COUNT];

    unsigned win_frames;
    int64_t  win_wall_ns;
    int64_t  win_min_ns;
    int64_t  win_max_ns;
    unsigned win_light;
    unsigned win_heavy;
} s;

#define PHASE_HEAVY_NS ((int64_t)PHASE_HEAVY_MS * 1000000)

bool nv2a_phase_timers_probe(void)
{
    int e = getenv("XEMU_PHASE_TIMERS") ? 1 : 0;
    qatomic_set(&nv2a_phase_timers_state, e);
    if (e) {
        fprintf(stderr, "phase: XEMU_PHASE_TIMERS on — per-60-flip frame-phase "
                        "decomposition enabled\n");
    }
    return e == 1;
}

int64_t nv2a_phase_now_ns(void)
{
    return qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
}

void nv2a_phase_add_dl_wait(int64_t start_ns)
{
    if (!nv2a_phase_timers_on()) {
        return;
    }
    int64_t d = nv2a_phase_now_ns() - start_ns;

    qatomic_set_i64(&s.vcpu_dl_ns, qatomic_read_i64(&s.vcpu_dl_ns) + d);
    qatomic_set_i64(&s.vcpu_dl_count, qatomic_read_i64(&s.vcpu_dl_count) + 1);
    qatomic_set_i64(&s.vcpu_blocked_ns,
                    qatomic_read_i64(&s.vcpu_blocked_ns) + d);
}

void nv2a_phase_add_fifo_wait(int64_t start_ns)
{
    if (!nv2a_phase_timers_on()) {
        return;
    }
    int64_t d = nv2a_phase_now_ns() - start_ns;

    qatomic_set_i64(&s.vcpu_blocked_ns,
                    qatomic_read_i64(&s.vcpu_blocked_ns) + d);
}

void nv2a_phase_add_pfifo_idle(int64_t start_ns)
{
    if (!nv2a_phase_timers_on()) {
        return;
    }
    s.pfifo_idle_ns += nv2a_phase_now_ns() - start_ns;
}

void nv2a_phase_add_dl_proc(int64_t start_ns)
{
    if (!nv2a_phase_timers_on()) {
        return;
    }
    s.dl_proc_ns += nv2a_phase_now_ns() - start_ns;
}

void nv2a_phase_add_present(int64_t start_ns)
{
    if (!nv2a_phase_timers_on()) {
        return;
    }
    s.present_ns += nv2a_phase_now_ns() - start_ns;
}

NV2APhaseLeaf nv2a_phase_leaf_open(int bucket)
{
    NV2APhaseLeaf l = { .bucket = bucket, .active = 0, .start_ns = 0 };
    if (!nv2a_phase_timers_on()) {
        return l;
    }
    s.pf_leaf_depth++;
    l.active = 1;
    l.start_ns = nv2a_phase_now_ns();
    return l;
}

void nv2a_phase_leaf_close(NV2APhaseLeaf *l)
{
    if (!l->active) {
        return;
    }
    int64_t d = nv2a_phase_now_ns() - l->start_ns;
    if (--s.pf_leaf_depth == 0) {
        s.pf_leaf_ns[l->bucket] += d;
        s.pf_child_ns += d;
    }
}

int64_t nv2a_phase_method_begin(void)
{
    if (!nv2a_phase_timers_on()) {
        return 0;
    }
    s.pf_pusher_child_snap = s.pf_child_ns;
    return nv2a_phase_now_ns();
}

void nv2a_phase_method_end(int64_t start_ns)
{
    if (!nv2a_phase_timers_on()) {
        return;
    }
    int64_t span = nv2a_phase_now_ns() - start_ns;
    int64_t child = s.pf_child_ns - s.pf_pusher_child_snap;
    int64_t self = span - child;
    if (self < 0) {
        self = 0;
    }
    s.pf_method_ns += self;
}

static void msub_charge(int64_t now)
{
    int64_t span = now - s.pf_msub_last_ns;
    int64_t child = s.pf_child_ns - s.pf_msub_child_snap;
    int64_t self = span - child;
    if (self < 0) {
        self = 0;
    }
    s.pf_msub_ns[s.pf_msub_cur] += self;
    s.pf_msub_last_ns = now;
    s.pf_msub_child_snap = s.pf_child_ns;
}

NV2APhaseMsub nv2a_phase_msub_open(int cat)
{
    NV2APhaseMsub m = { .active = 0 };
    if (!nv2a_phase_timers_on()) {
        return m;
    }
    s.pf_msub_active = 1;
    s.pf_msub_cur = cat;
    s.pf_msub_last_ns = nv2a_phase_now_ns();
    s.pf_msub_child_snap = s.pf_child_ns;
    m.active = 1;
    return m;
}

void nv2a_phase_msub_close(NV2APhaseMsub *m)
{
    if (!m->active) {
        return;
    }
    msub_charge(nv2a_phase_now_ns());
    s.pf_msub_active = 0;
}

void nv2a_phase_msub_mark(int cat)
{
    if (!nv2a_phase_timers_on() || !s.pf_msub_active) {
        return;
    }
    msub_charge(nv2a_phase_now_ns());
    s.pf_msub_cur = cat;
}

NV2APhaseMsub nv2a_phase_msub_redirect_open(int cat)
{
    NV2APhaseMsub m = { .active = 0, .saved = 0 };
    if (!nv2a_phase_timers_on() || !s.pf_msub_active) {
        return m;
    }
    msub_charge(nv2a_phase_now_ns());
    m.saved = s.pf_msub_cur;
    m.active = 1;
    s.pf_msub_cur = cat;
    return m;
}

void nv2a_phase_msub_redirect_close(NV2APhaseMsub *m)
{
    if (!m->active) {
        return;
    }
    msub_charge(nv2a_phase_now_ns());
    s.pf_msub_cur = m->saved;
}

void nv2a_phase_add_psub(int which, int64_t start_ns)
{
    if (!nv2a_phase_timers_on()) {
        return;
    }
    s.pf_psub_ns[which] += nv2a_phase_now_ns() - start_ns;
}

static void phase_snapshot(void)
{
    s.snap_blocked_ns = qatomic_read_i64(&s.vcpu_blocked_ns);
    s.snap_dl_ns = qatomic_read_i64(&s.vcpu_dl_ns);
    s.snap_dl_count = qatomic_read_i64(&s.vcpu_dl_count);
    s.snap_pfifo_idle_ns = s.pfifo_idle_ns;
    s.snap_dl_proc_ns = s.dl_proc_ns;
    s.snap_present_ns = s.present_ns;
    for (int i = 0; i < PF_LEAF__COUNT; i++) {
        s.snap_pf_leaf_ns[i] = s.pf_leaf_ns[i];
    }
    s.snap_pf_method_ns = s.pf_method_ns;
    for (int i = 0; i < PF_MSUB__COUNT; i++) {
        s.snap_pf_msub_ns[i] = s.pf_msub_ns[i];
    }
    for (int i = 0; i < PF_PSUB__COUNT; i++) {
        s.snap_pf_psub_ns[i] = s.pf_psub_ns[i];
    }

    s.win_frames = 0;
    s.win_wall_ns = 0;
    s.win_min_ns = INT64_MAX;
    s.win_max_ns = 0;
    s.win_light = 0;
    s.win_heavy = 0;
}

static double ns_to_ms_mean(int64_t total_ns, unsigned frames)
{
    if (!frames) {
        return 0.0;
    }
    return (double)total_ns / (double)frames / 1e6;
}

static double clamp_nonneg(double v)
{
    return v < 0.0 ? 0.0 : v;
}

static void phase_emit(void)
{
    unsigned n = s.win_frames;

    int64_t blocked_d = qatomic_read_i64(&s.vcpu_blocked_ns) - s.snap_blocked_ns;
    int64_t dl_d = qatomic_read_i64(&s.vcpu_dl_ns) - s.snap_dl_ns;
    int64_t dlc_d = qatomic_read_i64(&s.vcpu_dl_count) - s.snap_dl_count;
    int64_t idle_d = s.pfifo_idle_ns - s.snap_pfifo_idle_ns;
    int64_t proc_d = s.dl_proc_ns - s.snap_dl_proc_ns;
    int64_t present_d = s.present_ns - s.snap_present_ns;

    double mspf = ns_to_ms_mean(s.win_wall_ns, n);
    double min_ms = (double)s.win_min_ns / 1e6;
    double max_ms = (double)s.win_max_ns / 1e6;
    double blocked = ns_to_ms_mean(blocked_d, n);
    double run = clamp_nonneg(mspf - blocked);
    double dl = ns_to_ms_mean(dl_d, n);
    double dl_n = (double)dlc_d / (double)n;
    double pfifo_idle = ns_to_ms_mean(idle_d, n);
    double pfifo_work = clamp_nonneg(mspf - pfifo_idle);
    double dl_proc = ns_to_ms_mean(proc_d, n);
    double present = ns_to_ms_mean(present_d, n);

    fprintf(stderr,
            "phase: mspf=%.1f[%.1f-%.1f] run=%.1f blocked=%.1f "
            "dl_stall=%.1f(%.1fx) dl_proc=%.1f pfifo_work=%.1f "
            "pfifo_idle=%.1f present=%.1f light=%u heavy=%u\n",
            mspf, min_ms, max_ms, run, blocked, dl, dl_n, dl_proc,
            pfifo_work, pfifo_idle, present, s.win_light, s.win_heavy);

    double method = ns_to_ms_mean(s.pf_method_ns - s.snap_pf_method_ns, n);
    double texhash = ns_to_ms_mean(
        s.pf_leaf_ns[PF_LEAF_TEXHASH] - s.snap_pf_leaf_ns[PF_LEAF_TEXHASH], n);
    double upload = ns_to_ms_mean(
        s.pf_leaf_ns[PF_LEAF_UPLOAD] - s.snap_pf_leaf_ns[PF_LEAF_UPLOAD], n);
    double download = ns_to_ms_mean(
        s.pf_leaf_ns[PF_LEAF_DOWNLOAD] - s.snap_pf_leaf_ns[PF_LEAF_DOWNLOAD], n);
    double submit = ns_to_ms_mean(
        s.pf_leaf_ns[PF_LEAF_SUBMIT] - s.snap_pf_leaf_ns[PF_LEAF_SUBMIT], n);
    double queries = ns_to_ms_mean(
        s.pf_leaf_ns[PF_LEAF_QUERIES] - s.snap_pf_leaf_ns[PF_LEAF_QUERIES], n);
    double pf_present = ns_to_ms_mean(
        s.pf_leaf_ns[PF_LEAF_PRESENT] - s.snap_pf_leaf_ns[PF_LEAF_PRESENT], n);
    double residual = clamp_nonneg(pfifo_work - method - texhash - upload -
                                   download - submit - queries - pf_present);

    fprintf(stderr,
            "pfifo: work=%.1f method=%.1f texhash=%.1f queries=%.1f "
            "download=%.1f upload=%.1f submit=%.1f present=%.1f residual=%.1f\n",
            pfifo_work, method, texhash, queries, download, upload, submit,
            pf_present, residual);

    double m_const = ns_to_ms_mean(
        s.pf_msub_ns[PF_MSUB_CONST] - s.snap_pf_msub_ns[PF_MSUB_CONST], n);
    double m_attr = ns_to_ms_mean(
        s.pf_msub_ns[PF_MSUB_ATTR] - s.snap_pf_msub_ns[PF_MSUB_ATTR], n);
    double m_pipe = ns_to_ms_mean(
        s.pf_msub_ns[PF_MSUB_PIPE] - s.snap_pf_msub_ns[PF_MSUB_PIPE], n);
    double m_desc = ns_to_ms_mean(
        s.pf_msub_ns[PF_MSUB_DESC] - s.snap_pf_msub_ns[PF_MSUB_DESC], n);
    double m_draw = ns_to_ms_mean(
        s.pf_msub_ns[PF_MSUB_DRAW] - s.snap_pf_msub_ns[PF_MSUB_DRAW], n);
    double m_synctex = ns_to_ms_mean(
        s.pf_msub_ns[PF_MSUB_SYNCTEX] - s.snap_pf_msub_ns[PF_MSUB_SYNCTEX], n);
    double m_rpchurn = ns_to_ms_mean(
        s.pf_msub_ns[PF_MSUB_RPCHURN] - s.snap_pf_msub_ns[PF_MSUB_RPCHURN], n);
    double m_resid = clamp_nonneg(method - m_const - m_attr - m_pipe - m_desc -
                                  m_draw - m_synctex - m_rpchurn);

    double p_gpuwait = ns_to_ms_mean(
        s.pf_psub_ns[PF_PSUB_GPUWAIT] - s.snap_pf_psub_ns[PF_PSUB_GPUWAIT], n);
    double p_composite = ns_to_ms_mean(
        s.pf_psub_ns[PF_PSUB_COMPOSITE] - s.snap_pf_psub_ns[PF_PSUB_COMPOSITE],
        n);
    double p_vrsubmit = ns_to_ms_mean(
        s.pf_psub_ns[PF_PSUB_VRSUBMIT] - s.snap_pf_psub_ns[PF_PSUB_VRSUBMIT], n);
    double p_resid =
        clamp_nonneg(pf_present - p_gpuwait - p_composite - p_vrsubmit);

    fprintf(stderr,
            "pfifo2: method_synctex=%.1f method_const=%.1f method_attr=%.1f "
            "method_pipe=%.1f method_desc=%.1f method_draw=%.1f "
            "method_rpchurn=%.1f method_resid=%.1f present_gpuwait=%.1f "
            "present_composite=%.1f present_vrsubmit=%.1f present_resid=%.1f\n",
            m_synctex, m_const, m_attr, m_pipe, m_desc, m_draw, m_rpchurn,
            m_resid, p_gpuwait, p_composite, p_vrsubmit, p_resid);
}

void nv2a_phase_flip(void)
{
    if (!nv2a_phase_timers_on()) {
        return;
    }

    int64_t now = nv2a_phase_now_ns();

    if (s.last_flip_ns == 0) {
        s.last_flip_ns = now;
        phase_snapshot();
        return;
    }

    int64_t wall = now - s.last_flip_ns;
    s.last_flip_ns = now;

    s.win_frames++;
    s.win_wall_ns += wall;
    if (wall < s.win_min_ns) {
        s.win_min_ns = wall;
    }
    if (wall > s.win_max_ns) {
        s.win_max_ns = wall;
    }
    if (wall >= PHASE_HEAVY_NS) {
        s.win_heavy++;
    } else {
        s.win_light++;
    }

    if (s.win_frames >= PHASE_WINDOW) {
        phase_emit();
        phase_snapshot();
    }
}
