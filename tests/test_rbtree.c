/* Milestone 1 unit tests: rb_create, rb_insert (with rotations/fixup),
 * rb_find, rb_destroy, rb_size, rb_validate, and rb_foreach.
 *
 * rb_validate-based tests use deliberately constructed insertion
 * sequences to check red-black invariants (including the node-count vs.
 * rb_size cross-check, Invariant 5 / Section 9) after fixup/rotation.
 *
 * Deliberately out of scope: fault-injection tests for the "malloc fails
 * -> tree unchanged, value not consumed" contract. Building that harness
 * (fail-after-N-allocations hook, or link-time wrapping) is a nontrivial
 * addition on its own and the Makefile isn't being touched to support it.
 * The rb_malloc/rb_free seam in src/rbtree.c keeps this possible later
 * without being required now.
 *
 * Milestone 2 adds rb_delete coverage via a small table-driven harness
 * (run_ops): a sequence of insert/delete ops is applied one at a time,
 * asserting rb_validate(t) == 0 and the expected rb_size after every
 * single op, so a fixup bug is caught at the exact op that breaks it.
 */
#include "rbtree.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- value_free stub used by several tests to observe frees ---- */

static int   free_calls;
static void *last_freed;

static void counting_free(void *value) {
    free_calls++;
    last_freed = value;
}

static void reset_counting_free(void) {
    free_calls = 0;
    last_freed = NULL;
}

/* value_free stub that actually releases a heap block and counts it,
 * used to check rb_destroy releases every stored value exactly once. */
static int free_backed_calls;

static void free_backed_free(void *value) {
    free_backed_calls++;
    free(value);
}

static void test_create_basic(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    /* Explicit, standalone assertion right after rb_create. */
    assert(rb_size(t) == 0);

    rb_destroy(t);
}

static void test_insert_find_single(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    int value = 42;
    assert(rb_insert(t, "hello", &value) == 0);

    assert(rb_find(t, "hello") == &value);
    assert(rb_size(t) == 1);

    rb_destroy(t);
}

static void test_find_missing_on_empty(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    assert(rb_find(t, "missing") == NULL);

    rb_destroy(t);
}

static void test_find_missing_key(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    int a = 1, b = 2;
    assert(rb_insert(t, "alpha", &a) == 0);
    assert(rb_insert(t, "beta", &b) == 0);

    assert(rb_find(t, "gamma") == NULL);

    rb_destroy(t);
}

static void test_insert_duplicate_overwrites(void) {
    reset_counting_free();
    rbtree_t *t = rb_create(counting_free);
    assert(t != NULL);

    int v1 = 1, v2 = 2;
    assert(rb_insert(t, "a", &v1) == 0);
    assert(rb_size(t) == 1);

    assert(rb_insert(t, "a", &v2) == 0);

    /* value_free must have been called exactly once, on the OLD value. */
    assert(free_calls == 1);
    assert(last_freed == &v1);

    assert(rb_find(t, "a") == &v2);
    assert(rb_size(t) == 1);

    rb_destroy(t);
}

static void test_insert_many_bst_correctness(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    /* Mixed, non-sorted order so inserts branch both left and right at
     * varying depths without relying on balancing. */
    const char *keys[] = {
        "m", "f", "t", "b", "h", "p", "z",
        "a", "d", "g", "j", "n", "r", "x",
        "m", /* duplicate, must not add a new entry */
    };
    int values[15];
    for (size_t i = 0; i < 15; i++) {
        values[i] = (int)i;
    }

    for (size_t i = 0; i < 15; i++) {
        assert(rb_insert(t, keys[i], &values[i]) == 0);
    }

    /* 14 distinct keys ("m" repeated once). */
    assert(rb_size(t) == 14);

    /* Every distinct key resolves; "m" resolves to the LAST value stored
     * for it (the duplicate-overwrite semantics). */
    assert(rb_find(t, "f") == &values[1]);
    assert(rb_find(t, "t") == &values[2]);
    assert(rb_find(t, "b") == &values[3]);
    assert(rb_find(t, "h") == &values[4]);
    assert(rb_find(t, "p") == &values[5]);
    assert(rb_find(t, "z") == &values[6]);
    assert(rb_find(t, "a") == &values[7]);
    assert(rb_find(t, "d") == &values[8]);
    assert(rb_find(t, "g") == &values[9]);
    assert(rb_find(t, "j") == &values[10]);
    assert(rb_find(t, "n") == &values[11]);
    assert(rb_find(t, "r") == &values[12]);
    assert(rb_find(t, "x") == &values[13]);
    assert(rb_find(t, "m") == &values[14]);

    rb_destroy(t);
}

