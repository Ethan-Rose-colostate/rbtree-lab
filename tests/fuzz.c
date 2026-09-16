/* Randomized insert/find/delete fuzzer for rbtree.c.
 *
 * Each iteration picks a random operation (insert/find/delete) and a
 * random key from a bounded key space via a small local-state PRNG (not
 * rand()/srand()). A shadow table tracks the expected value/presence per
 * key, so every insert/find/delete result is checked against ground
 * truth, not just against rb_validate()'s structural invariants. Every
 * 100 ops (and once after the run), rb_validate() and a full rb_foreach()
 * traversal are checked: keys must come out in strictly increasing
 * strcmp order, every visited value must match the shadow, and the
 * number of nodes visited must match the shadow's live key count.
 *
 * assert() is deliberately not used anywhere in this file: -DNDEBUG
 * would compile it out and silently disable the checks. Every check is
 * an explicit `if` that reports the seed/iteration/op/key and returns 1,
 * so any failure is reproducible via `./fuzz <iterations> <seed>`.
 */
#include "rbtree.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { KEY_SPACE = 2048 };

static const uint32_t DEFAULT_SEED = 1234567891u;

typedef struct {
    uint32_t state;
} rng_t;

static uint32_t rng_next(rng_t *rng) {
    /* xorshift32; must never be seeded with 0 (fixed point) */
    uint32_t x = rng->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng->state = x;
    return x;
}

static unsigned long rng_below(rng_t *rng, unsigned long bound) {
    return rng_next(rng) % bound; /* small modulo bias is fine for fuzzing */
}

typedef struct {
    int value;
    bool present;
} shadow_slot_t;

static void free_value(void *value) {
    free(value);
}

static int report_failure(uint32_t seed, unsigned long iter, const char *op,
                           const char *key) {
    fprintf(stderr, "fuzz: FAILURE seed=%u iteration=%lu op=%s key=%s\n",
            (unsigned)seed, iter, op, key);
    return 1;
}

typedef struct {
    const shadow_slot_t *shadow;
    unsigned long visited;
    bool has_prev;
    char prev_key[32];
    bool ok;
    char bad_key[32];
    const char *reason;
} foreach_ctx_t;

static void foreach_check_cb(const char *key, void *value, void *ctx_) {
    foreach_ctx_t *ctx = ctx_;
    if (!ctx->ok) {
        return; /* already failed; rb_foreach has no early-stop mechanism */
    }

    if (ctx->has_prev && strcmp(ctx->prev_key, key) >= 0) {
        ctx->ok = false;
        ctx->reason = "foreach-order";
        snprintf(ctx->bad_key, sizeof ctx->bad_key, "%s", key);
        return;
    }

    unsigned long k = 0;
    if (sscanf(key, "key-%lu", &k) != 1 || k >= (unsigned long)KEY_SPACE ||
        !ctx->shadow[k].present || *(int *)value != ctx->shadow[k].value) {
        ctx->ok = false;
        ctx->reason = "foreach-value";
        snprintf(ctx->bad_key, sizeof ctx->bad_key, "%s", key);
        return;
    }

    ctx->visited++;
    snprintf(ctx->prev_key, sizeof ctx->prev_key, "%s", key);
    ctx->has_prev = true;
}

static int periodic_check(rbtree_t *t, const shadow_slot_t *shadow,
                           size_t shadow_count, uint32_t seed,
                           unsigned long iter) {
    if (rb_validate(t) != 0) {
        return report_failure(seed, iter, "validate", "-");
    }

    foreach_ctx_t ctx = {.shadow = shadow, .ok = true};
    rb_foreach(t, foreach_check_cb, &ctx);

    if (!ctx.ok) {
        return report_failure(seed, iter, ctx.reason, ctx.bad_key);
    }
    if (ctx.visited != shadow_count) {
        return report_failure(seed, iter, "foreach-count", "-");
    }
    return 0;
}

int main(int argc, char **argv) {
    unsigned long iterations = 100000;
    if (argc > 1) {
        char *end = NULL;
        unsigned long parsed = strtoul(argv[1], &end, 10);
        if (end != argv[1] && *end == '\0') {
            iterations = parsed;
        }
    }

    uint32_t seed = DEFAULT_SEED;
    if (argc > 2) {
        char *end = NULL;
        unsigned long parsed = strtoul(argv[2], &end, 10);
        if (end != argv[2] && *end == '\0') {
            seed = (uint32_t)parsed;
        }
    }
    if (seed == 0) {
        seed = 1; /* xorshift32 is stuck at 0 */
    }
    fprintf(stderr, "fuzz: seed=%u iterations=%lu\n", (unsigned)seed,
            iterations);

    rng_t rng = {.state = seed};
    shadow_slot_t shadow[KEY_SPACE] = {0};
    size_t shadow_count = 0;

    rbtree_t *t = rb_create(free_value);
    if (t == NULL) {
        return 1;
    }

    /* invariant: shadow_count always equals the number of shadow[] slots
     * with present == true, which is what makes the rb_size/visited
     * checks below meaningful ground truth rather than self-referential */
    for (unsigned long i = 0; i < iterations; i++) {
        unsigned long op = rng_below(&rng, 3); /* 0=insert 1=find 2=delete */
        unsigned long k = rng_below(&rng, KEY_SPACE);
        char key[32];
        snprintf(key, sizeof key, "key-%lu", k);

        if (op == 0) {
            int *value = malloc(sizeof *value);
            if (value == NULL) {
                rb_destroy(t);
                return report_failure(seed, i, "insert-alloc", key);
            }
            *value = (int)i;

            if (rb_insert(t, key, value) != 0) {
                free(value);
                rb_destroy(t);
                return report_failure(seed, i, "insert", key);
            }

            void *found = rb_find(t, key);
            if (found == NULL || *(int *)found != (int)i) {
                rb_destroy(t);
                return report_failure(seed, i, "insert-echo", key);
            }

            if (!shadow[k].present) {
                shadow_count++;
            }
            shadow[k].present = true;
            shadow[k].value = (int)i;
        } else if (op == 1) {
            void *found = rb_find(t, key);
            bool ok = shadow[k].present
                          ? (found != NULL && *(int *)found == shadow[k].value)
                          : (found == NULL);
            if (!ok) {
                rb_destroy(t);
                return report_failure(seed, i, "find", key);
            }
        } else {
            int rc = rb_delete(t, key);
            if (shadow[k].present) {
                if (rc != 0) {
                    rb_destroy(t);
                    return report_failure(seed, i, "delete", key);
                }
                shadow[k].present = false;
                shadow_count--;
            } else if (rc != -1) {
                rb_destroy(t);
                return report_failure(seed, i, "delete", key);
            }
        }

        if (rb_size(t) != shadow_count) {
            rb_destroy(t);
            return report_failure(seed, i, "size", key);
        }

        if ((i + 1) % 100 == 0 &&
            periodic_check(t, shadow, shadow_count, seed, i) != 0) {
            rb_destroy(t);
            return 1;
        }
    }

    if (periodic_check(t, shadow, shadow_count, seed, iterations) != 0) {
        rb_destroy(t);
        return 1;
    }

    rb_destroy(t);
    return 0;
}
