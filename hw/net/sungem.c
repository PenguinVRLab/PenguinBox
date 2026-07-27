/*
 * QEMU model of SUN GEM ethernet controller
 *
 * As found in Apple ASICs among others
 *
 * Copyright 2016 Ben Herrenschmidt
 * Copyright 2017 Mark Cave-Ayland
 */

#include "qemu/osdep.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "net/net.h"
#include "net/eth.h"
#include "net/checksum.h"
#include "hw/net/mii.h"
#include "system/system.h"
#include "trace.h"
#include "qom/object.h"

#define TYPE_SUNGEM "sungem"

OBJECT_DECLARE_SIMPLE_TYPE(SunGEMState, SUNGEM)

#define MAX_PACKET_SIZE 9016

#define SUNGEM_MMIO_SIZE        0x200000

#define SUNGEM_MMIO_GREG_SIZE   0x2000

#define GREG_SEBSTATE     0x0000UL

#define GREG_STAT         0x000CUL
#define GREG_STAT_TXINTME     0x00000001
#define GREG_STAT_TXALL       0x00000002
#define GREG_STAT_TXDONE      0x00000004
#define GREG_STAT_RXDONE      0x00000010
#define GREG_STAT_RXNOBUF     0x00000020
#define GREG_STAT_RXTAGERR    0x00000040
#define GREG_STAT_TXMAC       0x00004000
#define GREG_STAT_RXMAC       0x00008000
#define GREG_STAT_MAC         0x00010000
#define GREG_STAT_TXNR        0xfff80000
#define GREG_STAT_TXNR_SHIFT  19

#define GREG_STAT_LATCH       (GREG_STAT_TXALL  | GREG_STAT_TXINTME | \
                               GREG_STAT_RXDONE | GREG_STAT_RXDONE |  \
                               GREG_STAT_RXNOBUF | GREG_STAT_RXTAGERR)

#define GREG_IMASK        0x0010UL
#define GREG_IACK         0x0014UL
#define GREG_STAT2        0x001CUL
#define GREG_PCIESTAT     0x1000UL
#define GREG_PCIEMASK     0x1004UL

#define GREG_SWRST        0x1010UL
#define GREG_SWRST_TXRST      0x00000001
#define GREG_SWRST_RXRST      0x00000002
#define GREG_SWRST_RSTOUT     0x00000004

#define SUNGEM_MMIO_TXDMA_SIZE   0x1000

#define TXDMA_KICK        0x0000UL

#define TXDMA_CFG         0x0004UL
#define TXDMA_CFG_ENABLE      0x00000001
#define TXDMA_CFG_RINGSZ      0x0000001e

#define TXDMA_DBLOW       0x0008UL
#define TXDMA_DBHI        0x000CUL
#define TXDMA_PCNT        0x0024UL
#define TXDMA_SMACHINE    0x0028UL
#define TXDMA_DPLOW       0x0030UL
#define TXDMA_DPHI        0x0034UL
#define TXDMA_TXDONE      0x0100UL
#define TXDMA_FTAG        0x0108UL
#define TXDMA_FSZ         0x0118UL

#define SUNGEM_MMIO_RXDMA_SIZE   0x2000

#define RXDMA_CFG         0x0000UL
#define RXDMA_CFG_ENABLE      0x00000001
#define RXDMA_CFG_RINGSZ      0x0000001e
#define RXDMA_CFG_FBOFF       0x00001c00
#define RXDMA_CFG_CSUMOFF     0x000fe000

#define RXDMA_DBLOW       0x0004UL
#define RXDMA_DBHI        0x0008UL
#define RXDMA_PCNT        0x0018UL
#define RXDMA_SMACHINE    0x001CUL
#define RXDMA_PTHRESH     0x0020UL
#define RXDMA_DPLOW       0x0024UL
#define RXDMA_DPHI        0x0028UL
#define RXDMA_KICK        0x0100UL
#define RXDMA_DONE        0x0104UL
#define RXDMA_BLANK       0x0108UL
#define RXDMA_FTAG        0x0110UL
#define RXDMA_FSZ         0x0120UL

#define SUNGEM_MMIO_WOL_SIZE   0x14

#define WOL_MATCH0        0x0000UL
#define WOL_MATCH1        0x0004UL
#define WOL_MATCH2        0x0008UL
#define WOL_MCOUNT        0x000CUL
#define WOL_WAKECSR       0x0010UL

#define SUNGEM_MMIO_MAC_SIZE   0x200

#define MAC_TXRST         0x0000UL
#define MAC_RXRST         0x0004UL
#define MAC_TXSTAT        0x0010UL
#define MAC_RXSTAT        0x0014UL

#define MAC_CSTAT         0x0018UL
#define MAC_CSTAT_PTR         0xffff0000

#define MAC_TXMASK        0x0020UL
#define MAC_RXMASK        0x0024UL
#define MAC_MCMASK        0x0028UL

#define MAC_TXCFG         0x0030UL
#define MAC_TXCFG_ENAB        0x00000001

#define MAC_RXCFG         0x0034UL
#define MAC_RXCFG_ENAB        0x00000001
#define MAC_RXCFG_SFCS        0x00000004
#define MAC_RXCFG_PROM        0x00000008
#define MAC_RXCFG_PGRP        0x00000010
#define MAC_RXCFG_HFE         0x00000020

#define MAC_XIFCFG        0x003CUL
#define MAC_XIFCFG_LBCK       0x00000002

