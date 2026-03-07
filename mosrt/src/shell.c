#include "shell.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "proc.h"

#define SHELL_MAX_LINE 256
#define SHELL_MAX_ARGS 8
#define SHELL_MAX_TRACKED_PIDS MOSRT_MAX_PROCS

typedef enum {
    SCHED_RR = 0,
    SCHED_PRIO,
    SCHED_MLFQ
} sched_algo_t;

typedef struct {
    uint64_t tick;
    sched_algo_t sched_algo;
    unsigned quantum_ms;
    int trace_all;
    int trace_pid;
    int started;
    int tracked_pids[SHELL_MAX_TRACKED_PIDS];
    int tracked_count;
    int rr_cursor;
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

static int is_alive_pid(const shell_ctx_t *ctx, int pid) {
    for (int i = 0; i < ctx->tracked_count; ++i) {
        if (ctx->tracked_pids[i] == pid) {
            const pcb_t *p = proc_get_const(pid);
            return p != NULL && p->used;
        }
    }
    return 0;
}

static int pick_next_ready_pid(shell_ctx_t *ctx) {
    if (ctx->tracked_count == 0) {
        return -1;
    }

    for (int i = 0; i < ctx->tracked_count; ++i) {
        int idx = (ctx->rr_cursor + i) % ctx->tracked_count;
        int pid = ctx->tracked_pids[idx];
        const pcb_t *p = proc_get_const(pid);
        if (p != NULL && p->used && p->state == PROC_READY) {
            ctx->rr_cursor = (idx + 1) % ctx->tracked_count;
            return pid;
        }
    }
    return -1;
}

static void cmd_help(void) {
    printf("commands:\n");
    printf("  run <workload>\n");
    printf("  ps\n");
    printf("  kill <pid>\n");
    printf("  sched <rr|prio|mlfq>\n");
    printf("  quantum <ms>\n");
    printf("  trace <pid|all>\n");
    printf("  start\n");
    printf("  step <n>\n");
    printf("  help\n");
    printf("  exit\n");
}

static void cmd_run(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    if (argc < 2) {
        printf("usage: run <workload>\n");
        return;
    }
    if (ctx->tracked_count >= SHELL_MAX_TRACKED_PIDS) {
        printf("error: process tracking table full\n");
        return;
    }

    int pid = proc_create(0, 0, ctx->tick, 0);
    if (pid < 0) {
        printf("error: failed to create process\n");
        return;
    }

    (void)proc_set_state(pid, PROC_READY, ctx->tick);
    ctx->tracked_pids[ctx->tracked_count++] = pid;
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

    if (!is_alive_pid(ctx, pid)) {
        printf("error: pid %d not found\n", pid);
        return;
    }

    proc_destroy(pid, 0);
    printf("killed pid=%d\n", pid);
}

static void cmd_sched(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    if (argc < 2) {
        printf("usage: sched <rr|prio|mlfq>\n");
        return;
    }

    if (strcmp(argv[1], "rr") == 0) {
        ctx->sched_algo = SCHED_RR;
    } else if (strcmp(argv[1], "prio") == 0) {
        ctx->sched_algo = SCHED_PRIO;
    } else if (strcmp(argv[1], "mlfq") == 0) {
        ctx->sched_algo = SCHED_MLFQ;
    } else {
        printf("error: unknown scheduler '%s'\n", argv[1]);
        return;
    }

    printf("scheduler set to %s\n", argv[1]);
}

static void cmd_quantum(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    int ms = 0;
    if (argc < 2 || !parse_int(argv[1], &ms) || ms <= 0) {
        printf("usage: quantum <ms>\n");
        return;
    }

    ctx->quantum_ms = (unsigned)ms;
    printf("quantum set to %u ms\n", ctx->quantum_ms);
}

static void cmd_trace(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    int pid = 0;
    if (argc < 2) {
        printf("usage: trace <pid|all>\n");
        return;
    }

    if (strcmp(argv[1], "all") == 0) {
        ctx->trace_all = 1;
        ctx->trace_pid = -1;
        printf("trace target set to all\n");
        return;
    }

    if (!parse_int(argv[1], &pid)) {
        printf("usage: trace <pid|all>\n");
        return;
    }

    ctx->trace_all = 0;
    ctx->trace_pid = pid;
    printf("trace target set to pid=%d\n", pid);
}

static void run_one_tick(shell_ctx_t *ctx) {
    int pid = pick_next_ready_pid(ctx);
    if (pid < 0) {
        ctx->tick++;
        if (ctx->trace_all) {
            printf("[tick=%" PRIu64 "] idle\n", ctx->tick);
        }
        return;
    }

    if (!proc_set_state(pid, PROC_RUNNING, ctx->tick)) {
        ctx->tick++;
        return;
    }

    pcb_t *p = proc_get(pid);
    if (p != NULL) {
        p->cpu_time++;
    }

    ctx->tick++;
    (void)proc_set_state(pid, PROC_READY, ctx->tick);

    if (ctx->trace_all || ctx->trace_pid == pid) {
        printf("[tick=%" PRIu64 "] ran pid=%d\n", ctx->tick, pid);
    }
}

static void cmd_start(shell_ctx_t *ctx) {
    ctx->started = 1;
    printf("runtime started at tick=%" PRIu64 "\n", ctx->tick);
}

static void cmd_step(shell_ctx_t *ctx, int argc, char *argv[SHELL_MAX_ARGS]) {
    int n = 0;
    if (argc < 2 || !parse_int(argv[1], &n) || n <= 0) {
        printf("usage: step <n>\n");
        return;
    }
    if (!ctx->started) {
        printf("error: runtime not started (use 'start')\n");
        return;
    }

    for (int i = 0; i < n; ++i) {
        run_one_tick(ctx);
    }
    printf("advanced %d tick(s), now at tick=%" PRIu64 "\n", n, ctx->tick);
}

void shell_run_repl(void) {
    shell_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.sched_algo = SCHED_RR;
    ctx.quantum_ms = 10U;
    ctx.trace_pid = -1;

    proc_table_init();
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
        } else if (strcmp(argv[0], "trace") == 0) {
            cmd_trace(&ctx, argc, argv);
        } else if (strcmp(argv[0], "start") == 0) {
            cmd_start(&ctx);
        } else if (strcmp(argv[0], "step") == 0) {
            cmd_step(&ctx, argc, argv);
        } else if (strcmp(argv[0], "exit") == 0 || strcmp(argv[0], "quit") == 0) {
            break;
        } else {
            printf("unknown command: %s\n", argv[0]);
        }
    }
}
