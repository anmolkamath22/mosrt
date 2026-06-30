#ifndef MOSRT_PAGE_TABLE_H
#define MOSRT_PAGE_TABLE_H

#include "vm_types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    pte_t entries[VM_NUM_PAGES];
} page_table_t;

/* Initialize a page table, setting default permissions and present = false. */
void page_table_init(page_table_t *pt);

/* Map a virtual page (VPN) to a physical frame (PFN) with protection bits. */
void page_table_map(page_table_t *pt, uint8_t vpn, uint8_t pfn, uint8_t permissions);

/* Unmap a virtual page, clearing present bit and physical mapping. */
void page_table_unmap(page_table_t *pt, uint8_t vpn);

/* Look up a page table entry (PTE). Returns NULL if invalid vpn. */
pte_t *page_table_lookup(page_table_t *pt, uint8_t vpn);

/* Const look up a page table entry (PTE). */
const pte_t *page_table_lookup_const(const page_table_t *pt, uint8_t vpn);

/* Validate permissions for a virtual page. Returns true if valid. */
bool page_table_validate_permissions(const page_table_t *pt, uint8_t vpn, uint8_t access_type);

/* Print page table mappings to a stream. */
void page_table_dump(const page_table_t *pt, FILE *out);

/* Helper to get segment name of a virtual address/VPN. */
const char *page_table_get_segment_name(uint8_t vpn);

/* Get default permissions of a segment. */
uint8_t page_table_get_segment_default_permissions(uint8_t vpn);

#endif /* MOSRT_PAGE_TABLE_H */
