#include <assert.h>
#include <stdio.h>
#include "replacement.h"
#include "page_table.h"

static void test_fifo_policy(void) {
    void *state = NULL;
    fifo_ops.init(&state);
    assert(state != NULL);

    /* Insert PFNs: 10, 11, 12 */
    fifo_ops.insert(state, 10, 1);
    fifo_ops.insert(state, 11, 2);
    fifo_ops.insert(state, 12, 3);

    /* Evict: should be 10 (First-In) */
    int evicted = fifo_ops.evict(state, NULL);
    assert(evicted == 10);

    /* Insert PFN: 13 */
    fifo_ops.insert(state, 13, 4);

    /* Evict remaining: should be 11, then 12, then 13 */
    assert(fifo_ops.evict(state, NULL) == 11);
    assert(fifo_ops.evict(state, NULL) == 12);
    assert(fifo_ops.evict(state, NULL) == 13);
    assert(fifo_ops.evict(state, NULL) == -1);

    fifo_ops.shutdown(state);
}

static void test_lru_policy(void) {
    void *state = NULL;
    lru_ops.init(&state);
    assert(state != NULL);

    /* Insert PFNs: 10, 11, 12 */
    lru_ops.insert(state, 10, 1);
    lru_ops.insert(state, 11, 2);
    lru_ops.insert(state, 12, 3);

    /* Update PFN 10 to tick 5 (most recently used) */
    lru_ops.update(state, 10, 5, false);

    /* Evict: should be 11 (oldest tick 2) */
    int evicted = lru_ops.evict(state, NULL);
    assert(evicted == 11);

    /* Update PFN 12 to tick 6 */
    lru_ops.update(state, 12, 6, false);

    /* Evict: should be 10 (tick 5), then 12 (tick 6) */
    assert(lru_ops.evict(state, NULL) == 10);
    assert(lru_ops.evict(state, NULL) == 12);
    assert(lru_ops.evict(state, NULL) == -1);

    lru_ops.shutdown(state);
}

static void test_clock_policy(void) {
    void *state = NULL;
    clock_ops.init(&state);
    assert(state != NULL);

    page_table_t pt;
    page_table_init(&pt);

    /* Let's mock a simple global frame mapping. 
     * PFN 0 -> VPN 10 (owner)
     * PFN 1 -> VPN 11
     * PFN 2 -> VPN 12
     * Wait, Clock checks the referenced bit of the page in the page table.
     * In the global replacement framework, pager retrieves the owner VPN/PID from frame.c
     * and checks the referenced bit in the owner process page table.
     * In our clock_ops.evict test, since clock_evict casts page_table_ptr to page_table_t,
     * and looks up the page table entries for the VPN.
     * Wait! Let's check how clock_evict was implemented:
     *   uint8_t vpn = s->pages[s->hand];
     *   pte_t *pte = page_table_lookup_const(pt, vpn);
     * In the clock_evict code, it checks the entries of the page table using the VPN/PFN.
     * Yes! We insert vpn directly into the circular buffer of Clock.
     * Let's see: clock_insert(state, vpn, tick).
     * If we insert VPNs: 5, 6, 7.
     * Let's map VPNs 5, 6, 7 in pt.
     */
    page_table_map(&pt, 5, 100, VM_PROT_READ);
    page_table_map(&pt, 6, 101, VM_PROT_READ);
    page_table_map(&pt, 7, 102, VM_PROT_READ);

    clock_ops.insert(state, 5, 1);
    clock_ops.insert(state, 6, 2);
    clock_ops.insert(state, 7, 3);

    /* Initially all referenced bits are 0 (after page_table_map) */
    /* Set referenced bits:
     * VPN 5 -> referenced = true
     * VPN 6 -> referenced = false
     * VPN 7 -> referenced = true
     */
    pt.entries[5].referenced = true;
    pt.entries[6].referenced = false;
    pt.entries[7].referenced = true;

    /* Evict:
     * Hand starts at 0 (VPN 5). VPN 5 is referenced: clear it to 0, advance hand to 1 (VPN 6).
     * VPN 6 is not referenced: select VPN 6 for eviction!
     * So evict should return 6.
     */
    int evicted = clock_ops.evict(state, &pt);
    assert(evicted == 6);

    /* Now hand is at index 1 in the remaining list [5, 7], which points to VPN 7.
     * Set referenced bits:
     * VPN 5 -> referenced = false (cleared in previous step)
     * VPN 7 -> referenced = true
     */
    /* Evict:
     * Hand points to VPN 7. VPN 7 is referenced: clear to 0, advance hand to VPN 5.
     * VPN 5 is not referenced: select VPN 5 for eviction!
     */
    evicted = clock_ops.evict(state, &pt);
    assert(evicted == 5);

    /* Remaining is [7]. Evict should return 7. */
    evicted = clock_ops.evict(state, &pt);
    assert(evicted == 7);

    clock_ops.shutdown(state);
}

int main(void) {
    test_fifo_policy();
    test_lru_policy();
    test_clock_policy();
    printf("test_replacement passed\n");
    return 0;
}
