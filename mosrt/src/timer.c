#include "timer.h"

#include <signal.h>
#include <string.h>
#include <sys/time.h>

static volatile sig_atomic_t g_need_resched;
static volatile sig_atomic_t g_signal_ticks;

static void on_sigalrm(int signo) {
    (void)signo;
    g_need_resched = 1;
    ++g_signal_ticks;
}

void timer_init(void) {
    g_need_resched = 0;
    g_signal_ticks = 0;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigalrm;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    (void)sigaction(SIGALRM, &sa, NULL);
}

bool timer_start(unsigned tick_ms) {
    if (tick_ms == 0U) {
        tick_ms = 1U;
    }
    timer_init();
    struct itimerval it;
    memset(&it, 0, sizeof(it));
    it.it_interval.tv_sec = tick_ms / 1000U;
    it.it_interval.tv_usec = (suseconds_t)((tick_ms % 1000U) * 1000U);
    it.it_value = it.it_interval;
    return setitimer(ITIMER_REAL, &it, NULL) == 0;
}

void timer_stop(void) {
    struct itimerval it;
    memset(&it, 0, sizeof(it));
    (void)setitimer(ITIMER_REAL, &it, NULL);
}

bool timer_need_resched(void) {
    return g_need_resched != 0;
}

void timer_clear_resched(void) {
    g_need_resched = 0;
}

uint64_t timer_signal_ticks(void) {
    return (uint64_t)g_signal_ticks;
}
