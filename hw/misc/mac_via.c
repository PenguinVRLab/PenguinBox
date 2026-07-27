/*
 * QEMU m68k Macintosh VIA device support
 *
 * Copyright (c) 2011-2018 Laurent Vivier
 * Copyright (c) 2018 Mark Cave-Ayland
 *
 * Some parts from hw/misc/macio/cuda.c
 *
 * Copyright (c) 2004-2007 Fabrice Bellard
 * Copyright (c) 2007 Jocelyn Mayer
 *
 * some parts from linux-2.6.29, arch/m68k/include/asm/mac_via.h
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "system/address-spaces.h"
#include "migration/vmstate.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "qemu/timer.h"
#include "hw/misc/mac_via.h"
#include "hw/misc/mos6522.h"
#include "hw/input/adb.h"
#include "system/runstate.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "system/block-backend.h"
#include "system/rtc.h"
#include "trace.h"
#include "qemu/log.h"

#define VIA1A_vSccWrReq 0x80
#define VIA1A_vRev8     0x40
#define VIA1A_vHeadSel  0x20
#define VIA1A_vOverlay  0x10
#define VIA1A_vSync     0x08

#define VIA1A_vVolume   0x07
#define VIA1A_CPUID0    0x02
#define VIA1A_CPUID1    0x04
#define VIA1A_CPUID2    0x10
#define VIA1A_CPUID3    0x40
#define VIA1A_CPUID_MASK (VIA1A_CPUID0 | VIA1A_CPUID1 | \
                          VIA1A_CPUID2 | VIA1A_CPUID3)
#define VIA1A_CPUID_Q800 (VIA1A_CPUID0 | VIA1A_CPUID2)

#define VIA1B_vSound   0x80
#define VIA1B_vMystery 0x40
#define VIA1B_vADBS2   0x20
#define VIA1B_vADBS1   0x10
#define VIA1B_vADBInt  0x08
#define VIA1B_vRTCEnb  0x04
#define VIA1B_vRTCClk  0x02
#define VIA1B_vRTCData 0x01

#define VIA2A_vRAM1    0x80
#define VIA2A_vRAM0    0x40
#define VIA2A_vIRQE    0x20
#define VIA2A_vIRQD    0x10
#define VIA2A_vIRQC    0x08
#define VIA2A_vIRQB    0x04
#define VIA2A_vIRQA    0x02
#define VIA2A_vIRQ9    0x01

#define VIA2B_vVBL    0x80
#define VIA2B_vSndJck 0x40
#define VIA2B_vTfr0   0x20
#define VIA2B_vTfr1   0x10
#define VIA2B_vMode32 0x08
#define VIA2B_vPower  0x04
#define VIA2B_vBusLk  0x02
#define VIA2B_vCDis   0x01

#define IRQ_SET         0x80

#define VIA_IRQ_TIMER1      0x40
#define VIA_IRQ_TIMER2      0x20

#define vBufB    0x0000
#define vBufAH   0x0200
#define vDirB    0x0400
#define vDirA    0x0600
#define vT1CL    0x0800
#define vT1CH    0x0a00
#define vT1LL    0x0c00
#define vT1LH    0x0e00
#define vT2CL    0x1000
#define vT2CH    0x1200
#define vSR      0x1400
#define vACR     0x1600
#define vPCR     0x1800
#define vIFR     0x1a00
#define vIER     0x1c00
#define vBufA    0x1e00

/* from linux 2.6 drivers/macintosh/via-macii.c */

#define VIA1ACR_vShiftCtrl         0x1c
#define VIA1ACR_vShiftExtClk       0x0c
#define VIA1ACR_vShiftOut          0x10

#define ADB_STATE_NEW       0
#define ADB_STATE_EVEN      1
#define ADB_STATE_ODD       2
#define ADB_STATE_IDLE      3

#define VIA1B_vADB_StateMask    (VIA1B_vADBS1 | VIA1B_vADBS2)
#define VIA1B_vADB_StateShift   4

#define VIA_TIMER_FREQ (783360)
#define VIA_ADB_POLL_FREQ 50

#define VIA_60HZ_TIMER_PERIOD_NS   16625800

#define RTC_OFFSET 2082844800

enum {
    REG_0,
    REG_1,
    REG_2,
    REG_3,
    REG_TEST,
    REG_WPROTECT,
    REG_PRAM_ADDR,
    REG_PRAM_ADDR_LAST = REG_PRAM_ADDR + 19,
    REG_PRAM_SECT,
    REG_PRAM_SECT_LAST = REG_PRAM_SECT + 7,
    REG_INVALID,
    REG_EMPTY = 0xff,
};

