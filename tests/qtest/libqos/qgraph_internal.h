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

#ifndef QGRAPH_INTERNAL_H
#define QGRAPH_INTERNAL_H

#include "qgraph.h"

typedef struct QOSGraphMachine QOSGraphMachine;
typedef enum QOSEdgeType QOSEdgeType;
typedef enum QOSNodeType QOSNodeType;

typedef void (*QOSTestCallback) (QOSGraphNode *path, int len);

enum QOSEdgeType {
    QEDGE_CONTAINS,
    QEDGE_PRODUCES,
    QEDGE_CONSUMED_BY
};

enum QOSNodeType {
    QNODE_MACHINE,
    QNODE_DRIVER,
    QNODE_INTERFACE,
    QNODE_TEST
};

struct QOSGraphNode {
    QOSNodeType type;
    bool available;
    bool visited;
    char *name;
    char *qemu_name;
    char *command_line;
    union {
        struct {
            QOSCreateDriverFunc constructor;
        } driver;
        struct {
            QOSCreateMachineFunc constructor;
        } machine;
        struct {
            QOSTestFunc function;
            void *arg;
            QOSBeforeTest before;
            bool subprocess;
        } test;
    } u;

    QOSGraphEdge *path_edge;
};

QOSGraphNode *qos_graph_get_node(const char *key);

bool qos_graph_has_node(const char *node);

QOSNodeType qos_graph_get_node_type(const char *node);

bool qos_graph_get_node_availability(const char *node);

QOSGraphEdge *qos_graph_get_edge(const char *node, const char *dest);

QOSEdgeType qos_graph_edge_get_type(QOSGraphEdge *edge);

char *qos_graph_edge_get_dest(QOSGraphEdge *edge);

bool qos_graph_has_edge(const char *start, const char *dest);

void *qos_graph_edge_get_arg(QOSGraphEdge *edge);

char *qos_graph_edge_get_after_cmd_line(QOSGraphEdge *edge);

char *qos_graph_edge_get_before_cmd_line(QOSGraphEdge *edge);

char *qos_graph_edge_get_extra_device_opts(QOSGraphEdge *edge);

char *qos_graph_edge_get_name(QOSGraphEdge *edge);

QOSGraphNode *qos_graph_get_machine(const char *node);

bool qos_graph_has_machine(const char *node);


void qos_print_graph(void);

void qos_graph_foreach_test_path(QOSTestCallback fn);

char *qos_get_machine_type(char *name);

void qos_delete_cmd_line(const char *name);

void qos_graph_node_set_availability(const char *node, bool av);

#define qos_printf(...) printf("# " __VA_ARGS__)

#define qos_printf_literal printf

#endif
