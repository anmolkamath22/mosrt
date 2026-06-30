#include <assert.h>

#include "proc.h"
#include "sched.h"

static unsigned lcg_next(unsigned *state) {
    *state = (*state * 1103515245U) + 12345U;
    return *state;
}

int main(void) {
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
    assert(sched_pick_next(&sched) == p2);

    sched_init(&sched, SCHED_RR, 2);
    assert(sched_enqueue(&sched, p1));
    assert(sched_enqueue(&sched, p2));
    assert(sched_pick_next(&sched) == p1);
    sched_on_tick(&sched, p1);
    assert(!sched_should_preempt(&sched, p1));
    sched_on_tick(&sched, p1);
    assert(sched_should_preempt(&sched, p1));

    proc_table_init();
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
    return 0;
}