static void via1_sixty_hz_update(MOS6522Q800VIA1State *v1s)
{
    v1s->next_sixty_hz = (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                          VIA_60HZ_TIMER_PERIOD_NS) /
                          VIA_60HZ_TIMER_PERIOD_NS * VIA_60HZ_TIMER_PERIOD_NS;
    timer_mod(v1s->sixty_hz_timer, v1s->next_sixty_hz);
}

static void via1_one_second_update(MOS6522Q800VIA1State *v1s)
{
    v1s->next_second = (qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1000) /
                       1000 * 1000;
    timer_mod(v1s->one_second_timer, v1s->next_second);
}

static void via1_sixty_hz(void *opaque)
{
    MOS6522Q800VIA1State *v1s = opaque;
    MOS6522State *s = MOS6522(v1s);
    qemu_irq irq = qdev_get_gpio_in(DEVICE(s), VIA1_IRQ_60HZ_BIT);

    qemu_irq_lower(irq);
    qemu_irq_raise(irq);

    via1_sixty_hz_update(v1s);
}

static void via1_one_second(void *opaque)
{
    MOS6522Q800VIA1State *v1s = opaque;
    MOS6522State *s = MOS6522(v1s);
    qemu_irq irq = qdev_get_gpio_in(DEVICE(s), VIA1_IRQ_ONE_SECOND_BIT);

    qemu_irq_lower(irq);
    qemu_irq_raise(irq);

    via1_one_second_update(v1s);
}


static void pram_update(MOS6522Q800VIA1State *v1s)
{
    if (v1s->blk) {
        if (blk_pwrite(v1s->blk, 0, sizeof(v1s->PRAM), v1s->PRAM, 0) < 0) {
            qemu_log("pram_update: cannot write to file\n");
        }
    }
}

static int via1_rtc_compact_cmd(uint8_t value)
{
    uint8_t read = value & 0x80;

    value &= 0x7f;

    if ((value & 0x78) == 0x38) {
        return read | (REG_PRAM_SECT + (value & 0x07));
    }
    if ((value & 0x03) == 0x01) {
        value >>= 2;
        if ((value & 0x18) == 0) {
            return read | (REG_0 + (value & 0x03));
        } else if ((value == 0x0c) && !read) {
            return REG_TEST;
        } else if ((value == 0x0d) && !read) {
            return REG_WPROTECT;
        } else if ((value & 0x1c) == 0x08) {
            return read | (REG_PRAM_ADDR + 0x10 + (value & 0x03));
        } else if ((value & 0x10) == 0x10) {
            return read | (REG_PRAM_ADDR + (value & 0x0f));
        }
    }
    return REG_INVALID;
}

