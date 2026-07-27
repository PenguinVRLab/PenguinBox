/*
 *  CFI parallel flash with AMD command set emulation
 *
 *  Copyright (c) 2005 Jocelyn Mayer
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
 */

/*
 * For now, this code can emulate flashes of 1, 2 or 4 bytes width.
 * Supported commands/modes are:
 * - flash read
 * - flash write
 * - flash ID read
 * - sector erase
 * - chip erase
 * - unlock bypass command
 * - CFI queries
 *
 * It does not support flash interleaving.
 * It does not implement software data protection as found in many real chips
 */

#include "qemu/osdep.h"
#include "hw/block/block.h"
#include "hw/block/flash.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/bitmap.h"
#include "qemu/timer.h"
#include "system/block-backend.h"
#include "qemu/host-utils.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "trace.h"

#define PFLASH_LAZY_ROMD_THRESHOLD 42

#define PFLASH_MAX_ERASE_REGIONS 4

enum {
    WCYCLE_CFI              = 7,
    WCYCLE_AUTOSELECT_CFI   = 8,
};

struct PFlashCFI02 {
    SysBusDevice parent_obj;

    BlockBackend *blk;
    uint32_t uniform_nb_blocs;
    uint32_t uniform_sector_len;
    uint32_t total_sectors;
    uint32_t nb_blocs[PFLASH_MAX_ERASE_REGIONS];
    uint32_t sector_len[PFLASH_MAX_ERASE_REGIONS];
    uint32_t chip_len;
    uint8_t mappings;
    uint8_t width;
    uint8_t be;
    int wcycle;
    int bypass;
    int ro;
    uint8_t cmd;
    uint8_t status;
    uint16_t ident0;
    uint16_t ident1;
    uint16_t ident2;
    uint16_t ident3;
    uint16_t unlock_addr0;
    uint16_t unlock_addr1;
    uint8_t cfi_table[0x4d];
    QEMUTimer timer;
    MemoryRegion mem;
    MemoryRegion *mem_mappings;
    MemoryRegion orig_mem;
    bool rom_mode;
    int read_counter;
    int sectors_to_erase;
    uint64_t erase_time_remaining;
    unsigned long *sector_erase_map;
    char *name;
    void *storage;
};

static inline void toggle_dq7(PFlashCFI02 *pfl)
{
    pfl->status ^= 0x80;
}

static inline void set_dq7(PFlashCFI02 *pfl, uint8_t value)
{
    pfl->status &= 0x7F;
    pfl->status |= value & 0x80;
}

static inline void toggle_dq6(PFlashCFI02 *pfl)
{
    pfl->status ^= 0x40;
}

static inline void assert_dq3(PFlashCFI02 *pfl)
{
    pfl->status |= 0x08;
}

static inline void reset_dq3(PFlashCFI02 *pfl)
{
    pfl->status &= ~0x08;
}

static inline void toggle_dq2(PFlashCFI02 *pfl)
{
    pfl->status ^= 0x04;
}

static void pflash_setup_mappings(PFlashCFI02 *pfl)
{
    unsigned i;
    hwaddr size = memory_region_size(&pfl->orig_mem);

    memory_region_init(&pfl->mem, OBJECT(pfl), "pflash", pfl->mappings * size);
    pfl->mem_mappings = g_new(MemoryRegion, pfl->mappings);
    for (i = 0; i < pfl->mappings; ++i) {
        memory_region_init_alias(&pfl->mem_mappings[i], OBJECT(pfl),
                                 "pflash-alias", &pfl->orig_mem, 0, size);
        memory_region_add_subregion(&pfl->mem, i * size, &pfl->mem_mappings[i]);
    }
}

static void pflash_reset_state_machine(PFlashCFI02 *pfl)
{
    trace_pflash_reset(pfl->name);
    pfl->cmd = 0x00;
    pfl->wcycle = 0;
}

static void pflash_mode_read_array(PFlashCFI02 *pfl)
{
    trace_pflash_mode_read_array(pfl->name);
    pflash_reset_state_machine(pfl);
    pfl->rom_mode = true;
    memory_region_rom_device_set_romd(&pfl->orig_mem, true);
}

static size_t pflash_regions_count(PFlashCFI02 *pfl)
{
    return pfl->cfi_table[0x2c];
}

