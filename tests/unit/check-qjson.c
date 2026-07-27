/*
 * Copyright IBM, Corp. 2009
 * Copyright (c) 2013, 2015 Red Hat Inc.
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Markus Armbruster <armbru@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qobject/qbool.h"
#include "qobject/qjson.h"
#include "qobject/qlit.h"
#include "qobject/qnull.h"
#include "qobject/qnum.h"
#include "qobject/qstring.h"
#include "qemu/unicode.h"

static QString *from_json_str(const char *jstr, bool single, Error **errp)
{
    char quote = single ? '\'' : '"';
    char *qjstr = g_strdup_printf("%c%s%c", quote, jstr, quote);
    QString *ret = qobject_to(QString, qobject_from_json(qjstr, errp));

    g_free(qjstr);
    return ret;
}

static char *to_json_str(QString *str)
{
    GString *json = qobject_to_json(QOBJECT(str));

    if (!json) {
        return NULL;
    }
    g_string_truncate(json, json->len - 1);
    g_string_erase(json, 0, 1);
    return g_string_free(json, false);
}

static void escaped_string(void)
{
    struct {
        const char *json_in;
        const char *utf8_out;
        int skip;
    } test_cases[] = {
        { "\\b\\f\\n\\r\\t\\\\\\\"", "\b\f\n\r\t\\\"" },
        { "\\/\\'", "/'", .skip = 1 },
        { "single byte utf-8 \\u0020", "single byte utf-8  ", .skip = 1 },
        { "double byte utf-8 \\u00A2", "double byte utf-8 \xc2\xa2" },
        { "triple byte utf-8 \\u20AC", "triple byte utf-8 \xe2\x82\xac" },
        { "quadruple byte utf-8 \\uD834\\uDD1E",
          "quadruple byte utf-8 \xF0\x9D\x84\x9E" },
        { "\\", NULL },
        { "\\z", NULL },
        { "\\ux", NULL },
        { "\\u1x", NULL },
        { "\\u12x", NULL },
        { "\\u123x", NULL },
        { "\\u12345", "\341\210\2645" },
        { "\\u0000x", "\xC0\x80x" },
        { "unpaired leading surrogate \\uD800", NULL },
        { "unpaired leading surrogate \\uD800\\uCAFE", NULL },
        { "unpaired leading surrogate \\uD800\\uD801\\uDC02", NULL },
        { "unpaired trailing surrogate \\uDC00", NULL },
        { "backward surrogate pair \\uDC00\\uD800", NULL },
        { "noncharacter U+FDD0 \\uFDD0", NULL },
        { "noncharacter U+FDEF \\uFDEF", NULL },
        { "noncharacter U+1FFFE \\uD87F\\uDFFE", NULL },
        { "noncharacter U+10FFFF \\uDC3F\\uDFFF", NULL },
        {}
    };
    int i, j;
    QString *cstr;
    char *jstr;

    for (i = 0; test_cases[i].json_in; i++) {
        for (j = 0; j < 2; j++) {
            if (test_cases[i].utf8_out) {
                cstr = from_json_str(test_cases[i].json_in, j, &error_abort);
                g_assert_cmpstr(qstring_get_str(cstr),
                                ==, test_cases[i].utf8_out);
                if (!test_cases[i].skip) {
                    jstr = to_json_str(cstr);
                    g_assert_cmpstr(jstr, ==, test_cases[i].json_in);
                    g_free(jstr);
                }
                qobject_unref(cstr);
            } else {
                cstr = from_json_str(test_cases[i].json_in, j, NULL);
                g_assert(!cstr);
            }
        }
    }
}

static void string_with_quotes(void)
{
    const char *test_cases[] = {
        "\"the bee's knees\"",
        "'double quote \"'",
        NULL
    };
    int i;
    QString *str;
    char *cstr;

    for (i = 0; test_cases[i]; i++) {
        str = qobject_to(QString,
                         qobject_from_json(test_cases[i], &error_abort));
        g_assert(str);
        cstr = g_strndup(test_cases[i] + 1, strlen(test_cases[i]) - 2);
        g_assert_cmpstr(qstring_get_str(str), ==, cstr);
        g_free(cstr);
        qobject_unref(str);
    }
}

static void utf8_string(void)
{
    static const struct {
        const char *json_in;
        const char *utf8_out;
        const char *json_out;
    } test_cases[] = {
        {
            "\x01\x02\x03\x04\x05\x06\x07"
            "\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F"
            "\x10\x11\x12\x13\x14\x15\x16\x17"
            "\x18\x19\x1A\x1B\x1C\x1D\x1E\x1F",
            NULL,
            "\\u0001\\u0002\\u0003\\u0004\\u0005\\u0006\\u0007"
            "\\b\\t\\n\\u000B\\f\\r\\u000E\\u000F"
            "\\u0010\\u0011\\u0012\\u0013\\u0014\\u0015\\u0016\\u0017"
            "\\u0018\\u0019\\u001A\\u001B\\u001C\\u001D\\u001E\\u001F",
        },
        {
            "Falsches \xC3\x9C" "ben von Xylophonmusik qu\xC3\xA4lt"
            " jeden gr\xC3\xB6\xC3\x9F" "eren Zwerg.",
            "Falsches \xC3\x9C" "ben von Xylophonmusik qu\xC3\xA4lt"
            " jeden gr\xC3\xB6\xC3\x9F" "eren Zwerg.",
            "Falsches \\u00DCben von Xylophonmusik qu\\u00E4lt"
            " jeden gr\\u00F6\\u00DFeren Zwerg.",
        },
        {
            "\xCE\xBA\xE1\xBD\xB9\xCF\x83\xCE\xBC\xCE\xB5",
            "\xCE\xBA\xE1\xBD\xB9\xCF\x83\xCE\xBC\xCE\xB5",
            "\\u03BA\\u1F79\\u03C3\\u03BC\\u03B5",
        },
        {
            "100%",
            "100%",
        },
        {
            " ",
            " ",
        },
        {
            "\xC2\x80",
            "\xC2\x80",
            "\\u0080",
        },
        {
            "\xE0\xA0\x80",
            "\xE0\xA0\x80",
            "\\u0800",
        },
        {
            "\xF0\x90\x80\x80",
            "\xF0\x90\x80\x80",
            "\\uD800\\uDC00",
        },
        {
            "\xF8\x88\x80\x80\x80",
            NULL,
            "\\uFFFD",
        },
        {
            "\xFC\x84\x80\x80\x80\x80",
            NULL,
            "\\uFFFD",
        },
        {
            "\x7F",
            "\x7F",
            "\\u007F",
        },
        {
            "\xDF\xBF",
            "\xDF\xBF",
            "\\u07FF",
        },
        {
            "\xEF\xBF\xBC",
            "\xEF\xBF\xBC",
            "\\uFFFC",
        },
        {
            "\xF7\xBF\xBF\xBF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xFB\xBF\xBF\xBF\xBF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xFD\xBF\xBF\xBF\xBF\xBF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xED\x9F\xBF",
            "\xED\x9F\xBF",
            "\\uD7FF",
        },
        {
            "\xEE\x80\x80",
            "\xEE\x80\x80",
            "\\uE000",
        },
        {
            "\xEF\xBF\xBD",
            "\xEF\xBF\xBD",
            "\\uFFFD",
        },
        {
            "\xF4\x8F\xBF\xBD",
            "\xF4\x8F\xBF\xBD",
            "\\uDBFF\\uDFFD"
        },
        {
            "\xF4\x90\x80\x80",
            NULL,
            "\\uFFFD",
        },
        {
            "\x80",
            NULL,
            "\\uFFFD",
        },
        {
            "\xBF",
            NULL,
            "\\uFFFD",
        },
        {
            "\x80\xBF",
            NULL,
            "\\uFFFD\\uFFFD",
        },
        {
            "\x80\xBF\x80",
            NULL,
            "\\uFFFD\\uFFFD\\uFFFD",
        },
        {
            "\x80\xBF\x80\xBF",
            NULL,
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD",
        },
        {
            "\x80\xBF\x80\xBF\x80",
            NULL,
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD",
        },
        {
            "\x80\xBF\x80\xBF\x80\xBF",
            NULL,
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD",
        },
        {
            "\x80\xBF\x80\xBF\x80\xBF\x80",
            NULL,
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD",
        },
        {
            "\x80\x81\x82\x83\x84\x85\x86\x87"
            "\x88\x89\x8A\x8B\x8C\x8D\x8E\x8F"
            "\x90\x91\x92\x93\x94\x95\x96\x97"
            "\x98\x99\x9A\x9B\x9C\x9D\x9E\x9F"
            "\xA0\xA1\xA2\xA3\xA4\xA5\xA6\xA7"
            "\xA8\xA9\xAA\xAB\xAC\xAD\xAE\xAF"
            "\xB0\xB1\xB2\xB3\xB4\xB5\xB6\xB7"
            "\xB8\xB9\xBA\xBB\xBC\xBD\xBE\xBF",
            NULL,
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD"
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD"
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD"
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD"
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD"
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD"
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD"
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD",
        },
        {
            "\xC0 \xC1 \xC2 \xC3 \xC4 \xC5 \xC6 \xC7 "
            "\xC8 \xC9 \xCA \xCB \xCC \xCD \xCE \xCF "
            "\xD0 \xD1 \xD2 \xD3 \xD4 \xD5 \xD6 \xD7 "
            "\xD8 \xD9 \xDA \xDB \xDC \xDD \xDE \xDF ",
            NULL,
            "\\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD "
            "\\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD "
            "\\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD "
            "\\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD ",
        },
        {
            "\xE0 \xE1 \xE2 \xE3 \xE4 \xE5 \xE6 \xE7 "
            "\xE8 \xE9 \xEA \xEB \xEC \xED \xEE \xEF ",
            NULL,
            "\\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD "
            "\\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD ",
        },
        {
            "\xF0 \xF1 \xF2 \xF3 \xF4 \xF5 \xF6 \xF7 ",
            NULL,
            "\\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD ",
        },
        {
            "\xF8 \xF9 \xFA \xFB ",
            NULL,
            "\\uFFFD \\uFFFD \\uFFFD \\uFFFD ",
        },
        {
            "\xFC \xFD ",
            NULL,
            "\\uFFFD \\uFFFD ",
        },
        {
            "\xC0",
            NULL,
            "\\uFFFD",
        },
        {
            "\xE0\x80",
            NULL,
            "\\uFFFD",
        },
        {
            "\xF0\x80\x80",
            NULL,
            "\\uFFFD",
        },
        {
            "\xF8\x80\x80\x80",
            NULL,
            "\\uFFFD",
        },
        {
            "\xFC\x80\x80\x80\x80",
            NULL,
            "\\uFFFD",
        },
        {
            "\xDF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xEF\xBF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xF7\xBF\xBF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xFB\xBF\xBF\xBF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xFD\xBF\xBF\xBF\xBF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xC0\xE0\x80\xF0\x80\x80\xF8\x80\x80\x80\xFC\x80\x80\x80\x80"
            "\xDF\xEF\xBF\xF7\xBF\xBF\xFB\xBF\xBF\xBF\xFD\xBF\xBF\xBF\xBF",
            NULL,
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD"
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD",
        },
        {
            "\xFE",
            NULL,
            "\\uFFFD",
        },
        {
            "\xFF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xFE\xFE\xFF\xFF",
            NULL,
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD",
        },
        {
            "\xC0\xAF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xE0\x80\xAF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xF0\x80\x80\xAF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xF8\x80\x80\x80\xAF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xFC\x80\x80\x80\x80\xAF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xC1\xBF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xE0\x9F\xBF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xF0\x8F\xBF\xBC",
            NULL,
            "\\uFFFD",
        },
        {
            "\xF8\x87\xBF\xBF\xBF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xFC\x83\xBF\xBF\xBF\xBF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xC0\x80",
            "\xC0\x80",
            "\\u0000",
        },
        {
            "\xE0\x80\x80",
            NULL,
            "\\uFFFD",
        },
        {
            "\xF0\x80\x80\x80",
            NULL,
            "\\uFFFD",
        },
        {
            "\xF8\x80\x80\x80\x80",
            NULL,
            "\\uFFFD",
        },
        {
            "\xFC\x80\x80\x80\x80\x80",
            NULL,
            "\\uFFFD",
        },
        {
            "\xED\xA0\x80",
            NULL,
            "\\uFFFD",
        },
        {
            "\xED\xAD\xBF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xED\xAE\x80",
            NULL,
            "\\uFFFD",
        },
        {
            "\xED\xAF\xBF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xED\xB0\x80",
            NULL,
            "\\uFFFD",
        },
        {
            "\xED\xBE\x80",
            NULL,
            "\\uFFFD",
        },
        {
            "\xED\xBF\xBF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xED\xA0\x80\xED\xB0\x80",
            NULL,
            "\\uFFFD\\uFFFD",
        },
        {
            "\xED\xA0\x80\xED\xBF\xBF",
            NULL,
            "\\uFFFD\\uFFFD",
        },
        {
            "\xED\xAD\xBF\xED\xB0\x80",
            NULL,
            "\\uFFFD\\uFFFD",
        },
        {
            "\xED\xAD\xBF\xED\xBF\xBF",
            NULL,
            "\\uFFFD\\uFFFD",
        },
        {
            "\xED\xAE\x80\xED\xB0\x80",
            NULL,
            "\\uFFFD\\uFFFD",
        },
        {
            "\xED\xAE\x80\xED\xBF\xBF",
            NULL,
            "\\uFFFD\\uFFFD",
        },
        {
            "\xED\xAF\xBF\xED\xB0\x80",
            NULL,
            "\\uFFFD\\uFFFD",
        },
        {
            "\xED\xAF\xBF\xED\xBF\xBF",
            NULL,
            "\\uFFFD\\uFFFD",
        },
        {
            "\xEF\xBF\xBE",
            NULL,
            "\\uFFFD",
        },
        {
            "\xEF\xBF\xBF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xEF\xB7\x90",
            NULL,
            "\\uFFFD",
        },
        {
            "\xEF\xB7\xAF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xF0\x9F\xBF\xBE\xF0\x9F\xBF\xBF"
            "\xF0\xAF\xBF\xBE\xF0\xAF\xBF\xBF"
            "\xF0\xBF\xBF\xBE\xF0\xBF\xBF\xBF"
            "\xF1\x8F\xBF\xBE\xF1\x8F\xBF\xBF"
            "\xF1\x9F\xBF\xBE\xF1\x9F\xBF\xBF"
            "\xF1\xAF\xBF\xBE\xF1\xAF\xBF\xBF"
            "\xF1\xBF\xBF\xBE\xF1\xBF\xBF\xBF"
            "\xF2\x8F\xBF\xBE\xF2\x8F\xBF\xBF"
            "\xF2\x9F\xBF\xBE\xF2\x9F\xBF\xBF"
            "\xF2\xAF\xBF\xBE\xF2\xAF\xBF\xBF"
            "\xF2\xBF\xBF\xBE\xF2\xBF\xBF\xBF"
            "\xF3\x8F\xBF\xBE\xF3\x8F\xBF\xBF"
            "\xF3\x9F\xBF\xBE\xF3\x9F\xBF\xBF"
            "\xF3\xAF\xBF\xBE\xF3\xAF\xBF\xBF"
            "\xF3\xBF\xBF\xBE\xF3\xBF\xBF\xBF"
            "\xF4\x8F\xBF\xBE\xF4\x8F\xBF\xBF",
            NULL,
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD"
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD"
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD"
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD",
        },
        {}
    };
    int i, j;
    QString *str;
    const char *json_in, *utf8_out, *utf8_in, *json_out, *tail;
    char *end, *in, *jstr;

    for (i = 0; test_cases[i].json_in; i++) {
        for (j = 0; j < 2; j++) {
            json_in = test_cases[i].json_in;
            utf8_out = test_cases[i].utf8_out;
            utf8_in = test_cases[i].utf8_out ?: test_cases[i].json_in;
            json_out = test_cases[i].json_out ?: test_cases[i].json_in;

            if (utf8_out) {
                str = from_json_str(json_in, j, &error_abort);
                g_assert_cmpstr(qstring_get_str(str), ==, utf8_out);
                qobject_unref(str);
            } else {
                str = from_json_str(json_in, j, NULL);
                g_assert(!str);
                for (tail = json_in; *tail; tail = end) {
                    mod_utf8_codepoint(tail, 6, &end);
                    if (*end == ' ') {
                        end++;
                    }
                    in = g_strndup(tail, end - tail);
                    str = from_json_str(in, j, NULL);
                    g_assert(!str);
                    g_free(in);
                }
            }

            str = qstring_from_str(utf8_in);
            jstr = to_json_str(str);
            g_assert_cmpstr(jstr, ==, json_out);
            qobject_unref(str);
            g_free(jstr);

            if (!strstr(json_out, "\\uFFFD")) {
                str = from_json_str(json_out, j, &error_abort);
                g_assert_cmpstr(qstring_get_str(str), ==, utf8_in);
                qobject_unref(str);
            }
        }
    }
}

static void int_number(void)
{
    struct {
        const char *encoded;
        int64_t decoded;
        const char *reencoded;
    } test_cases[] = {
        { "0", 0 },
        { "1234", 1234 },
        { "1", 1 },
        { "-32", -32 },
        { "-0", 0, "0" },
        {},
    };
    int i;
    QNum *qnum;
    int64_t ival;
    uint64_t uval;
    GString *str;

    for (i = 0; test_cases[i].encoded; i++) {
        qnum = qobject_to(QNum,
                          qobject_from_json(test_cases[i].encoded,
                                            &error_abort));
        g_assert(qnum);
        g_assert(qnum_get_try_int(qnum, &ival));
        g_assert_cmpint(ival, ==, test_cases[i].decoded);
        if (test_cases[i].decoded >= 0) {
            g_assert(qnum_get_try_uint(qnum, &uval));
            g_assert_cmpuint(uval, ==, (uint64_t)test_cases[i].decoded);
        } else {
            g_assert(!qnum_get_try_uint(qnum, &uval));
        }
        g_assert_cmpfloat(qnum_get_double(qnum), ==,
                          (double)test_cases[i].decoded);

        str = qobject_to_json(QOBJECT(qnum));
        g_assert_cmpstr(str->str, ==,
                        test_cases[i].reencoded ?: test_cases[i].encoded);
        g_string_free(str, true);

        qobject_unref(qnum);
    }
}

static void uint_number(void)
{
    struct {
        const char *encoded;
        uint64_t decoded;
        const char *reencoded;
    } test_cases[] = {
        { "9223372036854775808", (uint64_t)1 << 63 },
        { "18446744073709551615", UINT64_MAX },
        {},
    };
    int i;
    QNum *qnum;
    int64_t ival;
    uint64_t uval;
    GString *str;

    for (i = 0; test_cases[i].encoded; i++) {
        qnum = qobject_to(QNum,
                          qobject_from_json(test_cases[i].encoded,
                                            &error_abort));
        g_assert(qnum);
        g_assert(qnum_get_try_uint(qnum, &uval));
        g_assert_cmpuint(uval, ==, test_cases[i].decoded);
        g_assert(!qnum_get_try_int(qnum, &ival));
        g_assert_cmpfloat(qnum_get_double(qnum), ==,
                          (double)test_cases[i].decoded);

        str = qobject_to_json(QOBJECT(qnum));
        g_assert_cmpstr(str->str, ==,
                        test_cases[i].reencoded ?: test_cases[i].encoded);
        g_string_free(str, true);

        qobject_unref(qnum);
    }
}

static void float_number(void)
{
    struct {
        const char *encoded;
        double decoded;
        const char *reencoded;
    } test_cases[] = {
        { "32.43", 32.43 },
        { "0.222", 0.222 },
        { "-32.12313", -32.12313, "-32.123130000000003" },
        { "-32.20e-10", -32.20e-10, "-3.22e-09" },
        { "18446744073709551616", 0x1p64, "1.8446744073709552e+19" },
        { "-9223372036854775809", -0x1p63, "-9.2233720368547758e+18" },
        {},
    };
    int i;
    QNum *qnum;
    int64_t ival;
    uint64_t uval;
    GString *str;

    for (i = 0; test_cases[i].encoded; i++) {
        qnum = qobject_to(QNum,
                          qobject_from_json(test_cases[i].encoded,
                                            &error_abort));
        g_assert(qnum);
        g_assert_cmpfloat(qnum_get_double(qnum), ==, test_cases[i].decoded);
        g_assert(!qnum_get_try_int(qnum, &ival));
        g_assert(!qnum_get_try_uint(qnum, &uval));

        str = qobject_to_json(QOBJECT(qnum));
        g_assert_cmpstr(str->str, ==,
                        test_cases[i].reencoded ?: test_cases[i].encoded);
        g_string_free(str, true);

        qobject_unref(qnum);
    }
}

static void keyword_literal(void)
{
    QObject *obj;
    QBool *qbool;
    QNull *null;
    GString *str;

    obj = qobject_from_json("true", &error_abort);
    qbool = qobject_to(QBool, obj);
    g_assert(qbool);
    g_assert(qbool_get_bool(qbool) == true);

    str = qobject_to_json(obj);
    g_assert_cmpstr(str->str, ==, "true");
    g_string_free(str, true);

    qobject_unref(qbool);

    obj = qobject_from_json("false", &error_abort);
    qbool = qobject_to(QBool, obj);
    g_assert(qbool);
    g_assert(qbool_get_bool(qbool) == false);

    str = qobject_to_json(obj);
    g_assert_cmpstr(str->str, ==, "false");
    g_string_free(str, true);

    qobject_unref(qbool);

    obj = qobject_from_json("null", &error_abort);
    g_assert(obj != NULL);
    g_assert(qobject_type(obj) == QTYPE_QNULL);

    null = qnull();
    g_assert(QOBJECT(null) == obj);

    qobject_unref(obj);
    qobject_unref(null);
}

static void interpolation_valid(void)
{
    long long value_lld = 0x123456789abcdefLL;
    int64_t value_d64 = value_lld;
    long value_ld = (long)value_lld;
    int value_d = (int)value_lld;
    unsigned long long value_llu = 0xfedcba9876543210ULL;
    uint64_t value_u64 = value_llu;
    unsigned long value_lu = (unsigned long)value_llu;
    unsigned value_u = (unsigned)value_llu;
    double value_f = 2.323423423;
    const char *value_s = "hello world";
    QObject *value_p = QOBJECT(qnull());
    QBool *qbool;
    QNum *qnum;
    QString *qstr;
    QObject *qobj;

    qbool = qobject_to(QBool, qobject_from_jsonf_nofail("%i", false));
    g_assert(qbool);
    g_assert(qbool_get_bool(qbool) == false);
    qobject_unref(qbool);

    qbool = qobject_to(QBool, qobject_from_jsonf_nofail("%i", 2));
    g_assert(qbool);
    g_assert(qbool_get_bool(qbool) == true);
    qobject_unref(qbool);

    qnum = qobject_to(QNum, qobject_from_jsonf_nofail("%d", value_d));
    g_assert_cmpint(qnum_get_int(qnum), ==, value_d);
    qobject_unref(qnum);

    qnum = qobject_to(QNum, qobject_from_jsonf_nofail("%ld", value_ld));
    g_assert_cmpint(qnum_get_int(qnum), ==, value_ld);
    qobject_unref(qnum);

    qnum = qobject_to(QNum, qobject_from_jsonf_nofail("%lld", value_lld));
    g_assert_cmpint(qnum_get_int(qnum), ==, value_lld);
    qobject_unref(qnum);

    qnum = qobject_to(QNum, qobject_from_jsonf_nofail("%" PRId64, value_d64));
    g_assert_cmpint(qnum_get_int(qnum), ==, value_lld);
    qobject_unref(qnum);

    qnum = qobject_to(QNum, qobject_from_jsonf_nofail("%u", value_u));
    g_assert_cmpuint(qnum_get_uint(qnum), ==, value_u);
    qobject_unref(qnum);

    qnum = qobject_to(QNum, qobject_from_jsonf_nofail("%lu", value_lu));
    g_assert_cmpuint(qnum_get_uint(qnum), ==, value_lu);
    qobject_unref(qnum);

    qnum = qobject_to(QNum, qobject_from_jsonf_nofail("%llu", value_llu));
    g_assert_cmpuint(qnum_get_uint(qnum), ==, value_llu);
    qobject_unref(qnum);

    qnum = qobject_to(QNum, qobject_from_jsonf_nofail("%" PRIu64, value_u64));
    g_assert_cmpuint(qnum_get_uint(qnum), ==, value_llu);
    qobject_unref(qnum);

    qnum = qobject_to(QNum, qobject_from_jsonf_nofail("%f", value_f));
    g_assert(qnum_get_double(qnum) == value_f);
    qobject_unref(qnum);

    qstr = qobject_to(QString, qobject_from_jsonf_nofail("%s", value_s));
    g_assert_cmpstr(qstring_get_str(qstr), ==, value_s);
    qobject_unref(qstr);

    qobj = qobject_from_jsonf_nofail("%p", value_p);
    g_assert(qobj == value_p);
}

static void interpolation_unknown(void)
{
    if (g_test_subprocess()) {
        qobject_from_jsonf_nofail("%x", 666);
    }
    g_test_trap_subprocess(NULL, 0, 0);
    g_test_trap_assert_failed();
    g_test_trap_assert_stderr("*Unexpected error*"
                              "invalid interpolation '%x'*");
}

static void interpolation_string(void)
{
    if (g_test_subprocess()) {
        qobject_from_jsonf_nofail("['%s', %s]", "eins", "zwei");
    }
    g_test_trap_subprocess(NULL, 0, 0);
    g_test_trap_assert_failed();
    g_test_trap_assert_stderr("*Unexpected error*"
                              "can't interpolate into string*");
}

static void simple_dict(void)
{
    int i;
    struct {
        const char *encoded;
        QLitObject decoded;
    } test_cases[] = {
        {
            .encoded = "{\"foo\": 42, \"bar\": \"hello world\"}",
            .decoded = QLIT_QDICT(((QLitDictEntry[]){
                        { "foo", QLIT_QNUM(42) },
                        { "bar", QLIT_QSTR("hello world") },
                        { }
                    })),
        }, {
            .encoded = "{}",
            .decoded = QLIT_QDICT(((QLitDictEntry[]){
                        { }
                    })),
        }, {
            .encoded = "{\"foo\": 43}",
            .decoded = QLIT_QDICT(((QLitDictEntry[]){
                        { "foo", QLIT_QNUM(43) },
                        { }
                    })),
        },
        { }
    };

    for (i = 0; test_cases[i].encoded; i++) {
        QObject *obj;
        GString *str;

        obj = qobject_from_json(test_cases[i].encoded, &error_abort);
        g_assert(qlit_equal_qobject(&test_cases[i].decoded, obj));

        str = qobject_to_json(obj);
        qobject_unref(obj);

        obj = qobject_from_json(str->str, &error_abort);
        g_assert(qlit_equal_qobject(&test_cases[i].decoded, obj));
        qobject_unref(obj);
        g_string_free(str, true);
    }
}

static void gen_test_json(GString *gstr, int nest_level_max,
                          int elem_count)
{
    int i;

    g_assert(gstr);
    if (nest_level_max == 0) {
        g_string_append(gstr, "[");
        for (i = 0; i < elem_count; i++) {
            g_string_append_printf(gstr, "%d", i);
            if (i < elem_count - 1) {
                g_string_append_printf(gstr, ", ");
            }
        }
        g_string_append(gstr, "]");
        return;
    }

    g_string_append(gstr, "{");
    for (i = 0; i < nest_level_max; i++) {
        g_string_append_printf(gstr, "'key%d': ", i);
        gen_test_json(gstr, i, elem_count);
        if (i < nest_level_max - 1) {
            g_string_append(gstr, ",");
        }
    }
    g_string_append(gstr, "}");
}

static void large_dict(void)
{
    GString *gstr = g_string_new("");
    QObject *obj;

    gen_test_json(gstr, 10, 100);
    obj = qobject_from_json(gstr->str, &error_abort);
    g_assert(obj != NULL);

    qobject_unref(obj);
    g_string_free(gstr, true);
}

static void simple_list(void)
{
    int i;
    struct {
        const char *encoded;
        QLitObject decoded;
    } test_cases[] = {
        {
            .encoded = "[43,42]",
            .decoded = QLIT_QLIST(((QLitObject[]){
                        QLIT_QNUM(43),
                        QLIT_QNUM(42),
                        { }
                    })),
        },
        {
            .encoded = "[43]",
            .decoded = QLIT_QLIST(((QLitObject[]){
                        QLIT_QNUM(43),
                        { }
                    })),
        },
        {
            .encoded = "[]",
            .decoded = QLIT_QLIST(((QLitObject[]){
                        { }
                    })),
        },
        {
            .encoded = "[{}]",
            .decoded = QLIT_QLIST(((QLitObject[]){
                        QLIT_QDICT(((QLitDictEntry[]){
                                    {},
                                        })),
                        {},
                            })),
        },
        { }
    };

    for (i = 0; test_cases[i].encoded; i++) {
        QObject *obj;
        GString *str;

        obj = qobject_from_json(test_cases[i].encoded, &error_abort);
        g_assert(qlit_equal_qobject(&test_cases[i].decoded, obj));

        str = qobject_to_json(obj);
        qobject_unref(obj);

        obj = qobject_from_json(str->str, &error_abort);
        g_assert(qlit_equal_qobject(&test_cases[i].decoded, obj));
        qobject_unref(obj);
        g_string_free(str, true);
    }
}

static void simple_whitespace(void)
{
    int i;
    struct {
        const char *encoded;
        QLitObject decoded;
    } test_cases[] = {
        {
            .encoded = " [ 43 , 42 ]",
            .decoded = QLIT_QLIST(((QLitObject[]){
                        QLIT_QNUM(43),
                        QLIT_QNUM(42),
                        { }
                    })),
        },
        {
            .encoded = "\t[ 43 , { 'h' : 'b' },\r\n\t[ ], 42 ]\n",
            .decoded = QLIT_QLIST(((QLitObject[]){
                        QLIT_QNUM(43),
                        QLIT_QDICT(((QLitDictEntry[]){
                                    { "h", QLIT_QSTR("b") },
                                    { }})),
                        QLIT_QLIST(((QLitObject[]){
                                    { }})),
                        QLIT_QNUM(42),
                        { }
                    })),
        },
        {
            .encoded = " [ 43 , { 'h' : 'b' , 'a' : 32 }, [ ], 42 ]",
            .decoded = QLIT_QLIST(((QLitObject[]){
                        QLIT_QNUM(43),
                        QLIT_QDICT(((QLitDictEntry[]){
                                    { "h", QLIT_QSTR("b") },
                                    { "a", QLIT_QNUM(32) },
                                    { }})),
                        QLIT_QLIST(((QLitObject[]){
                                    { }})),
                        QLIT_QNUM(42),
                        { }
                    })),
        },
        { }
    };

    for (i = 0; test_cases[i].encoded; i++) {
        QObject *obj;
        GString *str;

        obj = qobject_from_json(test_cases[i].encoded, &error_abort);
        g_assert(qlit_equal_qobject(&test_cases[i].decoded, obj));

        str = qobject_to_json(obj);
        qobject_unref(obj);

        obj = qobject_from_json(str->str, &error_abort);
        g_assert(qlit_equal_qobject(&test_cases[i].decoded, obj));

        qobject_unref(obj);
        g_string_free(str, true);
    }
}

static void simple_interpolation(void)
{
    QObject *embedded_obj;
    QObject *obj;
    QLitObject decoded = QLIT_QLIST(((QLitObject[]){
            QLIT_QNUM(1),
            QLIT_QSTR("100%"),
            QLIT_QLIST(((QLitObject[]){
                        QLIT_QNUM(32),
                        QLIT_QNUM(42),
                        {}})),
            {}}));

    embedded_obj = qobject_from_json("[32, 42]", &error_abort);
    g_assert(embedded_obj != NULL);

    obj = qobject_from_jsonf_nofail("[%d, '100%%', %p]", 1, embedded_obj);
    g_assert(qlit_equal_qobject(&decoded, obj));

    qobject_unref(obj);
}

static void empty_input(void)
{
    Error *err = NULL;
    QObject *obj;

    obj = qobject_from_json("", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static void blank_input(void)
{
    Error *err = NULL;
    QObject *obj;

    obj = qobject_from_json("\n ", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static void junk_input(void)
{
    Error *err = NULL;
    QObject *obj;

    obj = qobject_from_json("@", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);

    obj = qobject_from_json("{\x01", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);

    obj = qobject_from_json("[0\xFF]", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);

    obj = qobject_from_json("00", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);

    obj = qobject_from_json("[1e", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);

    obj = qobject_from_json("truer", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static void unterminated_string(void)
{
    Error *err = NULL;
    QObject *obj = qobject_from_json("\"abc", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static void unterminated_sq_string(void)
{
    Error *err = NULL;
    QObject *obj = qobject_from_json("'abc", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static void unterminated_escape(void)
{
    Error *err = NULL;
    QObject *obj = qobject_from_json("\"abc\\\"", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static void unterminated_array(void)
{
    Error *err = NULL;
    QObject *obj = qobject_from_json("[32", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static void unterminated_array_comma(void)
{
    Error *err = NULL;
    QObject *obj = qobject_from_json("[32,", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static void invalid_array_comma(void)
{
    Error *err = NULL;
    QObject *obj = qobject_from_json("[32,}", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static void unterminated_dict(void)
{
    Error *err = NULL;
    QObject *obj = qobject_from_json("{'abc':32", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static void unterminated_dict_comma(void)
{
    Error *err = NULL;
    QObject *obj = qobject_from_json("{'abc':32,", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static void invalid_dict_comma(void)
{
    Error *err = NULL;
    QObject *obj = qobject_from_json("{'abc':32,}", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static void invalid_dict_key(void)
{
    Error *err = NULL;
    QObject *obj = qobject_from_json("{32:'abc'}", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static void unterminated_literal(void)
{
    Error *err = NULL;
    QObject *obj = qobject_from_json("nul", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static char *make_nest(char *buf, size_t cnt)
{
    memset(buf, '[', cnt - 1);
    buf[cnt - 1] = '{';
    buf[cnt] = '}';
    memset(buf + cnt + 1, ']', cnt - 1);
    buf[2 * cnt] = 0;
    return buf;
}

static void limits_nesting(void)
{
    Error *err = NULL;
    enum { max_nesting = 1024 };
    char buf[2 * (max_nesting + 1) + 1];
    QObject *obj;

    obj = qobject_from_json(make_nest(buf, max_nesting), &error_abort);
    g_assert(obj != NULL);
    qobject_unref(obj);

    obj = qobject_from_json(make_nest(buf, max_nesting + 1), &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static void multiple_values(void)
{
    Error *err = NULL;
    QObject *obj;

    obj = qobject_from_json("false true", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);

    obj = qobject_from_json("} true", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/literals/string/escaped", escaped_string);
    g_test_add_func("/literals/string/quotes", string_with_quotes);
    g_test_add_func("/literals/string/utf8", utf8_string);

    g_test_add_func("/literals/number/int", int_number);
    g_test_add_func("/literals/number/uint", uint_number);
    g_test_add_func("/literals/number/float", float_number);

    g_test_add_func("/literals/keyword", keyword_literal);

    g_test_add_func("/literals/interpolation/valid", interpolation_valid);
    g_test_add_func("/literals/interpolation/unknown", interpolation_unknown);
    g_test_add_func("/literals/interpolation/string", interpolation_string);

    g_test_add_func("/dicts/simple_dict", simple_dict);
    g_test_add_func("/dicts/large_dict", large_dict);
    g_test_add_func("/lists/simple_list", simple_list);

    g_test_add_func("/mixed/simple_whitespace", simple_whitespace);
    g_test_add_func("/mixed/interpolation", simple_interpolation);

    g_test_add_func("/errors/empty", empty_input);
    g_test_add_func("/errors/blank", blank_input);
    g_test_add_func("/errors/junk", junk_input);
    g_test_add_func("/errors/unterminated/string", unterminated_string);
    g_test_add_func("/errors/unterminated/escape", unterminated_escape);
    g_test_add_func("/errors/unterminated/sq_string", unterminated_sq_string);
    g_test_add_func("/errors/unterminated/array", unterminated_array);
    g_test_add_func("/errors/unterminated/array_comma", unterminated_array_comma);
    g_test_add_func("/errors/unterminated/dict", unterminated_dict);
    g_test_add_func("/errors/unterminated/dict_comma", unterminated_dict_comma);
    g_test_add_func("/errors/invalid_array_comma", invalid_array_comma);
    g_test_add_func("/errors/invalid_dict_comma", invalid_dict_comma);
    g_test_add_func("/errors/invalid_dict_key", invalid_dict_key);
    g_test_add_func("/errors/unterminated/literal", unterminated_literal);
    g_test_add_func("/errors/limits/nesting", limits_nesting);
    g_test_add_func("/errors/multiple_values", multiple_values);

    return g_test_run();
}
