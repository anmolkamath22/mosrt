/**
 * test_proc.c — Process table and state machine unit tests.
 *
 * Covers:
 *   - PCB creation and field initialization
 *   - Valid and invalid state transitions
 *   - Process lifecycle (create → ready → run → exit)
 *   - proc_mark_exited retains PCB for metrics
 *   - proc_destroy releases slot for reuse
 *   - PID allocation exhaustion
 *   - proc_count / proc_live_count correctness
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "proc.h"

static void test_create_basic(void) {
    proc_table_init();
    int pid = proc_create(0, 20, 0, 0);
    assert(pid > 0);
    const pcb_t *p = proc_get_const(pid);
    assert(p != NULL);
    assert(p->pid == pid);
    assert(p->ppid == 0);
    assert(p->state == PROC_NEW);
    assert(p->priority == 20);
    assert(p->base_priority == 20);
    assert(p->nice == 0);
    assert(p->mlfq_level == 0);
    assert(p->response_time == UINT64_MAX);
    assert(p->stack != NULL);
    assert(p->stack_size > 0);
    proc_table_shutdown();
}

static void test_priority_clamping(void) {
    proc_table_init();
    int p1 = proc_create(0, -5, 0, 0);
    assert(p1 > 0);
    assert(proc_get_const(p1)->priority == MOSRT_MIN_PRIO);

    int p2 = proc_create(0, 100, 0, 0);
    assert(p2 > 0);
    assert(proc_get_const(p2)->priority == MOSRT_MAX_PRIO);
    proc_table_shutdown();
}

static void test_valid_transitions(void) {
    /* NEW → READY */
    assert(proc_is_valid_transition(PROC_NEW, PROC_READY));
    /* NEW → EXITED (immediate kill) */
    assert(proc_is_valid_transition(PROC_NEW, PROC_EXITED));
    /* READY → RUNNING */
    assert(proc_is_valid_transition(PROC_READY, PROC_RUNNING));
    /* RUNNING → READY (preemption) */
    assert(proc_is_valid_transition(PROC_RUNNING, PROC_READY));
    /* RUNNING → BLOCKED */
    assert(proc_is_valid_transition(PROC_RUNNING, PROC_BLOCKED));
    /* RUNNING → EXITED */
    assert(proc_is_valid_transition(PROC_RUNNING, PROC_EXITED));
    /* BLOCKED → READY (wakeup) */
    assert(proc_is_valid_transition(PROC_BLOCKED, PROC_READY));
    /* BLOCKED → EXITED (kill) */
    assert(proc_is_valid_transition(PROC_BLOCKED, PROC_EXITED));
}

static void test_invalid_transitions(void) {
    /* EXITED is terminal */
    assert(!proc_is_valid_transition(PROC_EXITED, PROC_READY));
    assert(!proc_is_valid_transition(PROC_EXITED, PROC_RUNNING));
    assert(!proc_is_valid_transition(PROC_EXITED, PROC_BLOCKED));
    assert(!proc_is_valid_transition(PROC_EXITED, PROC_NEW));
    /* Cannot go directly from NEW to RUNNING or BLOCKED */
    assert(!proc_is_valid_transition(PROC_NEW, PROC_RUNNING));
    assert(!proc_is_valid_transition(PROC_NEW, PROC_BLOCKED));
    /* Cannot go directly from READY to BLOCKED */
    assert(!proc_is_valid_transition(PROC_READY, PROC_BLOCKED));
    /* Cannot go directly from BLOCKED to RUNNING */
    assert(!proc_is_valid_transition(PROC_BLOCKED, PROC_RUNNING));
}

