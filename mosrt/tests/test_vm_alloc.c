#include <assert.h>
#include <stdio.h>
#include "vm.h"
#include "vm_alloc.h"
#include "proc.h"

static void test_heap_allocator(void) {
    proc_table_init();
    vm_init();

    int pid = proc_create(0, 1, 0, 0);
    pcb_t *p = proc_get(pid);
    assert(p != NULL);
    p->vm = vm_proc_init(pid);
    assert(p->vm != NULL);

    vm_state_t *vm = p->vm;

    /* Initial break should be start of heap segment */
    assert(vm->stats.heap_break == vm->stats.heap_start);

    /* Allocate 64 bytes */
    uint16_t a1 = vm_alloc_malloc(pid, vm, 64, 0);
    assert(a1 == vm->stats.heap_start + VM_BLOCK_HDR_SIZE);
    assert(vm->stats.heap_break > vm->stats.heap_start);

    /* Allocate 128 bytes */
    uint16_t a2 = vm_alloc_malloc(pid, vm, 128, 0);
    assert(a2 == a1 + 64 + VM_BLOCK_HDR_SIZE);

    /* Get allocator stats */
    vm_alloc_stats_t stats = vm_alloc_get_stats(pid, vm, 0);
    assert(stats.allocated_blocks == 2);

    /* Free first allocation */
    vm_alloc_free(pid, vm, a1, 0);
    stats = vm_alloc_get_stats(pid, vm, 0);
    assert(stats.allocated_blocks == 1);
    assert(stats.free_blocks == 2);

    /* Allocate 32 bytes (should split the freed 64-byte block) */
    uint16_t a3 = vm_alloc_malloc(pid, vm, 32, 0);
    assert(a3 == a1); /* Same address as first allocation */

    stats = vm_alloc_get_stats(pid, vm, 0);
    assert(stats.allocated_blocks == 2);
    assert(stats.free_blocks == 2); /* Remains a split part and the end free block */

    /* Free second allocation and the new split part */
    vm_alloc_free(pid, vm, a3, 0);
    vm_alloc_free(pid, vm, a2, 0);

    /* All should coalesce back into a single block */
    stats = vm_alloc_get_stats(pid, vm, 0);
    assert(stats.allocated_blocks == 0);
    assert(stats.total_blocks == 1);

    proc_table_shutdown();
    vm_shutdown();
}

int main(void) {
    test_heap_allocator();
    printf("test_vm_alloc passed\n");
    return 0;
}
