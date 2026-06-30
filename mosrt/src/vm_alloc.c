#include "vm_alloc.h"
#include "vm.h"
#include "page_table.h"
#include "vm_types.h"
#include <string.h>

static bool read_hdr(int pid, vm_state_t *vm, uint16_t addr, vm_block_hdr_t *hdr, uint64_t tick) {
    return vm_read_mem(pid, vm, addr, (uint8_t *)hdr, sizeof(vm_block_hdr_t), tick) ==
           (int)sizeof(vm_block_hdr_t);
}

static bool write_hdr(int pid, vm_state_t *vm, uint16_t addr, const vm_block_hdr_t *hdr,
                      uint64_t tick) {
    return vm_write_mem(pid, vm, addr, (const uint8_t *)hdr, sizeof(vm_block_hdr_t), tick) ==
           (int)sizeof(vm_block_hdr_t);
}

static size_t align_up(size_t size, size_t alignment) {
    if (alignment == 0U || size > SIZE_MAX - (alignment - 1U)) {
        return 0U;
    }
    return (size + alignment - 1) & ~(alignment - 1);
}

static bool heap_grow(vm_state_t *vm, size_t pages_needed) {
    if (vm == NULL || pages_needed == 0U) {
        return false;
    }
    uint32_t current_pages = (vm->stats.heap_break - vm->stats.heap_start) / VM_PAGE_SIZE;
    if (current_pages + pages_needed > VM_SEG_HEAP_MAX_PAGES) {
        return false; /* Out of heap address space limit */
    }

    /* Grow the heap break virtually without mapping physical memory (demand paging) */
    for (size_t i = 0; i < pages_needed; ++i) {
        uint16_t new_page_addr = vm->stats.heap_break;
        uint8_t vpn = (uint8_t)(new_page_addr >> VM_PAGE_SHIFT);
        
        pte_t *pte = page_table_lookup(&vm->pt, vpn);
        if (pte != NULL) {
            pte->permissions = VM_PROT_READ | VM_PROT_WRITE;
            pte->present = false;
            pte->in_swap = false;
        }
        vm->stats.heap_break += VM_PAGE_SIZE;
    }
    return true;
}

uint16_t vm_alloc_malloc(int pid, vm_state_t *vm, size_t size, uint64_t tick) {
    if (vm == NULL || size == 0 || size > UINT16_MAX) return 0;
    size = align_up(size, 4);
    if (size == 0 || size > UINT16_MAX) return 0;

    /* Initialize heap if empty */
    if (vm->stats.heap_break == vm->stats.heap_start) {
        if (!heap_grow(vm, 2)) {
            return 0;
        }
        vm_block_hdr_t initial_hdr = {
            .size = (uint16_t)(vm->stats.heap_break - vm->stats.heap_start - VM_BLOCK_HDR_SIZE),
            .free = true,
            .next = 0,
            .prev = 0
        };
        if (!write_hdr(pid, vm, vm->stats.heap_start, &initial_hdr, tick)) {
            return 0;
        }
    }

    uint16_t curr_addr = vm->stats.heap_start;
    vm_block_hdr_t curr_hdr;

    while (curr_addr < vm->stats.heap_break) {
        if (!read_hdr(pid, vm, curr_addr, &curr_hdr, tick)) {
            return 0;
        }
        
        if (curr_hdr.free && curr_hdr.size >= size) {
            /* Check if we can split the block */
            if (curr_hdr.size >= size + VM_BLOCK_HDR_SIZE + 4) {
                uint16_t next_addr = (uint16_t)(curr_addr + VM_BLOCK_HDR_SIZE + size);
                vm_block_hdr_t next_hdr = {
                    .size = (uint16_t)(curr_hdr.size - size - VM_BLOCK_HDR_SIZE),
                    .free = true,
                    .next = curr_hdr.next,
                    .prev = curr_addr
                };
                if (!write_hdr(pid, vm, next_addr, &next_hdr, tick)) {
                    return 0;
                }

                /* Update current block */
                curr_hdr.size = (uint16_t)size;
                curr_hdr.free = false;
                curr_hdr.next = next_addr;
                if (!write_hdr(pid, vm, curr_addr, &curr_hdr, tick)) {
                    return 0;
                }

                /* Update the next-next block's prev link if it exists */
                if (next_hdr.next != 0) {
                    vm_block_hdr_t nn_hdr;
                    if (!read_hdr(pid, vm, next_hdr.next, &nn_hdr, tick)) {
                        return 0;
                    }
                    nn_hdr.prev = next_addr;
                    if (!write_hdr(pid, vm, next_hdr.next, &nn_hdr, tick)) {
                        return 0;
                    }
                }
            } else {
                /* Allocate entire block without splitting */
                curr_hdr.free = false;
                if (!write_hdr(pid, vm, curr_addr, &curr_hdr, tick)) {
                    return 0;
                }
            }
            return (uint16_t)(curr_addr + VM_BLOCK_HDR_SIZE);
        }

        if (curr_hdr.next == 0) {
            break; /* Reached end of list */
        }
        curr_addr = curr_hdr.next;
    }

    /* No free block large enough: grow the heap */
    size_t growth_bytes = VM_BLOCK_HDR_SIZE + size;
    size_t pages_needed = align_up(growth_bytes, VM_PAGE_SIZE) / VM_PAGE_SIZE;
    if (pages_needed == 0U) {
        return 0;
    }
    
    uint16_t old_break = vm->stats.heap_break;
    if (!heap_grow(vm, pages_needed)) {
        return 0; /* Heap overflow */
    }

    /* Create the new free block at the old break */
    uint16_t new_block_addr = old_break;
    vm_block_hdr_t new_block_hdr = {
        .size = (uint16_t)(pages_needed * VM_PAGE_SIZE - VM_BLOCK_HDR_SIZE),
        .free = true,
        .next = 0,
        .prev = curr_addr
    };
    if (!write_hdr(pid, vm, new_block_addr, &new_block_hdr, tick)) {
        return 0;
    }

    /* Link it to the previous last block */
    curr_hdr.next = new_block_addr;
    if (!write_hdr(pid, vm, curr_addr, &curr_hdr, tick)) {
        return 0;
    }

    /* Recursively try to allocate now that heap has grown */
    return vm_alloc_malloc(pid, vm, size, tick);
}

