/*
 * QEMU Hyper-V VMBus
 *
 * Copyright (c) 2017-2018 Virtuozzo International GmbH.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_HYPERV_VMBUS_H
#define HW_HYPERV_VMBUS_H

#include "system/system.h"
#include "system/dma.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"
#include "hw/hyperv/vmbus-proto.h"
#include "qemu/uuid.h"
#include "qom/object.h"

#define TYPE_VMBUS_DEVICE "vmbus-dev"

OBJECT_DECLARE_TYPE(VMBusDevice, VMBusDeviceClass,
                    VMBUS_DEVICE)

#define TYPE_VMBUS "vmbus"
OBJECT_DECLARE_SIMPLE_TYPE(VMBus, VMBUS)

typedef struct VMBusGpadl VMBusGpadl;
typedef struct VMBusChannel VMBusChannel;

typedef void(*VMBusChannelNotifyCb)(struct VMBusChannel *chan);

struct VMBusDeviceClass {
    DeviceClass parent;

    QemuUUID classid;
    QemuUUID instanceid;
    uint16_t channel_flags;
    uint16_t mmio_size_mb;

    void (*vmdev_realize)(VMBusDevice *vdev, Error **errp);
    void (*vmdev_unrealize)(VMBusDevice *vdev);
    void (*vmdev_reset)(VMBusDevice *vdev);
    uint16_t (*num_channels)(VMBusDevice *vdev);
    int (*open_channel)(VMBusChannel *chan);
    void (*close_channel)(VMBusChannel *chan);
    VMBusChannelNotifyCb chan_notify_cb;
};

struct VMBusDevice {
    DeviceState parent;
    QemuUUID instanceid;
    uint16_t num_channels;
    VMBusChannel *channels;
    AddressSpace *dma_as;
};

extern const VMStateDescription vmstate_vmbus_dev;

typedef struct VMBusChanReq {
    VMBusChannel *chan;
    uint16_t pkt_type;
    uint32_t msglen;
    void *msg;
    uint64_t transaction_id;
    bool need_comp;
    QEMUSGList sgl;
} VMBusChanReq;

VMBusDevice *vmbus_channel_device(VMBusChannel *chan);
VMBusChannel *vmbus_device_channel(VMBusDevice *dev, uint32_t chan_idx);
uint32_t vmbus_channel_idx(VMBusChannel *chan);
bool vmbus_channel_is_open(VMBusChannel *chan);

void vmbus_channel_notify_host(VMBusChannel *chan);

int vmbus_channel_reserve(VMBusChannel *chan,
                          uint32_t desclen, uint32_t msglen);

ssize_t vmbus_channel_send(VMBusChannel *chan, uint16_t pkt_type,
                           void *desc, uint32_t desclen,
                           void *msg, uint32_t msglen,
                           bool need_comp, uint64_t transaction_id);

int vmbus_channel_recv_start(VMBusChannel *chan);

ssize_t vmbus_channel_send_completion(VMBusChanReq *req,
                                      void *msg, uint32_t msglen);

void *vmbus_channel_recv_peek(VMBusChannel *chan, uint32_t size);

void vmbus_channel_recv_pop(VMBusChannel *chan);

ssize_t vmbus_channel_recv_done(VMBusChannel *chan);

void vmbus_free_req(void *req);

VMBusGpadl *vmbus_get_gpadl(VMBusChannel *chan, uint32_t gpadl_id);

void vmbus_put_gpadl(VMBusGpadl *gpadl);

uint32_t vmbus_gpadl_len(VMBusGpadl *gpadl);

ssize_t vmbus_iov_to_gpadl(VMBusChannel *chan, VMBusGpadl *gpadl, uint32_t off,
                           const struct iovec *iov, size_t iov_cnt);

int vmbus_map_sgl(VMBusChanReq *req, DMADirection dir, struct iovec *iov,
                  unsigned iov_cnt, size_t len, size_t off);

void vmbus_unmap_sgl(VMBusChanReq *req, DMADirection dir, struct iovec *iov,
                     unsigned iov_cnt, size_t accessed);

#endif
