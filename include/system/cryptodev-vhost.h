/*
 * QEMU Crypto Device Common Vhost Implement
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *    Gonglei <arei.gonglei@huawei.com>
 *    Jay Zhou <jianjay.zhou@huawei.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */
#ifndef CRYPTODEV_VHOST_H
#define CRYPTODEV_VHOST_H

#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-backend.h"
#include "chardev/char.h"

#include "system/cryptodev.h"


typedef struct CryptoDevBackendVhostOptions {
    VhostBackendType backend_type;
    void *opaque;
    int total_queues;
    CryptoDevBackendClient *cc;
} CryptoDevBackendVhostOptions;

typedef struct CryptoDevBackendVhost {
    struct vhost_dev dev;
    struct vhost_virtqueue vqs[1];
    int backend;
    CryptoDevBackendClient *cc;
} CryptoDevBackendVhost;

uint64_t
cryptodev_vhost_get_max_queues(
                        CryptoDevBackendVhost *crypto);


struct CryptoDevBackendVhost *
cryptodev_vhost_init(
             CryptoDevBackendVhostOptions *options);

void cryptodev_vhost_cleanup(
                        CryptoDevBackendVhost *crypto);

CryptoDevBackendVhost *
cryptodev_get_vhost(CryptoDevBackendClient *cc,
                            CryptoDevBackend *b,
                            uint16_t queue);
int cryptodev_vhost_start(VirtIODevice *dev, int total_queues);

void cryptodev_vhost_stop(VirtIODevice *dev, int total_queues);

void cryptodev_vhost_virtqueue_mask(VirtIODevice *dev,
                                           int queue,
                                           int idx, bool mask);

bool cryptodev_vhost_virtqueue_pending(VirtIODevice *dev,
                                              int queue, int idx);

#endif
