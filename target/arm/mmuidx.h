/*
 * QEMU Arm software mmu index definitions
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TARGET_ARM_MMUIDX_H
#define TARGET_ARM_MMUIDX_H

#define ARM_MMU_IDX_A     0x20
#define ARM_MMU_IDX_NOTLB 0x40
#define ARM_MMU_IDX_M     0x80

#define ARM_MMU_IDX_M_PRIV   0x1
#define ARM_MMU_IDX_M_NEGPRI 0x2
#define ARM_MMU_IDX_M_S      0x4

#define ARM_MMU_IDX_TYPE_MASK \
    (ARM_MMU_IDX_A | ARM_MMU_IDX_M | ARM_MMU_IDX_NOTLB)
#define ARM_MMU_IDX_COREIDX_MASK 0x1f

typedef enum ARMMMUIdx {

    ARMMMUIdx_E10_0      = 0 | ARM_MMU_IDX_A,
    ARMMMUIdx_E10_0_GCS  = 1 | ARM_MMU_IDX_A,
    ARMMMUIdx_E10_1      = 2 | ARM_MMU_IDX_A,
    ARMMMUIdx_E10_1_PAN  = 3 | ARM_MMU_IDX_A,
    ARMMMUIdx_E10_1_GCS  = 4 | ARM_MMU_IDX_A,

    ARMMMUIdx_E20_0      = 5 | ARM_MMU_IDX_A,
    ARMMMUIdx_E20_0_GCS  = 6 | ARM_MMU_IDX_A,
    ARMMMUIdx_E20_2      = 7 | ARM_MMU_IDX_A,
    ARMMMUIdx_E20_2_PAN  = 8 | ARM_MMU_IDX_A,
    ARMMMUIdx_E20_2_GCS  = 9 | ARM_MMU_IDX_A,

    ARMMMUIdx_E2         = 10 | ARM_MMU_IDX_A,
    ARMMMUIdx_E2_GCS     = 11 | ARM_MMU_IDX_A,

    ARMMMUIdx_E3         = 12 | ARM_MMU_IDX_A,
    ARMMMUIdx_E3_GCS     = 13 | ARM_MMU_IDX_A,
    ARMMMUIdx_E30_0      = 14 | ARM_MMU_IDX_A,
    ARMMMUIdx_E30_3_PAN  = 15 | ARM_MMU_IDX_A,

    ARMMMUIdx_Stage2_S   = 16 | ARM_MMU_IDX_A,
    ARMMMUIdx_Stage2     = 17 | ARM_MMU_IDX_A,

    ARMMMUIdx_Phys_S     = 18 | ARM_MMU_IDX_A,
    ARMMMUIdx_Phys_NS    = 19 | ARM_MMU_IDX_A,
    ARMMMUIdx_Phys_Root  = 20 | ARM_MMU_IDX_A,
    ARMMMUIdx_Phys_Realm = 21 | ARM_MMU_IDX_A,

    ARMMMUIdx_Stage1_E0 = 0 | ARM_MMU_IDX_NOTLB,
    ARMMMUIdx_Stage1_E1 = 1 | ARM_MMU_IDX_NOTLB,
    ARMMMUIdx_Stage1_E1_PAN = 2 | ARM_MMU_IDX_NOTLB,
    ARMMMUIdx_Stage1_E0_GCS = 3 | ARM_MMU_IDX_NOTLB,
    ARMMMUIdx_Stage1_E1_GCS = 4 | ARM_MMU_IDX_NOTLB,

    ARMMMUIdx_MUser = ARM_MMU_IDX_M,
    ARMMMUIdx_MPriv = ARM_MMU_IDX_M | ARM_MMU_IDX_M_PRIV,
    ARMMMUIdx_MUserNegPri = ARMMMUIdx_MUser | ARM_MMU_IDX_M_NEGPRI,
    ARMMMUIdx_MPrivNegPri = ARMMMUIdx_MPriv | ARM_MMU_IDX_M_NEGPRI,
    ARMMMUIdx_MSUser = ARMMMUIdx_MUser | ARM_MMU_IDX_M_S,
    ARMMMUIdx_MSPriv = ARMMMUIdx_MPriv | ARM_MMU_IDX_M_S,
    ARMMMUIdx_MSUserNegPri = ARMMMUIdx_MUserNegPri | ARM_MMU_IDX_M_S,
    ARMMMUIdx_MSPrivNegPri = ARMMMUIdx_MPrivNegPri | ARM_MMU_IDX_M_S,
} ARMMMUIdx;

#define TO_CORE_BIT(NAME) \
    ARMMMUIdxBit_##NAME = 1 << (ARMMMUIdx_##NAME & ARM_MMU_IDX_COREIDX_MASK)

typedef enum ARMMMUIdxBit {
    TO_CORE_BIT(E10_0),
    TO_CORE_BIT(E10_0_GCS),
    TO_CORE_BIT(E10_1),
    TO_CORE_BIT(E10_1_PAN),
    TO_CORE_BIT(E10_1_GCS),
    TO_CORE_BIT(E20_0),
    TO_CORE_BIT(E20_0_GCS),
    TO_CORE_BIT(E20_2),
    TO_CORE_BIT(E20_2_PAN),
    TO_CORE_BIT(E20_2_GCS),
    TO_CORE_BIT(E2),
    TO_CORE_BIT(E2_GCS),
    TO_CORE_BIT(E3),
    TO_CORE_BIT(E3_GCS),
    TO_CORE_BIT(E30_0),
    TO_CORE_BIT(E30_3_PAN),
    TO_CORE_BIT(Stage2),
    TO_CORE_BIT(Stage2_S),

    TO_CORE_BIT(MUser),
    TO_CORE_BIT(MPriv),
    TO_CORE_BIT(MUserNegPri),
    TO_CORE_BIT(MPrivNegPri),
    TO_CORE_BIT(MSUser),
    TO_CORE_BIT(MSPriv),
    TO_CORE_BIT(MSUserNegPri),
    TO_CORE_BIT(MSPrivNegPri),
} ARMMMUIdxBit;

#undef TO_CORE_BIT

#define MMU_USER_IDX 0

#endif