#define MAC_MINFSZ        0x0050UL
#define MAC_MAXFSZ        0x0054UL
#define MAC_ADDR0         0x0080UL
#define MAC_ADDR1         0x0084UL
#define MAC_ADDR2         0x0088UL
#define MAC_ADDR3         0x008CUL
#define MAC_ADDR4         0x0090UL
#define MAC_ADDR5         0x0094UL
#define MAC_HASH0         0x00C0UL
#define MAC_PATMPS        0x0114UL
#define MAC_SMACHINE      0x0134UL

#define SUNGEM_MMIO_MIF_SIZE   0x20

#define MIF_FRAME         0x000CUL
#define MIF_FRAME_OP          0x30000000
#define MIF_FRAME_PHYAD       0x0f800000
#define MIF_FRAME_REGAD       0x007c0000
#define MIF_FRAME_TALSB       0x00010000
#define MIF_FRAME_DATA        0x0000ffff

#define MIF_CFG           0x0010UL
#define MIF_CFG_MDI0          0x00000100
#define MIF_CFG_MDI1          0x00000200

#define MIF_STATUS        0x0018UL
#define MIF_SMACHINE      0x001CUL

#define SUNGEM_MMIO_PCS_SIZE   0x60
#define PCS_MIISTAT       0x0004UL
#define PCS_ISTAT         0x0018UL

#define PCS_SSTATE        0x005CUL

struct gem_txd {
    uint64_t control_word;
    uint64_t buffer;
};

#define TXDCTRL_BUFSZ     0x0000000000007fffULL
#define TXDCTRL_CSTART    0x00000000001f8000ULL
#define TXDCTRL_COFF      0x000000001fe00000ULL
#define TXDCTRL_CENAB     0x0000000020000000ULL
#define TXDCTRL_EOF       0x0000000040000000ULL
#define TXDCTRL_SOF       0x0000000080000000ULL
#define TXDCTRL_INTME     0x0000000100000000ULL

struct gem_rxd {
    uint64_t status_word;
    uint64_t buffer;
};

#define RXDCTRL_HPASS     0x1000000000000000ULL
#define RXDCTRL_ALTMAC    0x2000000000000000ULL


struct SunGEMState {
    PCIDevice pdev;

    MemoryRegion sungem;
    MemoryRegion greg;
    MemoryRegion txdma;
    MemoryRegion rxdma;
    MemoryRegion wol;
    MemoryRegion mac;
    MemoryRegion mif;
    MemoryRegion pcs;
    NICState *nic;
    NICConf conf;
    uint32_t phy_addr;

    uint32_t gregs[SUNGEM_MMIO_GREG_SIZE >> 2];
    uint32_t txdmaregs[SUNGEM_MMIO_TXDMA_SIZE >> 2];
    uint32_t rxdmaregs[SUNGEM_MMIO_RXDMA_SIZE >> 2];
    uint32_t macregs[SUNGEM_MMIO_MAC_SIZE >> 2];
    uint32_t mifregs[SUNGEM_MMIO_MIF_SIZE >> 2];
    uint32_t pcsregs[SUNGEM_MMIO_PCS_SIZE >> 2];

    uint32_t rx_mask;
    uint32_t tx_mask;

    uint8_t tx_data[MAX_PACKET_SIZE];
    uint32_t tx_size;
    uint64_t tx_first_ctl;
};


static void sungem_eval_irq(SunGEMState *s)
{
    uint32_t stat, mask;

    mask = s->gregs[GREG_IMASK >> 2];
    stat = s->gregs[GREG_STAT >> 2] & ~GREG_STAT_TXNR;
    if (stat & ~mask) {
        pci_set_irq(PCI_DEVICE(s), 1);
    } else {
        pci_set_irq(PCI_DEVICE(s), 0);
    }
}

static void sungem_update_status(SunGEMState *s, uint32_t bits, bool val)
{
    uint32_t stat;

    stat = s->gregs[GREG_STAT >> 2];
    if (val) {
        stat |= bits;
    } else {
        stat &= ~bits;
    }
    s->gregs[GREG_STAT >> 2] = stat;
    sungem_eval_irq(s);
}

static void sungem_eval_cascade_irq(SunGEMState *s)
{
    uint32_t stat, mask;

    mask = s->macregs[MAC_TXSTAT >> 2];
    stat = s->macregs[MAC_TXMASK >> 2];
    if (stat & ~mask) {
        sungem_update_status(s, GREG_STAT_TXMAC, true);
    } else {
        sungem_update_status(s, GREG_STAT_TXMAC, false);
    }

    mask = s->macregs[MAC_RXSTAT >> 2];
    stat = s->macregs[MAC_RXMASK >> 2];
    if (stat & ~mask) {
        sungem_update_status(s, GREG_STAT_RXMAC, true);
    } else {
        sungem_update_status(s, GREG_STAT_RXMAC, false);
    }

    mask = s->macregs[MAC_CSTAT >> 2];
    stat = s->macregs[MAC_MCMASK >> 2] & ~MAC_CSTAT_PTR;
    if (stat & ~mask) {
        sungem_update_status(s, GREG_STAT_MAC, true);
    } else {
        sungem_update_status(s, GREG_STAT_MAC, false);
    }
}

static void sungem_do_tx_csum(SunGEMState *s)
{
    uint16_t start, off;
    uint32_t csum;

    start = (s->tx_first_ctl & TXDCTRL_CSTART) >> 15;
    off = (s->tx_first_ctl & TXDCTRL_COFF) >> 21;

    trace_sungem_tx_checksum(start, off);

    if (start > (s->tx_size - 2) || off > (s->tx_size - 2)) {
        trace_sungem_tx_checksum_oob();
        return;
    }

    csum = net_raw_checksum(s->tx_data + start, s->tx_size - start);
    stw_be_p(s->tx_data + off, csum);
}

