/*
 * QEMU ATAPI Emulation
 *
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2006 Openedhand Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "hw/scsi/scsi.h"
#include "system/block-backend.h"
#include "scsi/constants.h"
#include "ide-internal.h"
#include "trace.h"

#define ATAPI_SECTOR_BITS (2 + BDRV_SECTOR_BITS)
#define ATAPI_SECTOR_SIZE (1 << ATAPI_SECTOR_BITS)

static void ide_atapi_cmd_read_dma_cb(void *opaque, int ret);

static void padstr8(uint8_t *buf, int buf_size, const char *src)
{
    int i;
    for(i = 0; i < buf_size; i++) {
        if (*src)
            buf[i] = *src++;
        else
            buf[i] = ' ';
    }
}

static void lba_to_msf(uint8_t *buf, int lba)
{
    lba += 150;
    buf[0] = (lba / 75) / 60;
    buf[1] = (lba / 75) % 60;
    buf[2] = lba % 75;
}

static inline int media_present(IDEState *s)
{
    return !s->tray_open && s->nb_sectors > 0;
}

static inline int media_is_dvd(IDEState *s)
{
    return (media_present(s) && s->nb_sectors > CD_MAX_SECTORS);
}

static inline int media_is_cd(IDEState *s)
{
    return (media_present(s) && s->nb_sectors <= CD_MAX_SECTORS);
}

static void cd_data_to_raw(uint8_t *buf, int lba)
{
    buf[0] = 0x00;
    memset(buf + 1, 0xff, 10);
    buf[11] = 0x00;
    buf += 12;
    lba_to_msf(buf, lba);
    buf[3] = 0x01;
    buf += 4;
    buf += 2048;
    memset(buf, 0, 288);
}

static int
cd_read_sector_sync(IDEState *s)
{
    int ret;
    block_acct_start(blk_get_stats(s->blk), &s->acct,
                     ATAPI_SECTOR_SIZE, BLOCK_ACCT_READ);

    trace_cd_read_sector_sync(s->lba);

    switch (s->cd_sector_size) {
    case 2048:
        ret = blk_pread(s->blk, (int64_t)s->lba << ATAPI_SECTOR_BITS,
                        ATAPI_SECTOR_SIZE, s->io_buffer, 0);
        break;
    case 2352:
        ret = blk_pread(s->blk, (int64_t)s->lba << ATAPI_SECTOR_BITS,
                        ATAPI_SECTOR_SIZE, s->io_buffer + 16, 0);
        if (ret >= 0) {
            cd_data_to_raw(s->io_buffer, s->lba);
        }
        break;
    default:
        block_acct_invalid(blk_get_stats(s->blk), BLOCK_ACCT_READ);
        return -EIO;
    }

    if (ret < 0) {
        block_acct_failed(blk_get_stats(s->blk), &s->acct);
    } else {
        block_acct_done(blk_get_stats(s->blk), &s->acct);
        s->lba++;
        s->io_buffer_index = 0;
    }

    return ret;
}

static void cd_read_sector_cb(void *opaque, int ret)
{
    IDEState *s = opaque;

    trace_cd_read_sector_cb(s->lba, ret);

    if (ret < 0) {
        block_acct_failed(blk_get_stats(s->blk), &s->acct);
        ide_atapi_io_error(s, ret);
        return;
    }

    block_acct_done(blk_get_stats(s->blk), &s->acct);

    if (s->cd_sector_size == 2352) {
        cd_data_to_raw(s->io_buffer, s->lba);
    }

    s->lba++;
    s->io_buffer_index = 0;
    s->status &= ~BUSY_STAT;

    ide_atapi_cmd_reply_end(s);
}

static int cd_read_sector(IDEState *s)
{
    void *buf;

    if (s->cd_sector_size != 2048 && s->cd_sector_size != 2352) {
        block_acct_invalid(blk_get_stats(s->blk), BLOCK_ACCT_READ);
        return -EINVAL;
    }

    buf = (s->cd_sector_size == 2352) ? s->io_buffer + 16 : s->io_buffer;
    qemu_iovec_init_buf(&s->qiov, buf, ATAPI_SECTOR_SIZE);

    trace_cd_read_sector(s->lba);

    block_acct_start(blk_get_stats(s->blk), &s->acct,
                     ATAPI_SECTOR_SIZE, BLOCK_ACCT_READ);

    ide_buffered_readv(s, (int64_t)s->lba << 2, &s->qiov, 4,
                       cd_read_sector_cb, s);

    s->status |= BUSY_STAT;
    return 0;
}

void ide_atapi_cmd_ok(IDEState *s)
{
    s->error = 0;
    s->status = READY_STAT | SEEK_STAT;
    s->nsector = (s->nsector & ~7) | ATAPI_INT_REASON_IO | ATAPI_INT_REASON_CD;
    ide_transfer_stop(s);
    ide_bus_set_irq(s->bus);
}

void ide_atapi_cmd_error(IDEState *s, int sense_key, int asc)
{
    trace_ide_atapi_cmd_error(s, sense_key, asc);
    s->error = sense_key << 4;
    s->status = READY_STAT | ERR_STAT;
    s->nsector = (s->nsector & ~7) | ATAPI_INT_REASON_IO | ATAPI_INT_REASON_CD;
    s->sense_key = sense_key;
    s->asc = asc;
    ide_transfer_stop(s);
    ide_bus_set_irq(s->bus);
}

void ide_atapi_io_error(IDEState *s, int ret)
{
    if (ret == -ENOMEDIUM) {
        ide_atapi_cmd_error(s, NOT_READY,
                            ASC_MEDIUM_NOT_PRESENT);
    } else {
        ide_atapi_cmd_error(s, ILLEGAL_REQUEST,
                            ASC_LOGICAL_BLOCK_OOR);
    }
}

static uint16_t atapi_byte_count_limit(IDEState *s)
{
    uint16_t bcl;

    bcl = s->lcyl | (s->hcyl << 8);
    if (bcl == 0xffff) {
        return 0xfffe;
    }
    return bcl;
}

void ide_atapi_cmd_reply_end(IDEState *s)
{
    int byte_count_limit, size, ret;
    while (s->packet_transfer_size > 0) {
        trace_ide_atapi_cmd_reply_end(s, s->packet_transfer_size,
                                      s->elementary_transfer_size,
                                      s->io_buffer_index);

        if (s->lba != -1 && s->io_buffer_index >= s->cd_sector_size) {
            if (!s->elementary_transfer_size) {
                ret = cd_read_sector(s);
                if (ret < 0) {
                    ide_atapi_io_error(s, ret);
                }
                return;
            } else {
                ret = cd_read_sector_sync(s);
                if (ret < 0) {
                    ide_atapi_io_error(s, ret);
                    return;
                }
            }
        }
        if (s->elementary_transfer_size > 0) {
            size = s->cd_sector_size - s->io_buffer_index;
            if (size > s->elementary_transfer_size)
                size = s->elementary_transfer_size;
        } else {
            s->nsector = (s->nsector & ~7) | ATAPI_INT_REASON_IO;
            ide_bus_set_irq(s->bus);
            byte_count_limit = atapi_byte_count_limit(s);
            trace_ide_atapi_cmd_reply_end_bcl(s, byte_count_limit);
            size = s->packet_transfer_size;
            if (size > byte_count_limit) {
                if (byte_count_limit & 1)
                    byte_count_limit--;
                size = byte_count_limit;
            }
            s->lcyl = size & 0xff;
            s->hcyl = size >> 8;
            s->elementary_transfer_size = size;
            if (s->lba != -1) {
                if (size > (s->cd_sector_size - s->io_buffer_index))
                    size = (s->cd_sector_size - s->io_buffer_index);
            }
            trace_ide_atapi_cmd_reply_end_new(s, s->status);
        }
        s->packet_transfer_size -= size;
        s->elementary_transfer_size -= size;
        s->io_buffer_index += size;
        assert(size <= s->io_buffer_total_len);
        assert(s->io_buffer_index <= s->io_buffer_total_len);

        if (!ide_transfer_start_norecurse(s,
                                          s->io_buffer + s->io_buffer_index - size,
                                          size, ide_atapi_cmd_reply_end)) {
            return;
        }
    }

    trace_ide_atapi_cmd_reply_end_eot(s, s->status);
    ide_atapi_cmd_ok(s);
    ide_bus_set_irq(s->bus);
}

static void ide_atapi_cmd_reply(IDEState *s, int size, int max_size)
{
    if (size > max_size)
        size = max_size;
    s->lba = -1;
    s->packet_transfer_size = size;
    s->io_buffer_size = size;
    s->elementary_transfer_size = 0;

    if (s->atapi_dma) {
        block_acct_start(blk_get_stats(s->blk), &s->acct, size,
                         BLOCK_ACCT_READ);
        s->status = READY_STAT | SEEK_STAT | DRQ_STAT;
        ide_start_dma(s, ide_atapi_cmd_read_dma_cb);
    } else {
        s->status = READY_STAT | SEEK_STAT;
        s->io_buffer_index = 0;
        ide_atapi_cmd_reply_end(s);
    }
}

static void ide_atapi_cmd_read_pio(IDEState *s, int lba, int nb_sectors,
                                   int sector_size)
{
    assert(0 <= lba && lba < (s->nb_sectors >> 2));

    s->lba = lba;
    s->packet_transfer_size = nb_sectors * sector_size;
    s->elementary_transfer_size = 0;
    s->io_buffer_index = sector_size;
    s->cd_sector_size = sector_size;

    ide_atapi_cmd_reply_end(s);
}

static void ide_atapi_cmd_check_status(IDEState *s)
{
    trace_ide_atapi_cmd_check_status(s);
    s->error = MC_ERR | (UNIT_ATTENTION << 4);
    s->status = ERR_STAT;
    s->nsector = 0;
    ide_bus_set_irq(s->bus);
}

static void ide_atapi_cmd_read_dma_cb(void *opaque, int ret)
{
    IDEState *s = opaque;
    int data_offset, n;

    if (ret < 0) {
        if (ide_handle_rw_error(s, -ret, ide_dma_cmd_to_retry(s->dma_cmd))) {
            if (s->bus->error_status) {
                s->bus->dma->aiocb = NULL;
                return;
            }
            goto eot;
        }
    }

    if (s->io_buffer_size > 0) {
        if (s->lba != -1) {
            if (s->cd_sector_size == 2352) {
                n = 1;
                cd_data_to_raw(s->io_buffer, s->lba);
            } else {
                n = s->io_buffer_size >> 11;
            }
            s->lba += n;
        }
        s->packet_transfer_size -= s->io_buffer_size;
        if (s->bus->dma->ops->rw_buf(s->bus->dma, 1) == 0)
            goto eot;
    }

    if (s->packet_transfer_size <= 0) {
        s->status = READY_STAT | SEEK_STAT;
        s->nsector = (s->nsector & ~7) | ATAPI_INT_REASON_IO | ATAPI_INT_REASON_CD;
        ide_bus_set_irq(s->bus);
        goto eot;
    }

    s->io_buffer_index = 0;
    if (s->cd_sector_size == 2352) {
        n = 1;
        s->io_buffer_size = s->cd_sector_size;
        data_offset = 16;
    } else {
        n = s->packet_transfer_size >> 11;
        if (n > (IDE_DMA_BUF_SECTORS / 4))
            n = (IDE_DMA_BUF_SECTORS / 4);
        s->io_buffer_size = n * 2048;
        data_offset = 0;
    }
    trace_ide_atapi_cmd_read_dma_cb_aio(s, s->lba, n);
    qemu_iovec_init_buf(&s->bus->dma->qiov, s->io_buffer + data_offset,
                        n * ATAPI_SECTOR_SIZE);

    s->bus->dma->aiocb = ide_buffered_readv(s, (int64_t)s->lba << 2,
                                            &s->bus->dma->qiov, n * 4,
                                            ide_atapi_cmd_read_dma_cb, s);
    return;

eot:
    if (ret < 0) {
        block_acct_failed(blk_get_stats(s->blk), &s->acct);
    } else {
        block_acct_done(blk_get_stats(s->blk), &s->acct);
    }
    ide_set_inactive(s, false);
}

static void ide_atapi_cmd_read_dma(IDEState *s, int lba, int nb_sectors,
                                   int sector_size)
{
    assert(0 <= lba && lba < (s->nb_sectors >> 2));

    s->lba = lba;
    s->packet_transfer_size = nb_sectors * sector_size;
    s->io_buffer_size = 0;
    s->cd_sector_size = sector_size;

    block_acct_start(blk_get_stats(s->blk), &s->acct, s->packet_transfer_size,
                     BLOCK_ACCT_READ);

    s->status = READY_STAT | SEEK_STAT | DRQ_STAT | BUSY_STAT;
    ide_start_dma(s, ide_atapi_cmd_read_dma_cb);
}

static void ide_atapi_cmd_read(IDEState *s, int lba, int nb_sectors,
                               int sector_size)
{
    trace_ide_atapi_cmd_read(s, s->atapi_dma ? "dma" : "pio",
                             lba, nb_sectors);
    if (s->atapi_dma) {
        ide_atapi_cmd_read_dma(s, lba, nb_sectors, sector_size);
    } else {
        ide_atapi_cmd_read_pio(s, lba, nb_sectors, sector_size);
    }
}

void ide_atapi_dma_restart(IDEState *s)
{
    s->unit = s->bus->retry_unit;
    s->bus->dma->ops->restart_dma(s->bus->dma);
    ide_atapi_cmd(s);
}

static inline uint8_t ide_atapi_set_profile(uint8_t *buf, uint8_t *index,
                                            uint16_t profile)
{
    uint8_t *buf_profile = buf + 12;

    buf_profile += ((*index) * 4);
    stw_be_p(buf_profile, profile);
    buf_profile[2] = ((buf_profile[0] == buf[6]) && (buf_profile[1] == buf[7]));

    (*index)++;
    buf[11] += 4;

    return 4;
}

static int ide_dvd_read_structure(IDEState *s, int format,
                                  const uint8_t *packet, uint8_t *buf)
{
    switch (format) {
        case 0x0:
            {
                int layer = packet[6];
                uint64_t total_sectors;

                if (layer != 0)
                    return -ASC_INV_FIELD_IN_CMD_PACKET;

                total_sectors = s->nb_sectors >> 2;
                if (total_sectors == 0) {
                    return -ASC_MEDIUM_NOT_PRESENT;
                }

                buf[4] = 1;
                buf[5] = 0xf;
                buf[6] = 1;
                buf[7] = 0;

                stl_be_p(buf + 8, 0);
                stl_be_p(buf + 12, total_sectors - 1);
                stl_be_p(buf + 16, total_sectors - 1);

                stw_be_p(buf, 2048 + 2);

                return (2048 + 4);
            }

        case 0x01:
            buf[4] = 0;
            buf[5] = 0;

            stw_be_p(buf, 4 + 2);

            return (4 + 4);

        case 0x03:
            return -ASC_INV_FIELD_IN_CMD_PACKET;

        case 0x04:
            stw_be_p(buf, 2048 + 2);

            return (2048 + 4);

        case 0xff:

            buf[4] = 0x00;
            buf[5] = 0x40;
            stw_be_p(buf + 6, 2048 + 4);

            buf[8] = 0x01;
            buf[9] = 0x40;
            stw_be_p(buf + 10, 4 + 4);

            buf[12] = 0x03;
            buf[13] = 0x40;
            stw_be_p(buf + 14, 188 + 4);

            buf[16] = 0x04;
            buf[17] = 0x40;
            stw_be_p(buf + 18, 2048 + 4);

            stw_be_p(buf, 16 + 2);

            return (16 + 4);

        default:
            return -ASC_INV_FIELD_IN_CMD_PACKET;
    }
}

static unsigned int event_status_media(IDEState *s,
                                       uint8_t *buf)
{
    uint8_t event_code, media_status;

    media_status = 0;
    if (s->tray_open) {
        media_status = MS_TRAY_OPEN;
    } else if (blk_is_inserted(s->blk)) {
        media_status = MS_MEDIA_PRESENT;
    }

    event_code = MEC_NO_CHANGE;
    if (media_status != MS_TRAY_OPEN) {
        if (s->events.new_media) {
            event_code = MEC_NEW_MEDIA;
            s->events.new_media = false;
        } else if (s->events.eject_request) {
            event_code = MEC_EJECT_REQUESTED;
            s->events.eject_request = false;
        }
    }

    buf[4] = event_code;
    buf[5] = media_status;

    buf[6] = 0;
    buf[7] = 0;

    return 8;
}

static bool validate_bcl(IDEState *s)
{
    if (s->atapi_dma || atapi_byte_count_limit(s)) {
        return true;
    }

    ide_abort_command(s);
    return false;
}

static void cmd_get_event_status_notification(IDEState *s,
                                              uint8_t *buf)
{
    const uint8_t *packet = buf;

    struct {
        uint8_t opcode;
        uint8_t polled;
        uint8_t reserved2[2];
        uint8_t class;
        uint8_t reserved3[2];
        uint16_t len;
        uint8_t control;
    } QEMU_PACKED *gesn_cdb;

    struct {
        uint16_t len;
        uint8_t notification_class;
        uint8_t supported_events;
    } QEMU_PACKED *gesn_event_header;
    unsigned int max_len, used_len;

    gesn_cdb = (void *)packet;
    gesn_event_header = (void *)buf;

    max_len = be16_to_cpu(gesn_cdb->len);

    if (!(gesn_cdb->polled & 0x01)) {
        ide_atapi_cmd_error(s, ILLEGAL_REQUEST,
                            ASC_INV_FIELD_IN_CMD_PACKET);
        return;
    }

    gesn_event_header->supported_events = 1 << GESN_MEDIA;

    gesn_event_header->notification_class = 0;

    if (gesn_cdb->class & (1 << GESN_MEDIA)) {
        gesn_event_header->notification_class |= GESN_MEDIA;
        used_len = event_status_media(s, buf);
    } else {
        gesn_event_header->notification_class = 0x80;
        used_len = sizeof(*gesn_event_header);
    }
    gesn_event_header->len = cpu_to_be16(used_len
                                         - sizeof(*gesn_event_header));
    ide_atapi_cmd_reply(s, used_len, max_len);
}

static void cmd_request_sense(IDEState *s, uint8_t *buf)
{
    int max_len = buf[4];

    memset(buf, 0, 18);
    buf[0] = 0x70 | (1 << 7);
    buf[2] = s->sense_key;
    buf[7] = 10;
    buf[12] = s->asc;

    if (s->sense_key == UNIT_ATTENTION) {
        s->sense_key = NO_SENSE;
    }

    ide_atapi_cmd_reply(s, 18, max_len);
}

static void cmd_inquiry(IDEState *s, uint8_t *buf)
{
    uint8_t page_code = buf[2];
    int max_len = buf[4];

    unsigned idx = 0;
    unsigned size_idx;
    unsigned preamble_len;

    if (buf[1] & 0x01) {
        preamble_len = 4;
        size_idx = 3;

        buf[idx++] = 0x05;
        buf[idx++] = page_code;
        buf[idx++] = 0x00;
        idx++;

        switch (page_code) {
        case 0x00:
            buf[idx++] = 0x00;
            buf[idx++] = 0x83;
            break;

        case 0x83:

            if (idx + 24 > max_len) {
                ide_atapi_cmd_error(s, ILLEGAL_REQUEST,
                                    ASC_DATA_PHASE_ERROR);
                return;
            }
            buf[idx++] = 0x02;
            buf[idx++] = 0x00;
            buf[idx++] = 0x00;
            buf[idx++] = 20;
            padstr8(buf + idx, 20, s->drive_serial_str);
            idx += 20;

            if (idx + 72 > max_len) {
                goto out;
            }
            buf[idx++] = 0x02;
            buf[idx++] = 0x01;
            buf[idx++] = 0x00;
            buf[idx++] = 68;
            padstr8(buf + idx, 8, "ATA");
            idx += 8;
            padstr8(buf + idx, 40, s->drive_model_str);
            idx += 40;
            padstr8(buf + idx, 20, s->drive_serial_str);
            idx += 20;

            if (s->wwn && (idx + 12 <= max_len)) {
                buf[idx++] = 0x01;
                buf[idx++] = 0x03;
                buf[idx++] = 0x00;
                buf[idx++] = 0x08;
                stq_be_p(&buf[idx], s->wwn);
                idx += 8;
            }
            break;

        default:
            ide_atapi_cmd_error(s, ILLEGAL_REQUEST,
                                ASC_INV_FIELD_IN_CMD_PACKET);
            return;
        }
    } else {
        preamble_len = 5;
        size_idx = 4;

        buf[0] = 0x05;
        buf[1] = 0x80;
        buf[2] = 0x00;
        buf[3] = 0x21;
        buf[5] = 0;
        buf[6] = 0;
        buf[7] = 0;
        padstr8(buf + 8, 8, "QEMU");
        padstr8(buf + 16, 16, "QEMU DVD-ROM");
        padstr8(buf + 32, 4, s->version);
        idx = 36;
    }

 out:
    buf[size_idx] = idx - preamble_len;
    ide_atapi_cmd_reply(s, idx, max_len);
}

static void cmd_get_configuration(IDEState *s, uint8_t *buf)
{
    uint32_t len;
    uint8_t index = 0;
    int max_len;

    if (buf[2] != 0 || buf[3] != 0) {
        ide_atapi_cmd_error(s, ILLEGAL_REQUEST,
                            ASC_INV_FIELD_IN_CMD_PACKET);
        return;
    }

    max_len = lduw_be_p(buf + 7);

    if (max_len > BDRV_SECTOR_SIZE) {
        max_len = BDRV_SECTOR_SIZE;
    }

    memset(buf, 0, max_len);
    if (media_is_dvd(s)) {
        stw_be_p(buf + 6, MMC_PROFILE_DVD_ROM);
    } else if (media_is_cd(s)) {
        stw_be_p(buf + 6, MMC_PROFILE_CD_ROM);
    }

    buf[10] = 0x02 | 0x01;
    len = 12;
    len += ide_atapi_set_profile(buf, &index, MMC_PROFILE_DVD_ROM);
    len += ide_atapi_set_profile(buf, &index, MMC_PROFILE_CD_ROM);
    stl_be_p(buf, len - 4);

    ide_atapi_cmd_reply(s, len, max_len);
}

static void cmd_mode_sense(IDEState *s, uint8_t *buf)
{
    int action, code;
    int max_len;

    max_len = lduw_be_p(buf + 7);
    action = buf[2] >> 6;
    code = buf[2] & 0x3f;

    switch(action) {
    case 0:
        switch(code) {
        case MODE_PAGE_R_W_ERROR:
            stw_be_p(&buf[0], 16 - 2);
            buf[2] = 0x70;
            buf[3] = 0;
            buf[4] = 0;
            buf[5] = 0;
            buf[6] = 0;
            buf[7] = 0;

            buf[8] = MODE_PAGE_R_W_ERROR;
            buf[9] = 16 - 10;
            buf[10] = 0x00;
            buf[11] = 0x05;
            buf[12] = 0x00;
            buf[13] = 0x00;
            buf[14] = 0x00;
            buf[15] = 0x00;
            ide_atapi_cmd_reply(s, 16, max_len);
            break;
        case MODE_PAGE_AUDIO_CTL:
            stw_be_p(&buf[0], 24 - 2);
            buf[2] = 0x70;
            buf[3] = 0;
            buf[4] = 0;
            buf[5] = 0;
            buf[6] = 0;
            buf[7] = 0;

            buf[8] = MODE_PAGE_AUDIO_CTL;
            buf[9] = 24 - 10;
            buf[17] = 0;
            buf[19] = 0;
            buf[21] = 0;
            buf[23] = 0;

            ide_atapi_cmd_reply(s, 24, max_len);
            break;
        case MODE_PAGE_CAPABILITIES:
            stw_be_p(&buf[0], 30 - 2);
            buf[2] = 0x70;
            buf[3] = 0;
            buf[4] = 0;
            buf[5] = 0;
            buf[6] = 0;
            buf[7] = 0;

            buf[8] = MODE_PAGE_CAPABILITIES;
            buf[9] = 30 - 10;
            buf[10] = 0x3b;
            buf[11] = 0x00;

            buf[12] = 0x71;
            buf[13] = 3 << 5;
            buf[14] = (1 << 0) | (1 << 3) | (1 << 5);
            if (s->tray_locked) {
                buf[14] |= 1 << 1;
            }
            buf[15] = 0x00;
            stw_be_p(&buf[16], 704);
            buf[18] = 0;
            buf[19] = 2;
            stw_be_p(&buf[20], 512);
            stw_be_p(&buf[22], 704);
            buf[24] = 0;
            buf[25] = 0;
            buf[26] = 0;
            buf[27] = 0;
            buf[28] = 0;
            buf[29] = 0;
            ide_atapi_cmd_reply(s, 30, max_len);
            break;
        default:
            goto error_cmd;
        }
        break;
    case 1:
        goto error_cmd;
    case 2:
        goto error_cmd;
    default:
    case 3:
        ide_atapi_cmd_error(s, ILLEGAL_REQUEST,
                            ASC_SAVING_PARAMETERS_NOT_SUPPORTED);
        break;
    }
    return;

error_cmd:
    ide_atapi_cmd_error(s, ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET);
}

static void cmd_test_unit_ready(IDEState *s, uint8_t *buf)
{
    ide_atapi_cmd_ok(s);
}

static void cmd_prevent_allow_medium_removal(IDEState *s, uint8_t* buf)
{
    s->tray_locked = buf[4] & 1;
    blk_lock_medium(s->blk, buf[4] & 1);
    ide_atapi_cmd_ok(s);
}

static void cmd_read(IDEState *s, uint8_t* buf)
{
    unsigned int nb_sectors, lba;

    uint64_t total_sectors = s->nb_sectors >> 2;

    if (buf[0] == GPCMD_READ_10) {
        nb_sectors = lduw_be_p(buf + 7);
    } else {
        nb_sectors = ldl_be_p(buf + 6);
    }
    if (nb_sectors == 0) {
        ide_atapi_cmd_ok(s);
        return;
    }

    lba = ldl_be_p(buf + 2);
    if (lba >= total_sectors || lba + nb_sectors - 1 >= total_sectors) {
        ide_atapi_cmd_error(s, ILLEGAL_REQUEST, ASC_LOGICAL_BLOCK_OOR);
        return;
    }

    ide_atapi_cmd_read(s, lba, nb_sectors, 2048);
}

static void cmd_read_cd(IDEState *s, uint8_t* buf)
{
    unsigned int nb_sectors, lba, transfer_request;

    uint64_t total_sectors = s->nb_sectors >> 2;

    nb_sectors = (buf[6] << 16) | (buf[7] << 8) | buf[8];
    if (nb_sectors == 0) {
        ide_atapi_cmd_ok(s);
        return;
    }

    lba = ldl_be_p(buf + 2);
    if (lba >= total_sectors || lba + nb_sectors - 1 >= total_sectors) {
        ide_atapi_cmd_error(s, ILLEGAL_REQUEST, ASC_LOGICAL_BLOCK_OOR);
        return;
    }

    transfer_request = buf[9] & 0xf8;
    if (transfer_request == 0x00) {
        ide_atapi_cmd_ok(s);
        return;
    }

    if (!validate_bcl(s)) {
        return;
    }

    switch (transfer_request) {
    case 0x10:
        ide_atapi_cmd_read(s, lba, nb_sectors, 2048);
        break;
    case 0xf8:
        ide_atapi_cmd_read(s, lba, nb_sectors, 2352);
        break;
    default:
        ide_atapi_cmd_error(s, ILLEGAL_REQUEST,
                            ASC_INV_FIELD_IN_CMD_PACKET);
        break;
    }
}

static void cmd_seek(IDEState *s, uint8_t* buf)
{
    unsigned int lba;
    uint64_t total_sectors = s->nb_sectors >> 2;

    lba = ldl_be_p(buf + 2);
    if (lba >= total_sectors) {
        ide_atapi_cmd_error(s, ILLEGAL_REQUEST, ASC_LOGICAL_BLOCK_OOR);
        return;
    }

    ide_atapi_cmd_ok(s);
}

static void cmd_start_stop_unit(IDEState *s, uint8_t* buf)
{
    int sense;
    bool start = buf[4] & 1;
    bool loej = buf[4] & 2;
    int pwrcnd = buf[4] & 0xf0;

    if (pwrcnd) {
        ide_atapi_cmd_ok(s);
        return;
    }

    if (loej) {
        if (!start && !s->tray_open && s->tray_locked) {
            sense = blk_is_inserted(s->blk)
                ? NOT_READY : ILLEGAL_REQUEST;
            ide_atapi_cmd_error(s, sense, ASC_MEDIA_REMOVAL_PREVENTED);
            return;
        }

        if (s->tray_open != !start) {
            blk_eject(s->blk, !start);
            s->tray_open = !start;
        }
    }

    ide_atapi_cmd_ok(s);
}

static void cmd_mechanism_status(IDEState *s, uint8_t* buf)
{
    int max_len = lduw_be_p(buf + 8);

    stw_be_p(buf, 0);
    buf[2] = 0;
    buf[3] = 0;
    buf[4] = 0;
    buf[5] = 1;
    stw_be_p(buf + 6, 0);
    ide_atapi_cmd_reply(s, 8, max_len);
}

static void cmd_read_toc_pma_atip(IDEState *s, uint8_t* buf)
{
    int format, msf, start_track, len;
    int max_len;
    uint64_t total_sectors = s->nb_sectors >> 2;

    max_len = lduw_be_p(buf + 7);
    format = buf[9] >> 6;
    msf = (buf[1] >> 1) & 1;
    start_track = buf[6];

    switch(format) {
    case 0:
        len = cdrom_read_toc(total_sectors, buf, msf, start_track);
        if (len < 0)
            goto error_cmd;
        ide_atapi_cmd_reply(s, len, max_len);
        break;
    case 1:
        memset(buf, 0, 12);
        buf[1] = 0x0a;
        buf[2] = 0x01;
        buf[3] = 0x01;
        ide_atapi_cmd_reply(s, 12, max_len);
        break;
    case 2:
        len = cdrom_read_toc_raw(total_sectors, buf, msf, start_track);
        if (len < 0)
            goto error_cmd;
        ide_atapi_cmd_reply(s, len, max_len);
        break;
    default:
    error_cmd:
        ide_atapi_cmd_error(s, ILLEGAL_REQUEST,
                            ASC_INV_FIELD_IN_CMD_PACKET);
    }
}

static void cmd_read_cdvd_capacity(IDEState *s, uint8_t* buf)
{
    uint64_t total_sectors = s->nb_sectors >> 2;

    stl_be_p(buf, total_sectors - 1);
    stl_be_p(buf + 4, 2048);
    ide_atapi_cmd_reply(s, 8, 8);
}

static void cmd_read_disc_information(IDEState *s, uint8_t* buf)
{
    uint8_t type = buf[1] & 7;
    uint32_t max_len = lduw_be_p(buf + 7);

    if (type != 0) {
        ide_atapi_cmd_error(s, ILLEGAL_REQUEST,
                            ASC_INV_FIELD_IN_CMD_PACKET);
        return;
    }

    memset(buf, 0, 34);
    buf[1] = 32;
    buf[2] = 0xe;
    buf[3] = 1;
    buf[4] = 1;
    buf[5] = 1;
    buf[6] = 1;
    buf[7] = 0x20;
    buf[8] = 0x00;

    ide_atapi_cmd_reply(s, 34, max_len);
}

static void cmd_read_dvd_structure(IDEState *s, uint8_t* buf)
{
    int max_len;
    int media = buf[1];
    int format = buf[7];
    int ret;

    max_len = lduw_be_p(buf + 8);

    if (format < 0xff) {
        if (media_is_cd(s)) {
            ide_atapi_cmd_error(s, ILLEGAL_REQUEST,
                                ASC_INCOMPATIBLE_FORMAT);
            return;
        } else if (!media_present(s)) {
            ide_atapi_cmd_error(s, ILLEGAL_REQUEST,
                                ASC_INV_FIELD_IN_CMD_PACKET);
            return;
        }
    }

    memset(buf, 0, max_len > IDE_DMA_BUF_SECTORS * BDRV_SECTOR_SIZE + 4 ?
           IDE_DMA_BUF_SECTORS * BDRV_SECTOR_SIZE + 4 : max_len);

    switch (format) {
        case 0x00 ... 0x7f:
        case 0xff:
            if (media == 0) {
                ret = ide_dvd_read_structure(s, format, buf, buf);

                if (ret < 0) {
                    ide_atapi_cmd_error(s, ILLEGAL_REQUEST, -ret);
                } else {
                    ide_atapi_cmd_reply(s, ret, max_len);
                }

                break;
            }
            /* TODO: BD support, fall through for now */

        case 0x80:
        case 0x81:
        case 0x82:
        case 0x83:
        case 0x90:
        case 0xc0:
        default:
            ide_atapi_cmd_error(s, ILLEGAL_REQUEST,
                                ASC_INV_FIELD_IN_CMD_PACKET);
            break;
    }
}

