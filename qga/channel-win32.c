#include "qemu/osdep.h"
#include <windows.h>
#include <io.h>
#include "guest-agent-core.h"
#include "channel.h"

typedef struct GAChannelReadState {
    guint thread_id;
    uint8_t *buf;
    size_t buf_size;
    size_t cur;
    size_t pending;
    OVERLAPPED ov;
    bool ov_pending;
} GAChannelReadState;

struct GAChannel {
    HANDLE handle;
    GAChannelCallback cb;
    gpointer user_data;
    GAChannelReadState rstate;
    GIOCondition pending_events;
    GSource *source;
};

typedef struct GAWatch {
    GSource source;
    GPollFD pollfd;
    GAChannel *channel;
    GIOCondition events_mask;
} GAWatch;

static gboolean ga_channel_prepare(GSource *source, gint *timeout_ms)
{
    GAWatch *watch = (GAWatch *)source;
    GAChannel *c = (GAChannel *)watch->channel;
    GAChannelReadState *rs = &c->rstate;
    DWORD count_read, count_to_read = 0;
    bool success;
    GIOCondition new_events = 0;

    g_debug("prepare");
    if (!rs->ov_pending) {
        if (rs->cur + rs->pending >= rs->buf_size) {
            if (rs->cur) {
                memmove(rs->buf, rs->buf + rs->cur, rs->pending);
                rs->cur = 0;
            }
        }
        count_to_read = rs->buf_size - rs->cur - rs->pending;
    }

    if (rs->ov_pending || count_to_read <= 0) {
            goto out;
    }

    success = ReadFile(c->handle, rs->buf + rs->cur + rs->pending,
                       count_to_read, &count_read, &rs->ov);
    if (success) {
        rs->pending += count_read;
        rs->ov_pending = false;
    } else {
        if (GetLastError() == ERROR_IO_PENDING) {
            rs->ov_pending = true;
        } else {
            new_events |= G_IO_ERR;
        }
    }

out:
    *timeout_ms = 500;
    if (rs->pending) {
        new_events |= G_IO_IN;
    }
    c->pending_events |= new_events;
    return !!c->pending_events;
}

static gboolean ga_channel_check(GSource *source)
{
    GAWatch *watch = (GAWatch *)source;
    GAChannel *c = (GAChannel *)watch->channel;
    GAChannelReadState *rs = &c->rstate;
    DWORD count_read, error;
    BOOL success;

    GIOCondition new_events = 0;

    g_debug("check");

    g_assert(rs->ov_pending);

    success = GetOverlappedResult(c->handle, &rs->ov, &count_read, FALSE);
    if (success) {
        g_debug("thread: overlapped result, count_read: %d", (int)count_read);
        rs->pending += count_read;
        new_events |= G_IO_IN;
    } else {
        error = GetLastError();
        if (error == 0 || error == ERROR_HANDLE_EOF ||
            error == ERROR_NO_SYSTEM_RESOURCES ||
            error == ERROR_OPERATION_ABORTED) {
            new_events |= G_IO_HUP;
        } else if (error != ERROR_IO_INCOMPLETE) {
            g_critical("error retrieving overlapped result: %d", (int)error);
            new_events |= G_IO_ERR;
        }
    }

    if (new_events) {
        rs->ov_pending = 0;
    }
    c->pending_events |= new_events;

    return !!c->pending_events;
}

static gboolean ga_channel_dispatch(GSource *source, GSourceFunc unused,
                                    gpointer user_data)
{
    GAWatch *watch = (GAWatch *)source;
    GAChannel *c = (GAChannel *)watch->channel;
    GAChannelReadState *rs = &c->rstate;
    gboolean success;

    g_debug("dispatch");
    success = c->cb(watch->pollfd.revents, c->user_data);

    if (c->pending_events & G_IO_ERR) {
        g_critical("channel error, removing source");
        return false;
    }

    c->pending_events &= ~G_IO_HUP;
    if (!rs->pending) {
        c->pending_events &= ~G_IO_IN;
    } else {
        c->pending_events = 0;
    }
    return success;
}

static void ga_channel_finalize(GSource *source)
{
    g_debug("finalize");
}

GSourceFuncs ga_channel_watch_funcs = {
    ga_channel_prepare,
    ga_channel_check,
    ga_channel_dispatch,
    ga_channel_finalize
};

static GSource *ga_channel_create_watch(GAChannel *c)
{
    GSource *source = g_source_new(&ga_channel_watch_funcs, sizeof(GAWatch));
    GAWatch *watch = (GAWatch *)source;

    watch->channel = c;
    watch->pollfd.fd = (gintptr) c->rstate.ov.hEvent;
    g_source_add_poll(source, &watch->pollfd);

    return source;
}

