/*
 * Allwinner (sun4i and above) SD Host Controller emulation
 *
 * Copyright (C) 2019 Niek Linnenbank <nieklinnenbank@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "system/blockdev.h"
#include "system/dma.h"
#include "hw/qdev-properties.h"
#include "hw/irq.h"
#include "hw/sd/allwinner-sdhost.h"
#include "migration/vmstate.h"
#include "trace.h"
#include "qom/object.h"

#define TYPE_AW_SDHOST_BUS "allwinner-sdhost-bus"
DECLARE_INSTANCE_CHECKER(SDBus, AW_SDHOST_BUS,
                         TYPE_AW_SDHOST_BUS)

enum {
    REG_SD_GCTL       = 0x00,
    REG_SD_CKCR       = 0x04,
    REG_SD_TMOR       = 0x08,
    REG_SD_BWDR       = 0x0C,
    REG_SD_BKSR       = 0x10,
    REG_SD_BYCR       = 0x14,
    REG_SD_CMDR       = 0x18,
    REG_SD_CAGR       = 0x1C,
    REG_SD_RESP0      = 0x20,
    REG_SD_RESP1      = 0x24,
    REG_SD_RESP2      = 0x28,
    REG_SD_RESP3      = 0x2C,
    REG_SD_IMKR       = 0x30,
    REG_SD_MISR       = 0x34,
    REG_SD_RISR       = 0x38,
    REG_SD_STAR       = 0x3C,
    REG_SD_FWLR       = 0x40,
    REG_SD_FUNS       = 0x44,
    REG_SD_DBGC       = 0x50,
    REG_SD_A12A       = 0x58,
    REG_SD_NTSR       = 0x5C,
    REG_SD_SDBG       = 0x60,
    REG_SD_HWRST      = 0x78,
    REG_SD_DMAC       = 0x80,
    REG_SD_DLBA       = 0x84,
    REG_SD_IDST       = 0x88,
    REG_SD_IDIE       = 0x8C,
    REG_SD_THLDC      = 0x100,
    REG_SD_DSBD       = 0x10C,
    REG_SD_RES_CRC    = 0x110,
    REG_SD_DATA7_CRC  = 0x114,
    REG_SD_DATA6_CRC  = 0x118,
    REG_SD_DATA5_CRC  = 0x11C,
    REG_SD_DATA4_CRC  = 0x120,
    REG_SD_DATA3_CRC  = 0x124,
    REG_SD_DATA2_CRC  = 0x128,
    REG_SD_DATA1_CRC  = 0x12C,
    REG_SD_DATA0_CRC  = 0x130,
    REG_SD_CRC_STA    = 0x134,
    REG_SD_SAMP_DL    = 0x144,
    REG_SD_FIFO       = 0x200,
};

enum {
    SD_GCTL_FIFO_AC_MOD     = (1 << 31),
    SD_GCTL_DDR_MOD_SEL     = (1 << 10),
    SD_GCTL_CD_DBC_ENB      = (1 << 8),
    SD_GCTL_DMA_ENB         = (1 << 5),
    SD_GCTL_INT_ENB         = (1 << 4),
    SD_GCTL_DMA_RST         = (1 << 2),
    SD_GCTL_FIFO_RST        = (1 << 1),
    SD_GCTL_SOFT_RST        = (1 << 0),
};

enum {
    SD_CMDR_LOAD            = (1 << 31),
    SD_CMDR_CLKCHANGE       = (1 << 21),
    SD_CMDR_WRITE           = (1 << 10),
    SD_CMDR_AUTOSTOP        = (1 << 12),
    SD_CMDR_DATA            = (1 << 9),
    SD_CMDR_RESPONSE_LONG   = (1 << 7),
    SD_CMDR_RESPONSE        = (1 << 6),
    SD_CMDR_CMDID_MASK      = (0x3f),
};

enum {
    SD_RISR_CARD_REMOVE     = (1 << 31),
    SD_RISR_CARD_INSERT     = (1 << 30),
    SD_RISR_SDIO_INTR       = (1 << 16),
    SD_RISR_AUTOCMD_DONE    = (1 << 14),
    SD_RISR_DATA_COMPLETE   = (1 << 3),
    SD_RISR_CMD_COMPLETE    = (1 << 2),
    SD_RISR_NO_RESPONSE     = (1 << 1),
};

enum {
    SD_STAR_FIFO_EMPTY      = (1 << 2),
    SD_STAR_CARD_PRESENT    = (1 << 8),
    SD_STAR_FIFO_LEVEL_1    = (1 << 17),
};

enum {
    SD_IDST_INT_SUMMARY     = (1 << 8),
    SD_IDST_RECEIVE_IRQ     = (1 << 1),
    SD_IDST_TRANSMIT_IRQ    = (1 << 0),
    SD_IDST_IRQ_MASK        = (1 << 1) | (1 << 0) | (1 << 8),
    SD_IDST_WR_MASK         = (0x3ff),
};

enum {
    REG_SD_GCTL_RST         = 0x00000300,
    REG_SD_CKCR_RST         = 0x0,
    REG_SD_TMOR_RST         = 0xFFFFFF40,
    REG_SD_BWDR_RST         = 0x0,
    REG_SD_BKSR_RST         = 0x00000200,
    REG_SD_BYCR_RST         = 0x00000200,
    REG_SD_CMDR_RST         = 0x0,
    REG_SD_CAGR_RST         = 0x0,
    REG_SD_RESP_RST         = 0x0,
    REG_SD_IMKR_RST         = 0x0,
    REG_SD_MISR_RST         = 0x0,
    REG_SD_RISR_RST         = 0x0,
    REG_SD_STAR_RST         = 0x00000100,
    REG_SD_FWLR_RST         = 0x000F0000,
    REG_SD_FUNS_RST         = 0x0,
    REG_SD_DBGC_RST         = 0x0,
    REG_SD_A12A_RST         = 0x0000FFFF,
    REG_SD_NTSR_RST         = 0x00000001,
    REG_SD_SDBG_RST         = 0x0,
    REG_SD_HWRST_RST        = 0x00000001,
    REG_SD_DMAC_RST         = 0x0,
    REG_SD_DLBA_RST         = 0x0,
    REG_SD_IDST_RST         = 0x0,
    REG_SD_IDIE_RST         = 0x0,
    REG_SD_THLDC_RST        = 0x0,
    REG_SD_DSBD_RST         = 0x0,
    REG_SD_RES_CRC_RST      = 0x0,
    REG_SD_DATA_CRC_RST     = 0x0,
    REG_SD_CRC_STA_RST      = 0x0,
    REG_SD_SAMPLE_DL_RST    = 0x00002000,
    REG_SD_FIFO_RST         = 0x0,
};

typedef struct TransferDescriptor {
    uint32_t status;
    uint32_t size;
    uint32_t addr;
    uint32_t next;
} TransferDescriptor;

enum {
    DESC_STATUS_HOLD   = (1 << 31),
    DESC_STATUS_ERROR  = (1 << 30),
    DESC_STATUS_CHAIN  = (1 << 4),
    DESC_STATUS_FIRST  = (1 << 3),
    DESC_STATUS_LAST   = (1 << 2),
    DESC_STATUS_NOIRQ  = (1 << 1),
    DESC_SIZE_MASK     = (0xfffffffc)
};

static void allwinner_sdhost_update_irq(AwSdHostState *s)
{
    uint32_t irq;

    if (s->global_ctl & SD_GCTL_INT_ENB) {
        irq = s->irq_status & s->irq_mask;
    } else {
        irq = 0;
    }

    trace_allwinner_sdhost_update_irq(irq);
    qemu_set_irq(s->irq, !!irq);
}

static void allwinner_sdhost_update_transfer_cnt(AwSdHostState *s,
                                                 uint32_t bytes)
{
    if (s->transfer_cnt > bytes) {
        s->transfer_cnt -= bytes;
    } else {
        s->transfer_cnt = 0;
    }

    if (!s->transfer_cnt) {
        s->irq_status |= SD_RISR_DATA_COMPLETE;
    }
}

static void allwinner_sdhost_set_inserted(DeviceState *dev, bool inserted)
{
    AwSdHostState *s = AW_SDHOST(dev);

    trace_allwinner_sdhost_set_inserted(inserted);

    if (inserted) {
        s->irq_status |= SD_RISR_CARD_INSERT;
        s->irq_status &= ~SD_RISR_CARD_REMOVE;
        s->status |= SD_STAR_CARD_PRESENT;
    } else {
        s->irq_status &= ~SD_RISR_CARD_INSERT;
        s->irq_status |= SD_RISR_CARD_REMOVE;
        s->status &= ~SD_STAR_CARD_PRESENT;
    }

    allwinner_sdhost_update_irq(s);
}

static void allwinner_sdhost_send_command(AwSdHostState *s)
{
    SDRequest request;
    uint8_t resp[16];
    size_t rlen;

    s->command &= ~SD_CMDR_LOAD;

    if (!(s->command & SD_CMDR_CLKCHANGE)) {

        request.cmd = s->command & SD_CMDR_CMDID_MASK;
        request.arg = s->command_arg;

        rlen = sdbus_do_command(&s->sdbus, &request, resp, sizeof(resp));

        if ((s->command & SD_CMDR_RESPONSE)) {
            if (rlen == 4 && !(s->command & SD_CMDR_RESPONSE_LONG)) {
                s->response[0] = ldl_be_p(&resp[0]);
                s->response[1] = s->response[2] = s->response[3] = 0;

            } else if (rlen == 16 && (s->command & SD_CMDR_RESPONSE_LONG)) {
                s->response[0] = ldl_be_p(&resp[12]);
                s->response[1] = ldl_be_p(&resp[8]);
                s->response[2] = ldl_be_p(&resp[4]);
                s->response[3] = ldl_be_p(&resp[0]);
            } else {
                goto error;
            }
        }
    }

    s->irq_status |= SD_RISR_CMD_COMPLETE;
    return;

error:
    s->irq_status |= SD_RISR_NO_RESPONSE;
}

static void allwinner_sdhost_auto_stop(AwSdHostState *s)
{
    if ((s->command & SD_CMDR_AUTOSTOP) && (s->transfer_cnt == 0)) {
        uint32_t saved_cmd = s->command;
        uint32_t saved_arg = s->command_arg;

        s->command &= ~SD_CMDR_CMDID_MASK;
        s->command |= 12;
        s->command_arg = 0;

        allwinner_sdhost_send_command(s);

        s->command = saved_cmd;
        s->command_arg = saved_arg;

        s->irq_status |= SD_RISR_AUTOCMD_DONE;
    }
}

static void read_descriptor(AwSdHostState *s, hwaddr desc_addr,
                            TransferDescriptor *desc)
{
    uint32_t desc_words[4];
    dma_memory_read(&s->dma_as, desc_addr, &desc_words, sizeof(desc_words),
                    MEMTXATTRS_UNSPECIFIED);
    desc->status = le32_to_cpu(desc_words[0]);
    desc->size = le32_to_cpu(desc_words[1]);
    desc->addr = le32_to_cpu(desc_words[2]);
    desc->next = le32_to_cpu(desc_words[3]);
}

static void write_descriptor(AwSdHostState *s, hwaddr desc_addr,
                             const TransferDescriptor *desc)
{
    uint32_t desc_words[4];
    desc_words[0] = cpu_to_le32(desc->status);
    desc_words[1] = cpu_to_le32(desc->size);
    desc_words[2] = cpu_to_le32(desc->addr);
    desc_words[3] = cpu_to_le32(desc->next);
    dma_memory_write(&s->dma_as, desc_addr, &desc_words, sizeof(desc_words),
                     MEMTXATTRS_UNSPECIFIED);
}

static uint32_t allwinner_sdhost_process_desc(AwSdHostState *s,
                                              hwaddr desc_addr,
                                              TransferDescriptor *desc,
                                              bool is_write, uint32_t max_bytes)
{
    AwSdHostClass *klass = AW_SDHOST_GET_CLASS(s);
    uint32_t num_done = 0;
    uint32_t num_bytes = max_bytes;
    uint8_t buf[1024];

    read_descriptor(s, desc_addr, desc);
    if (desc->size == 0) {
        desc->size = klass->max_desc_size;
    } else if (desc->size > klass->max_desc_size) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: DMA descriptor buffer size "
                      " is out-of-bounds: %" PRIu32 " > %zu",
                      __func__, desc->size, klass->max_desc_size);
        desc->size = klass->max_desc_size;
    }
    if (desc->size < num_bytes) {
        num_bytes = desc->size;
    }

    trace_allwinner_sdhost_process_desc(desc_addr, desc->size,
                                        is_write, max_bytes);

    while (num_done < num_bytes) {
        uint32_t buf_bytes = num_bytes - num_done;
        if (buf_bytes > sizeof(buf)) {
            buf_bytes = sizeof(buf);
        }

        if (is_write) {
            dma_memory_read(&s->dma_as,
                            (desc->addr & DESC_SIZE_MASK) + num_done, buf,
                            buf_bytes, MEMTXATTRS_UNSPECIFIED);
            sdbus_write_data(&s->sdbus, buf, buf_bytes);

        } else {
            sdbus_read_data(&s->sdbus, buf, buf_bytes);
            dma_memory_write(&s->dma_as,
                             (desc->addr & DESC_SIZE_MASK) + num_done, buf,
                             buf_bytes, MEMTXATTRS_UNSPECIFIED);
        }
        num_done += buf_bytes;
    }

    desc->status &= ~DESC_STATUS_HOLD;
    write_descriptor(s, desc_addr, desc);

    return num_done;
}

static void allwinner_sdhost_dma(AwSdHostState *s)
{
    TransferDescriptor desc;
    hwaddr desc_addr = s->desc_base;
    bool is_write = (s->command & SD_CMDR_WRITE);
    uint32_t bytes_done = 0;

    if (s->byte_count == 0 || s->block_size == 0 ||
      !(s->global_ctl & SD_GCTL_DMA_ENB)) {
        return;
    }

    if (!is_write && !sdbus_data_ready(&s->sdbus)) {
        return;
    }

    while (s->byte_count > 0) {
        bytes_done = allwinner_sdhost_process_desc(s, desc_addr, &desc,
                                                   is_write, s->byte_count);
        allwinner_sdhost_update_transfer_cnt(s, bytes_done);

        if (bytes_done <= s->byte_count) {
            s->byte_count -= bytes_done;
        } else {
            s->byte_count = 0;
        }

        if (desc.status & DESC_STATUS_LAST) {
            break;
        } else {
            desc_addr = desc.next;
        }
    }

    s->irq_status |= SD_RISR_DATA_COMPLETE | SD_RISR_SDIO_INTR;

    s->dmac_status |= SD_IDST_INT_SUMMARY;

    if (is_write) {
        s->dmac_status |= SD_IDST_TRANSMIT_IRQ;
    } else {
        s->dmac_status |= SD_IDST_RECEIVE_IRQ;
    }
}

static uint32_t allwinner_sdhost_fifo_read(AwSdHostState *s)
{
    uint32_t res = 0;

    if (sdbus_data_ready(&s->sdbus)) {
        sdbus_read_data(&s->sdbus, &res, sizeof(uint32_t));
        le32_to_cpus(&res);
        allwinner_sdhost_update_transfer_cnt(s, sizeof(uint32_t));
        allwinner_sdhost_auto_stop(s);
        allwinner_sdhost_update_irq(s);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: no data ready on SD bus\n",
                      __func__);
    }

    return res;
}

static uint64_t allwinner_sdhost_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    AwSdHostState *s = AW_SDHOST(opaque);
    AwSdHostClass *sc = AW_SDHOST_GET_CLASS(s);
    bool out_of_bounds = false;
    uint32_t res = 0;

    switch (offset) {
    case REG_SD_GCTL:
        res = s->global_ctl;
        break;
    case REG_SD_CKCR:
        res = s->clock_ctl;
        break;
    case REG_SD_TMOR:
        res = s->timeout;
        break;
    case REG_SD_BWDR:
        res = s->bus_width;
        break;
    case REG_SD_BKSR:
        res = s->block_size;
        break;
    case REG_SD_BYCR:
        res = s->byte_count;
        break;
    case REG_SD_CMDR:
        res = s->command;
        break;
    case REG_SD_CAGR:
        res = s->command_arg;
        break;
    case REG_SD_RESP0:
        res = s->response[0];
        break;
    case REG_SD_RESP1:
        res = s->response[1];
        break;
    case REG_SD_RESP2:
        res = s->response[2];
        break;
    case REG_SD_RESP3:
        res = s->response[3];
        break;
    case REG_SD_IMKR:
        res = s->irq_mask;
        break;
    case REG_SD_MISR:
        res = s->irq_status & s->irq_mask;
        break;
    case REG_SD_RISR:
        res = s->irq_status;
        break;
    case REG_SD_STAR:
        res = s->status;
        if (sdbus_data_ready(&s->sdbus)) {
            res |= SD_STAR_FIFO_LEVEL_1;
        } else {
            res |= SD_STAR_FIFO_EMPTY;
        }
        break;
    case REG_SD_FWLR:
        res = s->fifo_wlevel;
        break;
    case REG_SD_FUNS:
        res = s->fifo_func_sel;
        break;
    case REG_SD_DBGC:
        res = s->debug_enable;
        break;
    case REG_SD_A12A:
        res = s->auto12_arg;
        break;
    case REG_SD_NTSR:
        res = s->newtiming_set;
        break;
    case REG_SD_SDBG:
        res = s->newtiming_debug;
        break;
    case REG_SD_HWRST:
        res = s->hardware_rst;
        break;
    case REG_SD_DMAC:
        res = s->dmac;
        break;
    case REG_SD_DLBA:
        res = s->desc_base;
        break;
    case REG_SD_IDST:
        res = s->dmac_status;
        break;
    case REG_SD_IDIE:
        res = s->dmac_irq;
        break;
    case REG_SD_THLDC:
        if (sc->is_sun4i) {
            res = allwinner_sdhost_fifo_read(s);
        } else {
            res = s->card_threshold;
        }
        break;
    case REG_SD_DSBD:
        res = s->startbit_detect;
        break;
    case REG_SD_RES_CRC:
        res = s->response_crc;
        break;
    case REG_SD_DATA7_CRC:
    case REG_SD_DATA6_CRC:
    case REG_SD_DATA5_CRC:
    case REG_SD_DATA4_CRC:
    case REG_SD_DATA3_CRC:
    case REG_SD_DATA2_CRC:
    case REG_SD_DATA1_CRC:
    case REG_SD_DATA0_CRC:
        res = s->data_crc[((offset - REG_SD_DATA7_CRC) / sizeof(uint32_t))];
        break;
    case REG_SD_CRC_STA:
        res = s->status_crc;
        break;
    case REG_SD_FIFO:
        res = allwinner_sdhost_fifo_read(s);
        break;
    case REG_SD_SAMP_DL:
        if (sc->can_calibrate) {
            res = s->sample_delay;
        } else {
            out_of_bounds = true;
        }
        break;
    default:
        out_of_bounds = true;
        res = 0;
        break;
    }

    if (out_of_bounds) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset %"
                      HWADDR_PRIx"\n", __func__, offset);
    }

    trace_allwinner_sdhost_read(offset, res, size);
    return res;
}

static void allwinner_sdhost_fifo_write(AwSdHostState *s, uint64_t value)
{
    uint32_t u32 = cpu_to_le32(value);
    sdbus_write_data(&s->sdbus, &u32, sizeof(u32));
    allwinner_sdhost_update_transfer_cnt(s, sizeof(u32));
    allwinner_sdhost_auto_stop(s);
    allwinner_sdhost_update_irq(s);
}

static void allwinner_sdhost_write(void *opaque, hwaddr offset,
                                   uint64_t value, unsigned size)
{
    AwSdHostState *s = AW_SDHOST(opaque);
    AwSdHostClass *sc = AW_SDHOST_GET_CLASS(s);
    bool out_of_bounds = false;

    trace_allwinner_sdhost_write(offset, value, size);

    switch (offset) {
    case REG_SD_GCTL:
        s->global_ctl = value;
        s->global_ctl &= ~(SD_GCTL_DMA_RST | SD_GCTL_FIFO_RST |
                           SD_GCTL_SOFT_RST);
        allwinner_sdhost_update_irq(s);
        break;
    case REG_SD_CKCR:
        s->clock_ctl = value;
        break;
    case REG_SD_TMOR:
        s->timeout = value;
        break;
    case REG_SD_BWDR:
        s->bus_width = value;
        break;
    case REG_SD_BKSR:
        s->block_size = value;
        break;
    case REG_SD_BYCR:
        s->byte_count = value;
        s->transfer_cnt = value;
        break;
    case REG_SD_CMDR:
        s->command = value;
        if (value & SD_CMDR_LOAD) {
            allwinner_sdhost_send_command(s);
            allwinner_sdhost_dma(s);
            allwinner_sdhost_auto_stop(s);
        }
        allwinner_sdhost_update_irq(s);
        break;
    case REG_SD_CAGR:
        s->command_arg = value;
        break;
    case REG_SD_RESP0:
        s->response[0] = value;
        break;
    case REG_SD_RESP1:
        s->response[1] = value;
        break;
    case REG_SD_RESP2:
        s->response[2] = value;
        break;
    case REG_SD_RESP3:
        s->response[3] = value;
        break;
    case REG_SD_IMKR:
        s->irq_mask = value;
        allwinner_sdhost_update_irq(s);
        break;
    case REG_SD_MISR:
    case REG_SD_RISR:
        s->irq_status &= ~value;
        allwinner_sdhost_update_irq(s);
        break;
    case REG_SD_STAR:
        s->status &= ~value;
        allwinner_sdhost_update_irq(s);
        break;
    case REG_SD_FWLR:
        s->fifo_wlevel = value;
        break;
    case REG_SD_FUNS:
        s->fifo_func_sel = value;
        break;
    case REG_SD_DBGC:
        s->debug_enable = value;
        break;
    case REG_SD_A12A:
        s->auto12_arg = value;
        break;
    case REG_SD_NTSR:
        s->newtiming_set = value;
        break;
    case REG_SD_SDBG:
        s->newtiming_debug = value;
        break;
    case REG_SD_HWRST:
        s->hardware_rst = value;
        break;
    case REG_SD_DMAC:
        s->dmac = value;
        allwinner_sdhost_update_irq(s);
        break;
    case REG_SD_DLBA:
        s->desc_base = value;
        break;
    case REG_SD_IDST:
        s->dmac_status &= (~SD_IDST_WR_MASK) | (~value & SD_IDST_WR_MASK);
        allwinner_sdhost_update_irq(s);
        break;
    case REG_SD_IDIE:
        s->dmac_irq = value;
        allwinner_sdhost_update_irq(s);
        break;
    case REG_SD_THLDC:
        if (sc->is_sun4i) {
            allwinner_sdhost_fifo_write(s, value);
        } else {
            s->card_threshold = value;
        }
        break;
    case REG_SD_DSBD:
        s->startbit_detect = value;
        break;
    case REG_SD_FIFO:
        allwinner_sdhost_fifo_write(s, value);
        break;
    case REG_SD_RES_CRC:
    case REG_SD_DATA7_CRC:
    case REG_SD_DATA6_CRC:
    case REG_SD_DATA5_CRC:
    case REG_SD_DATA4_CRC:
    case REG_SD_DATA3_CRC:
    case REG_SD_DATA2_CRC:
    case REG_SD_DATA1_CRC:
    case REG_SD_DATA0_CRC:
    case REG_SD_CRC_STA:
        break;
    case REG_SD_SAMP_DL:
        if (sc->can_calibrate) {
            s->sample_delay = value;
        } else {
            out_of_bounds = true;
        }
        break;
    default:
        out_of_bounds = true;
        break;
    }

    if (out_of_bounds) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset %"
                      HWADDR_PRIx"\n", __func__, offset);
    }
}

static const MemoryRegionOps allwinner_sdhost_ops = {
    .read = allwinner_sdhost_read,
    .write = allwinner_sdhost_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static const VMStateDescription vmstate_allwinner_sdhost = {
    .name = "allwinner-sdhost",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(global_ctl, AwSdHostState),
        VMSTATE_UINT32(clock_ctl, AwSdHostState),
        VMSTATE_UINT32(timeout, AwSdHostState),
        VMSTATE_UINT32(bus_width, AwSdHostState),
        VMSTATE_UINT32(block_size, AwSdHostState),
        VMSTATE_UINT32(byte_count, AwSdHostState),
        VMSTATE_UINT32(transfer_cnt, AwSdHostState),
        VMSTATE_UINT32(command, AwSdHostState),
        VMSTATE_UINT32(command_arg, AwSdHostState),
        VMSTATE_UINT32_ARRAY(response, AwSdHostState, 4),
        VMSTATE_UINT32(irq_mask, AwSdHostState),
        VMSTATE_UINT32(irq_status, AwSdHostState),
        VMSTATE_UINT32(status, AwSdHostState),
        VMSTATE_UINT32(fifo_wlevel, AwSdHostState),
        VMSTATE_UINT32(fifo_func_sel, AwSdHostState),
        VMSTATE_UINT32(debug_enable, AwSdHostState),
        VMSTATE_UINT32(auto12_arg, AwSdHostState),
        VMSTATE_UINT32(newtiming_set, AwSdHostState),
        VMSTATE_UINT32(newtiming_debug, AwSdHostState),
        VMSTATE_UINT32(hardware_rst, AwSdHostState),
        VMSTATE_UINT32(dmac, AwSdHostState),
        VMSTATE_UINT32(desc_base, AwSdHostState),
        VMSTATE_UINT32(dmac_status, AwSdHostState),
        VMSTATE_UINT32(dmac_irq, AwSdHostState),
        VMSTATE_UINT32(card_threshold, AwSdHostState),
        VMSTATE_UINT32(startbit_detect, AwSdHostState),
        VMSTATE_UINT32(response_crc, AwSdHostState),
        VMSTATE_UINT32_ARRAY(data_crc, AwSdHostState, 8),
        VMSTATE_UINT32(status_crc, AwSdHostState),
        VMSTATE_UINT32(sample_delay, AwSdHostState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property allwinner_sdhost_properties[] = {
    DEFINE_PROP_LINK("dma-memory", AwSdHostState, dma_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
};

static void allwinner_sdhost_init(Object *obj)
{
    AwSdHostState *s = AW_SDHOST(obj);

    qbus_init(&s->sdbus, sizeof(s->sdbus),
              TYPE_AW_SDHOST_BUS, DEVICE(s), "sd-bus");

    memory_region_init_io(&s->iomem, obj, &allwinner_sdhost_ops, s,
                           TYPE_AW_SDHOST, 4 * KiB);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->irq);
}

static void allwinner_sdhost_realize(DeviceState *dev, Error **errp)
{
    AwSdHostState *s = AW_SDHOST(dev);

    if (!s->dma_mr) {
        error_setg(errp, TYPE_AW_SDHOST " 'dma-memory' link not set");
        return;
    }

    address_space_init(&s->dma_as, s->dma_mr, "sdhost-dma");
}

static void allwinner_sdhost_reset(DeviceState *dev)
{
    AwSdHostState *s = AW_SDHOST(dev);
    AwSdHostClass *sc = AW_SDHOST_GET_CLASS(s);

    s->global_ctl = REG_SD_GCTL_RST;
    s->clock_ctl = REG_SD_CKCR_RST;
    s->timeout = REG_SD_TMOR_RST;
    s->bus_width = REG_SD_BWDR_RST;
    s->block_size = REG_SD_BKSR_RST;
    s->byte_count = REG_SD_BYCR_RST;
    s->transfer_cnt = 0;

    s->command = REG_SD_CMDR_RST;
    s->command_arg = REG_SD_CAGR_RST;

    for (int i = 0; i < ARRAY_SIZE(s->response); i++) {
        s->response[i] = REG_SD_RESP_RST;
    }

    s->irq_mask = REG_SD_IMKR_RST;
    s->irq_status = REG_SD_RISR_RST;
    s->status = REG_SD_STAR_RST;

    s->fifo_wlevel = REG_SD_FWLR_RST;
    s->fifo_func_sel = REG_SD_FUNS_RST;
    s->debug_enable = REG_SD_DBGC_RST;
    s->auto12_arg = REG_SD_A12A_RST;
    s->newtiming_set = REG_SD_NTSR_RST;
    s->newtiming_debug = REG_SD_SDBG_RST;
    s->hardware_rst = REG_SD_HWRST_RST;
    s->dmac = REG_SD_DMAC_RST;
    s->desc_base = REG_SD_DLBA_RST;
    s->dmac_status = REG_SD_IDST_RST;
    s->dmac_irq = REG_SD_IDIE_RST;
    s->card_threshold = REG_SD_THLDC_RST;
    s->startbit_detect = REG_SD_DSBD_RST;
    s->response_crc = REG_SD_RES_CRC_RST;

    for (int i = 0; i < ARRAY_SIZE(s->data_crc); i++) {
        s->data_crc[i] = REG_SD_DATA_CRC_RST;
    }

    s->status_crc = REG_SD_CRC_STA_RST;

    if (sc->can_calibrate) {
        s->sample_delay = REG_SD_SAMPLE_DL_RST;
    }
}

static void allwinner_sdhost_bus_class_init(ObjectClass *klass,
                                            const void *data)
{
    SDBusClass *sbc = SD_BUS_CLASS(klass);

    sbc->set_inserted = allwinner_sdhost_set_inserted;
}

static void allwinner_sdhost_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, allwinner_sdhost_reset);
    dc->vmsd = &vmstate_allwinner_sdhost;
    dc->realize = allwinner_sdhost_realize;
    device_class_set_props(dc, allwinner_sdhost_properties);
}

static void allwinner_sdhost_sun4i_class_init(ObjectClass *klass,
                                              const void *data)
{
    AwSdHostClass *sc = AW_SDHOST_CLASS(klass);
    sc->max_desc_size = 8 * KiB;
    sc->is_sun4i = true;
    sc->can_calibrate = false;
}

static void allwinner_sdhost_sun5i_class_init(ObjectClass *klass,
                                              const void *data)
{
    AwSdHostClass *sc = AW_SDHOST_CLASS(klass);
    sc->max_desc_size = 64 * KiB;
    sc->is_sun4i = false;
    sc->can_calibrate = false;
}

static void allwinner_sdhost_sun50i_a64_class_init(ObjectClass *klass,
                                                   const void *data)
{
    AwSdHostClass *sc = AW_SDHOST_CLASS(klass);
    sc->max_desc_size = 64 * KiB;
    sc->is_sun4i = false;
    sc->can_calibrate = true;
}

static void allwinner_sdhost_sun50i_a64_emmc_class_init(ObjectClass *klass,
                                                        const void *data)
{
    AwSdHostClass *sc = AW_SDHOST_CLASS(klass);
    sc->max_desc_size = 8 * KiB;
    sc->is_sun4i = false;
    sc->can_calibrate = true;
}

static const TypeInfo allwinner_sdhost_info = {
    .name          = TYPE_AW_SDHOST,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = allwinner_sdhost_init,
    .instance_size = sizeof(AwSdHostState),
    .class_init    = allwinner_sdhost_class_init,
    .class_size    = sizeof(AwSdHostClass),
    .abstract      = true,
};

static const TypeInfo allwinner_sdhost_sun4i_info = {
    .name          = TYPE_AW_SDHOST_SUN4I,
    .parent        = TYPE_AW_SDHOST,
    .class_init    = allwinner_sdhost_sun4i_class_init,
};

static const TypeInfo allwinner_sdhost_sun5i_info = {
    .name          = TYPE_AW_SDHOST_SUN5I,
    .parent        = TYPE_AW_SDHOST,
    .class_init    = allwinner_sdhost_sun5i_class_init,
};

static const TypeInfo allwinner_sdhost_sun50i_a64_info = {
    .name          = TYPE_AW_SDHOST_SUN50I_A64,
    .parent        = TYPE_AW_SDHOST,
    .class_init    = allwinner_sdhost_sun50i_a64_class_init,
};

static const TypeInfo allwinner_sdhost_sun50i_a64_emmc_info = {
    .name          = TYPE_AW_SDHOST_SUN50I_A64_EMMC,
    .parent        = TYPE_AW_SDHOST,
    .class_init    = allwinner_sdhost_sun50i_a64_emmc_class_init,
};

static const TypeInfo allwinner_sdhost_bus_info = {
    .name = TYPE_AW_SDHOST_BUS,
    .parent = TYPE_SD_BUS,
    .instance_size = sizeof(SDBus),
    .class_init = allwinner_sdhost_bus_class_init,
};

static void allwinner_sdhost_register_types(void)
{
    type_register_static(&allwinner_sdhost_info);
    type_register_static(&allwinner_sdhost_sun4i_info);
    type_register_static(&allwinner_sdhost_sun5i_info);
    type_register_static(&allwinner_sdhost_sun50i_a64_info);
    type_register_static(&allwinner_sdhost_sun50i_a64_emmc_info);
    type_register_static(&allwinner_sdhost_bus_info);
}

type_init(allwinner_sdhost_register_types)
