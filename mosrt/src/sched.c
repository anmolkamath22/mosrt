#include "sched.h"

#include <string.h>

#include "proc.h"

#define AGING_INTERVAL 5U

static bool queue_contains(const int *q, size_t head, size_t len, int pid) {
    for (size_t i = 0; i < len; ++i) {
        if (q[(head + i) % MOSRT_MAX_PROCS] == pid) {
            return true;
        }
    }
    return false;
}

static bool queue_push(int *q, size_t *head, size_t *len, int pid) {
    if (*len >= MOSRT_MAX_PROCS || queue_contains(q, *head, *len, pid)) {
        return false;
    }
    q[(*head + *len) % MOSRT_MAX_PROCS] = pid;
    ++*len;
    return true;
}

static int queue_pop(int *q, size_t *head, size_t *len) {
    while (*len > 0U) {
        int pid = q[*head];
        *head = (*head + 1U) % MOSRT_MAX_PROCS;
        --*len;
        const pcb_t *p = proc_get_const(pid);
        if (p != NULL && p->state == PROC_READY) {
            return pid;
        }
    }
    return -1;
}

static bool prio_less(int left_pid, int right_pid) {
    const pcb_t *a = proc_get_const(left_pid);
    const pcb_t *b = proc_get_const(right_pid);
    if (a == NULL) {
        return false;
    }
    if (b == NULL) {
        return true;
    }
    if (a->priority != b->priority) {
        return a->priority < b->priority;
    }
    return a->last_ready_tick < b->last_ready_tick;
}

static bool heap_contains(const scheduler_t *sched, int pid) {
    for (size_t i = 0; i < sched->heap_len; ++i) {
        if (sched->heap[i] == pid) {
            return true;
        }
    }
    return false;
}

static bool heap_push(scheduler_t *sched, int pid) {
    if (sched->heap_len >= MOSRT_MAX_PROCS || heap_contains(sched, pid)) {
        return false;
    }
    size_t i = sched->heap_len++;
    sched->heap[i] = pid;
    while (i > 0U) {
        size_t parent = (i - 1U) / 2U;
        if (!prio_less(sched->heap[i], sched->heap[parent])) {
            break;
        }
        int tmp = sched->heap[i];
        sched->heap[i] = sched->heap[parent];
        sched->heap[parent] = tmp;
        i = parent;
    }
    return true;
}

static int heap_pop(scheduler_t *sched) {
    while (sched->heap_len > 0U) {
        int top = sched->heap[0];
        sched->heap[0] = sched->heap[--sched->heap_len];
        size_t i = 0U;
        for (;;) {
            size_t l = i * 2U + 1U;
            size_t r = l + 1U;
            size_t best = i;
            if (l < sched->heap_len && prio_less(sched->heap[l], sched->heap[best])) {
                best = l;
            }
            if (r < sched->heap_len && prio_less(sched->heap[r], sched->heap[best])) {
                best = r;
            }
            if (best == i) {
                break;
            }
            int tmp = sched->heap[i];
            sched->heap[i] = sched->heap[best];
            sched->heap[best] = tmp;
            i = best;
        }
        const pcb_t *p = proc_get_const(top);
        if (p != NULL && p->state == PROC_READY) {
            return top;
        }
    }
    return -1;
}

void sched_init(scheduler_t *sched, sched_algo_t algo, unsigned quantum_ticks) {
    if (sched == NULL) {
        return;
    }
    memset(sched, 0, sizeof(*sched));
    sched->algo = algo;
    sched->quantum_ticks = quantum_ticks == 0U ? 1U : quantum_ticks;
    sched->current_pid = -1;
}

void sched_clear(scheduler_t *sched) {
    sched_algo_t algo = sched->algo;
    unsigned quantum = sched->quantum_ticks;
    sched_init(sched, algo, quantum);
}

void sched_set_algo(scheduler_t *sched, sched_algo_t algo) {
    unsigned quantum = sched->quantum_ticks;
    sched_init(sched, algo, quantum);
}

void sched_set_quantum(scheduler_t *sched, unsigned quantum_ticks) {
    if (sched != NULL) {
        sched->quantum_ticks = quantum_ticks == 0U ? 1U : quantum_ticks;
    }
}

sched_algo_t sched_algo(const scheduler_t *sched) {
    return sched == NULL ? SCHED_RR : sched->algo;
}

const char *sched_algo_name(sched_algo_t algo) {
    switch (algo) {
    case SCHED_FCFS:
        return "fcfs";
    case SCHED_RR:
        return "rr";
    case SCHED_PRIO:
        return "prio";
    case SCHED_MLFQ:
        return "mlfq";
    default:
        return "unknown";
    }
}

bool sched_parse_algo(const char *name, sched_algo_t *out) {
    if (name == NULL || out == NULL) {
        return false;
    }
    if (strcmp(name, "fcfs") == 0) {
        *out = SCHED_FCFS;
    } else if (strcmp(name, "rr") == 0) {
        *out = SCHED_RR;
    } else if (strcmp(name, "prio") == 0) {
        *out = SCHED_PRIO;
    } else if (strcmp(name, "mlfq") == 0) {
        *out = SCHED_MLFQ;
    } else {
        return false;
    }
    return true;
}

