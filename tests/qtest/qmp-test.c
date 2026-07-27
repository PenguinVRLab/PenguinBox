/*
 * QMP protocol test cases
 *
 * Copyright (c) 2017-2018 Red Hat Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qapi/error.h"
#include "qapi/qapi-visit-control.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"
#include "qapi/qobject-input-visitor.h"
#include "qobject/qstring.h"

const char common_args[] = "-nodefaults -machine none";

static void test_version(QObject *version)
{
    Visitor *v;
    VersionInfo *vinfo;

    g_assert(version);
    v = qobject_input_visitor_new(version);
    visit_type_VersionInfo(v, "version", &vinfo, &error_abort);
    qapi_free_VersionInfo(vinfo);
    visit_free(v);
}

static void assert_recovered(QTestState *qts)
{
    QDict *resp;

    resp = qtest_qmp(qts, "{ 'execute': 'no-such-cmd' }");
    qmp_expect_error_and_unref(resp, "CommandNotFound");
}

static void test_malformed(QTestState *qts)
{
    QDict *resp;

    qtest_qmp_send_raw(qts, "{]\n");
    resp = qtest_qmp_receive_dict(qts);
    qmp_expect_error_and_unref(resp, "GenericError");
    assert_recovered(qts);

    qtest_qmp_send_raw(qts, "{\xFF");
    resp = qtest_qmp_receive_dict(qts);
    qmp_expect_error_and_unref(resp, "GenericError");
    assert_recovered(qts);

    qtest_qmp_send_raw(qts, "{\x01");
    resp = qtest_qmp_receive_dict(qts);
    qmp_expect_error_and_unref(resp, "GenericError");
    assert_recovered(qts);

    qtest_qmp_send_raw(qts, "{'bad \xFF");
    resp = qtest_qmp_receive_dict(qts);
    qmp_expect_error_and_unref(resp, "GenericError");
    assert_recovered(qts);

    qtest_qmp_send_raw(qts, "{'execute': 'nonexistent', 'id':'\n");
    resp = qtest_qmp_receive_dict(qts);
    qmp_expect_error_and_unref(resp, "GenericError");
    assert_recovered(qts);

    qtest_qmp_send_raw(qts, "%%p");
    resp = qtest_qmp_receive_dict(qts);
    qmp_expect_error_and_unref(resp, "GenericError");
    assert_recovered(qts);

    resp = qtest_qmp(qts, "null");
    qmp_expect_error_and_unref(resp, "GenericError");

    resp = qtest_qmp(qts, "{}");
    qmp_expect_error_and_unref(resp, "GenericError");

    resp = qtest_qmp(qts, "{ 'execute': true }");
    qmp_expect_error_and_unref(resp, "GenericError");

    resp = qtest_qmp(qts, "{ 'execute': 'no-such-cmd', 'arguments': [] }");
    qmp_expect_error_and_unref(resp, "GenericError");

    resp = qtest_qmp(qts, "{ 'execute': 'no-such-cmd', 'extra': true }");
    qmp_expect_error_and_unref(resp, "GenericError");
}

static void test_qmp_protocol(void)
{
    QDict *resp, *q, *ret;
    QList *capabilities;
    QTestState *qts;

    qts = qtest_init_without_qmp_handshake(common_args);

    resp = qtest_qmp_receive_dict(qts);
    q = qdict_get_qdict(resp, "QMP");
    g_assert(q);
    test_version(qdict_get(q, "version"));
    capabilities = qdict_get_qlist(q, "capabilities");
    g_assert(capabilities);
    qobject_unref(resp);

    resp = qtest_qmp(qts, "{ 'execute': 'query-version' }");
    qmp_expect_error_and_unref(resp, "CommandNotFound");

    test_malformed(qts);

    resp = qtest_qmp(qts, "{ 'execute': 'qmp_capabilities' }");
    ret = qdict_get_qdict(resp, "return");
    g_assert(ret && !qdict_size(ret));
    qobject_unref(resp);

    resp = qtest_qmp(qts, "{ 'execute': 'qmp_capabilities' }");
    qmp_expect_error_and_unref(resp, "CommandNotFound");

    resp = qtest_qmp(qts, "{ 'execute': 'query-version' }");
    test_version(qdict_get(resp, "return"));
    qobject_unref(resp);

    test_malformed(qts);

    resp = qtest_qmp(qts, "{ 'execute': 'query-name', 'id': 'cookie#1' }");
    ret = qdict_get_qdict(resp, "return");
    g_assert(ret);
    g_assert_cmpstr(qdict_get_try_str(resp, "id"), ==, "cookie#1");
    qobject_unref(resp);

    resp = qtest_qmp(qts, "{ 'execute': 'human-monitor-command', 'id': 2 }");
    g_assert_cmpint(qdict_get_int(resp, "id"), ==, 2);
    qmp_expect_error_and_unref(resp, "GenericError");

    qtest_quit(qts);
}

#ifndef _WIN32

char *tmpdir;
char *fifo_name;

static void setup_blocking_cmd(void)
{
    GError *err = NULL;
    tmpdir = g_dir_make_tmp("qmp-test-XXXXXX", &err);
    g_assert_no_error(err);

    fifo_name = g_strdup_printf("%s/fifo", tmpdir);
    if (mkfifo(fifo_name, 0666)) {
        g_error("mkfifo: %s", strerror(errno));
    }
}

static void cleanup_blocking_cmd(void)
{
    unlink(fifo_name);
    rmdir(tmpdir);
    g_free(tmpdir);
}

static void send_cmd_that_blocks(QTestState *s, const char *id)
{
    qtest_qmp_send(s, "{ 'execute': 'blockdev-add',  'id': %s,"
                   " 'arguments': {"
                   " 'driver': 'blkdebug', 'node-name': %s,"
                   " 'config': %s,"
                   " 'image': { 'driver': 'null-co', 'read-zeroes': true } } }",
                   id, id, fifo_name);
}

static void unblock_blocked_cmd(void)
{
    int fd = open(fifo_name, O_WRONLY);
    g_assert(fd >= 0);
    close(fd);
}

static void send_oob_cmd_that_fails(QTestState *s, const char *id)
{
    qtest_qmp_send(s, "{ 'exec-oob': 'migrate-pause', 'id': %s }", id);
}

static void recv_cmd_id(QTestState *s, const char *id)
{
    QDict *resp = qtest_qmp_receive_dict(s);

    g_assert_cmpstr(qdict_get_try_str(resp, "id"), ==, id);
    qobject_unref(resp);
}

static void test_qmp_oob(void)
{
    QTestState *qts;
    QDict *resp, *q;
    const QListEntry *entry;
    QList *capabilities;
    QString *qstr;

    qts = qtest_init_without_qmp_handshake(common_args);

    resp = qtest_qmp_receive_dict(qts);
    q = qdict_get_qdict(resp, "QMP");
    g_assert(q);
    capabilities = qdict_get_qlist(q, "capabilities");
    g_assert(capabilities && !qlist_empty(capabilities));
    entry = qlist_first(capabilities);
    g_assert(entry);
    qstr = qobject_to(QString, entry->value);
    g_assert(qstr);
    g_assert_cmpstr(qstring_get_str(qstr), ==, "oob");
    qobject_unref(resp);

    resp = qtest_qmp(qts,
                     "{ 'execute': 'qmp_capabilities', "
                     "  'arguments': { 'enable': [ 'cap-does-not-exist' ] } }");
    g_assert(qdict_haskey(resp, "error"));
    qobject_unref(resp);

    resp = qtest_qmp(qts,
                     "{ 'execute': 'qmp_capabilities', "
                     "  'arguments': { 'enable': [ 'oob' ] } }");
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);

    resp = qtest_qmp(qts, "{ 'exec-oob': 'query-cpus-fast' }");
    g_assert(qdict_haskey(resp, "error"));
    qobject_unref(resp);

    setup_blocking_cmd();
    send_cmd_that_blocks(qts, "ib-blocks-1");
    qtest_qmp_send(qts, "{ 'execute': 'query-name', 'id': 'ib-quick-1' }");
    send_oob_cmd_that_fails(qts, "oob-1");
    recv_cmd_id(qts, "oob-1");
    unblock_blocked_cmd();
    recv_cmd_id(qts, "ib-blocks-1");
    recv_cmd_id(qts, "ib-quick-1");

    send_cmd_that_blocks(qts, "blocks-2");
    qtest_qmp_send(qts, "{ 'id': 'err-2' }");
    unblock_blocked_cmd();
    recv_cmd_id(qts, "blocks-2");
    recv_cmd_id(qts, "err-2");
    cleanup_blocking_cmd();

    qtest_quit(qts);
}

#endif

static void test_qmp_preconfig(void)
{
    QDict *rsp, *ret;
    QTestState *qs = qtest_initf("%s --preconfig", common_args);

    g_assert(!qmp_rsp_is_err(qtest_qmp(qs, "{ 'execute': 'query-commands' }")));

    g_assert(qmp_rsp_is_err(qtest_qmp(qs, "{ 'execute': 'query-cpus-fast' }")));

    rsp = qtest_qmp(qs, "{ 'execute': 'query-status' }");
    ret = qdict_get_qdict(rsp, "return");
    g_assert(ret);
    g_assert_cmpstr(qdict_get_try_str(ret, "status"), ==, "prelaunch");
    qobject_unref(rsp);

    g_assert(!qmp_rsp_is_err(qtest_qmp(qs, "{ 'execute': 'x-exit-preconfig' }")));
    qtest_qmp_eventwait(qs, "RESUME");

    rsp = qtest_qmp(qs, "{ 'execute': 'query-status' }");
    ret = qdict_get_qdict(rsp, "return");
    g_assert(ret);
    g_assert_cmpstr(qdict_get_try_str(ret, "status"), ==, "running");
    qobject_unref(rsp);

    g_assert(qmp_rsp_is_err(qtest_qmp(qs, "{ 'execute': 'x-exit-preconfig' }")));

    g_assert(!qmp_rsp_is_err(qtest_qmp(qs, "{ 'execute': 'query-cpus-fast' }")));

    qtest_quit(qs);
}

static void test_qmp_missing_any_arg(void)
{
    QTestState *qts;
    QDict *resp;

    qts = qtest_init(common_args);
    resp = qtest_qmp(qts, "{'execute': 'qom-set', 'arguments':"
                     " { 'path': '/machine', 'property': 'rtc-time' } }");
    g_assert_nonnull(resp);
    qmp_expect_error_and_unref(resp, "GenericError");
    qtest_quit(qts);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("qmp/protocol", test_qmp_protocol);
#ifndef _WIN32
    qtest_add_func("qmp/oob", test_qmp_oob);
#endif
    qtest_add_func("qmp/preconfig", test_qmp_preconfig);
    qtest_add_func("qmp/missing-any-arg", test_qmp_missing_any_arg);

    return g_test_run();
}