static void via1_rtc_update(MOS6522Q800VIA1State *v1s)
{
    MOS6522State *s = MOS6522(v1s);
    int cmd, sector, addr;
    uint32_t time;

    if (s->b & VIA1B_vRTCEnb) {
        return;
    }

    if (s->dirb & VIA1B_vRTCData) {
        if (!(v1s->last_b & VIA1B_vRTCClk) && (s->b & VIA1B_vRTCClk)) {
            v1s->data_out <<= 1;
            v1s->data_out |= s->b & VIA1B_vRTCData;
            v1s->data_out_cnt++;
        }
        trace_via1_rtc_update_data_out(v1s->data_out_cnt, v1s->data_out);
    } else {
        trace_via1_rtc_update_data_in(v1s->data_in_cnt, v1s->data_in);
        if ((v1s->last_b & VIA1B_vRTCClk) &&
            !(s->b & VIA1B_vRTCClk) &&
            v1s->data_in_cnt) {
            s->b = (s->b & ~VIA1B_vRTCData) |
                   ((v1s->data_in >> 7) & VIA1B_vRTCData);
            v1s->data_in <<= 1;
            v1s->data_in_cnt--;
        }
        return;
    }

    if (v1s->data_out_cnt != 8) {
        return;
    }

    v1s->data_out_cnt = 0;

    trace_via1_rtc_internal_status(v1s->cmd, v1s->alt, v1s->data_out);
    if (v1s->cmd == REG_EMPTY) {

        cmd = via1_rtc_compact_cmd(v1s->data_out);
        trace_via1_rtc_internal_cmd(cmd);

        if (cmd == REG_INVALID) {
            trace_via1_rtc_cmd_invalid(v1s->data_out);
            return;
        }

        if (cmd & 0x80) {
            switch (cmd & 0x7f) {
            case REG_0...REG_3:

                time = v1s->tick_offset + (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)
                       / NANOSECONDS_PER_SECOND);
                trace_via1_rtc_internal_time(time);
                v1s->data_in = (time >> ((cmd & 0x03) << 3)) & 0xff;
                v1s->data_in_cnt = 8;
                trace_via1_rtc_cmd_seconds_read((cmd & 0x7f) - REG_0,
                                                v1s->data_in);
                break;
            case REG_PRAM_ADDR...REG_PRAM_ADDR_LAST:
                v1s->data_in = v1s->PRAM[(cmd & 0x7f) - REG_PRAM_ADDR];
                v1s->data_in_cnt = 8;
                trace_via1_rtc_cmd_pram_read((cmd & 0x7f) - REG_PRAM_ADDR,
                                             v1s->data_in);
                break;
            case REG_PRAM_SECT...REG_PRAM_SECT_LAST:
                trace_via1_rtc_internal_set_cmd(cmd);
                v1s->cmd = cmd;
                break;
            default:
                g_assert_not_reached();
            }
            return;
        }

        if (cmd == REG_WPROTECT || !v1s->wprotect) {
            trace_via1_rtc_internal_set_cmd(cmd);
            v1s->cmd = cmd;
        } else {
            trace_via1_rtc_internal_ignore_cmd(cmd);
        }
        return;
    }

    if (v1s->alt == REG_EMPTY) {
        switch (v1s->cmd & 0x7f) {
        case REG_0...REG_3:
            trace_via1_rtc_cmd_seconds_write(v1s->cmd - REG_0, v1s->data_out);
            v1s->cmd = REG_EMPTY;
            break;
        case REG_TEST:
            trace_via1_rtc_cmd_test_write(v1s->data_out);
            v1s->cmd = REG_EMPTY;
            break;
        case REG_WPROTECT:
            trace_via1_rtc_cmd_wprotect_write(v1s->data_out);
            v1s->wprotect = !!(v1s->data_out & 0x80);
            v1s->cmd = REG_EMPTY;
            break;
        case REG_PRAM_ADDR...REG_PRAM_ADDR_LAST:
            trace_via1_rtc_cmd_pram_write(v1s->cmd - REG_PRAM_ADDR,
                                          v1s->data_out);
            v1s->PRAM[v1s->cmd - REG_PRAM_ADDR] = v1s->data_out;
            pram_update(v1s);
            v1s->cmd = REG_EMPTY;
            break;
        case REG_PRAM_SECT...REG_PRAM_SECT_LAST:
            addr = (v1s->data_out >> 2) & 0x1f;
            sector = (v1s->cmd & 0x7f) - REG_PRAM_SECT;
            if (v1s->cmd & 0x80) {
                v1s->data_in = v1s->PRAM[sector * 32 + addr];
                v1s->data_in_cnt = 8;
                trace_via1_rtc_cmd_pram_sect_read(sector, addr,
                                                  sector * 32 + addr,
                                                  v1s->data_in);
                v1s->cmd = REG_EMPTY;
            } else {
                trace_via1_rtc_internal_set_alt(addr, sector, addr);
                v1s->alt = addr;
            }
            break;
        default:
            g_assert_not_reached();
        }
        return;
    }

    g_assert(REG_PRAM_SECT <= v1s->cmd && v1s->cmd <= REG_PRAM_SECT_LAST);
    sector = v1s->cmd - REG_PRAM_SECT;
    v1s->PRAM[sector * 32 + v1s->alt] = v1s->data_out;
    pram_update(v1s);
    trace_via1_rtc_cmd_pram_sect_write(sector, v1s->alt, sector * 32 + v1s->alt,
                                       v1s->data_out);
    v1s->alt = REG_EMPTY;
    v1s->cmd = REG_EMPTY;
}

static void adb_via_poll(void *opaque)
{
    MOS6522Q800VIA1State *v1s = MOS6522_Q800_VIA1(opaque);
    MOS6522State *s = MOS6522(v1s);
    ADBBusState *adb_bus = &v1s->adb_bus;
    uint8_t obuf[9];
    uint8_t *data = &s->sr;
    int olen;

    adb_autopoll_block(adb_bus);

    if (v1s->adb_data_in_size > 0 && v1s->adb_data_in_index == 0) {
        *data = v1s->adb_data_out[0];
        olen = v1s->adb_data_in_size;

        s->b &= ~VIA1B_vADBInt;
        qemu_irq_raise(v1s->adb_data_ready);
    } else {
        v1s->adb_data_in_index = 0;
        v1s->adb_data_out_index = 0;
        olen = adb_poll(adb_bus, obuf, adb_bus->autopoll_mask);

        if (olen > 0) {
            *data = obuf[0];
            olen--;
            memcpy(v1s->adb_data_in, &obuf[1], olen);
            v1s->adb_data_in_size = olen;

            s->b &= ~VIA1B_vADBInt;
            qemu_irq_raise(v1s->adb_data_ready);
        } else {
            *data = v1s->adb_autopoll_cmd;
            obuf[0] = 0xff;
            obuf[1] = 0xff;
            olen = 2;

            memcpy(v1s->adb_data_in, obuf, olen);
            v1s->adb_data_in_size = olen;

            s->b &= ~VIA1B_vADBInt;
            qemu_irq_raise(v1s->adb_data_ready);
        }
    }

    trace_via1_adb_poll(*data, (s->b & VIA1B_vADBInt) ? "+" : "-",
                        adb_bus->status, v1s->adb_data_in_index, olen);
}

