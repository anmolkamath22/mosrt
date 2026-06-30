#ifndef MOSRT_TIMER_H
#define MOSRT_TIMER_H

#include <stdbool.h>
#include <stdint.h>

/** Initialize SIGALRM-based timer state without starting it. */
void timer_init(void);
/** Start periodic SIGALRM ticks with the requested interval in milliseconds. */
bool timer_start(unsigned tick_ms);
/** Stop the interval timer. */
void timer_stop(void);
/** Return whether SIGALRM requested a safe reschedule point. */
bool timer_need_resched(void);
/** Clear the safe reschedule flag. */
void timer_clear_resched(void);
/** Return the number of SIGALRM ticks observed. */
uint64_t timer_signal_ticks(void);

#endif
