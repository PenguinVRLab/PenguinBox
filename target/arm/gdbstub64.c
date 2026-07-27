/*
 * ARM gdb server stub: AArch64 specific functions.
 *
 * Copyright (c) 2013 SUSE LINUX Products GmbH
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
#include "internals.h"
#include "gdbstub/helpers.h"
#include "gdbstub/commands.h"
#include "tcg/mte_helper.h"
#if defined(CONFIG_USER_ONLY) && defined(CONFIG_LINUX)
#include <sys/prctl.h>
#include "mte_user_helper.h"
#endif
#ifdef CONFIG_TCG
#include "accel/tcg/cpu-mmu-index.h"
#include "exec/target_page.h"
#endif

int aarch64_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    if (n < 31) {
        return gdb_get_reg64(mem_buf, env->xregs[n]);
    }
    switch (n) {
    case 31:
        return gdb_get_reg64(mem_buf, env->xregs[31]);
    case 32:
        return gdb_get_reg64(mem_buf, env->pc);
    case 33:
        return gdb_get_reg32(mem_buf, pstate_read(env));
    }
    return 0;
}

int aarch64_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    uint64_t tmp;

    tmp = ldq_p(mem_buf);

    if (n < 31) {
        env->xregs[n] = tmp;
        return 8;
    }
    switch (n) {
    case 31:
        env->xregs[31] = tmp;
        return 8;
    case 32:
        env->pc = tmp;
        return 8;
    case 33:
        pstate_write(env, tmp);
        return 4;
    }
    return 0;
}

int aarch64_gdb_get_fpu_reg(CPUState *cs, GByteArray *buf, int reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    switch (reg) {
    case 0 ... 31:
    {
        uint64_t *q = aa64_vfp_qreg(env, reg);
        return gdb_get_reg128(buf, q[1], q[0]);
    }
    case 32:
        return gdb_get_reg32(buf, vfp_get_fpsr(env));
    case 33:
        return gdb_get_reg32(buf, vfp_get_fpcr(env));
    default:
        return 0;
    }
}

int aarch64_gdb_set_fpu_reg(CPUState *cs, uint8_t *buf, int reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    switch (reg) {
    case 0 ... 31:
        {
            uint64_t *q = aa64_vfp_qreg(env, reg);

            if (target_big_endian()) {
                q[1] = ldq_p(buf);
                q[0] = ldq_p(buf + 8);
            } else{
                q[0] = ldq_p(buf);
                q[1] = ldq_p(buf + 8);
            }

            return 16;
        }
    case 32:
        vfp_set_fpsr(env, ldl_p(buf));
        return 4;
    case 33:
        vfp_set_fpcr(env, ldl_p(buf));
        return 4;
    default:
        return 0;
    }
}

int aarch64_gdb_get_sve_reg(CPUState *cs, GByteArray *buf, int reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    switch (reg) {
    case 0 ... 31:
    {
        int vq, len = 0;
        for (vq = 0; vq < cpu->sve_max_vq; vq++) {
            len += gdb_get_reg128(buf,
                                  env->vfp.zregs[reg].d[vq * 2 + 1],
                                  env->vfp.zregs[reg].d[vq * 2]);
        }
        return len;
    }
    case 32:
        return gdb_get_reg32(buf, vfp_get_fpsr(env));
    case 33:
        return gdb_get_reg32(buf, vfp_get_fpcr(env));
    case 34 ... 50:
    {
        int preg = reg - 34;
        int vq, len = 0;
        for (vq = 0; vq < cpu->sve_max_vq; vq = vq + 4) {
            len += gdb_get_reg64(buf, env->vfp.pregs[preg].p[vq / 4]);
        }
        return len;
    }
    case 51:
    {
        int vq = sve_vqm1_for_el(env, arm_current_el(env)) + 1;
        return gdb_get_reg64(buf, vq * 2);
    }
    default:
        qemu_log_mask(LOG_UNIMP, "%s: out of range register %d", __func__, reg);
        break;
    }

    return 0;
}

int aarch64_gdb_set_sve_reg(CPUState *cs, uint8_t *buf, int reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    switch (reg) {
    case 0 ... 31:
    {
        int vq, len = 0;
        for (vq = 0; vq < cpu->sve_max_vq; vq++) {
            if (target_big_endian()) {
                env->vfp.zregs[reg].d[vq * 2 + 1] = ldq_p(buf);
                buf += 8;
                env->vfp.zregs[reg].d[vq * 2] = ldq_p(buf);
            } else{
                env->vfp.zregs[reg].d[vq * 2] = ldq_p(buf);
                buf += 8;
                env->vfp.zregs[reg].d[vq * 2 + 1] = ldq_p(buf);
            }
            buf += 8;
            len += 16;
        }
        return len;
    }
    case 32:
        vfp_set_fpsr(env, *(uint32_t *)buf);
        return 4;
    case 33:
        vfp_set_fpcr(env, *(uint32_t *)buf);
        return 4;
    case 34 ... 50:
    {
        int preg = reg - 34;
        int vq, len = 0;
        for (vq = 0; vq < cpu->sve_max_vq; vq = vq + 4) {
            env->vfp.pregs[preg].p[vq / 4] = ldq_p(buf);
            buf += 8;
            len += 8;
        }
        return len;
    }
    case 51:
        return 0;
    default:
        break;
    }

    return 0;
}

int aarch64_gdb_get_sme_reg(CPUState *cs, GByteArray *buf, int reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    switch (reg) {
    case 0:
    {
        int vq = 0;
        if (FIELD_EX64(env->svcr, SVCR, SM)) {
            vq = sve_vqm1_for_el_sm(env, arm_current_el(env),
                                    FIELD_EX64(env->svcr, SVCR, SM)) + 1;
        }
        return gdb_get_reg64(buf, vq * 2);
    }
    case 1:
        return gdb_get_reg64(buf, env->svcr);
    case 2:
    {
        int len = 0;
        int vq = cpu->sme_max_vq;
        int svl = vq * 16;
        for (int i = 0; i < svl; i++) {
            for (int q = 0; q < vq; q++) {
                len += gdb_get_reg128(buf,
                                      env->za_state.za[i].d[q * 2 + 1],
                                      env->za_state.za[i].d[q * 2]);
            }
        }
        return len;
    }
    default:
        qemu_log_mask(LOG_UNIMP, "%s: out of range register %d", __func__, reg);
        break;
    }

    return 0;
}

int aarch64_gdb_set_sme_reg(CPUState *cs, uint8_t *buf, int reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    switch (reg) {
    case 0:
        return 8;
    case 1:
        aarch64_set_svcr(env, ldq_le_p(buf),
                         R_SVCR_SM_MASK | R_SVCR_ZA_MASK);
        return 8;
    case 2:
    {
        int len = 0;
        int vq = cpu->sme_max_vq;
        int svl = vq * 16;
        for (int i = 0; i < svl; i++) {
            for (int q = 0; q < vq; q++) {
                if (target_big_endian()) {
                    env->za_state.za[i].d[q * 2 + 1] = ldq_p(buf);
                    buf += 8;
                    env->za_state.za[i].d[q * 2] = ldq_p(buf);
                } else{
                    env->za_state.za[i].d[q * 2] = ldq_p(buf);
                    buf += 8;
                    env->za_state.za[i].d[q * 2 + 1] = ldq_p(buf);
                }
                buf += 8;
                len += 16;
            }
        }
        return len;
    }
    default:
        break;
    }

    return 0;
}

int aarch64_gdb_get_sme2_reg(CPUState *cs, GByteArray *buf, int reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    int len = 0;

    switch (reg) {
    case 0:
        for (int i = 0; i < ARRAY_SIZE(env->za_state.zt0); i += 2) {
            len += gdb_get_reg128(buf, env->za_state.zt0[i + 1],
                                  env->za_state.zt0[i]);
        }
        return len;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: out of range register %d", __func__, reg);
        break;
    }

    return 0;
}

int aarch64_gdb_set_sme2_reg(CPUState *cs, uint8_t *buf, int reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    int len = 0;

    switch (reg) {
    case 0:
        for (int i = 0; i < ARRAY_SIZE(env->za_state.zt0); i += 2) {
            if (target_big_endian()) {
                env->za_state.zt0[i + 1] = ldq_p(buf);
                buf += 8;
                env->za_state.zt0[i] = ldq_p(buf);
            } else {
                env->za_state.zt0[i] = ldq_p(buf);
                buf += 8;
                env->za_state.zt0[i + 1] = ldq_p(buf);
            }
            buf += 8;
            len += 16;
        }
        return len;
    default:
        break;
    }

    return 0;
}

int aarch64_gdb_get_tls_reg(CPUState *cs, GByteArray *buf, int reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    switch (reg) {
    case 0:
        return gdb_get_reg64(buf, env->cp15.tpidr_el[0]);
    case 1:
        return gdb_get_reg64(buf, env->cp15.tpidr2_el0);
    default:
        break;
    }

    return 0;
}

int aarch64_gdb_set_tls_reg(CPUState *cs, uint8_t *buf, int reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    switch (reg) {
    case 0:
        env->cp15.tpidr_el[0] = ldq_p(buf);
        return 8;
    case 1:
        env->cp15.tpidr2_el0 = ldq_p(buf);
        return 8;
    default:
        break;
    }

    return 0;
}

int aarch64_gdb_get_pauth_reg(CPUState *cs, GByteArray *buf, int reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    switch (reg) {
    case 0:
    case 1:
    case 2:
    case 3:
        {
            bool is_data = !(reg & 1);
            bool is_high = reg & 2;
            ARMMMUIdx mmu_idx = arm_stage1_mmu_idx(env);
            ARMVAParameters param;

            param = aa64_va_parameters(env, -is_high, mmu_idx, is_data, false);
            return gdb_get_reg64(buf, pauth_ptr_mask(param));
        }
    default:
        return 0;
    }
}

int aarch64_gdb_set_pauth_reg(CPUState *cs, uint8_t *buf, int reg)
{
    return 0;
}

static void output_vector_union_type(GDBFeatureBuilder *builder, int reg_width,
                                     const char *name)
{
    struct TypeSize {
        const char *gdb_type;
        short size;
        char sz, suffix;
    };

    static const struct TypeSize vec_lanes[] = {
        { "uint128", 128, 'q', 'u' },
        { "int128", 128, 'q', 's' },
        { "ieee_double", 64, 'd', 'f' },
        { "uint64", 64, 'd', 'u' },
        { "int64", 64, 'd', 's' },
        { "ieee_single", 32, 's', 'f' },
        { "uint32", 32, 's', 'u' },
        { "int32", 32, 's', 's' },
        { "ieee_half", 16, 'h', 'f' },
        { "uint16", 16, 'h', 'u' },
        { "int16", 16, 'h', 's' },
        { "uint8", 8, 'b', 'u' },
        { "int8", 8, 'b', 's' },
    };

    static const char suf[] = { 'b', 'h', 's', 'd', 'q' };
    int i, j;

    for (i = 0; i < ARRAY_SIZE(vec_lanes); i++) {
        gdb_feature_builder_append_tag(
            builder, "<vector id=\"%s%c%c\" type=\"%s\" count=\"%d\"/>",
            name, vec_lanes[i].sz, vec_lanes[i].suffix,
            vec_lanes[i].gdb_type, reg_width / vec_lanes[i].size);
    }

    for (i = 0; i < ARRAY_SIZE(suf); i++) {
        int bits = 8 << i;

        gdb_feature_builder_append_tag(builder, "<union id=\"%sn%c\">",
                                       name, suf[i]);
        for (j = 0; j < ARRAY_SIZE(vec_lanes); j++) {
            if (vec_lanes[j].size == bits) {
                gdb_feature_builder_append_tag(
                    builder, "<field name=\"%c\" type=\"%s%c%c\"/>",
                    vec_lanes[j].suffix, name,
                    vec_lanes[j].sz, vec_lanes[j].suffix);
            }
        }
        gdb_feature_builder_append_tag(builder, "</union>");
    }

    gdb_feature_builder_append_tag(builder, "<union id=\"%s\">", name);
    for (i = ARRAY_SIZE(suf) - 1; i >= 0; i--) {
        gdb_feature_builder_append_tag(builder,
                                       "<field name=\"%c\" type=\"%sn%c\"/>",
                                       suf[i], name, suf[i]);
    }
    gdb_feature_builder_append_tag(builder, "</union>");
}

GDBFeature *arm_gen_dynamic_svereg_feature(CPUState *cs, int base_reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    int reg_width = cpu->sve_max_vq * 128;
    int pred_width = cpu->sve_max_vq * 16;
    GDBFeatureBuilder builder;
    char *name;
    int reg = 0;
    int i;

    gdb_feature_builder_init(&builder, &cpu->dyn_svereg_feature.desc,
                             "org.gnu.gdb.aarch64.sve", "sve-registers.xml",
                             base_reg);

    output_vector_union_type(&builder, reg_width, "svev");

    gdb_feature_builder_append_tag(
        &builder, "<vector id=\"svep\" type=\"uint8\" count=\"%d\"/>",
        pred_width / 8);

    for (i = 0; i < 32; i++) {
        name = g_strdup_printf("z%d", i);
        gdb_feature_builder_append_reg(&builder, name, reg_width, reg++,
                                       "svev", NULL);
    }

    gdb_feature_builder_append_reg(&builder, "fpsr", 32, reg++,
                                   "int", "float");
    gdb_feature_builder_append_reg(&builder, "fpcr", 32, reg++,
                                   "int", "float");

    for (i = 0; i < 16; i++) {
        name = g_strdup_printf("p%d", i);
        gdb_feature_builder_append_reg(&builder, name, pred_width, reg++,
                                       "svep", NULL);
    }
    gdb_feature_builder_append_reg(&builder, "ffr", pred_width, reg++,
                                   "svep", "vector");

    gdb_feature_builder_append_reg(&builder, "vg", 64, reg++, "int", NULL);

    gdb_feature_builder_end(&builder);

    return &cpu->dyn_svereg_feature.desc;
}

GDBFeature *arm_gen_dynamic_smereg_feature(CPUState *cs, int base_reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    int vq = cpu->sme_max_vq;
    int svl = vq * 16;
    GDBFeatureBuilder builder;
    int reg = 0;

    gdb_feature_builder_init(&builder, &cpu->dyn_smereg_feature.desc,
                             "org.gnu.gdb.aarch64.sme", "sme-registers.xml",
                             base_reg);


    gdb_feature_builder_append_tag(
        &builder, "<vector id=\"sme_bv\" type=\"uint8\" count=\"%d\"/>",
        svl);

    gdb_feature_builder_append_tag(
        &builder, "<vector id=\"sme_bvv\" type=\"sme_bv\" count=\"%d\"/>",
        svl);

    gdb_feature_builder_append_reg(&builder, "svg", 64, reg++, "int", NULL);
    gdb_feature_builder_append_reg(&builder, "svcr", 64, reg++, "int", NULL);
    gdb_feature_builder_append_reg(&builder, "za", svl * svl * 8, reg++,
                                   "sme_bvv", NULL);

    gdb_feature_builder_end(&builder);

    return &cpu->dyn_smereg_feature.desc;
}

GDBFeature *arm_gen_dynamic_tls_feature(CPUState *cs, int base_reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    GDBFeatureBuilder builder;
    int reg = 0;

    gdb_feature_builder_init(&builder, &cpu->dyn_tls_feature.desc,
                             "org.gnu.gdb.aarch64.tls", "tls-registers.xml",
                             base_reg);

    gdb_feature_builder_append_reg(&builder, "tpidr", 64,
                                   reg++, "data_ptr", NULL);
    if (cpu_isar_feature(aa64_sme, cpu)) {
        gdb_feature_builder_append_reg(&builder, "tpidr2", 64,
                                       reg++, "data_ptr", NULL);
    }
    gdb_feature_builder_end(&builder);

    return &cpu->dyn_tls_feature.desc;
}

#ifdef CONFIG_USER_ONLY
int aarch64_gdb_get_tag_ctl_reg(CPUState *cs, GByteArray *buf, int reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    uint64_t tcf0;

    assert(reg == 0);

    tcf0 = extract64(env->cp15.sctlr_el[1], 38, 2);

    return gdb_get_reg64(buf, tcf0);
}

int aarch64_gdb_set_tag_ctl_reg(CPUState *cs, uint8_t *buf, int reg)
{
#if defined(CONFIG_LINUX)
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    uint8_t tcf;

    assert(reg == 0);

    tcf = *buf << PR_MTE_TCF_SHIFT;

    if (!tcf) {
        return 0;
    }

    arm_set_mte_tcf0(env, tcf);

    return 1;
#else
    return 0;
#endif
}
#endif

#ifdef CONFIG_TCG
static void handle_q_memtag(GArray *params, void *user_ctx)
{
    ARMCPU *cpu = ARM_CPU(user_ctx);
    CPUARMState *env = &cpu->env;
    uint32_t mmu_index;

    uint64_t addr = gdb_get_cmd_param(params, 0)->val_ull;
    uint64_t len = gdb_get_cmd_param(params, 1)->val_ul;
    int type = gdb_get_cmd_param(params, 2)->val_ul;

    uint8_t *tags;
    uint8_t addr_tag;

    g_autoptr(GString) str_buf = g_string_new(NULL);

    if (len != 1) {
        gdb_put_packet("E02");
    }

    if (type != 1) {
        gdb_put_packet("E03");
    }

    mmu_index = cpu_mmu_index(env_cpu(env), false);
    tags = allocation_tag_mem_probe(env, mmu_index, addr, MMU_DATA_LOAD, 1,
                                    MMU_DATA_LOAD, true, 0);
    if (!tags) {
        gdb_put_packet("E04");
        return;
    }

    addr_tag = load_tag1(addr, tags);
    g_string_printf(str_buf, "m%.2x", addr_tag);

    gdb_put_packet(str_buf->str);
}

static void handle_q_isaddresstagged(GArray *params, void *user_ctx)
{
    ARMCPU *cpu = ARM_CPU(user_ctx);
    CPUARMState *env = &cpu->env;
    uint32_t mmu_index;

    uint64_t addr = gdb_get_cmd_param(params, 0)->val_ull;

    uint8_t *tags;
    const char *reply;

    mmu_index = cpu_mmu_index(env_cpu(env), false);
    tags = allocation_tag_mem_probe(env, mmu_index, addr, MMU_DATA_LOAD, 1,
                                    MMU_DATA_LOAD, true, 0);
    reply = tags ? "01" : "00";

    gdb_put_packet(reply);
}

static void handle_Q_memtag(GArray *params, void *user_ctx)
{
    ARMCPU *cpu = ARM_CPU(user_ctx);
    CPUARMState *env = &cpu->env;
    uint32_t mmu_index;

    uint64_t start_addr = gdb_get_cmd_param(params, 0)->val_ull;
    uint64_t len = gdb_get_cmd_param(params, 1)->val_ul;
    int type = gdb_get_cmd_param(params, 2)->val_ul;
    char const *new_tags_str = gdb_get_cmd_param(params, 3)->data;

    uint64_t end_addr;

    int num_new_tags;
    uint8_t *tags;

    g_autoptr(GByteArray) new_tags = g_byte_array_new();

    if (type != 1) {
        gdb_put_packet("E02");
        return;
    }

    end_addr = start_addr + (len - 1);
    if ((start_addr ^ end_addr) & TARGET_PAGE_MASK) {
        gdb_put_packet("E03");
        return;
    }

    mmu_index = cpu_mmu_index(env_cpu(env), false);
    tags = allocation_tag_mem_probe(env, mmu_index, start_addr, MMU_DATA_STORE,
                                    1, MMU_DATA_STORE, true, 0);
    if (!tags) {
        gdb_put_packet("E04");
        return;
    }

    num_new_tags = strlen(new_tags_str) / 2;
    gdb_hextomem(new_tags, new_tags_str, num_new_tags);

    uint64_t address = start_addr;
    int new_tag_index = 0;
    while (address <= end_addr) {
        uint8_t new_tag;
        int packed_index;

        packed_index = new_tag_index / 2;

        new_tag = new_tags->data[new_tag_index % num_new_tags];
        store_tag1(address, tags + packed_index, new_tag);

        address += TAG_GRANULE;
        new_tag_index++;
    }

    gdb_put_packet("OK");
}

enum Command {
    qMemTags,
    qIsAddressTagged,
    QMemTags,
    NUM_CMDS
};

static const GdbCmdParseEntry cmd_handler_table[NUM_CMDS] = {
    [qMemTags] = {
        .handler = handle_q_memtag,
        .cmd_startswith = true,
        .cmd = "MemTags:",
        .schema = "L,l:l0",
        .need_cpu_context = true
    },
    [qIsAddressTagged] = {
        .handler = handle_q_isaddresstagged,
        .cmd_startswith = true,
        .cmd = "IsAddressTagged:",
        .schema = "L0",
        .need_cpu_context = true
    },
    [QMemTags] = {
        .handler = handle_Q_memtag,
        .cmd_startswith = true,
        .cmd = "MemTags:",
        .schema = "L,l:l:s0",
        .need_cpu_context = true
    },
};
#endif

void aarch64_cpu_register_gdb_commands(ARMCPU *cpu, GString *qsupported,
                                       GPtrArray *qtable, GPtrArray *stable)
{
#ifdef CONFIG_TCG
    if (cpu_isar_feature(aa64_mte, cpu)) {
        g_string_append(qsupported, ";memory-tagging+");

        g_ptr_array_add(qtable, (gpointer) &cmd_handler_table[qMemTags]);
        g_ptr_array_add(qtable, (gpointer) &cmd_handler_table[qIsAddressTagged]);
        g_ptr_array_add(stable, (gpointer) &cmd_handler_table[QMemTags]);
    }
#endif
}
