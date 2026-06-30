#include "replacement.h"
#include <stdlib.h>
#include <string.h>

#define MAX_LRU_PAGES 256

typedef struct {
    uint8_t vpn;
    uint32_t last_tick;
} lru_entry_t;

typedef struct {
    lru_entry_t pages[MAX_LRU_PAGES];
    int count;
} lru_state_t;

static void lru_init(void **state) {
    lru_state_t *s = malloc(sizeof(lru_state_t));
    if (s != NULL) {
        s->count = 0;
        memset(s->pages, 0, sizeof(s->pages));
    }
    *state = s;
}

static void lru_shutdown(void *state) {
    free(state);
}

static void lru_update(void *state, uint8_t vpn, uint64_t tick, bool write) {
    (void)write;
    lru_state_t *s = state;
    if (s == NULL) return;
    
    for (int i = 0; i < s->count; ++i) {
        if (s->pages[i].vpn == vpn) {
            s->pages[i].last_tick = (uint32_t)tick;
            return;
        }
    }
    
    /* If not found (should be inserted already, but just in case) */
    if (s->count < MAX_LRU_PAGES) {
        s->pages[s->count].vpn = vpn;
        s->pages[s->count].last_tick = (uint32_t)tick;
        s->count++;
    }
}

static void lru_insert(void *state, uint8_t vpn, uint64_t tick) {
    lru_update(state, vpn, tick, false);
}

static void lru_remove(void *state, uint8_t vpn) {
    lru_state_t *s = state;
    if (s == NULL) return;
    
    for (int i = 0; i < s->count; ++i) {
        if (s->pages[i].vpn == vpn) {
            for (int j = i; j < s->count - 1; ++j) {
                s->pages[j] = s->pages[j+1];
            }
            s->count--;
            break;
        }
    }
}

static int lru_evict(void *state, const void *page_table_ptr) {
    (void)page_table_ptr;
    lru_state_t *s = state;
    if (s == NULL || s->count == 0) return -1;
    
    int lru_idx = 0;
    uint32_t min_tick = s->pages[0].last_tick;
    
    for (int i = 1; i < s->count; ++i) {
        if (s->pages[i].last_tick < min_tick) {
            min_tick = s->pages[i].last_tick;
            lru_idx = i;
        }
    }
    
    int evicted_vpn = s->pages[lru_idx].vpn;
    
    /* Remove from list */
    for (int j = lru_idx; j < s->count - 1; ++j) {
        s->pages[j] = s->pages[j+1];
    }
    s->count--;
    
    return evicted_vpn;
}

const replacement_ops_t lru_ops = {
    .name = "lru",
    .init = lru_init,
    .shutdown = lru_shutdown,
    .update = lru_update,
    .insert = lru_insert,
    .remove = lru_remove,
    .evict = lru_evict,
};