static void cmd_set_speed(IDEState *s, uint8_t* buf)
{
    ide_atapi_cmd_ok(s);
}

enum {
    ALLOW_UA = 0x01,

    CHECK_READY = 0x02,

    NONDATA = 0x04,

    CONDDATA = 0x08,
};

static const struct AtapiCmd {
    void (*handler)(IDEState *s, uint8_t *buf);
    int flags;
} atapi_cmd_table[0x100] = {
    [ 0x00 ] = { cmd_test_unit_ready,               CHECK_READY | NONDATA },
    [ 0x03 ] = { cmd_request_sense,                 ALLOW_UA },
    [ 0x12 ] = { cmd_inquiry,                       ALLOW_UA },
    [ 0x1b ] = { cmd_start_stop_unit,               NONDATA },
    [ 0x1e ] = { cmd_prevent_allow_medium_removal,  NONDATA },
    [ 0x25 ] = { cmd_read_cdvd_capacity,            CHECK_READY },
    [ 0x28 ] = { cmd_read,               CHECK_READY },
    [ 0x2b ] = { cmd_seek,                          CHECK_READY | NONDATA },
    [ 0x43 ] = { cmd_read_toc_pma_atip,             CHECK_READY },
    [ 0x46 ] = { cmd_get_configuration,             ALLOW_UA },
    [ 0x4a ] = { cmd_get_event_status_notification, ALLOW_UA },
    [ 0x51 ] = { cmd_read_disc_information,         CHECK_READY },
    [ 0x5a ] = { cmd_mode_sense,         0 },
    [ 0xa8 ] = { cmd_read,               CHECK_READY },
    [ 0xad ] = { cmd_read_dvd_structure,            CHECK_READY },
    [ 0xbb ] = { cmd_set_speed,                     NONDATA },
    [ 0xbd ] = { cmd_mechanism_status,              0 },
    [ 0xbe ] = { cmd_read_cd,                       CHECK_READY | CONDDATA },
};

