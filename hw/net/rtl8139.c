/**
 * QEMU RTL8139 emulation
 *
 * Copyright (c) 2006 Igor Kovalenko
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

 * Modifications:
 *  2006-Jan-28  Mark Malakanov :   TSAD and CSCR implementation (for Windows driver)
 *
 *  2006-Apr-28  Juergen Lock   :   EEPROM emulation changes for FreeBSD driver
 *                                  HW revision ID changes for FreeBSD driver
 *
 *  2006-Jul-01  Igor Kovalenko :   Implemented loopback mode for FreeBSD driver
 *                                  Corrected packet transfer reassembly routine for 8139C+ mode
 *                                  Rearranged debugging print statements
 *                                  Implemented PCI timer interrupt (disabled by default)
 *                                  Implemented Tally Counters, increased VM load/save version
 *                                  Implemented IP/TCP/UDP checksum task offloading
 *
 *  2006-Jul-04  Igor Kovalenko :   Implemented TCP segmentation offloading
 *                                  Fixed MTU=1500 for produced ethernet frames
 *
 *  2006-Jul-09  Igor Kovalenko :   Fixed TCP header length calculation while processing
 *                                  segmentation offloading
 *                                  Removed slirp.h dependency
 *                                  Added rx/tx buffer reset when enabling rx/tx operation
 *
 *  2010-Feb-04  Frediano Ziglio:   Rewrote timer support using QEMU timer only
 *                                  when strictly needed (required for
 *                                  Darwin)
 *  2011-Mar-22  Benjamin Poirier:  Implemented VLAN offloading
 */

#include "qemu/osdep.h"
#include <zlib.h>

#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "system/dma.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/bswap.h"
#include "net/net.h"
#include "net/eth.h"
#include "system/system.h"
#include "qom/object.h"

#define PCI_PERIOD 30

#define SET_MASKED(input, mask, curr) \
    ( ( (input) & ~(mask) ) | ( (curr) & (mask) ) )

#define MOD2(input, size) \
    ( ( input ) & ( size - 1 )  )

#define ETHER_TYPE_LEN 2

#define VLAN_TCI_LEN 2
#define VLAN_HLEN (ETHER_TYPE_LEN + VLAN_TCI_LEN)

