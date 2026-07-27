/*
 * Test serial output of some machines.
 *
 * Copyright 2016 Thomas Huth, Red Hat Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2
 * or later. See the COPYING file in the top-level directory.
 *
 * This test is used to check that the serial output of the firmware
 * (that we provide for some machines) or some small mini-kernels that
 * we provide here contains an expected string. Thus we check that the
 * firmware/kernel still boots at least to a certain point and so we
 * know that the machine is not completely broken.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "ppc-util.h"

static const uint8_t bios_avr[] = {
    0x88, 0xe0,
    0x80, 0x93, 0xc1, 0x00,
    0x86, 0xe0,
    0x80, 0x93, 0xc2, 0x00,
    0x84, 0xe5,
    0x80, 0x93, 0xc6, 0x00,
};

static const uint8_t bios_loongarch64[] = {
    0x0c, 0xc0, 0x3f, 0x14,
    0x8c, 0x81, 0x87, 0x03,
    0x0d, 0x50, 0x81, 0x03,
    0x8d, 0x01, 0x00, 0x29,
    0xff, 0xf3, 0xff, 0x53,
};

static const uint8_t kernel_mcf5208[] = {
    0x41, 0xf9, 0xfc, 0x06, 0x00, 0x00,
    0x10, 0x3c, 0x00, 0x54,
    0x11, 0x7c, 0x00, 0x04, 0x00, 0x08,
    0x11, 0x40, 0x00, 0x0c,
    0x60, 0xfa
};

static const uint8_t bios_nextcube[] = {
    0x06, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x08,
    0x41, 0xf9, 0x02, 0x11, 0x80, 0x00,
    0x10, 0x3c, 0x00, 0x54,
    0x11, 0x7c, 0x00, 0x05, 0x00, 0x01,
    0x11, 0x7c, 0x00, 0x68, 0x00, 0x01,
    0x11, 0x40, 0x00, 0x03,
    0x60, 0xfa
};

static const uint8_t kernel_pls3adsp1800[] = {
    0xb0, 0x00, 0x84, 0x00,
    0x30, 0x60, 0x00, 0x04,
    0x30, 0x80, 0x00, 0x54,
    0xf0, 0x83, 0x00, 0x00,
    0xb8, 0x00, 0xff, 0xfc
};

static const uint8_t kernel_plml605[] = {
    0xe0, 0x83, 0x00, 0xb0,
    0x00, 0x10, 0x60, 0x30,
    0x54, 0x00, 0x80, 0x30,
    0x00, 0x00, 0x83, 0xf0,
    0xfc, 0xff, 0x00, 0xb8
};

static const uint8_t bios_raspi2[] = {
    0x10, 0x30, 0x9f, 0xe5,
    0x10, 0x20, 0x9f, 0xe5,
    0xb0, 0x23, 0xc3, 0xe1,
    0x54, 0x20, 0xa0, 0xe3,
    0x00, 0x20, 0xc3, 0xe5,
    0xff, 0xff, 0xff, 0xea,
    0x00, 0x10, 0x20, 0x3f,
    0x01, 0x01, 0x00, 0x00,
};

static const uint8_t kernel_aarch64[] = {
    0x02, 0x20, 0xa1, 0xd2,
    0x21, 0x20, 0x80, 0x52,
    0x41, 0x60, 0x00, 0x79,
    0x81, 0x0a, 0x80, 0x52,
    0x41, 0x00, 0x00, 0x39,
    0xff, 0xff, 0xff, 0x17,
};

static const uint8_t kernel_nrf51[] = {
    0x00, 0x00, 0x00, 0x00,
    0x09, 0x00, 0x00, 0x00,
    0x04, 0x4a,
    0x04, 0x21,
    0x11, 0x60,
    0x04, 0x4a,
    0x01, 0x21,
    0x11, 0x60,
    0x03, 0x4a,
    0x54, 0x21,
    0x11, 0x60,
    0xfe, 0xe7,
    0x00, 0x25, 0x00, 0x40,
    0x08, 0x20, 0x00, 0x40,
    0x1c, 0x25, 0x00, 0x40
};

static const uint8_t kernel_stm32vldiscovery[] = {
    0x00, 0x00, 0x00, 0x00,
    0x1d, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x0b, 0x4b,
    0x44, 0xf2, 0x04, 0x02,
    0x1a, 0x60,
    0x0a, 0x4b,
    0x1a, 0x68,
    0x22, 0xf0, 0xf0, 0x02,
    0x1a, 0x60,
    0x1a, 0x68,
    0x42, 0xf0, 0xb0, 0x02,
    0x1a, 0x60,
    0x07, 0x4b,
    0x45, 0x22,
    0x1a, 0x60,
    0x06, 0x4b,
    0x42, 0xf2, 0x08, 0x02,
    0x1a, 0x60,
    0x05, 0x4b,
    0x54, 0x22,
    0x1a, 0x60,
    0xfe, 0xe7,
    0x18, 0x10, 0x02, 0x40,
    0x04, 0x08, 0x01, 0x40,
    0x08, 0x38, 0x01, 0x40,
    0x0c, 0x38, 0x01, 0x40,
    0x04, 0x38, 0x01, 0x40
};

typedef struct testdef {
    const char *arch;
    const char *machine;
    const char *extra;
    const char *expect;
    size_t codesize;
    const uint8_t *kernel;
    const uint8_t *bios;
} testdef_t;

static const testdef_t tests[] = {
    { "alpha", "clipper", "", "PCI:" },
    { "avr", "arduino-duemilanove", "", "T", sizeof(bios_avr), NULL, bios_avr },
    { "avr", "arduino-mega-2560-v3", "", "T", sizeof(bios_avr), NULL, bios_avr},
    { "ppc", "ppce500", "", "U-Boot" },
    { "ppc", "40p", "-vga none -boot d", "Trying cd:," },
    { "ppc", "g3beige", "", "PowerPC,750" },
    { "ppc", "mac99", "", "PowerPC,G4" },
    { "ppc", "sam460ex", "-m 256", "DRAM:  256 MiB" },
    { "ppc64", "ppce500", "", "U-Boot" },
    { "ppc64", "40p", "-m 192", "Memory: 192M" },
    { "ppc64", "mac99", "", "PowerPC,970FX" },
    { "ppc64", "pseries",
      "-machine " PSERIES_DEFAULT_CAPABILITIES,
      "Open Firmware" },
    { "ppc64", "powernv8", "", "OPAL" },
    { "ppc64", "powernv9", "", "OPAL" },
    { "ppc64", "sam460ex", "-device pci-bridge,chassis_nr=2", "1b36  0001" },
    { "i386", "isapc", "-cpu qemu32 -M graphics=off", "SeaBIOS" },
    { "i386", "pc", "-M graphics=off", "SeaBIOS" },
    { "i386", "q35", "-M graphics=off", "SeaBIOS" },
    { "x86_64", "isapc", "-cpu qemu32 -M graphics=off", "SeaBIOS" },
    { "x86_64", "q35", "-M graphics=off", "SeaBIOS" },
    { "sparc", "LX", "", "TMS390S10" },
    { "sparc", "SS-4", "", "MB86904" },
    { "sparc", "SS-600MP", "", "TMS390Z55" },
    { "sparc64", "sun4u", "", "UltraSPARC" },
    { "s390x", "s390-ccw-virtio", "", "device" },
    { "loongarch64", "virt", "-cpu max", "TT", sizeof(bios_loongarch64),
      NULL, bios_loongarch64 },
    { "m68k", "mcf5208evb", "", "TT", sizeof(kernel_mcf5208), kernel_mcf5208 },
    { "m68k", "next-cube", "", "TT", sizeof(bios_nextcube), 0, bios_nextcube },
    { "microblaze", "petalogix-s3adsp1800", "", "TT",
      sizeof(kernel_pls3adsp1800), kernel_pls3adsp1800 },
    { "microblazeel", "petalogix-ml605", "", "TT",
      sizeof(kernel_plml605), kernel_plml605 },
    { "arm", "raspi2b", "", "TT", sizeof(bios_raspi2), 0, bios_raspi2 },
    { "aarch64", "virt", "-cpu max", "TT", sizeof(kernel_aarch64),
      kernel_aarch64 },
    { "arm", "microbit", "", "T", sizeof(kernel_nrf51), kernel_nrf51 },
    { "arm", "stm32vldiscovery", "", "T",
      sizeof(kernel_stm32vldiscovery), kernel_stm32vldiscovery },

    { NULL }
};

static bool check_guest_output(QTestState *qts, const testdef_t *test, int fd)
{
    int nbr = 0, pos = 0, ccnt;
    time_t now, start = time(NULL);
    char ch;

    while (1) {
        ccnt = 0;
        while (ccnt++ < 512 && (nbr = read(fd, &ch, 1)) == 1) {
            if (ch == test->expect[pos]) {
                pos += 1;
                if (test->expect[pos] == '\0') {
                    return true;
                }
            } else {
                pos = 0;
            }
        }
        g_assert(nbr >= 0);
        if (!qtest_probe_child(qts)) {
            break;
        }
        now = time(NULL);
        if (now - start >= 360) {
            break;
        }
        g_usleep(10000);
    }

    return false;
}

static void test_machine(const void *data)
{
    const testdef_t *test = data;
    g_autofree char *serialtmp = NULL;
    g_autofree char *codetmp = NULL;
    const char *codeparam = "";
    const uint8_t *code = NULL;
    QTestState *qts;
    int ser_fd;

    ser_fd = g_file_open_tmp("qtest-boot-serial-sXXXXXX", &serialtmp, NULL);
    g_assert(ser_fd != -1);
    close(ser_fd);

    if (test->kernel) {
        code = test->kernel;
        codeparam = "-kernel";
    } else if (test->bios) {
        code = test->bios;
        codeparam = "-bios";
    }

    if (code) {
        ssize_t wlen;
        int code_fd;

        code_fd = g_file_open_tmp("qtest-boot-serial-cXXXXXX", &codetmp, NULL);
        g_assert(code_fd != -1);
        wlen = write(code_fd, code, test->codesize);
        g_assert(wlen == test->codesize);
        close(code_fd);
    }

    qts = qtest_initf("%s %s -M %s -no-shutdown "
                      "-chardev file,id=serial0,path=%s "
                      "-serial chardev:serial0 -accel tcg -accel kvm %s",
                      codeparam, code ? codetmp : "", test->machine,
                      serialtmp, test->extra);
    if (code) {
        unlink(codetmp);
    }

    ser_fd = open(serialtmp, O_RDONLY);
    g_assert(ser_fd != -1);
    if (!check_guest_output(qts, test, ser_fd)) {
        g_error("Failed to find expected string. Please check '%s'",
                serialtmp);
    }
    unlink(serialtmp);

    qtest_quit(qts);

    close(ser_fd);
}

int main(int argc, char *argv[])
{
    const char *arch = qtest_get_arch();
    int i;

    g_test_init(&argc, &argv, NULL);

    if (!qtest_has_accel("tcg") && !qtest_has_accel("kvm")) {
        g_test_skip("No KVM or TCG accelerator available");
        return 0;
    }

    for (i = 0; tests[i].arch != NULL; i++) {
        if (g_str_equal(arch, tests[i].arch) &&
            qtest_has_machine(tests[i].machine)) {
            char *name = g_strdup_printf("boot-serial/%s", tests[i].machine);
            qtest_add_data_func(name, &tests[i], test_machine);
            g_free(name);
        }
    }

    return g_test_run();
}
