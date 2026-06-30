#include <assert.h>
#include <stdio.h>
#include "vm.h"
#include "frame.h"
#include "tlb.h"
#include "swap.h"
#include "pager.h"
#include "proc.h"

static void test_demand_paging(void) {
    proc_table_init();
    vm_init();

    int pid = proc_create(0, 1, 0, 0);
    pcb_t *p = proc_get(pid);
    assert(p != NULL);
    p->vm = vm_proc_init(pid);
    assert(p->vm != NULL);

    vm_state_t *vm = p->vm;

    /* Access a page in the HEAP segment (e.g. page 64, address 0x4000) */
    uint16_t addr1 = 0x4000;
    uint8_t write_val = 0xAB;
    uint8_t read_val = 0;

    pager_reset_stats();

    /* Write access: should generate a minor page fault (no swap in, page is new) */
    int status = vm_write_mem(pid, vm, addr1, &write_val, 1, 1);
    assert(status == 1);

    pager_stats_t p_stats = pager_get_stats();
    assert(p_stats.minor_faults == 1);
    assert(p_stats.major_faults == 0);

    /* Read access to same address: should hit in TLB */
    status = vm_read_mem(pid, vm, addr1, &read_val, 1, 2);
    assert(status == 1);
    assert(read_val == 0xAB);

    tlb_stats_t t_stats = tlb_get_stats();
    assert(t_stats.hits == 1);

    proc_table_shutdown();
    vm_shutdown();
}

static void test_eviction_and_swap(void) {
    proc_table_init();
    vm_init();
    pager_set_policy("fifo");

    int pid = proc_create(0, 1, 0, 0);
    pcb_t *p = proc_get(pid);
    p->vm = vm_proc_init(pid);
    vm_state_t *vm = p->vm;

    /* Make sure page table has permissions mapped for all pages we will access.
     * We will access VPN 64 to VPN 130. All are in the HEAP segment, so they
     * already have permissions set to RW. */
    
    pager_reset_stats();
    swap_reset_stats();

    /* Access 64 pages (fills physical memory completely, as VM_NUM_FRAMES = 64) */
    for (int i = 0; i < 64; ++i) {
        uint16_t addr = (uint16_t)((64 + i) * VM_PAGE_SIZE);
        uint8_t val = (uint8_t)i;
        int status = vm_write_mem(pid, vm, addr, &val, 1, (uint64_t)(i + 1));
        assert(status == 1);
    }

    pager_stats_t p_stats = pager_get_stats();
    assert(p_stats.minor_faults == 64);
    assert(p_stats.evictions == 0);

    /* Access one more page (VPN 128, address 128 * 256). 
     * This must trigger an eviction! Since we used FIFO policy, the first page
     * accessed (VPN 64) should be evicted. Since we wrote to VPN 64, it's dirty,
     * so it must be swapped out! */
    uint16_t addr_evict = 128 * VM_PAGE_SIZE;
    uint8_t val = 128;
    int status = vm_write_mem(pid, vm, addr_evict, &val, 1, 100);
    assert(status == 1);

    p_stats = pager_get_stats();
    assert(p_stats.evictions == 1);
    
    swap_stats_t s_stats = swap_get_stats();
    assert(s_stats.swap_outs == 1);

    /* VPN 64 should now be non-present and in swap space */
    pte_t *pte = page_table_lookup(&vm->pt, 64);
    assert(pte != NULL);
    assert(!pte->present);
    assert(pte->in_swap);

    /* Access VPN 64 again. This should trigger a MAJOR fault, 
     * causing page-in from swap space! */
    uint16_t addr_swap_in = 64 * VM_PAGE_SIZE;
    uint8_t read_val = 0;
    status = vm_read_mem(pid, vm, addr_swap_in, &read_val, 1, 200);
    assert(status == 1);
    assert(read_val == 0); /* First page had value 0 */

    p_stats = pager_get_stats();
    assert(p_stats.major_faults == 1);
    
    s_stats = swap_get_stats();
    assert(s_stats.swap_ins == 1);

    proc_table_shutdown();
    vm_shutdown();
}

int main(void) {
    test_demand_paging();
    test_eviction_and_swap();
    printf("test_pager passed\n");
    return 0;
}
