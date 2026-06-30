#ifndef MOSRT_TLB_H
#define MOSRT_TLB_H

#include "vm_types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint32_t hits;
    uint32_t misses;
} tlb_stats_t;

/* Initialize global TLB. */
void tlb_init(void);

/* Reset TLB hit/miss statistics. */
void tlb_reset_stats(void);

/* Lookup a VPN in TLB. If hit, returns PFN and sets out_dirty/out_permissions. Returns -1 on miss.
 */
int tlb_lookup(uint8_t vpn, uint64_t tick, bool *out_dirty, uint8_t *out_permissions);

/* Insert a mapping into TLB, evicting using LRU if full. */
void tlb_insert(uint8_t vpn, uint8_t pfn, bool dirty, uint8_t permissions, uint64_t tick);

/* Flush/invalidate the entire TLB (called on context switches). */
void tlb_flush(void);

/* Invalidate a specific VPN entry from the TLB. */
void tlb_flush_entry(uint8_t vpn);

/* Get TLB stats. */
tlb_stats_t tlb_get_stats(void);

/* Print TLB contents to a stream. */
void tlb_dump(FILE *out);

#endif /* MOSRT_TLB_H */
