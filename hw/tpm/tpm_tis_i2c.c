/*
 * tpm_tis_i2c.c - QEMU's TPM TIS I2C Device
 *
 * Copyright (c) 2023 IBM Corporation
 *
 * Authors:
 *   Ninad Palsule <ninad@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * TPM I2C implementation follows TCG TPM I2c Interface specification,
 * Family 2.0, Level 00, Revision 1.00
 *
 * TPM TIS for TPM 2 implementation following TCG PC Client Platform
 * TPM Profile (PTP) Specification, Family 2.0, Revision 00.43
 *
 */

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "hw/sysbus.h"
#include "hw/acpi/tpm.h"
#include "migration/vmstate.h"
#include "tpm_prop.h"
#include "qemu/log.h"
#include "trace.h"
#include "tpm_tis.h"

#define OP_SEND   1
#define OP_RECV   2

#define TPM_TIS_I2C_IS_VALID_LOCTY(x)   TPM_TIS_IS_VALID_LOCTY(x)

typedef struct TPMStateI2C {
    I2CSlave    parent_obj;

    uint8_t     offset;
    uint8_t     operation;
    uint8_t     data[5];

    uint8_t     loc_sel;
    uint8_t     csum_enable;

    /* Derived from the above */
    const char *reg_name;
    uint32_t    tis_addr;

    TPMState    state;

} TPMStateI2C;

DECLARE_INSTANCE_CHECKER(TPMStateI2C, TPM_TIS_I2C,
                         TYPE_TPM_TIS_I2C)

static inline void tpm_tis_i2c_to_tis_reg(TPMStateI2C *i2cst, uint8_t i2c_reg);

typedef struct regMap {
    uint8_t   i2c_reg;
    uint16_t  tis_reg;
    const char *reg_name;
} I2CRegMap;

static const I2CRegMap tpm_tis_reg_map[] = {
    { TPM_I2C_REG_DATA_FIFO,        TPM_TIS_REG_DATA_FIFO,       "FIFO",      },
    { TPM_I2C_REG_STS,              TPM_TIS_REG_STS,             "STS",       },
    { TPM_I2C_REG_DATA_CSUM_GET,    TPM_I2C_REG_UNKNOWN,         "CSUM_GET",  },
    { TPM_I2C_REG_LOC_SEL,          TPM_I2C_REG_UNKNOWN,         "LOC_SEL",   },
    { TPM_I2C_REG_ACCESS,           TPM_TIS_REG_ACCESS,          "ACCESS",    },
    { TPM_I2C_REG_INT_ENABLE,       TPM_TIS_REG_INT_ENABLE,     "INTR_ENABLE",},
    { TPM_I2C_REG_INT_CAPABILITY,   TPM_I2C_REG_UNKNOWN,         "INTR_CAP",  },
    { TPM_I2C_REG_INTF_CAPABILITY,  TPM_TIS_REG_INTF_CAPABILITY, "INTF_CAP",  },
    { TPM_I2C_REG_DID_VID,          TPM_TIS_REG_DID_VID,         "DID_VID",   },
    { TPM_I2C_REG_RID,              TPM_TIS_REG_RID,             "RID",       },
    { TPM_I2C_REG_I2C_DEV_ADDRESS,  TPM_I2C_REG_UNKNOWN,        "DEV_ADDRESS",},
    { TPM_I2C_REG_DATA_CSUM_ENABLE, TPM_I2C_REG_UNKNOWN,        "CSUM_ENABLE",},
};

static int tpm_tis_i2c_pre_save(void *opaque)
{
    TPMStateI2C *i2cst = opaque;

    return tpm_tis_pre_save(&i2cst->state);
}

static int tpm_tis_i2c_post_load(void *opaque, int version_id)
{
    TPMStateI2C *i2cst = opaque;

    if (i2cst->offset >= 1) {
        tpm_tis_i2c_to_tis_reg(i2cst, i2cst->data[0]);
    }

    return 0;
}

