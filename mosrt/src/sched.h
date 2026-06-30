#ifndef MOSRT_SCHED_H
#define MOSRT_SCHED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "proc.h"

#define MOSRT_MLFQ_LEVELS 4

typedef enum { SCHED_FCFS = 0, SCHED_RR, SCHED_PRIO, SCHED_MLFQ } sched_algo_t;

typedef struct scheduler scheduler_t;

/** Initialize a scheduler object with the requested policy and quantum. */
void sched_init(scheduler_t *sched, sched_algo_t algo, unsigned quantum_ticks);
/** Reset all ready queues while preserving selected policy. */
void sched_clear(scheduler_t *sched);
/** Change scheduling policy; queues are cleared and must be repopulated. */
void sched_set_algo(scheduler_t *sched, sched_algo_t algo);
/** Change the base quantum used by RR/MLFQ. */
void sched_set_quantum(scheduler_t *sched, unsigned quantum_ticks);
/** Return the active scheduling policy. */
sched_algo_t sched_algo(const scheduler_t *sched);
/** Return a stable policy name. */
const char *sched_algo_name(sched_algo_t algo);
/** Parse a scheduler name such as fcfs, rr, prio, or mlfq. */
bool sched_parse_algo(const char *name, sched_algo_t *out);
/** Enqueue a ready pid according to the active scheduling policy. */
bool sched_enqueue(scheduler_t *sched, int pid);
/** Select the next pid to dispatch, or -1 when no process is ready. */
int sched_pick_next(scheduler_t *sched);
/** Notify the scheduler that a running pid consumed one CPU tick. */
void sched_on_tick(scheduler_t *sched, int pid);
/** Return true when the current process should yield at this tick. */
bool sched_should_preempt(const scheduler_t *sched, int current_pid);
/** Update priority-aging metadata for all READY processes. */
void sched_age_ready(scheduler_t *sched, uint64_t now_tick);
/** Demote a process after consuming an entire MLFQ quantum. */
void sched_demote_after_quantum(scheduler_t *sched, int pid);
/** Promote all retained processes to the highest MLFQ queue. */
void sched_priority_boost(scheduler_t *sched);
/** Print queue state for scheduler visualization. */
void sched_dump(const scheduler_t *sched, FILE *out);

struct scheduler {
    sched_algo_t algo;
    unsigned quantum_ticks;
    unsigned consumed_ticks;
    int current_pid;
    int rr[MOSRT_MAX_PROCS];
    size_t rr_head;
    size_t rr_len;
    int heap[MOSRT_MAX_PROCS];
    size_t heap_len;
    int mlfq[MOSRT_MLFQ_LEVELS][MOSRT_MAX_PROCS];
    size_t mlfq_head[MOSRT_MLFQ_LEVELS];
    size_t mlfq_len[MOSRT_MLFQ_LEVELS];
    uint64_t last_boost_tick;
};

#endif
