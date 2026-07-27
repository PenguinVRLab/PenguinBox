/*
 * libqos driver framework
 *
 * Copyright (c) 2018 Emanuele Giuseppe Esposito <e.emanuelegiuseppe@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#ifndef QGRAPH_QSDHCI_H
#define QGRAPH_QSDHCI_H

#include "qgraph.h"
#include "pci.h"

typedef struct QSDHCI QSDHCI;
typedef struct QSDHCI_MemoryMapped QSDHCI_MemoryMapped;
typedef struct QSDHCI_PCI  QSDHCI_PCI;
typedef struct QSDHCIProperties QSDHCIProperties;

struct QSDHCIProperties {
    uint8_t version;
    uint8_t baseclock;
    struct {
        bool sdma;
        uint64_t reg;
    } capab;
};

struct QSDHCI {
    uint16_t (*readw)(QSDHCI *s, uint32_t reg);
    uint64_t (*readq)(QSDHCI *s, uint32_t reg);
    void (*writeq)(QSDHCI *s, uint32_t reg, uint64_t val);
    QSDHCIProperties props;
};

struct QSDHCI_MemoryMapped {
    QOSGraphObject obj;
    QTestState *qts;
    QSDHCI sdhci;
    uint64_t addr;
};

struct QSDHCI_PCI {
    QOSGraphObject obj;
    QPCIDevice dev;
    QSDHCI sdhci;
    QPCIBar mem_bar;
};

void qos_init_sdhci_mm(QSDHCI_MemoryMapped *sdhci, QTestState *qts,
                       uint32_t addr, QSDHCIProperties *common);

#endif
