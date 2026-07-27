/*
 * QEMU PowerPC PowerNV Emulation of some ChipTOD behaviour
 *
 * Copyright (c) 2022-2023, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * ChipTOD (aka TOD) is a facility implemented in the nest / pervasive. The
 * purpose is to keep time-of-day across chips and cores.
 *
 * There is a master chip TOD, which sends signals to slave chip TODs to
 * keep them synchronized. There are two sets of configuration registers
 * called primary and secondary, which can be used fail over.
 *
 * The chip TOD also distributes synchronisation signals to the timebase
 * facility in each of the cores on the chip. In particular there is a
 * feature that can move the TOD value in the ChipTOD to and from the TB.
 *
 * Initialisation typically brings all ChipTOD into sync (see tod_state),
 * and then brings each core TB into sync with the ChipTODs (see timebase
 * state and TFMR). This model is a very basic simulation of the init sequence
 * performed by skiboot.
 */

#include "qemu/osdep.h"
#include "system/reset.h"
#include "target/ppc/cpu.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/ppc/fdt.h"
#include "hw/ppc/ppc.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_chip.h"
#include "hw/ppc/pnv_core.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/ppc/pnv_chiptod.h"
#include "trace.h"

#include <libfdt.h>

#define TOD_M_PATH_CTRL_REG             0x00000000
#define TOD_PRI_PORT_0_CTRL_REG         0x00000001
#define TOD_PRI_PORT_1_CTRL_REG         0x00000002
#define TOD_SEC_PORT_0_CTRL_REG         0x00000003
#define TOD_SEC_PORT_1_CTRL_REG         0x00000004
#define TOD_S_PATH_CTRL_REG             0x00000005
#define TOD_I_PATH_CTRL_REG             0x00000006

#define TOD_PSS_MSS_CTRL_REG            0x00000007

#define TOD_PSS_MSS_STATUS_REG          0x00000008

#define TOD_CHIP_CTRL_REG               0x00000010

#define TOD_TX_TTYPE_0_REG              0x00000011
#define TOD_TX_TTYPE_1_REG              0x00000012
#define TOD_TX_TTYPE_2_REG              0x00000013
#define TOD_TX_TTYPE_3_REG              0x00000014
#define TOD_TX_TTYPE_4_REG              0x00000015
#define TOD_TX_TTYPE_5_REG              0x00000016

#define TOD_MOVE_TOD_TO_TB_REG          0x00000017
#define TOD_LOAD_TOD_MOD_REG            0x00000018
#define TOD_LOAD_TOD_REG                0x00000021
#define TOD_START_TOD_REG               0x00000022
#define TOD_FSM_REG                     0x00000024

#define TOD_TX_TTYPE_CTRL_REG           0x00000027
#define   TOD_TX_TTYPE_PIB_SLAVE_ADDR      PPC_BITMASK(26, 31)

#define TOD_ERROR_REG                   0x00000030

#define   PC_TOD                        0x4A3

static uint64_t pnv_chiptod_xscom_read(void *opaque, hwaddr addr,
                                          unsigned size)
{
    PnvChipTOD *chiptod = PNV_CHIPTOD(opaque);
    uint32_t offset = addr >> 3;
    uint64_t val = 0;

    switch (offset) {
    case TOD_PSS_MSS_STATUS_REG:
        val |= PPC_BITMASK(6, 10);
        val |= PPC_BIT(12);
        if (chiptod->tod_state == tod_running) {
            val |= PPC_BIT(20);
        }
        val |= PPC_BIT(21);
        val |= PPC_BIT(26);

        if (chiptod->primary) {
            val |= PPC_BIT(23);
        } else if (chiptod->secondary) {
            val |= PPC_BIT(24);
        } else {
            val |= PPC_BIT(25);
        }
        break;
    case TOD_PSS_MSS_CTRL_REG:
        val = chiptod->pss_mss_ctrl_reg;
        break;
    case TOD_TX_TTYPE_CTRL_REG:
        val = 0;
        break;
    case TOD_ERROR_REG:
        val = chiptod->tod_error;
        break;
    case TOD_FSM_REG:
        if (chiptod->tod_state == tod_running) {
            val |= PPC_BIT(4);
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "pnv_chiptod: unimplemented register: Ox%"
                      HWADDR_PRIx "\n", addr >> 3);
    }

    trace_pnv_chiptod_xscom_read(addr >> 3, val);

    return val;
}

