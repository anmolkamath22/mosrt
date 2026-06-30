#include "runtime.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "ipc.h"
#include "log.h"
#include "pager.h"
#include "proc.h"
#include "sync.h"
#include "timer.h"
#include "tlb.h"
#include "vm.h"
#include "workload.h"

#define MLFQ_BOOST_INTERVAL 50U
#define DEFAULT_QUANTUM 10U
#define MAX_DISPATCH_GUARD 1024U
#define BENCH_MAX_TICKS 500U
#define ERR_BUF_SIZE 160

static runtime_t *g_context_runtime;

static void process_context_trampoline(int pid) {
    for (;;) {
        runtime_t *rt = g_context_runtime;
        pcb_t *p = proc_get(pid);
        if (rt == NULL || p == NULL) {
            return;
        }
        trace_event(&rt->trace, rt->tick, pid, "CTXSW", "swapcontext");
        if (swapcontext(&p->context, &rt->dispatcher_context) != 0) {
            return;
        }
    }
}

static void make_detail(char *buf, size_t bufsz, const char *text, int value) {
    snprintf(buf, bufsz, "%s %d", text, value);
}

/* O(1) PID→slot index; -1 when unmapped. Mirrors proc.c pid_to_slot. */
static int g_slot_by_pid[MOSRT_MAX_PROCS];

static int slot_index(const runtime_t *rt, int pid) {
    if (pid >= 1 && pid <= MOSRT_MAX_PROCS) {
        int idx = g_slot_by_pid[pid - 1];
        if (idx >= 0 && idx < MOSRT_MAX_PROCS && rt->slots[idx].used && rt->slots[idx].pid == pid) {
            return idx;
        }
    }
    return -1;
}

static int alloc_slot(runtime_t *rt, int pid) {
    for (int i = 0; i < MOSRT_MAX_PROCS; ++i) {
        if (!rt->slots[i].used) {
            memset(&rt->slots[i], 0, sizeof(rt->slots[i]));
            rt->slots[i].used = true;
            rt->slots[i].pid = pid;
            if (pid >= 1 && pid <= MOSRT_MAX_PROCS) {
                g_slot_by_pid[pid - 1] = i;
            }
            return i;
        }
    }
    return -1;
}

static void enqueue_ready(runtime_t *rt, int pid) {
    if (proc_set_state(pid, PROC_READY, rt->tick)) {
        (void)sched_enqueue(&rt->scheduler, pid);
        trace_event(&rt->trace, rt->tick, pid, "READY", runtime_scheduler_name(rt));
    }
}

static void wake_pid(runtime_t *rt, int pid, const char *why) {
    pcb_t *p = proc_get(pid);
    if (p != NULL && p->state == PROC_BLOCKED) {
        enqueue_ready(rt, pid);
        trace_event(&rt->trace, rt->tick, pid, "WAKE", why);
    }
}

static void wake_blocked_by_time(pcb_t *p, void *ctx) {
    runtime_t *rt = ctx;
    if (p->state == PROC_BLOCKED && p->wakeup_tick > 0U && p->wakeup_tick <= rt->tick) {
        p->wakeup_tick = 0U;
        wake_pid(rt, p->pid, "timer");
    }
}

static void account_ready_wait(pcb_t *p, void *ctx) {
    (void)ctx;
    if (p->state == PROC_READY) {
        ++p->wait_time;
    }
}

static void rebuild_one_ready(pcb_t *p, void *ctx) {
    runtime_t *rt = ctx;
    if (p->state == PROC_READY) {
        (void)sched_enqueue(&rt->scheduler, p->pid);
    }
}

static void rebuild_ready_queues(runtime_t *rt) {
    int current_pid = rt->scheduler.current_pid;
    unsigned consumed_ticks = rt->scheduler.consumed_ticks;
    sched_clear(&rt->scheduler);
    proc_for_each(rebuild_one_ready, rt);
    rt->scheduler.current_pid = current_pid;
    rt->scheduler.consumed_ticks = consumed_ticks;
}

