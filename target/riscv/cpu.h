/*
 * QEMU RISC-V CPU
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef RISCV_CPU_H
#define RISCV_CPU_H

#include "hw/core/cpu.h"
#include "hw/registerfields.h"
#include "hw/qdev-properties.h"
#include "exec/cpu-common.h"
#include "exec/cpu-defs.h"
#include "exec/cpu-interrupt.h"
#include "exec/gdbstub.h"
#include "qemu/cpu-float.h"
#include "qom/object.h"
#include "qemu/int128.h"
#include "cpu_bits.h"
#include "cpu_cfg.h"
#include "qapi/qapi-types-common.h"
#include "cpu-qom.h"

typedef struct CPUArchState CPURISCVState;

#define CPU_RESOLVING_TYPE TYPE_RISCV_CPU

#if defined(TARGET_RISCV32)
# define TYPE_RISCV_CPU_BASE            TYPE_RISCV_CPU_BASE32
#elif defined(TARGET_RISCV64)
# define TYPE_RISCV_CPU_BASE            TYPE_RISCV_CPU_BASE64
#endif

#define RISCV_UW2_ALWAYS_STORE_AMO 1

#define RV(x) BIT(x - 'A')

#define RVI RV('I')
#define RVE RV('E')
#define RVM RV('M')
#define RVA RV('A')
#define RVF RV('F')
#define RVD RV('D')
#define RVV RV('V')
#define RVC RV('C')
#define RVS RV('S')
#define RVU RV('U')
#define RVH RV('H')
#define RVG RV('G')
#define RVB RV('B')

extern const uint32_t misa_bits[];
const char *riscv_get_misa_ext_name(uint32_t bit);
const char *riscv_get_misa_ext_description(uint32_t bit);

#define CPU_CFG_OFFSET(_prop) offsetof(struct RISCVCPUConfig, _prop)
#define ENV_CSR_OFFSET(_csr) offsetof(CPURISCVState, _csr)

typedef struct riscv_cpu_profile {
    struct riscv_cpu_profile *u_parent;
    struct riscv_cpu_profile *s_parent;
    const char *name;
    uint32_t misa_ext;
    bool enabled;
    bool present;
    bool user_set;
    int priv_spec;
    int satp_mode;
    const int32_t ext_offsets[];
} RISCVCPUProfile;

#define RISCV_PROFILE_EXT_LIST_END -1
#define RISCV_PROFILE_ATTR_UNUSED -1

extern RISCVCPUProfile *riscv_profiles[];

#define PRIV_VER_1_10_0_STR "v1.10.0"
#define PRIV_VER_1_11_0_STR "v1.11.0"
#define PRIV_VER_1_12_0_STR "v1.12.0"
#define PRIV_VER_1_13_0_STR "v1.13.0"
enum {
    PRIV_VERSION_1_10_0 = 0,
    PRIV_VERSION_1_11_0,
    PRIV_VERSION_1_12_0,
    PRIV_VERSION_1_13_0,

    PRIV_VERSION_LATEST = PRIV_VERSION_1_13_0,
};

#define VEXT_VERSION_1_00_0 0x00010000
#define VEXT_VER_1_00_0_STR "v1.0"

enum {
    TRANSLATE_SUCCESS,
    TRANSLATE_FAIL,
    TRANSLATE_PMP_FAIL,
    TRANSLATE_G_STAGE_FAIL
};

typedef enum {
    EXT_STATUS_DISABLED = 0,
    EXT_STATUS_INITIAL,
    EXT_STATUS_CLEAN,
    EXT_STATUS_DIRTY,
} RISCVExtStatus;

typedef enum {
    PMM_FIELD_DISABLED = 0,
    PMM_FIELD_RESERVED = 1,
    PMM_FIELD_PMLEN7   = 2,
    PMM_FIELD_PMLEN16  = 3,
} RISCVPmPmm;

typedef struct riscv_cpu_implied_exts_rule {
#ifndef CONFIG_USER_ONLY
    unsigned long *enabled;
#endif
    bool is_misa;
    const uint32_t ext;
    const uint32_t implied_misa_exts;
    const uint32_t implied_multi_exts[];
} RISCVCPUImpliedExtsRule;

extern RISCVCPUImpliedExtsRule *riscv_misa_ext_implied_rules[];
extern RISCVCPUImpliedExtsRule *riscv_multi_ext_implied_rules[];

#define RISCV_IMPLIED_EXTS_RULE_END -1

#define MMU_USER_IDX 3

#define MAX_RISCV_PMPS (64)
#define OLD_MAX_RISCV_PMPS (16)
#define MIN_RISCV_PMP_GRANULARITY 4

#if !defined(CONFIG_USER_ONLY)
#include "pmp.h"
#include "debug.h"
#endif

#define RV_VLEN_MAX 1024
#define RV_MAX_MHPMEVENTS 32
#define RV_MAX_MHPMCOUNTERS 32

FIELD(VTYPE, VLMUL, 0, 3)
FIELD(VTYPE, VSEW, 3, 3)
FIELD(VTYPE, VTA, 6, 1)
FIELD(VTYPE, VMA, 7, 1)
FIELD(VTYPE, VEDIV, 8, 2)
FIELD(VTYPE, RESERVED, 10, sizeof(target_ulong) * 8 - 11)

typedef struct PMUCTRState {
    target_ulong mhpmcounter_val;
    target_ulong mhpmcounterh_val;
    target_ulong mhpmcounter_prev;
    target_ulong mhpmcounterh_prev;
    target_ulong irq_overflow_left;
} PMUCTRState;

typedef struct PMUFixedCtrState {
        uint64_t counter[4];
        uint64_t counter_prev[4];
        uint64_t counter_virt[2];
        uint64_t counter_virt_prev[2];
} PMUFixedCtrState;

struct CPUArchState {
    target_ulong gpr[32];
    target_ulong gprh[32];

    uint64_t vreg[32 * RV_VLEN_MAX / 64] QEMU_ALIGNED(16);
    target_ulong vxrm;
    target_ulong vxsat;
    target_ulong vl;
    target_ulong vstart;
    target_ulong vtype;
    bool vill;

    target_ulong pc;
    target_ulong load_res;
    target_ulong load_val;

    uint64_t fpr[32];
    target_ulong frm;
    float_status fp_status;

    target_ulong badaddr;
    target_ulong bins;

    target_ulong guest_phys_fault_addr;

    target_ulong priv_ver;
    target_ulong vext_ver;

    uint32_t misa_mxl;
    uint32_t misa_ext;
    uint32_t misa_ext_mask;
    uint32_t xl;

    target_ulong retxh;

    target_ulong jvt;

    bool      elp;
    target_ulong ssp;
    target_ulong excp_uw2;
    target_ulong sw_check_code;
#ifdef CONFIG_USER_ONLY
    uint32_t elf_flags;
#endif

    target_ulong priv;
    uint64_t menvcfg;
    target_ulong senvcfg;

#ifndef CONFIG_USER_ONLY
    bool virt_enabled;
    target_ulong geilen;
    uint64_t resetvec;

    target_ulong mhartid;
    uint64_t mstatus;

    uint64_t mip;
    bool external_seip;
    bool software_seip;

    uint64_t miclaim;

    uint64_t mie;
    uint64_t mideleg;

    uint64_t sie;

    uint64_t vsie;

    target_ulong satp;
    target_ulong stval;
    target_ulong medeleg;

    target_ulong stvec;
    target_ulong sepc;
    target_ulong scause;

    target_ulong mtvec;
    target_ulong mepc;
    target_ulong mcause;
    target_ulong mtval;

    uint64_t mctrctl;
    uint32_t sctrdepth;
    uint32_t sctrstatus;
    uint64_t vsctrctl;

    uint64_t ctr_src[16 << SCTRDEPTH_MAX];
    uint64_t ctr_dst[16 << SCTRDEPTH_MAX];
    uint64_t ctr_data[16 << SCTRDEPTH_MAX];

    uint8_t miprio[64];
    uint8_t siprio[64];

    target_ulong miselect;
    target_ulong siselect;
    uint64_t mvien;
    uint64_t mvip;

    target_ulong hstatus;
    target_ulong hedeleg;
    uint64_t hideleg;
    uint32_t hcounteren;
    target_ulong htval;
    target_ulong htinst;
    target_ulong hgatp;
    target_ulong hgeie;
    target_ulong hgeip;
    uint64_t htimedelta;
    uint64_t hvien;

    uint64_t hvip;

    target_ulong hvictl;
    uint8_t hviprio[64];

    uint64_t mscratchh;
    uint64_t sscratchh;

    uint64_t vsstatus;
    target_ulong vstvec;
    target_ulong vsscratch;
    target_ulong vsepc;
    target_ulong vscause;
    target_ulong vstval;
    target_ulong vsatp;

    target_ulong vsiselect;

    target_ulong mtval2;
    target_ulong mtinst;

    target_ulong stvec_hs;
    target_ulong sscratch_hs;
    target_ulong sepc_hs;
    target_ulong scause_hs;
    target_ulong stval_hs;
    target_ulong satp_hs;
    uint64_t mstatus_hs;

    bool two_stage_lookup;
    bool two_stage_indirect_lookup;

    uint32_t scounteren;
    uint32_t mcounteren;

    uint32_t scountinhibit;
    uint32_t mcountinhibit;

    target_ulong mcyclecfg;
    target_ulong mcyclecfgh;
    target_ulong minstretcfg;
    target_ulong minstretcfgh;

    PMUCTRState pmu_ctrs[RV_MAX_MHPMCOUNTERS];

    target_ulong mhpmevent_val[RV_MAX_MHPMEVENTS];

    target_ulong mhpmeventh_val[RV_MAX_MHPMEVENTS];

    PMUFixedCtrState pmu_fixed_ctrs[2];

    target_ulong sscratch;
    target_ulong mscratch;

    uint64_t stimecmp;

    uint64_t vstimecmp;

    pmp_table_t pmp_state;
    target_ulong mseccfg;

    target_ulong trigger_cur;
    target_ulong tdata1[RV_MAX_TRIGGERS];
    target_ulong tdata2[RV_MAX_TRIGGERS];
    target_ulong tdata3[RV_MAX_TRIGGERS];
    target_ulong mcontext;
    struct CPUBreakpoint *cpu_breakpoint[RV_MAX_TRIGGERS];
    struct CPUWatchpoint *cpu_watchpoint[RV_MAX_TRIGGERS];
    QEMUTimer *itrigger_timer[RV_MAX_TRIGGERS];
    int64_t last_icount;
    bool itrigger_enabled;

    uint64_t (*rdtime_fn)(void *);
    void *rdtime_fn_arg;

#define AIA_MAKE_IREG(__isel, __priv, __virt, __vgein, __xlen) \
    ((((__xlen) & 0xff) << 24) | \
     (((__vgein) & 0x3f) << 20) | \
     (((__virt) & 0x1) << 18) | \
     (((__priv) & 0x3) << 16) | \
     (__isel & 0xffff))
#define AIA_IREG_ISEL(__ireg)                  ((__ireg) & 0xffff)
#define AIA_IREG_PRIV(__ireg)                  (((__ireg) >> 16) & 0x3)
#define AIA_IREG_VIRT(__ireg)                  (((__ireg) >> 18) & 0x1)
#define AIA_IREG_VGEIN(__ireg)                 (((__ireg) >> 20) & 0x3f)
#define AIA_IREG_XLEN(__ireg)                  (((__ireg) >> 24) & 0xff)
    int (*aia_ireg_rmw_fn[4])(void *arg, target_ulong reg,
        target_ulong *val, target_ulong new_val, target_ulong write_mask);
    void *aia_ireg_rmw_fn_arg[4];

    bool debugger;

    uint64_t mstateen[SMSTATEEN_MAX_COUNT];
    uint64_t hstateen[SMSTATEEN_MAX_COUNT];
    uint64_t sstateen[SMSTATEEN_MAX_COUNT];
    uint64_t henvcfg;
#endif

    QEMUTimer *stimer;
    QEMUTimer *vstimer;
    bool vstime_irq;

    hwaddr kernel_addr;
    hwaddr fdt_addr;

#ifdef CONFIG_KVM
    bool kvm_timer_dirty;
    uint64_t kvm_timer_time;
    uint64_t kvm_timer_compare;
    uint64_t kvm_timer_state;
    uint64_t kvm_timer_frequency;
#endif

    target_ulong mnscratch;
    target_ulong mnepc;
    target_ulong mncause;
    target_ulong mnstatus;
    target_ulong rnmip;
    uint64_t rnmi_irqvec;
    uint64_t rnmi_excpvec;
};

typedef struct {
    uint16_t map, init;
} RISCVSATPModes;

struct ArchCPU {
    CPUState parent_obj;

    CPURISCVState env;

    GDBFeature dyn_csr_feature;
    GDBFeature dyn_vreg_feature;

    RISCVCPUConfig cfg;
    RISCVSATPModes satp_modes;

    QEMUTimer *pmu_timer;
    uint32_t pmu_avail_ctrs;
    GHashTable *pmu_event_ctr_map;
    const GPtrArray *decoders;
};

typedef struct RISCVCSR RISCVCSR;

typedef struct RISCVCPUDef {
    RISCVMXL misa_mxl_max;
    RISCVCPUProfile *profile;
    uint32_t misa_ext;
    int priv_spec;
    int32_t vext_spec;
    RISCVCPUConfig cfg;
    bool bare;
    const RISCVCSR *custom_csrs;
} RISCVCPUDef;

struct RISCVCPUClass {
    CPUClass parent_class;

    DeviceRealize parent_realize;
    ResettablePhases parent_phases;
    RISCVCPUDef *def;
};

static inline int riscv_has_ext(CPURISCVState *env, uint32_t ext)
{
    return (env->misa_ext & ext) != 0;
}

#include "cpu_user.h"

extern const char * const riscv_int_regnames[];
extern const char * const riscv_int_regnamesh[];
extern const char * const riscv_fpr_regnames[];
extern const char * const riscv_rvv_regnames[];

const char *riscv_cpu_get_trap_name(target_ulong cause, bool async);
int riscv_cpu_write_elf64_note(WriteCoreDumpFunction f, CPUState *cs,
                               int cpuid, DumpState *s);
int riscv_cpu_write_elf32_note(WriteCoreDumpFunction f, CPUState *cs,
                               int cpuid, DumpState *s);
int riscv_cpu_gdb_read_register(CPUState *cpu, GByteArray *buf, int reg);
int riscv_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);
int riscv_cpu_hviprio_index2irq(int index, int *out_irq, int *out_rdzero);
uint8_t riscv_cpu_default_priority(int irq);
uint64_t riscv_cpu_all_pending(CPURISCVState *env);
int riscv_cpu_mirq_pending(CPURISCVState *env);
int riscv_cpu_sirq_pending(CPURISCVState *env);
int riscv_cpu_vsirq_pending(CPURISCVState *env);
bool riscv_cpu_fp_enabled(CPURISCVState *env);
target_ulong riscv_cpu_get_geilen(CPURISCVState *env);
void riscv_cpu_set_geilen(CPURISCVState *env, target_ulong geilen);
bool riscv_cpu_vector_enabled(CPURISCVState *env);
void riscv_cpu_set_virt_enabled(CPURISCVState *env, bool enable);
int riscv_env_mmu_index(CPURISCVState *env, bool ifetch);
bool cpu_get_fcfien(CPURISCVState *env);
bool cpu_get_bcfien(CPURISCVState *env);
bool riscv_env_smode_dbltrp_enabled(CPURISCVState *env, bool virt);
G_NORETURN void  riscv_cpu_do_unaligned_access(CPUState *cs, vaddr addr,
                                               MMUAccessType access_type,
                                               int mmu_idx, uintptr_t retaddr);
bool riscv_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                        MMUAccessType access_type, int mmu_idx,
                        bool probe, uintptr_t retaddr);
char *riscv_isa_string(RISCVCPU *cpu);
int riscv_cpu_max_xlen(RISCVCPUClass *mcc);
bool riscv_cpu_option_set(const char *optname);

#ifndef CONFIG_USER_ONLY
void riscv_cpu_do_interrupt(CPUState *cpu);
void riscv_isa_write_fdt(RISCVCPU *cpu, void *fdt, char *nodename);
void riscv_cpu_do_transaction_failed(CPUState *cs, hwaddr physaddr,
                                     vaddr addr, unsigned size,
                                     MMUAccessType access_type,
                                     int mmu_idx, MemTxAttrs attrs,
                                     MemTxResult response, uintptr_t retaddr);
hwaddr riscv_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
bool riscv_cpu_exec_interrupt(CPUState *cs, int interrupt_request);
void riscv_cpu_swap_hypervisor_regs(CPURISCVState *env);
int riscv_cpu_claim_interrupts(RISCVCPU *cpu, uint64_t interrupts);
uint64_t riscv_cpu_update_mip(CPURISCVState *env, uint64_t mask,
                              uint64_t value);
void riscv_cpu_set_rnmi(RISCVCPU *cpu, uint32_t irq, bool level);
void riscv_cpu_interrupt(CPURISCVState *env);
#define BOOL_TO_MASK(x) (-!!(x))
void riscv_cpu_set_rdtime_fn(CPURISCVState *env, uint64_t (*fn)(void *),
                             void *arg);
void riscv_cpu_set_aia_ireg_rmw_fn(CPURISCVState *env, uint32_t priv,
                                   int (*rmw_fn)(void *arg,
                                                 target_ulong reg,
                                                 target_ulong *val,
                                                 target_ulong new_val,
                                                 target_ulong write_mask),
                                   void *rmw_fn_arg);

RISCVException smstateen_acc_ok(CPURISCVState *env, int index, uint64_t bit);
#endif

void riscv_cpu_set_mode(CPURISCVState *env, target_ulong newpriv, bool virt_en);

void riscv_ctr_add_entry(CPURISCVState *env, target_long src, target_long dst,
    enum CTRType type, target_ulong prev_priv, bool prev_virt);
void riscv_ctr_clear(CPURISCVState *env);

void riscv_translate_init(void);
void riscv_translate_code(CPUState *cs, TranslationBlock *tb,
                          int *max_insns, vaddr pc, void *host_pc);

G_NORETURN void riscv_raise_exception(CPURISCVState *env,
                                      RISCVException exception,
                                      uintptr_t pc);

target_ulong riscv_cpu_get_fflags(CPURISCVState *env);
void riscv_cpu_set_fflags(CPURISCVState *env, target_ulong);

FIELD(TB_FLAGS, MEM_IDX, 0, 3)
FIELD(TB_FLAGS, FS, 3, 2)
FIELD(TB_FLAGS, VS, 5, 2)
FIELD(TB_FLAGS, LMUL, 7, 3)
FIELD(TB_FLAGS, SEW, 10, 3)
FIELD(TB_FLAGS, VL_EQ_VLMAX, 13, 1)
FIELD(TB_FLAGS, VILL, 14, 1)
FIELD(TB_FLAGS, VSTART_EQ_ZERO, 15, 1)
FIELD(TB_FLAGS, XL, 16, 2)
FIELD(TB_FLAGS, PM_MASK_ENABLED, 18, 1)
FIELD(TB_FLAGS, PM_BASE_ENABLED, 19, 1)
FIELD(TB_FLAGS, VTA, 18, 1)
FIELD(TB_FLAGS, VMA, 19, 1)
FIELD(TB_FLAGS, ITRIGGER, 20, 1)
FIELD(TB_FLAGS, VIRT_ENABLED, 21, 1)
FIELD(TB_FLAGS, PRIV, 22, 2)
FIELD(TB_FLAGS, AXL, 24, 2)
FIELD(TB_FLAGS, FCFI_ENABLED, 26, 1)
FIELD(TB_FLAGS, FCFI_LP_EXPECTED, 27, 1)
FIELD(TB_FLAGS, BCFI_ENABLED, 28, 1)
FIELD(TB_FLAGS, PM_PMM, 29, 2)
FIELD(TB_FLAGS, PM_SIGNEXTEND, 31, 1)

#ifdef TARGET_RISCV32
#define riscv_cpu_mxl(env)  ((void)(env), MXL_RV32)
#else
static inline RISCVMXL riscv_cpu_mxl(CPURISCVState *env)
{
    return env->misa_mxl;
}
#endif
#define riscv_cpu_mxl_bits(env) (1UL << (4 + riscv_cpu_mxl(env)))

static inline const RISCVCPUConfig *riscv_cpu_cfg(CPURISCVState *env)
{
    return &env_archcpu(env)->cfg;
}

#if !defined(CONFIG_USER_ONLY)
static inline int cpu_address_mode(CPURISCVState *env)
{
    int mode = env->priv;

    if (mode == PRV_M && get_field(env->mstatus, MSTATUS_MPRV)) {
        mode = get_field(env->mstatus, MSTATUS_MPP);
    }
    return mode;
}

static inline RISCVMXL cpu_get_xl(CPURISCVState *env, target_ulong mode)
{
    RISCVMXL xl = env->misa_mxl;
    if (xl != MXL_RV32) {
        switch (mode) {
        case PRV_M:
            break;
        case PRV_U:
            xl = get_field(env->mstatus, MSTATUS64_UXL);
            break;
        default:
            xl = get_field(env->mstatus, MSTATUS64_SXL);
            break;
        }
    }
    return xl;
}
#endif

#if defined(TARGET_RISCV32)
#define cpu_recompute_xl(env)  ((void)(env), MXL_RV32)
#else
static inline RISCVMXL cpu_recompute_xl(CPURISCVState *env)
{
#if !defined(CONFIG_USER_ONLY)
    return cpu_get_xl(env, env->priv);
#else
    return env->misa_mxl;
#endif
}
#endif

#if defined(TARGET_RISCV32)
#define cpu_address_xl(env)  ((void)(env), MXL_RV32)
#else
static inline RISCVMXL cpu_address_xl(CPURISCVState *env)
{
#ifdef CONFIG_USER_ONLY
    return env->xl;
#else
    int mode = cpu_address_mode(env);

    return cpu_get_xl(env, mode);
#endif
}
#endif

static inline int riscv_cpu_xlen(CPURISCVState *env)
{
    return 16 << env->xl;
}

#ifdef TARGET_RISCV32
#define riscv_cpu_sxl(env)  ((void)(env), MXL_RV32)
#else
static inline RISCVMXL riscv_cpu_sxl(CPURISCVState *env)
{
#ifdef CONFIG_USER_ONLY
    return env->misa_mxl;
#else
    if (env->misa_mxl != MXL_RV32) {
        return get_field(env->mstatus, MSTATUS64_SXL);
    }
#endif
    return MXL_RV32;
}
#endif

static inline bool riscv_cpu_allow_16bit_insn(const RISCVCPUConfig *cfg,
                                              target_long priv_ver,
                                              uint32_t misa_ext)
{
    if (priv_ver >= PRIV_VERSION_1_12_0) {
        return cfg->ext_zca;
    } else {
        return misa_ext & RVC;
    }
}

static inline uint32_t vext_get_vlmax(uint32_t vlenb, uint32_t vsew,
                                      int8_t lmul)
{
    uint32_t vlen = vlenb << 3;

    return vlen >> (vsew + 3 - lmul);
}

bool riscv_cpu_is_32bit(RISCVCPU *cpu);

bool riscv_cpu_virt_mem_enabled(CPURISCVState *env);
RISCVPmPmm riscv_pm_get_pmm(CPURISCVState *env);
RISCVPmPmm riscv_pm_get_virt_pmm(CPURISCVState *env);
uint32_t riscv_pm_get_pmlen(RISCVPmPmm pmm);

RISCVException riscv_csrr(CPURISCVState *env, int csrno,
                          target_ulong *ret_value);

RISCVException riscv_csrrw(CPURISCVState *env, int csrno,
                           target_ulong *ret_value, target_ulong new_value,
                           target_ulong write_mask, uintptr_t ra);
RISCVException riscv_csrrw_debug(CPURISCVState *env, int csrno,
                                 target_ulong *ret_value,
                                 target_ulong new_value,
                                 target_ulong write_mask);

static inline void riscv_csr_write(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
    riscv_csrrw(env, csrno, NULL, val, MAKE_64BIT_MASK(0, TARGET_LONG_BITS), 0);
}

static inline target_ulong riscv_csr_read(CPURISCVState *env, int csrno)
{
    target_ulong val = 0;
    riscv_csrr(env, csrno, &val);
    return val;
}

typedef RISCVException (*riscv_csr_predicate_fn)(CPURISCVState *env,
                                                 int csrno);
typedef RISCVException (*riscv_csr_read_fn)(CPURISCVState *env, int csrno,
                                            target_ulong *ret_value);
typedef RISCVException (*riscv_csr_write_fn)(CPURISCVState *env, int csrno,
                                             target_ulong new_value,
                                             uintptr_t ra);
typedef RISCVException (*riscv_csr_op_fn)(CPURISCVState *env, int csrno,
                                          target_ulong *ret_value,
                                          target_ulong new_value,
                                          target_ulong write_mask);

RISCVException riscv_csrr_i128(CPURISCVState *env, int csrno,
                               Int128 *ret_value);
RISCVException riscv_csrrw_i128(CPURISCVState *env, int csrno,
                                Int128 *ret_value, Int128 new_value,
                                Int128 write_mask, uintptr_t ra);

typedef RISCVException (*riscv_csr_read128_fn)(CPURISCVState *env, int csrno,
                                               Int128 *ret_value);
typedef RISCVException (*riscv_csr_write128_fn)(CPURISCVState *env, int csrno,
                                             Int128 new_value);

typedef struct {
    const char *name;
    riscv_csr_predicate_fn predicate;
    riscv_csr_read_fn read;
    riscv_csr_write_fn write;
    riscv_csr_op_fn op;
    riscv_csr_read128_fn read128;
    riscv_csr_write128_fn write128;
    uint32_t min_priv_ver;
} riscv_csr_operations;

struct RISCVCSR {
    int csrno;
    bool (*insertion_test)(RISCVCPU *cpu);
    riscv_csr_operations csr_ops;
};

enum {
    CSR_TABLE_SIZE = 0x1000
};

enum riscv_pmu_event_idx {
    RISCV_PMU_EVENT_HW_CPU_CYCLES = 0x01,
    RISCV_PMU_EVENT_HW_INSTRUCTIONS = 0x02,
    RISCV_PMU_EVENT_CACHE_DTLB_READ_MISS = 0x10019,
    RISCV_PMU_EVENT_CACHE_DTLB_WRITE_MISS = 0x1001B,
    RISCV_PMU_EVENT_CACHE_ITLB_PREFETCH_MISS = 0x10021,
};

void isa_ext_update_enabled(RISCVCPU *cpu, uint32_t ext_offset, bool en);
bool isa_ext_is_enabled(RISCVCPU *cpu, uint32_t ext_offset);
void riscv_cpu_set_misa_ext(CPURISCVState *env, uint32_t ext);
bool riscv_cpu_is_vendor(Object *cpu_obj);

typedef struct RISCVCPUMultiExtConfig {
    const char *name;
    uint32_t offset;
    bool enabled;
} RISCVCPUMultiExtConfig;

extern const RISCVCPUMultiExtConfig riscv_cpu_extensions[];
extern const RISCVCPUMultiExtConfig riscv_cpu_vendor_exts[];
extern const RISCVCPUMultiExtConfig riscv_cpu_experimental_exts[];
extern const RISCVCPUMultiExtConfig riscv_cpu_named_features[];

typedef struct isa_ext_data {
    const char *name;
    int min_version;
    int ext_enable_offset;
} RISCVIsaExtData;
extern const RISCVIsaExtData isa_edata_arr[];
char *riscv_cpu_get_name(RISCVCPU *cpu);

void riscv_cpu_finalize_features(RISCVCPU *cpu, Error **errp);
void riscv_add_satp_mode_properties(Object *obj);
bool riscv_cpu_accelerator_compatible(RISCVCPU *cpu);

extern riscv_csr_operations csr_ops[CSR_TABLE_SIZE];

extern const bool valid_vm_1_10_32[], valid_vm_1_10_64[];

void riscv_get_csr_ops(int csrno, riscv_csr_operations *ops);
void riscv_set_csr_ops(int csrno, const riscv_csr_operations *ops);

void riscv_cpu_register_gdb_regs_for_features(CPUState *cs);

target_ulong riscv_new_csr_seed(target_ulong new_value,
                                target_ulong write_mask);

const char *satp_mode_str(uint8_t satp_mode, bool is_32_bit);

extern const RISCVCSR th_csr_list[];

const char *priv_spec_to_str(int priv_version);
#endif
