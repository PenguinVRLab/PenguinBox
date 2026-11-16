/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include "vr_padxr_core.h"
#include "vr_padxr_profiles.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PADXR_DATA_DIR
#define PADXR_DATA_DIR "tests/unit"
#endif

static int g_n, g_failed, g_case_fail;

#define CHECK(cond) do { if (!(cond)) { g_case_fail++; \
    printf("#   %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { g_case_fail++; printf("#   %s:%d: %s = %lld, want %lld\n", \
    __FILE__, __LINE__, #a, _a, _b); } } while (0)

static void run(const char *name, void (*fn)(void))
{
    g_case_fail = 0;
    fn();
    g_n++;
    if (g_case_fail) {
        g_failed++;
        printf("not ok %d - %s\n", g_n, name);
    } else {
        printf("ok %d - %s\n", g_n, name);
    }
}

static FILE *open_data(const char *name)
{
    char path[512];
    const char *dir = getenv("PADXR_DATA_DIR");
    snprintf(path, sizeof(path), "%s/%s", dir ? dir : PADXR_DATA_DIR, name);
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("#   cannot open %s\n", path);
    }
    return f;
}

static int claim_of(const char *s)
{
    for (int c = 0; c < PADXR_CLAIM__COUNT; c++) {
        if (strcmp(padxr_claim_name(c), s) == 0) {
            return c;
        }
    }
    return -1;
}

static int each_row(const char *kind, void (*fn)(char **f, int n))
{
    FILE *fp = open_data("padxr-merge-vectors.csv");
    if (!fp) {
        g_case_fail++;
        return 0;
    }
    char line[512];
    int rows = 0;
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '#' || line[0] == 0) {
            continue;
        }
        char *f[8];
        int n = 0;
        for (char *t = strtok(line, ","); t && n < 8; t = strtok(NULL, ",")) {
            f[n++] = t;
        }
        if (n > 0 && strcmp(f[0], kind) == 0) {
            fn(f, n);
            rows++;
        }
    }
    fclose(fp);
    return rows;
}

static void claim_row(char **f, int n)
{
    CHECK_EQ(n, 4);
    int a = claim_of(f[1]), b = claim_of(f[2]);
    CHECK(a >= 0 && b >= 0);
    if (padxr_claim_pair_compatible(a, b) != (atoi(f[3]) == 1)) {
        printf("#   claim %s + %s\n", f[1], f[2]);
        g_case_fail++;
    }
}

static void test_claim_matrix_vectors(void)
{
    CHECK_EQ(each_row("claim", claim_row), 49);
}

static void stick_row(char **f, int n)
{
    CHECK_EQ(n, 5);
    int real = (int)strtol(f[1], NULL, 16);
    int want = (int)strtol(f[4], NULL, 16);
    int c = strcmp(f[2], "none") == 0 ? PADXR_CLAIM_NONE : claim_of(f[2]);
    int got = padxr_axis_merge(&PADXR_RANGE_U8, real, c, strtof(f[3], NULL));
    if (got != want) {
        printf("#   stick %s %s %s: got %02X want %02X\n", f[1], f[2], f[3],
               got, want);
        g_case_fail++;
    }
}

static void stickcell_row(char **f, int n)
{
    CHECK_EQ(n, 4);
    PadxrAxisCell c = { 0 };
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", f[2]);
    char *save = NULL;
    for (char *w = strtok_r(buf, "+", &save); w; w = strtok_r(NULL, "+", &save)) {
        char *colon = strchr(w, ':');
        *colon = 0;
        padxr_axis_cell_write(&c, &PADXR_RANGE_U8, claim_of(w),
                              strtof(colon + 1, NULL));
    }
    int got = padxr_axis_cell_apply(&c, &PADXR_RANGE_U8,
                                    (int)strtol(f[1], NULL, 16));
    if (got != (int)strtol(f[3], NULL, 16)) {
        printf("#   stickcell %s %s: got %02X want %s\n", f[1], f[2], got, f[3]);
        g_case_fail++;
    }
}

