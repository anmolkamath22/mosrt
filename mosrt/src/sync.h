#ifndef MOSRT_SYNC_H
#define MOSRT_SYNC_H

#include <stdbool.h>
#include <stddef.h>

#define MOSRT_MAX_SEMAPHORES 32
#define MOSRT_SYNC_WAITERS 128

typedef enum { SYNC_OK = 0, SYNC_WOULD_BLOCK, SYNC_ERROR } sync_result_t;

typedef struct {
    size_t waiters_len;
    size_t grants_len;
    int id;
    int count;
    int owner_pid;
    int waiters[MOSRT_SYNC_WAITERS];
    int grants[MOSRT_SYNC_WAITERS];
    bool used;
    bool mutex;
} mosrt_sem_t;

/** Initialize semaphore and mutex tables. */
void sync_init(void);
/** Create or reset a counting semaphore. */
bool sync_create_sem(int id, int initial);
/** Create or reset a mutex. */
bool sync_create_mutex(int id);
/** Try to decrement a semaphore or mutex, blocking when unavailable. */
sync_result_t sync_wait(int id, int pid);
/** Increment/release a semaphore or mutex and return a waiter to wake, if any. */
sync_result_t sync_post(int id, int pid, int *woken_pid);
/** Try to lock a mutex, creating it on first use. */
sync_result_t sync_mutex_lock(int id, int pid);
/** Unlock a mutex and return a waiter to wake, if any. */
sync_result_t sync_mutex_unlock(int id, int pid, int *woken_pid);

#endif
