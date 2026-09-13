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
 * Also out of scope: rb_delete is Milestone 2 and is not exercised here;
 * it only needs to compile/link safely against its stub.
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

    printf("ALL TESTS PASSED\n");
    return 0;
}
