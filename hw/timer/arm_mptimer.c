/*
 * Private peripheral timer/watchdog blocks for ARM 11MPCore and A9MP
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Copyright (c) 2011 Linaro Limited
 * Written by Paul Brook, Peter Maydell
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/ptimer.h"
#include "hw/qdev-properties.h"
#include "hw/timer/arm_mptimer.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/core/cpu.h"

#define PTIMER_POLICY                       \
    (PTIMER_POLICY_WRAP_AFTER_ONE_PERIOD |  \
     PTIMER_POLICY_CONTINUOUS_TRIGGER    |  \
     PTIMER_POLICY_NO_IMMEDIATE_TRIGGER  |  \
     PTIMER_POLICY_NO_IMMEDIATE_RELOAD   |  \
     PTIMER_POLICY_NO_COUNTER_ROUND_DOWN)

static inline int get_current_cpu(ARMMPTimerState *s)
{
    int cpu_id = current_cpu ? current_cpu->cpu_index : 0;

    if (cpu_id >= s->num_cpu) {
        hw_error("arm_mptimer: num-cpu %d but this cpu is %d!\n",
                 s->num_cpu, cpu_id);
    }

    return cpu_id;
}

static inline void timerblock_update_irq(TimerBlock *tb)
{
    qemu_set_irq(tb->irq, tb->status && (tb->control & 4));
}

static inline uint32_t timerblock_scale(uint32_t control)
{
    return (((control >> 8) & 0xff) + 1) * 10;
}

static inline void timerblock_set_count(struct ptimer_state *timer,
                                        uint32_t control, uint64_t *count)
{
    if ((control & 3) == 3 && (control & 0xff00) == 0 && *count == 0) {
        *count = ptimer_get_limit(timer);
    }
    ptimer_set_count(timer, *count);
}

static inline void timerblock_run(struct ptimer_state *timer,
                                  uint32_t control, uint32_t load)
{
    if ((control & 1) && ((control & 0xff00) || load != 0)) {
        ptimer_run(timer, !(control & 2));
    }
}

static void timerblock_tick(void *opaque)
{
    TimerBlock *tb = (TimerBlock *)opaque;
    if ((tb->control & 2) && (tb->control & 0xff00) == 0 &&
            ptimer_get_limit(tb->timer) == 0) {
        ptimer_stop(tb->timer);
    }
    tb->status = 1;
    timerblock_update_irq(tb);
}

static uint64_t timerblock_read(void *opaque, hwaddr addr,
                                unsigned size)
{
    TimerBlock *tb = (TimerBlock *)opaque;
    switch (addr) {
    case 0:
        return ptimer_get_limit(tb->timer);
    case 4:
        return ptimer_get_count(tb->timer);
    case 8:
        return tb->control;
    case 12:
        return tb->status;
    default:
        return 0;
    }
}

static void timerblock_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned size)
{
    TimerBlock *tb = (TimerBlock *)opaque;
    uint32_t control = tb->control;
    switch (addr) {
    case 0:
        ptimer_transaction_begin(tb->timer);
        if ((control & 1) && (control & 0xff00) == 0 && value == 0) {
            ptimer_stop(tb->timer);
        }
        ptimer_set_limit(tb->timer, value, 1);
        timerblock_run(tb->timer, control, value);
        ptimer_transaction_commit(tb->timer);
        break;
    case 4:
        ptimer_transaction_begin(tb->timer);
        if ((control & 1) && (control & 0xff00) == 0 && value == 0 &&
                (!(control & 2) || ptimer_get_limit(tb->timer) == 0)) {
            ptimer_stop(tb->timer);
        }
        timerblock_set_count(tb->timer, control, &value);
        timerblock_run(tb->timer, control, value);
        ptimer_transaction_commit(tb->timer);
        break;
    case 8:
        ptimer_transaction_begin(tb->timer);
        if ((control & 3) != (value & 3)) {
            ptimer_stop(tb->timer);
        }
        if ((control & 0xff00) != (value & 0xff00)) {
            ptimer_set_period(tb->timer, timerblock_scale(value));
        }
        if (value & 1) {
            uint64_t count = ptimer_get_count(tb->timer);
            if ((value & 2) && count == 0) {
                timerblock_set_count(tb->timer, value, &count);
            }
            timerblock_run(tb->timer, value, count);
        }
        tb->control = value;
        ptimer_transaction_commit(tb->timer);
        break;
    case 12:
        tb->status &= ~value;
        timerblock_update_irq(tb);
        break;
    }
}

static uint64_t arm_thistimer_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    ARMMPTimerState *s = (ARMMPTimerState *)opaque;
    int id = get_current_cpu(s);
    return timerblock_read(&s->timerblock[id], addr, size);
}

static void arm_thistimer_write(void *opaque, hwaddr addr,
                                uint64_t value, unsigned size)
{
    ARMMPTimerState *s = (ARMMPTimerState *)opaque;
    int id = get_current_cpu(s);
    timerblock_write(&s->timerblock[id], addr, value, size);
}

static const MemoryRegionOps arm_thistimer_ops = {
    .read = arm_thistimer_read,
    .write = arm_thistimer_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const MemoryRegionOps timerblock_ops = {
    .read = timerblock_read,
    .write = timerblock_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void timerblock_reset(TimerBlock *tb)
{
    tb->control = 0;
    tb->status = 0;
    if (tb->timer) {
        ptimer_transaction_begin(tb->timer);
        ptimer_stop(tb->timer);
        ptimer_set_limit(tb->timer, 0, 1);
        ptimer_set_period(tb->timer, timerblock_scale(0));
        ptimer_transaction_commit(tb->timer);
    }
}

static void arm_mptimer_reset(DeviceState *dev)
{
    ARMMPTimerState *s = ARM_MPTIMER(dev);
    int i;

    for (i = 0; i < ARRAY_SIZE(s->timerblock); i++) {
        timerblock_reset(&s->timerblock[i]);
    }
}

static void arm_mptimer_init(Object *obj)
{
    ARMMPTimerState *s = ARM_MPTIMER(obj);

    memory_region_init_io(&s->iomem, obj, &arm_thistimer_ops, s,
                          "arm_mptimer_timer", 0x20);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void arm_mptimer_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    ARMMPTimerState *s = ARM_MPTIMER(dev);
    int i;

    if (s->num_cpu < 1 || s->num_cpu > ARM_MPTIMER_MAX_CPUS) {
        error_setg(errp, "num-cpu must be between 1 and %d",
                   ARM_MPTIMER_MAX_CPUS);
        return;
    }
    for (i = 0; i < s->num_cpu; i++) {
        TimerBlock *tb = &s->timerblock[i];
        tb->timer = ptimer_init(timerblock_tick, tb, PTIMER_POLICY);
        sysbus_init_irq(sbd, &tb->irq);
        memory_region_init_io(&tb->iomem, OBJECT(s), &timerblock_ops, tb,
                              "arm_mptimer_timerblock", 0x20);
        sysbus_init_mmio(sbd, &tb->iomem);
    }
}

static const VMStateDescription vmstate_timerblock = {
    .name = "arm_mptimer_timerblock",
    .version_id = 3,
    .minimum_version_id = 3,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(control, TimerBlock),
        VMSTATE_UINT32(status, TimerBlock),
        VMSTATE_PTIMER(timer, TimerBlock),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_arm_mptimer = {
    .name = "arm_mptimer",
    .version_id = 3,
    .minimum_version_id = 3,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_VARRAY_UINT32(timerblock, ARMMPTimerState, num_cpu,
                                     3, vmstate_timerblock, TimerBlock),
        VMSTATE_END_OF_LIST()
    }
};

static const Property arm_mptimer_properties[] = {
    DEFINE_PROP_UINT32("num-cpu", ARMMPTimerState, num_cpu, 0),
};

static void arm_mptimer_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = arm_mptimer_realize;
    dc->vmsd = &vmstate_arm_mptimer;
    device_class_set_legacy_reset(dc, arm_mptimer_reset);
    device_class_set_props(dc, arm_mptimer_properties);
}

static const TypeInfo arm_mptimer_info = {
    .name          = TYPE_ARM_MPTIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ARMMPTimerState),
    .instance_init = arm_mptimer_init,
    .class_init    = arm_mptimer_class_init,
};

static void arm_mptimer_register_types(void)
{
    type_register_static(&arm_mptimer_info);
}

type_init(arm_mptimer_register_types)