static uint64_t pflash_erase_time(PFlashCFI02 *pfl)
{
    return ((1ULL << pfl->cfi_table[0x21]) * pfl->sectors_to_erase) * SCALE_US;
}

static inline bool pflash_erase_suspend_mode(PFlashCFI02 *pfl)
{
    return pfl->erase_time_remaining > 0;
}

static void pflash_timer(void *opaque)
{
    PFlashCFI02 *pfl = opaque;

    trace_pflash_timer_expired(pfl->name, pfl->cmd);
    if (pfl->cmd == 0x30) {
        if ((pfl->status & 0x08) == 0) {
            assert_dq3(pfl);
            uint64_t timeout = pflash_erase_time(pfl);
            timer_mod(&pfl->timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + timeout);
            trace_pflash_erase_timeout(pfl->name, pfl->sectors_to_erase);
            return;
        }
        trace_pflash_erase_complete(pfl->name);
        bitmap_zero(pfl->sector_erase_map, pfl->total_sectors);
        pfl->sectors_to_erase = 0;
        reset_dq3(pfl);
    }

    toggle_dq7(pfl);
    if (pfl->bypass) {
        pfl->wcycle = 2;
        pfl->cmd = 0;
    } else {
        pflash_mode_read_array(pfl);
    }
}

static uint64_t pflash_data_read(PFlashCFI02 *pfl, hwaddr offset,
                                 unsigned int width)
{
    uint8_t *p = (uint8_t *)pfl->storage + offset;
    uint64_t ret = pfl->be ? ldn_be_p(p, width) : ldn_le_p(p, width);
    trace_pflash_data_read(pfl->name, offset, width, ret);
    return ret;
}

typedef struct {
    uint32_t len;
    uint32_t num;
} SectorInfo;

static SectorInfo pflash_sector_info(PFlashCFI02 *pfl, hwaddr offset)
{
    assert(offset < pfl->chip_len);
    hwaddr addr = 0;
    uint32_t sector_num = 0;
    for (int i = 0; i < pflash_regions_count(pfl); ++i) {
        uint64_t region_size = (uint64_t)pfl->nb_blocs[i] * pfl->sector_len[i];
        if (addr <= offset && offset < addr + region_size) {
            return (SectorInfo) {
                .len = pfl->sector_len[i],
                .num = sector_num + (offset - addr) / pfl->sector_len[i],
            };
        }
        sector_num += pfl->nb_blocs[i];
        addr += region_size;
    }
    abort();
}

static bool pflash_sector_is_erasing(PFlashCFI02 *pfl, hwaddr offset)
{
    long sector_num = pflash_sector_info(pfl, offset).num;
    return test_bit(sector_num, pfl->sector_erase_map);
}

static uint64_t pflash_read(void *opaque, hwaddr offset, unsigned int width)
{
    PFlashCFI02 *pfl = opaque;
    hwaddr boff;
    uint64_t ret;

    if (!pfl->rom_mode && pfl->wcycle == 0 &&
        ++pfl->read_counter > PFLASH_LAZY_ROMD_THRESHOLD) {
        pflash_mode_read_array(pfl);
    }
    offset &= pfl->chip_len - 1;
    boff = offset & 0xFF;
    if (pfl->width == 2) {
        boff = boff >> 1;
    } else if (pfl->width == 4) {
        boff = boff >> 2;
    }
    switch (pfl->cmd) {
    default:
        trace_pflash_read_unknown_state(pfl->name, pfl->cmd);
        pflash_reset_state_machine(pfl);
        /* fall through to the read code */
    case 0x80:
    case 0x00:
        if (pflash_erase_suspend_mode(pfl) &&
            pflash_sector_is_erasing(pfl, offset)) {
            toggle_dq2(pfl);
            ret = pfl->status;
            trace_pflash_read_status(pfl->name, ret);
            break;
        }
        ret = pflash_data_read(pfl, offset, width);
        break;
    case 0x90:
        switch (boff) {
        case 0x00:
        case 0x01:
            ret = boff & 0x01 ? pfl->ident1 : pfl->ident0;
            break;
        case 0x02:
            ret = 0x00;
            break;
        case 0x0E:
        case 0x0F:
            ret = boff & 0x01 ? pfl->ident3 : pfl->ident2;
            if (ret != (uint8_t)-1) {
                break;
            }
            /* Fall through to data read. */
        default:
            ret = pflash_data_read(pfl, offset, width);
        }
        trace_pflash_read_done(pfl->name, boff, ret);
        break;
    case 0x10:
    case 0x30:
        toggle_dq2(pfl);
        /* fall through */
    case 0xA0:
        toggle_dq6(pfl);
        ret = pfl->status;
        trace_pflash_read_status(pfl->name, ret);
        break;
    case 0x98:
        if (boff < sizeof(pfl->cfi_table)) {
            ret = pfl->cfi_table[boff];
        } else {
            ret = 0;
        }
        break;
    }
    trace_pflash_io_read(pfl->name, offset, width, ret, pfl->cmd, pfl->wcycle);

    return ret;
}

