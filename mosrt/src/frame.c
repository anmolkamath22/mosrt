#include "frame.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    int pid;
    uint8_t vpn;
    bool allocated;
} frame_meta_t;

static uint8_t g_ram[VM_PHYS_MEM_SIZE];
static frame_meta_t g_frames[VM_NUM_FRAMES];
static uint32_t g_allocated_count;

void frame_init(void) {
    memset(g_ram, 0, sizeof(g_ram));
    memset(g_frames, 0, sizeof(g_frames));
    g_allocated_count = 0;
}

void frame_shutdown(void) {
    memset(g_ram, 0, sizeof(g_ram));
    memset(g_frames, 0, sizeof(g_frames));
    g_allocated_count = 0;
}

int frame_alloc(int pid, uint8_t vpn) {
    for (size_t i = 0; i < VM_NUM_FRAMES; ++i) {
        if (!g_frames[i].allocated) {
            g_frames[i].allocated = true;
            g_frames[i].pid = pid;
            g_frames[i].vpn = vpn;
            g_allocated_count++;
            /* Zero out the physical memory frame */
            memset(&g_ram[i * VM_PAGE_SIZE], 0, VM_PAGE_SIZE);
            return (int)i;
        }
    }
    return -1;
}

void frame_free(uint8_t pfn) {
    if (pfn < VM_NUM_FRAMES) {
        if (g_frames[pfn].allocated) {
            g_frames[pfn].allocated = false;
            g_frames[pfn].pid = 0;
            g_frames[pfn].vpn = 0;
            g_allocated_count--;
        }
    }
}

uint8_t frame_read_byte(uint32_t phys_addr) {
    if (phys_addr < VM_PHYS_MEM_SIZE) {
        return g_ram[phys_addr];
    }
    return 0;
}

void frame_write_byte(uint32_t phys_addr, uint8_t val) {
    if (phys_addr < VM_PHYS_MEM_SIZE) {
        g_ram[phys_addr] = val;
    }
}

void frame_read_block(uint8_t pfn, uint8_t *dest) {
    if (pfn < VM_NUM_FRAMES && dest != NULL) {
        memcpy(dest, &g_ram[(size_t)pfn * VM_PAGE_SIZE], VM_PAGE_SIZE);
    }
}

void frame_write_block(uint8_t pfn, const uint8_t *src) {
    if (pfn < VM_NUM_FRAMES && src != NULL) {
        memcpy(&g_ram[(size_t)pfn * VM_PAGE_SIZE], src, VM_PAGE_SIZE);
    }
}

frame_stats_t frame_get_stats(void) {
    frame_stats_t stats;
    stats.allocated_frames = g_allocated_count;
    stats.free_frames = VM_NUM_FRAMES - g_allocated_count;
    return stats;
}

bool frame_get_owner(uint8_t pfn, int *out_pid, uint8_t *out_vpn) {
    if (pfn < VM_NUM_FRAMES) {
        if (g_frames[pfn].allocated) {
            if (out_pid != NULL)
                *out_pid = g_frames[pfn].pid;
            if (out_vpn != NULL)
                *out_vpn = g_frames[pfn].vpn;
            return true;
        }
    }
    return false;
}
