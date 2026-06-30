/**
 * fuzz_workload.c — Lightweight deterministic fuzz harness for the workload parser.
 *
 * Generates random workload files with valid and invalid instructions,
 * verifies the parser never crashes, leaks, or corrupts memory.
 * Run under AddressSanitizer for best results.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "workload.h"

static unsigned g_rng = 0xDEADBEEFU;

static unsigned rng_next(void) {
    g_rng = g_rng * 1103515245U + 12345U;
    return g_rng;
}

static const char *k_valid_ops[] = {
    "CPU", "IO", "SEND", "RECV", "SEM_WAIT", "SEM_POST", "LOCK", "UNLOCK", "EXIT",
};
static const char *k_garbage[] = {
    "",        "BOGUS",  "cpu",
    "io",      "1234",   "NULL",
    "CPU CPU", "CPU -1", "CPU 99999999999999999",
    "SEND",    "SEND 1", "RECV 1 2",
};

static void write_random_workload(const char *path) {
    FILE *fp = fopen(path, "w");
    assert(fp != NULL);
    unsigned lines = rng_next() % 150U;
    for (unsigned i = 0; i < lines; ++i) {
        unsigned choice = rng_next() % 100U;
        if (choice < 5U) {
            /* blank line */
            fprintf(fp, "\n");
        } else if (choice < 10U) {
            /* comment */
            fprintf(fp, "# random comment %u\n", rng_next());
        } else if (choice < 50U) {
            /* valid instruction */
            unsigned op = rng_next() % 9U;
            if (op == 0 || op == 1) {
                fprintf(fp, "%s %u\n", k_valid_ops[op], (rng_next() % 100U) + 1U);
            } else if (op == 2) {
                fprintf(fp, "SEND %u %d\n", rng_next() % 16U, (int)(rng_next() % 200U) - 100);
            } else if (op >= 3 && op <= 7) {
                fprintf(fp, "%s %u\n", k_valid_ops[op], rng_next() % 32U);
            } else {
                fprintf(fp, "EXIT\n");
            }
        } else {
            /* garbage */
            unsigned gi = rng_next() % (sizeof(k_garbage) / sizeof(k_garbage[0]));
            fprintf(fp, "%s\n", k_garbage[gi]);
        }
    }
    fclose(fp);
}

int main(int argc, char **argv) {
    unsigned iterations = 1000U;
    if (argc > 1) {
        iterations = (unsigned)atoi(argv[1]);
    }
    if (argc > 2) {
        g_rng = (unsigned)atoi(argv[2]);
    }

    const char *path = "fuzz_workload_input.wl";
    unsigned parse_ok = 0U;
    unsigned parse_fail = 0U;

    for (unsigned i = 0; i < iterations; ++i) {
        write_random_workload(path);
        workload_t workload;
        char err[256];
        int rc = workload_load(path, &workload, err, sizeof(err));
        if (rc == 0) {
            ++parse_ok;
            assert(workload.count > 0);
            assert(workload.insns[workload.count - 1].op == WORKLOAD_EXIT);
        } else {
            ++parse_fail;
        }
    }

    remove(path);
    printf("fuzz_workload: %u iterations, %u ok, %u rejected (no crashes)\n", iterations, parse_ok,
           parse_fail);
    return 0;
}
