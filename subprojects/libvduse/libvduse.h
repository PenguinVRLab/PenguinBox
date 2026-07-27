/*
 * VDUSE (vDPA Device in Userspace) library
 *
 * Copyright (C) 2022 Bytedance Inc. and/or its affiliates. All rights reserved.
 *
 * Author:
 *   Xie Yongji <xieyongji@bytedance.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#ifndef LIBVDUSE_H
#define LIBVDUSE_H

#include <stdint.h>
#include <sys/uio.h>

#define VIRTQUEUE_MAX_SIZE 1024

typedef struct VduseDev VduseDev;

typedef struct VduseVirtq VduseVirtq;

typedef struct VduseOps {
    void (*enable_queue)(VduseDev *dev, VduseVirtq *vq);
    void (*disable_queue)(VduseDev *dev, VduseVirtq *vq);
} VduseOps;

typedef struct VduseVirtqElement {
    unsigned int index;
    unsigned int out_num;
    unsigned int in_num;
    struct iovec *in_sg;
    struct iovec *out_sg;
} VduseVirtqElement;


uint64_t vduse_get_virtio_features(void);

VduseDev *vduse_queue_get_dev(VduseVirtq *vq);

int vduse_queue_get_fd(VduseVirtq *vq);

void *vduse_queue_pop(VduseVirtq *vq, size_t sz);

void vduse_queue_push(VduseVirtq *vq, const VduseVirtqElement *elem,
                      unsigned int len);
void vduse_queue_notify(VduseVirtq *vq);

void *vduse_dev_get_priv(VduseDev *dev);

VduseVirtq *vduse_dev_get_queue(VduseDev *dev, int index);

int vduse_dev_get_fd(VduseDev *dev);

int vduse_dev_handler(VduseDev *dev);

int vduse_dev_update_config(VduseDev *dev, uint32_t size,
                            uint32_t offset, char *buffer);

int vduse_dev_setup_queue(VduseDev *dev, int index, int max_size);

int vduse_set_reconnect_log_file(VduseDev *dev, const char *filename);

VduseDev *vduse_dev_create_by_fd(int fd, uint16_t num_queues,
                                 const VduseOps *ops, void *priv);

VduseDev *vduse_dev_create_by_name(const char *name, uint16_t num_queues,
                                   const VduseOps *ops, void *priv);

VduseDev *vduse_dev_create(const char *name, uint32_t device_id,
                           uint32_t vendor_id, uint64_t features,
                           uint16_t num_queues, uint32_t config_size,
                           char *config, const VduseOps *ops, void *priv);

int vduse_dev_destroy(VduseDev *dev);

#endif
