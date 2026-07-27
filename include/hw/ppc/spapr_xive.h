/*
 * QEMU PowerPC sPAPR XIVE interrupt controller model
 *
 * Copyright (c) 2017-2018, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PPC_SPAPR_XIVE_H
#define PPC_SPAPR_XIVE_H

#include "hw/ppc/spapr_irq.h"
#include "hw/ppc/xive.h"

#define TYPE_SPAPR_XIVE "spapr-xive"
#define SPAPR_XIVE(obj) OBJECT_CHECK(SpaprXive, (obj), TYPE_SPAPR_XIVE)
#define SPAPR_XIVE_CLASS(klass)                                         \
    OBJECT_CLASS_CHECK(SpaprXiveClass, (klass), TYPE_SPAPR_XIVE)
#define SPAPR_XIVE_GET_CLASS(obj)                               \
    OBJECT_GET_CLASS(SpaprXiveClass, (obj), TYPE_SPAPR_XIVE)

typedef struct SpaprXive {
    XiveRouter    parent;

    XiveSource    source;
    hwaddr        vc_base;

    XiveENDSource end_source;
    hwaddr        end_base;

    gchar *nodename;

    XiveEAS       *eat;
    uint32_t      nr_irqs;
    XiveEND       *endt;
    uint32_t      nr_ends;

    hwaddr        tm_base;
    MemoryRegion  tm_mmio;

    int           fd;
    void          *tm_mmap;
    MemoryRegion  tm_mmio_kvm;
    VMChangeStateEntry *change;

    uint8_t       hv_prio;
} SpaprXive;

typedef struct SpaprXiveClass {
    XiveRouterClass parent;

    DeviceRealize parent_realize;
} SpaprXiveClass;

#define SPAPR_XIVE_BLOCK_ID 0x0

struct SpaprMachineState;
void spapr_xive_hcall_init(struct SpaprMachineState *spapr);
void spapr_xive_mmio_set_enabled(SpaprXive *xive, bool enable);
void spapr_xive_map_mmio(SpaprXive *xive);

int spapr_xive_end_to_target(uint8_t end_blk, uint32_t end_idx,
                             uint32_t *out_server, uint8_t *out_prio);

int kvmppc_xive_connect(SpaprInterruptController *intc, uint32_t nr_servers,
                        Error **errp);
void kvmppc_xive_disconnect(SpaprInterruptController *intc);
void kvmppc_xive_reset(SpaprXive *xive, Error **errp);
int kvmppc_xive_set_source_config(SpaprXive *xive, uint32_t lisn, XiveEAS *eas,
                                  Error **errp);
void kvmppc_xive_sync_source(SpaprXive *xive, uint32_t lisn, Error **errp);
uint64_t kvmppc_xive_esb_rw(XiveSource *xsrc, int srcno, uint32_t offset,
                            uint64_t data, bool write);
int kvmppc_xive_set_queue_config(SpaprXive *xive, uint8_t end_blk,
                                 uint32_t end_idx, XiveEND *end,
                                 Error **errp);
int kvmppc_xive_get_queue_config(SpaprXive *xive, uint8_t end_blk,
                                 uint32_t end_idx, XiveEND *end,
                                 Error **errp);
void kvmppc_xive_synchronize_state(SpaprXive *xive, Error **errp);
int kvmppc_xive_pre_save(SpaprXive *xive);
int kvmppc_xive_post_load(SpaprXive *xive, int version_id);

#endif