static void sungem_send_packet(SunGEMState *s, const uint8_t *buf,
                               int size)
{
    NetClientState *nc = qemu_get_queue(s->nic);

    if (s->macregs[MAC_XIFCFG >> 2] & MAC_XIFCFG_LBCK) {
        qemu_receive_packet(nc, buf, size);
    } else {
        qemu_send_packet(nc, buf, size);
    }
}

static void sungem_process_tx_desc(SunGEMState *s, struct gem_txd *desc)
{
    PCIDevice *d = PCI_DEVICE(s);
    uint32_t len;

    if (desc->control_word & TXDCTRL_SOF) {
        if (s->tx_first_ctl) {
            trace_sungem_tx_unfinished();
        }
        s->tx_size = 0;
        s->tx_first_ctl = desc->control_word;
    }

    len = desc->control_word & TXDCTRL_BUFSZ;

    if ((s->tx_size + len) > MAX_PACKET_SIZE) {
        trace_sungem_tx_overflow();
        len = MAX_PACKET_SIZE - s->tx_size;
    }

    pci_dma_read(d, desc->buffer, &s->tx_data[s->tx_size], len);
    s->tx_size += len;

    if (desc->control_word & TXDCTRL_EOF) {
        trace_sungem_tx_finished(s->tx_size);

        if (s->tx_first_ctl & TXDCTRL_CENAB) {
            sungem_do_tx_csum(s);
        }

        sungem_send_packet(s, s->tx_data, s->tx_size);

        s->tx_size = 0;
        s->tx_first_ctl = 0;
    }
}

static void sungem_tx_kick(SunGEMState *s)
{
    PCIDevice *d = PCI_DEVICE(s);
    uint32_t comp, kick;
    uint32_t txdma_cfg, txmac_cfg, ints;
    uint64_t dbase;

    trace_sungem_tx_kick();

    txdma_cfg = s->txdmaregs[TXDMA_CFG >> 2];
    txmac_cfg = s->macregs[MAC_TXCFG >> 2];
    if (!(txdma_cfg & TXDMA_CFG_ENABLE) ||
        !(txmac_cfg & MAC_TXCFG_ENAB)) {
        trace_sungem_tx_disabled();
        return;
    }

    dbase = s->txdmaregs[TXDMA_DBHI >> 2];
    dbase = (dbase << 32) | s->txdmaregs[TXDMA_DBLOW >> 2];

    comp = s->txdmaregs[TXDMA_TXDONE >> 2] & s->tx_mask;
    kick = s->txdmaregs[TXDMA_KICK >> 2] & s->tx_mask;

    trace_sungem_tx_process(comp, kick, s->tx_mask + 1);

    while (comp != kick) {
        struct gem_txd desc;

        pci_dma_read(d, dbase + comp * sizeof(desc), &desc, sizeof(desc));

        desc.control_word = le64_to_cpu(desc.control_word);
        desc.buffer = le64_to_cpu(desc.buffer);
        trace_sungem_tx_desc(comp, desc.control_word, desc.buffer);

        sungem_process_tx_desc(s, &desc);

        ints = GREG_STAT_TXDONE;
        if (desc.control_word & TXDCTRL_INTME) {
            ints |= GREG_STAT_TXINTME;
        }
        sungem_update_status(s, ints, true);

        comp = (comp + 1) & s->tx_mask;
        s->txdmaregs[TXDMA_TXDONE >> 2] = comp;
    }

    sungem_update_status(s, GREG_STAT_TXALL, true);
}

static bool sungem_rx_full(SunGEMState *s, uint32_t kick, uint32_t done)
{
    return kick == ((done + 1) & s->rx_mask);
}

static bool sungem_can_receive(NetClientState *nc)
{
    SunGEMState *s = qemu_get_nic_opaque(nc);
    uint32_t kick, done, rxdma_cfg, rxmac_cfg;
    bool full;

    rxmac_cfg = s->macregs[MAC_RXCFG >> 2];
    rxdma_cfg = s->rxdmaregs[RXDMA_CFG >> 2];

    if ((rxmac_cfg & MAC_RXCFG_ENAB) == 0) {
        trace_sungem_rx_mac_disabled();
        return false;
    }
    if ((rxdma_cfg & RXDMA_CFG_ENABLE) == 0) {
        trace_sungem_rx_txdma_disabled();
        return false;
    }

    kick = s->rxdmaregs[RXDMA_KICK >> 2];
    done = s->rxdmaregs[RXDMA_DONE >> 2];
    full = sungem_rx_full(s, kick, done);

    trace_sungem_rx_check(!full, kick, done);

    return !full;
}

enum {
        rx_no_match,
        rx_match_promisc,
        rx_match_bcast,
        rx_match_allmcast,
        rx_match_mcast,
        rx_match_mac,
        rx_match_altmac,
};

