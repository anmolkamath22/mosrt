#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "proc.h"
#include "runtime.h"
#include "sched.h"

typedef struct {
    const char *name;
    const char *workload;
    unsigned processes;
    unsigned max_ticks;
} bench_case_t;

typedef struct {
    unsigned completed;
    uint64_t turnaround_sum;
    uint64_t wait_sum;
    uint64_t response_sum;
} bench_metrics_t;

static const sched_algo_t k_algos[] = {
    SCHED_FCFS,
    SCHED_RR,
    SCHED_PRIO,
    SCHED_MLFQ,
};

static const bench_case_t k_cases[] = {
    {"cpu_bound", "benchmarks/workloads/cpu_bound.wl", 4U, 2000U},
    {"io_bound", "benchmarks/workloads/io_bound.wl", 4U, 2000U},
    {"mixed", "benchmarks/workloads/mixed.wl", 4U, 2000U},
    {"100_process", "benchmarks/workloads/tiny_mixed.wl", 100U, 5000U},
    {"1000_process", "benchmarks/workloads/tiny_cpu.wl", 1000U, 5000U},
};

static void collect_one(pcb_t *p, void *ctx) {
    bench_metrics_t *m = ctx;
    if (p->state != PROC_EXITED) {
        return;
    }
    ++m->completed;
    m->turnaround_sum += p->finish_tick - p->start_tick;
    m->wait_sum += p->wait_time;
    if (p->response_time != UINT64_MAX) {
        m->response_sum += p->response_time;
    }
}

static int run_case(FILE *csv, FILE *md, const bench_case_t *c, sched_algo_t algo) {
    runtime_t rt;
    runtime_init(&rt);
    runtime_set_scheduler(&rt, algo);
    runtime_set_quantum(&rt, 4U);

    for (unsigned i = 0U; i < c->processes; ++i) {
        int prio = MOSRT_DEFAULT_PRIO + (int)(i % 5U);
        if (runtime_run_workload(&rt, c->workload, prio) < 0) {
            runtime_shutdown(&rt);
            return -1;
        }
    }

    (void)runtime_start(&rt);
    for (unsigned t = 0U; t < c->max_ticks && proc_live_count() > 0; ++t) {
        runtime_step(&rt, 1U);
    }
    runtime_stop(&rt);

    bench_metrics_t m = {0};
    proc_for_each(collect_one, &m);
    uint64_t total_ticks = rt.busy_ticks + rt.idle_ticks;
    double util = total_ticks == 0U ? 0.0 : 100.0 * (double)rt.busy_ticks / (double)total_ticks;
    double throughput =
        runtime_tick(&rt) == 0U ? 0.0 : (double)m.completed / (double)runtime_tick(&rt);
    double avg_turnaround =
        m.completed == 0U ? 0.0 : (double)m.turnaround_sum / (double)m.completed;
    double avg_wait = m.completed == 0U ? 0.0 : (double)m.wait_sum / (double)m.completed;
    double avg_response = m.completed == 0U ? 0.0 : (double)m.response_sum / (double)m.completed;

    fprintf(csv, "%s,%s,%u,%" PRIu64 ",%u,%.2f,%.6f,%.2f,%.2f,%.2f\n", c->name,
            sched_algo_name(algo), c->processes, runtime_tick(&rt), m.completed, util, throughput,
            avg_turnaround, avg_wait, avg_response);
    fprintf(md, "| %s | %s | %u | %" PRIu64 " | %u | %.2f | %.6f | %.2f | %.2f | %.2f |\n", c->name,
            sched_algo_name(algo), c->processes, runtime_tick(&rt), m.completed, util, throughput,
            avg_turnaround, avg_wait, avg_response);

    runtime_shutdown(&rt);
    return 0;
}

int main(int argc, char **argv) {
    const char *csv_path = argc > 1 ? argv[1] : "benchmarks/results/scheduler_comparison.csv";
    const char *md_path = argc > 2 ? argv[2] : "benchmarks/results/scheduler_comparison.md";

    FILE *csv = fopen(csv_path, "w");
    if (csv == NULL) {
        perror(csv_path);
        return 1;
    }
    FILE *md = fopen(md_path, "w");
    if (md == NULL) {
        perror(md_path);
        fclose(csv);
        return 1;
    }

    fprintf(csv, "scenario,scheduler,processes,ticks,completed,cpu_util,throughput,avg_turnaround,"
                 "avg_wait,avg_response\n");
    fprintf(md, "# MOSRT Scheduler Benchmark Results\n\n");
    fprintf(md, "| Scenario | Scheduler | Processes | Ticks | Completed | CPU Util %% | Throughput "
                "| Avg Turnaround | Avg Wait | Avg Response |\n");
    fprintf(md, "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n");

    int rc = 0;
    for (size_t i = 0U; i < sizeof(k_cases) / sizeof(k_cases[0]); ++i) {
        for (size_t j = 0U; j < sizeof(k_algos) / sizeof(k_algos[0]); ++j) {
            if (run_case(csv, md, &k_cases[i], k_algos[j]) != 0) {
                rc = 1;
            }
        }
    }

    fclose(md);
    fclose(csv);
    return rc;
}