static int adb_via_send_len(uint8_t data)
{
    uint8_t cmd = data & 0xc;
    uint8_t reg = data & 0x3;

    switch (cmd) {
    case 0x8:
        switch (reg) {
        case 2:
            return 3;
        case 3:
            return 3;
        default:
            qemu_log_mask(LOG_UNIMP, "ADB unknown length for register %d\n",
                          reg);
            return 1;
        }
    default:
        return 1;
    }
}

static void adb_via_send(MOS6522Q800VIA1State *v1s, int state, uint8_t data)
{
    MOS6522State *ms = MOS6522(v1s);
    ADBBusState *adb_bus = &v1s->adb_bus;
    uint16_t autopoll_mask;

    switch (state) {
    case ADB_STATE_NEW:
        adb_autopoll_block(adb_bus);

        if (adb_bus->status & ADB_STATUS_POLLREPLY) {
            ms->b &= ~VIA1B_vADBInt;
        } else {
            ms->b |= VIA1B_vADBInt;
            v1s->adb_data_out_index = 0;
            v1s->adb_data_out[v1s->adb_data_out_index++] = data;
        }

        trace_via1_adb_send(" NEW", data, (ms->b & VIA1B_vADBInt) ? "+" : "-");
        qemu_irq_raise(v1s->adb_data_ready);
        break;

    case ADB_STATE_EVEN:
    case ADB_STATE_ODD:
        ms->b |= VIA1B_vADBInt;
        v1s->adb_data_out[v1s->adb_data_out_index++] = data;

        trace_via1_adb_send(state == ADB_STATE_EVEN ? "EVEN" : " ODD",
                            data, (ms->b & VIA1B_vADBInt) ? "+" : "-");
        qemu_irq_raise(v1s->adb_data_ready);
        break;

    case ADB_STATE_IDLE:
        ms->b |= VIA1B_vADBInt;
        adb_autopoll_unblock(adb_bus);

        trace_via1_adb_send("IDLE", data,
                            (ms->b & VIA1B_vADBInt) ? "+" : "-");

        return;
    }

    if (v1s->adb_data_out_index == adb_via_send_len(v1s->adb_data_out[0])) {
        v1s->adb_data_in_size = adb_request(adb_bus, v1s->adb_data_in,
                                            v1s->adb_data_out,
                                            v1s->adb_data_out_index);
        v1s->adb_data_in_index = 0;

        if (adb_bus->status & ADB_STATUS_BUSTIMEOUT) {
            v1s->adb_data_in[0] = 0xff;
            v1s->adb_data_in[1] = 0xff;
            v1s->adb_data_in_size = 2;
        }

        if ((v1s->adb_data_out[0] & 0xc) == 0xc) {
            v1s->adb_autopoll_cmd = v1s->adb_data_out[0];

            autopoll_mask = 1 << (v1s->adb_autopoll_cmd >> 4);
            adb_set_autopoll_mask(adb_bus, autopoll_mask);
        }
    }
}