static void test_destroy_null_safe(void) {
    rb_destroy(NULL);
    /* Reaching here without crashing is the assertion. */
    assert(1);
}

static void test_destroy_frees_everything(void) {
    free_backed_calls = 0;
    rbtree_t *t = rb_create(free_backed_free);
    assert(t != NULL);

    const char *keys[] = {"one", "two", "three", "four", "five"};
    for (size_t i = 0; i < 5; i++) {
        int *v = malloc(sizeof *v);
        assert(v != NULL);
        *v = (int)i;
        assert(rb_insert(t, keys[i], v) == 0);
    }

    rb_destroy(t);

    /* This is a proxy (call count), not a substitute for the real
     * leak/double-free check that `make memcheck`/`make asan` perform. */
    assert(free_backed_calls == 5);
}

static void test_size_tracking(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    const char *keys[] = {"k1", "k2", "k3", "k4"};
    int values[4] = {0, 1, 2, 3};

    for (size_t i = 0; i < 4; i++) {
        assert(rb_insert(t, keys[i], &values[i]) == 0);
        assert(rb_size(t) == i + 1);
    }

    /* Duplicate insert must not change size. */
    assert(rb_insert(t, "k1", &values[0]) == 0);
    assert(rb_size(t) == 4);

    rb_destroy(t);
}

static void test_value_not_consumed_on_success(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    int v = 7;
    assert(rb_insert(t, "a", &v) == 0);

    /* The tree stores exactly the pointer handed to it. */
    assert(rb_find(t, "a") == &v);

    rb_destroy(t);
}

/* ---- Milestone 2: rotation/fixup correctness tests ---- */

static void test_fixup_recolor_uncle_red(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    /* "m" root black; "d","t" red children (no violation yet); "c" red
     * under "d" triggers Case 1 (red uncle "t") -- recolor only. */
    assert(rb_insert(t, "m", (void *)1) == 0);
    assert(rb_insert(t, "d", (void *)2) == 0);
    assert(rb_insert(t, "t", (void *)3) == 0);
    assert(rb_insert(t, "c", (void *)4) == 0);

    assert(rb_validate(t) == 0);
    assert(rb_find(t, "m") == (void *)1);
    assert(rb_find(t, "d") == (void *)2);
    assert(rb_find(t, "t") == (void *)3);
    assert(rb_find(t, "c") == (void *)4);

    rb_destroy(t);
}

static void test_fixup_left_left_rotation_only(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    /* "c","b","a" is a straight left-left line -> Case 3 rotate_right
     * at the (then-current) root "c". */
    assert(rb_insert(t, "c", (void *)1) == 0);
    assert(rb_insert(t, "b", (void *)2) == 0);
    assert(rb_insert(t, "a", (void *)3) == 0);

    assert(rb_validate(t) == 0);
    assert(rb_find(t, "a") == (void *)3);
    assert(rb_find(t, "b") == (void *)2);
    assert(rb_find(t, "c") == (void *)1);

    rb_destroy(t);
}

static void test_fixup_left_right_rotation(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    /* "c","a","b" is a left-right triangle -> Case 2 rotate_left at "a"
     * then Case 3 rotate_right at the (then-current) root "c". */
    assert(rb_insert(t, "c", (void *)1) == 0);
    assert(rb_insert(t, "a", (void *)2) == 0);
    assert(rb_insert(t, "b", (void *)3) == 0);

    assert(rb_validate(t) == 0);
    assert(rb_find(t, "a") == (void *)2);
    assert(rb_find(t, "b") == (void *)3);
    assert(rb_find(t, "c") == (void *)1);

    rb_destroy(t);
}

