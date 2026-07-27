/*
 * Control Flow plugin
 *
 * This plugin will track changes to control flow and detect where
 * instructions fault.
 *
 * Copyright (c) 2024 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <glib.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

typedef enum {
    SORT_HOTTEST,
    SORT_EXCEPTION,
    SORT_POPDEST,
} ReportType;

ReportType report = SORT_HOTTEST;
int topn = 10;

typedef struct {
    uint64_t daddr;
    uint64_t dcount;
} DestData;

typedef struct {
    GMutex lock;
    uint64_t addr;
    GArray *dests;
    uint64_t early_exit;
    uint64_t dest_count;
    char *insn_disas;
    const char *symbol;
    int last_count;
    int mid_count;
} NodeData;

typedef enum {
    LAST_INSN = (1 << 0),
    EXCP_INSN = (1 << 1),
    MULT_INSN = (1 << 2),
} InsnTypes;

typedef struct {
    uint64_t addr;
    char *insn_disas;
    const char *symbol;
    InsnTypes type_flag;
} InsnData;

typedef struct {
    uint64_t tb_pc;
    uint64_t end_block;
    uint64_t pc_after_block;
    uint64_t last_pc;
} VCPUScoreBoard;

static qemu_plugin_u64 tb_pc;
static qemu_plugin_u64 end_block;
static qemu_plugin_u64 pc_after_block;
static qemu_plugin_u64 last_pc;


static GMutex node_lock;
static GHashTable *nodes;
struct qemu_plugin_scoreboard *state;

static gint hottest(gconstpointer a, gconstpointer b, gpointer d)
{
    NodeData *na = (NodeData *) a;
    NodeData *nb = (NodeData *) b;

    return na->dest_count > nb->dest_count ? -1 :
        na->dest_count == nb->dest_count ? 0 : 1;
}

static gint exception(gconstpointer a, gconstpointer b, gpointer d)
{
    NodeData *na = (NodeData *) a;
    NodeData *nb = (NodeData *) b;

    return na->early_exit > nb->early_exit ? -1 :
        na->early_exit == nb->early_exit ? 0 : 1;
}

static gint popular(gconstpointer a, gconstpointer b, gpointer d)
{
    NodeData *na = (NodeData *) a;
    NodeData *nb = (NodeData *) b;

    return na->dests->len > nb->dests->len ? -1 :
        na->dests->len == nb->dests->len ? 0 : 1;
}

static gboolean filter_non_branches(gpointer key, gpointer value,
                                    gpointer user_data)
{
    NodeData *node = (NodeData *) value;

    return node->dest_count == 0;
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    g_autoptr(GString) result = g_string_new("collected ");
    GList *data;
    GCompareDataFunc sort = &hottest;
    int i = 0;

    g_mutex_lock(&node_lock);
    g_string_append_printf(result, "%d control flow nodes in the hash table\n",
                           g_hash_table_size(nodes));

    g_hash_table_foreach_remove(nodes, filter_non_branches, NULL);

    data = g_hash_table_get_values(nodes);

    switch (report) {
    case SORT_HOTTEST:
        sort = &hottest;
        break;
    case SORT_EXCEPTION:
        sort = &exception;
        break;
    case SORT_POPDEST:
        sort = &popular;
        break;
    }

    data = g_list_sort_with_data(data, sort, NULL);

    for (GList *l = data;
         l != NULL && i < topn;
         l = l->next, i++) {
        NodeData *n = l->data;
        const char *type = n->mid_count ? "sync fault" : "branch";
        g_string_append_printf(result, "  addr: 0x%"PRIx64 " %s: %s (%s)\n",
                               n->addr, n->symbol, n->insn_disas, type);
        if (n->early_exit) {
            g_string_append_printf(result, "    early exits %"PRId64"\n",
                                   n->early_exit);
        }
        g_string_append_printf(result, "    branches %"PRId64"\n",
                               n->dest_count);
        for (int j = 0; j < n->dests->len; j++) {
            DestData *dd = &g_array_index(n->dests, DestData, j);
            g_string_append_printf(result, "      to 0x%"PRIx64" (%"PRId64")\n",
                                   dd->daddr, dd->dcount);
        }
    }

    qemu_plugin_outs(result->str);

    g_mutex_unlock(&node_lock);
}

static void plugin_init(void)
{
    g_mutex_init(&node_lock);
    nodes = g_hash_table_new(g_int64_hash, g_int64_equal);
    state = qemu_plugin_scoreboard_new(sizeof(VCPUScoreBoard));

    tb_pc = qemu_plugin_scoreboard_u64_in_struct(state, VCPUScoreBoard, tb_pc);
    end_block = qemu_plugin_scoreboard_u64_in_struct(state, VCPUScoreBoard,
                                                     end_block);
    pc_after_block = qemu_plugin_scoreboard_u64_in_struct(state, VCPUScoreBoard,
                                                          pc_after_block);
    last_pc = qemu_plugin_scoreboard_u64_in_struct(state, VCPUScoreBoard,
                                                   last_pc);
}

static NodeData *create_node(uint64_t addr)
{
    NodeData *node = g_new0(NodeData, 1);
    g_mutex_init(&node->lock);
    node->addr = addr;
    node->dests = g_array_new(true, true, sizeof(DestData));
    return node;
}

static NodeData *fetch_node(uint64_t addr, bool create_if_not_found)
{
    NodeData *node = NULL;

    g_mutex_lock(&node_lock);
    node = (NodeData *) g_hash_table_lookup(nodes, &addr);
    if (!node && create_if_not_found) {
        node = create_node(addr);
        g_hash_table_insert(nodes, &node->addr, node);
    }
    g_mutex_unlock(&node_lock);
    return node;
}

static void vcpu_tb_branched_exec(unsigned int cpu_index, void *udata)
{
    uint64_t lpc = qemu_plugin_u64_get(last_pc, cpu_index);
    uint64_t ebpc = qemu_plugin_u64_get(end_block, cpu_index);
    uint64_t npc = qemu_plugin_u64_get(pc_after_block, cpu_index);
    uint64_t pc = qemu_plugin_u64_get(tb_pc, cpu_index);

    if (!lpc) {
        return;
    }

    NodeData *node = fetch_node(lpc, true);
    DestData *data = NULL;
    bool early_exit = (lpc != ebpc);
    GArray *dests;

    g_assert(pc != npc);

    g_mutex_lock(&node->lock);

    if (early_exit) {
        fprintf(stderr, "%s: pc=%"PRIx64", epbc=%"PRIx64
                " npc=%"PRIx64", lpc=%"PRIx64"\n",
                __func__, pc, ebpc, npc, lpc);
        node->early_exit++;
        if (!node->mid_count) {
            node->mid_count++;
        }
    }

    dests = node->dests;
    for (int i = 0; i < dests->len; i++) {
        if (g_array_index(dests, DestData, i).daddr == pc) {
            data = &g_array_index(dests, DestData, i);
        }
    }

    if (!data) {
        DestData new_entry = { .daddr = pc };
        g_array_append_val(dests, new_entry);
        data = &g_array_index(dests, DestData, dests->len - 1);
        g_assert(data->daddr == pc);
    }

    data->dcount++;
    node->dest_count++;

    g_mutex_unlock(&node->lock);
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    uint64_t pc = qemu_plugin_tb_vaddr(tb);
    size_t insns = qemu_plugin_tb_n_insns(tb);
    struct qemu_plugin_insn *first_insn = qemu_plugin_tb_get_insn(tb, 0);
    struct qemu_plugin_insn *last_insn = qemu_plugin_tb_get_insn(tb, insns - 1);

    qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(
        tb, QEMU_PLUGIN_INLINE_STORE_U64, tb_pc, pc);
    qemu_plugin_register_vcpu_tb_exec_cond_cb(
        tb, vcpu_tb_branched_exec, QEMU_PLUGIN_CB_NO_REGS,
        QEMU_PLUGIN_COND_NE, pc_after_block, pc, NULL);

    qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(first_insn,
                                                      QEMU_PLUGIN_INLINE_STORE_U64,
                                                      end_block, qemu_plugin_insn_vaddr(last_insn));
    qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(first_insn,
                                                      QEMU_PLUGIN_INLINE_STORE_U64,
                                                      pc_after_block,
                                                      qemu_plugin_insn_vaddr(last_insn) +
                                                      qemu_plugin_insn_size(last_insn));

    for (int idx = 0; idx < qemu_plugin_tb_n_insns(tb); ++idx) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, idx);
        uint64_t ipc = qemu_plugin_insn_vaddr(insn);
        NodeData *node = fetch_node(ipc, last_insn);
        if (node) {
            g_mutex_lock(&node->lock);
            if (!node->insn_disas) {
                node->insn_disas = qemu_plugin_insn_disas(insn);
            }
            if (!node->symbol) {
                node->symbol = qemu_plugin_insn_symbol(insn);
            }
            if (last_insn == insn) {
                node->last_count++;
            } else {
                node->mid_count++;
            }
            g_mutex_unlock(&node->lock);
        }

        qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(insn,
                                                            QEMU_PLUGIN_INLINE_STORE_U64,
                                                            last_pc, ipc);
    }
}

QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                        int argc, char **argv)
{
    for (int i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_auto(GStrv) tokens = g_strsplit(opt, "=", 2);
        if (g_strcmp0(tokens[0], "sort") == 0) {
            if (g_strcmp0(tokens[1], "hottest") == 0) {
                report = SORT_HOTTEST;
            } else if (g_strcmp0(tokens[1], "early") == 0) {
                report = SORT_EXCEPTION;
            } else if (g_strcmp0(tokens[1], "exceptions") == 0) {
                report = SORT_POPDEST;
            } else {
                fprintf(stderr, "failed to parse: %s\n", tokens[1]);
                return -1;
            }
        } else {
            fprintf(stderr, "option parsing failed: %s\n", opt);
            return -1;
        }
    }

    plugin_init();

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
