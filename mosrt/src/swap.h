#ifndef MOSRT_SWAP_H
#define MOSRT_SWAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "vm_types.h"

typedef struct {
    uint32_t allocated_slots;
    uint32_t free_slots;
    uint32_t swap_ins;
    uint32_t swap_outs;
} swap_stats_t;

/* Initialize global swap space. */
void swap_init(void);

/* Shutdown swap space. */
void swap_shutdown(void);

/* Allocate a free swap slot for a process and VPN. Returns slot index or -1 if full. */
int swap_alloc(int pid, uint8_t vpn);

/* Free a swap slot. */
void swap_free(uint16_t slot);

/* Write a physical frame's data out to a swap slot. */
void swap_out(uint8_t pfn, uint16_t slot);

/* Read swap slot's data into a physical frame. */
void swap_in(uint16_t slot, uint8_t pfn);

/* Get swap statistics. */
swap_stats_t swap_get_stats(void);

/* Reset swap statistics (useful for benchmarks). */
void swap_reset_stats(void);

#endif /* MOSRT_SWAP_H */