static const VMStateDescription vmstate_tpm_tis_i2c = {
    .name = "tpm-tis-i2c",
    .version_id = 0,
    .pre_save  = tpm_tis_i2c_pre_save,
    .post_load  = tpm_tis_i2c_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_BUFFER(state.buffer, TPMStateI2C),
        VMSTATE_UINT16(state.rw_offset, TPMStateI2C),
        VMSTATE_UINT8(state.active_locty, TPMStateI2C),
        VMSTATE_UINT8(state.aborting_locty, TPMStateI2C),
        VMSTATE_UINT8(state.next_locty, TPMStateI2C),

        VMSTATE_STRUCT_ARRAY(state.loc, TPMStateI2C, TPM_TIS_NUM_LOCALITIES, 0,
                             vmstate_locty, TPMLocality),

        VMSTATE_UINT8(offset, TPMStateI2C),
        VMSTATE_UINT8(operation, TPMStateI2C),
        VMSTATE_BUFFER(data, TPMStateI2C),
        VMSTATE_UINT8(loc_sel, TPMStateI2C),
        VMSTATE_UINT8(csum_enable, TPMStateI2C),

        VMSTATE_END_OF_LIST()
    }
};

static void tpm_tis_i2c_set_data(TPMStateI2C *i2cst, uint32_t data)
{
    i2cst->data[1] = data;
    i2cst->data[2] = data >> 8;
    i2cst->data[3] = data >> 16;
    i2cst->data[4] = data >> 24;
}
static uint32_t tpm_tis_i2c_interface_capability(TPMStateI2C *i2cst,
                                                 uint32_t tis_cap)
{
    uint32_t i2c_cap;

    i2c_cap = (TPM_I2C_CAP_INTERFACE_TYPE |
               TPM_I2C_CAP_INTERFACE_VER  |
               TPM_I2C_CAP_TPM2_FAMILY    |
               TPM_I2C_CAP_LOCALITY_CAP   |
               TPM_I2C_CAP_BUS_SPEED      |
               TPM_I2C_CAP_DEV_ADDR_CHANGE);

    if (tis_cap & TPM_TIS_CAP_BURST_COUNT_STATIC) {
        i2c_cap |= TPM_I2C_CAP_BURST_COUNT_STATIC;
    }

    return i2c_cap;
}

static inline void tpm_tis_i2c_to_tis_reg(TPMStateI2C *i2cst, uint8_t i2c_reg)
{
    const I2CRegMap *reg_map;
    int i;

    i2cst->tis_addr = 0xffffffff;

    if (i2c_reg >= TPM_I2C_REG_STS && i2c_reg <= TPM_I2C_REG_STS + 3) {
        i2c_reg = TPM_I2C_REG_STS;
    }

    for (i = 0; i < ARRAY_SIZE(tpm_tis_reg_map); i++) {
        reg_map = &tpm_tis_reg_map[i];
        if (reg_map->i2c_reg == i2c_reg) {
            i2cst->reg_name = reg_map->reg_name;
            i2cst->tis_addr = reg_map->tis_reg;

            assert(TPM_TIS_I2C_IS_VALID_LOCTY(i2cst->loc_sel));
            i2cst->tis_addr += (i2cst->loc_sel << TPM_TIS_LOCALITY_SHIFT);
            break;
        }
    }
}

static inline void tpm_tis_i2c_clear_data(TPMStateI2C *i2cst)
{
    i2cst->operation = 0;
    i2cst->offset = 0;
    i2cst->tis_addr = 0xffffffff;
    i2cst->reg_name = NULL;
    memset(i2cst->data, 0, sizeof(i2cst->data));
}

