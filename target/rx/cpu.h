/*
 *  RX emulation definition
 *
 *  Copyright (c) 2019 Yoshinori Sato
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

#ifndef RX_CPU_H
#define RX_CPU_H

#include "qemu/bitops.h"
#include "hw/registerfields.h"
#include "cpu-qom.h"

#include "exec/cpu-common.h"
#include "exec/cpu-defs.h"
#include "exec/cpu-interrupt.h"
#include "qemu/cpu-float.h"

#ifdef CONFIG_USER_ONLY
#error "RX does not support user mode emulation"
#endif

REG32(PSW, 0)
FIELD(PSW, C, 0, 1)
FIELD(PSW, Z, 1, 1)
FIELD(PSW, S, 2, 1)
FIELD(PSW, O, 3, 1)
FIELD(PSW, I, 16, 1)
FIELD(PSW, U, 17, 1)
FIELD(PSW, PM, 20, 1)
FIELD(PSW, IPL, 24, 4)

REG32(FPSW, 0)
FIELD(FPSW, RM, 0, 2)
FIELD(FPSW, CV, 2, 1)
FIELD(FPSW, CO, 3, 1)
FIELD(FPSW, CZ, 4, 1)
FIELD(FPSW, CU, 5, 1)
FIELD(FPSW, CX, 6, 1)
FIELD(FPSW, CE, 7, 1)
FIELD(FPSW, CAUSE, 2, 6)
FIELD(FPSW, DN, 8, 1)
FIELD(FPSW, EV, 10, 1)
FIELD(FPSW, EO, 11, 1)
FIELD(FPSW, EZ, 12, 1)
FIELD(FPSW, EU, 13, 1)
FIELD(FPSW, EX, 14, 1)
FIELD(FPSW, ENABLE, 10, 5)
FIELD(FPSW, FV, 26, 1)
FIELD(FPSW, FO, 27, 1)
FIELD(FPSW, FZ, 28, 1)
FIELD(FPSW, FU, 29, 1)
FIELD(FPSW, FX, 30, 1)
FIELD(FPSW, FLAGS, 26, 4)
FIELD(FPSW, FS, 31, 1)

enum {
    NUM_REGS = 16,
};

typedef struct CPUArchState {
    uint32_t regs[NUM_REGS];
    uint32_t psw_o;
    uint32_t psw_s;
    uint32_t psw_z;
    uint32_t psw_c;
    uint32_t psw_u;
    uint32_t psw_i;
    uint32_t psw_pm;
    uint32_t psw_ipl;
    uint32_t bpsw;
    uint32_t bpc;
    uint32_t isp;
    uint32_t usp;
    uint32_t pc;
    uint32_t intb;
    uint32_t fintv;
    uint32_t fpsw;
    uint64_t acc;

    struct {} end_reset_fields;

    uint32_t in_sleep;
    uint32_t req_irq;
    uint32_t req_ipl;
    uint32_t ack_irq;
    uint32_t ack_ipl;
    float_status fp_status;
    qemu_irq ack;
} CPURXState;

struct ArchCPU {
    CPUState parent_obj;

    CPURXState env;
};

struct RXCPUClass {
    CPUClass parent_class;

    DeviceRealize parent_realize;
    ResettablePhases parent_phases;
};

#define CPU_RESOLVING_TYPE TYPE_RX_CPU

const char *rx_crname(uint8_t cr);
void rx_cpu_do_interrupt(CPUState *cpu);
bool rx_cpu_exec_interrupt(CPUState *cpu, int int_req);
hwaddr rx_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
void rx_cpu_dump_state(CPUState *cpu, FILE *f, int flags);
int rx_cpu_gdb_read_register(CPUState *cpu, GByteArray *buf, int reg);
int rx_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);

void rx_translate_init(void);
void rx_translate_code(CPUState *cs, TranslationBlock *tb,
                       int *max_insns, vaddr pc, void *host_pc);
void rx_cpu_unpack_psw(CPURXState *env, uint32_t psw, int rte);

#define CPU_INTERRUPT_SOFT CPU_INTERRUPT_TGT_INT_0
#define CPU_INTERRUPT_FIR  CPU_INTERRUPT_TGT_INT_1

#define RX_CPU_IRQ 0
#define RX_CPU_FIR 1

static inline uint32_t rx_cpu_pack_psw(CPURXState *env)
{
    uint32_t psw = 0;
    psw = FIELD_DP32(psw, PSW, IPL, env->psw_ipl);
    psw = FIELD_DP32(psw, PSW, PM,  env->psw_pm);
    psw = FIELD_DP32(psw, PSW, U,   env->psw_u);
    psw = FIELD_DP32(psw, PSW, I,   env->psw_i);
    psw = FIELD_DP32(psw, PSW, O,   env->psw_o >> 31);
    psw = FIELD_DP32(psw, PSW, S,   env->psw_s >> 31);
    psw = FIELD_DP32(psw, PSW, Z,   env->psw_z == 0);
    psw = FIELD_DP32(psw, PSW, C,   env->psw_c);
    return psw;
}

#endif
