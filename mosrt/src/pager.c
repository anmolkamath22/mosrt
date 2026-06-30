#include "pager.h"
#include "frame.h"
#include "proc.h"
#include "swap.h"
#include "tlb.h"
#include "vm.h"
#include <stdio.h>
#include <string.h>

static const replacement_ops_t *g_policy;
static void *g_policy_state;
static pager_stats_t g_stats;

void pager_init(void) {
    g_policy = &fifo_ops;
    g_policy->init(&g_policy_state);
    memset(&g_stats, 0, sizeof(g_stats));
}

void pager_shutdown(void) {
    if (g_policy != NULL && g_policy_state != NULL) {
        g_policy->shutdown(g_policy_state);
    }
    g_policy = NULL;
    g_policy_state = NULL;
}

bool pager_set_policy(const char *name) {
    const replacement_ops_t *new_policy = NULL;
    if (strcmp(name, "fifo") == 0) {
        new_policy = &fifo_ops;
    } else if (strcmp(name, "lru") == 0) {
        new_policy = &lru_ops;
    } else if (strcmp(name, "clock") == 0) {
        new_policy = &clock_ops;
    } else {
        return false;
    }

    /* Shutdown old policy */
    if (g_policy != NULL && g_policy_state != NULL) {
        g_policy->shutdown(g_policy_state);
    }

    g_policy = new_policy;
    g_policy->init(&g_policy_state);

    /* Re-insert all currently allocated frames into the new policy tracker to maintain state */
    for (uint8_t pfn = 0; pfn < VM_NUM_FRAMES; ++pfn) {
        int pid;
        uint8_t vpn;
        if (frame_get_owner(pfn, &pid, &vpn)) {
            g_policy->insert(g_policy_state, pfn, 0);
        }
    }

    return true;
}

const char *pager_get_policy_name(void) {
    return g_policy ? g_policy->name : "none";
}

void pager_proc_init(int pid, void **policy_state) {
    (void)pid;
    if (policy_state != NULL)
        *policy_state = NULL;
}

void pager_proc_destroy(void *policy_state) {
    (void)policy_state;
}

pager_stats_t pager_get_stats(void) {
    return g_stats;
}

void pager_reset_stats(void) {
    g_stats.minor_faults = 0;
    g_stats.major_faults = 0;
    g_stats.evictions = 0;
}

pager_result_t pager_access_page(int pid, page_table_t *pt, void *policy_state, uint8_t vpn,
                                 bool write, uint64_t tick, int *out_pfn) {
    (void)policy_state;
    if (pt == NULL)
        return PAGER_ERROR_ADDR;

    pte_t *pte = page_table_lookup(pt, vpn);
    if (pte == NULL)
        return PAGER_ERROR_ADDR;

    /* 1. Validate permissions */
    uint8_t req_perm = write ? VM_PROT_WRITE : VM_PROT_READ;
    if (!page_table_validate_permissions(pt, vpn, req_perm)) {
        return PAGER_ERROR_PERM;
    }

    /* 2. Hit path */
    if (pte->present) {
        pte->accessed = true;
        pte->referenced = true;
        pte->last_accessed_tick = (uint32_t)tick;
        if (write) {
            pte->dirty = true;
        }

        /* Update replacement algorithm with the frame access */
        if (g_policy != NULL && g_policy_state != NULL) {
            g_policy->update(g_policy_state, pte->frame_num, tick, write);
        }

        /* Populate TLB */
        tlb_insert(vpn, pte->frame_num, pte->dirty, pte->permissions, tick);

        if (out_pfn != NULL)
            *out_pfn = pte->frame_num;
        return PAGER_HIT;
    }

    /* 3. Page Fault path */
    bool is_major = pte->in_swap;

    /* Try to allocate a physical frame */
    int pfn = frame_alloc(pid, vpn);

    if (pfn < 0) {
        /* RAM is full: select a frame for eviction */
        if (g_policy == NULL || g_policy_state == NULL) {
            return PAGER_ERROR_OOM;
        }

        /* For Clock algorithm, we pass the current active process page table,
         * but since Clock scans all frames, the evict handler will look up the specific owner page
         * table. */
        int victim_pfn = g_policy->evict(g_policy_state, pt);
        if (victim_pfn < 0) {
            return PAGER_ERROR_OOM;
        }

        /* Retrieve owner of the victim frame */
        int victim_pid;
        uint8_t victim_vpn;
        if (!frame_get_owner((uint8_t)victim_pfn, &victim_pid, &victim_vpn)) {
            return PAGER_ERROR_OOM;
        }

        /* Get victim's page table */
        pcb_t *victim_pcb = proc_get(victim_pid);
        if (victim_pcb == NULL || victim_pcb->vm == NULL) {
            return PAGER_ERROR_OOM;
        }

        vm_state_t *victim_vm = (vm_state_t *)victim_pcb->vm;
        pte_t *victim_pte = page_table_lookup(&victim_vm->pt, victim_vpn);

        if (victim_pte != NULL) {
            /* Only swap out the page if it is dirty (modified since last page-in).
             * Clean pages can be safely discarded — their backing content is already
             * correct in swap (or was never modified), so no write is needed. */
            if (victim_pte->dirty) {
                int swap_slot = victim_pte->in_swap ? victim_pte->swap_slot
                                                    : swap_alloc(victim_pid, victim_vpn);
                if (swap_slot < 0) {
                    return PAGER_ERROR_OOM; /* Swap space full */
                }
                victim_pte->swap_slot = (uint16_t)swap_slot;
                swap_out((uint8_t)victim_pfn, (uint16_t)swap_slot);
                victim_pte->in_swap = true;
            }

            /* Unmap victim page */
            victim_pte->present = false;
            victim_pte->dirty = false;
            tlb_flush_entry(victim_vpn);
        }

        /* Free victim frame and allocate it to the new page */
        frame_free((uint8_t)victim_pfn);
        pfn = frame_alloc(pid, vpn);
        if (pfn < 0) {
            return PAGER_ERROR_OOM;
        }
        g_stats.evictions++;
    }

    /* Perform major/minor paging operations */
    if (is_major) {
        /* Major fault: read from swap space */
        swap_in(pte->swap_slot, (uint8_t)pfn);

        /* Free the swap slot to keep swap clean if desired, or keep it allocated */
        swap_free(pte->swap_slot);
        pte->in_swap = false;
        pte->swap_slot = 0;

        g_stats.major_faults++;
    } else {
        /* Minor fault: zero-fill the frame */
        uint8_t zero_block[VM_PAGE_SIZE] = {0};
        frame_write_block((uint8_t)pfn, zero_block);

        g_stats.minor_faults++;
    }

    /* Map the page in the page table */
    page_table_map(pt, vpn, (uint8_t)pfn, pte->permissions);
    pte->accessed = true;
    pte->referenced = true;
    pte->dirty = write;
    pte->last_accessed_tick = (uint32_t)tick;

    /* Register the frame with the replacement policy */
    if (g_policy != NULL && g_policy_state != NULL) {
        g_policy->insert(g_policy_state, (uint8_t)pfn, tick);
    }

    /* Invalidate TLB entry and insert new mapping */
    tlb_flush_entry(vpn);
    tlb_insert(vpn, (uint8_t)pfn, pte->dirty, pte->permissions, tick);

    if (out_pfn != NULL)
        *out_pfn = pfn;
    return is_major ? PAGER_FAULT_MAJOR : PAGER_FAULT_MINOR;
}
