/*
 * QEMU Xen emulation: The actual implementation of XenStore
 *
 * Copyright © 2023 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>, Paul Durrant <paul@xen.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qom/object.h"

#include "hw/xen/xen.h"

#include "xen_xenstore.h"
#include "xenstore_impl.h"

#include "hw/xen/interface/io/xs_wire.h"

#define XS_MAX_WATCHES          128
#define XS_MAX_DOMAIN_NODES     1000
#define XS_MAX_NODE_SIZE        2048
#define XS_MAX_TRANSACTIONS     10
#define XS_MAX_PERMS_PER_NODE   5

#define XS_VALID_CHARS "abcdefghijklmnopqrstuvwxyz" \
                       "ABCDEFGHIJKLMNOPQRSTUVWXYZ" \
                       "0123456789-/_"

typedef struct XsNode {
    uint32_t ref;
    GByteArray *content;
    GList *perms;
    GHashTable *children;
    uint64_t gencnt;
    bool deleted_in_tx;
    bool modified_in_tx;
    unsigned int serialized_tx;
#ifdef XS_NODE_UNIT_TEST
    gchar *name;
#endif
} XsNode;

typedef struct XsWatch {
    struct XsWatch *next;
    xs_impl_watch_fn *cb;
    void *cb_opaque;
    char *token;
    unsigned int dom_id;
    int rel_prefix;
} XsWatch;

typedef struct XsTransaction {
    XsNode *root;
    unsigned int nr_nodes;
    unsigned int base_tx;
    unsigned int tx_id;
    unsigned int dom_id;
} XsTransaction;

struct XenstoreImplState {
    XsNode *root;
    unsigned int nr_nodes;
    GHashTable *watches;
    unsigned int nr_domu_watches;
    GHashTable *transactions;
    unsigned int nr_domu_transactions;
    unsigned int root_tx;
    unsigned int last_tx;
    bool serialized;
};


static void nobble_tx(gpointer key, gpointer value, gpointer user_data)
{
    unsigned int *new_tx_id = user_data;
    XsTransaction *tx = value;

    if (tx->base_tx == *new_tx_id) {
        tx->base_tx = XBT_NULL;
    }
}

static inline unsigned int next_tx(struct XenstoreImplState *s)
{
    unsigned int tx_id;

    do {
        tx_id = ++s->last_tx;
    } while (tx_id == XBT_NULL || tx_id == s->root_tx ||
             g_hash_table_lookup(s->transactions, GINT_TO_POINTER(tx_id)));

    g_hash_table_foreach(s->transactions, nobble_tx, &tx_id);

    return tx_id;
}

static inline XsNode *xs_node_new(void)
{
    XsNode *n = g_new0(XsNode, 1);
    n->ref = 1;

#ifdef XS_NODE_UNIT_TEST
    nr_xs_nodes++;
    xs_node_list = g_list_prepend(xs_node_list, n);
#endif
    return n;
}

static inline XsNode *xs_node_ref(XsNode *n)
{
    g_assert(n->ref < INT_MAX);

    g_assert(n->ref);
    n->ref++;
    return n;
}

static inline void xs_node_unref(XsNode *n)
{
    if (!n) {
        return;
    }
    g_assert(n->ref);
    if (--n->ref) {
        return;
    }

    if (n->content) {
        g_byte_array_unref(n->content);
    }
    if (n->perms) {
        g_list_free_full(n->perms, g_free);
    }
    if (n->children) {
        g_hash_table_unref(n->children);
    }
#ifdef XS_NODE_UNIT_TEST
    g_free(n->name);
    nr_xs_nodes--;
    xs_node_list = g_list_remove(xs_node_list, n);
#endif
    g_free(n);
}

char *xs_perm_as_string(unsigned int perm, unsigned int domid)
{
    char letter;

    switch (perm) {
    case XS_PERM_READ | XS_PERM_WRITE:
        letter = 'b';
        break;
    case XS_PERM_READ:
        letter = 'r';
        break;
    case XS_PERM_WRITE:
        letter = 'w';
        break;
    case XS_PERM_NONE:
    default:
        letter = 'n';
        break;
    }

    return g_strdup_printf("%c%u", letter, domid);
}

static gpointer do_perm_copy(gconstpointer src, gpointer user_data)
{
    return g_strdup(src);
}

static XsNode *xs_node_create(const char *name, GList *perms)
{
    XsNode *n = xs_node_new();

#ifdef XS_NODE_UNIT_TEST
    if (name) {
        n->name = g_strdup(name);
    }
#endif

    n->perms = g_list_copy_deep(perms, do_perm_copy, NULL);

    return n;
}

static void do_child_insert(gpointer key, gpointer value, gpointer user_data)
{
    g_hash_table_insert(user_data, g_strdup(key), xs_node_ref(value));
}

static XsNode *xs_node_copy(XsNode *old)
{
    XsNode *n = xs_node_new();

    n->gencnt = old->gencnt;

#ifdef XS_NODE_UNIT_TEST
    if (n->name) {
        n->name = g_strdup(old->name);
    }
#endif

    assert(old);
    if (old->children) {
        n->children = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                            (GDestroyNotify)xs_node_unref);
        g_hash_table_foreach(old->children, do_child_insert, n->children);
    }
    if (old->perms) {
        n->perms = g_list_copy_deep(old->perms, do_perm_copy, NULL);
    }
    if (old->content) {
        n->content = g_byte_array_ref(old->content);
    }
    return n;
}

static bool xs_node_add_child(XsNode *n, const char *path_elem, XsNode *child)
{
    assert(!strchr(path_elem, '/'));

    if (!child) {
        assert(n->children);
        return g_hash_table_remove(n->children, path_elem);
    }

#ifdef XS_NODE_UNIT_TEST
    g_free(child->name);
    child->name = g_strdup(path_elem);
#endif
    if (!n->children) {
        n->children = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                            (GDestroyNotify)xs_node_unref);
    }

    return g_hash_table_insert(n->children, g_strdup(path_elem), child);
}

struct walk_op {
    struct XenstoreImplState *s;
    char path[XENSTORE_ABS_PATH_MAX + 2];
    int (*op_fn)(XsNode **n, struct walk_op *op);
    void *op_opaque;
    void *op_opaque2;

    GList *watches;
    unsigned int dom_id;
    unsigned int tx_id;

    unsigned int new_nr_nodes;

    bool inplace;
    bool mutating;
    bool create_dirs;
    bool in_transaction;

    bool deleted_in_tx;
};

static void fire_watches(struct walk_op *op, bool parents)
{
    GList *l = NULL;
    XsWatch *w;

    if (!op->mutating || op->in_transaction) {
        return;
    }

    if (parents) {
        l = op->watches;
    }

    w = g_hash_table_lookup(op->s->watches, op->path);
    while (w || l) {
        if (!w) {
            w = l->data;
            l = l->next;
            continue;
        }

        assert(strlen(op->path) > w->rel_prefix);
        w->cb(w->cb_opaque, op->path + w->rel_prefix, w->token);

        w = w->next;
    }
}

static int xs_node_add_content(XsNode **n, struct walk_op *op)
{
    GByteArray *data = op->op_opaque;

    if (op->dom_id) {
        if (data->len > XS_MAX_NODE_SIZE) {
            return E2BIG;
        }
    }
    if (!op->inplace) {
        XsNode *old = *n;
        *n = xs_node_copy(old);
        xs_node_unref(old);
    }

    if ((*n)->content) {
        g_byte_array_unref((*n)->content);
    }
    (*n)->content = g_byte_array_ref(data);
    if (op->tx_id != XBT_NULL) {
        (*n)->modified_in_tx = true;
    }
    return 0;
}

static int xs_node_get_content(XsNode **n, struct walk_op *op)
{
    GByteArray *data = op->op_opaque;
    GByteArray *node_data;

    assert(op->inplace);
    assert(*n);

    node_data = (*n)->content;
    if (node_data) {
        g_byte_array_append(data, node_data->data, node_data->len);
    }

    return 0;
}

static int node_rm_recurse(gpointer key, gpointer value, gpointer user_data)
{
    struct walk_op *op = user_data;
    int path_len = strlen(op->path);
    int key_len = strlen(key);
    XsNode *n = value;
    bool this_inplace = op->inplace;

    if (n->ref != 1) {
        op->inplace = 0;
    }

    assert(key_len + path_len + 2 <= sizeof(op->path));
    op->path[path_len] = '/';
    memcpy(op->path + path_len + 1, key, key_len + 1);

    if (n->children) {
        g_hash_table_foreach_remove(n->children, node_rm_recurse, op);
    }
    op->new_nr_nodes--;

    fire_watches(op, false);
    op->path[path_len] = '\0';

    return this_inplace;
}

static XsNode *xs_node_copy_deleted(XsNode *old, struct walk_op *op);
static void copy_deleted_recurse(gpointer key, gpointer value,
                                 gpointer user_data)
{
    struct walk_op *op = user_data;
    GHashTable *siblings = op->op_opaque2;
    XsNode *n = xs_node_copy_deleted(value, op);

    g_hash_table_insert(siblings, g_strdup(key), n);
}

static XsNode *xs_node_copy_deleted(XsNode *old, struct walk_op *op)
{
    XsNode *n = xs_node_new();

    n->gencnt = old->gencnt;

#ifdef XS_NODE_UNIT_TEST
    if (old->name) {
        n->name = g_strdup(old->name);
    }
#endif

    if (old->children) {
        n->children = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                            (GDestroyNotify)xs_node_unref);
        op->op_opaque2 = n->children;
        g_hash_table_foreach(old->children, copy_deleted_recurse, op);
    }
    if (old->perms) {
        n->perms = g_list_copy_deep(old->perms, do_perm_copy, NULL);
    }
    n->deleted_in_tx = true;
    if (old->content) {
        n->modified_in_tx = true;
    }
    op->new_nr_nodes--;
    return n;
}

static int xs_node_rm(XsNode **n, struct walk_op *op)
{
    bool this_inplace = op->inplace;

    if (op->tx_id != XBT_NULL) {
        XsNode *old = *n;
        *n = xs_node_copy_deleted(old, op);
        xs_node_unref(old);
        return 0;
    }

    if ((*n)->children) {
        g_hash_table_foreach_remove((*n)->children, node_rm_recurse, op);
    }
    op->new_nr_nodes--;

    if (this_inplace) {
        xs_node_unref(*n);
    }
    *n = NULL;
    return 0;
}

static int xs_node_get_perms(XsNode **n, struct walk_op *op)
{
    GList **perms = op->op_opaque;

    assert(op->inplace);
    assert(*n);

    *perms = g_list_copy_deep((*n)->perms, do_perm_copy, NULL);
    return 0;
}

static void parse_perm(const char *perm, char *letter, unsigned int *dom_id)
{
    unsigned int n = sscanf(perm, "%c%u", letter, dom_id);

    assert(n == 2);
}

static bool can_access(unsigned int dom_id, GList *perms, const char *letters)
{
    unsigned int i, n;
    char perm_letter;
    unsigned int perm_dom_id;
    bool access;

    if (dom_id == 0) {
        return true;
    }

    n = g_list_length(perms);
    assert(n >= 1);

    parse_perm(g_list_nth_data(perms, 0), &perm_letter, &perm_dom_id);
    if (dom_id == perm_dom_id) {
        return true;
    }

    access = !!strchr(letters, perm_letter);
    for (i = 1; i < n; i++) {
        parse_perm(g_list_nth_data(perms, i), &perm_letter, &perm_dom_id);
        if (dom_id != perm_dom_id) {
            continue;
        }
        access = !!strchr(letters, perm_letter);
    }

    return access;
}

static int xs_node_set_perms(XsNode **n, struct walk_op *op)
{
    GList *perms = op->op_opaque;

    if (op->dom_id) {
        unsigned int perm_dom_id;
        char perm_letter;

        if (!can_access(op->dom_id, (*n)->perms, "")) {
            return EPERM;
        }

        parse_perm(perms->data, &perm_letter, &perm_dom_id);
        if (perm_dom_id != op->dom_id) {
            return EPERM;
        }

        if (g_list_length(perms) > XS_MAX_PERMS_PER_NODE) {
            return ENOSPC;
        }
    }

    if (!op->inplace) {
        XsNode *old = *n;
        *n = xs_node_copy(old);
        xs_node_unref(old);
    }

    if ((*n)->perms) {
        g_list_free_full((*n)->perms, g_free);
    }
    (*n)->perms = g_list_copy_deep(perms, do_perm_copy, NULL);
    if (op->tx_id != XBT_NULL) {
        (*n)->modified_in_tx = true;
    }
    return 0;
}

static int xs_node_walk(XsNode **n, struct walk_op *op)
{
    char *child_name = NULL;
    size_t namelen;
    XsNode *old = *n, *child = NULL;
    bool stole_child = false;
    bool this_inplace;
    XsWatch *watch;
    int err;

    namelen = strlen(op->path);
    watch = g_hash_table_lookup(op->s->watches, op->path);

    if (op->path[namelen + 1]) {
        char *slash;
        child_name = op->path + namelen + 1;
        slash = strchr(child_name, '/');
        if (slash) {
            *slash = '\0';
        }
        op->path[namelen] = '/';
    }

    if (op->mutating && old->ref != 1) {
        op->inplace = false;
    }

    if (!child_name) {
        const char *letters = op->mutating ? "wb" : "rb";

        if (!can_access(op->dom_id, old->perms, letters)) {
            err = EACCES;
            goto out;
        }

        err = op->op_fn(n, op);
        if (!err) {
            fire_watches(op, true);
        }
        goto out;
    }

    this_inplace = op->inplace;

    if (old && old->children) {
        child = g_hash_table_lookup(old->children, child_name);
    }

    if (child) {
        if (child->deleted_in_tx) {
            assert(child->ref == 1);
        }
        xs_node_ref(child);
        if (op->mutating && this_inplace) {
            g_hash_table_remove(old->children, child_name);
            stole_child = true;
        }
    } else if (op->create_dirs) {
        assert(op->mutating);

        if (!can_access(op->dom_id, old->perms, "wb")) {
            err = EACCES;
            goto out;
        }

        if (op->dom_id && op->new_nr_nodes >= XS_MAX_DOMAIN_NODES) {
            err = ENOSPC;
            goto out;
        }

        child = xs_node_create(child_name, old->perms);
        op->new_nr_nodes++;

        op->inplace = true;
    } else {
        err = ENOENT;
        goto out;
    }

    if (watch) {
        op->watches = g_list_append(op->watches, watch);
    }

    err = xs_node_walk(&child, op);

    if (watch) {
        op->watches = g_list_remove(op->watches, watch);
    }

    if (err || !op->mutating) {
        if (stole_child) {
            g_hash_table_replace(old->children, g_strdup(child_name), child);
        } else {
            xs_node_unref(child);
        }
        goto out;
    }

    if (!this_inplace) {
        *n = xs_node_copy(old);
        xs_node_unref(old);
    }

    if (op->create_dirs && child && child->deleted_in_tx) {
        op->new_nr_nodes++;
        child->deleted_in_tx = false;
    }

    if ((xs_node_add_child(*n, child_name, child) && !stole_child) || !child) {
        (*n)->gencnt++;
    }

 out:
    op->path[namelen] = '\0';
    if (!namelen) {
        assert(!op->watches);
        if (!err && op->mutating) {
            if (!op->in_transaction) {
                if (op->s->root_tx != op->s->last_tx) {
                    op->s->root_tx = next_tx(op->s);
                }
                op->s->nr_nodes = op->new_nr_nodes;
            } else {
                XsTransaction *tx = g_hash_table_lookup(op->s->transactions,
                                                        GINT_TO_POINTER(op->tx_id));
                assert(tx);
                tx->nr_nodes = op->new_nr_nodes;
            }
        }
    }
    return err;
}

static void append_directory_item(gpointer key, gpointer value,
                                  gpointer user_data)
{
    GList **items = user_data;

    *items = g_list_insert_sorted(*items, g_strdup(key), (GCompareFunc)strcmp);
}

static int xs_node_directory(XsNode **n, struct walk_op *op)
{
    GList **items = op->op_opaque;

    assert(op->inplace);
    assert(*n);

    if ((*n)->children) {
        g_hash_table_foreach((*n)->children, append_directory_item, items);
    }

    if (op->op_opaque2) {
        *(uint64_t *)op->op_opaque2 = (*n)->gencnt;
    }

    return 0;
}

static int validate_path(char *outpath, const char *userpath,
                         unsigned int dom_id)
{
    size_t i, pathlen = strlen(userpath);

    if (!pathlen || userpath[pathlen] == '/' || strstr(userpath, "//")) {
        return EINVAL;
    }
    for (i = 0; i < pathlen; i++) {
        if (!strchr(XS_VALID_CHARS, userpath[i])) {
            return EINVAL;
        }
    }
    if (userpath[0] == '/') {
        if (pathlen > XENSTORE_ABS_PATH_MAX) {
            return E2BIG;
        }
        memcpy(outpath, userpath, pathlen + 1);
    } else {
        if (pathlen > XENSTORE_REL_PATH_MAX) {
            return E2BIG;
        }
        snprintf(outpath, XENSTORE_ABS_PATH_MAX, "/local/domain/%u/%s", dom_id,
                 userpath);
    }
    return 0;
}


static int init_walk_op(XenstoreImplState *s, struct walk_op *op,
                        xs_transaction_t tx_id, unsigned int dom_id,
                        const char *path, XsNode ***rootp)
{
    int ret = validate_path(op->path, path, dom_id);
    if (ret) {
        return ret;
    }

    op->path[strlen(op->path) + 1] = '\0';
    op->watches = NULL;
    op->path[0] = '\0';
    op->inplace = true;
    op->mutating = false;
    op->create_dirs = false;
    op->in_transaction = false;
    op->dom_id = dom_id;
    op->tx_id = tx_id;
    op->s = s;

    if (tx_id == XBT_NULL) {
        *rootp = &s->root;
        op->new_nr_nodes = s->nr_nodes;
    } else {
        XsTransaction *tx = g_hash_table_lookup(s->transactions,
                                                GINT_TO_POINTER(tx_id));
        if (!tx) {
            return ENOENT;
        }
        *rootp = &tx->root;
        op->new_nr_nodes = tx->nr_nodes;
        op->in_transaction = true;
    }

    return 0;
}

int xs_impl_read(XenstoreImplState *s, unsigned int dom_id,
                 xs_transaction_t tx_id, const char *path, GByteArray *data)
{
    struct walk_op op;
    XsNode **n;
    int ret;

    ret = init_walk_op(s, &op, tx_id, dom_id, path, &n);
    if (ret) {
        return ret;
    }
    op.op_fn = xs_node_get_content;
    op.op_opaque = data;
    return xs_node_walk(n, &op);
}

int xs_impl_write(XenstoreImplState *s, unsigned int dom_id,
                  xs_transaction_t tx_id, const char *path, GByteArray *data)
{
    struct walk_op op;
    XsNode **n;
    int ret;

    ret = init_walk_op(s, &op, tx_id, dom_id, path, &n);
    if (ret) {
        return ret;
    }
    op.op_fn = xs_node_add_content;
    op.op_opaque = data;
    op.mutating = true;
    op.create_dirs = true;
    return xs_node_walk(n, &op);
}

int xs_impl_directory(XenstoreImplState *s, unsigned int dom_id,
                      xs_transaction_t tx_id, const char *path,
                      uint64_t *gencnt, GList **items)
{
    struct walk_op op;
    XsNode **n;
    int ret;

    ret = init_walk_op(s, &op, tx_id, dom_id, path, &n);
    if (ret) {
        return ret;
    }
    op.op_fn = xs_node_directory;
    op.op_opaque = items;
    op.op_opaque2 = gencnt;
    return xs_node_walk(n, &op);
}

int xs_impl_transaction_start(XenstoreImplState *s, unsigned int dom_id,
                              xs_transaction_t *tx_id)
{
    XsTransaction *tx;

    if (*tx_id != XBT_NULL) {
        return EINVAL;
    }

    if (dom_id && s->nr_domu_transactions >= XS_MAX_TRANSACTIONS) {
        return ENOSPC;
    }

    tx = g_new0(XsTransaction, 1);

    tx->nr_nodes = s->nr_nodes;
    tx->tx_id = next_tx(s);
    tx->base_tx = s->root_tx;
    tx->root = xs_node_ref(s->root);
    tx->dom_id = dom_id;

    g_hash_table_insert(s->transactions, GINT_TO_POINTER(tx->tx_id), tx);
    if (dom_id) {
        s->nr_domu_transactions++;
    }
    *tx_id = tx->tx_id;
    return 0;
}

static gboolean tx_commit_walk(gpointer key, gpointer value,
                               gpointer user_data)
{
    struct walk_op *op = user_data;
    int path_len = strlen(op->path);
    int key_len = strlen(key);
    bool fire_parents = true;
    XsWatch *watch;
    XsNode *n = value;

    if (n->ref != 1) {
        return false;
    }

    if (n->deleted_in_tx) {
        fire_parents = !op->deleted_in_tx;

        op->deleted_in_tx = true;
    }

    assert(key_len + path_len + 2 <= sizeof(op->path));
    op->path[path_len] = '/';
    memcpy(op->path + path_len + 1, key, key_len + 1);

    watch = g_hash_table_lookup(op->s->watches, op->path);
    if (watch) {
        op->watches = g_list_append(op->watches, watch);
    }

    if (n->children) {
        g_hash_table_foreach_remove(n->children, tx_commit_walk, op);
    }

    if (watch) {
        op->watches = g_list_remove(op->watches, watch);
    }

    if (n->modified_in_tx || n->deleted_in_tx) {
        fire_watches(op, fire_parents);
        n->modified_in_tx = false;
    }
    op->path[path_len] = '\0';

    return n->deleted_in_tx;
}

static int transaction_commit(XenstoreImplState *s, XsTransaction *tx)
{
    struct walk_op op;
    XsNode **n;
    int ret;

    if (s->root_tx != tx->base_tx) {
        return EAGAIN;
    }
    xs_node_unref(s->root);
    s->root = tx->root;
    tx->root = NULL;
    s->root_tx = tx->tx_id;
    s->nr_nodes = tx->nr_nodes;

    ret = init_walk_op(s, &op, XBT_NULL, tx->dom_id, "/", &n);
    g_assert(!ret);

    op.deleted_in_tx = false;
    op.mutating = true;

    if (s->root->children) {
        g_hash_table_foreach_remove(s->root->children, tx_commit_walk, &op);
    }

    return 0;
}

int xs_impl_transaction_end(XenstoreImplState *s, unsigned int dom_id,
                            xs_transaction_t tx_id, bool commit)
{
    int ret = 0;
    XsTransaction *tx = g_hash_table_lookup(s->transactions,
                                            GINT_TO_POINTER(tx_id));

    if (!tx || tx->dom_id != dom_id) {
        return ENOENT;
    }

    if (commit) {
        ret = transaction_commit(s, tx);
    }

    g_hash_table_remove(s->transactions, GINT_TO_POINTER(tx_id));
    if (dom_id) {
        assert(s->nr_domu_transactions);
        s->nr_domu_transactions--;
    }
    return ret;
}

int xs_impl_rm(XenstoreImplState *s, unsigned int dom_id,
               xs_transaction_t tx_id, const char *path)
{
    struct walk_op op;
    XsNode **n;
    int ret;

    ret = init_walk_op(s, &op, tx_id, dom_id, path, &n);
    if (ret) {
        return ret;
    }
    op.op_fn = xs_node_rm;
    op.mutating = true;
    return xs_node_walk(n, &op);
}

int xs_impl_get_perms(XenstoreImplState *s, unsigned int dom_id,
                      xs_transaction_t tx_id, const char *path, GList **perms)
{
    struct walk_op op;
    XsNode **n;
    int ret;

    ret = init_walk_op(s, &op, tx_id, dom_id, path, &n);
    if (ret) {
        return ret;
    }
    op.op_fn = xs_node_get_perms;
    op.op_opaque = perms;
    return xs_node_walk(n, &op);
}

static void is_valid_perm(gpointer data, gpointer user_data)
{
    char *perm = data;
    bool *valid = user_data;
    char letter;
    unsigned int dom_id;

    if (!*valid) {
        return;
    }

    if (sscanf(perm, "%c%u", &letter, &dom_id) != 2) {
        *valid = false;
        return;
    }

    switch (letter) {
    case 'n':
    case 'r':
    case 'w':
    case 'b':
        break;

    default:
        *valid = false;
        break;
    }
}

int xs_impl_set_perms(XenstoreImplState *s, unsigned int dom_id,
                      xs_transaction_t tx_id, const char *path, GList *perms)
{
    struct walk_op op;
    XsNode **n;
    bool valid = true;
    int ret;

    if (!g_list_length(perms)) {
        return EINVAL;
    }

    g_list_foreach(perms, is_valid_perm, &valid);
    if (!valid) {
        return EINVAL;
    }

    ret = init_walk_op(s, &op, tx_id, dom_id, path, &n);
    if (ret) {
        return ret;
    }
    op.op_fn = xs_node_set_perms;
    op.op_opaque = perms;
    op.mutating = true;
    return xs_node_walk(n, &op);
}

static int do_xs_impl_watch(XenstoreImplState *s, unsigned int dom_id,
                            const char *path, const char *token,
                            xs_impl_watch_fn fn, void *opaque)

{
    char abspath[XENSTORE_ABS_PATH_MAX + 1];
    XsWatch *w, *l;
    int ret;

    ret = validate_path(abspath, path, dom_id);
    if (ret) {
        return ret;
    }

    l = w = g_hash_table_lookup(s->watches, abspath);
    while (w) {
        if (!g_strcmp0(token, w->token) &&  opaque == w->cb_opaque &&
            fn == w->cb && dom_id == w->dom_id) {
            return EEXIST;
        }
        w = w->next;
    }

    if (dom_id && s->nr_domu_watches >= XS_MAX_WATCHES) {
        return E2BIG;
    }

    w = g_new0(XsWatch, 1);
    w->token = g_strdup(token);
    w->cb = fn;
    w->cb_opaque = opaque;
    w->dom_id = dom_id;
    w->rel_prefix = strlen(abspath) - strlen(path);

    if (l) {
        w->next = l->next;
        l->next = w;
    } else {
        g_hash_table_insert(s->watches, g_strdup(abspath), w);
    }
    if (dom_id) {
        s->nr_domu_watches++;
    }

    return 0;
}

int xs_impl_watch(XenstoreImplState *s, unsigned int dom_id, const char *path,
                  const char *token, xs_impl_watch_fn fn, void *opaque)
{
    int ret = do_xs_impl_watch(s, dom_id, path, token, fn, opaque);

    if (!ret) {
        fn(opaque, path, token);
    }

    return ret;
}

static XsWatch *free_watch(XenstoreImplState *s, XsWatch *w)
{
    XsWatch *next = w->next;

    if (w->dom_id) {
        assert(s->nr_domu_watches);
        s->nr_domu_watches--;
    }

    g_free(w->token);
    g_free(w);

    return next;
}

int xs_impl_unwatch(XenstoreImplState *s, unsigned int dom_id,
                    const char *path, const char *token,
                    xs_impl_watch_fn fn, void *opaque)
{
    char abspath[XENSTORE_ABS_PATH_MAX + 1];
    XsWatch *w, **l;
    int ret;

    ret = validate_path(abspath, path, dom_id);
    if (ret) {
        return ret;
    }

    w = g_hash_table_lookup(s->watches, abspath);
    if (!w) {
        return ENOENT;
    }

    if (!g_strcmp0(token, w->token) && fn == w->cb && opaque == w->cb_opaque &&
        dom_id == w->dom_id) {
        if (w->next) {
            g_hash_table_insert(s->watches, g_strdup(abspath), w->next);
        } else {
            g_hash_table_remove(s->watches, abspath);
        }
        free_watch(s, w);
        return 0;
    }

    for (l = &w->next; *l; l = &w->next) {
        w = *l;

        if (!g_strcmp0(token, w->token) && fn == w->cb &&
            opaque != w->cb_opaque && dom_id == w->dom_id) {
            *l = free_watch(s, w);
            return 0;
        }
    }

    return ENOENT;
}

int xs_impl_reset_watches(XenstoreImplState *s, unsigned int dom_id)
{
    char **watch_paths;
    guint nr_watch_paths;
    guint i;

    watch_paths = (char **)g_hash_table_get_keys_as_array(s->watches,
                                                          &nr_watch_paths);

    for (i = 0; i < nr_watch_paths; i++) {
        XsWatch *w1 = g_hash_table_lookup(s->watches, watch_paths[i]);
        XsWatch *w2, *w, **l;

        w = w2 = w1;
        l = &w;
        while (w) {
            if (w->dom_id == dom_id) {
                if (w2 == w) {
                    w2 = w->next;
                }
                *l = free_watch(s, w);
            } else {
                l = &w->next;
            }
            w = *l;
        }
        if (w1 != w2) {
            g_hash_table_steal(s->watches, watch_paths[i]);

            if (w2) {
                g_hash_table_insert(s->watches, watch_paths[i], w2);
            } else {
                g_free(watch_paths[i]);
            }
        }
    }
    g_free(watch_paths);
    return 0;
}

static void xs_tx_free(void *_tx)
{
    XsTransaction *tx = _tx;
    if (tx->root) {
        xs_node_unref(tx->root);
    }
    g_free(tx);
}

XenstoreImplState *xs_impl_create(unsigned int dom_id)
{
    XenstoreImplState *s = g_new0(XenstoreImplState, 1);
    GList *perms;

    s->watches = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    s->transactions = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                            NULL, xs_tx_free);

    perms = g_list_append(NULL, xs_perm_as_string(XS_PERM_NONE, 0));
    s->root = xs_node_create("/", perms);
    g_list_free_full(perms, g_free);
    s->nr_nodes = 1;

    s->root_tx = s->last_tx = 1;
    return s;
}


static void clear_serialized_tx(gpointer key, gpointer value, gpointer opaque)
{
    XsNode *n = value;

    n->serialized_tx = XBT_NULL;
    if (n->children) {
        g_hash_table_foreach(n->children, clear_serialized_tx, NULL);
    }
}

static void clear_tx_serialized_tx(gpointer key, gpointer value,
                                   gpointer opaque)
{
    XsTransaction *t = value;

    clear_serialized_tx(NULL, t->root, NULL);
}

static void write_be32(GByteArray *save, uint32_t val)
{
    uint32_t be = htonl(val);
    g_byte_array_append(save, (void *)&be, sizeof(be));
}


struct save_state {
    GByteArray *bytes;
    unsigned int tx_id;
};

#define MODIFIED_IN_TX  (1U << 0)
#define DELETED_IN_TX   (1U << 1)
#define NODE_REF        (1U << 2)

static void save_node(gpointer key, gpointer value, gpointer opaque)
{
    struct save_state *ss = opaque;
    XsNode *n = value;
    char *name = key;
    uint8_t flag = 0;

    if (name) {
        g_byte_array_append(ss->bytes, key, strlen(key) + 1);
    }

    if (n->serialized_tx != XBT_NULL) {
        flag = NODE_REF;
        g_byte_array_append(ss->bytes, &flag, 1);
        write_be32(ss->bytes, n->serialized_tx);
    } else {
        GList *l;
        n->serialized_tx = ss->tx_id;

        if (n->modified_in_tx) {
            flag |= MODIFIED_IN_TX;
        }
        if (n->deleted_in_tx) {
            flag |= DELETED_IN_TX;
        }
        g_byte_array_append(ss->bytes, &flag, 1);

        if (n->content) {
            write_be32(ss->bytes, n->content->len);
            g_byte_array_append(ss->bytes, n->content->data, n->content->len);
        } else {
            write_be32(ss->bytes, 0);
        }

        for (l = n->perms; l; l = l->next) {
            g_byte_array_append(ss->bytes, l->data, strlen(l->data) + 1);
        }
        g_byte_array_append(ss->bytes, (void *)"", 1);

        if (n->children) {
            g_hash_table_foreach(n->children, save_node, ss);
        }
        g_byte_array_append(ss->bytes, (void *)"", 1);
    }
}

static void save_tree(struct save_state *ss, uint32_t tx_id, XsNode *root)
{
    write_be32(ss->bytes, tx_id);
    ss->tx_id = tx_id;
    save_node(NULL, root, ss);
}

static void save_tx(gpointer key, gpointer value, gpointer opaque)
{
    uint32_t tx_id = GPOINTER_TO_INT(key);
    struct save_state *ss = opaque;
    XsTransaction *n = value;

    write_be32(ss->bytes, n->base_tx);
    write_be32(ss->bytes, n->dom_id);

    save_tree(ss, tx_id, n->root);
}

static void save_watch(gpointer key, gpointer value, gpointer opaque)
{
    struct save_state *ss = opaque;
    XsWatch *w = value;

    if (w->dom_id) {
        gpointer relpath = key + w->rel_prefix;
        g_byte_array_append(ss->bytes, relpath, strlen(relpath) + 1);
        g_byte_array_append(ss->bytes, (void *)w->token, strlen(w->token) + 1);
    }
}

GByteArray *xs_impl_serialize(XenstoreImplState *s)
{
    struct save_state ss;

    ss.bytes = g_byte_array_new();

    if (s->serialized) {
        clear_serialized_tx(NULL, s->root, NULL);
        g_hash_table_foreach(s->transactions, clear_tx_serialized_tx, NULL);
    }

    s->serialized = true;

    write_be32(ss.bytes, s->last_tx);
    save_tree(&ss, s->root_tx, s->root);
    g_hash_table_foreach(s->transactions, save_tx, &ss);

    write_be32(ss.bytes, XBT_NULL);

    g_hash_table_foreach(s->watches, save_watch, &ss);
    g_byte_array_append(ss.bytes, (void *)"", 1);

    return ss.bytes;
}

struct unsave_state {
    char path[XENSTORE_ABS_PATH_MAX + 1];
    XenstoreImplState *s;
    GByteArray *bytes;
    uint8_t *d;
    size_t l;
    bool root_walk;
};

static int consume_be32(struct unsave_state *us, unsigned int *val)
{
    uint32_t d;

    if (us->l < sizeof(d)) {
        return -EINVAL;
    }
    memcpy(&d, us->d, sizeof(d));
    *val = ntohl(d);
    us->d += sizeof(d);
    us->l -= sizeof(d);
    return 0;
}

static int consume_string(struct unsave_state *us, char **str, size_t *len)
{
    size_t l;

    if (!us->l) {
        return -EINVAL;
    }

    l = strnlen((void *)us->d, us->l);
    if (l == us->l) {
        return -EINVAL;
    }

    if (str) {
        *str = (void *)us->d;
    }
    if (len) {
        *len = l;
    }

    us->d += l + 1;
    us->l -= l + 1;
    return 0;
}

static XsNode *lookup_node(XsNode *n, char *path)
{
    char *slash = strchr(path, '/');
    XsNode *child;

    if (path[0] == '\0') {
        return n;
    }

    if (slash) {
        *slash = '\0';
    }

    if (!n->children) {
        return NULL;
    }
    child = g_hash_table_lookup(n->children, path);
    if (!slash) {
        return child;
    }

    *slash = '/';
    if (!child) {
        return NULL;
    }
    return lookup_node(child, slash + 1);
}

static XsNode *lookup_tx_node(struct unsave_state *us, unsigned int tx_id)
{
    XsTransaction *t;
    if (tx_id == us->s->root_tx) {
        return lookup_node(us->s->root, us->path + 1);
    }

    t = g_hash_table_lookup(us->s->transactions, GINT_TO_POINTER(tx_id));
    if (!t) {
        return NULL;
    }
    g_assert(t->root);
    return lookup_node(t->root, us->path + 1);
}

static void count_child_nodes(gpointer key, gpointer value, gpointer user_data)
{
    unsigned int *nr_nodes = user_data;
    XsNode *n = value;

    (*nr_nodes)++;

    if (n->children) {
        g_hash_table_foreach(n->children, count_child_nodes, nr_nodes);
    }
}

static int consume_node(struct unsave_state *us, XsNode **nodep,
                        unsigned int *nr_nodes)
{
    XsNode *n = NULL;
    uint8_t flags;
    int ret;

    if (us->l < 1) {
        return -EINVAL;
    }
    flags = us->d[0];
    us->d++;
    us->l--;

    if (flags == NODE_REF) {
        unsigned int tx;

        ret = consume_be32(us, &tx);
        if (ret) {
            return ret;
        }

        n = lookup_tx_node(us, tx);
        if (!n) {
            return -EINVAL;
        }
        n->ref++;
        if (n->children) {
            g_hash_table_foreach(n->children, count_child_nodes, nr_nodes);
        }
    } else {
        uint32_t datalen;

        if (flags & ~(DELETED_IN_TX | MODIFIED_IN_TX)) {
            return -EINVAL;
        }
        n = xs_node_new();

        if (flags & DELETED_IN_TX) {
            n->deleted_in_tx = true;
        }
        if (flags & MODIFIED_IN_TX) {
            n->modified_in_tx = true;
        }
        ret = consume_be32(us, &datalen);
        if (ret) {
            xs_node_unref(n);
            return -EINVAL;
        }
        if (datalen) {
            if (datalen > us->l) {
                xs_node_unref(n);
                return -EINVAL;
            }

            GByteArray *node_data = g_byte_array_new();
            g_byte_array_append(node_data, us->d, datalen);
            us->d += datalen;
            us->l -= datalen;
            n->content = node_data;

            if (us->root_walk) {
                n->modified_in_tx = true;
            }
        }
        while (1) {
            char *perm = NULL;
            size_t permlen = 0;

            ret = consume_string(us, &perm, &permlen);
            if (ret) {
                xs_node_unref(n);
                return ret;
            }

            if (!permlen) {
                break;
            }

            n->perms = g_list_append(n->perms, g_strdup(perm));
        }

        while (1) {
            size_t childlen;
            char *childname;
            char *pathend;
            XsNode *child = NULL;

            ret = consume_string(us, &childname, &childlen);
            if (ret) {
                xs_node_unref(n);
                return ret;
            }

            if (!childlen) {
                break;
            }

            pathend = us->path + strlen(us->path);
            strncat(us->path, "/", sizeof(us->path) - 1);
            strncat(us->path, childname, sizeof(us->path) - 1);

            ret = consume_node(us, &child, nr_nodes);
            *pathend = '\0';
            if (ret) {
                xs_node_unref(n);
                return ret;
            }
            g_assert(child);
            xs_node_add_child(n, childname, child);
        }

        if (us->root_walk && !n->children) {
            n->modified_in_tx = true;
        }
    }

    if (!n->deleted_in_tx) {
        (*nr_nodes)++;
    }

    *nodep = n;
    return 0;
}

static int consume_tree(struct unsave_state *us, XsTransaction *t)
{
    int ret;

    ret = consume_be32(us, &t->tx_id);
    if (ret) {
        return ret;
    }

    if (t->tx_id > us->s->last_tx) {
        return -EINVAL;
    }

    us->path[0] = '\0';

    return consume_node(us, &t->root, &t->nr_nodes);
}

int xs_impl_deserialize(XenstoreImplState *s, GByteArray *bytes,
                        unsigned int dom_id, xs_impl_watch_fn watch_fn,
                        void *watch_opaque)
{
    struct unsave_state us;
    XsTransaction base_t = { 0 };
    int ret;

    us.s = s;
    us.bytes = bytes;
    us.d = bytes->data;
    us.l = bytes->len;

    xs_impl_reset_watches(s, dom_id);
    g_hash_table_remove_all(s->transactions);

    xs_node_unref(s->root);
    s->root = NULL;
    s->root_tx = s->last_tx = XBT_NULL;

    ret = consume_be32(&us, &s->last_tx);
    if (ret) {
        return ret;
    }

    base_t.dom_id = dom_id;
    base_t.base_tx = XBT_NULL;
    us.root_walk = true;
    ret = consume_tree(&us, &base_t);
    if (ret) {
        return ret;
    }
    us.root_walk = false;

    ret = transaction_commit(s, &base_t);
    if (ret) {
        return ret;
    }

    while (1) {
        unsigned int base_tx;
        XsTransaction *t;

        ret = consume_be32(&us, &base_tx);
        if (ret) {
            return ret;
        }
        if (base_tx == XBT_NULL) {
            break;
        }

        t = g_new0(XsTransaction, 1);
        t->base_tx = base_tx;

        ret = consume_be32(&us, &t->dom_id);
        if (!ret) {
            ret = consume_tree(&us, t);
        }
        if (ret) {
            g_free(t);
            return ret;
        }
        g_assert(t->root);
        if (t->dom_id) {
            s->nr_domu_transactions++;
        }
        g_hash_table_insert(s->transactions, GINT_TO_POINTER(t->tx_id), t);
    }

    while (1) {
        char *path, *token;
        size_t pathlen, toklen;

        ret = consume_string(&us, &path, &pathlen);
        if (ret) {
            return ret;
        }
        if (!pathlen) {
            break;
        }

        ret = consume_string(&us, &token, &toklen);
        if (ret) {
            return ret;
        }

        if (!watch_fn) {
            continue;
        }

        ret = do_xs_impl_watch(s, dom_id, path, token, watch_fn, watch_opaque);
        if (ret) {
            return ret;
        }
    }

    if (us.l) {
        return -EINVAL;
    }

    return 0;
}
