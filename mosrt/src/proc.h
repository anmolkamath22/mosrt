#ifndef MOSRT_PROC_H
#define MOSRT_PROC_H

// Includes boolean functions 
#include <stdbool.h> 
// Foundational C header for core related projects
#include <stddef.h>
// Fixed width integer types which provide integer control
#include <stdint.h>
// Standard library for input / output functions
#include <stdio.h>
// A posix header file that provides user-level context switching primitives
#include <ucontext.h>

#define MOSRT_MAX_PROCS 128
#define MOSRT_DEFAULT_STACK_SIZE (64U * 1024U)

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
    int priority; // priority
    uint64_t cpu_time; 
    uint64_t wait_time;
    uint64_t response_time;
    uint64_t start_tick;
    uint64_t wakeup_tick;
    ucontext_t context; // ucontext_t is used for structure representing execution state
    void *stack;
    size_t stack_size;
    int exit_code;
} pcb_t;

void proc_table_init(void);
int proc_create(int ppid, int priority, uint64_t now_tick, size_t stack_size);
pcb_t *proc_get(int pid);
const pcb_t *proc_get_const(int pid);
bool proc_is_valid_transition(proc_state_t from, proc_state_t to);
bool proc_set_state(int pid, proc_state_t new_state, uint64_t now_tick);
void proc_destroy(int pid, int exit_code);
int proc_count(void);
const char *proc_state_to_string(proc_state_t state);
void proc_dump(FILE *out);

#endif
