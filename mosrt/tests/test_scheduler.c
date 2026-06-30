/**
 * test_scheduler.c — Scheduler unit tests for all four policies.
 *
 * Covers:
 *   - Priority heap ordering (basic + randomized)
 *   - Round Robin quantum preemption
 *   - FCFS FIFO ordering and non-preemption
 *   - MLFQ demotion and priority boost
 *   - Scheduler name parsing
 *   - Duplicate enqueue rejection
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "proc.h"
#include "sched.h"

static unsigned lcg_next(unsigned *state) {
    *state = (*state * 1103515245U) + 12345U;
    return *state;
}

/* ---- Priority Scheduler Tests ---- */

static void test_prio_basic(void) {
    scheduler_t sched;
    proc_table_init();
    int p1 = proc_create(0, 20, 0, 0);
    int p2 = proc_create(0, 10, 0, 0);
    int p3 = proc_create(0, 30, 0, 0);
    assert(p1 > 0 && p2 > 0 && p3 > 0);
    assert(proc_set_state(p1, PROC_READY, 0));
    assert(proc_set_state(p2, PROC_READY, 0));
    assert(proc_set_state(p3, PROC_READY, 0));

    sched_init(&sched, SCHED_PRIO, 2);
    assert(sched_enqueue(&sched, p1));
    assert(sched_enqueue(&sched, p2));
    assert(sched_enqueue(&sched, p3));
    /* Lowest numeric priority should be selected first */
    assert(sched_pick_next(&sched) == p2);
    proc_table_shutdown();
}

static void test_prio_randomized(void) {
    proc_table_init();
    scheduler_t sched;
    sched_init(&sched, SCHED_PRIO, 4);
    unsigned rng = 0xC0FFEEU;
    int last_priority = MOSRT_MIN_PRIO;
    for (int i = 0; i < 64; ++i) {
        int priority = (int)(lcg_next(&rng) % (MOSRT_MAX_PRIO + 1));
        int pid = proc_create(0, priority, 0, 0);
        assert(pid > 0);
        assert(proc_set_state(pid, PROC_READY, 0));
        assert(sched_enqueue(&sched, pid));
    }
    for (int i = 0; i < 64; ++i) {
        int pid = sched_pick_next(&sched);
        const pcb_t *p = proc_get_const(pid);
        assert(p != NULL);
        assert(p->priority >= last_priority);
        last_priority = p->priority;
    }
    proc_table_shutdown();
}

/* ---- Round Robin Tests ---- */

static void test_rr_quantum(void) {
    proc_table_init();
    int p1 = proc_create(0, 20, 0, 0);
    int p2 = proc_create(0, 20, 0, 0);
    assert(proc_set_state(p1, PROC_READY, 0));
    assert(proc_set_state(p2, PROC_READY, 0));

    scheduler_t sched;
    sched_init(&sched, SCHED_RR, 2);
    assert(sched_enqueue(&sched, p1));
    assert(sched_enqueue(&sched, p2));
    assert(sched_pick_next(&sched) == p1);
    sched_on_tick(&sched, p1);
    assert(!sched_should_preempt(&sched, p1));
    sched_on_tick(&sched, p1);
    assert(sched_should_preempt(&sched, p1));
    proc_table_shutdown();
}

static void test_rr_fifo_order(void) {
    proc_table_init();
    int p1 = proc_create(0, 20, 0, 0);
    int p2 = proc_create(0, 20, 1, 0);
    int p3 = proc_create(0, 20, 2, 0);
    assert(proc_set_state(p1, PROC_READY, 0));
    assert(proc_set_state(p2, PROC_READY, 1));
    assert(proc_set_state(p3, PROC_READY, 2));

    scheduler_t sched;
    sched_init(&sched, SCHED_RR, 10);
    assert(sched_enqueue(&sched, p1));
    assert(sched_enqueue(&sched, p2));
    assert(sched_enqueue(&sched, p3));
    /* FIFO: p1 first, then p2, then p3 */
    assert(sched_pick_next(&sched) == p1);
    assert(sched_pick_next(&sched) == p2);
    assert(sched_pick_next(&sched) == p3);
    proc_table_shutdown();
}

/* ---- FCFS Tests ---- */

static void test_fcfs_ordering(void) {
    proc_table_init();
    int p1 = proc_create(0, 10, 0, 0);
    int p2 = proc_create(0, 5, 0, 0); /* lower prio but created second */
    int p3 = proc_create(0, 30, 0, 0);
    assert(proc_set_state(p1, PROC_READY, 0));
    assert(proc_set_state(p2, PROC_READY, 0));
    assert(proc_set_state(p3, PROC_READY, 0));

    scheduler_t sched;
    sched_init(&sched, SCHED_FCFS, 1);
    assert(sched_enqueue(&sched, p1));
    assert(sched_enqueue(&sched, p2));
    assert(sched_enqueue(&sched, p3));
    /* FCFS: order of enqueue, regardless of priority */
    assert(sched_pick_next(&sched) == p1);
    assert(sched_pick_next(&sched) == p2);
    assert(sched_pick_next(&sched) == p3);
    proc_table_shutdown();
}

static void test_fcfs_no_preemption(void) {
    proc_table_init();
    int p1 = proc_create(0, 20, 0, 0);
    assert(proc_set_state(p1, PROC_READY, 0));

    scheduler_t sched;
    sched_init(&sched, SCHED_FCFS, 1);
    assert(sched_enqueue(&sched, p1));
    assert(sched_pick_next(&sched) == p1);

    /* FCFS should never preempt regardless of ticks consumed */
    for (int i = 0; i < 100; ++i) {
        sched_on_tick(&sched, p1);
        assert(!sched_should_preempt(&sched, p1));
    }
    proc_table_shutdown();
}

