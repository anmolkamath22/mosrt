#ifndef MOSRT_VM_ALLOC_H
#define MOSRT_VM_ALLOC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Forward declare vm_state_t to avoid circular dependency. */
struct vm_state;

typedef struct {
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t allocated_blocks;
    uint32_t internal_frag_bytes;
    uint32_t external_frag_bytes;
} vm_alloc_stats_t;

/* Allocate a block of memory of 'size' bytes. Returns virtual address, or 0 on error. */
uint16_t vm_alloc_malloc(int pid, struct vm_state *vm, size_t size, uint64_t tick);

/* Free a previously allocated block of memory. */
void vm_alloc_free(int pid, struct vm_state *vm, uint16_t addr, uint64_t tick);

/* Get heap allocator fragmentation and utilization statistics. */
vm_alloc_stats_t vm_alloc_get_stats(int pid, struct vm_state *vm, uint64_t tick);

#endif /* MOSRT_VM_ALLOC_H */
