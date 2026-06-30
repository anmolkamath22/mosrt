#include "tlb.h"
#include <assert.h>
#include <stdio.h>

static void test_tlb_lookup_and_insert(void) {
    tlb_init();

    tlb_stats_t stats = tlb_get_stats();
    assert(stats.hits == 0);
    assert(stats.misses == 0);

    /* Miss path */
    bool dirty = false;
    uint8_t perm = 0;
    int pfn = tlb_lookup(5, 10, &dirty, &perm);
    assert(pfn == -1);

    stats = tlb_get_stats();
    assert(stats.misses == 1);

    /* Insert translation */
    tlb_insert(5, 0xAA, true, VM_PROT_READ | VM_PROT_WRITE, 10);

    /* Hit path */
    pfn = tlb_lookup(5, 11, &dirty, &perm);
    assert(pfn == 0xAA);
    assert(dirty == true);
    assert(perm == (VM_PROT_READ | VM_PROT_WRITE));

    stats = tlb_get_stats();
    assert(stats.hits == 1);
}

static void test_tlb_eviction_lru(void) {
    tlb_init();

    /* Fill the TLB */
    for (int i = 0; i < VM_TLB_SIZE; ++i) {
        tlb_insert((uint8_t)i, (uint8_t)(i + 10), false, VM_PROT_READ, (uint64_t)(i + 1));
    }

    /* All entries should be valid. Lookup entry 0 with tick 100 to make it most recently used */
    bool dirty;
    uint8_t perm;
    int pfn = tlb_lookup(0, 100, &dirty, &perm);
    assert(pfn == 10);

    /* Entry 1 has last used tick = 2 (oldest). Now insert entry 100, which should evict entry 1 */
    tlb_insert(100, 200, false, VM_PROT_READ, 101);

    /* Lookup entry 1, should miss */
    pfn = tlb_lookup(1, 102, &dirty, &perm);
    assert(pfn == -1);

    /* Lookup entry 0, should still hit */
    pfn = tlb_lookup(0, 103, &dirty, &perm);
    assert(pfn == 10);
}

static void test_tlb_flush(void) {
    tlb_init();
    tlb_insert(5, 10, false, VM_PROT_READ, 1);
    tlb_insert(6, 12, false, VM_PROT_READ, 2);

    /* Flush single entry */
    tlb_flush_entry(5);
    bool dirty;
    uint8_t perm;
    assert(tlb_lookup(5, 3, &dirty, &perm) == -1);
    assert(tlb_lookup(6, 4, &dirty, &perm) == 12);

    /* Flush all */
    tlb_flush();
    assert(tlb_lookup(6, 5, &dirty, &perm) == -1);
}

int main(void) {
    test_tlb_lookup_and_insert();
    test_tlb_eviction_lru();
    test_tlb_flush();
    printf("test_tlb passed\n");
    return 0;
}
