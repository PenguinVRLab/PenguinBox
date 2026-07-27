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
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "block/block_int.h"
#include "block/qdict.h"
#include "system/block-backend.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/crc32c.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"
#include "qemu/memalign.h"
#include "vhdx.h"
#include "migration/blocker.h"
#include "qemu/uuid.h"
#include "qobject/qdict.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qapi-visit-block-core.h"

#define VHDX_BLOCK_OPT_LOG_SIZE   "log_size"
#define VHDX_BLOCK_OPT_BLOCK_SIZE "block_size"
#define VHDX_BLOCK_OPT_ZERO "block_state_zero"

typedef enum VHDXImageType {
    VHDX_TYPE_DYNAMIC = 0,
    VHDX_TYPE_FIXED,
    VHDX_TYPE_DIFFERENCING,
} VHDXImageType;

static QemuOptsList vhdx_create_opts;

static const MSGUID bat_guid =      { .data1 = 0x2dc27766,
                                      .data2 = 0xf623,
                                      .data3 = 0x4200,
                                      .data4 = { 0x9d, 0x64, 0x11, 0x5e,
                                                 0x9b, 0xfd, 0x4a, 0x08} };

static const MSGUID metadata_guid = { .data1 = 0x8b7ca206,
                                      .data2 = 0x4790,
                                      .data3 = 0x4b9a,
                                      .data4 = { 0xb8, 0xfe, 0x57, 0x5f,
                                                 0x05, 0x0f, 0x88, 0x6e} };



static const MSGUID file_param_guid =   { .data1 = 0xcaa16737,
                                          .data2 = 0xfa36,
                                          .data3 = 0x4d43,
                                          .data4 = { 0xb3, 0xb6, 0x33, 0xf0,
                                                     0xaa, 0x44, 0xe7, 0x6b} };

static const MSGUID virtual_size_guid = { .data1 = 0x2FA54224,
                                          .data2 = 0xcd1b,
                                          .data3 = 0x4876,
                                          .data4 = { 0xb2, 0x11, 0x5d, 0xbe,
                                                     0xd8, 0x3b, 0xf4, 0xb8} };

static const MSGUID page83_guid =       { .data1 = 0xbeca12ab,
                                          .data2 = 0xb2e6,
                                          .data3 = 0x4523,
                                          .data4 = { 0x93, 0xef, 0xc3, 0x09,
                                                     0xe0, 0x00, 0xc7, 0x46} };


static const MSGUID phys_sector_guid =  { .data1 = 0xcda348c7,
                                          .data2 = 0x445d,
                                          .data3 = 0x4471,
                                          .data4 = { 0x9c, 0xc9, 0xe9, 0x88,
                                                     0x52, 0x51, 0xc5, 0x56} };

static const MSGUID parent_locator_guid = { .data1 = 0xa8d35f2d,
                                            .data2 = 0xb30b,
                                            .data3 = 0x454d,
                                            .data4 = { 0xab, 0xf7, 0xd3,
                                                       0xd8, 0x48, 0x34,
                                                       0xab, 0x0c} };

static const MSGUID logical_sector_guid = { .data1 = 0x8141bf1d,
                                            .data2 = 0xa96f,
                                            .data3 = 0x4709,
                                            .data4 = { 0xba, 0x47, 0xf2,
                                                       0x33, 0xa8, 0xfa,
                                                       0xab, 0x5f} };

static const MSGUID parent_vhdx_guid __attribute__((unused))
                                     = { .data1 = 0xb04aefb7,
                                         .data2 = 0xd19e,
                                         .data3 = 0x4a81,
                                         .data4 = { 0xb7, 0x89, 0x25, 0xb8,
                                                    0xe9, 0x44, 0x59, 0x13} };


#define META_FILE_PARAMETER_PRESENT      0x01
#define META_VIRTUAL_DISK_SIZE_PRESENT   0x02
#define META_PAGE_83_PRESENT             0x04
#define META_LOGICAL_SECTOR_SIZE_PRESENT 0x08
#define META_PHYS_SECTOR_SIZE_PRESENT    0x10
#define META_PARENT_LOCATOR_PRESENT      0x20

#define META_ALL_PRESENT    \
    (META_FILE_PARAMETER_PRESENT | META_VIRTUAL_DISK_SIZE_PRESENT | \
     META_PAGE_83_PRESENT | META_LOGICAL_SECTOR_SIZE_PRESENT | \
     META_PHYS_SECTOR_SIZE_PRESENT)


typedef struct VHDXSectorInfo {
    uint32_t bat_idx;
    uint32_t sectors_avail;
    uint32_t bytes_left;
    uint32_t bytes_avail;
    uint64_t file_offset;
    uint64_t block_offset;
} VHDXSectorInfo;

uint32_t vhdx_update_checksum(uint8_t *buf, size_t size, int crc_offset)
{
    uint32_t crc;

    assert(buf != NULL);
    assert(size > (crc_offset + sizeof(crc)));

    memset(buf + crc_offset, 0, sizeof(crc));
    crc =  crc32c(0xffffffff, buf, size);
    crc = cpu_to_le32(crc);
    memcpy(buf + crc_offset, &crc, sizeof(crc));

    return crc;
}

uint32_t vhdx_checksum_calc(uint32_t crc, uint8_t *buf, size_t size,
                            int crc_offset)
{
    uint32_t crc_new;
    uint32_t crc_orig;
    assert(buf != NULL);

    if (crc_offset > 0) {
        memcpy(&crc_orig, buf + crc_offset, sizeof(crc_orig));
        memset(buf + crc_offset, 0, sizeof(crc_orig));
    }

    crc_new = crc32c(crc, buf, size);
    if (crc_offset > 0) {
        memcpy(buf + crc_offset, &crc_orig, sizeof(crc_orig));
    }

    return crc_new;
}

bool vhdx_checksum_is_valid(uint8_t *buf, size_t size, int crc_offset)
{
    uint32_t crc_orig;
    uint32_t crc;

    assert(buf != NULL);
    assert(size > (crc_offset + 4));

    memcpy(&crc_orig, buf + crc_offset, sizeof(crc_orig));
    crc_orig = le32_to_cpu(crc_orig);

    crc = vhdx_checksum_calc(0xffffffff, buf, size, crc_offset);

    return crc == crc_orig;
}


void vhdx_guid_generate(MSGUID *guid)
{
    QemuUUID uuid;
    assert(guid != NULL);

    qemu_uuid_generate(&uuid);
    memcpy(guid, &uuid, sizeof(MSGUID));
}

static int vhdx_region_check(BDRVVHDXState *s, uint64_t start, uint64_t length)
{
    int ret = 0;
    uint64_t end;
    VHDXRegionEntry *r;

    end = start + length;
    QLIST_FOREACH(r, &s->regions, entries) {
        if (!((start >= r->end) || (end <= r->start))) {
            error_report("VHDX region %" PRIu64 "-%" PRIu64 " overlaps with "
                         "region %" PRIu64 "-%." PRIu64, start, end, r->start,
                         r->end);
            ret = -EINVAL;
            goto exit;
        }
    }

exit:
    return ret;
}

static void vhdx_region_register(BDRVVHDXState *s,
                                 uint64_t start, uint64_t length)
{
    VHDXRegionEntry *r;

    r = g_malloc0(sizeof(*r));

    r->start = start;
    r->end = start + length;

    QLIST_INSERT_HEAD(&s->regions, r, entries);
}

static void vhdx_region_unregister_all(BDRVVHDXState *s)
{
    VHDXRegionEntry *r, *r_next;

    QLIST_FOREACH_SAFE(r, &s->regions, entries, r_next) {
        QLIST_REMOVE(r, entries);
        g_free(r);
    }
}

static void vhdx_set_shift_bits(BDRVVHDXState *s)
{
    s->logical_sector_size_bits = ctz32(s->logical_sector_size);
    s->sectors_per_block_bits =   ctz32(s->sectors_per_block);
    s->chunk_ratio_bits =         ctz64(s->chunk_ratio);
    s->block_size_bits =          ctz32(s->block_size);
}

static int vhdx_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    if (buf_size >= 8 && !memcmp(buf, "vhdxfile", 8)) {
        return 100;
    }
    return 0;
}