#if defined (DEBUG_RTL8139)
#  define DPRINTF(fmt, ...) \
    do { fprintf(stderr, "RTL8139: " fmt, ## __VA_ARGS__); } while (0)
#else
static inline G_GNUC_PRINTF(1, 2) int DPRINTF(const char *fmt, ...)
{
    return 0;
}
#endif

#define TYPE_RTL8139 "rtl8139"

OBJECT_DECLARE_SIMPLE_TYPE(RTL8139State, RTL8139)

enum RTL8139_registers {
    MAC0 = 0,
    MAR0 = 8,
    TxStatus0 = 0x10,
    TxAddr0 = 0x20,
    RxBuf = 0x30,
    ChipCmd = 0x37,
    RxBufPtr = 0x38,
    RxBufAddr = 0x3A,
    IntrMask = 0x3C,
    IntrStatus = 0x3E,
    TxConfig = 0x40,
    RxConfig = 0x44,
    Timer = 0x48,
    RxMissed = 0x4C,
    Cfg9346 = 0x50,
    Config0 = 0x51,
    Config1 = 0x52,
    FlashReg = 0x54,
    MediaStatus = 0x58,
    Config3 = 0x59,
    Config4 = 0x5A,
    HltClk = 0x5B,
    MultiIntr = 0x5C,
    PCIRevisionID = 0x5E,
    TxSummary = 0x60,
    BasicModeCtrl = 0x62,
    BasicModeStatus = 0x64,
    NWayAdvert = 0x66,
    NWayLPAR = 0x68,
    NWayExpansion = 0x6A,
    FIFOTMS = 0x70,
    CSCR = 0x74,
    PARA78 = 0x78,
    PARA7c = 0x7c,
    Config5 = 0xD8,
    TxPoll        = 0xD9,
    RxMaxSize    = 0xDA,
    CpCmd        = 0xE0,
    IntrMitigate    = 0xE2,
    RxRingAddrLO    = 0xE4,
    RxRingAddrHI    = 0xE8,
    TxThresh    = 0xEC,
};

enum ClearBitMasks {
    MultiIntrClear = 0xF000,
    ChipCmdClear = 0xE2,
    Config1Clear = (1<<7)|(1<<6)|(1<<3)|(1<<2)|(1<<1),
};

enum ChipCmdBits {
    CmdReset = 0x10,
    CmdRxEnb = 0x08,
    CmdTxEnb = 0x04,
    RxBufEmpty = 0x01,
};

enum CplusCmdBits {
    CPlusRxVLAN   = 0x0040,
    CPlusRxChkSum = 0x0020,
    CPlusRxEnb    = 0x0002,
    CPlusTxEnb    = 0x0001,
};

enum IntrStatusBits {
    PCIErr = 0x8000,
    PCSTimeout = 0x4000,
    RxFIFOOver = 0x40,
    RxUnderrun = 0x20,
    RxOverflow = 0x10,
    TxErr = 0x08,
    TxOK = 0x04,
    RxErr = 0x02,
    RxOK = 0x01,

    RxAckBits = RxFIFOOver | RxOverflow | RxOK,
};

enum TxStatusBits {
    TxHostOwns = 0x2000,
    TxUnderrun = 0x4000,
    TxStatOK = 0x8000,
    TxOutOfWindow = 0x20000000,
    TxAborted = 0x40000000,
    TxCarrierLost = 0x80000000,
};
enum RxStatusBits {
    RxMulticast = 0x8000,
    RxPhysical = 0x4000,
    RxBroadcast = 0x2000,
    RxBadSymbol = 0x0020,
    RxRunt = 0x0010,
    RxTooLong = 0x0008,
    RxCRCErr = 0x0004,
    RxBadAlign = 0x0002,
    RxStatusOK = 0x0001,
};

enum rx_mode_bits {
    AcceptErr = 0x20,
    AcceptRunt = 0x10,
    AcceptBroadcast = 0x08,
    AcceptMulticast = 0x04,
    AcceptMyPhys = 0x02,
    AcceptAllPhys = 0x01,
};

enum tx_config_bits {

        TxIFGShift = 24,
        TxIFG84 = (0 << TxIFGShift),
        TxIFG88 = (1 << TxIFGShift),
        TxIFG92 = (2 << TxIFGShift),
        TxIFG96 = (3 << TxIFGShift),

    TxLoopBack = (1 << 18) | (1 << 17),
    TxCRC = (1 << 16),
    TxClearAbt = (1 << 0),
    TxDMAShift = 8,
    TxRetryShift = 4,

    TxVersionMask = 0x7C800000,
};


enum TSAD_bits {
 TSAD_TOK3 = 1<<15,
 TSAD_TOK2 = 1<<14,
 TSAD_TOK1 = 1<<13,
 TSAD_TOK0 = 1<<12,
 TSAD_TUN3 = 1<<11,
 TSAD_TUN2 = 1<<10,
 TSAD_TUN1 = 1<<9,
 TSAD_TUN0 = 1<<8,
 TSAD_TABT3 = 1<<07,
 TSAD_TABT2 = 1<<06,
 TSAD_TABT1 = 1<<05,
 TSAD_TABT0 = 1<<04,
 TSAD_OWN3 = 1<<03,
 TSAD_OWN2 = 1<<02,
 TSAD_OWN1 = 1<<01,
 TSAD_OWN0 = 1<<00,
};


enum Config1Bits {
    Cfg1_PM_Enable = 0x01,
    Cfg1_VPD_Enable = 0x02,
    Cfg1_PIO = 0x04,
    Cfg1_MMIO = 0x08,
    LWAKE = 0x10,
    Cfg1_Driver_Load = 0x20,
    Cfg1_LED0 = 0x40,
    Cfg1_LED1 = 0x80,
    SLEEP = (1 << 1),
    PWRDN = (1 << 0),
};

enum Config3Bits {
    Cfg3_FBtBEn    = (1 << 0),
    Cfg3_FuncRegEn = (1 << 1),
    Cfg3_CLKRUN_En = (1 << 2),
    Cfg3_CardB_En  = (1 << 3),
    Cfg3_LinkUp    = (1 << 4),
    Cfg3_Magic     = (1 << 5),
    Cfg3_PARM_En   = (1 << 6),
    Cfg3_GNTSel    = (1 << 7),
};

enum Config4Bits {
    LWPTN = (1 << 2),
};

enum Config5Bits {
    Cfg5_PME_STS     = (1 << 0),
    Cfg5_LANWake     = (1 << 1),
    Cfg5_LDPS        = (1 << 2),
    Cfg5_FIFOAddrPtr = (1 << 3),
    Cfg5_UWF         = (1 << 4),
    Cfg5_MWF         = (1 << 5),
    Cfg5_BWF         = (1 << 6),
};

enum RxConfigBits {
    RxCfgFIFOShift = 13,
    RxCfgFIFONone = (7 << RxCfgFIFOShift),

    RxCfgDMAShift = 8,
    RxCfgDMAUnlimited = (7 << RxCfgDMAShift),

    RxCfgRcv8K = 0,
    RxCfgRcv16K = (1 << 11),
    RxCfgRcv32K = (1 << 12),
    RxCfgRcv64K = (1 << 11) | (1 << 12),

    RxNoWrap = (1 << 7),
};

enum CSCRBits {
    CSCR_Testfun = 1<<15,
    CSCR_LD  = 1<<9,
    CSCR_HEART_BIT = 1<<8,
    CSCR_JBEN = 1<<7,
    CSCR_F_LINK_100 = 1<<6,
    CSCR_F_Connect  = 1<<5,
    CSCR_Con_status = 1<<3,
    CSCR_Con_status_En = 1<<2,
    CSCR_PASS_SCR = 1<<0,
};

enum Cfg9346Bits {
    Cfg9346_Normal = 0x00,
    Cfg9346_Autoload = 0x40,
    Cfg9346_Programming = 0x80,
    Cfg9346_ConfigWrite = 0xC0,
};

typedef enum {
    CH_8139 = 0,
    CH_8139_K,
    CH_8139A,
    CH_8139A_G,
    CH_8139B,
    CH_8130,
    CH_8139C,
    CH_8100,
    CH_8100B_8139D,
    CH_8101,
} chip_t;

enum chip_flags {
    HasHltClk = (1 << 0),
    HasLWake = (1 << 1),
};

#define HW_REVID(b30, b29, b28, b27, b26, b23, b22) \
    (b30<<30 | b29<<29 | b28<<28 | b27<<27 | b26<<26 | b23<<23 | b22<<22)
#define HW_REVID_MASK    HW_REVID(1, 1, 1, 1, 1, 1, 1)

#define RTL8139_PCI_REVID_8139      0x10
#define RTL8139_PCI_REVID_8139CPLUS 0x20

#define RTL8139_PCI_REVID           RTL8139_PCI_REVID_8139CPLUS

#define EEPROM_9346_ADDR_BITS 6
#define EEPROM_9346_SIZE  (1 << EEPROM_9346_ADDR_BITS)
#define EEPROM_9346_ADDR_MASK (EEPROM_9346_SIZE - 1)

enum Chip9346Operation
{
    Chip9346_op_mask = 0xc0,
    Chip9346_op_read = 0x80,
    Chip9346_op_write = 0x40,
    Chip9346_op_ext_mask = 0xf0,
    Chip9346_op_write_enable = 0x30,
    Chip9346_op_write_all = 0x10,
    Chip9346_op_write_disable = 0x00,
};

enum Chip9346Mode
{
    Chip9346_none = 0,
    Chip9346_enter_command_mode,
    Chip9346_read_command,
    Chip9346_data_read,
    Chip9346_data_write,
    Chip9346_data_write_all,
};

typedef struct EEprom9346
{
    uint16_t contents[EEPROM_9346_SIZE];
    int      mode;
    uint32_t tick;
    uint8_t  address;
    uint16_t input;
    uint16_t output;

    uint8_t eecs;
    uint8_t eesk;
    uint8_t eedi;
    uint8_t eedo;
} EEprom9346;

typedef struct RTL8139TallyCounters
{
    uint64_t   TxOk;
    uint64_t   RxOk;
    uint64_t   TxERR;
    uint32_t   RxERR;
    uint16_t   MissPkt;
    uint16_t   FAE;
    uint32_t   Tx1Col;
    uint32_t   TxMCol;
    uint64_t   RxOkPhy;
    uint64_t   RxOkBrd;
    uint32_t   RxOkMul;
    uint16_t   TxAbt;
    uint16_t   TxUndrn;
} RTL8139TallyCounters;

static void RTL8139TallyCounters_clear(RTL8139TallyCounters* counters);

struct RTL8139State {
    PCIDevice parent_obj;

    uint8_t phys[8];
    uint8_t mult[8];

    uint32_t TxStatus[4];
    uint32_t TxAddr[4];
    uint32_t RxBuf;
    uint32_t RxBufferSize;
    uint32_t RxBufPtr;
    uint32_t RxBufAddr;

    uint16_t IntrStatus;
    uint16_t IntrMask;

    uint32_t TxConfig;
    uint32_t RxConfig;
    uint32_t RxMissed;

    uint16_t CSCR;

    uint8_t  Cfg9346;
    uint8_t  Config0;
    uint8_t  Config1;
    uint8_t  Config3;
    uint8_t  Config4;
    uint8_t  Config5;

    uint8_t  clock_enabled;
    uint8_t  bChipCmdState;

    uint16_t MultiIntr;

    uint16_t BasicModeCtrl;
    uint16_t BasicModeStatus;
    uint16_t NWayAdvert;
    uint16_t NWayLPAR;
    uint16_t NWayExpansion;

    uint16_t CpCmd;
    uint8_t  TxThresh;

    NICState *nic;
    NICConf conf;

    uint32_t   currTxDesc;

    uint32_t   cplus_enabled;

    uint32_t   currCPlusRxDesc;
    uint32_t   currCPlusTxDesc;

    uint32_t   RxRingAddrLO;
    uint32_t   RxRingAddrHI;

    EEprom9346 eeprom;

    uint32_t   TCTR;
    uint32_t   TimerInt;
    int64_t    TCTR_base;

    RTL8139TallyCounters tally_counters;

    uint8_t   *cplus_txbuffer;
    int        cplus_txbuffer_len;
    int        cplus_txbuffer_offset;

    QEMUTimer *timer;

    MemoryRegion bar_io;
    MemoryRegion bar_mem;

    int rtl8139_mmio_io_addr_dummy;
};

static void RTL8139TallyCounters_dma_write(RTL8139State *s, dma_addr_t tc_addr);

static void rtl8139_set_next_tctr_time(RTL8139State *s);

static void prom9346_decode_command(EEprom9346 *eeprom, uint8_t command)
{
    DPRINTF("eeprom command 0x%02x\n", command);

    switch (command & Chip9346_op_mask)
    {
        case Chip9346_op_read:
        {
            eeprom->address = command & EEPROM_9346_ADDR_MASK;
            eeprom->output = eeprom->contents[eeprom->address];
            eeprom->eedo = 0;
            eeprom->tick = 0;
            eeprom->mode = Chip9346_data_read;
            DPRINTF("eeprom read from address 0x%02x data=0x%04x\n",
                eeprom->address, eeprom->output);
        }
        break;

        case Chip9346_op_write:
        {
            eeprom->address = command & EEPROM_9346_ADDR_MASK;
            eeprom->input = 0;
            eeprom->tick = 0;
            eeprom->mode = Chip9346_none;
            DPRINTF("eeprom begin write to address 0x%02x\n",
                eeprom->address);
        }
        break;
        default:
            eeprom->mode = Chip9346_none;
            switch (command & Chip9346_op_ext_mask)
            {
                case Chip9346_op_write_enable:
                    DPRINTF("eeprom write enabled\n");
                    break;
                case Chip9346_op_write_all:
                    DPRINTF("eeprom begin write all\n");
                    break;
                case Chip9346_op_write_disable:
                    DPRINTF("eeprom write disabled\n");
                    break;
            }
            break;
    }
}

static void prom9346_shift_clock(EEprom9346 *eeprom)
{
    int bit = eeprom->eedi?1:0;

    ++ eeprom->tick;

    DPRINTF("eeprom: tick %d eedi=%d eedo=%d\n", eeprom->tick, eeprom->eedi,
        eeprom->eedo);

    switch (eeprom->mode)
    {
        case Chip9346_enter_command_mode:
            if (bit)
            {
                eeprom->mode = Chip9346_read_command;
                eeprom->tick = 0;
                eeprom->input = 0;
                DPRINTF("eeprom: +++ synchronized, begin command read\n");
            }
            break;

        case Chip9346_read_command:
            eeprom->input = (eeprom->input << 1) | (bit & 1);
            if (eeprom->tick == 8)
            {
                prom9346_decode_command(eeprom, eeprom->input & 0xff);
            }
            break;

        case Chip9346_data_read:
            eeprom->eedo = (eeprom->output & 0x8000)?1:0;
            eeprom->output <<= 1;
            if (eeprom->tick == 16)
            {
#if 1
                eeprom->mode = Chip9346_enter_command_mode;
                eeprom->input = 0;
                eeprom->tick = 0;

                DPRINTF("eeprom: +++ end of read, awaiting next command\n");
#else
                ++eeprom->address;
                eeprom->address &= EEPROM_9346_ADDR_MASK;
                eeprom->output = eeprom->contents[eeprom->address];
                eeprom->tick = 0;

                DPRINTF("eeprom: +++ read next address 0x%02x data=0x%04x\n",
                    eeprom->address, eeprom->output);
#endif
            }
            break;

        case Chip9346_data_write:
            eeprom->input = (eeprom->input << 1) | (bit & 1);
            if (eeprom->tick == 16)
            {
                DPRINTF("eeprom write to address 0x%02x data=0x%04x\n",
                    eeprom->address, eeprom->input);

                eeprom->contents[eeprom->address] = eeprom->input;
                eeprom->mode = Chip9346_none;
                eeprom->tick = 0;
                eeprom->input = 0;
            }
            break;

        case Chip9346_data_write_all:
            eeprom->input = (eeprom->input << 1) | (bit & 1);
            if (eeprom->tick == 16)
            {
                int i;
                for (i = 0; i < EEPROM_9346_SIZE; i++)
                {
                    eeprom->contents[i] = eeprom->input;
                }
                DPRINTF("eeprom filled with data=0x%04x\n", eeprom->input);

                eeprom->mode = Chip9346_enter_command_mode;
                eeprom->tick = 0;
                eeprom->input = 0;
            }
            break;

        default:
            break;
    }
}

static int prom9346_get_wire(RTL8139State *s)
{
    EEprom9346 *eeprom = &s->eeprom;
    if (!eeprom->eecs)
        return 0;

    return eeprom->eedo;
}

static void prom9346_set_wire(RTL8139State *s, int eecs, int eesk, int eedi)
{
    EEprom9346 *eeprom = &s->eeprom;
    uint8_t old_eecs = eeprom->eecs;
    uint8_t old_eesk = eeprom->eesk;

    eeprom->eecs = eecs;
    eeprom->eesk = eesk;
    eeprom->eedi = eedi;

    DPRINTF("eeprom: +++ wires CS=%d SK=%d DI=%d DO=%d\n", eeprom->eecs,
        eeprom->eesk, eeprom->eedi, eeprom->eedo);

    if (!old_eecs && eecs)
    {
        eeprom->tick = 0;
        eeprom->input = 0;
        eeprom->output = 0;
        eeprom->mode = Chip9346_enter_command_mode;

        DPRINTF("=== eeprom: begin access, enter command mode\n");
    }

    if (!eecs)
    {
        DPRINTF("=== eeprom: end access\n");
        return;
    }

    if (!old_eesk && eesk)
    {
        prom9346_shift_clock(eeprom);
    }
}

static void rtl8139_update_irq(RTL8139State *s)
{
    PCIDevice *d = PCI_DEVICE(s);
    int isr;
    isr = (s->IntrStatus & s->IntrMask) & 0xffff;

    DPRINTF("Set IRQ to %d (%04x %04x)\n", isr ? 1 : 0, s->IntrStatus,
        s->IntrMask);

    pci_set_irq(d, (isr != 0));
}

static int rtl8139_RxWrap(RTL8139State *s)
{
    return (s->RxConfig & (1 << 7));
}

static int rtl8139_receiver_enabled(RTL8139State *s)
{
    return s->bChipCmdState & CmdRxEnb;
}

static int rtl8139_transmitter_enabled(RTL8139State *s)
{
    return s->bChipCmdState & CmdTxEnb;
}

static int rtl8139_cp_receiver_enabled(RTL8139State *s)
{
    return s->CpCmd & CPlusRxEnb;
}

static int rtl8139_cp_transmitter_enabled(RTL8139State *s)
{
    return s->CpCmd & CPlusTxEnb;
}

static void rtl8139_write_buffer(RTL8139State *s, const void *buf, int size)
{
    PCIDevice *d = PCI_DEVICE(s);

    if (s->RxBufAddr + size > s->RxBufferSize)
    {
        int wrapped = MOD2(s->RxBufAddr + size, s->RxBufferSize);

        if (wrapped && !(s->RxBufferSize < 65536 && rtl8139_RxWrap(s)))
        {
            DPRINTF(">>> rx packet wrapped in buffer at %d\n", size - wrapped);

            if (size > wrapped)
            {
                pci_dma_write(d, s->RxBuf + s->RxBufAddr,
                              buf, size-wrapped);
            }

            s->RxBufAddr = 0;

            pci_dma_write(d, s->RxBuf + s->RxBufAddr,
                          buf + (size-wrapped), wrapped);

            s->RxBufAddr = wrapped;

            return;
        }
    }

    pci_dma_write(d, s->RxBuf + s->RxBufAddr, buf, size);

    s->RxBufAddr += size;
}

#define MIN_BUF_SIZE 60
static inline dma_addr_t rtl8139_addr64(uint32_t low, uint32_t high)
{
    return low | ((uint64_t)high << 32);
}

static bool rtl8139_cp_rx_valid(RTL8139State *s)
{
    return !(s->RxRingAddrLO == 0 && s->RxRingAddrHI == 0);
}

static bool rtl8139_can_receive(NetClientState *nc)
{
    RTL8139State *s = qemu_get_nic_opaque(nc);
    int avail;

    if (!s->clock_enabled) {
        return true;
    }
    if (!rtl8139_receiver_enabled(s)) {
        return true;
    }

    if (rtl8139_cp_receiver_enabled(s) && rtl8139_cp_rx_valid(s)) {
        return true;
    }

    avail = MOD2(s->RxBufferSize + s->RxBufPtr - s->RxBufAddr,
                 s->RxBufferSize);
    return avail == 0 || avail >= 1514 || (s->IntrMask & RxOverflow);
}

static ssize_t rtl8139_do_receive(NetClientState *nc, const uint8_t *buf, size_t size_, int do_interrupt)
{
    RTL8139State *s = qemu_get_nic_opaque(nc);
    PCIDevice *d = PCI_DEVICE(s);
    size_t size = size_;
    const uint8_t *dot1q_buf = NULL;

    uint32_t packet_header = 0;

    static const uint8_t broadcast_macaddr[6] =
        { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

    DPRINTF(">>> received len=%zu\n", size);

    if (!s->clock_enabled)
    {
        DPRINTF("stopped ==========================\n");
        return -1;
    }

    if (!rtl8139_receiver_enabled(s))
    {
        DPRINTF("receiver disabled ================\n");
        return -1;
    }

    if (s->RxConfig & AcceptAllPhys) {
        DPRINTF(">>> packet received in promiscuous mode\n");

    } else {
        if (!memcmp(buf,  broadcast_macaddr, 6)) {
            if (!(s->RxConfig & AcceptBroadcast))
            {
                DPRINTF(">>> broadcast packet rejected\n");

                ++s->tally_counters.RxERR;

                return size;
            }

            packet_header |= RxBroadcast;

            DPRINTF(">>> broadcast packet received\n");

            ++s->tally_counters.RxOkBrd;

        } else if (buf[0] & 0x01) {
            if (!(s->RxConfig & AcceptMulticast))
            {
                DPRINTF(">>> multicast packet rejected\n");

                ++s->tally_counters.RxERR;

                return size;
            }

            int mcast_idx = net_crc32(buf, ETH_ALEN) >> 26;

            if (!(s->mult[mcast_idx >> 3] & (1 << (mcast_idx & 7))))
            {
                DPRINTF(">>> multicast address mismatch\n");

                ++s->tally_counters.RxERR;

                return size;
            }

            packet_header |= RxMulticast;

            DPRINTF(">>> multicast packet received\n");

            ++s->tally_counters.RxOkMul;

        } else if (s->phys[0] == buf[0] &&
                   s->phys[1] == buf[1] &&
                   s->phys[2] == buf[2] &&
                   s->phys[3] == buf[3] &&
                   s->phys[4] == buf[4] &&
                   s->phys[5] == buf[5]) {
            if (!(s->RxConfig & AcceptMyPhys))
            {
                DPRINTF(">>> rejecting physical address matching packet\n");

                ++s->tally_counters.RxERR;

                return size;
            }

            packet_header |= RxPhysical;

            DPRINTF(">>> physical address matching packet received\n");

            ++s->tally_counters.RxOkPhy;

        } else {

            DPRINTF(">>> unknown packet\n");

            ++s->tally_counters.RxERR;

            return size;
        }
    }

    if (rtl8139_cp_receiver_enabled(s))
    {
        if (!rtl8139_cp_rx_valid(s)) {
            return size;
        }

        DPRINTF("in C+ Rx mode ================\n");

#define CP_RX_OWN (1<<31)
#define CP_RX_EOR (1<<30)
#define CP_RX_BUFFER_SIZE_MASK ((1<<13) - 1)
#define CP_RX_TAVA (1<<16)
#define CP_RX_VLAN_TAG_MASK ((1<<16) - 1)

        int descriptor = s->currCPlusRxDesc;
        dma_addr_t cplus_rx_ring_desc;

        cplus_rx_ring_desc = rtl8139_addr64(s->RxRingAddrLO, s->RxRingAddrHI);
        cplus_rx_ring_desc += 16 * descriptor;

        DPRINTF("+++ C+ mode reading RX descriptor %d from host memory at "
            "%08x %08x = "DMA_ADDR_FMT"\n", descriptor, s->RxRingAddrHI,
            s->RxRingAddrLO, cplus_rx_ring_desc);

        uint32_t val, rxdw0,rxdw1,rxbufLO,rxbufHI;

        pci_dma_read(d, cplus_rx_ring_desc, &val, 4);
        rxdw0 = le32_to_cpu(val);
        pci_dma_read(d, cplus_rx_ring_desc+4, &val, 4);
        rxdw1 = le32_to_cpu(val);
        pci_dma_read(d, cplus_rx_ring_desc+8, &val, 4);
        rxbufLO = le32_to_cpu(val);
        pci_dma_read(d, cplus_rx_ring_desc+12, &val, 4);
        rxbufHI = le32_to_cpu(val);

        DPRINTF("+++ C+ mode RX descriptor %d %08x %08x %08x %08x\n",
            descriptor, rxdw0, rxdw1, rxbufLO, rxbufHI);

        if (!(rxdw0 & CP_RX_OWN))
        {
            DPRINTF("C+ Rx mode : descriptor %d is owned by host\n",
                descriptor);

            s->IntrStatus |= RxOverflow;
            ++s->RxMissed;

            ++s->tally_counters.RxERR;
            ++s->tally_counters.MissPkt;

            rtl8139_update_irq(s);
            return size_;
        }

        uint32_t rx_space = rxdw0 & CP_RX_BUFFER_SIZE_MASK;

        if (s->CpCmd & CPlusRxVLAN &&
            lduw_be_p(&buf[ETH_ALEN * 2]) == ETH_P_VLAN) {
            dot1q_buf = &buf[ETH_ALEN * 2];
            size -= VLAN_HLEN;
            if (size < MIN_BUF_SIZE) {
                size = MIN_BUF_SIZE;
            }

            rxdw1 &= ~CP_RX_VLAN_TAG_MASK;
            rxdw1 |= CP_RX_TAVA | lduw_le_p(&dot1q_buf[ETHER_TYPE_LEN]);

            DPRINTF("C+ Rx mode : extracted vlan tag with tci: ""%u\n",
                lduw_be_p(&dot1q_buf[ETHER_TYPE_LEN]));
        } else {
            rxdw1 &= ~CP_RX_TAVA;
        }

        if (size+4 > rx_space)
        {
            DPRINTF("C+ Rx mode : descriptor %d size %d received %zu + 4\n",
                descriptor, rx_space, size);

            s->IntrStatus |= RxOverflow;
            ++s->RxMissed;

            ++s->tally_counters.RxERR;
            ++s->tally_counters.MissPkt;

            rtl8139_update_irq(s);
            return size_;
        }

        dma_addr_t rx_addr = rtl8139_addr64(rxbufLO, rxbufHI);

        if (dot1q_buf) {
            pci_dma_write(d, rx_addr, buf, 2 * ETH_ALEN);
            pci_dma_write(d, rx_addr + 2 * ETH_ALEN,
                          buf + 2 * ETH_ALEN + VLAN_HLEN,
                          size - 2 * ETH_ALEN);
        } else {
            pci_dma_write(d, rx_addr, buf, size);
        }

        if (s->CpCmd & CPlusRxChkSum)
        {
        }

        val = cpu_to_le32(crc32(0, buf, size_));
        pci_dma_write(d, rx_addr+size, (uint8_t *)&val, 4);

#define CP_RX_STATUS_FS (1<<29)
#define CP_RX_STATUS_LS (1<<28)
#define CP_RX_STATUS_MAR (1<<26)
#define CP_RX_STATUS_PAM (1<<25)
#define CP_RX_STATUS_BAR (1<<24)
#define CP_RX_STATUS_RUNT (1<<19)
#define CP_RX_STATUS_CRC (1<<18)
#define CP_RX_STATUS_IPF (1<<15)
#define CP_RX_STATUS_UDPF (1<<14)
#define CP_RX_STATUS_TCPF (1<<13)

        rxdw0 &= ~CP_RX_OWN;

        rxdw0 |= CP_RX_STATUS_FS;

        rxdw0 |= CP_RX_STATUS_LS;

        if (packet_header & RxBroadcast)
            rxdw0 |= CP_RX_STATUS_BAR;
        if (packet_header & RxMulticast)
            rxdw0 |= CP_RX_STATUS_MAR;
        if (packet_header & RxPhysical)
            rxdw0 |= CP_RX_STATUS_PAM;

        rxdw0 &= ~CP_RX_BUFFER_SIZE_MASK;
        rxdw0 |= (size+4);

        val = cpu_to_le32(rxdw0);
        pci_dma_write(d, cplus_rx_ring_desc, (uint8_t *)&val, 4);
        val = cpu_to_le32(rxdw1);
        pci_dma_write(d, cplus_rx_ring_desc+4, (uint8_t *)&val, 4);

        ++s->tally_counters.RxOk;

        if (rxdw0 & CP_RX_EOR)
        {
            s->currCPlusRxDesc = 0;
        }
        else
        {
            ++s->currCPlusRxDesc;
        }

        DPRINTF("done C+ Rx mode ----------------\n");

    }
    else
    {
        DPRINTF("in ring Rx mode ================\n");

        int avail = MOD2(s->RxBufferSize + s->RxBufPtr - s->RxBufAddr, s->RxBufferSize);

#define RX_ALIGN(x) (((x) + 3) & ~0x3)

        if (avail != 0 && RX_ALIGN(size + 8) >= avail)
        {
            DPRINTF("rx overflow: rx buffer length %d head 0x%04x "
                "read 0x%04x === available 0x%04x need 0x%04zx\n",
                s->RxBufferSize, s->RxBufAddr, s->RxBufPtr, avail, size + 8);

            s->IntrStatus |= RxOverflow;
            ++s->RxMissed;
            rtl8139_update_irq(s);
            return 0;
        }

        packet_header |= RxStatusOK;

        packet_header |= (((size+4) << 16) & 0xffff0000);

        uint32_t val = cpu_to_le32(packet_header);

        rtl8139_write_buffer(s, (uint8_t *)&val, 4);

        rtl8139_write_buffer(s, buf, size);

        val = cpu_to_le32(crc32(0, buf, size));
        rtl8139_write_buffer(s, (uint8_t *)&val, 4);

        s->RxBufAddr = MOD2(RX_ALIGN(s->RxBufAddr), s->RxBufferSize);

        DPRINTF("received: rx buffer length %d head 0x%04x read 0x%04x\n",
            s->RxBufferSize, s->RxBufAddr, s->RxBufPtr);
    }

    s->IntrStatus |= RxOK;

    if (do_interrupt)
    {
        rtl8139_update_irq(s);
    }

    return size_;
}

static ssize_t rtl8139_receive(NetClientState *nc, const uint8_t *buf, size_t size)
{
    return rtl8139_do_receive(nc, buf, size, 1);
}

static void rtl8139_reset_rxring(RTL8139State *s, uint32_t bufferSize)
{
    s->RxBufferSize = bufferSize;
    s->RxBufPtr  = 0;
    s->RxBufAddr = 0;
}

static void rtl8139_reset_phy(RTL8139State *s)
{
    s->BasicModeStatus  = 0x7809;
    s->BasicModeStatus |= 0x0020;
    s->BasicModeStatus |= qemu_get_queue(s->nic)->link_down ? 0 : 0x04;

    s->NWayAdvert    = 0x05e1;
    s->NWayLPAR      = 0x05e1;
    s->NWayExpansion = 0x0001;

    s->CSCR = CSCR_F_LINK_100 | CSCR_HEART_BIT | CSCR_LD;
}

static void rtl8139_reset(DeviceState *d)
{
    RTL8139State *s = RTL8139(d);
    int i;

    memcpy(s->phys, s->conf.macaddr.a, 6);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->phys);

    s->IntrStatus = 0;
    s->IntrMask = 0;

    rtl8139_update_irq(s);

    for (i = 0; i < 4; ++i)
    {
        s->TxStatus[i] = TxHostOwns;
    }

    s->currTxDesc = 0;
    s->currCPlusRxDesc = 0;
    s->currCPlusTxDesc = 0;

    s->RxRingAddrLO = 0;
    s->RxRingAddrHI = 0;

    s->RxBuf = 0;

    rtl8139_reset_rxring(s, 8192);

    s->TxConfig = 0;

#if 0
    s->clock_enabled = 0;
#else
    s->TxConfig |= HW_REVID(1, 1, 1, 0, 1, 1, 0);
    s->clock_enabled = 1;
#endif

    s->bChipCmdState = CmdReset; ;

    s->Config0 = 0x0;
    s->Config1 = 0xC;
    s->Config3 = 0x1;
    s->Config5 = 0x0;

    s->CpCmd   = 0x0;
    s->cplus_enabled = 0;

    s->BasicModeCtrl = 0x1000;

    rtl8139_reset_phy(s);

    s->TCTR = 0;
    s->TimerInt = 0;
    s->TCTR_base = 0;
    rtl8139_set_next_tctr_time(s);

    RTL8139TallyCounters_clear(&s->tally_counters);
}

static void RTL8139TallyCounters_clear(RTL8139TallyCounters* counters)
{
    counters->TxOk = 0;
    counters->RxOk = 0;
    counters->TxERR = 0;
    counters->RxERR = 0;
    counters->MissPkt = 0;
    counters->FAE = 0;
    counters->Tx1Col = 0;
    counters->TxMCol = 0;
    counters->RxOkPhy = 0;
    counters->RxOkBrd = 0;
    counters->RxOkMul = 0;
    counters->TxAbt = 0;
    counters->TxUndrn = 0;
}

static void RTL8139TallyCounters_dma_write(RTL8139State *s, dma_addr_t tc_addr)
{
    PCIDevice *d = PCI_DEVICE(s);
    RTL8139TallyCounters *tally_counters = &s->tally_counters;
    uint16_t val16;
    uint32_t val32;
    uint64_t val64;

    val64 = cpu_to_le64(tally_counters->TxOk);
    pci_dma_write(d, tc_addr + 0,     (uint8_t *)&val64, 8);

    val64 = cpu_to_le64(tally_counters->RxOk);
    pci_dma_write(d, tc_addr + 8,     (uint8_t *)&val64, 8);

    val64 = cpu_to_le64(tally_counters->TxERR);
    pci_dma_write(d, tc_addr + 16,    (uint8_t *)&val64, 8);

    val32 = cpu_to_le32(tally_counters->RxERR);
    pci_dma_write(d, tc_addr + 24,    (uint8_t *)&val32, 4);

    val16 = cpu_to_le16(tally_counters->MissPkt);
    pci_dma_write(d, tc_addr + 28,    (uint8_t *)&val16, 2);

    val16 = cpu_to_le16(tally_counters->FAE);
    pci_dma_write(d, tc_addr + 30,    (uint8_t *)&val16, 2);

    val32 = cpu_to_le32(tally_counters->Tx1Col);
    pci_dma_write(d, tc_addr + 32,    (uint8_t *)&val32, 4);

    val32 = cpu_to_le32(tally_counters->TxMCol);
    pci_dma_write(d, tc_addr + 36,    (uint8_t *)&val32, 4);

    val64 = cpu_to_le64(tally_counters->RxOkPhy);
    pci_dma_write(d, tc_addr + 40,    (uint8_t *)&val64, 8);

    val64 = cpu_to_le64(tally_counters->RxOkBrd);
    pci_dma_write(d, tc_addr + 48,    (uint8_t *)&val64, 8);

    val32 = cpu_to_le32(tally_counters->RxOkMul);
    pci_dma_write(d, tc_addr + 56,    (uint8_t *)&val32, 4);

    val16 = cpu_to_le16(tally_counters->TxAbt);
    pci_dma_write(d, tc_addr + 60,    (uint8_t *)&val16, 2);

    val16 = cpu_to_le16(tally_counters->TxUndrn);
    pci_dma_write(d, tc_addr + 62,    (uint8_t *)&val16, 2);
}

static void rtl8139_ChipCmd_write(RTL8139State *s, uint32_t val)
{
    DeviceState *d = DEVICE(s);

    val &= 0xff;

    DPRINTF("ChipCmd write val=0x%08x\n", val);

    if (val & CmdReset)
    {
        DPRINTF("ChipCmd reset\n");
        rtl8139_reset(d);
    }
    if (val & CmdRxEnb)
    {
        DPRINTF("ChipCmd enable receiver\n");

        s->currCPlusRxDesc = 0;
    }
    if (val & CmdTxEnb)
    {
        DPRINTF("ChipCmd enable transmitter\n");

        s->currCPlusTxDesc = 0;
    }

    val = SET_MASKED(val, 0xe3, s->bChipCmdState);

    val &= ~CmdReset;

    s->bChipCmdState = val;
}

static int rtl8139_RxBufferEmpty(RTL8139State *s)
{
    int unread = MOD2(s->RxBufferSize + s->RxBufAddr - s->RxBufPtr, s->RxBufferSize);

    if (unread != 0)
    {
        DPRINTF("receiver buffer data available 0x%04x\n", unread);
        return 0;
    }

    DPRINTF("receiver buffer is empty\n");

    return 1;
}

static uint32_t rtl8139_ChipCmd_read(RTL8139State *s)
{
    uint32_t ret = s->bChipCmdState;

    if (rtl8139_RxBufferEmpty(s))
        ret |= RxBufEmpty;

    DPRINTF("ChipCmd read val=0x%04x\n", ret);

    return ret;
}

static void rtl8139_CpCmd_write(RTL8139State *s, uint32_t val)
{
    val &= 0xffff;

    DPRINTF("C+ command register write(w) val=0x%04x\n", val);

    s->cplus_enabled = 1;

    val = SET_MASKED(val, 0xff84, s->CpCmd);

    s->CpCmd = val;
}

static uint32_t rtl8139_CpCmd_read(RTL8139State *s)
{
    uint32_t ret = s->CpCmd;

    DPRINTF("C+ command register read(w) val=0x%04x\n", ret);

    return ret;
}

static void rtl8139_IntrMitigate_write(RTL8139State *s, uint32_t val)
{
    DPRINTF("C+ IntrMitigate register write(w) val=0x%04x\n", val);
}

static uint32_t rtl8139_IntrMitigate_read(RTL8139State *s)
{
    uint32_t ret = 0;

    DPRINTF("C+ IntrMitigate register read(w) val=0x%04x\n", ret);

    return ret;
}

static int rtl8139_config_writable(RTL8139State *s)
{
    if ((s->Cfg9346 & Chip9346_op_mask) == Cfg9346_ConfigWrite)
    {
        return 1;
    }

    DPRINTF("Configuration registers are write-protected\n");

    return 0;
}

static void rtl8139_BasicModeCtrl_write(RTL8139State *s, uint32_t val)
{
    val &= 0xffff;

    DPRINTF("BasicModeCtrl register write(w) val=0x%04x\n", val);

    uint32_t mask = 0xccff;

    if (1 || !rtl8139_config_writable(s))
    {
        mask |= 0x3000;
        mask |= 0x0100;
    }

    if (val & 0x8000) {
        rtl8139_reset_phy(s);
    }

    val = SET_MASKED(val, mask, s->BasicModeCtrl);

    s->BasicModeCtrl = val;
}

static uint32_t rtl8139_BasicModeCtrl_read(RTL8139State *s)
{
    uint32_t ret = s->BasicModeCtrl;

    DPRINTF("BasicModeCtrl register read(w) val=0x%04x\n", ret);

    return ret;
}

static void rtl8139_BasicModeStatus_write(RTL8139State *s, uint32_t val)
{
    val &= 0xffff;

    DPRINTF("BasicModeStatus register write(w) val=0x%04x\n", val);

    val = SET_MASKED(val, 0xff3f, s->BasicModeStatus);

    s->BasicModeStatus = val;
}

static uint32_t rtl8139_BasicModeStatus_read(RTL8139State *s)
{
    uint32_t ret = s->BasicModeStatus;

    DPRINTF("BasicModeStatus register read(w) val=0x%04x\n", ret);

    return ret;
}

static void rtl8139_Cfg9346_write(RTL8139State *s, uint32_t val)
{
    DeviceState *d = DEVICE(s);

    val &= 0xff;

    DPRINTF("Cfg9346 write val=0x%02x\n", val);

    val = SET_MASKED(val, 0x31, s->Cfg9346);

    uint32_t opmode = val & 0xc0;
    uint32_t eeprom_val = val & 0xf;

    if (opmode == 0x80) {
        int eecs = (eeprom_val & 0x08)?1:0;
        int eesk = (eeprom_val & 0x04)?1:0;
        int eedi = (eeprom_val & 0x02)?1:0;
        prom9346_set_wire(s, eecs, eesk, eedi);
    } else if (opmode == 0x40) {
        val = 0;
        rtl8139_reset(d);
    }

    s->Cfg9346 = val;
}

static uint32_t rtl8139_Cfg9346_read(RTL8139State *s)
{
    uint32_t ret = s->Cfg9346;

    uint32_t opmode = ret & 0xc0;

    if (opmode == 0x80)
    {
        int eedo = prom9346_get_wire(s);
        if (eedo)
        {
            ret |=  0x01;
        }
        else
        {
            ret &= ~0x01;
        }
    }

    DPRINTF("Cfg9346 read val=0x%02x\n", ret);

    return ret;
}

static void rtl8139_Config0_write(RTL8139State *s, uint32_t val)
{
    val &= 0xff;

    DPRINTF("Config0 write val=0x%02x\n", val);

    if (!rtl8139_config_writable(s)) {
        return;
    }

    val = SET_MASKED(val, 0xf8, s->Config0);

    s->Config0 = val;
}

static uint32_t rtl8139_Config0_read(RTL8139State *s)
{
    uint32_t ret = s->Config0;

    DPRINTF("Config0 read val=0x%02x\n", ret);

    return ret;
}

static void rtl8139_Config1_write(RTL8139State *s, uint32_t val)
{
    val &= 0xff;

    DPRINTF("Config1 write val=0x%02x\n", val);

    if (!rtl8139_config_writable(s)) {
        return;
    }

    val = SET_MASKED(val, 0xC, s->Config1);

    s->Config1 = val;
}

static uint32_t rtl8139_Config1_read(RTL8139State *s)
{
    uint32_t ret = s->Config1;

    DPRINTF("Config1 read val=0x%02x\n", ret);

    return ret;
}

static void rtl8139_Config3_write(RTL8139State *s, uint32_t val)
{
    val &= 0xff;

    DPRINTF("Config3 write val=0x%02x\n", val);

    if (!rtl8139_config_writable(s)) {
        return;
    }

    val = SET_MASKED(val, 0x8F, s->Config3);

    s->Config3 = val;
}

static uint32_t rtl8139_Config3_read(RTL8139State *s)
{
    uint32_t ret = s->Config3;

    DPRINTF("Config3 read val=0x%02x\n", ret);

    return ret;
}

static void rtl8139_Config4_write(RTL8139State *s, uint32_t val)
{
    val &= 0xff;

    DPRINTF("Config4 write val=0x%02x\n", val);

    if (!rtl8139_config_writable(s)) {
        return;
    }

    val = SET_MASKED(val, 0x0a, s->Config4);

    s->Config4 = val;
}

static uint32_t rtl8139_Config4_read(RTL8139State *s)
{
    uint32_t ret = s->Config4;

    DPRINTF("Config4 read val=0x%02x\n", ret);

    return ret;
}

static void rtl8139_Config5_write(RTL8139State *s, uint32_t val)
{
    val &= 0xff;

    DPRINTF("Config5 write val=0x%02x\n", val);

    val = SET_MASKED(val, 0x80, s->Config5);

    s->Config5 = val;
}

static uint32_t rtl8139_Config5_read(RTL8139State *s)
{
    uint32_t ret = s->Config5;

    DPRINTF("Config5 read val=0x%02x\n", ret);

    return ret;
}

static void rtl8139_TxConfig_write(RTL8139State *s, uint32_t val)
{
    if (!rtl8139_transmitter_enabled(s))
    {
        DPRINTF("transmitter disabled; no TxConfig write val=0x%08x\n", val);
        return;
    }

    DPRINTF("TxConfig write val=0x%08x\n", val);

    val = SET_MASKED(val, TxVersionMask | 0x8070f80f, s->TxConfig);

    s->TxConfig = val;
}

static void rtl8139_TxConfig_writeb(RTL8139State *s, uint32_t val)
{
    DPRINTF("RTL8139C TxConfig via write(b) val=0x%02x\n", val);

    uint32_t tc = s->TxConfig;
    tc &= 0xFFFFFF00;
    tc |= (val & 0x000000FF);
    rtl8139_TxConfig_write(s, tc);
}

static uint32_t rtl8139_TxConfig_read(RTL8139State *s)
{
    uint32_t ret = s->TxConfig;

    DPRINTF("TxConfig read val=0x%04x\n", ret);

    return ret;
}

static void rtl8139_RxConfig_write(RTL8139State *s, uint32_t val)
{
    DPRINTF("RxConfig write val=0x%08x\n", val);

    val = SET_MASKED(val, 0xf0fc0040, s->RxConfig);

    s->RxConfig = val;

    rtl8139_reset_rxring(s, 8192 << ((s->RxConfig >> 11) & 0x3));

    DPRINTF("RxConfig write reset buffer size to %d\n", s->RxBufferSize);
}

static uint32_t rtl8139_RxConfig_read(RTL8139State *s)
{
    uint32_t ret = s->RxConfig;

    DPRINTF("RxConfig read val=0x%08x\n", ret);

    return ret;
}

static void rtl8139_transfer_frame(RTL8139State *s, uint8_t *buf, int size,
    int do_interrupt, const uint8_t *dot1q_buf)
{
    struct iovec *iov = NULL;
    struct iovec vlan_iov[3];

    if (!size)
    {
        DPRINTF("+++ empty ethernet frame\n");
        return;
    }

    if (dot1q_buf && size >= ETH_ALEN * 2) {
        iov = (struct iovec[3]) {
            { .iov_base = buf, .iov_len = ETH_ALEN * 2 },
            { .iov_base = (void *) dot1q_buf, .iov_len = VLAN_HLEN },
            { .iov_base = buf + ETH_ALEN * 2,
                .iov_len = size - ETH_ALEN * 2 },
        };

        memcpy(vlan_iov, iov, sizeof(vlan_iov));
        iov = vlan_iov;
    }

    if (TxLoopBack == (s->TxConfig & TxLoopBack))
    {
        size_t buf2_size;
        uint8_t *buf2;

        if (iov) {
            buf2_size = iov_size(iov, 3);
            buf2 = g_malloc(buf2_size);
            iov_to_buf(iov, 3, 0, buf2, buf2_size);
            buf = buf2;
        }

        DPRINTF("+++ transmit loopback mode\n");
        qemu_receive_packet(qemu_get_queue(s->nic), buf, size);

        if (iov) {
            g_free(buf2);
        }
    }
    else
    {
        if (iov) {
            qemu_sendv_packet(qemu_get_queue(s->nic), iov, 3);
        } else {
            qemu_send_packet(qemu_get_queue(s->nic), buf, size);
        }
    }
}

static int rtl8139_transmit_one(RTL8139State *s, int descriptor)
{
    if (!rtl8139_transmitter_enabled(s))
    {
        DPRINTF("+++ cannot transmit from descriptor %d: transmitter "
            "disabled\n", descriptor);
        return 0;
    }

    if (s->TxStatus[descriptor] & TxHostOwns)
    {
        DPRINTF("+++ cannot transmit from descriptor %d: owned by host "
            "(%08x)\n", descriptor, s->TxStatus[descriptor]);
        return 0;
    }

    DPRINTF("+++ transmitting from descriptor %d\n", descriptor);

    PCIDevice *d = PCI_DEVICE(s);
    int txsize = s->TxStatus[descriptor] & 0x1fff;
    QEMU_UNINITIALIZED uint8_t txbuffer[0x2000];

    DPRINTF("+++ transmit reading %d bytes from host memory at 0x%08x\n",
        txsize, s->TxAddr[descriptor]);

    pci_dma_read(d, s->TxAddr[descriptor], txbuffer, txsize);

    s->TxStatus[descriptor] |= TxHostOwns;
    s->TxStatus[descriptor] |= TxStatOK;

    rtl8139_transfer_frame(s, txbuffer, txsize, 0, NULL);

    DPRINTF("+++ transmitted %d bytes from descriptor %d\n", txsize,
        descriptor);

    s->IntrStatus |= TxOK;
    rtl8139_update_irq(s);

    return 1;
}

#define TCP_HEADER_CLEAR_FLAGS(tcp, off) ((tcp)->th_offset_flags &= cpu_to_be16(~TCP_FLAGS_ONLY(off)))

static uint16_t ones_complement_sum(uint8_t *data, size_t len)
{
    uint32_t result = 0;

    for (; len > 1; data+=2, len-=2)
    {
        result += *(uint16_t*)data;
    }

    if (len)
    {
        uint8_t odd[2] = {*data, 0};
        result += *(uint16_t*)odd;
    }

    while (result>>16)
        result = (result & 0xffff) + (result >> 16);

    return result;
}

static uint16_t ip_checksum(void *data, size_t len)
{
    return ~ones_complement_sum((uint8_t*)data, len);
}

static int rtl8139_cplus_transmit_one(RTL8139State *s)
{
    if (!rtl8139_transmitter_enabled(s))
    {
        DPRINTF("+++ C+ mode: transmitter disabled\n");
        return 0;
    }

    if (!rtl8139_cp_transmitter_enabled(s))
    {
        DPRINTF("+++ C+ mode: C+ transmitter disabled\n");
        return 0 ;
    }

    PCIDevice *d = PCI_DEVICE(s);
    int descriptor = s->currCPlusTxDesc;

    dma_addr_t cplus_tx_ring_desc = rtl8139_addr64(s->TxAddr[0], s->TxAddr[1]);

    cplus_tx_ring_desc += 16 * descriptor;

    DPRINTF("+++ C+ mode reading TX descriptor %d from host memory at "
        "%08x %08x = 0x"DMA_ADDR_FMT"\n", descriptor, s->TxAddr[1],
        s->TxAddr[0], cplus_tx_ring_desc);

    uint32_t val, txdw0,txdw1,txbufLO,txbufHI;

    pci_dma_read(d, cplus_tx_ring_desc,    (uint8_t *)&val, 4);
    txdw0 = le32_to_cpu(val);
    pci_dma_read(d, cplus_tx_ring_desc+4,  (uint8_t *)&val, 4);
    txdw1 = le32_to_cpu(val);
    pci_dma_read(d, cplus_tx_ring_desc+8,  (uint8_t *)&val, 4);
    txbufLO = le32_to_cpu(val);
    pci_dma_read(d, cplus_tx_ring_desc+12, (uint8_t *)&val, 4);
    txbufHI = le32_to_cpu(val);

    DPRINTF("+++ C+ mode TX descriptor %d %08x %08x %08x %08x\n", descriptor,
        txdw0, txdw1, txbufLO, txbufHI);

#define CP_TX_OWN (1<<31)
#define CP_TX_EOR (1<<30)
#define CP_TX_FS (1<<29)
#define CP_TX_LS (1<<28)
#define CP_TX_LGSEN (1<<27)
#define CP_TC_LGSEN_MSS_SHIFT 16
#define CP_TC_LGSEN_MSS_MASK ((1 << 11) - 1)

#define CP_TX_IPCS (1<<18)
#define CP_TX_UDPCS (1<<17)
#define CP_TX_TCPCS (1<<16)

#define CP_TX_BUFFER_SIZE (1<<16)
#define CP_TX_BUFFER_SIZE_MASK (CP_TX_BUFFER_SIZE - 1)
#define CP_TX_TAGC (1<<17)
#define CP_TX_VLAN_TAG_MASK ((1<<16) - 1)

#define CP_TX_STATUS_UNF (1<<25)
#define CP_TX_STATUS_TES (1<<23)
#define CP_TX_STATUS_OWC (1<<22)
#define CP_TX_STATUS_LNKF (1<<21)
#define CP_TX_STATUS_EXC (1<<20)

    if (!(txdw0 & CP_TX_OWN))
    {
        DPRINTF("C+ Tx mode : descriptor %d is owned by host\n", descriptor);
        return 0 ;
    }

    DPRINTF("+++ C+ Tx mode : transmitting from descriptor %d\n", descriptor);

    if (txdw0 & CP_TX_FS)
    {
        DPRINTF("+++ C+ Tx mode : descriptor %d is first segment "
            "descriptor\n", descriptor);

        s->cplus_txbuffer_offset = 0;
    }

    int txsize = txdw0 & CP_TX_BUFFER_SIZE_MASK;
    dma_addr_t tx_addr = rtl8139_addr64(txbufLO, txbufHI);

    if (!s->cplus_txbuffer)
    {
        s->cplus_txbuffer_len = CP_TX_BUFFER_SIZE;
        s->cplus_txbuffer = g_malloc(s->cplus_txbuffer_len);
        s->cplus_txbuffer_offset = 0;

        DPRINTF("+++ C+ mode transmission buffer allocated space %d\n",
            s->cplus_txbuffer_len);
    }

    if (s->cplus_txbuffer_offset + txsize >= s->cplus_txbuffer_len)
    {
        txsize = s->cplus_txbuffer_len - s->cplus_txbuffer_offset;
        DPRINTF("+++ C+ mode transmission buffer overrun, truncated descriptor"
                "length to %d\n", txsize);
    }

    DPRINTF("+++ C+ mode transmit reading %d bytes from host memory at "
            DMA_ADDR_FMT" to offset %d\n", txsize, tx_addr,
            s->cplus_txbuffer_offset);

    pci_dma_read(d, tx_addr,
                 s->cplus_txbuffer + s->cplus_txbuffer_offset, txsize);
    s->cplus_txbuffer_offset += txsize;

    if (txdw0 & CP_TX_EOR)
    {
        s->currCPlusTxDesc = 0;
    }
    else
    {
        ++s->currCPlusTxDesc;
        if (s->currCPlusTxDesc >= 64)
            s->currCPlusTxDesc = 0;
    }

    uint32_t tx_status = txdw0;

    tx_status &= ~CP_TX_OWN;

    tx_status &= ~CP_TX_STATUS_UNF;
    tx_status &= ~CP_TX_STATUS_TES;
    tx_status &= ~CP_TX_STATUS_OWC;
    tx_status &= ~CP_TX_STATUS_LNKF;
    tx_status &= ~CP_TX_STATUS_EXC;

    val = cpu_to_le32(tx_status);
    pci_dma_write(d, cplus_tx_ring_desc, (uint8_t *)&val, 4);

    if (txdw0 & CP_TX_LS)
    {
        uint8_t dot1q_buffer_space[VLAN_HLEN];
        uint16_t *dot1q_buffer;

        DPRINTF("+++ C+ Tx mode : descriptor %d is last segment descriptor\n",
            descriptor);

        uint8_t *saved_buffer  = s->cplus_txbuffer;
        int      saved_size    = s->cplus_txbuffer_offset;
        int      saved_buffer_len = s->cplus_txbuffer_len;

        if (txdw1 & CP_TX_TAGC) {
            DPRINTF("+++ C+ Tx mode : inserting vlan tag with ""tci: %u\n",
                bswap16(txdw1 & CP_TX_VLAN_TAG_MASK));

            dot1q_buffer = (uint16_t *) dot1q_buffer_space;
            dot1q_buffer[0] = cpu_to_be16(ETH_P_VLAN);
            dot1q_buffer[1] = cpu_to_le16(txdw1 & CP_TX_VLAN_TAG_MASK);
        } else {
            dot1q_buffer = NULL;
        }

        s->cplus_txbuffer = NULL;
        s->cplus_txbuffer_offset = 0;
        s->cplus_txbuffer_len = 0;

        if (txdw0 & (CP_TX_IPCS | CP_TX_UDPCS | CP_TX_TCPCS | CP_TX_LGSEN))
        {
            DPRINTF("+++ C+ mode offloaded task checksum\n");

            if (saved_size < ETH_HLEN + sizeof(struct ip_header)) {
                goto skip_offload;
            }

            struct ip_header *ip = NULL;
            int hlen = 0;
            uint8_t  ip_protocol = 0;
            uint16_t ip_data_len = 0;

            uint8_t *eth_payload_data = NULL;
            size_t   eth_payload_len  = 0;

            int proto = be16_to_cpu(*(uint16_t *)(saved_buffer + 12));
            if (proto != ETH_P_IP)
            {
                goto skip_offload;
            }

            DPRINTF("+++ C+ mode has IP packet\n");

            eth_payload_data = saved_buffer + ETH_HLEN;
            eth_payload_len  = saved_size   - ETH_HLEN;

            ip = (struct ip_header*)eth_payload_data;

            if (IP_HEADER_VERSION(ip) != IP_HEADER_VERSION_4) {
                DPRINTF("+++ C+ mode packet has bad IP version %d "
                    "expected %d\n", IP_HEADER_VERSION(ip),
                    IP_HEADER_VERSION_4);
                goto skip_offload;
            }

            hlen = IP_HDR_GET_LEN(ip);
            if (hlen < sizeof(struct ip_header) || hlen > eth_payload_len) {
                goto skip_offload;
            }

            ip_protocol = ip->ip_p;

            ip_data_len = be16_to_cpu(ip->ip_len);
            if (ip_data_len < hlen || ip_data_len > eth_payload_len) {
                goto skip_offload;
            }
            ip_data_len -= hlen;

            if (!(txdw0 & CP_TX_LGSEN) && (txdw0 & CP_TX_IPCS))
            {
                DPRINTF("+++ C+ mode need IP checksum\n");

                ip->ip_sum = 0;
                ip->ip_sum = ip_checksum(ip, hlen);
                DPRINTF("+++ C+ mode IP header len=%d checksum=%04x\n",
                    hlen, ip->ip_sum);
            }

            if ((txdw0 & CP_TX_LGSEN) && ip_protocol == IP_PROTO_TCP)
            {
                if (ip_data_len < sizeof(tcp_header)) {
                    goto skip_offload;
                }

                int large_send_mss = (txdw0 >> CP_TC_LGSEN_MSS_SHIFT) &
                                     CP_TC_LGSEN_MSS_MASK;
                if (large_send_mss == 0) {
                    goto skip_offload;
                }

                DPRINTF("+++ C+ mode offloaded task TSO IP data %d "
                    "frame data %d specified MSS=%d\n",
                    ip_data_len, saved_size - ETH_HLEN, large_send_mss);

                int tcp_send_offset = 0;

                uint8_t saved_ip_header[60];

                memcpy(saved_ip_header, eth_payload_data, hlen);

                uint8_t *data_to_checksum     = eth_payload_data + hlen - 12;

                tcp_header *p_tcp_hdr = (tcp_header*)(eth_payload_data + hlen);

                int tcp_hlen = TCP_HEADER_DATA_OFFSET(p_tcp_hdr);

                if (tcp_hlen < sizeof(tcp_header) || tcp_hlen > ip_data_len) {
                    goto skip_offload;
                }

                int tcp_data_len = ip_data_len - tcp_hlen;

                DPRINTF("+++ C+ mode TSO IP data len %d TCP hlen %d TCP "
                    "data len %d\n", ip_data_len, tcp_hlen, tcp_data_len);

                int is_last_frame = 0;

                for (tcp_send_offset = 0; tcp_send_offset < tcp_data_len; tcp_send_offset += large_send_mss)
                {
                    uint16_t chunk_size = large_send_mss;

                    if (tcp_send_offset + large_send_mss >= tcp_data_len)
                    {
                        is_last_frame = 1;
                        chunk_size = tcp_data_len - tcp_send_offset;
                    }

                    DPRINTF("+++ C+ mode TSO TCP seqno %08x\n",
                            ldl_be_p(&p_tcp_hdr->th_seq));

                    memcpy(data_to_checksum, saved_ip_header + 12, 8);

                    DPRINTF("+++ C+ mode TSO calculating TCP checksum for "
                        "packet with %d bytes data\n", tcp_hlen +
                        chunk_size);

                    if (tcp_send_offset)
                    {
                        memcpy((uint8_t*)p_tcp_hdr + tcp_hlen, (uint8_t*)p_tcp_hdr + tcp_hlen + tcp_send_offset, chunk_size);
                    }

                    if (!is_last_frame)
                    {
                        TCP_HEADER_CLEAR_FLAGS(p_tcp_hdr, TH_PUSH | TH_FIN);
                    }

                    ip_pseudo_header *p_tcpip_hdr = (ip_pseudo_header *)data_to_checksum;
                    p_tcpip_hdr->zeros      = 0;
                    p_tcpip_hdr->ip_proto   = IP_PROTO_TCP;
                    p_tcpip_hdr->ip_payload = cpu_to_be16(tcp_hlen + chunk_size);

                    p_tcp_hdr->th_sum = 0;

                    int tcp_checksum = ip_checksum(data_to_checksum, tcp_hlen + chunk_size + 12);
                    DPRINTF("+++ C+ mode TSO TCP checksum %04x\n",
                        tcp_checksum);

                    p_tcp_hdr->th_sum = tcp_checksum;

                    memcpy(eth_payload_data, saved_ip_header, hlen);

                    ip->ip_len = cpu_to_be16(hlen + tcp_hlen + chunk_size);

                    ip->ip_id = cpu_to_be16(tcp_send_offset/large_send_mss + be16_to_cpu(ip->ip_id));

                    ip->ip_sum = 0;
                    ip->ip_sum = ip_checksum(eth_payload_data, hlen);
                    DPRINTF("+++ C+ mode TSO IP header len=%d "
                        "checksum=%04x\n", hlen, ip->ip_sum);

                    int tso_send_size = ETH_HLEN + hlen + tcp_hlen + chunk_size;
                    DPRINTF("+++ C+ mode TSO transferring packet size "
                        "%d\n", tso_send_size);
                    rtl8139_transfer_frame(s, saved_buffer, tso_send_size,
                        0, (uint8_t *) dot1q_buffer);

                    stl_be_p(&p_tcp_hdr->th_seq,
                             chunk_size + ldl_be_p(&p_tcp_hdr->th_seq));
                }

                saved_size = 0;
            }
            else if (!(txdw0 & CP_TX_LGSEN) && (txdw0 & (CP_TX_TCPCS|CP_TX_UDPCS)))
            {
                DPRINTF("+++ C+ mode need TCP or UDP checksum\n");

                uint8_t saved_ip_header[60];
                memcpy(saved_ip_header, eth_payload_data, hlen);

                uint8_t *data_to_checksum     = eth_payload_data + hlen - 12;

                memcpy(data_to_checksum, saved_ip_header + 12, 8);

                if ((txdw0 & CP_TX_TCPCS) && ip_protocol == IP_PROTO_TCP)
                {
                    DPRINTF("+++ C+ mode calculating TCP checksum for "
                        "packet with %d bytes data\n", ip_data_len);

                    ip_pseudo_header *p_tcpip_hdr = (ip_pseudo_header *)data_to_checksum;
                    p_tcpip_hdr->zeros      = 0;
                    p_tcpip_hdr->ip_proto   = IP_PROTO_TCP;
                    p_tcpip_hdr->ip_payload = cpu_to_be16(ip_data_len);

                    tcp_header* p_tcp_hdr = (tcp_header *) (data_to_checksum+12);

                    p_tcp_hdr->th_sum = 0;

                    int tcp_checksum = ip_checksum(data_to_checksum, ip_data_len + 12);
                    DPRINTF("+++ C+ mode TCP checksum %04x\n",
                        tcp_checksum);

                    p_tcp_hdr->th_sum = tcp_checksum;
                }
                else if ((txdw0 & CP_TX_UDPCS) && ip_protocol == IP_PROTO_UDP)
                {
                    DPRINTF("+++ C+ mode calculating UDP checksum for "
                        "packet with %d bytes data\n", ip_data_len);

                    ip_pseudo_header *p_udpip_hdr = (ip_pseudo_header *)data_to_checksum;
                    p_udpip_hdr->zeros      = 0;
                    p_udpip_hdr->ip_proto   = IP_PROTO_UDP;
                    p_udpip_hdr->ip_payload = cpu_to_be16(ip_data_len);

                    udp_header *p_udp_hdr = (udp_header *) (data_to_checksum+12);

                    p_udp_hdr->uh_sum = 0;

                    int udp_checksum = ip_checksum(data_to_checksum, ip_data_len + 12);
                    DPRINTF("+++ C+ mode UDP checksum %04x\n",
                        udp_checksum);

                    p_udp_hdr->uh_sum = udp_checksum;
                }

                memcpy(eth_payload_data, saved_ip_header, hlen);
            }
        }

skip_offload:
        ++s->tally_counters.TxOk;

        DPRINTF("+++ C+ mode transmitting %d bytes packet\n", saved_size);

        rtl8139_transfer_frame(s, saved_buffer, saved_size, 1,
            (uint8_t *) dot1q_buffer);

        if (!s->cplus_txbuffer)
        {
            s->cplus_txbuffer        = saved_buffer;
            s->cplus_txbuffer_len    = saved_buffer_len;
            s->cplus_txbuffer_offset = 0;
        }
        else
        {
            g_free(saved_buffer);
        }
    }
    else
    {
        DPRINTF("+++ C+ mode transmission continue to next descriptor\n");
    }

    return 1;
}

static void rtl8139_cplus_transmit(RTL8139State *s)
{
    int txcount = 0;

    while (txcount < 64 && rtl8139_cplus_transmit_one(s))
    {
        ++txcount;
    }

    if (!txcount)
    {
        DPRINTF("C+ mode : transmitter queue stalled, current TxDesc = %d\n",
            s->currCPlusTxDesc);
    }
    else
    {
        s->IntrStatus |= TxOK;
        rtl8139_update_irq(s);
    }
}

static void rtl8139_transmit(RTL8139State *s)
{
    int descriptor = s->currTxDesc, txcount = 0;

    if (rtl8139_transmit_one(s, descriptor))
    {
        ++s->currTxDesc;
        s->currTxDesc %= 4;
        ++txcount;
    }

    if (!txcount)
    {
        DPRINTF("transmitter queue stalled, current TxDesc = %d\n",
            s->currTxDesc);
    }
}

static void rtl8139_TxStatus_write(RTL8139State *s, uint32_t txRegOffset, uint32_t val)
{

    int descriptor = txRegOffset/4;

    if (s->cplus_enabled)
    {
        DPRINTF("RTL8139C+ DTCCR write offset=0x%x val=0x%08x "
            "descriptor=%d\n", txRegOffset, val, descriptor);

        s->TxStatus[descriptor] = val;

        if (descriptor == 0 && (val & 0x8))
        {
            hwaddr tc_addr = rtl8139_addr64(s->TxStatus[0] & ~0x3f, s->TxStatus[1]);

            RTL8139TallyCounters_dma_write(s, tc_addr);

            s->TxStatus[0] &= ~0x8;
        }

        return;
    }

    DPRINTF("TxStatus write offset=0x%x val=0x%08x descriptor=%d\n",
        txRegOffset, val, descriptor);

    val &= ~0xff00c000;
    val = SET_MASKED(val, 0x00c00000, s->TxStatus[descriptor]);

    s->TxStatus[descriptor] = val;

    rtl8139_transmit(s);
}

static uint32_t rtl8139_TxStatus_TxAddr_read(RTL8139State *s, uint32_t regs[],
                                             uint32_t base, uint8_t addr,
                                             int size)
{
    uint32_t reg = (addr - base) / 4;
    uint32_t offset = addr & 0x3;
    uint32_t ret = 0;

    if (addr & (size - 1)) {
        DPRINTF("not implemented read for TxStatus/TxAddr "
                "addr=0x%x size=0x%x\n", addr, size);
        return ret;
    }

    switch (size) {
    case 1: /* fall through */
    case 2: /* fall through */
    case 4:
        ret = (regs[reg] >> offset * 8) & (((uint64_t)1 << (size * 8)) - 1);
        DPRINTF("TxStatus/TxAddr[%d] read addr=0x%x size=0x%x val=0x%08x\n",
                reg, addr, size, ret);
        break;
    default:
        DPRINTF("unsupported size 0x%x of TxStatus/TxAddr reading\n", size);
        break;
    }

    return ret;
}

static uint16_t rtl8139_TSAD_read(RTL8139State *s)
{
    uint16_t ret = 0;

    ret = ((s->TxStatus[3] & TxStatOK  )?TSAD_TOK3:0)
         |((s->TxStatus[2] & TxStatOK  )?TSAD_TOK2:0)
         |((s->TxStatus[1] & TxStatOK  )?TSAD_TOK1:0)
         |((s->TxStatus[0] & TxStatOK  )?TSAD_TOK0:0)

         |((s->TxStatus[3] & TxUnderrun)?TSAD_TUN3:0)
         |((s->TxStatus[2] & TxUnderrun)?TSAD_TUN2:0)
         |((s->TxStatus[1] & TxUnderrun)?TSAD_TUN1:0)
         |((s->TxStatus[0] & TxUnderrun)?TSAD_TUN0:0)

         |((s->TxStatus[3] & TxAborted )?TSAD_TABT3:0)
         |((s->TxStatus[2] & TxAborted )?TSAD_TABT2:0)
         |((s->TxStatus[1] & TxAborted )?TSAD_TABT1:0)
         |((s->TxStatus[0] & TxAborted )?TSAD_TABT0:0)

         |((s->TxStatus[3] & TxHostOwns )?TSAD_OWN3:0)
         |((s->TxStatus[2] & TxHostOwns )?TSAD_OWN2:0)
         |((s->TxStatus[1] & TxHostOwns )?TSAD_OWN1:0)
         |((s->TxStatus[0] & TxHostOwns )?TSAD_OWN0:0) ;


    DPRINTF("TSAD read val=0x%04x\n", ret);

    return ret;
}

static uint16_t rtl8139_CSCR_read(RTL8139State *s)
{
    uint16_t ret = s->CSCR;

    DPRINTF("CSCR read val=0x%04x\n", ret);

    return ret;
}

static void rtl8139_TxAddr_write(RTL8139State *s, uint32_t txAddrOffset, uint32_t val)
{
    DPRINTF("TxAddr write offset=0x%x val=0x%08x\n", txAddrOffset, val);

    s->TxAddr[txAddrOffset/4] = val;
}

static uint32_t rtl8139_TxAddr_read(RTL8139State *s, uint32_t txAddrOffset)
{
    uint32_t ret = s->TxAddr[txAddrOffset/4];

    DPRINTF("TxAddr read offset=0x%x val=0x%08x\n", txAddrOffset, ret);

    return ret;
}

static void rtl8139_RxBufPtr_write(RTL8139State *s, uint32_t val)
{
    DPRINTF("RxBufPtr write val=0x%04x\n", val);

    s->RxBufPtr = MOD2(val + 0x10, s->RxBufferSize);

    qemu_flush_queued_packets(qemu_get_queue(s->nic));

    DPRINTF(" CAPR write: rx buffer length %d head 0x%04x read 0x%04x\n",
        s->RxBufferSize, s->RxBufAddr, s->RxBufPtr);
}

static uint32_t rtl8139_RxBufPtr_read(RTL8139State *s)
{
    uint32_t ret = s->RxBufPtr - 0x10;

    DPRINTF("RxBufPtr read val=0x%04x\n", ret);

    return ret;
}

static uint32_t rtl8139_RxBufAddr_read(RTL8139State *s)
{
    uint32_t ret = s->RxBufAddr;

    DPRINTF("RxBufAddr read val=0x%04x\n", ret);

    return ret;
}

static void rtl8139_RxBuf_write(RTL8139State *s, uint32_t val)
{
    DPRINTF("RxBuf write val=0x%08x\n", val);

    s->RxBuf = val;

}

static uint32_t rtl8139_RxBuf_read(RTL8139State *s)
{
    uint32_t ret = s->RxBuf;

    DPRINTF("RxBuf read val=0x%08x\n", ret);

    return ret;
}

static void rtl8139_IntrMask_write(RTL8139State *s, uint32_t val)
{
    DPRINTF("IntrMask write(w) val=0x%04x\n", val);

    val = SET_MASKED(val, 0x1e00, s->IntrMask);

    s->IntrMask = val;

    rtl8139_update_irq(s);

}

static uint32_t rtl8139_IntrMask_read(RTL8139State *s)
{
    uint32_t ret = s->IntrMask;

    DPRINTF("IntrMask read(w) val=0x%04x\n", ret);

    return ret;
}

static void rtl8139_IntrStatus_write(RTL8139State *s, uint32_t val)
{
    DPRINTF("IntrStatus write(w) val=0x%04x\n", val);

#if 0

    return;

#else
    uint16_t newStatus = s->IntrStatus & ~val;

    newStatus = SET_MASKED(newStatus, 0x1e00, s->IntrStatus);

    s->IntrStatus = 0;
    rtl8139_update_irq(s);

    s->IntrStatus = newStatus;
    rtl8139_set_next_tctr_time(s);
    rtl8139_update_irq(s);

#endif
}

static uint32_t rtl8139_IntrStatus_read(RTL8139State *s)
{
    uint32_t ret = s->IntrStatus;

    DPRINTF("IntrStatus read(w) val=0x%04x\n", ret);

#if 0

    s->IntrStatus = 0;

    rtl8139_update_irq(s);

#endif

    return ret;
}

static void rtl8139_MultiIntr_write(RTL8139State *s, uint32_t val)
{
    DPRINTF("MultiIntr write(w) val=0x%04x\n", val);

    val = SET_MASKED(val, 0xf000, s->MultiIntr);

    s->MultiIntr = val;
}

static uint32_t rtl8139_MultiIntr_read(RTL8139State *s)
{
    uint32_t ret = s->MultiIntr;

    DPRINTF("MultiIntr read(w) val=0x%04x\n", ret);

    return ret;
}

static void rtl8139_io_writeb(void *opaque, uint8_t addr, uint32_t val)
{
    RTL8139State *s = opaque;

    switch (addr)
    {
        case MAC0 ... MAC0+4:
            s->phys[addr - MAC0] = val;
            break;
        case MAC0+5:
            s->phys[addr - MAC0] = val;
            qemu_format_nic_info_str(qemu_get_queue(s->nic), s->phys);
            break;
        case MAC0+6 ... MAC0+7:
            break;
        case MAR0 ... MAR0+7:
            s->mult[addr - MAR0] = val;
            break;
        case ChipCmd:
            rtl8139_ChipCmd_write(s, val);
            break;
        case Cfg9346:
            rtl8139_Cfg9346_write(s, val);
            break;
        case TxConfig:
            rtl8139_TxConfig_writeb(s, val);
            break;
        case Config0:
            rtl8139_Config0_write(s, val);
            break;
        case Config1:
            rtl8139_Config1_write(s, val);
            break;
        case Config3:
            rtl8139_Config3_write(s, val);
            break;
        case Config4:
            rtl8139_Config4_write(s, val);
            break;
        case Config5:
            rtl8139_Config5_write(s, val);
            break;
        case MediaStatus:
            DPRINTF("not implemented write(b) to MediaStatus val=0x%02x\n",
                val);
            break;

        case HltClk:
            DPRINTF("HltClk write val=0x%08x\n", val);
            if (val == 'R')
            {
                s->clock_enabled = 1;
            }
            else if (val == 'H')
            {
                s->clock_enabled = 0;
            }
            break;

        case TxThresh:
            DPRINTF("C+ TxThresh write(b) val=0x%02x\n", val);
            s->TxThresh = val;
            break;

        case TxPoll:
            DPRINTF("C+ TxPoll write(b) val=0x%02x\n", val);
            if (val & (1 << 7))
            {
                DPRINTF("C+ TxPoll high priority transmission (not "
                    "implemented)\n");
            }
            if (val & (1 << 6))
            {
                DPRINTF("C+ TxPoll normal priority transmission\n");
                rtl8139_cplus_transmit(s);
            }

            break;
        case RxConfig:
            DPRINTF("RxConfig write(b) val=0x%02x\n", val);
            rtl8139_RxConfig_write(s,
                (rtl8139_RxConfig_read(s) & 0xFFFFFF00) | val);
            break;
        default:
            DPRINTF("not implemented write(b) addr=0x%x val=0x%02x\n", addr,
                val);
            break;
    }
}

static void rtl8139_io_writew(void *opaque, uint8_t addr, uint32_t val)
{
    RTL8139State *s = opaque;

    switch (addr)
    {
        case IntrMask:
            rtl8139_IntrMask_write(s, val);
            break;

        case IntrStatus:
            rtl8139_IntrStatus_write(s, val);
            break;

        case MultiIntr:
            rtl8139_MultiIntr_write(s, val);
            break;

        case RxBufPtr:
            rtl8139_RxBufPtr_write(s, val);
            break;

        case BasicModeCtrl:
            rtl8139_BasicModeCtrl_write(s, val);
            break;
        case BasicModeStatus:
            rtl8139_BasicModeStatus_write(s, val);
            break;
        case NWayAdvert:
            DPRINTF("NWayAdvert write(w) val=0x%04x\n", val);
            s->NWayAdvert = val;
            break;
        case NWayLPAR:
            DPRINTF("forbidden NWayLPAR write(w) val=0x%04x\n", val);
            break;
        case NWayExpansion:
            DPRINTF("NWayExpansion write(w) val=0x%04x\n", val);
            s->NWayExpansion = val;
            break;

        case CpCmd:
            rtl8139_CpCmd_write(s, val);
            break;

        case IntrMitigate:
            rtl8139_IntrMitigate_write(s, val);
            break;

        default:
            DPRINTF("ioport write(w) addr=0x%x val=0x%04x via write(b)\n",
                addr, val);

            rtl8139_io_writeb(opaque, addr, val & 0xff);
            rtl8139_io_writeb(opaque, addr + 1, (val >> 8) & 0xff);
            break;
    }
}

static void rtl8139_set_next_tctr_time(RTL8139State *s)
{
    const uint64_t ns_per_period = (uint64_t)PCI_PERIOD << 32;

    DPRINTF("entered rtl8139_set_next_tctr_time\n");

    while (s->TCTR_base + ns_per_period <= qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)) {
        s->TCTR_base += ns_per_period;
    }

    if (!s->TimerInt) {
        timer_del(s->timer);
    } else {
        uint64_t delta = (uint64_t)s->TimerInt * PCI_PERIOD;
        if (s->TCTR_base + delta <= qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)) {
            delta += ns_per_period;
        }
        timer_mod(s->timer, s->TCTR_base + delta);
    }
}

