/*
 * m68k virtual CPU header
 *
 *  Copyright (c) 2005-2007 CodeSourcery
 *  Written by Paul Brook
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

#ifndef M68K_CPU_H
#define M68K_CPU_H

#include "exec/cpu-common.h"
#include "exec/cpu-defs.h"
#include "exec/cpu-interrupt.h"
#include "qemu/cpu-float.h"
#include "cpu-qom.h"

#define OS_BYTE     0
#define OS_WORD     1
#define OS_LONG     2
#define OS_SINGLE   3
#define OS_DOUBLE   4
#define OS_EXTENDED 5
#define OS_PACKED   6
#define OS_UNSIZED  7

#define EXCP_ACCESS         2
#define EXCP_ADDRESS        3
#define EXCP_ILLEGAL        4
#define EXCP_DIV0           5
#define EXCP_CHK            6
#define EXCP_TRAPCC         7
#define EXCP_PRIVILEGE      8
#define EXCP_TRACE          9
#define EXCP_LINEA          10
#define EXCP_LINEF          11
#define EXCP_DEBUGNBP       12
#define EXCP_DEBEGBP        13
#define EXCP_FORMAT         14
#define EXCP_UNINITIALIZED  15
#define EXCP_SPURIOUS       24
#define EXCP_INT_LEVEL_1    25
#define EXCP_INT_LEVEL_7    31
#define EXCP_TRAP0          32
#define EXCP_TRAP15         47
#define EXCP_FP_BSUN        48
#define EXCP_FP_INEX        49
#define EXCP_FP_DZ          50
#define EXCP_FP_UNFL        51
#define EXCP_FP_OPERR       52
#define EXCP_FP_OVFL        53
#define EXCP_FP_SNAN        54
#define EXCP_FP_UNIMP       55
#define EXCP_MMU_CONF       56
#define EXCP_MMU_ILLEGAL    57
#define EXCP_MMU_ACCESS     58

#define EXCP_RTE            0x100
#define EXCP_SEMIHOSTING    0x101

#define M68K_DTTR0   0
#define M68K_DTTR1   1
#define M68K_ITTR0   2
#define M68K_ITTR1   3

#define M68K_MAX_TTR 2
#define TTR(type, index) ttr[((type & ACCESS_CODE) == ACCESS_CODE) * 2 + index]

typedef CPU_LDoubleU FPReg;

typedef struct CPUArchState {
    uint32_t dregs[8];
    uint32_t aregs[8];
    uint32_t pc;
    uint32_t sr;

    int current_sp;
    uint32_t sp[3];

    uint32_t cc_op;
    uint32_t cc_x;
    uint32_t cc_n;
    uint32_t cc_v;
    uint32_t cc_c;
    uint32_t cc_z;

    FPReg fregs[8];
    FPReg fp_result;
    uint32_t fpcr;
    uint32_t fpsr;
    float_status fp_status;

    uint64_t mactmp;
    uint64_t macc[4];
    uint32_t macsr;
    uint32_t mac_mask;

    struct {
        uint32_t ar;
        uint32_t ssw;
        uint16_t tcr;
        uint32_t urp;
        uint32_t srp;
        bool fault;
        uint32_t ttr[4];
        uint32_t mmusr;
    } mmu;

    uint32_t vbr;
    uint32_t mbar;
    uint32_t rambar0;
    uint32_t cacr;
    uint32_t sfc;
    uint32_t dfc;

    int pending_vector;
    int pending_level;

    struct {} end_reset_fields;

    uint64_t features;
} CPUM68KState;

struct ArchCPU {
    CPUState parent_obj;

    CPUM68KState env;
};

struct M68kCPUClass {
    CPUClass parent_class;

    DeviceRealize parent_realize;
    ResettablePhases parent_phases;
};

#ifndef CONFIG_USER_ONLY
void m68k_cpu_do_interrupt(CPUState *cpu);
bool m68k_cpu_exec_interrupt(CPUState *cpu, int int_req);
hwaddr m68k_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
#endif
void m68k_cpu_dump_state(CPUState *cpu, FILE *f, int flags);
int m68k_cpu_gdb_read_register(CPUState *cpu, GByteArray *buf, int reg);
int m68k_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);

void m68k_tcg_init(void);
void m68k_translate_code(CPUState *cs, TranslationBlock *tb,
                         int *max_insns, vaddr pc, void *host_pc);
void m68k_cpu_init_gdb(M68kCPU *cpu);
uint32_t cpu_m68k_get_ccr(CPUM68KState *env);
void cpu_m68k_set_ccr(CPUM68KState *env, uint32_t);
void cpu_m68k_set_sr(CPUM68KState *env, uint32_t);
void cpu_m68k_restore_fp_status(CPUM68KState *env);
void cpu_m68k_set_fpcr(CPUM68KState *env, uint32_t val);
uint32_t cpu_m68k_get_fpsr(CPUM68KState *env);
void cpu_m68k_set_fpsr(CPUM68KState *env, uint32_t val);

typedef enum {
    CC_OP_DYNAMIC,

    CC_OP_FLAGS,

    CC_OP_ADDB, CC_OP_ADDW, CC_OP_ADDL,
    CC_OP_SUBB, CC_OP_SUBW, CC_OP_SUBL,

    CC_OP_CMPB, CC_OP_CMPW, CC_OP_CMPL,

    CC_OP_LOGIC,

    CC_OP_NB
} CCOp;

#define CCF_C 0x01
#define CCF_V 0x02
#define CCF_Z 0x04
#define CCF_N 0x08
#define CCF_X 0x10

#define SR_I_SHIFT 8
#define SR_I  0x0700
#define SR_M  0x1000
#define SR_S  0x2000
#define SR_T_SHIFT 14
#define SR_T  0xc000

#define M68K_SR_TRACE(sr) ((sr & SR_T) >> SR_T_SHIFT)
#define M68K_SR_TRACE_ANY_INS 0x2

#define M68K_SSP    0
#define M68K_USP    1
#define M68K_ISP    2

#define M68K_CP_040  0x8000
#define M68K_CU_040  0x4000
#define M68K_CT_040  0x2000
#define M68K_CM_040  0x1000
#define M68K_MA_040  0x0800
#define M68K_ATC_040 0x0400
#define M68K_LK_040  0x0200
#define M68K_RW_040  0x0100
#define M68K_SIZ_040 0x0060
#define M68K_TT_040  0x0018
#define M68K_TM_040  0x0007

#define M68K_TM_040_DATA  0x0001
#define M68K_TM_040_CODE  0x0002
#define M68K_TM_040_SUPER 0x0004

#define M68K_WBV_040   0x80
#define M68K_WBSIZ_040 0x60
#define M68K_WBBYT_040 0x20
#define M68K_WBWRD_040 0x40
#define M68K_WBLNG_040 0x00
#define M68K_WBTT_040  0x18
#define M68K_WBTM_040  0x07

#define M68K_BA_SIZE_MASK    0x60
#define M68K_BA_SIZE_BYTE    0x20
#define M68K_BA_SIZE_WORD    0x40
#define M68K_BA_SIZE_LONG    0x00
#define M68K_BA_SIZE_LINE    0x60

#define M68K_BA_TT_MOVE16    0x08

#define M68K_MMU_B_040   0x0800
#define M68K_MMU_G_040   0x0400
#define M68K_MMU_U1_040  0x0200
#define M68K_MMU_U0_040  0x0100
#define M68K_MMU_S_040   0x0080
#define M68K_MMU_CM_040  0x0060
#define M68K_MMU_M_040   0x0010
#define M68K_MMU_WP_040  0x0004
#define M68K_MMU_T_040   0x0002
#define M68K_MMU_R_040   0x0001

#define M68K_MMU_SR_MASK_040 (M68K_MMU_G_040 | M68K_MMU_U1_040 | \
                              M68K_MMU_U0_040 | M68K_MMU_S_040 | \
                              M68K_MMU_CM_040 | M68K_MMU_M_040 | \
                              M68K_MMU_WP_040)

#define M68K_TCR_ENABLED 0x8000
#define M68K_TCR_PAGE_8K 0x4000

#define M68K_DESC_WRITEPROT 0x00000004
#define M68K_DESC_USED      0x00000008
#define M68K_DESC_MODIFIED  0x00000010
#define M68K_DESC_CACHEMODE 0x00000060
#define M68K_DESC_CM_WRTHRU 0x00000000
#define M68K_DESC_CM_COPYBK 0x00000020
#define M68K_DESC_CM_SERIAL 0x00000040
#define M68K_DESC_CM_NCACHE 0x00000060
#define M68K_DESC_SUPERONLY 0x00000080
#define M68K_DESC_USERATTR  0x00000300
#define M68K_DESC_USERATTR_SHIFT     8
#define M68K_DESC_GLOBAL    0x00000400
#define M68K_DESC_URESERVED 0x00000800

#define M68K_ROOT_POINTER_ENTRIES   128
#define M68K_4K_PAGE_MASK           (~0xff)
#define M68K_POINTER_BASE(entry)    (entry & ~0x1ff)
#define M68K_ROOT_INDEX(addr)       ((address >> 23) & 0x1fc)
#define M68K_POINTER_INDEX(addr)    ((address >> 16) & 0x1fc)
#define M68K_4K_PAGE_BASE(entry)    (next & M68K_4K_PAGE_MASK)
#define M68K_4K_PAGE_INDEX(addr)    ((address >> 10) & 0xfc)
#define M68K_8K_PAGE_MASK           (~0x7f)
#define M68K_8K_PAGE_BASE(entry)    (next & M68K_8K_PAGE_MASK)
#define M68K_8K_PAGE_INDEX(addr)    ((address >> 11) & 0x7c)
#define M68K_UDT_VALID(entry)       (entry & 2)
#define M68K_PDT_VALID(entry)       (entry & 3)
#define M68K_PDT_INDIRECT(entry)    ((entry & 3) == 2)
#define M68K_INDIRECT_POINTER(addr) (addr & ~3)
#define M68K_TTS_POINTER_SHIFT      18
#define M68K_TTS_ROOT_SHIFT         25

#define M68K_TTR_ADDR_BASE 0xff000000
#define M68K_TTR_ADDR_MASK 0x00ff0000
#define M68K_TTR_ADDR_MASK_SHIFT    8
#define M68K_TTR_ENABLED   0x00008000
#define M68K_TTR_SFIELD    0x00006000
#define M68K_TTR_SFIELD_USER   0x0000
#define M68K_TTR_SFIELD_SUPER  0x2000

#define M68K_CR_ASID     0x003
#define M68K_CR_ACR0     0x004
#define M68K_CR_ACR1     0x005
#define M68K_CR_ACR2     0x006
#define M68K_CR_ACR3     0x007
#define M68K_CR_MMUBAR   0x008

#define M68K_CR_PC       0x80F

#define M68K_CR_ROMBAR0  0xC00
#define M68K_CR_ROMBAR1  0xC01
#define M68K_CR_RAMBAR0  0xC04
#define M68K_CR_RAMBAR1  0xC05
#define M68K_CR_MPCR     0xC0C
#define M68K_CR_EDRAMBAR 0xC0D
#define M68K_CR_SECMBAR  0xC0E
#define M68K_CR_MBAR     0xC0F

#define M68K_CR_PCR1U0   0xD02
#define M68K_CR_PCR1L0   0xD03
#define M68K_CR_PCR2U0   0xD04
#define M68K_CR_PCR2L0   0xD05
#define M68K_CR_PCR3U0   0xD06
#define M68K_CR_PCR3L0   0xD07
#define M68K_CR_PCR1U1   0xD0A
#define M68K_CR_PCR1L1   0xD0B
#define M68K_CR_PCR2U1   0xD0C
#define M68K_CR_PCR2L1   0xD0D
#define M68K_CR_PCR3U1   0xD0E
#define M68K_CR_PCR3L1   0xD0F

#define M68K_CR_SFC      0x000
#define M68K_CR_DFC      0x001
#define M68K_CR_USP      0x800
#define M68K_CR_VBR      0x801

#define M68K_CR_CACR     0x002
#define M68K_CR_CAAR     0x802
#define M68K_CR_MSP      0x803
#define M68K_CR_ISP      0x804

#define M68K_CR_TC       0x003
#define M68K_CR_ITT0     0x004
#define M68K_CR_ITT1     0x005
#define M68K_CR_DTT0     0x006
#define M68K_CR_DTT1     0x007
#define M68K_CR_MMUSR    0x805
#define M68K_CR_URP      0x806
#define M68K_CR_SRP      0x807

#define M68K_CR_IACR0    0x004
#define M68K_CR_IACR1    0x005
#define M68K_CR_DACR0    0x006
#define M68K_CR_DACR1    0x007

#define M68K_CR_BUSCR    0x008
#define M68K_CR_PCR      0x808

#define M68K_FPIAR_SHIFT  0
#define M68K_FPIAR        (1 << M68K_FPIAR_SHIFT)
#define M68K_FPSR_SHIFT   1
#define M68K_FPSR         (1 << M68K_FPSR_SHIFT)
#define M68K_FPCR_SHIFT   2
#define M68K_FPCR         (1 << M68K_FPCR_SHIFT)

#define FPSR_CC_MASK  0x0f000000
#define FPSR_CC_A     0x01000000
#define FPSR_CC_I     0x02000000
#define FPSR_CC_Z     0x04000000
#define FPSR_CC_N     0x08000000

#define FPSR_QT_MASK  0x00ff0000
#define FPSR_QT_SHIFT 16

#define FPCR_RND_MASK   0x0030
#define FPCR_RND_N      0x0000
#define FPCR_RND_Z      0x0010
#define FPCR_RND_M      0x0020
#define FPCR_RND_P      0x0030

#define FPCR_PREC_MASK  0x00c0
#define FPCR_PREC_X     0x0000
#define FPCR_PREC_S     0x0040
#define FPCR_PREC_D     0x0080
#define FPCR_PREC_U     0x00c0

#define FPCR_EXCP_MASK 0xff00

#define M68K_CACR_EUSP  0x10

#define MACSR_PAV0  0x100
#define MACSR_OMC   0x080
#define MACSR_SU    0x040
#define MACSR_FI    0x020
#define MACSR_RT    0x010
#define MACSR_N     0x008
#define MACSR_Z     0x004
#define MACSR_V     0x002
#define MACSR_EV    0x001

void m68k_set_irq_level(M68kCPU *cpu, int level, uint8_t vector);
void m68k_switch_sp(CPUM68KState *env);

void do_m68k_semihosting(CPUM68KState *env, int nr);

enum m68k_features {
    M68K_FEATURE_M68K,
    M68K_FEATURE_M68010,
    M68K_FEATURE_M68020,
    M68K_FEATURE_M68030,
    M68K_FEATURE_M68040,
    M68K_FEATURE_M68060,
    M68K_FEATURE_CF_ISA_A,
    M68K_FEATURE_CF_ISA_B,
    M68K_FEATURE_CF_ISA_APLUSC,
    M68K_FEATURE_BRAL,
    M68K_FEATURE_CF_FPU,
    M68K_FEATURE_CF_MAC,
    M68K_FEATURE_CF_EMAC,
    M68K_FEATURE_CF_EMAC_B,
    M68K_FEATURE_USP,
    M68K_FEATURE_MSP,
    M68K_FEATURE_EXT_FULL,
    M68K_FEATURE_WORD_INDEX,
    M68K_FEATURE_SCALED_INDEX,
    M68K_FEATURE_LONG_MULDIV,
    M68K_FEATURE_QUAD_MULDIV,
    M68K_FEATURE_BCCL,
    M68K_FEATURE_BITFIELD,
    M68K_FEATURE_FPU,
    M68K_FEATURE_CAS,
    M68K_FEATURE_BKPT,
    M68K_FEATURE_RTD,
    M68K_FEATURE_CHK2,
    M68K_FEATURE_MOVEP,
    M68K_FEATURE_MOVEC,
    M68K_FEATURE_UNALIGNED_DATA,
    M68K_FEATURE_TRAPCC,
    M68K_FEATURE_MOVEFROMSR_PRIV,
    M68K_FEATURE_EXCEPTION_FORMAT_VEC,
};

static inline bool m68k_feature(CPUM68KState *env, int feature)
{
    return (env->features & BIT_ULL(feature)) != 0;
}

void register_m68k_insns (CPUM68KState *env);

enum {
    ACCESS_SUPER = 0x01,
    ACCESS_STORE = 0x02,
    ACCESS_DEBUG = 0x04,
    ACCESS_PTEST = 0x08,
    ACCESS_CODE  = 0x10,
    ACCESS_DATA  = 0x20,
};

#define CPU_RESOLVING_TYPE TYPE_M68K_CPU

#define MMU_KERNEL_IDX 0
#define MMU_USER_IDX 1

bool m68k_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                       MMUAccessType access_type, int mmu_idx,
                       bool probe, uintptr_t retaddr);
#ifndef CONFIG_USER_ONLY
void m68k_cpu_transaction_failed(CPUState *cs, hwaddr physaddr, vaddr addr,
                                 unsigned size, MMUAccessType access_type,
                                 int mmu_idx, MemTxAttrs attrs,
                                 MemTxResult response, uintptr_t retaddr);
#endif

#define TB_FLAGS_MACSR          0x0f
#define TB_FLAGS_MSR_S_BIT      13
#define TB_FLAGS_MSR_S          (1 << TB_FLAGS_MSR_S_BIT)
#define TB_FLAGS_SFC_S_BIT      14
#define TB_FLAGS_SFC_S          (1 << TB_FLAGS_SFC_S_BIT)
#define TB_FLAGS_DFC_S_BIT      15
#define TB_FLAGS_DFC_S          (1 << TB_FLAGS_DFC_S_BIT)
#define TB_FLAGS_TRACE          16
#define TB_FLAGS_TRACE_BIT      (1 << TB_FLAGS_TRACE)

void dump_mmu(CPUM68KState *env);

#endif
