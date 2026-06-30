#ifndef MOSRT_PROC_H
#define MOSRT_PROC_H

#include <stdbool.h> 
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <ucontext.h>

#define MOSRT_MAX_PROCS 1024
#define MOSRT_DEFAULT_STACK_SIZE (64U * 1024U)
#define MOSRT_MAX_PRIO 39
#define MOSRT_MIN_PRIO 0
#define MOSRT_DEFAULT_PRIO 20

typedef enum {
    PROC_NEW = 0,
    PROC_READY,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_EXITED
} proc_state_t;

typedef struct {
    bool used;
    int pid; // process identifier
    int ppid;// parent process identifier
    proc_state_t state; // state
    int priority; // dynamic priority, lower values run first
    int base_priority;
    int nice;
    unsigned mlfq_level;
    uint64_t cpu_time; 
    uint64_t wait_time;
    uint64_t response_time;
    uint64_t start_tick;
    uint64_t finish_tick;
    uint64_t last_ready_tick;
    uint64_t wakeup_tick;
    ucontext_t context;
    void *stack;
    size_t stack_size;
    size_t stack_mapping_size;
    int exit_code;
} pcb_t;

void proc_table_init(void);
/** Release all process-owned resources and clear the process table. */
void proc_table_shutdown(void);
/** Create a PCB and allocate an owned stack for its user context. */
int proc_create(int ppid, int priority, uint64_t now_tick, size_t stack_size);
/** Return a mutable PCB by pid, or NULL if the pid is not alive/retained. */
pcb_t *proc_get(int pid);
/** Return an immutable PCB by pid, or NULL if the pid is not alive/retained. */
const pcb_t *proc_get_const(int pid);
bool proc_is_valid_transition(proc_state_t from, proc_state_t to);
bool proc_set_state(int pid, proc_state_t new_state, uint64_t now_tick);
/** Mark a process EXITED while retaining its PCB for metrics and ps output. */
bool proc_mark_exited(int pid, int exit_code, uint64_t now_tick);
/** Release an owned PCB and stack. Used by kill/cleanup. */
void proc_destroy(int pid, int exit_code);
int proc_count(void);
int proc_live_count(void);
const char *proc_state_to_string(proc_state_t state);
void proc_dump(FILE *out);
void proc_for_each(void (*fn)(pcb_t *proc, void *ctx), void *ctx);

#endif