static void chiptod_receive_ttype(PnvChipTOD *chiptod, uint32_t trigger)
{
    switch (trigger) {
    case TOD_TX_TTYPE_4_REG:
        if (chiptod->tod_state != tod_not_set) {
            qemu_log_mask(LOG_GUEST_ERROR, "pnv_chiptod: received TTYPE4 in "
                          " state %d, should be in 7 (TOD_NOT_SET)\n",
                          chiptod->tod_state);
        } else {
            chiptod->tod_state = tod_running;
        }
        break;
    case TOD_TX_TTYPE_5_REG:
        chiptod->tod_state = tod_not_set;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "pnv_chiptod: received unimplemented "
                      " TTYPE %u\n", trigger);
        break;
    }
}

static void chiptod_power9_broadcast_ttype(PnvChipTOD *sender,
                                            uint32_t trigger)
{
    PnvMachineState *pnv = PNV_MACHINE(qdev_get_machine());
    int i;

    for (i = 0; i < pnv->num_chips; i++) {
        Pnv9Chip *chip9 = PNV9_CHIP(pnv->chips[i]);
        PnvChipTOD *chiptod = &chip9->chiptod;

        if (chiptod != sender) {
            chiptod_receive_ttype(chiptod, trigger);
        }
    }
}

static void chiptod_power10_broadcast_ttype(PnvChipTOD *sender,
                                            uint32_t trigger)
{
    PnvMachineState *pnv = PNV_MACHINE(qdev_get_machine());
    int i;

    for (i = 0; i < pnv->num_chips; i++) {
        Pnv10Chip *chip10 = PNV10_CHIP(pnv->chips[i]);
        PnvChipTOD *chiptod = &chip10->chiptod;

        if (chiptod != sender) {
            chiptod_receive_ttype(chiptod, trigger);
        }
    }
}

static void chiptod_power11_broadcast_ttype(PnvChipTOD *sender,
                                            uint32_t trigger)
{
    PnvMachineState *pnv = PNV_MACHINE(qdev_get_machine());
    int i;

    for (i = 0; i < pnv->num_chips; i++) {
        Pnv11Chip *chip11 = PNV11_CHIP(pnv->chips[i]);
        PnvChipTOD *chiptod = &chip11->chiptod;

        if (chiptod != sender) {
            chiptod_receive_ttype(chiptod, trigger);
        }
    }
}

static PnvCore *pnv_chip_get_core_by_xscom_base(PnvChip *chip,
                                                uint32_t xscom_base)
{
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(chip);
    int i;

    for (i = 0; i < chip->nr_cores; i++) {
        PnvCore *pc = chip->cores[i];
        CPUCore *cc = CPU_CORE(pc);
        int core_hwid = cc->core_id;

        if (pcc->xscom_core_base(chip, core_hwid) == xscom_base) {
            return pc;
        }
    }
    return NULL;
}

static PnvCore *chiptod_power9_tx_ttype_target(PnvChipTOD *chiptod,
                                               uint64_t val)
{
    if (val & PPC_BIT(35)) {
        uint32_t addr = val >> 32;
        uint32_t reg = addr & 0xfff;

        if (reg != PC_TOD) {
            qemu_log_mask(LOG_GUEST_ERROR, "pnv_chiptod: SCOM addressing: "
                          "unimplemented slave register 0x%" PRIx32 "\n", reg);
            return NULL;
        }

        return pnv_chip_get_core_by_xscom_base(chiptod->chip, addr & ~0xfff);

    } else {
        uint32_t core_id = GETFIELD(TOD_TX_TTYPE_PIB_SLAVE_ADDR, val) & 0x1f;
        return pnv_chip_find_core(chiptod->chip, core_id);
    }
}

static PnvCore *chiptod_power10_tx_ttype_target(PnvChipTOD *chiptod,
                                               uint64_t val)
{
    if (val & PPC_BIT(35)) {
        uint32_t addr = val >> 32;
        uint32_t reg = addr & 0xfff;

        if (reg != PC_TOD) {
            qemu_log_mask(LOG_GUEST_ERROR, "pnv_chiptod: SCOM addressing: "
                          "unimplemented slave register 0x%" PRIx32 "\n", reg);
            return NULL;
        }

        return pnv_chip_get_core_by_xscom_base(chiptod->chip, addr & ~0xfff);

    } else {
        qemu_log_mask(LOG_UNIMP, "pnv_chiptod: TX TTYPE Core ID "
                      "addressing is not implemented for POWER10\n");
        return NULL;
    }
}

