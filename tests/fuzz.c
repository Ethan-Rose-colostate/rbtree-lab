/* Milestone 1, Slice 1 fuzz stub: not a real fuzzing strategy (no random
 * corpus or crash triage) -- just enough real work over `rbtree.c` that
 * `make test`/`make asan`/`make memcheck` exercise the code under
 * sanitizers/valgrind instead of trivially passing on an empty binary.
 */
#include "rbtree.h"

#include <stdio.h>
#include <stdlib.h>

static void free_value(void *value) {
    free(value);
}

int main(int argc, char **argv) {
    unsigned long iterations = 1000;
    if (argc > 1) {
        char *end = NULL;
        unsigned long parsed = strtoul(argv[1], &end, 10);
        if (end != argv[1] && *end == '\0') {
            iterations = parsed;
        }
    }

    rbtree_t *t = rb_create(free_value);
    if (t == NULL) {
        return 1;
    }

    for (unsigned long i = 0; i < iterations; i++) {
        char key[32];
        snprintf(key, sizeof key, "key-%lu", i % 512);

        int *value = malloc(sizeof *value);
        if (value == NULL) {
            rb_destroy(t);
            return 1;
        }
        *value = (int)i;

        if (rb_insert(t, key, value) != 0) {
            free(value);
            rb_destroy(t);
            return 1;
        }

        void *found = rb_find(t, key);
        if (found == NULL) {
            rb_destroy(t);
            return 1;
        }
    }

    rb_destroy(t);
    return 0;
}
