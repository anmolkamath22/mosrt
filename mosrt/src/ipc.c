#include "ipc.h"

#include <string.h>

static ipc_queue_t g_queues[MOSRT_MAX_MSG_QUEUES];

static ipc_queue_t *find_queue(int id) {
    for (size_t i = 0; i < MOSRT_MAX_MSG_QUEUES; ++i) {
        if (g_queues[i].used && g_queues[i].id == id) {
            return &g_queues[i];
        }
    }
    return NULL;
}

static void waitq_add(int *waiters, size_t *len, int pid) {
    for (size_t i = 0; i < *len; ++i) {
        if (waiters[i] == pid) {
            return;
        }
    }
    if (*len < MOSRT_WAITQ_CAPACITY) {
        waiters[(*len)++] = pid;
    }
}

static int waitq_pop(int *waiters, size_t *len) {
    if (*len == 0U) {
        return -1;
    }
    int pid = waiters[0];
    memmove(waiters, waiters + 1, (*len - 1U) * sizeof(waiters[0]));
    --*len;
    return pid;
}

static int waitq_peek(const int *waiters, size_t len) {
    return len == 0U ? -1 : waiters[0];
}

static void waitq_remove(int *waiters, size_t *len, int pid) {
    for (size_t i = 0; i < *len; ++i) {
        if (waiters[i] == pid) {
            memmove(waiters + i, waiters + i + 1, (*len - i - 1U) * sizeof(waiters[0]));
            --*len;
            return;
        }
    }
}

static bool grant_add(ipc_queue_t *q, int pid, ipc_message_t msg) {
    if (q->grants_len >= MOSRT_WAITQ_CAPACITY) {
        return false;
    }
    q->grant_pids[q->grants_len] = pid;
    q->grants[q->grants_len] = msg;
    ++q->grants_len;
    return true;
}

static bool grant_take(ipc_queue_t *q, int pid, ipc_message_t *out) {
    for (size_t i = 0; i < q->grants_len; ++i) {
        if (q->grant_pids[i] == pid) {
            if (out != NULL) {
                *out = q->grants[i];
            }
            memmove(q->grant_pids + i, q->grant_pids + i + 1,
                    (q->grants_len - i - 1U) * sizeof(q->grant_pids[0]));
            memmove(q->grants + i, q->grants + i + 1,
                    (q->grants_len - i - 1U) * sizeof(q->grants[0]));
            --q->grants_len;
            return true;
        }
    }
    return false;
}

void ipc_init(void) {
    memset(g_queues, 0, sizeof(g_queues));
}

bool ipc_create_queue(int id) {
    ipc_queue_t *existing = find_queue(id);
    if (existing != NULL) {
        memset(existing, 0, sizeof(*existing));
        existing->used = true;
        existing->id = id;
        return true;
    }
    for (size_t i = 0; i < MOSRT_MAX_MSG_QUEUES; ++i) {
        if (!g_queues[i].used) {
            memset(&g_queues[i], 0, sizeof(g_queues[i]));
            g_queues[i].used = true;
            g_queues[i].id = id;
            return true;
        }
    }
    return false;
}

ipc_result_t ipc_send(int qid, int sender_pid, int value) {
    ipc_queue_t *q = find_queue(qid);
    if (q == NULL && !ipc_create_queue(qid)) {
        return IPC_ERROR;
    }
    q = find_queue(qid);
    if (q == NULL) {
        return IPC_ERROR;
    }
    int receiver = waitq_peek(q->recv_waiters, q->recv_waiters_len);
    if (receiver >= 0) {
        ipc_message_t msg = {.sender_pid = sender_pid, .value = value};
        return grant_add(q, receiver, msg) ? IPC_OK : IPC_ERROR;
    }
    if (q->len >= MOSRT_MSG_CAPACITY) {
        return IPC_WOULD_BLOCK;
    }
    size_t idx = (q->head + q->len) % MOSRT_MSG_CAPACITY;
    q->items[idx].sender_pid = sender_pid;
    q->items[idx].value = value;
    ++q->len;
    return IPC_OK;
}

ipc_result_t ipc_recv(int qid, int receiver_pid, ipc_message_t *out) {
    ipc_queue_t *q = find_queue(qid);
    if (q == NULL && !ipc_create_queue(qid)) {
        return IPC_ERROR;
    }
    q = find_queue(qid);
    if (q == NULL) {
        return IPC_ERROR;
    }
    if (grant_take(q, receiver_pid, out)) {
        waitq_remove(q->recv_waiters, &q->recv_waiters_len, receiver_pid);
        return IPC_OK;
    }
    int first_waiter = waitq_peek(q->recv_waiters, q->recv_waiters_len);
    if (first_waiter >= 0 && first_waiter != receiver_pid) {
        return IPC_WOULD_BLOCK;
    }
    if (q->len == 0U) {
        return IPC_WOULD_BLOCK;
    }
    if (out != NULL) {
        *out = q->items[q->head];
    }
    q->head = (q->head + 1U) % MOSRT_MSG_CAPACITY;
    --q->len;
    return IPC_OK;
}

void ipc_wait_sender(int qid, int pid) {
    if (find_queue(qid) == NULL) {
        (void)ipc_create_queue(qid);
    }
    ipc_queue_t *q = find_queue(qid);
    if (q != NULL) {
        waitq_add(q->send_waiters, &q->send_waiters_len, pid);
    }
}

void ipc_wait_receiver(int qid, int pid) {
    if (find_queue(qid) == NULL) {
        (void)ipc_create_queue(qid);
    }
    ipc_queue_t *q = find_queue(qid);
    if (q != NULL) {
        waitq_add(q->recv_waiters, &q->recv_waiters_len, pid);
    }
}

int ipc_pop_sender_waiter(int qid) {
    ipc_queue_t *q = find_queue(qid);
    return q == NULL ? -1 : waitq_pop(q->send_waiters, &q->send_waiters_len);
}

int ipc_pop_receiver_waiter(int qid) {
    ipc_queue_t *q = find_queue(qid);
    return q == NULL ? -1 : waitq_pop(q->recv_waiters, &q->recv_waiters_len);
}

bool ipc_has_receiver_grant(int qid, int pid) {
    ipc_queue_t *q = find_queue(qid);
    if (q == NULL) {
        return false;
    }
    for (size_t i = 0; i < q->grants_len; ++i) {
        if (q->grant_pids[i] == pid) {
            return true;
        }
    }
    return false;
}
