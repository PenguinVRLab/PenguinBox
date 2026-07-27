/*
 * QTest test cases for virtio balloon device
 *
 * Copyright (c) 2024 Gao Shiyuan <gaoshiyuan@baidu.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "standard-headers/linux/virtio_balloon.h"

static void oss_fuzz_71649(void)
{
    QTestState *s = qtest_init("-device virtio-balloon -machine q35"
                               " -nodefaults");

    qtest_outl(s, 0xcf8, 0x80000890);
    qtest_outl(s, 0xcfc, 0x2);
    qtest_outl(s, 0xcf8, 0x80000891);
    qtest_inl(s, 0xcfc);
    qtest_quit(s);
}

static void query_stats(void)
{
    QTestState *s = qtest_init("-device virtio-balloon,id=balloon"
                               " -nodefaults");
    QDict *ret = qtest_qmp_assert_success_ref(
        s,
        "{ 'execute': 'qom-get', 'arguments': "     \
        "{ 'path': '/machine/peripheral/balloon', " \
        "  'property': 'guest-stats' } }");
    QDict *stats = qdict_get_qdict(ret, "stats");

    assert(qdict_size(stats) == VIRTIO_BALLOON_S_NR);

    qobject_unref(ret);
    qtest_quit(s);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("virtio-balloon/oss_fuzz_71649", oss_fuzz_71649);
    qtest_add_func("virtio-balloon/query-stats", query_stats);

    return g_test_run();
}