static bool dispatch(runtime_t *rt) {
    if (rt->current_pid >= 0) {
        return true;
    }
    int pid = sched_pick_next(&rt->scheduler);
    if (pid < 0) {
        return false;
    }
    if (!proc_set_state(pid, PROC_RUNNING, rt->tick)) {
        return false;
    }
    rt->current_pid = pid;
    /* Trace dispatch *before* the context switch so the recorded tick
     * reflects when the process was selected, not when it yielded back. */
    trace_event(&rt->trace, rt->tick, pid, "DISPATCH", runtime_scheduler_name(rt));
    pcb_t *p = proc_get(pid);
    if (p != NULL) {
        tlb_flush();
        g_context_runtime = rt;
        (void)swapcontext(&rt->dispatcher_context, &p->context);
    }
    return true;
}

static void exit_current(runtime_t *rt, int code) {
    int pid = rt->current_pid;
    if (pid < 0) {
        return;
    }
    if (proc_mark_exited(pid, code, rt->tick)) {
        ++rt->completed;
        trace_event(&rt->trace, rt->tick, pid, "EXIT", "workload complete");
    }
    rt->current_pid = -1;
}

static void block_current(runtime_t *rt, const char *why) {
    int pid = rt->current_pid;
    if (pid < 0) {
        return;
    }
    if (proc_set_state(pid, PROC_BLOCKED, rt->tick)) {
        trace_event(&rt->trace, rt->tick, pid, "BLOCK", why);
    }
    rt->current_pid = -1;
}

static void maybe_preempt(runtime_t *rt) {
    int pid = rt->current_pid;
    if (pid < 0) {
        return;
    }
    bool preempt =
        rt->need_resched || timer_need_resched() || sched_should_preempt(&rt->scheduler, pid);
    if (!preempt) {
        return;
    }
    if (sched_algo(&rt->scheduler) == SCHED_MLFQ) {
        sched_demote_after_quantum(&rt->scheduler, pid);
    }
    rt->need_resched = false;
    timer_clear_resched();
    enqueue_ready(rt, pid);
    trace_event(&rt->trace, rt->tick, pid, "YIELD", "quantum/preempt");
    rt->current_pid = -1;
}

static void complete_immediate(runtime_t *rt, int idx) {
    if (idx >= 0) {
        rt->slots[idx].pc++;
        rt->slots[idx].remaining = 0U;
    }
}

