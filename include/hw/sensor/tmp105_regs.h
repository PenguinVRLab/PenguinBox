/*
 * Texas Instruments TMP105 Temperature Sensor I2C messages
 *
 * Browse the data sheet:
 *
 *    http://www.ti.com/lit/gpn/tmp105
 *
 * Copyright (C) 2012 Alex Horn <alex.horn@cs.ox.ac.uk>
 * Copyright (C) 2008-2012 Andrzej Zaborowski <balrogg@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later. See the COPYING file in the top-level directory.
 */

#ifndef TMP105_REGS_H
#define TMP105_REGS_H

typedef enum TMP105Reg {
    TMP105_REG_TEMPERATURE = 0,
    TMP105_REG_CONFIG,
    TMP105_REG_T_LOW,
    TMP105_REG_T_HIGH,
} TMP105Reg;

#endif
