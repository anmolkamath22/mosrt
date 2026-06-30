#ifndef MOSRT_REPLACEMENT_H
#define MOSRT_REPLACEMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name;

    /* Initialize policy-specific state. */
    void (*init)(void **state);

    /* Shutdown and clean up policy-specific state. */
    void (*shutdown)(void *state);

    /* Called when a page is accessed (read/write). */
    void (*update)(void *state, uint8_t vpn, uint64_t tick, bool write);

    /* Called when a new page is mapped into memory. */
    void (*insert)(void *state, uint8_t vpn, uint64_t tick);

    /* Called when a page is unmapped. */
    void (*remove)(void *state, uint8_t vpn);

    /* Choose a VPN to evict. Returns VPN, or -1 if nothing can be evicted. */
    int (*evict)(void *state, const void *page_table_ptr);
} replacement_ops_t;

/* Global registries for available algorithms. */
extern const replacement_ops_t fifo_ops;
extern const replacement_ops_t lru_ops;
extern const replacement_ops_t clock_ops;

#endif /* MOSRT_REPLACEMENT_H */
