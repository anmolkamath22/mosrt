#include "shell.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "proc.h"
#include "runtime.h"
#include "sched.h"
#include "vm.h"

#define SHELL_MAX_LINE 256
#define SHELL_MAX_ARGS 8
#define SHELL_MAX_TRACKED_PIDS MOSRT_MAX_PROCS

typedef struct {
    runtime_t runtime;
} shell_ctx_t;

static int split_args(char *line, char *argv[SHELL_MAX_ARGS]) {
    int argc = 0;
    char *tok = strtok(line, " \t\r\n");
    while (tok != NULL && argc < SHELL_MAX_ARGS) {
        argv[argc++] = tok;
        tok = strtok(NULL, " \t\r\n");
    }
    return argc;
}

static int parse_int(const char *s, int *out) {
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (s == end || *end != '\0') {
        return 0;
    }
    if (v < (long)INT_MIN || v > (long)INT_MAX) {
        return 0;
    }
    *out = (int)v;
    return 1;
}

static void cmd_help(void) {
    printf("commands:\n");
    printf("  run <workload> [priority]\n");
    printf("  ps\n");
    printf("  kill <pid>\n");
    printf("  sched <fcfs|rr|prio|mlfq>\n");
    printf("  quantum <ticks>\n");
    printf("  nice <pid> <nice>\n");
    printf("  prio <pid> <priority>\n");
    printf("  trace <pid|all>\n");
    printf("  start\n");
    printf("  stop\n");
    printf("  step <n>\n");
    printf("  queues\n");
    printf("  metrics\n");
    printf("  export trace <path.csv>\n");
    printf("  export metrics <path.csv>\n");
    printf("  bench\n");
    printf("  reset\n");
    printf("  vmmap <pid>\n");
    printf("  pte <pid>\n");
    printf("  frames\n");
    printf("  tlb\n");
    printf("  policy [fifo|lru|clock]\n");
    printf("  vmem\n");
    printf("  help\n");
    printf("  exit\n");
}

static void cmd_run(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    if (argc < 2) {
        printf("usage: run <workload>\n");
        return;
    }
    int priority = MOSRT_DEFAULT_PRIO;
    if (argc >= 3 && !parse_int(argv[2], &priority)) {
        printf("usage: run <workload> [priority]\n");
        return;
    }

    int pid = runtime_run_workload(&ctx->runtime, argv[1], priority);
    if (pid < 0) {
        printf("error: failed to create process\n");
        return;
    }

    printf("created pid=%d from workload=%s\n", pid, argv[1]);
}

static void cmd_kill(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    int pid = 0;
    if (argc < 2 || !parse_int(argv[1], &pid)) {
        printf("usage: kill <pid>\n");
        return;
    }

    if (!runtime_kill(&ctx->runtime, pid)) {
        printf("error: pid %d not found\n", pid);
        return;
    }

    printf("killed pid=%d\n", pid);
}

static void cmd_sched(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    if (argc < 2) {
        printf("usage: sched <rr|prio|mlfq>\n");
        return;
    }

    sched_algo_t algo;
    if (!sched_parse_algo(argv[1], &algo)) {
        printf("error: unknown scheduler '%s'\n", argv[1]);
        return;
    }

    runtime_set_scheduler(&ctx->runtime, algo);
    printf("scheduler set to %s\n", runtime_scheduler_name(&ctx->runtime));
}

static void cmd_quantum(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    int ticks = 0;
    if (argc < 2 || !parse_int(argv[1], &ticks) || ticks <= 0) {
        printf("usage: quantum <ticks>\n");
        return;
    }

    runtime_set_quantum(&ctx->runtime, (unsigned)ticks);
    printf("quantum set to %u tick(s)\n", (unsigned)ticks);
}

