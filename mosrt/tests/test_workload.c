#include <assert.h>
#include <stdio.h>

#include "workload.h"

int main(void) {
    workload_t workload;
    char err[160];

    assert(workload_load("workloads/cpu_bound.wl", &workload, err, sizeof(err)) == 0);
    assert(workload.count >= 2U);
    assert(workload.insns[0].op == WORKLOAD_CPU);
    assert(workload.insns[0].ticks > 0U);
    assert(workload.insns[workload.count - 1U].op == WORKLOAD_EXIT);
    assert(workload_load("workloads/io_bound.wl", &workload, err, sizeof(err)) == 0);
    assert(workload.insns[1].op == WORKLOAD_IO);
    assert(workload_op_name(WORKLOAD_SEND) != NULL);
    return 0;
}