static inline void tpm_tis_i2c_tpm_send(TPMStateI2C *i2cst)
{
    uint32_t data;
    size_t offset = 0;
    uint32_t sz = 4;

    if ((i2cst->operation == OP_SEND) && (i2cst->offset > 1)) {

        switch (i2cst->data[0]) {
        case TPM_I2C_REG_DATA_CSUM_ENABLE:
            i2cst->csum_enable = i2cst->data[1] & TPM_DATA_CSUM_ENABLED;
            break;
        case TPM_I2C_REG_DATA_FIFO:
            break;
        case TPM_I2C_REG_LOC_SEL:
            if (TPM_TIS_I2C_IS_VALID_LOCTY(i2cst->data[1])) {
                i2cst->loc_sel = i2cst->data[1];
            }
            break;
        default:

            data = i2cst->data[1];
            data |= i2cst->data[2] << 8;
            data |= i2cst->data[3] << 16;
            data |= i2cst->data[4] << 24;

            switch (i2cst->data[0]) {
            case TPM_I2C_REG_INT_ENABLE:
                data &= TPM_I2C_INT_ENABLE_MASK;
                break;
            case TPM_I2C_REG_STS ... TPM_I2C_REG_STS + 3:
                offset = i2cst->data[0] - TPM_I2C_REG_STS;
                if (offset > 0) {
                    sz = 1;
                }
                data &= (TPM_I2C_STS_WRITE_MASK >> (offset * 8));
                break;
            }

            tpm_tis_write_data(&i2cst->state, i2cst->tis_addr + offset, data,
                               sz);
            break;
        }

        tpm_tis_i2c_clear_data(i2cst);
    }
}

static void tpm_tis_i2c_request_completed(TPMIf *ti, int ret)
{
    TPMStateI2C *i2cst = TPM_TIS_I2C(ti);
    TPMState *s = &i2cst->state;

    tpm_tis_request_completed(s, ret);
}

static enum TPMVersion tpm_tis_i2c_get_tpm_version(TPMIf *ti)
{
    TPMStateI2C *i2cst = TPM_TIS_I2C(ti);
    TPMState *s = &i2cst->state;

    return tpm_tis_get_tpm_version(s);
}

static int tpm_tis_i2c_event(I2CSlave *i2c, enum i2c_event event)
{
    TPMStateI2C *i2cst = TPM_TIS_I2C(i2c);
    int ret = 0;

    switch (event) {
    case I2C_START_RECV:
        trace_tpm_tis_i2c_event("START_RECV");
        break;
    case I2C_START_SEND:
        trace_tpm_tis_i2c_event("START_SEND");
        tpm_tis_i2c_clear_data(i2cst);
        break;
    case I2C_FINISH:
        trace_tpm_tis_i2c_event("FINISH");
        if (i2cst->operation == OP_SEND) {
            tpm_tis_i2c_tpm_send(i2cst);
        } else {
            tpm_tis_i2c_clear_data(i2cst);
        }
        break;
    default:
        break;
    }

    return ret;
}

static uint8_t tpm_tis_i2c_recv(I2CSlave *i2c)
{
    int          ret = 0;
    uint32_t     data_read;
    TPMStateI2C *i2cst = TPM_TIS_I2C(i2c);
    TPMState    *s = &i2cst->state;
    uint16_t     i2c_reg = i2cst->data[0];
    size_t       offset;

    if (i2cst->operation == OP_RECV) {

        if (i2cst->data[0] == TPM_I2C_REG_DATA_FIFO) {
            data_read = tpm_tis_read_data(s, i2cst->tis_addr, 1);
            ret = (data_read & 0xff);
        } else if (i2cst->offset < sizeof(i2cst->data)) {
            ret = i2cst->data[i2cst->offset++];
        }

    } else if ((i2cst->operation == OP_SEND) && (i2cst->offset < 2)) {

        i2cst->operation = OP_RECV;

        switch (i2c_reg) {
        case TPM_I2C_REG_LOC_SEL:
            tpm_tis_i2c_set_data(i2cst, i2cst->loc_sel);
            break;
        case TPM_I2C_REG_DATA_FIFO:
            data_read = tpm_tis_read_data(s, i2cst->tis_addr, 1);
            tpm_tis_i2c_set_data(i2cst, (data_read & 0xff));
            break;
        case TPM_I2C_REG_DATA_CSUM_ENABLE:
            tpm_tis_i2c_set_data(i2cst, i2cst->csum_enable);
            break;
        case TPM_I2C_REG_INT_CAPABILITY:
            tpm_tis_i2c_set_data(i2cst, TPM_I2C_INT_ENABLE_MASK);
            break;
        case TPM_I2C_REG_DATA_CSUM_GET:
            data_read = tpm_tis_get_checksum(s);

            tpm_tis_i2c_set_data(i2cst, data_read);
            break;
        default:
            data_read = tpm_tis_read_data(s, i2cst->tis_addr, 4);

            switch (i2c_reg) {
            case TPM_I2C_REG_INTF_CAPABILITY:
                data_read = tpm_tis_i2c_interface_capability(i2cst,
                                                             data_read);
                break;
            case TPM_I2C_REG_STS ... TPM_I2C_REG_STS + 3:
                offset = i2c_reg - TPM_I2C_REG_STS;
                data_read &= TPM_I2C_STS_READ_MASK;
                data_read >>= (offset * 8);
                break;
            }

            tpm_tis_i2c_set_data(i2cst, data_read);
            break;
        }

        i2cst->offset = 1;
        ret = i2cst->data[i2cst->offset++];
    } else {
        i2cst->operation = OP_RECV;
    }

    trace_tpm_tis_i2c_recv(ret);

    return ret;
}

