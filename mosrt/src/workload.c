#include "workload.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static char *trim(char *s) {
    while (is_space((unsigned char)*s)) {
        ++s;
    }
    char *end = s + strlen(s);
    while (end > s && is_space((unsigned char)end[-1])) {
        *--end = '\0';
    }
    return s;
}

static int parse_u64(const char *s, uint64_t *out) {
    char *end = NULL;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 10);
    if (s == end || *end != '\0' || errno == ERANGE) {
        return 0;
    }
    *out = (uint64_t)v;
    return 1;
}

static int parse_i32(const char *s, int *out) {
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (s == end || *end != '\0' || errno == ERANGE || v < INT_MIN || v > INT_MAX) {
        return 0;
    }
    *out = (int)v;
    return 1;
}

static int add_insn(workload_t *w, workload_insn_t insn, char *err, size_t errsz) {
    if (w->count >= MOSRT_MAX_WORKLOAD_INSNS) {
        snprintf(err, errsz, "too many workload instructions");
        return -1;
    }
    w->insns[w->count++] = insn;
    return 0;
}

int workload_load(const char *path, workload_t *out, char *err, size_t errsz) {
    if (path == NULL || out == NULL || err == NULL || errsz == 0U) {
        return -1;
    }
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        snprintf(err, errsz, "failed to open workload '%s'", path);
        return -1;
    }

    memset(out, 0, sizeof(*out));
    snprintf(out->name, sizeof(out->name), "%s", path);

    char line[256];
    unsigned lineno = 0U;
    while (fgets(line, sizeof(line), fp) != NULL) {
        ++lineno;
        char *hash = strchr(line, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        char *body = trim(line);
        if (*body == '\0') {
            continue;
        }

        char *argv[4] = {0};
        int argc = 0;
        char *tok = strtok(body, " \t\r\n");
        while (tok != NULL && argc < 4) {
            argv[argc++] = tok;
            tok = strtok(NULL, " \t\r\n");
        }
        if (tok != NULL) {
            snprintf(err, errsz, "%s:%u: too many instruction fields", path, lineno);
            fclose(fp);
            return -1;
        }

        if (argc == 0) {
            continue;
        }

        workload_insn_t insn = {0};
        if (strcmp(argv[0], "CPU") == 0 && argc == 2 && parse_u64(argv[1], &insn.ticks)) {
            if (insn.ticks == 0U) {
                snprintf(err, errsz, "%s:%u: CPU burst must be greater than zero", path, lineno);
                fclose(fp);
                return -1;
            }
            insn.op = WORKLOAD_CPU;
        } else if (strcmp(argv[0], "IO") == 0 && argc == 2 && parse_u64(argv[1], &insn.ticks)) {
            if (insn.ticks == 0U) {
                snprintf(err, errsz, "%s:%u: IO burst must be greater than zero", path, lineno);
                fclose(fp);
                return -1;
            }
            insn.op = WORKLOAD_IO;
        } else if (strcmp(argv[0], "SEND") == 0 && argc == 3 && parse_i32(argv[1], &insn.arg0) &&
                   parse_i32(argv[2], &insn.arg1)) {
            insn.op = WORKLOAD_SEND;
        } else if (strcmp(argv[0], "RECV") == 0 && argc == 2 && parse_i32(argv[1], &insn.arg0)) {
            insn.op = WORKLOAD_RECV;
        } else if (strcmp(argv[0], "SEM_WAIT") == 0 && argc == 2 &&
                   parse_i32(argv[1], &insn.arg0)) {
            insn.op = WORKLOAD_SEM_WAIT;
        } else if (strcmp(argv[0], "SEM_POST") == 0 && argc == 2 &&
                   parse_i32(argv[1], &insn.arg0)) {
            insn.op = WORKLOAD_SEM_POST;
        } else if (strcmp(argv[0], "LOCK") == 0 && argc == 2 && parse_i32(argv[1], &insn.arg0)) {
            insn.op = WORKLOAD_LOCK;
        } else if (strcmp(argv[0], "UNLOCK") == 0 && argc == 2 && parse_i32(argv[1], &insn.arg0)) {
            insn.op = WORKLOAD_UNLOCK;
        } else if (strcmp(argv[0], "MMAP") == 0 && argc == 2 && parse_i32(argv[1], &insn.arg0)) {
            insn.op = WORKLOAD_MMAP;
        } else if (strcmp(argv[0], "ACCESS") == 0 && argc == 3 && parse_i32(argv[1], &insn.arg0) &&
                   parse_i32(argv[2], &insn.arg1)) {
            insn.op = WORKLOAD_ACCESS;
        } else if (strcmp(argv[0], "MFREE") == 0 && argc == 1) {
            insn.op = WORKLOAD_MFREE;
        } else if (strcmp(argv[0], "EXIT") == 0 && argc == 1) {
            insn.op = WORKLOAD_EXIT;
        } else {
            snprintf(err, errsz, "%s:%u: invalid instruction", path, lineno);
            fclose(fp);
            return -1;
        }

        if (add_insn(out, insn, err, errsz) != 0) {
            fclose(fp);
            return -1;
        }
    }
    fclose(fp);

    if (out->count == 0U || out->insns[out->count - 1U].op != WORKLOAD_EXIT) {
        workload_insn_t exit_insn = {.op = WORKLOAD_EXIT};
        return add_insn(out, exit_insn, err, errsz);
    }
    return 0;
}

const char *workload_op_name(workload_op_t op) {
    switch (op) {
    case WORKLOAD_CPU:
        return "CPU";
    case WORKLOAD_IO:
        return "IO";
    case WORKLOAD_SEND:
        return "SEND";
    case WORKLOAD_RECV:
        return "RECV";
    case WORKLOAD_SEM_WAIT:
        return "SEM_WAIT";
    case WORKLOAD_SEM_POST:
        return "SEM_POST";
    case WORKLOAD_LOCK:
        return "LOCK";
    case WORKLOAD_UNLOCK:
        return "UNLOCK";
    case WORKLOAD_MMAP:
        return "MMAP";
    case WORKLOAD_ACCESS:
        return "ACCESS";
    case WORKLOAD_MFREE:
        return "MFREE";
    case WORKLOAD_EXIT:
        return "EXIT";
    default:
        return "UNKNOWN";
    }
}
