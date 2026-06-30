#ifndef MOSRT_WORKLOAD_H
#define MOSRT_WORKLOAD_H

#include <stddef.h>
#include <stdint.h>

#define MOSRT_MAX_WORKLOAD_INSNS 128

typedef enum {
    WORKLOAD_CPU = 0,
    WORKLOAD_IO,
    WORKLOAD_SEND,
    WORKLOAD_RECV,
    WORKLOAD_SEM_WAIT,
    WORKLOAD_SEM_POST,
    WORKLOAD_LOCK,
    WORKLOAD_UNLOCK,
    WORKLOAD_EXIT
} workload_op_t;

typedef struct {
    workload_op_t op;
    int arg0;
    int arg1;
    uint64_t ticks;
} workload_insn_t;

typedef struct {
    char name[128];
    workload_insn_t insns[MOSRT_MAX_WORKLOAD_INSNS];
    size_t count;
} workload_t;

/** Parse a deterministic MOSRT workload script from disk. */
int workload_load(const char *path, workload_t *out, char *err, size_t errsz);
/** Convert an opcode to a stable printable name. */
const char *workload_op_name(workload_op_t op);

#endif
