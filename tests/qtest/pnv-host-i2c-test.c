/*
 * QTest testcase for PowerNV 10 Host I2C Communications
 *
 * Copyright (c) 2023, IBM Corporation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later. See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "libqtest.h"
#include "hw/gpio/pca9554_regs.h"
#include "hw/gpio/pca9552_regs.h"
#include "pnv-xscom.h"

#define PPC_BIT(bit)            (0x8000000000000000ULL >> (bit))
#define PPC_BIT32(bit)          (0x80000000 >> (bit))
#define PPC_BIT8(bit)           (0x80 >> (bit))
#define PPC_BITMASK(bs, be)     ((PPC_BIT(bs) - PPC_BIT(be)) | PPC_BIT(bs))
#define PPC_BITMASK32(bs, be)   ((PPC_BIT32(bs) - PPC_BIT32(be)) | \
                                 PPC_BIT32(bs))

#define MASK_TO_LSH(m)          (__builtin_ffsll(m) - 1)
#define GETFIELD(m, v)          (((v) & (m)) >> MASK_TO_LSH(m))
#define SETFIELD(m, v, val) \
        (((v) & ~(m)) | ((((typeof(v))(val)) << MASK_TO_LSH(m)) & (m)))

#define PNV10_XSCOM_I2CM_BASE   0xa0000
#define PNV10_XSCOM_I2CM_SIZE   0x1000

#include "hw/i2c/pnv_i2c_regs.h"

typedef struct {
    QTestState    *qts;
    const PnvChip *chip;
    int           engine;
} PnvI2cCtlr;

typedef struct {
    PnvI2cCtlr  *ctlr;
    int         port;
    uint8_t     addr;
} PnvI2cDev;


static uint64_t pnv_i2c_xscom_addr(PnvI2cCtlr *ctlr, uint32_t reg)
{
    return pnv_xscom_addr(ctlr->chip, PNV10_XSCOM_I2CM_BASE +
                          (PNV10_XSCOM_I2CM_SIZE * ctlr->engine) + reg);
}

static uint64_t pnv_i2c_xscom_read(PnvI2cCtlr *ctlr, uint32_t reg)
{
    return qtest_readq(ctlr->qts, pnv_i2c_xscom_addr(ctlr, reg));
}

static void pnv_i2c_xscom_write(PnvI2cCtlr *ctlr, uint32_t reg, uint64_t val)
{
    qtest_writeq(ctlr->qts, pnv_i2c_xscom_addr(ctlr, reg), val);
}

static void pnv_i2c_send(PnvI2cDev *dev, const uint8_t *buf, uint16_t len)
{
    int byte_num;
    uint64_t reg64;

    reg64 = SETFIELD(I2C_MODE_BIT_RATE_DIV, 0ull, 0x2be);
    reg64 = SETFIELD(I2C_MODE_PORT_NUM, reg64, dev->port);
    pnv_i2c_xscom_write(dev->ctlr, I2C_MODE_REG, reg64);

    reg64 = pnv_i2c_xscom_read(dev->ctlr, I2C_EXTD_STAT_REG);
    g_assert_cmphex(reg64 & I2C_EXTD_STAT_I2C_BUSY, ==, 0);
    reg64 = pnv_i2c_xscom_read(dev->ctlr, I2C_STAT_REG);
    g_assert_cmphex(reg64 & (I2C_STAT_ANY_ERR | I2C_STAT_CMD_COMP), ==,
                    I2C_STAT_CMD_COMP);

    reg64 = I2C_CMD_WITH_START | I2C_CMD_WITH_ADDR | I2C_CMD_WITH_STOP;
    reg64 = SETFIELD(I2C_CMD_DEV_ADDR, reg64, dev->addr);
    reg64 = SETFIELD(I2C_CMD_LEN_BYTES, reg64, len);
    pnv_i2c_xscom_write(dev->ctlr, I2C_CMD_REG, reg64);

    reg64 = pnv_i2c_xscom_read(dev->ctlr, I2C_STAT_REG);
    g_assert_cmphex(reg64 & I2C_STAT_ANY_ERR, ==, 0);

    for (byte_num = 0; byte_num < len; byte_num++) {
        reg64 = SETFIELD(I2C_FIFO, 0ull, buf[byte_num]);
        pnv_i2c_xscom_write(dev->ctlr, I2C_FIFO_REG, reg64);
    }

    reg64 = pnv_i2c_xscom_read(dev->ctlr, I2C_EXTD_STAT_REG);
    g_assert_cmphex(reg64 & I2C_EXTD_STAT_I2C_BUSY, ==, 0);
    reg64 = pnv_i2c_xscom_read(dev->ctlr, I2C_STAT_REG);
    g_assert_cmphex(reg64 & (I2C_STAT_ANY_ERR | I2C_STAT_CMD_COMP), ==,
                    I2C_STAT_CMD_COMP);
}

static void pnv_i2c_recv(PnvI2cDev *dev, uint8_t *buf, uint16_t len)
{
    int byte_num;
    uint64_t reg64;

    reg64 = SETFIELD(I2C_MODE_BIT_RATE_DIV, 0ull, 0x2be);
    reg64 = SETFIELD(I2C_MODE_PORT_NUM, reg64, dev->port);
    pnv_i2c_xscom_write(dev->ctlr, I2C_MODE_REG, reg64);

    reg64 = pnv_i2c_xscom_read(dev->ctlr, I2C_EXTD_STAT_REG);
    g_assert_cmphex(reg64 & I2C_EXTD_STAT_I2C_BUSY, ==, 0);
    reg64 = pnv_i2c_xscom_read(dev->ctlr, I2C_STAT_REG);
    g_assert_cmphex(reg64 & (I2C_STAT_ANY_ERR | I2C_STAT_CMD_COMP), ==,
                    I2C_STAT_CMD_COMP);

    reg64 = I2C_CMD_WITH_START | I2C_CMD_WITH_ADDR |
            I2C_CMD_WITH_STOP | I2C_CMD_READ_NOT_WRITE;
    reg64 = SETFIELD(I2C_CMD_DEV_ADDR, reg64, dev->addr);
    reg64 = SETFIELD(I2C_CMD_LEN_BYTES, reg64, len);
    pnv_i2c_xscom_write(dev->ctlr, I2C_CMD_REG, reg64);

    reg64 = pnv_i2c_xscom_read(dev->ctlr, I2C_STAT_REG);
    g_assert_cmphex(reg64 & I2C_STAT_ANY_ERR, ==, 0);

    for (byte_num = 0; byte_num < len; byte_num++) {
        reg64 = pnv_i2c_xscom_read(dev->ctlr, I2C_FIFO_REG);
        buf[byte_num] = GETFIELD(I2C_FIFO, reg64);
    }

    reg64 = pnv_i2c_xscom_read(dev->ctlr, I2C_EXTD_STAT_REG);
    g_assert_cmphex(reg64 & I2C_EXTD_STAT_I2C_BUSY, ==, 0);
    reg64 = pnv_i2c_xscom_read(dev->ctlr, I2C_STAT_REG);
    g_assert_cmphex(reg64 & (I2C_STAT_ANY_ERR | I2C_STAT_CMD_COMP), ==,
                    I2C_STAT_CMD_COMP);
}

static void pnv_i2c_pca9554_default_cfg(PnvI2cDev *dev)
{
    uint8_t buf[2];

    buf[0] = PCA9554_POLARITY;
    buf[1] = 0;
    pnv_i2c_send(dev, buf, 2);

    buf[0] = PCA9554_CONFIG;
    buf[1] = 0xff;
    pnv_i2c_send(dev, buf, 2);

    buf[0] = PCA9554_OUTPUT;
    buf[1] = 0xff;
    pnv_i2c_send(dev, buf, 2);
}

static void pnv_i2c_pca9554_set_pin(PnvI2cDev *dev, int pin, bool high)
{
    uint8_t send_buf[2];
    uint8_t recv_buf[2];
    uint8_t mask = 0x1 << pin;
    uint8_t new_value = ((high) ? 1 : 0) << pin;

    send_buf[0] = PCA9554_OUTPUT;
    pnv_i2c_send(dev, send_buf, 1);
    pnv_i2c_recv(dev, recv_buf, 1);

    send_buf[1] = (recv_buf[0] & ~mask) | new_value;
    pnv_i2c_send(dev, send_buf, 2);

    send_buf[0] = PCA9554_CONFIG;
    pnv_i2c_send(dev, send_buf, 1);
    pnv_i2c_recv(dev, recv_buf, 1);
    send_buf[1] = recv_buf[0] & ~mask;
    pnv_i2c_send(dev, send_buf, 2);
}

static uint8_t pnv_i2c_pca9554_read_pins(PnvI2cDev *dev)
{
    uint8_t send_buf[1];
    uint8_t recv_buf[1];
    send_buf[0] = PCA9554_INPUT;
    pnv_i2c_send(dev, send_buf, 1);
    pnv_i2c_recv(dev, recv_buf, 1);
    return recv_buf[0];
}

static void pnv_i2c_pca9554_flip_polarity(PnvI2cDev *dev)
{
    uint8_t recv_buf[1];
    uint8_t send_buf[2];

    send_buf[0] = PCA9554_POLARITY;
    pnv_i2c_send(dev, send_buf, 1);
    pnv_i2c_recv(dev, recv_buf, 1);
    send_buf[1] = recv_buf[0] ^ 0xff;
    pnv_i2c_send(dev, send_buf, 2);
}

static void pnv_i2c_pca9554_default_inputs(PnvI2cDev *dev)
{
    uint8_t pin_values = pnv_i2c_pca9554_read_pins(dev);
    g_assert_cmphex(pin_values, ==, 0xff);
}

static void pnv_i2c_pca554_set_pins(PnvI2cDev *dev)
{
    uint8_t pin_values;
    pnv_i2c_pca9554_set_pin(dev, 0, 0);
    pin_values = pnv_i2c_pca9554_read_pins(dev);
    g_assert_cmphex(pin_values, ==, 0xfe);
    pnv_i2c_pca9554_flip_polarity(dev);
    pin_values = pnv_i2c_pca9554_read_pins(dev);
    g_assert_cmphex(pin_values, ==, 0x01);
    pnv_i2c_pca9554_set_pin(dev, 2, 0);
    pin_values = pnv_i2c_pca9554_read_pins(dev);
    g_assert_cmphex(pin_values, ==, 0x05);
    pnv_i2c_pca9554_flip_polarity(dev);
    pin_values = pnv_i2c_pca9554_read_pins(dev);
    g_assert_cmphex(pin_values, ==, 0xfa);
    pnv_i2c_pca9554_default_cfg(dev);
    pin_values = pnv_i2c_pca9554_read_pins(dev);
    g_assert_cmphex(pin_values, ==, 0xff);
}

static void pnv_i2c_pca9552_default_cfg(PnvI2cDev *dev)
{
    uint8_t buf[2];
    buf[0] = PCA9552_PSC0;
    buf[1] = 0xff;
    pnv_i2c_send(dev, buf, 2);
    buf[0] = PCA9552_PWM0;
    buf[1] = 0x80;
    pnv_i2c_send(dev, buf, 2);
    buf[0] = PCA9552_PSC1;
    buf[1] = 0xff;
    pnv_i2c_send(dev, buf, 2);
    buf[0] = PCA9552_PWM1;
    buf[1] = 0x80;
    pnv_i2c_send(dev, buf, 2);

    buf[0] = PCA9552_LS0;
    buf[1] = 0x55;
    pnv_i2c_send(dev, buf, 2);
    buf[0] = PCA9552_LS1;
    buf[1] = 0x55;
    pnv_i2c_send(dev, buf, 2);
    buf[0] = PCA9552_LS2;
    buf[1] = 0x55;
    pnv_i2c_send(dev, buf, 2);
    buf[0] = PCA9552_LS3;
    buf[1] = 0x55;
    pnv_i2c_send(dev, buf, 2);
}

static void pnv_i2c_pca9552_set_pin(PnvI2cDev *dev, int pin, bool high)
{
    uint8_t send_buf[2];
    uint8_t recv_buf[2];
    uint8_t reg = PCA9552_LS0 + (pin / 4);
    uint8_t shift = (pin % 4) * 2;
    uint8_t mask = ~(0x3 << shift);
    uint8_t new_value = ((high) ? 1 : 0) << shift;

    send_buf[0] = reg;
    pnv_i2c_send(dev, send_buf, 1);
    pnv_i2c_recv(dev, recv_buf, 1);

    send_buf[1] = (recv_buf[0] & mask) | new_value;
    pnv_i2c_send(dev, send_buf, 2);
}

static uint16_t pnv_i2c_pca9552_read_pins(PnvI2cDev *dev)
{
    uint8_t send_buf[2];
    uint8_t recv_buf[2];
    uint16_t inputs;
    send_buf[0] = PCA9552_INPUT0;
    pnv_i2c_send(dev, send_buf, 1);
    pnv_i2c_recv(dev, recv_buf, 1);
    inputs = recv_buf[0];
    send_buf[0] = PCA9552_INPUT1;
    pnv_i2c_send(dev, send_buf, 1);
    pnv_i2c_recv(dev, recv_buf, 1);
    inputs |= recv_buf[0] << 8;
    return inputs;
}

static void pnv_i2c_pca9552_default_inputs(PnvI2cDev *dev)
{
    uint16_t pin_values = pnv_i2c_pca9552_read_pins(dev);
    g_assert_cmphex(pin_values, ==, 0xffff);
}

static void pnv_i2c_pca552_set_pins(PnvI2cDev *dev)
{
    uint16_t pin_values;

    pnv_i2c_pca9552_set_pin(dev, 0, 0);
    pin_values = pnv_i2c_pca9552_read_pins(dev);

    g_assert_cmphex(pin_values, ==, 0xffde);

    pnv_i2c_pca9552_set_pin(dev, 1, 0);
    pin_values = pnv_i2c_pca9552_read_pins(dev);

    g_assert_cmphex(pin_values, ==, 0xff9c);

    pnv_i2c_pca9552_set_pin(dev, 2, 0);
    pin_values = pnv_i2c_pca9552_read_pins(dev);

    g_assert_cmphex(pin_values, ==, 0xff18);

    pnv_i2c_pca9552_set_pin(dev, 3, 0);
    pin_values = pnv_i2c_pca9552_read_pins(dev);

    g_assert_cmphex(pin_values, ==, 0xfe10);

    pnv_i2c_pca9552_set_pin(dev, 4, 0);
    pin_values = pnv_i2c_pca9552_read_pins(dev);

    g_assert_cmphex(pin_values, ==, 0xfc00);

    pnv_i2c_pca9552_default_cfg(dev);
    pin_values = pnv_i2c_pca9552_read_pins(dev);

    g_assert_cmphex(pin_values, ==, 0xffff);
}

static void reset_engine(PnvI2cCtlr *ctlr)
{
    pnv_i2c_xscom_write(ctlr, I2C_RESET_I2C_REG, 0);
}

static void check_i2cm_por_regs(QTestState *qts, const PnvChip *chip)
{
    int engine;
    for (engine = 0; engine < chip->num_i2c; engine++) {
        PnvI2cCtlr ctlr;
        ctlr.qts = qts;
        ctlr.chip = chip;
        ctlr.engine = engine;

        uint64_t value = pnv_i2c_xscom_read(&ctlr, I2C_EXTD_STAT_REG);
        g_assert_cmphex(value & I2C_EXTD_STAT_I2C_VERSION, ==, 0x1700000000);

        value = pnv_i2c_xscom_read(&ctlr, I2C_STAT_REG);
        g_assert_cmphex(value & (I2C_STAT_ANY_ERR | I2C_STAT_CMD_COMP),
                        ==,
                        I2C_STAT_CMD_COMP);
    }
}

static void reset_all(QTestState *qts, const PnvChip *chip)
{
    int engine;
    for (engine = 0; engine < chip->num_i2c; engine++) {
        PnvI2cCtlr ctlr;
        ctlr.qts = qts;
        ctlr.chip = chip;
        ctlr.engine = engine;
        reset_engine(&ctlr);
        pnv_i2c_xscom_write(&ctlr, I2C_MODE_REG, 0x02be040000000000);
    }
}

static void test_host_i2c(const void *data)
{
    const PnvChip *chip = data;
    QTestState *qts;
    const char *machine = "powernv8";
    PnvI2cCtlr ctlr;
    PnvI2cDev pca9552;
    PnvI2cDev pca9554;

    if (chip->chip_type == PNV_CHIP_POWER9) {
        machine = "powernv9";
    } else if (chip->chip_type == PNV_CHIP_POWER10) {
        machine = "powernv10-rainier";
    }

    qts = qtest_initf("-M %s -smp %d,cores=1,threads=%d -nographic "
                      "-nodefaults -serial mon:stdio -S "
                      "-d guest_errors",
                      machine, SMT, SMT);

    check_i2cm_por_regs(qts, chip);

    reset_all(qts, chip);

    check_i2cm_por_regs(qts, chip);

    if (chip->chip_type != PNV_CHIP_POWER10) {
        qtest_quit(qts);
        return;
    }

    ctlr.qts = qts;
    ctlr.chip = chip;
    ctlr.engine = 2;
    pca9552.ctlr = &ctlr;
    pca9552.port = 1;
    pca9552.addr = 0x63;

    pnv_i2c_pca9552_default_cfg(&pca9552);

    pnv_i2c_pca9552_default_inputs(&pca9552);

    pnv_i2c_pca552_set_pins(&pca9552);

    pca9554.ctlr = &ctlr;
    pca9554.port = 1;
    pca9554.addr = 0x25;

    pnv_i2c_pca9554_default_cfg(&pca9554);

    pnv_i2c_pca9554_default_inputs(&pca9554);

    pnv_i2c_pca554_set_pins(&pca9554);

    qtest_quit(qts);
}

static void add_test(const char *name, void (*test)(const void *data))
{
    int i;

    for (i = 0; i < ARRAY_SIZE(pnv_chips); i++) {
        char *tname = g_strdup_printf("pnv-xscom/%s/%s", name,
                                      pnv_chips[i].cpu_model);
        qtest_add_data_func(tname, &pnv_chips[i], test);
        g_free(tname);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    add_test("host-i2c", test_host_i2c);
    return g_test_run();
}