static void test_stick_vectors(void)
{
    CHECK_EQ(each_row("stick", stick_row), 60);
    CHECK_EQ(each_row("stickcell", stickcell_row), 7);
}

static void word_row(char **f, int n)
{
    CHECK_EQ(n, 6);
    uint64_t real = ~strtoull(f[1], NULL, 16) & 0xFFFF;
    PadxrButtonCell c = { strtoull(f[2], NULL, 16), strtoull(f[3], NULL, 16),
                          strtoull(f[4], NULL, 16) };
    uint64_t got = ~padxr_merge_buttons(real, &c) & 0xFFFF;
    if (got != strtoull(f[5], NULL, 16)) {
        printf("#   word %s %s %s %s: got %04llX want %s\n", f[1], f[2], f[3],
               f[4], (unsigned long long)got, f[5]);
        g_case_fail++;
    }
}

static void test_word_vectors(void)
{
    CHECK_EQ(each_row("word", word_row), 9);
}

static void edge_row(char **f, int n)
{
    CHECK_EQ(n, 3);
    PadxrEdgeSched s;
    padxr_sched_reset(&s);
    char ops[128], want[128], got[256] = "";
    snprintf(ops, sizeof(ops), "%s", f[1]);
    snprintf(want, sizeof(want), "%s", f[2]);
    char *save = NULL;
    for (char *op = strtok_r(ops, ";", &save); op; op = strtok_r(NULL, ";", &save)) {
        if (op[0] == 's') {
            char m[16];
            snprintf(m, sizeof(m), "%s%04llX", got[0] ? ";" : "",
                     (unsigned long long)padxr_sched_step(&s));
            strncat(got, m, sizeof(got) - strlen(got) - 1);
        } else if (op[0] == 'p') {
            char *colon = strchr(op, ':');
            *colon = 0;
            padxr_sched_press(&s, atoi(op + 1), (uint32_t)atoi(colon + 1));
        }
    }
    if (strcmp(got, want) != 0) {
        printf("#   edge %s: got %s want %s\n", f[1], got, want);
        g_case_fail++;
    }
}

static void test_edge_vectors(void)
{
    CHECK_EQ(each_row("edge", edge_row), 6);
}

static PadxrMemberDesc desc(const char *id)
{
    PadxrMemberDesc d;
    memset(&d, 0, sizeof(d));
    d.id = id;
    d.kind = id;
    d.session_gates = PADXR_SG_ALL;
    return d;
}

static void test_two_owners_refused(void)
{
    PadxrMemberDesc a = desc("a"), b = desc("b");
    a.res[PADXR_RES_RIGHT_STICK] = PADXR_CLAIM_OWN;
    b.res[PADXR_RES_RIGHT_STICK] = PADXR_CLAIM_OWN;
    int clash = -1;
    CHECK(!padxr_claims_compatible(&a, &b, &clash));
    CHECK_EQ(clash, PADXR_RES_RIGHT_STICK);
    b.res[PADXR_RES_RIGHT_STICK] = PADXR_CLAIM_ADD;
    CHECK(padxr_claims_compatible(&a, &b, NULL));
    a.res[PADXR_RES_MIC] = PADXR_CLAIM_EXCLUSIVE;
    b.res[PADXR_RES_MIC] = PADXR_CLAIM_TEE;
    CHECK(!padxr_claims_compatible(&a, &b, &clash));
    CHECK_EQ(clash, PADXR_RES_MIC);
    b.harness = true;
    CHECK(padxr_claims_compatible(&a, &b, NULL));
}

static void test_two_button_owners_refused(void)
{
    PadxrMemberDesc a = desc("a"), b = desc("b");
    a.own_pad = b.own_pad = PADXR_PAD_SBC;
    a.own_buttons = 1ull << 40;
    b.own_buttons = (1ull << 40) | 1;
    int clash = -1;
    CHECK(!padxr_claims_compatible(&a, &b, &clash));
    CHECK_EQ(clash, PADXR_CLASH_BUTTONS);
    CHECK(strcmp(padxr_res_name(clash), "buttons") == 0);
    b.own_buttons = 1;
    CHECK(padxr_claims_compatible(&a, &b, NULL));
    b.own_buttons = 1ull << 40;
    b.own_pad = PADXR_PAD_GAMEPAD;
    CHECK(padxr_claims_compatible(&a, &b, NULL));
}

