#ifndef _TESTSUITE_KLATENCY_H
#define _TESTSUITE_KLATENCY_H

#include <rtai_config.h>
#include <nucleus/types.h>

typedef struct rtai_latency_stat {

    int minjitter;
    int maxjitter;
    int avgjitter;
    int overrun;

} rtai_latency_stat_t;

#endif /* _TESTSUITE_KLATENCY_H */
