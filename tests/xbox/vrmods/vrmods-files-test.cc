/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "vr_mods.h"

static int fails, checks;

static void expect(const char *what, bool ok)
{
    checks++;
    fails += !ok;
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static uint32_t bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

static const char *P = "vr-mods/private/4D53004B/pgr2-cockpit--v1.yaml";

static const std::string GOOD =
    "4D53004B:\n"
    "  name: \"PGR2: cockpit camera (community tweak)\"\n"
    "  kind: fix\n"
    "  version: \"1\"\n"
    "  xbe_versions: [0x00010003]\n"
    "  origin:\n"
    "    name: \"camera_settings.ini cockpit tweak\"\n"
    "    by: \"unknown (a 2003 forum tweak)\"\n"
    "    sources: [\"Ars Technica forum, 2004\"]\n"
    "  find:\n"
    "    floats: [0.65, -0.15, 8.5, 0.9]\n"
    "    range: [0x00100000, 0x04000000]\n"
    "    align: 4\n"
    "  ram:\n"
    "    - { offset: 0x0, float: 0.600, original: 0.650 }\n"
    "    - { offset: 0x4, float: -0.100, original: -0.150 }\n"
    "    - { offset: 0xC, float: -0.700, original: 0.900 }\n"
    "  guards:\n"
    "    - { offset: 0x8, equals: 0x41080000, width: 4 }\n";

static std::string with(const std::string &from, const std::string &to)
{
    std::string s = GOOD;
    size_t i = s.find(from);
    if (i == std::string::npos) {
        std::fprintf(stderr, "test bug: '%s' not in GOOD\n", from.c_str());
        std::exit(2);
    }
    return s.replace(i, from.size(), to);
}

static int parse(const char *path, const std::string &text, VrMod *m,
                 std::string *why)
{
    char w[512] = "";
    int r = vr_mods_parse_file(path, text.data(), text.size(), m, w, sizeof(w));
    *why = w;
    if (r == 1) {
        vr_mods_free(m);
    }
    return r;
}

static void refused(const char *what, const char *path, const std::string &text,
                    const char *needle)
{
    VrMod m;
    std::string why;
    int r = parse(path, text, &m, &why);
    bool ok = r == -1 && why.find(needle) != std::string::npos;
    if (!ok) {
        std::printf("     (r %d, why '%s')\n", r, why.c_str());
    }
    expect(what, ok);
}

static void good(void)
{
    VrMod m;
    char why[512];
    int r = vr_mods_parse_file(P, GOOD.data(), GOOD.size(), &m, why,
                               sizeof(why));
    expect("good: PGR2's shape reads as a fix", r == 1);
    if (r != 1) {
        std::printf("     (%s)\n", why);
        return;
    }
    expect("good: the title id from the folder, the slug and version from the "
           "name, the source path, the name, enabled",
           m.title_id == 0x4D53004Bu && !std::strcmp(m.slug, "pgr2-cockpit") &&
           !std::strcmp(m.version, "1") && !std::strcmp(m.source, P) &&
           !std::strcmp(m.name, "PGR2: cockpit camera (community tweak)") &&
           m.enabled);
    expect("good: xbe_versions", m.n_xbe_versions == 1 &&
           m.xbe_versions[0] == 0x00010003u);
    uint32_t want[4] = { bits(0.65f), bits(-0.15f), bits(8.5f), bits(0.9f) };
    bool pat = m.has_find && m.find.len == 16 &&
               !std::memcmp(m.find.bytes, want, 16) && m.find.align == 4 &&
               m.find.lo == 0x00100000u && m.find.hi == 0x04000000u;
    for (int i = 0; i < 16; i++) {
        pat &= m.find.mask[i] == 0xFF;
    }
    expect("good: find: the floats as little-endian bytes, the range, align",
           pat);
    expect("good: the words: offsets, float bits, originals",
           m.n_words == 3 && m.words[0].at == 0 &&
           m.words[0].value == bits(0.6f) &&
           m.words[0].original == bits(0.65f) && m.words[0].has_original &&
           m.words[1].at == 4 && m.words[1].value == bits(-0.1f) &&
           m.words[2].at == 0xC && m.words[2].value == bits(-0.7f) &&
           m.words[2].original == bits(0.9f));
    expect("good: the guard by offset", m.n_guards == 1 &&
           m.guards[0].at == 8 && m.guards[0].equals == 0x41080000u &&
           m.guards[0].width == 4);
    vr_mods_free(&m);

    std::string lst =
        "4d53004b:\n"
        "  - name: \"fixed\"\n"
        "    kind: fix\n"
        "    version: \"2.1\"\n"
        "    enabled: false\n"
        "    ram:\n"
        "      - { address: 0x1000, value: 0xDEADBEEF }\n"
        "    guards:\n"
        "      - { address: 0x2000, equals: 0x5678, width: 2 }\n";
    r = vr_mods_parse_file("x/4D53004B/fixed--v2.1.yaml", lst.data(),
                           lst.size(), &m, why, sizeof(why));
    expect("list form, lower-case key, a fixed address, enabled false, a "
           "2-byte guard", r == 1 && !m.has_find && !m.enabled &&
           m.words[0].at == 0x1000 && m.words[0].value == 0xDEADBEEFu &&
           !m.words[0].has_original && m.guards[0].width == 2 &&
           m.guards[0].equals == 0x5678);
    if (r == 1) {
        vr_mods_free(&m);
    }
    std::string bytes = with("    floats: [0.65, -0.15, 8.5, 0.9]\n",
                             "    bytes: \"66 66 26 3F ?? ?? ?? ??\"\n");
    r = vr_mods_parse_file(P, bytes.data(), bytes.size(), &m, why, sizeof(why));
    expect("bytes: with ?? wildcards (mask 0)", r == 1 && m.find.len == 8 &&
           m.find.bytes[0] == 0x66 && m.find.bytes[3] == 0x3F &&
           m.find.mask[3] == 0xFF && m.find.mask[4] == 0 &&
           m.find.mask[7] == 0);
    if (r == 1) {
        vr_mods_free(&m);
    }
    std::string noname =
        "4D53004B:\n  kind: fix\n  version: \"1\"\n  enabled: FALSE\n"
        "  ram:\n    - { address: 10, value: 10 }\n"
        "  guards:\n    - { address: 20, equals: 1, width: 2 }\n";
    r = vr_mods_parse_file(P, noname.data(), noname.size(), &m, why,
                           sizeof(why));
    expect("8.4 and the PS2 core's reading: no name is (unnamed), enabled "
           "FALSE is off, '10' is hex 0x10", r == 1 &&
           !std::strcmp(m.name, "(unnamed)") && !m.enabled &&
           m.words[0].at == 0x10 && m.words[0].value == 0x10 &&
           m.guards[0].at == 0x20);
    if (r == 1) {
        vr_mods_free(&m);
    }
    std::string on = with("  version: \"1\"\n", "  version: \"1\"\n  enabled: yes\n");
    r = vr_mods_parse_file(P, on.data(), on.size(), &m, why, sizeof(why));
    expect("enabled: anything but false is on", r == 1 && m.enabled);
    if (r == 1) {
        vr_mods_free(&m);
    }
    std::string conv = "4D53004B:\n  kind: conversion\n  version: \"1\"\n";
    r = vr_mods_parse_file(P, conv.data(), conv.size(), &m, why, sizeof(why));
    expect("a conversion: valid, not applied (0)", r == 0);
}

static void rules(void)
{
    refused("name: no version in the file name", "x/4D53004B/pgr2.yaml", GOOD,
            "version is always in the name");
    refused("name: upper case in the slug", "x/4D53004B/PGR2--v1.yaml", GOOD,
            "file name");
    refused("name: the folder is not 8 hex digits", "x/PGR2/pgr2--v1.yaml",
            GOOD, "8 hex digits");
    refused("key: the top key is not the folder", P,
            with("4D53004B:", "4D530004:"), "is not the folder's");
    refused("key: two top keys", P, GOOD + "4D530004:\n  kind: fix\n",
            "one top key");
    refused("entry: two entries in the list form", P,
            "4D53004B:\n  - { kind: fix }\n  - { kind: fix }\n",
            "one entry per file");
    refused("kind: missing", P, with("  kind: fix\n", ""), "kind is");
    refused("version: not the name's", P,
            with("  version: \"1\"", "  version: \"2\""), "not the file name's");
    refused("PS2 field: ee in an Xbox fix", P, GOOD + "  ee: []\n",
            "PS2 field");
    refused("align: a ram word at an unaligned offset", P,
            with("{ offset: 0x4, float: -0.100", "{ offset: 0x6, float: -0.100"),
            "4-aligned");
    refused("align: a 4-byte guard at an unaligned offset", P,
            with("{ offset: 0x8, equals", "{ offset: 0xA, equals"),
            "aligned to its width");
    refused("8.4: a decimal-looking number is hex, and 9 digits are refused",
            P, with("equals: 0x41080000", "equals: 141080000"), "hex number");
    refused("8.4: a non-hex digit", P,
            with("equals: 0x41080000", "equals: 0x4108G000"), "hex number");
    refused("origin: without 'by'", P,
            with("    by: \"unknown (a 2003 forum tweak)\"\n", ""), "origin:");
    refused("F2: address: under find:", P,
            with("{ offset: 0x0, float: 0.600", "{ address: 0x0, float: 0.600"),
            "says 'offset'");
    refused("F2: both offset and address under find:", P,
            with("{ offset: 0x0, float: 0.600",
                 "{ offset: 0x0, address: 0x10, float: 0.600"),
            "says 'offset'");
    refused("F2: a guard by address under find:", P,
            with("{ offset: 0x8, equals", "{ address: 0x8, equals"),
            "says 'offset'");
    refused("F2: offset: without find:", "x/4D53004B/f--v1.yaml",
            "4D53004B:\n  name: f\n  kind: fix\n  version: \"1\"\n"
            "  ram:\n    - { offset: 0x0, value: 1 }\n",
            "says 'address'");
    refused("F3: a word under find: without original", P,
            with(", original: 0.650 }", " }"), "carries 'original'");
    refused("F6: value and float both", P,
            with("{ offset: 0x0, float: 0.600,",
                 "{ offset: 0x0, value: 1, float: 0.600,"),
            "exactly one of 'value' and 'float'");
    refused("F6: two pattern forms", P,
            with("    align: 4\n", "    align: 4\n    words: [1]\n"),
            "exactly one of 'floats'");
    std::string big = "    floats: [";
    for (int i = 0; i < 65; i++) {
        big += i ? ", 1.0" : "1.0";
    }
    big += "]\n";
    refused("F6: a pattern over 256 bytes", P,
            with("    floats: [0.65, -0.15, 8.5, 0.9]\n", big), "1 to 256");
    refused("find: align 3", P, with("    align: 4\n", "    align: 3\n"),
            "align is 1, 2 or 4");
    refused("find: range lo >= hi", P,
            with("[0x00100000, 0x04000000]", "[0x04000000, 0x00100000]"),
            "lo < hi");
    refused("guard: width 3", P, with("width: 4 }", "width: 3 }"),
            "width is 1, 2 or 4");
    refused("number: a value past 32 bits", P,
            with("equals: 0x41080000", "equals: 0x141080000"), "hex number");
    refused("number: a negative address", "x/4D53004B/f--v1.yaml",
            "4D53004B:\n  name: f\n  kind: fix\n  version: \"1\"\n"
            "  ram:\n    - { address: -4, value: 1 }\n",
            "hex number");
    refused("ram: none", P,
            with("  ram:\n    - { offset: 0x0, float: 0.600, original: 0.650 }\n"
                 "    - { offset: 0x4, float: -0.100, original: -0.150 }\n"
                 "    - { offset: 0xC, float: -0.700, original: 0.900 }\n", ""),
            "at least one 'ram' word");
    refused("YAML: a syntax error is refused, not a crash", P,
            "4D53004B:\n  name: [unclosed\n  kind: fix\n", "YAML");
}

static void write_file(const std::string &path, const std::string &text)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
}

