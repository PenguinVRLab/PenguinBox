/*
 * Block driver for Hyper-V VHDX Images
 *
 * Copyright (c) 2013 Red Hat, Inc.,
 *
 * Authors:
 *  Jeff Cody <jcody@redhat.com>
 *
 *  This is based on the "VHDX Format Specification v1.00", published 8/25/2012
 *  by Microsoft:
 *      https://www.microsoft.com/en-us/download/details.aspx?id=34750
 *
 * This file covers the functionality of the metadata log writing, parsing, and
 * replay.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "block/block-io.h"
#include "block/block_int.h"
#include "qemu/error-report.h"
#include "qemu/bswap.h"
#include "qemu/memalign.h"
#include "vhdx.h"


typedef struct VHDXLogSequence {
    bool valid;
    uint32_t count;
    VHDXLogEntries log;
    VHDXLogEntryHeader hdr;
} VHDXLogSequence;

typedef struct VHDXLogDescEntries {
    VHDXLogEntryHeader hdr;
    VHDXLogDescriptor desc[];
} VHDXLogDescEntries;

static const MSGUID zero_guid = { 0 };

static int GRAPH_RDLOCK
vhdx_log_peek_hdr(BlockDriverState *bs, VHDXLogEntries *log,
                  VHDXLogEntryHeader *hdr)
{
    int ret = 0;
    uint64_t offset;
    uint32_t read;

    assert(hdr != NULL);

    if (log->read % VHDX_LOG_SECTOR_SIZE) {
        ret = -EFAULT;
        goto exit;
    }

    read = log->read;
    if ((read + sizeof(VHDXLogEntryHeader)) > log->length) {
        read = 0;
    }

    if (read == log->write) {
        ret = -EINVAL;
        goto exit;
    }

    offset = log->offset + read;

    ret = bdrv_pread(bs->file, offset, sizeof(VHDXLogEntryHeader), hdr, 0);
    if (ret < 0) {
        goto exit;
    }
    vhdx_log_entry_hdr_le_import(hdr);

exit:
    return ret;
}

static int vhdx_log_inc_idx(uint32_t idx, uint64_t length)
{
    idx += VHDX_LOG_SECTOR_SIZE;
    return idx >= length ? 0 : idx;
}


static void GRAPH_RDLOCK vhdx_log_reset(BlockDriverState *bs, BDRVVHDXState *s)
{
    MSGUID guid = { 0 };
    s->log.read = s->log.write = 0;
    vhdx_update_headers(bs, s, false, &guid);
}

static int GRAPH_RDLOCK
vhdx_log_read_sectors(BlockDriverState *bs, VHDXLogEntries *log,
                      uint32_t *sectors_read, void *buffer,
                      uint32_t num_sectors, bool peek)
{
    int ret = 0;
    uint64_t offset;
    uint32_t read;

    read = log->read;

    *sectors_read = 0;
    while (num_sectors) {
        if (read == log->write) {
            break;
        }
        offset = log->offset + read;

        ret = bdrv_pread(bs->file, offset, VHDX_LOG_SECTOR_SIZE, buffer, 0);
        if (ret < 0) {
            goto exit;
        }
        read = vhdx_log_inc_idx(read, log->length);

        *sectors_read = *sectors_read + 1;
        num_sectors--;
    }

exit:
    if (!peek) {
        log->read = read;
    }
    return ret;
}

static int coroutine_fn GRAPH_RDLOCK
vhdx_log_write_sectors(BlockDriverState *bs, VHDXLogEntries *log,
                       uint32_t *sectors_written, void *buffer,
                       uint32_t num_sectors)
{
    int ret = 0;
    uint64_t offset;
    uint32_t write;
    void *buffer_tmp;
    BDRVVHDXState *s = bs->opaque;

    ret = vhdx_user_visible_write(bs, s);
    if (ret < 0) {
        goto exit;
    }

    write = log->write;

    buffer_tmp = buffer;
    while (num_sectors) {

        offset = log->offset + write;
        write = vhdx_log_inc_idx(write, log->length);
        if (write == log->read) {
            break;
        }
        ret = bdrv_co_pwrite(bs->file, offset, VHDX_LOG_SECTOR_SIZE, buffer_tmp, 0);
        if (ret < 0) {
            goto exit;
        }
        buffer_tmp += VHDX_LOG_SECTOR_SIZE;

        log->write = write;
        *sectors_written = *sectors_written + 1;
        num_sectors--;
    }

exit:
    return ret;
}


static bool vhdx_log_hdr_is_valid(VHDXLogEntries *log, VHDXLogEntryHeader *hdr,
                                  BDRVVHDXState *s)
{
    int valid = false;

    if (hdr->signature != VHDX_LOG_SIGNATURE) {
        goto exit;
    }

    if (log->length < hdr->entry_length) {
        goto exit;
    }

    if (hdr->entry_length % (VHDX_LOG_SECTOR_SIZE)) {
        goto exit;
    }

    if (hdr->sequence_number == 0) {
        goto exit;
    }

    if (!guid_eq(hdr->log_guid, s->headers[s->curr_header]->log_guid)) {
        goto exit;
    }

    if (hdr->descriptor_count * sizeof(VHDXLogDescriptor) > hdr->entry_length) {
        goto exit;
    }

    valid = true;

exit:
    return valid;
}

static bool vhdx_log_desc_is_valid(VHDXLogDescriptor *desc,
                                   VHDXLogEntryHeader *hdr)
{
    bool ret = false;

    if (desc->sequence_number != hdr->sequence_number) {
        goto exit;
    }
    if (desc->file_offset % VHDX_LOG_SECTOR_SIZE) {
        goto exit;
    }

    if (desc->signature == VHDX_LOG_ZERO_SIGNATURE) {
        if (desc->zero_length % VHDX_LOG_SECTOR_SIZE == 0) {
            ret = true;
        }
    } else if (desc->signature == VHDX_LOG_DESC_SIGNATURE) {
            ret = true;
    }

exit:
    return ret;
}


static int vhdx_compute_desc_sectors(uint32_t desc_cnt)
{
    uint32_t desc_sectors;

    desc_cnt += 2;
    desc_sectors = desc_cnt / 128;
    if (desc_cnt % 128) {
        desc_sectors++;
    }

    return desc_sectors;
}


static int GRAPH_RDLOCK
vhdx_log_read_desc(BlockDriverState *bs, BDRVVHDXState *s, VHDXLogEntries *log,
                   VHDXLogDescEntries **buffer, bool convert_endian)
{
    int ret = 0;
    uint32_t desc_sectors;
    uint32_t sectors_read;
    VHDXLogEntryHeader hdr;
    VHDXLogDescEntries *desc_entries = NULL;
    VHDXLogDescriptor desc;
    int i;

    assert(*buffer == NULL);

    ret = vhdx_log_peek_hdr(bs, log, &hdr);
    if (ret < 0) {
        goto exit;
    }

    if (vhdx_log_hdr_is_valid(log, &hdr, s) == false) {
        ret = -EINVAL;
        goto exit;
    }

    desc_sectors = vhdx_compute_desc_sectors(hdr.descriptor_count);
    desc_entries = qemu_try_blockalign(bs->file->bs,
                                       desc_sectors * VHDX_LOG_SECTOR_SIZE);
    if (desc_entries == NULL) {
        ret = -ENOMEM;
        goto exit;
    }

    ret = vhdx_log_read_sectors(bs, log, &sectors_read, desc_entries,
                                desc_sectors, false);
    if (ret < 0) {
        goto free_and_exit;
    }
    if (sectors_read != desc_sectors) {
        ret = -EINVAL;
        goto free_and_exit;
    }

    for (i = 0; i < hdr.descriptor_count; i++) {
        desc = desc_entries->desc[i];
        vhdx_log_desc_le_import(&desc);
        if (convert_endian) {
            desc_entries->desc[i] = desc;
        }
        if (vhdx_log_desc_is_valid(&desc, &hdr) == false) {
            ret = -EINVAL;
            goto free_and_exit;
        }
    }
    if (convert_endian) {
        desc_entries->hdr = hdr;
    }

    *buffer = desc_entries;
    goto exit;

free_and_exit:
    qemu_vfree(desc_entries);
exit:
    return ret;
}


static int GRAPH_RDLOCK
vhdx_log_flush_desc(BlockDriverState *bs, VHDXLogDescriptor *desc,
                    VHDXLogDataSector *data)
{
    int ret = 0;
    uint64_t seq, file_offset;
    uint32_t offset = 0;
    void *buffer = NULL;
    uint64_t count = 1;
    int i;

    buffer = qemu_blockalign(bs, VHDX_LOG_SECTOR_SIZE);

    if (desc->signature == VHDX_LOG_DESC_SIGNATURE) {
        if (data == NULL) {
            ret = -EFAULT;
            goto exit;
        }

        seq = data->sequence_high;
        seq <<= 32;
        seq |= data->sequence_low & 0xffffffff;

        if (seq != desc->sequence_number) {
            ret = -EINVAL;
            goto exit;
        }

        memcpy(buffer, &desc->leading_bytes, 8);
        offset += 8;

        memcpy(buffer+offset, data->data, 4084);
        offset += 4084;

        memcpy(buffer+offset, &desc->trailing_bytes, 4);

    } else if (desc->signature == VHDX_LOG_ZERO_SIGNATURE) {
        memset(buffer, 0, VHDX_LOG_SECTOR_SIZE);
        count = desc->zero_length / VHDX_LOG_SECTOR_SIZE;
    } else {
        error_report("Invalid VHDX log descriptor entry signature 0x%" PRIx32,
                      desc->signature);
        ret = -EINVAL;
        goto exit;
    }

    file_offset = desc->file_offset;

    for (i = 0; i < count; i++) {
        ret = bdrv_pwrite_sync(bs->file, file_offset, VHDX_LOG_SECTOR_SIZE,
                               buffer, 0);
        if (ret < 0) {
            goto exit;
        }
        file_offset += VHDX_LOG_SECTOR_SIZE;
    }

exit:
    qemu_vfree(buffer);
    return ret;
}

static int GRAPH_RDLOCK
vhdx_log_flush(BlockDriverState *bs, BDRVVHDXState *s, VHDXLogSequence *logs)
{
    int ret = 0;
    int i;
    uint32_t cnt, sectors_read;
    uint64_t new_file_size;
    void *data = NULL;
    int64_t file_length;
    VHDXLogDescEntries *desc_entries = NULL;
    VHDXLogEntryHeader hdr_tmp = { 0 };

    cnt = logs->count;

    data = qemu_blockalign(bs, VHDX_LOG_SECTOR_SIZE);

    ret = vhdx_user_visible_write(bs, s);
    if (ret < 0) {
        goto exit;
    }

    while (cnt--) {
        ret = vhdx_log_peek_hdr(bs, &logs->log, &hdr_tmp);
        if (ret < 0) {
            goto exit;
        }
        file_length = bdrv_getlength(bs->file->bs);
        if (file_length < 0) {
            ret = file_length;
            goto exit;
        }
        if (hdr_tmp.flushed_file_offset > file_length) {
            ret = -EINVAL;
            goto exit;
        }

        ret = vhdx_log_read_desc(bs, s, &logs->log, &desc_entries, true);
        if (ret < 0) {
            goto exit;
        }

        for (i = 0; i < desc_entries->hdr.descriptor_count; i++) {
            if (desc_entries->desc[i].signature == VHDX_LOG_DESC_SIGNATURE) {
                ret = vhdx_log_read_sectors(bs, &logs->log, &sectors_read,
                                            data, 1, false);
                if (ret < 0) {
                    goto exit;
                }
                if (sectors_read != 1) {
                    ret = -EINVAL;
                    goto exit;
                }
                vhdx_log_data_le_import(data);
            }

            ret = vhdx_log_flush_desc(bs, &desc_entries->desc[i], data);
            if (ret < 0) {
                goto exit;
            }
        }
        if (file_length < desc_entries->hdr.last_file_offset) {
            new_file_size = desc_entries->hdr.last_file_offset;
            if (new_file_size % (1 * MiB)) {
                new_file_size = QEMU_ALIGN_UP(new_file_size, MiB);
                if (new_file_size > INT64_MAX) {
                    ret = -EINVAL;
                    goto exit;
                }
                ret = bdrv_truncate(bs->file, new_file_size, false,
                                    PREALLOC_MODE_OFF, 0, NULL);
                if (ret < 0) {
                    goto exit;
                }
            }
        }
        qemu_vfree(desc_entries);
        desc_entries = NULL;
    }

    ret = bdrv_flush(bs);
    if (ret < 0) {
        goto exit;
    }
    vhdx_log_reset(bs, s);

exit:
    qemu_vfree(data);
    qemu_vfree(desc_entries);
    return ret;
}

static int GRAPH_RDLOCK
vhdx_validate_log_entry(BlockDriverState *bs, BDRVVHDXState *s,
                        VHDXLogEntries *log, uint64_t seq,
                        bool *valid, VHDXLogEntryHeader *entry)
{
    int ret = 0;
    VHDXLogEntryHeader hdr;
    void *buffer = NULL;
    uint32_t i, desc_sectors, total_sectors, crc;
    uint32_t sectors_read = 0;
    VHDXLogDescEntries *desc_buffer = NULL;

    *valid = false;

    ret = vhdx_log_peek_hdr(bs, log, &hdr);
    if (ret < 0) {
        goto inc_and_exit;
    }

    if (vhdx_log_hdr_is_valid(log, &hdr, s) == false) {
        goto inc_and_exit;
    }

    if (seq > 0) {
        if (hdr.sequence_number != seq + 1) {
            goto inc_and_exit;
        }
    }

    desc_sectors = vhdx_compute_desc_sectors(hdr.descriptor_count);

    total_sectors = hdr.entry_length / VHDX_LOG_SECTOR_SIZE;


    ret = vhdx_log_read_desc(bs, s, log, &desc_buffer, false);
    if (ret < 0) {
        goto free_and_exit;
    }

    crc = vhdx_checksum_calc(0xffffffff, (void *)desc_buffer,
                            desc_sectors * VHDX_LOG_SECTOR_SIZE, 4);
    crc ^= 0xffffffff;

    buffer = qemu_blockalign(bs, VHDX_LOG_SECTOR_SIZE);
    if (total_sectors > desc_sectors) {
        for (i = 0; i < total_sectors - desc_sectors; i++) {
            sectors_read = 0;
            ret = vhdx_log_read_sectors(bs, log, &sectors_read, buffer,
                                        1, false);
            if (ret < 0 || sectors_read != 1) {
                goto free_and_exit;
            }
            crc = vhdx_checksum_calc(crc, buffer, VHDX_LOG_SECTOR_SIZE, -1);
            crc ^= 0xffffffff;
        }
    }
    crc ^= 0xffffffff;
    if (crc != hdr.checksum) {
        goto free_and_exit;
    }

    *valid = true;
    *entry = hdr;
    goto free_and_exit;

inc_and_exit:
    log->read = vhdx_log_inc_idx(log->read, log->length);

free_and_exit:
    qemu_vfree(buffer);
    qemu_vfree(desc_buffer);
    return ret;
}

static int GRAPH_RDLOCK
vhdx_log_search(BlockDriverState *bs, BDRVVHDXState *s, VHDXLogSequence *logs)
{
    int ret = 0;
    uint32_t tail;
    bool seq_valid = false;
    VHDXLogSequence candidate = { 0 };
    VHDXLogEntryHeader hdr = { 0 };
    VHDXLogEntries curr_log;

    memcpy(&curr_log, &s->log, sizeof(VHDXLogEntries));
    curr_log.write = curr_log.length;
    curr_log.read = 0;


    for (;;) {
        uint64_t curr_seq = 0;
        VHDXLogSequence current = { 0 };

        tail = curr_log.read;

        ret = vhdx_validate_log_entry(bs, s, &curr_log, curr_seq,
                                      &seq_valid, &hdr);
        if (ret < 0) {
            goto exit;
        }

        if (seq_valid) {
            current.valid     = true;
            current.log       = curr_log;
            current.log.read  = tail;
            current.log.write = curr_log.read;
            current.count     = 1;
            current.hdr       = hdr;


            for (;;) {
                ret = vhdx_validate_log_entry(bs, s, &curr_log, curr_seq,
                                              &seq_valid, &hdr);
                if (ret < 0) {
                    goto exit;
                }
                if (seq_valid == false) {
                    break;
                }
                current.log.write = curr_log.read;
                current.count++;

                curr_seq = hdr.sequence_number;
            }
        }

        if (current.valid) {
            if (candidate.valid == false ||
                current.hdr.sequence_number > candidate.hdr.sequence_number) {
                candidate = current;
            }
        }

        if (curr_log.read < tail) {
            break;
        }
    }

    *logs = candidate;

    if (candidate.valid) {
        s->log.sequence = candidate.hdr.sequence_number + 1;
    }


exit:
    return ret;
}

int vhdx_parse_log(BlockDriverState *bs, BDRVVHDXState *s, bool *flushed,
                   Error **errp)
{
    int ret = 0;
    VHDXHeader *hdr;
    VHDXLogSequence logs = { 0 };

    hdr = s->headers[s->curr_header];

    *flushed = false;

    if (s->log.hdr == NULL) {
        s->log.hdr = qemu_blockalign(bs, sizeof(VHDXLogEntryHeader));
    }

    s->log.offset = hdr->log_offset;
    s->log.length = hdr->log_length;

    if (s->log.offset < VHDX_LOG_MIN_SIZE ||
        s->log.offset % VHDX_LOG_MIN_SIZE) {
        ret = -EINVAL;
        goto exit;
    }

    if (hdr->log_version != 0) {
        ret = -EINVAL;
        goto exit;
    }

    if (guid_eq(hdr->log_guid, zero_guid)) {
        goto exit;
    }

    if (hdr->log_length == 0) {
        goto exit;
    }

    if (hdr->log_length % VHDX_LOG_MIN_SIZE) {
        ret = -EINVAL;
        goto exit;
    }


    ret = vhdx_log_search(bs, s, &logs);
    if (ret < 0) {
        goto exit;
    }

    if (logs.valid) {
        if (bdrv_is_read_only(bs)) {
            bdrv_refresh_filename(bs);
            ret = -EPERM;
            error_setg(errp,
                       "VHDX image file '%s' opened read-only, but "
                       "contains a log that needs to be replayed",
                       bs->filename);
            error_append_hint(errp,  "To replay the log, run:\n"
                              "qemu-img check -r all '%s'\n",
                              bs->filename);
            goto exit;
        }
        ret = vhdx_log_flush(bs, s, &logs);
        if (ret < 0) {
            goto exit;
        }
        *flushed = true;
    }


exit:
    return ret;
}



static void vhdx_log_raw_to_le_sector(VHDXLogDescriptor *desc,
                                      VHDXLogDataSector *sector, void *data,
                                      uint64_t seq)
{
    memcpy(&desc->leading_bytes, data, 8);
    data += 8;
    desc->leading_bytes = cpu_to_le64(desc->leading_bytes);
    memcpy(sector->data, data, 4084);
    data += 4084;
    memcpy(&desc->trailing_bytes, data, 4);
    desc->trailing_bytes = cpu_to_le32(desc->trailing_bytes);
    data += 4;

    sector->sequence_high  = (uint32_t) (seq >> 32);
    sector->sequence_low   = (uint32_t) (seq & 0xffffffff);
    sector->data_signature = VHDX_LOG_DATA_SIGNATURE;

    vhdx_log_desc_le_export(desc);
    vhdx_log_data_le_export(sector);
}


static int coroutine_fn GRAPH_RDLOCK
vhdx_log_write(BlockDriverState *bs, BDRVVHDXState *s,
               void *data, uint32_t length, uint64_t offset)
{
    int ret = 0;
    void *buffer = NULL;
    void *merged_sector = NULL;
    void *data_tmp, *sector_write;
    unsigned int i;
    int sector_offset;
    uint32_t desc_sectors, sectors, total_length;
    uint32_t sectors_written = 0;
    uint32_t aligned_length;
    uint32_t leading_length = 0;
    uint32_t trailing_length = 0;
    uint32_t partial_sectors = 0;
    uint32_t bytes_written = 0;
    uint64_t file_offset;
    int64_t file_length;
    VHDXHeader *header;
    VHDXLogEntryHeader new_hdr;
    VHDXLogDescriptor *new_desc = NULL;
    VHDXLogDataSector *data_sector = NULL;
    MSGUID new_guid = { 0 };

    header = s->headers[s->curr_header];

    if (length > header->log_length) {
        ret = -EINVAL;
        goto exit;
    }

    if (guid_eq(header->log_guid, zero_guid)) {
        vhdx_guid_generate(&new_guid);
        vhdx_update_headers(bs, s, false, &new_guid);
    } else {
        ret = -ENOTSUP;
        goto exit;
    }

    if (s->log.sequence == 0) {
        s->log.sequence = 1;
    }

    sector_offset = offset % VHDX_LOG_SECTOR_SIZE;
    file_offset = QEMU_ALIGN_DOWN(offset, VHDX_LOG_SECTOR_SIZE);

    aligned_length = length;

    if (sector_offset) {
        leading_length = (VHDX_LOG_SECTOR_SIZE - sector_offset);
        leading_length = leading_length > length ? length : leading_length;
        aligned_length -= leading_length;
        partial_sectors++;
    }

    sectors = aligned_length / VHDX_LOG_SECTOR_SIZE;
    trailing_length = aligned_length - (sectors * VHDX_LOG_SECTOR_SIZE);
    if (trailing_length) {
        partial_sectors++;
    }

    sectors += partial_sectors;

    file_length = bdrv_co_getlength(bs->file->bs);
    if (file_length < 0) {
        ret = file_length;
        goto exit;
    }

    new_hdr = (VHDXLogEntryHeader) {
                .signature           = VHDX_LOG_SIGNATURE,
                .tail                = s->log.tail,
                .sequence_number     = s->log.sequence,
                .descriptor_count    = sectors,
                .reserved            = 0,
                .flushed_file_offset = file_length,
                .last_file_offset    = file_length,
                .log_guid            = header->log_guid,
              };


    desc_sectors = vhdx_compute_desc_sectors(new_hdr.descriptor_count);

    total_length = (desc_sectors + sectors) * VHDX_LOG_SECTOR_SIZE;
    new_hdr.entry_length = total_length;

    vhdx_log_entry_hdr_le_export(&new_hdr);

    buffer = qemu_blockalign(bs, total_length);
    memcpy(buffer, &new_hdr, sizeof(new_hdr));

    new_desc = buffer + sizeof(new_hdr);
    data_sector = buffer + (desc_sectors * VHDX_LOG_SECTOR_SIZE);
    data_tmp = data;

    merged_sector = qemu_blockalign(bs, VHDX_LOG_SECTOR_SIZE);

    for (i = 0; i < sectors; i++) {
        new_desc->signature       = VHDX_LOG_DESC_SIGNATURE;
        new_desc->sequence_number = s->log.sequence;
        new_desc->file_offset     = file_offset;

        if (i == 0 && leading_length) {
            ret = bdrv_co_pread(bs->file, file_offset, VHDX_LOG_SECTOR_SIZE,
                                merged_sector, 0);
            if (ret < 0) {
                goto exit;
            }
            memcpy(merged_sector + sector_offset, data_tmp, leading_length);
            bytes_written = leading_length;
            sector_write = merged_sector;
        } else if (i == sectors - 1 && trailing_length) {
            ret = bdrv_co_pread(bs->file, file_offset + trailing_length,
                                VHDX_LOG_SECTOR_SIZE - trailing_length,
                                merged_sector + trailing_length, 0);
            if (ret < 0) {
                goto exit;
            }
            memcpy(merged_sector, data_tmp, trailing_length);
            bytes_written = trailing_length;
            sector_write = merged_sector;
        } else {
            bytes_written = VHDX_LOG_SECTOR_SIZE;
            sector_write = data_tmp;
        }

        vhdx_log_raw_to_le_sector(new_desc, data_sector, sector_write,
                                  s->log.sequence);

        data_tmp += bytes_written;
        data_sector++;
        new_desc++;
        file_offset += VHDX_LOG_SECTOR_SIZE;
    }

    vhdx_update_checksum(buffer, total_length,
                         offsetof(VHDXLogEntryHeader, checksum));

    ret = vhdx_log_write_sectors(bs, &s->log, &sectors_written, buffer,
                                 desc_sectors + sectors);
    if (ret < 0) {
        goto exit;
    }

    if (sectors_written != desc_sectors + sectors) {
        ret = -EINVAL;
        goto exit;
    }

    s->log.sequence++;
    s->log.tail = s->log.write;

exit:
    qemu_vfree(buffer);
    qemu_vfree(merged_sector);
    return ret;
}

int coroutine_fn
vhdx_log_write_and_flush(BlockDriverState *bs, BDRVVHDXState *s,
                         void *data, uint32_t length, uint64_t offset)
{
    int ret = 0;
    VHDXLogSequence logs = { .valid = true,
                             .count = 1,
                             .hdr = { 0 } };


    ret = bdrv_co_flush(bs);
    if (ret < 0) {
        goto exit;
    }

    ret = vhdx_log_write(bs, s, data, length, offset);
    if (ret < 0) {
        goto exit;
    }
    logs.log = s->log;

    ret = bdrv_co_flush(bs);
    if (ret < 0) {
        goto exit;
    }

    ret = vhdx_log_flush(bs, s, &logs);
    if (ret < 0) {
        goto exit;
    }

    s->log = logs.log;

exit:
    return ret;
}

