#include "frame.h"
#include "page_table.h"
#include "replacement.h"
#include "vm_types.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t pages[VM_NUM_FRAMES];
    int count;
    int hand;
} clock_state_t;

static void clock_init(void **state) {
    clock_state_t *s = malloc(sizeof(clock_state_t));
    if (s != NULL) {
        s->count = 0;
        s->hand = 0;
        memset(s->pages, 0, sizeof(s->pages));
    }
    *state = s;
}

static void clock_shutdown(void *state) {
    free(state);
}

static void clock_update(void *state, uint8_t vpn, uint64_t tick, bool write) {
    (void)state;
    (void)vpn;
    (void)tick;
    (void)write;
    /* Handled by pager setting referenced bit on PTE */
}

static void clock_insert(void *state, uint8_t vpn, uint64_t tick) {
    (void)tick;
    clock_state_t *s = state;
    if (s == NULL)
        return;

    /* Avoid duplicates */
    for (int i = 0; i < s->count; ++i) {
        if (s->pages[i] == vpn)
            return;
    }

    if (s->count < (int)VM_NUM_FRAMES) {
        s->pages[s->count++] = vpn;
    }
}

static void clock_remove(void *state, uint8_t vpn) {
    clock_state_t *s = state;
    if (s == NULL)
        return;

    for (int i = 0; i < s->count; ++i) {
        if (s->pages[i] == vpn) {
            /* Shift remaining */
            for (int j = i; j < s->count - 1; ++j) {
                s->pages[j] = s->pages[j + 1];
            }
            s->count--;

            /* Adjust hand if needed */
            if (s->hand >= s->count) {
                s->hand = 0;
            }
            break;
        }
    }
}

static int clock_evict(void *state, const void *page_table_ptr) {
    clock_state_t *s = state;
    /* page_table_ptr is the victim's page table; we need to mutate the
     * referenced bit so we must accept it as mutable via a cast.
     * The evict() contract in replacement.h accepts const void* for
     * interface uniformity across policies, but Clock's second-chance
     * algorithm necessarily modifies PTE bits. */
    page_table_t *pt = (page_table_t *)page_table_ptr;
    if (s == NULL || s->count == 0 || pt == NULL)
        return -1;

    int attempts = 0;
    /* Loop at most 2 * count times to prevent infinite loops */
    while (attempts < 2 * s->count) {
        uint8_t vpn = s->pages[s->hand];
        pte_t *pte = (pte_t *)page_table_lookup_const(pt, vpn);

        if (pte != NULL) {
            if (pte->referenced) {
                /* Give a second chance: clear reference bit, advance hand */
                pte->referenced = false;
                s->hand = (s->hand + 1) % s->count;
            } else {
                /* Found target for eviction */
                int evicted_vpn = vpn;

                /* Advance hand before removal to point to next element */
                int next_hand = (s->hand + 1) % s->count;

                /* Remove from pages array */
                for (int j = s->hand; j < s->count - 1; ++j) {
                    s->pages[j] = s->pages[j + 1];
                }
                s->count--;

                /* Update hand index */
                if (s->count > 0) {
                    s->hand = next_hand % s->count;
                    if (s->hand >= s->count) {
                        s->hand = 0;
                    }
                } else {
                    s->hand = 0;
                }

                return evicted_vpn;
            }
        } else {
            /* If PTE is somehow NULL, just skip and advance hand */
            s->hand = (s->hand + 1) % s->count;
        }
        attempts++;
    }

    /* Fallback: just evict first element */
    int evicted_vpn = s->pages[0];
    for (int j = 0; j < s->count - 1; ++j) {
        s->pages[j] = s->pages[j + 1];
    }
    s->count--;
    s->hand = 0;
    return evicted_vpn;
}

const replacement_ops_t clock_ops = {
    .name = "clock",
    .init = clock_init,
    .shutdown = clock_shutdown,
    .update = clock_update,
    .insert = clock_insert,
    .remove = clock_remove,
    .evict = clock_evict,
};