static bool execute_instruction(runtime_t *rt) {
    int pid = rt->current_pid;
    int idx = slot_index(rt, pid);
    if (idx < 0) {
        exit_current(rt, 1);
        return false;
    }
    if (rt->slots[idx].pc >= rt->slots[idx].workload.count) {
        exit_current(rt, 0);
        return false;
    }

    workload_insn_t *insn = &rt->slots[idx].workload.insns[rt->slots[idx].pc];
    char detail[96];
    switch (insn->op) {
    case WORKLOAD_CPU:
        if (rt->slots[idx].remaining == 0U) {
            rt->slots[idx].remaining = insn->ticks;
        }
        pcb_t *p = proc_get(pid);
        if (p == NULL || rt->slots[idx].remaining == 0U) {
            exit_current(rt, 1);
            return false;
        }
        proc_for_each(account_ready_wait, NULL);
        ++p->cpu_time;
        ++rt->busy_ticks;
        --rt->slots[idx].remaining;
        ++rt->tick;
        sched_on_tick(&rt->scheduler, pid);
        if (rt->slots[idx].remaining == 0U) {
            ++rt->slots[idx].pc;
        }
        trace_event(&rt->trace, rt->tick, pid, "CPU", "tick");
        maybe_preempt(rt);
        return true;
    case WORKLOAD_IO: {
        pcb_t *io_p = proc_get(pid);
        if (io_p == NULL) {
            exit_current(rt, 1);
            return false;
        }
        io_p->wakeup_tick = rt->tick + insn->ticks;
        make_detail(detail, sizeof(detail), "until", (int)io_p->wakeup_tick);
        complete_immediate(rt, idx);
        block_current(rt, detail);
        return false;
    }
    case WORKLOAD_SEND: {
        ipc_result_t r = ipc_send(insn->arg0, pid, insn->arg1);
        if (r == IPC_ERROR) {
            exit_current(rt, 3);
            return false;
        }
        if (r == IPC_WOULD_BLOCK) {
            ipc_wait_sender(insn->arg0, pid);
            block_current(rt, "send full");
            return false;
        }
        int wake = ipc_pop_receiver_waiter(insn->arg0);
        if (wake >= 0) {
            wake_pid(rt, wake, "message");
        }
        complete_immediate(rt, idx);
        return false;
    }
    case WORKLOAD_RECV: {
        ipc_message_t msg;
        ipc_result_t r = ipc_recv(insn->arg0, pid, &msg);
        if (r == IPC_ERROR) {
            exit_current(rt, 3);
            return false;
        }
        if (r == IPC_WOULD_BLOCK) {
            ipc_wait_receiver(insn->arg0, pid);
            block_current(rt, "recv empty");
            return false;
        }
        int wake = ipc_pop_sender_waiter(insn->arg0);
        if (wake >= 0) {
            wake_pid(rt, wake, "queue space");
        }
        complete_immediate(rt, idx);
        return false;
    }
    case WORKLOAD_SEM_WAIT: {
        sync_result_t r = sync_wait(insn->arg0, pid);
        if (r == SYNC_ERROR) {
            exit_current(rt, 4);
            return false;
        }
        if (r == SYNC_WOULD_BLOCK) {
            block_current(rt, "sem wait");
            return false;
        }
        complete_immediate(rt, idx);
        return false;
    }
    case WORKLOAD_SEM_POST: {
        int wake = -1;
        if (sync_post(insn->arg0, pid, &wake) == SYNC_ERROR) {
            exit_current(rt, 4);
            return false;
        }
        if (wake >= 0) {
            wake_pid(rt, wake, "sem post");
        }
        complete_immediate(rt, idx);
        return false;
    }
    case WORKLOAD_LOCK: {
        sync_result_t r = sync_mutex_lock(insn->arg0, pid);
        if (r == SYNC_ERROR) {
            exit_current(rt, 4);
            return false;
        }
        if (r == SYNC_WOULD_BLOCK) {
            block_current(rt, "mutex lock");
            return false;
        }
        complete_immediate(rt, idx);
        return false;
    }
    case WORKLOAD_UNLOCK: {
        int wake = -1;
        if (sync_mutex_unlock(insn->arg0, pid, &wake) == SYNC_ERROR) {
            exit_current(rt, 4);
            return false;
        }
        if (wake >= 0) {
            wake_pid(rt, wake, "mutex unlock");
        }
        complete_immediate(rt, idx);
        return false;
    }
    case WORKLOAD_MMAP: {
        pcb_t *p = proc_get(pid);
        if (p == NULL || p->vm == NULL) {
            exit_current(rt, 5);
            return false;
        }
        uint16_t addr = vm_malloc(pid, p->vm, insn->arg0, rt->tick);
        if (addr == 0) {
            exit_current(rt, 5);
            return false;
        }
        p->vm_last_alloc = addr;
        snprintf(detail, sizeof(detail), "size=%d addr=0x%04X", insn->arg0, addr);
        trace_event(&rt->trace, rt->tick, pid, "MMAP", detail);
        complete_immediate(rt, idx);
        return false;
    }
    case WORKLOAD_ACCESS: {
        pcb_t *p = proc_get(pid);
        if (p == NULL || p->vm == NULL) {
            exit_current(rt, 5);
            return false;
        }
        uint16_t addr =
            (insn->arg0 < 0) ? (uint16_t)(p->vm_last_alloc - insn->arg0) : (uint16_t)insn->arg0;
        bool is_write = (insn->arg1 != 0);
        uint8_t byte_val = 0xAA;

        tlb_stats_t tlb_before = tlb_get_stats();
        pager_stats_t pager_before = pager_get_stats();

        int status;
        if (is_write) {
            status = vm_write_mem(pid, p->vm, addr, &byte_val, 1, rt->tick);
        } else {
            status = vm_read_mem(pid, p->vm, addr, &byte_val, 1, rt->tick);
        }

        if (status < 0) {
            exit_current(rt, 6);
            return false;
        }

        pager_stats_t pager_after = pager_get_stats();
        tlb_stats_t tlb_after = tlb_get_stats();

        bool hit_tlb = (tlb_after.hits > tlb_before.hits);
        bool minor_fault = (pager_after.minor_faults > pager_before.minor_faults);
        bool major_fault = (pager_after.major_faults > pager_before.major_faults);

        snprintf(detail, sizeof(detail), "%s addr=0x%04X %s", is_write ? "write" : "read", addr,
                 hit_tlb
                     ? "TLB-hit"
                     : (minor_fault ? "minor-fault" : (major_fault ? "major-fault" : "page-hit")));
        trace_event(&rt->trace, rt->tick, pid, "ACCESS", detail);

        if (major_fault) {
            /* major fault blocks process for simulated disk read latency.
             * Do not advance PC (re-execute access instruction). */
            p->wakeup_tick = rt->tick + VM_PAGE_FAULT_LATENCY;
            block_current(rt, "page fault");
            return false;
        }

        complete_immediate(rt, idx);
        return false;
    }
    case WORKLOAD_MFREE: {
        pcb_t *p = proc_get(pid);
        if (p == NULL || p->vm == NULL) {
            exit_current(rt, 5);
            return false;
        }
        vm_free(pid, p->vm, p->vm_last_alloc, rt->tick);
        snprintf(detail, sizeof(detail), "addr=0x%04X", p->vm_last_alloc);
        trace_event(&rt->trace, rt->tick, pid, "MFREE", detail);
        p->vm_last_alloc = 0;
        complete_immediate(rt, idx);
        return false;
    }
    case WORKLOAD_EXIT:
        exit_current(rt, 0);
        return false;
    default:
        exit_current(rt, 2);
        return false;
    }
}

