/*
 * QEMU PowerPC sPAPR XIVE interrupt controller model
 *
 * Copyright (c) 2017-2019, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "target/ppc/cpu.h"
#include "system/cpus.h"
#include "system/kvm.h"
#include "system/runstate.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_cpu_core.h"
#include "hw/ppc/spapr_xive.h"
#include "hw/ppc/xive.h"
#include "kvm_ppc.h"
#include "trace.h"

#include <sys/ioctl.h>

typedef struct KVMEnabledCPU {
    unsigned long vcpu_id;
    QLIST_ENTRY(KVMEnabledCPU) node;
} KVMEnabledCPU;

static QLIST_HEAD(, KVMEnabledCPU)
    kvm_enabled_cpus = QLIST_HEAD_INITIALIZER(&kvm_enabled_cpus);

static bool kvm_cpu_is_enabled(CPUState *cs)
{
    KVMEnabledCPU *enabled_cpu;
    unsigned long vcpu_id = kvm_arch_vcpu_id(cs);

    QLIST_FOREACH(enabled_cpu, &kvm_enabled_cpus, node) {
        if (enabled_cpu->vcpu_id == vcpu_id) {
            return true;
        }
    }
    return false;
}

static void kvm_cpu_enable(CPUState *cs)
{
    KVMEnabledCPU *enabled_cpu;
    unsigned long vcpu_id = kvm_arch_vcpu_id(cs);

    enabled_cpu = g_malloc(sizeof(*enabled_cpu));
    enabled_cpu->vcpu_id = vcpu_id;
    QLIST_INSERT_HEAD(&kvm_enabled_cpus, enabled_cpu, node);
}

static void kvm_cpu_disable_all(void)
{
    KVMEnabledCPU *enabled_cpu, *next;

    QLIST_FOREACH_SAFE(enabled_cpu, &kvm_enabled_cpus, node, next) {
        QLIST_REMOVE(enabled_cpu, node);
        g_free(enabled_cpu);
    }
}

int kvmppc_xive_cpu_set_state(XiveTCTX *tctx, Error **errp)
{
    SpaprXive *xive = SPAPR_XIVE(tctx->xptr);
    uint64_t state[2];
    int ret;

    assert(xive->fd != -1);

    state[0] = *((uint64_t *) &tctx->regs[TM_QW1_OS]);

    ret = kvm_set_one_reg(tctx->cs, KVM_REG_PPC_VP_STATE, state);
    if (ret != 0) {
        error_setg_errno(errp, -ret,
                         "XIVE: could not restore KVM state of CPU %ld",
                         kvm_arch_vcpu_id(tctx->cs));
        return ret;
    }

    return 0;
}

int kvmppc_xive_cpu_get_state(XiveTCTX *tctx, Error **errp)
{
    SpaprXive *xive = SPAPR_XIVE(tctx->xptr);
    uint64_t state[2] = { 0 };
    int ret;

    assert(xive->fd != -1);

    ret = kvm_get_one_reg(tctx->cs, KVM_REG_PPC_VP_STATE, state);
    if (ret != 0) {
        error_setg_errno(errp, -ret,
                         "XIVE: could not capture KVM state of CPU %ld",
                         kvm_arch_vcpu_id(tctx->cs));
        return ret;
    }

    *((uint64_t *) &tctx->regs[TM_QW1_OS]) = state[0];

    return 0;
}

typedef struct {
    XiveTCTX *tctx;
    Error **errp;
    int ret;
} XiveCpuGetState;

static void kvmppc_xive_cpu_do_synchronize_state(CPUState *cpu,
                                                 run_on_cpu_data arg)
{
    XiveCpuGetState *s = arg.host_ptr;

    s->ret = kvmppc_xive_cpu_get_state(s->tctx, s->errp);
}

int kvmppc_xive_cpu_synchronize_state(XiveTCTX *tctx, Error **errp)
{
    XiveCpuGetState s = {
        .tctx = tctx,
        .errp = errp,
    };

    run_on_cpu(tctx->cs, kvmppc_xive_cpu_do_synchronize_state,
               RUN_ON_CPU_HOST_PTR(&s));

    return s.ret;
}

int kvmppc_xive_cpu_connect(XiveTCTX *tctx, Error **errp)
{
    ERRP_GUARD();
    SpaprXive *xive = SPAPR_XIVE(tctx->xptr);
    unsigned long vcpu_id;
    int ret;

    assert(xive->fd != -1);

    if (kvm_cpu_is_enabled(tctx->cs)) {
        return 0;
    }

    vcpu_id = kvm_arch_vcpu_id(tctx->cs);

    trace_kvm_xive_cpu_connect(vcpu_id);

    ret = kvm_vcpu_enable_cap(tctx->cs, KVM_CAP_PPC_IRQ_XIVE, 0, xive->fd,
                              vcpu_id, 0);
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "XIVE: unable to connect CPU%ld to KVM device",
                         vcpu_id);
        if (ret == -ENOSPC) {
            error_append_hint(errp, "Try -smp maxcpus=N with N < %u\n",
                              MACHINE(qdev_get_machine())->smp.max_cpus);
        }
        return ret;
    }

    kvm_cpu_enable(tctx->cs);
    return 0;
}

int kvmppc_xive_set_source_config(SpaprXive *xive, uint32_t lisn, XiveEAS *eas,
                                  Error **errp)
{
    uint32_t end_idx;
    uint32_t end_blk;
    uint8_t priority;
    uint32_t server;
    bool masked;
    uint32_t eisn;
    uint64_t kvm_src;

    assert(xive_eas_is_valid(eas));

    end_idx = xive_get_field64(EAS_END_INDEX, eas->w);
    end_blk = xive_get_field64(EAS_END_BLOCK, eas->w);
    eisn = xive_get_field64(EAS_END_DATA, eas->w);
    masked = xive_eas_is_masked(eas);

    spapr_xive_end_to_target(end_blk, end_idx, &server, &priority);

    kvm_src = priority << KVM_XIVE_SOURCE_PRIORITY_SHIFT &
        KVM_XIVE_SOURCE_PRIORITY_MASK;
    kvm_src |= server << KVM_XIVE_SOURCE_SERVER_SHIFT &
        KVM_XIVE_SOURCE_SERVER_MASK;
    kvm_src |= ((uint64_t) masked << KVM_XIVE_SOURCE_MASKED_SHIFT) &
        KVM_XIVE_SOURCE_MASKED_MASK;
    kvm_src |= ((uint64_t)eisn << KVM_XIVE_SOURCE_EISN_SHIFT) &
        KVM_XIVE_SOURCE_EISN_MASK;

    return kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_SOURCE_CONFIG, lisn,
                             &kvm_src, true, errp);
}

void kvmppc_xive_sync_source(SpaprXive *xive, uint32_t lisn, Error **errp)
{
    kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_SOURCE_SYNC, lisn,
                      NULL, true, errp);
}

int kvmppc_xive_source_reset_one(XiveSource *xsrc, int srcno, Error **errp)
{
    SpaprXive *xive = SPAPR_XIVE(xsrc->xive);
    uint64_t state = 0;

    trace_kvm_xive_source_reset(srcno);

    assert(xive->fd != -1);

    if (xive_source_irq_is_lsi(xsrc, srcno)) {
        state |= KVM_XIVE_LEVEL_SENSITIVE;
        if (xive_source_is_asserted(xsrc, srcno)) {
            state |= KVM_XIVE_LEVEL_ASSERTED;
        }
    }

    return kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_SOURCE, srcno, &state,
                             true, errp);
}

static int kvmppc_xive_source_reset(XiveSource *xsrc, Error **errp)
{
    SpaprXive *xive = SPAPR_XIVE(xsrc->xive);
    int i;

    for (i = 0; i < xsrc->nr_irqs; i++) {
        int ret;

        if (!xive_eas_is_valid(&xive->eat[i])) {
            continue;
        }

        ret = kvmppc_xive_source_reset_one(xsrc, i, errp);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

static uint64_t xive_esb_rw(XiveSource *xsrc, int srcno, uint32_t offset,
                            uint64_t data, bool write)
{
    uint64_t *addr = xsrc->esb_mmap + xive_source_esb_mgmt(xsrc, srcno) +
        offset;

    if (write) {
        *addr = cpu_to_be64(data);
        return -1;
    } else {
        volatile uint64_t value = be64_to_cpu(*addr);
        return value;
    }
}

static uint8_t xive_esb_read(XiveSource *xsrc, int srcno, uint32_t offset)
{
    return xive_esb_rw(xsrc, srcno, offset, 0, 0) & 0x3;
}

static void kvmppc_xive_esb_trigger(XiveSource *xsrc, int srcno)
{
    xive_esb_rw(xsrc, srcno, 0, 0, true);
}

uint64_t kvmppc_xive_esb_rw(XiveSource *xsrc, int srcno, uint32_t offset,
                            uint64_t data, bool write)
{
    if (write) {
        return xive_esb_rw(xsrc, srcno, offset, data, 1);
    }

    if (xive_source_irq_is_lsi(xsrc, srcno) &&
        offset == XIVE_ESB_LOAD_EOI) {
        xive_esb_read(xsrc, srcno, XIVE_ESB_SET_PQ_00);
        if (xive_source_is_asserted(xsrc, srcno)) {
            kvmppc_xive_esb_trigger(xsrc, srcno);
        }
        return 0;
    } else {
        return xive_esb_rw(xsrc, srcno, offset, 0, 0);
    }
}

static void kvmppc_xive_source_get_state(XiveSource *xsrc)
{
    SpaprXive *xive = SPAPR_XIVE(xsrc->xive);
    int i;

    for (i = 0; i < xsrc->nr_irqs; i++) {
        uint8_t pq;

        if (!xive_eas_is_valid(&xive->eat[i])) {
            continue;
        }

        pq = xive_esb_read(xsrc, i, XIVE_ESB_GET);

        xive_source_esb_set(xsrc, i, pq);
    }
}

void kvmppc_xive_source_set_irq(void *opaque, int srcno, int val)
{
    XiveSource *xsrc = opaque;

    if (!xive_source_irq_is_lsi(xsrc, srcno)) {
        if (!val) {
            return;
        }
    } else {
        xive_source_set_asserted(xsrc, srcno, val);
    }

    kvmppc_xive_esb_trigger(xsrc, srcno);
}

int kvmppc_xive_get_queue_config(SpaprXive *xive, uint8_t end_blk,
                                 uint32_t end_idx, XiveEND *end,
                                 Error **errp)
{
    struct kvm_ppc_xive_eq kvm_eq = { 0 };
    uint64_t kvm_eq_idx;
    uint8_t priority;
    uint32_t server;
    int ret;

    assert(xive_end_is_valid(end));

    spapr_xive_end_to_target(end_blk, end_idx, &server, &priority);

    kvm_eq_idx = priority << KVM_XIVE_EQ_PRIORITY_SHIFT &
            KVM_XIVE_EQ_PRIORITY_MASK;
    kvm_eq_idx |= server << KVM_XIVE_EQ_SERVER_SHIFT &
        KVM_XIVE_EQ_SERVER_MASK;

    ret = kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_EQ_CONFIG, kvm_eq_idx,
                            &kvm_eq, false, errp);
    if (ret < 0) {
        return ret;
    }

    end->w1 = xive_set_field32(END_W1_GENERATION, 0ul, kvm_eq.qtoggle) |
        xive_set_field32(END_W1_PAGE_OFF, 0ul, kvm_eq.qindex);

    return 0;
}

int kvmppc_xive_set_queue_config(SpaprXive *xive, uint8_t end_blk,
                                 uint32_t end_idx, XiveEND *end,
                                 Error **errp)
{
    struct kvm_ppc_xive_eq kvm_eq = { 0 };
    uint64_t kvm_eq_idx;
    uint8_t priority;
    uint32_t server;

    kvm_eq.flags = 0;
    if (xive_get_field32(END_W0_UCOND_NOTIFY, end->w0)) {
        kvm_eq.flags |= KVM_XIVE_EQ_ALWAYS_NOTIFY;
    }

    if (xive_end_is_valid(end)) {
        kvm_eq.qshift = xive_get_field32(END_W0_QSIZE, end->w0) + 12;
        kvm_eq.qaddr  = xive_end_qaddr(end);
        kvm_eq.qtoggle = xive_get_field32(END_W1_GENERATION, end->w1);
        kvm_eq.qindex  = xive_get_field32(END_W1_PAGE_OFF, end->w1);
    } else {
        kvm_eq.qshift = 0;
        kvm_eq.qaddr  = 0;
    }

    spapr_xive_end_to_target(end_blk, end_idx, &server, &priority);

    kvm_eq_idx = priority << KVM_XIVE_EQ_PRIORITY_SHIFT &
            KVM_XIVE_EQ_PRIORITY_MASK;
    kvm_eq_idx |= server << KVM_XIVE_EQ_SERVER_SHIFT &
        KVM_XIVE_EQ_SERVER_MASK;

    return
        kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_EQ_CONFIG, kvm_eq_idx,
                          &kvm_eq, true, errp);
}

void kvmppc_xive_reset(SpaprXive *xive, Error **errp)
{
    kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_CTRL, KVM_DEV_XIVE_RESET,
                      NULL, true, errp);
}

static int kvmppc_xive_get_queues(SpaprXive *xive, Error **errp)
{
    int i;
    int ret;

    for (i = 0; i < xive->nr_ends; i++) {
        if (!xive_end_is_valid(&xive->endt[i])) {
            continue;
        }

        ret = kvmppc_xive_get_queue_config(xive, SPAPR_XIVE_BLOCK_ID, i,
                                           &xive->endt[i], errp);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

static void kvmppc_xive_change_state_handler(void *opaque, bool running,
                                             RunState state)
{
    SpaprXive *xive = opaque;
    XiveSource *xsrc = &xive->source;
    Error *local_err = NULL;
    int i;

    if (running) {
        for (i = 0; i < xsrc->nr_irqs; i++) {
            uint8_t pq;
            uint8_t old_pq;

            if (!xive_eas_is_valid(&xive->eat[i])) {
                continue;
            }

            pq = xive_source_esb_get(xsrc, i);
            old_pq = xive_esb_read(xsrc, i, XIVE_ESB_SET_PQ_00 + (pq << 8));

            if (pq == XIVE_ESB_RESET && old_pq == XIVE_ESB_QUEUED) {
                kvmppc_xive_esb_trigger(xsrc, i);
            }
        }

        return;
    }

    for (i = 0; i < xsrc->nr_irqs; i++) {
        uint8_t pq;

        if (!xive_eas_is_valid(&xive->eat[i])) {
            continue;
        }

        pq = xive_esb_read(xsrc, i, XIVE_ESB_GET);

        if (pq != XIVE_ESB_OFF) {
            pq = xive_esb_read(xsrc, i, XIVE_ESB_SET_PQ_10);
        }
        xive_source_esb_set(xsrc, i, pq);
    }

    kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_CTRL,
                      KVM_DEV_XIVE_EQ_SYNC, NULL, true, &local_err);
    if (local_err) {
        error_report_err(local_err);
        return;
    }
}

void kvmppc_xive_synchronize_state(SpaprXive *xive, Error **errp)
{
    assert(xive->fd != -1);

    if (runstate_is_running()) {
        kvmppc_xive_source_get_state(&xive->source);
    }

    kvmppc_xive_get_queues(xive, errp);
}

int kvmppc_xive_pre_save(SpaprXive *xive)
{
    Error *local_err = NULL;
    int ret;

    assert(xive->fd != -1);

    ret = kvmppc_xive_get_queues(xive, &local_err);
    if (ret < 0) {
        error_report_err(local_err);
        return ret;
    }

    return 0;
}

int kvmppc_xive_post_load(SpaprXive *xive, int version_id)
{
    Error *local_err = NULL;
    CPUState *cs;
    int i;
    int ret;

    assert(xive->fd != -1);

    for (i = 0; i < xive->nr_ends; i++) {
        if (!xive_end_is_valid(&xive->endt[i])) {
            continue;
        }

        ret = kvmppc_xive_set_queue_config(xive, SPAPR_XIVE_BLOCK_ID, i,
                                           &xive->endt[i], &local_err);
        if (ret < 0) {
            goto fail;
        }
    }

    for (i = 0; i < xive->nr_irqs; i++) {
        if (!xive_eas_is_valid(&xive->eat[i])) {
            continue;
        }

        ret = kvmppc_xive_source_reset_one(&xive->source, i, &local_err);
        if (ret < 0) {
            goto fail;
        }

        ret = kvmppc_xive_set_source_config(xive, i, &xive->eat[i], &local_err);
        if (ret < 0) {
            goto fail;
        }
    }

    CPU_FOREACH(cs) {
        PowerPCCPU *cpu = POWERPC_CPU(cs);

        ret = kvmppc_xive_cpu_set_state(spapr_cpu_state(cpu)->tctx, &local_err);
        if (ret < 0) {
            goto fail;
        }
    }

    return 0;

fail:
    error_report_err(local_err);
    return ret;
}

static void *kvmppc_xive_mmap(SpaprXive *xive, int pgoff, size_t len,
                              Error **errp)
{
    void *addr;
    uint32_t page_shift = 16;

    addr = mmap(NULL, len, PROT_WRITE | PROT_READ, MAP_SHARED, xive->fd,
                pgoff << page_shift);
    if (addr == MAP_FAILED) {
        error_setg_errno(errp, errno, "XIVE: unable to set memory mapping");
    }

    return addr;
}

int kvmppc_xive_connect(SpaprInterruptController *intc, uint32_t nr_servers,
                        Error **errp)
{
    SpaprXive *xive = SPAPR_XIVE(intc);
    XiveSource *xsrc = &xive->source;
    uint64_t esb_len = xive_source_esb_len(xsrc);
    size_t tima_len = 4ull << TM_SHIFT;
    CPUState *cs;
    int fd;
    void *addr;
    int ret;

    if (xive->fd != -1) {
        return 0;
    }

    if (!kvmppc_has_cap_xive()) {
        error_setg(errp, "IRQ_XIVE capability must be present for KVM");
        return -1;
    }

    fd = kvm_create_device(kvm_state, KVM_DEV_TYPE_XIVE, false);
    if (fd < 0) {
        error_setg_errno(errp, -fd, "XIVE: error creating KVM device");
        return -1;
    }
    xive->fd = fd;

    if (kvm_device_check_attr(xive->fd, KVM_DEV_XIVE_GRP_CTRL,
                              KVM_DEV_XIVE_NR_SERVERS)) {
        ret = kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_CTRL,
                                KVM_DEV_XIVE_NR_SERVERS, &nr_servers, true,
                                errp);
        if (ret < 0) {
            goto fail;
        }
    }

    addr = kvmppc_xive_mmap(xive, KVM_XIVE_ESB_PAGE_OFFSET, esb_len, errp);
    if (addr == MAP_FAILED) {
        goto fail;
    }
    xsrc->esb_mmap = addr;

    memory_region_init_ram_device_ptr(&xsrc->esb_mmio_kvm, OBJECT(xsrc),
                                      "xive.esb-kvm", esb_len, xsrc->esb_mmap);
    memory_region_add_subregion_overlap(&xsrc->esb_mmio, 0,
                                        &xsrc->esb_mmio_kvm, 1);

    addr = kvmppc_xive_mmap(xive, KVM_XIVE_TIMA_PAGE_OFFSET, tima_len, errp);
    if (addr == MAP_FAILED) {
        goto fail;
    }
    xive->tm_mmap = addr;

    memory_region_init_ram_device_ptr(&xive->tm_mmio_kvm, OBJECT(xive),
                                      "xive.tima", tima_len, xive->tm_mmap);
    memory_region_add_subregion_overlap(&xive->tm_mmio, 0,
                                        &xive->tm_mmio_kvm, 1);

    xive->change = qemu_add_vm_change_state_handler(
        kvmppc_xive_change_state_handler, xive);

    CPU_FOREACH(cs) {
        PowerPCCPU *cpu = POWERPC_CPU(cs);

        ret = kvmppc_xive_cpu_connect(spapr_cpu_state(cpu)->tctx, errp);
        if (ret < 0) {
            goto fail;
        }
    }

    ret = kvmppc_xive_source_reset(xsrc, errp);
    if (ret < 0) {
        goto fail;
    }

    kvm_kernel_irqchip = true;
    kvm_msi_via_irqfd_allowed = true;
    kvm_gsi_direct_mapping = true;
    return 0;

fail:
    kvmppc_xive_disconnect(intc);
    return -1;
}

void kvmppc_xive_disconnect(SpaprInterruptController *intc)
{
    SpaprXive *xive = SPAPR_XIVE(intc);
    XiveSource *xsrc;
    uint64_t esb_len;

    assert(xive->fd != -1);

    xsrc = &xive->source;
    esb_len = xive_source_esb_len(xsrc);

    if (xsrc->esb_mmap) {
        memory_region_del_subregion(&xsrc->esb_mmio, &xsrc->esb_mmio_kvm);
        object_unparent(OBJECT(&xsrc->esb_mmio_kvm));
        munmap(xsrc->esb_mmap, esb_len);
        xsrc->esb_mmap = NULL;
    }

    if (xive->tm_mmap) {
        memory_region_del_subregion(&xive->tm_mmio, &xive->tm_mmio_kvm);
        object_unparent(OBJECT(&xive->tm_mmio_kvm));
        munmap(xive->tm_mmap, 4ull << TM_SHIFT);
        xive->tm_mmap = NULL;
    }

    close(xive->fd);
    xive->fd = -1;

    kvm_kernel_irqchip = false;
    kvm_msi_via_irqfd_allowed = false;
    kvm_gsi_direct_mapping = false;

    kvm_cpu_disable_all();

    if (xive->change) {
        qemu_del_vm_change_state_handler(xive->change);
        xive->change = NULL;
    }
}
