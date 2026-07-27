#include "qemu/osdep.h"
#include "hw/virtio/virtio-gpu.h"

bool virtio_gpu_have_udmabuf(void)
{
    return false;
}

void virtio_gpu_init_udmabuf(struct virtio_gpu_simple_resource *res)
{
}

void virtio_gpu_fini_udmabuf(struct virtio_gpu_simple_resource *res)
{
}

int virtio_gpu_update_dmabuf(VirtIOGPU *g,
                             uint32_t scanout_id,
                             struct virtio_gpu_simple_resource *res,
                             struct virtio_gpu_framebuffer *fb,
                             struct virtio_gpu_rect *r)
{
    return 0;
}
