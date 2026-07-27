/*
 * QEMU PowerPC pSeries Logical Partition NUMA associativity handling
 *
 * Copyright IBM Corp. 2020
 *
 * Authors:
 *  Daniel Henrique Barboza      <danielhb413@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/ppc/spapr_numa.h"
#include "hw/pci-host/spapr.h"
#include "hw/ppc/fdt.h"

#define SPAPR_GPU_NUMA_ID           (cpu_to_be32(1))

static int get_max_dist_ref_points(SpaprMachineState *spapr)
{
    if (spapr_ovec_test(spapr->ov5_cas, OV5_FORM2_AFFINITY)) {
        return FORM2_DIST_REF_POINTS;
    }

    return FORM1_DIST_REF_POINTS;
}

static int get_numa_assoc_size(SpaprMachineState *spapr)
{
    if (spapr_ovec_test(spapr->ov5_cas, OV5_FORM2_AFFINITY)) {
        return FORM2_NUMA_ASSOC_SIZE;
    }

    return FORM1_NUMA_ASSOC_SIZE;
}

static int get_vcpu_assoc_size(SpaprMachineState *spapr)
{
    return get_numa_assoc_size(spapr) + 1;
}

static const uint32_t *get_associativity(SpaprMachineState *spapr, int node_id)
{
    if (spapr_ovec_test(spapr->ov5_cas, OV5_FORM2_AFFINITY)) {
        return spapr->FORM2_assoc_array[node_id];
    }
    return spapr->FORM1_assoc_array[node_id];
}

static int get_numa_distance(MachineState *ms, int src, int dst)
{
    NodeInfo *numa_info = ms->numa_state->nodes;
    int ret = numa_info[src].distance[dst];

    if (ret != 0) {
        return ret;
    }

    if (src == dst) {
        return NUMA_DISTANCE_MIN;
    }

    return NUMA_DISTANCE_DEFAULT;
}

static bool spapr_numa_is_symmetrical(MachineState *ms)
{
    int nb_numa_nodes = ms->numa_state->num_nodes;
    int src, dst;

    for (src = 0; src < nb_numa_nodes; src++) {
        for (dst = src; dst < nb_numa_nodes; dst++) {
            if (get_numa_distance(ms, src, dst) !=
                get_numa_distance(ms, dst, src)) {
                return false;
            }
        }
    }

    return true;
}

static uint8_t spapr_numa_get_numa_level(uint8_t distance)
{
    if (distance == 10) {
        return 0x4;
    } else if (distance > 11 && distance <= 30) {
        return 0x3;
    } else if (distance > 31 && distance <= 60) {
        return 0x2;
    } else if (distance > 61 && distance <= 120) {
        return 0x1;
    }

    return 0;
}

static void spapr_numa_define_FORM1_domains(SpaprMachineState *spapr)
{
    MachineState *ms = MACHINE(spapr);
    int nb_numa_nodes = ms->numa_state->num_nodes;
    int src, dst, i, j;

    for (i = 1; i < nb_numa_nodes; i++) {
        for (j = 1; j < FORM1_DIST_REF_POINTS; j++) {
            spapr->FORM1_assoc_array[i][j] = cpu_to_be32(i);
        }
    }

    for (src = 0; src < nb_numa_nodes; src++) {
        for (dst = src; dst < nb_numa_nodes; dst++) {
            uint8_t distance = get_numa_distance(ms, src, dst);
            uint8_t n_level = spapr_numa_get_numa_level(distance);
            uint32_t assoc_src;

            if (n_level == 0) {
                continue;
            }

            for (i = n_level; i > 0; i--) {
                assoc_src = spapr->FORM1_assoc_array[src][i];
                spapr->FORM1_assoc_array[dst][i] = assoc_src;
            }
        }
    }

}

static void spapr_numa_FORM1_affinity_check(MachineState *machine)
{
    int i;

    if (machine->numa_state->num_nodes) {
        for (i = 0; i < machine->numa_state->num_nodes; ++i) {
            if (machine->numa_state->nodes[i].node_mem == 0) {
                CPUState *cs;
                int found = 0;
                CPU_FOREACH(cs) {
                    PowerPCCPU *cpu = POWERPC_CPU(cs);
                    if (cpu->node_id == i) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    error_report(
"Memory-less/cpu-less nodes are not supported with FORM1 NUMA (node %d)", i);
                    exit(EXIT_FAILURE);
                }
            }
        }
    }

    if (!spapr_numa_is_symmetrical(machine)) {
        error_report(
"Asymmetrical NUMA topologies aren't supported in the pSeries machine using FORM1 NUMA");
        exit(EXIT_FAILURE);
    }
}

static void spapr_numa_FORM1_affinity_init(SpaprMachineState *spapr,
                                           MachineState *machine)
{
    SpaprMachineClass *smc = SPAPR_MACHINE_GET_CLASS(spapr);
    int nb_numa_nodes = machine->numa_state->num_nodes;
    int i, j;

    for (i = 0; i < nb_numa_nodes; i++) {
        spapr->FORM1_assoc_array[i][0] = cpu_to_be32(FORM1_DIST_REF_POINTS);
        spapr->FORM1_assoc_array[i][FORM1_DIST_REF_POINTS] = cpu_to_be32(i);
    }

    for (i = nb_numa_nodes; i < nb_numa_nodes; i++) {
        spapr->FORM1_assoc_array[i][0] = cpu_to_be32(FORM1_DIST_REF_POINTS);

        for (j = 1; j < FORM1_DIST_REF_POINTS; j++) {
            uint32_t gpu_assoc = smc->pre_5_1_assoc_refpoints ?
                                 SPAPR_GPU_NUMA_ID : cpu_to_be32(i);
            spapr->FORM1_assoc_array[i][j] = gpu_assoc;
        }

        spapr->FORM1_assoc_array[i][FORM1_DIST_REF_POINTS] = cpu_to_be32(i);
    }

    if (smc->pre_5_2_numa_associativity ||
        machine->numa_state->num_nodes <= 1) {
        return;
    }

    spapr_numa_define_FORM1_domains(spapr);
}

static void spapr_numa_FORM2_affinity_init(SpaprMachineState *spapr)
{
    int i;

    for (i = 0; i < NUMA_NODES_MAX_NUM; i++) {
        spapr->FORM2_assoc_array[i][0] = cpu_to_be32(1);
        spapr->FORM2_assoc_array[i][1] = cpu_to_be32(i);
    }
}

void spapr_numa_associativity_init(SpaprMachineState *spapr,
                                   MachineState *machine)
{
    spapr_numa_FORM1_affinity_init(spapr, machine);
    spapr_numa_FORM2_affinity_init(spapr);
}

void spapr_numa_associativity_check(SpaprMachineState *spapr)
{
    if (spapr_ovec_test(spapr->ov5_cas, OV5_FORM2_AFFINITY)) {
        return;
    }

    spapr_numa_FORM1_affinity_check(MACHINE(spapr));
}

void spapr_numa_write_associativity_dt(SpaprMachineState *spapr, void *fdt,
                                       int offset, int nodeid)
{
    const uint32_t *associativity = get_associativity(spapr, nodeid);

    _FDT((fdt_setprop(fdt, offset, "ibm,associativity",
                      associativity,
                      get_numa_assoc_size(spapr) * sizeof(uint32_t))));
}

static uint32_t *spapr_numa_get_vcpu_assoc(SpaprMachineState *spapr,
                                           PowerPCCPU *cpu)
{
    const uint32_t *associativity = get_associativity(spapr, cpu->node_id);
    int max_distance_ref_points = get_max_dist_ref_points(spapr);
    int vcpu_assoc_size = get_vcpu_assoc_size(spapr);
    uint32_t *vcpu_assoc = g_new(uint32_t, vcpu_assoc_size);
    int index = spapr_get_vcpu_id(cpu);

    vcpu_assoc[0] = cpu_to_be32(max_distance_ref_points + 1);
    vcpu_assoc[vcpu_assoc_size - 1] = cpu_to_be32(index);
    memcpy(vcpu_assoc + 1, associativity + 1,
           (vcpu_assoc_size - 2) * sizeof(uint32_t));

    return vcpu_assoc;
}

int spapr_numa_fixup_cpu_dt(SpaprMachineState *spapr, void *fdt,
                            int offset, PowerPCCPU *cpu)
{
    g_autofree uint32_t *vcpu_assoc = NULL;
    int vcpu_assoc_size = get_vcpu_assoc_size(spapr);

    vcpu_assoc = spapr_numa_get_vcpu_assoc(spapr, cpu);

    return fdt_setprop(fdt, offset, "ibm,associativity", vcpu_assoc,
                       vcpu_assoc_size * sizeof(uint32_t));
}


int spapr_numa_write_assoc_lookup_arrays(SpaprMachineState *spapr, void *fdt,
                                         int offset)
{
    MachineState *machine = MACHINE(spapr);
    int max_distance_ref_points = get_max_dist_ref_points(spapr);
    int nb_numa_nodes = machine->numa_state->num_nodes;
    int nr_nodes = nb_numa_nodes ? nb_numa_nodes : 1;
    g_autofree uint32_t *int_buf = NULL;
    uint32_t *cur_index;
    int i;

    int_buf = g_new0(uint32_t, nr_nodes * max_distance_ref_points + 2);
    cur_index = int_buf;
    int_buf[0] = cpu_to_be32(nr_nodes);
    int_buf[1] = cpu_to_be32(max_distance_ref_points);
    cur_index += 2;
    for (i = 0; i < nr_nodes; i++) {
        const uint32_t *associativity = get_associativity(spapr, i);
        memcpy(cur_index, ++associativity,
               sizeof(uint32_t) * max_distance_ref_points);
        cur_index += max_distance_ref_points;
    }

    return fdt_setprop(fdt, offset, "ibm,associativity-lookup-arrays",
                       int_buf, (cur_index - int_buf) * sizeof(uint32_t));
}

static void spapr_numa_FORM1_write_rtas_dt(SpaprMachineState *spapr,
                                           void *fdt, int rtas)
{
    MachineState *ms = MACHINE(spapr);
    SpaprMachineClass *smc = SPAPR_MACHINE_GET_CLASS(spapr);
    uint32_t refpoints[] = {
        cpu_to_be32(0x4),
        cpu_to_be32(0x3),
        cpu_to_be32(0x2),
        cpu_to_be32(0x1),
    };
    uint32_t nr_refpoints = ARRAY_SIZE(refpoints);
    uint32_t maxdomain = ms->numa_state->num_nodes;
    uint32_t maxdomains[] = {
        cpu_to_be32(4),
        cpu_to_be32(maxdomain),
        cpu_to_be32(maxdomain),
        cpu_to_be32(maxdomain),
        cpu_to_be32(maxdomain)
    };

    if (smc->pre_5_2_numa_associativity ||
        ms->numa_state->num_nodes <= 1) {
        uint32_t legacy_refpoints[] = {
            cpu_to_be32(0x4),
            cpu_to_be32(0x4),
            cpu_to_be32(0x2),
        };
        uint32_t legacy_maxdomains[] = {
            cpu_to_be32(4),
            cpu_to_be32(0),
            cpu_to_be32(0),
            cpu_to_be32(0),
            cpu_to_be32(maxdomain ? maxdomain : 1),
        };

        G_STATIC_ASSERT(sizeof(legacy_refpoints) <= sizeof(refpoints));
        G_STATIC_ASSERT(sizeof(legacy_maxdomains) <= sizeof(maxdomains));

        nr_refpoints = 3;

        memcpy(refpoints, legacy_refpoints, sizeof(legacy_refpoints));
        memcpy(maxdomains, legacy_maxdomains, sizeof(legacy_maxdomains));

        if (smc->pre_5_1_assoc_refpoints) {
            nr_refpoints = 2;
        }
    }

    _FDT(fdt_setprop(fdt, rtas, "ibm,associativity-reference-points",
                     refpoints, nr_refpoints * sizeof(refpoints[0])));

    _FDT(fdt_setprop(fdt, rtas, "ibm,max-associativity-domains",
                     maxdomains, sizeof(maxdomains)));
}

static void spapr_numa_FORM2_write_rtas_tables(SpaprMachineState *spapr,
                                               void *fdt, int rtas)
{
    MachineState *ms = MACHINE(spapr);
    int nb_numa_nodes = ms->numa_state->num_nodes;
    int distance_table_entries = nb_numa_nodes * nb_numa_nodes;
    g_autofree uint32_t *lookup_index_table = NULL;
    g_autofree uint8_t *distance_table = NULL;
    int src, dst, i, distance_table_size;

    lookup_index_table = g_new0(uint32_t, nb_numa_nodes + 1);
    lookup_index_table[0] = cpu_to_be32(nb_numa_nodes);

    for (i = 0; i < nb_numa_nodes; i++) {
        lookup_index_table[i + 1] = cpu_to_be32(i);
    }

    _FDT(fdt_setprop(fdt, rtas, "ibm,numa-lookup-index-table",
                     lookup_index_table,
                     (nb_numa_nodes + 1) * sizeof(uint32_t)));

    distance_table_size = distance_table_entries * sizeof(uint8_t) +
                          sizeof(uint32_t);
    distance_table = g_new0(uint8_t, distance_table_size);
    stl_be_p(distance_table, distance_table_entries);

    i = sizeof(uint32_t);

    for (src = 0; src < nb_numa_nodes; src++) {
        for (dst = 0; dst < nb_numa_nodes; dst++) {
            distance_table[i++] = get_numa_distance(ms, src, dst);
        }
    }

    _FDT(fdt_setprop(fdt, rtas, "ibm,numa-distance-table",
                     distance_table, distance_table_size));
}

static void spapr_numa_FORM2_write_rtas_dt(SpaprMachineState *spapr,
                                           void *fdt, int rtas)
{
    MachineState *ms = MACHINE(spapr);

    uint32_t refpoints[] = { cpu_to_be32(1) };

    uint32_t maxdomain = ms->numa_state->num_nodes;
    uint32_t maxdomains[] = { cpu_to_be32(1), cpu_to_be32(maxdomain) };

    _FDT(fdt_setprop(fdt, rtas, "ibm,associativity-reference-points",
                     refpoints, sizeof(refpoints)));

    _FDT(fdt_setprop(fdt, rtas, "ibm,max-associativity-domains",
                     maxdomains, sizeof(maxdomains)));

    spapr_numa_FORM2_write_rtas_tables(spapr, fdt, rtas);
}

void spapr_numa_write_rtas_dt(SpaprMachineState *spapr, void *fdt, int rtas)
{
    if (spapr_ovec_test(spapr->ov5_cas, OV5_FORM2_AFFINITY)) {
        spapr_numa_FORM2_write_rtas_dt(spapr, fdt, rtas);
        return;
    }

    spapr_numa_FORM1_write_rtas_dt(spapr, fdt, rtas);
}

static target_ulong h_home_node_associativity(PowerPCCPU *cpu,
                                              SpaprMachineState *spapr,
                                              target_ulong opcode,
                                              target_ulong *args)
{
    g_autofree uint32_t *vcpu_assoc = NULL;
    target_ulong flags = args[0];
    target_ulong procno = args[1];
    PowerPCCPU *tcpu;
    int idx, assoc_idx;
    int vcpu_assoc_size = get_vcpu_assoc_size(spapr);

    if (flags != 0x1) {
        return H_FUNCTION;
    }

    tcpu = spapr_find_cpu(procno);
    if (tcpu == NULL) {
        return H_P2;
    }

    g_assert((vcpu_assoc_size - 1) <= 12);

    vcpu_assoc = spapr_numa_get_vcpu_assoc(spapr, tcpu);
    assoc_idx = 1;

#define ASSOCIATIVITY(a, b) (((uint64_t)(a) << 32) | \
                             ((uint64_t)(b) & 0xffffffff))

    for (idx = 0; idx < 6; idx++) {
        int32_t a, b;

        a = assoc_idx < vcpu_assoc_size ?
            be32_to_cpu(vcpu_assoc[assoc_idx++]) : -1;
        b = assoc_idx < vcpu_assoc_size ?
            be32_to_cpu(vcpu_assoc[assoc_idx++]) : -1;

        args[idx] = ASSOCIATIVITY(a, b);
    }
#undef ASSOCIATIVITY

    return H_SUCCESS;
}

static void spapr_numa_register_types(void)
{
    spapr_register_hypercall(H_HOME_NODE_ASSOCIATIVITY,
                             h_home_node_associativity);
}

type_init(spapr_numa_register_types)