static void adb_via_receive(MOS6522Q800VIA1State *v1s, int state, uint8_t *data)
{
    MOS6522State *ms = MOS6522(v1s);
    ADBBusState *adb_bus = &v1s->adb_bus;
    uint16_t pending;

    switch (state) {
    case ADB_STATE_NEW:
        ms->b |= VIA1B_vADBInt;
        return;

    case ADB_STATE_IDLE:
        ms->b |= VIA1B_vADBInt;
        adb_autopoll_unblock(adb_bus);

        trace_via1_adb_receive("IDLE", *data,
                        (ms->b & VIA1B_vADBInt) ? "+" : "-", adb_bus->status,
                        v1s->adb_data_in_index, v1s->adb_data_in_size);

        break;

    case ADB_STATE_EVEN:
    case ADB_STATE_ODD:
        switch (v1s->adb_data_in_index) {
        case 0:
            *data = v1s->adb_data_in[v1s->adb_data_in_index];
            if (adb_bus->status & ADB_STATUS_BUSTIMEOUT) {
                ms->b &= ~VIA1B_vADBInt;
            } else {
                ms->b |= VIA1B_vADBInt;
            }

            trace_via1_adb_receive(state == ADB_STATE_EVEN ? "EVEN" : " ODD",
                                   *data, (ms->b & VIA1B_vADBInt) ? "+" : "-",
                                   adb_bus->status, v1s->adb_data_in_index,
                                   v1s->adb_data_in_size);

            v1s->adb_data_in_index++;
            break;

        case 1:
            *data = v1s->adb_data_in[v1s->adb_data_in_index];
            pending = adb_bus->pending & ~(1 << (v1s->adb_autopoll_cmd >> 4));
            if (pending) {
                ms->b &= ~VIA1B_vADBInt;
            } else {
                ms->b |= VIA1B_vADBInt;
            }

            trace_via1_adb_receive(state == ADB_STATE_EVEN ? "EVEN" : " ODD",
                                   *data, (ms->b & VIA1B_vADBInt) ? "+" : "-",
                                   adb_bus->status, v1s->adb_data_in_index,
                                   v1s->adb_data_in_size);

            v1s->adb_data_in_index++;
            break;

        default:
            if (v1s->adb_data_in_index < v1s->adb_data_in_size) {
                *data = v1s->adb_data_in[v1s->adb_data_in_index];
                ms->b |= VIA1B_vADBInt;
            } else if (v1s->adb_data_in_index == v1s->adb_data_in_size) {
                if (adb_bus->status & ADB_STATUS_BUSTIMEOUT) {
                    *data = 0xff;
                } else {
                    *data = 0;
                }
                ms->b &= ~VIA1B_vADBInt;
            } else {
                *data = 0xff;
                ms->b &= ~VIA1B_vADBInt;
                adb_bus->status = 0;
                adb_autopoll_unblock(adb_bus);
            }

            trace_via1_adb_receive(state == ADB_STATE_EVEN ? "EVEN" : " ODD",
                                   *data, (ms->b & VIA1B_vADBInt) ? "+" : "-",
                                   adb_bus->status, v1s->adb_data_in_index,
                                   v1s->adb_data_in_size);

            if (v1s->adb_data_in_index <= v1s->adb_data_in_size) {
                v1s->adb_data_in_index++;
            }
            break;
        }

        qemu_irq_raise(v1s->adb_data_ready);
        break;
    }
}

static void via1_adb_update(MOS6522Q800VIA1State *v1s)
{
    MOS6522State *s = MOS6522(v1s);
    int oldstate, state;

    oldstate = (v1s->last_b & VIA1B_vADB_StateMask) >> VIA1B_vADB_StateShift;
    state = (s->b & VIA1B_vADB_StateMask) >> VIA1B_vADB_StateShift;

    if (state != oldstate) {
        if (s->acr & VIA1ACR_vShiftOut) {
            adb_via_send(v1s, state, s->sr);
        } else {
            adb_via_receive(v1s, state, &s->sr);
        }
    }
}

static void via1_auxmode_update(MOS6522Q800VIA1State *v1s)
{
    MOS6522State *s = MOS6522(v1s);
    int oldirq, irq;

    oldirq = (v1s->last_b & VIA1B_vMystery) ? 1 : 0;
    irq = (s->b & VIA1B_vMystery) ? 1 : 0;

    if (irq != oldirq) {
        trace_via1_auxmode(irq);
        qemu_set_irq(v1s->auxmode_irq, irq);

        s->b |= VIA1B_vADBInt;
    }
}

#define MACOS_TIMEDBRA        0xd00
#define MACOS_TIMESCCB        0xd02

#define MACOS_TIMEDBRA_VALUE  (0x2a00 * 3)
#define MACOS_TIMESCCB_VALUE  (0x079d * 3)

static bool via1_is_toolbox_timer_calibrated(void)
{
    uint16_t timedbra = lduw_be_phys(&address_space_memory, MACOS_TIMEDBRA);
    uint16_t timesccdb = lduw_be_phys(&address_space_memory, MACOS_TIMESCCB);

    return (timedbra == MACOS_TIMEDBRA_VALUE &&
            timesccdb == MACOS_TIMESCCB_VALUE);
}

static void via1_timer_calibration_hack(MOS6522Q800VIA1State *v1s, int addr,
                                        uint64_t val, int size)
{
    int old_timer_hack_state = v1s->timer_hack_state;

    switch (v1s->timer_hack_state) {
    case 0:
        if (addr == VIA_REG_PCR && val == 0x22) {
            v1s->timer_hack_state = 1;
        }
        break;
    case 1:
        if (addr == VIA_REG_T2CL && val == 0xc) {
            if (!via1_is_toolbox_timer_calibrated()) {
                v1s->timer_hack_state = 2;
            } else {
                v1s->timer_hack_state = 0;
            }
        }
        break;
    case 2:
        if (addr == VIA_REG_T2CH && val == 0x3) {
            if (!via1_is_toolbox_timer_calibrated()) {
                v1s->timer_hack_state = 3;
            } else {
                v1s->timer_hack_state = 0;
            }
        }
        break;
    case 3:
        if (addr == VIA_REG_IER && val == 0x20) {
            stw_be_phys(&address_space_memory, MACOS_TIMEDBRA,
                        MACOS_TIMEDBRA_VALUE);
            stw_be_phys(&address_space_memory, MACOS_TIMESCCB,
                        MACOS_TIMESCCB_VALUE);

            v1s->timer_hack_state = 4;
        }
        break;
    case 4:
        if (addr == VIA_REG_PCR && val == 0x22) {
            v1s->timer_hack_state = 1;
        }

        if (addr == VIA_REG_T2CL && val == 0xf0) {
            v1s->timer_hack_state = 5;
        }
        break;
    case 5:
        if (addr == VIA_REG_T2CH && val == 0x3c) {
            v1s->timer_hack_state = 6;
        } else if ((addr == VIA_REG_IER && val == 0x20) ||
                   addr == VIA_REG_T2CH) {
            v1s->timer_hack_state = 0;
        }
        break;
    case 6:
        if ((addr == VIA_REG_IER && val == 0x20) || addr == VIA_REG_T2CH) {
            v1s->timer_hack_state = 7;
        } else {
            v1s->timer_hack_state = 0;
        }
        break;
    case 7:
        if (addr == VIA_REG_PCR && val == 0x22) {
            v1s->timer_hack_state = 1;
        }
        break;
    default:
        g_assert_not_reached();
    }

    if (old_timer_hack_state != v1s->timer_hack_state) {
        trace_via1_timer_hack_state(v1s->timer_hack_state);
    }
}

