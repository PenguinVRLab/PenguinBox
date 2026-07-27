/*
 * xlnx_dpdma.h
 *
 *  Copyright (C) 2015 : GreenSocs Ltd
 *      http://www.greensocs.com/ , email: info@greensocs.com
 *
 *  Developed by :
 *  Frederic Konrad   <fred.konrad@greensocs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef XLNX_DPDMA_H
#define XLNX_DPDMA_H

#include "hw/sysbus.h"
#include "system/dma.h"
#include "qom/object.h"

#define XLNX_DPDMA_REG_ARRAY_SIZE (0x1000 >> 2)

struct XlnxDPDMAState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t registers[XLNX_DPDMA_REG_ARRAY_SIZE];
    uint8_t *data[6];
    bool operation_finished[6];
    qemu_irq irq;
};


#define TYPE_XLNX_DPDMA "xlnx.dpdma"
OBJECT_DECLARE_SIMPLE_TYPE(XlnxDPDMAState, XLNX_DPDMA)

size_t xlnx_dpdma_start_operation(XlnxDPDMAState *s, uint8_t channel,
                                  bool one_desc);

void xlnx_dpdma_set_host_data_location(XlnxDPDMAState *s, uint8_t channel,
                                       void *p);

void xlnx_dpdma_trigger_vsync_irq(XlnxDPDMAState *s);

#endif
