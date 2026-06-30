#ifndef MOSRT_VM_H
#define MOSRT_VM_H

#include "page_table.h"
#include "pager.h"
#include "vm_types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Structure to hold virtual memory stats per process (stored in PCB). */
typedef struct {
    uint32_t minor_faults;
    uint32_t major_faults;
    uint32_t page_out_count;
    uint32_t page_in_count;
    uint32_t tlb_hits;
    uint32_t tlb_misses;
    uint16_t heap_break;  /* Current top of heap virtual address */
    uint16_t heap_start;  /* Base of heap virtual address */
    uint16_t stack_limit; /* Bottom of stack virtual address limit */
} vm_proc_stats_t;

/* Structure representing the virtual memory space of a process. */
typedef struct vm_state {
    int pid;
    page_table_t pt;
    vm_proc_stats_t stats;
    void *policy_state;
} vm_state_t;

/* Initialize global VM subsystem (frames, TLB, swap, pager). */
void vm_init(void);

/* Shutdown global VM subsystem. */
void vm_shutdown(void);

/* Create and initialize virtual memory state for a new process. */
vm_state_t *vm_proc_init(int pid);

/* Destroy virtual memory state for a process. */
void vm_proc_destroy(vm_state_t *vm);

/* Read bytes from process virtual memory. Handles paging, translation, and permission checking.
 * Returns number of bytes successfully read, or negative on fault/error.
 */
int vm_read_mem(int pid, vm_state_t *vm, uint16_t virt_addr, uint8_t *dest, size_t len,
                uint64_t tick);

/* Write bytes to process virtual memory. Handles translation, paging, and permission checking.
 * Returns number of bytes successfully written, or negative on fault/error.
 */
int vm_write_mem(int pid, vm_state_t *vm, uint16_t virt_addr, const uint8_t *src, size_t len,
                 uint64_t tick);

/* Trigger a page fault latency delay in the runtime if major fault occurred. */
bool vm_handle_fault_blocking(int pid, pager_result_t res, uint64_t tick);

/* Dynamic Heap Allocation wrappers. */
uint16_t vm_malloc(int pid, vm_state_t *vm, size_t size, uint64_t tick);
void vm_free(int pid, vm_state_t *vm, uint16_t addr, uint64_t tick);

/* Metrics / Dumps. */
void vm_dump_proc_map(const vm_state_t *vm, FILE *out);
void vm_dump_proc_pte(const vm_state_t *vm, FILE *out);
void vm_dump_global_frames(FILE *out);
void vm_dump_global_tlb(FILE *out);
void vm_dump_global_faults(FILE *out);

#endif /* MOSRT_VM_H */
