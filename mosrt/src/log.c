#include "log.h"

#include <limits.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

void trace_init(trace_log_t *log) {
    if (log == NULL) {
        return;
    }
    memset(log, 0, sizeof(*log));
    log->enabled_pid = INT_MIN;
}

void trace_configure(trace_log_t *log, bool all, int pid) {
    if (log == NULL) {
        return;
    }
    log->enabled_all = all;
    log->enabled_pid = pid;
}

void trace_event(trace_log_t *log, uint64_t tick, int pid, const char *event, const char *detail) {
    if (log == NULL || event == NULL) {
        return;
    }
    if (log->count < MOSRT_MAX_TRACE_EVENTS) {
        trace_event_t *e = &log->events[log->count++];
        e->tick = tick;
        e->pid = pid;
        snprintf(e->event, sizeof(e->event), "%s", event);
        snprintf(e->detail, sizeof(e->detail), "%s", detail == NULL ? "" : detail);
    }
    if (log->enabled_all || log->enabled_pid == pid) {
        printf("[tick=%" PRIu64 "] pid=%d %-10s %s\n", tick, pid, event, detail == NULL ? "" : detail);
    }
}

int trace_export_csv(const trace_log_t *log, const char *path) {
    if (log == NULL || path == NULL) {
        return -1;
    }
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        return -1;
    }
    fprintf(fp, "tick,pid,event,detail\n");
    for (size_t i = 0; i < log->count; ++i) {
        const trace_event_t *e = &log->events[i];
        fprintf(fp, "%" PRIu64 ",%d,%s,%s\n", e->tick, e->pid, e->event, e->detail);
    }
    fclose(fp);
    return 0;
}

void trace_dump(const trace_log_t *log, FILE *out) {
    if (log == NULL) {
        return;
    }
    if (out == NULL) {
        out = stdout;
    }
    for (size_t i = 0; i < log->count; ++i) {
        const trace_event_t *e = &log->events[i];
        fprintf(out, "[tick=%" PRIu64 "] pid=%d %-10s %s\n",
                e->tick, e->pid, e->event, e->detail);
    }
}