static void pflash_update(PFlashCFI02 *pfl, int offset, int size)
{
    int offset_end;
    int ret;
    if (pfl->blk) {
        offset_end = offset + size;
        offset = QEMU_ALIGN_DOWN(offset, BDRV_SECTOR_SIZE);
        offset_end = QEMU_ALIGN_UP(offset_end, BDRV_SECTOR_SIZE);
        ret = blk_pwrite(pfl->blk, offset, offset_end - offset,
                         pfl->storage + offset, 0);
        if (ret < 0) {
            error_report("Could not update PFLASH: %s", strerror(-ret));
        }
    }
}

static void pflash_sector_erase(PFlashCFI02 *pfl, hwaddr offset)
{
    SectorInfo sector_info = pflash_sector_info(pfl, offset);
    uint64_t sector_len = sector_info.len;
    offset &= ~(sector_len - 1);
    trace_pflash_sector_erase_start(pfl->name, pfl->width * 2, offset,
                                    pfl->width * 2, offset + sector_len - 1);
    if (!pfl->ro) {
        uint8_t *p = pfl->storage;
        memset(p + offset, 0xff, sector_len);
        pflash_update(pfl, offset, sector_len);
    }
    set_dq7(pfl, 0x00);
    ++pfl->sectors_to_erase;
    set_bit(sector_info.num, pfl->sector_erase_map);
    timer_mod(&pfl->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 50000);
}