static int sungem_check_rx_mac(SunGEMState *s, const uint8_t *mac, uint32_t crc)
{
    uint32_t rxcfg = s->macregs[MAC_RXCFG >> 2];
    uint32_t mac0, mac1, mac2;

    if (rxcfg & MAC_RXCFG_PROM) {
        return rx_match_promisc;
    }

    mac0 = (mac[4] << 8) | mac[5];
    mac1 = (mac[2] << 8) | mac[3];
    mac2 = (mac[0] << 8) | mac[1];

    trace_sungem_rx_mac_check(mac0, mac1, mac2);

    if (mac0 == 0xffff && mac1 == 0xffff && mac2 == 0xffff) {
        return rx_match_bcast;
    }

    if (mac[0] & 1) {
        trace_sungem_rx_mac_multicast();

        if (rxcfg & MAC_RXCFG_PGRP) {
            return rx_match_allmcast;
        }

        if (rxcfg & MAC_RXCFG_HFE) {
            uint32_t hash, idx;

            crc >>= 24;
            idx = (crc >> 2) & 0x3c;
            hash = s->macregs[(MAC_HASH0 + idx) >> 2];
            if (hash & (1 << (15 - (crc & 0xf)))) {
                return rx_match_mcast;
            }
        }
        return rx_no_match;
    }

    trace_sungem_rx_mac_compare(s->macregs[MAC_ADDR0 >> 2],
                                s->macregs[MAC_ADDR1 >> 2],
                                s->macregs[MAC_ADDR2 >> 2]);

    if (mac0 == s->macregs[MAC_ADDR0 >> 2] &&
        mac1 == s->macregs[MAC_ADDR1 >> 2] &&
        mac2 == s->macregs[MAC_ADDR2 >> 2]) {
        return rx_match_mac;
    }

    if (mac0 == s->macregs[MAC_ADDR3 >> 2] &&
        mac1 == s->macregs[MAC_ADDR4 >> 2] &&
        mac2 == s->macregs[MAC_ADDR5 >> 2]) {
        return rx_match_altmac;
    }

    return rx_no_match;
}

static ssize_t sungem_receive(NetClientState *nc, const uint8_t *buf,
                              size_t size)
{
    SunGEMState *s = qemu_get_nic_opaque(nc);
    PCIDevice *d = PCI_DEVICE(s);
    uint32_t mac_crc, done, kick, max_fsize;
    uint32_t fcs_size, ints, rxdma_cfg, rxmac_cfg, csum, coff;
    struct gem_rxd desc;
    uint64_t dbase, baddr;
    unsigned int rx_cond;

    trace_sungem_rx_packet(size);

    rxmac_cfg = s->macregs[MAC_RXCFG >> 2];
    rxdma_cfg = s->rxdmaregs[RXDMA_CFG >> 2];
    max_fsize = s->macregs[MAC_MAXFSZ >> 2] & 0x7fff;

    if (!(rxdma_cfg & RXDMA_CFG_ENABLE) ||
        !(rxmac_cfg & MAC_RXCFG_ENAB)) {
        trace_sungem_rx_disabled();
        return 0;
    }

    if (rxmac_cfg & MAC_RXCFG_SFCS) {
        fcs_size = 0;
    } else {
        fcs_size = 4;
    }

    if (size < 6 || (size + 4) > max_fsize) {
        trace_sungem_rx_bad_frame_size(size);
        return size;
    }

    mac_crc = net_crc32_le(buf, ETH_ALEN);

    rx_cond = sungem_check_rx_mac(s, buf, mac_crc);
    if (rx_cond == rx_no_match) {
        trace_sungem_rx_unmatched();
        return size;
    }

    kick = s->rxdmaregs[RXDMA_KICK >> 2] & s->rx_mask;
    done = s->rxdmaregs[RXDMA_DONE >> 2] & s->rx_mask;

    trace_sungem_rx_process(done, kick, s->rx_mask + 1);

    if (sungem_rx_full(s, kick, done)) {
        trace_sungem_rx_ringfull();
        return 0;
    }

    dbase = s->rxdmaregs[RXDMA_DBHI >> 2];
    dbase = (dbase << 32) | s->rxdmaregs[RXDMA_DBLOW >> 2];

    pci_dma_read(d, dbase + done * sizeof(desc), &desc, sizeof(desc));

    trace_sungem_rx_desc(le64_to_cpu(desc.status_word),
                         le64_to_cpu(desc.buffer));

    baddr = le64_to_cpu(desc.buffer) & ~7ull;
    baddr |= (rxdma_cfg & RXDMA_CFG_FBOFF) >> 10;

    pci_dma_write(d, baddr, buf, size);

    if (fcs_size) {
    }

    coff = (rxdma_cfg & RXDMA_CFG_CSUMOFF) >> 13;
    csum = net_raw_checksum((uint8_t *)buf + coff, size - coff);

    desc.status_word = (size + fcs_size) << 16;
    desc.status_word |= ((uint64_t)(mac_crc >> 16)) << 44;
    desc.status_word |= csum;
    if (rx_cond == rx_match_mcast) {
        desc.status_word |= RXDCTRL_HPASS;
    }
    if (rx_cond == rx_match_altmac) {
        desc.status_word |= RXDCTRL_ALTMAC;
    }
    desc.status_word = cpu_to_le64(desc.status_word);

    pci_dma_write(d, dbase + done * sizeof(desc), &desc, sizeof(desc));

    done = (done + 1) & s->rx_mask;
    s->rxdmaregs[RXDMA_DONE >> 2] = done;

    ints = GREG_STAT_RXDONE;
    if (sungem_rx_full(s, kick, done)) {
        ints |= GREG_STAT_RXNOBUF;
    }
    sungem_update_status(s, ints, true);

    return size;
}

static void sungem_set_link_status(NetClientState *nc)
{
}

static void sungem_update_masks(SunGEMState *s)
{
    uint32_t sz;

    sz = 1 << (((s->rxdmaregs[RXDMA_CFG >> 2] & RXDMA_CFG_RINGSZ) >> 1) + 5);
    s->rx_mask = sz - 1;

    sz = 1 << (((s->txdmaregs[TXDMA_CFG >> 2] & TXDMA_CFG_RINGSZ) >> 1) + 5);
    s->tx_mask = sz - 1;
}

static void sungem_reset_rx(SunGEMState *s)
{
    trace_sungem_rx_reset();

    s->rxdmaregs[RXDMA_FSZ >> 2] = 0x140;
    s->rxdmaregs[RXDMA_DONE >> 2] = 0;
    s->rxdmaregs[RXDMA_KICK >> 2] = 0;
    s->rxdmaregs[RXDMA_CFG >> 2] = 0x1000010;
    s->rxdmaregs[RXDMA_PTHRESH >> 2] = 0xf8;
    s->rxdmaregs[RXDMA_BLANK >> 2] = 0;

    sungem_update_masks(s);
}