void runtime_init(runtime_t *rt) {
    if (rt == NULL) {
        return;
    }
    memset(rt, 0, sizeof(*rt));
    memset(g_slot_by_pid, -1, sizeof(g_slot_by_pid));
    rt->current_pid = -1;
    proc_table_init();
    vm_init();
    sched_init(&rt->scheduler, SCHED_RR, DEFAULT_QUANTUM);
    trace_init(&rt->trace);
    ipc_init();
    sync_init();
    timer_init();
}

void runtime_shutdown(runtime_t *rt) {
    if (rt != NULL) {
        rt->started = false;
        rt->current_pid = -1;
    }
    timer_stop();
    proc_table_shutdown();
    vm_shutdown();
}

int runtime_run_workload(runtime_t *rt, const char *path, int priority) {
    if (rt == NULL || path == NULL) {
        return -1;
    }
    char err[ERR_BUF_SIZE];
    workload_t workload;
    char candidate1[256];
    char candidate2[256];
    const char *trimmed = path;
    const char prefix[] = "workloads/";
    if (strncmp(trimmed, prefix, sizeof(prefix) - 1U) == 0) {
        trimmed += sizeof(prefix) - 1U;
    }

    snprintf(candidate1, sizeof(candidate1), "workloads/%s", trimmed);
    snprintf(candidate2, sizeof(candidate2), "mosrt/workloads/%s", trimmed);

    if (workload_load(path, &workload, err, sizeof(err)) != 0 &&
        workload_load(candidate1, &workload, err, sizeof(err)) != 0 &&
        workload_load(candidate2, &workload, err, sizeof(err)) != 0) {
        printf("error: failed to open workload '%s'\n", path);
        printf("hint: try run mixed.wl, run workloads/mixed.wl, or run mosrt/workloads/mixed.wl\n");
        return -1;
    }
    int pid = proc_create(0, priority, rt->tick, 0U);
    if (pid < 0) {
        return -1;
    }
    pcb_t *p = proc_get(pid);
    if (p != NULL) {
        p->vm = vm_proc_init(pid);
        if (p->vm == NULL) {
            proc_destroy(pid, 1);
            return -1;
        }
    }
    if (p != NULL && getcontext(&p->context) == 0) {
        p->context.uc_stack.ss_sp = p->stack;
        p->context.uc_stack.ss_size = p->stack_size;
        p->context.uc_link = NULL;
        makecontext(&p->context, (void (*)(void))process_context_trampoline, 1, pid);
    }
    int idx = alloc_slot(rt, pid);
    if (idx < 0) {
        proc_destroy(pid, 1);
        return -1;
    }
    rt->slots[idx].workload = workload;
    enqueue_ready(rt, pid);
    trace_event(&rt->trace, rt->tick, pid, "CREATE", workload.name);
    return pid;
}

