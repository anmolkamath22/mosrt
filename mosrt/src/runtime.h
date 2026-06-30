#ifndef MOSRT_RUNTIME_H
#define MOSRT_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "log.h"
#include "proc.h"
#include "sched.h"
#include "workload.h"

typedef struct runtime runtime_t;

/** Create a clean runtime with process table, scheduler, IPC, sync, trace, and timer state. */
void runtime_init(runtime_t *rt);
/** Stop timers and release process-owned resources held by the runtime. */
void runtime_shutdown(runtime_t *rt);
/** Load a deterministic workload and create a READY process. */
int runtime_run_workload(runtime_t *rt, const char *path, int priority);
/** Advance the runtime by n deterministic ticks. */
void runtime_step(runtime_t *rt, unsigned ticks);
/** Mark a process killed and release it from future scheduling. */
bool runtime_kill(runtime_t *rt, int pid);
/** Start runtime execution state and arm safe timer preemption. */
bool runtime_start(runtime_t *rt);
/** Stop safe timer preemption. */
void runtime_stop(runtime_t *rt);
/** Cooperative yield hook used by workload/context code. */
void runtime_yield(runtime_t *rt);
/** Change scheduler policy and rebuild ready queues. */
void runtime_set_scheduler(runtime_t *rt, sched_algo_t algo);
/** Change RR/MLFQ quantum in ticks. */
void runtime_set_quantum(runtime_t *rt, unsigned ticks);
/** Configure trace output. */
void runtime_trace(runtime_t *rt, bool all, int pid);
/** Print process table and scheduler state. */
void runtime_dump_queues(runtime_t *rt, FILE *out);
/** Print aggregate metrics. */
void runtime_print_metrics(runtime_t *rt, FILE *out);
/** Export aggregate metrics to CSV. */
int runtime_export_metrics(runtime_t *rt, const char *path);
/** Export trace events to CSV. */
int runtime_export_trace(runtime_t *rt, const char *path);
/** Adjust nice value and derived priority for a process. */
bool runtime_set_nice(runtime_t *rt, int pid, int nice_value);
/** Set exact dynamic/base priority for a process. */
bool runtime_set_priority(runtime_t *rt, int pid, int priority);
/** Run scheduler comparison benchmarks from the default workload suite. */
void runtime_benchmark(FILE *out);
/** Return current global tick. */
uint64_t runtime_tick(const runtime_t *rt);
/** Return active scheduler name. */
const char *runtime_scheduler_name(const runtime_t *rt);

struct runtime {
    uint64_t tick;
    bool started;
    bool need_resched;
    int current_pid;
    uint64_t busy_ticks;
    uint64_t idle_ticks;
    unsigned completed;
    ucontext_t dispatcher_context;
    scheduler_t scheduler;
    trace_log_t trace;
    struct {
        bool used;
        int pid;
        workload_t workload;
        size_t pc;
        uint64_t remaining;
    } slots[MOSRT_MAX_PROCS];
};

#endif