static void test_s16_axis_math(void)
{
    const PadxrRange *r = &PADXR_RANGE_S16;
    CHECK_EQ(padxr_axis_value(r, PADXR_CLAIM_SET, -1.0f), -32768);
    CHECK_EQ(padxr_axis_value(r, PADXR_CLAIM_SET, 0.0f), 0);
    CHECK_EQ(padxr_axis_value(r, PADXR_CLAIM_SET, 1.0f), 32767);
    CHECK_EQ(padxr_axis_value(r, PADXR_CLAIM_OWN, NAN), 0);
    CHECK_EQ(padxr_axis_merge(r, 30000, PADXR_CLAIM_ADD, 0.5f), 32767);
    CHECK_EQ(padxr_axis_merge(r, -30000, PADXR_CLAIM_ADD, -0.5f), -32768);
    CHECK_EQ(padxr_axis_merge(r, 1000, PADXR_CLAIM_ADD, 0.25f), 1000 + 8192);
    CHECK_EQ(padxr_axis_merge(r, 1234, PADXR_CLAIM_NONE, 1.0f), 1234);
    CHECK_EQ(padxr_axis_value(&PADXR_RANGE_U15, PADXR_CLAIM_SET, 0.0f), 0);
    CHECK_EQ(padxr_axis_value(&PADXR_RANGE_U15, PADXR_CLAIM_SET, 1.0f), 32767);
    CHECK_EQ(padxr_axis_value(&PADXR_RANGE_U15, PADXR_CLAIM_SET, -1.0f), 0);
}

static void test_player_hold_wins(void)
{
    PadxrButtonCell c = { 1ull << 3, 0, 0 };
    uint64_t got = padxr_merge_buttons(1ull << 40, &c);
    CHECK_EQ(got, (1ull << 40) | (1ull << 3));
    PadxrButtonCell o = { 0, 1ull << 40, 0 };
    CHECK_EQ(padxr_merge_buttons(1ull << 40, &o), 0);
}

static void test_glove_v2_goldens(void)
{
    static const struct { PadxrCue cue; uint8_t want[8]; } rows[] = {
        { { 1, PADXR_LOCUS_POINTER, 0.6f, 30 },  { 2, 1, 0x02, 60, 30, 0, 0, 0 } },
        { { 0, 0, 0.6f, 30 },                    { 2, 0, 0xFF, 60, 30, 0, 0, 0 } },
        { { 0, PADXR_LOCUS_FINGERTIPS, 1.5f, 2500 }, { 2, 0, 0x1F, 100, 0xD0, 0x07, 0, 0 } },
        { { 1, 0xC0, 0.004f, 0 },                { 2, 1, 0xC0, 0, 1, 0, 0, 0 } },
        { { 1, 0x03, 0.155f, 300 },              { 2, 1, 0x03, 16, 0x2C, 0x01, 0, 0 } },
    };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        uint8_t out[8];
        CHECK_EQ(padxr_glove_encode_cue(out, &rows[i].cue), 8);
        if (memcmp(out, rows[i].want, 8) != 0) {
            printf("#   row %zu: %02x %02x %02x %02x %02x %02x\n", i, out[0],
                   out[1], out[2], out[3], out[4], out[5]);
            g_case_fail++;
        }
    }
}

static void test_cue_rules(void)
{
    PadxrCue weak = { 0, 0, 0.3f, 20 }, strong = { 0, 0, 0.5f, 10 },
             longer = { 0, 0, 0.5f, 30 }, bad = { 2, 0, 0.5f, 10 };
    CHECK(padxr_cue_stronger(&weak, &strong));
    CHECK(!padxr_cue_stronger(&strong, &weak));
    CHECK(padxr_cue_stronger(&strong, &longer));
    CHECK(padxr_cue_valid(&weak));
    CHECK(!padxr_cue_valid(&bad));
}

