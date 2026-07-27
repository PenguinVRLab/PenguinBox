/* Coverity Scan model
 *
 * Copyright (C) 2014 Red Hat, Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>
 *  Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version.  See the COPYING file in the top-level directory.
 */


/*
 * This is the source code for our Coverity user model file.  The
 * purpose of user models is to increase scanning accuracy by explaining
 * code Coverity can't see (out of tree libraries) or doesn't
 * sufficiently understand.  Better accuracy means both fewer false
 * positives and more true defects.  Memory leaks in particular.
 *
 * - A model file can't import any header files.  Some built-in primitives are
 *   available but not wchar_t, NULL etc.
 * - Modeling doesn't need full structs and typedefs. Rudimentary structs
 *   and similar types are sufficient.
 * - An uninitialized local variable signifies that the variable could be
 *   any value.
 *
 * The model file must be uploaded by an admin in the analysis settings of
 * http://scan.coverity.com/projects/378
 */

#define NULL ((void *)0)

typedef unsigned char uint8_t;
typedef char int8_t;
typedef unsigned int uint32_t;
typedef int int32_t;
typedef long ssize_t;
typedef unsigned long long uint64_t;
typedef long long int64_t;
typedef _Bool bool;

typedef struct va_list_str *va_list;

typedef struct {} name2keysym_t;
static int get_keysym(const name2keysym_t *table,
                      const char *name)
{
    int result;
    if (result > 0) {
        __coverity_tainted_string_sanitize_content__(name);
        return result;
    } else {
        return 0;
    }
}

uint8_t replay_get_byte(void)
{
    uint8_t byte;
    return byte;
}


void *g_malloc_n(size_t nmemb, size_t size)
{
    void *ptr;

    __coverity_negative_sink__(nmemb);
    __coverity_negative_sink__(size);
    ptr = __coverity_alloc__(nmemb * size);
    if (!ptr) {
        __coverity_panic__();
    }
    __coverity_mark_as_uninitialized_buffer__(ptr);
    __coverity_mark_as_afm_allocated__(ptr, AFM_free);
    return ptr;
}

void *g_malloc0_n(size_t nmemb, size_t size)
{
    void *ptr;

    __coverity_negative_sink__(nmemb);
    __coverity_negative_sink__(size);
    ptr = __coverity_alloc__(nmemb * size);
    if (!ptr) {
        __coverity_panic__();
    }
    __coverity_writeall0__(ptr);
    __coverity_mark_as_afm_allocated__(ptr, AFM_free);
    return ptr;
}

void *g_realloc_n(void *ptr, size_t nmemb, size_t size)
{
    __coverity_negative_sink__(nmemb);
    __coverity_negative_sink__(size);
    __coverity_escape__(ptr);
    ptr = __coverity_alloc__(nmemb * size);
    if (!ptr) {
        __coverity_panic__();
    }
    __coverity_writeall__(ptr);
    __coverity_mark_as_afm_allocated__(ptr, AFM_free);
    return ptr;
}

void g_free(void *ptr)
{
    __coverity_free__(ptr);
    __coverity_mark_as_afm_freed__(ptr, AFM_free);
}

void *g_try_malloc_n(size_t nmemb, size_t size)
{
    int nomem;

    if (nomem) {
        return NULL;
    }
    return g_malloc_n(nmemb, size);
}

void *g_try_malloc0_n(size_t nmemb, size_t size)
{
    int nomem;

    if (nomem) {
        return NULL;
    }
    return g_malloc0_n(nmemb, size);
}

void *g_try_realloc_n(void *ptr, size_t nmemb, size_t size)
{
    int nomem;

    if (nomem) {
        return NULL;
    }
    return g_realloc_n(ptr, nmemb, size);
}

void *g_malloc(size_t size)
{
    void *ptr;

    __coverity_negative_sink__(size);
    ptr = __coverity_alloc__(size);
    if (!ptr) {
        __coverity_panic__();
    }
    __coverity_mark_as_uninitialized_buffer__(ptr);
    __coverity_mark_as_afm_allocated__(ptr, AFM_free);
    return ptr;
}

void *g_malloc0(size_t size)
{
    void *ptr;

    __coverity_negative_sink__(size);
    ptr = __coverity_alloc__(size);
    if (!ptr) {
        __coverity_panic__();
    }
    __coverity_writeall0__(ptr);
    __coverity_mark_as_afm_allocated__(ptr, AFM_free);
    return ptr;
}

void *g_realloc(void *ptr, size_t size)
{
    __coverity_negative_sink__(size);
    __coverity_escape__(ptr);
    ptr = __coverity_alloc__(size);
    if (!ptr) {
        __coverity_panic__();
    }
    __coverity_writeall__(ptr);
    __coverity_mark_as_afm_allocated__(ptr, AFM_free);
    return ptr;
}

void *g_try_malloc(size_t size)
{
    int nomem;

    if (nomem) {
        return NULL;
    }
    return g_malloc(size);
}

void *g_try_malloc0(size_t size)
{
    int nomem;

    if (nomem) {
        return NULL;
    }
    return g_malloc0(size);
}

void *g_try_realloc(void *ptr, size_t size)
{
    int nomem;

    if (nomem) {
        return NULL;
    }
    return g_realloc(ptr, size);
}

typedef struct pollfd GPollFD;

int poll();

int g_poll (GPollFD *fds, unsigned nfds, int timeout)
{
    return poll(fds, nfds, timeout);
}

typedef struct _GIOChannel GIOChannel;
GIOChannel *g_io_channel_unix_new(int fd)
{
    GIOChannel *c = g_malloc0(20 * sizeof(void *));
    __coverity_escape__(fd);
    return c;
}

void g_assertion_message_expr(const char     *domain,
                              const char     *file,
                              int             line,
                              const char     *func,
                              const char     *expr)
{
    __coverity_panic__();
}
