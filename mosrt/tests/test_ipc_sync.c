#include <assert.h>

#include "ipc.h"
#include "sync.h"

int main(void) {
    ipc_init();
    assert(ipc_create_queue(1));
    for (int i = 0; i < MOSRT_MSG_CAPACITY; ++i) {
        assert(ipc_send(1, 10, i) == IPC_OK);
    }
    assert(ipc_send(1, 10, 99) == IPC_WOULD_BLOCK);
    ipc_message_t msg;
    assert(ipc_recv(1, 11, &msg) == IPC_OK);
    assert(msg.sender_pid == 10);
    ipc_wait_receiver(2, 21);
    assert(ipc_send(2, 20, 1234) == IPC_OK);
    assert(ipc_has_receiver_grant(2, 21));
    assert(ipc_recv(2, 21, &msg) == IPC_OK);
    assert(msg.value == 1234);

    sync_init();
    assert(sync_create_sem(7, 1));
    assert(sync_wait(7, 1) == SYNC_OK);
    assert(sync_wait(7, 2) == SYNC_WOULD_BLOCK);
    int wake = -1;
    assert(sync_post(7, 1, &wake) == SYNC_OK);
    assert(wake == 2);
    assert(sync_wait(7, 2) == SYNC_OK);

    assert(sync_create_mutex(8));
    assert(sync_wait(8, 3) == SYNC_OK);
    assert(sync_wait(8, 3) == SYNC_ERROR);
    assert(sync_wait(8, 4) == SYNC_WOULD_BLOCK);
    assert(sync_post(8, 3, &wake) == SYNC_OK);
    assert(wake == 4);
    return 0;
}
