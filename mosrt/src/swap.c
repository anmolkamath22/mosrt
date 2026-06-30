#include "swap.h"
#include "frame.h"
#include <string.h>

typedef struct {
    bool allocated;
    int pid;
    uint8_t vpn;
} swap_meta_t;

static uint8_t g_swap_mem[VM_NUM_SWAP_SLOTS][VM_PAGE_SIZE];
static swap_meta_t g_swap_meta[VM_NUM_SWAP_SLOTS];
static swap_stats_t g_stats;

void swap_init(void) {
    memset(g_swap_mem, 0, sizeof(g_swap_mem));
    memset(g_swap_meta, 0, sizeof(g_swap_meta));
    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.free_slots = VM_NUM_SWAP_SLOTS;
}

void swap_shutdown(void) {
    memset(g_swap_mem, 0, sizeof(g_swap_mem));
    memset(g_swap_meta, 0, sizeof(g_swap_meta));
    memset(&g_stats, 0, sizeof(g_stats));
}

int swap_alloc(int pid, uint8_t vpn) {
    for (size_t i = 0; i < VM_NUM_SWAP_SLOTS; ++i) {
        if (!g_swap_meta[i].allocated) {
            g_swap_meta[i].allocated = true;
            g_swap_meta[i].pid = pid;
            g_swap_meta[i].vpn = vpn;
            g_stats.allocated_slots++;
            g_stats.free_slots--;
            return (int)i;
        }
    }
    return -1;
}

void swap_free(uint16_t slot) {
    if (slot < VM_NUM_SWAP_SLOTS) {
        if (g_swap_meta[slot].allocated) {
            g_swap_meta[slot].allocated = false;
            g_swap_meta[slot].pid = 0;
            g_swap_meta[slot].vpn = 0;
            g_stats.allocated_slots--;
            g_stats.free_slots++;
        }
    }
}

void swap_out(uint8_t pfn, uint16_t slot) {
    if (slot < VM_NUM_SWAP_SLOTS) {
        frame_read_block(pfn, g_swap_mem[slot]);
        g_stats.swap_outs++;
    }
}

void swap_in(uint16_t slot, uint8_t pfn) {
    if (slot < VM_NUM_SWAP_SLOTS) {
        frame_write_block(pfn, g_swap_mem[slot]);
        g_stats.swap_ins++;
    }
}

swap_stats_t swap_get_stats(void) {
    return g_stats;
}

void swap_reset_stats(void) {
    g_stats.swap_ins = 0;
    g_stats.swap_outs = 0;
}
