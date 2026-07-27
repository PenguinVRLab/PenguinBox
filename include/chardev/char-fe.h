#ifndef QEMU_CHAR_FE_H
#define QEMU_CHAR_FE_H

#include "chardev/char.h"
#include "qemu/main-loop.h"

typedef void IOEventHandler(void *opaque, QEMUChrEvent event);
typedef int BackendChangeHandler(void *opaque);

struct CharFrontend {
    Chardev *chr;
    IOEventHandler *chr_event;
    IOCanReadHandler *chr_can_read;
    IOReadHandler *chr_read;
    BackendChangeHandler *chr_be_change;
    void *opaque;
    unsigned int tag;
    bool fe_is_open;
};

bool qemu_chr_fe_init(CharFrontend *c, Chardev *be, Error **errp);

void qemu_chr_fe_deinit(CharFrontend *c, bool del);

Chardev *qemu_chr_fe_get_driver(CharFrontend *c);

bool qemu_chr_fe_backend_connected(CharFrontend *c);

bool qemu_chr_fe_backend_open(CharFrontend *c);

void qemu_chr_fe_set_handlers_full(CharFrontend *c,
                                   IOCanReadHandler *fd_can_read,
                                   IOReadHandler *fd_read,
                                   IOEventHandler *fd_event,
                                   BackendChangeHandler *be_change,
                                   void *opaque,
                                   GMainContext *context,
                                   bool set_open,
                                   bool sync_state);

void qemu_chr_fe_set_handlers(CharFrontend *c,
                              IOCanReadHandler *fd_can_read,
                              IOReadHandler *fd_read,
                              IOEventHandler *fd_event,
                              BackendChangeHandler *be_change,
                              void *opaque,
                              GMainContext *context,
                              bool set_open);

void qemu_chr_fe_take_focus(CharFrontend *c);

void qemu_chr_fe_accept_input(CharFrontend *c);

void qemu_chr_fe_disconnect(CharFrontend *c);

int qemu_chr_fe_wait_connected(CharFrontend *c, Error **errp);

void qemu_chr_fe_set_echo(CharFrontend *c, bool echo);

void qemu_chr_fe_set_open(CharFrontend *c, bool is_open);

void qemu_chr_fe_printf(CharFrontend *c, const char *fmt, ...)
    G_GNUC_PRINTF(2, 3);


typedef gboolean (*FEWatchFunc)(void *do_not_use, GIOCondition condition, void *data);

guint qemu_chr_fe_add_watch(CharFrontend *c, GIOCondition cond,
                            FEWatchFunc func, void *user_data);

int qemu_chr_fe_write(CharFrontend *c, const uint8_t *buf, int len);

int qemu_chr_fe_write_all(CharFrontend *c, const uint8_t *buf, int len);

int qemu_chr_fe_read_all(CharFrontend *c, uint8_t *buf, int len);

int qemu_chr_fe_ioctl(CharFrontend *c, int cmd, void *arg);

int qemu_chr_fe_get_msgfd(CharFrontend *c);

int qemu_chr_fe_get_msgfds(CharFrontend *c, int *fds, int num);

int qemu_chr_fe_set_msgfds(CharFrontend *c, int *fds, int num);

#endif
