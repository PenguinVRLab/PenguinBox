#ifndef QEMU_I2C_H
#define QEMU_I2C_H

#include "hw/qdev-core.h"
#include "qom/object.h"

enum i2c_event {
    I2C_START_RECV,
    I2C_START_SEND,
    I2C_START_SEND_ASYNC,
    I2C_FINISH,
    I2C_NACK
};

typedef struct I2CNodeList I2CNodeList;

#define TYPE_I2C_SLAVE "i2c-slave"
OBJECT_DECLARE_TYPE(I2CSlave, I2CSlaveClass,
                    I2C_SLAVE)

struct I2CSlaveClass {
    DeviceClass parent_class;

    int (*send)(I2CSlave *s, uint8_t data);

    void (*send_async)(I2CSlave *s, uint8_t data);

    uint8_t (*recv)(I2CSlave *s);

    int (*event)(I2CSlave *s, enum i2c_event event);

    bool (*match_and_add)(I2CSlave *candidate, uint8_t address, bool broadcast,
                          I2CNodeList *current_devs);
};

struct I2CSlave {
    DeviceState qdev;

    uint8_t address;
};

#define TYPE_I2C_BUS "i2c-bus"
OBJECT_DECLARE_SIMPLE_TYPE(I2CBus, I2C_BUS)

typedef struct I2CNode I2CNode;

struct I2CNode {
    I2CSlave *elt;
    QLIST_ENTRY(I2CNode) next;
};

typedef struct I2CPendingMaster I2CPendingMaster;

struct I2CPendingMaster {
    QEMUBH *bh;
    QSIMPLEQ_ENTRY(I2CPendingMaster) entry;
};

typedef QLIST_HEAD(I2CNodeList, I2CNode) I2CNodeList;
typedef QSIMPLEQ_HEAD(I2CPendingMasters, I2CPendingMaster) I2CPendingMasters;

struct I2CBus {
    BusState qbus;
    I2CNodeList current_devs;
    I2CPendingMasters pending_masters;
    uint8_t saved_address;
    bool broadcast;

    QEMUBH *bh;
};

I2CBus *i2c_init_bus(DeviceState *parent, const char *name);
int i2c_bus_busy(I2CBus *bus);

int i2c_start_transfer(I2CBus *bus, uint8_t address, bool is_recv);

int i2c_start_recv(I2CBus *bus, uint8_t address);

int i2c_start_send(I2CBus *bus, uint8_t address);

int i2c_start_send_async(I2CBus *bus, uint8_t address);

void i2c_schedule_pending_master(I2CBus *bus);

void i2c_end_transfer(I2CBus *bus);
void i2c_nack(I2CBus *bus);
void i2c_ack(I2CBus *bus);
void i2c_bus_master(I2CBus *bus, QEMUBH *bh);
void i2c_bus_release(I2CBus *bus);
int i2c_send(I2CBus *bus, uint8_t data);
int i2c_send_async(I2CBus *bus, uint8_t data);
uint8_t i2c_recv(I2CBus *bus);
bool i2c_scan_bus(I2CBus *bus, uint8_t address, bool broadcast,
                  I2CNodeList *current_devs);

I2CSlave *i2c_slave_new(const char *name, uint8_t addr);

I2CSlave *i2c_slave_create_simple(I2CBus *bus, const char *name, uint8_t addr);

bool i2c_slave_realize_and_unref(I2CSlave *dev, I2CBus *bus, Error **errp);

void i2c_slave_set_address(I2CSlave *dev, uint8_t address);

extern const VMStateDescription vmstate_i2c_slave;

#define VMSTATE_I2C_SLAVE(_field, _state) {                          \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(I2CSlave),                                  \
    .vmsd       = &vmstate_i2c_slave,                                \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, I2CSlave),    \
}

#endif
