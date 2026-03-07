#include "proc.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    pcb_t entries[MOSRT_MAX_PROCS];
    int next_pid;
} process_table_t;

static process_table_t g_ptable;

static int find_free_slot(void) {
    for (int i = 0; i < MOSRT_MAX_PROCS; ++i) {
        if (!g_ptable.entries[i].used) {
            return i;
        }
    }
    return -1;
}

static int find_slot_by_pid(int pid) {
    for (int i = 0; i < MOSRT_MAX_PROCS; ++i) {
        if (g_ptable.entries[i].used && g_ptable.entries[i].pid == pid) {
            return i;
        }
    }
    return -1;
}

void proc_table_init(void) {
    memset(&g_ptable, 0, sizeof(g_ptable));
    g_ptable.next_pid = 1;
}

int proc_create(int ppid, int priority, uint64_t now_tick, size_t stack_size) {
    int slot = find_free_slot();
    if (slot < 0) {
        return -1;
    }

    if (stack_size == 0U) {
        stack_size = MOSRT_DEFAULT_STACK_SIZE;
    }

    void *stack = malloc(stack_size);
    if (stack == NULL) {
        return -1;
    }

    pcb_t *p = &g_ptable.entries[slot];
    memset(p, 0, sizeof(*p));

    p->used = true;
    p->pid = g_ptable.next_pid++;
    p->ppid = ppid;
    p->state = PROC_NEW;
    p->priority = priority;
    p->start_tick = now_tick;
    p->response_time = UINT64_MAX;
    p->stack = stack;
    p->stack_size = stack_size;
    p->exit_code = 0;

    return p->pid;
}

pcb_t *proc_get(int pid) {
    int slot = find_slot_by_pid(pid);
    if (slot < 0) {
        return NULL;
    }
    return &g_ptable.entries[slot];
}

const pcb_t *proc_get_const(int pid) {
    int slot = find_slot_by_pid(pid);
    if (slot < 0) {
        return NULL;
    }
    return &g_ptable.entries[slot];
}

bool proc_is_valid_transition(proc_state_t from, proc_state_t to) {
    switch (from) {
        case PROC_NEW:
            return to == PROC_READY || to == PROC_EXITED;
        case PROC_READY:
            return to == PROC_RUNNING || to == PROC_EXITED;
        case PROC_RUNNING:
            return to == PROC_READY || to == PROC_BLOCKED || to == PROC_EXITED;
        case PROC_BLOCKED:
            return to == PROC_READY || to == PROC_EXITED;
        case PROC_EXITED:
            return false;
        default:
            return false;
    }
}

bool proc_set_state(int pid, proc_state_t new_state, uint64_t now_tick) {
    pcb_t *p = proc_get(pid);
    if (p == NULL) {
        return false;
    }

    if (!proc_is_valid_transition(p->state, new_state)) {
        return false;
    }

    if (new_state == PROC_RUNNING && p->response_time == UINT64_MAX) {
        p->response_time = now_tick - p->start_tick;
    }

    p->state = new_state;
    return true;
}

void proc_destroy(int pid, int exit_code) {
    pcb_t *p = proc_get(pid);
    if (p == NULL) {
        return;
    }

    free(p->stack);
    p->stack = NULL;
    p->stack_size = 0U;
    p->exit_code = exit_code;
    p->state = PROC_EXITED;
    p->used = false;
}

int proc_count(void) {
    int count = 0;
    for (int i = 0; i < MOSRT_MAX_PROCS; ++i) {
        if (g_ptable.entries[i].used) {
            ++count;
        }
    }
    return count;
}

const char *proc_state_to_string(proc_state_t state) {
    switch (state) {
        case PROC_NEW:
            return "NEW";
        case PROC_READY:
            return "READY";
        case PROC_RUNNING:
            return "RUNNING";
        case PROC_BLOCKED:
            return "BLOCKED";
        case PROC_EXITED:
            return "EXITED";
        default:
            return "UNKNOWN";
    }
}

void proc_dump(FILE *out) {
    if (out == NULL) {
        out = stdout;
    }

    fprintf(out, "PID PPID STATE    PRIO CPU WAIT RESP START WAKE EXIT\n");
    for (int i = 0; i < MOSRT_MAX_PROCS; ++i) {
        const pcb_t *p = &g_ptable.entries[i];
        if (!p->used) {
            continue;
        }

        fprintf(out,
                "%3d %4d %-8s %4d %3" PRIu64 " %4" PRIu64 " ",
                p->pid,
                p->ppid,
                proc_state_to_string(p->state),
                p->priority,
                p->cpu_time,
                p->wait_time);

        if (p->response_time == UINT64_MAX) {
            fprintf(out, "  NA ");
        } else {
            fprintf(out, "%4" PRIu64 " ", p->response_time);
        }

        fprintf(out, "%5" PRIu64 " %4" PRIu64 " %4d\n",
                p->start_tick,
                p->wakeup_tick,
                p->exit_code);
    }
}
