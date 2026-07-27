/*
 * QEMU single LED device
 *
 * Copyright (C) 2020 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_MISC_LED_H
#define HW_MISC_LED_H

#include "qom/object.h"
#include "hw/qdev-core.h"

#define TYPE_LED "led"

typedef enum {
    LED_COLOR_VIOLET,
    LED_COLOR_BLUE,
    LED_COLOR_CYAN,
    LED_COLOR_GREEN,
    LED_COLOR_YELLOW,
    LED_COLOR_AMBER,
    LED_COLOR_ORANGE,
    LED_COLOR_RED,
} LEDColor;

struct LEDState {
    DeviceState parent_obj;

    uint8_t intensity_percent;
    qemu_irq irq;

    char *description;
    char *color;
    bool gpio_active_high;
};
typedef struct LEDState LEDState;
DECLARE_INSTANCE_CHECKER(LEDState, LED, TYPE_LED)

void led_set_intensity(LEDState *s, unsigned intensity_percent);

unsigned led_get_intensity(LEDState *s);

void led_set_state(LEDState *s, bool is_emitting);

LEDState *led_create_simple(Object *parentobj,
                            GpioPolarity gpio_polarity,
                            LEDColor color,
                            const char *description);

#endif
