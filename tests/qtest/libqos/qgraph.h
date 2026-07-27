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

#ifndef QGRAPH_H
#define QGRAPH_H

#include <gmodule.h>
#include "qemu/module.h"
#include "libqos-malloc.h"

#define QOS_PATH_MAX_ELEMENT_SIZE 128

typedef struct QOSGraphObject QOSGraphObject;
typedef struct QOSGraphNode QOSGraphNode;
typedef struct QOSGraphEdge QOSGraphEdge;
typedef struct QOSGraphEdgeOptions QOSGraphEdgeOptions;
typedef struct QOSGraphTestOptions QOSGraphTestOptions;

typedef void *(*QOSCreateDriverFunc) (void *parent, QGuestAllocator *alloc,
                                      void *addr);
typedef void *(*QOSCreateMachineFunc) (QTestState *qts);
typedef void (*QOSTestFunc) (void *parent, void *arg, QGuestAllocator *alloc);

typedef void *(*QOSGetDriver) (void *object, const char *interface);
typedef QOSGraphObject *(*QOSGetDevice) (void *object, const char *name);
typedef void (*QOSDestructorFunc) (QOSGraphObject *object);
typedef void (*QOSStartFunct) (QOSGraphObject *object);

typedef void *(*QOSBeforeTest) (GString *cmd_line, void *arg);

struct QOSGraphEdgeOptions {
    void *arg;
    uint32_t size_arg;
    const char *extra_device_opts;
    const char *before_cmd_line;
    const char *after_cmd_line;
    const char *edge_name;
};

struct QOSGraphTestOptions {
    QOSGraphEdgeOptions edge;
    void *arg;
    QOSBeforeTest before;
    bool subprocess;
};

/**
 * struct QOSGraphObject:
 * Each driver, test or machine of this framework will have a
 * QOSGraphObject as first field.
 *
 * This set of functions offered by QOSGraphObject are executed
 * in different stages of the framework:
 * @get_driver: see @get_device
 * @get_device: Once a machine-to-test path has been
 *              found, the framework traverses it again and allocates all the
 *              nodes, using the provided constructor. To satisfy their
 *              relations, i.e. for produces or contains, where a struct
 *              constructor needs an external parameter represented by the
 *              previous node, the framework will call
 *              @get_device (for contains) or @get_driver (for produces),
 *              depending on the edge type, passing them the name of the next
 *              node to be taken and getting from them the corresponding
 *              pointer to the actual structure of the next node to
 *              be used in the path.
 * @start_hw: This function is executed after all the path objects
 *            have been allocated, but before the test is run. It starts the
 *            hw, setting the initial configurations (\*_device_enable) and
 *            making it ready for the test.
 * @destructor: Opposite to the node constructor, destroys the object.
 *              This function is called after the test has been executed, and
 *              performs a complete cleanup of each node allocated field.
 *              In case no constructor is provided, no destructor will be
 *              called.
 * @free: free the memory associated to the QOSGraphObject and its contained
 *        children
 */
struct QOSGraphObject {
    QOSGetDriver get_driver;
    QOSGetDevice get_device;
    QOSStartFunct start_hw;
    QOSDestructorFunc destructor;
    GDestroyNotify free;
};

void qos_graph_init(void);

void qos_graph_destroy(void);

void qos_node_destroy(void *key);

void qos_edge_destroy(void *key);

void qos_add_test(const char *name, const char *interface,
                  QOSTestFunc test_func,
                  QOSGraphTestOptions *opts);

void qos_node_create_machine(const char *name, QOSCreateMachineFunc function);

void qos_node_create_machine_args(const char *name,
                                  QOSCreateMachineFunc function,
                                  const char *opts);

void qos_node_create_driver(const char *name, QOSCreateDriverFunc function);

void qos_node_create_driver_named(const char *name, const char *qemu_name,
                                  QOSCreateDriverFunc function);

void qos_node_contains(const char *container, const char *contained,
                       QOSGraphEdgeOptions *opts, ...);

void qos_node_produces(const char *producer, const char *interface);

void qos_node_consumes(const char *consumer, const char *interface,
                       QOSGraphEdgeOptions *opts);

void qos_invalidate_command_line(void);

const char *qos_get_current_command_line(void);

void *qos_allocate_objects(QTestState *qts, QGuestAllocator **p_alloc);

void qos_object_destroy(QOSGraphObject *obj);

void qos_object_queue_destroy(QOSGraphObject *obj);

void qos_object_start_hw(QOSGraphObject *obj);

QOSGraphObject *qos_machine_new(QOSGraphNode *node, QTestState *qts);

QOSGraphObject *qos_driver_new(QOSGraphNode *node, QOSGraphObject *parent,
                               QGuestAllocator *alloc, void *arg);

void qos_dump_graph(void);

#endif