static int tpm_tis_i2c_send(I2CSlave *i2c, uint8_t data)
{
    TPMStateI2C *i2cst = TPM_TIS_I2C(i2c);

    if (i2cst->offset == 0) {
        tpm_tis_i2c_to_tis_reg(i2cst, data);
        if (i2cst->tis_addr == 0xffffffff) {
            return 0xffffffff;
        }

        trace_tpm_tis_i2c_send_reg(i2cst->reg_name, data);

        if (data == TPM_I2C_REG_I2C_DEV_ADDRESS) {
            qemu_log_mask(LOG_UNIMP, "%s: Device address change "
                          "is not supported.\n", __func__);
            return 0xffffffff;
        }
    } else {
        trace_tpm_tis_i2c_send(data);
    }

    if (i2cst->offset < sizeof(i2cst->data)) {
        i2cst->operation = OP_SEND;

        if ((i2cst->offset == 0) ||
            (i2cst->data[0] != TPM_I2C_REG_DATA_FIFO)) {
            i2cst->data[i2cst->offset++] = data;
        } else {
            tpm_tis_write_data(&i2cst->state, i2cst->tis_addr, data, 1);
        }

        return 0;
    }

    return 1;
}

static const Property tpm_tis_i2c_properties[] = {
    DEFINE_PROP_TPMBE("tpmdev", TPMStateI2C, state.be_driver),
};

static void tpm_tis_i2c_realizefn(DeviceState *dev, Error **errp)
{
    TPMStateI2C *i2cst = TPM_TIS_I2C(dev);
    TPMState *s = &i2cst->state;

    if (!tpm_find()) {
        error_setg(errp, "at most one TPM device is permitted");
        return;
    }

    s->be_driver = qemu_find_tpm_be("tpm0");

    if (!s->be_driver) {
        error_setg(errp, "'tpmdev' property is required");
        return;
    }
}

static void tpm_tis_i2c_reset(DeviceState *dev)
{
    TPMStateI2C *i2cst = TPM_TIS_I2C(dev);
    TPMState *s = &i2cst->state;

    tpm_tis_i2c_clear_data(i2cst);

    i2cst->csum_enable = 0;
    i2cst->loc_sel = 0x00;

    return tpm_tis_reset(s);
}

static void tpm_tis_i2c_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    TPMIfClass *tc = TPM_IF_CLASS(klass);

    dc->realize = tpm_tis_i2c_realizefn;
    device_class_set_legacy_reset(dc, tpm_tis_i2c_reset);
    dc->vmsd = &vmstate_tpm_tis_i2c;
    device_class_set_props(dc, tpm_tis_i2c_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);

    k->event = tpm_tis_i2c_event;
    k->recv = tpm_tis_i2c_recv;
    k->send = tpm_tis_i2c_send;

    tc->model = TPM_MODEL_TPM_TIS;
    tc->request_completed = tpm_tis_i2c_request_completed;
    tc->get_version = tpm_tis_i2c_get_tpm_version;
}

static const TypeInfo tpm_tis_i2c_info = {
    .name          = TYPE_TPM_TIS_I2C,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(TPMStateI2C),
    .class_init    = tpm_tis_i2c_class_init,
        .interfaces = (const InterfaceInfo[]) {
        { TYPE_TPM_IF },
        { }
    }
};

static void tpm_tis_i2c_register_types(void)
{
    type_register_static(&tpm_tis_i2c_info);
}

type_init(tpm_tis_i2c_register_types)