void vm_alloc_free(int pid, vm_state_t *vm, uint16_t addr, uint64_t tick) {
    if (vm == NULL) {
        return;
    }
    if (addr < vm->stats.heap_start + VM_BLOCK_HDR_SIZE || addr >= vm->stats.heap_break) {
        return;
    }

    uint16_t curr_addr = (uint16_t)(addr - VM_BLOCK_HDR_SIZE);
    vm_block_hdr_t curr_hdr;
    if (!read_hdr(pid, vm, curr_addr, &curr_hdr, tick)) {
        return;
    }

    if (curr_hdr.free) {
        return; /* Double free protection */
    }

    curr_hdr.free = true;
    if (!write_hdr(pid, vm, curr_addr, &curr_hdr, tick)) {
        return;
    }

    /* Coalesce with next block if free */
    if (curr_hdr.next != 0) {
        vm_block_hdr_t next_hdr;
        if (!read_hdr(pid, vm, curr_hdr.next, &next_hdr, tick)) {
            return;
        }
        if (next_hdr.free) {
            curr_hdr.size = (uint16_t)(curr_hdr.size + VM_BLOCK_HDR_SIZE + next_hdr.size);
            curr_hdr.next = next_hdr.next;
            if (!write_hdr(pid, vm, curr_addr, &curr_hdr, tick)) {
                return;
            }

            /* Update new next block's prev link */
            if (curr_hdr.next != 0) {
                vm_block_hdr_t nn_hdr;
                if (!read_hdr(pid, vm, curr_hdr.next, &nn_hdr, tick)) {
                    return;
                }
                nn_hdr.prev = curr_addr;
                if (!write_hdr(pid, vm, curr_hdr.next, &nn_hdr, tick)) {
                    return;
                }
            }
        }
    }

    /* Coalesce with prev block if free */
    if (curr_hdr.prev != 0) {
        vm_block_hdr_t prev_hdr;
        if (!read_hdr(pid, vm, curr_hdr.prev, &prev_hdr, tick)) {
            return;
        }
        if (prev_hdr.free) {
            prev_hdr.size = (uint16_t)(prev_hdr.size + VM_BLOCK_HDR_SIZE + curr_hdr.size);
            prev_hdr.next = curr_hdr.next;
            if (!write_hdr(pid, vm, curr_hdr.prev, &prev_hdr, tick)) {
                return;
            }

            /* Update new next block's prev link */
            if (prev_hdr.next != 0) {
                vm_block_hdr_t nn_hdr;
                if (!read_hdr(pid, vm, prev_hdr.next, &nn_hdr, tick)) {
                    return;
                }
                nn_hdr.prev = curr_hdr.prev;
                (void)write_hdr(pid, vm, prev_hdr.next, &nn_hdr, tick);
            }
        }
    }
}

vm_alloc_stats_t vm_alloc_get_stats(int pid, vm_state_t *vm, uint64_t tick) {
    vm_alloc_stats_t stats = {0};
    if (vm == NULL) {
        return stats;
    }
    if (vm->stats.heap_break == vm->stats.heap_start) {
        return stats;
    }

    uint16_t curr_addr = vm->stats.heap_start;
    vm_block_hdr_t curr_hdr;

    while (curr_addr < vm->stats.heap_break) {
        if (!read_hdr(pid, vm, curr_addr, &curr_hdr, tick)) {
            break;
        }
        stats.total_blocks++;
        if (curr_hdr.free) {
            stats.free_blocks++;
            stats.external_frag_bytes += curr_hdr.size;
        } else {
            stats.allocated_blocks++;
            /* In a real allocator, internal fragmentation is the difference
             * between the allocated block's actual capacity and the requested size.
             * Since we don't store requested size, we approximate it by aligning up,
             * so internal fragmentation is small but present. */
        }

        if (curr_hdr.next == 0) break;
        curr_addr = curr_hdr.next;
    }
    return stats;
}