GIOStatus ga_channel_read(GAChannel *c, char *buf, size_t size, gsize *count)
{
    GAChannelReadState *rs = &c->rstate;
    GIOStatus status;
    size_t to_read = 0;

    if (c->pending_events & G_IO_ERR) {
        return G_IO_STATUS_ERROR;
    }

    *count = to_read = MIN(size, rs->pending);
    if (to_read) {
        memcpy(buf, rs->buf + rs->cur, to_read);
        rs->cur += to_read;
        rs->pending -= to_read;
        status = G_IO_STATUS_NORMAL;
    } else {
        status = G_IO_STATUS_AGAIN;
    }

    return status;
}

static GIOStatus ga_channel_write(GAChannel *c, const char *buf, size_t size,
                                  size_t *count)
{
    GIOStatus status;
    OVERLAPPED ov = {0};
    BOOL ret;
    DWORD written;

    ov.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    ret = WriteFile(c->handle, buf, size, &written, &ov);
    if (!ret) {
        if (GetLastError() == ERROR_IO_PENDING) {
            ret = GetOverlappedResult(c->handle, &ov, &written, TRUE);
            if (!ret) {
                if (!GetLastError()) {
                    status = G_IO_STATUS_AGAIN;
                } else {
                    status = G_IO_STATUS_ERROR;
                }
            } else {
                status = G_IO_STATUS_NORMAL;
                *count = written;
            }
        } else {
            status = G_IO_STATUS_ERROR;
        }
    } else {
        status = G_IO_STATUS_NORMAL;
        *count = written;
    }

    if (ov.hEvent) {
        CloseHandle(ov.hEvent);
        ov.hEvent = NULL;
    }
    return status;
}

GIOStatus ga_channel_write_all(GAChannel *c, const char *buf, size_t size)
{
    GIOStatus status = G_IO_STATUS_NORMAL;
    size_t count = 0;

    while (size) {
        status = ga_channel_write(c, buf, size, &count);
        if (status == G_IO_STATUS_NORMAL) {
            size -= count;
            buf += count;
        } else if (status != G_IO_STATUS_AGAIN) {
            break;
        }
    }

    return status;
}

static gboolean ga_channel_open(GAChannel *c, GAChannelMethod method,
                                const gchar *path)
{
    COMMTIMEOUTS comTimeOut = {0};
    gchar newpath[MAXPATHLEN] = {0};
    comTimeOut.ReadIntervalTimeout = 1;

    if (method != GA_CHANNEL_VIRTIO_SERIAL && method != GA_CHANNEL_ISA_SERIAL) {
        g_critical("unsupported communication method");
        return false;
    }

    if (method == GA_CHANNEL_ISA_SERIAL) {
        snprintf(newpath, sizeof(newpath), "\\\\.\\%s", path);
    } else {
        g_strlcpy(newpath, path, sizeof(newpath));
    }

    c->handle = CreateFile(newpath, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED, NULL);
    if (c->handle == INVALID_HANDLE_VALUE) {
        g_autofree gchar *emsg = g_win32_error_message(GetLastError());
        g_critical("error opening path %s: %s", newpath, emsg);
        return false;
    }

    if (method == GA_CHANNEL_ISA_SERIAL
            && !SetCommTimeouts(c->handle, &comTimeOut)) {
        g_autofree gchar *emsg = g_win32_error_message(GetLastError());
        g_critical("error setting timeout for com port: %s", emsg);
        CloseHandle(c->handle);
        return false;
    }

    return true;
}

GAChannel *ga_channel_new(GAChannelMethod method, const gchar *path,
                          int listen_fd, GAChannelCallback cb, gpointer opaque)
{
    GAChannel *c = g_new0(GAChannel, 1);
    SECURITY_ATTRIBUTES sec_attrs;

    if (!ga_channel_open(c, method, path)) {
        g_critical("error opening channel");
        g_free(c);
        return NULL;
    }

    c->cb = cb;
    c->user_data = opaque;

    sec_attrs.nLength = sizeof(SECURITY_ATTRIBUTES);
    sec_attrs.lpSecurityDescriptor = NULL;
    sec_attrs.bInheritHandle = false;

    c->rstate.buf_size = QGA_READ_COUNT_DEFAULT;
    c->rstate.buf = g_malloc(QGA_READ_COUNT_DEFAULT);
    c->rstate.ov.hEvent = CreateEvent(&sec_attrs, FALSE, FALSE, NULL);

    c->source = ga_channel_create_watch(c);
    g_source_attach(c->source, NULL);
    return c;
}

void ga_channel_free(GAChannel *c)
{
    if (c->source) {
        g_source_destroy(c->source);
    }
    if (c->rstate.ov.hEvent) {
        CloseHandle(c->rstate.ov.hEvent);
    }
    g_free(c->rstate.buf);
    g_free(c);
}
