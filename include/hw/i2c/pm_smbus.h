#ifndef PM_SMBUS_H
#define PM_SMBUS_H

#include "system/memory.h"
#include "hw/i2c/smbus_master.h"

#define PM_SMBUS_MAX_MSG_SIZE 32

typedef struct PMSMBus {
    I2CBus *smbus;
    MemoryRegion io;

    uint8_t smb_stat;
    uint8_t smb_ctl;
    uint8_t smb_cmd;
    uint8_t smb_addr;
    uint8_t smb_data0;
    uint8_t smb_data1;
    uint8_t smb_data[PM_SMBUS_MAX_MSG_SIZE];
    uint8_t smb_blkdata;
    uint8_t smb_auxctl;
    uint32_t smb_index;

    void (*reset)(struct PMSMBus *s);

    bool i2c_enable;
    void (*set_irq)(struct PMSMBus *s, bool enabled);
    void *opaque;

    bool op_done;

    bool in_i2c_block_read;

    bool start_transaction_on_status_read;
} PMSMBus;

void pm_smbus_init(DeviceState *parent, PMSMBus *smb, bool force_aux_blk);

bool pm_smbus_vmstate_needed(void);

extern const VMStateDescription pmsmb_vmstate;

#endif