static void cmd_trace(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    int pid = 0;
    if (argc < 2) {
        printf("usage: trace <pid|all>\n");
        return;
    }

    if (strcmp(argv[1], "all") == 0) {
        runtime_trace(&ctx->runtime, true, -1);
        printf("trace target set to all\n");
        return;
    }

    if (!parse_int(argv[1], &pid)) {
        printf("usage: trace <pid|all>\n");
        return;
    }

    runtime_trace(&ctx->runtime, false, pid);
    printf("trace target set to pid=%d\n", pid);
}

static void cmd_start(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    (void)argc;
    (void)argv;
    if (!runtime_start(&ctx->runtime)) {
        printf("error: failed to start timer\n");
        return;
    }
    printf("runtime started at tick=%" PRIu64 "\n", runtime_tick(&ctx->runtime));
}

static void cmd_stop(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    (void)argc;
    (void)argv;
    runtime_stop(&ctx->runtime);
    printf("runtime stopped at tick=%" PRIu64 "\n", runtime_tick(&ctx->runtime));
}

static void cmd_step(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    int n = 0;
    if (argc < 2 || !parse_int(argv[1], &n) || n <= 0) {
        printf("usage: step <n>\n");
        return;
    }
    if (!ctx->runtime.started) {
        printf("error: runtime not started (use 'start')\n");
        return;
    }

    runtime_step(&ctx->runtime, (unsigned)n);
    printf("advanced %d tick(s), now at tick=%" PRIu64 "\n", n, runtime_tick(&ctx->runtime));
}

static void cmd_nice(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    int pid = 0;
    int nice_value = 0;
    if (argc < 3 || !parse_int(argv[1], &pid) || !parse_int(argv[2], &nice_value)) {
        printf("usage: nice <pid> <nice>\n");
        return;
    }
    if (!runtime_set_nice(&ctx->runtime, pid, nice_value)) {
        printf("error: pid %d not found\n", pid);
        return;
    }
    printf("pid=%d nice=%d\n", pid, nice_value);
}

static void cmd_prio(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    int pid = 0;
    int priority = 0;
    if (argc < 3 || !parse_int(argv[1], &pid) || !parse_int(argv[2], &priority)) {
        printf("usage: prio <pid> <priority>\n");
        return;
    }
    if (!runtime_set_priority(&ctx->runtime, pid, priority)) {
        printf("error: pid %d not found\n", pid);
        return;
    }
    printf("pid=%d priority=%d\n", pid, priority);
}

static void cmd_export(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    if (argc < 3) {
        printf("usage: export <trace|metrics> <path.csv>\n");
        return;
    }
    int rc = -1;
    if (strcmp(argv[1], "trace") == 0) {
        rc = runtime_export_trace(&ctx->runtime, argv[2]);
    } else if (strcmp(argv[1], "metrics") == 0) {
        rc = runtime_export_metrics(&ctx->runtime, argv[2]);
    } else {
        printf("usage: export <trace|metrics> <path.csv>\n");
        return;
    }
    printf("%s export %s: %s\n", argv[1], argv[2], rc == 0 ? "ok" : "failed");
}

static void cmd_reset(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    (void)argc;
    (void)argv;
    runtime_shutdown(&ctx->runtime);
    runtime_init(&ctx->runtime);
    printf("runtime reset\n");
}

static void cmd_queues(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    (void)argc;
    (void)argv;
    runtime_dump_queues(&ctx->runtime, stdout);
}

static void cmd_metrics_cmd(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    (void)argc;
    (void)argv;
    runtime_print_metrics(&ctx->runtime, stdout);
}

static void cmd_bench_cmd(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    (void)argc;
    (void)argv;
    runtime_benchmark(stdout);
    runtime_init(&ctx->runtime);
}

static void cmd_ps_cmd(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    (void)ctx;
    (void)argc;
    (void)argv;
    proc_dump(stdout);
}

static void cmd_vmmap(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    (void)ctx;
    if (argc < 2) {
        printf("usage: vmmap <pid>\n");
        return;
    }
    int pid;
    if (!parse_int(argv[1], &pid)) {
        printf("invalid pid: %s\n", argv[1]);
        return;
    }
    pcb_t *p = proc_get(pid);
    if (p == NULL || p->vm == NULL) {
        printf("pid not found or has no VM: %d\n", pid);
        return;
    }
    vm_dump_proc_map(p->vm, stdout);
}

