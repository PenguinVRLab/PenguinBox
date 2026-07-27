/*
 * QEMU EEPROM 93xx emulation
 *
 * Copyright (c) 2006-2007 Stefan Weil
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef EEPROM93XX_H
#define EEPROM93XX_H

typedef struct _eeprom_t eeprom_t;

eeprom_t *eeprom93xx_new(DeviceState *dev, uint16_t nwords);

void eeprom93xx_free(DeviceState *dev, eeprom_t *eeprom);

uint16_t eeprom93xx_read(eeprom_t *eeprom);

void eeprom93xx_write(eeprom_t *eeprom, int eecs, int eesk, int eedi);

uint16_t *eeprom93xx_data(eeprom_t *eeprom);

#endif
