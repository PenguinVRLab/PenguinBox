/*
 * Support for QEMU/KVM hypercalls on s390x
 *
 * Copyright IBM Corp. 2012, 2017
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef HW_S390_HYPERCALL_H
#define HW_S390_HYPERCALL_H

#include "cpu.h"

#define DIAG500_VIRTIO_NOTIFY           0
#define DIAG500_VIRTIO_RESET            1
#define DIAG500_VIRTIO_SET_STATUS       2
#define DIAG500_VIRTIO_CCW_NOTIFY       3
#define DIAG500_STORAGE_LIMIT           4

void handle_diag_500(S390CPU *cpu, uintptr_t ra);

#endif
