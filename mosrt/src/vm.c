#include "vm.h"
#include "frame.h"
#include "tlb.h"
#include "swap.h"
#include "pager.h"
#include "vm_alloc.h"
#include "proc.h"
#include <stdlib.h>
#include <string.h>

void vm_init(void) {
    frame_init();
    tlb_init();
    swap_init();
    pager_init();
}

void vm_shutdown(void) {
    pager_shutdown();
    swap_shutdown();
    frame_shutdown();
    tlb_flush();
}

vm_state_t *vm_proc_init(int pid) {
    vm_state_t *vm = malloc(sizeof(vm_state_t));
    if (vm == NULL) return NULL;
    
    vm->pid = pid;
    page_table_init(&vm->pt);
    
    memset(&vm->stats, 0, sizeof(vm->stats));
    vm->stats.heap_start = VM_SEG_HEAP_START;
    vm->stats.heap_break = VM_SEG_HEAP_START;
    vm->stats.stack_limit = VM_NUM_PAGES - VM_SEG_STACK_MAX_PAGES;

    pager_proc_init(pid, &vm->policy_state);
    
    return vm;
}

void vm_proc_destroy(vm_state_t *vm) {
    if (vm == NULL) return;

    /* Free all physical frames allocated to this process */
    for (uint32_t vpn = 0; vpn < VM_NUM_PAGES; ++vpn) {
        pte_t *pte = page_table_lookup(&vm->pt, (uint8_t)vpn);
        if (pte != NULL) {
            if (pte->present) {
                frame_free(pte->frame_num);
            }
            if (pte->in_swap) {
                swap_free(pte->swap_slot);
            }
        }
    }

    pager_proc_destroy(vm->policy_state);
    free(vm);
}

int vm_read_mem(int pid, vm_state_t *vm, uint16_t virt_addr, uint8_t *dest, size_t len, uint64_t tick) {
    if (vm == NULL || dest == NULL) return -1;
    if ((uint32_t)virt_addr + len > VM_VIRT_ADDR_SPACE_SIZE) return -1;

    tlb_stats_t tlb_before = tlb_get_stats();

    for (size_t i = 0; i < len; ++i) {
        uint16_t curr_addr = (uint16_t)(virt_addr + i);
        uint8_t vpn = (uint8_t)(curr_addr >> VM_PAGE_SHIFT);
        uint8_t offset = (uint8_t)(curr_addr & VM_PAGE_MASK);
        int pfn = -1;

        bool tlb_dirty = false;
        uint8_t tlb_perm = 0;
        pfn = tlb_lookup(vpn, tick, &tlb_dirty, &tlb_perm);

        if (pfn >= 0) {
            if ((tlb_perm & VM_PROT_READ) == 0) {
                return -1;
            }
            pte_t *pte = page_table_lookup(&vm->pt, vpn);
            if (pte != NULL) {
                pte->accessed = true;
            }
        } else {
            pager_result_t res = pager_access_page(pid, &vm->pt, vm->policy_state, vpn, false, tick, &pfn);
            if (res == PAGER_ERROR_PERM || res == PAGER_ERROR_OOM || res == PAGER_ERROR_ADDR) {
                return -1;
            }

            if (res == PAGER_FAULT_MINOR) {
                vm->stats.minor_faults++;
            } else if (res == PAGER_FAULT_MAJOR) {
                vm->stats.major_faults++;
                vm->stats.page_in_count++;
            }

            pte_t *pte = page_table_lookup(&vm->pt, vpn);
            if (pte != NULL) {
                tlb_insert(vpn, (uint8_t)pfn, pte->dirty, pte->permissions, tick);
            }
        }

        uint32_t phys_addr = (uint32_t)pfn * VM_PAGE_SIZE + offset;
        dest[i] = frame_read_byte(phys_addr);
    }

    tlb_stats_t tlb_after = tlb_get_stats();
    vm->stats.tlb_hits += (tlb_after.hits - tlb_before.hits);
    vm->stats.tlb_misses += (tlb_after.misses - tlb_before.misses);

    return (int)len;
}

int vm_write_mem(int pid, vm_state_t *vm, uint16_t virt_addr, const uint8_t *src, size_t len, uint64_t tick) {
    if (vm == NULL || src == NULL) return -1;
    if ((uint32_t)virt_addr + len > VM_VIRT_ADDR_SPACE_SIZE) return -1;

    tlb_stats_t tlb_before = tlb_get_stats();

    for (size_t i = 0; i < len; ++i) {
        uint16_t curr_addr = (uint16_t)(virt_addr + i);
        uint8_t vpn = (uint8_t)(curr_addr >> VM_PAGE_SHIFT);
        uint8_t offset = (uint8_t)(curr_addr & VM_PAGE_MASK);
        int pfn = -1;

        bool tlb_dirty = false;
        uint8_t tlb_perm = 0;
        pfn = tlb_lookup(vpn, tick, &tlb_dirty, &tlb_perm);

        if (pfn >= 0) {
            if ((tlb_perm & VM_PROT_WRITE) == 0) {
                return -1;
            }
            pte_t *pte = page_table_lookup(&vm->pt, vpn);
            if (pte != NULL) {
                pte->accessed = true;
                pte->dirty = true;
            }
            if (!tlb_dirty) {
                tlb_insert(vpn, (uint8_t)pfn, true, tlb_perm, tick);
            }
        } else {
            pager_result_t res = pager_access_page(pid, &vm->pt, vm->policy_state, vpn, true, tick, &pfn);
            if (res == PAGER_ERROR_PERM || res == PAGER_ERROR_OOM || res == PAGER_ERROR_ADDR) {
                return -1;
            }

            if (res == PAGER_FAULT_MINOR) {
                vm->stats.minor_faults++;
            } else if (res == PAGER_FAULT_MAJOR) {
                vm->stats.major_faults++;
                vm->stats.page_in_count++;
            }

            pte_t *pte = page_table_lookup(&vm->pt, vpn);
            if (pte != NULL) {
                tlb_insert(vpn, (uint8_t)pfn, pte->dirty, pte->permissions, tick);
            }
        }

        uint32_t phys_addr = (uint32_t)pfn * VM_PAGE_SIZE + offset;
        frame_write_byte(phys_addr, src[i]);
    }

    tlb_stats_t tlb_after = tlb_get_stats();
    vm->stats.tlb_hits += (tlb_after.hits - tlb_before.hits);
    vm->stats.tlb_misses += (tlb_after.misses - tlb_before.misses);

    return (int)len;
}

