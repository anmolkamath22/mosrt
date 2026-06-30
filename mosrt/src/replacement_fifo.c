#include "replacement.h"
#include <stdlib.h>
#include <string.h>

#define MAX_QUEUE_SIZE 256

typedef struct {
    uint8_t queue[MAX_QUEUE_SIZE];
    int size;
} fifo_state_t;

static void fifo_init(void **state) {
    fifo_state_t *s = malloc(sizeof(fifo_state_t));
    if (s != NULL) {
        s->size = 0;
        memset(s->queue, 0, sizeof(s->queue));
    }
    *state = s;
}

static void fifo_shutdown(void *state) {
    free(state);
}

static void fifo_update(void *state, uint8_t vpn, uint64_t tick, bool write) {
    (void)state; (void)vpn; (void)tick; (void)write;
    /* FIFO does not care about access/use ticks */
}

static void fifo_insert(void *state, uint8_t vpn, uint64_t tick) {
    (void)tick;
    fifo_state_t *s = state;
    if (s == NULL) return;
    
    /* Make sure not already in queue */
    for (int i = 0; i < s->size; ++i) {
        if (s->queue[i] == vpn) return;
    }
    
    if (s->size < MAX_QUEUE_SIZE) {
        s->queue[s->size++] = vpn;
    }
}

static void fifo_remove(void *state, uint8_t vpn) {
    fifo_state_t *s = state;
    if (s == NULL) return;
    
    for (int i = 0; i < s->size; ++i) {
        if (s->queue[i] == vpn) {
            /* Shift remaining elements left */
            for (int j = i; j < s->size - 1; ++j) {
                s->queue[j] = s->queue[j+1];
            }
            s->size--;
            break;
        }
    }
}

static int fifo_evict(void *state, const void *page_table_ptr) {
    (void)page_table_ptr;
    fifo_state_t *s = state;
    if (s == NULL || s->size == 0) return -1;
    
    /* FIFO: evict front of the queue */
    int evicted_vpn = s->queue[0];
    
    /* Shift left */
    for (int j = 0; j < s->size - 1; ++j) {
        s->queue[j] = s->queue[j+1];
    }
    s->size--;
    
    return evicted_vpn;
}

const replacement_ops_t fifo_ops = {
    .name = "fifo",
    .init = fifo_init,
    .shutdown = fifo_shutdown,
    .update = fifo_update,
    .insert = fifo_insert,
    .remove = fifo_remove,
    .evict = fifo_evict,
};
