/*
 *  qdev vm change state handlers
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License,
 *  or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/qdev-core.h"
#include "system/runstate.h"

static int qdev_get_dev_tree_depth(DeviceState *dev)
{
    int depth;

    for (depth = 0; dev; depth++) {
        BusState *bus = dev->parent_bus;

        if (!bus) {
            break;
        }

        dev = bus->parent;
    }

    return depth;
}

VMChangeStateEntry *qdev_add_vm_change_state_handler(DeviceState *dev,
                                                     VMChangeStateHandler *cb,
                                                     VMChangeStateHandlerWithRet *cb_ret,
                                                     void *opaque)
{
    assert(!cb || !cb_ret);
    return qdev_add_vm_change_state_handler_full(dev, cb, NULL, cb_ret, opaque);
}

VMChangeStateEntry *qdev_add_vm_change_state_handler_full(
    DeviceState *dev, VMChangeStateHandler *cb, VMChangeStateHandler *prepare_cb,
    VMChangeStateHandlerWithRet *cb_ret, void *opaque)
{
    int depth = qdev_get_dev_tree_depth(dev);

    assert(!cb || !cb_ret);
    return qemu_add_vm_change_state_handler_prio_full(cb, prepare_cb, cb_ret,
                                                      opaque, depth);
}