static PnvCore *chiptod_power11_tx_ttype_target(PnvChipTOD *chiptod,
                                               uint64_t val)
{
    return chiptod_power10_tx_ttype_target(chiptod, val);
}

static void pnv_chiptod_xscom_write(void *opaque, hwaddr addr,
                                    uint64_t val, unsigned size)
{
    PnvChipTOD *chiptod = PNV_CHIPTOD(opaque);
    PnvChipTODClass *pctc = PNV_CHIPTOD_GET_CLASS(chiptod);
    uint32_t offset = addr >> 3;

    trace_pnv_chiptod_xscom_write(addr >> 3, val);

    switch (offset) {
    case TOD_PSS_MSS_CTRL_REG:
        if (chiptod->primary) {
            val |= PPC_BIT(1);
        } else {
            val &= ~PPC_BIT(1);
        }
        val |= PPC_BIT(2);
        chiptod->pss_mss_ctrl_reg = val & PPC_BITMASK(0, 31);
        break;

    case TOD_TX_TTYPE_CTRL_REG:
        chiptod->slave_pc_target = pctc->tx_ttype_target(chiptod, val);
        if (!chiptod->slave_pc_target) {
            qemu_log_mask(LOG_GUEST_ERROR, "pnv_chiptod: xscom write reg"
                          " TOD_TX_TTYPE_CTRL_REG val 0x%" PRIx64
                          " invalid slave address\n", val);
        }
        break;
    case TOD_ERROR_REG:
        chiptod->tod_error &= ~val;
        break;
    case TOD_LOAD_TOD_MOD_REG:
        if (!(val & PPC_BIT(0))) {
            qemu_log_mask(LOG_GUEST_ERROR, "pnv_chiptod: xscom write reg"
                          " TOD_LOAD_TOD_MOD_REG with bad val 0x%" PRIx64"\n",
                          val);
        } else {
            chiptod->tod_state = tod_not_set;
        }
        break;
    case TOD_LOAD_TOD_REG:
        if (chiptod->tod_state != tod_not_set) {
            qemu_log_mask(LOG_GUEST_ERROR, "pnv_chiptod: LOAD_TOG_REG in "
                          " state %d, should be in 7 (TOD_NOT_SET)\n",
                          chiptod->tod_state);
        } else {
            if (val & PPC_BIT(63)) {
                chiptod->tod_state = tod_stopped;
            } else {
                chiptod->tod_state = tod_running;
            }
        }
        break;

    case TOD_MOVE_TOD_TO_TB_REG:
        if (chiptod->tod_state != tod_running) {
            qemu_log_mask(LOG_GUEST_ERROR, "pnv_chiptod: xscom write reg"
                          " TOD_MOVE_TOD_TO_TB_REG in bad state %d\n",
                          chiptod->tod_state);
        } else if (!(val & PPC_BIT(0))) {
            qemu_log_mask(LOG_GUEST_ERROR, "pnv_chiptod: xscom write reg"
                          " TOD_MOVE_TOD_TO_TB_REG with bad val 0x%" PRIx64"\n",
                          val);
        } else if (chiptod->slave_pc_target == NULL) {
            qemu_log_mask(LOG_GUEST_ERROR, "pnv_chiptod: xscom write reg"
                          " TOD_MOVE_TOD_TO_TB_REG with no slave target\n");
        } else {
            PnvCore *pc = chiptod->slave_pc_target;

            if (pc->tod_state.tb_ready_for_tod) {
                pc->tod_state.tod_sent_to_tb = 1;
            } else {
                qemu_log_mask(LOG_GUEST_ERROR, "pnv_chiptod: xscom write reg"
                              " TOD_MOVE_TOD_TO_TB_REG with TB not ready to"
                              " receive TOD\n");
            }
        }
        break;
    case TOD_START_TOD_REG:
        if (chiptod->tod_state != tod_stopped) {
            qemu_log_mask(LOG_GUEST_ERROR, "pnv_chiptod: LOAD_TOG_REG in "
                          " state %d, should be in 1 (TOD_STOPPED)\n",
                          chiptod->tod_state);
        } else {
            chiptod->tod_state = tod_running;
        }
        break;
    case TOD_TX_TTYPE_4_REG:
    case TOD_TX_TTYPE_5_REG:
        pctc->broadcast_ttype(chiptod, offset);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "pnv_chiptod: unimplemented register: Ox%"
                      HWADDR_PRIx "\n", addr >> 3);
    }
}

