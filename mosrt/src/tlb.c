#include "tlb.h"
#include <stdio.h>
#include <string.h>

static tlb_entry_t g_tlb[VM_TLB_SIZE];
static tlb_stats_t g_stats;

void tlb_init(void) {
    tlb_flush();
    tlb_reset_stats();
}

void tlb_reset_stats(void) {
    g_stats.hits = 0;
    g_stats.misses = 0;
}

int tlb_lookup(uint8_t vpn, uint64_t tick, bool *out_dirty, uint8_t *out_permissions) {
    for (int i = 0; i < VM_TLB_SIZE; ++i) {
        if (g_tlb[i].valid && g_tlb[i].vpn == vpn) {
            g_tlb[i].last_used_tick = (uint32_t)tick;
            if (out_dirty != NULL)
                *out_dirty = g_tlb[i].dirty;
            if (out_permissions != NULL)
                *out_permissions = g_tlb[i].permissions;
            g_stats.hits++;
            return g_tlb[i].pfn;
        }
    }
    g_stats.misses++;
    return -1;
}

void tlb_insert(uint8_t vpn, uint8_t pfn, bool dirty, uint8_t permissions, uint64_t tick) {
    /* Check if already in TLB (update it) */
    for (int i = 0; i < VM_TLB_SIZE; ++i) {
        if (g_tlb[i].valid && g_tlb[i].vpn == vpn) {
            g_tlb[i].pfn = pfn;
            g_tlb[i].dirty = dirty;
            g_tlb[i].permissions = permissions;
            g_tlb[i].last_used_tick = (uint32_t)tick;
            return;
        }
    }

    /* Find an invalid entry first */
    for (int i = 0; i < VM_TLB_SIZE; ++i) {
        if (!g_tlb[i].valid) {
            g_tlb[i].valid = true;
            g_tlb[i].vpn = vpn;
            g_tlb[i].pfn = pfn;
            g_tlb[i].dirty = dirty;
            g_tlb[i].permissions = permissions;
            g_tlb[i].last_used_tick = (uint32_t)tick;
            return;
        }
    }

    /* If full, evict using LRU */
    int lru_idx = 0;
    uint32_t min_tick = g_tlb[0].last_used_tick;
    for (int i = 1; i < VM_TLB_SIZE; ++i) {
        if (g_tlb[i].last_used_tick < min_tick) {
            min_tick = g_tlb[i].last_used_tick;
            lru_idx = i;
        }
    }

    /* Evict and insert */
    g_tlb[lru_idx].valid = true;
    g_tlb[lru_idx].vpn = vpn;
    g_tlb[lru_idx].pfn = pfn;
    g_tlb[lru_idx].dirty = dirty;
    g_tlb[lru_idx].permissions = permissions;
    g_tlb[lru_idx].last_used_tick = (uint32_t)tick;
}

void tlb_flush(void) {
    for (int i = 0; i < VM_TLB_SIZE; ++i) {
        g_tlb[i].valid = false;
        g_tlb[i].vpn = 0;
        g_tlb[i].pfn = 0;
        g_tlb[i].dirty = false;
        g_tlb[i].permissions = 0;
        g_tlb[i].last_used_tick = 0;
    }
}

void tlb_flush_entry(uint8_t vpn) {
    for (int i = 0; i < VM_TLB_SIZE; ++i) {
        if (g_tlb[i].valid && g_tlb[i].vpn == vpn) {
            g_tlb[i].valid = false;
        }
    }
}

tlb_stats_t tlb_get_stats(void) {
    return g_stats;
}

void tlb_dump(FILE *out) {
    if (out == NULL)
        out = stdout;
    fprintf(out, "%-5s %-5s %-5s %-5s %-5s %-10s\n", "Entry", "VPN", "PFN", "Valid", "Dirty",
            "Last Used");
    for (int i = 0; i < VM_TLB_SIZE; ++i) {
        if (g_tlb[i].valid) {
            fprintf(out, "[%02d]  0x%02X  0x%02X  %-5s %-5s %-10u\n", i, g_tlb[i].vpn, g_tlb[i].pfn,
                    g_tlb[i].valid ? "yes" : "no", g_tlb[i].dirty ? "yes" : "no",
                    g_tlb[i].last_used_tick);
        }
    }
}
