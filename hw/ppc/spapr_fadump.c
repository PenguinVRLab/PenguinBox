/*
 * Firmware Assisted Dump in PSeries
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/ppc/spapr.h"
#include "qemu/units.h"
#include "system/cpus.h"
#include "system/hw_accel.h"
#include <math.h>

static uint64_t fadump_str_to_u64(const char *str)
{
    uint64_t val = 0;
    int i;

    for (i = 0; i < sizeof(val); i++) {
        val = (*str) ? (val << 8) | *str++ : val << 8;
    }
    return val;
}

static uint64_t fadump_gpr_id_to_u64(uint32_t gpr_id)
{
    uint64_t val = 0;

    assert(gpr_id < 32);

    if (gpr_id <= 9) {
        val = fadump_str_to_u64("GPR0");
    } else if (gpr_id <= 19) {
        val = fadump_str_to_u64("GPR1");
    } else if (gpr_id <= 29) {
        val = fadump_str_to_u64("GPR2");
    } else {
        val = fadump_str_to_u64("GPR3");
    }

    val |= 0x30000000;
    val |= ((gpr_id % 10) << 24);

    return val;
}

uint32_t do_fadump_register(SpaprMachineState *spapr, target_ulong args)
{
    FadumpSectionHeader header;
    FadumpSection regions[FADUMP_MAX_SECTIONS] = {0};
    target_ulong fdm_addr = rtas_ld(args, 1);
    target_ulong fdm_size = rtas_ld(args, 2);
    AddressSpace *default_as = &address_space_memory;
    MemTxResult io_result;
    MemTxAttrs attrs;
    uint64_t next_section_addr;
    uint16_t dump_num_sections;

    attrs.user = 0;
    attrs.memory = 1;

    if (spapr->fadump_registered) {
        return RTAS_OUT_DUMP_ALREADY_REGISTERED;
    }

    if (spapr->fadump_dump_active) {
        return RTAS_OUT_DUMP_ACTIVE;
    }

    if (fdm_size < sizeof(FadumpSectionHeader)) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "FADump: Header size is invalid: " TARGET_FMT_lu "\n", fdm_size);
        return RTAS_OUT_PARAM_ERROR;
    }

    if ((fdm_addr <= 0) || ((fdm_addr + fdm_size) > spapr->rma_size)) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "FADump: Invalid fdm address: " TARGET_FMT_lu "\n", fdm_addr);
        return RTAS_OUT_PARAM_ERROR;
    }

    io_result = address_space_read(default_as, fdm_addr, attrs,
            &header, sizeof(header));
    if (io_result != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "FADump: Unable to read fdm: " TARGET_FMT_lu "\n", fdm_addr);

        return RTAS_OUT_HW_ERROR;
    }

    if (header.dump_format_version != cpu_to_be32(FADUMP_VERSION)) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "FADump: Unknown fadump header version: 0x%x\n",
            header.dump_format_version);
        return RTAS_OUT_PARAM_ERROR;
    }

    header.dump_status_flag = 0;

    dump_num_sections = be16_to_cpu(header.dump_num_sections);

    if (dump_num_sections > FADUMP_MAX_SECTIONS) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "FADump: Too many sections: %d sections\n", dump_num_sections);
        return RTAS_OUT_PARAM_ERROR;
    }

    next_section_addr =
        fdm_addr +
        be32_to_cpu(header.offset_first_dump_section);

    for (int i = 0; i < dump_num_sections; ++i) {
        io_result = address_space_read(default_as, next_section_addr, attrs,
                &regions[i], sizeof(regions[i]));
        if (io_result != MEMTX_OK) {
            qemu_log_mask(LOG_UNIMP,
                "FADump: Unable to read fadump %dth section\n", i);
            return RTAS_OUT_PARAM_ERROR;
        }

        next_section_addr += sizeof(regions[i]);
    }

    spapr->fadump_registered = true;
    spapr->fadump_dump_active = false;

    spapr->registered_fdm.header = header;
    for (int i = 0; i < dump_num_sections; ++i) {
        spapr->registered_fdm.rgn[i] = regions[i];
    }

    return RTAS_OUT_SUCCESS;
}

static bool do_preserve_region(FadumpSection *region)
{
    AddressSpace *default_as = &address_space_memory;
    MemTxResult io_result;
    MemTxAttrs attrs;
    uint64_t src_addr, src_len, dest_addr;
    uint64_t num_chunks;
    g_autofree void *copy_buffer = NULL;

    src_addr  = be64_to_cpu(region->source_address);
    src_len   = be64_to_cpu(region->source_len);
    dest_addr = be64_to_cpu(region->destination_address);

    attrs.user = 0;
    attrs.memory = 1;

    if (src_addr == dest_addr) {
        region->bytes_dumped = cpu_to_be64(src_len);
        return true;
    }

#define FADUMP_CHUNK_SIZE  ((size_t)(32 * MiB))
    copy_buffer = g_try_malloc(FADUMP_CHUNK_SIZE);
    if (copy_buffer == NULL) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "FADump: Failed allocating memory (size: %zu) for copying"
            " reserved memory regions\n", FADUMP_CHUNK_SIZE);
        return false;
    }

    num_chunks = ceil((src_len * 1.0f) / FADUMP_CHUNK_SIZE);
    for (uint64_t chunk_id = 0; chunk_id < num_chunks; ++chunk_id) {
        uint64_t copy_len = MIN(
                        src_len - (chunk_id * FADUMP_CHUNK_SIZE),
                        FADUMP_CHUNK_SIZE
                    );

        io_result = address_space_read(default_as, src_addr, attrs,
                copy_buffer, copy_len);
        if ((io_result & MEMTX_DECODE_ERROR) ||
            (io_result & MEMTX_ACCESS_ERROR)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                "FADump: Failed to decode/access address in section: %d\n",
                region->source_data_type);

            region->error_flags = FADUMP_ERROR_INVALID_SOURCE_ADDR;
            region->bytes_dumped = 0;
            return true;
        } else if (io_result != MEMTX_OK) {
            qemu_log_mask(LOG_GUEST_ERROR,
                "FADump: Failed to read source region in section: %d\n",
                region->source_data_type);

            return false;
        }

        io_result = address_space_write(default_as, dest_addr, attrs,
                copy_buffer, copy_len);
        if ((io_result & MEMTX_DECODE_ERROR) ||
            (io_result & MEMTX_ACCESS_ERROR)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                "FADump: Failed to decode/access address in section: %d\n",
                region->source_data_type);

            region->error_flags = FADUMP_ERROR_INVALID_DEST_ADDR;
            region->bytes_dumped = 0;
            return true;
        } else if (io_result != MEMTX_OK) {
            qemu_log_mask(LOG_GUEST_ERROR,
                "FADump: Failed to write destination in section: %d\n",
                region->source_data_type);

            return false;
        }

        src_addr += FADUMP_CHUNK_SIZE;
        dest_addr += FADUMP_CHUNK_SIZE;
    }
#undef FADUMP_CHUNK_SIZE

    region->bytes_dumped = cpu_to_be64(src_len);
    return true;
}

static FadumpRegEntry *populate_cpu_reg_entries(CPUState *cpu,
        FadumpRegEntry *curr_reg_entry)
{
    CPUPPCState *env;
    PowerPCCPU *ppc_cpu;
    uint32_t num_regs_per_cpu = 0;

    ppc_cpu = POWERPC_CPU(cpu);
    env = cpu_env(cpu);
    num_regs_per_cpu = 0;

    curr_reg_entry->reg_id =
        cpu_to_be64(fadump_str_to_u64("CPUSTRT"));
    curr_reg_entry->reg_value = cpu_to_be64(
            ppc_cpu->vcpu_id & FADUMP_CPU_ID_MASK);
    ++curr_reg_entry;

#define REG_ENTRY(id, val)                             \
    do {                                               \
        curr_reg_entry->reg_id =                       \
            cpu_to_be64(fadump_str_to_u64(#id));       \
        curr_reg_entry->reg_value = cpu_to_be64(val);  \
        ++curr_reg_entry;                              \
        ++num_regs_per_cpu;                            \
    } while (0)

    REG_ENTRY(ACOP, env->spr[SPR_ACOP]);
    REG_ENTRY(AMR, env->spr[SPR_AMR]);
    REG_ENTRY(BESCR, env->spr[SPR_BESCR]);
    REG_ENTRY(CFAR, env->spr[SPR_CFAR]);
    REG_ENTRY(CIABR, env->spr[SPR_CIABR]);

    REG_ENTRY(CR, ppc_get_cr(env));

    REG_ENTRY(CTR, env->spr[SPR_CTR]);
    REG_ENTRY(CTRL, env->spr[SPR_CTRL]);
    REG_ENTRY(DABR, env->spr[SPR_DABR]);
    REG_ENTRY(DABRX, env->spr[SPR_DABRX]);
    REG_ENTRY(DAR, env->spr[SPR_DAR]);
    REG_ENTRY(DAWR0, env->spr[SPR_DAWR0]);
    REG_ENTRY(DAWR1, env->spr[SPR_DAWR1]);
    REG_ENTRY(DAWRX0, env->spr[SPR_DAWRX0]);
    REG_ENTRY(DAWRX1, env->spr[SPR_DAWRX1]);
    REG_ENTRY(DPDES, env->spr[SPR_DPDES]);
    REG_ENTRY(DSCR, env->spr[SPR_DSCR]);
    REG_ENTRY(DSISR, env->spr[SPR_DSISR]);
    REG_ENTRY(EBBHR, env->spr[SPR_EBBHR]);
    REG_ENTRY(EBBRR, env->spr[SPR_EBBRR]);

    REG_ENTRY(FPSCR, env->fpscr);
    REG_ENTRY(FSCR, env->spr[SPR_FSCR]);

    for (int gpr_id = 0; gpr_id < 32; ++gpr_id) {
        curr_reg_entry->reg_id =
            cpu_to_be64(fadump_gpr_id_to_u64(gpr_id));
        curr_reg_entry->reg_value =
            cpu_to_be64(env->gpr[gpr_id]);
        ++curr_reg_entry;
        ++num_regs_per_cpu;
    }

    REG_ENTRY(IAMR, env->spr[SPR_IAMR]);
    REG_ENTRY(IC, env->spr[SPR_IC]);
    REG_ENTRY(LR, env->spr[SPR_LR]);

    REG_ENTRY(MSR, env->msr);
    REG_ENTRY(NIA, env->nip);
    REG_ENTRY(PIR, env->spr[SPR_PIR]);
    REG_ENTRY(PSPB, env->spr[SPR_PSPB]);
    REG_ENTRY(PVR, env->spr[SPR_PVR]);
    REG_ENTRY(RPR, env->spr[SPR_RPR]);
    REG_ENTRY(SPURR, env->spr[SPR_SPURR]);
    REG_ENTRY(SRR0, env->spr[SPR_SRR0]);
    REG_ENTRY(SRR1, env->spr[SPR_SRR1]);
    REG_ENTRY(TAR, env->spr[SPR_TAR]);
    REG_ENTRY(TEXASR, env->spr[SPR_TEXASR]);
    REG_ENTRY(TFHAR, env->spr[SPR_TFHAR]);
    REG_ENTRY(TFIAR, env->spr[SPR_TFIAR]);
    REG_ENTRY(TIR, env->spr[SPR_TIR]);
    REG_ENTRY(UAMOR, env->spr[SPR_UAMOR]);
    REG_ENTRY(VRSAVE, env->spr[SPR_VRSAVE]);
    REG_ENTRY(VSCR, env->vscr);
    REG_ENTRY(VTB, env->spr[SPR_VTB]);
    REG_ENTRY(WORT, env->spr[SPR_WORT]);
    REG_ENTRY(XER, env->spr[SPR_XER]);

#undef REG_ENTRY

    curr_reg_entry->reg_id =
        cpu_to_be64(fadump_str_to_u64("CPUEND"));
    curr_reg_entry->reg_value = cpu_to_be64(
            ppc_cpu->vcpu_id & FADUMP_CPU_ID_MASK);

    assert(FADUMP_PER_CPU_REG_ENTRIES == num_regs_per_cpu + 2 );

    ++curr_reg_entry;

    return curr_reg_entry;
}

static void *get_cpu_state_data(uint64_t *cpu_state_len)
{
    FadumpRegSaveAreaHeader reg_save_hdr;
    g_autofree FadumpRegEntry *reg_entries = NULL;
    FadumpRegEntry *curr_reg_entry;
    CPUState *cpu;

    uint32_t num_reg_entries;
    uint32_t reg_entries_size;
    uint32_t num_cpus = 0;

    void *cpu_state_buffer = NULL;
    uint64_t offset = 0;

    CPU_FOREACH(cpu) {
        ++num_cpus;
    }

    reg_save_hdr.version = cpu_to_be32(0);
    reg_save_hdr.magic_number =
        cpu_to_be64(fadump_str_to_u64("REGSAVE"));

    reg_save_hdr.num_cpu_offset =
        cpu_to_be32(sizeof(FadumpRegSaveAreaHeader));

    num_reg_entries = num_cpus * FADUMP_PER_CPU_REG_ENTRIES;
    reg_entries_size = num_reg_entries * sizeof(FadumpRegEntry);

    reg_entries = g_new(FadumpRegEntry, num_reg_entries);

    curr_reg_entry = reg_entries;

    CPU_FOREACH(cpu) {
        cpu_synchronize_state(cpu);
        curr_reg_entry = populate_cpu_reg_entries(cpu, curr_reg_entry);
    }

    *cpu_state_len = 0;
    *cpu_state_len += sizeof(reg_save_hdr);
    *cpu_state_len += 0xc;
    *cpu_state_len += sizeof(num_cpus);
    *cpu_state_len += reg_entries_size;

    cpu_state_buffer = g_malloc(*cpu_state_len);

    memcpy(cpu_state_buffer + offset,
            &reg_save_hdr, sizeof(reg_save_hdr));
    offset += sizeof(reg_save_hdr);

    num_cpus = cpu_to_be32(num_cpus);
    memcpy(cpu_state_buffer + offset, &num_cpus, sizeof(num_cpus));
    offset += sizeof(num_cpus);

    memcpy(cpu_state_buffer + offset, reg_entries, reg_entries_size);
    offset += reg_entries_size;

    return cpu_state_buffer;
}

static bool do_populate_cpu_state(FadumpSection *region)
{
    uint64_t dest_addr = be64_to_cpu(region->destination_address);
    uint64_t cpu_state_len = 0;
    g_autofree void *cpu_state_buffer = NULL;
    AddressSpace *default_as = &address_space_memory;
    MemTxResult io_result;
    MemTxAttrs attrs;

    assert(region->source_data_type == cpu_to_be16(FADUMP_CPU_STATE_DATA));

    attrs.user = 0;
    attrs.memory = 1;

    cpu_state_buffer  = get_cpu_state_data(&cpu_state_len);

    io_result = address_space_write(default_as, dest_addr, attrs,
            cpu_state_buffer, cpu_state_len);
    if ((io_result & MEMTX_DECODE_ERROR) ||
        (io_result & MEMTX_ACCESS_ERROR)) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "FADump: Failed to decode/access address in CPU State Region's"
            " destination address: 0x%016" PRIx64 "\n", dest_addr);

        region->error_flags = FADUMP_ERROR_INVALID_SOURCE_ADDR;
        region->bytes_dumped = 0;
        return true;
    } else if (io_result != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "FADump: Failed to write CPU state region.\n");

        return false;
    }

    region->bytes_dumped = cpu_to_be64(cpu_state_len);

    if (region->source_len != region->bytes_dumped) {
        qemu_log_mask(LOG_GUEST_ERROR,
                "FADump: Mismatch in CPU State region's length exported:"
                " Kernel expected: 0x%" PRIx64 " bytes,"
                " QEMU exported: 0x%" PRIx64 " bytes\n",
                be64_to_cpu(region->source_len),
                be64_to_cpu(region->bytes_dumped));
    }

    return true;
}

static bool fadump_preserve_mem(SpaprMachineState *spapr)
{
    FadumpMemStruct *fdm = &spapr->registered_fdm;
    uint16_t dump_num_sections, data_type;

    assert(spapr->fadump_registered);

    dump_num_sections = be16_to_cpu(fdm->header.dump_num_sections);
    for (int i = 0; i < dump_num_sections; ++i) {
        data_type = be16_to_cpu(fdm->rgn[i].source_data_type);

        fdm->rgn[i].error_flags = 0;
        fdm->rgn[i].bytes_dumped = 0;

        if (be32_to_cpu(fdm->rgn[i].request_flag) != FADUMP_REQUEST_FLAG) {
            qemu_log_mask(LOG_UNIMP,
                "FADump: Skipping copying region as not requested\n");
            continue;
        }

        switch (data_type) {
        case FADUMP_CPU_STATE_DATA:
            if (!do_populate_cpu_state(&fdm->rgn[i])) {
                qemu_log_mask(LOG_GUEST_ERROR,
                    "FADump: Failed to store CPU State Data");
                fdm->header.dump_status_flag |=
                    cpu_to_be16(FADUMP_STATUS_DUMP_ERROR);

                return false;
            }

            break;
        case FADUMP_HPTE_REGION:
            break;
        case FADUMP_REAL_MODE_REGION:
        case FADUMP_PARAM_AREA:
            if (!do_preserve_region(&fdm->rgn[i])) {
                qemu_log_mask(LOG_GUEST_ERROR,
                    "FADump: Failed to preserve dump section: %d\n",
                    be16_to_cpu(fdm->rgn[i].source_data_type));
                fdm->header.dump_status_flag |=
                    cpu_to_be16(FADUMP_STATUS_DUMP_ERROR);
            }

            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                "FADump: Skipping unknown source data type: %d\n", data_type);

            fdm->rgn[i].error_flags =
                cpu_to_be16(FADUMP_ERROR_INVALID_DATA_TYPE);
        }
    }

    return true;
}

void trigger_fadump_boot(SpaprMachineState *spapr, target_ulong spapr_retcode)
{
    FadumpSectionHeader *header = &spapr->registered_fdm.header;

    pause_all_vcpus();

    if (!fadump_preserve_mem(spapr)) {
        rtas_st(spapr_retcode, 0, RTAS_OUT_HW_ERROR);

        qemu_system_guest_panicked(NULL);
        return;
    }

    header->dump_status_flag = cpu_to_be16(
            FADUMP_STATUS_DUMP_TRIGGERED |
            FADUMP_STATUS_DUMP_PERFORMED
    );

    spapr->fadump_registered = false;
    spapr->fadump_dump_active = true;

    qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);

    rtas_st(spapr_retcode, 0, RTAS_OUT_SUCCESS);
}