static int vhdx_write_header(BdrvChild *file, VHDXHeader *hdr,
                             uint64_t offset, bool read)
{
    BlockDriverState *bs_file = file->bs;
    uint8_t *buffer = NULL;
    int ret;
    VHDXHeader *header_le;

    assert(bs_file != NULL);
    assert(hdr != NULL);

    buffer = qemu_blockalign(bs_file, VHDX_HEADER_SIZE);
    if (read) {
        ret = bdrv_pread(file, offset, VHDX_HEADER_SIZE, buffer, 0);
        if (ret < 0) {
            goto exit;
        }
    } else {
        memset(buffer, 0, VHDX_HEADER_SIZE);
    }

    header_le = (VHDXHeader *)buffer;
    memcpy(header_le, hdr, sizeof(VHDXHeader));
    vhdx_header_le_export(hdr, header_le);
    vhdx_update_checksum(buffer, VHDX_HEADER_SIZE,
                         offsetof(VHDXHeader, checksum));
    ret = bdrv_pwrite_sync(file, offset, sizeof(VHDXHeader), header_le, 0);

exit:
    qemu_vfree(buffer);
    return ret;
}

static int GRAPH_RDLOCK
vhdx_update_header(BlockDriverState *bs, BDRVVHDXState *s,
                   bool generate_data_write_guid, MSGUID *log_guid)
{
    int ret = 0;
    int hdr_idx = 0;
    uint64_t header_offset = VHDX_HEADER1_OFFSET;

    VHDXHeader *active_header;
    VHDXHeader *inactive_header;

    if (s->curr_header == 0) {
        hdr_idx = 1;
        header_offset = VHDX_HEADER2_OFFSET;
    }

    active_header   = s->headers[s->curr_header];
    inactive_header = s->headers[hdr_idx];

    inactive_header->sequence_number = active_header->sequence_number + 1;

    inactive_header->file_write_guid = s->session_guid;

    if (generate_data_write_guid) {
        vhdx_guid_generate(&inactive_header->data_write_guid);
    }

    if (log_guid) {
        inactive_header->log_guid = *log_guid;
    }

    ret = vhdx_write_header(bs->file, inactive_header, header_offset, true);
    if (ret < 0) {
        goto exit;
    }
    s->curr_header = hdr_idx;

exit:
    return ret;
}

int vhdx_update_headers(BlockDriverState *bs, BDRVVHDXState *s,
                        bool generate_data_write_guid, MSGUID *log_guid)
{
    int ret;

    ret = vhdx_update_header(bs, s, generate_data_write_guid, log_guid);
    if (ret < 0) {
        return ret;
    }
    return vhdx_update_header(bs, s, generate_data_write_guid, log_guid);
}

static void GRAPH_RDLOCK
vhdx_parse_header(BlockDriverState *bs, BDRVVHDXState *s, Error **errp)
{
    int ret;
    VHDXHeader *header1;
    VHDXHeader *header2;
    bool h1_valid = false;
    bool h2_valid = false;
    uint64_t h1_seq = 0;
    uint64_t h2_seq = 0;
    uint8_t *buffer;

    header1 = qemu_blockalign(bs, sizeof(VHDXHeader));
    header2 = qemu_blockalign(bs, sizeof(VHDXHeader));

    buffer = qemu_blockalign(bs, VHDX_HEADER_SIZE);

    s->headers[0] = header1;
    s->headers[1] = header2;

    ret = bdrv_pread(bs->file, VHDX_HEADER1_OFFSET, VHDX_HEADER_SIZE, buffer,
                     0);
    if (ret < 0) {
        goto fail;
    }
    memcpy(header1, buffer, sizeof(VHDXHeader));

    if (vhdx_checksum_is_valid(buffer, VHDX_HEADER_SIZE, 4)) {
        vhdx_header_le_import(header1);
        if (header1->signature == VHDX_HEADER_SIGNATURE &&
            header1->version == 1) {
            h1_seq = header1->sequence_number;
            h1_valid = true;
        }
    }

    ret = bdrv_pread(bs->file, VHDX_HEADER2_OFFSET, VHDX_HEADER_SIZE, buffer,
                     0);
    if (ret < 0) {
        goto fail;
    }
    memcpy(header2, buffer, sizeof(VHDXHeader));

    if (vhdx_checksum_is_valid(buffer, VHDX_HEADER_SIZE, 4)) {
        vhdx_header_le_import(header2);
        if (header2->signature == VHDX_HEADER_SIGNATURE &&
            header2->version == 1) {
            h2_seq = header2->sequence_number;
            h2_valid = true;
        }
    }

    if (h1_valid && !h2_valid) {
        s->curr_header = 0;
    } else if (!h1_valid && h2_valid) {
        s->curr_header = 1;
    } else if (!h1_valid && !h2_valid) {
        goto fail;
    } else {
        if (h1_seq > h2_seq) {
            s->curr_header = 0;
        } else if (h2_seq > h1_seq) {
            s->curr_header = 1;
        } else {
            if (!memcmp(header1, header2, sizeof(VHDXHeader))) {
                s->curr_header = 0;
            } else {
                goto fail;
            }
        }
    }

    vhdx_region_register(s, s->headers[s->curr_header]->log_offset,
                            s->headers[s->curr_header]->log_length);
    goto exit;

fail:
    error_setg_errno(errp, -ret, "No valid VHDX header found");
    qemu_vfree(header1);
    qemu_vfree(header2);
    s->headers[0] = NULL;
    s->headers[1] = NULL;
exit:
    qemu_vfree(buffer);
}


static int GRAPH_RDLOCK
vhdx_open_region_tables(BlockDriverState *bs, BDRVVHDXState *s)
{
    int ret = 0;
    uint8_t *buffer;
    int offset = 0;
    VHDXRegionTableEntry rt_entry;
    uint32_t i;
    bool bat_rt_found = false;
    bool metadata_rt_found = false;

    buffer = qemu_blockalign(bs, VHDX_HEADER_BLOCK_SIZE);

    ret = bdrv_pread(bs->file, VHDX_REGION_TABLE_OFFSET,
                     VHDX_HEADER_BLOCK_SIZE, buffer, 0);
    if (ret < 0) {
        goto fail;
    }
    memcpy(&s->rt, buffer, sizeof(s->rt));
    offset += sizeof(s->rt);

    if (!vhdx_checksum_is_valid(buffer, VHDX_HEADER_BLOCK_SIZE, 4)) {
        ret = -EINVAL;
        goto fail;
    }

    vhdx_region_header_le_import(&s->rt);

    if (s->rt.signature != VHDX_REGION_SIGNATURE) {
        ret = -EINVAL;
        goto fail;
    }


    if (s->rt.entry_count > 2047) {
        ret = -EINVAL;
        goto fail;
    }

    for (i = 0; i < s->rt.entry_count; i++) {
        memcpy(&rt_entry, buffer + offset, sizeof(rt_entry));
        offset += sizeof(rt_entry);

        vhdx_region_entry_le_import(&rt_entry);

        ret = vhdx_region_check(s, rt_entry.file_offset, rt_entry.length);
        if (ret < 0) {
            goto fail;
        }

        vhdx_region_register(s, rt_entry.file_offset, rt_entry.length);

        if (guid_eq(rt_entry.guid, bat_guid)) {
            if (bat_rt_found) {
                ret = -EINVAL;
                goto fail;
            }
            bat_rt_found = true;
            s->bat_rt = rt_entry;
            continue;
        }

        if (guid_eq(rt_entry.guid, metadata_guid)) {
            if (metadata_rt_found) {
                ret = -EINVAL;
                goto fail;
            }
            metadata_rt_found = true;
            s->metadata_rt = rt_entry;
            continue;
        }

        if (rt_entry.data_bits & VHDX_REGION_ENTRY_REQUIRED) {
            ret = -ENOTSUP;
            goto fail;
        }
    }

    if (!bat_rt_found || !metadata_rt_found) {
        ret = -EINVAL;
        goto fail;
    }

    ret = 0;

fail:
    qemu_vfree(buffer);
    return ret;
}



