/*
 * AHCI test cases
 *
 * Copyright (c) 2014 John Snow <jsnow@redhat.com>
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
#include <getopt.h>

#include "libqtest.h"
#include "libqos/libqos-pc.h"
#include "libqos/ahci.h"
#include "libqos/pci-pc.h"

#include "qobject/qdict.h"
#include "qemu/host-utils.h"

#include "hw/pci/pci_ids.h"
#include "hw/pci/pci_regs.h"

#define TEST_IMAGE_SIZE_MB_LARGE (200 * 1024)
#define TEST_IMAGE_SIZE_MB_SMALL 64

static char *tmp_path;
static char *debug_path;
static char *mig_socket;
static bool ahci_pedantic;
static const char *imgfmt;
static unsigned test_image_size_mb;

static void ahci_test_port_spec(AHCIQState *ahci, uint8_t port);
static void ahci_test_pci_spec(AHCIQState *ahci);
static void ahci_test_pci_caps(AHCIQState *ahci, uint16_t header,
                               uint8_t offset);
static void ahci_test_satacap(AHCIQState *ahci, uint8_t offset);
static void ahci_test_msicap(AHCIQState *ahci, uint8_t offset);
static void ahci_test_pmcap(AHCIQState *ahci, uint8_t offset);

static uint64_t mb_to_sectors(uint64_t image_size_mb)
{
    return (image_size_mb * 1024 * 1024) / AHCI_SECTOR_SIZE;
}

static void string_bswap16(uint16_t *s, size_t bytes)
{
    g_assert_cmphex((bytes & 1), ==, 0);
    bytes /= 2;

    while (bytes--) {
        *s = bswap16(*s);
        s++;
    }
}

static void verify_state(AHCIQState *ahci, uint64_t hba_old)
{
    int i, j;
    uint32_t ahci_fingerprint;
    uint64_t hba_base;
    AHCICommandHeader cmd;

    ahci_fingerprint = qpci_config_readl(ahci->dev, PCI_VENDOR_ID);
    g_assert_cmphex(ahci_fingerprint, ==, ahci->fingerprint);

    if (!ahci->enabled) {
        return;
    }

    hba_base = (uint64_t)qpci_config_readl(ahci->dev, PCI_BASE_ADDRESS_5);
    g_assert_cmphex(hba_base, ==, hba_old);

    g_assert_cmphex(ahci_rreg(ahci, AHCI_CAP), ==, ahci->cap);
    g_assert_cmphex(ahci_rreg(ahci, AHCI_CAP2), ==, ahci->cap2);

    for (i = 0; i < 32; i++) {
        g_assert_cmphex(ahci_px_rreg(ahci, i, AHCI_PX_FB), ==,
                        ahci->port[i].fb);
        g_assert_cmphex(ahci_px_rreg(ahci, i, AHCI_PX_CLB), ==,
                        ahci->port[i].clb);
        for (j = 0; j < 32; j++) {
            ahci_get_command_header(ahci, i, j, &cmd);
            g_assert_cmphex(cmd.prdtl, ==, ahci->port[i].prdtl[j]);
            g_assert_cmphex(cmd.ctba, ==, ahci->port[i].ctba[j]);
        }
    }
}

static void ahci_migrate(AHCIQState *from, AHCIQState *to, const char *uri)
{
    QOSState *tmp = to->parent;
    QPCIDevice *dev = to->dev;
    char *uri_local = NULL;
    uint64_t hba_old;

    if (uri == NULL) {
        uri_local = g_strdup_printf("%s%s", "unix:", mig_socket);
        uri = uri_local;
    }

    hba_old = (uint64_t)qpci_config_readl(from->dev, PCI_BASE_ADDRESS_5);

    migrate(from->parent, to->parent, uri);

    memcpy(to, from, sizeof(AHCIQState));
    to->parent = tmp;
    to->dev = dev;

    tmp = from->parent;
    dev = from->dev;
    memset(from, 0x00, sizeof(AHCIQState));
    from->parent = tmp;
    from->dev = dev;

    verify_state(to, hba_old);
    g_free(uri_local);
}

G_GNUC_PRINTF(1, 0)
static AHCIQState *ahci_vboot(const char *cli, va_list ap)
{
    AHCIQState *s;

    s = g_new0(AHCIQState, 1);
    s->parent = qtest_pc_vboot(cli, ap);
    alloc_set_flags(&s->parent->alloc, ALLOC_LEAK_ASSERT);

    s->dev = get_ahci_device(s->parent->qts, &s->fingerprint);

    return s;
}

G_GNUC_PRINTF(1, 2)
static AHCIQState *ahci_boot(const char *cli, ...)
{
    AHCIQState *s;
    va_list ap;

    if (cli) {
        va_start(ap, cli);
        s = ahci_vboot(cli, ap);
        va_end(ap);
    } else {
        cli = "-drive if=none,id=drive0,file=%s,cache=writeback,format=%s"
            " -M q35 "
            "-device ide-hd,drive=drive0 "
            "-global ide-hd.serial=%s "
            "-global ide-hd.ver=%s";
        s = ahci_boot(cli, tmp_path, imgfmt, "testdisk", "version");
    }

    return s;
}

static void ahci_shutdown(AHCIQState *ahci)
{
    QOSState *qs = ahci->parent;

    ahci_clean_mem(ahci);
    free_ahci_device(ahci->dev);
    g_free(ahci);
    qtest_shutdown(qs);
}

G_GNUC_PRINTF(1, 2)
static AHCIQState *ahci_boot_and_enable(const char *cli, ...)
{
    AHCIQState *ahci;
    va_list ap;
    uint16_t buff[256];
    uint8_t port;
    uint8_t hello;

    if (cli) {
        va_start(ap, cli);
        ahci = ahci_vboot(cli, ap);
        va_end(ap);
    } else {
        ahci = ahci_boot(NULL);
    }

    ahci_pci_enable(ahci);
    ahci_hba_enable(ahci);
    port = ahci_port_select(ahci);
    ahci_port_clear(ahci, port);
    if (is_atapi(ahci, port)) {
        hello = CMD_PACKET_ID;
    } else {
        hello = CMD_IDENTIFY;
    }
    ahci_io(ahci, port, hello, &buff, sizeof(buff), 0);

    return ahci;
}

static void ahci_test_pci_spec(AHCIQState *ahci)
{
    uint8_t datab;
    uint16_t data;
    uint32_t datal;

    data = qpci_config_readw(ahci->dev, PCI_COMMAND);
    ASSERT_BIT_CLEAR(data, PCI_COMMAND_MEMORY);
    ASSERT_BIT_CLEAR(data, PCI_COMMAND_MASTER);
    ASSERT_BIT_CLEAR(data, PCI_COMMAND_SPECIAL);
    ASSERT_BIT_CLEAR(data, PCI_COMMAND_VGA_PALETTE);
    ASSERT_BIT_CLEAR(data, PCI_COMMAND_PARITY);
    ASSERT_BIT_CLEAR(data, PCI_COMMAND_WAIT);
    ASSERT_BIT_CLEAR(data, PCI_COMMAND_SERR);
    ASSERT_BIT_CLEAR(data, PCI_COMMAND_FAST_BACK);
    ASSERT_BIT_CLEAR(data, PCI_COMMAND_INTX_DISABLE);
    ASSERT_BIT_CLEAR(data, 0xF800);

    data = qpci_config_readw(ahci->dev, PCI_STATUS);
    ASSERT_BIT_CLEAR(data, 0x01 | 0x02 | 0x04);
    ASSERT_BIT_CLEAR(data, PCI_STATUS_INTERRUPT);
    ASSERT_BIT_SET(data, PCI_STATUS_CAP_LIST);
    ASSERT_BIT_CLEAR(data, PCI_STATUS_UDF);
    ASSERT_BIT_CLEAR(data, PCI_STATUS_PARITY);
    ASSERT_BIT_CLEAR(data, PCI_STATUS_SIG_TARGET_ABORT);
    ASSERT_BIT_CLEAR(data, PCI_STATUS_REC_TARGET_ABORT);
    ASSERT_BIT_CLEAR(data, PCI_STATUS_REC_MASTER_ABORT);
    ASSERT_BIT_CLEAR(data, PCI_STATUS_SIG_SYSTEM_ERROR);
    ASSERT_BIT_CLEAR(data, PCI_STATUS_DETECTED_PARITY);

    datal = qpci_config_readl(ahci->dev, PCI_CLASS_REVISION);
    if (ahci_pedantic) {
        ASSERT_BIT_CLEAR(datal, 0xFF);
    }

    g_assert_cmphex(PCI_BCC(datal), ==, 0x01);
    if (PCI_SCC(datal) == 0x01) {
        ASSERT_BIT_SET(0x80000000, datal);
        ASSERT_BIT_CLEAR(0x60000000, datal);
    } else if (PCI_SCC(datal) == 0x04) {
        g_assert_cmphex(PCI_PI(datal), ==, 0);
    } else if (PCI_SCC(datal) == 0x06) {
        g_assert_cmphex(PCI_PI(datal), ==, 0x01);
    } else {
        g_assert_not_reached();
    }

    datab = qpci_config_readb(ahci->dev, PCI_CACHE_LINE_SIZE);
    g_assert_cmphex(datab, ==, 0);

    datab = qpci_config_readb(ahci->dev, PCI_LATENCY_TIMER);
    g_assert_cmphex(datab, ==, 0);

    datab = qpci_config_readb(ahci->dev, PCI_HEADER_TYPE);
    ASSERT_BIT_CLEAR(datab, 0x7F);

    datab = qpci_config_readb(ahci->dev, PCI_BIST);
    ASSERT_BIT_CLEAR(datab, 0x7F);

    datal = qpci_config_readl(ahci->dev, PCI_BASE_ADDRESS_5);
    g_assert_cmphex(datal, ==, 0);

    qpci_config_writel(ahci->dev, PCI_BASE_ADDRESS_5, 0xFFFFFFFF);
    datal = qpci_config_readl(ahci->dev, PCI_BASE_ADDRESS_5);
    ASSERT_BIT_CLEAR(datal, 0xFF);

    datal = qpci_config_readl(ahci->dev, PCI_CAPABILITY_LIST);
    ASSERT_BIT_CLEAR(datal, ~0xFF);
    g_assert_cmphex(datal, !=, 0);

    data = qpci_config_readw(ahci->dev, datal);

    switch (ahci->fingerprint) {
    case AHCI_INTEL_ICH9:
        g_assert_cmphex((data & 0xFF), ==, PCI_CAP_ID_MSI);
        break;
    default:
        g_assert_cmphex((data & 0xFF), ==, PCI_CAP_ID_PM);
    }

    ahci_test_pci_caps(ahci, data, (uint8_t)datal);

    datal = qpci_config_readl(ahci->dev, PCI_CAPABILITY_LIST + 4);
    g_assert_cmphex(datal, ==, 0);

    datab = qpci_config_readb(ahci->dev, PCI_INTERRUPT_LINE);
    g_assert_cmphex(datab, ==, 0);
}

static void ahci_test_pci_caps(AHCIQState *ahci, uint16_t header,
                               uint8_t offset)
{
    uint8_t cid = header & 0xFF;
    uint8_t next = header >> 8;

    g_test_message("CID: %02x; next: %02x", cid, next);

    switch (cid) {
    case PCI_CAP_ID_PM:
        ahci_test_pmcap(ahci, offset);
        break;
    case PCI_CAP_ID_MSI:
        ahci_test_msicap(ahci, offset);
        break;
    case PCI_CAP_ID_SATA:
        ahci_test_satacap(ahci, offset);
        break;

    default:
        g_test_message("Unknown CAP 0x%02x", cid);
    }

    if (next) {
        ahci_test_pci_caps(ahci, qpci_config_readw(ahci->dev, next), next);
    }
}

static void ahci_test_satacap(AHCIQState *ahci, uint8_t offset)
{
    uint16_t dataw;
    uint32_t datal;

    g_test_message("Verifying SATACAP");

    dataw = qpci_config_readw(ahci->dev, offset + 2);
    g_assert_cmphex(dataw, ==, 0x10);

    datal = qpci_config_readw(ahci->dev, offset + 4);

    switch (datal & 0x0F) {
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x07:
    case 0x08:
    case 0x09:
    case 0x0F:
        break;
    default:
        g_assert_not_reached();
    }

    g_assert_cmphex((datal >> 24), ==, 0x00);
}

static void ahci_test_msicap(AHCIQState *ahci, uint8_t offset)
{
    uint16_t dataw;
    uint32_t datal;

    g_test_message("Verifying MSICAP");

    dataw = qpci_config_readw(ahci->dev, offset + PCI_MSI_FLAGS);
    ASSERT_BIT_CLEAR(dataw, PCI_MSI_FLAGS_ENABLE);
    ASSERT_BIT_CLEAR(dataw, PCI_MSI_FLAGS_QSIZE);
    ASSERT_BIT_CLEAR(dataw, PCI_MSI_FLAGS_RESERVED);

    datal = qpci_config_readl(ahci->dev, offset + PCI_MSI_ADDRESS_LO);
    g_assert_cmphex(datal, ==, 0);

    if (dataw & PCI_MSI_FLAGS_64BIT) {
        g_test_message("MSICAP is 64bit");
        datal = qpci_config_readl(ahci->dev, offset + PCI_MSI_ADDRESS_HI);
        g_assert_cmphex(datal, ==, 0);
        dataw = qpci_config_readw(ahci->dev, offset + PCI_MSI_DATA_64);
        g_assert_cmphex(dataw, ==, 0);
    } else {
        g_test_message("MSICAP is 32bit");
        dataw = qpci_config_readw(ahci->dev, offset + PCI_MSI_DATA_32);
        g_assert_cmphex(dataw, ==, 0);
    }
}

static void ahci_test_pmcap(AHCIQState *ahci, uint8_t offset)
{
    uint16_t dataw;

    g_test_message("Verifying PMCAP");

    dataw = qpci_config_readw(ahci->dev, offset + PCI_PM_PMC);
    ASSERT_BIT_CLEAR(dataw, PCI_PM_CAP_PME_CLOCK);
    ASSERT_BIT_CLEAR(dataw, PCI_PM_CAP_RESERVED);
    ASSERT_BIT_CLEAR(dataw, PCI_PM_CAP_D1);
    ASSERT_BIT_CLEAR(dataw, PCI_PM_CAP_D2);

    dataw = qpci_config_readw(ahci->dev, offset + PCI_PM_CTRL);
    ASSERT_BIT_CLEAR(dataw, PCI_PM_CTRL_STATE_MASK);
    ASSERT_BIT_CLEAR(dataw, PCI_PM_CTRL_RESERVED);
    ASSERT_BIT_CLEAR(dataw, PCI_PM_CTRL_DATA_SEL_MASK);
    ASSERT_BIT_CLEAR(dataw, PCI_PM_CTRL_DATA_SCALE_MASK);
}

static void ahci_test_hba_spec(AHCIQState *ahci)
{
    unsigned i;
    uint32_t reg;
    uint32_t ports;
    uint8_t nports_impl;
    uint8_t maxports;

    g_assert(ahci != NULL);

    ahci->cap = ahci_rreg(ahci, AHCI_CAP);
    ASSERT_BIT_CLEAR(ahci->cap, AHCI_CAP_RESERVED);

    reg = ahci_rreg(ahci, AHCI_GHC);
    ASSERT_BIT_CLEAR(reg, AHCI_GHC_HR);
    ASSERT_BIT_CLEAR(reg, AHCI_GHC_IE);
    ASSERT_BIT_CLEAR(reg, AHCI_GHC_MRSM);
    if (BITSET(ahci->cap, AHCI_CAP_SAM)) {
        g_test_message("Supports AHCI-Only Mode: GHC_AE is Read-Only.");
        ASSERT_BIT_SET(reg, AHCI_GHC_AE);
    } else {
        g_test_message("Supports AHCI/Legacy mix.");
        ASSERT_BIT_CLEAR(reg, AHCI_GHC_AE);
    }

    reg = ahci_rreg(ahci, AHCI_IS);
    g_assert_cmphex(reg, ==, 0);

    ports = ahci_rreg(ahci, AHCI_PI);
    g_assert_cmphex(ports, !=, 0);
    nports_impl = ctpopl(ports);
    g_assert_cmpuint(((AHCI_CAP_NP & ahci->cap) + 1), >=, nports_impl);

    g_assert_cmphex(ahci->barsize, >, 0);
    maxports = (ahci->barsize - HBA_DATA_REGION_SIZE) / HBA_PORT_DATA_SIZE;
    g_assert_cmphex((reg >> maxports), ==, 0);

    reg = ahci_rreg(ahci, AHCI_VS);
    switch (reg) {
    case AHCI_VERSION_0_95:
    case AHCI_VERSION_1_0:
    case AHCI_VERSION_1_1:
    case AHCI_VERSION_1_2:
    case AHCI_VERSION_1_3:
        break;
    default:
        g_assert_not_reached();
    }

    reg = ahci_rreg(ahci, AHCI_CCCCTL);
    if (BITSET(ahci->cap, AHCI_CAP_CCCS)) {
        ASSERT_BIT_CLEAR(reg, AHCI_CCCCTL_EN);
        ASSERT_BIT_CLEAR(reg, AHCI_CCCCTL_RESERVED);
        ASSERT_BIT_SET(reg, AHCI_CCCCTL_CC);
        ASSERT_BIT_SET(reg, AHCI_CCCCTL_TV);
    } else {
        g_assert_cmphex(reg, ==, 0);
    }

    reg = ahci_rreg(ahci, AHCI_CCCPORTS);
    g_assert_cmphex(reg, ==, 0);

    reg = ahci_rreg(ahci, AHCI_EMLOC);
    if (BITCLR(ahci->cap, AHCI_CAP_EMS)) {
        g_assert_cmphex(reg, ==, 0);
    }

    reg = ahci_rreg(ahci, AHCI_EMCTL);
    if (BITSET(ahci->cap, AHCI_CAP_EMS)) {
        ASSERT_BIT_CLEAR(reg, AHCI_EMCTL_STSMR);
        ASSERT_BIT_CLEAR(reg, AHCI_EMCTL_CTLTM);
        ASSERT_BIT_CLEAR(reg, AHCI_EMCTL_CTLRST);
        ASSERT_BIT_CLEAR(reg, AHCI_EMCTL_RESERVED);
    } else {
        g_assert_cmphex(reg, ==, 0);
    }

    ahci->cap2 = ahci_rreg(ahci, AHCI_CAP2);
    ASSERT_BIT_CLEAR(ahci->cap2, AHCI_CAP2_RESERVED);

    reg = ahci_rreg(ahci, AHCI_BOHC);
    g_assert_cmphex(reg, ==, 0);

    g_test_message("Verifying HBA reserved area is empty.");
    for (i = AHCI_RESERVED; i < AHCI_NVMHCI; ++i) {
        reg = ahci_rreg(ahci, i);
        g_assert_cmphex(reg, ==, 0);
    }

    if (BITCLR(ahci->cap2, AHCI_CAP2_NVMP)) {
        g_test_message("Verifying HBA/NVMHCI area is empty.");
        for (i = AHCI_NVMHCI; i < AHCI_VENDOR; ++i) {
            reg = ahci_rreg(ahci, i);
            g_assert_cmphex(reg, ==, 0);
        }
    }

    g_test_message("Verifying HBA/Vendor area is empty.");
    for (i = AHCI_VENDOR; i < AHCI_PORTS; ++i) {
        reg = ahci_rreg(ahci, i);
        g_assert_cmphex(reg, ==, 0);
    }

    for (i = 0; ports || (i < maxports); ports >>= 1, ++i) {
        if (BITSET(ports, 0x1)) {
            g_test_message("Testing port %u for spec", i);
            ahci_test_port_spec(ahci, i);
        } else {
            uint16_t j;
            uint16_t low = AHCI_PORTS + (32 * i);
            uint16_t high = AHCI_PORTS + (32 * (i + 1));
            g_test_message("Asserting unimplemented port %u "
                           "(reg [%u-%u]) is empty.",
                           i, low, high - 1);
            for (j = low; j < high; ++j) {
                reg = ahci_rreg(ahci, j);
                g_assert_cmphex(reg, ==, 0);
            }
        }
    }
}

static void ahci_test_port_spec(AHCIQState *ahci, uint8_t port)
{
    uint32_t reg;
    unsigned i;

    reg = ahci_px_rreg(ahci, port, AHCI_PX_CLB);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_CLB_RESERVED);

    if (BITCLR(ahci->cap, AHCI_CAP_S64A)) {
        reg = ahci_px_rreg(ahci, port, AHCI_PX_CLBU);
        g_assert_cmphex(reg, ==, 0);
    }

    reg = ahci_px_rreg(ahci, port, AHCI_PX_FB);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_FB_RESERVED);

    if (BITCLR(ahci->cap, AHCI_CAP_S64A)) {
        reg = ahci_px_rreg(ahci, port, AHCI_PX_FBU);
        g_assert_cmphex(reg, ==, 0);
    }

    reg = ahci_px_rreg(ahci, port, AHCI_PX_IS);
    g_assert_cmphex(reg, ==, 0);

    reg = ahci_px_rreg(ahci, port, AHCI_PX_IE);
    g_assert_cmphex(reg, ==, 0);

    reg = ahci_px_rreg(ahci, port, AHCI_PX_CMD);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_FRE);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_RESERVED);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_CCS);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_FR);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_CR);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_PMA);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_APSTE);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_ATAPI);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_DLAE);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_ALPE);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_ASP);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_ICC);
    if (BITCLR(reg, AHCI_PX_CMD_CPD)) {
        ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_CPS);
    }
    if (BITCLR(reg, AHCI_PX_CMD_MPSP)) {
        ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_MPSS);
    }
    if (BITCLR(ahci->cap, AHCI_CAP_SMPS)) {
        ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_MPSS);
        ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_MPSP);
    }
    if (BITANY(reg, AHCI_PX_CMD_CPD | AHCI_PX_CMD_MPSP)) {
        ASSERT_BIT_SET(reg, AHCI_PX_CMD_HPCP);
    }
    g_assert(!BITSET(reg, AHCI_PX_CMD_HPCP | AHCI_PX_CMD_ESP));
    if (BITCLR(ahci->cap, AHCI_CAP_FBSS)) {
        ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_FBSCP);
    }

    reg = ahci_px_rreg(ahci, port, AHCI_PX_RES1);
    g_assert_cmphex(reg, ==, 0);

    reg = ahci_px_rreg(ahci, port, AHCI_PX_TFD);
    ASSERT_BIT_SET(reg, AHCI_PX_TFD_STS_ERR);
    ASSERT_BIT_SET(reg, AHCI_PX_TFD_STS_CS1);
    ASSERT_BIT_SET(reg, AHCI_PX_TFD_STS_DRQ);
    ASSERT_BIT_SET(reg, AHCI_PX_TFD_STS_CS2);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_TFD_STS_BSY);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_TFD_ERR);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_TFD_RESERVED);

    reg = ahci_px_rreg(ahci, port, AHCI_PX_SSTS);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_SSTS_RESERVED);

    reg = ahci_px_rreg(ahci, port, AHCI_PX_SCTL);
    g_assert_cmphex(reg, ==, 0);

    reg = ahci_px_rreg(ahci, port, AHCI_PX_SERR);
    g_assert_cmphex(reg, ==, 0);

    reg = ahci_px_rreg(ahci, port, AHCI_PX_SACT);
    g_assert_cmphex(reg, ==, 0);

    reg = ahci_px_rreg(ahci, port, AHCI_PX_CI);
    g_assert_cmphex(reg, ==, 0);

    reg = ahci_px_rreg(ahci, port, AHCI_PX_SNTF);
    g_assert_cmphex(reg, ==, 0);

    reg = ahci_px_rreg(ahci, port, AHCI_PX_FBS);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_FBS_EN);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_FBS_DEC);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_FBS_SDE);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_FBS_DEV);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_FBS_DWE);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_FBS_RESERVED);
    if (BITSET(ahci->cap, AHCI_CAP_FBSS)) {
        g_assert((reg & AHCI_PX_FBS_ADO) >> ctzl(AHCI_PX_FBS_ADO) >= 2);
    }

    for (i = AHCI_PX_RES2; i < AHCI_PX_VS; ++i) {
        reg = ahci_px_rreg(ahci, port, i);
        g_assert_cmphex(reg, ==, 0);
    }

    for (i = AHCI_PX_VS; i < 32; ++i) {
        reg = ahci_px_rreg(ahci, port, i);
        if (reg) {
            g_test_message("INFO: Vendor register %u non-empty", i);
        }
    }
}

static void ahci_test_identify(AHCIQState *ahci)
{
    uint16_t buff[256];
    unsigned px;
    int rc;
    uint16_t sect_size;
    const size_t buffsize = 512;

    g_assert(ahci != NULL);

    px = ahci_port_select(ahci);
    g_test_message("Selected port %u for test", px);

    ahci_port_clear(ahci, px);

    ahci_io(ahci, px, CMD_IDENTIFY, &buff, buffsize, 0);

    string_bswap16(&buff[10], 20);
    rc = memcmp(&buff[10], "testdisk            ", 20);
    g_assert_cmphex(rc, ==, 0);

    string_bswap16(&buff[23], 8);
    rc = memcmp(&buff[23], "version ", 8);
    g_assert_cmphex(rc, ==, 0);

    sect_size = le16_to_cpu(*((uint16_t *)(&buff[5])));
    g_assert_cmphex(sect_size, ==, AHCI_SECTOR_SIZE);
}

static void ahci_test_io_rw_simple(AHCIQState *ahci, unsigned bufsize,
                                   uint64_t sector, uint8_t read_cmd,
                                   uint8_t write_cmd)
{
    uint64_t ptr;
    uint8_t port;
    unsigned char *tx = g_malloc(bufsize);
    unsigned char *rx = g_malloc0(bufsize);

    g_assert(ahci != NULL);

    port = ahci_port_select(ahci);
    ahci_port_clear(ahci, port);

    ptr = ahci_alloc(ahci, bufsize);
    g_assert(ptr);

    generate_pattern(tx, bufsize, AHCI_SECTOR_SIZE);
    qtest_bufwrite(ahci->parent->qts, ptr, tx, bufsize);

    ahci_guest_io(ahci, port, write_cmd, ptr, bufsize, sector);
    qtest_memset(ahci->parent->qts, ptr, 0x00, bufsize);
    ahci_guest_io(ahci, port, read_cmd, ptr, bufsize, sector);

    qtest_bufread(ahci->parent->qts, ptr, rx, bufsize);
    g_assert_cmphex(memcmp(tx, rx, bufsize), ==, 0);

    ahci_free(ahci, ptr);
    g_free(tx);
    g_free(rx);
}

static uint8_t ahci_test_nondata(AHCIQState *ahci, uint8_t ide_cmd)
{
    uint8_t port;

    port = ahci_port_select(ahci);
    ahci_port_clear(ahci, port);

    ahci_io(ahci, port, ide_cmd, NULL, 0, 0);

    return port;
}

static void ahci_test_flush(AHCIQState *ahci)
{
    ahci_test_nondata(ahci, CMD_FLUSH_CACHE);
}

static void ahci_test_max(AHCIQState *ahci)
{
    RegD2HFIS *d2h = g_malloc0(0x20);
    uint64_t nsect;
    uint8_t port;
    uint8_t cmd;
    uint64_t config_sect = mb_to_sectors(test_image_size_mb) - 1;

    if (config_sect > 0xFFFFFF) {
        cmd = CMD_READ_MAX_EXT;
    } else {
        cmd = CMD_READ_MAX;
    }

    port = ahci_test_nondata(ahci, cmd);
    qtest_memread(ahci->parent->qts, ahci->port[port].fb + 0x40, d2h, 0x20);
    nsect = (uint64_t)d2h->lba_hi[2] << 40 |
        (uint64_t)d2h->lba_hi[1] << 32 |
        (uint64_t)d2h->lba_hi[0] << 24 |
        (uint64_t)d2h->lba_lo[2] << 16 |
        (uint64_t)d2h->lba_lo[1] << 8 |
        (uint64_t)d2h->lba_lo[0];

    g_assert_cmphex(nsect, ==, config_sect);
    g_free(d2h);
}


static void test_sanity(void)
{
    AHCIQState *ahci;
    ahci = ahci_boot(NULL);
    ahci_shutdown(ahci);
}

static void test_pci_spec(void)
{
    AHCIQState *ahci;
    ahci = ahci_boot(NULL);
    ahci_test_pci_spec(ahci);
    ahci_shutdown(ahci);
}

static void test_pci_enable(void)
{
    AHCIQState *ahci;
    ahci = ahci_boot(NULL);
    ahci_pci_enable(ahci);
    ahci_shutdown(ahci);
}

static void test_hba_spec(void)
{
    AHCIQState *ahci;

    ahci = ahci_boot(NULL);
    ahci_pci_enable(ahci);
    ahci_test_hba_spec(ahci);
    ahci_shutdown(ahci);
}

static void test_hba_enable(void)
{
    AHCIQState *ahci;

    ahci = ahci_boot(NULL);
    ahci_pci_enable(ahci);
    ahci_hba_enable(ahci);
    ahci_shutdown(ahci);
}

static void test_identify(void)
{
    AHCIQState *ahci;

    ahci = ahci_boot_and_enable(NULL);
    ahci_test_identify(ahci);
    ahci_shutdown(ahci);
}

static void test_dma_fragmented(void)
{
    AHCIQState *ahci;
    AHCICommand *cmd;
    uint8_t px;
    size_t bufsize = 4096;
    unsigned char *tx = g_malloc(bufsize);
    unsigned char *rx = g_malloc0(bufsize);
    uint64_t ptr;

    ahci = ahci_boot_and_enable(NULL);
    px = ahci_port_select(ahci);
    ahci_port_clear(ahci, px);

    generate_pattern(tx, bufsize, AHCI_SECTOR_SIZE);

    ptr = guest_alloc(&ahci->parent->alloc, bufsize);
    g_assert(ptr);
    qtest_bufwrite(ahci->parent->qts, ptr, tx, bufsize);

    cmd = ahci_command_create(CMD_WRITE_DMA);
    ahci_command_adjust(cmd, 0, ptr, bufsize, 32);
    ahci_command_commit(ahci, cmd, px);
    ahci_command_issue(ahci, cmd);
    ahci_command_verify(ahci, cmd);
    ahci_command_free(cmd);

    cmd = ahci_command_create(CMD_READ_DMA);
    ahci_command_adjust(cmd, 0, ptr, bufsize, 32);
    ahci_command_commit(ahci, cmd, px);
    ahci_command_issue(ahci, cmd);
    ahci_command_verify(ahci, cmd);
    ahci_command_free(cmd);

    qtest_bufread(ahci->parent->qts, ptr, rx, bufsize);
    guest_free(&ahci->parent->alloc, ptr);

    g_assert_cmphex(memcmp(tx, rx, bufsize), ==, 0);

    ahci_shutdown(ahci);

    g_free(rx);
    g_free(tx);
}

static void make_dirty(AHCIQState* ahci, uint8_t port)
{
    uint64_t ptr;
    unsigned bufsize = 512;

    ptr = ahci_alloc(ahci, bufsize);
    g_assert(ptr);

    ahci_guest_io(ahci, port, CMD_WRITE_DMA, ptr, bufsize, 1);
    ahci_free(ahci, ptr);
}

static void test_flush(void)
{
    AHCIQState *ahci;
    uint8_t port;

    ahci = ahci_boot_and_enable(NULL);

    port = ahci_port_select(ahci);
    ahci_port_clear(ahci, port);

    make_dirty(ahci, port);

    ahci_test_flush(ahci);
    ahci_shutdown(ahci);
}

static void test_flush_retry(void)
{
    AHCIQState *ahci;
    AHCICommand *cmd;
    uint8_t port;

    prepare_blkdebug_script(debug_path, "flush_to_disk");
    ahci = ahci_boot_and_enable("-drive file=blkdebug:%s:%s,if=none,id=drive0,"
                                "format=%s,cache=writeback,"
                                "rerror=stop,werror=stop "
                                "-M q35 "
                                "-device ide-hd,drive=drive0 ",
                                debug_path,
                                tmp_path, imgfmt);

    port = ahci_port_select(ahci);
    ahci_port_clear(ahci, port);

    make_dirty(ahci, port);

    cmd = ahci_guest_io_halt(ahci, port, CMD_FLUSH_CACHE, 0, 0, 0);
    ahci_guest_io_resume(ahci, cmd);

    ahci_shutdown(ahci);
}

static void test_migrate_sanity(void)
{
    AHCIQState *src, *dst;
    char *uri = g_strdup_printf("unix:%s", mig_socket);

    src = ahci_boot("-m 384 -M q35 "
                    "-drive if=ide,file=%s,format=%s ", tmp_path, imgfmt);
    dst = ahci_boot("-m 384 -M q35 "
                    "-drive if=ide,file=%s,format=%s "
                    "-incoming %s", tmp_path, imgfmt, uri);

    ahci_migrate(src, dst, uri);

    ahci_shutdown(src);
    ahci_shutdown(dst);
    g_free(uri);
}

static void ahci_migrate_simple(uint8_t cmd_read, uint8_t cmd_write)
{
    AHCIQState *src, *dst;
    uint8_t px;
    size_t bufsize = 4096;
    unsigned char *tx = g_malloc(bufsize);
    unsigned char *rx = g_malloc0(bufsize);
    char *uri = g_strdup_printf("unix:%s", mig_socket);

    src = ahci_boot_and_enable("-m 384 -M q35 "
                               "-drive if=ide,format=%s,file=%s ",
                               imgfmt, tmp_path);
    dst = ahci_boot("-m 384 -M q35 "
                    "-drive if=ide,format=%s,file=%s "
                    "-incoming %s", imgfmt, tmp_path, uri);

    px = ahci_port_select(src);
    ahci_port_clear(src, px);

    generate_pattern(tx, bufsize, AHCI_SECTOR_SIZE);

    ahci_io(src, px, cmd_write, tx, bufsize, 0);
    ahci_migrate(src, dst, uri);
    ahci_io(dst, px, cmd_read, rx, bufsize, 0);

    g_assert_cmphex(memcmp(tx, rx, bufsize), ==, 0);

    ahci_shutdown(src);
    ahci_shutdown(dst);
    g_free(rx);
    g_free(tx);
    g_free(uri);
}

static void test_migrate_dma(void)
{
    ahci_migrate_simple(CMD_READ_DMA, CMD_WRITE_DMA);
}

static void test_migrate_ncq(void)
{
    ahci_migrate_simple(READ_FPDMA_QUEUED, WRITE_FPDMA_QUEUED);
}

static void ahci_halted_io_test(uint8_t cmd_read, uint8_t cmd_write)
{
    AHCIQState *ahci;
    uint8_t port;
    size_t bufsize = 4096;
    unsigned char *tx = g_malloc(bufsize);
    unsigned char *rx = g_malloc0(bufsize);
    uint64_t ptr;
    AHCICommand *cmd;

    prepare_blkdebug_script(debug_path, "write_aio");

    ahci = ahci_boot_and_enable("-drive file=blkdebug:%s:%s,if=none,id=drive0,"
                                "format=%s,cache=writeback,"
                                "rerror=stop,werror=stop "
                                "-M q35 "
                                "-device ide-hd,drive=drive0 ",
                                debug_path,
                                tmp_path, imgfmt);

    port = ahci_port_select(ahci);
    ahci_port_clear(ahci, port);

    generate_pattern(tx, bufsize, AHCI_SECTOR_SIZE);
    ptr = ahci_alloc(ahci, bufsize);
    g_assert(ptr);
    qtest_memwrite(ahci->parent->qts, ptr, tx, bufsize);

    cmd = ahci_guest_io_halt(ahci, port, cmd_write,
                             ptr, bufsize, 0);

    ahci_guest_io_resume(ahci, cmd);
    ahci_free(ahci, ptr);

    ahci_io(ahci, port, cmd_read, rx, bufsize, 0);
    g_assert_cmphex(memcmp(tx, rx, bufsize), ==, 0);

    ahci_shutdown(ahci);
    g_free(rx);
    g_free(tx);
}

static void test_halted_dma(void)
{
    ahci_halted_io_test(CMD_READ_DMA, CMD_WRITE_DMA);
}

static void test_halted_ncq(void)
{
    ahci_halted_io_test(READ_FPDMA_QUEUED, WRITE_FPDMA_QUEUED);
}

static void ahci_migrate_halted_io(uint8_t cmd_read, uint8_t cmd_write)
{
    AHCIQState *src, *dst;
    uint8_t port;
    size_t bufsize = 4096;
    unsigned char *tx = g_malloc(bufsize);
    unsigned char *rx = g_malloc0(bufsize);
    uint64_t ptr;
    AHCICommand *cmd;
    char *uri = g_strdup_printf("unix:%s", mig_socket);

    prepare_blkdebug_script(debug_path, "write_aio");

    src = ahci_boot_and_enable("-drive file=blkdebug:%s:%s,if=none,id=drive0,"
                               "format=%s,cache=writeback,"
                               "rerror=stop,werror=stop "
                               "-M q35 "
                               "-device ide-hd,drive=drive0 ",
                               debug_path,
                               tmp_path, imgfmt);

    dst = ahci_boot("-drive file=%s,if=none,id=drive0,"
                    "format=%s,cache=writeback,"
                    "rerror=stop,werror=stop "
                    "-M q35 "
                    "-device ide-hd,drive=drive0 "
                    "-incoming %s",
                    tmp_path, imgfmt, uri);

    port = ahci_port_select(src);
    ahci_port_clear(src, port);
    generate_pattern(tx, bufsize, AHCI_SECTOR_SIZE);

    ptr = ahci_alloc(src, bufsize);
    g_assert(ptr);
    qtest_memwrite(src->parent->qts, ptr, tx, bufsize);

    cmd = ahci_guest_io_halt(src, port, cmd_write,
                             ptr, bufsize, 0);
    ahci_migrate(src, dst, uri);
    ahci_guest_io_resume(dst, cmd);
    ahci_free(dst, ptr);

    ahci_io(dst, port, cmd_read, rx, bufsize, 0);

    g_assert_cmphex(memcmp(tx, rx, bufsize), ==, 0);

    ahci_shutdown(src);
    ahci_shutdown(dst);
    g_free(rx);
    g_free(tx);
    g_free(uri);
}

static void test_migrate_halted_dma(void)
{
    ahci_migrate_halted_io(CMD_READ_DMA, CMD_WRITE_DMA);
}

static void test_migrate_halted_ncq(void)
{
    ahci_migrate_halted_io(READ_FPDMA_QUEUED, WRITE_FPDMA_QUEUED);
}

static void test_flush_migrate(void)
{
    AHCIQState *src, *dst;
    AHCICommand *cmd;
    uint8_t px;
    char *uri = g_strdup_printf("unix:%s", mig_socket);

    prepare_blkdebug_script(debug_path, "flush_to_disk");

    src = ahci_boot_and_enable("-drive file=blkdebug:%s:%s,if=none,id=drive0,"
                               "cache=writeback,rerror=stop,werror=stop,"
                               "format=%s "
                               "-M q35 "
                               "-device ide-hd,drive=drive0 ",
                               debug_path, tmp_path, imgfmt);
    dst = ahci_boot("-drive file=%s,if=none,id=drive0,"
                    "cache=writeback,rerror=stop,werror=stop,"
                    "format=%s "
                    "-M q35 "
                    "-device ide-hd,drive=drive0 "
                    "-incoming %s", tmp_path, imgfmt, uri);

    px = ahci_port_select(src);
    ahci_port_clear(src, px);

    make_dirty(src, px);

    cmd = ahci_command_create(CMD_FLUSH_CACHE);
    ahci_command_commit(src, cmd, px);
    ahci_command_issue_async(src, cmd);
    qtest_qmp_eventwait(src->parent->qts, "STOP");

    ahci_migrate(src, dst, uri);

    qtest_qmp_send(dst->parent->qts, "{'execute':'cont' }");
    qtest_qmp_eventwait(dst->parent->qts, "RESUME");
    ahci_command_wait(dst, cmd);
    ahci_command_verify(dst, cmd);

    ahci_command_free(cmd);
    ahci_shutdown(src);
    ahci_shutdown(dst);
    g_free(uri);
}

static void test_max(void)
{
    AHCIQState *ahci;

    ahci = ahci_boot_and_enable(NULL);
    ahci_test_max(ahci);
    ahci_shutdown(ahci);
}

static void test_reset(void)
{
    AHCIQState *ahci;
    int i;

    ahci = ahci_boot(NULL);
    ahci_test_pci_spec(ahci);
    ahci_pci_enable(ahci);

    for (i = 0; i < 2; i++) {
        ahci_test_hba_spec(ahci);
        ahci_hba_enable(ahci);
        ahci_test_identify(ahci);
        ahci_test_io_rw_simple(ahci, 4096, 0,
                               CMD_READ_DMA_EXT,
                               CMD_WRITE_DMA_EXT);
        ahci_set(ahci, AHCI_GHC, AHCI_GHC_HR);
        ahci_clean_mem(ahci);
    }

    ahci_shutdown(ahci);
}

static void test_reset_pending_callback(void)
{
    AHCIQState *ahci;
    AHCICommand *cmd;
    uint8_t port;
    uint64_t ptr1;
    uint64_t ptr2;

    int bufsize = 4 * 1024;
    int speed = bufsize + (bufsize / 2);
    int offset1 = 0;
    int offset2 = bufsize / AHCI_SECTOR_SIZE;

    g_autofree unsigned char *tx1 = g_malloc(bufsize);
    g_autofree unsigned char *tx2 = g_malloc(bufsize);
    g_autofree unsigned char *rx1 = g_malloc0(bufsize);
    g_autofree unsigned char *rx2 = g_malloc0(bufsize);

    ahci = ahci_boot_and_enable("-drive if=none,id=drive0,file=%s,"
                                "cache=writeback,format=%s,"
                                "throttling.bps-write=%d "
                                "-M q35 "
                                "-device ide-hd,drive=drive0 ",
                                tmp_path, imgfmt, speed);

    port = ahci_port_select(ahci);
    ahci_port_clear(ahci, port);

    ptr1 = ahci_alloc(ahci, bufsize);
    ptr2 = ahci_alloc(ahci, bufsize);

    g_assert(ptr1 && ptr2);

    do {
        generate_pattern(tx1, bufsize, AHCI_SECTOR_SIZE);
        generate_pattern(tx2, bufsize, AHCI_SECTOR_SIZE);
    } while (memcmp(tx1, tx2, bufsize) == 0);

    qtest_bufwrite(ahci->parent->qts, ptr1, tx1, bufsize);
    qtest_bufwrite(ahci->parent->qts, ptr2, tx2, bufsize);

    ahci_guest_io(ahci, port, CMD_WRITE_DMA_EXT, ptr1, bufsize, offset1);

    cmd = ahci_command_create(CMD_WRITE_DMA_EXT);
    ahci_command_adjust(cmd, offset2, ptr2, bufsize, 0);
    ahci_command_commit(ahci, cmd, port);
    ahci_command_issue_async(ahci, cmd);

    ahci_set(ahci, AHCI_GHC, AHCI_GHC_HR);

    ahci_command_free(cmd);

    sleep(1);

    ahci_clean_mem(ahci);
    ahci_pci_enable(ahci);
    ahci_hba_enable(ahci);
    port = ahci_port_select(ahci);
    ahci_port_clear(ahci, port);

    ahci_guest_io(ahci, port, CMD_READ_DMA_EXT, ptr1, bufsize, offset1);
    qtest_bufread(ahci->parent->qts, ptr1, rx1, bufsize);
    g_assert_cmphex(memcmp(tx1, rx1, bufsize), ==, 0);

    ahci_guest_io(ahci, port, CMD_READ_DMA_EXT, ptr2, bufsize, offset2);
    qtest_bufread(ahci->parent->qts, ptr2, rx2, bufsize);
    g_assert_cmphex(memcmp(tx2, rx2, bufsize), ==, 0);

    ahci_free(ahci, ptr1);
    ahci_free(ahci, ptr2);

    ahci_clean_mem(ahci);

    ahci_shutdown(ahci);
}

static void test_ncq_simple(void)
{
    AHCIQState *ahci;

    ahci = ahci_boot_and_enable(NULL);
    ahci_test_io_rw_simple(ahci, 4096, 0,
                           READ_FPDMA_QUEUED,
                           WRITE_FPDMA_QUEUED);
    ahci_shutdown(ahci);
}

static int prepare_iso(size_t size, unsigned char **buf, char **name)
{
    g_autofree char *cdrom_path = NULL;
    unsigned char *patt;
    ssize_t ret;
    int fd = g_file_open_tmp("qtest.iso.XXXXXX", &cdrom_path, NULL);

    g_assert(fd != -1);
    g_assert(buf);
    g_assert(name);
    patt = g_malloc(size);

    generate_pattern(patt, size, ATAPI_SECTOR_SIZE);
    ret = write(fd, patt, size);
    g_assert(ret == size);

    *name = g_strdup(cdrom_path);
    *buf = patt;
    return fd;
}

static void remove_iso(int fd, char *name)
{
    unlink(name);
    g_free(name);
    close(fd);
}

static int ahci_cb_cmp_buff(AHCIQState *ahci, AHCICommand *cmd,
                            const AHCIOpts *opts)
{
    unsigned char *tx = opts->opaque;
    unsigned char *rx;

    if (!opts->size) {
        return 0;
    }

    rx = g_malloc0(opts->size);
    qtest_bufread(ahci->parent->qts, opts->buffer, rx, opts->size);
    g_assert_cmphex(memcmp(tx, rx, opts->size), ==, 0);
    g_free(rx);

    return 0;
}

static void ahci_test_cdrom(int nsectors, bool dma, uint8_t cmd,
                            bool override_bcl, uint16_t bcl)
{
    AHCIQState *ahci;
    unsigned char *tx;
    char *iso;
    int fd;
    AHCIOpts opts = {
        .size = ((uint64_t)ATAPI_SECTOR_SIZE * nsectors),
        .atapi = true,
        .atapi_dma = dma,
        .post_cb = ahci_cb_cmp_buff,
        .set_bcl = override_bcl,
        .bcl = bcl,
    };
    uint64_t iso_size = (uint64_t)ATAPI_SECTOR_SIZE * (nsectors + 1);

    fd = prepare_iso(iso_size, &tx, &iso);
    opts.opaque = tx;

    ahci = ahci_boot_and_enable("-drive if=none,id=drive0,file=%s,format=raw "
                                "-M q35 "
                                "-device ide-cd,drive=drive0 ", iso);

    ahci_exec(ahci, ahci_port_select(ahci), cmd, &opts);

    g_free(tx);
    ahci_shutdown(ahci);
    remove_iso(fd, iso);
}

static void ahci_test_cdrom_read10(int nsectors, bool dma)
{
    ahci_test_cdrom(nsectors, dma, CMD_ATAPI_READ_10, false, 0);
}

static void test_cdrom_dma(void)
{
    ahci_test_cdrom_read10(1, true);
}

static void test_cdrom_dma_multi(void)
{
    ahci_test_cdrom_read10(3, true);
}

static void test_cdrom_pio(void)
{
    ahci_test_cdrom_read10(1, false);
}

static void test_cdrom_pio_multi(void)
{
    ahci_test_cdrom_read10(3, false);
}

static void test_atapi_bcl(void)
{
    ahci_test_cdrom(0, false, CMD_ATAPI_READ_CD, true, 0);
}


static void atapi_wait_tray(AHCIQState *ahci, bool open)
{
    QDict *rsp = qtest_qmp_eventwait_ref(ahci->parent->qts,
                                         "DEVICE_TRAY_MOVED");
    QDict *data = qdict_get_qdict(rsp, "data");
    if (open) {
        g_assert(qdict_get_bool(data, "tray-open"));
    } else {
        g_assert(!qdict_get_bool(data, "tray-open"));
    }
    qobject_unref(rsp);
}

static void test_atapi_tray(void)
{
    AHCIQState *ahci;
    unsigned char *tx;
    char *iso;
    int fd;
    uint8_t port, sense, asc;
    uint64_t iso_size = ATAPI_SECTOR_SIZE;
    QDict *rsp;

    fd = prepare_iso(iso_size, &tx, &iso);
    ahci = ahci_boot_and_enable("-blockdev node-name=drive0,driver=file,filename=%s "
                                "-M q35 "
                                "-device ide-cd,id=cd0,drive=drive0 ", iso);
    port = ahci_port_select(ahci);

    ahci_atapi_eject(ahci, port);
    atapi_wait_tray(ahci, true);

    ahci_atapi_load(ahci, port);
    atapi_wait_tray(ahci, false);

    qtest_qmp_send(ahci->parent->qts, "{'execute': 'blockdev-open-tray', "
                    "'arguments': {'id': 'cd0'}}");
    atapi_wait_tray(ahci, true);
    rsp = qtest_qmp_receive(ahci->parent->qts);
    qobject_unref(rsp);

    qtest_qmp_assert_success(ahci->parent->qts,
                             "{'execute': 'blockdev-remove-medium', "
                             "'arguments': {'id': 'cd0'}}");

    ahci_atapi_load(ahci, port);
    atapi_wait_tray(ahci, false);

    ahci_atapi_eject(ahci, port);
    atapi_wait_tray(ahci, true);

    qtest_qmp_assert_success(
        ahci->parent->qts,
        "{'execute': 'blockdev-add', "
        "'arguments': {'node-name': 'node0', "
                      "'driver': 'raw', "
                      "'file': { 'driver': 'file', "
                                "'filename': %s }}}", iso);
    qtest_qmp_assert_success(
        ahci->parent->qts,
        "{'execute': 'blockdev-insert-medium',"
        "'arguments': { 'id': 'cd0', "
                       "'node-name': 'node0' }}");

    qtest_qmp_send(ahci->parent->qts, "{'execute': 'blockdev-close-tray', "
                   "'arguments': {'id': 'cd0'}}");
    atapi_wait_tray(ahci, false);
    rsp = qtest_qmp_receive(ahci->parent->qts);
    qobject_unref(rsp);

    ahci_atapi_test_ready(ahci, port, false, SENSE_NOT_READY);
    ahci_atapi_get_sense(ahci, port, &sense, &asc);
    g_assert_cmpuint(sense, ==, SENSE_NOT_READY);
    g_assert_cmpuint(asc, ==, ASC_MEDIUM_NOT_PRESENT);

    ahci_atapi_test_ready(ahci, port, false, SENSE_UNIT_ATTENTION);
    ahci_atapi_get_sense(ahci, port, &sense, &asc);
    g_assert_cmpuint(sense, ==, SENSE_UNIT_ATTENTION);
    g_assert_cmpuint(asc, ==, ASC_MEDIUM_MAY_HAVE_CHANGED);

    ahci_atapi_test_ready(ahci, port, true, SENSE_NO_SENSE);
    ahci_atapi_get_sense(ahci, port, &sense, &asc);
    g_assert_cmpuint(sense, ==, SENSE_NO_SENSE);

    ahci_atapi_eject(ahci, port);
    atapi_wait_tray(ahci, true);

    ahci_atapi_load(ahci, port);
    atapi_wait_tray(ahci, false);

    g_free(tx);
    ahci_shutdown(ahci);
    remove_iso(fd, iso);
}

enum BuffLen {
    LEN_BEGIN = 0,
    LEN_SIMPLE = LEN_BEGIN,
    LEN_DOUBLE,
    LEN_LONG,
    LEN_SHORT,
    NUM_LENGTHS
};

static const char *buff_len_str[NUM_LENGTHS] = { "simple", "double",
                                                 "long", "short" };

enum AddrMode {
    ADDR_MODE_BEGIN = 0,
    ADDR_MODE_LBA28 = ADDR_MODE_BEGIN,
    ADDR_MODE_LBA48,
    NUM_ADDR_MODES
};

static const char *addr_mode_str[NUM_ADDR_MODES] = { "lba28", "lba48" };

enum IOMode {
    MODE_BEGIN = 0,
    MODE_PIO = MODE_BEGIN,
    MODE_DMA,
    NUM_MODES
};

static const char *io_mode_str[NUM_MODES] = { "pio", "dma" };

enum IOOps {
    IO_BEGIN = 0,
    IO_READ = IO_BEGIN,
    IO_WRITE,
    NUM_IO_OPS
};

enum OffsetType {
    OFFSET_BEGIN = 0,
    OFFSET_ZERO = OFFSET_BEGIN,
    OFFSET_LOW,
    OFFSET_HIGH,
    NUM_OFFSETS
};

static const char *offset_str[NUM_OFFSETS] = { "zero", "low", "high" };

typedef struct AHCIIOTestOptions {
    enum BuffLen length;
    enum AddrMode address_type;
    enum IOMode io_type;
    enum OffsetType offset;
} AHCIIOTestOptions;

static uint64_t offset_sector(enum OffsetType ofst,
                              enum AddrMode addr_type,
                              uint64_t buffsize)
{
    uint64_t ceil;
    uint64_t nsectors;

    switch (ofst) {
    case OFFSET_ZERO:
        return 0;
    case OFFSET_LOW:
        return 1;
    case OFFSET_HIGH:
        ceil = (addr_type == ADDR_MODE_LBA28) ? 0xfffffff : 0xffffffffffff;
        ceil = MIN(ceil, mb_to_sectors(test_image_size_mb) - 1);
        nsectors = buffsize / AHCI_SECTOR_SIZE;
        return ceil - nsectors + 1;
    default:
        g_assert_not_reached();
    }
}

static const uint8_t io_cmds[NUM_MODES][NUM_ADDR_MODES][NUM_IO_OPS] = {
    [MODE_PIO] = {
        [ADDR_MODE_LBA28] = {
            [IO_READ] = CMD_READ_PIO,
            [IO_WRITE] = CMD_WRITE_PIO },
        [ADDR_MODE_LBA48] = {
            [IO_READ] = CMD_READ_PIO_EXT,
            [IO_WRITE] = CMD_WRITE_PIO_EXT }
    },
    [MODE_DMA] = {
        [ADDR_MODE_LBA28] = {
            [IO_READ] = CMD_READ_DMA,
            [IO_WRITE] = CMD_WRITE_DMA },
        [ADDR_MODE_LBA48] = {
            [IO_READ] = CMD_READ_DMA_EXT,
            [IO_WRITE] = CMD_WRITE_DMA_EXT }
    }
};

static void test_io_rw_interface(enum AddrMode lba48, enum IOMode dma,
                                 unsigned bufsize, uint64_t sector)
{
    AHCIQState *ahci;

    ahci = ahci_boot_and_enable(NULL);
    ahci_test_io_rw_simple(ahci, bufsize, sector,
                           io_cmds[dma][lba48][IO_READ],
                           io_cmds[dma][lba48][IO_WRITE]);
    ahci_shutdown(ahci);
}

static void test_io_interface(gconstpointer opaque)
{
    AHCIIOTestOptions *opts = (AHCIIOTestOptions *)opaque;
    unsigned bufsize;
    uint64_t sector;

    switch (opts->length) {
    case LEN_SIMPLE:
        bufsize = 4096;
        break;
    case LEN_DOUBLE:
        bufsize = 8192;
        break;
    case LEN_LONG:
        bufsize = 4096 * 64;
        break;
    case LEN_SHORT:
        bufsize = 512;
        break;
    default:
        g_assert_not_reached();
    }

    sector = offset_sector(opts->offset, opts->address_type, bufsize);
    test_io_rw_interface(opts->address_type, opts->io_type, bufsize, sector);
    g_free(opts);
}

static void create_ahci_io_test(enum IOMode type, enum AddrMode addr,
                                enum BuffLen len, enum OffsetType offset)
{
    char *name;
    AHCIIOTestOptions *opts;

    opts = g_new(AHCIIOTestOptions, 1);
    opts->length = len;
    opts->address_type = addr;
    opts->io_type = type;
    opts->offset = offset;

    name = g_strdup_printf("ahci/io/%s/%s/%s/%s",
                           io_mode_str[type],
                           addr_mode_str[addr],
                           buff_len_str[len],
                           offset_str[offset]);

    if ((addr == ADDR_MODE_LBA48) && (offset == OFFSET_HIGH) &&
        (mb_to_sectors(test_image_size_mb) <= 0xFFFFFFF)) {
        g_test_message("%s: skipped; test image too small", name);
        g_free(opts);
        g_free(name);
        return;
    }

    qtest_add_data_func(name, opts, test_io_interface);
    g_free(name);
}

int main(int argc, char **argv)
{
    const char *arch, *base;
    int ret;
    int fd;
    int c;
    int i, j, k, m;

    static struct option long_options[] = {
        {"pedantic", no_argument, 0, 'p' },
        {0, 0, 0, 0},
    };

    g_test_init(&argc, &argv, NULL);

    while (1) {
        c = getopt_long(argc, argv, "", long_options, NULL);
        if (c == -1) {
            break;
        }
        switch (c) {
        case -1:
            break;
        case 'p':
            ahci_pedantic = 1;
            break;
        default:
            fprintf(stderr, "Unrecognized ahci_test option.\n");
            g_assert_not_reached();
        }
    }

    arch = qtest_get_arch();
    if (strcmp(arch, "i386") && strcmp(arch, "x86_64")) {
        g_test_message("Skipping test for non-x86");
        return 0;
    }

#ifndef _WIN32
    base = g_get_tmp_dir();
#else
    base = ".";
#endif

    tmp_path = g_strdup_printf("%s/qtest.XXXXXX", base);
    fd = g_mkstemp(tmp_path);
    g_assert(fd >= 0);
    if (have_qemu_img()) {
        imgfmt = "qcow2";
        test_image_size_mb = TEST_IMAGE_SIZE_MB_LARGE;
        mkqcow2(tmp_path, TEST_IMAGE_SIZE_MB_LARGE);
    } else {
        g_test_message("QTEST_QEMU_IMG not set or qemu-img missing; "
                       "skipping LBA48 high-sector tests");
        imgfmt = "raw";
        test_image_size_mb = TEST_IMAGE_SIZE_MB_SMALL;
        ret = ftruncate(fd, test_image_size_mb * 1024 * 1024);
        g_assert(ret == 0);
    }
    close(fd);

    debug_path = g_strdup_printf("%s/qtest-blkdebug.XXXXXX", base);
    fd = g_mkstemp(debug_path);
    g_assert(fd >= 0);
    close(fd);

    fd = g_file_open_tmp("qtest-migration.XXXXXX", &mig_socket, NULL);
    g_assert(fd >= 0);
    close(fd);

    qtest_add_func("/ahci/sanity",     test_sanity);
    qtest_add_func("/ahci/pci_spec",   test_pci_spec);
    qtest_add_func("/ahci/pci_enable", test_pci_enable);
    qtest_add_func("/ahci/hba_spec",   test_hba_spec);
    qtest_add_func("/ahci/hba_enable", test_hba_enable);
    qtest_add_func("/ahci/identify",   test_identify);

    for (i = MODE_BEGIN; i < NUM_MODES; i++) {
        for (j = ADDR_MODE_BEGIN; j < NUM_ADDR_MODES; j++) {
            for (k = LEN_BEGIN; k < NUM_LENGTHS; k++) {
                for (m = OFFSET_BEGIN; m < NUM_OFFSETS; m++) {
                    create_ahci_io_test(i, j, k, m);
                }
            }
        }
    }

    qtest_add_func("/ahci/io/dma/lba28/fragmented", test_dma_fragmented);

    qtest_add_func("/ahci/flush/simple", test_flush);
    qtest_add_func("/ahci/flush/retry", test_flush_retry);
    qtest_add_func("/ahci/flush/migrate", test_flush_migrate);

    qtest_add_func("/ahci/migrate/sanity", test_migrate_sanity);
    qtest_add_func("/ahci/migrate/dma/simple", test_migrate_dma);
    qtest_add_func("/ahci/io/dma/lba28/retry", test_halted_dma);
    qtest_add_func("/ahci/migrate/dma/halted", test_migrate_halted_dma);

    qtest_add_func("/ahci/max", test_max);
    qtest_add_func("/ahci/reset/simple", test_reset);
    qtest_add_func("/ahci/reset/pending_callback", test_reset_pending_callback);

    qtest_add_func("/ahci/io/ncq/simple", test_ncq_simple);
    qtest_add_func("/ahci/migrate/ncq/simple", test_migrate_ncq);
    qtest_add_func("/ahci/io/ncq/retry", test_halted_ncq);
    qtest_add_func("/ahci/migrate/ncq/halted", test_migrate_halted_ncq);

    qtest_add_func("/ahci/cdrom/dma/single", test_cdrom_dma);
    qtest_add_func("/ahci/cdrom/dma/multi", test_cdrom_dma_multi);
    qtest_add_func("/ahci/cdrom/pio/single", test_cdrom_pio);
    qtest_add_func("/ahci/cdrom/pio/multi", test_cdrom_pio_multi);

    qtest_add_func("/ahci/cdrom/pio/bcl", test_atapi_bcl);
    qtest_add_func("/ahci/cdrom/eject", test_atapi_tray);

    ret = g_test_run();

    unlink(tmp_path);
    g_free(tmp_path);
    unlink(debug_path);
    g_free(debug_path);
    unlink(mig_socket);
    g_free(mig_socket);

    return ret;
}
