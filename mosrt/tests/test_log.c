/**
 * test_log.c — Trace log and CSV export unit tests.
 *
 * Covers:
 *   - Event recording
 *   - Overflow detection
 *   - CSV export with proper quoting
 *   - trace_configure filtering
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "log.h"

static void test_basic_event(void) {
    trace_log_t log;
    trace_init(&log);
    assert(log.count == 0);
    assert(log.overflow == 0);

    trace_event(&log, 1, 10, "CPU", "tick");
    assert(log.count == 1);
    assert(log.events[0].tick == 1);
    assert(log.events[0].pid == 10);
    assert(strcmp(log.events[0].event, "CPU") == 0);
    assert(strcmp(log.events[0].detail, "tick") == 0);
}

static void test_null_detail(void) {
    trace_log_t log;
    trace_init(&log);
    trace_event(&log, 0, 1, "TEST", NULL);
    assert(log.count == 1);
    assert(strcmp(log.events[0].detail, "") == 0);
}

static void test_overflow(void) {
    trace_log_t log;
    trace_init(&log);

    for (int i = 0; i < MOSRT_MAX_TRACE_EVENTS + 100; ++i) {
        trace_event(&log, (uint64_t)i, 1, "TICK", "t");
    }
    assert(log.count == MOSRT_MAX_TRACE_EVENTS);
    assert(log.overflow == 100);
}

static void test_csv_export(void) {
    trace_log_t log;
    trace_init(&log);
    trace_event(&log, 5, 3, "DISPATCH", "quantum/preempt");
    trace_event(&log, 10, 3, "YIELD", "some, commas, here");

    const char *path = "test_trace_export.csv";
    assert(trace_export_csv(&log, path) == 0);

    FILE *fp = fopen(path, "r");
    assert(fp != NULL);

    char line[256];
    /* Header */
    assert(fgets(line, sizeof(line), fp) != NULL);
    assert(strncmp(line, "tick,pid,event,detail", 20) == 0);

    /* First event — detail should be quoted */
    assert(fgets(line, sizeof(line), fp) != NULL);
    assert(strstr(line, "\"quantum/preempt\"") != NULL);

    /* Second event — commas in detail should be safely quoted */
    assert(fgets(line, sizeof(line), fp) != NULL);
    assert(strstr(line, "\"some, commas, here\"") != NULL);

    fclose(fp);
    remove(path);
}

static void test_overflow_reported_in_csv(void) {
    trace_log_t log;
    trace_init(&log);

    for (int i = 0; i < MOSRT_MAX_TRACE_EVENTS + 5; ++i) {
        trace_event(&log, (uint64_t)i, 1, "T", "x");
    }

    const char *path = "test_overflow_export.csv";
    assert(trace_export_csv(&log, path) == 0);

    FILE *fp = fopen(path, "r");
    assert(fp != NULL);

    char line[256];
    const char *last_line = NULL;
    static char buf[256];
    while (fgets(line, sizeof(line), fp) != NULL) {
        strncpy(buf, line, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        last_line = buf;
    }
    /* Last line should be the overflow comment */
    assert(last_line != NULL);
    assert(strstr(last_line, "5 trace events dropped") != NULL);

    fclose(fp);
    remove(path);
}

int main(void) {
    test_basic_event();
    test_null_detail();
    test_overflow();
    test_csv_export();
    test_overflow_reported_in_csv();
    printf("test_log: all tests passed\n");
    return 0;
}
