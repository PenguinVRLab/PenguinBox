/* Macro file for Coccinelle
 *
 * Copyright (C) 2015 Red Hat, Inc.
 *
 * Authors:
 *  Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version.  See the COPYING file in the top-level directory.
 */

/* Coccinelle only does limited parsing of headers, and chokes on some idioms
 * defined in compiler.h and queue.h.  Macros that Coccinelle must know about
 * in order to parse .c files must be in a separate macro file---which is
 * exactly what you're staring at now.
 *
 * To use this file, add the "--macro-file scripts/cocci-macro-file.h" to the
 * Coccinelle command line.
 */

/* From qemu/compiler.h */
#define G_NORETURN __attribute__ ((__noreturn__))
#define G_GNUC_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#define G_GNUC_NULL_TERMINATED __attribute__((sentinel))

#define QEMU_PACKED __attribute__((packed))

#define cat(x,y) x ## y
#define cat2(x,y) cat(x,y)
#define QEMU_BUILD_BUG_ON(x) \
    typedef char cat2(qemu_build_bug_on__,__LINE__)[(x)?-1:1] __attribute__((unused));

#define G_GNUC_PRINTF(n, m) __attribute__((format(gnu_printf, n, m)))

#define xglue(x, y) x ## y
#define glue(x, y) xglue(x, y)
#define stringify(s)	tostring(s)
#define tostring(s)	#s

#define typeof_field(type, field) typeof(((type *)0)->field)
#define type_check(t1,t2) ((t1*)0 - (t2*)0)

#define QLIST_HEAD(name, type)                                          \
struct name {                                                           \
        struct type *lh_first;                     \
}

#define QLIST_HEAD_INITIALIZER(head)                                    \
        { NULL }

#define QLIST_ENTRY(type)                                               \
struct {                                                                \
        struct type *le_next;                      \
        struct type **le_prev;  \
}

#define QSLIST_HEAD(name, type)                                          \
struct name {                                                           \
        struct type *slh_first;                     \
}

#define QSLIST_HEAD_INITIALIZER(head)                                    \
        { NULL }

#define QSLIST_ENTRY(type)                                               \
struct {                                                                \
        struct type *sle_next;                      \
}

#define QSIMPLEQ_HEAD(name, type)                                       \
struct name {                                                           \
    struct type *sqh_first;                      \
    struct type **sqh_last;          \
}

#define QSIMPLEQ_HEAD_INITIALIZER(head)                                 \
    { NULL, &(head).sqh_first }

#define QSIMPLEQ_ENTRY(type)                                            \
struct {                                                                \
    struct type *sqe_next;                        \
}

#define QTAILQ_HEAD(name, type)                                         \
union name {                                                            \
        struct type *tqh_first;               \
        QTailQLink tqh_circ;       \
}

#define QTAILQ_HEAD_INITIALIZER(head)                                   \
        { .tqh_circ = { NULL, &(head).tqh_circ } }

#define QTAILQ_ENTRY(type)                                              \
union {                                                                 \
        struct type *tqe_next;                \
        QTailQLink tqe_circ;       \
}

#define g_assert_cmpint(a, op, b)   g_assert(a op b)
#define g_assert_cmpuint(a, op, b)   g_assert(a op b)
#define g_assert_cmphex(a, op, b)   g_assert(a op b)
#define g_assert_cmpstr(a, op, b)   g_assert(strcmp(a, b) op 0)
