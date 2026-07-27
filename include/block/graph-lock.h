/*
 * Graph lock: rwlock to protect block layer graph manipulations (add/remove
 * edges and nodes)
 *
 *  Copyright (c) 2022 Red Hat
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
#ifndef GRAPH_LOCK_H
#define GRAPH_LOCK_H

typedef struct BdrvGraphRWlock BdrvGraphRWlock;

typedef struct TSA_CAPABILITY("mutex") BdrvGraphLock {
} BdrvGraphLock;

extern BdrvGraphLock graph_lock;

#define GRAPH_WRLOCK TSA_REQUIRES(graph_lock)
#define GRAPH_RDLOCK TSA_REQUIRES_SHARED(graph_lock)
#define GRAPH_UNLOCKED TSA_EXCLUDES(graph_lock)

#define GRAPH_RDLOCK_PTR TSA_GUARDED_BY(graph_lock)
#define GRAPH_WRLOCK_PTR TSA_GUARDED_BY(graph_lock)
#define GRAPH_UNLOCKED_PTR

void register_aiocontext(AioContext *ctx);

void unregister_aiocontext(AioContext *ctx);

/*
 * bdrv_graph_wrlock:
 * Start an exclusive write operation to modify the graph. This means we are
 * adding or removing an edge or a node in the block layer graph. Nobody else
 * is allowed to access the graph.
 *
 * Must only be called from outside bdrv_graph_co_rdlock.
 *
 * The wrlock can only be taken from the main loop, with BQL held, as only the
 * main loop is allowed to modify the graph.
 */
void no_coroutine_fn TSA_ACQUIRE(graph_lock) TSA_NO_TSA
bdrv_graph_wrlock(void);

void no_coroutine_fn TSA_ACQUIRE(graph_lock) TSA_NO_TSA
bdrv_graph_wrlock_drained(void);

void no_coroutine_fn TSA_RELEASE(graph_lock) TSA_NO_TSA
bdrv_graph_wrunlock(void);

/*
 * bdrv_graph_co_rdlock:
 * Read the bs graph. This usually means traversing all nodes in
 * the graph, therefore it can't happen while another thread is
 * modifying it.
 * Increases the reader counter of the current aiocontext,
 * and if has_writer is set, it means that the writer is modifying
 * the graph, therefore wait in a coroutine queue.
 * The writer will then wake this coroutine once it is done.
 *
 * This lock should be taken from Iothreads (IO_CODE() class of functions)
 * because it signals the writer that there are some
 * readers currently running, or waits until the current
 * write is finished before continuing.
 * Calling this function from the Main Loop with BQL held
 * is not necessary, since the Main Loop itself is the only
 * writer, thus won't be able to read and write at the same time.
 * The only exception to that is when we can't take the lock in the
 * function/coroutine itself, and need to delegate the caller (usually main
 * loop) to take it and wait that the coroutine ends, so that
 * we always signal that a reader is running.
 */
void coroutine_fn TSA_ACQUIRE_SHARED(graph_lock) TSA_NO_TSA
bdrv_graph_co_rdlock(void);

void coroutine_fn TSA_RELEASE_SHARED(graph_lock) TSA_NO_TSA
bdrv_graph_co_rdunlock(void);

void TSA_ACQUIRE_SHARED(graph_lock) TSA_NO_TSA
bdrv_graph_rdlock_main_loop(void);

void TSA_RELEASE_SHARED(graph_lock) TSA_NO_TSA
bdrv_graph_rdunlock_main_loop(void);

void GRAPH_RDLOCK assert_bdrv_graph_readable(void);

void GRAPH_WRLOCK assert_bdrv_graph_writable(void);

static inline void TSA_ASSERT_SHARED(graph_lock) TSA_NO_TSA
assume_graph_lock(void)
{
}

typedef struct GraphLockable { } GraphLockable;

#define GML_OBJ_() (&(GraphLockable) { })

static inline GraphLockable * TSA_ACQUIRE_SHARED(graph_lock) coroutine_fn
graph_lockable_auto_lock(GraphLockable *x)
{
    bdrv_graph_co_rdlock();
    return x;
}

static inline void TSA_RELEASE_SHARED(graph_lock) coroutine_fn
graph_lockable_auto_unlock(GraphLockable **x)
{
    bdrv_graph_co_rdunlock();
}

#define GRAPH_AUTO_UNLOCK __attribute__((cleanup(graph_lockable_auto_unlock)))

#define WITH_GRAPH_RDLOCK_GUARD_(var)                                         \
    for (GraphLockable *unlock_var GRAPH_AUTO_UNLOCK =                        \
            graph_lockable_auto_lock(GML_OBJ_()),                             \
            *var = unlock_var;                                                \
         var;                                                                 \
         var = NULL)

#define WITH_GRAPH_RDLOCK_GUARD() \
    WITH_GRAPH_RDLOCK_GUARD_(glue(graph_lockable_auto, __COUNTER__))

#define GRAPH_RDLOCK_GUARD(x)                                       \
    GraphLockable * GRAPH_AUTO_UNLOCK                               \
    glue(graph_lockable_auto, __COUNTER__) G_GNUC_UNUSED =          \
            graph_lockable_auto_lock(GML_OBJ_())


typedef struct GraphLockableMainloop { } GraphLockableMainloop;

#define GMLML_OBJ_() (&(GraphLockableMainloop) { })

static inline GraphLockableMainloop * TSA_ASSERT_SHARED(graph_lock) TSA_NO_TSA
graph_lockable_auto_lock_mainloop(GraphLockableMainloop *x)
{
    bdrv_graph_rdlock_main_loop();
    return x;
}

static inline void TSA_NO_TSA
graph_lockable_auto_unlock_mainloop(GraphLockableMainloop *x)
{
    bdrv_graph_rdunlock_main_loop();
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GraphLockableMainloop,
                              graph_lockable_auto_unlock_mainloop)

#define GRAPH_RDLOCK_GUARD_MAINLOOP(x)                              \
    g_autoptr(GraphLockableMainloop)                                \
    glue(graph_lockable_auto, __COUNTER__) G_GNUC_UNUSED =          \
            graph_lockable_auto_lock_mainloop(GMLML_OBJ_())

#endif