static void test_fixup_right_right_rotation_only(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    /* "a","b","c" is a straight right-right line -> mirror Case 3
     * rotate_left at the (then-current) root "a". */
    assert(rb_insert(t, "a", (void *)1) == 0);
    assert(rb_insert(t, "b", (void *)2) == 0);
    assert(rb_insert(t, "c", (void *)3) == 0);

    assert(rb_validate(t) == 0);
    assert(rb_find(t, "a") == (void *)1);
    assert(rb_find(t, "b") == (void *)2);
    assert(rb_find(t, "c") == (void *)3);

    rb_destroy(t);
}

static void test_fixup_right_left_rotation(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    /* "a","c","b" is a right-left triangle -> mirror Case 2 rotate_right
     * at "c" then mirror Case 3 rotate_left at the (then-current) root "a". */
    assert(rb_insert(t, "a", (void *)1) == 0);
    assert(rb_insert(t, "c", (void *)2) == 0);
    assert(rb_insert(t, "b", (void *)3) == 0);

    assert(rb_validate(t) == 0);
    assert(rb_find(t, "a") == (void *)1);
    assert(rb_find(t, "b") == (void *)3);
    assert(rb_find(t, "c") == (void *)2);

    rb_destroy(t);
}

static void test_fixup_recolor_then_rotation_further_up(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    /* Chains a Case-1 recolor with a rotation higher up. Correctness of
     * this test rests on rb_validate(t) == 0, not on precisely which
     * case fires at each step. */
    const char *keys[] = {"e", "c", "g", "b", "d", "a"};
    for (size_t i = 0; i < 6; i++) {
        assert(rb_insert(t, keys[i], (void *)(i + 1)) == 0);
        assert(rb_validate(t) == 0);
    }

    for (size_t i = 0; i < 6; i++) {
        assert(rb_find(t, keys[i]) == (void *)(i + 1));
    }

    rb_destroy(t);
}

static void test_fixup_random_deterministic_large(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    enum { N = 300 };
    static char keys[N][16];
    for (int i = 0; i < N; i++) {
        snprintf(keys[i], sizeof keys[i], "key-%d", i);
    }

    /* Fixed, reproducible shuffle via a constant-seeded LCG -- not real
     * randomness, so the test is deterministic across runs. */
    unsigned long state = 12345;
    for (int i = N - 1; i > 0; i--) {
        state = state * 1103515245UL + 12345UL;
        int j = (int)(state % (unsigned long)(i + 1));
        char tmp[16];
        memcpy(tmp, keys[i], sizeof tmp);
        memcpy(keys[i], keys[j], sizeof tmp);
        memcpy(keys[j], tmp, sizeof tmp);
    }

    for (int i = 0; i < N; i++) {
        assert(rb_insert(t, keys[i], (void *)(intptr_t)i) == 0);
    }

    assert(rb_validate(t) == 0);
    assert(rb_size(t) == (size_t)N);

    for (int i = 0; i < N; i++) {
        assert(rb_find(t, keys[i]) == (void *)(intptr_t)i);
    }

    rb_destroy(t);
}

static void test_rotation_at_root_updates_root(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    /* "c","b","a" forces a rotate_right exactly at the (then-current)
     * root "c", making "b" the new root. If t->root weren't updated by
     * the rotation, later traversal would still start from the old
     * root object "c" (now demoted to a child), and finds for keys
     * reachable only through the real new root would fail. */
    assert(rb_insert(t, "c", (void *)1) == 0);
    assert(rb_insert(t, "b", (void *)2) == 0);
    assert(rb_insert(t, "a", (void *)3) == 0);

    /* Populate both of the new root's subtrees with more than one node
     * each, so a stale t->root would break finds on both sides. */
    assert(rb_insert(t, "d", (void *)4) == 0); /* right subtree (under c) */
    assert(rb_insert(t, "A", (void *)5) == 0); /* left subtree (under a) */

    assert(rb_validate(t) == 0);
    assert(rb_find(t, "A") == (void *)5);
    assert(rb_find(t, "a") == (void *)3);
    assert(rb_find(t, "b") == (void *)2);
    assert(rb_find(t, "c") == (void *)1);
    assert(rb_find(t, "d") == (void *)4);

    rb_destroy(t);
}

static const char *foreach_prev_key;
static int          foreach_order_ok;
static int          foreach_visit_count;