bool sched_enqueue(scheduler_t *sched, int pid) {
    if (sched == NULL || proc_get_const(pid) == NULL) {
        return false;
    }
    if (sched->algo == SCHED_PRIO) {
        return heap_push(sched, pid);
    }
    if (sched->algo == SCHED_MLFQ) {
        pcb_t *p = proc_get(pid);
        unsigned level =
            p->mlfq_level >= MOSRT_MLFQ_LEVELS ? MOSRT_MLFQ_LEVELS - 1U : p->mlfq_level;
        return queue_push(sched->mlfq[level], &sched->mlfq_head[level], &sched->mlfq_len[level],
                          pid);
    }
    return queue_push(sched->rr, &sched->rr_head, &sched->rr_len, pid);
}

int sched_pick_next(scheduler_t *sched) {
    if (sched == NULL) {
        return -1;
    }
    int pid = -1;
    if (sched->algo == SCHED_PRIO) {
        pid = heap_pop(sched);
    } else if (sched->algo == SCHED_MLFQ) {
        for (unsigned q = 0U; q < MOSRT_MLFQ_LEVELS; ++q) {
            pid = queue_pop(sched->mlfq[q], &sched->mlfq_head[q], &sched->mlfq_len[q]);
            if (pid >= 0) {
                break;
            }
        }
    } else {
        pid = queue_pop(sched->rr, &sched->rr_head, &sched->rr_len);
    }
    sched->current_pid = pid;
    sched->consumed_ticks = 0U;
    return pid;
}

void sched_on_tick(scheduler_t *sched, int pid) {
    if (sched != NULL && sched->current_pid == pid) {
        ++sched->consumed_ticks;
    }
}

bool sched_should_preempt(const scheduler_t *sched, int current_pid) {
    if (sched == NULL || current_pid < 0) {
        return false;
    }
    if (sched->algo == SCHED_FCFS) {
        return false;
    }
    if (sched->algo == SCHED_PRIO) {
        for (size_t i = 0; i < sched->heap_len; ++i) {
            if (prio_less(sched->heap[i], current_pid)) {
                return true;
            }
        }
    }
    return sched->consumed_ticks >= sched->quantum_ticks;
}

static void age_one(pcb_t *p, void *ctx) {
    uint64_t now_tick = *(uint64_t *)ctx;
    if (p->state == PROC_READY && p->priority > MOSRT_MIN_PRIO && now_tick > p->last_ready_tick &&
        (now_tick - p->last_ready_tick) % AGING_INTERVAL == 0U) {
        --p->priority;
    }
}

void sched_age_ready(scheduler_t *sched, uint64_t now_tick) {
    if (sched != NULL && sched->algo == SCHED_PRIO) {
        proc_for_each(age_one, &now_tick);
    }
}

void sched_demote_after_quantum(scheduler_t *sched, int pid) {
    pcb_t *p = proc_get(pid);
    if (sched == NULL || p == NULL || sched->algo != SCHED_MLFQ) {
        return;
    }
    if (p->mlfq_level + 1U < MOSRT_MLFQ_LEVELS) {
        ++p->mlfq_level;
    }
}

static void boost_one(pcb_t *p, void *ctx) {
    (void)ctx;
    if (p->state != PROC_EXITED) {
        p->mlfq_level = 0U;
    }
}

void sched_priority_boost(scheduler_t *sched) {
    if (sched != NULL && sched->algo == SCHED_MLFQ) {
        proc_for_each(boost_one, NULL);
    }
}

void sched_dump(const scheduler_t *sched, FILE *out) {
    if (out == NULL) {
        out = stdout;
    }
    if (sched == NULL) {
        return;
    }
    fprintf(out, "scheduler=%s quantum=%u current=%d\n", sched_algo_name(sched->algo),
            sched->quantum_ticks, sched->current_pid);
    if (sched->algo == SCHED_MLFQ) {
        for (unsigned q = 0U; q < MOSRT_MLFQ_LEVELS; ++q) {
            fprintf(out, "Q%u:", q);
            for (size_t i = 0; i < sched->mlfq_len[q]; ++i) {
                fprintf(out, " %d", sched->mlfq[q][(sched->mlfq_head[q] + i) % MOSRT_MAX_PROCS]);
            }
            fprintf(out, "\n");
        }
        return;
    }
    if (sched->algo == SCHED_PRIO) {
        fprintf(out, "heap:");
        for (size_t i = 0; i < sched->heap_len; ++i) {
            fprintf(out, " %d", sched->heap[i]);
        }
        fprintf(out, "\n");
        return;
    }
    fprintf(out, "ready:");
    for (size_t i = 0; i < sched->rr_len; ++i) {
        fprintf(out, " %d", sched->rr[(sched->rr_head + i) % MOSRT_MAX_PROCS]);
    }
    fprintf(out, "\n");
}
