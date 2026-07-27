/*
 * s390x internal definitions and helpers
 *
 * Copyright (c) 2009 Ulrich Hecht
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef S390X_INTERNAL_H
#define S390X_INTERNAL_H

#include "cpu.h"
#include "fpu/softfloat.h"

#ifndef CONFIG_USER_ONLY
typedef struct LowCore {
    uint32_t        ccw1[2];
    uint32_t        ccw2[4];
    uint8_t         pad1[0x80 - 0x18];
    uint32_t        ext_params;
    uint16_t        cpu_addr;
    uint16_t        ext_int_code;
    uint16_t        svc_ilen;
    uint16_t        svc_code;
    uint16_t        pgm_ilen;
    uint16_t        pgm_code;
    uint32_t        data_exc_code;
    uint16_t        mon_class_num;
    uint16_t        per_perc_atmid;
    uint64_t        per_address;
    uint8_t         exc_access_id;
    uint8_t         per_access_id;
    uint8_t         op_access_id;
    uint8_t         ar_access_id;
    uint8_t         pad2[0xA8 - 0xA4];
    uint64_t        trans_exc_code;
    uint64_t        monitor_code;
    uint16_t        subchannel_id;
    uint16_t        subchannel_nr;
    uint32_t        io_int_parm;
    uint32_t        io_int_word;
    uint8_t         pad3[0xc8 - 0xc4];
    uint32_t        stfl_fac_list;
    uint8_t         pad4[0xe8 - 0xcc];
    uint64_t        mcic;
    uint8_t         pad5[0xf4 - 0xf0];
    uint32_t        external_damage_code;
    uint64_t        failing_storage_address;
    uint8_t         pad6[0x110 - 0x100];
    uint64_t        per_breaking_event_addr;
    uint8_t         pad7[0x120 - 0x118];
    PSW             restart_old_psw;
    PSW             external_old_psw;
    PSW             svc_old_psw;
    PSW             program_old_psw;
    PSW             mcck_old_psw;
    PSW             io_old_psw;
    uint8_t         pad8[0x1a0 - 0x180];
    PSW             restart_new_psw;
    PSW             external_new_psw;
    PSW             svc_new_psw;
    PSW             program_new_psw;
    PSW             mcck_new_psw;
    PSW             io_new_psw;
    uint8_t         pad13[0x11b0 - 0x200];

    uint64_t        mcesad;

    uint64_t        ext_params2;

    uint8_t         pad14[0x1200 - 0x11C0];

    uint64_t        floating_pt_save_area[16];
    uint64_t        gpregs_save_area[16];
    uint32_t        st_status_fixed_logout[4];
    uint8_t         pad15[0x1318 - 0x1310];
    uint32_t        prefixreg_save_area;
    uint32_t        fpt_creg_save_area;
    uint8_t         pad16[0x1324 - 0x1320];
    uint32_t        tod_progreg_save_area;
    uint64_t        cpu_timer_save_area;
    uint64_t        clock_comp_save_area;
    uint8_t         pad17[0x1340 - 0x1338];
    uint32_t        access_regs_save_area[16];
    uint64_t        cregs_save_area[16];

    uint8_t         pad18[0x2000 - 0x1400];
} QEMU_PACKED LowCore;
QEMU_BUILD_BUG_ON(sizeof(LowCore) != 8192);
#endif

#define MAX_ILEN 6

static inline int get_ilen(uint8_t opc)
{
    switch (opc >> 6) {
    case 0:
        return 2;
    case 1:
    case 2:
        return 4;
    default:
        return 6;
    }
}

static inline uint8_t get_per_atmid(CPUS390XState *env)
{
    return ((env->psw.mask & PSW_MASK_64) ?       (1 << 7) : 0) |
                                                  (1 << 6)      |
           ((env->psw.mask & PSW_MASK_32) ?       (1 << 5) : 0) |
           ((env->psw.mask & PSW_MASK_DAT) ?      (1 << 4) : 0) |
           ((env->psw.mask & PSW_ASC_SECONDARY) ? (1 << 3) : 0) |
           ((env->psw.mask & PSW_ASC_ACCREG) ?    (1 << 2) : 0);
}

static inline uint64_t wrap_address(CPUS390XState *env, uint64_t a)
{
    if (!(env->psw.mask & PSW_MASK_64)) {
        if (!(env->psw.mask & PSW_MASK_32)) {
            a &= 0x00ffffff;
        } else {
            a &= 0x7fffffff;
        }
    }
    return a;
}

enum cc_op {
    CC_OP_CONST0 = 0,
    CC_OP_CONST1,
    CC_OP_CONST2,
    CC_OP_CONST3,

    CC_OP_DYNAMIC,
    CC_OP_STATIC,

    CC_OP_NZ,
    CC_OP_ADDU,
    CC_OP_SUBU,

    CC_OP_LTGT_32,
    CC_OP_LTGT_64,
    CC_OP_LTUGTU_32,
    CC_OP_LTUGTU_64,
    CC_OP_LTGT0_32,
    CC_OP_LTGT0_64,

    CC_OP_ADD_64,
    CC_OP_SUB_64,
    CC_OP_ABS_64,
    CC_OP_NABS_64,
    CC_OP_MULS_64,

    CC_OP_ADD_32,
    CC_OP_SUB_32,
    CC_OP_ABS_32,
    CC_OP_NABS_32,
    CC_OP_MULS_32,

    CC_OP_COMP_32,
    CC_OP_COMP_64,

    CC_OP_TM_32,
    CC_OP_TM_64,

    CC_OP_NZ_F32,
    CC_OP_NZ_F64,
    CC_OP_NZ_F128,

    CC_OP_ICM,
    CC_OP_SLA,
    CC_OP_FLOGR,
    CC_OP_LCBB,
    CC_OP_VC,
    CC_OP_MAX
};

#ifndef CONFIG_USER_ONLY

static inline hwaddr decode_basedisp_s(CPUS390XState *env, uint32_t ipb,
                                       uint8_t *ar)
{
    hwaddr addr = 0;
    uint8_t reg;

    reg = ipb >> 28;
    if (reg > 0) {
        addr = env->regs[reg];
    }
    addr += (ipb >> 16) & 0xfff;
    if (ar) {
        *ar = reg;
    }

    return addr;
}

#define decode_basedisp_rs decode_basedisp_s

#endif

int s390_cpu_write_elf64_note(WriteCoreDumpFunction f, CPUState *cs,
                              int cpuid, DumpState *s);


const char *cc_name(enum cc_op cc_op);
uint32_t calc_cc(CPUS390XState *env, uint32_t cc_op, uint64_t src, uint64_t dst,
                 uint64_t vr);

#ifndef CONFIG_USER_ONLY
unsigned int s390_count_running_cpus(void);
void s390_cpu_halt(S390CPU *cpu);
void s390_cpu_unhalt(S390CPU *cpu);
void s390_cpu_system_init(Object *obj);
bool s390_cpu_system_realize(DeviceState *dev, Error **errp);
void s390_cpu_finalize(Object *obj);
void s390_cpu_system_class_init(CPUClass *cc);
void s390_cpu_machine_reset_cb(void *opaque);
bool s390_cpu_has_work(CPUState *cs);
#endif


void s390_cpu_model_class_register_props(ObjectClass *oc);
void s390_realize_cpu_model(CPUState *cs, Error **errp);
S390CPUModel *get_max_cpu_model(Error **errp);
void apply_cpu_model(const S390CPUModel *model, Error **errp);
ObjectClass *s390_cpu_class_by_name(const char *name);


void s390x_cpu_debug_excp_handler(CPUState *cs);
void s390_cpu_do_interrupt(CPUState *cpu);
bool s390_cpu_exec_interrupt(CPUState *cpu, int int_req);

#ifdef CONFIG_USER_ONLY
void s390_cpu_record_sigsegv(CPUState *cs, vaddr address,
                             MMUAccessType access_type,
                             bool maperr, uintptr_t retaddr);
void s390_cpu_record_sigbus(CPUState *cs, vaddr address,
                            MMUAccessType access_type, uintptr_t retaddr);
#else
bool s390_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                       MMUAccessType access_type, int mmu_idx,
                       bool probe, uintptr_t retaddr);
G_NORETURN void s390x_cpu_do_unaligned_access(CPUState *cs, vaddr addr,
                                              MMUAccessType access_type, int mmu_idx,
                                              uintptr_t retaddr);
#endif


uint32_t set_cc_nz_f32(float32 v);
uint32_t set_cc_nz_f64(float64 v);
uint32_t set_cc_nz_f128(float128 v);
#define S390_IEEE_MASK_INVALID   0x80
#define S390_IEEE_MASK_DIVBYZERO 0x40
#define S390_IEEE_MASK_OVERFLOW  0x20
#define S390_IEEE_MASK_UNDERFLOW 0x10
#define S390_IEEE_MASK_INEXACT   0x08
#define S390_IEEE_MASK_QUANTUM   0x04
uint8_t s390_softfloat_exc_to_ieee(unsigned int exc);
int s390_swap_bfp_rounding_mode(CPUS390XState *env, int m3);
void s390_restore_bfp_rounding_mode(CPUS390XState *env, int old_mode);
int float_comp_to_cc(CPUS390XState *env, FloatRelation float_compare);

#define DCMASK_ZERO             0x0c00
#define DCMASK_NORMAL           0x0300
#define DCMASK_SUBNORMAL        0x00c0
#define DCMASK_INFINITY         0x0030
#define DCMASK_QUIET_NAN        0x000c
#define DCMASK_SIGNALING_NAN    0x0003
#define DCMASK_NAN              0x000f
#define DCMASK_NEGATIVE         0x0555
uint16_t float32_dcmask(CPUS390XState *env, float32 f1);
uint16_t float64_dcmask(CPUS390XState *env, float64 f1);
uint16_t float128_dcmask(CPUS390XState *env, float128 f1);


int s390_cpu_gdb_read_register(CPUState *cpu, GByteArray *buf, int reg);
int s390_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);
void s390_cpu_gdb_init(CPUState *cs);


void s390_cpu_dump_state(CPUState *cpu, FILE *f, int flags);
void do_restart_interrupt(CPUS390XState *env);
#ifndef CONFIG_USER_ONLY
void s390_cpu_recompute_watchpoints(CPUState *cs);
void s390x_tod_timer(void *opaque);
void s390x_cpu_timer(void *opaque);
void s390_handle_wait(S390CPU *cpu);
hwaddr s390_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
hwaddr s390_cpu_get_phys_addr_debug(CPUState *cpu, vaddr addr);
LowCore *cpu_map_lowcore(CPUS390XState *env);
void cpu_unmap_lowcore(CPUS390XState *env, LowCore *lowcore);
#endif


void trigger_pgm_exception(CPUS390XState *env, uint32_t code);
#ifndef CONFIG_USER_ONLY
void cpu_inject_clock_comparator(S390CPU *cpu);
void cpu_inject_cpu_timer(S390CPU *cpu);
void cpu_inject_emergency_signal(S390CPU *cpu, uint16_t src_cpu_addr);
int cpu_inject_external_call(S390CPU *cpu, uint16_t src_cpu_addr);
bool s390_cpu_has_io_int(S390CPU *cpu);
bool s390_cpu_has_ext_int(S390CPU *cpu);
bool s390_cpu_has_mcck_int(S390CPU *cpu);
bool s390_cpu_has_int(S390CPU *cpu);
bool s390_cpu_has_restart_int(S390CPU *cpu);
bool s390_cpu_has_stop_int(S390CPU *cpu);
void cpu_inject_restart(S390CPU *cpu);
void cpu_inject_stop(S390CPU *cpu);
#endif


#ifndef CONFIG_USER_ONLY
void ioinst_handle_xsch(S390CPU *cpu, uint64_t reg1, uintptr_t ra);
void ioinst_handle_csch(S390CPU *cpu, uint64_t reg1, uintptr_t ra);
void ioinst_handle_hsch(S390CPU *cpu, uint64_t reg1, uintptr_t ra);
void ioinst_handle_msch(S390CPU *cpu, uint64_t reg1, uint32_t ipb,
                        uintptr_t ra);
void ioinst_handle_ssch(S390CPU *cpu, uint64_t reg1, uint32_t ipb,
                        uintptr_t ra);
void ioinst_handle_stcrw(S390CPU *cpu, uint32_t ipb, uintptr_t ra);
void ioinst_handle_stsch(S390CPU *cpu, uint64_t reg1, uint32_t ipb,
                         uintptr_t ra);
int ioinst_handle_tsch(S390CPU *cpu, uint64_t reg1, uint32_t ipb, uintptr_t ra);
void ioinst_handle_chsc(S390CPU *cpu, uint32_t ipb, uintptr_t ra);
void ioinst_handle_schm(S390CPU *cpu, uint64_t reg1, uint64_t reg2,
                        uint32_t ipb, uintptr_t ra);
void ioinst_handle_rsch(S390CPU *cpu, uint64_t reg1, uintptr_t ra);
void ioinst_handle_rchp(S390CPU *cpu, uint64_t reg1, uintptr_t ra);
void ioinst_handle_sal(S390CPU *cpu, uint64_t reg1, uintptr_t ra);
#endif


target_ulong mmu_real2abs(CPUS390XState *env, target_ulong raddr);
void probe_write_access(CPUS390XState *env, uint64_t addr, uint64_t len,
                        uintptr_t ra);


bool mmu_absolute_addr_valid(target_ulong addr, bool is_write);
#define MMU_S390_LRA        -1
int mmu_translate(CPUS390XState *env, target_ulong vaddr, int rw, uint64_t asc,
                  target_ulong *raddr, int *flags, uint64_t *tec);
int mmu_translate_real(CPUS390XState *env, target_ulong raddr, int rw,
                       target_ulong *addr, int *flags, uint64_t *tec);


int handle_diag_288(CPUS390XState *env, uint64_t r1, uint64_t r3);
void handle_diag_308(CPUS390XState *env, uint64_t r1, uint64_t r3,
                     uintptr_t ra);


void s390x_translate_init(void);
void s390x_translate_code(CPUState *cs, TranslationBlock *tb,
                          int *max_insns, vaddr pc, void *host_pc);
void s390x_restore_state_to_opc(CPUState *cs,
                                const TranslationBlock *tb,
                                const uint64_t *data);

int handle_sigp(CPUS390XState *env, uint8_t order, uint64_t r1, uint64_t r3);
void do_stop_interrupt(CPUS390XState *env);

#endif
