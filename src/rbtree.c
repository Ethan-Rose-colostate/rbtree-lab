/* Milestone 1: the complete core red-black tree -- rb_create, rb_insert
 * with rotations and CLRS-style insert fixup, rb_find, rb_destroy,
 * rb_size, rb_validate, and rb_foreach.
 *
 * Milestone 2: rb_delete, built on CLRS's transplant + delete-fixup.
 *
 * Leaf representation: a missing child is NULL, permanently -- there is no
 * shared sentinel node. NULL is treated as black for all red-black color
 * logic (see color_of()); rotations/fixup/validate all check for NULL
 * rather than dereference a sentinel's color field. This is also why
 * delete_fixup takes an explicit x_parent argument instead of reading
 * x->parent the way CLRS does with its sentinel: x itself may be NULL,
 * and NULL has no parent pointer to read.
 */
#include "rbtree.h"

#include <stdlib.h>
#include <string.h>

enum rb_color { RB_RED, RB_BLACK };

struct rb_node {
    char           *key;      /* owned copy, NUL-terminated */
    void           *value;    /* owned only if t->value_free != NULL */
    enum rb_color   color;    /* read by insert_fixup, delete_fixup, and validate */
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

/* NULL counts as a black leaf everywhere in red-black logic, since there
 * is no sentinel node to hold a color. */
static enum rb_color color_of(const struct rb_node *n) { return n ? n->color : RB_BLACK; }

/* Promotes x->right to x's place; x becomes its former right child's left
 * child. Order matters: capture the mover (y) first, migrate the orphaned
 * middle subtree next (read before overwrite), reparent y into the
 * grandparent slot (or root) third, link x under y last. */
static void rotate_left(rbtree_t *t, struct rb_node *x) {
    struct rb_node *y = x->right;

    x->right = y->left;
    if (y->left != NULL) {
        y->left->parent = x;
    }

    y->parent = x->parent;
    if (x->parent == NULL) {
        t->root = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }

    y->left = x;
    x->parent = y;
}

/* Mirror of rotate_left: promotes x->left to x's place. */
static void rotate_right(rbtree_t *t, struct rb_node *x) {
    struct rb_node *y = x->left;

    x->left = y->right;
    if (y->right != NULL) {
        y->right->parent = x;
    }

    y->parent = x->parent;
    if (x->parent == NULL) {
        t->root = y;
    } else if (x == x->parent->right) {
        x->parent->right = y;
    } else {
        x->parent->left = y;
    }

    y->right = x;
    x->parent = y;
}

/* Restores red-black properties after inserting red node z.
 * Loop invariant: z is red, and the only possible violation is a red-red
 * edge between z and z->parent -- every other red-black property holds. */
static void insert_fixup(rbtree_t *t, struct rb_node *z) {
    while (color_of(z->parent) == RB_RED) {
        if (z->parent == z->parent->parent->left) {
            struct rb_node *uncle = z->parent->parent->right;
            if (color_of(uncle) == RB_RED) {
                /* Case 1: uncle red -- recolor only, push violation up. */
                z->parent->color = RB_BLACK;
                uncle->color = RB_BLACK;
                z->parent->parent->color = RB_RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->right) {
                    /* Case 2: triangle -- rotate at parent to form a line. */
                    z = z->parent;
                    rotate_left(t, z);
                }
                /* Case 3: line -- recolor then rotate at grandparent. */
                z->parent->color = RB_BLACK;
                z->parent->parent->color = RB_RED;
                rotate_right(t, z->parent->parent);
            }
        } else {
            /* Mirror: parent is a right child; uncle is grandparent->left;
             * Case 2 uses rotate_right, Case 3 uses rotate_left. */
            struct rb_node *uncle = z->parent->parent->left;
            if (color_of(uncle) == RB_RED) {
                z->parent->color = RB_BLACK;
                uncle->color = RB_BLACK;
                z->parent->parent->color = RB_RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    rotate_right(t, z);
                }
                z->parent->color = RB_BLACK;
                z->parent->parent->color = RB_RED;
                rotate_left(t, z->parent->parent);
            }
        }
    }
    t->root->color = RB_BLACK;
}

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
        node->color = RB_BLACK; /* redundant-but-harmless: insert_fixup's
                                  * tail line also guarantees this on every
                                  * call; kept as a self-documenting base
                                  * case for the empty-tree fast path. */
        t->root = node;
    } else if (cmp < 0) {
        parent->left = node;
    } else {
        parent->right = node;
    }
    insert_fixup(t, node);
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