static void foreach_check_ascending(const char *key, void *value, void *ctx) {
    (void)value;
    (void)ctx;
    foreach_visit_count++;
    if (foreach_prev_key != NULL && strcmp(foreach_prev_key, key) >= 0) {
        foreach_order_ok = 0;
    }
    foreach_prev_key = key;
}

static void test_foreach_ascending_order(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    const char *keys[] = {"m", "f", "t", "b", "h", "p", "z", "a", "d"};
    for (size_t i = 0; i < 9; i++) {
        assert(rb_insert(t, keys[i], NULL) == 0);
    }

    foreach_prev_key = NULL;
    foreach_order_ok = 1;
    foreach_visit_count = 0;
    rb_foreach(t, foreach_check_ascending, NULL);

    assert(foreach_order_ok);
    assert(foreach_visit_count == 9);

    rb_destroy(t);
}

static void test_foreach_empty_and_null_safe(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    foreach_visit_count = 0;
    rb_foreach(t, foreach_check_ascending, NULL);
    assert(foreach_visit_count == 0);

    rb_foreach(NULL, foreach_check_ascending, NULL);
    rb_foreach(t, NULL, NULL);
    /* Reaching here without crashing is the assertion. */
    assert(1);

    rb_destroy(t);
}

/* ---- Milestone 2: table-driven rb_delete tests ---- */

typedef struct {
    char        op;     /* 'i' insert, 'd' delete */
    const char *key;
    int         expect; /* expected rb_insert/rb_delete return value */
} rb_op;

/* Applies a sequence of insert/delete ops one at a time, asserting
 * rb_validate(t) == 0 and the expected size after every single op, so a
 * fixup bug is caught at the exact op that introduces it. Every insert in
 * these tables uses a key not already present, so a successful op always
 * changes size by exactly +-1. */
static void run_ops(rbtree_t *t, const rb_op *ops, size_t n) {
    size_t size = rb_size(t);
    for (size_t i = 0; i < n; i++) {
        int rc = (ops[i].op == 'i') ? rb_insert(t, ops[i].key, NULL)
                                     : rb_delete(t, ops[i].key);
        assert(rc == ops[i].expect);
        if (rc == 0) {
            if (ops[i].op == 'i') {
                size++;
            } else {
                size--;
            }
        }
        assert(rb_size(t) == size);
        assert(rb_validate(t) == 0);
    }
}

static void test_delete_empty_tree(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    assert(rb_delete(t, "missing") == -1);
    assert(rb_size(t) == 0);
    assert(rb_validate(t) == 0);

    rb_destroy(t);
}

static void test_delete_missing_key_leaves_tree_unchanged(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    const rb_op ops[] = {
        {'i', "b", 0}, {'i', "a", 0}, {'i', "c", 0},
        {'d', "z", -1}, /* absent key: tree unchanged */
    };
    run_ops(t, ops, sizeof ops / sizeof ops[0]);

    assert(rb_size(t) == 3);

    rb_destroy(t);
}

static void test_delete_null_args(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);
    assert(rb_insert(t, "a", NULL) == 0);

    assert(rb_delete(NULL, "a") == -1);
    assert(rb_delete(t, NULL) == -1);
    assert(rb_size(t) == 1);

    rb_destroy(t);
}

static void test_delete_only_node_empties_tree(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    const rb_op ops[] = {{'i', "a", 0}, {'d', "a", 0}};
    run_ops(t, ops, sizeof ops / sizeof ops[0]);

    assert(rb_find(t, "a") == NULL);
    assert(rb_size(t) == 0);

    rb_destroy(t);
}

static void test_delete_red_leaf_no_fixup(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    /* "b" root black, "a"/"c" red leaves; deleting a red leaf never
     * triggers delete_fixup (its original color was red). */
    const rb_op ops[] = {
        {'i', "b", 0}, {'i', "a", 0}, {'i', "c", 0},
        {'d', "a", 0},
    };
    run_ops(t, ops, sizeof ops / sizeof ops[0]);

    assert(rb_find(t, "a") == NULL);
    assert(rb_size(t) == 2);

    rb_destroy(t);
}

static void test_delete_node_with_only_left_child(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    /* "b" root black with only a red left child "a"; deleting "b" takes
     * the z->right == NULL splice path and promotes "a" to root. */
    const rb_op ops[] = {{'i', "b", 0}, {'i', "a", 0}, {'d', "b", 0}};
    run_ops(t, ops, sizeof ops / sizeof ops[0]);

    assert(rb_find(t, "b") == NULL);
    assert(rb_size(t) == 1);

    rb_destroy(t);
}