void ide_atapi_cmd(IDEState *s)
{
    uint8_t *buf = s->io_buffer;
    const struct AtapiCmd *cmd = &atapi_cmd_table[s->io_buffer[0]];

    trace_ide_atapi_cmd(s, s->io_buffer[0]);

    if (trace_event_get_state_backends(TRACE_IDE_ATAPI_CMD_PACKET)) {
        g_autoptr(GString) str =
            qemu_hexdump_line(NULL, buf, ATAPI_PACKET_SIZE, 1, 0);
        trace_ide_atapi_cmd_packet(s, s->lcyl | (s->hcyl << 8), str->str);
    }

    if (s->sense_key == UNIT_ATTENTION && !(cmd->flags & ALLOW_UA)) {
        ide_atapi_cmd_check_status(s);
        return;
    }
    if (!(cmd->flags & ALLOW_UA) &&
        !s->tray_open && blk_is_inserted(s->blk) && s->cdrom_changed) {

        if (s->cdrom_changed == 1) {
            ide_atapi_cmd_error(s, NOT_READY, ASC_MEDIUM_NOT_PRESENT);
            s->cdrom_changed = 2;
        } else {
            ide_atapi_cmd_error(s, UNIT_ATTENTION, ASC_MEDIUM_MAY_HAVE_CHANGED);
            s->cdrom_changed = 0;
        }

        return;
    }

    if ((cmd->flags & CHECK_READY) &&
        (!media_present(s) || !blk_is_inserted(s->blk)))
    {
        ide_atapi_cmd_error(s, NOT_READY, ASC_MEDIUM_NOT_PRESENT);
        return;
    }

    if (cmd->handler && !(cmd->flags & (NONDATA | CONDDATA))) {
        if (!validate_bcl(s)) {
            return;
        }
    }

    if (cmd->handler) {
        cmd->handler(s, buf);
        return;
    }

    ide_atapi_cmd_error(s, ILLEGAL_REQUEST, ASC_ILLEGAL_OPCODE);
}
