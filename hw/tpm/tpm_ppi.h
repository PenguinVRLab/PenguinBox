/*
 * TPM Physical Presence Interface
 *
 * Copyright (C) 2018 IBM Corporation
 *
 * Authors:
 *  Stefan Berger    <stefanb@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef TPM_TPM_PPI_H
#define TPM_TPM_PPI_H

#include "system/memory.h"

typedef struct TPMPPI {
    MemoryRegion ram;
    uint8_t *buf;
} TPMPPI;

void tpm_ppi_init(TPMPPI *tpmppi, MemoryRegion *m,
                  hwaddr addr, Object *obj);

void tpm_ppi_reset(TPMPPI *tpmppi);

#endif