static void rtl8139_io_writel(void *opaque, uint8_t addr, uint32_t val)
{
    RTL8139State *s = opaque;

    switch (addr)
    {
        case RxMissed:
            DPRINTF("RxMissed clearing on write\n");
            s->RxMissed = 0;
            break;

        case TxConfig:
            rtl8139_TxConfig_write(s, val);
            break;

        case RxConfig:
            rtl8139_RxConfig_write(s, val);
            break;

        case TxStatus0 ... TxStatus0+4*4-1:
            rtl8139_TxStatus_write(s, addr-TxStatus0, val);
            break;

        case TxAddr0 ... TxAddr0+4*4-1:
            rtl8139_TxAddr_write(s, addr-TxAddr0, val);
            break;

        case RxBuf:
            rtl8139_RxBuf_write(s, val);
            break;

        case RxRingAddrLO:
            DPRINTF("C+ RxRing low bits write val=0x%08x\n", val);
            s->RxRingAddrLO = val;
            break;

        case RxRingAddrHI:
            DPRINTF("C+ RxRing high bits write val=0x%08x\n", val);
            s->RxRingAddrHI = val;
            break;

        case Timer:
            DPRINTF("TCTR Timer reset on write\n");
            s->TCTR_base = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            rtl8139_set_next_tctr_time(s);
            break;

        case FlashReg:
            DPRINTF("FlashReg TimerInt write val=0x%08x\n", val);
            if (s->TimerInt != val) {
                s->TimerInt = val;
                rtl8139_set_next_tctr_time(s);
            }
            break;

        default:
            DPRINTF("ioport write(l) addr=0x%x val=0x%08x via write(b)\n",
                addr, val);
            rtl8139_io_writeb(opaque, addr, val & 0xff);
            rtl8139_io_writeb(opaque, addr + 1, (val >> 8) & 0xff);
            rtl8139_io_writeb(opaque, addr + 2, (val >> 16) & 0xff);
            rtl8139_io_writeb(opaque, addr + 3, (val >> 24) & 0xff);
            break;
    }
}

