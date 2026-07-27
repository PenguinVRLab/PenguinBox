/*
 * libqos driver framework
 *
 * Copyright (c) 2018 Emanuele Giuseppe Esposito <e.emanuelegiuseppe@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"
#include <getopt.h>
#include "libqtest-single.h"
#include "qapi/error.h"
#include "qobject/qdict.h"
#include "qemu/module.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qapi-visit-machine.h"
#include "qapi/qapi-visit-qom.h"
#include "libqos/libqos-malloc.h"
#include "libqos/qgraph.h"
#include "libqos/qgraph_internal.h"
#include "libqos/qos_external.h"

static char *old_path;


static void qos_set_machines_devices_available(void)
{
    QDict *response;
    QDict *args = qdict_new();
    QObject *ret;
    Visitor *v;
    MachineInfoList *mach_info;
    ObjectTypeInfoList *type_info;

    qtest_start("-machine none");
    response = qmp("{ 'execute': 'query-machines' }");
    ret = qdict_get(response, "return");

    v = qobject_input_visitor_new(ret);
    visit_type_MachineInfoList(v, NULL, &mach_info, &error_abort);
    visit_free(v);
    machines_apply_to_node(mach_info);
    qapi_free_MachineInfoList(mach_info);

    qobject_unref(response);

    qdict_put_bool(args, "abstract", true);
    qdict_put_str(args, "implements", "device");

    response = qmp("{'execute': 'qom-list-types',"
                   " 'arguments': %p }", args);
    ret = qdict_get(response, "return");

    v = qobject_input_visitor_new(ret);
    visit_type_ObjectTypeInfoList(v, NULL, &type_info, &error_abort);
    visit_free(v);
    types_apply_to_node(type_info);
    qapi_free_ObjectTypeInfoList(type_info);

    qtest_end();
    qobject_unref(response);
}


static void restart_qemu_or_continue(char *path)
{
    if (g_test_verbose()) {
        qos_printf("Run QEMU with: '%s'\n", path);
    }
    if (g_strcmp0(old_path, path)) {
        qtest_end();
        qos_invalidate_command_line();
        old_path = g_strdup(path);
        qtest_start(path);
    } else {
        qtest_system_reset(global_qtest);
    }
}

void qos_invalidate_command_line(void)
{
    g_free(old_path);
    old_path = NULL;
}


static char **current_path;

const char *qos_get_current_command_line(void)
{
    return current_path[0];
}

void *qos_allocate_objects(QTestState *qts, QGuestAllocator **p_alloc)
{
    return allocate_objects(qts, current_path + 1, p_alloc);
}

static void run_one_test(const void *arg)
{
    QOSGraphNode *test_node;
    QGuestAllocator *alloc = NULL;
    void *obj;
    char **path = (char **) arg;
    GString *cmd_line = g_string_new(path[0]);
    void *test_arg;

    current_path = path;
    test_node = qos_graph_get_node(path[(g_strv_length(path) - 1)]);
    test_arg = test_node->u.test.arg;
    if (test_node->u.test.before) {
        test_arg = test_node->u.test.before(cmd_line, test_arg);
    }

    restart_qemu_or_continue(cmd_line->str);
    g_string_free(cmd_line, true);

    obj = qos_allocate_objects(global_qtest, &alloc);
    test_node->u.test.function(obj, test_arg, alloc);
}

static void subprocess_run_one_test(const void *arg)
{
    const gchar *path = arg;
    g_test_trap_subprocess(path, 180 * G_USEC_PER_SEC,
                           G_TEST_SUBPROCESS_INHERIT_STDOUT |
                           G_TEST_SUBPROCESS_INHERIT_STDERR);
    g_test_trap_assert_passed();
}

static void destroy_pathv(void *arg)
{
    g_free(((char **)arg)[0]);
    g_free(arg);
}

static void walk_path(QOSGraphNode *orig_path, int len)
{
    QOSGraphNode *path;
    QOSGraphEdge *edge;

    QOSEdgeType etype = QEDGE_CONSUMED_BY;

    char **path_vec = g_new0(char *, (QOS_PATH_MAX_ELEMENT_SIZE * 2));
    int path_vec_size = 0;

    char *after_cmd, *before_cmd, *after_device;
    GString *after_device_str = g_string_new("");
    char *node_name = orig_path->name, *path_str;

    GString *cmd_line = g_string_new("");
    GString *cmd_line2 = g_string_new("");

    path = qos_graph_get_node(node_name);
    node_name = qos_graph_edge_get_dest(path->path_edge);

    path_vec[path_vec_size++] = node_name;
    path_vec[path_vec_size++] = qos_get_machine_type(node_name);

    for (;;) {
        path = qos_graph_get_node(node_name);
        if (!path->path_edge) {
            break;
        }

        node_name = qos_graph_edge_get_dest(path->path_edge);

        if (path->command_line && etype == QEDGE_CONSUMED_BY) {
            g_string_append(cmd_line, path->command_line);
            g_string_append(cmd_line, after_device_str->str);
            g_string_truncate(after_device_str, 0);
        }

        path_vec[path_vec_size++] = qos_graph_edge_get_name(path->path_edge);
        after_cmd = qos_graph_edge_get_after_cmd_line(path->path_edge);
        after_device = qos_graph_edge_get_extra_device_opts(path->path_edge);
        before_cmd = qos_graph_edge_get_before_cmd_line(path->path_edge);
        edge = qos_graph_get_edge(path->name, node_name);
        etype = qos_graph_edge_get_type(edge);

        if (before_cmd) {
            g_string_append(cmd_line, before_cmd);
        }
        if (after_cmd) {
            g_string_append(cmd_line2, after_cmd);
        }
        if (after_device) {
            g_string_append(after_device_str, after_device);
        }
    }

    path_vec[path_vec_size++] = NULL;
    g_string_append(cmd_line, after_device_str->str);
    g_string_free(after_device_str, true);

    g_string_append(cmd_line, cmd_line2->str);
    g_string_free(cmd_line2, true);

    path_str = g_strjoinv("/", path_vec + 1);

    path_vec[1] = path_vec[0];
    path_vec[0] = g_string_free(cmd_line, false);

    if (path->u.test.subprocess) {
        gchar *subprocess_path = g_strdup_printf("/%s/%s/subprocess",
                                                 qtest_get_arch(), path_str);
        qtest_add_data_func_full(path_str, subprocess_path,
                                 subprocess_run_one_test, g_free);
        g_test_add_data_func_full(subprocess_path, path_vec,
                                  run_one_test, destroy_pathv);
    } else {
        qtest_add_data_func_full(path_str, path_vec,
                                 run_one_test, destroy_pathv);
    }

    g_free(path_str);
}



int main(int argc, char **argv, char** envp)
{
    g_test_init(&argc, &argv, NULL);
    if (g_test_verbose()) {
        qos_printf("ENVIRONMENT VARIABLES: {\n");
        for (char **env = envp; *env != 0; env++) {
            qos_printf("\t%s\n", *env);
        }
        qos_printf("}\n");
    }
    qos_graph_init();
    module_call_init(MODULE_INIT_QOM);
    module_call_init(MODULE_INIT_LIBQOS);
    qos_set_machines_devices_available();

    qos_graph_foreach_test_path(walk_path);
    if (g_test_verbose()) {
        qos_dump_graph();
    }
    g_test_run();
    qtest_end();
    qos_graph_destroy();
    g_free(old_path);
    return 0;
}
