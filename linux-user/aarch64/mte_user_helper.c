/*
 * ARM MemTag convenience functions.
 *
 * This code is licensed under the GNU GPL v2 or later.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "qemu/osdep.h"
#include "qemu.h"
#include "mte_user_helper.h"

void arm_set_mte_tcf0(CPUArchState *env, abi_long value)
{
    unsigned tcf = 0;
    if (value & PR_MTE_TCF_SYNC) {
        tcf = 1;
    } else if (value & PR_MTE_TCF_ASYNC) {
        tcf = 2;
    }
    env->cp15.sctlr_el[1] = deposit64(env->cp15.sctlr_el[1], 38, 2, tcf);
}
