/*
 * Copyright (c) 2016-2018 Red Hat, Inc. and/or its affiliates
 *   based on the vhost-user-test.c that is:
 *      Copyright (c) 2014 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include "libqtest.h"
#include <qapi/qapi-types-migration.h>

#define FILE_TEST_FILENAME "migfile"
#define FILE_TEST_OFFSET 0x1000
#define FILE_TEST_MARKER 'X'

typedef enum {
    MEM_TYPE_ANON,
    MEM_TYPE_SHMEM,
    MEM_TYPE_MEMFD,
    MEM_TYPE_NUM,
} MemType;

typedef struct MigrationTestEnv {
    bool has_kvm;
    bool has_tcg;
    bool has_uffd;
    bool uffd_feature_thread_id;
    bool has_dirty_ring;
    bool is_x86;
    bool full_set;
    const char *arch;
    const char *qemu_src;
    const char *qemu_dst;
    char *tmpfs;
} MigrationTestEnv;

MigrationTestEnv *migration_get_env(void);
int migration_env_clean(MigrationTestEnv *env);

typedef void * (*TestMigrateStartHook)(QTestState *from,
                                       QTestState *to);

typedef void (*TestMigrateEndHook)(QTestState *from,
                                   QTestState *to,
                                   void *opaque);

typedef struct {
    bool hide_stderr;
    MemType mem_type;
    bool only_source;
    bool only_target;
    bool use_dirty_ring;
    const char *opts_source;
    const char *opts_target;
    bool suspend_me;
    bool oob;

    bool defer_target_connect;

    bool caps[MIGRATION_CAPABILITY__MAX];
} MigrateStart;

typedef enum PostcopyRecoveryFailStage {
    POSTCOPY_FAIL_NONE = 0,
    POSTCOPY_FAIL_CHANNEL_ESTABLISH,
    POSTCOPY_FAIL_RECOVERY,
    POSTCOPY_FAIL_MAX
} PostcopyRecoveryFailStage;

typedef struct {
    MigrateStart start;

    const char *listen_uri;

    const char *connect_uri;

    const char *connect_channels;

    const char *cpr_channel;

    TestMigrateStartHook start_hook;
    TestMigrateEndHook end_hook;

    enum {
        MIG_TEST_SUCCEED = 0,
        MIG_TEST_FAIL,
        MIG_TEST_FAIL_DEST_QUIT_ERR,
        MIG_TEST_QMP_ERROR,
    } result;

    unsigned int iterations;

    bool live;

    void *postcopy_data;
    PostcopyRecoveryFailStage postcopy_recovery_fail_stage;
} MigrateCommon;

void wait_for_serial(const char *side);
void migrate_prepare_for_dirty_mem(QTestState *from);
void migrate_wait_for_dirty_mem(QTestState *from, QTestState *to);

int migrate_args(char **from, char **to, const char *uri, MigrateStart *args);
int migrate_start(QTestState **from, QTestState **to, const char *uri,
                  MigrateStart *args);
void migrate_end(QTestState *from, QTestState *to, bool test_dest);

void test_postcopy_common(MigrateCommon *args);
void test_postcopy_recovery_common(MigrateCommon *args);
int test_precopy_common(MigrateCommon *args);
void test_file_common(MigrateCommon *args, bool stop_src);
void *migrate_hook_start_precopy_tcp_multifd_common(QTestState *from,
                                                    QTestState *to,
                                                    const char *method);

typedef struct QTestMigrationState QTestMigrationState;
QTestMigrationState *get_src(void);
QTestMigrationState *get_dst(void);

#ifdef CONFIG_GNUTLS
void migration_test_add_tls(MigrationTestEnv *env);
#else
static inline void migration_test_add_tls(MigrationTestEnv *env) {};
#endif
void migration_test_add_compression(MigrationTestEnv *env);
void migration_test_add_postcopy(MigrationTestEnv *env);
void migration_test_add_file(MigrationTestEnv *env);
void migration_test_add_precopy(MigrationTestEnv *env);
void migration_test_add_cpr(MigrationTestEnv *env);
void migration_test_add_misc(MigrationTestEnv *env);

#endif
