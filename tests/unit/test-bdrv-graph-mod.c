/*
 * Block node graph modifications tests
 *
 * Copyright (c) 2019-2021 Virtuozzo International GmbH. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "block/block_int.h"
#include "system/block-backend.h"

static BlockDriver bdrv_pass_through = {
    .format_name = "pass-through",
    .is_filter = true,
    .filtered_child_is_backing = true,
    .bdrv_child_perm = bdrv_default_perms,
};

static void no_perm_default_perms(BlockDriverState *bs, BdrvChild *c,
                                         BdrvChildRole role,
                                         BlockReopenQueue *reopen_queue,
                                         uint64_t perm, uint64_t shared,
                                         uint64_t *nperm, uint64_t *nshared)
{
    *nperm = 0;
    *nshared = BLK_PERM_ALL;
}

static BlockDriver bdrv_no_perm = {
    .format_name = "no-perm",
    .supports_backing = true,
    .bdrv_child_perm = no_perm_default_perms,
};

static void exclusive_write_perms(BlockDriverState *bs, BdrvChild *c,
                                  BdrvChildRole role,
                                  BlockReopenQueue *reopen_queue,
                                  uint64_t perm, uint64_t shared,
                                  uint64_t *nperm, uint64_t *nshared)
{
    *nperm = BLK_PERM_WRITE;
    *nshared = BLK_PERM_ALL & ~BLK_PERM_WRITE;
}

static BlockDriver bdrv_exclusive_writer = {
    .format_name = "exclusive-writer",
    .is_filter = true,
    .filtered_child_is_backing = true,
    .bdrv_child_perm = exclusive_write_perms,
};

static BlockDriverState *no_perm_node(const char *name)
{
    return bdrv_new_open_driver(&bdrv_no_perm, name, BDRV_O_RDWR, &error_abort);
}

static BlockDriverState *pass_through_node(const char *name)
{
    return bdrv_new_open_driver(&bdrv_pass_through, name,
                                BDRV_O_RDWR, &error_abort);
}

static BlockDriverState *exclusive_writer_node(const char *name)
{
    return bdrv_new_open_driver(&bdrv_exclusive_writer, name,
                                BDRV_O_RDWR, &error_abort);
}

static void test_update_perm_tree(void)
{
    int ret;

    BlockBackend *root = blk_new(qemu_get_aio_context(),
                                 BLK_PERM_WRITE | BLK_PERM_CONSISTENT_READ,
                                 BLK_PERM_ALL & ~BLK_PERM_WRITE);
    BlockDriverState *bs = no_perm_node("node");
    BlockDriverState *filter = pass_through_node("filter");

    blk_insert_bs(root, bs, &error_abort);

    bdrv_graph_wrlock_drained();
    bdrv_attach_child(filter, bs, "child", &child_of_bds,
                      BDRV_CHILD_DATA, &error_abort);
    bdrv_graph_wrunlock();

    ret = bdrv_append(filter, bs, NULL);
    g_assert_cmpint(ret, <, 0);

    bdrv_unref(filter);
    blk_unref(root);
}

static void test_should_update_child(void)
{
    BlockBackend *root = blk_new(qemu_get_aio_context(), 0, BLK_PERM_ALL);
    BlockDriverState *bs = no_perm_node("node");
    BlockDriverState *filter = no_perm_node("filter");
    BlockDriverState *target = no_perm_node("target");

    blk_insert_bs(root, bs, &error_abort);

    bdrv_graph_wrlock_drained();
    bdrv_set_backing_hd(target, bs, &error_abort);

    g_assert(target->backing->bs == bs);
    bdrv_attach_child(filter, target, "target", &child_of_bds,
                      BDRV_CHILD_DATA, &error_abort);
    bdrv_graph_wrunlock();
    bdrv_append(filter, bs, &error_abort);

    bdrv_graph_rdlock_main_loop();
    g_assert(target->backing->bs == bs);
    bdrv_graph_rdunlock_main_loop();

    bdrv_unref(filter);
    bdrv_unref(bs);
    blk_unref(root);
}

static void test_parallel_exclusive_write(void)
{
    BlockDriverState *top = exclusive_writer_node("top");
    BlockDriverState *base = no_perm_node("base");
    BlockDriverState *fl1 = pass_through_node("fl1");
    BlockDriverState *fl2 = pass_through_node("fl2");

    bdrv_drained_begin(fl1);
    bdrv_drained_begin(fl2);

    bdrv_ref(base);
    bdrv_ref(fl1);

    bdrv_graph_wrlock_drained();
    bdrv_attach_child(top, fl1, "backing", &child_of_bds,
                      BDRV_CHILD_FILTERED | BDRV_CHILD_PRIMARY,
                      &error_abort);
    bdrv_attach_child(fl1, base, "backing", &child_of_bds,
                      BDRV_CHILD_FILTERED | BDRV_CHILD_PRIMARY,
                      &error_abort);
    bdrv_attach_child(fl2, base, "backing", &child_of_bds,
                      BDRV_CHILD_FILTERED | BDRV_CHILD_PRIMARY,
                      &error_abort);

    bdrv_replace_node(fl1, fl2, &error_abort);
    bdrv_graph_wrunlock();

    bdrv_drained_end(fl2);
    bdrv_drained_end(fl1);

    bdrv_unref(fl1);
    bdrv_unref(fl2);
    bdrv_unref(top);
}

typedef struct BDRVWriteToSelectedState {
    BdrvChild *selected;
} BDRVWriteToSelectedState;

static void write_to_selected_perms(BlockDriverState *bs, BdrvChild *c,
                                    BdrvChildRole role,
                                    BlockReopenQueue *reopen_queue,
                                    uint64_t perm, uint64_t shared,
                                    uint64_t *nperm, uint64_t *nshared)
{
    BDRVWriteToSelectedState *s = bs->opaque;

    if (s->selected && c == s->selected) {
        *nperm = BLK_PERM_WRITE;
        *nshared = BLK_PERM_ALL & ~BLK_PERM_WRITE;
    } else {
        *nperm = 0;
        *nshared = BLK_PERM_ALL;
    }
}

static BlockDriver bdrv_write_to_selected = {
    .format_name = "write-to-selected",
    .instance_size = sizeof(BDRVWriteToSelectedState),
    .bdrv_child_perm = write_to_selected_perms,
};


static void test_parallel_perm_update(void)
{
    BlockDriverState *top = no_perm_node("top");
    BlockDriverState *ws =
            bdrv_new_open_driver(&bdrv_write_to_selected, "ws", BDRV_O_RDWR,
                                 &error_abort);
    BDRVWriteToSelectedState *s = ws->opaque;
    BlockDriverState *base = no_perm_node("base");
    BlockDriverState *fl1 = pass_through_node("fl1");
    BlockDriverState *fl2 = pass_through_node("fl2");
    BdrvChild *c_fl1, *c_fl2;

    bdrv_ref(base);

    bdrv_graph_wrlock_drained();
    bdrv_attach_child(top, ws, "file", &child_of_bds, BDRV_CHILD_DATA,
                      &error_abort);
    c_fl1 = bdrv_attach_child(ws, fl1, "first", &child_of_bds,
                              BDRV_CHILD_DATA, &error_abort);
    c_fl2 = bdrv_attach_child(ws, fl2, "second", &child_of_bds,
                              BDRV_CHILD_DATA, &error_abort);
    bdrv_attach_child(fl1, base, "backing", &child_of_bds,
                      BDRV_CHILD_FILTERED | BDRV_CHILD_PRIMARY,
                      &error_abort);
    bdrv_attach_child(fl2, base, "backing", &child_of_bds,
                      BDRV_CHILD_FILTERED | BDRV_CHILD_PRIMARY,
                      &error_abort);
    bdrv_graph_wrunlock();

    s->selected = c_fl1;

    bdrv_graph_rdlock_main_loop();

    bdrv_child_refresh_perms(top, top->children.lh_first, &error_abort);

    assert(c_fl1->perm & BLK_PERM_WRITE);
    assert(!(c_fl2->perm & BLK_PERM_WRITE));

    s->selected = c_fl2;
    bdrv_child_refresh_perms(top, top->children.lh_first, &error_abort);

    assert(c_fl2->perm & BLK_PERM_WRITE);
    assert(!(c_fl1->perm & BLK_PERM_WRITE));

    s->selected = c_fl1;
    bdrv_child_refresh_perms(top, top->children.lh_first, &error_abort);

    assert(c_fl1->perm & BLK_PERM_WRITE);
    assert(!(c_fl2->perm & BLK_PERM_WRITE));

    bdrv_graph_rdunlock_main_loop();
    bdrv_unref(top);
}

static void test_append_greedy_filter(void)
{
    BlockDriverState *top = exclusive_writer_node("top");
    BlockDriverState *base = no_perm_node("base");
    BlockDriverState *fl = exclusive_writer_node("fl1");

    bdrv_graph_wrlock_drained();
    bdrv_attach_child(top, base, "backing", &child_of_bds,
                      BDRV_CHILD_FILTERED | BDRV_CHILD_PRIMARY,
                      &error_abort);
    bdrv_graph_wrunlock();

    bdrv_append(fl, base, &error_abort);
    bdrv_unref(fl);
    bdrv_unref(top);
}

int main(int argc, char *argv[])
{
    bdrv_init();
    qemu_init_main_loop(&error_abort);

    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/bdrv-graph-mod/update-perm-tree", test_update_perm_tree);
    g_test_add_func("/bdrv-graph-mod/should-update-child",
                    test_should_update_child);
    g_test_add_func("/bdrv-graph-mod/parallel-perm-update",
                    test_parallel_perm_update);
    g_test_add_func("/bdrv-graph-mod/parallel-exclusive-write",
                    test_parallel_exclusive_write);
    g_test_add_func("/bdrv-graph-mod/append-greedy-filter",
                    test_append_greedy_filter);

    return g_test_run();
}