void runtime_step(runtime_t *rt, unsigned ticks) {
    if (rt == NULL) {
        return;
    }
    for (unsigned i = 0U; i < ticks; ++i) {
        bool consumed = false;
        unsigned guard = 0U;
        while (!consumed && guard++ < MAX_DISPATCH_GUARD) {
            proc_for_each(wake_blocked_by_time, rt);
            sched_age_ready(&rt->scheduler, rt->tick);
            if (sched_algo(&rt->scheduler) == SCHED_PRIO) {
                rebuild_ready_queues(rt);
            }
            if (sched_algo(&rt->scheduler) == SCHED_MLFQ &&
                rt->tick - rt->scheduler.last_boost_tick >= MLFQ_BOOST_INTERVAL) {
                sched_priority_boost(&rt->scheduler);
                rt->scheduler.last_boost_tick = rt->tick;
                rebuild_ready_queues(rt);
                trace_event(&rt->trace, rt->tick, -1, "BOOST", "mlfq");
            }
            if (!dispatch(rt)) {
                proc_for_each(account_ready_wait, NULL);
                ++rt->idle_ticks;
                ++rt->tick;
                trace_event(&rt->trace, rt->tick, -1, "IDLE", "no ready process");
                consumed = true;
            } else {
                consumed = execute_instruction(rt);
            }
        }
    }
}

bool runtime_kill(runtime_t *rt, int pid) {
    if (rt == NULL || proc_get(pid) == NULL) {
        return false;
    }
    if (rt->current_pid == pid) {
        rt->current_pid = -1;
    }
    if (!proc_mark_exited(pid, 130, rt->tick)) {
        return false;
    }
    int idx = slot_index(rt, pid);
    if (idx >= 0) {
        memset(&rt->slots[idx], 0, sizeof(rt->slots[idx]));
        if (pid >= 1 && pid <= MOSRT_MAX_PROCS) {
            g_slot_by_pid[pid - 1] = -1;
        }
    }
    trace_event(&rt->trace, rt->tick, pid, "KILL", "requested");
    rebuild_ready_queues(rt);
    return true;
}

bool runtime_start(runtime_t *rt) {
    if (rt == NULL) {
        return false;
    }
    rt->started = true;
    return timer_start(rt->scheduler.quantum_ticks);
}

void runtime_stop(runtime_t *rt) {
    if (rt != NULL) {
        rt->started = false;
    }
    timer_stop();
}

void runtime_yield(runtime_t *rt) {
    if (rt != NULL) {
        rt->need_resched = true;
    }
}

void runtime_set_scheduler(runtime_t *rt, sched_algo_t algo) {
    if (rt == NULL) {
        return;
    }
    sched_set_algo(&rt->scheduler, algo);
    rebuild_ready_queues(rt);
}

void runtime_set_quantum(runtime_t *rt, unsigned ticks) {
    if (rt != NULL) {
        sched_set_quantum(&rt->scheduler, ticks);
    }
}

void runtime_trace(runtime_t *rt, bool all, int pid) {
    if (rt != NULL) {
        trace_configure(&rt->trace, all, pid);
    }
}

void runtime_dump_queues(runtime_t *rt, FILE *out) {
    if (rt != NULL) {
        sched_dump(&rt->scheduler, out);
    }
}

typedef struct {
    unsigned completed;
    uint64_t turnaround_sum;
    uint64_t response_sum;
    uint64_t wait_sum;
} metric_accum_t;

static void collect_metrics(pcb_t *p, void *ctx) {
    metric_accum_t *m = ctx;
    if (p->state == PROC_EXITED) {
        ++m->completed;
        m->turnaround_sum += p->finish_tick - p->start_tick;
        m->wait_sum += p->wait_time;
        if (p->response_time != UINT64_MAX) {
            m->response_sum += p->response_time;
        }
    }
}

static void export_metric_row(pcb_t *p, void *ctx) {
    FILE *out = ctx;
    uint64_t turnaround = p->finish_tick >= p->start_tick ? p->finish_tick - p->start_tick : 0U;
    fprintf(out,
            "%d,%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%d,%u\n",
            p->pid, proc_state_to_string(p->state), p->cpu_time, p->wait_time,
            p->response_time == UINT64_MAX ? 0U : p->response_time, turnaround, p->start_tick,
            p->finish_tick, p->priority, p->mlfq_level);
}