static PadxrInput published_at(int64_t t)
{
    PadxrInput p;
    memset(&p, 0, sizeof(p));
    p.generation = 7;
    p.published_us = t;
    p.actions_active = true;
    p.hands[1].aim.valid = true;
    p.hands[1].select = true;
    p.head.valid = true;
    return p;
}

static void test_stale_reads_neutral(void)
{
    PadxrInput pub = published_at(1000000), got;
    padxr_input_read_rule(&pub, 1000000 + 400000, &got);
    CHECK(!got.stale);
    CHECK(got.hands[1].select);
    CHECK_EQ(got.caps, PADXR_NEED_PAD | PADXR_NEED_HEAD_POSE |
                       PADXR_NEED_HEAD_6DOF | PADXR_NEED_CONTROLLERS);
    padxr_input_read_rule(&pub, 1000000 + 600000, &got);
    CHECK(got.stale);
    CHECK(!got.hands[1].select);
    CHECK(!got.actions_active);
    CHECK(!got.head.valid);
    CHECK_EQ(got.generation, 7);
    CHECK_EQ(got.caps, PADXR_NEED_PAD);
}

static void test_never_published_is_neutral(void)
{
    PadxrInput pub, got;
    memset(&pub, 0, sizeof(pub));
    pub.hands[0].select = true;
    padxr_input_read_rule(&pub, 0, &got);
    CHECK(got.stale);
    CHECK(!got.hands[0].select);
    CHECK_EQ(got.caps, PADXR_NEED_PAD);
}

static uint8_t g_ram[64];

static bool ram_read(void *ctx, uint32_t addr, int width, uint32_t *v)
{
    (void)ctx;
    if (addr + (uint32_t)width > sizeof(g_ram)) {
        return false;
    }
    *v = 0;
    memcpy(v, &g_ram[addr], (size_t)width);
    return true;
}

static bool mod_armed(void *ctx, const char *slug)
{
    (void)ctx;
    return strcmp(slug, "on") == 0;
}

static const PadxrGameMem g_mem = { ram_read, mod_armed, NULL };

static void test_gate_eval(void)
{
    memset(g_ram, 0, sizeof(g_ram));
    g_ram[4] = 0x35;
    PadxrGate g = { .kind = PADXR_GATE_MEM, .width = 1, .addr = 4, .value = 0x35 };
    CHECK(padxr_gate_eval(&g, &g_mem));
    g.not_equals = true;
    CHECK(!padxr_gate_eval(&g, &g_mem));
    g.not_equals = false;
    g.value = 0x05;
    g.mask = 0x0F;
    CHECK(padxr_gate_eval(&g, &g_mem));
    g_ram[8] = 0x10;
    g_ram[0x14] = 0xEF;
    g_ram[0x15] = 0xBE;
    PadxrGate c = { .kind = PADXR_GATE_MEM, .width = 2, .addr = 8, .n_chain = 1,
                    .chain = { 4 }, .value = 0xBEEF };
    CHECK(padxr_gate_eval(&c, &g_mem));
    PadxrGate m = { .kind = PADXR_GATE_MOD, .mod_slug = "on" };
    CHECK(padxr_gate_eval(&m, &g_mem));
    m.mod_slug = "off";
    CHECK(!padxr_gate_eval(&m, &g_mem));
}

static void test_gate_fails_closed(void)
{
    PadxrGate g = { .kind = PADXR_GATE_MEM, .width = 4, .addr = 1000,
                    .not_equals = true, .value = 0 };
    CHECK(!padxr_gate_eval(&g, &g_mem));
    PadxrGate c = { .kind = PADXR_GATE_MEM, .width = 1, .addr = 0, .n_chain = 1,
                    .not_equals = true };
    memset(g_ram, 0, sizeof(g_ram));
    CHECK(!padxr_gate_eval(&c, &g_mem));
    PadxrGate w = { .kind = PADXR_GATE_MEM, .width = 3, .addr = 0 };
    CHECK(!padxr_gate_eval(&w, &g_mem));
    CHECK(!padxr_gate_eval(&g, NULL));
}