static void sungem_reset_tx(SunGEMState *s)
{
    trace_sungem_tx_reset();

    s->txdmaregs[TXDMA_FSZ >> 2] = 0x90;
    s->txdmaregs[TXDMA_TXDONE >> 2] = 0;
    s->txdmaregs[TXDMA_KICK >> 2] = 0;
    s->txdmaregs[TXDMA_CFG >> 2] = 0x118010;

    sungem_update_masks(s);

    s->tx_size = 0;
    s->tx_first_ctl = 0;
}

static void sungem_reset_all(SunGEMState *s, bool pci_reset)
{
    trace_sungem_reset(pci_reset);

    sungem_reset_rx(s);
    sungem_reset_tx(s);

    s->gregs[GREG_IMASK >> 2] = 0xFFFFFFF;
    s->gregs[GREG_STAT >> 2] = 0;
    if (pci_reset) {
        uint8_t *ma = s->conf.macaddr.a;

        s->gregs[GREG_SWRST >> 2] = 0;
        s->macregs[MAC_ADDR0 >> 2] = (ma[4] << 8) | ma[5];
        s->macregs[MAC_ADDR1 >> 2] = (ma[2] << 8) | ma[3];
        s->macregs[MAC_ADDR2 >> 2] = (ma[0] << 8) | ma[1];
    } else {
        s->gregs[GREG_SWRST >> 2] &= GREG_SWRST_RSTOUT;
    }
    s->mifregs[MIF_CFG >> 2] = MIF_CFG_MDI0;
}

static void sungem_mii_write(SunGEMState *s, uint8_t phy_addr,
                             uint8_t reg_addr, uint16_t val)
{
    trace_sungem_mii_write(phy_addr, reg_addr, val);

}

static uint16_t __sungem_mii_read(SunGEMState *s, uint8_t phy_addr,
                                  uint8_t reg_addr)
{
    if (phy_addr != s->phy_addr) {
        return 0xffff;
    }
    switch (reg_addr) {
    case MII_BMCR:
        return 0;
    case MII_PHYID1:
        return 0x0040;
    case MII_PHYID2:
        return 0x6210;
    case MII_BMSR:
        if (qemu_get_queue(s->nic)->link_down) {
            return MII_BMSR_100TX_FD  | MII_BMSR_AUTONEG;
        } else {
            return MII_BMSR_100TX_FD | MII_BMSR_AN_COMP |
                    MII_BMSR_AUTONEG | MII_BMSR_LINK_ST;
        }
    case MII_ANLPAR:
    case MII_ANAR:
        return MII_ANLPAR_TXFD;
    case 0x18:
        return 3;
    default:
        return 0;
    };
}
static uint16_t sungem_mii_read(SunGEMState *s, uint8_t phy_addr,
                                uint8_t reg_addr)
{
    uint16_t val;

    val = __sungem_mii_read(s, phy_addr, reg_addr);

    trace_sungem_mii_read(phy_addr, reg_addr, val);

    return val;
}

static uint32_t sungem_mii_op(SunGEMState *s, uint32_t val)
{
    uint8_t phy_addr, reg_addr, op;

    if ((val >> 30) != 1) {
        trace_sungem_mii_invalid_sof(val >> 30);
        return 0xffff;
    }
    phy_addr = (val & MIF_FRAME_PHYAD) >> 23;
    reg_addr = (val & MIF_FRAME_REGAD) >> 18;
    op = (val & MIF_FRAME_OP) >> 28;
    switch (op) {
    case 1:
        sungem_mii_write(s, phy_addr, reg_addr, val & MIF_FRAME_DATA);
        return val | MIF_FRAME_TALSB;
    case 2:
        return sungem_mii_read(s, phy_addr, reg_addr) | MIF_FRAME_TALSB;
    default:
        trace_sungem_mii_invalid_op(op);
    }
    return 0xffff | MIF_FRAME_TALSB;
}

static void sungem_mmio_greg_write(void *opaque, hwaddr addr, uint64_t val,
                                   unsigned size)
{
    SunGEMState *s = opaque;

    if (!(addr < 0x20) && !(addr >= 0x1000 && addr <= 0x1010)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Write to unknown GREG register 0x%"HWADDR_PRIx"\n",
                      addr);
        return;
    }

    trace_sungem_mmio_greg_write(addr, val);

    switch (addr) {
    case GREG_SEBSTATE:
    case GREG_STAT:
    case GREG_STAT2:
    case GREG_PCIESTAT:
        return;
    case GREG_IACK:
        val &= GREG_STAT_LATCH;
        s->gregs[GREG_STAT >> 2] &= ~val;
        sungem_eval_irq(s);
        return;
    case GREG_PCIEMASK:
        val &= 0x7;
        break;
    }

    s->gregs[addr  >> 2] = val;

    switch (addr) {
    case GREG_IMASK:
        sungem_eval_irq(s);
        break;
    case GREG_SWRST:
        switch (val & (GREG_SWRST_TXRST | GREG_SWRST_RXRST)) {
        case GREG_SWRST_RXRST:
            sungem_reset_rx(s);
            break;
        case GREG_SWRST_TXRST:
            sungem_reset_tx(s);
            break;
        case GREG_SWRST_RXRST | GREG_SWRST_TXRST:
            sungem_reset_all(s, false);
        }
        break;
    }
}

