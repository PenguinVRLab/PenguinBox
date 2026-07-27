/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * CPU Topology
 *
 * Copyright IBM Corp. 2022, 2023
 * Author(s): Pierre Morel <pmorel@linux.ibm.com>
 *
 * S390 topology handling can be divided in two parts:
 *
 * - The first part in this file is taking care of all common functions
 *   used by KVM and TCG to create and modify the topology.
 *
 * - The second part, building the topology information data for the
 *   guest with CPU and KVM specificity will be implemented inside
 *   the target/s390/kvm sub tree.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/qdev-properties.h"
#include "hw/boards.h"
#include "target/s390x/cpu.h"
#include "hw/s390x/s390-virtio-ccw.h"
#include "hw/s390x/cpu-topology.h"
#include "qapi/qapi-commands-machine-s390x.h"
#include "qapi/qapi-events-machine-s390x.h"

S390Topology s390_topology = {
    .cores_per_socket = NULL,
    .polarization = S390_CPU_POLARIZATION_HORIZONTAL,
};

static int s390_socket_nb_from_ids(int drawer_id, int book_id, int socket_id)
{
    return (drawer_id * current_machine->smp.books + book_id) *
           current_machine->smp.sockets + socket_id;
}

static int s390_socket_nb(S390CPU *cpu)
{
    return s390_socket_nb_from_ids(cpu->env.drawer_id, cpu->env.book_id,
                                   cpu->env.socket_id);
}

bool s390_has_topology(void)
{
    return s390_has_feat(S390_FEAT_CONFIGURATION_TOPOLOGY);
}

static void s390_topology_init(MachineState *ms)
{
    CpuTopology *smp = &ms->smp;

    s390_topology.cores_per_socket = g_new0(uint8_t, smp->sockets *
                                            smp->books * smp->drawers);
}

void s390_handle_ptf(S390CPU *cpu, uint8_t r1, uintptr_t ra)
{
    S390CpuPolarization polarization;
    CPUS390XState *env = &cpu->env;
    uint64_t reg = env->regs[r1];
    int fc = reg & S390_TOPO_FC_MASK;

    if (!s390_has_feat(S390_FEAT_CONFIGURATION_TOPOLOGY)) {
        s390_program_interrupt(env, PGM_OPERATION, ra);
        return;
    }

    if (env->psw.mask & PSW_MASK_PSTATE) {
        s390_program_interrupt(env, PGM_PRIVILEGED, ra);
        return;
    }

    if (reg & ~S390_TOPO_FC_MASK) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }

    polarization = S390_CPU_POLARIZATION_VERTICAL;
    switch (fc) {
    case 0:
        polarization = S390_CPU_POLARIZATION_HORIZONTAL;
        /* fallthrough */
    case 1:
        if (s390_topology.polarization == polarization) {
            env->regs[r1] |= S390_PTF_REASON_DONE;
            setcc(cpu, 2);
        } else {
            s390_topology.polarization = polarization;
            s390_cpu_topology_set_changed(true);
            qapi_event_send_cpu_polarization_change(polarization);
            setcc(cpu, 0);
        }
        break;
    default:
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }
}

void s390_topology_reset(void)
{
    s390_cpu_topology_set_changed(false);
    s390_topology.polarization = S390_CPU_POLARIZATION_HORIZONTAL;
}

static bool s390_topology_cpu_default(S390CPU *cpu, Error **errp)
{
    CpuTopology *smp = &current_machine->smp;
    CPUS390XState *env = &cpu->env;

    if ((env->socket_id < 0 || env->book_id < 0 || env->drawer_id < 0) &&
        (env->socket_id >= 0 || env->book_id >= 0 || env->drawer_id >= 0)) {
        error_setg(errp,
                   "Please define all or none of the topology geometry attributes");
        return false;
    }

    if (env->socket_id < 0) {
        env->socket_id = s390_std_socket(env->core_id, smp);
        env->book_id = s390_std_book(env->core_id, smp);
        env->drawer_id = s390_std_drawer(env->core_id, smp);
    }

    if (env->entitlement == S390_CPU_ENTITLEMENT_AUTO) {
        if (env->dedicated) {
            env->entitlement = S390_CPU_ENTITLEMENT_HIGH;
        } else {
            env->entitlement = S390_CPU_ENTITLEMENT_MEDIUM;
        }
    }
    return true;
}

static bool s390_topology_check(uint16_t socket_id, uint16_t book_id,
                                uint16_t drawer_id, uint16_t entitlement,
                                bool dedicated, Error **errp)
{
    CpuTopology *smp = &current_machine->smp;

    if (socket_id >= smp->sockets) {
        error_setg(errp, "Unavailable socket: %d", socket_id);
        return false;
    }
    if (book_id >= smp->books) {
        error_setg(errp, "Unavailable book: %d", book_id);
        return false;
    }
    if (drawer_id >= smp->drawers) {
        error_setg(errp, "Unavailable drawer: %d", drawer_id);
        return false;
    }
    if (entitlement >= S390_CPU_ENTITLEMENT__MAX) {
        error_setg(errp, "Unknown entitlement: %d", entitlement);
        return false;
    }
    if (dedicated && (entitlement == S390_CPU_ENTITLEMENT_LOW ||
                      entitlement == S390_CPU_ENTITLEMENT_MEDIUM)) {
        error_setg(errp, "A dedicated CPU implies high entitlement");
        return false;
    }
    return true;
}