static char g_log[16][160];
static int g_nlog;

static void log_sink(void *ctx, const char *line)
{
    (void)ctx;
    if (g_nlog < 16) {
        snprintf(g_log[g_nlog++], sizeof(g_log[0]), "%s", line);
    }
}

static bool log_has(const char *needle)
{
    for (int i = 0; i < g_nlog; i++) {
        if (strstr(g_log[i], needle)) {
            return true;
        }
    }
    return false;
}

typedef struct TestMember { int ticks, arms, loaded; } TestMember;

static bool tm_arm(void *self, const char **why)
{
    (void)why;
    ((TestMember *)self)->arms++;
    return true;
}

static void tm_tick(void *self, const PadxrTickCtx *ctx, PadxrOutputs *out)
{
    TestMember *m = self;
    m->ticks++;
    padxr_out_press(out, 0, 5, 2);
    padxr_out_hold(out, 0, 40);
    padxr_out_own(out, 0, 41, true);
    if (ctx->tick == 2) {
        PadxrCue c = { 1, PADXR_LOCUS_POINTER, 0.6f, 30 };
        padxr_out_cue(out, &c);
    }
}

static void tm_loaded(void *self)
{
    ((TestMember *)self)->loaded++;
}

static PadxrMemberDesc g_tdesc;
static PadxrMemberOps g_tops;

static void seat_with_member(PadxrSeat *s, TestMember *m, const PadxrGate *g,
                             int n_gates)
{
    g_nlog = 0;
    padxr_seat_init(s, 2, log_sink, NULL);
    g_tdesc = desc("testMember");
    g_tops = (PadxrMemberOps){ &g_tdesc, tm_arm, tm_tick, NULL, tm_loaded };
    memset(m, 0, sizeof(*m));
    CHECK_EQ(padxr_seat_arm(s, &g_tops, m, g, n_gates, PADXR_NEED_PAD), 0);
}

static void test_no_members_writes_nothing(void)
{
    PadxrSeat s;
    padxr_seat_init(&s, 2, NULL, NULL);
    PadxrInput in = published_at(0);
    PadxrOutputs out, zero;
    memset(&zero, 0, sizeof(zero));
    padxr_seat_tick(&s, &in, &g_mem, 0, &out);
    CHECK(memcmp(&out, &zero, sizeof(out)) == 0);
    CHECK(!padxr_seat_any_armed(&s));
}

static void test_closed_gate_not_ticked(void)
{
    PadxrSeat s;
    TestMember m;
    memset(g_ram, 0, sizeof(g_ram));
    PadxrGate g = { .kind = PADXR_GATE_MEM, .width = 1, .addr = 4, .value = 1 };
    seat_with_member(&s, &m, &g, 1);
    PadxrInput in = published_at(0);
    PadxrOutputs out;
    padxr_seat_tick(&s, &in, &g_mem, 0, &out);
    CHECK_EQ(m.ticks, 0);
    CHECK_EQ(out.port[0].n_press, 0);
    g_ram[4] = 1;
    padxr_seat_tick(&s, &in, &g_mem, 0, &out);
    CHECK_EQ(m.ticks, 1);
    CHECK(log_has("testMember gate open"));
    in.stale = true;
    padxr_seat_tick(&s, &in, &g_mem, 0, &out);
    CHECK_EQ(m.ticks, 1);
    CHECK(log_has("testMember gate closed"));
}

