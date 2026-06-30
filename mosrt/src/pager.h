#ifndef MOSRT_PAGER_H
#define MOSRT_PAGER_H

#include "page_table.h"
#include "replacement.h"
#include "vm_types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PAGER_HIT = 0,
    PAGER_FAULT_MINOR,
    PAGER_FAULT_MAJOR,
    PAGER_ERROR_PERM, /* Permission violation */
    PAGER_ERROR_OOM,  /* Out of physical memory and swap */
    PAGER_ERROR_ADDR  /* Invalid address */
} pager_result_t;

typedef struct {
    uint32_t minor_faults;
    uint32_t major_faults;
    uint32_t evictions;
} pager_stats_t;

/* Initialize global pager. */
void pager_init(void);

/* Shutdown global pager. */
void pager_shutdown(void);

/* Set replacement policy by name ("fifo", "lru", "clock"). Returns true on success. */
bool pager_set_policy(const char *name);

/* Get current active policy name. */
const char *pager_get_policy_name(void);

/* Initialize replacement state for a new process. */
void pager_proc_init(int pid, void **policy_state);

/* Destroy replacement state for a process. */
void pager_proc_destroy(void *policy_state);

/* Access a virtual page (VPN) for a process.
 * If a fault is generated, the function updates the page tables, handles evictions/swapping,
 * and returns the fault type.
 * 'tick' is the current global runtime tick.
 * 'write' indicates if it is a write operation.
 */
pager_result_t pager_access_page(int pid, page_table_t *pt, void *policy_state, uint8_t vpn,
                                 bool write, uint64_t tick, int *out_pfn);

/* Get pager statistics. */
pager_stats_t pager_get_stats(void);

/* Reset pager statistics. */
void pager_reset_stats(void);

#endif /* MOSRT_PAGER_H */