static struct rb_node *find_node(rbtree_t *t, const char *key) {
    struct rb_node *cur = t->root;
    while (cur != NULL) {
        int cmp = strcmp(key, cur->key);
        if (cmp == 0) {
            return cur;
        }
        cur = (cmp < 0) ? cur->left : cur->right;
    }
    return NULL;
}

/* Descends left from n; loop invariant: any key smaller than n->key (if
 * one exists in this subtree) lies in n->left. */
static struct rb_node *tree_minimum(struct rb_node *n) {
    while (n->left != NULL) {
        n = n->left;
    }
    return n;
}

/* Replaces the subtree rooted at u with the subtree rooted at v, relinking
 * u's parent (or t->root) to point at v. v may be NULL. */
static void rb_transplant(rbtree_t *t, struct rb_node *u, struct rb_node *v) {
    if (u->parent == NULL) {
        t->root = v;
    } else if (u == u->parent->left) {
        u->parent->left = v;
    } else {
        u->parent->right = v;
    }
    if (v != NULL) {
        v->parent = u->parent;
    }
}

/* Restores red-black properties after a black node was spliced out,
 * represented by x (possibly NULL) carrying one extra unit of black.
 * x_parent is x's parent even when x is NULL, since NULL has no parent
 * pointer of its own to read.
 * Loop invariant: every red-black property holds except that x's subtree
 * is short one black node on every root-to-leaf path through it. */
static void delete_fixup(rbtree_t *t, struct rb_node *x, struct rb_node *x_parent) {
    while (x != t->root && color_of(x) == RB_BLACK) {
        if (x == x_parent->left) {
            struct rb_node *w = x_parent->right;
            if (color_of(w) == RB_RED) {
                /* Case 1: red sibling -- rotate to expose a black sibling. */
                w->color = RB_BLACK;
                x_parent->color = RB_RED;
                rotate_left(t, x_parent);
                w = x_parent->right;
            }
            if (color_of(w->left) == RB_BLACK && color_of(w->right) == RB_BLACK) {
                /* Case 2: black sibling, both nephews black -- recolor,
                 * push the extra black up to the parent. */
                w->color = RB_RED;
                x = x_parent;
                x_parent = x->parent;
            } else {
                if (color_of(w->right) == RB_BLACK) {
                    /* Case 3: near nephew red, far nephew black -- rotate
                     * at w to reshape into Case 4. */
                    w->left->color = RB_BLACK;
                    w->color = RB_RED;
                    rotate_right(t, w);
                    w = x_parent->right;
                }
                /* Case 4: far nephew red -- recolor and rotate; terminal. */
                w->color = x_parent->color;
                x_parent->color = RB_BLACK;
                w->right->color = RB_BLACK;
                rotate_left(t, x_parent);
                x = t->root;
            }
        } else {
            /* Mirror: x is a right child; sibling w is x_parent->left. */
            struct rb_node *w = x_parent->left;
            if (color_of(w) == RB_RED) {
                w->color = RB_BLACK;
                x_parent->color = RB_RED;
                rotate_right(t, x_parent);
                w = x_parent->left;
            }
            if (color_of(w->right) == RB_BLACK && color_of(w->left) == RB_BLACK) {
                w->color = RB_RED;
                x = x_parent;
                x_parent = x->parent;
            } else {
                if (color_of(w->left) == RB_BLACK) {
                    w->right->color = RB_BLACK;
                    w->color = RB_RED;
                    rotate_left(t, w);
                    w = x_parent->left;
                }
                w->color = x_parent->color;
                x_parent->color = RB_BLACK;
                w->left->color = RB_BLACK;
                rotate_right(t, x_parent);
                x = t->root;
            }
        }
    }
    if (x != NULL) {
        x->color = RB_BLACK;
    }
}

