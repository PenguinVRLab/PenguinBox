/*
 * QEMU PowerPC SPI model
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PNV_SPI_CONTROLLER_REGS_H
#define PNV_SPI_CONTROLLER_REGS_H

/*
 * Macros from target/ppc/cpu.h
 * These macros are copied from ppc target specific file target/ppc/cpu.h
 * as target/ppc/cpu.h cannot be included here.
 */
#define PPC_BIT(bit)            (0x8000000000000000ULL >> (bit))
#define PPC_BIT8(bit)           (0x80 >> (bit))
#define PPC_BITMASK(bs, be)     ((PPC_BIT(bs) - PPC_BIT(be)) | PPC_BIT(bs))
#define PPC_BITMASK8(bs, be)    ((PPC_BIT8(bs) - PPC_BIT8(be)) | PPC_BIT8(bs))
#define MASK_TO_LSH(m)          (__builtin_ffsll(m) - 1)
#define GETFIELD(m, v)          (((v) & (m)) >> MASK_TO_LSH(m))
#define SETFIELD(m, v, val) \
        (((v) & ~(m)) | ((((typeof(v))(val)) << MASK_TO_LSH(m)) & (m)))

#define ERROR_REG               0x00

#define SPI_CTR_CFG_REG         0x01
#define SPI_CTR_CFG_N1          PPC_BITMASK(0, 7)
#define SPI_CTR_CFG_N2          PPC_BITMASK(8, 15)
#define SPI_CTR_CFG_CMP1        PPC_BITMASK(24, 31)
#define SPI_CTR_CFG_CMP2        PPC_BITMASK(32, 39)
#define SPI_CTR_CFG_N1_CTRL_B1  PPC_BIT(49)
#define SPI_CTR_CFG_N1_CTRL_B2  PPC_BIT(50)
#define SPI_CTR_CFG_N1_CTRL_B3  PPC_BIT(51)
#define SPI_CTR_CFG_N2_CTRL_B0  PPC_BIT(52)
#define SPI_CTR_CFG_N2_CTRL_B1  PPC_BIT(53)
#define SPI_CTR_CFG_N2_CTRL_B2  PPC_BIT(54)
#define SPI_CTR_CFG_N2_CTRL_B3  PPC_BIT(55)

#define CONFIG_REG1             0x02

#define SPI_CLK_CFG_REG         0x03
#define SPI_CLK_CFG_HARD_RST    0x0084000000000000;
#define SPI_CLK_CFG_RST_CTRL    PPC_BITMASK(24, 27)
#define SPI_CLK_CFG_ECC_EN      PPC_BIT(28)
#define SPI_CLK_CFG_ECC_CTRL    PPC_BITMASK(29, 30)

#define SPI_MM_REG              0x04
#define SPI_MM_RDR_MATCH_VAL    PPC_BITMASK(32, 47)
#define SPI_MM_RDR_MATCH_MASK   PPC_BITMASK(48, 63)

#define SPI_XMIT_DATA_REG       0x05

#define SPI_RCV_DATA_REG        0x06

#define SPI_SEQ_OP_REG          0x07

#define SPI_STS_REG             0x08
#define SPI_STS_RDR_FULL        PPC_BIT(0)
#define SPI_STS_RDR_OVERRUN     PPC_BIT(1)
#define SPI_STS_RDR_UNDERRUN    PPC_BIT(2)
#define SPI_STS_TDR_FULL        PPC_BIT(4)
#define SPI_STS_TDR_OVERRUN     PPC_BIT(5)
#define SPI_STS_TDR_UNDERRUN    PPC_BIT(6)
#define SPI_STS_SEQ_FSM         PPC_BITMASK(8, 15)
#define SPI_STS_SHIFTER_FSM     PPC_BITMASK(16, 27)
#define SPI_STS_SEQ_INDEX       PPC_BITMASK(28, 31)
#define SPI_STS_GEN_STATUS_B3   PPC_BIT(35)
#define SPI_STS_RDR             PPC_BITMASK(1, 3)
#define SPI_STS_TDR             PPC_BITMASK(5, 7)

#define FSM_DONE                        0x100
#define FSM_SHIFT_N2                    0x020
#define FSM_WAIT                        0x010
#define FSM_SHIFT_N1                    0x004
#define FSM_START                       0x002
#define FSM_IDLE                        0x001

#define SEQ_STATE_INDEX_INCREMENT       0x08
#define SEQ_STATE_EXECUTE               0x04
#define SEQ_STATE_DECODE                0x02
#define SEQ_STATE_IDLE                  0x01

#define SEQ_OP_STOP                     0x00
#define SEQ_OP_SELECT_SLAVE             0x10
#define SEQ_OP_SHIFT_N1                 0x30
#define SEQ_OP_SHIFT_N2                 0x40
#define SEQ_OP_BRANCH_IFNEQ_RDR         0x60
#define SEQ_OP_TRANSFER_TDR             0xC0
#define SEQ_OP_BRANCH_IFNEQ_INC_1       0xE0
#define SEQ_OP_BRANCH_IFNEQ_INC_2       0xF0
#define NUM_SEQ_OPS                     8

#endif