static uint64_t mos6522_q800_via1_read(void *opaque, hwaddr addr, unsigned size)
{
    MOS6522Q800VIA1State *s = MOS6522_Q800_VIA1(opaque);
    MOS6522State *ms = MOS6522(s);
    uint64_t ret;
    int64_t now;

    addr = (addr >> 9) & 0xf;
    ret = mos6522_read(ms, addr, size);
    switch (addr) {
    case VIA_REG_A:
    case VIA_REG_ANH:
        ret = (ret & ~VIA1A_CPUID_MASK) | VIA1A_CPUID_Q800;
        break;
    case VIA_REG_T2CH:
        if (s->timer_hack_state == 6) {
            now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            if (now & 1) {
                ret += 0x5;
            }
        }
        break;
    }
    return ret;
}

static void mos6522_q800_via1_write(void *opaque, hwaddr addr, uint64_t val,
                                    unsigned size)
{
    MOS6522Q800VIA1State *v1s = MOS6522_Q800_VIA1(opaque);
    MOS6522State *ms = MOS6522(v1s);
    int oldstate, state;
    int oldsr = ms->sr;

    addr = (addr >> 9) & 0xf;

    via1_timer_calibration_hack(v1s, addr, val, size);

    mos6522_write(ms, addr, val, size);

    switch (addr) {
    case VIA_REG_B:
        via1_rtc_update(v1s);
        via1_adb_update(v1s);
        via1_auxmode_update(v1s);

        v1s->last_b = ms->b;
        break;

    case VIA_REG_SR:
        {
            oldstate = (v1s->last_b & VIA1B_vADB_StateMask) >>
                       VIA1B_vADB_StateShift;
            state = (ms->b & VIA1B_vADB_StateMask) >> VIA1B_vADB_StateShift;

            if (oldstate == ADB_STATE_NEW && state == ADB_STATE_NEW &&
                    (ms->acr & VIA1ACR_vShiftOut) &&
                    oldsr == 0 ) {
                trace_via1_adb_netbsd_enum_hack();
                adb_via_send(v1s, state, ms->sr);
            }
        }
        break;
    }
}