static void cmd_pte(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    (void)ctx;
    if (argc < 2) {
        printf("usage: pte <pid>\n");
        return;
    }
    int pid;
    if (!parse_int(argv[1], &pid)) {
        printf("invalid pid: %s\n", argv[1]);
        return;
    }
    pcb_t *p = proc_get(pid);
    if (p == NULL || p->vm == NULL) {
        printf("pid not found or has no VM: %d\n", pid);
        return;
    }
    vm_dump_proc_pte(p->vm, stdout);
}

static void cmd_frames(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    (void)ctx;
    (void)argc;
    (void)argv;
    vm_dump_global_frames(stdout);
}

static void cmd_tlb_cmd(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    (void)ctx;
    (void)argc;
    (void)argv;
    vm_dump_global_tlb(stdout);
}

static void cmd_policy(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    (void)ctx;
    if (argc < 2) {
        printf("Current page replacement policy: %s\n", pager_get_policy_name());
        printf("usage to set: policy <fifo|lru|clock>\n");
        return;
    }
    if (pager_set_policy(argv[1])) {
        printf("Replacement policy changed to: %s\n", argv[1]);
    } else {
        printf("Invalid replacement policy: %s (choose fifo, lru, clock)\n", argv[1]);
    }
}

static void cmd_vmem(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    (void)ctx;
    (void)argc;
    (void)argv;
    vm_dump_global_faults(stdout);
}

static void cmd_help_cmd(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    (void)ctx;
    (void)argc;
    (void)argv;
    cmd_help();
}

typedef void (*shell_handler_fn)(shell_ctx_t *, int, char *[SHELL_MAX_ARGS]);

typedef struct {
    const char *name;
    shell_handler_fn handler;
} shell_command_t;

/* Command table — sorted by expected frequency for branch prediction. */
static const shell_command_t k_commands[] = {
    {"step", cmd_step},           {"run", cmd_run},       {"ps", cmd_ps_cmd},
    {"kill", cmd_kill},           {"sched", cmd_sched},   {"quantum", cmd_quantum},
    {"nice", cmd_nice},           {"prio", cmd_prio},     {"trace", cmd_trace},
    {"start", cmd_start},         {"stop", cmd_stop},     {"queues", cmd_queues},
    {"metrics", cmd_metrics_cmd}, {"export", cmd_export}, {"bench", cmd_bench_cmd},
    {"reset", cmd_reset},         {"vmmap", cmd_vmmap},   {"pte", cmd_pte},
    {"frames", cmd_frames},       {"tlb", cmd_tlb_cmd},   {"policy", cmd_policy},
    {"vmem", cmd_vmem},           {"help", cmd_help_cmd},
};

#define NUM_COMMANDS (sizeof(k_commands) / sizeof(k_commands[0]))

void shell_run_repl(void) {
    shell_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    runtime_init(&ctx.runtime);

    printf("MOSRT shell (type 'help' for commands)\n");

    for (;;) {
        char line[SHELL_MAX_LINE];
        char *argv[SHELL_MAX_ARGS] = {0};
        int argc = 0;

        printf("mosrt> ");
        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break;
        }

        argc = split_args(line, argv);
        if (argc == 0) {
            continue;
        }

        if (strcmp(argv[0], "exit") == 0 || strcmp(argv[0], "quit") == 0) {
            runtime_shutdown(&ctx.runtime);
            break;
        }

        bool handled = false;
        for (size_t i = 0; i < NUM_COMMANDS; ++i) {
            if (strcmp(argv[0], k_commands[i].name) == 0) {
                k_commands[i].handler(&ctx, argc, argv);
                handled = true;
                break;
            }
        }
        if (!handled) {
            printf("unknown command: %s\n", argv[0]);
        }
    }
    runtime_shutdown(&ctx.runtime);
}
