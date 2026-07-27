#ifndef QEMU_DATADIR_H
#define QEMU_DATADIR_H

typedef enum {
    QEMU_FILE_TYPE_BIOS,
    QEMU_FILE_TYPE_DTB,
    QEMU_FILE_TYPE_KEYMAP,
} QemuFileType;

char *qemu_find_file(QemuFileType type, const char *name);
void qemu_add_default_firmwarepath(void);
void qemu_add_data_dir(char *path);
void qemu_list_data_dirs(void);

#endif