static int GRAPH_RDLOCK
vhdx_parse_metadata(BlockDriverState *bs, BDRVVHDXState *s)
{
    int ret = 0;
    uint8_t *buffer;
    int offset = 0;
    uint32_t i = 0;
    VHDXMetadataTableEntry md_entry;

    buffer = qemu_blockalign(bs, VHDX_METADATA_TABLE_MAX_SIZE);

    ret = bdrv_pread(bs->file, s->metadata_rt.file_offset,
                     VHDX_METADATA_TABLE_MAX_SIZE, buffer, 0);
    if (ret < 0) {
        goto exit;
    }
    memcpy(&s->metadata_hdr, buffer, sizeof(s->metadata_hdr));
    offset += sizeof(s->metadata_hdr);

    vhdx_metadata_header_le_import(&s->metadata_hdr);

    if (s->metadata_hdr.signature != VHDX_METADATA_SIGNATURE) {
        ret = -EINVAL;
        goto exit;
    }

    s->metadata_entries.present = 0;

    if ((s->metadata_hdr.entry_count * sizeof(md_entry)) >
        (VHDX_METADATA_TABLE_MAX_SIZE - offset)) {
        ret = -EINVAL;
        goto exit;
    }

    for (i = 0; i < s->metadata_hdr.entry_count; i++) {
        memcpy(&md_entry, buffer + offset, sizeof(md_entry));
        offset += sizeof(md_entry);

        vhdx_metadata_entry_le_import(&md_entry);

        if (guid_eq(md_entry.item_id, file_param_guid)) {
            if (s->metadata_entries.present & META_FILE_PARAMETER_PRESENT) {
                ret = -EINVAL;
                goto exit;
            }
            s->metadata_entries.file_parameters_entry = md_entry;
            s->metadata_entries.present |= META_FILE_PARAMETER_PRESENT;
            continue;
        }

        if (guid_eq(md_entry.item_id, virtual_size_guid)) {
            if (s->metadata_entries.present & META_VIRTUAL_DISK_SIZE_PRESENT) {
                ret = -EINVAL;
                goto exit;
            }
            s->metadata_entries.virtual_disk_size_entry = md_entry;
            s->metadata_entries.present |= META_VIRTUAL_DISK_SIZE_PRESENT;
            continue;
        }

        if (guid_eq(md_entry.item_id, page83_guid)) {
            if (s->metadata_entries.present & META_PAGE_83_PRESENT) {
                ret = -EINVAL;
                goto exit;
            }
            s->metadata_entries.page83_data_entry = md_entry;
            s->metadata_entries.present |= META_PAGE_83_PRESENT;
            continue;
        }

        if (guid_eq(md_entry.item_id, logical_sector_guid)) {
            if (s->metadata_entries.present &
                META_LOGICAL_SECTOR_SIZE_PRESENT) {
                ret = -EINVAL;
                goto exit;
            }
            s->metadata_entries.logical_sector_size_entry = md_entry;
            s->metadata_entries.present |= META_LOGICAL_SECTOR_SIZE_PRESENT;
            continue;
        }

        if (guid_eq(md_entry.item_id, phys_sector_guid)) {
            if (s->metadata_entries.present & META_PHYS_SECTOR_SIZE_PRESENT) {
                ret = -EINVAL;
                goto exit;
            }
            s->metadata_entries.phys_sector_size_entry = md_entry;
            s->metadata_entries.present |= META_PHYS_SECTOR_SIZE_PRESENT;
            continue;
        }

        if (guid_eq(md_entry.item_id, parent_locator_guid)) {
            if (s->metadata_entries.present & META_PARENT_LOCATOR_PRESENT) {
                ret = -EINVAL;
                goto exit;
            }
            s->metadata_entries.parent_locator_entry = md_entry;
            s->metadata_entries.present |= META_PARENT_LOCATOR_PRESENT;
            continue;
        }

        if (md_entry.data_bits & VHDX_META_FLAGS_IS_REQUIRED) {
            ret = -ENOTSUP;
            goto exit;
        }
    }

    if (s->metadata_entries.present != META_ALL_PRESENT) {
        ret = -ENOTSUP;
        goto exit;
    }

    ret = bdrv_pread(bs->file,
                     s->metadata_entries.file_parameters_entry.offset
                                         + s->metadata_rt.file_offset,
                     sizeof(s->params),
                     &s->params,
                     0);

    if (ret < 0) {
        goto exit;
    }

    s->params.block_size = le32_to_cpu(s->params.block_size);
    s->params.data_bits = le32_to_cpu(s->params.data_bits);


    if (s->params.data_bits & VHDX_PARAMS_HAS_PARENT) {
        if (s->metadata_entries.present & META_PARENT_LOCATOR_PRESENT) {
            ret = -ENOTSUP;
            goto exit;
        } else {
            ret = -EINVAL;
            goto exit;
        }
    }

    ret = bdrv_pread(bs->file,
                     s->metadata_entries.virtual_disk_size_entry.offset
                                           + s->metadata_rt.file_offset,
                     sizeof(uint64_t),
                     &s->virtual_disk_size,
                     0);
    if (ret < 0) {
        goto exit;
    }
    ret = bdrv_pread(bs->file,
                     s->metadata_entries.logical_sector_size_entry.offset
                                             + s->metadata_rt.file_offset,
                     sizeof(uint32_t),
                     &s->logical_sector_size,
                     0);
    if (ret < 0) {
        goto exit;
    }
    ret = bdrv_pread(bs->file,
                     s->metadata_entries.phys_sector_size_entry.offset
                                          + s->metadata_rt.file_offset,
                     sizeof(uint32_t),
                     &s->physical_sector_size,
                     0);
    if (ret < 0) {
        goto exit;
    }

    s->virtual_disk_size = le64_to_cpu(s->virtual_disk_size);
    s->logical_sector_size = le32_to_cpu(s->logical_sector_size);
    s->physical_sector_size = le32_to_cpu(s->physical_sector_size);

    if (s->params.block_size < VHDX_BLOCK_SIZE_MIN ||
        s->params.block_size > VHDX_BLOCK_SIZE_MAX) {
        ret = -EINVAL;
        goto exit;
    }

    if (s->logical_sector_size != 512) {
        ret = -ENOTSUP;
        goto exit;
    }

    s->sectors_per_block = s->params.block_size / s->logical_sector_size;
    s->chunk_ratio = (VHDX_MAX_SECTORS_PER_BLOCK) *
                     (uint64_t)s->logical_sector_size /
                     (uint64_t)s->params.block_size;

    if (s->logical_sector_size & (s->logical_sector_size - 1)) {
        ret = -EINVAL;
        goto exit;
    }
    if (s->sectors_per_block & (s->sectors_per_block - 1)) {
        ret = -EINVAL;
        goto exit;
    }
    if (s->chunk_ratio & (s->chunk_ratio - 1)) {
        ret = -EINVAL;
        goto exit;
    }
    s->block_size = s->params.block_size;
    if (s->block_size & (s->block_size - 1)) {
        ret = -EINVAL;
        goto exit;
    }

    vhdx_set_shift_bits(s);

    ret = 0;

exit:
    qemu_vfree(buffer);
    return ret;
}

static void vhdx_calc_bat_entries(BDRVVHDXState *s)
{
    uint32_t data_blocks_cnt, bitmap_blocks_cnt;

    data_blocks_cnt = DIV_ROUND_UP(s->virtual_disk_size, s->block_size);
    bitmap_blocks_cnt = DIV_ROUND_UP(data_blocks_cnt, s->chunk_ratio);

    if (s->parent_entries) {
        s->bat_entries = bitmap_blocks_cnt * (s->chunk_ratio + 1);
    } else {
        s->bat_entries = data_blocks_cnt +
                         ((data_blocks_cnt - 1) >> s->chunk_ratio_bits);
    }

}