static uint64_t sungem_mmio_greg_read(void *opaque, hwaddr addr, unsigned size)
{
    SunGEMState *s = opaque;
    uint32_t val;

    if (!(addr < 0x20) && !(addr >= 0x1000 && addr <= 0x1010)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Read from unknown GREG register 0x%"HWADDR_PRIx"\n",
                      addr);
        return 0;
    }

    val = s->gregs[addr >> 2];

    trace_sungem_mmio_greg_read(addr, val);

    switch (addr) {
    case GREG_STAT:
        s->gregs[GREG_STAT >> 2] &= ~GREG_STAT_LATCH;
        sungem_eval_irq(s);

        val = (val & ~GREG_STAT_TXNR) |
                (s->txdmaregs[TXDMA_TXDONE >> 2] << GREG_STAT_TXNR_SHIFT);
        break;
    case GREG_STAT2:
        val = (s->gregs[GREG_STAT >> 2] & ~GREG_STAT_TXNR) |
              (s->txdmaregs[TXDMA_TXDONE >> 2] << GREG_STAT_TXNR_SHIFT);
        break;
    }

    return val;
}

static const MemoryRegionOps sungem_mmio_greg_ops = {
    .read = sungem_mmio_greg_read,
    .write = sungem_mmio_greg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void sungem_mmio_txdma_write(void *opaque, hwaddr addr, uint64_t val,
                                    unsigned size)
{
    SunGEMState *s = opaque;

    if (!(addr < 0x38) && !(addr >= 0x100 && addr <= 0x118)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Write to unknown TXDMA register 0x%"HWADDR_PRIx"\n",
                      addr);
        return;
    }

    trace_sungem_mmio_txdma_write(addr, val);

    switch (addr) {
    case TXDMA_TXDONE:
    case TXDMA_PCNT:
    case TXDMA_SMACHINE:
    case TXDMA_DPLOW:
    case TXDMA_DPHI:
    case TXDMA_FSZ:
    case TXDMA_FTAG:
        return;
    }

    s->txdmaregs[addr >> 2] = val;

    switch (addr) {
    case TXDMA_KICK:
        sungem_tx_kick(s);
        break;
    case TXDMA_CFG:
        sungem_update_masks(s);
        break;
    }
}

static uint64_t sungem_mmio_txdma_read(void *opaque, hwaddr addr, unsigned size)
{
    SunGEMState *s = opaque;
    uint32_t val;

    if (!(addr < 0x38) && !(addr >= 0x100 && addr <= 0x118)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Read from unknown TXDMA register 0x%"HWADDR_PRIx"\n",
                      addr);
        return 0;
    }

    val = s->txdmaregs[addr >> 2];

    trace_sungem_mmio_txdma_read(addr, val);

    return val;
}

static const MemoryRegionOps sungem_mmio_txdma_ops = {
    .read = sungem_mmio_txdma_read,
    .write = sungem_mmio_txdma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void sungem_mmio_rxdma_write(void *opaque, hwaddr addr, uint64_t val,
                                    unsigned size)
{
    SunGEMState *s = opaque;

    if (!(addr <= 0x28) && !(addr >= 0x100 && addr <= 0x120)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Write to unknown RXDMA register 0x%"HWADDR_PRIx"\n",
                      addr);
        return;
    }

    trace_sungem_mmio_rxdma_write(addr, val);

    switch (addr) {
    case RXDMA_DONE:
    case RXDMA_PCNT:
    case RXDMA_SMACHINE:
    case RXDMA_DPLOW:
    case RXDMA_DPHI:
    case RXDMA_FSZ:
    case RXDMA_FTAG:
        return;
    }

    s->rxdmaregs[addr >> 2] = val;

    switch (addr) {
    case RXDMA_KICK:
        trace_sungem_rx_kick(val);
        break;
    case RXDMA_CFG:
        sungem_update_masks(s);
        if ((s->macregs[MAC_RXCFG >> 2] & MAC_RXCFG_ENAB) != 0 &&
            (s->rxdmaregs[RXDMA_CFG >> 2] & RXDMA_CFG_ENABLE) != 0) {
            qemu_flush_queued_packets(qemu_get_queue(s->nic));
        }
        break;
    }
}

static uint64_t sungem_mmio_rxdma_read(void *opaque, hwaddr addr, unsigned size)
{
    SunGEMState *s = opaque;
    uint32_t val;

    if (!(addr <= 0x28) && !(addr >= 0x100 && addr <= 0x120)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Read from unknown RXDMA register 0x%"HWADDR_PRIx"\n",
                      addr);
        return 0;
    }

    val = s->rxdmaregs[addr >> 2];

    trace_sungem_mmio_rxdma_read(addr, val);

    return val;
}