static const MemoryRegionOps mos6522_q800_via1_ops = {
    .read = mos6522_q800_via1_read,
    .write = mos6522_q800_via1_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static uint64_t mos6522_q800_via2_read(void *opaque, hwaddr addr, unsigned size)
{
    MOS6522Q800VIA2State *s = MOS6522_Q800_VIA2(opaque);
    MOS6522State *ms = MOS6522(s);
    uint64_t val;

    addr = (addr >> 9) & 0xf;
    val = mos6522_read(ms, addr, size);

    switch (addr) {
    case VIA_REG_IFR:
        val &= ~VIA2_IRQ_SCSI_DATA;
        val |= (~ms->last_irq_levels & VIA2_IRQ_SCSI_DATA);
        break;
    }

    return val;
}

static void mos6522_q800_via2_write(void *opaque, hwaddr addr, uint64_t val,
                                    unsigned size)
{
    MOS6522Q800VIA2State *s = MOS6522_Q800_VIA2(opaque);
    MOS6522State *ms = MOS6522(s);

    addr = (addr >> 9) & 0xf;
    mos6522_write(ms, addr, val, size);
}

static const MemoryRegionOps mos6522_q800_via2_ops = {
    .read = mos6522_q800_via2_read,
    .write = mos6522_q800_via2_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void via1_postload_update_cb(void *opaque, bool running, RunState state)
{
    MOS6522Q800VIA1State *v1s = MOS6522_Q800_VIA1(opaque);

    qemu_del_vm_change_state_handler(v1s->vmstate);
    v1s->vmstate = NULL;

    pram_update(v1s);
}

static int via1_post_load(void *opaque, int version_id)
{
    MOS6522Q800VIA1State *v1s = MOS6522_Q800_VIA1(opaque);

    if (v1s->blk) {
        v1s->vmstate = qemu_add_vm_change_state_handler(
                           via1_postload_update_cb, v1s);
    }

    return 0;
}

static void mos6522_q800_via1_reset_hold(Object *obj, ResetType type)
{
    MOS6522Q800VIA1State *v1s = MOS6522_Q800_VIA1(obj);
    MOS6522State *ms = MOS6522(v1s);
    MOS6522DeviceClass *mdc = MOS6522_GET_CLASS(ms);
    ADBBusState *adb_bus = &v1s->adb_bus;

    if (mdc->parent_phases.hold) {
        mdc->parent_phases.hold(obj, type);
    }

    ms->timers[0].frequency = VIA_TIMER_FREQ;
    ms->timers[1].frequency = VIA_TIMER_FREQ;

    ms->b = VIA1B_vADB_StateMask | VIA1B_vADBInt | VIA1B_vRTCEnb;

    adb_set_autopoll_enabled(adb_bus, true);
    v1s->cmd = REG_EMPTY;
    v1s->alt = REG_EMPTY;

    v1s->timer_hack_state = 0;
}

static void mos6522_q800_via1_realize(DeviceState *dev, Error **errp)
{
    MOS6522Q800VIA1State *v1s = MOS6522_Q800_VIA1(dev);
    ADBBusState *adb_bus = &v1s->adb_bus;
    struct tm tm;
    int ret;

    v1s->one_second_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, via1_one_second,
                                         v1s);
    via1_one_second_update(v1s);
    v1s->sixty_hz_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, via1_sixty_hz,
                                       v1s);
    via1_sixty_hz_update(v1s);

    qemu_get_timedate(&tm, 0);
    v1s->tick_offset = (uint32_t)mktimegm(&tm) + RTC_OFFSET;

    adb_register_autopoll_callback(adb_bus, adb_via_poll, v1s);
    v1s->adb_data_ready = qdev_get_gpio_in(dev, VIA1_IRQ_ADB_READY_BIT);

    if (v1s->blk) {
        int64_t len = blk_getlength(v1s->blk);
        if (len < 0) {
            error_setg_errno(errp, -len,
                             "could not get length of backing image");
            return;
        }
        ret = blk_set_perm(v1s->blk,
                           BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE,
                           BLK_PERM_ALL, errp);
        if (ret < 0) {
            return;
        }

        ret = blk_pread(v1s->blk, 0, sizeof(v1s->PRAM), v1s->PRAM, 0);
        if (ret < 0) {
            error_setg(errp, "can't read PRAM contents");
            return;
        }
    }
}

static void mos6522_q800_via1_init(Object *obj)
{
    MOS6522Q800VIA1State *v1s = MOS6522_Q800_VIA1(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(v1s);

    memory_region_init_io(&v1s->via_mem, obj, &mos6522_q800_via1_ops, v1s,
                          "via1", VIA_SIZE);
    sysbus_init_mmio(sbd, &v1s->via_mem);

    qbus_init((BusState *)&v1s->adb_bus, sizeof(v1s->adb_bus),
              TYPE_ADB_BUS, DEVICE(v1s), "adb.0");

    qdev_init_gpio_out(DEVICE(obj), &v1s->auxmode_irq, 1);
}

static const VMStateDescription vmstate_q800_via1 = {
    .name = "q800-via1",
    .version_id = 0,
    .minimum_version_id = 0,
    .post_load = via1_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(parent_obj, MOS6522Q800VIA1State, 0, vmstate_mos6522,
                       MOS6522State),
        VMSTATE_UINT8(last_b, MOS6522Q800VIA1State),
        VMSTATE_BUFFER(PRAM, MOS6522Q800VIA1State),
        VMSTATE_UINT32(tick_offset, MOS6522Q800VIA1State),
        VMSTATE_UINT8(data_out, MOS6522Q800VIA1State),
        VMSTATE_INT32(data_out_cnt, MOS6522Q800VIA1State),
        VMSTATE_UINT8(data_in, MOS6522Q800VIA1State),
        VMSTATE_UINT8(data_in_cnt, MOS6522Q800VIA1State),
        VMSTATE_UINT8(cmd, MOS6522Q800VIA1State),
        VMSTATE_INT32(wprotect, MOS6522Q800VIA1State),
        VMSTATE_INT32(alt, MOS6522Q800VIA1State),
        VMSTATE_INT32(adb_data_in_size, MOS6522Q800VIA1State),
        VMSTATE_INT32(adb_data_in_index, MOS6522Q800VIA1State),
        VMSTATE_INT32(adb_data_out_index, MOS6522Q800VIA1State),
        VMSTATE_BUFFER(adb_data_in, MOS6522Q800VIA1State),
        VMSTATE_BUFFER(adb_data_out, MOS6522Q800VIA1State),
        VMSTATE_UINT8(adb_autopoll_cmd, MOS6522Q800VIA1State),
        VMSTATE_TIMER_PTR(one_second_timer, MOS6522Q800VIA1State),
        VMSTATE_INT64(next_second, MOS6522Q800VIA1State),
        VMSTATE_TIMER_PTR(sixty_hz_timer, MOS6522Q800VIA1State),
        VMSTATE_INT64(next_sixty_hz, MOS6522Q800VIA1State),
        VMSTATE_INT32(timer_hack_state, MOS6522Q800VIA1State),
        VMSTATE_END_OF_LIST()
    }
};