static void pflash_write(void *opaque, hwaddr offset, uint64_t value,
                         unsigned int width)
{
    PFlashCFI02 *pfl = opaque;
    hwaddr boff;
    uint8_t *p;
    uint8_t cmd;

    trace_pflash_io_write(pfl->name, offset, width, value, pfl->wcycle);
    cmd = value;
    if (pfl->cmd != 0xA0) {
        if (cmd == 0xF0 && pfl->cmd != 0x10 && pfl->cmd != 0x30) {
            if (pfl->wcycle == WCYCLE_AUTOSELECT_CFI) {
                pfl->wcycle = 3;
                pfl->cmd = 0x90;
                return;
            }
            goto reset_flash;
        }
    }
    offset &= pfl->chip_len - 1;

    boff = offset;
    if (pfl->width == 2) {
        boff = boff >> 1;
    } else if (pfl->width == 4) {
        boff = boff >> 2;
    }
    boff &= 0x7FF;
    switch (pfl->wcycle) {
    case 0:
        if (pfl->rom_mode) {
            pfl->rom_mode = false;
            memory_region_rom_device_set_romd(&pfl->orig_mem, false);
        }
        pfl->read_counter = 0;
    check_unlock0:
        if (boff == 0x55 && cmd == 0x98) {
            pfl->wcycle = WCYCLE_CFI;
            pfl->cmd = 0x98;
            return;
        }
        if (cmd == 0x30) {
            if (pflash_erase_suspend_mode(pfl)) {
                timer_mod(&pfl->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                          pfl->erase_time_remaining);
                pfl->erase_time_remaining = 0;
                pfl->wcycle = 6;
                pfl->cmd = 0x30;
                set_dq7(pfl, 0x00);
                assert_dq3(pfl);
                return;
            }
            goto reset_flash;
        }
        if (cmd == 0xB0) {
            return;
        }
        if (boff != pfl->unlock_addr0 || cmd != 0xAA) {
            trace_pflash_unlock0_failed(pfl->name, boff,
                                        cmd, pfl->unlock_addr0);
            goto reset_flash;
        }
        trace_pflash_write(pfl->name, "unlock sequence started");
        break;
    case 1:
    check_unlock1:
        if (boff != pfl->unlock_addr1 || cmd != 0x55) {
            trace_pflash_unlock1_failed(pfl->name, boff, cmd);
            goto reset_flash;
        }
        trace_pflash_write(pfl->name, "unlock sequence done");
        break;
    case 2:
        if (!pfl->bypass && boff != pfl->unlock_addr0) {
            trace_pflash_write_failed(pfl->name, boff, cmd);
            goto reset_flash;
        }
        switch (cmd) {
        case 0x20:
            pfl->bypass = 1;
            goto do_bypass;
        case 0x80:
        case 0x90:
        case 0xA0:
            pfl->cmd = cmd;
            trace_pflash_write_start(pfl->name, cmd);
            break;
        default:
            trace_pflash_write_unknown(pfl->name, cmd);
            goto reset_flash;
        }
        break;
    case 3:
        switch (pfl->cmd) {
        case 0x80:
            goto check_unlock0;
        case 0xA0:
            if (pflash_erase_suspend_mode(pfl) &&
                pflash_sector_is_erasing(pfl, offset)) {
                if (pfl->bypass) {
                    goto do_bypass;
                }
                goto reset_flash;
            }
            trace_pflash_data_write(pfl->name, offset, width, value);
            if (!pfl->ro) {
                p = (uint8_t *)pfl->storage + offset;
                if (pfl->be) {
                    uint64_t current = ldn_be_p(p, width);
                    stn_be_p(p, width, current & value);
                } else {
                    uint64_t current = ldn_le_p(p, width);
                    stn_le_p(p, width, current & value);
                }
                pflash_update(pfl, offset, width);
            }
            set_dq7(pfl, ~value);
            if (pfl->bypass)
                goto do_bypass;
            goto reset_flash;
        case 0x90:
            if (pfl->bypass && cmd == 0x00) {
                goto reset_flash;
            }
            if (boff == 0x55 && cmd == 0x98) {
                pfl->wcycle = WCYCLE_AUTOSELECT_CFI;
                pfl->cmd = 0x98;
                return;
            }
            /* fall through */
        default:
            trace_pflash_write_invalid(pfl->name, pfl->cmd);
            goto reset_flash;
        }
    case 4:
        switch (pfl->cmd) {
        case 0xA0:
            return;
        case 0x80:
            goto check_unlock1;
        default:
            trace_pflash_write_invalid_state(pfl->name, pfl->cmd, 5);
            goto reset_flash;
        }
        break;
    case 5:
        if (pflash_erase_suspend_mode(pfl)) {
            goto reset_flash;
        }
        switch (cmd) {
        case 0x10:
            if (boff != pfl->unlock_addr0) {
                trace_pflash_chip_erase_invalid(pfl->name, offset);
                goto reset_flash;
            }
            trace_pflash_chip_erase_start(pfl->name);
            if (!pfl->ro) {
                memset(pfl->storage, 0xff, pfl->chip_len);
                pflash_update(pfl, 0, pfl->chip_len);
            }
            set_dq7(pfl, 0x00);
            timer_mod(&pfl->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                      (1ULL << pfl->cfi_table[0x22]) * SCALE_MS);
            break;
        case 0x30:
            pflash_sector_erase(pfl, offset);
            break;
        default:
            trace_pflash_write_invalid_command(pfl->name, cmd);
            goto reset_flash;
        }
        pfl->cmd = cmd;
        break;
    case 6:
        switch (pfl->cmd) {
        case 0x10:
            return;
        case 0x30:
            if (cmd == 0xB0) {
                if ((pfl->status & 0x08) == 0) {
                    pfl->erase_time_remaining = pflash_erase_time(pfl);
                } else {
                    int64_t delta = timer_expire_time_ns(&pfl->timer) -
                        qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
                    pfl->erase_time_remaining = delta <= 0 ? 1 : delta;
                }
                reset_dq3(pfl);
                timer_del(&pfl->timer);
                pflash_reset_state_machine(pfl);
                return;
            }
            if ((pfl->status & 0x08) == 0) {
                if (cmd == 0x30) {
                    pflash_sector_erase(pfl, offset);
                } else {
                    goto reset_flash;
                }
            }
            return;
        default:
            trace_pflash_write_invalid_state(pfl->name, pfl->cmd, 6);
            goto reset_flash;
        }
        break;
    case WCYCLE_CFI:
    case WCYCLE_AUTOSELECT_CFI:
        trace_pflash_write(pfl->name, "invalid write in CFI query mode");
        goto reset_flash;
    default:
        trace_pflash_write(pfl->name, "invalid write state (wc 7)");
        goto reset_flash;
    }
    pfl->wcycle++;

    return;

 reset_flash:
    pfl->bypass = 0;
    pflash_reset_state_machine(pfl);
    return;

 do_bypass:
    pfl->wcycle = 2;
    pfl->cmd = 0;
}