static void test_delete_node_with_only_right_child(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    /* Mirror: "a" root black with only a red right child "b"; deleting
     * "a" takes the z->left == NULL splice path. */
    const rb_op ops[] = {{'i', "a", 0}, {'i', "b", 0}, {'d', "a", 0}};
    run_ops(t, ops, sizeof ops / sizeof ops[0]);

    assert(rb_find(t, "a") == NULL);
    assert(rb_size(t) == 1);

    rb_destroy(t);
}

static void test_delete_two_children_successor_is_right_child(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    /* "b" root black, "a"/"c" red leaves. z="b" has two children and its
     * successor is z->right ("c") directly, i.e. the y->parent == z
     * splice path in rb_delete. Successor "c" is red, so no fixup fires
     * here; this test is purely about the transplant plumbing. */
    const rb_op ops[] = {
        {'i', "b", 0}, {'i', "a", 0}, {'i', "c", 0},
        {'d', "b", 0},
    };
    run_ops(t, ops, sizeof ops / sizeof ops[0]);

    assert(rb_find(t, "b") == NULL);
    assert(rb_size(t) == 2);

    rb_destroy(t);
}

static void test_delete_two_children_successor_is_deeper(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    /* Inserting b,a,d,c produces b(B) root, a(B) left leaf, d(B) right,
     * c(R) as d's left child (insert-fixup case 1 on "c"). Deleting "b"
     * (two children) finds its successor at tree_minimum(d) == "c",
     * which is NOT z->right directly -- the y->parent != z splice path.
     * Successor "c" is red, so again no delete_fixup fires; this test
     * targets the deeper-successor transplant plumbing specifically. */
    const rb_op ops[] = {
        {'i', "b", 0}, {'i', "a", 0}, {'i', "d", 0}, {'i', "c", 0},
        {'d', "b", 0},
    };
    run_ops(t, ops, sizeof ops / sizeof ops[0]);

    assert(rb_find(t, "b") == NULL);
    assert(rb_size(t) == 3);

    rb_destroy(t);
}

static void test_delete_fixup_case4_left(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    /* Inserting b,a,c,d produces b(B) root, a(B) left leaf, c(B) right,
     * d(R) as c's right child (insert-fixup case 1 recolors a and c
     * black). Deleting "a" makes x=NULL, x_parent=b, sibling w=c black
     * with far nephew d red -- delete_fixup Case 4 (left), which
     * terminates the loop in one step via rotate_left at b. */
    const rb_op ops[] = {
        {'i', "b", 0}, {'i', "a", 0}, {'i', "c", 0}, {'i', "d", 0},
        {'d', "a", 0},
    };
    run_ops(t, ops, sizeof ops / sizeof ops[0]);

    assert(rb_find(t, "a") == NULL);
    assert(rb_size(t) == 3);

    rb_destroy(t);
}

static void test_delete_fixup_case4_right(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    /* Mirror: inserting c,d,b,a produces c(B) root, b(B) left [a(R) as
     * b's left child], d(B) right leaf. Deleting "d" makes x=NULL,
     * x_parent=c, sibling w=b black with far nephew a red --
     * delete_fixup Case 4 (right/mirror). */
    const rb_op ops[] = {
        {'i', "c", 0}, {'i', "d", 0}, {'i', "b", 0}, {'i', "a", 0},
        {'d', "d", 0},
    };
    run_ops(t, ops, sizeof ops / sizeof ops[0]);

    assert(rb_find(t, "d") == NULL);
    assert(rb_size(t) == 3);

    rb_destroy(t);
}