static void test_status_counts(void)
{
    PadxrSeat s;
    TestMember m;
    seat_with_member(&s, &m, NULL, 0);
    PadxrInput in = published_at(0);
    PadxrOutputs out;
    for (int t = 0; t < 3; t++) {
        padxr_seat_tick(&s, &in, &g_mem, 0, &out);
    }
    const PadxrMemberStatus *st = &s.slot[0].st;
    CHECK_EQ(st->ticks, 3);
    CHECK_EQ(st->presses, 3);
    CHECK_EQ(st->cues_sent, 1);
    CHECK_EQ(st->refusals, 3);
    CHECK_EQ(out.port[0].buttons.own_mask, 0);
    CHECK_EQ(out.port[0].hold, 1ull << 40);
    padxr_seat_state_loaded(&s);
    CHECK_EQ(m.loaded, 1);
    CHECK_EQ(g_nlog, 2);
}

static void test_own_on_claimed_button(void)
{
    PadxrSeat s;
    TestMember m;
    seat_with_member(&s, &m, NULL, 0);
    g_tdesc.own_pad = PADXR_PAD_SBC;
    g_tdesc.own_buttons = 1ull << 41;
    PadxrInput in = published_at(0);
    PadxrOutputs out;
    padxr_seat_tick(&s, &in, &g_mem, 0, &out);
    CHECK_EQ(out.port[0].buttons.own_mask, 1ull << 41);
    CHECK_EQ(out.port[0].buttons.own_down, 1ull << 41);
    CHECK_EQ(out.refused, 0);
}

static void test_arm_refusals(void)
{
    PadxrSeat s;
    TestMember m1, m2 = { 0 };
    seat_with_member(&s, &m1, NULL, 0);
    static PadxrMemberDesc d2;
    static PadxrMemberOps o2;
    d2 = desc("needsHands");
    d2.needs[0] = PADXR_NEED_HANDS;
    o2 = (PadxrMemberOps){ &d2, tm_arm, tm_tick, NULL, NULL };
    CHECK_EQ(padxr_seat_arm(&s, &o2, &m2, NULL, 0, PADXR_NEED_PAD), -1);
    CHECK(log_has("needsHands not armed: need 0x20 unmet"));
    d2 = desc("offline");
    d2.online = PADXR_ONLINE_OFFLINE;
    s.online = true;
    CHECK_EQ(padxr_seat_arm(&s, &o2, &m2, NULL, 0, PADXR_NEED_PAD), -1);
    CHECK(log_has("offline not armed: offline member, online"));
    s.online = false;
    d2 = desc("threeLayers");
    d2.layers = 3;
    CHECK_EQ(padxr_seat_arm(&s, &o2, &m2, NULL, 0, PADXR_NEED_PAD), -1);
    CHECK(log_has("threeLayers not armed: layer budget"));
    const PadxrMemberEntry *e = padxr_member_find("sbcPanel");
    CHECK(e && !e->built);
    PadxrMemberOps only = { e ? e->desc : NULL, NULL, NULL, NULL, NULL };
    CHECK_EQ(padxr_seat_arm(&s, &only, NULL, NULL, 0,
                            PADXR_NEED_PAD | PADXR_NEED_CONTROLLERS), -1);
    CHECK(log_has("sbcPanel not armed: not built on this core"));
    CHECK_EQ(s.n, 1);
}

static void test_arm_refuses_second_owner(void)
{
    PadxrSeat s;
    TestMember m1, m2 = { 0 };
    seat_with_member(&s, &m1, NULL, 0);
    g_tdesc.res[PADXR_RES_RIGHT_STICK] = PADXR_CLAIM_OWN;
    static PadxrMemberDesc d2;
    static PadxrMemberOps o2;
    d2 = desc("menuStick");
    d2.res[PADXR_RES_RIGHT_STICK] = PADXR_CLAIM_OWN;
    o2 = (PadxrMemberOps){ &d2, tm_arm, tm_tick, NULL, NULL };
    CHECK_EQ(padxr_seat_arm(&s, &o2, &m2, NULL, 0, PADXR_NEED_PAD), -1);
    CHECK(log_has("menuStick not armed: rightStick clashes with testMember"));
    CHECK_EQ(m2.arms, 0);
}

static const PadxrRange *const k_gp_ranges[6] = {
    &PADXR_RANGE_U15, &PADXR_RANGE_U15, &PADXR_RANGE_S16, &PADXR_RANGE_S16,
    &PADXR_RANGE_S16, &PADXR_RANGE_S16 };