static void scan(void)
{
    char tmpl[] = "/tmp/vrmods-test-XXXXXX";
    std::string root = mkdtemp(tmpl);
    std::string user = root + "/user", ship = root + "/ship";
    for (const std::string &d :
         { user, user + "/4D53004B", ship, ship + "/public", ship + "/private",
           ship + "/private/4D53004B", ship + "/public/4D530004" }) {
        mkdir(d.c_str(), 0700);
    }
    write_file(ship + "/private/4D53004B/pgr2-cockpit--v1.yaml", GOOD);
    write_file(user + "/4D53004B/pgr2-cockpit--v1.yaml",
               with("PGR2: cockpit camera (community tweak)", "user copy"));
    write_file(ship + "/private/4D53004B/broken.yaml", GOOD);
    write_file(ship + "/public/4D530004/halo--v1.yaml",
               "4D530004:\n  name: halo\n  kind: fix\n  version: \"1\"\n"
               "  ram:\n    - { address: 0x10, value: 1 }\n");
    VrMod *mods = nullptr;
    int n = vr_mods_load(user.c_str(), ship.c_str(), &mods);
    bool user_won = false, halo = false, shipped_pgr2 = false;
    for (int i = 0; i < n; i++) {
        user_won |= !std::strcmp(mods[i].name, "user copy");
        shipped_pgr2 |= !std::strcmp(mods[i].name,
                                     "PGR2: cockpit camera (community tweak)");
        halo |= mods[i].title_id == 0x4D530004u;
    }
    expect("scan: two fixes read: the user's PGR2 file hides the shipped one, "
           "public/ read, the broken name refused",
           n == 2 && user_won && !shipped_pgr2 && halo);
    vr_mods_set_catalogue(mods, n);
    vr_mods_set_catalogue(nullptr, 0);
    VrMod *none = nullptr;
    expect("scan: no folders: none, no crash",
           vr_mods_load(nullptr, (root + "/nothing").c_str(), &none) == 0 &&
           none == nullptr);
    std::string cmd = "rm -rf '" + root + "'";
    if (std::system(cmd.c_str()) != 0) {
        std::fprintf(stderr, "could not remove %s\n", root.c_str());
    }
}

int main(void)
{
    FILE *log = std::tmpfile();
    if (!log || dup2(fileno(log), 2) < 0) {
        return 2;
    }
    good();
    rules();
    scan();
    std::printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