static uint32_t rtl8139_io_readb(void *opaque, uint8_t addr)
{
    RTL8139State *s = opaque;
    int ret;

    switch (addr)
    {
        case MAC0 ... MAC0+5:
            ret = s->phys[addr - MAC0];
            break;
        case MAC0+6 ... MAC0+7:
            ret = 0;
            break;
        case MAR0 ... MAR0+7:
            ret = s->mult[addr - MAR0];
            break;
        case TxStatus0 ... TxStatus0+4*4-1:
            ret = rtl8139_TxStatus_TxAddr_read(s, s->TxStatus, TxStatus0,
                                               addr, 1);
            break;
        case ChipCmd:
            ret = rtl8139_ChipCmd_read(s);
            break;
        case Cfg9346:
            ret = rtl8139_Cfg9346_read(s);
            break;
        case Config0:
            ret = rtl8139_Config0_read(s);
            break;
        case Config1:
            ret = rtl8139_Config1_read(s);
            break;
        case Config3:
            ret = rtl8139_Config3_read(s);
            break;
        case Config4:
            ret = rtl8139_Config4_read(s);
            break;
        case Config5:
            ret = rtl8139_Config5_read(s);
            break;

        case MediaStatus:
            ret = 0xd0 | (~s->BasicModeStatus & 0x04);
            DPRINTF("MediaStatus read 0x%x\n", ret);
            break;

        case HltClk:
            ret = s->clock_enabled;
            DPRINTF("HltClk read 0x%x\n", ret);
            break;

        case PCIRevisionID:
            ret = RTL8139_PCI_REVID;
            DPRINTF("PCI Revision ID read 0x%x\n", ret);
            break;

        case TxThresh:
            ret = s->TxThresh;
            DPRINTF("C+ TxThresh read(b) val=0x%02x\n", ret);
            break;

        case 0x43:
            ret = s->TxConfig >> 24;
            DPRINTF("RTL8139C TxConfig at 0x43 read(b) val=0x%02x\n", ret);
            break;

        default:
            DPRINTF("not implemented read(b) addr=0x%x\n", addr);
            ret = 0;
            break;
    }

    return ret;
}