static void test_merge_inert_with_nothing_armed(void)
{
    uint32_t seed = 12345;
    PadxrPortCells empty;
    memset(&empty, 0, sizeof(empty));
    for (int i = 0; i < 1000; i++) {
        int16_t axis[6], before[6];
        for (int a = 0; a < 6; a++) {
            seed = seed * 1103515245u + 12345u;
            axis[a] = before[a] = (int16_t)(seed >> 8);
        }
        uint64_t buttons = seed * 2654435761u, b0 = buttons;
        padxr_merge_port(&empty, 0, &buttons, axis, 6, k_gp_ranges);
        padxr_merge_port(NULL, 0, &buttons, axis, 6, k_gp_ranges);
        if (buttons != b0 || memcmp(axis, before, sizeof(axis)) != 0) {
            g_case_fail++;
            printf("#   case %d changed the report\n", i);
            break;
        }
    }
}

static void test_merge_port_applies(void)
{
    PadxrPortOut o;
    memset(&o, 0, sizeof(o));
    o.hold = 1u << 2;
    o.buttons.own_mask = 1u << 4;
    padxr_axis_cell_write(&o.axis[4], &PADXR_RANGE_S16, PADXR_CLAIM_ADD, 0.25f);
    PadxrPortCells c;
    padxr_port_cells_from_out(&o, &c);
    CHECK(c.live);
    int16_t axis[6] = { 100, 200, 300, 400, 1000, 600 };
    uint64_t buttons = (1u << 4) | 1u;
    padxr_merge_port(&c, 1u << 7, &buttons, axis, 6, k_gp_ranges);
    CHECK_EQ(buttons, 1u | (1u << 2) | (1u << 7));
    CHECK_EQ(axis[4], 1000 + 8192);
    CHECK_EQ(axis[0], 100);
    CHECK_EQ(axis[5], 600);
}

static void test_sbc_row(void)
{
    const PadxrProfileRow *r = padxr_profile_for_title(0x43430002);
    CHECK(r != NULL);
    if (r) {
        CHECK(strcmp(r->members[0].id, "sbcPanel") == 0);
        CHECK_EQ(r->members[0].cfg, PADXR_CFG_SBC_PANEL);
        for (int i = 0; i < PADXR_ROW_MEMBERS && r->members[i].id; i++) {
            CHECK(padxr_member_find(r->members[i].id) != NULL);
        }
    }
}

static void test_loc_row_same_as_sbc(void)
{
    const PadxrProfileRow *a = padxr_profile_for_title(0x43430002);
    const PadxrProfileRow *b = padxr_profile_for_title(0x43430009);
    CHECK(a != NULL && b != NULL && a != b);
    if (a && b) {
        for (int i = 0; i < PADXR_ROW_MEMBERS; i++) {
            CHECK((a->members[i].id == NULL) == (b->members[i].id == NULL));
            if (a->members[i].id && b->members[i].id) {
                CHECK(strcmp(a->members[i].id, b->members[i].id) == 0);
                CHECK_EQ(a->members[i].cfg, b->members[i].cfg);
            }
        }
    }
}

static void test_non_sbc_title_arms_none(void)
{
    CHECK(padxr_profile_for_title(0x4D530004) == NULL);
    CHECK(padxr_profile_for_title(-1) == NULL);
    CHECK(padxr_profile_for_title(0) == NULL);
}

