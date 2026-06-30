#include "sync.h"

#include <string.h>

static mosrt_sem_t g_sems[MOSRT_MAX_SEMAPHORES];

static mosrt_sem_t *find_sem(int id) {
    for (size_t i = 0; i < MOSRT_MAX_SEMAPHORES; ++i) {
        if (g_sems[i].used && g_sems[i].id == id) {
            return &g_sems[i];
        }
    }
    return NULL;
}

static bool create_common(int id, int initial, bool mutex) {
    mosrt_sem_t *s = find_sem(id);
    if (s == NULL) {
        for (size_t i = 0; i < MOSRT_MAX_SEMAPHORES; ++i) {
            if (!g_sems[i].used) {
                s = &g_sems[i];
                break;
            }
        }
    }
    if (s == NULL) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->used = true;
    s->id = id;
    s->count = initial;
    s->owner_pid = -1;
    s->mutex = mutex;
    return true;
}

static bool ensure_common(int id, int initial, bool mutex) {
    mosrt_sem_t *s = find_sem(id);
    if (s != NULL) {
        return s->mutex == mutex;
    }
    return create_common(id, initial, mutex);
}

static bool add_waiter(mosrt_sem_t *s, int pid) {
    for (size_t i = 0; i < s->waiters_len; ++i) {
        if (s->waiters[i] == pid) {
            return true;
        }
    }
    if (s->waiters_len < MOSRT_SYNC_WAITERS) {
        s->waiters[s->waiters_len++] = pid;
        return true;
    }
    return false;
}

static int pop_waiter(mosrt_sem_t *s) {
    if (s->waiters_len == 0U) {
        return -1;
    }
    int pid = s->waiters[0];
    memmove(s->waiters, s->waiters + 1, (s->waiters_len - 1U) * sizeof(s->waiters[0]));
    --s->waiters_len;
    return pid;
}

static void add_grant(mosrt_sem_t *s, int pid) {
    for (size_t i = 0; i < s->grants_len; ++i) {
        if (s->grants[i] == pid) {
            return;
        }
    }
    if (s->grants_len < MOSRT_SYNC_WAITERS) {
        s->grants[s->grants_len++] = pid;
    }
}

static bool take_grant(mosrt_sem_t *s, int pid) {
    for (size_t i = 0; i < s->grants_len; ++i) {
        if (s->grants[i] == pid) {
            memmove(s->grants + i, s->grants + i + 1,
                    (s->grants_len - i - 1U) * sizeof(s->grants[0]));
            --s->grants_len;
            return true;
        }
    }
    return false;
}

void sync_init(void) {
    memset(g_sems, 0, sizeof(g_sems));
}

bool sync_create_sem(int id, int initial) {
    return create_common(id, initial, false);
}

bool sync_create_mutex(int id) {
    return create_common(id, 1, true);
}

sync_result_t sync_wait(int id, int pid) {
    mosrt_sem_t *s = find_sem(id);
    if (s == NULL) {
        if (!sync_create_sem(id, 1)) {
            return SYNC_ERROR;
        }
        s = find_sem(id);
    }
    if (s == NULL) {
        return SYNC_ERROR;
    }
    if (take_grant(s, pid)) {
        return SYNC_OK;
    }
    if (s->mutex && s->owner_pid == pid) {
        return SYNC_ERROR;
    }
    if (s->count <= 0) {
        return add_waiter(s, pid) ? SYNC_WOULD_BLOCK : SYNC_ERROR;
    }
    --s->count;
    if (s->mutex) {
        s->owner_pid = pid;
    }
    return SYNC_OK;
}

sync_result_t sync_mutex_lock(int id, int pid) {
    if (!ensure_common(id, 1, true)) {
        return SYNC_ERROR;
    }
    return sync_wait(id, pid);
}

sync_result_t sync_mutex_unlock(int id, int pid, int *woken_pid) {
    return sync_post(id, pid, woken_pid);
}

sync_result_t sync_post(int id, int pid, int *woken_pid) {
    mosrt_sem_t *s = find_sem(id);
    if (woken_pid != NULL) {
        *woken_pid = -1;
    }
    if (s == NULL) {
        return SYNC_ERROR;
    }
    if (s->mutex && s->owner_pid != pid) {
        return SYNC_ERROR;
    }
    int wake = pop_waiter(s);
    if (wake >= 0) {
        if (s->mutex) {
            s->owner_pid = wake;
            s->count = 0;
        } else {
            add_grant(s, wake);
        }
        if (woken_pid != NULL) {
            *woken_pid = wake;
        }
        return SYNC_OK;
    }
    ++s->count;
    if (s->mutex) {
        s->owner_pid = -1;
        if (s->count > 1) {
            s->count = 1;
        }
    }
    return SYNC_OK;
}
