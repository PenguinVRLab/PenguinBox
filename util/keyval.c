/*
 * Parsing KEY=VALUE,... strings
 *
 * Copyright (C) 2017 Red Hat Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>,
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/*
 * KEY=VALUE,... syntax:
 *
 *   key-vals     = [ key-val { ',' key-val } [ ',' ] ]
 *   key-val      = key '=' val | help
 *   key          = key-fragment { '.' key-fragment }
 *   key-fragment = qapi-name | index
 *   qapi-name    = '__' / [a-z0-9.-]+ / '_' / [A-Za-z][A-Za-z0-9_-]* /
 *   index        = / [0-9]+ /
 *   val          = { / [^,]+ / | ',,' }
 *   help         = 'help' | '?'
 *
 * Semantics defined by reduction to JSON:
 *
 *   key-vals specifies a JSON object, i.e. a tree whose root is an
 *   object, inner nodes other than the root are objects or arrays,
 *   and leaves are strings.
 *
 *   Each key-val = key-fragment '.' ... '=' val specifies a path from
 *   root to a leaf (left of '='), and the leaf's value (right of
 *   '=').
 *
 *   A path from the root is defined recursively:
 *       L '.' key-fragment is a child of the node denoted by path L
 *       key-fragment is a child of the tree root
 *   If key-fragment is numeric, the parent is an array and the child
 *   is its key-fragment-th member, counting from zero.
 *   Else, the parent is an object, and the child is its member named
 *   key-fragment.
 *
 *   This constrains inner nodes to be either array or object.  The
 *   constraints must be satisfiable.  Counter-example: a.b=1,a=2 is
 *   not, because root.a must be an object to satisfy a.b=1 and a
 *   string to satisfy a=2.
 *
 *   Array subscripts can occur in any order, but the set of
 *   subscripts must not have gaps.  For instance, a.1=v is not okay,
 *   because root.a[0] is missing.
 *
 *   If multiple key-val denote the same leaf, the last one determines
 *   the value.
 *
 * Key-fragments must be valid QAPI names or consist only of decimal
 * digits.
 *
 * The length of any key-fragment must be between 1 and 127.
 *
 * If any key-val is help, the object is to be treated as a help
 * request.
 *
 * Design flaw: there is no way to denote an empty array or non-root
 * object.  While interpreting "key absent" as empty seems natural
 * (removing a key-val from the input string removes the member when
 * there are more, so why not when it's the last), it doesn't work:
 * "key absent" already means "optional object/array absent", which
 * isn't the same as "empty object/array present".
 *
 * Design flaw: scalar values can only be strings; there is no way to
 * denote numbers, true, false or null.  The special QObject input
 * visitor returned by qobject_input_visitor_new_keyval() mostly hides
 * this by automatically converting strings to the type the visitor
 * expects.  Breaks down for type 'any', where the visitor's
 * expectation isn't clear.  Code visiting 'any' needs to do the
 * conversion itself, but only when using this keyval visitor.
 * Awkward.  Note that we carefully restrict alternate types to avoid
 * similar ambiguity.
 *
 * Alternative syntax for use with an implied key:
 *
 *   key-vals     = [ key-val-1st { ',' key-val } [ ',' ] ]
 *   key-val-1st  = val-no-key | key-val
 *   val-no-key   = / [^=,]+ / - help
 *
 * where val-no-key is syntactic sugar for implied-key=val-no-key.
 *
 * Note that you can't use the sugared form when the value contains
 * '=' or ','.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"
#include "qobject/qstring.h"
#include "qemu/cutils.h"
#include "qemu/keyval.h"
#include "qemu/help_option.h"

static int key_to_index(const char *key, const char **end)
{
    int ret;
    unsigned long index;

    if (*key < '0' || *key > '9') {
        return -EINVAL;
    }
    ret = qemu_strtoul(key, end, 10, &index);
    if (ret) {
        return ret == -ERANGE ? INT_MAX : ret;
    }
    return index <= INT_MAX ? index : INT_MAX;
}

static QObject *keyval_parse_put(QDict *cur,
                                 const char *key_in_cur, QString *value,
                                 const char *key, const char *key_cursor,
                                 Error **errp)
{
    QObject *old, *new;

    old = qdict_get(cur, key_in_cur);
    if (old) {
        if (qobject_type(old) != (value ? QTYPE_QSTRING : QTYPE_QDICT)) {
            error_setg(errp, "Parameters '%.*s.*' used inconsistently",
                       (int)(key_cursor - key), key);
            qobject_unref(value);
            return NULL;
        }
        if (!value) {
            return old;
        }
        new = QOBJECT(value);
    } else {
        new = value ? QOBJECT(value) : QOBJECT(qdict_new());
    }
    qdict_put_obj(cur, key_in_cur, new);
    return new;
}

static const char *keyval_parse_one(QDict *qdict, const char *params,
                                    const char *implied_key, bool *help,
                                    Error **errp)
{
    const char *key, *key_end, *val_end, *s, *end;
    size_t len;
    char key_in_cur[128];
    QDict *cur;
    int ret;
    QObject *next;
    GString *val;

    key = params;
    val_end = NULL;
    len = strcspn(params, "=,");
    if (len && key[len] != '=') {
        if (starts_with_help_option(key) == len) {
            *help = true;
            s = key + len;
            if (*s == ',') {
                s++;
            }
            return s;
        }
        if (implied_key) {
            key = implied_key;
            val_end = params + len;
            len = strlen(implied_key);
        }
    }
    key_end = key + len;

    cur = qdict;
    s = key;
    for (;;) {
        if (s != key && key_to_index(s, &end) >= 0) {
            len = end - s;
        } else {
            ret = parse_qapi_name(s, false);
            len = ret < 0 ? 0 : ret;
        }
        assert(s + len <= key_end);
        if (!len || (s + len < key_end && s[len] != '.')) {
            assert(key != implied_key);
            error_setg(errp, "Invalid parameter '%.*s'",
                       (int)(key_end - key), key);
            return NULL;
        }
        if (len >= sizeof(key_in_cur)) {
            assert(key != implied_key);
            error_setg(errp, "Parameter%s '%.*s' is too long",
                       s != key || s + len != key_end ? " fragment" : "",
                       (int)len, s);
            return NULL;
        }

        if (s != key) {
            next = keyval_parse_put(cur, key_in_cur, NULL,
                                    key, s - 1, errp);
            if (!next) {
                return NULL;
            }
            cur = qobject_to(QDict, next);
            assert(cur);
        }

        memcpy(key_in_cur, s, len);
        key_in_cur[len] = 0;
        s += len;

        if (*s != '.') {
            break;
        }
        s++;
    }

    if (key == implied_key) {
        assert(!*s);
        val = g_string_new_len(params, val_end - params);
        s = val_end;
        if (*s == ',') {
            s++;
        }
    } else {
        if (*s != '=') {
            error_setg(errp, "Expected '=' after parameter '%.*s'",
                       (int)(s - key), key);
            return NULL;
        }
        s++;

        val = g_string_new(NULL);
        for (;;) {
            if (!*s) {
                break;
            } else if (*s == ',') {
                s++;
                if (*s != ',') {
                    break;
                }
            }
            g_string_append_c(val, *s++);
        }
    }

    if (!keyval_parse_put(cur, key_in_cur, qstring_from_gstring(val),
                          key, key_end, errp)) {
        return NULL;
    }
    return s;
}

static char *reassemble_key(GSList *key)
{
    GString *s = g_string_new("");
    GSList *p;

    for (p = key; p; p = p->next) {
        g_string_prepend_c(s, '.');
        g_string_prepend(s, (char *)p->data);
    }

    return g_string_free(s, FALSE);
}

static void keyval_do_merge(QDict *dest, const QDict *merged, GString *str, Error **errp)
{
    size_t save_len = str->len;
    const QDictEntry *ent;
    QObject *old_value;

    for (ent = qdict_first(merged); ent; ent = qdict_next(merged, ent)) {
        old_value = qdict_get(dest, ent->key);
        if (old_value) {
            if (qobject_type(old_value) != qobject_type(ent->value)) {
                error_setg(errp, "Parameter '%s%s' used inconsistently",
                           str->str, ent->key);
                return;
            } else if (qobject_type(ent->value) == QTYPE_QDICT) {
                g_string_append(str, ent->key);
                g_string_append_c(str, '.');
                keyval_do_merge(qobject_to(QDict, old_value),
                                qobject_to(QDict, ent->value),
                                str, errp);
                g_string_truncate(str, save_len);
                continue;
            } else if (qobject_type(ent->value) == QTYPE_QLIST) {
                QList *old = qobject_to(QList, old_value);
                QList *new = qobject_to(QList, ent->value);
                const QListEntry *item;
                QLIST_FOREACH_ENTRY(new, item) {
                    qobject_ref(item->value);
                    qlist_append_obj(old, item->value);
                }
                continue;
            } else {
                assert(qobject_type(ent->value) == QTYPE_QSTRING);
            }
        }

        qobject_ref(ent->value);
        qdict_put_obj(dest, ent->key, ent->value);
    }
}

void keyval_merge(QDict *dest, const QDict *merged, Error **errp)
{
    GString *str;

    str = g_string_new("");
    keyval_do_merge(dest, merged, str, errp);
    g_string_free(str, TRUE);
}

static QObject *keyval_listify(QDict *cur, GSList *key_of_cur, Error **errp)
{
    GSList key_node;
    bool has_index, has_member;
    const QDictEntry *ent;
    QDict *qdict;
    QObject *val;
    char *key;
    size_t nelt;
    QObject **elt;
    int index, max_index, i;
    QList *list;

    key_node.next = key_of_cur;

    has_index = false;
    has_member = false;
    for (ent = qdict_first(cur); ent; ent = qdict_next(cur, ent)) {
        if (key_to_index(ent->key, NULL) >= 0) {
            has_index = true;
        } else {
            has_member = true;
        }

        qdict = qobject_to(QDict, ent->value);
        if (!qdict) {
            continue;
        }

        key_node.data = ent->key;
        val = keyval_listify(qdict, &key_node, errp);
        if (!val) {
            return NULL;
        }
        if (val != ent->value) {
            qdict_put_obj(cur, ent->key, val);
        }
    }

    if (has_index && has_member) {
        key = reassemble_key(key_of_cur);
        error_setg(errp, "Parameters '%s*' used inconsistently", key);
        g_free(key);
        return NULL;
    }
    if (!has_index) {
        return QOBJECT(cur);
    }

    nelt = qdict_size(cur) + 1;
    elt = g_new0(QObject *, nelt);
    max_index = -1;
    for (ent = qdict_first(cur); ent; ent = qdict_next(cur, ent)) {
        index = key_to_index(ent->key, NULL);
        assert(index >= 0);
        if (index > max_index) {
            max_index = index;
        }
        if ((size_t)index >= nelt - 1) {
            continue;
        }
        elt[index] = ent->value;
    }

    list = qlist_new();
    assert(!elt[nelt-1]);
    for (i = 0; i < MIN(nelt, max_index + 1); i++) {
        if (!elt[i]) {
            key = reassemble_key(key_of_cur);
            error_setg(errp, "Parameter '%s%d' missing", key, i);
            g_free(key);
            g_free(elt);
            qobject_unref(list);
            return NULL;
        }
        qobject_ref(elt[i]);
        qlist_append_obj(list, elt[i]);
    }

    g_free(elt);
    return QOBJECT(list);
}

QDict *keyval_parse_into(QDict *qdict, const char *params, const char *implied_key,
                         bool *p_help, Error **errp)
{
    QObject *listified;
    const char *s;
    bool help = false;

    s = params;
    while (*s) {
        s = keyval_parse_one(qdict, s, implied_key, &help, errp);
        if (!s) {
            return NULL;
        }
        implied_key = NULL;
    }

    if (p_help) {
        *p_help = help;
    } else if (help) {
        error_setg(errp, "Help is not available for this option");
        return NULL;
    }

    listified = keyval_listify(qdict, NULL, errp);
    if (!listified) {
        return NULL;
    }
    assert(listified == QOBJECT(qdict));
    return qdict;
}

QDict *keyval_parse(const char *params, const char *implied_key,
                    bool *p_help, Error **errp)
{
    QDict *qdict = qdict_new();
    QDict *ret = keyval_parse_into(qdict, params, implied_key, p_help, errp);

    if (!ret) {
        qobject_unref(qdict);
    }
    return ret;
}
