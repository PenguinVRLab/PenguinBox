/*
 * QEMU CPU cluster
 *
 * Copyright (c) 2018 GreenSocs SAS
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 */
#ifndef HW_CPU_CLUSTER_H
#define HW_CPU_CLUSTER_H

#include "hw/qdev-core.h"
#include "qom/object.h"

#define TYPE_CPU_CLUSTER "cpu-cluster"
OBJECT_DECLARE_SIMPLE_TYPE(CPUClusterState, CPU_CLUSTER)

#define MAX_CLUSTERS 255

struct CPUClusterState {
    DeviceState parent_obj;

    uint32_t cluster_id;
};

#endif
