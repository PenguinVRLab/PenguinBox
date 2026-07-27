/*
 * ARM virtual CPU header
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ARM_CPU_H
#define ARM_CPU_H

#include "kvm-consts.h"
#include "qemu/cpu-float.h"
#include "hw/registerfields.h"
#include "cpu-qom.h"
#include "exec/cpu-common.h"
#include "exec/cpu-defs.h"
#include "exec/cpu-interrupt.h"
#include "exec/gdbstub.h"
#include "exec/page-protection.h"
#include "qapi/qapi-types-common.h"
#include "target/arm/multiprocessing.h"
#include "target/arm/gtimer.h"
#include "target/arm/cpu-sysregs.h"
#include "target/arm/mmuidx.h"

#define EXCP_UDEF            1
#define EXCP_SWI             2
#define EXCP_PREFETCH_ABORT  3
#define EXCP_DATA_ABORT      4
#define EXCP_IRQ             5
#define EXCP_FIQ             6
#define EXCP_BKPT            7
#define EXCP_EXCEPTION_EXIT  8
#define EXCP_KERNEL_TRAP     9
#define EXCP_HVC            11
#define EXCP_HYP_TRAP       12
#define EXCP_SMC            13
#define EXCP_VIRQ           14
#define EXCP_VFIQ           15
#define EXCP_SEMIHOST       16
#define EXCP_NOCP           17
#define EXCP_INVSTATE       18
#define EXCP_STKOF          19
#define EXCP_LAZYFP         20
#define EXCP_LSERR          21
#define EXCP_UNALIGNED      22
#define EXCP_DIVBYZERO      23
#define EXCP_VSERR          24
#define EXCP_GPC            25
#define EXCP_NMI            26
#define EXCP_VINMI          27
#define EXCP_VFNMI          28
#define EXCP_MON_TRAP       29

#define ARMV7M_EXCP_RESET   1
#define ARMV7M_EXCP_NMI     2
#define ARMV7M_EXCP_HARD    3
#define ARMV7M_EXCP_MEM     4
#define ARMV7M_EXCP_BUS     5
#define ARMV7M_EXCP_USAGE   6
#define ARMV7M_EXCP_SECURE  7
#define ARMV7M_EXCP_SVC     11
#define ARMV7M_EXCP_DEBUG   12
#define ARMV7M_EXCP_PENDSV  14
#define ARMV7M_EXCP_SYSTICK 15

#define CPU_INTERRUPT_FIQ   CPU_INTERRUPT_TGT_EXT_1
#define CPU_INTERRUPT_VIRQ  CPU_INTERRUPT_TGT_EXT_2
#define CPU_INTERRUPT_VFIQ  CPU_INTERRUPT_TGT_EXT_3
#define CPU_INTERRUPT_VSERR CPU_INTERRUPT_TGT_INT_0
#define CPU_INTERRUPT_NMI   CPU_INTERRUPT_TGT_EXT_4
#define CPU_INTERRUPT_VINMI CPU_INTERRUPT_TGT_EXT_0
#define CPU_INTERRUPT_VFNMI CPU_INTERRUPT_TGT_INT_1

#if HOST_BIG_ENDIAN
#define offsetoflow32(S, M) (offsetof(S, M) + sizeof(uint32_t))
#define offsetofhigh32(S, M) offsetof(S, M)
#else
#define offsetoflow32(S, M) offsetof(S, M)
#define offsetofhigh32(S, M) (offsetof(S, M) + sizeof(uint32_t))
#endif

#define ARM_INSN_START_WORD2_MASK ((1 << 26) - 1)
#define ARM_INSN_START_WORD2_SHIFT 13

typedef struct DynamicGDBFeatureInfo {
    GDBFeature desc;
    union {
        struct {
            uint32_t *keys;
        } cpregs;
    } data;
} DynamicGDBFeatureInfo;

typedef struct ARMGenericTimer {
    uint64_t cval;
    uint64_t ctl;
} ARMGenericTimer;

#define ARM_MAX_VQ    16

typedef struct ARMVectorReg {
    uint64_t d[2 * ARM_MAX_VQ] QEMU_ALIGNED(16);
} ARMVectorReg;

typedef struct ARMPredicateReg {
    uint64_t p[DIV_ROUND_UP(2 * ARM_MAX_VQ, 8)] QEMU_ALIGNED(16);
} ARMPredicateReg;

typedef struct ARMPACKey {
    uint64_t lo, hi;
} ARMPACKey;

typedef struct CPUARMTBFlags {
    uint32_t flags;
    uint64_t flags2;
} CPUARMTBFlags;

typedef struct ARMMMUFaultInfo ARMMMUFaultInfo;

typedef struct NVICState NVICState;

typedef enum ARMFPStatusFlavour {
    FPST_A32,
    FPST_A64,
    FPST_A32_F16,
    FPST_A64_F16,
    FPST_AH,
    FPST_AH_F16,
    FPST_ZA,
    FPST_ZA_F16,
    FPST_STD,
    FPST_STD_F16,
} ARMFPStatusFlavour;
#define FPST_COUNT  10

typedef struct CPUArchState {
    uint32_t regs[16];

    uint64_t xregs[32];
    uint64_t pc;
    uint64_t pstate;
    bool aarch64;
    bool thumb;

    CPUARMTBFlags hflags;

    uint32_t uncached_cpsr;
    uint32_t spsr;

    uint64_t banked_spsr[8];
    uint32_t banked_r13[8];
    uint32_t banked_r14[8];

    uint32_t usr_regs[5];
    uint32_t fiq_regs[5];

    uint32_t CF;
    uint32_t VF;
    uint32_t NF;
    uint32_t ZF;
    uint32_t QF;
    uint32_t GE;
    uint32_t condexec_bits;
    uint32_t btype;
    uint64_t daif;
    uint64_t svcr;

    uint64_t elr_el[4];
    uint64_t sp_el[4];

    struct {
        uint32_t c0_cpuid;
        union {
            struct {
                uint64_t _unused_csselr0;
                uint64_t csselr_ns;
                uint64_t _unused_csselr1;
                uint64_t csselr_s;
            };
            uint64_t csselr_el[4];
        };
        union {
            struct {
                uint64_t _unused_sctlr;
                uint64_t sctlr_ns;
                uint64_t hsctlr;
                uint64_t sctlr_s;
            };
            uint64_t sctlr_el[4];
        };
        uint64_t sctlr2_el[4];
        uint64_t vsctlr;
        uint64_t cpacr_el1;
        uint64_t cptr_el[4];
        uint64_t sder;
        uint32_t nsacr;
        union {
            struct {
                uint64_t _unused_ttbr0_0;
                uint64_t ttbr0_ns;
                uint64_t _unused_ttbr0_1;
                uint64_t ttbr0_s;
            };
            uint64_t ttbr0_el[4];
        };
        union {
            struct {
                uint64_t _unused_ttbr1_0;
                uint64_t ttbr1_ns;
                uint64_t _unused_ttbr1_1;
                uint64_t ttbr1_s;
            };
            uint64_t ttbr1_el[4];
        };
        uint64_t vttbr_el2;
        uint64_t vsttbr_el2;
        uint64_t tcr_el[4];
        uint64_t tcr2_el[3];
        uint64_t vtcr_el2;
        uint64_t vstcr_el2;
        uint64_t pir_el[4];
        uint64_t pire0_el2;
        uint64_t s2pir_el2;
        uint32_t c2_data;
        uint32_t c2_insn;
        union {
            struct {
                uint64_t dacr_ns;
                uint64_t dacr_s;
            };
            struct {
                uint64_t dacr32_el2;
            };
        };
        uint32_t pmsav5_data_ap;
        uint32_t pmsav5_insn_ap;
        uint64_t hcr_el2;
        uint64_t hcrx_el2;
        uint64_t scr_el3;
        union {
            struct {
                uint64_t ifsr_ns;
                uint64_t ifsr_s;
            };
            struct {
                uint64_t ifsr32_el2;
            };
        };
        union {
            struct {
                uint64_t _unused_dfsr;
                uint64_t dfsr_ns;
                uint64_t hsr;
                uint64_t dfsr_s;
            };
            uint64_t esr_el[4];
        };
        uint32_t c6_region[8];
        union {
            struct {
                uint64_t _unused_far0;
#if HOST_BIG_ENDIAN
                uint32_t ifar_ns;
                uint32_t dfar_ns;
                uint32_t ifar_s;
                uint32_t dfar_s;
#else
                uint32_t dfar_ns;
                uint32_t ifar_ns;
                uint32_t dfar_s;
                uint32_t ifar_s;
#endif
                uint64_t _unused_far3;
            };
            uint64_t far_el[4];
        };
        uint64_t hpfar_el2;
        uint64_t hstr_el2;
        union {
            struct {
                uint64_t _unused_par_0;
                uint64_t par_ns;
                uint64_t _unused_par_1;
                uint64_t par_s;
            };
            uint64_t par_el[4];
        };

        uint32_t c9_insn;
        uint32_t c9_data;
        uint64_t c9_pmcr;
        uint64_t c9_pmcnten;
        uint64_t c9_pmovsr;
        uint64_t c9_pmuserenr;
        uint64_t c9_pmselr;
        uint64_t c9_pminten;
        union {
            struct {
#if HOST_BIG_ENDIAN
                uint64_t _unused_mair_0;
                uint32_t mair1_ns;
                uint32_t mair0_ns;
                uint64_t _unused_mair_1;
                uint32_t mair1_s;
                uint32_t mair0_s;
#else
                uint64_t _unused_mair_0;
                uint32_t mair0_ns;
                uint32_t mair1_ns;
                uint64_t _unused_mair_1;
                uint32_t mair0_s;
                uint32_t mair1_s;
#endif
            };
            uint64_t mair_el[4];
        };
        uint64_t mair2_el[4];
        union {
            struct {
                uint64_t _unused_vbar;
                uint64_t vbar_ns;
                uint64_t hvbar;
                uint64_t vbar_s;
            };
            uint64_t vbar_el[4];
        };
        uint32_t mvbar;
        uint64_t rvbar;
        struct {
            uint32_t fcseidr_ns;
            uint32_t fcseidr_s;
        };
        union {
            struct {
                uint64_t _unused_contextidr_0;
                uint64_t contextidr_ns;
                uint64_t _unused_contextidr_1;
                uint64_t contextidr_s;
            };
            uint64_t contextidr_el[4];
        };
        union {
            struct {
                uint64_t tpidrurw_ns;
                uint64_t tpidrprw_ns;
                uint64_t htpidr;
                uint64_t _tpidr_el3;
            };
            uint64_t tpidr_el[4];
        };
        uint64_t tpidr2_el0;
        uint64_t tpidrurw_s;
        uint64_t tpidrprw_s;
        uint64_t tpidruro_s;

        union {
            uint64_t tpidruro_ns;
            uint64_t tpidrro_el[1];
        };
        uint64_t c14_cntfrq;
        uint64_t c14_cntkctl;
        uint64_t cnthctl_el2;
        uint64_t cntvoff_el2;
        uint64_t cntpoff_el2;
        ARMGenericTimer c14_timer[NUM_GTIMERS];
        uint32_t c15_ticonfig;
        uint32_t c15_i_max;
        uint32_t c15_i_min;
        uint32_t c15_threadid;
        uint32_t c15_config_base_address;
        uint32_t c15_diagnostic;
        uint32_t c15_power_diagnostic;
        uint32_t c15_power_control;
        uint64_t dbgbvr[16];
        uint64_t dbgbcr[16];
        uint64_t dbgwvr[16];
        uint64_t dbgwcr[16];
        uint64_t dbgclaim;
        uint64_t mdscr_el1;
        uint64_t oslsr_el1;
        uint64_t osdlr_el1;
        uint64_t mdcr_el2;
        uint64_t mdcr_el3;
        uint64_t c15_ccnt;
        uint64_t c15_ccnt_delta;
        uint64_t c14_pmevcntr[31];
        uint64_t c14_pmevcntr_delta[31];
        uint64_t c14_pmevtyper[31];
        uint64_t pmccfiltr_el0;
        uint64_t vpidr_el2;
        uint64_t vmpidr_el2;
        uint64_t tfsr_el[4];
        uint64_t gcr_el1;
        uint64_t rgsr_el1;

        uint64_t disr_el1;
        uint64_t vdisr_el2;
        uint64_t vsesr_el2;

        uint64_t fgt_read[2];
        uint64_t fgt_write[2];
        uint64_t fgt_exec[1];

        uint64_t gpccr_el3;
        uint64_t gptbr_el3;
        uint64_t mfar_el3;

        uint64_t vncr_el2;

        uint64_t gcscr_el[4];
        uint64_t gcspr_el[4];

        uint64_t mecid_p0_el2;
        uint64_t mecid_a0_el2;
        uint64_t mecid_p1_el2;
        uint64_t mecid_a1_el2;
        uint64_t mecid_rl_a_el3;
        uint64_t vmecid_p_el2;
        uint64_t vmecid_a_el2;
    } cp15;

    struct {
        uint32_t other_sp;
        uint32_t other_ss_msp;
        uint32_t other_ss_psp;
        uint32_t vecbase[M_REG_NUM_BANKS];
        uint32_t basepri[M_REG_NUM_BANKS];
        uint32_t control[M_REG_NUM_BANKS];
        uint32_t ccr[M_REG_NUM_BANKS];
        uint32_t cfsr[M_REG_NUM_BANKS];
        uint32_t hfsr;
        uint32_t dfsr;
        uint32_t sfsr;
        uint32_t mmfar[M_REG_NUM_BANKS];
        uint32_t bfar;
        uint32_t sfar;
        unsigned mpu_ctrl[M_REG_NUM_BANKS];
        int exception;
        uint32_t primask[M_REG_NUM_BANKS];
        uint32_t faultmask[M_REG_NUM_BANKS];
        uint32_t aircr;
        uint32_t secure;
        uint32_t csselr[M_REG_NUM_BANKS];
        uint32_t scr[M_REG_NUM_BANKS];
        uint32_t msplim[M_REG_NUM_BANKS];
        uint32_t psplim[M_REG_NUM_BANKS];
        uint32_t fpcar[M_REG_NUM_BANKS];
        uint32_t fpccr[M_REG_NUM_BANKS];
        uint32_t fpdscr[M_REG_NUM_BANKS];
        uint32_t cpacr[M_REG_NUM_BANKS];
        uint32_t nsacr;
        uint32_t ltpsize;
        uint32_t vpr;
    } v7m;

    struct {
        uint64_t syndrome;
        uint64_t vaddress;
        uint32_t fsr;
        uint32_t target_el;
    } exception;

    struct {
        uint8_t pending;
        uint8_t has_esr;
        uint64_t esr;
    } serror;

    uint8_t ext_dabt_raised;

    uint32_t irq_line_state;

    uint32_t teecr;
    uint32_t teehbr;

    struct {
        ARMVectorReg zregs[32];

#define FFR_PRED_NUM 16
        ARMPredicateReg pregs[17];
        ARMPredicateReg preg_tmp;

        uint32_t qc[4] QEMU_ALIGNED(16);
        int vec_len;
        int vec_stride;

        uint64_t fpsr;
        uint64_t fpcr;

        uint32_t xregs[16];

        float_status fp_status[FPST_COUNT];

        uint64_t zcr_el[4];
        uint64_t smcr_el[4];
    } vfp;

    uint64_t exclusive_addr;
    uint64_t exclusive_val;
    uint64_t exclusive_high;

    struct {
        ARMPACKey apia;
        ARMPACKey apib;
        ARMPACKey apda;
        ARMPACKey apdb;
        ARMPACKey apga;
    } keys;

    uint64_t scxtnum_el[4];

    struct {
        uint64_t zt0[512 / 64] QEMU_ALIGNED(16);

        ARMVectorReg za[ARM_MAX_VQ * 16];
    } za_state;

    struct CPUBreakpoint *cpu_breakpoint[16];
    struct CPUWatchpoint *cpu_watchpoint[16];

    ARMMMUFaultInfo *tlb_fi;

    struct {} end_reset_fields;

    uint64_t features;

    struct {
        uint32_t *drbar;
        uint32_t *drsr;
        uint32_t *dracr;
        uint32_t rnr[M_REG_NUM_BANKS];
    } pmsav7;

    struct {
        uint32_t *rbar[M_REG_NUM_BANKS];
        uint32_t *rlar[M_REG_NUM_BANKS];
        uint32_t *hprbar;
        uint32_t *hprlar;
        uint32_t mair0[M_REG_NUM_BANKS];
        uint32_t mair1[M_REG_NUM_BANKS];
        uint32_t hprselr;
    } pmsav8;

    struct {
        uint32_t *rbar;
        uint32_t *rlar;
        uint32_t rnr;
        uint32_t ctrl;
    } sau;

#if !defined(CONFIG_USER_ONLY)
    NVICState *nvic;
    const struct arm_boot_info *boot_info;
    void *gicv3state;
#else
    bool eabi;
    bool tagged_addr_enable;
#endif
} CPUARMState;

static inline void set_feature(CPUARMState *env, int feature)
{
    env->features |= 1ULL << feature;
}

static inline void unset_feature(CPUARMState *env, int feature)
{
    env->features &= ~(1ULL << feature);
}

typedef void ARMELChangeHookFn(ARMCPU *cpu, void *opaque);
typedef struct ARMELChangeHook ARMELChangeHook;
struct ARMELChangeHook {
    ARMELChangeHookFn *hook;
    void *opaque;
    QLIST_ENTRY(ARMELChangeHook) node;
};

typedef enum ARMPSCIState {
    PSCI_ON = 0,
    PSCI_OFF = 1,
    PSCI_ON_PENDING = 2
} ARMPSCIState;

typedef struct ARMISARegisters ARMISARegisters;

typedef struct {
    uint32_t map, init, supported;
} ARMVQMap;

#define FIELD_DP64_IDREG(ISAR, REG, FIELD, VALUE)                       \
    ({                                                                  \
        ARMISARegisters *i_ = (ISAR);                                   \
        uint64_t regval = i_->idregs[REG ## _EL1_IDX];                  \
        regval = FIELD_DP64(regval, REG, FIELD, VALUE);                 \
        i_->idregs[REG ## _EL1_IDX] = regval;                           \
    })

#define FIELD_DP32_IDREG(ISAR, REG, FIELD, VALUE)                       \
    ({                                                                  \
        ARMISARegisters *i_ = (ISAR);                                   \
        uint64_t regval = i_->idregs[REG ## _EL1_IDX];                  \
        regval = FIELD_DP32(regval, REG, FIELD, VALUE);                 \
        i_->idregs[REG ## _EL1_IDX] = regval;                           \
    })

#define FIELD_EX64_IDREG(ISAR, REG, FIELD)                              \
    ({                                                                  \
        const ARMISARegisters *i_ = (ISAR);                             \
        FIELD_EX64(i_->idregs[REG ## _EL1_IDX], REG, FIELD);            \
    })

#define FIELD_EX32_IDREG(ISAR, REG, FIELD)                              \
    ({                                                                  \
        const ARMISARegisters *i_ = (ISAR);                             \
        FIELD_EX32(i_->idregs[REG ## _EL1_IDX], REG, FIELD);            \
    })

#define FIELD_SEX64_IDREG(ISAR, REG, FIELD)                             \
    ({                                                                  \
        const ARMISARegisters *i_ = (ISAR);                             \
        FIELD_SEX64(i_->idregs[REG ## _EL1_IDX], REG, FIELD);           \
    })

#define SET_IDREG(ISAR, REG, VALUE)                                     \
    ({                                                                  \
        ARMISARegisters *i_ = (ISAR);                                   \
        i_->idregs[REG ## _EL1_IDX] = VALUE;                            \
    })

#define GET_IDREG(ISAR, REG)                                            \
    ({                                                                  \
        const ARMISARegisters *i_ = (ISAR);                             \
        i_->idregs[REG ## _EL1_IDX];                                    \
    })

struct ArchCPU {
    CPUState parent_obj;

    CPUARMState env;

    GHashTable *cp_regs;
    uint64_t *cpreg_indexes;
    uint64_t *cpreg_values;
    int32_t cpreg_array_len;
    uint64_t *cpreg_vmstate_indexes;
    uint64_t *cpreg_vmstate_values;
    int32_t cpreg_vmstate_array_len;

    DynamicGDBFeatureInfo dyn_sysreg_feature;
    DynamicGDBFeatureInfo dyn_svereg_feature;
    DynamicGDBFeatureInfo dyn_smereg_feature;
    DynamicGDBFeatureInfo dyn_m_systemreg_feature;
    DynamicGDBFeatureInfo dyn_m_secextreg_feature;
    DynamicGDBFeatureInfo dyn_tls_feature;

    QEMUTimer *gt_timer[NUM_GTIMERS];
    QEMUTimer *pmu_timer;
    QEMUTimer *wfxt_timer;

    qemu_irq gt_timer_outputs[NUM_GTIMERS];
    qemu_irq gicv3_maintenance_interrupt;
    qemu_irq pmu_interrupt;

    MemoryRegion *secure_memory;

    MemoryRegion *tag_memory;
    MemoryRegion *secure_tag_memory;

    Object *idau;

    const char *dtb_compatible;

    uint32_t psci_version;

    ARMPSCIState power_state;

    bool has_el2;
    bool has_el3;
    bool has_pmu;
    bool has_vfp;
    bool has_vfp_d32;
    bool has_neon;
    bool has_dsp;

    bool has_mpu;
    bool kvm_mte;
    uint32_t pmsav7_dregion;
    uint32_t pmsav8r_hdregion;
    uint32_t sau_sregion;

    uint32_t psci_conduit;

    uint32_t init_svtor;
    uint32_t init_nsvtor;

    uint32_t kvm_target;

    uint32_t kvm_init_features[7];

    bool kvm_adjvtime;
    bool kvm_vtime_dirty;
    uint64_t kvm_vtime;

    OnOffAuto kvm_steal_time;

    bool mp_is_up;

    bool host_cpu_probe_failed;

    bool backcompat_cntfrq;

    bool backcompat_pauth_default_use_qarma5;

    int32_t core_count;

    struct ARMISARegisters {
        uint32_t mvfr0;
        uint32_t mvfr1;
        uint32_t mvfr2;
        uint32_t dbgdidr;
        uint32_t dbgdevid;
        uint32_t dbgdevid1;
        uint64_t reset_pmcr_el0;
        uint64_t idregs[NUM_ID_IDX];
    } isar;
    uint64_t midr;
    uint32_t revidr;
    uint32_t reset_fpsid;
    uint64_t ctr;
    uint32_t reset_sctlr;
    uint64_t pmceid0;
    uint64_t pmceid1;
    uint64_t mp_affinity;
    uint64_t ccsidr[16];
    uint64_t reset_cbar;
    uint32_t reset_auxcr;
    bool reset_hivecs;
    uint8_t reset_l0gptsz;

    bool prop_pauth;
    bool prop_pauth_impdef;
    bool prop_pauth_qarma3;
    bool prop_pauth_qarma5;
    bool prop_lpa2;

    uint8_t dcz_blocksize;
    uint8_t gm_blocksize;

    uint64_t rvbar_prop;

    int gic_num_lrs;
    int gic_vpribits;
    int gic_vprebits;
    int gic_pribits;

    bool cfgend;

    QLIST_HEAD(, ARMELChangeHook) pre_el_change_hooks;
    QLIST_HEAD(, ARMELChangeHook) el_change_hooks;

    int32_t node_id;

    uint8_t device_irq_level;

    uint32_t sve_max_vq;
    uint32_t sme_max_vq;

#ifdef CONFIG_USER_ONLY
    uint32_t sve_default_vq;
    uint32_t sme_default_vq;
#endif

    ARMVQMap sve_vq;
    ARMVQMap sme_vq;

    uint64_t gt_cntfrq_hz;
};

typedef struct ARMCPUInfo {
    const char *name;
    const char *deprecation_note;
    void (*initfn)(Object *obj);
    void (*class_init)(ObjectClass *oc, const void *data);
} ARMCPUInfo;

struct ARMCPUClass {
    CPUClass parent_class;

    const ARMCPUInfo *info;
    DeviceRealize parent_realize;
    ResettablePhases parent_phases;
};

void arm_gt_ptimer_cb(void *opaque);
void arm_gt_vtimer_cb(void *opaque);
void arm_gt_htimer_cb(void *opaque);
void arm_gt_stimer_cb(void *opaque);
void arm_gt_hvtimer_cb(void *opaque);
void arm_gt_sel2timer_cb(void *opaque);
void arm_gt_sel2vtimer_cb(void *opaque);

unsigned int gt_cntfrq_period_ns(ARMCPU *cpu);
void gt_rme_post_el_change(ARMCPU *cpu, void *opaque);

#define ARM_AFF0_SHIFT 0
#define ARM_AFF0_MASK  (0xFFULL << ARM_AFF0_SHIFT)
#define ARM_AFF1_SHIFT 8
#define ARM_AFF1_MASK  (0xFFULL << ARM_AFF1_SHIFT)
#define ARM_AFF2_SHIFT 16
#define ARM_AFF2_MASK  (0xFFULL << ARM_AFF2_SHIFT)
#define ARM_AFF3_SHIFT 32
#define ARM_AFF3_MASK  (0xFFULL << ARM_AFF3_SHIFT)
#define ARM_DEFAULT_CPUS_PER_CLUSTER 8

#define ARM32_AFFINITY_MASK (ARM_AFF0_MASK | ARM_AFF1_MASK | ARM_AFF2_MASK)
#define ARM64_AFFINITY_MASK \
    (ARM_AFF0_MASK | ARM_AFF1_MASK | ARM_AFF2_MASK | ARM_AFF3_MASK)
#define ARM64_AFFINITY_INVALID (~ARM64_AFFINITY_MASK)

uint64_t arm_build_mp_affinity(int idx, uint8_t clustersz);

#ifndef CONFIG_USER_ONLY
extern const VMStateDescription vmstate_arm_cpu;

void arm_cpu_do_interrupt(CPUState *cpu);
void arm_v7m_cpu_do_interrupt(CPUState *cpu);

hwaddr arm_cpu_get_phys_page_attrs_debug(CPUState *cpu, vaddr addr,
                                         MemTxAttrs *attrs);
#endif

int arm_cpu_gdb_read_register(CPUState *cpu, GByteArray *buf, int reg);
int arm_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);

int arm_cpu_write_elf64_note(WriteCoreDumpFunction f, CPUState *cs,
                             int cpuid, DumpState *s);
int arm_cpu_write_elf32_note(WriteCoreDumpFunction f, CPUState *cs,
                             int cpuid, DumpState *s);

void arm_emulate_firmware_reset(CPUState *cpustate, int target_el);

int aarch64_cpu_gdb_read_register(CPUState *cpu, GByteArray *buf, int reg);
int aarch64_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);
void aarch64_sve_narrow_vq(CPUARMState *env, unsigned vq);
void aarch64_sve_change_el(CPUARMState *env, int old_el,
                           int new_el, bool el0_a64);
void aarch64_set_svcr(CPUARMState *env, uint64_t new, uint64_t mask);

static inline uint64_t *sve_bswap64(uint64_t *dst, uint64_t *src, int nr)
{
#if HOST_BIG_ENDIAN
    int i;

    for (i = 0; i < nr; ++i) {
        dst[i] = bswap64(src[i]);
    }

    return dst;
#else
    return src;
#endif
}

void aarch64_sync_32_to_64(CPUARMState *env);
void aarch64_sync_64_to_32(CPUARMState *env);

int fp_exception_el(CPUARMState *env, int cur_el);
int sve_exception_el(CPUARMState *env, int cur_el);
int sme_exception_el(CPUARMState *env, int cur_el);

uint32_t sve_vqm1_for_el_sm(CPUARMState *env, int el, bool sm);

uint32_t sve_vqm1_for_el(CPUARMState *env, int el);

static inline bool is_a64(CPUARMState *env)
{
    return env->aarch64;
}

void pmu_op_start(CPUARMState *env);
void pmu_op_finish(CPUARMState *env);

void arm_pmu_timer_cb(void *opaque);

void pmu_pre_el_change(ARMCPU *cpu, void *ignored);
void pmu_post_el_change(ARMCPU *cpu, void *ignored);

void pmu_init(ARMCPU *cpu);

#define SCTLR_M       (1U << 0)
#define SCTLR_A       (1U << 1)
#define SCTLR_C       (1U << 2)
#define SCTLR_W       (1U << 3)
#define SCTLR_nTLSMD_32 (1U << 3)
#define SCTLR_SA      (1U << 3)
#define SCTLR_P       (1U << 4)
#define SCTLR_LSMAOE_32 (1U << 4)
#define SCTLR_SA0     (1U << 4)
#define SCTLR_D       (1U << 5)
#define SCTLR_CP15BEN (1U << 5)
#define SCTLR_L       (1U << 6)
#define SCTLR_nAA     (1U << 6)
#define SCTLR_B       (1U << 7)
#define SCTLR_ITD     (1U << 7)
#define SCTLR_S       (1U << 8)
#define SCTLR_SED     (1U << 8)
#define SCTLR_R       (1U << 9)
#define SCTLR_UMA     (1U << 9)
#define SCTLR_F       (1U << 10)
#define SCTLR_SW      (1U << 10)
#define SCTLR_EnRCTX  (1U << 10)
#define SCTLR_Z       (1U << 11)
#define SCTLR_EOS     (1U << 11)
#define SCTLR_I       (1U << 12)
#define SCTLR_V       (1U << 13)
#define SCTLR_EnDB    (1U << 13)
#define SCTLR_RR      (1U << 14)
#define SCTLR_DZE     (1U << 14)
#define SCTLR_L4      (1U << 15)
#define SCTLR_UCT     (1U << 15)
#define SCTLR_DT      (1U << 16)
#define SCTLR_nTWI    (1U << 16)
#define SCTLR_HA      (1U << 17)
#define SCTLR_BR      (1U << 17)
#define SCTLR_IT      (1U << 18)
#define SCTLR_nTWE    (1U << 18)
#define SCTLR_WXN     (1U << 19)
#define SCTLR_ST      (1U << 20)
#define SCTLR_UWXN    (1U << 20)
#define SCTLR_TSCXT   (1U << 20)
#define SCTLR_FI      (1U << 21)
#define SCTLR_IESB    (1U << 21)
#define SCTLR_U       (1U << 22)
#define SCTLR_EIS     (1U << 22)
#define SCTLR_XP      (1U << 23)
#define SCTLR_SPAN    (1U << 23)
#define SCTLR_VE      (1U << 24)
#define SCTLR_E0E     (1U << 24)
#define SCTLR_EE      (1U << 25)
#define SCTLR_L2      (1U << 26)
#define SCTLR_UCI     (1U << 26)
#define SCTLR_NMFI    (1U << 27)
#define SCTLR_EnDA    (1U << 27)
#define SCTLR_TRE     (1U << 28)
#define SCTLR_nTLSMD_64 (1U << 28)
#define SCTLR_AFE     (1U << 29)
#define SCTLR_LSMAOE_64 (1U << 29)
#define SCTLR_TE      (1U << 30)
#define SCTLR_EnIB    (1U << 30)
#define SCTLR_EnIA    (1U << 31)
#define SCTLR_DSSBS_32 (1U << 31)
#define SCTLR_CMOW    (1ULL << 32)
#define SCTLR_MSCEN   (1ULL << 33)
#define SCTLR_BT0     (1ULL << 35)
#define SCTLR_BT1     (1ULL << 36)
#define SCTLR_ITFSB   (1ULL << 37)
#define SCTLR_TCF0    (3ULL << 38)
#define SCTLR_TCF     (3ULL << 40)
#define SCTLR_ATA0    (1ULL << 42)
#define SCTLR_ATA     (1ULL << 43)
#define SCTLR_DSSBS_64 (1ULL << 44)
#define SCTLR_TWEDEn  (1ULL << 45)
#define SCTLR_TWEDEL  MAKE_64_MASK(46, 4)
#define SCTLR_TMT0    (1ULL << 50)
#define SCTLR_TMT     (1ULL << 51)
#define SCTLR_TME0    (1ULL << 52)
#define SCTLR_TME     (1ULL << 53)
#define SCTLR_EnASR   (1ULL << 54)
#define SCTLR_EnAS0   (1ULL << 55)
#define SCTLR_EnALS   (1ULL << 56)
#define SCTLR_EPAN    (1ULL << 57)
#define SCTLR_EnTP2   (1ULL << 60)
#define SCTLR_NMI     (1ULL << 61)
#define SCTLR_SPINTMASK (1ULL << 62)
#define SCTLR_TIDCP   (1ULL << 63)

#define SCTLR2_EMEC (1ULL << 1)
#define SCTLR2_NMEA (1ULL << 2)
#define SCTLR2_ENADERR (1ULL << 3)
#define SCTLR2_ENANERR (1ULL << 4)
#define SCTLR2_EASE (1ULL << 5)
#define SCTLR2_ENIDCP128 (1ULL << 6)
#define SCTLR2_ENPACM (1ULL << 7)
#define SCTLR2_ENPACM0 (1ULL << 8)
#define SCTLR2_CPTA (1ULL << 9)
#define SCTLR2_CPTA0 (1ULL << 10)
#define SCTLR2_CPTM (1ULL << 11)
#define SCTLR2_CPTM0 (1ULL << 12)

#define CPSR_M (0x1fU)
#define CPSR_T (1U << 5)
#define CPSR_F (1U << 6)
#define CPSR_I (1U << 7)
#define CPSR_A (1U << 8)
#define CPSR_E (1U << 9)
#define CPSR_IT_2_7 (0xfc00U)
#define CPSR_GE (0xfU << 16)
#define CPSR_IL (1U << 20)
#define CPSR_DIT (1U << 21)
#define CPSR_PAN (1U << 22)
#define CPSR_SSBS (1U << 23)
#define CPSR_J (1U << 24)
#define CPSR_IT_0_1 (3U << 25)
#define CPSR_Q (1U << 27)
#define CPSR_V (1U << 28)
#define CPSR_C (1U << 29)
#define CPSR_Z (1U << 30)
#define CPSR_N (1U << 31)
#define CPSR_NZCV (CPSR_N | CPSR_Z | CPSR_C | CPSR_V)
#define CPSR_AIF (CPSR_A | CPSR_I | CPSR_F)
#define ISR_FS (1U << 9)
#define ISR_IS (1U << 10)

#define CPSR_IT (CPSR_IT_0_1 | CPSR_IT_2_7)
#define CACHED_CPSR_BITS (CPSR_T | CPSR_AIF | CPSR_GE | CPSR_IT | CPSR_Q \
    | CPSR_NZCV)
#define CPSR_USER (CPSR_NZCV | CPSR_Q | CPSR_GE | CPSR_E)
#define CPSR_EXEC (CPSR_T | CPSR_IT | CPSR_J | CPSR_IL)

#define XPSR_EXCP 0x1ffU
#define XPSR_SPREALIGN (1U << 9)
#define XPSR_IT_2_7 CPSR_IT_2_7
#define XPSR_GE CPSR_GE
#define XPSR_SFPA (1U << 20)
#define XPSR_T (1U << 24)
#define XPSR_IT_0_1 CPSR_IT_0_1
#define XPSR_Q CPSR_Q
#define XPSR_V CPSR_V
#define XPSR_C CPSR_C
#define XPSR_Z CPSR_Z
#define XPSR_N CPSR_N
#define XPSR_NZCV CPSR_NZCV
#define XPSR_IT CPSR_IT

#define PSTATE_SP (1U)
#define PSTATE_M (0xFU)
#define PSTATE_nRW (1U << 4)
#define PSTATE_F (1U << 6)
#define PSTATE_I (1U << 7)
#define PSTATE_A (1U << 8)
#define PSTATE_D (1U << 9)
#define PSTATE_BTYPE (3U << 10)
#define PSTATE_SSBS (1U << 12)
#define PSTATE_ALLINT (1U << 13)
#define PSTATE_IL (1U << 20)
#define PSTATE_SS (1U << 21)
#define PSTATE_PAN (1U << 22)
#define PSTATE_UAO (1U << 23)
#define PSTATE_DIT (1U << 24)
#define PSTATE_TCO (1U << 25)
#define PSTATE_V (1U << 28)
#define PSTATE_C (1U << 29)
#define PSTATE_Z (1U << 30)
#define PSTATE_N (1U << 31)
#define PSTATE_EXLOCK (1ULL << 34)
#define PSTATE_NZCV (PSTATE_N | PSTATE_Z | PSTATE_C | PSTATE_V)
#define PSTATE_DAIF (PSTATE_D | PSTATE_A | PSTATE_I | PSTATE_F)
#define CACHED_PSTATE_BITS (PSTATE_NZCV | PSTATE_DAIF | PSTATE_BTYPE)
#define PSTATE_MODE_EL3h 13
#define PSTATE_MODE_EL3t 12
#define PSTATE_MODE_EL2h 9
#define PSTATE_MODE_EL2t 8
#define PSTATE_MODE_EL1h 5
#define PSTATE_MODE_EL1t 4
#define PSTATE_MODE_EL0t 0

FIELD(SVCR, SM, 0, 1)
FIELD(SVCR, ZA, 1, 1)

FIELD(SMCR, LEN, 0, 4)
FIELD(SMCR, EZT0, 30, 1)
FIELD(SMCR, FA64, 31, 1)

void write_v7m_exception(CPUARMState *env, uint32_t new_exc);

static inline unsigned int aarch64_pstate_mode(unsigned int el, bool handler)
{
    return (el << 2) | handler;
}

static inline uint64_t pstate_read(CPUARMState *env)
{
    int ZF;

    ZF = (env->ZF == 0);
    return (env->NF & 0x80000000) | (ZF << 30)
        | (env->CF << 29) | ((env->VF & 0x80000000) >> 3)
        | env->pstate | env->daif | (env->btype << 10);
}

static inline void pstate_write(CPUARMState *env, uint64_t val)
{
    env->ZF = (~val) & PSTATE_Z;
    env->NF = val;
    env->CF = (val >> 29) & 1;
    env->VF = (val << 3) & 0x80000000;
    env->daif = val & PSTATE_DAIF;
    env->btype = (val >> 10) & 3;
    env->pstate = val & ~CACHED_PSTATE_BITS;
}

uint32_t cpsr_read(CPUARMState *env);

typedef enum CPSRWriteType {
    CPSRWriteByInstr = 0,
    CPSRWriteExceptionReturn = 1,
    CPSRWriteRaw = 2,
    CPSRWriteByGDBStub = 3,
} CPSRWriteType;

void cpsr_write(CPUARMState *env, uint32_t val, uint32_t mask,
                CPSRWriteType write_type);

static inline uint32_t xpsr_read(CPUARMState *env)
{
    int ZF;
    ZF = (env->ZF == 0);
    return (env->NF & 0x80000000) | (ZF << 30)
        | (env->CF << 29) | ((env->VF & 0x80000000) >> 3) | (env->QF << 27)
        | (env->thumb << 24) | ((env->condexec_bits & 3) << 25)
        | ((env->condexec_bits & 0xfc) << 8)
        | (env->GE << 16)
        | env->v7m.exception;
}

static inline void xpsr_write(CPUARMState *env, uint32_t val, uint32_t mask)
{
    if (mask & XPSR_NZCV) {
        env->ZF = (~val) & XPSR_Z;
        env->NF = val;
        env->CF = (val >> 29) & 1;
        env->VF = (val << 3) & 0x80000000;
    }
    if (mask & XPSR_Q) {
        env->QF = ((val & XPSR_Q) != 0);
    }
    if (mask & XPSR_GE) {
        env->GE = (val & XPSR_GE) >> 16;
    }
#ifndef CONFIG_USER_ONLY
    if (mask & XPSR_T) {
        env->thumb = ((val & XPSR_T) != 0);
    }
    if (mask & XPSR_IT_0_1) {
        env->condexec_bits &= ~3;
        env->condexec_bits |= (val >> 25) & 3;
    }
    if (mask & XPSR_IT_2_7) {
        env->condexec_bits &= 3;
        env->condexec_bits |= (val >> 8) & 0xfc;
    }
    if (mask & XPSR_EXCP) {
        write_v7m_exception(env, val & XPSR_EXCP);
    }
#endif
}

#define HCR_VM        (1ULL << 0)
#define HCR_SWIO      (1ULL << 1)
#define HCR_PTW       (1ULL << 2)
#define HCR_FMO       (1ULL << 3)
#define HCR_IMO       (1ULL << 4)
#define HCR_AMO       (1ULL << 5)
#define HCR_VF        (1ULL << 6)
#define HCR_VI        (1ULL << 7)
#define HCR_VSE       (1ULL << 8)
#define HCR_FB        (1ULL << 9)
#define HCR_BSU_MASK  (3ULL << 10)
#define HCR_DC        (1ULL << 12)
#define HCR_TWI       (1ULL << 13)
#define HCR_TWE       (1ULL << 14)
#define HCR_TID0      (1ULL << 15)
#define HCR_TID1      (1ULL << 16)
#define HCR_TID2      (1ULL << 17)
#define HCR_TID3      (1ULL << 18)
#define HCR_TSC       (1ULL << 19)
#define HCR_TIDCP     (1ULL << 20)
#define HCR_TACR      (1ULL << 21)
#define HCR_TSW       (1ULL << 22)
#define HCR_TPCP      (1ULL << 23)
#define HCR_TPU       (1ULL << 24)
#define HCR_TTLB      (1ULL << 25)
#define HCR_TVM       (1ULL << 26)
#define HCR_TGE       (1ULL << 27)
#define HCR_TDZ       (1ULL << 28)
#define HCR_HCD       (1ULL << 29)
#define HCR_TRVM      (1ULL << 30)
#define HCR_RW        (1ULL << 31)
#define HCR_CD        (1ULL << 32)
#define HCR_ID        (1ULL << 33)
#define HCR_E2H       (1ULL << 34)
#define HCR_TLOR      (1ULL << 35)
#define HCR_TERR      (1ULL << 36)
#define HCR_TEA       (1ULL << 37)
#define HCR_MIOCNCE   (1ULL << 38)
#define HCR_TME       (1ULL << 39)
#define HCR_APK       (1ULL << 40)
#define HCR_API       (1ULL << 41)
#define HCR_NV        (1ULL << 42)
#define HCR_NV1       (1ULL << 43)
#define HCR_AT        (1ULL << 44)
#define HCR_NV2       (1ULL << 45)
#define HCR_FWB       (1ULL << 46)
#define HCR_FIEN      (1ULL << 47)
#define HCR_GPF       (1ULL << 48)
#define HCR_TID4      (1ULL << 49)
#define HCR_TICAB     (1ULL << 50)
#define HCR_AMVOFFEN  (1ULL << 51)
#define HCR_TOCU      (1ULL << 52)
#define HCR_ENSCXT    (1ULL << 53)
#define HCR_TTLBIS    (1ULL << 54)
#define HCR_TTLBOS    (1ULL << 55)
#define HCR_ATA       (1ULL << 56)
#define HCR_DCT       (1ULL << 57)
#define HCR_TID5      (1ULL << 58)
#define HCR_TWEDEN    (1ULL << 59)
#define HCR_TWEDEL    MAKE_64BIT_MASK(60, 4)

#define SCR_NS                (1ULL << 0)
#define SCR_IRQ               (1ULL << 1)
#define SCR_FIQ               (1ULL << 2)
#define SCR_EA                (1ULL << 3)
#define SCR_FW                (1ULL << 4)
#define SCR_AW                (1ULL << 5)
#define SCR_NET               (1ULL << 6)
#define SCR_SMD               (1ULL << 7)
#define SCR_HCE               (1ULL << 8)
#define SCR_SIF               (1ULL << 9)
#define SCR_RW                (1ULL << 10)
#define SCR_ST                (1ULL << 11)
#define SCR_TWI               (1ULL << 12)
#define SCR_TWE               (1ULL << 13)
#define SCR_TLOR              (1ULL << 14)
#define SCR_TERR              (1ULL << 15)
#define SCR_APK               (1ULL << 16)
#define SCR_API               (1ULL << 17)
#define SCR_EEL2              (1ULL << 18)
#define SCR_EASE              (1ULL << 19)
#define SCR_NMEA              (1ULL << 20)
#define SCR_FIEN              (1ULL << 21)
#define SCR_ENSCXT            (1ULL << 25)
#define SCR_ATA               (1ULL << 26)
#define SCR_FGTEN             (1ULL << 27)
#define SCR_ECVEN             (1ULL << 28)
#define SCR_TWEDEN            (1ULL << 29)
#define SCR_TWEDEL            MAKE_64BIT_MASK(30, 4)
#define SCR_TME               (1ULL << 34)
#define SCR_AMVOFFEN          (1ULL << 35)
#define SCR_ENAS0             (1ULL << 36)
#define SCR_ADEN              (1ULL << 37)
#define SCR_HXEN              (1ULL << 38)
#define SCR_GCSEN             (1ULL << 39)
#define SCR_TRNDR             (1ULL << 40)
#define SCR_ENTP2             (1ULL << 41)
#define SCR_TCR2EN            (1ULL << 43)
#define SCR_SCTLR2EN          (1ULL << 44)
#define SCR_PIEN              (1ULL << 45)
#define SCR_AIEN              (1ULL << 46)
#define SCR_GPF               (1ULL << 48)
#define SCR_MECEN             (1ULL << 49)
#define SCR_NSE               (1ULL << 62)

#define GCSCR_PCRSEL    (1ULL << 0)
#define GCSCR_RVCHKEN   (1ULL << 5)
#define GCSCR_EXLOCKEN  (1ULL << 6)
#define GCSCR_PUSHMEN   (1ULL << 8)
#define GCSCR_STREN     (1ULL << 9)
#define GCSCRE0_NTR     (1ULL << 10)

uint32_t vfp_get_fpscr(CPUARMState *env);
void vfp_set_fpscr(CPUARMState *env, uint32_t val);

#define FPCR_FIZ    (1 << 0)
#define FPCR_AH     (1 << 1)
#define FPCR_NEP    (1 << 2)
#define FPCR_IOE    (1 << 8)
#define FPCR_DZE    (1 << 9)
#define FPCR_OFE    (1 << 10)
#define FPCR_UFE    (1 << 11)
#define FPCR_IXE    (1 << 12)
#define FPCR_EBF    (1 << 13)
#define FPCR_IDE    (1 << 15)
#define FPCR_LEN_MASK (7 << 16)
#define FPCR_FZ16   (1 << 19)
#define FPCR_STRIDE_MASK (3 << 20)
#define FPCR_RMODE_MASK (3 << 22)
#define FPCR_FZ     (1 << 24)
#define FPCR_DN     (1 << 25)
#define FPCR_AHP    (1 << 26)

#define FPCR_LTPSIZE_SHIFT 16
#define FPCR_LTPSIZE_MASK (7 << FPCR_LTPSIZE_SHIFT)
#define FPCR_LTPSIZE_LENGTH 3

#define FPCR_EEXC_MASK (FPCR_IOE | FPCR_DZE | FPCR_OFE | FPCR_UFE | FPCR_IXE | FPCR_IDE)

#define FPSR_IOC    (1 << 0)
#define FPSR_DZC    (1 << 1)
#define FPSR_OFC    (1 << 2)
#define FPSR_UFC    (1 << 3)
#define FPSR_IXC    (1 << 4)
#define FPSR_IDC    (1 << 7)
#define FPSR_QC     (1 << 27)
#define FPSR_V      (1 << 28)
#define FPSR_C      (1 << 29)
#define FPSR_Z      (1 << 30)
#define FPSR_N      (1 << 31)

#define FPSR_CEXC_MASK (FPSR_IOC | FPSR_DZC | FPSR_OFC | FPSR_UFC | FPSR_IXC | FPSR_IDC)

#define FPSR_NZCV_MASK (FPSR_N | FPSR_Z | FPSR_C | FPSR_V)
#define FPSR_NZCVQC_MASK (FPSR_NZCV_MASK | FPSR_QC)

#define FPSCR_FPSR_MASK (FPSR_NZCVQC_MASK | FPSR_CEXC_MASK)
#define FPSCR_FPCR_MASK (FPCR_EEXC_MASK | FPCR_LEN_MASK | FPCR_FZ16 | \
                         FPCR_STRIDE_MASK | FPCR_RMODE_MASK | \
                         FPCR_FZ | FPCR_DN | FPCR_AHP)
QEMU_BUILD_BUG_ON(FPSCR_FPSR_MASK & FPSCR_FPCR_MASK);

uint32_t vfp_get_fpsr(CPUARMState *env);

uint32_t vfp_get_fpcr(CPUARMState *env);

void vfp_set_fpsr(CPUARMState *env, uint32_t value);

void vfp_set_fpcr(CPUARMState *env, uint32_t value);

enum arm_cpu_mode {
  ARM_CPU_MODE_USR = 0x10,
  ARM_CPU_MODE_FIQ = 0x11,
  ARM_CPU_MODE_IRQ = 0x12,
  ARM_CPU_MODE_SVC = 0x13,
  ARM_CPU_MODE_MON = 0x16,
  ARM_CPU_MODE_ABT = 0x17,
  ARM_CPU_MODE_HYP = 0x1a,
  ARM_CPU_MODE_UND = 0x1b,
  ARM_CPU_MODE_SYS = 0x1f
};

#define ARM_VFP_FPSID   0
#define ARM_VFP_FPSCR   1
#define ARM_VFP_MVFR2   5
#define ARM_VFP_MVFR1   6
#define ARM_VFP_MVFR0   7
#define ARM_VFP_FPEXC   8
#define ARM_VFP_FPINST  9
#define ARM_VFP_FPINST2 10
#define ARM_VFP_FPSCR_NZCVQC 2
#define ARM_VFP_VPR 12
#define ARM_VFP_P0 13
#define ARM_VFP_FPCXT_NS 14
#define ARM_VFP_FPCXT_S 15

#define QEMU_VFP_FPSCR_NZCV 0xffff

FIELD(V7M_CCR, NONBASETHRDENA, 0, 1)
FIELD(V7M_CCR, USERSETMPEND, 1, 1)
FIELD(V7M_CCR, UNALIGN_TRP, 3, 1)
FIELD(V7M_CCR, DIV_0_TRP, 4, 1)
FIELD(V7M_CCR, BFHFNMIGN, 8, 1)
FIELD(V7M_CCR, STKALIGN, 9, 1)
FIELD(V7M_CCR, STKOFHFNMIGN, 10, 1)
FIELD(V7M_CCR, DC, 16, 1)
FIELD(V7M_CCR, IC, 17, 1)
FIELD(V7M_CCR, BP, 18, 1)
FIELD(V7M_CCR, LOB, 19, 1)
FIELD(V7M_CCR, TRD, 20, 1)

FIELD(V7M_SCR, SLEEPONEXIT, 1, 1)
FIELD(V7M_SCR, SLEEPDEEP, 2, 1)
FIELD(V7M_SCR, SLEEPDEEPS, 3, 1)
FIELD(V7M_SCR, SEVONPEND, 4, 1)

FIELD(V7M_AIRCR, VECTRESET, 0, 1)
FIELD(V7M_AIRCR, VECTCLRACTIVE, 1, 1)
FIELD(V7M_AIRCR, SYSRESETREQ, 2, 1)
FIELD(V7M_AIRCR, SYSRESETREQS, 3, 1)
FIELD(V7M_AIRCR, PRIGROUP, 8, 3)
FIELD(V7M_AIRCR, BFHFNMINS, 13, 1)
FIELD(V7M_AIRCR, PRIS, 14, 1)
FIELD(V7M_AIRCR, ENDIANNESS, 15, 1)
FIELD(V7M_AIRCR, VECTKEY, 16, 16)

FIELD(V7M_CFSR, IACCVIOL, 0, 1)
FIELD(V7M_CFSR, DACCVIOL, 1, 1)
FIELD(V7M_CFSR, MUNSTKERR, 3, 1)
FIELD(V7M_CFSR, MSTKERR, 4, 1)
FIELD(V7M_CFSR, MLSPERR, 5, 1)
FIELD(V7M_CFSR, MMARVALID, 7, 1)

FIELD(V7M_CFSR, IBUSERR, 8 + 0, 1)
FIELD(V7M_CFSR, PRECISERR, 8 + 1, 1)
FIELD(V7M_CFSR, IMPRECISERR, 8 + 2, 1)
FIELD(V7M_CFSR, UNSTKERR, 8 + 3, 1)
FIELD(V7M_CFSR, STKERR, 8 + 4, 1)
FIELD(V7M_CFSR, LSPERR, 8 + 5, 1)
FIELD(V7M_CFSR, BFARVALID, 8 + 7, 1)

FIELD(V7M_CFSR, UNDEFINSTR, 16 + 0, 1)
FIELD(V7M_CFSR, INVSTATE, 16 + 1, 1)
FIELD(V7M_CFSR, INVPC, 16 + 2, 1)
FIELD(V7M_CFSR, NOCP, 16 + 3, 1)
FIELD(V7M_CFSR, STKOF, 16 + 4, 1)
FIELD(V7M_CFSR, UNALIGNED, 16 + 8, 1)
FIELD(V7M_CFSR, DIVBYZERO, 16 + 9, 1)

FIELD(V7M_CFSR, MMFSR, 0, 8)
FIELD(V7M_CFSR, BFSR, 8, 8)
FIELD(V7M_CFSR, UFSR, 16, 16)

FIELD(V7M_HFSR, VECTTBL, 1, 1)
FIELD(V7M_HFSR, FORCED, 30, 1)
FIELD(V7M_HFSR, DEBUGEVT, 31, 1)

FIELD(V7M_DFSR, HALTED, 0, 1)
FIELD(V7M_DFSR, BKPT, 1, 1)
FIELD(V7M_DFSR, DWTTRAP, 2, 1)
FIELD(V7M_DFSR, VCATCH, 3, 1)
FIELD(V7M_DFSR, EXTERNAL, 4, 1)

FIELD(V7M_SFSR, INVEP, 0, 1)
FIELD(V7M_SFSR, INVIS, 1, 1)
FIELD(V7M_SFSR, INVER, 2, 1)
FIELD(V7M_SFSR, AUVIOL, 3, 1)
FIELD(V7M_SFSR, INVTRAN, 4, 1)
FIELD(V7M_SFSR, LSPERR, 5, 1)
FIELD(V7M_SFSR, SFARVALID, 6, 1)
FIELD(V7M_SFSR, LSERR, 7, 1)

FIELD(V7M_MPU_CTRL, ENABLE, 0, 1)
FIELD(V7M_MPU_CTRL, HFNMIENA, 1, 1)
FIELD(V7M_MPU_CTRL, PRIVDEFENA, 2, 1)

FIELD(V7M_CLIDR, CTYPE_ALL, 0, 21)
FIELD(V7M_CLIDR, LOUIS, 21, 3)
FIELD(V7M_CLIDR, LOC, 24, 3)
FIELD(V7M_CLIDR, LOUU, 27, 3)
FIELD(V7M_CLIDR, ICB, 30, 2)

FIELD(V7M_CSSELR, IND, 0, 1)
FIELD(V7M_CSSELR, LEVEL, 1, 3)
FIELD(V7M_CSSELR, INDEX, 0, 4)

FIELD(V7M_FPCCR, LSPACT, 0, 1)
FIELD(V7M_FPCCR, USER, 1, 1)
FIELD(V7M_FPCCR, S, 2, 1)
FIELD(V7M_FPCCR, THREAD, 3, 1)
FIELD(V7M_FPCCR, HFRDY, 4, 1)
FIELD(V7M_FPCCR, MMRDY, 5, 1)
FIELD(V7M_FPCCR, BFRDY, 6, 1)
FIELD(V7M_FPCCR, SFRDY, 7, 1)
FIELD(V7M_FPCCR, MONRDY, 8, 1)
FIELD(V7M_FPCCR, SPLIMVIOL, 9, 1)
FIELD(V7M_FPCCR, UFRDY, 10, 1)
FIELD(V7M_FPCCR, RES0, 11, 15)
FIELD(V7M_FPCCR, TS, 26, 1)
FIELD(V7M_FPCCR, CLRONRETS, 27, 1)
FIELD(V7M_FPCCR, CLRONRET, 28, 1)
FIELD(V7M_FPCCR, LSPENS, 29, 1)
FIELD(V7M_FPCCR, LSPEN, 30, 1)
FIELD(V7M_FPCCR, ASPEN, 31, 1)
#define R_V7M_FPCCR_BANKED_MASK                 \
    (R_V7M_FPCCR_LSPACT_MASK |                  \
     R_V7M_FPCCR_USER_MASK |                    \
     R_V7M_FPCCR_THREAD_MASK |                  \
     R_V7M_FPCCR_MMRDY_MASK |                   \
     R_V7M_FPCCR_SPLIMVIOL_MASK |               \
     R_V7M_FPCCR_UFRDY_MASK |                   \
     R_V7M_FPCCR_ASPEN_MASK)

FIELD(V7M_VPR, P0, 0, 16)
FIELD(V7M_VPR, MASK01, 16, 4)
FIELD(V7M_VPR, MASK23, 20, 4)

FIELD(GPCCR, PPS, 0, 3)
FIELD(GPCCR, RLPAD, 5, 1)
FIELD(GPCCR, NSPAD, 6, 1)
FIELD(GPCCR, SPAD, 7, 1)
FIELD(GPCCR, IRGN, 8, 2)
FIELD(GPCCR, ORGN, 10, 2)
FIELD(GPCCR, SH, 12, 2)
FIELD(GPCCR, PGS, 14, 2)
FIELD(GPCCR, GPC, 16, 1)
FIELD(GPCCR, GPCP, 17, 1)
FIELD(GPCCR, TBGPCD, 18, 1)
FIELD(GPCCR, NSO, 19, 1)
FIELD(GPCCR, L0GPTSZ, 20, 4)
FIELD(GPCCR, APPSAA, 24, 1)

FIELD(MFAR, FPA, 12, 40)
FIELD(MFAR, NSE, 62, 1)
FIELD(MFAR, NS, 63, 1)

QEMU_BUILD_BUG_ON(ARRAY_SIZE(((ARMCPU *)0)->ccsidr) <= R_V7M_CSSELR_INDEX_MASK);

enum arm_features {
    ARM_FEATURE_AUXCR,
    ARM_FEATURE_V6,
    ARM_FEATURE_V6K,
    ARM_FEATURE_V7,
    ARM_FEATURE_THUMB2,
    ARM_FEATURE_PMSA,
    ARM_FEATURE_NEON,
    ARM_FEATURE_M,
    ARM_FEATURE_OMAPCP,
    ARM_FEATURE_THUMB2EE,
    ARM_FEATURE_V7MP,
    ARM_FEATURE_V7VE,
    ARM_FEATURE_V4T,
    ARM_FEATURE_V5,
    ARM_FEATURE_STRONGARM,
    ARM_FEATURE_VAPA,
    ARM_FEATURE_GENERIC_TIMER,
    ARM_FEATURE_MVFR,
    ARM_FEATURE_DUMMY_C15_REGS,
    ARM_FEATURE_CACHE_TEST_CLEAN,
    ARM_FEATURE_CACHE_DIRTY_REG,
    ARM_FEATURE_CACHE_BLOCK_OPS,
    ARM_FEATURE_MPIDR,
    ARM_FEATURE_LPAE,
    ARM_FEATURE_V8,
    ARM_FEATURE_AARCH64,
    ARM_FEATURE_CBAR,
    ARM_FEATURE_CBAR_RO,
    ARM_FEATURE_EL2,
    ARM_FEATURE_EL3,
    ARM_FEATURE_THUMB_DSP,
    ARM_FEATURE_PMU,
    ARM_FEATURE_VBAR,
    ARM_FEATURE_M_SECURITY,
    ARM_FEATURE_M_MAIN,
    ARM_FEATURE_V8_1M,
    ARM_FEATURE_BACKCOMPAT_CNTFRQ,
};

static inline int arm_feature(CPUARMState *env, int feature)
{
    return (env->features & (1ULL << feature)) != 0;
}

void arm_cpu_finalize_features(ARMCPU *cpu, Error **errp);

typedef enum ARMSecuritySpace {
    ARMSS_Secure     = 0,
    ARMSS_NonSecure  = 1,
    ARMSS_Root       = 2,
    ARMSS_Realm      = 3,
} ARMSecuritySpace;

static inline bool arm_space_is_secure(ARMSecuritySpace space)
{
    return space == ARMSS_Secure || space == ARMSS_Root;
}

static inline ARMSecuritySpace arm_secure_to_space(bool secure)
{
    return secure ? ARMSS_Secure : ARMSS_NonSecure;
}

#if !defined(CONFIG_USER_ONLY)
ARMSecuritySpace arm_security_space_below_el3(CPUARMState *env);

static inline bool arm_is_secure_below_el3(CPUARMState *env)
{
    ARMSecuritySpace ss = arm_security_space_below_el3(env);
    return ss == ARMSS_Secure;
}

static inline bool arm_is_el3_or_mon(CPUARMState *env)
{
    assert(!arm_feature(env, ARM_FEATURE_M));
    if (arm_feature(env, ARM_FEATURE_EL3)) {
        if (is_a64(env) && extract32(env->pstate, 2, 2) == 3) {
            return true;
        } else if (!is_a64(env) &&
                (env->uncached_cpsr & CPSR_M) == ARM_CPU_MODE_MON) {
            return true;
        }
    }
    return false;
}

ARMSecuritySpace arm_security_space(CPUARMState *env);

static inline bool arm_is_secure(CPUARMState *env)
{
    return arm_space_is_secure(arm_security_space(env));
}

static inline bool arm_is_el2_enabled_secstate(CPUARMState *env,
                                               ARMSecuritySpace space)
{
    assert(space != ARMSS_Root);
    return arm_feature(env, ARM_FEATURE_EL2)
           && (space != ARMSS_Secure || (env->cp15.scr_el3 & SCR_EEL2));
}

static inline bool arm_is_el2_enabled(CPUARMState *env)
{
    return arm_is_el2_enabled_secstate(env, arm_security_space_below_el3(env));
}

#else
static inline ARMSecuritySpace arm_security_space_below_el3(CPUARMState *env)
{
    return ARMSS_NonSecure;
}

static inline bool arm_is_secure_below_el3(CPUARMState *env)
{
    return false;
}

static inline bool arm_is_el3_or_mon(CPUARMState *env)
{
    return false;
}

static inline ARMSecuritySpace arm_security_space(CPUARMState *env)
{
    return ARMSS_NonSecure;
}

static inline bool arm_is_secure(CPUARMState *env)
{
    return false;
}

static inline bool arm_is_el2_enabled_secstate(CPUARMState *env,
                                               ARMSecuritySpace space)
{
    return false;
}

static inline bool arm_is_el2_enabled(CPUARMState *env)
{
    return false;
}
#endif

uint64_t arm_hcr_el2_eff_secstate(CPUARMState *env, ARMSecuritySpace space);
uint64_t arm_hcr_el2_eff(CPUARMState *env);
uint64_t arm_hcr_el2_nvx_eff(CPUARMState *env);
uint64_t arm_hcrx_el2_eff(CPUARMState *env);

bool access_secure_reg(CPUARMState *env);

uint32_t arm_phys_excp_target_el(CPUState *cs, uint32_t excp_idx,
                                 uint32_t cur_el, bool secure);

static inline int arm_highest_el(CPUARMState *env)
{
    if (arm_feature(env, ARM_FEATURE_EL3)) {
        return 3;
    }
    if (arm_feature(env, ARM_FEATURE_EL2)) {
        return 2;
    }
    return 1;
}

static inline bool arm_v7m_is_handler_mode(CPUARMState *env)
{
    return env->v7m.exception != 0;
}

bool write_list_to_cpustate(ARMCPU *cpu);

bool write_cpustate_to_list(ARMCPU *cpu, bool kvm_sync);

#define ARM_CPUID_TI915T      0x54029152
#define ARM_CPUID_TI925T      0x54029252

#define CPU_RESOLVING_TYPE TYPE_ARM_CPU

#define TYPE_ARM_HOST_CPU "host-" TYPE_ARM_CPU

typedef enum ARMASIdx {
    ARMASIdx_NS = 0,
    ARMASIdx_S = 1,
    ARMASIdx_TagNS = 2,
    ARMASIdx_TagS = 3,
} ARMASIdx;

static inline ARMMMUIdx arm_space_to_phys(ARMSecuritySpace space)
{
    QEMU_BUILD_BUG_ON(ARMSS_Secure != 0);
    QEMU_BUILD_BUG_ON(ARMMMUIdx_Phys_NS != ARMMMUIdx_Phys_S + ARMSS_NonSecure);
    QEMU_BUILD_BUG_ON(ARMMMUIdx_Phys_Root != ARMMMUIdx_Phys_S + ARMSS_Root);
    QEMU_BUILD_BUG_ON(ARMMMUIdx_Phys_Realm != ARMMMUIdx_Phys_S + ARMSS_Realm);

    return ARMMMUIdx_Phys_S + space;
}

static inline ARMSecuritySpace arm_phys_to_space(ARMMMUIdx idx)
{
    assert(idx >= ARMMMUIdx_Phys_S && idx <= ARMMMUIdx_Phys_Realm);
    return idx - ARMMMUIdx_Phys_S;
}

static inline bool arm_v7m_csselr_razwi(ARMCPU *cpu)
{
    return (GET_IDREG(&cpu->isar, CLIDR) & R_V7M_CLIDR_CTYPE_ALL_MASK) != 0;
}

static inline bool arm_sctlr_b(CPUARMState *env)
{
    return
#ifndef CONFIG_USER_ONLY
        !arm_feature(env, ARM_FEATURE_V7) &&
#endif
        (env->cp15.sctlr_el[1] & SCTLR_B) != 0;
}

uint64_t arm_sctlr(CPUARMState *env, int el);

FIELD(TBFLAG_ANY, AARCH64_STATE, 0, 1)
FIELD(TBFLAG_ANY, SS_ACTIVE, 1, 1)
FIELD(TBFLAG_ANY, PSTATE__SS, 2, 1)
FIELD(TBFLAG_ANY, BE_DATA, 3, 1)
FIELD(TBFLAG_ANY, MMUIDX, 4, 4)
FIELD(TBFLAG_ANY, FPEXC_EL, 8, 2)
FIELD(TBFLAG_ANY, ALIGN_MEM, 10, 1)
FIELD(TBFLAG_ANY, PSTATE__IL, 11, 1)
FIELD(TBFLAG_ANY, FGT_ACTIVE, 12, 1)
FIELD(TBFLAG_ANY, FGT_SVC, 13, 1)

FIELD(TBFLAG_AM32, CONDEXEC, 24, 8)
FIELD(TBFLAG_AM32, THUMB, 23, 1)

FIELD(TBFLAG_A32, VECLEN, 0, 3)
FIELD(TBFLAG_A32, VECSTRIDE, 3, 2)
FIELD(TBFLAG_A32, VFPEN, 7, 1)
FIELD(TBFLAG_A32, SCTLR__B, 8, 1)
FIELD(TBFLAG_A32, HSTR_ACTIVE, 9, 1)
FIELD(TBFLAG_A32, NS, 10, 1)
FIELD(TBFLAG_A32, SME_TRAP_NONSTREAMING, 11, 1)

FIELD(TBFLAG_M32, HANDLER, 0, 1)
FIELD(TBFLAG_M32, STACKCHECK, 1, 1)
FIELD(TBFLAG_M32, LSPACT, 2, 1)
FIELD(TBFLAG_M32, NEW_FP_CTXT_NEEDED, 3, 1)
FIELD(TBFLAG_M32, FPCCR_S_WRONG, 4, 1)
FIELD(TBFLAG_M32, MVE_NO_PRED, 5, 1)
FIELD(TBFLAG_M32, SECURE, 6, 1)

FIELD(TBFLAG_A64, TBII, 0, 2)
FIELD(TBFLAG_A64, SVEEXC_EL, 2, 2)
FIELD(TBFLAG_A64, VL, 4, 4)
FIELD(TBFLAG_A64, PAUTH_ACTIVE, 8, 1)
FIELD(TBFLAG_A64, BT, 9, 1)
FIELD(TBFLAG_A64, BTYPE, 10, 2)
FIELD(TBFLAG_A64, TBID, 12, 2)
FIELD(TBFLAG_A64, UNPRIV, 14, 1)
FIELD(TBFLAG_A64, ATA, 15, 1)
FIELD(TBFLAG_A64, TCMA, 16, 2)
FIELD(TBFLAG_A64, MTE_ACTIVE, 18, 1)
FIELD(TBFLAG_A64, MTE0_ACTIVE, 19, 1)
FIELD(TBFLAG_A64, SMEEXC_EL, 20, 2)
FIELD(TBFLAG_A64, PSTATE_SM, 22, 1)
FIELD(TBFLAG_A64, PSTATE_ZA, 23, 1)
FIELD(TBFLAG_A64, SVL, 24, 4)
FIELD(TBFLAG_A64, SME_TRAP_NONSTREAMING, 28, 1)
FIELD(TBFLAG_A64, TRAP_ERET, 29, 1)
FIELD(TBFLAG_A64, NAA, 30, 1)
FIELD(TBFLAG_A64, ATA0, 31, 1)
FIELD(TBFLAG_A64, NV, 32, 1)
FIELD(TBFLAG_A64, NV1, 33, 1)
FIELD(TBFLAG_A64, NV2, 34, 1)
FIELD(TBFLAG_A64, E2H, 35, 1)
FIELD(TBFLAG_A64, NV2_MEM_BE, 36, 1)
FIELD(TBFLAG_A64, AH, 37, 1)
FIELD(TBFLAG_A64, NEP, 38, 1)
FIELD(TBFLAG_A64, ZT0EXC_EL, 39, 2)
FIELD(TBFLAG_A64, GCS_EN, 41, 1)
FIELD(TBFLAG_A64, GCS_RVCEN, 42, 1)
FIELD(TBFLAG_A64, GCSSTR_EL, 43, 2)

#define DP_TBFLAG_ANY(DST, WHICH, VAL) \
    (DST.flags = FIELD_DP32(DST.flags, TBFLAG_ANY, WHICH, VAL))
#define DP_TBFLAG_A64(DST, WHICH, VAL) \
    (DST.flags2 = FIELD_DP64(DST.flags2, TBFLAG_A64, WHICH, VAL))
#define DP_TBFLAG_A32(DST, WHICH, VAL) \
    (DST.flags2 = FIELD_DP32(DST.flags2, TBFLAG_A32, WHICH, VAL))
#define DP_TBFLAG_M32(DST, WHICH, VAL) \
    (DST.flags2 = FIELD_DP32(DST.flags2, TBFLAG_M32, WHICH, VAL))
#define DP_TBFLAG_AM32(DST, WHICH, VAL) \
    (DST.flags2 = FIELD_DP32(DST.flags2, TBFLAG_AM32, WHICH, VAL))

#define EX_TBFLAG_ANY(IN, WHICH)   FIELD_EX32(IN.flags, TBFLAG_ANY, WHICH)
#define EX_TBFLAG_A64(IN, WHICH)   FIELD_EX64(IN.flags2, TBFLAG_A64, WHICH)
#define EX_TBFLAG_A32(IN, WHICH)   FIELD_EX32(IN.flags2, TBFLAG_A32, WHICH)
#define EX_TBFLAG_M32(IN, WHICH)   FIELD_EX32(IN.flags2, TBFLAG_M32, WHICH)
#define EX_TBFLAG_AM32(IN, WHICH)  FIELD_EX32(IN.flags2, TBFLAG_AM32, WHICH)

static inline int sve_vq(CPUARMState *env)
{
    return EX_TBFLAG_A64(env->hflags, VL) + 1;
}

static inline int sme_vq(CPUARMState *env)
{
    return EX_TBFLAG_A64(env->hflags, SVL) + 1;
}

static inline bool bswap_code(bool sctlr_b)
{
#ifdef CONFIG_USER_ONLY
    return TARGET_BIG_ENDIAN ^ sctlr_b;
#else
    return 0;
#endif
}

enum {
    QEMU_PSCI_CONDUIT_DISABLED = 0,
    QEMU_PSCI_CONDUIT_SMC = 1,
    QEMU_PSCI_CONDUIT_HVC = 2,
};

#ifndef CONFIG_USER_ONLY
static inline int arm_asidx_from_attrs(CPUState *cs, MemTxAttrs attrs)
{
    return attrs.secure ? ARMASIdx_S : ARMASIdx_NS;
}

static inline AddressSpace *arm_addressspace(CPUState *cs, MemTxAttrs attrs)
{
    return cpu_get_address_space(cs, arm_asidx_from_attrs(cs, attrs));
}
#endif

void arm_register_pre_el_change_hook(ARMCPU *cpu, ARMELChangeHookFn *hook,
                                 void *opaque);
void arm_register_el_change_hook(ARMCPU *cpu, ARMELChangeHookFn *hook, void
        *opaque);

void arm_rebuild_hflags(CPUARMState *env);

static inline uint64_t *aa32_vfp_dreg(CPUARMState *env, unsigned regno)
{
    return &env->vfp.zregs[regno >> 1].d[regno & 1];
}

static inline uint64_t *aa32_vfp_qreg(CPUARMState *env, unsigned regno)
{
    return &env->vfp.zregs[regno].d[0];
}

static inline uint64_t *aa64_vfp_qreg(CPUARMState *env, unsigned regno)
{
    return &env->vfp.zregs[regno].d[0];
}

extern const uint64_t pred_esz_masks[5];

#define PAGE_BTI            PAGE_TARGET_1
#define PAGE_MTE            PAGE_TARGET_2

#define LOG2_TAG_GRANULE 4
#define TAG_GRANULE      (1 << LOG2_TAG_GRANULE)

#endif
