#ifndef _TESTSUITE_KLATENCY_H
#define _TESTSUITE_KLATENCY_H

#include <rtai_config.h>
#include <nucleus/types.h>

typedef struct rtai_latency_stat {

    int minjitter;
    int maxjitter;
    int avgjitter;
    int overrun;

#ifdef CONFIG_RTAI_OPT_TIMESTAMPS
    int has_timestamps;
    int tick_propagation;
    int timer_prologue;
    int timer_exec;
    int timer_overall;
    int timer_epilogue;
    int timer_drift;
    int timer_anticipation;
    int resume_time;
    int switch_time;
    int periodic_wakeup;
    int periodic_epilogue;
    int tick_overall;
#endif /* CONFIG_RTAI_OPT_TIMESTAMPS */

} rtai_latency_stat_t;

#endif /* _TESTSUITE_KLATENCY_H */
