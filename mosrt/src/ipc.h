#ifndef MOSRT_IPC_H
#define MOSRT_IPC_H

#include <stdbool.h>
#include <stddef.h>

#define MOSRT_MAX_MSG_QUEUES 16
#define MOSRT_MSG_CAPACITY 16
#define MOSRT_WAITQ_CAPACITY 128

typedef enum {
    IPC_OK = 0,
    IPC_WOULD_BLOCK,
    IPC_ERROR
} ipc_result_t;

typedef struct {
    int sender_pid;
    int value;
} ipc_message_t;

typedef struct {
    bool used;
    int id;
    ipc_message_t items[MOSRT_MSG_CAPACITY];
    ipc_message_t grants[MOSRT_WAITQ_CAPACITY];
    int grant_pids[MOSRT_WAITQ_CAPACITY];
    size_t grants_len;
    size_t head;
    size_t len;
    int send_waiters[MOSRT_WAITQ_CAPACITY];
    size_t send_waiters_len;
    int recv_waiters[MOSRT_WAITQ_CAPACITY];
    size_t recv_waiters_len;
} ipc_queue_t;

/** Initialize all bounded message queues. */
void ipc_init(void);
/** Create or reset a bounded message queue id. */
bool ipc_create_queue(int id);
/** Attempt a nonblocking send; returns IPC_WOULD_BLOCK when full. */
ipc_result_t ipc_send(int qid, int sender_pid, int value);
/** Attempt a nonblocking receive; returns IPC_WOULD_BLOCK when empty. */
ipc_result_t ipc_recv(int qid, int receiver_pid, ipc_message_t *out);
/** Register a sender blocked on a full queue. */
void ipc_wait_sender(int qid, int pid);
/** Register a receiver blocked on an empty queue. */
void ipc_wait_receiver(int qid, int pid);
/** Pop a sender that may be woken after receive space appears. */
int ipc_pop_sender_waiter(int qid);
/** Pop a receiver that may be woken after a message arrives. */
int ipc_pop_receiver_waiter(int qid);
/** Return true when a receiver has a direct handoff message waiting. */
bool ipc_has_receiver_grant(int qid, int pid);

#endif