static void test_delete_fixup_case2_left_propagates_to_root(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    /* Inserting d,b,f,a,c,e,g produces the perfect tree d(B) root,
     * b(B)/f(B) children each with two red leaves. Deleting the four red
     * leaves (no fixup) turns b and f into black leaves; deleting "b"
     * then makes x=NULL, x_parent=d, sibling w=f black with both
     * nephews black (NULL) -- delete_fixup Case 2 (left), which recolors
     * f red and pushes x up to the root, terminating the loop. */
    const rb_op ops[] = {
        {'i', "d", 0}, {'i', "b", 0}, {'i', "f", 0},
        {'i', "a", 0}, {'i', "c", 0}, {'i', "e", 0}, {'i', "g", 0},
        {'d', "a", 0}, {'d', "c", 0}, {'d', "e", 0}, {'d', "g", 0},
        {'d', "b", 0},
    };
    run_ops(t, ops, sizeof ops / sizeof ops[0]);

    assert(rb_find(t, "b") == NULL);
    assert(rb_size(t) == 2);

    rb_destroy(t);
}

static void test_delete_fixup_case2_right_propagates_to_root(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    /* Mirror of the above: same starting tree, but delete "f" last
     * instead of "b" -- x_parent=d, sibling w=b black with both nephews
     * black -- delete_fixup Case 2 (right/mirror). */
    const rb_op ops[] = {
        {'i', "d", 0}, {'i', "b", 0}, {'i', "f", 0},
        {'i', "a", 0}, {'i', "c", 0}, {'i', "e", 0}, {'i', "g", 0},
        {'d', "a", 0}, {'d', "c", 0}, {'d', "e", 0}, {'d', "g", 0},
        {'d', "f", 0},
    };
    run_ops(t, ops, sizeof ops / sizeof ops[0]);

    assert(rb_find(t, "f") == NULL);
    assert(rb_size(t) == 2);

    rb_destroy(t);
}

static void test_delete_root_triggers_fixup(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    /* Same case-4 shape as test_delete_fixup_case4_left, but this time
     * delete the ROOT's sibling-bearing side by deleting "b" won't hit
     * fixup (two children, red successor) -- instead delete "a" (as
     * before) then delete the resulting new root "c" to confirm a
     * second fixup-triggering delete at the root still validates. */
    const rb_op ops[] = {
        {'i', "b", 0}, {'i', "a", 0}, {'i', "c", 0}, {'i', "d", 0},
        {'d', "a", 0}, /* case 4 left, as above; new root becomes "c" */
        {'d', "c", 0}, /* deletes the (new) root itself */
    };
    run_ops(t, ops, sizeof ops / sizeof ops[0]);

    assert(rb_find(t, "a") == NULL);
    assert(rb_find(t, "c") == NULL);
    assert(rb_size(t) == 2);

    rb_destroy(t);
}

static void test_delete_value_free_called_once_on_success(void) {
    reset_counting_free();
    rbtree_t *t = rb_create(counting_free);
    assert(t != NULL);

    int v = 7;
    assert(rb_insert(t, "a", &v) == 0);

    assert(rb_delete(t, "a") == 0);
    assert(free_calls == 1);
    assert(last_freed == &v);
    assert(rb_find(t, "a") == NULL);

    rb_destroy(t);
}

static void test_delete_value_free_not_called_on_missing_key(void) {
    reset_counting_free();
    rbtree_t *t = rb_create(counting_free);
    assert(t != NULL);

    int v = 7;
    assert(rb_insert(t, "a", &v) == 0);

    assert(rb_delete(t, "missing") == -1);
    assert(free_calls == 0);
    assert(rb_find(t, "a") == &v);

    rb_destroy(t);
}

static void test_delete_frees_key_copy_and_value(void) {
    free_backed_calls = 0;
    rbtree_t *t = rb_create(free_backed_free);
    assert(t != NULL);

    const char *keys[] = {"one", "two", "three", "four", "five"};
    for (size_t i = 0; i < 5; i++) {
        int *v = malloc(sizeof *v);
        assert(v != NULL);
        *v = (int)i;
        assert(rb_insert(t, keys[i], v) == 0);
    }

    for (size_t i = 0; i < 5; i++) {
        assert(rb_delete(t, keys[i]) == 0);
        assert(rb_validate(t) == 0);
    }

    /* Proxy check (call count); make asan/make memcheck confirm no leak
     * or double-free of the key copy or the value. */
    assert(free_backed_calls == 5);
    assert(rb_size(t) == 0);

    rb_destroy(t);
}

