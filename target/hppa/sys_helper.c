/*
 * Helpers for HPPA system instructions.
 *
 * Copyright (c) 2016 Richard Henderson <rth@twiddle.net>
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

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "qemu/timer.h"
#include "system/runstate.h"
#include "system/system.h"
#include "chardev/char-fe.h"

void HELPER(write_interval_timer)(CPUHPPAState *env, target_ulong val)
{
    HPPACPU *cpu = env_archcpu(env);
    uint64_t current = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint64_t timeout;

    timeout = deposit64(current, 0, 34, (uint64_t)val << 2);

    if (timeout < current + 1000) {
        timeout += 1ULL << 34;
    }

    cpu->env.cr[CR_IT] = timeout;
    timer_mod(cpu->alarm_timer, timeout);
}

void HELPER(halt)(CPUHPPAState *env)
{
    qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    helper_excp(env, EXCP_HLT);
}

void HELPER(reset)(CPUHPPAState *env)
{
    qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    helper_excp(env, EXCP_HLT);
}

target_ulong HELPER(swap_system_mask)(CPUHPPAState *env, target_ulong nsm)
{
    target_ulong psw = env->psw;
    cpu_hppa_put_psw(env, (psw & ~PSW_SM) | (nsm & PSW_SM));
    return psw & PSW_SM;
}

void HELPER(rfi)(CPUHPPAState *env)
{
    uint64_t mask;

    cpu_hppa_put_psw(env, env->cr[CR_IPSW]);

    mask = env->gva_offset_mask;

    env->iaoq_f = env->cr[CR_IIAOQ];
    env->iaoq_b = env->cr_back[1];
    env->iasq_f = (env->cr[CR_IIASQ] << 32) & ~(env->iaoq_f & mask);
    env->iasq_b = (env->cr_back[0] << 32) & ~(env->iaoq_b & mask);

    if (qemu_loglevel_mask(CPU_LOG_INT)) {
        FILE *logfile = qemu_log_trylock();
        if (logfile) {
            CPUState *cs = env_cpu(env);

            fprintf(logfile, "RFI: cpu %d\n", cs->cpu_index);
            hppa_cpu_dump_state(cs, logfile, 0);
            qemu_log_unlock(logfile);
        }
    }
}

static void getshadowregs(CPUHPPAState *env)
{
    env->gr[1] = env->shadow[0];
    env->gr[8] = env->shadow[1];
    env->gr[9] = env->shadow[2];
    env->gr[16] = env->shadow[3];
    env->gr[17] = env->shadow[4];
    env->gr[24] = env->shadow[5];
    env->gr[25] = env->shadow[6];
}

void HELPER(rfi_r)(CPUHPPAState *env)
{
    getshadowregs(env);
    helper_rfi(env);
}

#ifndef CONFIG_USER_ONLY
void HELPER(diag_console_output)(CPUHPPAState *env)
{
    CharFrontend *fe;
    Chardev *serial_port;
    unsigned char c;

    serial_port = serial_hd(0);
    if (!serial_port) {
        return;
    }

    fe = serial_port->fe;
    if (!fe ||
        !qemu_chr_fe_backend_connected(fe)) {
        return;
    }

    c = (unsigned char)env->gr[26];
    qemu_chr_fe_write(fe, &c, sizeof(c));
}
#endif