static void test_manifest_matches_registry(void)
{
    FILE *fp = open_data("padxr-manifest-xbox.tsv");
    if (!fp) {
        g_case_fail++;
        return;
    }
    char rows[64][256];
    int n = 0;
    char line[256];
    bool header = false;
    while (fgets(line, sizeof(line), fp) && n < 64) {
        line[strcspn(line, "\r\n")] = 0;
        if (strncmp(line, "# padxr-manifest v1 core=xbox", 29) == 0) {
            header = true;
        }
        if (line[0] == '#' || line[0] == 0) {
            continue;
        }
        snprintf(rows[n++], sizeof(rows[0]), "%s", line);
    }
    fclose(fp);
    CHECK(header);
    CHECK(n > 0);
    size_t nm = 0;
    const PadxrMemberEntry *t = padxr_member_table(&nm);
    CHECK(nm > 0);
    for (size_t i = 0; i < nm; i++) {
        char want[160];
        int len = padxr_manifest_line(t[i].desc, want, sizeof(want));
        bool found = false;
        for (int r = 0; r < n; r++) {
            if (strncmp(rows[r], want, (size_t)len) == 0 && rows[r][len] == '\t') {
                found = true;
                bool reg_built = strcmp(rows[r] + len + 1, "built") == 0;
                if (reg_built != t[i].built) {
                    printf("#   %s: built here %d, registry %s\n", t[i].desc->id,
                           t[i].built, rows[r] + len + 1);
                    g_case_fail++;
                }
            }
        }
        if (!found) {
            printf("#   EXTRA %s (no such registry row)\n", want);
            g_case_fail++;
        }
    }
    for (int r = 0; r < n; r++) {
        const char *st = strrchr(rows[r], '\t');
        if (st && strcmp(st + 1, "built") == 0) {
            char id[64];
            snprintf(id, sizeof(id), "%.*s", (int)strcspn(rows[r], "\t"), rows[r]);
            const PadxrMemberEntry *e = padxr_member_find(id);
            if (!e || !e->built) {
                printf("#   MISSING %s (built in the registry)\n", id);
                g_case_fail++;
            }
        }
    }
}

static void test_manifest_line_format_wrapper(void)
{
    const PadxrMemberEntry *e = padxr_member_find("sbcPanel");
    CHECK(e != NULL);
    if (e) {
        char buf[160];
        padxr_manifest_line(e->desc, buf, sizeof(buf));
        CHECK(strcmp(buf, "sbcPanel\tpanel\tPAD\tmerge\tpress") == 0);
    }
}

static void test_seat_arrays_sized(void)
{
    CHECK(PADXR_MAX_SEATS >= 4);
    CHECK(PADXR_MAX_PORTS >= 4);
    CHECK(PADXR_MAX_AXES >= 8);
    CHECK_EQ(PADXR_MAX_BUTTONS, 64);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("1..27\n");
    run("claim_matrix_vectors", test_claim_matrix_vectors);
    run("stick_vectors", test_stick_vectors);
    run("word_vectors", test_word_vectors);
    run("edge_vectors", test_edge_vectors);
    run("two_owners_refused", test_two_owners_refused);
    run("two_button_owners_refused", test_two_button_owners_refused);
    run("s16_axis_math", test_s16_axis_math);
    run("player_hold_wins", test_player_hold_wins);
    run("glove_v2_goldens", test_glove_v2_goldens);
    run("cue_rules", test_cue_rules);
    run("stale_reads_neutral", test_stale_reads_neutral);
    run("never_published_is_neutral", test_never_published_is_neutral);
    run("gate_eval", test_gate_eval);
    run("gate_fails_closed", test_gate_fails_closed);
    run("no_members_writes_nothing", test_no_members_writes_nothing);
    run("closed_gate_not_ticked", test_closed_gate_not_ticked);
    run("status_counts", test_status_counts);
    run("own_on_claimed_button", test_own_on_claimed_button);
    run("arm_refusals", test_arm_refusals);
    run("arm_refuses_second_owner", test_arm_refuses_second_owner);
    run("merge_inert_with_nothing_armed", test_merge_inert_with_nothing_armed);
    run("merge_port_applies", test_merge_port_applies);
    run("sbc_row", test_sbc_row);
    run("loc_row_same_as_sbc", test_loc_row_same_as_sbc);
    run("non_sbc_title_arms_none", test_non_sbc_title_arms_none);
    run("manifest_matches_registry", test_manifest_matches_registry);
    run("manifest_line_format", test_manifest_line_format_wrapper);
    run("seat_arrays_sized", test_seat_arrays_sized);
    return g_failed ? 1 : 0;
}