static int coroutine_mixed_fn GRAPH_RDLOCK
vhdx_check_bat_entries(BlockDriverState *bs, int *errcnt)
{
    BDRVVHDXState *s = bs->opaque;
    int64_t image_file_size = bdrv_getlength(bs->file->bs);
    uint64_t payblocks = s->chunk_ratio;
    uint64_t i;
    int ret = 0;

    if (image_file_size < 0) {
        error_report("Could not determinate VHDX image file size.");
        return image_file_size;
    }

    for (i = 0; i < s->bat_entries; i++) {
        if ((s->bat[i] & VHDX_BAT_STATE_BIT_MASK) ==
            PAYLOAD_BLOCK_FULLY_PRESENT) {
            uint64_t offset = s->bat[i] & VHDX_BAT_FILE_OFF_MASK;
            uint32_t block_length = MIN(s->block_size,
                bs->total_sectors * BDRV_SECTOR_SIZE - i * s->block_size);
            if (offset > INT64_MAX - s->block_size) {
                error_report("VHDX BAT entry %" PRIu64 " offset overflow.", i);
                ret = -EINVAL;
                if (!errcnt) {
                    break;
                }
                (*errcnt)++;
            }
            if (offset >= image_file_size) {
                error_report("VHDX BAT entry %" PRIu64 " start offset %" PRIu64
                             " points after end of file (%" PRIi64 "). Image"
                             " has probably been truncated.",
                             i, offset, image_file_size);
                ret = -EINVAL;
                if (!errcnt) {
                    break;
                }
                (*errcnt)++;
            } else if (offset + block_length > image_file_size) {
                error_report("VHDX BAT entry %" PRIu64 " end offset %" PRIu64
                             " points after end of file (%" PRIi64 "). Image"
                             " has probably been truncated.",
                             i, offset + block_length - 1, image_file_size);
                ret = -EINVAL;
                if (!errcnt) {
                    break;
                }
                (*errcnt)++;
            }

            if (payblocks--) {
                int ret2;
                ret2 = vhdx_region_check(s, offset, s->block_size);
                if (ret2 < 0) {
                    ret = -EINVAL;
                    if (!errcnt) {
                        break;
                    }
                    (*errcnt)++;
                }
            } else {
                payblocks = s->chunk_ratio;
            }
        }
    }

    return ret;
}

static void vhdx_close(BlockDriverState *bs)
{
    BDRVVHDXState *s = bs->opaque;
    qemu_vfree(s->headers[0]);
    s->headers[0] = NULL;
    qemu_vfree(s->headers[1]);
    s->headers[1] = NULL;
    qemu_vfree(s->bat);
    s->bat = NULL;
    qemu_vfree(s->parent_entries);
    s->parent_entries = NULL;
    migrate_del_blocker(&s->migration_blocker);
    qemu_vfree(s->log.hdr);
    s->log.hdr = NULL;
    vhdx_region_unregister_all(s);
}

static int vhdx_open(BlockDriverState *bs, QDict *options, int flags,
                     Error **errp)
{
    BDRVVHDXState *s = bs->opaque;
    int ret = 0;
    uint32_t i;
    uint64_t signature;
    Error *local_err = NULL;

    GLOBAL_STATE_CODE();

    ret = bdrv_open_file_child(NULL, options, "file", bs, errp);
    if (ret < 0) {
        return ret;
    }

    GRAPH_RDLOCK_GUARD_MAINLOOP();

    s->bat = NULL;
    s->first_visible_write = true;

    qemu_co_mutex_init(&s->lock);
    QLIST_INIT(&s->regions);

    ret = bdrv_pread(bs->file, 0, sizeof(uint64_t), &signature, 0);
    if (ret < 0) {
        goto fail;
    }
    if (memcmp(&signature, "vhdxfile", 8)) {
        ret = -EINVAL;
        goto fail;
    }

    vhdx_guid_generate(&s->session_guid);

    vhdx_parse_header(bs, s, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto fail;
    }

    ret = vhdx_parse_log(bs, s, &s->log_replayed_on_open, errp);
    if (ret < 0) {
        goto fail;
    }

    ret = vhdx_open_region_tables(bs, s);
    if (ret < 0) {
        goto fail;
    }

    ret = vhdx_parse_metadata(bs, s);
    if (ret < 0) {
        goto fail;
    }

    s->block_size = s->params.block_size;

    bs->total_sectors = s->virtual_disk_size >> s->logical_sector_size_bits;

    vhdx_calc_bat_entries(s);

    s->bat_offset = s->bat_rt.file_offset;

    if (s->bat_entries > s->bat_rt.length / sizeof(VHDXBatEntry)) {
        ret = -EINVAL;
        goto fail;
    }

    s->bat = qemu_try_blockalign(bs->file->bs, s->bat_rt.length);
    if (s->bat == NULL) {
        ret = -ENOMEM;
        goto fail;
    }

    ret = bdrv_pread(bs->file, s->bat_offset, s->bat_rt.length, s->bat, 0);
    if (ret < 0) {
        goto fail;
    }

    for (i = 0; i < s->bat_entries; i++) {
        s->bat[i] = le64_to_cpu(s->bat[i]);
    }

    if (!(flags & BDRV_O_CHECK)) {
        ret = vhdx_check_bat_entries(bs, NULL);
        if (ret < 0) {
            goto fail;
        }
    }

    error_setg(&s->migration_blocker, "The vhdx format used by node '%s' "
               "does not support live migration",
               bdrv_get_device_or_node_name(bs));
    ret = migrate_add_blocker_normal(&s->migration_blocker, errp);
    if (ret < 0) {
        goto fail;
    }

    return 0;
fail:
    vhdx_close(bs);
    return ret;
}

static int vhdx_reopen_prepare(BDRVReopenState *state,
                               BlockReopenQueue *queue, Error **errp)
{
    return 0;
}


static void vhdx_block_translate(BDRVVHDXState *s, int64_t sector_num,
                                 int nb_sectors, VHDXSectorInfo *sinfo)
{
    uint32_t block_offset;

    sinfo->bat_idx = sector_num >> s->sectors_per_block_bits;
    block_offset = sector_num - (sinfo->bat_idx << s->sectors_per_block_bits);
    sinfo->bat_idx += sinfo->bat_idx >> s->chunk_ratio_bits;

    sinfo->sectors_avail = s->sectors_per_block - block_offset;

    sinfo->bytes_left = sinfo->sectors_avail << s->logical_sector_size_bits;

    if (sinfo->sectors_avail > nb_sectors) {
        sinfo->sectors_avail = nb_sectors;
    }

    sinfo->bytes_avail = sinfo->sectors_avail << s->logical_sector_size_bits;

    sinfo->file_offset = s->bat[sinfo->bat_idx] & VHDX_BAT_FILE_OFF_MASK;

    sinfo->block_offset = block_offset << s->logical_sector_size_bits;

    if (sinfo->file_offset == 0) {
        return;
    }

    sinfo->file_offset += sinfo->block_offset;
}


static int coroutine_fn
vhdx_co_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    BDRVVHDXState *s = bs->opaque;

    bdi->cluster_size = s->block_size;

    return 0;
}


static int coroutine_fn GRAPH_RDLOCK
vhdx_co_readv(BlockDriverState *bs, int64_t sector_num, int nb_sectors,
              QEMUIOVector *qiov)
{
    BDRVVHDXState *s = bs->opaque;
    int ret = 0;
    VHDXSectorInfo sinfo;
    uint64_t bytes_done = 0;
    QEMUIOVector hd_qiov;

    qemu_iovec_init(&hd_qiov, qiov->niov);

    qemu_co_mutex_lock(&s->lock);

    while (nb_sectors > 0) {
        if (s->params.data_bits & VHDX_PARAMS_HAS_PARENT) {
            ret = -ENOTSUP;
            goto exit;
        } else {
            vhdx_block_translate(s, sector_num, nb_sectors, &sinfo);

            qemu_iovec_reset(&hd_qiov);
            qemu_iovec_concat(&hd_qiov, qiov,  bytes_done, sinfo.bytes_avail);

            switch (s->bat[sinfo.bat_idx] & VHDX_BAT_STATE_BIT_MASK) {
            case PAYLOAD_BLOCK_NOT_PRESENT: /* fall through */
            case PAYLOAD_BLOCK_UNDEFINED:
            case PAYLOAD_BLOCK_UNMAPPED:
            case PAYLOAD_BLOCK_UNMAPPED_v095:
            case PAYLOAD_BLOCK_ZERO:
                qemu_iovec_memset(&hd_qiov, 0, 0, sinfo.bytes_avail);
                break;
            case PAYLOAD_BLOCK_FULLY_PRESENT:
                qemu_co_mutex_unlock(&s->lock);
                ret = bdrv_co_preadv(bs->file, sinfo.file_offset,
                                     sinfo.sectors_avail * BDRV_SECTOR_SIZE,
                                     &hd_qiov, 0);
                qemu_co_mutex_lock(&s->lock);
                if (ret < 0) {
                    goto exit;
                }
                break;
            case PAYLOAD_BLOCK_PARTIALLY_PRESENT:
                /* we don't yet support difference files, fall through
                 * to error */
            default:
                ret = -EIO;
                goto exit;
                break;
            }
            nb_sectors -= sinfo.sectors_avail;
            sector_num += sinfo.sectors_avail;
            bytes_done += sinfo.bytes_avail;
        }
    }
    ret = 0;
exit:
    qemu_co_mutex_unlock(&s->lock);
    qemu_iovec_destroy(&hd_qiov);
    return ret;
}