static uint32_t rtl8139_io_readw(void *opaque, uint8_t addr)
{
    RTL8139State *s = opaque;
    uint32_t ret;

    switch (addr)
    {
        case TxAddr0 ... TxAddr0+4*4-1:
            ret = rtl8139_TxStatus_TxAddr_read(s, s->TxAddr, TxAddr0, addr, 2);
            break;
        case IntrMask:
            ret = rtl8139_IntrMask_read(s);
            break;

        case IntrStatus:
            ret = rtl8139_IntrStatus_read(s);
            break;

        case MultiIntr:
            ret = rtl8139_MultiIntr_read(s);
            break;

        case RxBufPtr:
            ret = rtl8139_RxBufPtr_read(s);
            break;

        case RxBufAddr:
            ret = rtl8139_RxBufAddr_read(s);
            break;

        case BasicModeCtrl:
            ret = rtl8139_BasicModeCtrl_read(s);
            break;
        case BasicModeStatus:
            ret = rtl8139_BasicModeStatus_read(s);
            break;
        case NWayAdvert:
            ret = s->NWayAdvert;
            DPRINTF("NWayAdvert read(w) val=0x%04x\n", ret);
            break;
        case NWayLPAR:
            ret = s->NWayLPAR;
            DPRINTF("NWayLPAR read(w) val=0x%04x\n", ret);
            break;
        case NWayExpansion:
            ret = s->NWayExpansion;
            DPRINTF("NWayExpansion read(w) val=0x%04x\n", ret);
            break;

        case CpCmd:
            ret = rtl8139_CpCmd_read(s);
            break;

        case IntrMitigate:
            ret = rtl8139_IntrMitigate_read(s);
            break;

        case TxSummary:
            ret = rtl8139_TSAD_read(s);
            break;

        case CSCR:
            ret = rtl8139_CSCR_read(s);
            break;

        default:
            DPRINTF("ioport read(w) addr=0x%x via read(b)\n", addr);

            ret  = rtl8139_io_readb(opaque, addr);
            ret |= rtl8139_io_readb(opaque, addr + 1) << 8;

            DPRINTF("ioport read(w) addr=0x%x val=0x%04x\n", addr, ret);
            break;
    }

    return ret;
}

