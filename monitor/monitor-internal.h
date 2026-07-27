/*
 * QEMU monitor
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef MONITOR_INTERNAL_H
#define MONITOR_INTERNAL_H

#include "chardev/char-fe.h"
#include "monitor/monitor.h"
#include "qapi/qapi-types-control.h"
#include "qapi/qmp-registry.h"
#include "qobject/json-parser.h"
#include "qemu/readline.h"
#include "system/iothread.h"

typedef struct HMPCommand {
    const char *name;
    const char *args_type;
    const char *params;
    const char *help;
    const char *flags;
    void (*cmd)(Monitor *mon, const QDict *qdict);
    HumanReadableText *(*cmd_info_hrt)(Error **errp);
    bool coroutine;
    struct HMPCommand *sub_table;
    void (*command_completion)(ReadLineState *rs, int nb_args, const char *str);
} HMPCommand;

struct Monitor {
    CharFrontend chr;
    int suspend_cnt;
    bool is_qmp;
    bool skip_flush;
    bool use_io_thread;

    char *mon_cpu_path;
    QTAILQ_ENTRY(Monitor) entry;

    QemuMutex mon_lock;

    QLIST_HEAD(, mon_fd_t) fds;
    GString *outbuf;
    guint out_watch;
    int mux_out;
    int reset_seen;
};

struct MonitorHMP {
    Monitor common;
    bool use_readline;
    ReadLineState *rs;
};

typedef struct {
    Monitor common;
    JSONMessageParser parser;
    bool pretty;
    const QmpCommandList *commands;
    bool capab_offered[QMP_CAPABILITY__MAX];
    bool capab[QMP_CAPABILITY__MAX];
    QemuMutex qmp_queue_lock;
    GQueue *qmp_requests;
} MonitorQMP;

static inline bool monitor_is_qmp(const Monitor *mon)
{
    return mon->is_qmp;
}

typedef QTAILQ_HEAD(MonitorList, Monitor) MonitorList;
extern IOThread *mon_iothread;
extern Coroutine *qmp_dispatcher_co;
extern bool qmp_dispatcher_co_shutdown;
extern QmpCommandList qmp_commands, qmp_cap_negotiation_commands;
extern QemuMutex monitor_lock;
extern MonitorList mon_list;

extern HMPCommand hmp_cmds[];

void monitor_data_init(Monitor *mon, bool is_qmp, bool skip_flush,
                       bool use_io_thread);
void monitor_data_destroy(Monitor *mon);
int monitor_can_read(void *opaque);
void monitor_list_append(Monitor *mon);
void monitor_fdsets_cleanup(void);

void qmp_send_response(MonitorQMP *mon, const QDict *rsp);
void monitor_data_destroy_qmp(MonitorQMP *mon);
void coroutine_fn monitor_qmp_dispatcher_co(void *data);
void qmp_dispatcher_co_wake(void);

int get_monitor_def(Monitor *mon, int64_t *pval, const char *name);
void handle_hmp_command(MonitorHMP *mon, const char *cmdline);
int hmp_compare_cmd(const char *name, const char *list);

#endif
