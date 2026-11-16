/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "vr_mods.h"

#define RAM_SIZE (1u << 20)
#define TITLE 0x4D53004Bu
#define VER 0x00010003u

static uint8_t ram[RAM_SIZE];
static int fails, checks;
static FILE *log_file;
static long log_mark;

static void expect(const char *what, bool ok)
{
    checks++;
    fails += !ok;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static void mark(void)
{
    fflush(stderr);
    fseek(log_file, 0, SEEK_END);
    log_mark = ftell(log_file);
}

static int log_count(const char *needle)
{
    static char buf[1 << 16];
    size_t n;
    int k = 0;

    fflush(stderr);
    fseek(log_file, log_mark, SEEK_SET);
    n = fread(buf, 1, sizeof(buf) - 1, log_file);
    buf[n] = 0;
    for (const char *p = strstr(buf, needle); p; p = strstr(p + 1, needle)) {
        k++;
    }
    return k;
}

static uint32_t f2u(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

static uint32_t rd(uint32_t at)
{
    uint32_t v;
    memcpy(&v, ram + at, 4);
    return v;
}

static void wr(uint32_t at, uint32_t v)
{
    memcpy(ram + at, &v, 4);
}

static void frames(int n, int64_t title, int64_t ver)
{
    for (int i = 0; i < n; i++) {
        vr_mods_frame(ram, RAM_SIZE, title, ver);
    }
}

static const float SLOT[4] = { 0.65f, -0.15f, 8.5f, 0.9f };

static void put_slot(uint32_t at)
{
    for (int i = 0; i < 4; i++) {
        wr(at + 4 * i, f2u(SLOT[i]));
    }
}

static char *sdup(const char *s)
{
    return strcpy(malloc(strlen(s) + 1), s);
}

static VrMod pgr2_fix(void)
{
    VrMod m = { .name = sdup("pgr2 test"), .slug = sdup("pgr2-cockpit"),
                .version = sdup("1"), .source = sdup("test"),
                .title_id = TITLE, .enabled = true, .has_find = true };
    m.find.len = 16;
    m.find.align = 4;
    for (int i = 0; i < 4; i++) {
        uint32_t u = f2u(SLOT[i]);
        memcpy(m.find.bytes + 4 * i, &u, 4);
    }
    memset(m.find.mask, 0xFF, 16);
    m.n_words = 3;
    m.words = calloc(3, sizeof(VrModWord));
    m.words[0] = (VrModWord){ 0x0, f2u(0.6f), f2u(0.65f), true };
    m.words[1] = (VrModWord){ 0x4, f2u(-0.1f), f2u(-0.15f), true };
    m.words[2] = (VrModWord){ 0xC, f2u(-0.7f), f2u(0.9f), true };
    m.n_guards = 1;
    m.guards = calloc(1, sizeof(VrModGuard));
    m.guards[0] = (VrModGuard){ 0x8, f2u(8.5f), 4 };
    return m;
}

static bool modded_at(uint32_t at)
{
    return rd(at) == f2u(0.6f) && rd(at + 4) == f2u(-0.1f) &&
           rd(at + 8) == f2u(8.5f) && rd(at + 12) == f2u(-0.7f);
}

static bool stock_at(uint32_t at)
{
    return rd(at) == f2u(0.65f) && rd(at + 4) == f2u(-0.15f) &&
           rd(at + 8) == f2u(8.5f) && rd(at + 12) == f2u(0.9f);
}

static void fixed_address(void)
{
    VrMod *m = calloc(2, sizeof(VrMod));
    m[0] = (VrMod){ .name = sdup("fixed"), .slug = sdup("fixed"),
                    .version = sdup("1"), .source = sdup("test"),
                    .title_id = TITLE, .enabled = true };
    m[0].n_words = 1;
    m[0].words = calloc(1, sizeof(VrModWord));
    m[0].words[0] = (VrModWord){ 0x1000, 0xDEADBEEF, 0, false };
    m[0].n_guards = 1;
    m[0].guards = calloc(1, sizeof(VrModGuard));
    m[0].guards[0] = (VrModGuard){ 0x2000, 0x12345678, 4 };
    m[1] = m[0];
    m[1].name = sdup("fixed v9");
    m[1].slug = sdup("fixed-v9");
    m[1].version = sdup("1");
    m[1].source = sdup("test");
    m[1].words = calloc(1, sizeof(VrModWord));
    m[1].words[0] = (VrModWord){ 0x1100, 0xCAFEF00D, 0, false };
    m[1].guards = calloc(1, sizeof(VrModGuard));
    m[1].guards[0] = (VrModGuard){ 0x2000, 0x12345678, 4 };
    m[1].n_xbe_versions = 1;
    m[1].xbe_versions = calloc(1, sizeof(uint32_t));
    m[1].xbe_versions[0] = 9;
    memset(ram, 0, RAM_SIZE);
    vr_mods_set_catalogue(m, 2);

    frames(3, TITLE, VER);
    expect("fixed: the guard does not hold: nothing written",
           rd(0x1000) == 0 && !vr_mods_armed("fixed"));
    wr(0x2000, 0x12345678);
    mark();
    frames(1, TITLE, VER);
    expect("fixed: the guard holds: the word written, armed, said once",
           rd(0x1000) == 0xDEADBEEF && vr_mods_armed("fixed") &&
           log_count("'fixed' armed") == 1);
    wr(0x1000, 0);
    frames(1, TITLE, VER);
    expect("fixed: the game writes it back: asserted again the next frame",
           rd(0x1000) == 0xDEADBEEF);
    m[0].guards[0] = (VrModGuard){ 0x2000, 0x5678, 2 };
    wr(0x1000, 0);
    frames(1, TITLE, VER);
    expect("fixed: a 2-byte guard compares two bytes (0x5678 under 0x12345678)",
           rd(0x1000) == 0xDEADBEEF && vr_mods_armed("fixed"));
    m[0].guards[0] = (VrModGuard){ 0x2000, 0x12345678, 4 };
    expect("fixed: a fix keyed to another certificate version is not bound",
           rd(0x1100) == 0 && !vr_mods_armed("fixed-v9"));
    frames(1, TITLE, 9);
    expect("fixed: ... and is bound under that version",
           rd(0x1100) == 0xCAFEF00D && vr_mods_armed("fixed-v9"));
    memset(ram, 0, RAM_SIZE);
    wr(0x2000, 0x12345678);
    frames(2, TITLE + 1, VER);
    expect("fixed: another title: nothing written, nothing armed",
           rd(0x1000) == 0 && !vr_mods_armed("fixed"));
    frames(2, -1, -1);
    expect("fixed: no title known: nothing written", rd(0x1000) == 0);
    m[0].enabled = false;
    frames(1, TITLE, VER);
    expect("fixed: a disabled fix is not bound when the title binds again",
           rd(0x1000) == 0);
}

static void found(void)
{
    VrMod *m = calloc(1, sizeof(VrMod));
    *m = pgr2_fix();
    memset(ram, 0, RAM_SIZE);
    vr_mods_set_catalogue(m, 1);

    mark();
    frames(1, TITLE, VER);
    expect("F1: not in RAM: nothing written, not armed, said once",
           !vr_mods_armed("pgr2-cockpit") &&
           log_count("not in RAM yet") == 1);
    put_slot(0x5EBCE0 % RAM_SIZE);
    const uint32_t A = 0x5EBCE0 % RAM_SIZE;
    frames(59, TITLE, VER);
    expect("F1: the next search waits (no search for the next 60 frames)",
           stock_at(A));
    frames(2, TITLE, VER);
    expect("F1: ... then finds it and writes at the offsets",
           modded_at(A) && vr_mods_armed("pgr2-cockpit") &&
           log_count("found at 0x000EBCE0") == 1);

    mark();
    frames(20, TITLE, VER);
    expect("F4: our own bytes keep the anchor (no search again)",
           modded_at(A) && log_count("no longer holds") == 0 &&
           log_count("found at") == 0);
    wr(A + 12, f2u(0.9f));
    frames(1, TITLE, VER);
    expect("F4: an original read back is written again", modded_at(A));

    wr(A + 4, f2u(0.25f));
    wr(A + 0, f2u(0.65f));
    mark();
    frames(1, TITLE, VER);
    expect("F3/F4: a byte under a word reads neither: the anchor dropped, "
           "nothing written that frame", rd(A) == f2u(0.65f) &&
           log_count("no longer holds") == 1 && !vr_mods_armed("pgr2-cockpit"));

    memset(ram, 0, RAM_SIZE);
    put_slot(0x20CE0);
    mark();
    frames(1, TITLE, VER);
    expect("moved: found again at once after the anchor drops",
           modded_at(0x20CE0) && log_count("found at 0x00020CE0") == 1);

    memset(ram, 0, RAM_SIZE);
    vr_mods_forget();
    wr(0x24CE0, f2u(0.6f));
    wr(0x24CE4, f2u(-0.1f));
    wr(0x24CE8, f2u(8.5f));
    wr(0x24CEC, f2u(-0.7f));
    mark();
    frames(1, TITLE, VER);
    expect("F1: a state saved with the fix in is adopted (our bytes match)",
           modded_at(0x24CE0) && vr_mods_armed("pgr2-cockpit") &&
           log_count("found at 0x00024CE0") == 1);
    memset(ram, 0, RAM_SIZE);
    put_slot(0x20CE0);
    frames(1, TITLE, VER);
    frames(1, TITLE, VER);

    put_slot(0x30CE0);
    memset(ram + 0x20CE0, 0, 16);
    put_slot(0x40CE0);
    mark();
    frames(3, TITLE, VER);
    expect("F1: two matches: refused, neither written, said once",
           stock_at(0x30CE0) && stock_at(0x40CE0) &&
           log_count("more than once") == 1 && !vr_mods_armed("pgr2-cockpit"));
    memset(ram + 0x40CE0, 0, 16);
    frames(120, TITLE, VER);
    expect("F1: refused stays refused for the binding", stock_at(0x30CE0));

    vr_mods_forget();
    mark();
    frames(1, TITLE, VER);
    expect("F5: after a forget the fix binds from nothing and is found",
           modded_at(0x30CE0) && log_count("found at 0x00030CE0") == 1);
    vr_mods_forget();
    memset(ram + 0x30CE0, 0, 16);
    put_slot(0x50CE0);
    frames(1, TITLE, VER);
    expect("F5: a forget drops the old anchor (no write at the old place)",
           rd(0x30CE0) == 0 && modded_at(0x50CE0));

    memset(ram, 0, RAM_SIZE);
    vr_mods_forget();
    put_slot(0x58CE0);
    m->n_words = 4;
    m->words = realloc(m->words, 4 * sizeof(VrModWord));
    m->words[3] = (VrModWord){ 0x20, 0x11111111, 0x22222222, true };
    wr(0x58CE0 + 0x20, 0x33333333);
    frames(2, TITLE, VER);
    expect("F3: a word past the pattern reads neither: no word written",
           stock_at(0x58CE0) && rd(0x58D00) == 0x33333333 &&
           !vr_mods_armed("pgr2-cockpit"));
    wr(0x58CE0 + 0x20, 0x22222222);
    frames(1, TITLE, VER);
    expect("F3: ... reading its original: all written",
           modded_at(0x58CE0) && rd(0x58D00) == 0x11111111);
    m->n_words = 3;

    memset(ram, 0, RAM_SIZE);
    vr_mods_forget();
    put_slot(0x60CE0);
    m->guards[0].equals = f2u(9.0f);
    frames(2, TITLE, VER);
    expect("guard: a guard by offset that fails: nothing written",
           stock_at(0x60CE0) && !vr_mods_armed("pgr2-cockpit"));
    m->guards[0].equals = f2u(8.5f);
    frames(1, TITLE, VER);
    expect("guard: ... and holding: written", modded_at(0x60CE0));
}

static void watch(void)
{
    VrMod *m = calloc(1, sizeof(VrMod));
    *m = pgr2_fix();
    free(m->words);
    m->words = NULL;
    m->n_words = 0;
    m->slug[0] = 'w';
    m->guards[0] = (VrModGuard){ 0x10, 0, 4 };
    memset(ram, 0, RAM_SIZE);
    vr_mods_set_catalogue(m, 1);
    put_slot(0x31CE0);
    wr(0x31CF0, 1);
    frames(1, TITLE, VER);
    expect("watch: found, the state not holding: not armed, nothing written",
           !vr_mods_armed("wgr2-cockpit") && stock_at(0x31CE0) &&
           rd(0x31CF0) == 1);
    wr(0x31CF0, 0);
    mark();
    frames(1, TITLE, VER);
    expect("watch: the state holds: armed, nothing written, said as a watch",
           vr_mods_armed("wgr2-cockpit") && stock_at(0x31CE0) &&
           log_count("holds (a watch: nothing written)") == 1);
    wr(0x31CF0, 3);
    frames(1, TITLE, VER);
    expect("watch: the state changes: disarmed at the next frame",
           !vr_mods_armed("wgr2-cockpit"));

    memset(ram, 0, RAM_SIZE);
    vr_mods_forget();
    put_slot(0x31CE0);
    put_slot(0x41CE0);
    wr(0x31CF0, 0);
    wr(0x41CF0, 0);
    mark();
    frames(2, TITLE, VER);
    expect("watch: two matches: not armed, waited out (not refused), said "
           "once", !vr_mods_armed("wgr2-cockpit") &&
           log_count("a watch: searching again") == 1 &&
           log_count("refused") == 0);
    memset(ram + 0x41CE0, 0, 32);
    frames(60, TITLE, VER);
    expect("watch: ... one copy left: found at the next search, armed",
           vr_mods_armed("wgr2-cockpit"));
}

static void bounds(void)
{
    VrMod *m = calloc(1, sizeof(VrMod));
    *m = pgr2_fix();
    memset(ram, 0, RAM_SIZE);
    vr_mods_set_catalogue(m, 1);

    put_slot(RAM_SIZE - 16);
    m->words[2].at = 0x10;
    m->words[2].original = 0;
    frames(1, TITLE, VER);
    expect("bounds: a pattern at the very end is found; a word past the end "
           "writes nothing at all", stock_at(RAM_SIZE - 16) &&
           !vr_mods_armed("pgr2-cockpit"));

    memset(ram, 0, RAM_SIZE);
    m->words[2] = (VrModWord){ 0xC, f2u(-0.7f), f2u(0.9f), true };
    vr_mods_forget();
    put_slot(0x70CE2);
    frames(1, TITLE, VER);
    expect("align: an unaligned copy is not a match under align 4",
           stock_at(0x70CE2) && !vr_mods_armed("pgr2-cockpit"));
    m->find.align = 2;
    vr_mods_forget();
    frames(1, TITLE, VER);
    expect("align: ... and is one under align 2", modded_at(0x70CE2));

    memset(ram, 0, RAM_SIZE);
    m->find.align = 4;
    m->find.lo = 0x80000;
    vr_mods_forget();
    put_slot(0x10CE0);
    frames(1, TITLE, VER);
    expect("range: a match below the range is not found", stock_at(0x10CE0));
    put_slot(0x90CE0);
    memset(ram + 0x10CE0, 0, 16);
    vr_mods_forget();
    frames(1, TITLE, VER);
    expect("range: one inside it is", modded_at(0x90CE0));

    memset(ram, 0, RAM_SIZE);
    m->find.lo = 0;
    m->find.mask[8] = m->find.mask[9] = m->find.mask[10] = m->find.mask[11] = 0;
    m->guards[0].equals = f2u(7.0f);
    vr_mods_forget();
    put_slot(0xA0CE0);
    wr(0xA0CE8, f2u(7.0f));
    frames(1, TITLE, VER);
    expect("wildcard: ?? bytes match anything", rd(0xA0CE0) == f2u(0.6f));
}

int main(void)
{
    log_file = tmpfile();
    if (!log_file || dup2(fileno(log_file), 2) < 0) {
        return 2;
    }
    fixed_address();
    found();
    watch();
    bounds();
    vr_mods_set_catalogue(NULL, 0);
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