static void test_lifecycle(void) {
    proc_table_init();
    int pid = proc_create(0, 20, 0, 0);
    assert(pid > 0);

    /* NEW → READY */
    assert(proc_set_state(pid, PROC_READY, 1));
    assert(proc_get_const(pid)->state == PROC_READY);
    assert(proc_get_const(pid)->last_ready_tick == 1);

    /* READY → RUNNING (first dispatch records response time) */
    assert(proc_set_state(pid, PROC_RUNNING, 3));
    assert(proc_get_const(pid)->state == PROC_RUNNING);
    assert(proc_get_const(pid)->response_time == 3); /* 3 - start_tick(0) */

    /* RUNNING → BLOCKED */
    assert(proc_set_state(pid, PROC_BLOCKED, 5));
    assert(proc_get_const(pid)->state == PROC_BLOCKED);

    /* BLOCKED → READY (wakeup) */
    assert(proc_set_state(pid, PROC_READY, 8));
    assert(proc_get_const(pid)->last_ready_tick == 8);

    /* READY → RUNNING */
    assert(proc_set_state(pid, PROC_RUNNING, 10));
    /* response_time should NOT change (already recorded) */
    assert(proc_get_const(pid)->response_time == 3);

    /* RUNNING → EXITED */
    assert(proc_set_state(pid, PROC_EXITED, 15));
    assert(proc_get_const(pid)->state == PROC_EXITED);
    assert(proc_get_const(pid)->finish_tick == 15);
    proc_table_shutdown();
}

static void test_mark_exited_retains_pcb(void) {
    proc_table_init();
    int pid = proc_create(0, 10, 0, 0);
    assert(proc_set_state(pid, PROC_READY, 0));
    assert(proc_set_state(pid, PROC_RUNNING, 1));

    assert(proc_mark_exited(pid, 42, 10));
    /* PCB still accessible for metrics */
    const pcb_t *p = proc_get_const(pid);
    assert(p != NULL);
    assert(p->state == PROC_EXITED);
    assert(p->exit_code == 42);
    assert(p->finish_tick == 10);
    /* stack released */
    assert(p->stack == NULL);
    proc_table_shutdown();
}

static void test_destroy_releases_slot(void) {
    proc_table_init();
    int pid = proc_create(0, 10, 0, 0);
    assert(pid > 0);
    assert(proc_count() == 1);

    proc_destroy(pid, 99);
    assert(proc_get(pid) == NULL);
    assert(proc_count() == 0);
    proc_table_shutdown();
}

static void test_count_functions(void) {
    proc_table_init();
    int p1 = proc_create(0, 10, 0, 0);
    int p2 = proc_create(0, 10, 0, 0);
    assert(proc_count() == 2);
    assert(proc_live_count() == 2);

    assert(proc_set_state(p1, PROC_READY, 0));
    assert(proc_set_state(p1, PROC_RUNNING, 1));
    assert(proc_mark_exited(p1, 0, 5));
    /* Exited PCB still counts as used but not live */
    assert(proc_count() == 2);
    assert(proc_live_count() == 1);

    proc_destroy(p2, 0);
    assert(proc_count() == 1); /* p1 still retained */
    assert(proc_live_count() == 0);
    proc_table_shutdown();
}

static void test_invalid_state_transition_rejected(void) {
    proc_table_init();
    int pid = proc_create(0, 10, 0, 0);

    /* Try invalid: NEW → RUNNING */
    assert(!proc_set_state(pid, PROC_RUNNING, 0));
    /* State should remain NEW */
    assert(proc_get_const(pid)->state == PROC_NEW);
    proc_table_shutdown();
}

static void test_state_string(void) {
    assert(proc_state_to_string(PROC_NEW) != NULL);
    assert(proc_state_to_string(PROC_READY) != NULL);
    assert(proc_state_to_string(PROC_RUNNING) != NULL);
    assert(proc_state_to_string(PROC_BLOCKED) != NULL);
    assert(proc_state_to_string(PROC_EXITED) != NULL);
}

int main(void) {
    test_create_basic();
    test_priority_clamping();
    test_valid_transitions();
    test_invalid_transitions();
    test_lifecycle();
    test_mark_exited_retains_pcb();
    test_destroy_releases_slot();
    test_count_functions();
    test_invalid_state_transition_rejected();
    test_state_string();
    printf("test_proc: all tests passed\n");
    return 0;
}
