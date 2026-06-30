#ifndef MOSRT_FRAME_H
#define MOSRT_FRAME_H

#include "vm_types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t allocated_frames;
    uint32_t free_frames;
} frame_stats_t;

/* Initialize the global physical memory simulator. */
void frame_init(void);

/* Shutdown physical memory simulator and release frames. */
void frame_shutdown(void);

/* Allocate a free frame for a specific process and VPN. Returns PFN, or -1 if no frames free. */
int frame_alloc(int pid, uint8_t vpn);

/* Free a physical frame by frame number (PFN). */
void frame_free(uint8_t pfn);

/* Read a byte from physical memory at a specific physical address. */
uint8_t frame_read_byte(uint32_t phys_addr);

/* Write a byte to physical memory at a specific physical address. */
void frame_write_byte(uint32_t phys_addr, uint8_t val);

/* Read a block of data from a frame (256 bytes). */
void frame_read_block(uint8_t pfn, uint8_t *dest);

/* Write a block of data to a frame (256 bytes). */
void frame_write_block(uint8_t pfn, const uint8_t *src);

/* Get frame allocator statistics. */
frame_stats_t frame_get_stats(void);

/* Find which PID and VPN owns a physical frame. Returns true if occupied. */
bool frame_get_owner(uint8_t pfn, int *out_pid, uint8_t *out_vpn);

#endif /* MOSRT_FRAME_H */
