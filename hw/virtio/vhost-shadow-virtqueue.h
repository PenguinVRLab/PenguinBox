/*
 * vhost shadow virtqueue
 *
 * SPDX-FileCopyrightText: Red Hat, Inc. 2021
 * SPDX-FileContributor: Author: Eugenio Pérez <eperezma@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef VHOST_SHADOW_VIRTQUEUE_H
#define VHOST_SHADOW_VIRTQUEUE_H

#include "qemu/event_notifier.h"
#include "hw/virtio/virtio.h"
#include "standard-headers/linux/vhost_types.h"
#include "hw/virtio/vhost-iova-tree.h"

typedef struct SVQDescState {
    VirtQueueElement *elem;

    unsigned int ndescs;
} SVQDescState;

typedef struct VhostShadowVirtqueue VhostShadowVirtqueue;

typedef int (*VirtQueueAvailCallback)(VhostShadowVirtqueue *svq,
                                      VirtQueueElement *elem,
                                      void *vq_callback_opaque);

typedef struct VhostShadowVirtqueueOps {
    VirtQueueAvailCallback avail_handler;
} VhostShadowVirtqueueOps;

typedef struct VhostShadowVirtqueue {
    struct vring vring;

    EventNotifier hdev_kick;
    EventNotifier hdev_call;

    EventNotifier svq_kick;

    EventNotifier svq_call;

    VirtQueue *vq;

    VirtIODevice *vdev;

    VhostIOVATree *iova_tree;

    SVQDescState *desc_state;

    VirtQueueElement *next_guest_avail_elem;

    uint16_t *desc_next;

    const VhostShadowVirtqueueOps *ops;

    void *ops_opaque;

    uint16_t shadow_avail_idx;

    uint16_t free_head;

    uint16_t shadow_used_idx;

    uint16_t last_used_idx;

    uint16_t num_free;
} VhostShadowVirtqueue;

bool vhost_svq_valid_features(uint64_t features, Error **errp);

uint16_t vhost_svq_available_slots(const VhostShadowVirtqueue *svq);
void vhost_svq_push_elem(VhostShadowVirtqueue *svq,
                         const VirtQueueElement *elem, uint32_t len);
int vhost_svq_add(VhostShadowVirtqueue *svq, const struct iovec *out_sg,
                  size_t out_num, const hwaddr *out_addr,
                  const struct iovec *in_sg, size_t in_num,
                  const hwaddr *in_addr, VirtQueueElement *elem);
size_t vhost_svq_poll(VhostShadowVirtqueue *svq, size_t num);

void vhost_svq_set_svq_kick_fd(VhostShadowVirtqueue *svq, int svq_kick_fd);
void vhost_svq_set_svq_call_fd(VhostShadowVirtqueue *svq, int call_fd);
void vhost_svq_get_vring_addr(const VhostShadowVirtqueue *svq,
                              struct vhost_vring_addr *addr);
size_t vhost_svq_driver_area_size(const VhostShadowVirtqueue *svq);
size_t vhost_svq_device_area_size(const VhostShadowVirtqueue *svq);

void vhost_svq_start(VhostShadowVirtqueue *svq, VirtIODevice *vdev,
                     VirtQueue *vq, VhostIOVATree *iova_tree);
void vhost_svq_stop(VhostShadowVirtqueue *svq);

VhostShadowVirtqueue *vhost_svq_new(const VhostShadowVirtqueueOps *ops,
                                    void *ops_opaque);

void vhost_svq_free(gpointer vq);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(VhostShadowVirtqueue, vhost_svq_free);

#endif