static const MemoryRegionOps sungem_mmio_rxdma_ops = {
    .read = sungem_mmio_rxdma_read,
    .write = sungem_mmio_rxdma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void sungem_mmio_wol_write(void *opaque, hwaddr addr, uint64_t val,
                                    unsigned size)
{
    trace_sungem_mmio_wol_write(addr, val);

    switch (addr) {
    case WOL_WAKECSR:
        if (val != 0) {
            qemu_log_mask(LOG_UNIMP, "sungem: WOL not supported\n");
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "sungem: WOL not supported\n");
    }
}

static uint64_t sungem_mmio_wol_read(void *opaque, hwaddr addr, unsigned size)
{
    uint32_t val = -1;

    qemu_log_mask(LOG_UNIMP, "sungem: WOL not supported\n");

    trace_sungem_mmio_wol_read(addr, val);

    return val;
}

static const MemoryRegionOps sungem_mmio_wol_ops = {
    .read = sungem_mmio_wol_read,
    .write = sungem_mmio_wol_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void sungem_mmio_mac_write(void *opaque, hwaddr addr, uint64_t val,
                                  unsigned size)
{
    SunGEMState *s = opaque;

    if (!(addr <= 0x134)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Write to unknown MAC register 0x%"HWADDR_PRIx"\n",
                      addr);
        return;
    }

    trace_sungem_mmio_mac_write(addr, val);

    switch (addr) {
    case MAC_TXRST:
    case MAC_RXRST:
    case MAC_TXSTAT:
    case MAC_RXSTAT:
    case MAC_CSTAT:
    case MAC_PATMPS:
    case MAC_SMACHINE:
        return;
    case MAC_MINFSZ:
        val &= 0x3ff;
        break;
    }

    s->macregs[addr >> 2] = val;

    switch (addr) {
    case MAC_TXMASK:
    case MAC_RXMASK:
    case MAC_MCMASK:
        sungem_eval_cascade_irq(s);
        break;
    case MAC_RXCFG:
        sungem_update_masks(s);
        if ((s->macregs[MAC_RXCFG >> 2] & MAC_RXCFG_ENAB) != 0 &&
            (s->rxdmaregs[RXDMA_CFG >> 2] & RXDMA_CFG_ENABLE) != 0) {
            qemu_flush_queued_packets(qemu_get_queue(s->nic));
        }
        break;
    }
}

static uint64_t sungem_mmio_mac_read(void *opaque, hwaddr addr, unsigned size)
{
    SunGEMState *s = opaque;
    uint32_t val;

    if (!(addr <= 0x134)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Read from unknown MAC register 0x%"HWADDR_PRIx"\n",
                      addr);
        return 0;
    }

    val = s->macregs[addr >> 2];

    trace_sungem_mmio_mac_read(addr, val);

    switch (addr) {
    case MAC_TXSTAT:
        s->macregs[addr >> 2] = 0;
        sungem_update_status(s, GREG_STAT_TXMAC, false);
        break;
    case MAC_RXSTAT:
        s->macregs[addr >> 2] = 0;
        sungem_update_status(s, GREG_STAT_RXMAC, false);
        break;
    case MAC_CSTAT:
        s->macregs[addr >> 2] &= MAC_CSTAT_PTR;
        sungem_update_status(s, GREG_STAT_MAC, false);
        break;
    }

    return val;
}

static const MemoryRegionOps sungem_mmio_mac_ops = {
    .read = sungem_mmio_mac_read,
    .write = sungem_mmio_mac_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void sungem_mmio_mif_write(void *opaque, hwaddr addr, uint64_t val,
                                  unsigned size)
{
    SunGEMState *s = opaque;

    if (!(addr <= 0x1c)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Write to unknown MIF register 0x%"HWADDR_PRIx"\n",
                      addr);
        return;
    }

    trace_sungem_mmio_mif_write(addr, val);

    switch (addr) {
    case MIF_STATUS:
    case MIF_SMACHINE:
        return;
    case MIF_CFG:
        val &= ~MIF_CFG_MDI1;
        val |= MIF_CFG_MDI0;
        break;
    }

    s->mifregs[addr >> 2] = val;

    switch (addr) {
    case MIF_FRAME:
        s->mifregs[addr >> 2] = sungem_mii_op(s, val);
        break;
    }
}

static uint64_t sungem_mmio_mif_read(void *opaque, hwaddr addr, unsigned size)
{
    SunGEMState *s = opaque;
    uint32_t val;

    if (!(addr <= 0x1c)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Read from unknown MIF register 0x%"HWADDR_PRIx"\n",
                      addr);
        return 0;
    }

    val = s->mifregs[addr >> 2];

    trace_sungem_mmio_mif_read(addr, val);

    return val;
}

static const MemoryRegionOps sungem_mmio_mif_ops = {
    .read = sungem_mmio_mif_read,
    .write = sungem_mmio_mif_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void sungem_mmio_pcs_write(void *opaque, hwaddr addr, uint64_t val,
                                  unsigned size)
{
    SunGEMState *s = opaque;

    if (!(addr <= 0x18) && !(addr >= 0x50 && addr <= 0x5c)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Write to unknown PCS register 0x%"HWADDR_PRIx"\n",
                      addr);
        return;
    }

    trace_sungem_mmio_pcs_write(addr, val);

    switch (addr) {
    case PCS_MIISTAT:
    case PCS_ISTAT:
    case PCS_SSTATE:
        return;
    }

    s->pcsregs[addr >> 2] = val;
}

static uint64_t sungem_mmio_pcs_read(void *opaque, hwaddr addr, unsigned size)
{
    SunGEMState *s = opaque;
    uint32_t val;

    if (!(addr <= 0x18) && !(addr >= 0x50 && addr <= 0x5c)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Read from unknown PCS register 0x%"HWADDR_PRIx"\n",
                      addr);
        return 0;
    }

    val = s->pcsregs[addr >> 2];

    trace_sungem_mmio_pcs_read(addr, val);

    return val;
}

static const MemoryRegionOps sungem_mmio_pcs_ops = {
    .read = sungem_mmio_pcs_read,
    .write = sungem_mmio_pcs_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void sungem_uninit(PCIDevice *dev)
{
    SunGEMState *s = SUNGEM(dev);

    qemu_del_nic(s->nic);
}

static NetClientInfo net_sungem_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = sungem_can_receive,
    .receive = sungem_receive,
    .link_status_changed = sungem_set_link_status,
};

