#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "page_table.h"

static void test_page_table_mapping(void) {
    page_table_t pt;
    page_table_init(&pt);

    /* Verify all pages initially marked not present */
    for (uint32_t vpn = 0; vpn < VM_NUM_PAGES; ++vpn) {
        const pte_t *pte = page_table_lookup_const(&pt, (uint8_t)vpn);
        assert(pte != NULL);
        assert(!pte->present);
        assert(!pte->in_swap);
    }

    /* Map a page and verify */
    uint8_t vpn = 10;
    uint8_t pfn = 5;
    uint8_t perm = VM_PROT_READ | VM_PROT_EXEC;
    
    page_table_map(&pt, vpn, pfn, perm);
    
    pte_t *pte = page_table_lookup(&pt, vpn);
    assert(pte != NULL);
    assert(pte->present);
    assert(pte->frame_num == pfn);
    assert(pte->permissions == perm);

    /* Test permissions validation */
    assert(page_table_validate_permissions(&pt, vpn, VM_PROT_READ));
    assert(page_table_validate_permissions(&pt, vpn, VM_PROT_EXEC));
    assert(!page_table_validate_permissions(&pt, vpn, VM_PROT_WRITE));

    /* Unmap page */
    page_table_unmap(&pt, vpn);
    assert(!pt.entries[vpn].present);
}

static void test_segments(void) {
    /* Test segment classification */
    assert(strcmp(page_table_get_segment_name(0), "TEXT") == 0);
    assert(strcmp(page_table_get_segment_name(15), "TEXT") == 0);
    assert(strcmp(page_table_get_segment_name(16), "RODATA") == 0);
    assert(strcmp(page_table_get_segment_name(31), "RODATA") == 0);
    assert(strcmp(page_table_get_segment_name(32), "DATA") == 0);
    assert(strcmp(page_table_get_segment_name(47), "DATA") == 0);
    assert(strcmp(page_table_get_segment_name(48), "BSS") == 0);
    assert(strcmp(page_table_get_segment_name(63), "BSS") == 0);
    assert(strcmp(page_table_get_segment_name(64), "HEAP") == 0);
    assert(strcmp(page_table_get_segment_name(191), "HEAP") == 0);
    assert(strcmp(page_table_get_segment_name(192), "STACK") == 0);
    assert(strcmp(page_table_get_segment_name(255), "STACK") == 0);

    /* Test segment default permissions */
    assert(page_table_get_segment_default_permissions(0) == (VM_PROT_READ | VM_PROT_EXEC));
    assert(page_table_get_segment_default_permissions(16) == VM_PROT_READ);
    assert(page_table_get_segment_default_permissions(32) == (VM_PROT_READ | VM_PROT_WRITE));
    assert(page_table_get_segment_default_permissions(48) == (VM_PROT_READ | VM_PROT_WRITE));
    assert(page_table_get_segment_default_permissions(64) == (VM_PROT_READ | VM_PROT_WRITE));
    assert(page_table_get_segment_default_permissions(255) == (VM_PROT_READ | VM_PROT_WRITE));
}

int main(void) {
    test_page_table_mapping();
    test_segments();
    printf("test_pagetable passed\n");
    return 0;
}
