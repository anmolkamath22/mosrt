#include "shell.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "proc.h"
#include "runtime.h"
#include "sched.h"

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
    if (v < -2147483647L - 1L || v > 2147483647L) {
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

static void cmd_ps(void) {
    proc_dump(stdout);
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
    int ms = 0;
    if (argc < 2 || !parse_int(argv[1], &ms) || ms <= 0) {
        printf("usage: quantum <ms>\n");
        return;
    }

    runtime_set_quantum(&ctx->runtime, (unsigned)ms);
    printf("quantum set to %u tick(s)\n", (unsigned)ms);
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

static void cmd_start(shell_ctx_t *ctx) {
    if (!runtime_start(&ctx->runtime)) {
        printf("error: failed to start timer\n");
        return;
    }
    printf("runtime started at tick=%" PRIu64 "\n", runtime_tick(&ctx->runtime));
}

static void cmd_stop(shell_ctx_t *ctx) {
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

        if (strcmp(argv[0], "help") == 0) {
            cmd_help();
        } else if (strcmp(argv[0], "run") == 0) {
            cmd_run(&ctx, argc, argv);
        } else if (strcmp(argv[0], "ps") == 0) {
            cmd_ps();
        } else if (strcmp(argv[0], "kill") == 0) {
            cmd_kill(&ctx, argc, argv);
        } else if (strcmp(argv[0], "sched") == 0) {
            cmd_sched(&ctx, argc, argv);
        } else if (strcmp(argv[0], "quantum") == 0) {
            cmd_quantum(&ctx, argc, argv);
        } else if (strcmp(argv[0], "nice") == 0) {
            cmd_nice(&ctx, argc, argv);
        } else if (strcmp(argv[0], "prio") == 0) {
            cmd_prio(&ctx, argc, argv);
        } else if (strcmp(argv[0], "trace") == 0) {
            cmd_trace(&ctx, argc, argv);
        } else if (strcmp(argv[0], "start") == 0) {
            cmd_start(&ctx);
        } else if (strcmp(argv[0], "stop") == 0) {
            cmd_stop(&ctx);
        } else if (strcmp(argv[0], "step") == 0) {
            cmd_step(&ctx, argc, argv);
        } else if (strcmp(argv[0], "queues") == 0) {
            runtime_dump_queues(&ctx.runtime, stdout);
        } else if (strcmp(argv[0], "metrics") == 0) {
            runtime_print_metrics(&ctx.runtime, stdout);
        } else if (strcmp(argv[0], "export") == 0) {
            cmd_export(&ctx, argc, argv);
        } else if (strcmp(argv[0], "bench") == 0) {
            runtime_benchmark(stdout);
            runtime_init(&ctx.runtime);
        } else if (strcmp(argv[0], "exit") == 0 || strcmp(argv[0], "quit") == 0) {
            runtime_shutdown(&ctx.runtime);
            break;
        } else {
            printf("unknown command: %s\n", argv[0]);
        }
    }
    runtime_shutdown(&ctx.runtime);
}
