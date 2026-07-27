/*
 * QEMU "hardware version" machinery
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef QEMU_HW_VERSION_H
#define QEMU_HW_VERSION_H

#define QEMU_HW_VERSION "2.5+"

void qemu_set_hw_version(const char *);
const char *qemu_hw_version(void);

#endif
