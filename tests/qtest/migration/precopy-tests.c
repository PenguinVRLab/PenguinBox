/*
 * QTest testcase for precopy migration
 *
 * Copyright (c) 2016-2018 Red Hat, Inc. and/or its affiliates
 *   based on the vhost-user-test.c that is:
 *      Copyright (c) 2014 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "chardev/char.h"
#include "crypto/tlscredspsk.h"
#include "libqtest.h"
#include "migration/bootfile.h"
#include "migration/framework.h"
#include "migration/migration-qmp.h"
#include "migration/migration-util.h"
#include "ppc-util.h"
#include "qobject/qlist.h"
#include "qapi-types-migration.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/range.h"
#include "qemu/sockets.h"


#define DIRTYLIMIT_TOLERANCE_RANGE  25

static char *tmpfs;

static void test_precopy_unix_plain(void)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    MigrateCommon args = {
        .listen_uri = uri,
        .connect_uri = uri,
        .live = true,
    };

    test_precopy_common(&args);
}

static void test_precopy_unix_suspend_live(void)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    MigrateCommon args = {
        .listen_uri = uri,
        .connect_uri = uri,
        .live = true,
        .start.suspend_me = true,
    };

    test_precopy_common(&args);
}

static void test_precopy_unix_suspend_notlive(void)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    MigrateCommon args = {
        .listen_uri = uri,
        .connect_uri = uri,
        .start.suspend_me = true,
    };

    test_precopy_common(&args);
}

static void test_precopy_unix_dirty_ring(void)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    MigrateCommon args = {
        .start = {
            .use_dirty_ring = true,
        },
        .listen_uri = uri,
        .connect_uri = uri,
        .live = true,
    };

    test_precopy_common(&args);
}

#ifdef CONFIG_RDMA

#include <sys/resource.h>

#define REQUIRED_MEMLOCK_SZ (128 << 20)

static bool mlock_check(void)
{
    uid_t uid;
    struct rlimit rlim;

    uid = getuid();
    if (uid == 0) {
        return true;
    }

    if (getrlimit(RLIMIT_MEMLOCK, &rlim) != 0) {
        return false;
    }

    return rlim.rlim_cur >= REQUIRED_MEMLOCK_SZ;
}

#define RDMA_MIGRATION_HELPER "scripts/rdma-migration-helper.sh"
static int new_rdma_link(char *buffer, bool ipv6)
{
    char cmd[256];
    bool verbose = g_getenv("QTEST_LOG");

    snprintf(cmd, sizeof(cmd), "IP_FAMILY=%s %s detect %s",
             ipv6 ? "ipv6" : "ipv4", RDMA_MIGRATION_HELPER,
             verbose ? "" : "2>/dev/null");

    FILE *pipe = popen(cmd, "r");
    if (pipe == NULL) {
        perror("Failed to run script");
        return -1;
    }

    int idx = 0;
    while (fgets(buffer + idx, 128 - idx, pipe) != NULL) {
        idx += strlen(buffer);
    }

    int status = pclose(pipe);
    if (status == -1) {
        perror("Error reported by pclose()");
        return -1;
    } else if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    return -1;
}

static void __test_precopy_rdma_plain(bool ipv6)
{
    char buffer[128] = {};

    if (!mlock_check()) {
        g_test_skip("'ulimit -l' is too small, require >=128M");
        return;
    }

    if (new_rdma_link(buffer, ipv6)) {
        g_test_skip("No rdma link available\n"
                    "# To enable the test:\n"
                    "# Run \'" RDMA_MIGRATION_HELPER " setup\' with root to "
                    "setup a new rdma/rxe link and rerun the test\n"
                    "# Optional: run 'scripts/rdma-migration-helper.sh clean' "
                    "to revert the 'setup'");
        return;
    }

    g_autofree char *uri = g_strdup_printf("rdma:%s:29200", buffer);

    MigrateCommon args = {
        .listen_uri = uri,
        .connect_uri = uri,
    };

    test_precopy_common(&args);
}

static void test_precopy_rdma_plain(void)
{
    __test_precopy_rdma_plain(false);
}

static void test_precopy_rdma_plain_ipv6(void)
{
    __test_precopy_rdma_plain(true);
}
#endif

static void test_precopy_tcp_plain(void)
{
    MigrateCommon args = {
        .listen_uri = "tcp:127.0.0.1:0",
    };

    test_precopy_common(&args);
}

static void test_precopy_tcp_switchover_ack(void)
{
    MigrateCommon args = {
        .listen_uri = "tcp:127.0.0.1:0",
        .start = {
            .caps[MIGRATION_CAPABILITY_RETURN_PATH] = true,
            .caps[MIGRATION_CAPABILITY_SWITCHOVER_ACK] = true,
        },
        .live = true,
    };

    test_precopy_common(&args);
}

#ifndef _WIN32
static void *migrate_hook_start_fd(QTestState *from,
                                   QTestState *to)
{
    int ret;
    int pair[2];

    ret = qemu_socketpair(PF_LOCAL, SOCK_STREAM, 0, pair);
    g_assert_cmpint(ret, ==, 0);

    qtest_qmp_fds_assert_success(to, &pair[0], 1,
                                 "{ 'execute': 'getfd',"
                                 "  'arguments': { 'fdname': 'fd-mig' }}");
    close(pair[0]);

    migrate_incoming_qmp(to, "fd:fd-mig", NULL, "{}");

    qtest_qmp_fds_assert_success(from, &pair[1], 1,
                                 "{ 'execute': 'getfd',"
                                 "  'arguments': { 'fdname': 'fd-mig' }}");
    close(pair[1]);

    return NULL;
}

static void migrate_hook_end_fd(QTestState *from,
                                QTestState *to,
                                void *opaque)
{
    QDict *rsp;
    const char *error_desc;

    rsp = qtest_qmp(from,
                    "{ 'execute': 'closefd',"
                    "  'arguments': { 'fdname': 'fd-mig' }}");
    g_assert_true(qdict_haskey(rsp, "error"));
    error_desc = qdict_get_str(qdict_get_qdict(rsp, "error"), "desc");
    g_assert_cmpstr(error_desc, ==, "File descriptor named 'fd-mig' not found");
    qobject_unref(rsp);

    rsp = qtest_qmp(to,
                    "{ 'execute': 'closefd',"
                    "  'arguments': { 'fdname': 'fd-mig' }}");
    g_assert_true(qdict_haskey(rsp, "error"));
    error_desc = qdict_get_str(qdict_get_qdict(rsp, "error"), "desc");
    g_assert_cmpstr(error_desc, ==, "File descriptor named 'fd-mig' not found");
    qobject_unref(rsp);
}

static void test_precopy_fd_socket(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .connect_uri = "fd:fd-mig",
        .start_hook = migrate_hook_start_fd,
        .end_hook = migrate_hook_end_fd,
    };
    test_precopy_common(&args);
}

static void *migrate_hook_start_precopy_fd_file(QTestState *from,
                                                QTestState *to)
{
    g_autofree char *file = g_strdup_printf("%s/%s", tmpfs, FILE_TEST_FILENAME);
    int src_flags = O_CREAT | O_RDWR;
    int dst_flags = O_CREAT | O_RDWR;
    int fds[2];

    fds[0] = open(file, src_flags, 0660);
    assert(fds[0] != -1);

    fds[1] = open(file, dst_flags, 0660);
    assert(fds[1] != -1);


    qtest_qmp_fds_assert_success(to, &fds[0], 1,
                                 "{ 'execute': 'getfd',"
                                 "  'arguments': { 'fdname': 'fd-mig' }}");

    qtest_qmp_fds_assert_success(from, &fds[1], 1,
                                 "{ 'execute': 'getfd',"
                                 "  'arguments': { 'fdname': 'fd-mig' }}");

    close(fds[0]);
    close(fds[1]);

    return NULL;
}

static void test_precopy_fd_file(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .connect_uri = "fd:fd-mig",
        .start_hook = migrate_hook_start_precopy_fd_file,
        .end_hook = migrate_hook_end_fd,
    };
    test_file_common(&args, true);
}
#endif

static void test_auto_converge(void)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    MigrateStart args = {};
    QTestState *from, *to;
    int64_t percentage;

    const int64_t init_pct = 5, inc_pct = 25, max_pct = 95;
    uint64_t prev_dirty_sync_cnt, dirty_sync_cnt;
    int max_try_count, hit = 0;

    if (migrate_start(&from, &to, uri, &args)) {
        return;
    }

    migrate_set_capability(from, "auto-converge", true);
    migrate_set_parameter_int(from, "cpu-throttle-initial", init_pct);
    migrate_set_parameter_int(from, "cpu-throttle-increment", inc_pct);
    migrate_set_parameter_int(from, "max-cpu-throttle", max_pct);

    migrate_ensure_non_converge(from);

    migrate_set_capability(from, "pause-before-switchover", true);

    wait_for_serial("src_serial");

    migrate_qmp(from, to, uri, NULL, "{}");

    percentage = 0;
    do {
        percentage = read_migrate_property_int(from, "cpu-throttle-percentage");
        if (percentage != 0) {
            break;
        }
        usleep(20);
        g_assert_false(get_src()->stop_seen);
    } while (true);
    g_assert_cmpint(percentage, >=, init_pct);

    while ((dirty_sync_cnt = get_migration_pass(from)) < 2) {
        usleep(1000 * 1000);
    }

    prev_dirty_sync_cnt = dirty_sync_cnt;

    max_try_count = 10;
    while (--max_try_count) {
        dirty_sync_cnt = get_migration_pass(from);
        if (dirty_sync_cnt != prev_dirty_sync_cnt) {
            hit = 1;
            break;
        }
        prev_dirty_sync_cnt = dirty_sync_cnt;
        sleep(1);
    }
    g_assert_cmpint(hit, ==, 1);

    migrate_ensure_converge(from);

    wait_for_migration_status(from, "pre-switchover", NULL);

    percentage = read_migrate_property_int(from, "cpu-throttle-percentage");
    g_assert_cmpint(percentage, <=, max_pct);
    migrate_continue(from, "pre-switchover");

    qtest_qmp_eventwait(to, "RESUME");

    wait_for_serial("dest_serial");
    wait_for_migration_complete(from);

    migrate_end(from, to, true);
}

static void *
migrate_hook_start_precopy_tcp_multifd(QTestState *from,
                                       QTestState *to)
{
    return migrate_hook_start_precopy_tcp_multifd_common(from, to, "none");
}

static void *
migrate_hook_start_precopy_tcp_multifd_zero_page_legacy(QTestState *from,
                                                        QTestState *to)
{
    migrate_hook_start_precopy_tcp_multifd_common(from, to, "none");
    migrate_set_parameter_str(from, "zero-page-detection", "legacy");
    return NULL;
}

static void *
migrate_hook_start_precopy_tcp_multifd_no_zero_page(QTestState *from,
                                                    QTestState *to)
{
    migrate_hook_start_precopy_tcp_multifd_common(from, to, "none");
    migrate_set_parameter_str(from, "zero-page-detection", "none");
    return NULL;
}

static void test_multifd_tcp_uri_none(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start_hook = migrate_hook_start_precopy_tcp_multifd,
        .start = {
            .caps[MIGRATION_CAPABILITY_MULTIFD] = true,
        },
        .live = true,
    };
    test_precopy_common(&args);
}

static void test_multifd_tcp_zero_page_legacy(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start_hook = migrate_hook_start_precopy_tcp_multifd_zero_page_legacy,
        .start = {
            .caps[MIGRATION_CAPABILITY_MULTIFD] = true,
        },
        .live = true,
    };
    test_precopy_common(&args);
}

static void test_multifd_tcp_no_zero_page(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start_hook = migrate_hook_start_precopy_tcp_multifd_no_zero_page,
        .start = {
            .caps[MIGRATION_CAPABILITY_MULTIFD] = true,
        },
        .live = true,
    };
    test_precopy_common(&args);
}

static void test_multifd_tcp_channels_none(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start_hook = migrate_hook_start_precopy_tcp_multifd,
        .live = true,
        .start = {
            .caps[MIGRATION_CAPABILITY_MULTIFD] = true,
        },
        .connect_channels = ("[ { 'channel-type': 'main',"
                             "    'addr': { 'transport': 'socket',"
                             "              'type': 'inet',"
                             "              'host': '127.0.0.1',"
                             "              'port': '0' } } ]"),
    };
    test_precopy_common(&args);
}

static void test_multifd_tcp_cancel(bool postcopy_ram)
{
    MigrateStart args = {
        .hide_stderr = true,
    };
    QTestState *from, *to, *to2;

    if (migrate_start(&from, &to, "defer", &args)) {
        return;
    }

    migrate_ensure_non_converge(from);
    migrate_prepare_for_dirty_mem(from);

    if (postcopy_ram) {
        migrate_set_capability(from, "postcopy-ram", true);
        migrate_set_capability(to, "postcopy-ram", true);
    }

    migrate_set_parameter_int(from, "multifd-channels", 16);
    migrate_set_parameter_int(to, "multifd-channels", 16);

    migrate_set_capability(from, "multifd", true);
    migrate_set_capability(to, "multifd", true);

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");

    wait_for_serial("src_serial");

    migrate_qmp(from, to, NULL, NULL, "{}");

    migrate_wait_for_dirty_mem(from, to);

    migrate_cancel(from);

    qtest_set_expected_status(to, EXIT_FAILURE);
    qtest_wait_qemu(to);
    qtest_quit(to);

    wait_for_migration_status(from, "cancelled", NULL);

    args = (MigrateStart){
        .only_target = true,
    };

    if (migrate_start(&from, &to2, "defer", &args)) {
        return;
    }

    if (postcopy_ram) {
        migrate_set_capability(to2, "postcopy-ram", true);
    }

    migrate_set_parameter_int(to2, "multifd-channels", 16);

    migrate_set_capability(to2, "multifd", true);

    migrate_incoming_qmp(to2, "tcp:127.0.0.1:0", NULL, "{}");

    migrate_ensure_non_converge(from);

    migrate_qmp(from, to2, NULL, NULL, "{}");

    migrate_wait_for_dirty_mem(from, to2);

    migrate_ensure_converge(from);

    wait_for_stop(from, get_src());
    qtest_qmp_eventwait(to2, "RESUME");

    wait_for_serial("dest_serial");
    wait_for_migration_complete(from);
    migrate_end(from, to2, true);
}

static void test_multifd_precopy_tcp_cancel(void)
{
    test_multifd_tcp_cancel(false);
}

static void test_multifd_postcopy_tcp_cancel(void)
{
    test_multifd_tcp_cancel(true);
}

static void test_cancel_src_after_failed(QTestState *from, QTestState *to,
                                         const char *uri, const char *phase)
{

    wait_for_serial("src_serial");
    migrate_ensure_converge(from);

    migrate_qmp(from, to, uri, NULL, "{}");

    migration_event_wait(from, phase);
    migrate_cancel(from);

    wait_for_migration_status(from, "failed",
                              (const char * []) { "completed", NULL });

}

static void test_cancel_src_after_cancelled(QTestState *from, QTestState *to,
                                            const char *uri, const char *phase)
{
    migrate_incoming_qmp(to, uri, NULL, "{ 'exit-on-error': false }");

    wait_for_serial("src_serial");
    migrate_ensure_converge(from);

    migrate_qmp(from, to, uri, NULL, "{}");

    migrate_cancel(from);
    migration_event_wait(from, phase);

    migrate_cancel(from);

    wait_for_migration_status(from, "cancelled",
                              (const char * []) { "completed", NULL });

    wait_for_migration_status(to, "failed",
                              (const char * []) { "completed", NULL });
}

static void test_cancel_src_after_complete(QTestState *from, QTestState *to,
                                           const char *uri, const char *phase)
{
    migrate_incoming_qmp(to, uri, NULL, "{ 'exit-on-error': false }");

    wait_for_serial("src_serial");
    migrate_ensure_converge(from);

    migrate_qmp(from, to, uri, NULL, "{}");

    migration_event_wait(from, phase);
    migrate_cancel(from);

    wait_for_migration_complete(from);
    wait_for_migration_complete(to);
}

static void test_cancel_src_after_none(QTestState *from, QTestState *to,
                                       const char *uri, const char *phase)
{
    migrate_cancel(to);

    wait_for_serial("src_serial");
    migrate_cancel(from);

    migrate_incoming_qmp(to, uri, NULL, "{ 'exit-on-error': false }");

    migrate_ensure_converge(from);
    migrate_qmp(from, to, uri, NULL, "{}");

    wait_for_migration_complete(from);
    wait_for_migration_complete(to);
}

static void test_cancel_src_pre_switchover(QTestState *from, QTestState *to,
                                           const char *uri, const char *phase)
{
    migrate_set_capability(from, "pause-before-switchover", true);
    migrate_set_capability(to, "pause-before-switchover", true);

    migrate_set_capability(from, "multifd", true);
    migrate_set_capability(to, "multifd", true);

    migrate_incoming_qmp(to, uri, NULL, "{ 'exit-on-error': false }");

    wait_for_serial("src_serial");
    migrate_ensure_converge(from);

    migrate_qmp(from, to, uri, NULL, "{}");

    migration_event_wait(from, phase);
    migrate_cancel(from);
    migration_event_wait(from, "cancelling");

    wait_for_migration_status(from, "cancelled",
                              (const char * []) { "completed", NULL });

    wait_for_migration_status(to, "failed",
                              (const char * []) { "completed", NULL });
}

static void test_cancel_src_after_status(void *opaque)
{
    const char *test_path = opaque;
    g_autofree char *phase = g_path_get_basename(test_path);
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    QTestState *from, *to;
    MigrateStart args = {
        .hide_stderr = true,
    };

    if (migrate_start(&from, &to, "defer", &args)) {
        return;
    }

    if (g_str_equal(phase, "cancelling") ||
        g_str_equal(phase, "cancelled")) {
        test_cancel_src_after_cancelled(from, to, uri, phase);

    } else if (g_str_equal(phase, "completed")) {
        test_cancel_src_after_complete(from, to, uri, phase);

    } else if (g_str_equal(phase, "failed")) {
        test_cancel_src_after_failed(from, to, uri, phase);

    } else if (g_str_equal(phase, "none")) {
        test_cancel_src_after_none(from, to, uri, phase);

    } else {
        test_cancel_src_pre_switchover(from, to, uri, phase);
    }

    migrate_end(from, to, false);
}

static void calc_dirty_rate(QTestState *who, uint64_t calc_time)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'calc-dirty-rate',"
                             "'arguments': { "
                             "'calc-time': %" PRIu64 ","
                             "'mode': 'dirty-ring' }}",
                             calc_time);
}

static QDict *query_dirty_rate(QTestState *who)
{
    return qtest_qmp_assert_success_ref(who,
                                        "{ 'execute': 'query-dirty-rate' }");
}

static void dirtylimit_set_all(QTestState *who, uint64_t dirtyrate)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'set-vcpu-dirty-limit',"
                             "'arguments': { "
                             "'dirty-rate': %" PRIu64 " } }",
                             dirtyrate);
}

static void cancel_vcpu_dirty_limit(QTestState *who)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'cancel-vcpu-dirty-limit' }");
}

static QDict *query_vcpu_dirty_limit(QTestState *who)
{
    QDict *rsp;

    rsp = qtest_qmp(who, "{ 'execute': 'query-vcpu-dirty-limit' }");
    g_assert(!qdict_haskey(rsp, "error"));
    g_assert(qdict_haskey(rsp, "return"));

    return rsp;
}

static bool calc_dirtyrate_ready(QTestState *who)
{
    QDict *rsp_return;
    const char *status;
    bool ready;

    rsp_return = query_dirty_rate(who);
    g_assert(rsp_return);

    status = qdict_get_str(rsp_return, "status");
    g_assert(status);
    ready = g_strcmp0(status, "measuring");
    qobject_unref(rsp_return);

    return ready;
}

static void wait_for_calc_dirtyrate_complete(QTestState *who,
                                             int64_t time_s)
{
    int max_try_count = 10000;
    usleep(time_s * 1000000);

    while (!calc_dirtyrate_ready(who) && max_try_count--) {
        usleep(1000);
    }

    g_assert_cmpint(max_try_count, !=, 0);
}

static int64_t get_dirty_rate(QTestState *who)
{
    QDict *rsp_return;
    const char *status;
    QList *rates;
    const QListEntry *entry;
    QDict *rate;
    int64_t dirtyrate;

    rsp_return = query_dirty_rate(who);
    g_assert(rsp_return);

    status = qdict_get_str(rsp_return, "status");
    g_assert(status);
    g_assert_cmpstr(status, ==, "measured");

    rates = qdict_get_qlist(rsp_return, "vcpu-dirty-rate");
    g_assert(rates && !qlist_empty(rates));

    entry = qlist_first(rates);
    g_assert(entry);

    rate = qobject_to(QDict, qlist_entry_obj(entry));
    g_assert(rate);

    dirtyrate = qdict_get_try_int(rate, "dirty-rate", -1);

    qobject_unref(rsp_return);
    return dirtyrate;
}

static int64_t get_limit_rate(QTestState *who)
{
    QDict *rsp_return;
    QList *rates;
    const QListEntry *entry;
    QDict *rate;
    int64_t dirtyrate;

    rsp_return = query_vcpu_dirty_limit(who);
    g_assert(rsp_return);

    rates = qdict_get_qlist(rsp_return, "return");
    g_assert(rates && !qlist_empty(rates));

    entry = qlist_first(rates);
    g_assert(entry);

    rate = qobject_to(QDict, qlist_entry_obj(entry));
    g_assert(rate);

    dirtyrate = qdict_get_try_int(rate, "limit-rate", -1);

    qobject_unref(rsp_return);
    return dirtyrate;
}

static QTestState *dirtylimit_start_vm(void)
{
    QTestState *vm = NULL;
    g_autofree gchar *cmd = NULL;
    const char *bootpath;

    bootpath = bootfile_create(qtest_get_arch(), tmpfs, false);
    cmd = g_strdup_printf("-accel kvm,dirty-ring-size=4096 "
                          "-name dirtylimit-test,debug-threads=on "
                          "-m 150M -smp 1 "
                          "-serial file:%s/vm_serial "
                          "-drive file=%s,format=raw ",
                          tmpfs, bootpath);

    vm = qtest_init(cmd);
    return vm;
}

static void dirtylimit_stop_vm(QTestState *vm)
{
    g_autofree char *path = g_strdup_printf("%s/%s", tmpfs, "vm_serial");

    qtest_quit(vm);
    unlink(path);
}

static void test_vcpu_dirty_limit(void)
{
    QTestState *vm;
    int64_t origin_rate;
    int64_t quota_rate;
    int64_t rate ;
    int max_try_count = 20;
    int hit = 0;

    vm = dirtylimit_start_vm();

    wait_for_serial("vm_serial");

    calc_dirty_rate(vm, 1);

    wait_for_calc_dirtyrate_complete(vm, 1);

    origin_rate = get_dirty_rate(vm);

    assert(origin_rate != 0);

    quota_rate = origin_rate / 2;

    dirtylimit_set_all(vm, quota_rate);

    g_assert_cmpint(quota_rate, ==, get_limit_rate(vm));

    usleep(2000000);

    while (--max_try_count) {
        calc_dirty_rate(vm, 1);
        wait_for_calc_dirtyrate_complete(vm, 1);
        rate = get_dirty_rate(vm);

        if (rate < (quota_rate + DIRTYLIMIT_TOLERANCE_RANGE)) {
            hit = 1;
            break;
        }
    }

    g_assert_cmpint(hit, ==, 1);

    hit = 0;
    max_try_count = 20;

    cancel_vcpu_dirty_limit(vm);
    while (--max_try_count) {
        calc_dirty_rate(vm, 1);
        wait_for_calc_dirtyrate_complete(vm, 1);
        rate = get_dirty_rate(vm);

        if (rate > (quota_rate + DIRTYLIMIT_TOLERANCE_RANGE)) {
            hit = 1;
            break;
        }
    }

    g_assert_cmpint(hit, ==, 1);
    dirtylimit_stop_vm(vm);
}

static void migrate_dirty_limit_wait_showup(QTestState *from,
                                            const int64_t period,
                                            const int64_t value)
{
    migrate_set_capability(from, "dirty-limit", true);

    migrate_set_parameter_int(from, "x-vcpu-dirty-limit-period", period);
    migrate_set_parameter_int(from, "vcpu-dirty-limit", value);

    migrate_ensure_non_converge(from);

    migrate_set_capability(from, "pause-before-switchover", true);

    wait_for_serial("src_serial");
}

static void test_dirty_limit(void)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    QTestState *from, *to;
    int64_t remaining;
    uint64_t throttle_us_per_full;
    const int64_t dirtylimit_period = 1000, dirtylimit_value = 50;
    const int64_t max_bandwidth = 400000000;
    const int64_t downtime_limit = 250;
    const int64_t expected_threshold = max_bandwidth * downtime_limit / 1000;
    int max_try_count = 10;
    MigrateCommon args = {
        .start = {
            .hide_stderr = true,
            .use_dirty_ring = true,
        },
        .listen_uri = uri,
        .connect_uri = uri,
    };

    if (migrate_start(&from, &to, args.listen_uri, &args.start)) {
        return;
    }

    migrate_dirty_limit_wait_showup(from, dirtylimit_period, dirtylimit_value);

    migrate_qmp(from, to, args.connect_uri, NULL, "{}");

    throttle_us_per_full = 0;
    while (throttle_us_per_full == 0) {
        throttle_us_per_full =
            read_migrate_property_int(from,
                                      "dirty-limit-throttle-time-per-round");
        usleep(100);
        g_assert_false(get_src()->stop_seen);
    }

    migrate_cancel(from);
    wait_for_migration_status(from, "cancelled", NULL);

    migration_event_wait(to, "failed");
    qtest_set_expected_status(to, EXIT_FAILURE);
    qtest_quit(to);

    do {
        throttle_us_per_full =
            read_migrate_property_int(from,
                                      "dirty-limit-throttle-time-per-round");
        usleep(100);
        g_assert_false(get_src()->stop_seen);
    } while (throttle_us_per_full != 0 && --max_try_count);

    g_assert_cmpint(throttle_us_per_full, ==, 0);

    args = (MigrateCommon) {
        .start = {
            .only_target = true,
            .use_dirty_ring = true,
        },
        .listen_uri = uri,
        .connect_uri = uri,
    };

    if (migrate_start(&from, &to, args.listen_uri, &args.start)) {
        return;
    }

    migrate_qmp(from, to, args.connect_uri, NULL, "{}");

    throttle_us_per_full = 0;
    while (throttle_us_per_full == 0) {
        throttle_us_per_full =
            read_migrate_property_int(from,
                                      "dirty-limit-throttle-time-per-round");
        usleep(100);
        g_assert_false(get_src()->stop_seen);
    }

    g_assert_cmpint(dirtylimit_value, ==, get_limit_rate(from));

    migrate_set_parameter_int(from, "downtime-limit", downtime_limit);
    migrate_set_parameter_int(from, "max-bandwidth", max_bandwidth);

    wait_for_migration_status(from, "pre-switchover", NULL);

    remaining = read_ram_property_int(from, "remaining");
    g_assert_cmpint(remaining, <,
                    (expected_threshold + expected_threshold / 100));

    migrate_continue(from, "pre-switchover");

    qtest_qmp_eventwait(to, "RESUME");

    wait_for_serial("dest_serial");
    wait_for_migration_complete(from);

    migrate_end(from, to, true);
}

static void migration_test_add_precopy_smoke(MigrationTestEnv *env)
{
    if (env->is_x86) {
        migration_test_add("/migration/precopy/unix/suspend/live",
                           test_precopy_unix_suspend_live);
        migration_test_add("/migration/precopy/unix/suspend/notlive",
                           test_precopy_unix_suspend_notlive);
    }

    migration_test_add("/migration/precopy/unix/plain",
                       test_precopy_unix_plain);

    migration_test_add("/migration/precopy/tcp/plain", test_precopy_tcp_plain);
    migration_test_add("/migration/multifd/tcp/uri/plain/none",
                       test_multifd_tcp_uri_none);
    migration_test_add("/migration/multifd/tcp/plain/cancel",
                       test_multifd_precopy_tcp_cancel);
    if (env->has_uffd) {
        migration_test_add("/migration/multifd+postcopy/tcp/plain/cancel",
                           test_multifd_postcopy_tcp_cancel);
    }

#ifdef CONFIG_RDMA
    migration_test_add("/migration/precopy/rdma/plain",
                       test_precopy_rdma_plain);
    migration_test_add("/migration/precopy/rdma/plain/ipv6",
                       test_precopy_rdma_plain_ipv6);
#endif
}

void migration_test_add_precopy(MigrationTestEnv *env)
{
    tmpfs = env->tmpfs;

    migration_test_add_precopy_smoke(env);

    if (!env->full_set) {
        return;
    }

    migration_test_add("/migration/precopy/tcp/plain/switchover-ack",
                       test_precopy_tcp_switchover_ack);

#ifndef _WIN32
    migration_test_add("/migration/precopy/fd/tcp",
                       test_precopy_fd_socket);
    migration_test_add("/migration/precopy/fd/file",
                       test_precopy_fd_file);
#endif

    if (g_test_slow()) {
        migration_test_add("/migration/auto_converge",
                           test_auto_converge);
        if (g_str_equal(env->arch, "x86_64") &&
            env->has_kvm && env->has_dirty_ring) {
            migration_test_add("/dirty_limit",
                               test_dirty_limit);
        }
    }
    migration_test_add("/migration/multifd/tcp/channels/plain/none",
                       test_multifd_tcp_channels_none);
    migration_test_add("/migration/multifd/tcp/plain/zero-page/legacy",
                       test_multifd_tcp_zero_page_legacy);
    migration_test_add("/migration/multifd/tcp/plain/zero-page/none",
                       test_multifd_tcp_no_zero_page);
    if (g_str_equal(env->arch, "x86_64")
        && env->has_kvm && env->has_dirty_ring) {

        migration_test_add("/migration/dirty_ring",
                           test_precopy_unix_dirty_ring);
        if (qtest_has_machine("pc") && g_test_slow()) {
            migration_test_add("/migration/vcpu_dirty_limit",
                               test_vcpu_dirty_limit);
        }
    }

    assert(MIGRATION_STATUS__MAX == 16);

    for (int i = MIGRATION_STATUS_NONE; i < MIGRATION_STATUS__MAX; i++) {
        switch (i) {
        case MIGRATION_STATUS_DEVICE:
        case MIGRATION_STATUS_WAIT_UNPLUG:
        case MIGRATION_STATUS_COLO:
        case MIGRATION_STATUS_POSTCOPY_DEVICE:
        case MIGRATION_STATUS_POSTCOPY_ACTIVE:
        case MIGRATION_STATUS_POSTCOPY_PAUSED:
        case MIGRATION_STATUS_POSTCOPY_RECOVER_SETUP:
        case MIGRATION_STATUS_POSTCOPY_RECOVER:
            continue;
        default:
            migration_test_add_suffix("/migration/cancel/src/after/",
                                      MigrationStatus_str(i),
                                      test_cancel_src_after_status);
        }
    }
}