static bool s390_topology_need_report(S390CPU *cpu, int drawer_id,
                                      int book_id, int socket_id,
                                      uint16_t entitlement, bool dedicated)
{
    return cpu->env.drawer_id != drawer_id ||
           cpu->env.book_id != book_id ||
           cpu->env.socket_id != socket_id ||
           cpu->env.entitlement != entitlement ||
           cpu->env.dedicated != dedicated;
}

static void s390_update_cpu_props(MachineState *ms, S390CPU *cpu)
{
    CpuInstanceProperties *props;

    props = &ms->possible_cpus->cpus[cpu->env.core_id].props;

    props->socket_id = cpu->env.socket_id;
    props->book_id = cpu->env.book_id;
    props->drawer_id = cpu->env.drawer_id;
}

void s390_topology_setup_cpu(MachineState *ms, S390CPU *cpu, Error **errp)
{
    int entry;

    if (!s390_topology.cores_per_socket) {
        s390_topology_init(ms);
    }

    if (!s390_topology_cpu_default(cpu, errp)) {
        return;
    }

    if (!s390_topology_check(cpu->env.socket_id, cpu->env.book_id,
                             cpu->env.drawer_id, cpu->env.entitlement,
                             cpu->env.dedicated, errp)) {
        return;
    }

    entry = s390_socket_nb(cpu);
    if (s390_topology.cores_per_socket[entry] >= ms->smp.cores) {
        error_setg(errp, "No more space on this socket");
        return;
    }

    s390_topology.cores_per_socket[entry] += 1;

    s390_update_cpu_props(ms, cpu);
}

static void s390_change_topology(uint16_t core_id,
                                 bool has_socket_id, uint16_t socket_id,
                                 bool has_book_id, uint16_t book_id,
                                 bool has_drawer_id, uint16_t drawer_id,
                                 bool has_entitlement,
                                 S390CpuEntitlement entitlement,
                                 bool has_dedicated, bool dedicated,
                                 Error **errp)
{
    MachineState *ms = current_machine;
    int old_socket_entry;
    int new_socket_entry;
    bool report_needed;
    S390CPU *cpu;

    cpu = s390_cpu_addr2state(core_id);
    if (!cpu) {
        error_setg(errp, "Core-id %d does not exist!", core_id);
        return;
    }

    if (!has_socket_id) {
        socket_id = cpu->env.socket_id;
    }
    if (!has_book_id) {
        book_id = cpu->env.book_id;
    }
    if (!has_drawer_id) {
        drawer_id = cpu->env.drawer_id;
    }
    if (!has_dedicated) {
        dedicated = cpu->env.dedicated;
    }

    if (!has_entitlement || entitlement == S390_CPU_ENTITLEMENT_AUTO) {
        if (dedicated) {
            entitlement = S390_CPU_ENTITLEMENT_HIGH;
        } else {
            entitlement = S390_CPU_ENTITLEMENT_MEDIUM;
        }
    }

    if (!s390_topology_check(socket_id, book_id, drawer_id,
                             entitlement, dedicated, errp)) {
        return;
    }

    old_socket_entry = s390_socket_nb(cpu);
    new_socket_entry = s390_socket_nb_from_ids(drawer_id, book_id, socket_id);

    if (new_socket_entry != old_socket_entry) {
        if (s390_topology.cores_per_socket[new_socket_entry] >=
            ms->smp.cores) {
            error_setg(errp, "No more space on this socket");
            return;
        }
        s390_topology.cores_per_socket[new_socket_entry] += 1;
        s390_topology.cores_per_socket[old_socket_entry] -= 1;
    }

    report_needed = s390_topology_need_report(cpu, drawer_id, book_id,
                                              socket_id, entitlement,
                                              dedicated);

    cpu->env.drawer_id = drawer_id;
    cpu->env.book_id = book_id;
    cpu->env.socket_id = socket_id;
    cpu->env.dedicated = dedicated;
    cpu->env.entitlement = entitlement;

    s390_update_cpu_props(ms, cpu);

    if (report_needed) {
        s390_cpu_topology_set_changed(true);
    }
}

void qmp_set_cpu_topology(uint16_t core,
                          bool has_socket, uint16_t socket,
                          bool has_book, uint16_t book,
                          bool has_drawer, uint16_t drawer,
                          bool has_entitlement, S390CpuEntitlement entitlement,
                          bool has_dedicated, bool dedicated,
                          Error **errp)
{
    if (!s390_has_topology()) {
        error_setg(errp, "This machine doesn't support topology");
        return;
    }

    s390_change_topology(core, has_socket, socket, has_book, book,
                         has_drawer, drawer, has_entitlement, entitlement,
                         has_dedicated, dedicated, errp);
}

CpuPolarizationInfo *qmp_query_s390x_cpu_polarization(Error **errp)
{
    CpuPolarizationInfo *info = g_new0(CpuPolarizationInfo, 1);

    info->polarization = s390_topology.polarization;
    return info;
}
