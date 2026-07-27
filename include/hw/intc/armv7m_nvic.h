/*
 * ARMv7M NVIC object
 *
 * Copyright (c) 2017 Linaro Ltd
 * Written by Peter Maydell <peter.maydell@linaro.org>
 *
 * This code is licensed under the GPL version 2 or later.
 */

#ifndef HW_ARM_ARMV7M_NVIC_H
#define HW_ARM_ARMV7M_NVIC_H

#include "target/arm/cpu-qom.h"
#include "hw/sysbus.h"
#include "hw/timer/armv7m_systick.h"
#include "qom/object.h"

#define TYPE_NVIC "armv7m_nvic"
OBJECT_DECLARE_SIMPLE_TYPE(NVICState, NVIC)

#define NVIC_MAX_VECTORS 512
#define NVIC_INTERNAL_VECTORS 16

typedef struct VecInfo {
    int16_t prio;
    uint8_t enabled;
    uint8_t pending;
    uint8_t active;
    uint8_t level;
} VecInfo;

struct NVICState {
    SysBusDevice parent_obj;

    ARMCPU *cpu;

    VecInfo vectors[NVIC_MAX_VECTORS];
    VecInfo sec_vectors[NVIC_INTERNAL_VECTORS];
    uint32_t prigroup[M_REG_NUM_BANKS];
    uint8_t num_prio_bits;

    bool itns[NVIC_MAX_VECTORS];

    unsigned int vectpending;
    bool vectpending_is_s_banked;
    int exception_prio;
    int vectpending_prio;

    MemoryRegion sysregmem;

    uint32_t num_irq;
    qemu_irq excpout;
    qemu_irq sysresetreq;
};

void armv7m_nvic_set_pending(NVICState *s, int irq, bool secure);
void armv7m_nvic_set_pending_derived(NVICState *s, int irq, bool secure);
void armv7m_nvic_set_pending_lazyfp(NVICState *s, int irq, bool secure);
void armv7m_nvic_get_pending_irq_info(NVICState *s, int *pirq,
                                      bool *ptargets_secure);
void armv7m_nvic_acknowledge_irq(NVICState *s);
int armv7m_nvic_complete_irq(NVICState *s, int irq, bool secure);
bool armv7m_nvic_get_ready_status(NVICState *s, int irq, bool secure);
int armv7m_nvic_raw_execution_priority(NVICState *s);
bool armv7m_nvic_neg_prio_requested(NVICState *s, bool secure);
bool armv7m_nvic_can_take_pending_exception(NVICState *s);

#endif