static const MemoryRegionOps pnv_chiptod_xscom_ops = {
    .read = pnv_chiptod_xscom_read,
    .write = pnv_chiptod_xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static int pnv_chiptod_dt_xscom(PnvXScomInterface *dev, void *fdt,
                                int xscom_offset,
                                const char compat[], size_t compat_size)
{
    PnvChipTOD *chiptod = PNV_CHIPTOD(dev);
    g_autofree char *name = NULL;
    int offset;
    uint32_t chiptod_pcba = PNV9_XSCOM_CHIPTOD_BASE;
    uint32_t reg[] = {
        cpu_to_be32(chiptod_pcba),
        cpu_to_be32(PNV9_XSCOM_CHIPTOD_SIZE)
    };

    name = g_strdup_printf("chiptod@%x", chiptod_pcba);
    offset = fdt_add_subnode(fdt, xscom_offset, name);
    _FDT(offset);

    if (chiptod->primary) {
        _FDT((fdt_setprop(fdt, offset, "primary", NULL, 0)));
    } else if (chiptod->secondary) {
        _FDT((fdt_setprop(fdt, offset, "secondary", NULL, 0)));
    }

    _FDT((fdt_setprop(fdt, offset, "reg", reg, sizeof(reg))));
    _FDT((fdt_setprop(fdt, offset, "compatible", compat, compat_size)));
    return 0;
}

static int pnv_chiptod_power9_dt_xscom(PnvXScomInterface *dev, void *fdt,
                             int xscom_offset)
{
    const char compat[] = "ibm,power-chiptod\0ibm,power9-chiptod";

    return pnv_chiptod_dt_xscom(dev, fdt, xscom_offset, compat, sizeof(compat));
}

static const Property pnv_chiptod_properties[] = {
    DEFINE_PROP_BOOL("primary", PnvChipTOD, primary, false),
    DEFINE_PROP_BOOL("secondary", PnvChipTOD, secondary, false),
    DEFINE_PROP_LINK("chip", PnvChipTOD , chip, TYPE_PNV_CHIP, PnvChip *),
};

static void pnv_chiptod_power9_class_init(ObjectClass *klass, const void *data)
{
    PnvChipTODClass *pctc = PNV_CHIPTOD_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvXScomInterfaceClass *xdc = PNV_XSCOM_INTERFACE_CLASS(klass);

    dc->desc = "PowerNV ChipTOD Controller (POWER9)";
    device_class_set_props(dc, pnv_chiptod_properties);

    xdc->dt_xscom = pnv_chiptod_power9_dt_xscom;

    pctc->broadcast_ttype = chiptod_power9_broadcast_ttype;
    pctc->tx_ttype_target = chiptod_power9_tx_ttype_target;

    pctc->xscom_size = PNV_XSCOM_CHIPTOD_SIZE;
}

static const TypeInfo pnv_chiptod_power9_type_info = {
    .name          = TYPE_PNV9_CHIPTOD,
    .parent        = TYPE_PNV_CHIPTOD,
    .instance_size = sizeof(PnvChipTOD),
    .class_init    = pnv_chiptod_power9_class_init,
    .interfaces    = (const InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { }
    }
};

static int pnv_chiptod_power10_dt_xscom(PnvXScomInterface *dev, void *fdt,
                             int xscom_offset)
{
    const char compat[] = "ibm,power-chiptod\0ibm,power10-chiptod";

    return pnv_chiptod_dt_xscom(dev, fdt, xscom_offset, compat, sizeof(compat));
}

static void pnv_chiptod_power10_class_init(ObjectClass *klass, const void *data)
{
    PnvChipTODClass *pctc = PNV_CHIPTOD_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvXScomInterfaceClass *xdc = PNV_XSCOM_INTERFACE_CLASS(klass);

    dc->desc = "PowerNV ChipTOD Controller (POWER10)";
    device_class_set_props(dc, pnv_chiptod_properties);

    xdc->dt_xscom = pnv_chiptod_power10_dt_xscom;

    pctc->broadcast_ttype = chiptod_power10_broadcast_ttype;
    pctc->tx_ttype_target = chiptod_power10_tx_ttype_target;

    pctc->xscom_size = PNV_XSCOM_CHIPTOD_SIZE;
}

static const TypeInfo pnv_chiptod_power10_type_info = {
    .name          = TYPE_PNV10_CHIPTOD,
    .parent        = TYPE_PNV_CHIPTOD,
    .instance_size = sizeof(PnvChipTOD),
    .class_init    = pnv_chiptod_power10_class_init,
    .interfaces    = (const InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { }
    }
};

static int pnv_chiptod_power11_dt_xscom(PnvXScomInterface *dev, void *fdt,
                             int xscom_offset)
{
    const char compat[] = "ibm,power-chiptod\0ibm,power11-chiptod";

    return pnv_chiptod_dt_xscom(dev, fdt, xscom_offset, compat, sizeof(compat));
}

static void pnv_chiptod_power11_class_init(ObjectClass *klass, const void *data)
{
    PnvChipTODClass *pctc = PNV_CHIPTOD_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvXScomInterfaceClass *xdc = PNV_XSCOM_INTERFACE_CLASS(klass);

    dc->desc = "PowerNV ChipTOD Controller (Power11)";
    device_class_set_props(dc, pnv_chiptod_properties);

    xdc->dt_xscom = pnv_chiptod_power11_dt_xscom;

    pctc->broadcast_ttype = chiptod_power11_broadcast_ttype;
    pctc->tx_ttype_target = chiptod_power11_tx_ttype_target;

    pctc->xscom_size = PNV_XSCOM_CHIPTOD_SIZE;
}

static const TypeInfo pnv_chiptod_power11_type_info = {
    .name          = TYPE_PNV11_CHIPTOD,
    .parent        = TYPE_PNV_CHIPTOD,
    .instance_size = sizeof(PnvChipTOD),
    .class_init    = pnv_chiptod_power11_class_init,
    .interfaces    = (const InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { }
    }
};

static void pnv_chiptod_reset(void *dev)
{
    PnvChipTOD *chiptod = PNV_CHIPTOD(dev);

    chiptod->pss_mss_ctrl_reg = 0;
    if (chiptod->primary) {
        chiptod->pss_mss_ctrl_reg |= PPC_BIT(1);
    }
    chiptod->pss_mss_ctrl_reg |= PPC_BIT(2);

    chiptod->tod_error = 0;
    chiptod->tod_state = tod_error;
}

static void pnv_chiptod_realize(DeviceState *dev, Error **errp)
{
    PnvChipTOD *chiptod = PNV_CHIPTOD(dev);
    PnvChipTODClass *pctc = PNV_CHIPTOD_GET_CLASS(chiptod);

    pnv_xscom_region_init(&chiptod->xscom_regs, OBJECT(dev),
                          &pnv_chiptod_xscom_ops, chiptod, "xscom-chiptod",
                          pctc->xscom_size);

    qemu_register_reset(pnv_chiptod_reset, chiptod);
}

static void pnv_chiptod_unrealize(DeviceState *dev)
{
    PnvChipTOD *chiptod = PNV_CHIPTOD(dev);

    qemu_unregister_reset(pnv_chiptod_reset, chiptod);
}

static void pnv_chiptod_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pnv_chiptod_realize;
    dc->unrealize = pnv_chiptod_unrealize;
    dc->desc = "PowerNV ChipTOD Controller";
    dc->user_creatable = false;
}

static const TypeInfo pnv_chiptod_type_info = {
    .name          = TYPE_PNV_CHIPTOD,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(PnvChipTOD),
    .class_init    = pnv_chiptod_class_init,
    .class_size    = sizeof(PnvChipTODClass),
    .abstract      = true,
};

static void pnv_chiptod_register_types(void)
{
    type_register_static(&pnv_chiptod_type_info);
    type_register_static(&pnv_chiptod_power9_type_info);
    type_register_static(&pnv_chiptod_power10_type_info);
    type_register_static(&pnv_chiptod_power11_type_info);
}

type_init(pnv_chiptod_register_types);