static void sungem_realize(PCIDevice *pci_dev, Error **errp)
{
    DeviceState *dev = DEVICE(pci_dev);
    SunGEMState *s = SUNGEM(pci_dev);
    uint8_t *pci_conf;

    pci_conf = pci_dev->config;

    pci_set_word(pci_conf + PCI_STATUS,
                 PCI_STATUS_FAST_BACK |
                 PCI_STATUS_DEVSEL_MEDIUM |
                 PCI_STATUS_66MHZ);

    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, 0x0);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0x0);

    pci_conf[PCI_INTERRUPT_PIN] = 1;
    pci_conf[PCI_MIN_GNT] = 0x40;
    pci_conf[PCI_MAX_LAT] = 0x40;

    sungem_reset_all(s, true);
    memory_region_init(&s->sungem, OBJECT(s), "sungem", SUNGEM_MMIO_SIZE);

    memory_region_init_io(&s->greg, OBJECT(s), &sungem_mmio_greg_ops, s,
                          "sungem.greg", SUNGEM_MMIO_GREG_SIZE);
    memory_region_add_subregion(&s->sungem, 0, &s->greg);

    memory_region_init_io(&s->txdma, OBJECT(s), &sungem_mmio_txdma_ops, s,
                          "sungem.txdma", SUNGEM_MMIO_TXDMA_SIZE);
    memory_region_add_subregion(&s->sungem, 0x2000, &s->txdma);

    memory_region_init_io(&s->rxdma, OBJECT(s), &sungem_mmio_rxdma_ops, s,
                          "sungem.rxdma", SUNGEM_MMIO_RXDMA_SIZE);
    memory_region_add_subregion(&s->sungem, 0x4000, &s->rxdma);

    memory_region_init_io(&s->wol, OBJECT(s), &sungem_mmio_wol_ops, s,
                          "sungem.wol", SUNGEM_MMIO_WOL_SIZE);
    memory_region_add_subregion(&s->sungem, 0x3000, &s->wol);

    memory_region_init_io(&s->mac, OBJECT(s), &sungem_mmio_mac_ops, s,
                          "sungem.mac", SUNGEM_MMIO_MAC_SIZE);
    memory_region_add_subregion(&s->sungem, 0x6000, &s->mac);

    memory_region_init_io(&s->mif, OBJECT(s), &sungem_mmio_mif_ops, s,
                          "sungem.mif", SUNGEM_MMIO_MIF_SIZE);
    memory_region_add_subregion(&s->sungem, 0x6200, &s->mif);

    memory_region_init_io(&s->pcs, OBJECT(s), &sungem_mmio_pcs_ops, s,
                          "sungem.pcs", SUNGEM_MMIO_PCS_SIZE);
    memory_region_add_subregion(&s->sungem, 0x9000, &s->pcs);

    pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->sungem);

    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&net_sungem_info, &s->conf,
                          object_get_typename(OBJECT(dev)),
                          dev->id, &dev->mem_reentrancy_guard, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic),
                             s->conf.macaddr.a);
}

static void sungem_reset(DeviceState *dev)
{
    SunGEMState *s = SUNGEM(dev);

    sungem_reset_all(s, true);
}

static void sungem_instance_init(Object *obj)
{
    SunGEMState *s = SUNGEM(obj);

    device_add_bootindex_property(obj, &s->conf.bootindex,
                                  "bootindex", "/ethernet-phy@0",
                                  DEVICE(obj));
}

static const Property sungem_properties[] = {
    DEFINE_NIC_PROPERTIES(SunGEMState, conf),
    DEFINE_PROP_UINT32("phy_addr", SunGEMState, phy_addr, 0),
};

static const VMStateDescription vmstate_sungem = {
    .name = "sungem",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(pdev, SunGEMState),
        VMSTATE_MACADDR(conf.macaddr, SunGEMState),
        VMSTATE_UINT32(phy_addr, SunGEMState),
        VMSTATE_UINT32_ARRAY(gregs, SunGEMState, (SUNGEM_MMIO_GREG_SIZE >> 2)),
        VMSTATE_UINT32_ARRAY(txdmaregs, SunGEMState,
                             (SUNGEM_MMIO_TXDMA_SIZE >> 2)),
        VMSTATE_UINT32_ARRAY(rxdmaregs, SunGEMState,
                             (SUNGEM_MMIO_RXDMA_SIZE >> 2)),
        VMSTATE_UINT32_ARRAY(macregs, SunGEMState, (SUNGEM_MMIO_MAC_SIZE >> 2)),
        VMSTATE_UINT32_ARRAY(mifregs, SunGEMState, (SUNGEM_MMIO_MIF_SIZE >> 2)),
        VMSTATE_UINT32_ARRAY(pcsregs, SunGEMState, (SUNGEM_MMIO_PCS_SIZE >> 2)),
        VMSTATE_UINT32(rx_mask, SunGEMState),
        VMSTATE_UINT32(tx_mask, SunGEMState),
        VMSTATE_UINT8_ARRAY(tx_data, SunGEMState, MAX_PACKET_SIZE),
        VMSTATE_UINT32(tx_size, SunGEMState),
        VMSTATE_UINT64(tx_first_ctl, SunGEMState),
        VMSTATE_END_OF_LIST()
    }
};

static void sungem_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = sungem_realize;
    k->exit = sungem_uninit;
    k->vendor_id = PCI_VENDOR_ID_APPLE;
    k->device_id = PCI_DEVICE_ID_APPLE_UNI_N_GMAC;
    k->revision = 0x01;
    k->class_id = PCI_CLASS_NETWORK_ETHERNET;
    dc->vmsd = &vmstate_sungem;
    device_class_set_legacy_reset(dc, sungem_reset);
    device_class_set_props(dc, sungem_properties);
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo sungem_info = {
    .name          = TYPE_SUNGEM,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(SunGEMState),
    .class_init    = sungem_class_init,
    .instance_init = sungem_instance_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { }
    }
};

static void sungem_register_types(void)
{
    type_register_static(&sungem_info);
}

type_init(sungem_register_types)