static uint32_t rtl8139_io_readl(void *opaque, uint8_t addr)
{
    RTL8139State *s = opaque;
    uint32_t ret;

    switch (addr)
    {
        case RxMissed:
            ret = s->RxMissed;

            DPRINTF("RxMissed read val=0x%08x\n", ret);
            break;

        case TxConfig:
            ret = rtl8139_TxConfig_read(s);
            break;

        case RxConfig:
            ret = rtl8139_RxConfig_read(s);
            break;

        case TxStatus0 ... TxStatus0+4*4-1:
            ret = rtl8139_TxStatus_TxAddr_read(s, s->TxStatus, TxStatus0,
                                               addr, 4);
            break;

        case TxAddr0 ... TxAddr0+4*4-1:
            ret = rtl8139_TxAddr_read(s, addr-TxAddr0);
            break;

        case RxBuf:
            ret = rtl8139_RxBuf_read(s);
            break;

        case RxRingAddrLO:
            ret = s->RxRingAddrLO;
            DPRINTF("C+ RxRing low bits read val=0x%08x\n", ret);
            break;

        case RxRingAddrHI:
            ret = s->RxRingAddrHI;
            DPRINTF("C+ RxRing high bits read val=0x%08x\n", ret);
            break;

        case Timer:
            ret = (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - s->TCTR_base) /
                  PCI_PERIOD;
            DPRINTF("TCTR Timer read val=0x%08x\n", ret);
            break;

        case FlashReg:
            ret = s->TimerInt;
            DPRINTF("FlashReg TimerInt read val=0x%08x\n", ret);
            break;

        default:
            DPRINTF("ioport read(l) addr=0x%x via read(b)\n", addr);

            ret  = rtl8139_io_readb(opaque, addr);
            ret |= rtl8139_io_readb(opaque, addr + 1) << 8;
            ret |= rtl8139_io_readb(opaque, addr + 2) << 16;
            ret |= rtl8139_io_readb(opaque, addr + 3) << 24;

            DPRINTF("read(l) addr=0x%x val=%08x\n", addr, ret);
            break;
    }

    return ret;
}

