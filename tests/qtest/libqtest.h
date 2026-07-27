/*
 * QTest
 *
 * Copyright IBM, Corp. 2012
 * Copyright Red Hat, Inc. 2012
 * Copyright SUSE LINUX Products GmbH 2013
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Paolo Bonzini     <pbonzini@redhat.com>
 *  Andreas Färber    <afaerber@suse.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#ifndef LIBQTEST_H
#define LIBQTEST_H

#include "qobject/qobject.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"
#include "libqmp.h"

typedef struct QTestState QTestState;

QTestState *qtest_initf(const char *fmt, ...) G_GNUC_PRINTF(1, 2);

QTestState *qtest_vinitf(const char *fmt, va_list ap) G_GNUC_PRINTF(1, 0);

const char *qtest_qemu_binary(const char *var);

QTestState *qtest_init_after_exec(QTestState *qts);

gchar *qtest_qemu_args(const char *extra_args);

QTestState *qtest_init(const char *extra_args);

QTestState *qtest_init_ext(const char *var, const char *extra_args,
                           QList *capabilities, bool do_connect);

QTestState *qtest_init_without_qmp_handshake(const char *extra_args);

void qtest_connect(QTestState *s);

void qtest_qmp_handshake(QTestState *s, QList *capabilities);

QTestState *qtest_init_with_serial(const char *extra_args, int *sock_fd);

void qtest_system_reset(QTestState *s);

void qtest_system_reset_nowait(QTestState *s);

void qtest_wait_qemu(QTestState *s);

void qtest_kill_qemu(QTestState *s);

void qtest_quit(QTestState *s);

#ifndef _WIN32
QDict *qtest_qmp_fds(QTestState *s, int *fds, size_t fds_num,
                     const char *fmt, ...)
    G_GNUC_PRINTF(4, 5);
#endif

QDict *qtest_qmp(QTestState *s, const char *fmt, ...)
    G_GNUC_PRINTF(2, 3);

void qtest_qmp_send(QTestState *s, const char *fmt, ...)
    G_GNUC_PRINTF(2, 3);

void qtest_qmp_send_raw(QTestState *s, const char *fmt, ...)
    G_GNUC_PRINTF(2, 3);

int qtest_socket_server(const char *socket_path);

#ifndef _WIN32
QDict *qtest_vqmp_fds(QTestState *s, int *fds, size_t fds_num,
                      const char *fmt, va_list ap)
    G_GNUC_PRINTF(4, 0);
#endif

QDict *qtest_vqmp(QTestState *s, const char *fmt, va_list ap)
    G_GNUC_PRINTF(2, 0);

#ifndef _WIN32
void qtest_qmp_vsend_fds(QTestState *s, int *fds, size_t fds_num,
                         const char *fmt, va_list ap)
    G_GNUC_PRINTF(4, 0);
#endif

void qtest_qmp_vsend(QTestState *s, const char *fmt, va_list ap)
    G_GNUC_PRINTF(2, 0);

QDict *qtest_qmp_receive_dict(QTestState *s);

QDict *qtest_qmp_receive(QTestState *s);

typedef bool (*QTestQMPEventCallback)(QTestState *s, const char *name,
                                      QDict *event, void *opaque);

void qtest_qmp_set_event_callback(QTestState *s,
                                  QTestQMPEventCallback cb, void *opaque);

void qtest_qmp_eventwait(QTestState *s, const char *event);

QDict *qtest_qmp_eventwait_ref(QTestState *s, const char *event);

QDict *qtest_qmp_event_ref(QTestState *s, const char *event);

char *qtest_hmp(QTestState *s, const char *fmt, ...) G_GNUC_PRINTF(2, 3);

char *qtest_vhmp(QTestState *s, const char *fmt, va_list ap)
    G_GNUC_PRINTF(2, 0);

void qtest_module_load(QTestState *s, const char *prefix, const char *libname);

bool qtest_get_irq(QTestState *s, int num);

void qtest_irq_intercept_in(QTestState *s, const char *string);

void qtest_irq_intercept_out(QTestState *s, const char *string);

void qtest_irq_intercept_out_named(QTestState *s, const char *qom_path, const char *name);

void qtest_set_irq_in(QTestState *s, const char *string, const char *name,
                      int irq, int level);

void qtest_outb(QTestState *s, uint16_t addr, uint8_t value);

void qtest_outw(QTestState *s, uint16_t addr, uint16_t value);

void qtest_outl(QTestState *s, uint16_t addr, uint32_t value);

uint8_t qtest_inb(QTestState *s, uint16_t addr);

uint16_t qtest_inw(QTestState *s, uint16_t addr);

uint32_t qtest_inl(QTestState *s, uint16_t addr);

void qtest_writeb(QTestState *s, uint64_t addr, uint8_t value);

void qtest_writew(QTestState *s, uint64_t addr, uint16_t value);

void qtest_writel(QTestState *s, uint64_t addr, uint32_t value);

void qtest_writeq(QTestState *s, uint64_t addr, uint64_t value);

uint8_t qtest_readb(QTestState *s, uint64_t addr);

uint16_t qtest_readw(QTestState *s, uint64_t addr);

uint32_t qtest_readl(QTestState *s, uint64_t addr);

uint64_t qtest_readq(QTestState *s, uint64_t addr);

void qtest_memread(QTestState *s, uint64_t addr, void *data, size_t size);

uint64_t qtest_rtas_call(QTestState *s, const char *name,
                         uint32_t nargs, uint64_t args,
                         uint32_t nret, uint64_t ret);

uint64_t qtest_csr_call(QTestState *s, const char *name,
                         uint64_t cpu, int csr,
                         uint64_t *val);

void qtest_bufread(QTestState *s, uint64_t addr, void *data, size_t size);

void qtest_memwrite(QTestState *s, uint64_t addr, const void *data, size_t size);

void qtest_bufwrite(QTestState *s, uint64_t addr,
                    const void *data, size_t size);

void qtest_memset(QTestState *s, uint64_t addr, uint8_t patt, size_t size);

int64_t qtest_clock_step_next(QTestState *s);

int64_t qtest_clock_step(QTestState *s, int64_t step);

int64_t qtest_clock_set(QTestState *s, int64_t val);

bool qtest_big_endian(QTestState *s);

const char *qtest_get_arch(void);

bool qtest_has_accel(const char *accel_name);

void qtest_add_func(const char *str, void (*fn)(void));

void qtest_add_data_func(const char *str, const void *data,
                         void (*fn)(const void *));

void qtest_add_data_func_full(const char *str, void *data,
                              void (*fn)(const void *),
                              GDestroyNotify data_free_func);

#define qtest_add(testpath, Fixture, tdata, fsetup, ftest, fteardown) \
    do { \
        char *path = g_strdup_printf("/%s/%s", qtest_get_arch(), testpath); \
        g_test_add(path, Fixture, tdata, fsetup, ftest, fteardown); \
        g_free(path); \
    } while (0)

void qtest_add_abrt_handler(GHookFunc fn, const void *data);

void qtest_remove_abrt_handler(void *data);

QDict *qtest_vqmp_assert_success_ref(QTestState *qts,
                                     const char *fmt, va_list args)
    G_GNUC_PRINTF(2, 0);

void qtest_vqmp_assert_success(QTestState *qts,
                               const char *fmt, va_list args)
    G_GNUC_PRINTF(2, 0);

#ifndef _WIN32
QDict *qtest_vqmp_fds_assert_success_ref(QTestState *qts, int *fds, size_t nfds,
                                         const char *fmt, va_list args)
    G_GNUC_PRINTF(4, 0);

void qtest_vqmp_fds_assert_success(QTestState *qts, int *fds, size_t nfds,
                                   const char *fmt, va_list args)
    G_GNUC_PRINTF(4, 0);
#endif

QDict *qtest_qmp_assert_failure_ref(QTestState *qts, const char *fmt, ...)
    G_GNUC_PRINTF(2, 3);

QDict *qtest_vqmp_assert_failure_ref(QTestState *qts,
                                     const char *fmt, va_list args)
    G_GNUC_PRINTF(2, 0);

QDict *qtest_qmp_assert_success_ref(QTestState *qts, const char *fmt, ...)
    G_GNUC_PRINTF(2, 3);

void qtest_qmp_assert_success(QTestState *qts, const char *fmt, ...)
    G_GNUC_PRINTF(2, 3);

#ifndef _WIN32
QDict *qtest_qmp_fds_assert_success_ref(QTestState *qts, int *fds, size_t nfds,
                                        const char *fmt, ...)
    G_GNUC_PRINTF(4, 5);

void qtest_qmp_fds_assert_success(QTestState *qts, int *fds, size_t nfds,
                                  const char *fmt, ...)
    G_GNUC_PRINTF(4, 5);
#endif

void qtest_cb_for_every_machine(void (*cb)(const char *machine),
                                bool skip_old_versioned);

char *qtest_resolve_machine_alias(const char *var, const char *alias);

bool qtest_has_machine(const char *machine);

bool qtest_has_machine_with_env(const char *var, const char *machine);

bool qtest_has_cpu_model(const char *cpu);

bool qtest_has_device(const char *device);

void qtest_qmp_device_add_qdict(QTestState *qts, const char *drv,
                                const QDict *arguments);

void qtest_qmp_device_add(QTestState *qts, const char *driver, const char *id,
                          const char *fmt, ...) G_GNUC_PRINTF(4, 5);

void qtest_qmp_add_client(QTestState *qts, const char *protocol, int fd);

void qtest_qmp_device_del_send(QTestState *qts, const char *id);

void qtest_qmp_device_del(QTestState *qts, const char *id);

bool qtest_probe_child(QTestState *s);

void qtest_set_expected_status(QTestState *s, int status);

QTestState *qtest_inproc_init(QTestState **s, bool log, const char* arch,
                    void (*send)(void*, const char*));

void qtest_client_inproc_recv(void *opaque, const char *str);

void qtest_qom_set_bool(QTestState *s, const char *path, const char *property,
                         bool value);

bool qtest_qom_get_bool(QTestState *s, const char *path, const char *property);

pid_t qtest_pid(QTestState *s);

bool have_qemu_img(void);

bool mkimg(const char *file, const char *fmt, unsigned size_mb);

#endif