int rb_delete(rbtree_t *t, const char *key) {
    if (t == NULL || key == NULL) {
        return -1;
    }

    struct rb_node *z = find_node(t, key);
    if (z == NULL) {
        return -1;
    }

    struct rb_node *y = z;
    enum rb_color y_original_color = y->color;
    struct rb_node *x;
    struct rb_node *x_parent;

    if (z->left == NULL) {
        x = z->right;
        x_parent = z->parent;
        rb_transplant(t, z, z->right);
    } else if (z->right == NULL) {
        x = z->left;
        x_parent = z->parent;
        rb_transplant(t, z, z->left);
    } else {
        y = tree_minimum(z->right);
        y_original_color = y->color;
        x = y->right;
        if (y->parent == z) {
            x_parent = y;
        } else {
            x_parent = y->parent;
            rb_transplant(t, y, y->right);
            y->right = z->right;
            y->right->parent = y;
        }
        rb_transplant(t, z, y);
        y->left = z->left;
        y->left->parent = y;
        y->color = z->color;
    }

    rb_free(z->key);
    if (t->value_free != NULL) {
        t->value_free(z->value);
    }
    rb_free(z);
    t->size--;

    if (y_original_color == RB_BLACK) {
        delete_fixup(t, x, x_parent);
    }
    return 0;
}

size_t rb_size(const rbtree_t *t) {
    if (t == NULL) {
        return 0;
    }
    return t->size;
}

static void foreach_subtree(const struct rb_node *n,
                             void (*fn)(const char *key, void *value, void *ctx),
                             void *ctx) {
    if (n == NULL) {
        return;
    }
    foreach_subtree(n->left, fn, ctx);
    fn(n->key, n->value, ctx);
    foreach_subtree(n->right, fn, ctx);
}

void rb_foreach(const rbtree_t *t,
                void (*fn)(const char *key, void *value, void *ctx),
                void *ctx) {
    if (t == NULL || fn == NULL) {
        return;
    }
    foreach_subtree(t->root, fn, ctx);
}

#define RB_INVALID (-1)

/* Returns the subtree's black-height on success, RB_INVALID if any
 * red-black invariant, BST-ordering rule, or parent-pointer link is
 * violated anywhere in it. lo/hi are exclusive bounds inherited from
 * ancestor keys, NULL meaning unbounded. *count is incremented once per
 * visited node, so the caller can cross-check it against t->size
 * (Invariant 5, Section 9). */
static int validate_subtree(const struct rb_node *n, const struct rb_node *parent,
                             const char *lo, const char *hi, size_t *count) {
    if (n == NULL) {
        return 0;
    }
    (*count)++;
    if (n->parent != parent) {
        return RB_INVALID;
    }
    if ((lo != NULL && strcmp(n->key, lo) <= 0) ||
        (hi != NULL && strcmp(n->key, hi) >= 0)) {
        return RB_INVALID;
    }
    if (n->color == RB_RED &&
        (color_of(n->left) == RB_RED || color_of(n->right) == RB_RED)) {
        return RB_INVALID;
    }

    int lbh = validate_subtree(n->left, n, lo, n->key, count);
    if (lbh == RB_INVALID) {
        return RB_INVALID;
    }
    int rbh = validate_subtree(n->right, n, n->key, hi, count);
    if (rbh == RB_INVALID || rbh != lbh) {
        return RB_INVALID;
    }

    return lbh + (n->color == RB_BLACK ? 1 : 0);
}

int rb_validate(const rbtree_t *t) {
    if (t == NULL) {
        return 0; /* consistent with rb_size/rb_find: NULL means "empty" */
    }
    if (t->root != NULL && (t->root->parent != NULL || t->root->color != RB_BLACK)) {
        return -1;
    }
    size_t count = 0;
    if (validate_subtree(t->root, NULL, NULL, NULL, &count) == RB_INVALID) {
        return -1;
    }
    return count == t->size ? 0 : -1;
}
