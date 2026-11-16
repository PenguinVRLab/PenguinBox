/*
 * Copyright (C) 2026 Patrick Carey
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xemu-yaml-lite.h"

static void dump(const YlNode *n, char **o, size_t *len, size_t *cap)
{
#define PUT(s) do { size_t l_ = strlen(s); while (*len + l_ + 1 > *cap) { *cap *= 2; *o = realloc(*o, *cap); } \
    memcpy(*o + *len, s, l_ + 1); *len += l_; } while (0)
    if (!n || n->type == YL_NULL) {
        PUT("~");
    } else if (n->type == YL_SCALAR) {
        PUT("\"");
        for (const char *c = n->str; *c; c++) {
            char e[8];
            if (*c == '"' || *c == '\\') {
                snprintf(e, sizeof(e), "\\%c", *c);
            } else if (*c == '\n') {
                snprintf(e, sizeof(e), "\\n");
            } else {
                snprintf(e, sizeof(e), "%c", *c);
            }
            PUT(e);
        }
        PUT("\"");
    } else {
        PUT(n->type == YL_MAP ? "{" : "[");
        for (size_t i = 0; i < n->n; i++) {
            if (i) {
                PUT(",");
            }
            if (n->type == YL_MAP) {
                PUT("\"");
                PUT(n->keys[i]);
                PUT("\":");
            }
            dump(n->items[i], o, len, cap);
        }
        PUT(n->type == YL_MAP ? "}" : "]");
    }
#undef PUT
}

static char *canon(const YlNode *n)
{
    size_t len = 0, cap = 64;
    char *o = malloc(cap);
    o[0] = 0;
    dump(n, &o, &len, &cap);
    return o;
}

static int fails, checks;

static void ok(const char *label, const char *yaml, const char *want)
{
    char err[256];
    checks++;
    YlNode *r = yl_parse(yaml, strlen(yaml), err, sizeof(err));
    if (!r) {
        fails++;
        printf("FAIL %s: refused: %s\n", label, err);
        return;
    }
    char *got = canon(r);
    if (strcmp(got, want)) {
        fails++;
        printf("FAIL %s:\n  got  %s\n  want %s\n", label, got, want);
    } else {
        printf("ok   %s\n", label);
    }
    free(got);
    yl_free(r);
}

static void bad(const char *label, const char *yaml, const char *want)
{
    char err[256];
    checks++;
    YlNode *r = yl_parse(yaml, strlen(yaml), err, sizeof(err));
    if (r) {
        char *got = canon(r);
        fails++;
        printf("FAIL %s: accepted: %s\n", label, got);
        free(got);
        yl_free(r);
        return;
    }
    if (!strstr(err, want)) {
        fails++;
        printf("FAIL %s: reason '%s', want '%s'\n", label, err, want);
        return;
    }
    printf("ok   %s refused: %s\n", label, err);
}

int main(int argc, char **argv)
{
    ok("pcsx2-VR mod-DB shape",
       "# a mod profile\n"
       "\"43430009\":\n"
       "  - name: \"base game\"          # no hash: any XBE\n"
       "    recommends: { machine: { ram_mb: 128 } }\n"
       "  - name: 'mod v1'\n"
       "    xbe_sha256: [ab12, cd34]\n"
       "    basedOn: \"43430009\"\n"
       "    requires:\n"
       "      machine:\n"
       "        ram_mb: 128\n",
       "{\"43430009\":[{\"name\":\"base game\",\"recommends\":{\"machine\":{\"ram_mb\":\"128\"}}},"
       "{\"name\":\"mod v1\",\"xbe_sha256\":[\"ab12\",\"cd34\"],\"basedOn\":\"43430009\","
       "\"requires\":{\"machine\":{\"ram_mb\":\"128\"}}}]}");
    ok("vr-profiles title map with variants",
       "---\n"
       "enabled: true\n"
       "name: My mod\n"
       "\"53430005\":\n"
       "  name: \"A Title\"\n"
       "  variants:\n"
       "    - name: \"retail\"\n"
       "      basedOn: null\n"
       "  writes:\n"
       "  - id: cabin\n"
       "    when:\n"
       "      - { address: 0x006A2BA0, equals: 4, width: 4 }\n"
       "    once: true\n"
       "  requires:\n"
       "    renderer: VULKAN\n",
       "{\"enabled\":\"true\",\"name\":\"My mod\",\"53430005\":{\"name\":\"A Title\",\"variants\":"
       "[{\"name\":\"retail\",\"basedOn\":~}],\"writes\":[{\"id\":\"cabin\",\"when\":[{\"address\":"
       "\"0x006A2BA0\",\"equals\":\"4\",\"width\":\"4\"}],\"once\":\"true\"}],\"requires\":"
       "{\"renderer\":\"VULKAN\"}}}");
    ok("scalars and comments",
       "a: plain text with 'quotes', a#hash-inside and a url http://x/y#z   # a comment\n"
       "b: \"esc \\\"q\\\" \\\\ \\u00e9 # not a comment\"\n"
       "c: 'it''s'\n"
       "d: ~\n"
       "e:\n"
       "f: [ ]\n"
       "g: {}\n"
       "h: x:y\n",
       "{\"a\":\"plain text with 'quotes', a#hash-inside and a url http://x/y#z\",\"b\":\"esc \\\"q\\\" \\\\ \xc3\xa9 # not a comment\","
       "\"c\":\"it's\",\"d\":~,\"e\":~,\"f\":[],\"g\":{},\"h\":\"x:y\"}");
    ok("block scalars",
       "lit: |\n"
       "  one\n"
       "    two # kept\n"
       "\n"
       "  three\n"
       "fold: >-\n"
       "  a b\n"
       "  c\n"
       "\n"
       "  d\n"
       "keep: |+\n"
       "  x\n"
       "\n"
       "end: 1\n",
       "{\"lit\":\"one\\n  two # kept\\n\\nthree\\n\",\"fold\":\"a b c\\nd\",\"keep\":\"x\\n\\n\",\"end\":\"1\"}");
    ok("nested sequences and empty items",
       "- - a\n"
       "  - b\n"
       "-\n"
       "- [1, [2, 3], {k: v}]\n",
       "[[\"a\",\"b\"],~,[\"1\",[\"2\",\"3\"],{\"k\":\"v\"}]]");
    ok("empty document", "# only a comment\n\n", "~");
    ok("CRLF line ends", "a: 1\r\nb:\r\n  - x\r\n", "{\"a\":\"1\",\"b\":[\"x\"]}");

    bad("anchor", "a: &x 1\nb: 2\n", "anchors");
    bad("alias", "a: 1\nb: *a\n", "aliases");
    bad("tag", "a: !!str 1\n", "tags");
    bad("directive", "%YAML 1.2\n---\na: 1\n", "directives");
    bad("second document", "a: 1\n---\nb: 2\n", "second document");
    bad("document end", "a: 1\n...\n", "document end");
    bad("tab indentation", "a:\n\tb: 1\n", "tab");
    bad("duplicate key", "a: 1\nb: 2\na: 3\n", "duplicate key 'a'");
    bad("duplicate flow key", "a: { x: 1, x: 2 }\n", "duplicate key 'x'");
    bad("unterminated quote", "a: \"open\n  more\"\n", "unterminated");
    bad("multi-line flow", "a: [1,\n  2]\n", "flow collection must close");
    bad("explicit key", "? a\n: 1\n", "explicit keys");
    bad("bad indentation", "a: 1\n   b: 2\n", "unexpected indentation");
    bad("value with a colon", "a: b: c\n", "mapping value is not allowed");
    bad("block indentation indicator", "a: |2\n  x\n", "block scalar indicators");
    bad("unknown escape", "a: \"\\q\"\n", "unknown escape");
    bad("text after a quoted scalar", "a: \"x\" y\n", "text after a quoted scalar");
    bad("lone scalar in a mapping", "a: 1\njust text\n", "cannot stand alone");
    {
        const char *y = "n: 0x10\nm: -7\nq: \"5\"\nt: true\nf: False\nx: maybe\n";
        char err[256];
        YlNode *r = yl_parse(y, strlen(y), err, sizeof(err));
        long long v;
        checks++;
        if (!r || !yl_int(yl_get(r, "n"), &v) || v != 16 || !yl_int(yl_get(r, "m"), &v) || v != -7 ||
            yl_int(yl_get(r, "q"), &v) || yl_bool(yl_get(r, "t")) != 1 || yl_bool(yl_get(r, "f")) != 0 ||
            yl_bool(yl_get(r, "x")) != -1 || yl_get(r, "missing") || yl_len(r) != 6 ||
            strcmp(yl_key_at(r, 2), "q") || yl_str(yl_at(r, 5)) == NULL) {
            fails++;
            printf("FAIL accessors\n");
        } else {
            printf("ok   accessors (hex, negative, quoted is not a number, bools)\n");
        }
        yl_free(r);
    }
    for (int i = 1; i < argc; i++) {
        char err[256];
        YlNode *r = yl_parse_file(argv[i], err, sizeof(err));
        checks++;
        if (!r) {
            fails++;
            printf("FAIL %s: %s\n", argv[i], err);
            continue;
        }
        char *c = canon(r);
        printf("file %s %s\n", argv[i], c);
        free(c);
        yl_free(r);
    }
    printf("yaml-lite-test: %s (%d checks, %d failed)\n", fails ? "FAIL" : "PASS", checks, fails);
    return fails ? 1 : 0;
}