static const Property mos6522_q800_via1_properties[] = {
    DEFINE_PROP_DRIVE("drive", MOS6522Q800VIA1State, blk),
};

static void mos6522_q800_via1_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);
    MOS6522DeviceClass *mdc = MOS6522_CLASS(oc);

    dc->realize = mos6522_q800_via1_realize;
    resettable_class_set_parent_phases(rc, NULL, mos6522_q800_via1_reset_hold,
                                       NULL, &mdc->parent_phases);
    dc->vmsd = &vmstate_q800_via1;
    device_class_set_props(dc, mos6522_q800_via1_properties);
}

static const TypeInfo mos6522_q800_via1_type_info = {
    .name = TYPE_MOS6522_Q800_VIA1,
    .parent = TYPE_MOS6522,
    .instance_size = sizeof(MOS6522Q800VIA1State),
    .instance_init = mos6522_q800_via1_init,
    .class_init = mos6522_q800_via1_class_init,
};

static void mos6522_q800_via2_portB_write(MOS6522State *s)
{
    if (s->dirb & VIA2B_vPower && (s->b & VIA2B_vPower) == 0) {
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    }
}

static void mos6522_q800_via2_reset_hold(Object *obj, ResetType type)
{
    MOS6522State *ms = MOS6522(obj);
    MOS6522DeviceClass *mdc = MOS6522_GET_CLASS(ms);

    if (mdc->parent_phases.hold) {
        mdc->parent_phases.hold(obj, type);
    }

    ms->timers[0].frequency = VIA_TIMER_FREQ;
    ms->timers[1].frequency = VIA_TIMER_FREQ;

    ms->dirb = 0;
    ms->b = 0;
    ms->dira = 0;
    ms->a = 0x7f;
}

static void via2_nubus_irq_request(void *opaque, int n, int level)
{
    MOS6522Q800VIA2State *v2s = opaque;
    MOS6522State *s = MOS6522(v2s);
    qemu_irq irq = qdev_get_gpio_in(DEVICE(s), VIA2_IRQ_NUBUS_BIT);

    if (level) {
        s->a &= ~(1 << n);
    } else {
        s->a |= (1 << n);
    }

    qemu_set_irq(irq, !level);
}

static void mos6522_q800_via2_init(Object *obj)
{
    MOS6522Q800VIA2State *v2s = MOS6522_Q800_VIA2(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(v2s);

    memory_region_init_io(&v2s->via_mem, obj, &mos6522_q800_via2_ops, v2s,
                          "via2", VIA_SIZE);
    sysbus_init_mmio(sbd, &v2s->via_mem);

    qdev_init_gpio_in_named(DEVICE(obj), via2_nubus_irq_request, "nubus-irq",
                            VIA2_NUBUS_IRQ_NB);
}

static const VMStateDescription vmstate_q800_via2 = {
    .name = "q800-via2",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(parent_obj, MOS6522Q800VIA2State, 0, vmstate_mos6522,
                       MOS6522State),
        VMSTATE_END_OF_LIST()
    }
};

static void mos6522_q800_via2_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);
    MOS6522DeviceClass *mdc = MOS6522_CLASS(oc);

    resettable_class_set_parent_phases(rc, NULL, mos6522_q800_via2_reset_hold,
                                       NULL, &mdc->parent_phases);
    dc->vmsd = &vmstate_q800_via2;
    mdc->portB_write = mos6522_q800_via2_portB_write;
}

static const TypeInfo mos6522_q800_via2_type_info = {
    .name = TYPE_MOS6522_Q800_VIA2,
    .parent = TYPE_MOS6522,
    .instance_size = sizeof(MOS6522Q800VIA2State),
    .instance_init = mos6522_q800_via2_init,
    .class_init = mos6522_q800_via2_class_init,
};

static void mac_via_register_types(void)
{
    type_register_static(&mos6522_q800_via1_type_info);
    type_register_static(&mos6522_q800_via2_type_info);
}

type_init(mac_via_register_types);