static int coroutine_fn GRAPH_RDLOCK
vhdx_allocate_block(BlockDriverState *bs, BDRVVHDXState *s,
                    uint64_t *new_offset, bool *need_zero)
{
    int64_t current_len;

    current_len = bdrv_co_getlength(bs->file->bs);
    if (current_len < 0) {
        return current_len;
    }

    *new_offset = current_len;

    *new_offset = ROUND_UP(*new_offset, 1 * MiB);
    if (*new_offset > INT64_MAX) {
        return -EINVAL;
    }

    if (*need_zero) {
        int ret;

        ret = bdrv_co_truncate(bs->file, *new_offset + s->block_size, false,
                               PREALLOC_MODE_OFF, BDRV_REQ_ZERO_WRITE, NULL);
        if (ret != -ENOTSUP) {
            *need_zero = false;
            return ret;
        }
    }

    return bdrv_co_truncate(bs->file, *new_offset + s->block_size, false,
                            PREALLOC_MODE_OFF, 0, NULL);
}

static void vhdx_update_bat_table_entry(BlockDriverState *bs, BDRVVHDXState *s,
                                       VHDXSectorInfo *sinfo,
                                       uint64_t *bat_entry_le,
                                       uint64_t *bat_offset, int state)
{
    if ((state == PAYLOAD_BLOCK_ZERO)        ||
        (state == PAYLOAD_BLOCK_UNDEFINED)   ||
        (state == PAYLOAD_BLOCK_NOT_PRESENT) ||
        (state == PAYLOAD_BLOCK_UNMAPPED)) {
        s->bat[sinfo->bat_idx]  = 0;
    } else {
        s->bat[sinfo->bat_idx]  = sinfo->file_offset;
    }

    s->bat[sinfo->bat_idx] |= state & VHDX_BAT_STATE_BIT_MASK;

    *bat_entry_le = cpu_to_le64(s->bat[sinfo->bat_idx]);
    *bat_offset = s->bat_offset + sinfo->bat_idx * sizeof(VHDXBatEntry);

}

int vhdx_user_visible_write(BlockDriverState *bs, BDRVVHDXState *s)
{
    int ret = 0;
    if (s->first_visible_write) {
        s->first_visible_write = false;
        ret = vhdx_update_headers(bs, s, true, NULL);
    }
    return ret;
}

static int coroutine_fn GRAPH_RDLOCK
vhdx_co_writev(BlockDriverState *bs, int64_t sector_num, int nb_sectors,
               QEMUIOVector *qiov, int flags)
{
    int ret = -ENOTSUP;
    BDRVVHDXState *s = bs->opaque;
    VHDXSectorInfo sinfo;
    uint64_t bytes_done = 0;
    uint64_t bat_entry = 0;
    uint64_t bat_entry_offset = 0;
    QEMUIOVector hd_qiov;
    struct iovec iov1 = { 0 };
    struct iovec iov2 = { 0 };
    int sectors_to_write;
    int bat_state;
    uint64_t bat_prior_offset = 0;
    bool bat_update = false;

    qemu_iovec_init(&hd_qiov, qiov->niov);

    qemu_co_mutex_lock(&s->lock);

    ret = vhdx_user_visible_write(bs, s);
    if (ret < 0) {
        goto exit;
    }

    while (nb_sectors > 0) {
        bool use_zero_buffers = false;
        bat_update = false;
        if (s->params.data_bits & VHDX_PARAMS_HAS_PARENT) {
            ret = -ENOTSUP;
            goto exit;
        } else {
            vhdx_block_translate(s, sector_num, nb_sectors, &sinfo);
            sectors_to_write = sinfo.sectors_avail;

            qemu_iovec_reset(&hd_qiov);
            bat_state = s->bat[sinfo.bat_idx] & VHDX_BAT_STATE_BIT_MASK;
            switch (bat_state) {
            case PAYLOAD_BLOCK_ZERO:
                use_zero_buffers = true;
                /* fall through */
            case PAYLOAD_BLOCK_NOT_PRESENT: /* fall through */
            case PAYLOAD_BLOCK_UNMAPPED:
            case PAYLOAD_BLOCK_UNMAPPED_v095:
            case PAYLOAD_BLOCK_UNDEFINED:
                bat_prior_offset = sinfo.file_offset;
                ret = vhdx_allocate_block(bs, s, &sinfo.file_offset,
                                          &use_zero_buffers);
                if (ret < 0) {
                    goto exit;
                }
                vhdx_update_bat_table_entry(bs, s, &sinfo, &bat_entry,
                                            &bat_entry_offset,
                                            PAYLOAD_BLOCK_FULLY_PRESENT);
                bat_update = true;
                if (!use_zero_buffers) {
                    sinfo.file_offset += sinfo.block_offset;
                } else {
                    if (sinfo.block_offset) {
                        iov1.iov_len = sinfo.block_offset;
                        iov1.iov_base = qemu_blockalign(bs, iov1.iov_len);
                        memset(iov1.iov_base, 0, iov1.iov_len);
                        qemu_iovec_concat_iov(&hd_qiov, &iov1, 1, 0,
                                              iov1.iov_len);
                        sectors_to_write += iov1.iov_len >> BDRV_SECTOR_BITS;
                    }

                    qemu_iovec_concat(&hd_qiov, qiov, bytes_done,
                                      sinfo.bytes_avail);

                    if ((sinfo.bytes_avail - sinfo.block_offset) <
                         s->block_size) {
                        iov2.iov_len = s->block_size -
                                      (sinfo.bytes_avail + sinfo.block_offset);
                        iov2.iov_base = qemu_blockalign(bs, iov2.iov_len);
                        memset(iov2.iov_base, 0, iov2.iov_len);
                        qemu_iovec_concat_iov(&hd_qiov, &iov2, 1, 0,
                                              iov2.iov_len);
                        sectors_to_write += iov2.iov_len >> BDRV_SECTOR_BITS;
                    }
                }

                /* fall through */
            case PAYLOAD_BLOCK_FULLY_PRESENT:
                if (sinfo.file_offset < (1 * MiB)) {
                    ret = -EFAULT;
                    goto error_bat_restore;
                }

                if (!use_zero_buffers) {
                    qemu_iovec_concat(&hd_qiov, qiov,  bytes_done,
                                      sinfo.bytes_avail);
                }
                qemu_co_mutex_unlock(&s->lock);
                ret = bdrv_co_pwritev(bs->file, sinfo.file_offset,
                                      sectors_to_write * BDRV_SECTOR_SIZE,
                                      &hd_qiov, 0);
                qemu_co_mutex_lock(&s->lock);
                if (ret < 0) {
                    goto error_bat_restore;
                }
                break;
            case PAYLOAD_BLOCK_PARTIALLY_PRESENT:
                /* we don't yet support difference files, fall through
                 * to error */
            default:
                ret = -EIO;
                goto exit;
                break;
            }

            if (bat_update) {
                ret =  vhdx_log_write_and_flush(bs, s, &bat_entry,
                                                sizeof(VHDXBatEntry),
                                                bat_entry_offset);
                if (ret < 0) {
                    goto exit;
                }
            }

            nb_sectors -= sinfo.sectors_avail;
            sector_num += sinfo.sectors_avail;
            bytes_done += sinfo.bytes_avail;

        }
    }

    goto exit;

error_bat_restore:
    if (bat_update) {
        sinfo.file_offset = bat_prior_offset;
        vhdx_update_bat_table_entry(bs, s, &sinfo, &bat_entry,
                                    &bat_entry_offset, bat_state);
    }
exit:
    qemu_vfree(iov1.iov_base);
    qemu_vfree(iov2.iov_base);
    qemu_co_mutex_unlock(&s->lock);
    qemu_iovec_destroy(&hd_qiov);
    return ret;
}