void runtime_print_metrics(runtime_t *rt, FILE *out) {
    if (rt == NULL) {
        return;
    }
    if (out == NULL) {
        out = stdout;
    }
    metric_accum_t m = {0};
    proc_for_each(collect_metrics, &m);
    uint64_t total = rt->busy_ticks + rt->idle_ticks;
    double util = total == 0U ? 0.0 : (100.0 * (double)rt->busy_ticks / (double)total);
    double throughput = rt->tick == 0U ? 0.0 : (double)m.completed / (double)rt->tick;
    fprintf(out, "ticks=%" PRIu64 " completed=%u throughput=%.3f cpu_util=%.2f%%\n", rt->tick,
            m.completed, throughput, util);
    if (m.completed > 0U) {
        fprintf(out, "avg_turnaround=%.2f avg_wait=%.2f avg_response=%.2f\n",
                (double)m.turnaround_sum / (double)m.completed,
                (double)m.wait_sum / (double)m.completed,
                (double)m.response_sum / (double)m.completed);
    }
}

int runtime_export_metrics(runtime_t *rt, const char *path) {
    if (rt == NULL || path == NULL) {
        return -1;
    }
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        return -1;
    }
    fprintf(fp, "pid,state,cpu,wait,response,turnaround,start,finish,priority,queue\n");
    proc_for_each(export_metric_row, fp);
    fclose(fp);
    return 0;
}

int runtime_export_trace(runtime_t *rt, const char *path) {
    return rt == NULL ? -1 : trace_export_csv(&rt->trace, path);
}

bool runtime_set_nice(runtime_t *rt, int pid, int nice_value) {
    (void)rt;
    pcb_t *p = proc_get(pid);
    if (p == NULL) {
        return false;
    }
    if (nice_value < -20) {
        nice_value = -20;
    } else if (nice_value > 19) {
        nice_value = 19;
    }
    p->nice = nice_value;
    int prio = p->base_priority + nice_value;
    if (prio < MOSRT_MIN_PRIO) {
        prio = MOSRT_MIN_PRIO;
    } else if (prio > MOSRT_MAX_PRIO) {
        prio = MOSRT_MAX_PRIO;
    }
    p->priority = prio;
    return true;
}

bool runtime_set_priority(runtime_t *rt, int pid, int priority) {
    (void)rt;
    pcb_t *p = proc_get(pid);
    if (p == NULL) {
        return false;
    }
    if (priority < MOSRT_MIN_PRIO) {
        priority = MOSRT_MIN_PRIO;
    } else if (priority > MOSRT_MAX_PRIO) {
        priority = MOSRT_MAX_PRIO;
    }
    p->base_priority = priority;
    p->priority = priority;
    return true;
}

static void bench_one(FILE *out, sched_algo_t algo) {
    runtime_t rt;
    runtime_init(&rt);
    runtime_set_scheduler(&rt, algo);
    (void)runtime_run_workload(&rt, "workloads/cpu_bound.wl", MOSRT_DEFAULT_PRIO);
    (void)runtime_run_workload(&rt, "workloads/io_bound.wl", MOSRT_DEFAULT_PRIO);
    (void)runtime_run_workload(&rt, "workloads/mixed.wl", MOSRT_DEFAULT_PRIO);
    for (unsigned i = 0; i < BENCH_MAX_TICKS && proc_live_count() > 0; ++i) {
        runtime_step(&rt, 1U);
    }
    fprintf(out, "%s: ", sched_algo_name(algo));
    runtime_print_metrics(&rt, out);
    runtime_shutdown(&rt);
}

void runtime_benchmark(FILE *out) {
    if (out == NULL) {
        out = stdout;
    }
    bench_one(out, SCHED_FCFS);
    bench_one(out, SCHED_RR);
    bench_one(out, SCHED_PRIO);
    bench_one(out, SCHED_MLFQ);
}

uint64_t runtime_tick(const runtime_t *rt) {
    return rt == NULL ? 0U : rt->tick;
}

const char *runtime_scheduler_name(const runtime_t *rt) {
    return rt == NULL ? "none" : sched_algo_name(sched_algo(&rt->scheduler));
}