static void test_delete_size_tracking(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    const rb_op ops[] = {
        {'i', "k1", 0}, {'i', "k2", 0}, {'i', "k3", 0}, {'i', "k4", 0},
        {'d', "k2", 0}, {'d', "k4", 0},
        {'d', "k2", -1}, /* already gone: size must not change */
        {'d', "k1", 0}, {'d', "k3", 0},
        {'d', "k3", -1}, /* tree now empty */
    };
    run_ops(t, ops, sizeof ops / sizeof ops[0]);

    assert(rb_size(t) == 0);

    rb_destroy(t);
}

static void test_delete_random_deterministic_large(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);

    enum { N = 300 };
    static char keys[N][16];
    for (int i = 0; i < N; i++) {
        snprintf(keys[i], sizeof keys[i], "key-%d", i);
    }

    /* Same fixed-seed LCG shuffle style as
     * test_fixup_random_deterministic_large, applied twice: once for
     * insertion order, once (with a different seed) for deletion order,
     * so the tree shape at each delete isn't just insertion order
     * reversed. Both permutations are deterministic and reproducible.
     * Correctness of this test rests entirely on rb_validate(t) == 0
     * after every single delete, not on which delete_fixup case fires
     * at each step -- across a run like this every case (and its
     * mirror) fires many times. */
    unsigned long state = 12345;
    for (int i = N - 1; i > 0; i--) {
        state = state * 1103515245UL + 12345UL;
        int j = (int)(state % (unsigned long)(i + 1));
        char tmp[16];
        memcpy(tmp, keys[i], sizeof tmp);
        memcpy(keys[i], keys[j], sizeof tmp);
        memcpy(keys[j], tmp, sizeof tmp);
    }
    for (int i = 0; i < N; i++) {
        assert(rb_insert(t, keys[i], (void *)(intptr_t)i) == 0);
    }
    assert(rb_validate(t) == 0);
    assert(rb_size(t) == (size_t)N);

    state = 99999;
    for (int i = N - 1; i > 0; i--) {
        state = state * 1103515245UL + 12345UL;
        int j = (int)(state % (unsigned long)(i + 1));
        char tmp[16];
        memcpy(tmp, keys[i], sizeof tmp);
        memcpy(keys[i], keys[j], sizeof tmp);
        memcpy(keys[j], tmp, sizeof tmp);
    }
    for (int i = 0; i < N; i++) {
        assert(rb_delete(t, keys[i]) == 0);
        assert(rb_validate(t) == 0);
        assert(rb_size(t) == (size_t)(N - 1 - i));
    }

    for (int i = 0; i < N; i++) {
        assert(rb_find(t, keys[i]) == NULL);
    }
    assert(rb_size(t) == 0);

    rb_destroy(t);
}

int main(void) {
    test_create_basic();
    test_insert_find_single();
    test_find_missing_on_empty();
    test_find_missing_key();
    test_insert_duplicate_overwrites();
    test_insert_many_bst_correctness();
    test_destroy_null_safe();
    test_destroy_frees_everything();
    test_size_tracking();
    test_value_not_consumed_on_success();

    test_fixup_recolor_uncle_red();
    test_fixup_left_left_rotation_only();
    test_fixup_left_right_rotation();
    test_fixup_right_right_rotation_only();
    test_fixup_right_left_rotation();
    test_fixup_recolor_then_rotation_further_up();
    test_fixup_random_deterministic_large();
    test_rotation_at_root_updates_root();
    test_foreach_ascending_order();
    test_foreach_empty_and_null_safe();

    test_delete_empty_tree();
    test_delete_missing_key_leaves_tree_unchanged();
    test_delete_null_args();
    test_delete_only_node_empties_tree();
    test_delete_red_leaf_no_fixup();
    test_delete_node_with_only_left_child();
    test_delete_node_with_only_right_child();
    test_delete_two_children_successor_is_right_child();
    test_delete_two_children_successor_is_deeper();
    test_delete_fixup_case4_left();
    test_delete_fixup_case4_right();
    test_delete_fixup_case2_left_propagates_to_root();
    test_delete_fixup_case2_right_propagates_to_root();
    test_delete_root_triggers_fixup();
    test_delete_value_free_called_once_on_success();
    test_delete_value_free_not_called_on_missing_key();
    test_delete_frees_key_copy_and_value();
    test_delete_size_tracking();
    test_delete_random_deterministic_large();

    printf("ALL TESTS PASSED\n");
    return 0;
}
