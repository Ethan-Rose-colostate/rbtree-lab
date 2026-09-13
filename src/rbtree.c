/* Milestone 1, Slice 1: rb_create, plain-BST rb_insert (no fixups or
 * rotations yet), rb_find, recursive rb_destroy, and rb_size.
 *
 * rb_delete, rb_foreach, and rb_validate are out of scope this slice and
 * have minimal, honest stubs below.
 *
 * Leaf representation: a missing child is NULL, permanently -- there is no
 * shared sentinel node. NULL is treated as black for all red-black color
 * logic in every later slice (rotations/fixups must check for NULL rather
 * than dereference a sentinel's color field).
 */
#include "rbtree.h"

#include <stdlib.h>
#include <string.h>

enum rb_color { RB_RED, RB_BLACK };

struct rb_node {
    char           *key;      /* owned copy, NUL-terminated */
    void           *value;    /* owned only if t->value_free != NULL */
    enum rb_color   color;    /* set RB_RED on insert; unused for logic this slice */
    struct rb_node *left;
    struct rb_node *right;
    struct rb_node *parent;   /* NULL for root */
};

struct rbtree {
    struct rb_node   *root;       /* NULL when empty */
    size_t            size;
    rb_value_free_fn  value_free; /* may be NULL */
};

/* Allocation seam: every malloc/free in this file routes through these so
 * a later slice (or a test harness) can hook allocation without touching
 * call sites. */
static void *rb_malloc(size_t n) { return malloc(n); }
static void  rb_free(void *p)    { free(p); }

rbtree_t *rb_create(rb_value_free_fn value_free) {
    struct rbtree *t = rb_malloc(sizeof *t);
    if (t == NULL) {
        return NULL;
    }

    t->root = NULL;
    t->size = 0;
    t->value_free = value_free;
    return t;
}

int rb_insert(rbtree_t *t, const char *key, void *value) {
    if (t == NULL || key == NULL) {
        return -1;
    }

    struct rb_node *parent = NULL;
    struct rb_node *cur = t->root;
    int cmp = 0;

    /* cur descends the BST; parent trails one level behind; parent is
     * where the new node attaches if cur becomes NULL. */
    while (cur != NULL) {
        cmp = strcmp(key, cur->key);
        if (cmp == 0) {
            /* Overwrite path: same key reused verbatim, no allocation
             * is possible or needed on this path. */
            if (t->value_free != NULL) {
                t->value_free(cur->value);
            }
            cur->value = value;
            return 0;
        }
        parent = cur;
        cur = (cmp < 0) ? cur->left : cur->right;
    }

    size_t keylen = strlen(key);
    char *keycopy = NULL;
    struct rb_node *node = NULL;

    keycopy = rb_malloc(keylen + 1);
    if (keycopy == NULL) {
        goto fail;
    }
    memcpy(keycopy, key, keylen + 1);

    node = rb_malloc(sizeof *node);
    if (node == NULL) {
        goto fail;
    }

    node->key = keycopy;
    node->value = value;
    node->color = RB_RED;
    node->left = NULL;
    node->right = NULL;
    node->parent = parent;

    if (parent == NULL) {
        node->color = RB_BLACK; /* root is always black */
        t->root = node;
    } else if (cmp < 0) {
        parent->left = node;
    } else {
        parent->right = node;
    }
    t->size++;
    return 0;

fail:
    rb_free(keycopy);
    rb_free(node);
    return -1;
}

void *rb_find(const rbtree_t *t, const char *key) {
    if (t == NULL || key == NULL) {
        return NULL;
    }

    /* cur is the current candidate subtree root; the loop terminates on
     * a match or when a leaf (NULL child) is passed. */
    const struct rb_node *cur = t->root;
    while (cur != NULL) {
        int cmp = strcmp(key, cur->key);
        if (cmp == 0) {
            return cur->value;
        }
        cur = (cmp < 0) ? cur->left : cur->right;
    }
    return NULL;
}

static void free_subtree(struct rb_node *n, rb_value_free_fn value_free) {
    if (n == NULL) {
        return;
    }
    free_subtree(n->left, value_free);
    free_subtree(n->right, value_free);
    rb_free(n->key);
    if (value_free != NULL) {
        value_free(n->value);
    }
    rb_free(n);
}

void rb_destroy(rbtree_t *t) {
    if (t == NULL) {
        return;
    }
    free_subtree(t->root, t->value_free);
    rb_free(t);
}

int rb_delete(rbtree_t *t, const char *key) {
    (void)t;
    (void)key;
    return -1;
}

size_t rb_size(const rbtree_t *t) {
    if (t == NULL) {
        return 0;
    }
    return t->size;
}

void rb_foreach(const rbtree_t *t,
                void (*fn)(const char *key, void *value, void *ctx),
                void *ctx) {
    (void)t;
    (void)fn;
    (void)ctx;
}

int rb_validate(const rbtree_t *t) {
    (void)t;
    /* TODO Slice 2+: real invariant check. */
    return 0;
}