static int rtl8139_post_load(void *opaque, int version_id)
{
    RTL8139State* s = opaque;
    rtl8139_set_next_tctr_time(s);
    if (version_id < 4) {
        s->cplus_enabled = s->CpCmd != 0;
    }

    qemu_get_queue(s->nic)->link_down = (s->BasicModeStatus & 0x04) == 0;

    return 0;
}

static bool rtl8139_hotplug_ready_needed(void *opaque)
{
    return qdev_machine_modified();
}

static const VMStateDescription vmstate_rtl8139_hotplug_ready ={
    .name = "rtl8139/hotplug_ready",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = rtl8139_hotplug_ready_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static int rtl8139_pre_save(void *opaque)
{
    RTL8139State* s = opaque;
    int64_t current_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    s->TCTR = (current_time - s->TCTR_base) / PCI_PERIOD;
    s->rtl8139_mmio_io_addr_dummy = 0;

    return 0;
}

static const VMStateDescription vmstate_rtl8139 = {
    .name = "rtl8139",
    .version_id = 5,
    .minimum_version_id = 3,
    .post_load = rtl8139_post_load,
    .pre_save  = rtl8139_pre_save,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, RTL8139State),
        VMSTATE_PARTIAL_BUFFER(phys, RTL8139State, 6),
        VMSTATE_BUFFER(mult, RTL8139State),
        VMSTATE_UINT32_ARRAY(TxStatus, RTL8139State, 4),
        VMSTATE_UINT32_ARRAY(TxAddr, RTL8139State, 4),

        VMSTATE_UINT32(RxBuf, RTL8139State),
        VMSTATE_UINT32(RxBufferSize, RTL8139State),
        VMSTATE_UINT32(RxBufPtr, RTL8139State),
        VMSTATE_UINT32(RxBufAddr, RTL8139State),

        VMSTATE_UINT16(IntrStatus, RTL8139State),
        VMSTATE_UINT16(IntrMask, RTL8139State),

        VMSTATE_UINT32(TxConfig, RTL8139State),
        VMSTATE_UINT32(RxConfig, RTL8139State),
        VMSTATE_UINT32(RxMissed, RTL8139State),
        VMSTATE_UINT16(CSCR, RTL8139State),

        VMSTATE_UINT8(Cfg9346, RTL8139State),
        VMSTATE_UINT8(Config0, RTL8139State),
        VMSTATE_UINT8(Config1, RTL8139State),
        VMSTATE_UINT8(Config3, RTL8139State),
        VMSTATE_UINT8(Config4, RTL8139State),
        VMSTATE_UINT8(Config5, RTL8139State),

        VMSTATE_UINT8(clock_enabled, RTL8139State),
        VMSTATE_UINT8(bChipCmdState, RTL8139State),

        VMSTATE_UINT16(MultiIntr, RTL8139State),

        VMSTATE_UINT16(BasicModeCtrl, RTL8139State),
        VMSTATE_UINT16(BasicModeStatus, RTL8139State),
        VMSTATE_UINT16(NWayAdvert, RTL8139State),
        VMSTATE_UINT16(NWayLPAR, RTL8139State),
        VMSTATE_UINT16(NWayExpansion, RTL8139State),

        VMSTATE_UINT16(CpCmd, RTL8139State),
        VMSTATE_UINT8(TxThresh, RTL8139State),

        VMSTATE_UNUSED(4),
        VMSTATE_MACADDR(conf.macaddr, RTL8139State),
        VMSTATE_INT32(rtl8139_mmio_io_addr_dummy, RTL8139State),

        VMSTATE_UINT32(currTxDesc, RTL8139State),
        VMSTATE_UINT32(currCPlusRxDesc, RTL8139State),
        VMSTATE_UINT32(currCPlusTxDesc, RTL8139State),
        VMSTATE_UINT32(RxRingAddrLO, RTL8139State),
        VMSTATE_UINT32(RxRingAddrHI, RTL8139State),

        VMSTATE_UINT16_ARRAY(eeprom.contents, RTL8139State, EEPROM_9346_SIZE),
        VMSTATE_INT32(eeprom.mode, RTL8139State),
        VMSTATE_UINT32(eeprom.tick, RTL8139State),
        VMSTATE_UINT8(eeprom.address, RTL8139State),
        VMSTATE_UINT16(eeprom.input, RTL8139State),
        VMSTATE_UINT16(eeprom.output, RTL8139State),

        VMSTATE_UINT8(eeprom.eecs, RTL8139State),
        VMSTATE_UINT8(eeprom.eesk, RTL8139State),
        VMSTATE_UINT8(eeprom.eedi, RTL8139State),
        VMSTATE_UINT8(eeprom.eedo, RTL8139State),

        VMSTATE_UINT32(TCTR, RTL8139State),
        VMSTATE_UINT32(TimerInt, RTL8139State),
        VMSTATE_INT64(TCTR_base, RTL8139State),

        VMSTATE_UINT64(tally_counters.TxOk, RTL8139State),
        VMSTATE_UINT64(tally_counters.RxOk, RTL8139State),
        VMSTATE_UINT64(tally_counters.TxERR, RTL8139State),
        VMSTATE_UINT32(tally_counters.RxERR, RTL8139State),
        VMSTATE_UINT16(tally_counters.MissPkt, RTL8139State),
        VMSTATE_UINT16(tally_counters.FAE, RTL8139State),
        VMSTATE_UINT32(tally_counters.Tx1Col, RTL8139State),
        VMSTATE_UINT32(tally_counters.TxMCol, RTL8139State),
        VMSTATE_UINT64(tally_counters.RxOkPhy, RTL8139State),
        VMSTATE_UINT64(tally_counters.RxOkBrd, RTL8139State),
        VMSTATE_UINT32_V(tally_counters.RxOkMul, RTL8139State, 5),
        VMSTATE_UINT16(tally_counters.TxAbt, RTL8139State),
        VMSTATE_UINT16(tally_counters.TxUndrn, RTL8139State),

        VMSTATE_UINT32_V(cplus_enabled, RTL8139State, 4),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_rtl8139_hotplug_ready,
        NULL
    }
};

