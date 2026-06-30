/**
 * test_workload.c — Workload parser unit tests.
 *
 * Covers:
 *   - Successful load of all workload types
 *   - Auto-appended EXIT instruction
 *   - Opcode name lookup
 *   - Error paths: nonexistent file, invalid instructions
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "workload.h"

static void test_cpu_bound(void) {
    workload_t workload;
    char err[160];
    assert(workload_load("workloads/cpu_bound.wl", &workload, err, sizeof(err)) == 0);
    assert(workload.count >= 2U);
    assert(workload.insns[0].op == WORKLOAD_CPU);
    assert(workload.insns[0].ticks > 0U);
    assert(workload.insns[workload.count - 1U].op == WORKLOAD_EXIT);
}

static void test_io_bound(void) {
    workload_t workload;
    char err[160];
    assert(workload_load("workloads/io_bound.wl", &workload, err, sizeof(err)) == 0);
    assert(workload.insns[1].op == WORKLOAD_IO);
}

static void test_mixed(void) {
    workload_t workload;
    char err[160];
    assert(workload_load("workloads/mixed.wl", &workload, err, sizeof(err)) == 0);
    assert(workload.count > 2U);
}

static void test_producer(void) {
    workload_t workload;
    char err[160];
    assert(workload_load("workloads/producer.wl", &workload, err, sizeof(err)) == 0);
    /* Should contain SEND instructions */
    int found_send = 0;
    for (size_t i = 0; i < workload.count; ++i) {
        if (workload.insns[i].op == WORKLOAD_SEND) {
            ++found_send;
        }
    }
    assert(found_send > 0);
}

static void test_consumer(void) {
    workload_t workload;
    char err[160];
    assert(workload_load("workloads/consumer.wl", &workload, err, sizeof(err)) == 0);
    /* Should contain RECV instructions */
    int found_recv = 0;
    for (size_t i = 0; i < workload.count; ++i) {
        if (workload.insns[i].op == WORKLOAD_RECV) {
            ++found_recv;
        }
    }
    assert(found_recv > 0);
}

static void test_nonexistent_file(void) {
    workload_t workload;
    char err[160];
    assert(workload_load("nonexistent.wl", &workload, err, sizeof(err)) != 0);
    assert(strlen(err) > 0);
}

static void test_op_names(void) {
    assert(strcmp(workload_op_name(WORKLOAD_CPU), "CPU") == 0);
    assert(strcmp(workload_op_name(WORKLOAD_IO), "IO") == 0);
    assert(strcmp(workload_op_name(WORKLOAD_SEND), "SEND") == 0);
    assert(strcmp(workload_op_name(WORKLOAD_RECV), "RECV") == 0);
    assert(strcmp(workload_op_name(WORKLOAD_SEM_WAIT), "SEM_WAIT") == 0);
    assert(strcmp(workload_op_name(WORKLOAD_SEM_POST), "SEM_POST") == 0);
    assert(strcmp(workload_op_name(WORKLOAD_LOCK), "LOCK") == 0);
    assert(strcmp(workload_op_name(WORKLOAD_UNLOCK), "UNLOCK") == 0);
    assert(strcmp(workload_op_name(WORKLOAD_EXIT), "EXIT") == 0);
}

static void test_auto_exit_appended(void) {
    /* Create a temp workload without EXIT */
    const char *path = "test_no_exit.wl";
    FILE *fp = fopen(path, "w");
    assert(fp != NULL);
    fprintf(fp, "CPU 5\n");
    fclose(fp);

    workload_t workload;
    char err[160];
    assert(workload_load(path, &workload, err, sizeof(err)) == 0);
    assert(workload.insns[workload.count - 1U].op == WORKLOAD_EXIT);
    remove(path);
}

static void test_comments_and_blanks(void) {
    const char *path = "test_comments.wl";
    FILE *fp = fopen(path, "w");
    assert(fp != NULL);
    fprintf(fp, "# This is a comment\n");
    fprintf(fp, "\n");
    fprintf(fp, "  # indented comment\n");
    fprintf(fp, "CPU 3\n");
    fprintf(fp, "EXIT\n");
    fclose(fp);

    workload_t workload;
    char err[160];
    assert(workload_load(path, &workload, err, sizeof(err)) == 0);
    assert(workload.count == 2U);
    assert(workload.insns[0].op == WORKLOAD_CPU);
    assert(workload.insns[0].ticks == 3U);
    remove(path);
}

static void test_zero_burst_rejected(void) {
    const char *path = "test_zero_cpu.wl";
    FILE *fp = fopen(path, "w");
    assert(fp != NULL);
    fprintf(fp, "CPU 0\n");
    fclose(fp);

    workload_t workload;
    char err[160];
    assert(workload_load(path, &workload, err, sizeof(err)) != 0);
    remove(path);
}

static void test_invalid_opcode(void) {
    const char *path = "test_bad_op.wl";
    FILE *fp = fopen(path, "w");
    assert(fp != NULL);
    fprintf(fp, "INVALID 5\n");
    fclose(fp);

    workload_t workload;
    char err[160];
    assert(workload_load(path, &workload, err, sizeof(err)) != 0);
    remove(path);
}

int main(void) {
    test_cpu_bound();
    test_io_bound();
    test_mixed();
    test_producer();
    test_consumer();
    test_nonexistent_file();
    test_op_names();
    test_auto_exit_appended();
    test_comments_and_blanks();
    test_zero_burst_rejected();
    test_invalid_opcode();
    printf("test_workload: all tests passed\n");
    return 0;
}