static int coroutine_fn GRAPH_UNLOCKED
vhdx_create_new_headers(BlockBackend *blk, uint64_t image_size,
                        uint32_t log_size)
{
    BlockDriverState *bs = blk_bs(blk);
    BdrvChild *child;
    int ret = 0;
    VHDXHeader *hdr = NULL;

    GRAPH_RDLOCK_GUARD();

    hdr = g_new0(VHDXHeader, 1);

    hdr->signature       = VHDX_HEADER_SIGNATURE;
    hdr->sequence_number = g_random_int();
    hdr->log_version     = 0;
    hdr->version         = 1;
    hdr->log_length      = log_size;
    hdr->log_offset      = VHDX_HEADER_SECTION_END;
    vhdx_guid_generate(&hdr->file_write_guid);
    vhdx_guid_generate(&hdr->data_write_guid);

    child = QLIST_FIRST(&bs->parents);
    assert(!QLIST_NEXT(child, next_parent));

    ret = vhdx_write_header(child, hdr, VHDX_HEADER1_OFFSET, false);
    if (ret < 0) {
        goto exit;
    }
    hdr->sequence_number++;
    ret = vhdx_write_header(child, hdr, VHDX_HEADER2_OFFSET, false);
    if (ret < 0) {
        goto exit;
    }

exit:
    g_free(hdr);
    return ret;
}

#define VHDX_METADATA_ENTRY_BUFFER_SIZE \
                                    (sizeof(VHDXFileParameters)               +\
                                     sizeof(VHDXVirtualDiskSize)              +\
                                     sizeof(VHDXPage83Data)                   +\
                                     sizeof(VHDXVirtualDiskLogicalSectorSize) +\
                                     sizeof(VHDXVirtualDiskPhysicalSectorSize))

static int coroutine_fn
vhdx_create_new_metadata(BlockBackend *blk, uint64_t image_size,
                         uint32_t block_size, uint32_t sector_size,
                         uint64_t metadata_offset, VHDXImageType type)
{
    int ret = 0;
    uint32_t offset = 0;
    void *buffer = NULL;
    void *entry_buffer;
    VHDXMetadataTableHeader *md_table;
    VHDXMetadataTableEntry  *md_table_entry;

    VHDXFileParameters     *mt_file_params;
    VHDXVirtualDiskSize    *mt_virtual_size;
    VHDXPage83Data         *mt_page83;
    VHDXVirtualDiskLogicalSectorSize  *mt_log_sector_size;
    VHDXVirtualDiskPhysicalSectorSize *mt_phys_sector_size;

    entry_buffer = g_malloc0(VHDX_METADATA_ENTRY_BUFFER_SIZE);

    mt_file_params = entry_buffer;
    offset += sizeof(VHDXFileParameters);
    mt_virtual_size = entry_buffer + offset;
    offset += sizeof(VHDXVirtualDiskSize);
    mt_page83 = entry_buffer + offset;
    offset += sizeof(VHDXPage83Data);
    mt_log_sector_size = entry_buffer + offset;
    offset += sizeof(VHDXVirtualDiskLogicalSectorSize);
    mt_phys_sector_size = entry_buffer + offset;

    mt_file_params->block_size = cpu_to_le32(block_size);
    if (type == VHDX_TYPE_FIXED) {
        mt_file_params->data_bits |= VHDX_PARAMS_LEAVE_BLOCKS_ALLOCED;
        mt_file_params->data_bits = cpu_to_le32(mt_file_params->data_bits);
    }

    vhdx_guid_generate(&mt_page83->page_83_data);
    cpu_to_leguids(&mt_page83->page_83_data);
    mt_virtual_size->virtual_disk_size        = cpu_to_le64(image_size);
    mt_log_sector_size->logical_sector_size   = cpu_to_le32(sector_size);
    mt_phys_sector_size->physical_sector_size = cpu_to_le32(sector_size);

    buffer = g_malloc0(VHDX_HEADER_BLOCK_SIZE);
    md_table = buffer;

    md_table->signature   = VHDX_METADATA_SIGNATURE;
    md_table->entry_count = 5;
    vhdx_metadata_header_le_export(md_table);


    offset = 64 * KiB;

    md_table_entry = buffer + sizeof(VHDXMetadataTableHeader);

    md_table_entry[0].item_id = file_param_guid;
    md_table_entry[0].offset  = offset;
    md_table_entry[0].length  = sizeof(VHDXFileParameters);
    md_table_entry[0].data_bits |= VHDX_META_FLAGS_IS_REQUIRED;
    offset += md_table_entry[0].length;
    vhdx_metadata_entry_le_export(&md_table_entry[0]);

    md_table_entry[1].item_id = virtual_size_guid;
    md_table_entry[1].offset  = offset;
    md_table_entry[1].length  = sizeof(VHDXVirtualDiskSize);
    md_table_entry[1].data_bits |= VHDX_META_FLAGS_IS_REQUIRED |
                                   VHDX_META_FLAGS_IS_VIRTUAL_DISK;
    offset += md_table_entry[1].length;
    vhdx_metadata_entry_le_export(&md_table_entry[1]);

    md_table_entry[2].item_id = page83_guid;
    md_table_entry[2].offset  = offset;
    md_table_entry[2].length  = sizeof(VHDXPage83Data);
    md_table_entry[2].data_bits |= VHDX_META_FLAGS_IS_REQUIRED |
                                   VHDX_META_FLAGS_IS_VIRTUAL_DISK;
    offset += md_table_entry[2].length;
    vhdx_metadata_entry_le_export(&md_table_entry[2]);

    md_table_entry[3].item_id = logical_sector_guid;
    md_table_entry[3].offset  = offset;
    md_table_entry[3].length  = sizeof(VHDXVirtualDiskLogicalSectorSize);
    md_table_entry[3].data_bits |= VHDX_META_FLAGS_IS_REQUIRED |
                                   VHDX_META_FLAGS_IS_VIRTUAL_DISK;
    offset += md_table_entry[3].length;
    vhdx_metadata_entry_le_export(&md_table_entry[3]);

    md_table_entry[4].item_id = phys_sector_guid;
    md_table_entry[4].offset  = offset;
    md_table_entry[4].length  = sizeof(VHDXVirtualDiskPhysicalSectorSize);
    md_table_entry[4].data_bits |= VHDX_META_FLAGS_IS_REQUIRED |
                                   VHDX_META_FLAGS_IS_VIRTUAL_DISK;
    vhdx_metadata_entry_le_export(&md_table_entry[4]);

    ret = blk_co_pwrite(blk, metadata_offset, VHDX_HEADER_BLOCK_SIZE, buffer, 0);
    if (ret < 0) {
        goto exit;
    }

    ret = blk_co_pwrite(blk, metadata_offset + (64 * KiB),
                        VHDX_METADATA_ENTRY_BUFFER_SIZE, entry_buffer, 0);
    if (ret < 0) {
        goto exit;
    }


exit:
    g_free(buffer);
    g_free(entry_buffer);
    return ret;
}