static void rtl8139_ioport_write(void *opaque, hwaddr addr,
                                 uint64_t val, unsigned size)
{
    switch (size) {
    case 1:
        rtl8139_io_writeb(opaque, addr, val);
        break;
    case 2:
        rtl8139_io_writew(opaque, addr, val);
        break;
    case 4:
        rtl8139_io_writel(opaque, addr, val);
        break;
    }
}

static uint64_t rtl8139_ioport_read(void *opaque, hwaddr addr,
                                    unsigned size)
{
    switch (size) {
    case 1:
        return rtl8139_io_readb(opaque, addr);
    case 2:
        return rtl8139_io_readw(opaque, addr);
    case 4:
        return rtl8139_io_readl(opaque, addr);
    }

    return -1;
}

static const MemoryRegionOps rtl8139_io_ops = {
    .read = rtl8139_ioport_read,
    .write = rtl8139_ioport_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void rtl8139_timer(void *opaque)
{
    RTL8139State *s = opaque;

    if (!s->clock_enabled)
    {
        DPRINTF(">>> timer: clock is not running\n");
        return;
    }

    s->IntrStatus |= PCSTimeout;
    rtl8139_update_irq(s);
    rtl8139_set_next_tctr_time(s);
}

static void pci_rtl8139_uninit(PCIDevice *dev)
{
    RTL8139State *s = RTL8139(dev);

    g_free(s->cplus_txbuffer);
    s->cplus_txbuffer = NULL;
    timer_free(s->timer);
    qemu_del_nic(s->nic);
}

static void rtl8139_set_link_status(NetClientState *nc)
{
    RTL8139State *s = qemu_get_nic_opaque(nc);

    if (nc->link_down) {
        s->BasicModeStatus &= ~0x04;
    } else {
        s->BasicModeStatus |= 0x04;
    }

    s->IntrStatus |= RxUnderrun;
    rtl8139_update_irq(s);
}

static NetClientInfo net_rtl8139_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = rtl8139_can_receive,
    .receive = rtl8139_receive,
    .link_status_changed = rtl8139_set_link_status,
};

static void pci_rtl8139_realize(PCIDevice *dev, Error **errp)
{
    RTL8139State *s = RTL8139(dev);
    DeviceState *d = DEVICE(dev);
    uint8_t *pci_conf;

    pci_conf = dev->config;
    pci_conf[PCI_INTERRUPT_PIN] = 1;
    pci_conf[PCI_CAPABILITY_LIST] = 0xdc;

    memory_region_init_io(&s->bar_io, OBJECT(s), &rtl8139_io_ops, s,
                          "rtl8139", 0x100);
    memory_region_init_alias(&s->bar_mem, OBJECT(s), "rtl8139-mem", &s->bar_io,
                             0, 0x100);

    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_io);
    pci_register_bar(dev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_mem);

    qemu_macaddr_default_if_unset(&s->conf.macaddr);

    s->eeprom.contents[0] = 0x8129;
#if 1
    s->eeprom.contents[1] = PCI_VENDOR_ID_REALTEK;
    s->eeprom.contents[2] = PCI_DEVICE_ID_REALTEK_8139;
#endif
    s->eeprom.contents[7] = s->conf.macaddr.a[0] | s->conf.macaddr.a[1] << 8;
    s->eeprom.contents[8] = s->conf.macaddr.a[2] | s->conf.macaddr.a[3] << 8;
    s->eeprom.contents[9] = s->conf.macaddr.a[4] | s->conf.macaddr.a[5] << 8;

    s->nic = qemu_new_nic(&net_rtl8139_info, &s->conf,
                          object_get_typename(OBJECT(dev)), d->id,
                          &d->mem_reentrancy_guard, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);

    s->cplus_txbuffer = NULL;
    s->cplus_txbuffer_len = 0;
    s->cplus_txbuffer_offset = 0;

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, rtl8139_timer, s);
}

static void rtl8139_instance_init(Object *obj)
{
    RTL8139State *s = RTL8139(obj);

    device_add_bootindex_property(obj, &s->conf.bootindex,
                                  "bootindex", "/ethernet-phy@0",
                                  DEVICE(obj));
}

static const Property rtl8139_properties[] = {
    DEFINE_NIC_PROPERTIES(RTL8139State, conf),
};

static void rtl8139_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pci_rtl8139_realize;
    k->exit = pci_rtl8139_uninit;
    k->romfile = "efi-rtl8139.rom";
    k->vendor_id = PCI_VENDOR_ID_REALTEK;
    k->device_id = PCI_DEVICE_ID_REALTEK_8139;
    k->revision = RTL8139_PCI_REVID;
    k->class_id = PCI_CLASS_NETWORK_ETHERNET;
    device_class_set_legacy_reset(dc, rtl8139_reset);
    dc->vmsd = &vmstate_rtl8139;
    device_class_set_props(dc, rtl8139_properties);
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo rtl8139_info = {
    .name          = TYPE_RTL8139,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(RTL8139State),
    .class_init    = rtl8139_class_init,
    .instance_init = rtl8139_instance_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void rtl8139_register_types(void)
{
    type_register_static(&rtl8139_info);
}

type_init(rtl8139_register_types)
