/* Milestone 1, Slice 1 unit tests: rb_create, plain-BST rb_insert (no
 * fixups/rotations), rb_find, rb_destroy, rb_size.
 *
 * Deliberately out of scope this slice: fault-injection tests for the
 * "malloc fails -> tree unchanged, value not consumed" contract. Building
 * that harness (fail-after-N-allocations hook, or link-time wrapping) is a
 * nontrivial addition on its own and the Makefile isn't being touched to
 * support it. The rb_malloc/rb_free seam in src/rbtree.c keeps this
 * possible later without being required now.
 *
 * Also out of scope: rb_delete, rb_foreach, rb_validate are not exercised
 * here; they only need to compile/link safely against their Slice-1 stubs.
 */
#include "rbtree.h"

#include <assert.h>
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

    printf("ALL TESTS PASSED\n");
    return 0;
}