static int coroutine_fn GRAPH_UNLOCKED
vhdx_create_bat(BlockBackend *blk, BDRVVHDXState *s,
                uint64_t image_size, VHDXImageType type,
                bool use_zero_blocks, uint64_t file_offset,
                uint32_t length, Error **errp)
{
    int ret = 0;
    uint64_t data_file_offset;
    uint64_t total_sectors = 0;
    uint64_t sector_num = 0;
    uint64_t unused;
    int block_state;
    VHDXSectorInfo sinfo;
    bool has_zero_init;

    assert(s->bat == NULL);

    data_file_offset = file_offset + length + 5 * MiB;
    total_sectors = image_size >> s->logical_sector_size_bits;

    if (type == VHDX_TYPE_DYNAMIC) {
        ret = blk_co_truncate(blk, data_file_offset, false, PREALLOC_MODE_OFF,
                              0, errp);
        if (ret < 0) {
            goto exit;
        }
    } else if (type == VHDX_TYPE_FIXED) {
        ret = blk_co_truncate(blk, data_file_offset + image_size, false,
                              PREALLOC_MODE_OFF, 0, errp);
        if (ret < 0) {
            goto exit;
        }
    } else {
        error_setg(errp, "Unsupported image type");
        ret = -ENOTSUP;
        goto exit;
    }

    bdrv_graph_co_rdlock();
    has_zero_init = bdrv_has_zero_init(blk_bs(blk));
    bdrv_graph_co_rdunlock();

    if (type == VHDX_TYPE_FIXED ||
                use_zero_blocks ||
                has_zero_init == 0) {
        s->bat = g_try_malloc0(length);
        if (length && s->bat == NULL) {
            error_setg(errp, "Failed to allocate memory for the BAT");
            ret = -ENOMEM;
            goto exit;
        }
        block_state = type == VHDX_TYPE_FIXED ? PAYLOAD_BLOCK_FULLY_PRESENT :
                                                PAYLOAD_BLOCK_NOT_PRESENT;
        block_state = use_zero_blocks ? PAYLOAD_BLOCK_ZERO : block_state;
        while (sector_num < total_sectors) {
            vhdx_block_translate(s, sector_num, s->sectors_per_block, &sinfo);
            sinfo.file_offset = data_file_offset +
                                (sector_num << s->logical_sector_size_bits);
            sinfo.file_offset = ROUND_UP(sinfo.file_offset, MiB);
            vhdx_update_bat_table_entry(blk_bs(blk), s, &sinfo, &unused, &unused,
                                        block_state);
            s->bat[sinfo.bat_idx] = cpu_to_le64(s->bat[sinfo.bat_idx]);
            sector_num += s->sectors_per_block;
        }
        ret = blk_co_pwrite(blk, file_offset, length, s->bat, 0);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Failed to write the BAT");
            goto exit;
        }
    }



exit:
    g_free(s->bat);
    return ret;
}

static int coroutine_fn GRAPH_UNLOCKED
vhdx_create_new_region_table(BlockBackend *blk, uint64_t image_size,
                             uint32_t block_size, uint32_t sector_size,
                             uint32_t log_size, bool use_zero_blocks,
                             VHDXImageType type, uint64_t *metadata_offset,
                             Error **errp)
{
    int ret = 0;
    uint32_t offset = 0;
    void *buffer = NULL;
    uint64_t bat_file_offset;
    uint32_t bat_length;
    BDRVVHDXState *s = NULL;
    VHDXRegionTableHeader *region_table;
    VHDXRegionTableEntry *rt_bat;
    VHDXRegionTableEntry *rt_metadata;

    assert(metadata_offset != NULL);

    s = g_new0(BDRVVHDXState, 1);

    s->chunk_ratio = (VHDX_MAX_SECTORS_PER_BLOCK) *
                     (uint64_t) sector_size / (uint64_t) block_size;

    s->sectors_per_block = block_size / sector_size;
    s->virtual_disk_size = image_size;
    s->block_size = block_size;
    s->logical_sector_size = sector_size;

    vhdx_set_shift_bits(s);

    vhdx_calc_bat_entries(s);

    buffer = g_malloc0(VHDX_HEADER_BLOCK_SIZE);
    region_table = buffer;
    offset += sizeof(VHDXRegionTableHeader);
    rt_bat = buffer + offset;
    offset += sizeof(VHDXRegionTableEntry);
    rt_metadata  = buffer + offset;

    region_table->signature = VHDX_REGION_SIGNATURE;
    region_table->entry_count = 2;

    rt_bat->guid        = bat_guid;
    rt_bat->length      = ROUND_UP(s->bat_entries * sizeof(VHDXBatEntry), MiB);
    rt_bat->file_offset = ROUND_UP(VHDX_HEADER_SECTION_END + log_size, MiB);
    s->bat_offset = rt_bat->file_offset;

    rt_metadata->guid        = metadata_guid;
    rt_metadata->file_offset = ROUND_UP(rt_bat->file_offset + rt_bat->length,
                                        MiB);
    rt_metadata->length      = 1 * MiB;
    *metadata_offset = rt_metadata->file_offset;

    bat_file_offset = rt_bat->file_offset;
    bat_length = rt_bat->length;

    vhdx_region_header_le_export(region_table);
    vhdx_region_entry_le_export(rt_bat);
    vhdx_region_entry_le_export(rt_metadata);

    vhdx_update_checksum(buffer, VHDX_HEADER_BLOCK_SIZE,
                         offsetof(VHDXRegionTableHeader, checksum));


    ret = vhdx_create_bat(blk, s, image_size, type, use_zero_blocks,
                          bat_file_offset, bat_length, errp);
    if (ret < 0) {
        goto exit;
    }

    ret = blk_co_pwrite(blk, VHDX_REGION_TABLE_OFFSET, VHDX_HEADER_BLOCK_SIZE,
                        buffer, 0);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to write first region table");
        goto exit;
    }

    ret = blk_co_pwrite(blk, VHDX_REGION_TABLE2_OFFSET, VHDX_HEADER_BLOCK_SIZE,
                        buffer, 0);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to write second region table");
        goto exit;
    }

exit:
    g_free(s);
    g_free(buffer);
    return ret;
}

static int coroutine_fn GRAPH_UNLOCKED
vhdx_co_create(BlockdevCreateOptions *opts, Error **errp)
{
    BlockdevCreateOptionsVhdx *vhdx_opts;
    BlockBackend *blk = NULL;
    BlockDriverState *bs = NULL;

    int ret = 0;
    uint64_t image_size;
    uint32_t log_size;
    uint32_t block_size;
    uint64_t signature;
    uint64_t metadata_offset;
    bool use_zero_blocks = false;

    gunichar2 *creator = NULL;
    glong creator_items;
    VHDXImageType image_type;

    assert(opts->driver == BLOCKDEV_DRIVER_VHDX);
    vhdx_opts = &opts->u.vhdx;

    image_size = vhdx_opts->size;
    if (image_size > VHDX_MAX_IMAGE_SIZE) {
        error_setg(errp, "Image size too large; max of 64TB");
        return -EINVAL;
    }

    if (!vhdx_opts->has_log_size) {
        log_size = DEFAULT_LOG_SIZE;
    } else {
        if (vhdx_opts->log_size > UINT32_MAX) {
            error_setg(errp, "Log size must be smaller than 4 GB");
            return -EINVAL;
        }
        log_size = vhdx_opts->log_size;
    }
    if (log_size < MiB || (log_size % MiB) != 0) {
        error_setg(errp, "Log size must be a multiple of 1 MB");
        return -EINVAL;
    }

    if (!vhdx_opts->has_block_state_zero) {
        use_zero_blocks = true;
    } else {
        use_zero_blocks = vhdx_opts->block_state_zero;
    }

    if (!vhdx_opts->has_subformat) {
        vhdx_opts->subformat = BLOCKDEV_VHDX_SUBFORMAT_DYNAMIC;
    }

    switch (vhdx_opts->subformat) {
    case BLOCKDEV_VHDX_SUBFORMAT_DYNAMIC:
        image_type = VHDX_TYPE_DYNAMIC;
        break;
    case BLOCKDEV_VHDX_SUBFORMAT_FIXED:
        image_type = VHDX_TYPE_FIXED;
        break;
    default:
        g_assert_not_reached();
    }

    if (vhdx_opts->has_block_size) {
        block_size = vhdx_opts->block_size;
    } else {
        if (image_size > 32 * TiB) {
            block_size = 64 * MiB;
        } else if (image_size > (uint64_t) 100 * GiB) {
            block_size = 32 * MiB;
        } else if (image_size > 1 * GiB) {
            block_size = 16 * MiB;
        } else {
            block_size = 8 * MiB;
        }
    }

    if (block_size < MiB || (block_size % MiB) != 0) {
        error_setg(errp, "Block size must be a multiple of 1 MB");
        return -EINVAL;
    }
    if (!is_power_of_2(block_size)) {
        error_setg(errp, "Block size must be a power of two");
        return -EINVAL;
    }
    if (block_size > VHDX_BLOCK_SIZE_MAX) {
        error_setg(errp, "Block size must not exceed %" PRId64,
                   VHDX_BLOCK_SIZE_MAX);
        return -EINVAL;
    }

    bs = bdrv_co_open_blockdev_ref(vhdx_opts->file, errp);
    if (bs == NULL) {
        return -EIO;
    }

    blk = blk_co_new_with_bs(bs, BLK_PERM_WRITE | BLK_PERM_RESIZE, BLK_PERM_ALL,
                             errp);
    if (!blk) {
        ret = -EPERM;
        goto delete_and_exit;
    }
    blk_set_allow_write_beyond_eof(blk, true);

    creator = g_utf8_to_utf16("QEMU v" QEMU_VERSION, -1, NULL,
                              &creator_items, NULL);
    signature = cpu_to_le64(VHDX_FILE_SIGNATURE);
    ret = blk_co_pwrite(blk, VHDX_FILE_ID_OFFSET, sizeof(signature), &signature,
                        0);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to write file signature");
        goto delete_and_exit;
    }
    if (creator) {
        ret = blk_co_pwrite(blk, VHDX_FILE_ID_OFFSET + sizeof(signature),
                            creator_items * sizeof(gunichar2), creator, 0);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Failed to write creator field");
            goto delete_and_exit;
        }
    }


    ret = vhdx_create_new_headers(blk, image_size, log_size);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to write image headers");
        goto delete_and_exit;
    }

    ret = vhdx_create_new_region_table(blk, image_size, block_size, 512,
                                       log_size, use_zero_blocks, image_type,
                                       &metadata_offset, errp);
    if (ret < 0) {
        goto delete_and_exit;
    }

    ret = vhdx_create_new_metadata(blk, image_size, block_size, 512,
                                   metadata_offset, image_type);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to initialize metadata");
        goto delete_and_exit;
    }

    ret = 0;
