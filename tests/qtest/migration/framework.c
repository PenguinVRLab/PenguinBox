/*
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
#include "qapi/error.h"
#include "qobject/qjson.h"
#include "qobject/qlist.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/range.h"
#include "qemu/sockets.h"


#define QEMU_VM_FILE_MAGIC 0x5145564d
#define QEMU_ENV_SRC "QTEST_QEMU_BINARY_SRC"
#define QEMU_ENV_DST "QTEST_QEMU_BINARY_DST"
#define MULTIFD_TEST_CHANNELS 4

unsigned start_address;
unsigned end_address;
static QTestMigrationState src_state;
static QTestMigrationState dst_state;
static char *tmpfs;

#define MAGIC_OFFSET_BASE (3 * 1024 * 1024)
#define MAGIC_OFFSET_SHUFFLE 1024
#define MAGIC_OFFSET (MAGIC_OFFSET_BASE + MAGIC_OFFSET_SHUFFLE)
#define MAGIC_MARKER 0xFEED12345678CAFEULL


void wait_for_serial(const char *side)
{
    g_autofree char *serialpath = g_strdup_printf("%s/%s", tmpfs, side);
    FILE *serialfile = fopen(serialpath, "r");

    do {
        int readvalue = fgetc(serialfile);

        switch (readvalue) {
        case 'A':
            break;

        case 'B':
            fclose(serialfile);
            return;

        case EOF:
            fseek(serialfile, 0, SEEK_SET);
            usleep(1000);
            break;

        default:
            fprintf(stderr, "Unexpected %d on %s serial\n", readvalue, side);
            g_assert_not_reached();
        }
    } while (true);
}

void migrate_prepare_for_dirty_mem(QTestState *from)
{
    qtest_writeq(from, start_address + MAGIC_OFFSET, MAGIC_MARKER);
}

void migrate_wait_for_dirty_mem(QTestState *from, QTestState *to)
{
    uint64_t watch_address = start_address + MAGIC_OFFSET_BASE;
    uint64_t marker_address = start_address + MAGIC_OFFSET;
    uint8_t watch_byte;

    do {
        usleep(1000 * 10);
    } while (qtest_readq(to, marker_address) != MAGIC_MARKER);


    if (src_state.suspend_me) {
        return;
    }

    watch_byte = qtest_readb(from, watch_address);
    do {
        usleep(1000 * 10);
    } while (qtest_readb(from, watch_address) == watch_byte);
}

static void check_guests_ram(QTestState *who)
{
    unsigned address;
    uint8_t first_byte;
    uint8_t last_byte;
    bool hit_edge = false;
    int bad = 0;

    qtest_memread(who, start_address, &first_byte, 1);
    last_byte = first_byte;

    for (address = start_address + TEST_MEM_PAGE_SIZE; address < end_address;
         address += TEST_MEM_PAGE_SIZE)
    {
        uint8_t b;
        qtest_memread(who, address, &b, 1);
        if (b != last_byte) {
            if (((b + 1) % 256) == last_byte && !hit_edge) {
                hit_edge = true;
                last_byte = b;
            } else {
                bad++;
                if (bad <= 10) {
                    fprintf(stderr, "Memory content inconsistency at %x"
                            " first_byte = %x last_byte = %x current = %x"
                            " hit_edge = %x\n",
                            address, first_byte, last_byte, b, hit_edge);
                }
            }
        }
    }
    if (bad >= 10) {
        fprintf(stderr, "and in another %d pages", bad - 10);
    }
    g_assert(bad == 0);
}

static void cleanup(const char *filename)
{
    g_autofree char *path = g_strdup_printf("%s/%s", tmpfs, filename);

    unlink(path);
}

static QList *migrate_start_get_qmp_capabilities(const MigrateStart *args)
{
    QList *capabilities = NULL;

    if (args->oob) {
        capabilities = qlist_new();
        qlist_append_str(capabilities, "oob");
    }
    return capabilities;
}

static void migrate_start_set_capabilities(QTestState *from, QTestState *to,
                                           MigrateStart *args)
{
    for (uint8_t i = 0; i < MIGRATION_CAPABILITY__MAX; i++) {
        if (!args->caps[i]) {
            continue;
        }
        if (from) {
            migrate_set_capability(from,
                            MigrationCapability_lookup.array[i], true);
        }
        if (to) {
            migrate_set_capability(to,
                            MigrationCapability_lookup.array[i], true);
        }
    }

    migrate_set_capability(from, "events", true);
    if (!args->defer_target_connect && to) {
        migrate_set_capability(to, "events", true);
    }

    if (args->caps[MIGRATION_CAPABILITY_MULTIFD]) {
        migrate_set_parameter_int(from, "multifd-channels",
                                  MULTIFD_TEST_CHANNELS);
        if (to) {
            migrate_set_parameter_int(to, "multifd-channels",
                                      MULTIFD_TEST_CHANNELS);
        }
    }

    return;
}

static char *test_shmem_path(void)
{
    return g_strdup_printf("/dev/shm/qemu-%d", getpid());
}

#define  MIG_MEM_ID  "mig.mem"

static char *migrate_mem_type_get_opts(MemType type, const char *memory_size)
{
    g_autofree char *shmem_path = NULL;
    g_autofree char *backend = NULL;
    bool share = true;
    char *opts;

    switch (type) {
    case MEM_TYPE_ANON:
        backend = g_strdup("-object memory-backend-ram");
        share = false;
        break;
    case MEM_TYPE_SHMEM:
        shmem_path = test_shmem_path();
        backend = g_strdup_printf("-object memory-backend-file,mem-path=%s",
                                  shmem_path);
        break;
    case MEM_TYPE_MEMFD:
        backend = g_strdup("-object memory-backend-memfd");
        break;
    default:
        g_assert_not_reached();
        break;
    }

    opts = g_strdup_printf("%s,id=%s,size=%s,share=%s",
                           backend, MIG_MEM_ID, memory_size,
                           share ? "on" : "off");

    return opts;
}

int migrate_args(char **from, char **to, const char *uri, MigrateStart *args)
{
    g_autofree gchar *arch_opts = NULL;
    gchar *cmd_source = NULL;
    gchar *cmd_target = NULL;
    const gchar *ignore_stderr;
    g_autofree char *mem_object = NULL;
    const char *kvm_opts = NULL;
    const char *arch = qtest_get_arch();
    const char *memory_size;
    const char *machine_alias, *machine_opts = "";
    g_autofree char *machine = NULL;
    const char *bootpath = bootfile_get();
    g_autofree char *memory_backend = NULL;
    const char *events;

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        memory_size = "150M";

        if (g_str_equal(arch, "i386")) {
            machine_alias = "pc";
        } else {
            machine_alias = "q35";
        }
        arch_opts = g_strdup_printf(
            "-drive if=none,id=d0,file=%s,format=raw "
            "-device ide-hd,drive=d0,secs=1,cyls=1,heads=1", bootpath);
        start_address = X86_TEST_MEM_START;
        end_address = X86_TEST_MEM_END;
    } else if (g_str_equal(arch, "s390x")) {
        memory_size = "128M";
        machine_alias = "s390-ccw-virtio";
        arch_opts = g_strdup_printf("-bios %s", bootpath);
        start_address = S390_TEST_MEM_START;
        end_address = S390_TEST_MEM_END;
    } else if (strcmp(arch, "ppc64") == 0) {
        memory_size = "256M";
        start_address = PPC_TEST_MEM_START;
        end_address = PPC_TEST_MEM_END;
        machine_alias = "pseries";
        machine_opts = "vsmt=8";
        arch_opts = g_strdup_printf(
            "-nodefaults -machine " PSERIES_DEFAULT_CAPABILITIES " "
            "-bios %s", bootpath);
    } else if (strcmp(arch, "aarch64") == 0) {
        memory_size = "150M";
        machine_alias = "virt";
        machine_opts = "gic-version=3";
        arch_opts = g_strdup_printf("-cpu max -kernel %s", bootpath);
        start_address = ARM_TEST_MEM_START;
        end_address = ARM_TEST_MEM_END;
    } else {
        g_assert_not_reached();
    }

    if (!getenv("QTEST_LOG") && args->hide_stderr) {
#ifndef _WIN32
        ignore_stderr = "2>/dev/null";
#else
        ignore_stderr = "";
#endif
    } else {
        ignore_stderr = "";
    }

    mem_object = migrate_mem_type_get_opts(args->mem_type, memory_size);
    memory_backend = g_strdup_printf("-machine memory-backend=%s %s",
                                     MIG_MEM_ID, mem_object);

    if (args->use_dirty_ring) {
        kvm_opts = ",dirty-ring-size=4096";
    }

    if (!qtest_has_machine(machine_alias)) {
        g_autofree char *msg = g_strdup_printf("machine %s not supported", machine_alias);
        g_test_skip(msg);
        return -1;
    }

    machine = resolve_machine_version(machine_alias, QEMU_ENV_SRC,
                                      QEMU_ENV_DST);

    g_test_message("Using machine type: %s", machine);

    cmd_source = g_strdup_printf("-accel kvm%s -accel tcg "
                                 "-machine %s,%s "
                                 "-name source,debug-threads=on "
                                 "%s "
                                 "-serial file:%s/src_serial "
                                 "%s %s %s",
                                 kvm_opts ? kvm_opts : "",
                                 machine, machine_opts,
                                 memory_backend, tmpfs,
                                 arch_opts ? arch_opts : "",
                                 args->opts_source ? args->opts_source : "",
                                 ignore_stderr);

    events = args->defer_target_connect ? "-global migration.x-events=on" : "";

    cmd_target = g_strdup_printf("-accel kvm%s -accel tcg "
                                 "-machine %s,%s "
                                 "-name target,debug-threads=on "
                                 "%s "
                                 "-serial file:%s/dest_serial "
                                 "-incoming %s "
                                 "%s %s %s %s",
                                 kvm_opts ? kvm_opts : "",
                                 machine, machine_opts,
                                 memory_backend, tmpfs, uri,
                                 events,
                                 arch_opts ? arch_opts : "",
                                 args->opts_target ? args->opts_target : "",
                                 ignore_stderr);

    *from = cmd_source;
    *to = cmd_target;
    return 0;
}

static bool migrate_mem_type_prepare(MemType type)
{
    switch (type) {
    case MEM_TYPE_SHMEM:
        if (!g_file_test("/dev/shm", G_FILE_TEST_IS_DIR)) {
            g_test_skip("/dev/shm is not supported");
            return false;
        }
        break;
    default:
        break;
    }

    return true;
}

static void migrate_mem_type_cleanup(MemType type)
{
    g_autofree char *shmem_path = NULL;

    switch (type) {
    case MEM_TYPE_SHMEM:

        shmem_path = test_shmem_path();
        unlink(shmem_path);
        break;
    default:
        break;
    }
}

int migrate_start(QTestState **from, QTestState **to, const char *uri,
                  MigrateStart *args)
{
    g_autofree gchar *cmd_source = NULL;
    g_autofree gchar *cmd_target = NULL;
    g_autoptr(QList) capabilities = migrate_start_get_qmp_capabilities(args);

    if (!migrate_mem_type_prepare(args->mem_type)) {
        return -1;
    }

    dst_state = (QTestMigrationState) { };
    src_state = (QTestMigrationState) { };
    bootfile_create(qtest_get_arch(), tmpfs, args->suspend_me);
    src_state.suspend_me = args->suspend_me;

    if (migrate_args(&cmd_source, &cmd_target, uri, args)) {
        return -1;
    }

    if (!args->only_target) {
        *from = qtest_init_ext(QEMU_ENV_SRC, cmd_source, capabilities, true);
        qtest_qmp_set_event_callback(*from,
                                     migrate_watch_for_events,
                                     &src_state);
    }

    if (!args->only_source) {
        *to = qtest_init_ext(QEMU_ENV_DST, cmd_target, capabilities,
                             !args->defer_target_connect);
        qtest_qmp_set_event_callback(*to,
                                     migrate_watch_for_events,
                                     &dst_state);
    }

    migrate_mem_type_cleanup(args->mem_type);
    migrate_start_set_capabilities(*from,
                                   args->only_source ? NULL : *to,
                                   args);

    return 0;
}

void migrate_end(QTestState *from, QTestState *to, bool test_dest)
{
    unsigned char dest_byte_a, dest_byte_b, dest_byte_c, dest_byte_d;

    qtest_quit(from);

    if (test_dest) {
        qtest_memread(to, start_address, &dest_byte_a, 1);

        do {
            qtest_memread(to, start_address, &dest_byte_b, 1);
            usleep(1000 * 10);
        } while (dest_byte_a == dest_byte_b);

        qtest_qmp_assert_success(to, "{ 'execute' : 'stop'}");

        qtest_memread(to, start_address, &dest_byte_c, 1);
        usleep(1000 * 200);
        qtest_memread(to, start_address, &dest_byte_d, 1);
        g_assert_cmpint(dest_byte_c, ==, dest_byte_d);

        check_guests_ram(to);
    }

    qtest_quit(to);

    cleanup("migsocket");
    cleanup("cpr.sock");
    cleanup("src_serial");
    cleanup("dest_serial");
    cleanup(FILE_TEST_FILENAME);
}

static int migrate_postcopy_prepare(QTestState **from_ptr,
                                    QTestState **to_ptr,
                                    MigrateCommon *args)
{
    QTestState *from, *to;

    args->start.caps[MIGRATION_CAPABILITY_POSTCOPY_BLOCKTIME] = true;
    args->start.caps[MIGRATION_CAPABILITY_POSTCOPY_RAM] = true;

    if (migrate_start(&from, &to, "defer", &args->start)) {
        return -1;
    }

    if (args->start_hook) {
        args->postcopy_data = args->start_hook(from, to);
    }

    migrate_ensure_non_converge(from);
    migrate_prepare_for_dirty_mem(from);
    qtest_qmp_assert_success(to, "{ 'execute': 'migrate-incoming',"
                             "  'arguments': { "
                             "      'channels': [ { 'channel-type': 'main',"
                             "      'addr': { 'transport': 'socket',"
                             "                'type': 'inet',"
                             "                'host': '127.0.0.1',"
                             "                'port': '0' } } ] } }");

    wait_for_serial("src_serial");
    wait_for_suspend(from, &src_state);

    migrate_qmp(from, to, NULL, NULL, "{}");

    migrate_wait_for_dirty_mem(from, to);

    *from_ptr = from;
    *to_ptr = to;

    return 0;
}

static void migrate_postcopy_complete(QTestState *from, QTestState *to,
                                      MigrateCommon *args)
{
    MigrationTestEnv *env = migration_get_env();

    wait_for_migration_complete(from);

    if (args->start.suspend_me) {
        qtest_qmp_assert_success(to, "{'execute': 'system_wakeup'}");
    }

    wait_for_serial("dest_serial");

    if (env->uffd_feature_thread_id) {
        read_blocktime(to);
    }

    if (args->end_hook) {
        args->end_hook(from, to, args->postcopy_data);
        args->postcopy_data = NULL;
    }

    migrate_end(from, to, true);
}

void test_postcopy_common(MigrateCommon *args)
{
    QTestState *from, *to;

    if (migrate_postcopy_prepare(&from, &to, args)) {
        return;
    }
    migrate_postcopy_start(from, to, &src_state);
    migrate_postcopy_complete(from, to, args);
}

static void wait_for_postcopy_status(QTestState *one, const char *status)
{
    wait_for_migration_status(one, status,
                              (const char * []) {
                                  "failed", "active",
                                  "completed", NULL
                              });
}

static void postcopy_recover_fail(QTestState *from, QTestState *to,
                                  PostcopyRecoveryFailStage stage)
{
#ifndef _WIN32
    bool fail_early = (stage == POSTCOPY_FAIL_CHANNEL_ESTABLISH);
    int ret, pair1[2], pair2[2];
    char c;

    g_assert(stage > POSTCOPY_FAIL_NONE && stage < POSTCOPY_FAIL_MAX);

    ret = qemu_socketpair(PF_LOCAL, SOCK_STREAM, 0, pair1);
    g_assert_cmpint(ret, ==, 0);

    ret = qemu_socketpair(PF_LOCAL, SOCK_STREAM, 0, pair2);
    g_assert_cmpint(ret, ==, 0);

    qtest_qmp_fds_assert_success(from, &pair1[0], 1,
                                 "{ 'execute': 'getfd',"
                                 "  'arguments': { 'fdname': 'fd-mig' }}");
    qtest_qmp_fds_assert_success(to, &pair2[0], 1,
                                 "{ 'execute': 'getfd',"
                                 "  'arguments': { 'fdname': 'fd-mig' }}");

#define QEMU_VM_COMMAND              0x08
    c = QEMU_VM_COMMAND;
    ret = send(pair2[1], &c, 1, 0);
    g_assert_cmpint(ret, ==, 1);

    if (stage == POSTCOPY_FAIL_CHANNEL_ESTABLISH) {
        close(pair1[1]);
    }

    migrate_recover(to, "fd:fd-mig");
    migrate_qmp(from, to, "fd:fd-mig", NULL, "{'resume': true}");

    migration_event_wait(from, "postcopy-recover-setup");

    if (fail_early) {
        migration_event_wait(from, "postcopy-paused");
    } else {
        migration_event_wait(from, "postcopy-recover");
        wait_for_postcopy_status(from, "postcopy-recover");

        migrate_pause(from);
    }

    wait_for_postcopy_status(from, "postcopy-paused");
    wait_for_postcopy_status(to, "postcopy-recover");

    migrate_pause(to);
    wait_for_postcopy_status(to, "postcopy-paused");

    close(pair1[0]);
    close(pair2[0]);
    close(pair2[1]);

    if (stage != POSTCOPY_FAIL_CHANNEL_ESTABLISH) {
        close(pair1[1]);
    }
#endif
}

void test_postcopy_recovery_common(MigrateCommon *args)
{
    QTestState *from, *to;
    g_autofree char *uri = NULL;

    args->start.oob = true;

    args->start.hide_stderr = true;

    if (migrate_postcopy_prepare(&from, &to, args)) {
        return;
    }

    migrate_set_parameter_int(from, "max-postcopy-bandwidth", 4096);

    migrate_postcopy_start(from, to, &src_state);

    wait_for_migration_status(from, "postcopy-active", NULL);

    migrate_pause(from);

    wait_for_postcopy_status(to, "postcopy-paused");
    wait_for_postcopy_status(from, "postcopy-paused");

    if (args->postcopy_recovery_fail_stage) {
        postcopy_recover_fail(from, to, args->postcopy_recovery_fail_stage);
    }

    uri = g_strdup_printf("unix:%s/migsocket-recover", tmpfs);
    migrate_recover(to, uri);

    migrate_qmp(from, to, uri, NULL, "{'resume': true}");

    migrate_set_parameter_int(from, "max-postcopy-bandwidth", 0);

    migrate_postcopy_complete(from, to, args);
}

int test_precopy_common(MigrateCommon *args)
{
    QTestState *from, *to;
    void *data_hook = NULL;
    QObject *in_channels = NULL;
    QObject *out_channels = NULL;

    g_assert(!args->cpr_channel || args->connect_channels);

    if (migrate_start(&from, &to, args->listen_uri, &args->start)) {
        return -1;
    }

    if (args->start_hook) {
        data_hook = args->start_hook(from, to);
    }

    if (args->result == MIG_TEST_SUCCEED) {
        wait_for_serial("src_serial");
        wait_for_suspend(from, &src_state);
    }

    if (args->live) {
        migrate_ensure_non_converge(from);
        migrate_prepare_for_dirty_mem(from);
    } else {
        if (args->result == MIG_TEST_SUCCEED) {
            qtest_qmp_assert_success(from, "{ 'execute' : 'stop'}");
            wait_for_stop(from, &src_state);
            migrate_ensure_converge(from);
        }
    }

    if (args->connect_channels) {
        if (args->start.defer_target_connect &&
            !strcmp(args->listen_uri, "defer")) {
            in_channels = qobject_from_json(args->connect_channels,
                                            &error_abort);
        }
        out_channels = qobject_from_json(args->connect_channels, &error_abort);

        if (args->cpr_channel) {
            QList *channels_list = qobject_to(QList, out_channels);
            QObject *obj = migrate_str_to_channel(args->cpr_channel);

            qlist_append(channels_list, obj);
        }
    }

    if (args->result == MIG_TEST_QMP_ERROR) {
        migrate_qmp_fail(from, args->connect_uri, out_channels, "{}");
        goto finish;
    }

    migrate_qmp(from, to, args->connect_uri, out_channels, "{}");

    if (args->start.defer_target_connect) {
        qtest_connect(to);
        qtest_qmp_handshake(to, NULL);
        if (!strcmp(args->listen_uri, "defer")) {
            migrate_incoming_qmp(to, args->connect_uri, in_channels, "{}");
        }
    }

    if (args->result != MIG_TEST_SUCCEED) {
        bool allow_active = args->result == MIG_TEST_FAIL;
        wait_for_migration_fail(from, allow_active);

        if (args->result == MIG_TEST_FAIL_DEST_QUIT_ERR) {
            qtest_set_expected_status(to, EXIT_FAILURE);
        }
    } else {
        if (args->live) {
            while (args->iterations > 1) {
                wait_for_migration_pass(from, &src_state);
                args->iterations--;
            }
            migrate_wait_for_dirty_mem(from, to);

            migrate_ensure_converge(from);

            wait_for_migration_complete(from);

            wait_for_stop(from, &src_state);

        } else {
            wait_for_migration_complete(from);
            wait_for_migration_complete(to);

            qtest_qmp_assert_success(to, "{ 'execute' : 'cont'}");
        }

        wait_for_resume(to, &dst_state);

        if (args->start.suspend_me) {
            qtest_qmp_assert_success(to, "{'execute': 'system_wakeup'}");
        }

        wait_for_serial("dest_serial");
    }

finish:
    if (args->end_hook) {
        args->end_hook(from, to, data_hook);
    }

    migrate_end(from, to, args->result == MIG_TEST_SUCCEED);

    return 0;
}

static void file_dirty_offset_region(void)
{
    g_autofree char *path = g_strdup_printf("%s/%s", tmpfs, FILE_TEST_FILENAME);
    size_t size = FILE_TEST_OFFSET;
    g_autofree char *data = g_new0(char, size);

    memset(data, FILE_TEST_MARKER, size);
    g_assert(g_file_set_contents(path, data, size, NULL));
}

static void file_check_offset_region(void)
{
    g_autofree char *path = g_strdup_printf("%s/%s", tmpfs, FILE_TEST_FILENAME);
    size_t size = FILE_TEST_OFFSET;
    g_autofree char *expected = g_new0(char, size);
    g_autofree char *actual = NULL;
    uint64_t *stream_start;

    memset(expected, FILE_TEST_MARKER, size);

    g_assert(g_file_get_contents(path, &actual, NULL, NULL));
    g_assert(!memcmp(actual, expected, size));

    stream_start = (uint64_t *)(actual + size);
    g_assert_cmpint(cpu_to_be64(*stream_start) >> 32, ==, QEMU_VM_FILE_MAGIC);
}

void test_file_common(MigrateCommon *args, bool stop_src)
{
    QTestState *from, *to;
    void *data_hook = NULL;
    bool check_offset = false;

    if (migrate_start(&from, &to, args->listen_uri, &args->start)) {
        return;
    }

    g_assert_false(args->live);

    if (g_strrstr(args->connect_uri, "offset=")) {
        check_offset = true;
        file_dirty_offset_region();
    }

    if (args->start_hook) {
        data_hook = args->start_hook(from, to);
    }

    migrate_ensure_converge(from);
    wait_for_serial("src_serial");

    if (stop_src) {
        qtest_qmp_assert_success(from, "{ 'execute' : 'stop'}");
        wait_for_stop(from, &src_state);
    }

    if (args->result == MIG_TEST_QMP_ERROR) {
        migrate_qmp_fail(from, args->connect_uri, NULL, "{}");
        goto finish;
    }

    migrate_qmp(from, to, args->connect_uri, NULL, "{}");
    wait_for_migration_complete(from);

    migrate_incoming_qmp(to, args->connect_uri, NULL, "{}");
    wait_for_migration_complete(to);

    if (stop_src) {
        qtest_qmp_assert_success(to, "{ 'execute' : 'cont'}");
    }
    wait_for_resume(to, &dst_state);

    wait_for_serial("dest_serial");

    if (check_offset) {
        file_check_offset_region();
    }

finish:
    if (args->end_hook) {
        args->end_hook(from, to, data_hook);
    }

    migrate_end(from, to, args->result == MIG_TEST_SUCCEED);
}

void *migrate_hook_start_precopy_tcp_multifd_common(QTestState *from,
                                                    QTestState *to,
                                                    const char *method)
{
    migrate_set_parameter_str(from, "multifd-compression", method);
    migrate_set_parameter_str(to, "multifd-compression", method);

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");

    return NULL;
}

QTestMigrationState *get_src(void)
{
    return &src_state;
}

QTestMigrationState *get_dst(void)
{
    return &dst_state;
}

MigrationTestEnv *migration_get_env(void)
{
    static MigrationTestEnv *env;
    g_autoptr(GError) err = NULL;

    if (env) {
        return env;
    }

    env = g_new0(MigrationTestEnv, 1);
    env->qemu_src = getenv(QEMU_ENV_SRC);
    env->qemu_dst = getenv(QEMU_ENV_DST);

    if (env->qemu_src && env->qemu_dst) {
        g_test_message("Only one of %s, %s is allowed",
                       QEMU_ENV_SRC, QEMU_ENV_DST);
        exit(1);
    }

    env->has_kvm = qtest_has_accel("kvm");
    env->has_tcg = qtest_has_accel("tcg");

    if (!env->has_tcg && !env->has_kvm) {
        g_test_skip("No KVM or TCG accelerator available");
        return env;
    }

    env->has_dirty_ring = kvm_dirty_ring_supported();
    env->has_uffd = ufd_version_check(&env->uffd_feature_thread_id);
    env->arch = qtest_get_arch();
    env->is_x86 = !strcmp(env->arch, "i386") || !strcmp(env->arch, "x86_64");

    env->tmpfs = g_dir_make_tmp("migration-test-XXXXXX", &err);
    if (!env->tmpfs) {
        g_test_message("Can't create temporary directory in %s: %s",
                       g_get_tmp_dir(), err->message);
    }
    g_assert(env->tmpfs);

    tmpfs = env->tmpfs;

    return env;
}

int migration_env_clean(MigrationTestEnv *env)
{
    char *tmpfs;
    int ret = 0;

    if (!env) {
        return ret;
    }

    bootfile_delete();

    tmpfs = env->tmpfs;
    ret = rmdir(tmpfs);
    if (ret != 0) {
        g_test_message("unable to rmdir: path (%s): %s",
                       tmpfs, strerror(errno));
    }
    g_free(tmpfs);

    return ret;
}