bool vm_handle_fault_blocking(int pid, pager_result_t res, uint64_t tick) {
    (void)tick;
    if (res == PAGER_FAULT_MAJOR) {
        pcb_t *p = proc_get(pid);
        if (p != NULL) {
            p->wakeup_tick = tick + VM_PAGE_FAULT_LATENCY;
        }
        return true;
    }
    return false;
}

uint16_t vm_malloc(int pid, vm_state_t *vm, size_t size, uint64_t tick) {
    return vm_alloc_malloc(pid, vm, size, tick);
}

void vm_free(int pid, vm_state_t *vm, uint16_t addr, uint64_t tick) {
    vm_alloc_free(pid, vm, addr, tick);
}

void vm_dump_proc_map(const vm_state_t *vm, FILE *out) {
    if (vm == NULL || out == NULL) return;
    fprintf(out, "Virtual Memory Map for PID %d:\n", vm->pid);
    fprintf(out, "  %-10s : 0x%04X - 0x%04X\n", "TEXT", VM_SEG_TEXT_START, VM_SEG_TEXT_START + VM_SEG_TEXT_PAGES * VM_PAGE_SIZE - 1);
    fprintf(out, "  %-10s : 0x%04X - 0x%04X\n", "RODATA", VM_SEG_RODATA_START, VM_SEG_RODATA_START + VM_SEG_RODATA_PAGES * VM_PAGE_SIZE - 1);
    fprintf(out, "  %-10s : 0x%04X - 0x%04X\n", "DATA", VM_SEG_DATA_START, VM_SEG_DATA_START + VM_SEG_DATA_PAGES * VM_PAGE_SIZE - 1);
    fprintf(out, "  %-10s : 0x%04X - 0x%04X\n", "BSS", VM_SEG_BSS_START, VM_SEG_BSS_START + VM_SEG_BSS_PAGES * VM_PAGE_SIZE - 1);
    fprintf(out, "  %-10s : 0x%04X - 0x%04X (max limit: 0x%04X)\n", "HEAP", vm->stats.heap_start, vm->stats.heap_break - 1, vm->stats.heap_start + VM_SEG_HEAP_MAX_PAGES * VM_PAGE_SIZE - 1);
    fprintf(out, "  %-10s : 0x%04X - 0xFFFF (max limit: 0x%04X)\n", "STACK", vm->stats.stack_limit * VM_PAGE_SIZE, vm->stats.stack_limit * VM_PAGE_SIZE);
}

void vm_dump_proc_pte(const vm_state_t *vm, FILE *out) {
    if (vm == NULL || out == NULL) return;
    page_table_dump(&vm->pt, out);
}

void vm_dump_global_frames(FILE *out) {
    if (out == NULL) out = stdout;
    frame_stats_t stats = frame_get_stats();
    fprintf(out, "Physical Frame Allocator Stats:\n");
    fprintf(out, "  Allocated frames : %u / %u\n", stats.allocated_frames, VM_NUM_FRAMES);
    fprintf(out, "  Free frames      : %u / %u\n", stats.free_frames, VM_NUM_FRAMES);
    fprintf(out, "\nFrame ownership:\n");
    fprintf(out, "%-7s %-5s %-5s\n", "Frame", "PID", "VPN");
    for (uint8_t pfn = 0; pfn < VM_NUM_FRAMES; ++pfn) {
        int pid;
        uint8_t vpn;
        if (frame_get_owner(pfn, &pid, &vpn)) {
            fprintf(out, "0x%02X    %-5d 0x%02X\n", pfn, pid, vpn);
        }
    }
}

void vm_dump_global_tlb(FILE *out) {
    tlb_dump(out);
}

void vm_dump_global_faults(FILE *out) {
    if (out == NULL) out = stdout;
    pager_stats_t p_stats = pager_get_stats();
    swap_stats_t s_stats = swap_get_stats();
    fprintf(out, "Global Paging & Fault Stats:\n");
    fprintf(out, "  Minor faults    : %u\n", p_stats.minor_faults);
    fprintf(out, "  Major faults    : %u\n", p_stats.major_faults);
    fprintf(out, "  Evictions       : %u\n", p_stats.evictions);
    fprintf(out, "  Swap slots used : %u / %u\n", s_stats.allocated_slots, VM_NUM_SWAP_SLOTS);
    fprintf(out, "  Swap In count   : %u\n", s_stats.swap_ins);
    fprintf(out, "  Swap Out count  : %u\n", s_stats.swap_outs);
}
