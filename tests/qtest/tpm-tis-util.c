/*
 * QTest testcase for TPM TIS: common test functions used for both
 * the ISA and SYSBUS devices
 *
 * Copyright (c) 2018 Red Hat, Inc.
 * Copyright (c) 2018 IBM Corporation
 *
 * Authors:
 *   Marc-André Lureau <marcandre.lureau@redhat.com>
 *   Stefan Berger <stefanb@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib/gstdio.h>

#include "hw/acpi/tpm.h"
#include "io/channel-socket.h"
#include "libqtest-single.h"
#include "qemu/module.h"
#include "tpm-emu.h"
#include "tpm-util.h"
#include "tpm-tis-util.h"

#define DEBUG_TIS_TEST 0

#define DPRINTF(fmt, ...) do { \
    if (DEBUG_TIS_TEST) { \
        printf(fmt, ## __VA_ARGS__); \
    } \
} while (0)

#define DPRINTF_ACCESS \
    DPRINTF("%s: %d: locty=%d l=%d access=0x%02x pending_request_flag=0x%x\n", \
            __func__, __LINE__, locty, l, access, pending_request_flag)

#define DPRINTF_STS \
    DPRINTF("%s: %d: sts = 0x%08x\n", __func__, __LINE__, sts)

static const uint8_t TPM_CMD[12] =
    "\x80\x01\x00\x00\x00\x0c\x00\x00\x01\x44\x00\x00";

void tpm_tis_test_check_localities(const void *data)
{
    uint8_t locty;
    uint8_t access;
    uint32_t ifaceid;
    uint32_t capability;
    uint32_t didvid;
    uint32_t rid;

    for (locty = 0; locty < TPM_TIS_NUM_LOCALITIES; locty++) {
        access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        capability = readl(TIS_REG(locty, TPM_TIS_REG_INTF_CAPABILITY));
        g_assert_cmpint(capability, ==, TPM_TIS_CAPABILITIES_SUPPORTED2_0);

        ifaceid = readl(TIS_REG(locty, TPM_TIS_REG_INTERFACE_ID));
        g_assert_cmpint(ifaceid, ==, TPM_TIS_IFACE_ID_SUPPORTED_FLAGS2_0);

        didvid = readl(TIS_REG(locty, TPM_TIS_REG_DID_VID));
        g_assert_cmpint(didvid, !=, 0);
        g_assert_cmpint(didvid, !=, 0xffffffff);

        rid = readl(TIS_REG(locty, TPM_TIS_REG_RID));
        g_assert_cmpint(rid, !=, 0);
        g_assert_cmpint(rid, !=, 0xffffffff);
    }
}

void tpm_tis_test_check_access_reg(const void *data)
{
    uint8_t locty;
    uint8_t access;

    for (locty = 0; locty < TPM_TIS_NUM_LOCALITIES - 1; locty++) {
        access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        writeb(TIS_REG(locty, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_REQUEST_USE);

        access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        writeb(TIS_REG(locty, TPM_TIS_REG_ACCESS),
               TPM_TIS_ACCESS_ACTIVE_LOCALITY);
        access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
    }
}

void tpm_tis_test_check_access_reg_seize(const void *data)
{
    int locty, l;
    uint8_t access;
    uint8_t pending_request_flag;

    for (locty = 0; locty < TPM_TIS_NUM_LOCALITIES - 1; locty++) {
        pending_request_flag = 0;

        access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        writeb(TIS_REG(locty, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_REQUEST_USE);
        access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        for (l = 0; l < locty; l++) {
            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            writeb(TIS_REG(l, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_REQUEST_USE);

            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_REQUEST_USE |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        TPM_TIS_ACCESS_PENDING_REQUEST |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            writeb(TIS_REG(l, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_SEIZE);
            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_REQUEST_USE |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        TPM_TIS_ACCESS_PENDING_REQUEST |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            pending_request_flag = TPM_TIS_ACCESS_PENDING_REQUEST;
        }

        for (l = locty + 1; l < TPM_TIS_NUM_LOCALITIES - 1; l++) {
            writeb(TIS_REG(l, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_REQUEST_USE);

            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_REQUEST_USE |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            access = readb(TIS_REG(l - 1, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        TPM_TIS_ACCESS_PENDING_REQUEST |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            writeb(TIS_REG(l, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_SEIZE);

            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            access = readb(TIS_REG(l - 1, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_BEEN_SEIZED |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            writeb(TIS_REG(l - 1, TPM_TIS_REG_ACCESS),
                   TPM_TIS_ACCESS_BEEN_SEIZED);

            access = readb(TIS_REG(l - 1, TPM_TIS_REG_ACCESS));
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
        }

        if (locty <= 1) {
            pending_request_flag = 0;
        }

        l--;

        access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
        DPRINTF_ACCESS;

        DPRINTF("%s: %d: relinquishing control on l = %d\n",
                __func__, __LINE__, l);
        writeb(TIS_REG(l, TPM_TIS_REG_ACCESS),
               TPM_TIS_ACCESS_ACTIVE_LOCALITY);

        access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
        DPRINTF_ACCESS;
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    pending_request_flag |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        for (l = locty - 1; l >= 0; l--) {
            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            writeb(TIS_REG(l, TPM_TIS_REG_ACCESS),
                   TPM_TIS_ACCESS_ACTIVE_LOCALITY);

            if (l == 1) {
                pending_request_flag = 0;
            }
        }

        for (l = 0; l < TPM_TIS_NUM_LOCALITIES - 1; l++) {
            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
        }
    }
}

void tpm_tis_test_check_access_reg_release(const void *data)
{
    int locty, l;
    uint8_t access;
    uint8_t pending_request_flag;

    for (locty = TPM_TIS_NUM_LOCALITIES - 2; locty >= 0; locty--) {
        pending_request_flag = 0;

        access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        writeb(TIS_REG(locty, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_REQUEST_USE);
        access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        for (l = 0; l < TPM_TIS_NUM_LOCALITIES - 1; l++) {
            if (l == locty) {
                continue;
            }
            writeb(TIS_REG(l, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_REQUEST_USE);
            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_REQUEST_USE |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
            pending_request_flag = TPM_TIS_ACCESS_PENDING_REQUEST;
        }
        writeb(TIS_REG(locty, TPM_TIS_REG_ACCESS),
               TPM_TIS_ACCESS_ACTIVE_LOCALITY);
        for (l = TPM_TIS_NUM_LOCALITIES - 2; l >= 0; l--) {
            if (l == locty) {
                continue;
            }
            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
            writeb(TIS_REG(l, TPM_TIS_REG_ACCESS),
                   TPM_TIS_ACCESS_ACTIVE_LOCALITY);
            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            if (l == 1 || (locty <= 1 && l == 2)) {
                pending_request_flag = 0;
            }
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
        }
    }
}

void tpm_tis_test_check_transmit(const void *data)
{
    const TPMTestState *s = data;
    uint8_t access;
    uint32_t sts;
    uint16_t bcount;
    size_t i;

    writeb(TIS_REG(0, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_REQUEST_USE);
    access = readb(TIS_REG(0, TPM_TIS_REG_ACCESS));
    g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

    sts = readl(TIS_REG(0, TPM_TIS_REG_STS));
    DPRINTF_STS;

    g_assert_cmpint(sts & 0xff, ==, 0);
    g_assert_cmpint(sts & TPM_TIS_STS_TPM_FAMILY_MASK, ==,
                    TPM_TIS_STS_TPM_FAMILY2_0);

    bcount = (sts >> 8) & 0xffff;
    g_assert_cmpint(bcount, >=, 128);

    writel(TIS_REG(0, TPM_TIS_REG_STS), TPM_TIS_STS_COMMAND_READY);
    sts = readl(TIS_REG(0, TPM_TIS_REG_STS));
    DPRINTF_STS;
    g_assert_cmpint(sts & 0xff, ==, TPM_TIS_STS_COMMAND_READY);

    for (i = 0; i < sizeof(TPM_CMD); i++) {
        writeb(TIS_REG(0, TPM_TIS_REG_DATA_FIFO), TPM_CMD[i]);
        sts = readl(TIS_REG(0, TPM_TIS_REG_STS));
        DPRINTF_STS;
        if (i < sizeof(TPM_CMD) - 1) {
            g_assert_cmpint(sts & 0xff, ==,
                            TPM_TIS_STS_EXPECT | TPM_TIS_STS_VALID);
        } else {
            g_assert_cmpint(sts & 0xff, ==, TPM_TIS_STS_VALID);
        }
        g_assert_cmpint((sts >> 8) & 0xffff, ==, --bcount);
    }
    writeb(TIS_REG(0, TPM_TIS_REG_STS), TPM_TIS_STS_TPM_GO);

    uint64_t end_time = g_get_monotonic_time() + 50 * G_TIME_SPAN_SECOND;
    do {
        sts = readl(TIS_REG(0, TPM_TIS_REG_STS));
        if ((sts & TPM_TIS_STS_DATA_AVAILABLE) != 0) {
            break;
        }
    } while (g_get_monotonic_time() < end_time);

    sts = readl(TIS_REG(0, TPM_TIS_REG_STS));
    DPRINTF_STS;
    g_assert_cmpint(sts & 0xff, == ,
                    TPM_TIS_STS_VALID | TPM_TIS_STS_DATA_AVAILABLE);
    bcount = (sts >> 8) & 0xffff;

    uint8_t tpm_msg[sizeof(struct tpm_hdr)];
    g_assert_cmpint(sizeof(tpm_msg), ==, bcount);

    for (i = 0; i < sizeof(tpm_msg); i++) {
        tpm_msg[i] = readb(TIS_REG(0, TPM_TIS_REG_DATA_FIFO));
        sts = readl(TIS_REG(0, TPM_TIS_REG_STS));
        DPRINTF_STS;
        if (sts & TPM_TIS_STS_DATA_AVAILABLE) {
            g_assert_cmpint((sts >> 8) & 0xffff, ==, --bcount);
        }
    }
    g_assert_cmpmem(tpm_msg, sizeof(tpm_msg), s->tpm_msg, sizeof(*s->tpm_msg));

    writeb(TIS_REG(0, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_ACTIVE_LOCALITY);
    access = readb(TIS_REG(0, TPM_TIS_REG_ACCESS));
}

void tpm_tis_transfer(QTestState *s,
                      const unsigned char *req, size_t req_size,
                      unsigned char *rsp, size_t rsp_size)
{
    uint32_t sts;
    uint16_t bcount;
    size_t i;

    qtest_writeb(s, TIS_REG(0, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_REQUEST_USE);
    qtest_writel(s, TIS_REG(0, TPM_TIS_REG_STS), TPM_TIS_STS_COMMAND_READY);

    sts = qtest_readl(s, TIS_REG(0, TPM_TIS_REG_STS));
    bcount = (sts >> 8) & 0xffff;
    g_assert_cmpint(bcount, >=, req_size);

    for (i = 0; i < req_size; i++) {
        qtest_writeb(s, TIS_REG(0, TPM_TIS_REG_DATA_FIFO), req[i]);
    }

    qtest_writeb(s, TIS_REG(0, TPM_TIS_REG_STS), TPM_TIS_STS_TPM_GO);

    uint64_t end_time = g_get_monotonic_time() + 50 * G_TIME_SPAN_SECOND;
    do {
        sts = qtest_readl(s, TIS_REG(0, TPM_TIS_REG_STS));
        if ((sts & TPM_TIS_STS_DATA_AVAILABLE) != 0) {
            break;
        }
    } while (g_get_monotonic_time() < end_time);

    sts = qtest_readl(s, TIS_REG(0, TPM_TIS_REG_STS));
    bcount = (sts >> 8) & 0xffff;

    memset(rsp, 0, rsp_size);
    for (i = 0; i < bcount; i++) {
        rsp[i] = qtest_readb(s, TIS_REG(0, TPM_TIS_REG_DATA_FIFO));
    }

    qtest_writeb(s, TIS_REG(0, TPM_TIS_REG_ACCESS),
                 TPM_TIS_ACCESS_ACTIVE_LOCALITY);
}