/* ---- MLFQ Tests ---- */

static void test_mlfq_demotion(void) {
    proc_table_init();
    int p1 = proc_create(0, 20, 0, 0);
    assert(proc_set_state(p1, PROC_READY, 0));

    scheduler_t sched;
    sched_init(&sched, SCHED_MLFQ, 2);
    pcb_t *pcb = proc_get(p1);
    assert(pcb != NULL);
    assert(pcb->mlfq_level == 0);

    sched_demote_after_quantum(&sched, p1);
    assert(pcb->mlfq_level == 1);
    sched_demote_after_quantum(&sched, p1);
    assert(pcb->mlfq_level == 2);
    sched_demote_after_quantum(&sched, p1);
    assert(pcb->mlfq_level == 3);
    /* Should not exceed max level */
    sched_demote_after_quantum(&sched, p1);
    assert(pcb->mlfq_level == 3);
    proc_table_shutdown();
}

static void test_mlfq_boost(void) {
    proc_table_init();
    int p1 = proc_create(0, 20, 0, 0);
    int p2 = proc_create(0, 20, 0, 0);
    assert(proc_set_state(p1, PROC_READY, 0));
    assert(proc_set_state(p2, PROC_READY, 0));

    scheduler_t sched;
    sched_init(&sched, SCHED_MLFQ, 2);

    /* Demote both */
    pcb_t *pcb1 = proc_get(p1);
    pcb_t *pcb2 = proc_get(p2);
    pcb1->mlfq_level = 3;
    pcb2->mlfq_level = 2;

    /* Boost should reset everyone to level 0 */
    sched_priority_boost(&sched);
    assert(pcb1->mlfq_level == 0);
    assert(pcb2->mlfq_level == 0);
    proc_table_shutdown();
}

static void test_mlfq_higher_queue_priority(void) {
    proc_table_init();
    int p1 = proc_create(0, 20, 0, 0);
    int p2 = proc_create(0, 20, 0, 0);
    assert(proc_set_state(p1, PROC_READY, 0));
    assert(proc_set_state(p2, PROC_READY, 0));

    /* Put p1 in queue 0, p2 in queue 2 */
    proc_get(p1)->mlfq_level = 0;
    proc_get(p2)->mlfq_level = 2;

    scheduler_t sched;
    sched_init(&sched, SCHED_MLFQ, 4);
    assert(sched_enqueue(&sched, p2)); /* enqueue p2 first */
    assert(sched_enqueue(&sched, p1)); /* enqueue p1 second */

    /* p1 should be picked first because it's in a higher queue (lower index) */
    assert(sched_pick_next(&sched) == p1);
    proc_table_shutdown();
}

/* ---- Utility Tests ---- */

static void test_parse_algo(void) {
    sched_algo_t algo;
    assert(sched_parse_algo("fcfs", &algo) && algo == SCHED_FCFS);
    assert(sched_parse_algo("rr", &algo) && algo == SCHED_RR);
    assert(sched_parse_algo("prio", &algo) && algo == SCHED_PRIO);
    assert(sched_parse_algo("mlfq", &algo) && algo == SCHED_MLFQ);
    assert(!sched_parse_algo("unknown", &algo));
    assert(!sched_parse_algo(NULL, &algo));
}

static void test_algo_name(void) {
    assert(strcmp(sched_algo_name(SCHED_FCFS), "fcfs") == 0);
    assert(strcmp(sched_algo_name(SCHED_RR), "rr") == 0);
    assert(strcmp(sched_algo_name(SCHED_PRIO), "prio") == 0);
    assert(strcmp(sched_algo_name(SCHED_MLFQ), "mlfq") == 0);
}

static void test_duplicate_enqueue_rejected(void) {
    proc_table_init();
    int p1 = proc_create(0, 20, 0, 0);
    assert(proc_set_state(p1, PROC_READY, 0));

    scheduler_t sched;
    sched_init(&sched, SCHED_RR, 4);
    assert(sched_enqueue(&sched, p1));
    /* Second enqueue of same pid should fail */
    assert(!sched_enqueue(&sched, p1));
    proc_table_shutdown();
}

static void test_empty_queue_returns_neg(void) {
    scheduler_t sched;
    sched_init(&sched, SCHED_RR, 4);
    assert(sched_pick_next(&sched) == -1);

    sched_init(&sched, SCHED_PRIO, 4);
    assert(sched_pick_next(&sched) == -1);

    sched_init(&sched, SCHED_MLFQ, 4);
    assert(sched_pick_next(&sched) == -1);

    sched_init(&sched, SCHED_FCFS, 4);
    assert(sched_pick_next(&sched) == -1);
}

int main(void) {
    test_prio_basic();
    test_prio_randomized();
    test_rr_quantum();
    test_rr_fifo_order();
    test_fcfs_ordering();
    test_fcfs_no_preemption();
    test_mlfq_demotion();
    test_mlfq_boost();
    test_mlfq_higher_queue_priority();
    test_parse_algo();
    test_algo_name();
    test_duplicate_enqueue_rejected();
    test_empty_queue_returns_neg();
    printf("test_scheduler: all tests passed\n");
    return 0;
}