delete_and_exit:
    blk_co_unref(blk);
    bdrv_co_unref(bs);
    g_free(creator);
    return ret;
}

static int coroutine_fn GRAPH_UNLOCKED
vhdx_co_create_opts(BlockDriver *drv, const char *filename,
                    QemuOpts *opts, Error **errp)
{
    BlockdevCreateOptions *create_options = NULL;
    QDict *qdict;
    Visitor *v;
    BlockDriverState *bs = NULL;
    int ret;

    static const QDictRenames opt_renames[] = {
        { VHDX_BLOCK_OPT_LOG_SIZE,      "log-size" },
        { VHDX_BLOCK_OPT_BLOCK_SIZE,    "block-size" },
        { VHDX_BLOCK_OPT_ZERO,          "block-state-zero" },
        { NULL, NULL },
    };

    qdict = qemu_opts_to_qdict_filtered(opts, NULL, &vhdx_create_opts, true);

    if (!qdict_rename_keys(qdict, opt_renames, errp)) {
        ret = -EINVAL;
        goto fail;
    }

    ret = bdrv_co_create_file(filename, opts, true, errp);
    if (ret < 0) {
        goto fail;
    }

    bs = bdrv_co_open(filename, NULL, NULL,
                      BDRV_O_RDWR | BDRV_O_RESIZE | BDRV_O_PROTOCOL, errp);
    if (bs == NULL) {
        ret = -EIO;
        goto fail;
    }

    qdict_put_str(qdict, "driver", "vhdx");
    qdict_put_str(qdict, "file", bs->node_name);

    v = qobject_input_visitor_new_flat_confused(qdict, errp);
    if (!v) {
        ret = -EINVAL;
        goto fail;
    }

    visit_type_BlockdevCreateOptions(v, NULL, &create_options, errp);
    visit_free(v);
    if (!create_options) {
        ret = -EINVAL;
        goto fail;
    }

    assert(create_options->driver == BLOCKDEV_DRIVER_VHDX);
    create_options->u.vhdx.size =
        ROUND_UP(create_options->u.vhdx.size, BDRV_SECTOR_SIZE);

    if (create_options->u.vhdx.has_log_size) {
        create_options->u.vhdx.log_size =
            ROUND_UP(create_options->u.vhdx.log_size, MiB);
    }
    if (create_options->u.vhdx.has_block_size) {
        create_options->u.vhdx.block_size =
            ROUND_UP(create_options->u.vhdx.block_size, MiB);

        if (create_options->u.vhdx.block_size == 0) {
            create_options->u.vhdx.has_block_size = false;
        }
        if (create_options->u.vhdx.block_size > VHDX_BLOCK_SIZE_MAX) {
            create_options->u.vhdx.block_size = VHDX_BLOCK_SIZE_MAX;
        }
    }

    ret = vhdx_co_create(create_options, errp);

fail:
    qobject_unref(qdict);
    bdrv_co_unref(bs);
    qapi_free_BlockdevCreateOptions(create_options);
    return ret;
}

static int coroutine_fn GRAPH_RDLOCK
vhdx_co_check(BlockDriverState *bs, BdrvCheckResult *result,
              BdrvCheckMode fix)
{
    BDRVVHDXState *s = bs->opaque;

    if (s->log_replayed_on_open) {
        result->corruptions_fixed++;
    }

    vhdx_check_bat_entries(bs, &result->corruptions);

    return 0;
}

static int GRAPH_RDLOCK vhdx_has_zero_init(BlockDriverState *bs)
{
    BDRVVHDXState *s = bs->opaque;
    int state;

    if (!s->bat_entries) {
        return 1;
    }

    state = s->bat[0] & VHDX_BAT_STATE_BIT_MASK;
    if (state == PAYLOAD_BLOCK_FULLY_PRESENT) {
        return bdrv_has_zero_init(bs->file->bs);
    }

    return 1;
}

static QemuOptsList vhdx_create_opts = {
    .name = "vhdx-create-opts",
    .head = QTAILQ_HEAD_INITIALIZER(vhdx_create_opts.head),
    .desc = {
        {
           .name = BLOCK_OPT_SIZE,
           .type = QEMU_OPT_SIZE,
           .help = "Virtual disk size; max of 64TB."
       },
       {
           .name = VHDX_BLOCK_OPT_LOG_SIZE,
           .type = QEMU_OPT_SIZE,
           .def_value_str = stringify(DEFAULT_LOG_SIZE),
           .help = "Log size; min 1MB."
       },
       {
           .name = VHDX_BLOCK_OPT_BLOCK_SIZE,
           .type = QEMU_OPT_SIZE,
           .def_value_str = stringify(0),
           .help = "Block Size; min 1MB, max 256MB. "
                   "0 means auto-calculate based on image size."
       },
       {
           .name = BLOCK_OPT_SUBFMT,
           .type = QEMU_OPT_STRING,
           .help = "VHDX format type, can be either 'dynamic' or 'fixed'. "
                   "Default is 'dynamic'."
       },
       {
           .name = VHDX_BLOCK_OPT_ZERO,
           .type = QEMU_OPT_BOOL,
           .help = "Force use of payload blocks of type 'ZERO'. "
                   "Non-standard, but default.  Do not set to 'off' when "
                   "using 'qemu-img convert' with subformat=dynamic."
       },
       { NULL }
    }
};

static BlockDriver bdrv_vhdx = {
    .format_name            = "vhdx",
    .instance_size          = sizeof(BDRVVHDXState),
    .bdrv_probe             = vhdx_probe,
    .bdrv_open              = vhdx_open,
    .bdrv_close             = vhdx_close,
    .bdrv_reopen_prepare    = vhdx_reopen_prepare,
    .bdrv_child_perm        = bdrv_default_perms,
    .bdrv_co_readv          = vhdx_co_readv,
    .bdrv_co_writev         = vhdx_co_writev,
    .bdrv_co_create         = vhdx_co_create,
    .bdrv_co_create_opts    = vhdx_co_create_opts,
    .bdrv_co_get_info       = vhdx_co_get_info,
    .bdrv_co_check          = vhdx_co_check,
    .bdrv_has_zero_init     = vhdx_has_zero_init,

    .is_format              = true,
    .create_opts            = &vhdx_create_opts,
};

static void bdrv_vhdx_init(void)
{
    bdrv_register(&bdrv_vhdx);
}

block_init(bdrv_vhdx_init);