static const MemoryRegionOps pflash_cfi02_ops = {
    .read = pflash_read,
    .write = pflash_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void pflash_cfi02_fill_cfi_table(PFlashCFI02 *pfl, int nb_regions)
{
    const uint16_t pri_ofs = 0x40;
    pfl->cfi_table[0x10] = 'Q';
    pfl->cfi_table[0x11] = 'R';
    pfl->cfi_table[0x12] = 'Y';
    pfl->cfi_table[0x13] = 0x02;
    pfl->cfi_table[0x14] = 0x00;
    pfl->cfi_table[0x15] = pri_ofs;
    pfl->cfi_table[0x16] = pri_ofs >> 8;
    pfl->cfi_table[0x17] = 0x00;
    pfl->cfi_table[0x18] = 0x00;
    pfl->cfi_table[0x19] = 0x00;
    pfl->cfi_table[0x1A] = 0x00;
    pfl->cfi_table[0x1B] = 0x27;
    pfl->cfi_table[0x1C] = 0x36;
    pfl->cfi_table[0x1D] = 0x00;
    pfl->cfi_table[0x1E] = 0x00;
    pfl->cfi_table[0x1F] = 0x07;
    pfl->cfi_table[0x20] = 0x00;
    pfl->cfi_table[0x21] = 0x09;
    pfl->cfi_table[0x22] = 0x0C;
    pfl->cfi_table[0x23] = 0x01;
    pfl->cfi_table[0x24] = 0x00;
    pfl->cfi_table[0x25] = 0x0A;
    pfl->cfi_table[0x26] = 0x0D;
    pfl->cfi_table[0x27] = ctz32(pfl->chip_len);
    pfl->cfi_table[0x28] = 0x02;
    pfl->cfi_table[0x29] = 0x00;
    pfl->cfi_table[0x2A] = 0x00;
    pfl->cfi_table[0x2B] = 0x00;
    pfl->cfi_table[0x2c] = nb_regions;
    for (int i = 0; i < nb_regions; ++i) {
        uint32_t sector_len_per_device = pfl->sector_len[i];
        pfl->cfi_table[0x2d + 4 * i] = pfl->nb_blocs[i] - 1;
        pfl->cfi_table[0x2e + 4 * i] = (pfl->nb_blocs[i] - 1) >> 8;
        pfl->cfi_table[0x2f + 4 * i] = sector_len_per_device >> 8;
        pfl->cfi_table[0x30 + 4 * i] = sector_len_per_device >> 16;
    }
    assert(0x2c + 4 * nb_regions < pri_ofs);

    pfl->cfi_table[0x00 + pri_ofs] = 'P';
    pfl->cfi_table[0x01 + pri_ofs] = 'R';
    pfl->cfi_table[0x02 + pri_ofs] = 'I';

    pfl->cfi_table[0x03 + pri_ofs] = '1';
    pfl->cfi_table[0x04 + pri_ofs] = '0';

    pfl->cfi_table[0x05 + pri_ofs] = 0x00;
    pfl->cfi_table[0x06 + pri_ofs] = 0x02;
    pfl->cfi_table[0x07 + pri_ofs] = 0x00;
    pfl->cfi_table[0x08 + pri_ofs] = 0x00;

    pfl->cfi_table[0x09 + pri_ofs] = 0x00;

    pfl->cfi_table[0x0a + pri_ofs] = 0x00;
    pfl->cfi_table[0x0b + pri_ofs] = 0x00;
    pfl->cfi_table[0x0c + pri_ofs] = 0x00;
    assert(0x0c + pri_ofs < ARRAY_SIZE(pfl->cfi_table));
}

static void pflash_cfi02_realize(DeviceState *dev, Error **errp)
{
    ERRP_GUARD();
    PFlashCFI02 *pfl = PFLASH_CFI02(dev);
    int ret;

    if (pfl->uniform_sector_len == 0 && pfl->sector_len[0] == 0) {
        error_setg(errp, "attribute \"sector-length\" not specified or zero.");
        return;
    }
    if (pfl->uniform_nb_blocs == 0 && pfl->nb_blocs[0] == 0) {
        error_setg(errp, "attribute \"num-blocks\" not specified or zero.");
        return;
    }
    if (pfl->name == NULL) {
        error_setg(errp, "attribute \"name\" not specified.");
        return;
    }

    int nb_regions;
    pfl->chip_len = 0;
    pfl->total_sectors = 0;
    for (nb_regions = 0; nb_regions < PFLASH_MAX_ERASE_REGIONS; ++nb_regions) {
        if (pfl->nb_blocs[nb_regions] == 0) {
            break;
        }
        pfl->total_sectors += pfl->nb_blocs[nb_regions];
        uint64_t sector_len_per_device = pfl->sector_len[nb_regions];

        if (sector_len_per_device & 0xff ||
            sector_len_per_device >= (1 << 24) ||
            !is_power_of_2(sector_len_per_device))
        {
            error_setg(errp, "unsupported configuration: "
                       "sector length[%d] per device = %" PRIx64 ".",
                       nb_regions, sector_len_per_device);
            return;
        }
        if (pfl->chip_len & (sector_len_per_device - 1)) {
            error_setg(errp, "unsupported configuration: "
                       "flash region %d not correctly aligned.",
                       nb_regions);
            return;
        }

        pfl->chip_len += (uint64_t)pfl->sector_len[nb_regions] *
                          pfl->nb_blocs[nb_regions];
    }

    uint64_t uniform_len = (uint64_t)pfl->uniform_nb_blocs *
                           pfl->uniform_sector_len;
    if (nb_regions == 0) {
        nb_regions = 1;
        pfl->nb_blocs[0] = pfl->uniform_nb_blocs;
        pfl->sector_len[0] = pfl->uniform_sector_len;
        pfl->chip_len = uniform_len;
        pfl->total_sectors = pfl->uniform_nb_blocs;
    } else if (uniform_len != 0 && uniform_len != pfl->chip_len) {
        error_setg(errp, "\"num-blocks\"*\"sector-length\" "
                   "different from \"num-blocks0\"*\'sector-length0\" + ... + "
                   "\"num-blocks3\"*\"sector-length3\"");
        return;
    }

    memory_region_init_rom_device(&pfl->orig_mem, OBJECT(pfl),
                                  &pflash_cfi02_ops, pfl, pfl->name,
                                  pfl->chip_len, errp);
    if (*errp) {
        return;
    }

    pfl->storage = memory_region_get_ram_ptr(&pfl->orig_mem);

    if (pfl->blk) {
        uint64_t perm;
        pfl->ro = !blk_supports_write_perm(pfl->blk);
        perm = BLK_PERM_CONSISTENT_READ | (pfl->ro ? 0 : BLK_PERM_WRITE);
        ret = blk_set_perm(pfl->blk, perm, BLK_PERM_ALL, errp);
        if (ret < 0) {
            return;
        }
    } else {
        pfl->ro = 0;
    }

    if (pfl->blk) {
        if (!blk_check_size_and_read_all(pfl->blk, dev, pfl->storage,
                                         pfl->chip_len, errp)) {
            vmstate_unregister_ram(&pfl->orig_mem, DEVICE(pfl));
            return;
        }
    }

    pfl->unlock_addr0 &= 0x7FF;
    pfl->unlock_addr1 &= 0x7FF;

    pfl->sector_erase_map = bitmap_new(pfl->total_sectors);

    pfl->rom_mode = true;
    if (pfl->mappings > 1) {
        pflash_setup_mappings(pfl);
        sysbus_init_mmio(SYS_BUS_DEVICE(dev), &pfl->mem);
    } else {
        sysbus_init_mmio(SYS_BUS_DEVICE(dev), &pfl->orig_mem);
    }

    timer_init_ns(&pfl->timer, QEMU_CLOCK_VIRTUAL, pflash_timer, pfl);
    pfl->status = 0;

    pflash_cfi02_fill_cfi_table(pfl, nb_regions);
}

static void pflash_cfi02_reset(DeviceState *dev)
{
    PFlashCFI02 *pfl = PFLASH_CFI02(dev);

    pflash_reset_state_machine(pfl);
}

static const Property pflash_cfi02_properties[] = {
    DEFINE_PROP_DRIVE("drive", PFlashCFI02, blk),
    DEFINE_PROP_UINT32("num-blocks", PFlashCFI02, uniform_nb_blocs, 0),
    DEFINE_PROP_UINT32("sector-length", PFlashCFI02, uniform_sector_len, 0),
    DEFINE_PROP_UINT32("num-blocks0", PFlashCFI02, nb_blocs[0], 0),
    DEFINE_PROP_UINT32("sector-length0", PFlashCFI02, sector_len[0], 0),
    DEFINE_PROP_UINT32("num-blocks1", PFlashCFI02, nb_blocs[1], 0),
    DEFINE_PROP_UINT32("sector-length1", PFlashCFI02, sector_len[1], 0),
    DEFINE_PROP_UINT32("num-blocks2", PFlashCFI02, nb_blocs[2], 0),
    DEFINE_PROP_UINT32("sector-length2", PFlashCFI02, sector_len[2], 0),
    DEFINE_PROP_UINT32("num-blocks3", PFlashCFI02, nb_blocs[3], 0),
    DEFINE_PROP_UINT32("sector-length3", PFlashCFI02, sector_len[3], 0),
    DEFINE_PROP_UINT8("width", PFlashCFI02, width, 0),
    DEFINE_PROP_UINT8("mappings", PFlashCFI02, mappings, 0),
    DEFINE_PROP_UINT8("big-endian", PFlashCFI02, be, 0),
    DEFINE_PROP_UINT16("id0", PFlashCFI02, ident0, 0),
    DEFINE_PROP_UINT16("id1", PFlashCFI02, ident1, 0),
    DEFINE_PROP_UINT16("id2", PFlashCFI02, ident2, 0),
    DEFINE_PROP_UINT16("id3", PFlashCFI02, ident3, 0),
    DEFINE_PROP_UINT16("unlock-addr0", PFlashCFI02, unlock_addr0, 0),
    DEFINE_PROP_UINT16("unlock-addr1", PFlashCFI02, unlock_addr1, 0),
    DEFINE_PROP_STRING("name", PFlashCFI02, name),
};

static void pflash_cfi02_unrealize(DeviceState *dev)
{
    PFlashCFI02 *pfl = PFLASH_CFI02(dev);
    timer_del(&pfl->timer);
    g_free(pfl->sector_erase_map);
}

static void pflash_cfi02_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pflash_cfi02_realize;
    device_class_set_legacy_reset(dc, pflash_cfi02_reset);
    dc->unrealize = pflash_cfi02_unrealize;
    device_class_set_props(dc, pflash_cfi02_properties);
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo pflash_cfi02_info = {
    .name           = TYPE_PFLASH_CFI02,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(PFlashCFI02),
    .class_init     = pflash_cfi02_class_init,
};

static void pflash_cfi02_register_types(void)
{
    type_register_static(&pflash_cfi02_info);
}

type_init(pflash_cfi02_register_types)

PFlashCFI02 *pflash_cfi02_register(hwaddr base,
                                   const char *name,
                                   hwaddr size,
                                   BlockBackend *blk,
                                   uint32_t sector_len,
                                   int nb_mappings, int width,
                                   uint16_t id0, uint16_t id1,
                                   uint16_t id2, uint16_t id3,
                                   uint16_t unlock_addr0,
                                   uint16_t unlock_addr1,
                                   int be)
{
    DeviceState *dev = qdev_new(TYPE_PFLASH_CFI02);

    if (blk) {
        qdev_prop_set_drive(dev, "drive", blk);
    }
    assert(QEMU_IS_ALIGNED(size, sector_len));
    qdev_prop_set_uint32(dev, "num-blocks", size / sector_len);
    qdev_prop_set_uint32(dev, "sector-length", sector_len);
    qdev_prop_set_uint8(dev, "width", width);
    qdev_prop_set_uint8(dev, "mappings", nb_mappings);
    qdev_prop_set_uint8(dev, "big-endian", !!be);
    qdev_prop_set_uint16(dev, "id0", id0);
    qdev_prop_set_uint16(dev, "id1", id1);
    qdev_prop_set_uint16(dev, "id2", id2);
    qdev_prop_set_uint16(dev, "id3", id3);
    qdev_prop_set_uint16(dev, "unlock-addr0", unlock_addr0);
    qdev_prop_set_uint16(dev, "unlock-addr1", unlock_addr1);
    qdev_prop_set_string(dev, "name", name);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    return PFLASH_CFI02(dev);
}
