#ifndef RTAI_TRACE_H
#define RTAI_TRACE_H

#ifdef __KERNEL__

#include <rtai_config.h>

#ifdef CONFIG_RTAI_OPT_TRACES

#ifdef _cplusplus
extern "C" {
#endif

    typedef void
    rtai_trace_callback_t(const char *f, int l, const char *fn,
                       const char *fmt, ...)
#if defined(__GNUC__) && _GNUC__ > 2 || __GNUC__ == 2 &&  __GNUC_MINOR__ >= 96
        __attribute__((format(printf, 4, 5)))
#endif
        ;
    extern rtai_trace_callback_t *rtai_trace_callback;

#ifdef _cplusplus
}
#endif

#define RTAI_TRACE(fmt, args...)                                             \
    do {                                                                  \
        if (rtai_trace_callback)                                             \
            rtai_trace_callback(__FILE__, __LINE__, __func__, fmt , ##args); \
    } while(0)

#else /* ! CONFIG_RTAI_OPT_TRACES */

#define RTAI_TRACE(fmt, args...)

#endif /* CONFIG_RTAI_OPT_TRACES */

#endif /* __KERNEL__ */

#endif /*RTAI_TRACE_H*/
