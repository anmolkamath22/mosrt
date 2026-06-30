#include "proc.h"
#include "vm.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct {
    pcb_t entries[MOSRT_MAX_PROCS];
    int next_pid;
    /* O(1) pid→slot index; -1 when unmapped.  PIDs are sequential
     * starting from 1 and never reused within a single run, so only
     * the first next_pid-1 entries are meaningful. */
    int pid_to_slot[MOSRT_MAX_PROCS];
} process_table_t;

static process_table_t g_ptable;

static size_t page_size(void) {
    long n = sysconf(_SC_PAGESIZE);
    return n > 0 ? (size_t)n : 4096U;
}

static size_t round_up(size_t value, size_t align) {
    if (align == 0U || value > SIZE_MAX - (align - 1U)) {
        return 0U;
    }
    return (value + align - 1U) & ~(align - 1U);
}

static void *stack_alloc(size_t usable_size, size_t *mapped_size) {
    if (mapped_size == NULL || usable_size == 0U) {
        return NULL;
    }
    size_t guard = page_size();
    size_t usable_rounded = round_up(usable_size, guard);
    if (usable_rounded == 0U || usable_rounded > SIZE_MAX - guard) {
        *mapped_size = 0U;
        return NULL;
    }
    size_t total = usable_rounded + guard;
    void *mapping = mmap(NULL, total, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) {
        *mapped_size = 0U;
        return NULL;
    }
    if (mprotect(mapping, guard, PROT_NONE) != 0) {
        (void)munmap(mapping, total);
        *mapped_size = 0U;
        return NULL;
    }
    *mapped_size = total;
    return (char *)mapping + guard;
}

static void stack_free(void *stack, size_t mapped_size) {
    if (stack == NULL || mapped_size == 0U) {
        return;
    }
    size_t guard = page_size();
    void *mapping = (char *)stack - guard;
    (void)munmap(mapping, mapped_size);
}

static void release_stack(pcb_t *p) {
    if (p == NULL) {
        return;
    }
    stack_free(p->stack, p->stack_mapping_size);
    p->stack = NULL;
    p->stack_size = 0U;
    p->stack_mapping_size = 0U;
}

static int find_free_slot(void) {
    for (int i = 0; i < MOSRT_MAX_PROCS; ++i) {
        if (!g_ptable.entries[i].used) {
            return i;
        }
    }
    return -1;
}

static int find_slot_by_pid(int pid) {
    if (pid >= 1 && pid < g_ptable.next_pid && pid <= MOSRT_MAX_PROCS) {
        int slot = g_ptable.pid_to_slot[pid - 1];
        if (slot >= 0 && slot < MOSRT_MAX_PROCS &&
            g_ptable.entries[slot].used && g_ptable.entries[slot].pid == pid) {
            return slot;
        }
    }
    return -1;
}

void proc_table_init(void) {
    proc_table_shutdown();
    memset(&g_ptable, 0, sizeof(g_ptable));
    g_ptable.next_pid = 1;
    memset(g_ptable.pid_to_slot, -1, sizeof(g_ptable.pid_to_slot));
}

void proc_table_shutdown(void) {
    for (int i = 0; i < MOSRT_MAX_PROCS; ++i) {
        release_stack(&g_ptable.entries[i]);
        if (g_ptable.entries[i].vm != NULL) {
            vm_proc_destroy(g_ptable.entries[i].vm);
            g_ptable.entries[i].vm = NULL;
        }
    }
}

int proc_create(int ppid, int priority, uint64_t now_tick, size_t stack_size) {
    int slot = find_free_slot();
    if (slot < 0) {
        return -1;
    }

    if (stack_size == 0U) {
        stack_size = MOSRT_DEFAULT_STACK_SIZE;
    }

    size_t mapped_size = 0U;
    void *stack = stack_alloc(stack_size, &mapped_size);
    if (stack == NULL) {
        return -1;
    }

    pcb_t *p = &g_ptable.entries[slot];
    memset(p, 0, sizeof(*p));

    p->used = true;
    p->pid = g_ptable.next_pid++;
    if (p->pid <= MOSRT_MAX_PROCS) {
        g_ptable.pid_to_slot[p->pid - 1] = slot;
    }
    p->ppid = ppid;
    p->state = PROC_NEW;
    if (priority < MOSRT_MIN_PRIO) {
        priority = MOSRT_MIN_PRIO;
    } else if (priority > MOSRT_MAX_PRIO) {
        priority = MOSRT_MAX_PRIO;
    }
    p->priority = priority;
    p->base_priority = priority;
    p->nice = 0;
    p->mlfq_level = 0U;
    p->start_tick = now_tick;
    p->last_ready_tick = now_tick;
    p->finish_tick = 0U;
    p->response_time = UINT64_MAX;
    p->stack = stack;
    p->stack_size = stack_size;
    p->stack_mapping_size = mapped_size;
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
    if (new_state == PROC_READY) {
        p->last_ready_tick = now_tick;
    }
    if (new_state == PROC_EXITED) {
        p->finish_tick = now_tick;
    }

    p->state = new_state;
    return true;
}

bool proc_mark_exited(int pid, int exit_code, uint64_t now_tick) {
    pcb_t *p = proc_get(pid);
    if (p == NULL || p->state == PROC_EXITED) {
        return false;
    }
    if (!proc_is_valid_transition(p->state, PROC_EXITED)) {
        return false;
    }
    p->exit_code = exit_code;
    p->finish_tick = now_tick;
    p->state = PROC_EXITED;
    p->wakeup_tick = 0U;
    release_stack(p);
    return true;
}

void proc_destroy(int pid, int exit_code) {
    pcb_t *p = proc_get(pid);
    if (p == NULL) {
        return;
    }

    release_stack(p);
    if (p->vm != NULL) {
        vm_proc_destroy(p->vm);
        p->vm = NULL;
    }
    p->exit_code = exit_code;
    p->state = PROC_EXITED;
    p->used = false;
    if (pid >= 1 && pid <= MOSRT_MAX_PROCS) {
        g_ptable.pid_to_slot[pid - 1] = -1;
    }
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

int proc_live_count(void) {
    int count = 0;
    for (int i = 0; i < MOSRT_MAX_PROCS; ++i) {
        if (g_ptable.entries[i].used && g_ptable.entries[i].state != PROC_EXITED) {
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

    fprintf(out, "PID PPID STATE    PRIO Q CPU WAIT RESP START FINISH WAKE EXIT\n");
    for (int i = 0; i < MOSRT_MAX_PROCS; ++i) {
        const pcb_t *p = &g_ptable.entries[i];
        if (!p->used) {
            continue;
        }

        fprintf(out,
                "%3d %4d %-8s %4d %1u %3" PRIu64 " %4" PRIu64 " ",
                p->pid,
                p->ppid,
                proc_state_to_string(p->state),
                p->priority,
                p->mlfq_level,
                p->cpu_time,
                p->wait_time);

        if (p->response_time == UINT64_MAX) {
            fprintf(out, "  NA ");
        } else {
            fprintf(out, "%4" PRIu64 " ", p->response_time);
        }

        fprintf(out, "%5" PRIu64 " %6" PRIu64 " %4" PRIu64 " %4d\n",
                p->start_tick,
                p->finish_tick,
                p->wakeup_tick,
                p->exit_code);
    }
}

void proc_for_each(void (*fn)(pcb_t *proc, void *ctx), void *ctx) {
    if (fn == NULL) {
        return;
    }
    for (int i = 0; i < MOSRT_MAX_PROCS; ++i) {
        if (g_ptable.entries[i].used) {
            fn(&g_ptable.entries[i], ctx);
        }
    }
}
