#ifndef MOSRT_LOG_H
#define MOSRT_LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define MOSRT_MAX_TRACE_EVENTS 4096

typedef struct {
    uint64_t tick;
    int pid;
    char event[24];
    char detail[96];
} trace_event_t;

typedef struct {
    trace_event_t events[MOSRT_MAX_TRACE_EVENTS];
    size_t count;
    size_t overflow;
    bool enabled_all;
    int enabled_pid;
} trace_log_t;

/** Initialize an in-memory trace log. */
void trace_init(trace_log_t *log);
/** Configure tracing for all processes or a single pid (-1 disables pid filtering). */
void trace_configure(trace_log_t *log, bool all, int pid);
/** Append a trace event and optionally mirror it to stdout. */
void trace_event(trace_log_t *log, uint64_t tick, int pid, const char *event, const char *detail);
/** Export trace events to CSV. */
int trace_export_csv(const trace_log_t *log, const char *path);
/** Print the trace log to a stream. */
void trace_dump(const trace_log_t *log, FILE *out);

#endif
