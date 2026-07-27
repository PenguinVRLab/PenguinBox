#ifndef HW_MIPS_H
#define HW_MIPS_H

#include "qemu/units.h"

#define INITRD_PAGE_SIZE (64 * KiB)

#include "system/memory.h"

PCIBus *bonito_init(qemu_irq *pic);

typedef struct rc4030DMAState *rc4030_dma;
void rc4030_dma_read(void *dma, uint8_t *buf, int len);
void rc4030_dma_write(void *dma, uint8_t *buf, int len);

DeviceState *rc4030_init(rc4030_dma **dmas, IOMMUMemoryRegion **dma_mr);

#endif
