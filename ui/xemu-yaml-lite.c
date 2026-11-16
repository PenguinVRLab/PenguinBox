/*
 * Copyright (C) 2026 Patrick Carey
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef XEMU_YAML_LITE_STANDALONE
#include "qemu/osdep.h"
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#endif
#include "xemu-yaml-lite.h"

#define MAX_DEPTH 64
#define MAX_FILE (4 * 1024 * 1024)

typedef struct Line {
    int no;
    int indent;
    char *raw;
    char *text;
} Line;

typedef struct P {
    Line *l;
    int n, i;
    char *err;
    size_t errlen;
    int failed;
    int depth;
} P;

static void fail(P *p, int line, const char *fmt, ...)
{
    if (p->failed) {
        return;
    }
    p->failed = 1;
    if (p->err && p->errlen) {
        char msg[256];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(msg, sizeof(msg), fmt, ap);
        va_end(ap);
        snprintf(p->err, p->errlen, "line %d: %s", line, msg);
    }
}

static char *dupn(const char *s, size_t n)
{
    char *d = malloc(n + 1);
    if (d) {
        memcpy(d, s, n);
        d[n] = 0;
    }
    return d;
}

static YlNode *node(YlType t, int line)
{
    YlNode *x = calloc(1, sizeof(*x));
    if (x) {
        x->type = t;
        x->line = line;
    }
    return x;
}

void yl_free(YlNode *x)
{
    if (!x) {
        return;
    }
    for (size_t i = 0; i < x->n; i++) {
        if (x->keys) {
            free(x->keys[i]);
        }
        yl_free(x->items[i]);
    }
    free(x->keys);
    free(x->items);
    free(x->str);
    free(x);
}

static int push(P *p, YlNode *c, char *key, YlNode *v, int line)
{
    if (!v) {
        free(key);
        return -1;
    }
    if (c->type == YL_MAP) {
        for (size_t i = 0; i < c->n; i++) {
            if (!strcmp(c->keys[i], key)) {
                fail(p, line, "duplicate key '%s'", key);
                free(key);
                yl_free(v);
                return -1;
            }
        }
    }
    if (c->n == c->cap) {
        size_t nc = c->cap ? c->cap * 2 : 4;
        YlNode **ni = realloc(c->items, nc * sizeof(*ni));
        if (!ni) {
            free(key);
            yl_free(v);
            return -1;
        }
        c->items = ni;
        if (c->type == YL_MAP) {
            char **nk = realloc(c->keys, nc * sizeof(*nk));
            if (!nk) {
                free(key);
                yl_free(v);
                return -1;
            }
            c->keys = nk;
        }
        c->cap = nc;
    }
    if (c->type == YL_MAP) {
        c->keys[c->n] = key;
    }
    c->items[c->n++] = v;
    return 0;
}

static void put_utf8(char **o, unsigned long c)
{
    char *d = *o;
    if (c < 0x80) {
        *d++ = (char)c;
    } else if (c < 0x800) {
        *d++ = (char)(0xC0 | (c >> 6));
        *d++ = (char)(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        *d++ = (char)(0xE0 | (c >> 12));
        *d++ = (char)(0x80 | ((c >> 6) & 0x3F));
        *d++ = (char)(0x80 | (c & 0x3F));
    } else {
        *d++ = (char)(0xF0 | (c >> 18));
        *d++ = (char)(0x80 | ((c >> 12) & 0x3F));
        *d++ = (char)(0x80 | ((c >> 6) & 0x3F));
        *d++ = (char)(0x80 | (c & 0x3F));
    }
    *o = d;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static char *quoted(P *p, int line, const char **s)
{
    const char *q = *s;
    char quote = *q++;
    char *out = malloc(strlen(q) * 4 + 1), *o = out;
    if (!out) {
        return NULL;
    }
    for (;;) {
        char c = *q;
        if (!c) {
            fail(p, line, "unterminated quoted scalar (multi-line quoted scalars are not supported)");
            free(out);
            return NULL;
        }
        if (quote == '\'') {
            if (c == '\'') {
                if (q[1] == '\'') {
                    *o++ = '\'';
                    q += 2;
                    continue;
                }
                q++;
                break;
            }
            *o++ = c;
            q++;
            continue;
        }
        if (c == '"') {
            q++;
            break;
        }
        if (c != '\\') {
            *o++ = c;
            q++;
            continue;
        }
        q++;
        int digits = 0;
        switch (*q) {
        case '\\': *o++ = '\\'; break;
        case '"': *o++ = '"'; break;
        case '/': *o++ = '/'; break;
        case 'n': *o++ = '\n'; break;
        case 't': *o++ = '\t'; break;
        case 'r': *o++ = '\r'; break;
        case '0': *o++ = '\0'; break;
        case 'x': digits = 2; break;
        case 'u': digits = 4; break;
        default:
            fail(p, line, "unknown escape '\\%c' in a double-quoted scalar", *q ? *q : '?');
            free(out);
            return NULL;
        }
        if (digits) {
            unsigned long v = 0;
            for (int k = 1; k <= digits; k++) {
                int h = hexval(q[k]);
                if (h < 0) {
                    fail(p, line, "bad \\%c escape", *q);
                    free(out);
                    return NULL;
                }
                v = v * 16 + (unsigned long)h;
            }
            put_utf8(&o, v);
            q += digits;
        }
        q++;
    }
    *o = 0;
    *s = q;
    return out;
}

static int is_null_word(const char *s, size_t n)
{
    return (n == 1 && s[0] == '~') || (n == 4 && (!strncmp(s, "null", 4) ||
            !strncmp(s, "Null", 4) || !strncmp(s, "NULL", 4)));
}

static int refuse_lead(P *p, int line, char c)
{
    if (c == '&') {
        fail(p, line, "anchors (&) are not supported");
    } else if (c == '*') {
        fail(p, line, "aliases (*) are not supported");
    } else if (c == '!') {
        fail(p, line, "tags (!) are not supported");
    } else if (c == '%') {
        fail(p, line, "directives (%%) are not supported");
    } else if (c == '?') {
        fail(p, line, "explicit keys (?) are not supported");
    } else if (c == '@' || c == '`') {
        fail(p, line, "'%c' cannot start a plain scalar", c);
    } else {
        return 0;
    }
    return 1;
}

static YlNode *plain_or_null(const char *s, size_t n, int line)
{
    if (is_null_word(s, n)) {
        return node(YL_NULL, line);
    }
    YlNode *x = node(YL_SCALAR, line);
    if (x) {
        x->str = dupn(s, n);
    }
    return x;
}

static void skip_ws(const char **s)
{
    while (**s == ' ' || **s == '\t') {
        (*s)++;
    }
}

static YlNode *flow_value(P *p, int line, const char **s, int in_flow);

static YlNode *flow_scalar(P *p, int line, const char **s, const char *stop)
{
    skip_ws(s);
    if (**s == '"' || **s == '\'') {
        char *q = quoted(p, line, s);
        if (!q) {
            return NULL;
        }
        YlNode *x = node(YL_SCALAR, line);
        if (!x) {
            free(q);
            return NULL;
        }
        x->str = q;
        x->quoted = true;
        return x;
    }
    if (refuse_lead(p, line, **s)) {
        return NULL;
    }
    const char *b = *s;
    while (**s && !strchr(stop, **s)) {
        if (**s == ':' && ((*s)[1] == ' ' || strchr(stop, (*s)[1]))) {
            break;
        }
        (*s)++;
    }
    const char *e = *s;
    while (e > b && (e[-1] == ' ' || e[-1] == '\t')) {
        e--;
    }
    return plain_or_null(b, (size_t)(e - b), line);
}

static YlNode *flow_value(P *p, int line, const char **s, int in_flow)
{
    if (++p->depth > MAX_DEPTH) {
        fail(p, line, "nested too deeply");
        return NULL;
    }
    skip_ws(s);
    YlNode *r = NULL;
    if (**s == '{' || **s == '[') {
        char open = **s, close = open == '{' ? '}' : ']';
        r = node(open == '{' ? YL_MAP : YL_SEQ, line);
        if (!r) {
            return NULL;
        }
        (*s)++;
        skip_ws(s);
        if (**s == close) {
            (*s)++;
            p->depth--;
            return r;
        }
        for (;;) {
            char *key = NULL;
            if (open == '{') {
                YlNode *k = flow_scalar(p, line, s, ":,}");
                if (!k) {
                    yl_free(r);
                    return NULL;
                }
                key = k->type == YL_SCALAR ? k->str : NULL;
                k->str = NULL;
                yl_free(k);
                if (!key) {
                    fail(p, line, "a null key is not supported");
                    yl_free(r);
                    return NULL;
                }
                skip_ws(s);
                if (**s != ':') {
                    fail(p, line, "expected ':' after the key '%s' in a flow mapping", key);
                    free(key);
                    yl_free(r);
                    return NULL;
                }
                (*s)++;
            }
            skip_ws(s);
            YlNode *v;
            if (open == '{' && (**s == ',' || **s == '}')) {
                v = node(YL_NULL, line);
            } else {
                v = flow_value(p, line, s, 1);
            }
            if (!v || push(p, r, key, v, line)) {
                if (!v) {
                    free(key);
                }
                yl_free(r);
                return NULL;
            }
            skip_ws(s);
            if (**s == ',') {
                (*s)++;
                skip_ws(s);
                if (**s == close) {
                    (*s)++;
                    break;
                }
                continue;
            }
            if (**s == close) {
                (*s)++;
                break;
            }
            fail(p, line, **s ? "expected ',' or '%c' in a flow collection, saw '%c'"
                              : "a flow collection must close on its own line (expected '%c')",
                 close, **s);
            yl_free(r);
            return NULL;
        }
    } else if (in_flow) {
        r = flow_scalar(p, line, s, ",]}");
    } else {
        fail(p, line, "internal: not a flow value");
    }
    p->depth--;
    return r;
}

static void strip_comment(char *t)
{
    char q = 0;
    for (char *c = t; *c; c++) {
        if (q) {
            if (*c == q) {
                if (q == '\'' && c[1] == '\'') {
                    c++;
                    continue;
                }
                q = 0;
            } else if (q == '"' && *c == '\\' && c[1]) {
                c++;
            }
            continue;
        }
        if ((*c == '"' || *c == '\'') && (c == t || c[-1] == ' ' || c[-1] == '\t' ||
                                         strchr("[{,:", c[-1]))) {
            q = *c;
            continue;
        }
        if (*c == '#' && (c == t || c[-1] == ' ' || c[-1] == '\t')) {
            *c = 0;
            break;
        }
    }
    size_t n = strlen(t);
    while (n && (t[n - 1] == ' ' || t[n - 1] == '\t' || t[n - 1] == '\r')) {
        t[--n] = 0;
    }
}

static int next_content(P *p, int i)
{
    while (i < p->n && !p->l[i].text[0]) {
        i++;
    }
    return i;
}

static YlNode *block(P *p, int indent);

static int looks_like_key(const P *p, int line, const char *t)
{
    if (*t == '"' || *t == '\'') {
        P probe = *p;
        probe.err = NULL;
        probe.failed = 0;
        const char *s = t;
        char *k = quoted(&probe, line, &s);
        if (!k) {
            return 0;
        }
        free(k);
        skip_ws(&s);
        return *s == ':' && (s[1] == ' ' || !s[1]);
    }
    if (*t == '{' || *t == '[') {
        return 0;
    }
    for (const char *c = t; *c; c++) {
        if (*c == ':' && (c[1] == ' ' || !c[1])) {
            return 1;
        }
    }
    return 0;
}

static YlNode *inline_value(P *p, Line *L, const char *v, int ind, int key_ctx)
{
    if (!*v) {
        int j = next_content(p, p->i);
        if (j < p->n && p->l[j].indent > ind) {
            p->i = j;
            return block(p, p->l[j].indent);
        }
        if (key_ctx && j < p->n && p->l[j].indent == ind && p->l[j].text[0] == '-' &&
            (p->l[j].text[1] == ' ' || !p->l[j].text[1])) {
            p->i = j;
            return block(p, ind);
        }
        return node(YL_NULL, L->no);
    }
    if (*v == '|' || *v == '>') {
        char style = *v;
        int chomp = 0;
        const char *m = v + 1;
        if (*m == '-' || *m == '+') {
            chomp = *m == '-' ? -1 : 1;
            m++;
        }
        if (*m) {
            fail(p, L->no, "block scalar indicators other than | > - + are not supported");
            return NULL;
        }
        int j = p->i, bind = -1;
        size_t cap = 64, len = 0;
        char *out = malloc(cap);
        int blanks = 0;
        if (!out) {
            return NULL;
        }
        while (j < p->n) {
            Line *b = &p->l[j];
            const char *r = b->raw;
            int k = 0;
            while (r[k] == ' ') {
                k++;
            }
            int blank = !r[k] || r[k] == '\r';
            if (!blank && k <= ind) {
                break;
            }
            if (!blank && bind < 0) {
                bind = k;
            }
            if (!blank && k < bind) {
                fail(p, b->no, "block scalar line indented less than its first line");
                free(out);
                return NULL;
            }
            j++;
            if (blank) {
                blanks++;
                continue;
            }
            const char *txt = r + bind;
            size_t tl = strlen(txt);
            while (tl && txt[tl - 1] == '\r') {
                tl--;
            }
            size_t need = len + (size_t)blanks + tl + 2;
            if (need > cap) {
                while (cap < need) {
                    cap *= 2;
                }
                char *no = realloc(out, cap);
                if (!no) {
                    free(out);
                    return NULL;
                }
                out = no;
            }
            if (len) {
                if (style == '|') {
                    out[len++] = '\n';
                    for (int z = 0; z < blanks; z++) {
                        out[len++] = '\n';
                    }
                } else if (blanks) {
                    for (int z = 0; z < blanks; z++) {
                        out[len++] = '\n';
                    }
                } else {
                    out[len++] = ' ';
                }
            }
            blanks = 0;
            memcpy(out + len, txt, tl);
            len += tl;
        }
        if (chomp >= 0 && len) {
            out[len++] = '\n';
        }
        if (chomp > 0) {
            for (int z = 0; z < blanks; z++) {
                out[len++] = '\n';
            }
        }
        out[len] = 0;
        p->i = j;
        YlNode *x = node(YL_SCALAR, L->no);
        if (!x) {
            free(out);
            return NULL;
        }
        x->str = out;
        return x;
    }
    if (*v == '{' || *v == '[') {
        const char *s = v;
        YlNode *x = flow_value(p, L->no, &s, 0);
        if (x) {
            skip_ws(&s);
            if (*s) {
                fail(p, L->no, "text after a flow collection: '%s'", s);
                yl_free(x);
                return NULL;
            }
        }
        return x;
    }
    if (*v == '"' || *v == '\'') {
        const char *s = v;
        char *q = quoted(p, L->no, &s);
        if (!q) {
            return NULL;
        }
        skip_ws(&s);
        if (*s) {
            fail(p, L->no, "text after a quoted scalar: '%s'", s);
            free(q);
            return NULL;
        }
        YlNode *x = node(YL_SCALAR, L->no);
        if (!x) {
            free(q);
            return NULL;
        }
        x->str = q;
        x->quoted = true;
        return x;
    }
    if (refuse_lead(p, L->no, *v)) {
        return NULL;
    }
    const char *colon = strstr(v, ": ");
    if (colon || (strlen(v) > 1 && v[strlen(v) - 1] == ':')) {
        fail(p, L->no, "a mapping value is not allowed here (quote the scalar)");
        return NULL;
    }
    return plain_or_null(v, strlen(v), L->no);
}

static char *split_key(P *p, Line *L, char *t, const char **val)
{
    if (*t == '"' || *t == '\'') {
        const char *s = t;
        char *k = quoted(p, L->no, &s);
        if (!k) {
            return NULL;
        }
        skip_ws(&s);
        if (*s != ':' || (s[1] && s[1] != ' ')) {
            fail(p, L->no, "expected ':' after a quoted key");
            free(k);
            return NULL;
        }
        s++;
        skip_ws(&s);
        *val = s;
        return k;
    }
    if (refuse_lead(p, L->no, *t)) {
        return NULL;
    }
    char *c = t;
    for (; *c; c++) {
        if (*c == ':' && (c[1] == ' ' || !c[1])) {
            break;
        }
    }
    if (!*c) {
        fail(p, L->no, "expected 'key: value' (a scalar cannot stand alone in a mapping)");
        return NULL;
    }
    char *e = c;
    while (e > t && (e[-1] == ' ' || e[-1] == '\t')) {
        e--;
    }
    if (e == t) {
        fail(p, L->no, "empty key");
        return NULL;
    }
    const char *s = c + 1;
    skip_ws(&s);
    *val = s;
    return dupn(t, (size_t)(e - t));
}

static YlNode *mapping(P *p, int ind)
{
    YlNode *m = node(YL_MAP, p->l[p->i].no);
    if (!m) {
        return NULL;
    }
    while (!p->failed) {
        p->i = next_content(p, p->i);
        if (p->i >= p->n) {
            break;
        }
        Line *L = &p->l[p->i];
        if (L->indent < ind) {
            break;
        }
        if (L->indent > ind) {
            fail(p, L->no, "unexpected indentation");
            break;
        }
        if (L->text[0] == '-' && (L->text[1] == ' ' || !L->text[1])) {
            break;
        }
        const char *val;
        char *key = split_key(p, L, L->text, &val);
        if (!key) {
            break;
        }
        p->i++;
        YlNode *v = inline_value(p, L, val, ind, 1);
        if (push(p, m, key, v, L->no)) {
            break;
        }
    }
    if (p->failed) {
        yl_free(m);
        return NULL;
    }
    return m;
}

static YlNode *sequence(P *p, int ind)
{
    YlNode *q = node(YL_SEQ, p->l[p->i].no);
    if (!q) {
        return NULL;
    }
    while (!p->failed) {
        p->i = next_content(p, p->i);
        if (p->i >= p->n) {
            break;
        }
        Line *L = &p->l[p->i];
        if (L->indent < ind) {
            break;
        }
        if (L->indent > ind) {
            fail(p, L->no, "unexpected indentation");
            break;
        }
        if (!(L->text[0] == '-' && (L->text[1] == ' ' || !L->text[1]))) {
            break;
        }
        char *rest = L->text + 1;
        while (*rest == ' ') {
            rest++;
        }
        YlNode *v;
        if (!*rest) {
            p->i++;
            v = inline_value(p, L, "", ind, 0);
        } else {
            int is_map = looks_like_key(p, L->no, rest);
            int is_seq = rest[0] == '-' && (rest[1] == ' ' || !rest[1]);
            if (is_map || is_seq) {
                int col = L->indent + (int)(rest - L->text);
                L->indent = col;
                memmove(L->text, rest, strlen(rest) + 1);
                v = is_seq ? sequence(p, col) : mapping(p, col);
            } else {
                p->i++;
                v = inline_value(p, L, rest, ind, 0);
            }
        }
        if (push(p, q, NULL, v, L->no)) {
            break;
        }
    }
    if (p->failed) {
        yl_free(q);
        return NULL;
    }
    return q;
}

static YlNode *block(P *p, int indent)
{
    if (++p->depth > MAX_DEPTH) {
        fail(p, p->l[p->i].no, "nested too deeply");
        return NULL;
    }
    Line *L = &p->l[p->i];
    YlNode *r;
    if (L->text[0] == '-' && (L->text[1] == ' ' || !L->text[1])) {
        r = sequence(p, indent);
    } else if (!looks_like_key(p, L->no, L->text)) {
        p->i++;
        r = inline_value(p, L, L->text, indent - 1, 0);
        int j = next_content(p, p->i);
        if (r && j < p->n && p->l[j].indent >= indent) {
            fail(p, p->l[j].no, "unexpected content after a scalar");
            yl_free(r);
            r = NULL;
        }
    } else {
        r = mapping(p, indent);
    }
    p->depth--;
    return r;
}

YlNode *yl_parse(const char *text, size_t len, char *err, size_t errlen)
{
    P p = { .err = err, .errlen = errlen };
    if (err && errlen) {
        err[0] = 0;
    }
    char *buf = dupn(text, len);
    if (!buf) {
        return NULL;
    }
    if (memchr(text, 0, len)) {
        fail(&p, 1, "NUL byte in the file");
        free(buf);
        return NULL;
    }
    int cap = 64;
    p.l = calloc((size_t)cap, sizeof(Line));
    int no = 0, seen_content = 0;
    char *save = buf;
    while (save && !p.failed) {
        char *nl = strchr(save, '\n');
        if (nl) {
            *nl = 0;
        }
        no++;
        if (no == 1 && (unsigned char)save[0] == 0xEF && (unsigned char)save[1] == 0xBB &&
            (unsigned char)save[2] == 0xBF) {
            save += 3;
        }
        if (p.n == cap) {
            cap *= 2;
            Line *nl2 = realloc(p.l, (size_t)cap * sizeof(Line));
            if (!nl2) {
                break;
            }
            p.l = nl2;
        }
        Line *L = &p.l[p.n];
        L->no = no;
        L->raw = save;
        int k = 0;
        while (save[k] == ' ') {
            k++;
        }
        L->indent = k;
        L->text = dupn(save + k, strlen(save + k));
        if (!L->text) {
            break;
        }
        if (save[k] == '\t') {
            fail(&p, no, "a tab in the indentation (YAML indents with spaces)");
        }
        strip_comment(L->text);
        if (L->text[0] && k == 0) {
            if (!strcmp(L->text, "---") || !strncmp(L->text, "--- ", 4)) {
                if (seen_content) {
                    fail(&p, no, "a second document ('---') is not supported");
                } else if (L->text[3]) {
                    fail(&p, no, "content on the '---' line is not supported");
                }
                L->text[0] = 0;
            } else if (!strcmp(L->text, "...")) {
                fail(&p, no, "a document end marker ('...') is not supported");
            } else if (L->text[0] == '%') {
                fail(&p, no, "directives (%%) are not supported");
            }
        }
        if (L->text[0]) {
            seen_content = 1;
        }
        p.n++;
        save = nl ? nl + 1 : NULL;
    }

    YlNode *root = NULL;
    if (!p.failed) {
        p.i = next_content(&p, 0);
        if (p.i >= p.n) {
            root = node(YL_NULL, 1);
        } else {
            root = block(&p, p.l[p.i].indent);
            p.i = next_content(&p, p.i);
            if (root && p.i < p.n) {
                fail(&p, p.l[p.i].no, p.l[p.i].indent ? "unexpected indentation"
                                                      : "unexpected content");
            }
            if (p.failed) {
                yl_free(root);
                root = NULL;
            }
        }
    }
    for (int i = 0; i < p.n; i++) {
        free(p.l[i].text);
    }
    free(p.l);
    free(buf);
    if (!root && err && errlen && !err[0]) {
        snprintf(err, errlen, "out of memory");
    }
    return root;
}

YlNode *yl_parse_file(const char *path, char *err, size_t errlen)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, errlen, "cannot open: %s", strerror(errno));
        return NULL;
    }
    char *buf = malloc(MAX_FILE + 1);
    size_t n = buf ? fread(buf, 1, MAX_FILE + 1, f) : 0;
    fclose(f);
    if (!buf) {
        snprintf(err, errlen, "out of memory");
        return NULL;
    }
    if (n > MAX_FILE) {
        free(buf);
        snprintf(err, errlen, "larger than %d bytes", MAX_FILE);
        return NULL;
    }
    YlNode *r = yl_parse(buf, n, err, errlen);
    free(buf);
    return r;
}

const YlNode *yl_get(const YlNode *m, const char *key)
{
    if (!m || m->type != YL_MAP) {
        return NULL;
    }
    for (size_t i = 0; i < m->n; i++) {
        if (!strcmp(m->keys[i], key)) {
            return m->items[i];
        }
    }
    return NULL;
}

size_t yl_len(const YlNode *n)
{
    return n && (n->type == YL_MAP || n->type == YL_SEQ) ? n->n : 0;
}

const YlNode *yl_at(const YlNode *q, size_t i)
{
    return q && (q->type == YL_SEQ || q->type == YL_MAP) && i < q->n ? q->items[i] : NULL;
}

const char *yl_key_at(const YlNode *m, size_t i)
{
    return m && m->type == YL_MAP && i < m->n ? m->keys[i] : NULL;
}

const char *yl_str(const YlNode *n)
{
    return n && n->type == YL_SCALAR ? n->str : NULL;
}

bool yl_int(const YlNode *n, long long *out)
{
    const char *s = yl_str(n);
    if (!s || !*s || (n->quoted)) {
        return false;
    }
    char *end;
    errno = 0;
    long long v = strtoll(s, &end, (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) ||
                         ((s[0] == '-' || s[0] == '+') && s[1] == '0' &&
                          (s[2] == 'x' || s[2] == 'X')) ? 16 : 10);
    if (*end || errno) {
        return false;
    }
    *out = v;
    return true;
}

int yl_bool(const YlNode *n)
{
    const char *s = yl_str(n);
    if (!s || n->quoted) {
        return -1;
    }
    if (!strcmp(s, "true") || !strcmp(s, "True") || !strcmp(s, "TRUE")) {
        return 1;
    }
    if (!strcmp(s, "false") || !strcmp(s, "False") || !strcmp(s, "FALSE")) {
        return 0;
    }
    return -1;
}
